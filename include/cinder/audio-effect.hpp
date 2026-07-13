#pragma once

namespace Cinder {

enum class AudioEffectType {
  LowPassFilter,
  HighPassFilter,
  Reverb
};

struct AudioEffectParameters {
  float cutoffFrequencyHz = 0.0f;  // LowPass, HighPass
  float reverbDecaySeconds = 0.0f; // Reverb
  float reverbMix = 0.0f;          // Reverb — dry/wet, [0.0, 1.0]
};

struct AudioEffect {
  AudioEffectType effectType;
  AudioEffectParameters parameters;
};

} // namespace Cinder
