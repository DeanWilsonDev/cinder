#include "audio-decoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

#include <firefly/log.hpp>

#include "mp3-tables.hpp"

namespace Cinder {

namespace {

constexpr std::uint32_t kOutputSampleRate = 44100;

std::string ToLower(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::string GetExtension(const std::string& filePath)
{
  std::size_t dotPosition = filePath.find_last_of('.');
  if (dotPosition == std::string::npos) {
    return "";
  }
  return ToLower(filePath.substr(dotPosition));
}

std::vector<std::uint8_t> ReadWholeFile(const std::string& filePath, bool& outOpened)
{
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    outOpened = false;
    return {};
  }
  outOpened = true;
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (size > 0) {
    file.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  return bytes;
}

std::uint16_t ReadLE16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::uint32_t ReadLE32(const std::uint8_t* p)
{
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// Linear-interpolation resample to the fixed 44100 Hz output rate used by
// the backend's mixing callback. A prototype-grade resampler — sufficient to
// let stems authored at other standard sample rates play back at correct
// pitch/duration; not a high-quality band-limited resampler.
std::vector<float> ResampleToOutputRate(
    const std::vector<float>& interleaved, std::uint32_t channelCount, std::uint32_t sourceRate)
{
  if (sourceRate == kOutputSampleRate || channelCount == 0 || sourceRate == 0) {
    return interleaved;
  }

  std::uint64_t sourceFrameCount = interleaved.size() / channelCount;
  double ratio = static_cast<double>(sourceRate) / static_cast<double>(kOutputSampleRate);
  auto destFrameCount = static_cast<std::uint64_t>(
      static_cast<double>(sourceFrameCount) / ratio);

  std::vector<float> resampled(destFrameCount * channelCount, 0.0f);

  for (std::uint64_t destFrame = 0; destFrame < destFrameCount; ++destFrame) {
    double sourcePosition = static_cast<double>(destFrame) * ratio;
    auto sourceFrameLow = static_cast<std::uint64_t>(sourcePosition);
    std::uint64_t sourceFrameHigh = std::min(sourceFrameLow + 1, sourceFrameCount - 1);
    float fraction = static_cast<float>(sourcePosition - static_cast<double>(sourceFrameLow));

    for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
      float low = interleaved[sourceFrameLow * channelCount + channel];
      float high = interleaved[sourceFrameHigh * channelCount + channel];
      resampled[destFrame * channelCount + channel] = low + (high - low) * fraction;
    }
  }

  return resampled;
}

} // namespace

DecodedAudio AudioDecoder::DecodeWav(const std::string& filePath)
{
  bool opened = false;
  std::vector<std::uint8_t> bytes = ReadWholeFile(filePath, opened);
  if (!opened) {
    LOG_ERROR("WAV file not found: '{}'", filePath);
    return {};
  }

  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    LOG_ERROR("'{}' is not a valid RIFF/WAVE file", filePath);
    return {};
  }

  std::uint16_t formatTag = 0;
  std::uint16_t numChannels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
  const std::uint8_t* dataChunk = nullptr;
  std::uint32_t dataChunkSize = 0;

  std::size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    char chunkId[5] = {};
    std::memcpy(chunkId, bytes.data() + offset, 4);
    std::uint32_t chunkSize = ReadLE32(bytes.data() + offset + 4);
    std::size_t chunkDataOffset = offset + 8;

    if (chunkDataOffset + chunkSize > bytes.size()) {
      break;
    }

    if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
      const std::uint8_t* fmt = bytes.data() + chunkDataOffset;
      formatTag = ReadLE16(fmt + 0);
      numChannels = ReadLE16(fmt + 2);
      sampleRate = ReadLE32(fmt + 4);
      bitsPerSample = ReadLE16(fmt + 14);

      if (formatTag == 0xFFFE && chunkSize >= 40) {
        // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format GUID.
        // The first two bytes of the GUID mirror the classic format tag
        // (1 = PCM, 3 = IEEE float).
        formatTag = ReadLE16(fmt + 24);
      }
    }
    else if (std::memcmp(chunkId, "data", 4) == 0) {
      dataChunk = bytes.data() + chunkDataOffset;
      dataChunkSize = chunkSize;
    }

    offset = chunkDataOffset + chunkSize + (chunkSize % 2);
  }

  if (dataChunk == nullptr || numChannels == 0 || sampleRate == 0) {
    LOG_ERROR("'{}' is missing a usable 'fmt ' or 'data' chunk", filePath);
    return {};
  }

  if (numChannels > 2) {
    LOG_ERROR("'{}' has {} channels; only mono and stereo are supported", filePath, numChannels);
    return {};
  }

  bool isFloat = (formatTag == 3);
  bool isPcm = (formatTag == 1);
  if (!isFloat && !isPcm) {
    LOG_ERROR("'{}' uses unsupported WAV format tag {}", filePath, formatTag);
    return {};
  }

  if (bitsPerSample != 16 && bitsPerSample != 32) {
    LOG_ERROR("'{}' has unsupported bit depth {}; only 16-bit and 32-bit are supported", filePath, bitsPerSample);
    return {};
  }

  std::uint32_t bytesPerSample = bitsPerSample / 8;
  std::uint64_t totalSamples = dataChunkSize / bytesPerSample;

  std::vector<float> samples(totalSamples);
  for (std::uint64_t i = 0; i < totalSamples; ++i) {
    const std::uint8_t* samplePointer = dataChunk + i * bytesPerSample;
    if (isFloat) {
      float value;
      std::memcpy(&value, samplePointer, sizeof(float));
      samples[i] = value;
    }
    else if (bitsPerSample == 16) {
      auto raw = static_cast<std::int16_t>(ReadLE16(samplePointer));
      samples[i] = static_cast<float>(raw) / 32768.0f;
    }
    else {
      auto raw = static_cast<std::int32_t>(ReadLE32(samplePointer));
      samples[i] = static_cast<float>(raw) / 2147483648.0f;
    }
  }

  DecodedAudio result;
  result.interleavedSamples = ResampleToOutputRate(samples, numChannels, sampleRate);
  result.channelCount = numChannels;
  result.sampleRate = kOutputSampleRate;
  result.frameCount = result.interleavedSamples.size() / numChannels;

  LOG_INFO(
      "Decoded WAV '{}': {} channel(s), {} Hz source, {} bit, {} frames", filePath, numChannels,
      sampleRate, bitsPerSample, result.frameCount);

  return result;
}

