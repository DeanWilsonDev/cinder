#include "audio-backend.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <firefly/log.hpp>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#elif defined(__linux__)
#include <atomic>
#include <thread>

#include <alsa/asoundlib.h>
#else
#error "Cinder's audio backend only supports macOS and Linux."
#endif

namespace Cinder {

namespace {
constexpr float kSampleRate = 44100.0f;
constexpr std::uint32_t kBufferFrameCount = 1024;

// Schroeder reverb: 4 parallel feedback comb filters summed, followed by 2
// series allpass filters. Delay lengths are the classic Schroeder values;
// sufficient for prototype evaluation, not tuned further.
constexpr float kCombDelaysMs[EffectState::kCombCount] = {29.7f, 37.1f, 41.1f, 43.7f};
constexpr float kAllpassDelaysMs[EffectState::kAllpassCount] = {5.0f, 1.7f};
constexpr float kAllpassGain = 0.7f;

void InitializeReverb(EffectState& effect)
{
  float decaySeconds = effect.parameters.reverbDecaySeconds > 0.0f
                            ? effect.parameters.reverbDecaySeconds
                            : 0.001f;

  for (int channel = 0; channel < 2; ++channel) {
    for (int c = 0; c < EffectState::kCombCount; ++c) {
      int size = std::max(1, static_cast<int>(kCombDelaysMs[c] * 0.001f * kSampleRate));
      effect.combBuffer[channel][c].assign(static_cast<std::size_t>(size), 0.0f);
      effect.combWritePos[channel][c] = 0;
    }
    for (int a = 0; a < EffectState::kAllpassCount; ++a) {
      int size = std::max(1, static_cast<int>(kAllpassDelaysMs[a] * 0.001f * kSampleRate));
      effect.allpassBuffer[channel][a].assign(static_cast<std::size_t>(size), 0.0f);
      effect.allpassWritePos[channel][a] = 0;
    }
  }

  for (int c = 0; c < EffectState::kCombCount; ++c) {
    float delaySeconds = kCombDelaysMs[c] * 0.001f;
    effect.combFeedback[c] = std::pow(10.0f, -3.0f * delaySeconds / decaySeconds);
  }
}

float ApplyLowPass(EffectState& effect, int channel, float input)
{
  float cutoff = effect.parameters.cutoffFrequencyHz;
  if (cutoff <= 0.0f) {
    return input;
  }
  float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff);
  float dt = 1.0f / kSampleRate;
  float alpha = dt / (rc + dt);
  float output = effect.filterState[channel] + alpha * (input - effect.filterState[channel]);
  effect.filterState[channel] = output;
  return output;
}

float ApplyHighPass(EffectState& effect, int channel, float input)
{
  float cutoff = effect.parameters.cutoffFrequencyHz;
  if (cutoff <= 0.0f) {
    return input;
  }
  float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff);
  float dt = 1.0f / kSampleRate;
  float alpha = rc / (rc + dt);
  float output = alpha * (effect.filterState[channel] + input - effect.filterPrevInput[channel]);
  effect.filterPrevInput[channel] = input;
  effect.filterState[channel] = output;
  return output;
}

float ApplyReverb(EffectState& effect, int channel, float input)
{
  float combSum = 0.0f;
  for (int c = 0; c < EffectState::kCombCount; ++c) {
    std::vector<float>& buffer = effect.combBuffer[channel][c];
    int& pos = effect.combWritePos[channel][c];
    float delayed = buffer[static_cast<std::size_t>(pos)];
    buffer[static_cast<std::size_t>(pos)] = input + delayed * effect.combFeedback[c];
    pos = (pos + 1) % static_cast<int>(buffer.size());
    combSum += delayed;
  }
  combSum *= 0.25f;

  float signal = combSum;
  for (int a = 0; a < EffectState::kAllpassCount; ++a) {
    std::vector<float>& buffer = effect.allpassBuffer[channel][a];
    int& pos = effect.allpassWritePos[channel][a];
    float bufferOut = buffer[static_cast<std::size_t>(pos)];
    float output = -signal * kAllpassGain + bufferOut;
    buffer[static_cast<std::size_t>(pos)] = signal + bufferOut * kAllpassGain;
    pos = (pos + 1) % static_cast<int>(buffer.size());
    signal = output;
  }

  float mix = std::clamp(effect.parameters.reverbMix, 0.0f, 1.0f);
  return input * (1.0f - mix) + signal * mix;
}

