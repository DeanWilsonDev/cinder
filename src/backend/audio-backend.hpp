#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cinder/i-audio-backend.hpp"

#include "audio-decoder.hpp"

namespace Cinder {

// Per-stem DSP filter/delay-line state for whichever single effect is
// currently active on that stem (the prototype supports one active effect
// per stem -- applying a new one replaces the last).
struct EffectState {
  bool active = false;
  AudioEffectType type = AudioEffectType::LowPassFilter;
  AudioEffectParameters parameters;

  // Single-pole IIR filter state (low-pass / high-pass), one per channel.
  float filterState[2] = {0.0f, 0.0f};
  float filterPrevInput[2] = {0.0f, 0.0f};

  // Schroeder reverb: 4 comb filters + 2 series allpass filters, per
  // channel. Delay line lengths are fixed; feedback is derived from
  // reverbDecaySeconds each time the effect is (re)applied.
  static constexpr int kCombCount = 4;
  static constexpr int kAllpassCount = 2;
  std::vector<float> combBuffer[2][kCombCount];
  int combWritePos[2][kCombCount] = {};
  float combFeedback[kCombCount] = {};
  std::vector<float> allpassBuffer[2][kAllpassCount];
  int allpassWritePos[2][kAllpassCount] = {};
};

// One loaded, currently-registered audio stream (stem or SFX) and its live
// playback state. Owned entirely by AudioBackend; never exposed outside it.
struct StemPlaybackState {
  DecodedAudio audio;
  std::uint64_t playbackFrame = 0;
  bool isLooping = false;
  bool isPlaying = false;
  bool isComplete = false;
  float currentVolume = 0.0f;
  EffectState effect;
};

// Concrete IAudioBackend. Platform-specific audio output code (Core Audio
// on macOS, ALSA on Linux) lives entirely in audio-backend.cpp behind a
// forward-declared PlatformState -- this header never names a platform API.
class AudioBackend : public IAudioBackend {
public:
  AudioBackend();
  ~AudioBackend() override;

  void LoadAudioFile(const std::string& stemIdentifier, const std::string& filePath) override;
  void PlayStem(const std::string& stemIdentifier, bool shouldLoop) override;
  void PauseStem(const std::string& stemIdentifier) override;
  void StopStem(const std::string& stemIdentifier) override;
  void SetStemVolume(const std::string& stemIdentifier, float normalizedVolume) override;
  void ApplyEffect(const std::string& stemIdentifier, const AudioEffect& effect) override;
  void Update(float deltaTimeSeconds) override;

private:
  struct PlatformState;
  std::unique_ptr<PlatformState> platform_;

  std::mutex stateMutex_;
  std::unordered_map<std::string, StemPlaybackState> stems_;

  // Invoked from the realtime audio thread (platform glue calls this once
  // per hardware buffer). Mixes all active stems, applies each stem's
  // active effect, clamps, and writes interleaved stereo float samples.
  void RenderAudio(float* outputBuffer, std::uint32_t frameCount);
};

} // namespace Cinder
