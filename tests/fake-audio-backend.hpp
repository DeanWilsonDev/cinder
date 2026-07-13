#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "cinder/i-audio-backend.hpp"

namespace CinderTest {

struct PlayStemCall {
  std::string identifier;
  bool shouldLoop;
};

struct SetVolumeCall {
  std::string identifier;
  float volume;
};

struct ApplyEffectCall {
  std::string identifier;
  Cinder::AudioEffect effect;
};

// Test double for IAudioBackend. Records every call instead of touching
// real audio hardware, so tests can inspect what the mixer asked the
// backend to do without ever playing audio. `currentEffectByStem` mirrors
// the "one active effect per stem, last write wins" behaviour the real
// AudioBackend implements, so tests can verify that semantic through the
// interface without constructing the real (hardware-touching) backend.
class FakeAudioBackend : public Cinder::IAudioBackend {
public:
  void LoadAudioFile(const std::string& stemIdentifier, const std::string& filePath) override
  {
    loadedFiles.push_back({stemIdentifier, filePath});
  }

  void PlayStem(const std::string& stemIdentifier, bool shouldLoop) override
  {
    playCalls.push_back({stemIdentifier, shouldLoop});
  }

  void PauseStem(const std::string& stemIdentifier) override { pausedStems.push_back(stemIdentifier); }

  void StopStem(const std::string& stemIdentifier) override { stoppedStems.push_back(stemIdentifier); }

  void SetStemVolume(const std::string& stemIdentifier, float normalizedVolume) override
  {
    volumeCalls.push_back({stemIdentifier, normalizedVolume});
  }

  void ApplyEffect(const std::string& stemIdentifier, const Cinder::AudioEffect& effect) override
  {
    effectCalls.push_back({stemIdentifier, effect});
    currentEffectByStem[stemIdentifier] = effect;
  }

  void Update(float deltaTimeSeconds) override { (void)deltaTimeSeconds; }

  std::vector<std::pair<std::string, std::string>> loadedFiles;
  std::vector<PlayStemCall> playCalls;
  std::vector<std::string> pausedStems;
  std::vector<std::string> stoppedStems;
  std::vector<SetVolumeCall> volumeCalls;
  std::vector<ApplyEffectCall> effectCalls;
  std::unordered_map<std::string, Cinder::AudioEffect> currentEffectByStem;
};

} // namespace CinderTest
