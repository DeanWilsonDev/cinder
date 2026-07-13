# Cinder — Prototype Specification

## Purpose

This document is a complete specification for a standalone C++ prototype of **Cinder**, the audio
subsystem planned for the Umbra Engine. The goal is a working proof of concept that exercises every
major design surface — stem management, state transitions, per-stem effects, and one-shot SFX —
so that findings can inform the decision of whether to build a custom audio backend or adopt
miniaudio for the full implementation.

This prototype is **not** wired into Umbra. It is a self-contained CMake project.

---

## Project Name

`cinder`

---

## Repository Layout

```
cinder/
  CMakeLists.txt
  data/
    audio-states.json         # audio state definitions
    stems/                    # WAV or MP3 stem files (user-provided)
    sfx/                      # WAV or MP3 sound effect files (user-provided)
  include/
    cinder/
      i-audio-backend.hpp
      i-cinder-mixer.hpp
      stem.hpp
      audio-state.hpp
      audio-effect.hpp
  src/
    backend/
      audio-backend.hpp       # concrete backend, internal only
      audio-backend.cpp
      audio-decoder.hpp       # WAV + MP3 decoding, internal only
      audio-decoder.cpp
    mixer/
      cinder-mixer.hpp
      cinder-mixer.cpp
    loader/
      audio-state-loader.hpp
      audio-state-loader.cpp
  tests/
    CMakeLists.txt
    test-stem-fading.cpp
    test-state-transitions.cpp
    test-sfx-playback.cpp
    test-effect-application.cpp
  driver/
    CMakeLists.txt
    main.cpp
```

---

## Build System

CMake 3.21+, C++23. Dependencies pulled via `FetchContent` from the following GitHub repositories
under the `DeanWilsonDev` organisation. All repository names are lowercase:

| Library     | Repository                              | Role              |
|-------------|------------------------------------------|-------------------|
| Firefly     | `DeanWilsonDev/firefly`                 | Logging           |
| Cimmerian   | `DeanWilsonDev/cimmerian`               | Testing           |
| Amanuensis  | `DeanWilsonDev/amanuensis`              | JSON parsing      |

No other third-party dependencies. The audio backend must be implemented from scratch using only
platform APIs (see Backend section below).

---

## Platform Support

The prototype must build and run on **macOS** and **Linux** without conditional game logic.
Platform-specific audio output code is isolated behind `IAudioBackend` and selected at compile time
via CMake. The rest of the codebase is platform-agnostic C++23.

---

## Dependencies — Logging (Firefly)

All log output goes through Firefly using its macro-based API. Suggested log points:

- Backend initialisation and teardown
- Each stem loaded from disk
- Each audio state loaded from JSON
- State transitions (name, duration)
- Individual stem fade start and completion
- SFX playback
- Effect application
- Any error condition (file not found, decode failure, unknown state name)

Firefly must not appear in any public header. Initialise it once in `main.cpp` before constructing
any Cinder objects.

---

## Dependencies — Testing (Cimmerian)

Tests live in `tests/` and are built as a separate CMake target. Each test file covers one
subsystem. Use Cimmerian's registration and assertion API throughout. Tests must not play audio —
they operate on state, volume values, fade progress, and effect parameters only.

---

## Dependencies — JSON (Amanuensis)

Audio state configuration is loaded from `data/audio-states.json` at startup using Amanuensis.
No other JSON files are required for the prototype. Amanuensis must not appear in any public header.

---

## Audio File Formats

The backend must support **WAV** and **MP3**. Decoding is implemented from scratch inside
`audio-decoder.cpp` — no third-party decode library. The decoder is internal and never exposed
through a public header.

**WAV decoding requirements:**
- PCM format, 16-bit or 32-bit samples
- Mono and stereo
- Any standard sample rate

**MP3 decoding requirements:**
- MPEG-1 Layer III
- Mono and stereo
- Any standard bit rate

Decoded audio is stored as 32-bit floating point PCM in memory. All mixing operates on float
samples.

---

## Platform Audio Output

Audio output is written directly to the platform audio API. No audio middleware.

- **macOS**: Core Audio (`AudioUnit` or `AudioQueue`)
- **Linux**: ALSA (`libasound`)

