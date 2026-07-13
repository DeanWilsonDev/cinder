#include "cimmerian/test.hpp"

#include <cmath>
#include <memory>

#include "fake-audio-backend.hpp"
#include "mixer/cinder-mixer.hpp"

using namespace Cinder;

DESCRIBE("Stem Fading", {
  TEST("starts a registered stem silent", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {{"melody", "melody.wav"}};
    CinderMixer mixer(std::move(backend), {}, stemsToLoad);

    const Stem* stem = mixer.FindStem("melody");
    REQUIRE_TRUE(stem != nullptr);
    ASSERT_EQUAL(stem->currentVolume, 0.0f);
    ASSERT_FALSE(stem->isFading);
  });

  TEST("queues a fade with the correct target volume", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {{"melody", "melody.wav"}};
    CinderMixer mixer(std::move(backend), {}, stemsToLoad);

    mixer.FadeStem("melody", 0.8f, 2.0f);

    const Stem* stem = mixer.FindStem("melody");
    REQUIRE_TRUE(stem != nullptr);
    ASSERT_TRUE(stem->isFading);
    ASSERT_EQUAL(stem->fadeTargetVolume, 0.8f);
  });

  TEST("interpolates linearly to approximately half target volume at the midpoint", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {{"melody", "melody.wav"}};
    CinderMixer mixer(std::move(backend), {}, stemsToLoad);

    mixer.FadeStem("melody", 0.8f, 2.0f);
    mixer.Update(1.0f); // half the fade duration

    const Stem* stem = mixer.FindStem("melody");
    REQUIRE_TRUE(stem != nullptr);
    // Cimmerian has no epsilon-tolerant float assertion yet (see
    // docs/cimmerian.md) -- ASSERT_TRUE with a manual epsilon is the
    // documented workaround until ASSERT_NEAR lands upstream.
    ASSERT_TRUE(std::abs(stem->currentVolume - 0.4f) < 0.001f);
  });

  TEST("reaches the exact target volume and stops fading once the duration elapses", {
    auto backend = std::make_unique<CinderTest::FakeAudioBackend>();
    std::vector<StemFileEntry> stemsToLoad = {{"melody", "melody.wav"}};
    CinderMixer mixer(std::move(backend), {}, stemsToLoad);

    mixer.FadeStem("melody", 0.8f, 2.0f);
    mixer.Update(1.0f);
    mixer.Update(1.0f);

    const Stem* stem = mixer.FindStem("melody");
    REQUIRE_TRUE(stem != nullptr);
    ASSERT_EQUAL(stem->currentVolume, 0.8f);
    ASSERT_FALSE(stem->isFading);
  });
});
