#include "audio-state-loader.hpp"

#include <amanuensis/io/reader.hpp>
#include <amanuensis/value.hpp>
#include <firefly/log.hpp>

namespace Cinder {

namespace {

// Amanuensis::Value::AsDouble() throws unless the JSON literal contained a
// decimal point (i.e. was parsed as ValueType::Double); a bare integer like
// "melody": 1 parses as ValueType::Integer instead. Coerce either numeric
// representation to float so state authors don't have to remember to write
// "1.0" instead of "1".
float AsFloatNumber(const Amanuensis::Value& value)
{
  if (value.IsInteger()) {
    return static_cast<float>(value.AsInteger());
  }
  return static_cast<float>(value.AsDouble());
}

} // namespace

std::vector<AudioState> AudioStateLoader::LoadFromFile(const std::string& filePath)
{
  std::vector<AudioState> states;

  Amanuensis::ParseResult result = Amanuensis::Reader::ParseFile(filePath);
  if (!result.succeeded) {
    LOG_ERROR(
        "Failed to load audio states from '{}': {}:{} {}", filePath, result.error.line,
        result.error.column, result.error.message);
    return states;
  }

  const Amanuensis::Value& root = result.value;
  if (!root.IsObject() || !root.Contains("states")) {
    LOG_ERROR("Audio state file '{}' is malformed: missing top-level 'states' array", filePath);
    return states;
  }

  const Amanuensis::Value& statesArray = root.Get("states");
  if (!statesArray.IsArray()) {
    LOG_ERROR("Audio state file '{}' is malformed: 'states' is not an array", filePath);
    return states;
  }

  for (std::size_t stateIndex = 0; stateIndex < statesArray.Size(); ++stateIndex) {
    const Amanuensis::Value& stateValue = statesArray.At(stateIndex);
    if (!stateValue.IsObject() || !stateValue.Contains("name") ||
        !stateValue.Contains("stems")) {
      LOG_ERROR(
          "Audio state file '{}' is malformed: state at index {} is missing 'name' or 'stems'",
          filePath, stateIndex);
      continue;
    }

    AudioState state;
    state.stateName = stateValue.Get("name").AsString();

    const Amanuensis::Value& stems = stateValue.Get("stems");
    if (!stems.IsObject()) {
      LOG_ERROR(
          "Audio state '{}' in '{}' is malformed: 'stems' is not an object", state.stateName,
          filePath);
      continue;
    }

    for (auto it = stems.BeginObject(); it != stems.EndObject(); ++it) {
      state.stemTargetVolumes[it->first] = AsFloatNumber(it->second);
    }

    LOG_INFO(
        "Loaded audio state '{}' with {} stem target(s) from '{}'", state.stateName,
        state.stemTargetVolumes.size(), filePath);
    states.push_back(std::move(state));
  }

  return states;
}

} // namespace Cinder
