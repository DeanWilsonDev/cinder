#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cinder {

// Decoded audio, always resampled to the backend's fixed output rate
// (44100 Hz) and stored as interleaved 32-bit float PCM. Internal type —
// never exposed through a public header.
struct DecodedAudio {
  std::vector<float> interleavedSamples; // frameCount * channelCount
  std::uint32_t channelCount = 0;
  std::uint32_t sampleRate = 0; // always 44100 after decode
  std::uint64_t frameCount = 0;
};

class AudioDecoder {
public:
  // Dispatches on file extension (.wav / .mp3, case-insensitive). Returns an
  // empty DecodedAudio (frameCount == 0) on any failure.
  static DecodedAudio DecodeFile(const std::string& filePath);

  static DecodedAudio DecodeWav(const std::string& filePath);
  static DecodedAudio DecodeMp3(const std::string& filePath);
};

} // namespace Cinder
