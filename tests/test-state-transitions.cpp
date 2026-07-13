#include "cimmerian/test.hpp"

#include <memory>

#include "fake-audio-backend.hpp"
#include "mixer/cinder-mixer.hpp"

using namespace Cinder;

namespace {

// Hardcoded in-memory configuration -- no disk access in tests. Chapter2
// intentionally drops "melody" (present in Chapter1) so the "faded to
// silence when absent from the new state" behaviour has something to
// exercise.
std::vector<AudioState> MakeTestStates()
{
  AudioState chapter1;
  chapter1.stateName = "Chapter1";
  chapter1.stemTargetVolumes = {{"ambience", 0.5f}, {"melody", 0.8f}};

  AudioState chapter2;
  chapter2.stateName = "Chapter2";
  chapter2.stemTargetVolumes = {{"ambience", 0.5f}, {"harmony", 0.7f}, {"percussion", 0.6f}};

  return {chapter1, chapter2};
}

} // namespace

DESCRIBE("State Transitions", {
  TEST("queues correct target volumes when transitioning to Chapter1", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {
        {"ambience", "ambience.wav"}, {"melody", "melody.wav"}, {"harmony", "harmony.wav"},
        {"percussion", "percussion.wav"}};
    CinderMixer mixer(std::move(backend), MakeTestStates(), stemsToLoad);

    mixer.TransitionToState("Chapter1", 3.0f);

    const Stem* ambience = mixer.FindStem("ambience");
    const Stem* melody = mixer.FindStem("melody");
    REQUIRE_TRUE(ambience != nullptr);
    REQUIRE_TRUE(melody != nullptr);
    ASSERT_TRUE(ambience->isFading);
    ASSERT_EQUAL(ambience->fadeTargetVolume, 0.5f);
    ASSERT_TRUE(melody->isFading);
    ASSERT_EQUAL(melody->fadeTargetVolume, 0.8f);
  });

  TEST("queues harmony and percussion fades when transitioning to Chapter2", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {
        {"ambience", "ambience.wav"}, {"melody", "melody.wav"}, {"harmony", "harmony.wav"},
        {"percussion", "percussion.wav"}};
    CinderMixer mixer(std::move(backend), MakeTestStates(), stemsToLoad);

    mixer.TransitionToState("Chapter1", 3.0f);
    mixer.TransitionToState("Chapter2", 4.0f);

    const Stem* harmony = mixer.FindStem("harmony");
    const Stem* percussion = mixer.FindStem("percussion");
    REQUIRE_TRUE(harmony != nullptr);
    REQUIRE_TRUE(percussion != nullptr);
    ASSERT_TRUE(harmony->isFading);
    ASSERT_EQUAL(harmony->fadeTargetVolume, 0.7f);
    ASSERT_TRUE(percussion->isFading);
    ASSERT_EQUAL(percussion->fadeTargetVolume, 0.6f);
  });

  TEST("fades a stem absent from the new state to silence", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {
        {"ambience", "ambience.wav"}, {"melody", "melody.wav"}, {"harmony", "harmony.wav"},
        {"percussion", "percussion.wav"}};
    CinderMixer mixer(std::move(backend), MakeTestStates(), stemsToLoad);

    mixer.TransitionToState("Chapter1", 3.0f);
    mixer.TransitionToState("Chapter2", 4.0f);

    // "melody" is in Chapter1 but not Chapter2.
    const Stem* melody = mixer.FindStem("melody");
    REQUIRE_TRUE(melody != nullptr);
    ASSERT_TRUE(melody->isFading);
    ASSERT_EQUAL(melody->fadeTargetVolume, 0.0f);
  });
});
