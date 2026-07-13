#pragma once

#include <string>

#include "cinder/audio-effect.hpp"

namespace Cinder {

class IAudioBackend {
public:
  virtual ~IAudioBackend() = default;

  virtual void LoadAudioFile(
    const std::string& stemIdentifier,
    const std::string& filePath) = 0;

  virtual void PlayStem(
    const std::string& stemIdentifier,
    bool shouldLoop) = 0;

  virtual void PauseStem(const std::string& stemIdentifier) = 0;

  virtual void StopStem(const std::string& stemIdentifier) = 0;

  virtual void SetStemVolume(
    const std::string& stemIdentifier,
    float normalizedVolume) = 0;

  virtual void ApplyEffect(
    const std::string& stemIdentifier,
    const AudioEffect& effect) = 0;

  virtual void Update(float deltaTimeSeconds) = 0;
};

} // namespace Cinder