The backend selects the platform implementation at compile time. All platform code is confined to
`audio-backend.cpp`. The `IAudioBackend` interface is identical on both platforms.

---

## Architecture

### `IAudioBackend`

Abstract interface for all low-level audio operations. Defined in
`include/cinder/i-audio-backend.hpp`.

```cpp
namespace Cinder {

class IAudioBackend {
public:
  virtual ~IAudioBackend() = default;

  virtual void LoadAudioFile(
    const std::string& stemIdentifier,
    const std::string& filePath) = 0;

  virtual void PlayStem(
    const std::string& stemIdentifier,
    bool shouldLoop) = 0;

  virtual void PauseStem(const std::string& stemIdentifier) = 0;

  virtual void StopStem(const std::string& stemIdentifier) = 0;

  virtual void SetStemVolume(
    const std::string& stemIdentifier,
    float normalizedVolume) = 0;

  virtual void ApplyEffect(
    const std::string& stemIdentifier,
    const AudioEffect& effect) = 0;

  virtual void Update(float deltaTimeSeconds) = 0;
};

} // namespace Cinder
```

`normalizedVolume` is always in the range `[0.0f, 1.0f]`. The backend clamps any out-of-range
value and logs a warning via Firefly.

---

### `Stem`

Plain data type representing one named audio stream. Defined in `include/cinder/stem.hpp`.

```cpp
namespace Cinder {

struct Stem {
  std::string identifier;
  float currentVolume      = 0.0f;
  float fadeTargetVolume   = 0.0f;
  float fadeDurationSeconds = 0.0f;
  float fadeElapsedSeconds  = 0.0f;
  bool isFading            = false;
};

} // namespace Cinder
```

Stems are owned by `CinderMixer`. Game code never holds a `Stem` directly.

---

### `AudioEffect`

Describes a single effect applied to one stem. Defined in `include/cinder/audio-effect.hpp`.

```cpp
namespace Cinder {

enum class AudioEffectType {
  LowPassFilter,
  HighPassFilter,
  Reverb
};

struct AudioEffectParameters {
  float cutoffFrequencyHz = 0.0f;  // LowPass, HighPass
  float reverbDecaySeconds = 0.0f; // Reverb
  float reverbMix = 0.0f;          // Reverb — dry/wet, [0.0, 1.0]
};

struct AudioEffect {
  AudioEffectType effectType;
  AudioEffectParameters parameters;
};

} // namespace Cinder
```

---

### `AudioState`

Named snapshot of target stem volumes, loaded from JSON. Defined in
`include/cinder/audio-state.hpp`.

```cpp
namespace Cinder {

struct AudioState {
  std::string stateName;
  std::unordered_map<std::string, float> stemTargetVolumes;
};

} // namespace Cinder
```

---

### `ICinderMixer`

The public interface for all game-facing audio control. Defined in
`include/cinder/i-cinder-mixer.hpp`.

```cpp
namespace Cinder {

class ICinderMixer {
public:
  virtual ~ICinderMixer() = default;

  // Macro layer — state transitions
  virtual void TransitionToState(
    const std::string& stateName,
    float transitionDurationSeconds) = 0;

  // Micro layer — direct stem control
  virtual void SetStemVolume(
    const std::string& stemIdentifier,
    float normalizedVolume) = 0;

  virtual void FadeStem(
    const std::string& stemIdentifier,
    float targetNormalizedVolume,
    float durationSeconds) = 0;

  virtual void ApplyEffectToStem(
    const std::string& stemIdentifier,
    const AudioEffect& effect) = 0;

  // Sound effects
  virtual void PlaySoundEffect(const std::string& effectIdentifier) = 0;

  // Per-frame update — call once per game loop tick
  virtual void Update(float deltaTimeSeconds) = 0;
};

} // namespace Cinder
```

---

### `CinderMixer`

Concrete implementation of `ICinderMixer`. Owns all `Stem` instances and drives per-frame fade
updates. Defined in `src/mixer/cinder-mixer.hpp`, implemented in `src/mixer/cinder-mixer.cpp`.

Responsibilities:

