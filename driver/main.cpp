#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <firefly/log-registry.hpp>
#include <firefly/log.hpp>

#include "backend/audio-backend.hpp"
#include "cinder/audio-effect.hpp"
#include "loader/audio-state-loader.hpp"
#include "mixer/cinder-mixer.hpp"

using namespace Cinder;

namespace {

// Looks for `<directory>/<baseName>.wav` then `.mp3`. Returns an empty
// string if neither exists -- callers log a warning and continue rather
// than crash, per the spec's "missing files should not crash the driver".
std::string ResolveAudioFile(const std::string& directory, const std::string& baseName)
{
  namespace fs = std::filesystem;
  fs::path wavPath = fs::path(directory) / (baseName + ".wav");
  if (fs::exists(wavPath)) {
    return wavPath.string();
  }
  fs::path mp3Path = fs::path(directory) / (baseName + ".mp3");
  if (fs::exists(mp3Path)) {
    return mp3Path.string();
  }
  return "";
}

// Runs CinderMixer::Update at a fixed 60 Hz tick rate for `seconds`,
// sleeping the remainder of each ~16.67ms frame.
void RunGameLoop(CinderMixer& mixer, float seconds)
{
  constexpr float kTickSeconds = 1.0f / 60.0f;
  const auto tickDuration = std::chrono::duration<float>(kTickSeconds);
  int totalTicks = static_cast<int>(seconds / kTickSeconds);

  for (int tick = 0; tick < totalTicks; ++tick) {
    auto frameStart = std::chrono::steady_clock::now();

    mixer.Update(kTickSeconds);

    auto elapsed = std::chrono::steady_clock::now() - frameStart;
    auto sleepDuration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration) - elapsed;
    if (sleepDuration > std::chrono::steady_clock::duration::zero()) {
      std::this_thread::sleep_for(sleepDuration);
    }
  }
}

} // namespace

int main()
{
  Firefly::LogRegistry::RegisterLogger(FIREFLY_DEFAULT_LOGGER, "cinder-driver.log", true);
  LOG_INFO("Cinder driver starting");

  std::vector<AudioState> states = AudioStateLoader::LoadFromFile("data/audio-states.json");

  auto backend = std::make_unique<AudioBackend>();

  const std::vector<std::pair<std::string, std::string>> kStemNames = {
      {"ambience", "ambience"}, {"melody", "melody"}, {"harmony", "harmony"},
      {"percussion", "percussion"}, {"vocals", "vocals"}};

  std::vector<StemFileEntry> stemsToLoad;
  for (const auto& [identifier, baseName] : kStemNames) {
    std::string path = ResolveAudioFile("data/stems", baseName);
    if (path.empty()) {
      LOG_WARNING(
          "Stem '{}' has no matching .wav or .mp3 file in data/stems/; it will be silent",
          identifier);
      continue;
    }
    stemsToLoad.push_back({identifier, path});
  }

  const std::vector<std::pair<std::string, std::string>> kSfxNames = {
      {"ui_click", "ui-click"}, {"door_open", "door-open"}};

  for (const auto& [identifier, baseName] : kSfxNames) {
    std::string path = ResolveAudioFile("data/sfx", baseName);
    if (path.empty()) {
      LOG_WARNING(
          "SFX '{}' has no matching .wav or .mp3 file in data/sfx/; playback will be a no-op",
          identifier);
      continue;
    }
    backend->LoadAudioFile(identifier, path);
  }

  CinderMixer mixer(std::move(backend), states, stemsToLoad);
  LOG_INFO("Cinder driver startup complete");

  LOG_INFO("Step 1 -- Transition to Chapter1 over 3 seconds");
  mixer.TransitionToState("Chapter1", 3.0f);
  RunGameLoop(mixer, 5.0f);

  LOG_INFO("Step 2 -- Apply low pass filter to 'ambience' at 600 Hz");
  {
    AudioEffect effect;
    effect.effectType = AudioEffectType::LowPassFilter;
    effect.parameters.cutoffFrequencyHz = 600.0f;
    mixer.ApplyEffectToStem("ambience", effect);
  }
  RunGameLoop(mixer, 2.0f);

  LOG_INFO("Step 3 -- Transition to Chapter2 over 4 seconds");
  mixer.TransitionToState("Chapter2", 4.0f);
  RunGameLoop(mixer, 6.0f);

  LOG_INFO("Step 4 -- FadeStem 'melody' to 0.2 over 2 seconds");
  mixer.FadeStem("melody", 0.2f, 2.0f);
  RunGameLoop(mixer, 3.0f);

  LOG_INFO("Step 5 -- PlaySoundEffect 'ui_click'");
  mixer.PlaySoundEffect("ui_click");
  RunGameLoop(mixer, 1.0f);

  LOG_INFO("Step 6 -- PlaySoundEffect 'door_open'");
  mixer.PlaySoundEffect("door_open");
  RunGameLoop(mixer, 1.0f);

  LOG_INFO("Step 7 -- Apply reverb to 'harmony' (decay 1.2s, mix 0.25)");
  {
    AudioEffect effect;
    effect.effectType = AudioEffectType::Reverb;
    effect.parameters.reverbDecaySeconds = 1.2f;
    effect.parameters.reverbMix = 0.25f;
    mixer.ApplyEffectToStem("harmony", effect);
  }
  RunGameLoop(mixer, 3.0f);

  LOG_INFO("Step 8 -- Transition to PostGame_Run2 over 5 seconds");
  mixer.TransitionToState("PostGame_Run2", 5.0f);
  RunGameLoop(mixer, 7.0f);

  LOG_INFO("Step 9 -- Apply high pass filter to 'ambience' at 400 Hz");
  {
    AudioEffect effect;
    effect.effectType = AudioEffectType::HighPassFilter;
    effect.parameters.cutoffFrequencyHz = 400.0f;
    mixer.ApplyEffectToStem("ambience", effect);
  }
  RunGameLoop(mixer, 3.0f);

  LOG_INFO("Step 10 -- FadeStem all remaining audible stems to 0.0 over 3 seconds");
  for (const StemFileEntry& entry : stemsToLoad) {
    mixer.FadeStem(entry.identifier, 0.0f, 3.0f);
  }
  RunGameLoop(mixer, 4.0f);

  LOG_INFO("Step 11 -- Sequence complete, exiting");
  return 0;
}
