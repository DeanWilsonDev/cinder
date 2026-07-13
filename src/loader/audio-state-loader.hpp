#pragma once

#include <string>
#include <vector>

#include "cinder/audio-state.hpp"

namespace Cinder {

// Loads data/audio-states.json using Amanuensis at startup. Amanuensis is an
// implementation detail of this loader and never appears in a public header.
class AudioStateLoader {
public:
  static std::vector<AudioState> LoadFromFile(const std::string& filePath);
};

} // namespace Cinder
