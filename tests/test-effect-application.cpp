#include "cimmerian/test.hpp"

#include <memory>

#include "fake-audio-backend.hpp"
#include "mixer/cinder-mixer.hpp"

using namespace Cinder;

namespace {

std::unique_ptr<CinderMixer> MakeMixerWithStem(
    CinderTest::FakeAudioBackend** outBackend, const std::string& stemIdentifier)
{
  auto backendOwned = std::make_unique<CinderTest::FakeAudioBackend>();
  *outBackend = backendOwned.get();
  std::vector<StemFileEntry> stemsToLoad = {{stemIdentifier, stemIdentifier + ".wav"}};
  return std::make_unique<CinderMixer>(std::move(backendOwned), std::vector<AudioState>{}, stemsToLoad);
}

} // namespace

DESCRIBE("Effect Application", {
  TEST("applies a low pass filter and passes its parameters to the backend", {
    CinderTest::FakeAudioBackend* backend = nullptr;
    auto mixer = MakeMixerWithStem(&backend, "ambience");

    AudioEffect lowPass;
    lowPass.effectType = AudioEffectType::LowPassFilter;
    lowPass.parameters.cutoffFrequencyHz = 800.0f;
    mixer->ApplyEffectToStem("ambience", lowPass);

    REQUIRE_TRUE(!backend->effectCalls.empty());
    const CinderTest::ApplyEffectCall& call = backend->effectCalls.back();
    ASSERT_EQUAL(call.identifier, std::string("ambience"));
    ASSERT_TRUE(call.effect.effectType == AudioEffectType::LowPassFilter);
    ASSERT_EQUAL(call.effect.parameters.cutoffFrequencyHz, 800.0f);
  });

  TEST("applies a reverb effect and passes its parameters to the backend", {
    CinderTest::FakeAudioBackend* backend = nullptr;
    auto mixer = MakeMixerWithStem(&backend, "harmony");

    AudioEffect reverb;
    reverb.effectType = AudioEffectType::Reverb;
    reverb.parameters.reverbDecaySeconds = 1.5f;
    reverb.parameters.reverbMix = 0.3f;
    mixer->ApplyEffectToStem("harmony", reverb);

    REQUIRE_TRUE(!backend->effectCalls.empty());
    const CinderTest::ApplyEffectCall& call = backend->effectCalls.back();
    ASSERT_EQUAL(call.identifier, std::string("harmony"));
    ASSERT_TRUE(call.effect.effectType == AudioEffectType::Reverb);
    ASSERT_EQUAL(call.effect.parameters.reverbDecaySeconds, 1.5f);
    ASSERT_EQUAL(call.effect.parameters.reverbMix, 0.3f);
  });

  TEST("applies a high pass filter and passes its parameters to the backend", {
    CinderTest::FakeAudioBackend* backend = nullptr;
    auto mixer = MakeMixerWithStem(&backend, "ambience");

    AudioEffect highPass;
    highPass.effectType = AudioEffectType::HighPassFilter;
    highPass.parameters.cutoffFrequencyHz = 400.0f;
    mixer->ApplyEffectToStem("ambience", highPass);

    REQUIRE_TRUE(!backend->effectCalls.empty());
    const CinderTest::ApplyEffectCall& call = backend->effectCalls.back();
    ASSERT_EQUAL(call.identifier, std::string("ambience"));
    ASSERT_TRUE(call.effect.effectType == AudioEffectType::HighPassFilter);
    ASSERT_EQUAL(call.effect.parameters.cutoffFrequencyHz, 400.0f);
  });

  TEST("keeps only the most recently applied effect active on a stem", {
    CinderTest::FakeAudioBackend* backend = nullptr;
    auto mixer = MakeMixerWithStem(&backend, "ambience");

    AudioEffect lowPass;
    lowPass.effectType = AudioEffectType::LowPassFilter;
    lowPass.parameters.cutoffFrequencyHz = 600.0f;
    mixer->ApplyEffectToStem("ambience", lowPass);

    AudioEffect reverb;
    reverb.effectType = AudioEffectType::Reverb;
    reverb.parameters.reverbDecaySeconds = 1.2f;
    reverb.parameters.reverbMix = 0.25f;
    mixer->ApplyEffectToStem("ambience", reverb);

    ASSERT_EQUAL(backend->effectCalls.size(), static_cast<std::size_t>(2));

    // The prototype supports one active effect per stem; the backend's
    // current-effect tracking (mirrored here by the fake) must reflect
    // only the second, most recently applied effect -- not both.
    const AudioEffect& active = backend->currentEffectByStem.at("ambience");
    ASSERT_TRUE(active.effectType == AudioEffectType::Reverb);
    ASSERT_EQUAL(active.parameters.reverbDecaySeconds, 1.2f);
    ASSERT_EQUAL(active.parameters.reverbMix, 0.25f);
  });
});