float ApplyEffectSample(EffectState& effect, int channel, float input)
{
  switch (effect.type) {
    case AudioEffectType::LowPassFilter:
      return ApplyLowPass(effect, channel, input);
    case AudioEffectType::HighPassFilter:
      return ApplyHighPass(effect, channel, input);
    case AudioEffectType::Reverb:
      return ApplyReverb(effect, channel, input);
  }
  return input;
}

} // namespace

#if defined(__APPLE__)

struct AudioBackend::PlatformState {
  AudioBackend* owner = nullptr;
  AudioQueueRef queue = nullptr;
  AudioQueueBufferRef buffers[3] = {};

  static void RenderCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer)
  {
    auto* platform = static_cast<PlatformState*>(userData);
    std::uint32_t frameCount =
        buffer->mAudioDataBytesCapacity / static_cast<std::uint32_t>(sizeof(float) * 2);
    platform->owner->RenderAudio(reinterpret_cast<float*>(buffer->mAudioData), frameCount);
    buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  }
};

AudioBackend::AudioBackend() : platform_(std::make_unique<PlatformState>())
{
  LOG_INFO("Initialising Core Audio backend ({} Hz, stereo, float32)", static_cast<int>(kSampleRate));
  platform_->owner = this;

  AudioStreamBasicDescription format{};
  format.mSampleRate = kSampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mBytesPerPacket = sizeof(float) * 2;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = sizeof(float) * 2;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 32;

  OSStatus status = AudioQueueNewOutput(
      &format, PlatformState::RenderCallback, platform_.get(), nullptr, nullptr, 0,
      &platform_->queue);
  if (status != noErr) {
    LOG_ERROR("Failed to create Core Audio output queue (status {})", static_cast<int>(status));
    return;
  }

  std::uint32_t bufferBytes = kBufferFrameCount * static_cast<std::uint32_t>(sizeof(float) * 2);
  for (AudioQueueBufferRef& buffer : platform_->buffers) {
    AudioQueueAllocateBuffer(platform_->queue, bufferBytes, &buffer);
    buffer->mAudioDataByteSize = bufferBytes;
    PlatformState::RenderCallback(platform_.get(), platform_->queue, buffer);
  }

  AudioQueueStart(platform_->queue, nullptr);
}

AudioBackend::~AudioBackend()
{
  LOG_INFO("Shutting down Core Audio backend");
  if (platform_ && platform_->queue) {
    AudioQueueStop(platform_->queue, true);
    AudioQueueDispose(platform_->queue, true);
  }
}

#elif defined(__linux__)

struct AudioBackend::PlatformState {
  AudioBackend* owner = nullptr;
  snd_pcm_t* pcmHandle = nullptr;
  std::thread playbackThread;
  std::atomic<bool> running{false};

  void PlaybackLoop()
  {
    std::vector<float> buffer(kBufferFrameCount * 2);
    while (running.load()) {
      owner->RenderAudio(buffer.data(), kBufferFrameCount);
      snd_pcm_sframes_t written =
          snd_pcm_writei(pcmHandle, buffer.data(), kBufferFrameCount);
      if (written < 0) {
        written = snd_pcm_recover(pcmHandle, static_cast<int>(written), 0);
        if (written < 0) {
          break;
        }
      }
    }
  }
};

