#pragma once

#include <string>

#include "cinder/audio-effect.hpp"

namespace Cinder {

class ICinderMixer {
public:
  virtual ~ICinderMixer() = default;

  // Macro layer — state transitions
  virtual void TransitionToState(
    const std::string& stateName,
    float transitionDurationSeconds) = 0;

  // Micro layer — direct stem control
  virtual void SetStemVolume(
    const std::string& stemIdentifier,
    float normalizedVolume) = 0;

  virtual void FadeStem(
    const std::string& stemIdentifier,
    float targetNormalizedVolume,
    float durationSeconds) = 0;

  virtual void ApplyEffectToStem(
    const std::string& stemIdentifier,
    const AudioEffect& effect) = 0;

  // Sound effects
  virtual void PlaySoundEffect(const std::string& effectIdentifier) = 0;

  // Per-frame update — call once per game loop tick
  virtual void Update(float deltaTimeSeconds) = 0;
};

} // namespace Cinder
