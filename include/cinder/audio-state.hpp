#pragma once

#include <string>
#include <unordered_map>

namespace Cinder {

struct AudioState {
  std::string stateName;
  std::unordered_map<std::string, float> stemTargetVolumes;
};

} // namespace Cinder