AudioBackend::AudioBackend() : platform_(std::make_unique<PlatformState>())
{
  LOG_INFO("Initialising ALSA backend ({} Hz, stereo, float32)", static_cast<int>(kSampleRate));
  platform_->owner = this;

  int err = snd_pcm_open(&platform_->pcmHandle, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    LOG_ERROR("Failed to open ALSA PCM device: {}", snd_strerror(err));
    platform_->pcmHandle = nullptr;
    return;
  }

  snd_pcm_hw_params_t* hwParams = nullptr;
  snd_pcm_hw_params_alloca(&hwParams);
  snd_pcm_hw_params_any(platform_->pcmHandle, hwParams);
  snd_pcm_hw_params_set_access(platform_->pcmHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(platform_->pcmHandle, hwParams, SND_PCM_FORMAT_FLOAT_LE);
  snd_pcm_hw_params_set_channels(platform_->pcmHandle, hwParams, 2);
  unsigned int rate = static_cast<unsigned int>(kSampleRate);
  snd_pcm_hw_params_set_rate_near(platform_->pcmHandle, hwParams, &rate, nullptr);

  if (snd_pcm_hw_params(platform_->pcmHandle, hwParams) < 0) {
    LOG_ERROR("Failed to configure ALSA hardware parameters");
    snd_pcm_close(platform_->pcmHandle);
    platform_->pcmHandle = nullptr;
    return;
  }
  snd_pcm_prepare(platform_->pcmHandle);

  platform_->running.store(true);
  platform_->playbackThread = std::thread([this] { platform_->PlaybackLoop(); });
}

AudioBackend::~AudioBackend()
{
  LOG_INFO("Shutting down ALSA backend");
  if (platform_) {
    platform_->running.store(false);
    if (platform_->playbackThread.joinable()) {
      platform_->playbackThread.join();
    }
    if (platform_->pcmHandle) {
      snd_pcm_close(platform_->pcmHandle);
    }
  }
}

#endif

void AudioBackend::LoadAudioFile(const std::string& stemIdentifier, const std::string& filePath)
{
  DecodedAudio audio = AudioDecoder::DecodeFile(filePath);

  std::lock_guard<std::mutex> lock(stateMutex_);
  StemPlaybackState& state = stems_[stemIdentifier];
  state.audio = std::move(audio);
  state.playbackFrame = 0;
  state.isPlaying = false;
  state.isComplete = false;

  if (state.audio.frameCount == 0) {
    LOG_WARNING(
        "Stem '{}' has no usable audio (failed to load '{}'); it will play silently",
        stemIdentifier, filePath);
  }
  else {
    LOG_INFO(
        "Stem '{}' loaded from '{}' ({} frames, {} channel(s))", stemIdentifier, filePath,
        state.audio.frameCount, state.audio.channelCount);
  }
}

void AudioBackend::PlayStem(const std::string& stemIdentifier, bool shouldLoop)
{
  std::lock_guard<std::mutex> lock(stateMutex_);
  auto it = stems_.find(stemIdentifier);
  if (it == stems_.end()) {
    LOG_ERROR("Cannot play unknown stem '{}'", stemIdentifier);
    return;
  }
  it->second.playbackFrame = 0;
  it->second.isLooping = shouldLoop;
  it->second.isPlaying = true;
  it->second.isComplete = false;
  LOG_INFO("Playing stem '{}' (loop={})", stemIdentifier, shouldLoop);
}

void AudioBackend::PauseStem(const std::string& stemIdentifier)
{
  std::lock_guard<std::mutex> lock(stateMutex_);
  auto it = stems_.find(stemIdentifier);
  if (it == stems_.end()) {
    LOG_ERROR("Cannot pause unknown stem '{}'", stemIdentifier);
    return;
  }
  it->second.isPlaying = false;
  LOG_INFO("Paused stem '{}'", stemIdentifier);
}

void AudioBackend::StopStem(const std::string& stemIdentifier)
{
  std::lock_guard<std::mutex> lock(stateMutex_);
  auto it = stems_.find(stemIdentifier);
  if (it == stems_.end()) {
    LOG_ERROR("Cannot stop unknown stem '{}'", stemIdentifier);
    return;
  }
  it->second.isPlaying = false;
  it->second.playbackFrame = 0;
  LOG_INFO("Stopped stem '{}'", stemIdentifier);
}

void AudioBackend::SetStemVolume(const std::string& stemIdentifier, float normalizedVolume)
{
  std::lock_guard<std::mutex> lock(stateMutex_);
  auto it = stems_.find(stemIdentifier);
  if (it == stems_.end()) {
    LOG_ERROR("Cannot set volume on unknown stem '{}'", stemIdentifier);
    return;
  }

  float clamped = std::clamp(normalizedVolume, 0.0f, 1.0f);
  if (clamped != normalizedVolume) {
    LOG_WARNING(
        "Stem '{}' volume {} out of range [0,1], clamped to {}", stemIdentifier, normalizedVolume,
        clamped);
  }
  it->second.currentVolume = clamped;
}

void AudioBackend::ApplyEffect(const std::string& stemIdentifier, const AudioEffect& effect)
{
  std::lock_guard<std::mutex> lock(stateMutex_);
  auto it = stems_.find(stemIdentifier);
  if (it == stems_.end()) {
    LOG_ERROR("Cannot apply effect to unknown stem '{}'", stemIdentifier);
    return;
  }

  // Prototype supports one active effect per stem -- this replaces any
  // previously-active effect and its filter/delay-line state.
  EffectState& state = it->second.effect;
  state = EffectState{};
  state.active = true;
  state.type = effect.effectType;
  state.parameters = effect.parameters;
  if (state.type == AudioEffectType::Reverb) {
    InitializeReverb(state);
  }

  LOG_INFO("Applied effect to stem '{}'", stemIdentifier);
}

void AudioBackend::Update([[maybe_unused]] float deltaTimeSeconds)
{
  // Mixing runs continuously on the platform audio thread via RenderAudio;
  // there is no per-tick work required on the caller's thread today. Kept
  // as part of the interface for future non-realtime bookkeeping.
}

void AudioBackend::RenderAudio(float* outputBuffer, std::uint32_t frameCount)
{
  std::lock_guard<std::mutex> lock(stateMutex_);
  std::fill(outputBuffer, outputBuffer + frameCount * 2, 0.0f);

  for (auto& [identifier, stem] : stems_) {
    if (!stem.isPlaying || stem.audio.frameCount == 0) {
      continue;
    }

    std::uint32_t sourceChannels = stem.audio.channelCount;

    for (std::uint32_t i = 0; i < frameCount; ++i) {
      if (stem.playbackFrame >= stem.audio.frameCount) {
        if (stem.isLooping) {
          stem.playbackFrame = 0;
        }
        else {
          stem.isPlaying = false;
          stem.isComplete = true;
          break;
        }
      }

      std::uint64_t base = stem.playbackFrame * sourceChannels;
      float left;
      float right;
      if (sourceChannels == 1) {
        left = right = stem.audio.interleavedSamples[base];
      }
      else {
        left = stem.audio.interleavedSamples[base];
        right = stem.audio.interleavedSamples[base + 1];
      }

      if (stem.effect.active) {
        left = ApplyEffectSample(stem.effect, 0, left);
        right = ApplyEffectSample(stem.effect, 1, right);
      }

      left *= stem.currentVolume;
      right *= stem.currentVolume;

      outputBuffer[i * 2 + 0] += left;
      outputBuffer[i * 2 + 1] += right;

      ++stem.playbackFrame;
    }
  }

  for (std::uint32_t i = 0; i < frameCount * 2; ++i) {
    outputBuffer[i] = std::clamp(outputBuffer[i], -1.0f, 1.0f);
  }
}

} // namespace Cinder
