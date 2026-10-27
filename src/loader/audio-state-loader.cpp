#include "audio-state-loader.hpp"

#include <amanuensis/io/reader.hpp>
#include <amanuensis/json.hpp>
#include <firefly/log.hpp>

namespace Cinder {

namespace {

// Amanuensis::Json::AsDouble() throws unless the JSON literal contained a
// decimal point (i.e. was parsed as JsonValueType::Double); a bare integer like
// "melody": 1 parses as ValueType::Integer instead. Coerce either numeric
// representation to float so state authors don't have to remember to write
// "1.0" instead of "1".
float AsFloatNumber(const Amanuensis::JsonValue& value)
{
  if (Amanuensis::Json::IsInteger(value)) {
    return static_cast<float>(Amanuensis::Json::AsInteger(value));
  }
  return static_cast<float>(Amanuensis::Json::AsDouble(value));
}

} // namespace

std::vector<AudioState> AudioStateLoader::LoadFromFile(const std::string& filePath)
{
  std::vector<AudioState> states;

  Amanuensis::JsonParseResult result = Amanuensis::Reader::ParseFile(filePath);
  if (!result.succeeded) {
    LOG_ERROR(
        "Failed to load audio states from '{}': {}:{} {}", filePath, result.error.line,
        result.error.column, result.error.message);
    return states;
  }

  const Amanuensis::JsonValue& root = result.value;
  const Amanuensis::JsonValue* statesArray =
      Amanuensis::Json::IsObject(root) ? Amanuensis::Json::Find(root, "states") : nullptr;
  if (statesArray == nullptr) {
    LOG_ERROR("Audio state file '{}' is malformed: missing top-level 'states' array", filePath);
    return states;
  }

  if (!Amanuensis::Json::IsArray(*statesArray)) {
    LOG_ERROR("Audio state file '{}' is malformed: 'states' is not an array", filePath);
    return states;
  }

  for (std::size_t stateIndex = 0; stateIndex < Amanuensis::Json::Size(*statesArray); ++stateIndex) {
    const Amanuensis::JsonValue& stateValue = Amanuensis::Json::At(*statesArray, stateIndex);
    if (!Amanuensis::Json::IsObject(stateValue) || !Amanuensis::Json::Contains(stateValue, "name") ||
        !Amanuensis::Json::Contains(stateValue, "stems")) {
      LOG_ERROR(
          "Audio state file '{}' is malformed: state at index {} is missing 'name' or 'stems'",
          filePath, stateIndex);
      continue;
    }

    AudioState state;
    state.stateName = Amanuensis::Json::AsString(Amanuensis::Json::Get(stateValue, "name"));

    const Amanuensis::JsonValue* stems = Amanuensis::Json::Find(stateValue, "stems");
    if (!Amanuensis::Json::IsObject(*stems)) {
      LOG_ERROR(
          "Audio state '{}' in '{}' is malformed: 'stems' is not an object", state.stateName,
          filePath);
      continue;
    }

    for (auto it = Amanuensis::Json::BeginObject(*stems); it != Amanuensis::Json::EndObject(*stems); ++it) {
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