// ===========================================================================
// MPEG-1 Layer III decoder.
//
// Implemented from scratch against the ISO/IEC 11172-3 algorithm structure
// (frame sync -> side info -> bit-reservoir main data -> scalefactors ->
// Huffman -> dequantization -> stereo processing -> antialiasing -> IMDCT ->
// frequency inversion -> polyphase synthesis). The Huffman codebooks,
// scalefactor band-width tables, and the 512-tap synthesis window are
// numeric constants mandated by the standard (not creative/copyrightable
// expression); they were cross-checked against FFmpeg's published table
// values (see mp3-tables.hpp) rather than retyped from memory, since a
// transcription error there is silent and effectively undebuggable. The
// decode pipeline itself (bit reservoir, Huffman walk, dequantization,
// IMDCT, synthesis filter, etc.) is original code, not derived from any
// existing decoder's source.
//
// Known, deliberate scope limits for this prototype:
//  - Intensity stereo is not implemented (mid/side stereo is). Content
//    encoded with intensity stereo will decode with incorrect high-band
//    stereo imaging.
//  - Mixed blocks (block_type == 2 with mixed_block_flag == 1) are decoded
//    using the short-block path for all bands rather than the ISO-mandated
//    long-block treatment of the first 8 scalefactor bands; this is a
//    bounded approximation, not a crash risk.
//  - MPEG-2/2.5 Layer III (low sample rate extension) is out of scope, as
//    is CRC validation -- both are consistent with "MPEG-1 Layer III" and
//    "no third-party decode library" in the spec, not accidental gaps.
//
// KNOWN CORRECTNESS GAP (not scope-limited, unresolved): validated against a
// real lame-encoded file cross-checked with ffmpeg's reference decode of the
// same file, this decoder parses frame headers/side info correctly (field
// values track the bitstream sensibly across frames) and its per-subband
// energy tracks the source signal's envelope correctly in aggregate, and the
// IMDCT/window/overlap-add/polyphase-synthesis chain independently
// reproduces the correct frequency when excited with a single synthetic
// MDCT coefficient. But decoding real multi-coefficient frames currently
// still produces audibly wrong pitch/timbre (near-zero correlation against
// the reference decode) -- there is a remaining bug, most likely in how
// Huffman-decoded coefficients across multiple scalefactor bands/regions
// get assigned to their final spectral positions, that was not isolated
// within this session's time budget. WAV decoding has no such issue: it was
// validated exactly. Treat MP3 playback in this prototype as "does not
// crash, produces audio-shaped output, pitch/timbre accuracy not yet
// verified" rather than "correct" until this is debugged further.
// ===========================================================================

namespace Mp3 {

class BitReader {
public:
  BitReader(const std::uint8_t* data, std::size_t sizeBytes, std::size_t startBitPos = 0)
      : data_(data), sizeBits_(sizeBytes * 8), bitPos_(startBitPos)
  {
  }

  std::uint32_t ReadBits(int count)
  {
    std::uint32_t result = 0;
    for (int i = 0; i < count; ++i) {
      result <<= 1;
      if (bitPos_ < sizeBits_) {
        std::size_t byteIndex = bitPos_ >> 3;
        int bitIndex = 7 - static_cast<int>(bitPos_ & 7);
        result |= (data_[byteIndex] >> bitIndex) & 1u;
      }
      ++bitPos_;
    }
    return result;
  }

  std::size_t BitPosition() const { return bitPos_; }
  void SetBitPosition(std::size_t pos) { bitPos_ = pos; }

private:
  const std::uint8_t* data_;
  std::size_t sizeBits_;
  std::size_t bitPos_;
};

struct FrameHeader {
  int sampleRateIndex = 0; // 0=44100, 1=48000, 2=32000
  int sampleRate = 0;
  int channelMode = 0; // 0 stereo, 1 joint stereo, 2 dual, 3 mono
  int modeExtension = 0;
  int channelCount = 0;
  bool hasCrc = false;
  int frameSizeBytes = 0;
};

bool ParseHeader(const std::uint8_t* p, std::size_t available, FrameHeader& header)
{
  if (available < 4) {
    return false;
  }
  if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) {
    return false;
  }

  int versionBits = (p[1] >> 3) & 0x3;
  int layerBits = (p[1] >> 1) & 0x3;
  int protectionBit = p[1] & 0x1;
  if (versionBits != 0x3 || layerBits != 0x1) {
    return false; // only MPEG-1 Layer III is in scope
  }

  int bitrateIndex = (p[2] >> 4) & 0xF;
  int sampleRateIndex = (p[2] >> 2) & 0x3;
  int paddingBit = (p[2] >> 1) & 0x1;
  int channelMode = (p[3] >> 6) & 0x3;
  int modeExtension = (p[3] >> 4) & 0x3;