- Register stems at startup by loading each file path through `IAudioBackend::LoadAudioFile`
- Begin playing all stems in a looping, silent state at startup (volume `0.0f`)
- On `TransitionToState`: look up the named `AudioState`, call `FadeStem` for every stem listed
  in the state targeting its configured volume; any stem currently playing that is **not** listed
  in the new state is faded to `0.0f` over the same duration
- On `Update`: advance all in-progress fades by `deltaTime`, compute the interpolated volume for
  each fading stem, call `IAudioBackend::SetStemVolume`, mark fades complete when elapsed >= duration
- On `PlaySoundEffect`: call `IAudioBackend::PlayStem` with `shouldLoop = false`
- Log all state transitions and fade completions through Firefly

Fade interpolation uses **linear** interpolation for the prototype. Ease-in/ease-out is a future
consideration.

---

### `AudioStateLoader`

Loads `data/audio-states.json` using Amanuensis at startup and returns a
`std::vector<AudioState>`. Defined in `src/loader/audio-state-loader.hpp`.

Amanuensis must not appear in any public header — `AudioStateLoader` is an internal type used
only during initialisation.

---

## JSON Configuration

### `data/audio-states.json`

```json
{
  "states": [
    {
      "name": "Chapter1",
      "stems": {
        "ambience": 0.5,
        "melody": 0.8
      }
    },
    {
      "name": "Chapter2",
      "stems": {
        "ambience": 0.5,
        "melody": 0.8,
        "harmony": 0.7,
        "percussion": 0.6,
        "vocals": 0.7
      }
    },
    {
      "name": "PostGame_Run2",
      "stems": {
        "ambience": 0.3,
        "melody": 0.4,
        "harmony": 0.2
      }
    }
  ]
}
```

All stems not listed in a state are faded to silence (`0.0f`). The loader must log an error and
return an empty vector if the file is missing or malformed.

---

## Audio Backend — Implementation Requirements

The backend (`audio-backend.cpp`) is the only file that contains platform-specific code. It must:

1. Open a low-latency stereo output stream at 44100 Hz, 32-bit float
2. Maintain an internal map of stem identifier → decoded audio buffer + playback state
3. Mix all active stems in the audio callback by summing float samples, applying per-stem volume
4. Apply effects in the audio callback before writing to the output buffer:
   - **Low pass filter**: simple single-pole IIR filter — sufficient for prototype evaluation
   - **High pass filter**: single-pole IIR filter
   - **Reverb**: Schroeder reverb — four comb filters summed into two allpass filters
5. Clamp the final mixed output to `[-1.0f, 1.0f]` before writing to hardware
6. Support looping stems (restarts from the beginning of the buffer when end is reached)
7. Support non-looping one-shot playback (stops and marks complete when end is reached)

The Schroeder reverb does not need to be tunable at this stage beyond the `reverbDecaySeconds` and
`reverbMix` parameters on `AudioEffectParameters`. These are enough to evaluate whether the
reverb approach is suitable.

---

## Tests

Four test files, each a separate Cimmerian test suite.

### `test-stem-fading.cpp`

- A stem starts at volume `0.0f`
- After `FadeStem("melody", 0.8f, 2.0f)`, `isFading` is true and `fadeTargetVolume` is `0.8f`
- After `Update(1.0f)` (half the fade duration), the stem's interpolated volume is approximately `0.4f`
- After `Update(1.0f)` again, the stem volume is `0.8f` and `isFading` is false

### `test-state-transitions.cpp`

- Load two audio states from a hardcoded in-memory configuration (do not read from disk in tests)
- `Chapter1` has stems `ambience: 0.5, melody: 0.8`
- `Chapter2` adds `harmony: 0.7` and introduces `percussion: 0.6`
- Transition to `Chapter1`, verify fades are queued with correct target volumes
- Transition to `Chapter2`, verify `harmony` and `percussion` fades are queued
- Verify that any stem present in `Chapter1` but absent from `Chapter2` is faded to `0.0f`

### `test-sfx-playback.cpp`

- Load a sound effect identifier
- Call `PlaySoundEffect`
- Verify the backend received a `PlayStem` call with `shouldLoop = false`
- Verify that SFX playback does not create a stem entry in the mixer's stem map

