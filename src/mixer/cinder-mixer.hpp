#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cinder/audio-effect.hpp"
#include "cinder/audio-state.hpp"
#include "cinder/i-audio-backend.hpp"
#include "cinder/i-cinder-mixer.hpp"
#include "cinder/stem.hpp"

namespace Cinder {

// A stem identifier paired with the file path it should be loaded from.
struct StemFileEntry {
  std::string identifier;
  std::string filePath;
};

// Concrete ICinderMixer. Owns all Stem instances and drives per-frame fade
// updates. Game code interacts with it only through ICinderMixer — the
// FindStem() accessor below exists purely so the test suite (which links
// against this concrete type directly) can inspect fade state.
class CinderMixer : public ICinderMixer {
public:
  CinderMixer(
      std::unique_ptr<IAudioBackend> backend, std::vector<AudioState> audioStates,
      const std::vector<StemFileEntry>& stemsToLoad);

  void TransitionToState(const std::string& stateName, float transitionDurationSeconds) override;
  void SetStemVolume(const std::string& stemIdentifier, float normalizedVolume) override;
  void FadeStem(
      const std::string& stemIdentifier, float targetNormalizedVolume,
      float durationSeconds) override;
  void ApplyEffectToStem(const std::string& stemIdentifier, const AudioEffect& effect) override;
  void PlaySoundEffect(const std::string& effectIdentifier) override;
  void Update(float deltaTimeSeconds) override;

  const Stem* FindStem(const std::string& stemIdentifier) const;

private:
  std::unique_ptr<IAudioBackend> backend;
  std::vector<AudioState> audioStates;
  std::unordered_map<std::string, Stem> stems;
  std::unordered_map<std::string, float> fadeStartVolumes;

  const AudioState* FindState(const std::string& stateName) const;
};

} // namespace Cinder