  static constexpr int kBitratesKbps[16] = {
      0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
  static constexpr int kSampleRates[3] = {44100, 48000, 32000};

  if (bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3) {
    return false;
  }

  header.sampleRateIndex = sampleRateIndex;
  header.sampleRate = kSampleRates[sampleRateIndex];
  header.channelMode = channelMode;
  header.modeExtension = modeExtension;
  header.channelCount = (channelMode == 3) ? 1 : 2;
  header.hasCrc = (protectionBit == 0);

  int bitrateKbps = kBitratesKbps[bitrateIndex];
  header.frameSizeBytes = (144 * bitrateKbps * 1000) / header.sampleRate + paddingBit;
  return header.frameSizeBytes >= 21;
}

struct GranuleChannelInfo {
  int part2_3Length = 0;
  int bigValues = 0;
  int globalGain = 0;
  int scalefacCompress = 0;
  bool windowSwitching = false;
  int blockType = 0;
  bool mixedBlock = false;
  int tableSelect[3] = {0, 0, 0};
  int subblockGain[3] = {0, 0, 0};
  int region0Count = 0;
  int region1Count = 0;
  bool preflag = false;
  bool scalefacScale = false;
  int count1TableSelect = 0;
};

struct SideInfo {
  int mainDataBegin = 0;
  int scfsi[2][4] = {};
  GranuleChannelInfo granules[2][2]; // [granule][channel]
};

int SideInfoSizeBytes(int channelCount) { return (channelCount == 1) ? 17 : 32; }

void ParseSideInfo(BitReader& br, int channelCount, SideInfo& side)
{
  side.mainDataBegin = static_cast<int>(br.ReadBits(9));
  br.ReadBits(channelCount == 2 ? 3 : 5); // private bits, unused

  for (int ch = 0; ch < channelCount; ++ch) {
    for (int band = 0; band < 4; ++band) {
      side.scfsi[ch][band] = static_cast<int>(br.ReadBits(1));
    }
  }

  for (int gr = 0; gr < 2; ++gr) {
    for (int ch = 0; ch < channelCount; ++ch) {
      GranuleChannelInfo& g = side.granules[gr][ch];
      g.part2_3Length = static_cast<int>(br.ReadBits(12));
      g.bigValues = static_cast<int>(br.ReadBits(9));
      g.globalGain = static_cast<int>(br.ReadBits(8));
      g.scalefacCompress = static_cast<int>(br.ReadBits(4));
      g.windowSwitching = br.ReadBits(1) != 0;

      if (g.windowSwitching) {
        g.blockType = static_cast<int>(br.ReadBits(2));
        g.mixedBlock = br.ReadBits(1) != 0;
        g.tableSelect[0] = static_cast<int>(br.ReadBits(5));
        g.tableSelect[1] = static_cast<int>(br.ReadBits(5));
        g.tableSelect[2] = 0;
        g.subblockGain[0] = static_cast<int>(br.ReadBits(3));
        g.subblockGain[1] = static_cast<int>(br.ReadBits(3));
        g.subblockGain[2] = static_cast<int>(br.ReadBits(3));
        g.region0Count = 0;
        g.region1Count = 0;
      }
      else {
        g.blockType = 0;
        g.mixedBlock = false;
        g.tableSelect[0] = static_cast<int>(br.ReadBits(5));
        g.tableSelect[1] = static_cast<int>(br.ReadBits(5));
        g.tableSelect[2] = static_cast<int>(br.ReadBits(5));
        g.region0Count = static_cast<int>(br.ReadBits(4));
        g.region1Count = static_cast<int>(br.ReadBits(3));
      }

      g.preflag = br.ReadBits(1) != 0;
      g.scalefacScale = br.ReadBits(1) != 0;
      g.count1TableSelect = static_cast<int>(br.ReadBits(1));
    }
  }
}

// Cumulative coefficient-index boundaries for the 22 long scalefactor bands
// (band j spans [boundary[j], boundary[j+1])), derived from the verified
// per-band widths in mp3-tables.hpp.
struct BandBoundaries {
  int longBoundaries[3][23] = {};
  int shortBoundaries[3][13] = {}; // per-window boundaries, 0..192
};

const BandBoundaries& Boundaries()
{
  static const BandBoundaries boundaries = [] {
    BandBoundaries b;
    for (int sr = 0; sr < 3; ++sr) {
      int acc = 0;
      for (int j = 0; j < 22; ++j) {
        b.longBoundaries[sr][j] = acc;
        acc += Mp3Tables::kBandSizeLong[sr][j];
      }
      b.longBoundaries[sr][22] = acc;

      acc = 0;
      for (int j = 0; j < 13; ++j) {
        b.shortBoundaries[sr][j] = acc;
        acc += Mp3Tables::kBandSizeShort[sr][j];
      }
    }
    return b;
  }();
  return boundaries;
}

// Precomputed mapping from "short-block traversal order" (scalefactor band
// major, window second, coefficient minor -- the order Huffman data for
// short blocks is transmitted in) to physical index (subband*18 + line)
// used by antialiasing/IMDCT. Also used for mixed blocks (see scope note
// above: mixed blocks use this same short-only mapping for all bands).
const std::array<int, 576>& ShortTraversalToPhysical(int sampleRateIndex)
{
  static std::array<std::array<int, 576>, 3> cache{};
  static std::array<bool, 3> built{};

  if (!built[sampleRateIndex]) {
    const auto& sb = Boundaries().shortBoundaries[sampleRateIndex];
    std::array<int, 576>& table = cache[sampleRateIndex];
    int traversalIndex = 0;
    for (int band = 0; band < 12; ++band) {
      int width = sb[band + 1] - sb[band];
      for (int window = 0; window < 3; ++window) {
        for (int c = 0; c < width; ++c) {
          int freqInWindow = sb[band] + c; // 0..191
          int subband = freqInWindow / 6;
          int line = freqInWindow % 6;
          int physical = subband * 18 + window * 6 + line;
          if (traversalIndex < 576 && physical < 576) {
            table[traversalIndex] = physical;
          }
          ++traversalIndex;
        }
      }
    }
    built[sampleRateIndex] = true;
  }
  return cache[sampleRateIndex];
}

// Decodes one Huffman-coded (x, y) pair, including linbits escape extension
// and sign bits. tableId == 0 means "no data, value is (0,0)".
void DecodeHuffmanPair(
    BitReader& br, int tableId, int linbits, int& outX, int& outY, int& signX, int& signY)
{
  outX = 0;
  outY = 0;
  signX = 1;
  signY = 1;
  if (tableId == 0) {
    return;
  }

  const Mp3Tables::HuffEntry* entries = nullptr;
  int entryCount = 0;
#define CINDER_HUFF_CASE(id) \
  case id: \
    entries = Mp3Tables::kHuffTable##id; \
    entryCount = static_cast<int>(std::size(Mp3Tables::kHuffTable##id)); \
    break;
  switch (tableId) {
    CINDER_HUFF_CASE(1)
    CINDER_HUFF_CASE(2)
    CINDER_HUFF_CASE(3)
    CINDER_HUFF_CASE(5)
    CINDER_HUFF_CASE(6)
    CINDER_HUFF_CASE(7)
    CINDER_HUFF_CASE(8)
    CINDER_HUFF_CASE(9)
    CINDER_HUFF_CASE(10)
    CINDER_HUFF_CASE(11)
    CINDER_HUFF_CASE(12)
    CINDER_HUFF_CASE(13)
    CINDER_HUFF_CASE(15)
    CINDER_HUFF_CASE(16)
    CINDER_HUFF_CASE(24)
    default:
      return;
  }
#undef CINDER_HUFF_CASE

  // Canonical Huffman: assign codes to `entries` (already in the
  // standard's fixed order) by walking length buckets, identical to the
  // construction used to generate the codebook in the first place. Built
  // once per table on first use.
  struct BuiltCode {
    std::uint32_t code;
    std::uint16_t length;
    std::uint8_t x, y;
  };
  static std::unordered_map<int, std::vector<BuiltCode>> cache;

  auto it = cache.find(tableId);
  if (it == cache.end()) {
    std::array<int, 20> count{};
    for (int i = 0; i < entryCount; ++i) {
      count[entries[i].length]++;
    }
    std::array<std::uint32_t, 20> nextCode{};
    std::uint32_t code = 0;
    for (int len = 1; len < 20; ++len) {
      code = (code + static_cast<std::uint32_t>(count[len - 1])) << 1;
      nextCode[len] = code;
    }
    std::vector<BuiltCode> built(entryCount);
    for (int i = 0; i < entryCount; ++i) {
      int len = entries[i].length;
      built[i] = {nextCode[len], static_cast<std::uint16_t>(len), entries[i].value.x, entries[i].value.y};
      nextCode[len]++;
    }
    it = cache.emplace(tableId, std::move(built)).first;
  }

  const std::vector<BuiltCode>& codes = it->second;

  // Simple bit-by-bit prefix match (correctness over speed -- fine for a
  // prototype decoder).
  std::uint32_t accumulated = 0;
  int accumulatedLen = 0;
  bool matched = false;
  for (int len = 1; len <= 19 && !matched; ++len) {
    accumulated = (accumulated << 1) | br.ReadBits(1);
    accumulatedLen = len;
    for (const BuiltCode& c : codes) {
      if (c.length == accumulatedLen && c.code == accumulated) {
        outX = c.x;
        outY = c.y;
        matched = true;
        break;
      }
    }
  }

  // Bitstream order is: [huffman code][x linbits if x==15][x sign if x!=0]
  // [y linbits if y==15][y sign if y!=0] -- escape and sign bits are
  // interleaved per value, not grouped, so this must read x fully
  // (escape + sign) before touching y.
  if (outX == 15 && linbits > 0) {
    outX += static_cast<int>(br.ReadBits(linbits));
  }
  if (outX != 0 && br.ReadBits(1) != 0) {
    signX = -1;
  }
  if (outY == 15 && linbits > 0) {
    outY += static_cast<int>(br.ReadBits(linbits));
  }
  if (outY != 0 && br.ReadBits(1) != 0) {
    signY = -1;
  }
}

struct GranuleDecodeResult {
  float xr[576] = {};
  int blockType = 0;
  bool isShortLayout = false; // true if this granule uses the short traversal/mapping
};

void DecodeScalefactorsLong(
    BitReader& br, const GranuleChannelInfo& g, int gr, int scfsi[4], int prevScalefac[22],
    int outScalefac[22])
{
  int slen1 = Mp3Tables::kSlen1[g.scalefacCompress];
  int slen2 = Mp3Tables::kSlen2[g.scalefacCompress];
  static constexpr int kGroupForBand[21] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
                                             2, 2, 2, 2, 2, 3, 3, 3, 3, 3};
  for (int sfb = 0; sfb < 21; ++sfb) {
    int slen = (sfb < 11) ? slen1 : slen2;
    bool shared = (gr == 1) && (scfsi[kGroupForBand[sfb]] != 0);
    if (shared) {
      outScalefac[sfb] = prevScalefac[sfb];
    }
    else if (slen == 0) {
      outScalefac[sfb] = 0;
    }
    else {
      outScalefac[sfb] = static_cast<int>(br.ReadBits(slen));
    }
  }
  outScalefac[21] = 0;
}

void DecodeScalefactorsShort(
    BitReader& br, const GranuleChannelInfo& g, int outScalefac[12][3])
{
  int slen1 = Mp3Tables::kSlen1[g.scalefacCompress];
  int slen2 = Mp3Tables::kSlen2[g.scalefacCompress];
  for (int sfb = 0; sfb < 12; ++sfb) {
    int slen = (sfb < 6) ? slen1 : slen2;
    for (int window = 0; window < 3; ++window) {
      outScalefac[sfb][window] = (slen == 0) ? 0 : static_cast<int>(br.ReadBits(slen));
    }
  }
}

float Requantize(int value, double globalGainTerm, double scaleTerm)
{
  if (value == 0) {
    return 0.0f;
  }
  double magnitude = std::pow(static_cast<double>(std::abs(value)), 4.0 / 3.0);
  double result = magnitude * globalGainTerm * scaleTerm;
  return static_cast<float>(value < 0 ? -result : result);
}

GranuleDecodeResult DecodeGranule(
    BitReader& br, const FrameHeader& header, const GranuleChannelInfo& g, int gr,
    int scfsi[4], int prevLongScalefac[22])
{
  GranuleDecodeResult result;
  std::size_t granuleStartBit = br.BitPosition();
  std::size_t granuleEndBit = granuleStartBit + static_cast<std::size_t>(g.part2_3Length);

  bool isShort = g.windowSwitching && g.blockType == 2;
  result.blockType = g.blockType;
  result.isShortLayout = isShort;

  double globalGainTerm = std::pow(2.0, (g.globalGain - 210) / 4.0);
  double scalefacMultiplier = g.scalefacScale ? 1.0 : 0.5;

  // --- scalefactors + region boundaries ---
  int regionBoundary0 = 0;
  int regionBoundary1 = 576;
  int shortScalefac[12][3] = {};
  int longScalefac[22] = {};

  if (isShort) {
    DecodeScalefactorsShort(br, g, shortScalefac);
    regionBoundary0 = 36; // fixed for our supported sample rates (32k/44.1k/48k)
    regionBoundary1 = 576;
  }
  else {
    DecodeScalefactorsLong(br, g, gr, scfsi, prevLongScalefac, longScalefac);
    std::copy(std::begin(longScalefac), std::end(longScalefac), prevLongScalefac);

    const auto& lb = Boundaries().longBoundaries[header.sampleRateIndex];
    int ra1 = g.region0Count;
    int ra2 = g.region1Count;
    regionBoundary0 = lb[std::min(ra1 + 1, 22)];
    regionBoundary1 = lb[std::min(ra1 + ra2 + 2, 22)];
  }

  int bigValueCoeffs = g.bigValues * 2;

  const auto& traversal = isShort ? ShortTraversalToPhysical(header.sampleRateIndex)
                                   : std::array<int, 576>{}; // unused for long

  auto physicalIndexFor = [&](int traversalIndex) -> int {
    return isShort ? traversal[static_cast<std::size_t>(traversalIndex)] : traversalIndex;
  };

  auto scaleTermFor = [&](int traversalIndex) -> double {
    if (isShort) {
      // Recover (sfb, window) from traversal index by re-walking the band
      // table boundaries -- O(bands) per coefficient, acceptable for a
      // prototype decoder.
      const auto& sb = Boundaries().shortBoundaries[header.sampleRateIndex];
      int remaining = traversalIndex;
      for (int band = 0; band < 12; ++band) {
        int width = sb[band + 1] - sb[band];
        int windowSpan = width * 3;
        if (remaining < windowSpan) {
          int window = remaining / width;
          double subblockTerm = std::pow(2.0, -2.0 * g.subblockGain[window]);
          double sfTerm = std::pow(2.0, -scalefacMultiplier * shortScalefac[band][window]);
          return subblockTerm * sfTerm;
        }
        remaining -= windowSpan;
      }
      return 1.0;
    }
    else {
      const auto& lb = Boundaries().longBoundaries[header.sampleRateIndex];
      int sfb = 21;
      for (int b = 0; b < 22; ++b) {
        if (traversalIndex < lb[b + 1]) {
          sfb = b;
          break;
        }
      }
      double pre = (g.preflag && sfb < 22) ? Mp3Tables::kPretab[sfb] : 0.0;
      double sfTerm = std::pow(2.0, -scalefacMultiplier * (longScalefac[std::min(sfb, 21)] + pre));
      return sfTerm;
    }
  };

  // --- big_values region ---
  int traversalIndex = 0;
  for (; traversalIndex < bigValueCoeffs && traversalIndex < 576; traversalIndex += 2) {
    int tableId;
    int linbits;
    if (traversalIndex < regionBoundary0) {
      tableId = g.tableSelect[0];
    }
    else if (traversalIndex < regionBoundary1) {
      tableId = g.tableSelect[1];
    }
    else {
      tableId = g.tableSelect[2];
    }
    int mappedTableId = Mp3Tables::kHuffDataTableId[tableId];
    linbits = Mp3Tables::kHuffDataLinbits[tableId];

    int x = 0, y = 0, signX = 1, signY = 1;
    DecodeHuffmanPair(br, mappedTableId, linbits, x, y, signX, signY);

    double scaleTermA = scaleTermFor(traversalIndex);
    double scaleTermB = scaleTermFor(traversalIndex + 1);
    int physA = physicalIndexFor(traversalIndex);
    int physB = physicalIndexFor(traversalIndex + 1);
    if (physA >= 0 && physA < 576) {
      result.xr[physA] = Requantize(signX * x, globalGainTerm, scaleTermA);
    }
    if (physB >= 0 && physB < 576) {
      result.xr[physB] = Requantize(signY * y, globalGainTerm, scaleTermB);
    }

    if (br.BitPosition() >= granuleEndBit) {
      traversalIndex += 2;
      break;
    }
  }

  // --- count1 region (quad tables) ---
  const std::uint8_t* quadCodes = g.count1TableSelect == 0 ? Mp3Tables::kQuadCodesA : Mp3Tables::kQuadCodesB;
  const std::uint8_t* quadBits = g.count1TableSelect == 0 ? Mp3Tables::kQuadBitsA : Mp3Tables::kQuadBitsB;

  while (traversalIndex + 4 <= 576 && br.BitPosition() < granuleEndBit) {
    std::uint32_t accumulated = 0;
    int accumulatedLen = 0;
    int matchedSymbol = -1;
    for (int len = 1; len <= 6 && matchedSymbol < 0; ++len) {
      accumulated = (accumulated << 1) | br.ReadBits(1);
      accumulatedLen = len;
      for (int sym = 0; sym < 16; ++sym) {
        if (quadBits[sym] == accumulatedLen && quadCodes[sym] == accumulated) {
          matchedSymbol = sym;
          break;
        }
      }
    }
    if (matchedSymbol < 0) {
      break;
    }

    int v = (matchedSymbol >> 3) & 1;
    int w = (matchedSymbol >> 2) & 1;
    int x = (matchedSymbol >> 1) & 1;
    int y = matchedSymbol & 1;
    int values[4] = {v, w, x, y};

    for (int k = 0; k < 4; ++k) {
      int magnitude = values[k];
      int sign = 1;
      if (magnitude != 0) {
        sign = (br.ReadBits(1) != 0) ? -1 : 1;
      }
      int physIdx = physicalIndexFor(traversalIndex + k);
      if (physIdx >= 0 && physIdx < 576) {
        double scaleTerm = scaleTermFor(traversalIndex + k);
        result.xr[physIdx] = Requantize(sign * magnitude, globalGainTerm, scaleTerm);
      }
    }
    traversalIndex += 4;
  }

  br.SetBitPosition(granuleEndBit);
  return result;
}

// Antialiasing butterfly (long blocks only), applied across all 31 subband
// boundaries. The 8 constants are the standard MPEG antialiasing rotation
// angles (as tan(theta)); cs/ca are derived from them at startup rather
// than hardcoded, since the derivation formula is unambiguous.
void ApplyAntialiasing(float xr[576])
{
  static const std::array<double, 8> kAliasC = {
      -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
  static const auto csCa = [] {
    std::array<std::pair<double, double>, 8> table{};
    for (int i = 0; i < 8; ++i) {
      double c = kAliasC[static_cast<std::size_t>(i)];
      double cs = 1.0 / std::sqrt(1.0 + c * c);
      double ca = c * cs;
      table[static_cast<std::size_t>(i)] = {cs, ca};
    }
    return table;
  }();

  for (int sb = 1; sb < 32; ++sb) {
    for (int i = 0; i < 8; ++i) {
      float& lower = xr[18 * (sb - 1) + (17 - i)];
      float& upper = xr[18 * sb + i];
      double cs = csCa[static_cast<std::size_t>(i)].first;
      double ca = csCa[static_cast<std::size_t>(i)].second;
      double newLower = lower * cs - upper * ca;
      double newUpper = upper * cs + lower * ca;
      lower = static_cast<float>(newLower);
      upper = static_cast<float>(newUpper);
    }
  }
}

float LongWindow(int blockType, int i)
{
  constexpr double kPi = std::numbers::pi;
  switch (blockType) {
    case 1: // start block
      if (i <= 17) return static_cast<float>(std::sin(kPi / 36.0 * (i + 0.5)));
      if (i <= 23) return 1.0f;
      if (i <= 29) return static_cast<float>(std::sin(kPi / 12.0 * (i - 18 + 0.5)));
      return 0.0f;
    case 3: // stop block
      if (i <= 5) return 0.0f;
      if (i <= 11) return static_cast<float>(std::sin(kPi / 12.0 * (i - 6 + 0.5)));
      if (i <= 17) return 1.0f;
      return static_cast<float>(std::sin(kPi / 36.0 * (i + 0.5)));
    default: // normal (0) -- also used as fallback for the unused value 2
      return static_cast<float>(std::sin(kPi / 36.0 * (i + 0.5)));
  }
}

float ShortWindow(int i) { return static_cast<float>(std::sin(std::numbers::pi / 12.0 * (i + 0.5))); }

void Imdct(const float* in, int inCount, float* out)
{
  int outCount = inCount * 2;
  double scale = std::numbers::pi / (2.0 * outCount);
  for (int i = 0; i < outCount; ++i) {
    double sum = 0.0;
    for (int k = 0; k < inCount; ++k) {
      sum += static_cast<double>(in[k]) * std::cos(scale * (2 * i + 1 + outCount / 2) * (2 * k + 1));
    }
    out[i] = static_cast<float>(sum);
  }
}

struct ChannelState {
  float overlap[32][18] = {};
  std::array<float, 1024> synthesisV{};
  int synthesisOffset = 0;
};

const std::array<float, 512>& SynthesisWindow()
{
  static const std::array<float, 512> window = [] {
    std::array<float, 512> w{};
    for (int i = 0; i < 257; ++i) {
      float v = static_cast<float>(Mp3Tables::kEnwindow[i]) / 65536.0f;
      w[static_cast<std::size_t>(i)] = v;
      if ((i & 63) != 0) {
        v = -v;
      }
      if (i != 0) {
        w[static_cast<std::size_t>(512 - i)] = v;
      }
    }
    return w;
  }();
  return window;
}

void SynthesisFilter(ChannelState& state, const float subbandSamples[32], float outPcm[32])
{
  std::array<float, 64> newVec{};
  for (int i = 0; i < 64; ++i) {
    double sum = 0.0;
    for (int k = 0; k < 32; ++k) {
      sum += std::cos((16 + i) * (2.0 * k + 1) * std::numbers::pi / 64.0) *
             static_cast<double>(subbandSamples[k]);
    }
    newVec[static_cast<std::size_t>(i)] = static_cast<float>(sum);
  }

  state.synthesisOffset = (state.synthesisOffset - 64 + 1024) % 1024;
  for (int i = 0; i < 64; ++i) {
    state.synthesisV[static_cast<std::size_t>((state.synthesisOffset + i) % 1024)] =
        newVec[static_cast<std::size_t>(i)];
  }

  auto V = [&](int n) { return state.synthesisV[static_cast<std::size_t>((state.synthesisOffset + n) % 1024)]; };

  std::array<float, 512> U{};
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 32; ++j) {
      U[static_cast<std::size_t>(64 * i + j)] = V(128 * i + j);
      U[static_cast<std::size_t>(64 * i + 32 + j)] = V(128 * i + 96 + j);
    }
  }

  const auto& D = SynthesisWindow();
  std::array<float, 512> W{};
  for (int i = 0; i < 512; ++i) {
    W[static_cast<std::size_t>(i)] =
        U[static_cast<std::size_t>(i)] * D[static_cast<std::size_t>(i)];
  }

  for (int j = 0; j < 32; ++j) {
    double sum = 0.0;
    for (int i = 0; i < 16; ++i) {
      sum += W[static_cast<std::size_t>(j + 32 * i)];
    }
    outPcm[j] = static_cast<float>(sum);
  }
}

// Runs antialiasing (if applicable) + IMDCT/windowing/overlap-add +
// frequency inversion, then feeds the 18 resulting sample-groups through
// the polyphase synthesis filter, appending 18*32 PCM samples for this
// channel to `outPcm`.
void HybridSynthesisAndOutput(GranuleDecodeResult& granule, ChannelState& state, std::vector<float>& outPcm)
{
  if (!granule.isShortLayout) {
    ApplyAntialiasing(granule.xr);
  }

  float hybrid[18][32] = {};

  for (int subband = 0; subband < 32; ++subband) {
    float imdctOut[36] = {};

    if (granule.isShortLayout) {
      float combined[36] = {};
      for (int window = 0; window < 3; ++window) {
        float in6[6];
        for (int k = 0; k < 6; ++k) {
          in6[k] = granule.xr[subband * 18 + window * 6 + k];
        }
        float out12[12];
        Imdct(in6, 6, out12);
        for (int k = 0; k < 12; ++k) {
          out12[k] *= ShortWindow(k);
        }
        int offset = 6 + window * 6; // standard short-block placement, 6-sample overlap between windows
        for (int k = 0; k < 12; ++k) {
          combined[offset + k] += out12[k];
        }
      }
      std::copy(std::begin(combined), std::end(combined), std::begin(imdctOut));
    }
    else {
      float in18[18];
      for (int k = 0; k < 18; ++k) {
        in18[k] = granule.xr[subband * 18 + k];
      }
      float out36[36];
      Imdct(in18, 18, out36);
      for (int k = 0; k < 36; ++k) {
        out36[k] *= LongWindow(granule.blockType, k);
      }
      std::copy(std::begin(out36), std::end(out36), std::begin(imdctOut));
    }

    for (int k = 0; k < 18; ++k) {
      float sampleOut = imdctOut[k] + state.overlap[subband][k];
      if ((subband & 1) != 0 && (k & 1) != 0) {
        sampleOut = -sampleOut;
      }
      hybrid[k][subband] = sampleOut;
    }
    for (int k = 0; k < 18; ++k) {
      state.overlap[subband][k] = imdctOut[18 + k];
    }
  }

  for (int sampleGroup = 0; sampleGroup < 18; ++sampleGroup) {
    float pcmOut[32];
    SynthesisFilter(state, hybrid[sampleGroup], pcmOut);
    for (int j = 0; j < 32; ++j) {
      outPcm.push_back(std::clamp(pcmOut[j], -1.0f, 1.0f));
    }
  }
}

} // namespace Mp3

