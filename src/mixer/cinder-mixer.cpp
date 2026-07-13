#include "cinder-mixer.hpp"

#include <algorithm>
#include <utility>

#include <firefly/log.hpp>

namespace Cinder {

CinderMixer::CinderMixer(
    std::unique_ptr<IAudioBackend> backend, std::vector<AudioState> audioStates,
    const std::vector<StemFileEntry>& stemsToLoad)
    : backend(std::move(backend)), audioStates(std::move(audioStates))
{
  for (const StemFileEntry& entry : stemsToLoad) {
    this->backend->LoadAudioFile(entry.identifier, entry.filePath);

    Stem stem;
    stem.identifier = entry.identifier;
    stem.currentVolume = 0.0f;
    stem.fadeTargetVolume = 0.0f;
    stem.fadeDurationSeconds = 0.0f;
    stem.fadeElapsedSeconds = 0.0f;
    stem.isFading = false;
    this->stems.emplace(entry.identifier, std::move(stem));

    this->backend->PlayStem(entry.identifier, true);
    this->backend->SetStemVolume(entry.identifier, 0.0f);

    LOG_INFO("Stem '{}' loaded from '{}' and playing looped/silent", entry.identifier, entry.filePath);
  }
}

const AudioState* CinderMixer::FindState(const std::string& stateName) const
{
  for (const AudioState& state : this->audioStates) {
    if (state.stateName == stateName) {
      return &state;
    }
  }
  return nullptr;
}

const Stem* CinderMixer::FindStem(const std::string& stemIdentifier) const
{
  auto it = this->stems.find(stemIdentifier);
  return (it != this->stems.end()) ? &it->second : nullptr;
}

void CinderMixer::TransitionToState(const std::string& stateName, float transitionDurationSeconds)
{
  const AudioState* state = this->FindState(stateName);
  if (state == nullptr) {
    LOG_ERROR("Cannot transition to unknown audio state '{}'", stateName);
    return;
  }

  LOG_INFO("Transitioning to state '{}' over {}s", stateName, transitionDurationSeconds);

  for (auto& [identifier, stem] : this->stems) {
    auto targetIt = state->stemTargetVolumes.find(identifier);
    float targetVolume = (targetIt != state->stemTargetVolumes.end()) ? targetIt->second : 0.0f;
    this->FadeStem(identifier, targetVolume, transitionDurationSeconds);
  }
}

void CinderMixer::SetStemVolume(const std::string& stemIdentifier, float normalizedVolume)
{
  auto it = this->stems.find(stemIdentifier);
  if (it == this->stems.end()) {
    LOG_ERROR("Cannot set volume on unknown stem '{}'", stemIdentifier);
    return;
  }

  Stem& stem = it->second;
  stem.isFading = false;
  stem.currentVolume = normalizedVolume;
  stem.fadeTargetVolume = normalizedVolume;
  stem.fadeDurationSeconds = 0.0f;
  stem.fadeElapsedSeconds = 0.0f;

  this->backend->SetStemVolume(stemIdentifier, normalizedVolume);
}

void CinderMixer::FadeStem(
    const std::string& stemIdentifier, float targetNormalizedVolume, float durationSeconds)
{
  auto it = this->stems.find(stemIdentifier);
  if (it == this->stems.end()) {
    LOG_ERROR("Cannot fade unknown stem '{}'", stemIdentifier);
    return;
  }

  Stem& stem = it->second;

  if (durationSeconds <= 0.0f) {
    stem.currentVolume = targetNormalizedVolume;
    stem.fadeTargetVolume = targetNormalizedVolume;
    stem.fadeDurationSeconds = 0.0f;
    stem.fadeElapsedSeconds = 0.0f;
    stem.isFading = false;
    this->backend->SetStemVolume(stemIdentifier, targetNormalizedVolume);
    return;
  }

  this->fadeStartVolumes[stemIdentifier] = stem.currentVolume;
  stem.fadeTargetVolume = targetNormalizedVolume;
  stem.fadeDurationSeconds = durationSeconds;
  stem.fadeElapsedSeconds = 0.0f;
  stem.isFading = true;

  LOG_INFO(
      "Stem '{}' fade start: {} -> {} over {}s", stemIdentifier, stem.currentVolume,
      targetNormalizedVolume, durationSeconds);
}

void CinderMixer::ApplyEffectToStem(const std::string& stemIdentifier, const AudioEffect& effect)
{
  if (this->stems.find(stemIdentifier) == this->stems.end()) {
    LOG_ERROR("Cannot apply effect to unknown stem '{}'", stemIdentifier);
    return;
  }

  LOG_INFO("Applying effect to stem '{}'", stemIdentifier);
  this->backend->ApplyEffect(stemIdentifier, effect);
}

void CinderMixer::PlaySoundEffect(const std::string& effectIdentifier)
{
  LOG_INFO("Playing sound effect '{}'", effectIdentifier);
  this->backend->PlayStem(effectIdentifier, false);
}

void CinderMixer::Update(float deltaTimeSeconds)
{
  for (auto& [identifier, stem] : this->stems) {
    if (!stem.isFading) {
      continue;
    }

    stem.fadeElapsedSeconds += deltaTimeSeconds;
    float t = std::clamp(stem.fadeElapsedSeconds / stem.fadeDurationSeconds, 0.0f, 1.0f);
    float startVolume = this->fadeStartVolumes[identifier];
    stem.currentVolume = startVolume + (stem.fadeTargetVolume - startVolume) * t;

    this->backend->SetStemVolume(identifier, stem.currentVolume);

    if (stem.fadeElapsedSeconds >= stem.fadeDurationSeconds) {
      stem.currentVolume = stem.fadeTargetVolume;
      stem.isFading = false;
      LOG_INFO("Stem '{}' fade complete at volume {}", identifier, stem.currentVolume);
    }
  }

  this->backend->Update(deltaTimeSeconds);
}

} // namespace Cinder
