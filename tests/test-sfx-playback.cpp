#include "cimmerian/test.hpp"

#include <memory>

#include "fake-audio-backend.hpp"
#include "mixer/cinder-mixer.hpp"

using namespace Cinder;

DESCRIBE("SFX Playback", {
  TEST("forwards PlaySoundEffect to the backend as a non-looping PlayStem call", {
    auto backendOwned = std::make_unique<CinderTest::FakeAudioBackend>();
    CinderTest::FakeAudioBackend* backend = backendOwned.get();
    CinderMixer mixer(std::move(backendOwned), {}, {});

    mixer.PlaySoundEffect("ui_click");

    REQUIRE_TRUE(!backend->playCalls.empty());
    const CinderTest::PlayStemCall& call = backend->playCalls.back();
    ASSERT_EQUAL(call.identifier, std::string("ui_click"));
    ASSERT_FALSE(call.shouldLoop);
  });

  TEST("does not create a stem entry in the mixer's stem map for a sound effect", {
    auto backendOwned = std::make_unique<CinderTest::FakeAudioBackend>();
    CinderMixer mixer(std::move(backendOwned), {}, {});

    mixer.PlaySoundEffect("door_open");

    ASSERT_TRUE(mixer.FindStem("door_open") == nullptr);
  });
});
