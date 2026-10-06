# Cinder

Cinder is a standalone C++26 prototype of the audio subsystem planned for the **Umbra Engine**. It
exercises every major design surface of that subsystem — layered music stems, named audio states,
per-stem effects, and one-shot sound effects — so the findings can inform whether Umbra should ship
a custom audio backend or adopt an off-the-shelf library like miniaudio.

This is **not** wired into Umbra. It's a self-contained CMake project with its own tests and a
scripted driver program that exercises the whole system end-to-end.

Full design rationale and requirements live in [`docs/cinder_spec.md`](docs/cinder_spec.md).

## What it does

- **Stems** — named, looping audio streams (e.g. `ambience`, `melody`, `harmony`) that can be
  faded in and out independently.
- **Audio states** — named snapshots of target stem volumes (e.g. `Chapter1`, `Chapter2`), loaded
  from [`data/audio-states.json`](data/audio-states.json). Transitioning to a state fades every
  listed stem to its target volume and fades any currently-playing stem not in the new state to
  silence.
- **Effects** — a low-pass filter, high-pass filter, or Schroeder reverb can be applied to any
  stem.
- **Sound effects** — one-shot, non-looping playback triggered by identifier.

Audio output goes straight to the platform API with no middleware: Core Audio on macOS, ALSA on
Linux. WAV and MP3 decoding are both implemented from scratch. Logging goes through
[Firefly](https://github.com/DeanWilsonDev/firefly) and tests use
[Cimmerian](https://github.com/DeanWilsonDev/cimmerian); JSON parsing uses
[Amanuensis](https://github.com/DeanWilsonDev/amanuensis). All three are fetched automatically by
CMake.

## Requirements

- CMake 3.30+
- A C++26 compiler: GCC 14+ or Clang 18+ (or a recent AppleClang)
- macOS or Linux (no other platforms are supported)
- On Linux: ALSA development headers (`libasound2-dev` on Debian/Ubuntu)

## Building

```sh
cmake -B build -S .
cmake --build build
```

CMake fetches Firefly, Cimmerian, and Amanuensis automatically via `FetchContent` — no manual
dependency setup is needed.

## Running the tests

```sh
ctest --test-dir build
```

The four suites under [`tests/`](tests/) cover stem fading, audio state transitions, SFX playback,
and effect application. Tests never play actual audio — they only assert on state, volume, and
effect parameters.

## Running the driver

The driver ([`driver/main.cpp`](driver/main.cpp)) runs a scripted 11-step sequence — state
transitions, stem fades, effect application, and SFX playback — with no user input required. It
logs its progress to stdout and to `cinder-driver.log`.

```sh
./build/driver/cinder_driver
```

To hear anything, supply your own audio files (not included in this repo) under `data/stems/` and
`data/sfx/`:

| Directory     | Expected files                                                                          |
|---------------|------------------------------------------------------------------------------------------|
| `data/stems/` | `ambience`, `melody`, `harmony`, `percussion`, `vocals` (`.wav` or `.mp3`)                |
| `data/sfx/`   | `ui-click`, `door-open` (`.wav` or `.mp3`)                                                |

Missing files are logged as warnings, not fatal errors — the driver still runs through its
sequence without audible output.

## Layout

```
cinder/
  CMakeLists.txt
  data/
    audio-states.json   # audio state definitions
    stems/               # your WAV/MP3 stem files (not checked in)
    sfx/                 # your WAV/MP3 sound effect files (not checked in)
  include/cinder/         # public headers (IAudioBackend, ICinderMixer, Stem, AudioState, AudioEffect)
  src/
    backend/              # platform audio output + WAV/MP3 decoding (internal)
    mixer/                # CinderMixer — the concrete ICinderMixer implementation
    loader/                # loads data/audio-states.json via Amanuensis
  tests/                  # Cimmerian test suites
  driver/                 # scripted driver program (driver/main.cpp)
  docs/
    cinder_spec.md         # full prototype specification
    firefly.md              # upstream issues found in Firefly while building Cinder
    cimmerian.md            # upstream issues found in Cimmerian while building Cinder
```

## Scope

Deliberately out of scope for this prototype: DI container integration, spatial audio,
procedural stem generation, non-linear fade curves, stacking multiple effects on one stem, hot
reload of audio state JSON, an audio state editor, and extraction as a shared library. See
[`docs/cinder_spec.md`](docs/cinder_spec.md#what-the-prototype-is-not) for the full list.