### `test-effect-application.cpp`

- Apply a low pass filter to a stem with `cutoffFrequencyHz = 800.0f`
- Verify the effect parameters are stored and passed correctly to the backend
- Apply a reverb effect with `reverbDecaySeconds = 1.5f, reverbMix = 0.3f`
- Verify parameters are passed correctly
- Apply a high pass filter and verify parameters
- Apply two effects to the same stem in sequence and verify the last write wins (prototype does not
  stack effects per stem — one active effect per stem at a time is sufficient for evaluation)

---

## Driver Program

The driver program (`driver/main.cpp`) exercises all Cinder functionality in a scripted sequence.
It does not require user input. It prints progress to the terminal via Firefly log output.

### Startup sequence

1. Initialise Firefly logger to stdout and a log file (`cinder-driver.log`)
2. Load `data/audio-states.json` via `AudioStateLoader`
3. Construct the platform `AudioBackend`
4. Load stems from `data/stems/` — the driver hardcodes the following stem identifiers and
   expects matching files in that directory:
   - `ambience` → `ambience.wav` or `ambience.mp3`
   - `melody` → `melody.wav` or `melody.mp3`
   - `harmony` → `harmony.wav` or `harmony.mp3`
   - `percussion` → `percussion.wav` or `percussion.mp3`
   - `vocals` → `vocals.wav` or `vocals.mp3`
5. Load sound effects from `data/sfx/`:
   - `ui_click` → `ui-click.wav` or `ui-click.mp3`
   - `door_open` → `door-open.wav` or `door-open.mp3`
6. Construct `CinderMixer` and begin all stems looping silently
7. Log startup complete

If any stem or SFX file is missing, log a warning and continue — missing files should not crash
the driver.

### Scripted sequence

The driver runs the following sequence with `std::this_thread::sleep_for` pauses between steps.
Each step logs what it is about to do before doing it.

```
Step 1  — Transition to Chapter1 over 3 seconds
          Wait 5 seconds (stems audible at Chapter1 volumes)

Step 2  — Apply low pass filter to "ambience" at 600 Hz
          Wait 2 seconds

Step 3  — Transition to Chapter2 over 4 seconds
          Wait 6 seconds (full Chapter2 mix)

Step 4  — FadeStem "melody" to 0.2f over 2 seconds (micro layer during active state)
          Wait 3 seconds

Step 5  — PlaySoundEffect "ui_click"
          Wait 1 second

Step 6  — PlaySoundEffect "door_open"
          Wait 1 second

Step 7  — Apply reverb to "harmony" (decay 1.2s, mix 0.25)
          Wait 3 seconds

Step 8  — Transition to PostGame_Run2 over 5 seconds
          Wait 7 seconds (stripped mix)

Step 9  — Apply high pass filter to "ambience" at 400 Hz
          Wait 3 seconds

Step 10 — FadeStem all remaining audible stems to 0.0f over 3 seconds
           Wait 4 seconds

Step 11 — Log sequence complete, exit
```

The driver's game loop runs at a fixed 60 Hz tick rate, calling `CinderMixer::Update` with a
`deltaTime` of `1.0f / 60.0f` each tick, sleeping the remainder of each 16ms frame.

---

## What the Prototype Is Not

The following are explicitly out of scope and should not be implemented:

- DI container integration
- Spatial audio
- Dynamic/procedural stem generation
- Ease-in/ease-out fade curves
- Multiple simultaneous effects per stem
- Hot reload of audio state JSON
- Audio state editor or any UI
- Extraction as a shared library

These are documented here so that Claude does not gold-plate the prototype with features that are
not being evaluated.

---

## Acceptance Criteria

The prototype is considered complete when:

- [ ] The project builds on macOS and Linux with no warnings at `-Wall -Wextra`
- [ ] All four Cimmerian test suites pass
- [ ] The driver program runs to completion without crashing
- [ ] All ten scripted steps produce audible output (when audio files are present)
- [ ] All transitions and fade operations are logged clearly via Firefly
- [ ] No third-party audio library appears anywhere in the dependency graph
