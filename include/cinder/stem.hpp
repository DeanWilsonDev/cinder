#pragma once

#include <string>

namespace Cinder {

struct Stem {
  std::string identifier;
  float currentVolume = 0.0f;
  float fadeTargetVolume = 0.0f;
  float fadeDurationSeconds = 0.0f;
  float fadeElapsedSeconds = 0.0f;
  bool isFading = false;
};

} // namespace Cinder
