#include "cimmerian/test.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "loader/audio-state-loader.hpp"

using namespace Cinder;

namespace {

std::string WriteTemporaryStateFile(const std::string& fileName, const std::string& contents)
{
  const std::filesystem::path path = std::filesystem::temp_directory_path() / fileName;
  std::ofstream(path) << contents;
  return path.string();
}

} // namespace

DESCRIBE("Audio State Loader", {
  TEST("loads every state and its stem volumes from the shipped data file", {
    const std::vector<AudioState> states =
        AudioStateLoader::LoadFromFile(std::string(CINDER_DATA_DIR) + "/audio-states.json");
    REQUIRE_EQUAL(states.size(), size_t{3});
    ASSERT_EQUAL(states[0].stateName, std::string("Chapter1"));
    ASSERT_EQUAL(states[0].stemTargetVolumes.size(), size_t{2});
    REQUIRE_TRUE(states[0].stemTargetVolumes.contains("melody"));
    ASSERT_EQUAL(states[0].stemTargetVolumes.at("melody"), 0.8f);
    ASSERT_EQUAL(states[1].stemTargetVolumes.size(), size_t{5});
    ASSERT_EQUAL(states[2].stateName, std::string("PostGame_Run2"));
    REQUIRE_TRUE(states[2].stemTargetVolumes.contains("harmony"));
    ASSERT_EQUAL(states[2].stemTargetVolumes.at("harmony"), 0.2f);
  });

  TEST("reads a whole-number volume as a float", {
    const std::string path = WriteTemporaryStateFile(
        "cinder_integer_volume.json", R"({"states": [{"name": "Quiet", "stems": {"ambience": 1, "melody": 0}}]})");
    const std::vector<AudioState> states = AudioStateLoader::LoadFromFile(path);
    REQUIRE_EQUAL(states.size(), size_t{1});
    REQUIRE_TRUE(states[0].stemTargetVolumes.contains("ambience"));
    REQUIRE_TRUE(states[0].stemTargetVolumes.contains("melody"));
    ASSERT_EQUAL(states[0].stemTargetVolumes.at("ambience"), 1.0f);
    ASSERT_EQUAL(states[0].stemTargetVolumes.at("melody"), 0.0f);
  });

  TEST("skips malformed states and returns nothing for a malformed file", {
    const std::string partlyMalformed = WriteTemporaryStateFile(
        "cinder_partly_malformed.json",
        R"({"states": [{"name": "NoStems"}, {"name": "BadStems", "stems": []}, {"name": null, "stems": {"a": 0.1}}, )"
        R"({"name": "TextStem", "stems": {"a": "loud"}}, {"name": "Good", "stems": {"a": 0.5}}]})");
    const std::vector<AudioState> states = AudioStateLoader::LoadFromFile(partlyMalformed);
    REQUIRE_EQUAL(states.size(), size_t{1});
    ASSERT_EQUAL(states[0].stateName, std::string("Good"));

    ASSERT_EQUAL(AudioStateLoader::LoadFromFile(WriteTemporaryStateFile("cinder_no_states.json", R"({"other": 1})")).size(), size_t{0});
    ASSERT_EQUAL(AudioStateLoader::LoadFromFile(WriteTemporaryStateFile("cinder_states_object.json", R"({"states": {}})")).size(), size_t{0});
    ASSERT_EQUAL(AudioStateLoader::LoadFromFile(WriteTemporaryStateFile("cinder_not_json.json", "{")).size(), size_t{0});
  });
});