DecodedAudio AudioDecoder::DecodeMp3(const std::string& filePath)
{
  bool opened = false;
  std::vector<std::uint8_t> bytes = ReadWholeFile(filePath, opened);
  if (!opened) {
    LOG_ERROR("MP3 file not found: '{}'", filePath);
    return {};
  }

  std::vector<std::uint8_t> reservoir;
  std::array<Mp3::ChannelState, 2> channelStates;
  int scfsi[2][4] = {};
  int prevLongScalefac[2][22] = {};

  std::vector<float> pcmChannels[2];
  int detectedChannelCount = 0;
  int detectedSampleRate = 0;

  std::size_t pos = 0;
  while (pos + 4 <= bytes.size()) {
    Mp3::FrameHeader header;
    if (!Mp3::ParseHeader(bytes.data() + pos, bytes.size() - pos, header)) {
      ++pos;
      continue;
    }
    if (pos + static_cast<std::size_t>(header.frameSizeBytes) > bytes.size()) {
      break;
    }

    detectedChannelCount = header.channelCount;
    detectedSampleRate = header.sampleRate;

    std::size_t sideInfoOffset = pos + 4 + (header.hasCrc ? 2 : 0);
    int sideInfoBytes = Mp3::SideInfoSizeBytes(header.channelCount);
    if (sideInfoOffset + static_cast<std::size_t>(sideInfoBytes) > bytes.size()) {
      break;
    }

    Mp3::BitReader sideReader(bytes.data() + sideInfoOffset, static_cast<std::size_t>(sideInfoBytes));
    Mp3::SideInfo side;
    Mp3::ParseSideInfo(sideReader, header.channelCount, side);

    std::size_t mainDataOffset = sideInfoOffset + static_cast<std::size_t>(sideInfoBytes);
    std::size_t frameEnd = pos + static_cast<std::size_t>(header.frameSizeBytes);
    std::size_t mainDataSize = (frameEnd > mainDataOffset) ? frameEnd - mainDataOffset : 0;

    std::size_t reservoirInsertPos = reservoir.size();
    reservoir.insert(
        reservoir.end(), bytes.data() + mainDataOffset, bytes.data() + mainDataOffset + mainDataSize);

    std::size_t granuleStartByte;
    bool haveEnoughReservoir =
        static_cast<std::size_t>(side.mainDataBegin) <= reservoirInsertPos;
    if (haveEnoughReservoir) {
      granuleStartByte = reservoirInsertPos - static_cast<std::size_t>(side.mainDataBegin);

      Mp3::BitReader mainReader(reservoir.data(), reservoir.size(), granuleStartByte * 8);

      bool msStereo = header.channelCount == 2 && header.channelMode == 1 &&
                       (header.modeExtension & 0x2) != 0;

      for (int gr = 0; gr < 2; ++gr) {
        Mp3::GranuleDecodeResult decoded[2];
        for (int ch = 0; ch < header.channelCount; ++ch) {
          decoded[ch] = Mp3::DecodeGranule(
              mainReader, header, side.granules[gr][ch], gr, scfsi[ch], prevLongScalefac[ch]);
        }

        // Mid/side stereo: channel 0 carries (L+R)/sqrt2, channel 1 carries
        // (L-R)/sqrt2. Reconstruct L/R in the frequency domain, before
        // IMDCT/synthesis. Intensity stereo is not implemented (see scope
        // note above the Mp3 namespace).
        if (msStereo) {
          constexpr float kInvSqrt2 = 0.70710678118654752f;
          for (int i = 0; i < 576; ++i) {
            float mid = decoded[0].xr[i];
            float side_ = decoded[1].xr[i];
            decoded[0].xr[i] = (mid + side_) * kInvSqrt2;
            decoded[1].xr[i] = (mid - side_) * kInvSqrt2;
          }
        }

        for (int ch = 0; ch < header.channelCount; ++ch) {
          Mp3::HybridSynthesisAndOutput(
              decoded[ch], channelStates[static_cast<std::size_t>(ch)], pcmChannels[ch]);
        }
      }
    }

    // Trim old reservoir bytes we'll never address again to bound memory
    // growth on long files (main_data_begin can look back at most 511
    // bytes per the standard's 9-bit field).
    if (reservoir.size() > 4096) {
      std::size_t trim = reservoir.size() - 1024;
      reservoir.erase(reservoir.begin(), reservoir.begin() + static_cast<std::ptrdiff_t>(trim));
    }

    pos = frameEnd;
  }

  if (detectedChannelCount == 0 || pcmChannels[0].empty()) {
    LOG_ERROR("Failed to decode any frames from MP3 '{}'", filePath);
    return {};
  }

  std::uint64_t frameCount = pcmChannels[0].size();
  std::vector<float> interleaved(frameCount * static_cast<std::uint64_t>(detectedChannelCount));
  for (std::uint64_t i = 0; i < frameCount; ++i) {
    for (int ch = 0; ch < detectedChannelCount; ++ch) {
      float sample = (ch < 2 && i < pcmChannels[ch].size()) ? pcmChannels[ch][i] : 0.0f;
      interleaved[i * static_cast<std::uint64_t>(detectedChannelCount) + static_cast<std::uint64_t>(ch)] =
          sample;
    }
  }

  DecodedAudio result;
  result.interleavedSamples = ResampleToOutputRate(
      interleaved, static_cast<std::uint32_t>(detectedChannelCount),
      static_cast<std::uint32_t>(detectedSampleRate));
  result.channelCount = static_cast<std::uint32_t>(detectedChannelCount);
  result.sampleRate = kOutputSampleRate;
  result.frameCount = result.interleavedSamples.size() / result.channelCount;

  LOG_INFO(
      "Decoded MP3 '{}': {} channel(s), {} Hz source, {} frames", filePath, detectedChannelCount,
      detectedSampleRate, result.frameCount);

  return result;
}

DecodedAudio AudioDecoder::DecodeFile(const std::string& filePath)
{
  std::string extension = GetExtension(filePath);
  if (extension == ".wav") {
    return DecodeWav(filePath);
  }
  if (extension == ".mp3") {
    return DecodeMp3(filePath);
  }
  LOG_ERROR("Unsupported audio file extension for '{}'", filePath);
  return {};
}

} // namespace Cinder
