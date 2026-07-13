# Firefly — Fixes Required for Cinder

> **Scope:** investigated while scaffolding Cinder (`docs/cinder_spec.md`), a standalone C++23
> prototype that depends on `DeanWilsonDev/firefly` (logging), `DeanWilsonDev/cimmerian`
> (testing), and `DeanWilsonDev/amanuensis` (JSON) as sibling `FetchContent` dependencies.
> Investigated against Firefly `main` (commit reachable via
> `https://github.com/DeanWilsonDev/firefly`, cloned 2026-07-13).
> **Status:** item 1 is a **blocking build failure** — reproduced with a minimal CMake project,
> not theoretical. Item 2 is a real defect (undefined behaviour) that also breaks Cinder's
> `-Wall -Wextra`, zero-warnings acceptance criterion.
> **Not in scope:** Firefly's file-format sinks (CSV/JSON/NDJSON), log deduplication behaviour,
> and the `.json` sink's "partially implemented" state noted in Firefly's own README — none of
> these block Cinder.

---

## 0. Context

**Symptom:** A CMake project that `FetchContent`s both `firefly` and `amanuensis` — which is
exactly what the Cinder spec requires (Firefly for logging, Amanuensis for JSON, both listed as
top-level dependencies) — fails to configure:

```
CMake Error at build/_deps/amanuensis-src/CMakeLists.txt:23 (add_library):
  add_library cannot create target "amanuensis" because another target with
  the same name already exists.  The existing target is a static library
  created in source directory
  ".../build/_deps/firefly-src/external/amanuensis".
```

Root cause, in `firefly/CMakeLists.txt`:

```cmake
# -------------------------
# Install Dependencies
# -------------------------

add_subdirectory(external/amanuensis)
target_link_libraries(${PROJECT_NAME} PRIVATE amanuensis)
```

Firefly vendors its own copy of Amanuensis as a git submodule (`.gitmodules`:
`external/amanuensis` → `https://github.com/DeanWilsonDev/amanuensis.git`) and unconditionally
`add_subdirectory()`s it. Unlike the two other subdirectory calls in the same file —

```cmake
if(IS_ROOT_PROJECT)
  add_subdirectory(external/cimmerian)
  add_subdirectory(test)
endif()
```

— the `external/amanuensis` call has no `IS_ROOT_PROJECT` guard and no `if(NOT TARGET amanuensis)`
check. It runs every time Firefly's `CMakeLists.txt` is processed, whether Firefly is the root
project or pulled in as a dependency of something else.

This can't be worked around from Cinder's own `CMakeLists.txt` without relying on undocumented,
fragile behaviour: the only way to avoid the collision today is to skip declaring Cinder's own
`amanuensis` `FetchContent_Declare` and instead reuse whatever `amanuensis` target Firefly happens
to define internally, guarded by `if(NOT TARGET amanuensis)`. That makes Cinder's Amanuensis
version an unpinned, invisible side effect of Firefly's submodule pin rather than something Cinder
declares and controls — exactly the kind of coupling `FetchContent` is meant to avoid.

---

## 1. REQUIRED — guard the internal Amanuensis dependency

### Proposed fix

In `firefly/CMakeLists.txt`, guard the existing call the same way the other two internal
subdirectories are already guarded, plus a target-existence check so a sibling project that
depends on Amanuensis directly doesn't collide:

```cmake
if(NOT TARGET amanuensis)
  add_subdirectory(external/amanuensis)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE amanuensis)
```

This is the standard pattern for optional/shared transitive dependencies pulled in via
`FetchContent` or nested `add_subdirectory` — it makes Firefly's own build self-sufficient when
built standalone, but lets it defer to a parent project's `amanuensis` target when one already
exists (regardless of which side declares it first).

### What unblocks in Cinder

Cinder's top-level `CMakeLists.txt` can declare `firefly`, `cimmerian`, and `amanuensis` as three
independent, equally-pinned `FetchContent_Declare` calls — exactly as the spec's dependency table
implies — instead of silently depending on Firefly's internal submodule pin for its JSON library
version.

---

## 2. REQUIRED — make `Sinks::ISink`'s destructor virtual

### Root cause

`firefly/include/firefly/sinks/i-sink.hpp:12-17`:

```cpp
class ISink {
 public:
  ~ISink() = default;

  virtual std::string Format(LogEntry entry) const = 0;
};
```

`ISink` is an abstract base with a pure-virtual method, but its destructor is not `virtual`.
`Logger` (`firefly/include/firefly/logger.hpp:63`) destroys sink instances polymorphically:

```cpp
std::unique_ptr<Sinks::ISink> defaultSink = std::make_unique<Sinks::PlainTextSink>();
```

Destroying a `PlainTextSink` (or `CsvSink`/`JsonSink`/`NdjsonSink`) through
`unique_ptr<ISink>::~unique_ptr` with a non-virtual base destructor is undefined behaviour per the
C++ standard, independent of whether any derived class currently adds member state that would make
the UB "visible" today.

Concretely reproduced: compiling any translation unit that includes `<firefly/log.hpp>` — i.e. any
Cinder file that logs at all — with `-Wall -Wextra` on AppleClang produces, in the *consumer's*
translation unit (not just inside Firefly's own build):

```
.../firefly/include/firefly/logger.hpp:61:47: warning: delete called on 'Firefly::Sinks::ISink'
that is abstract but has non-virtual destructor [-Wdelete-abstract-non-virtual-dtor]
```

The same root cause also surfaces as a second, differently-named diagnostic for each *concrete*
sink type (`PlainTextSink`, `CsvSink`, `JsonSink`, `NdjsonSink`), since each inherits `ISink`'s
non-virtual destructor:

```
.../firefly/include/firefly/logger.hpp:61:52: warning: delete called on non-final
'Firefly::Sinks::PlainTextSink' that has virtual functions but non-virtual destructor
[-Wdelete-non-abstract-non-virtual-dtor]
```

Both trace to the same one-word fix below and both directly break Cinder's acceptance criterion of
building with `-Wall -Wextra` and zero warnings, in every single file that uses Firefly logging —
which per the spec is nearly every file in the project. Cinder's `CMakeLists.txt` currently
suppresses both diagnostics by name as a narrow, documented workaround (see the
`CINDER_WARNING_FLAGS` block).

### Proposed fix

```cpp
class ISink {
 public:
  virtual ~ISink() = default;

  virtual std::string Format(LogEntry entry) const = 0;
};
```

### What unblocks in Cinder

Removes a warning (and latent UB) from every Cinder translation unit that includes
`<firefly/log.hpp>`, which is required for Cinder's "no warnings at `-Wall -Wextra`" acceptance
criterion to be achievable at all while using Firefly as specified.

---

## 3. Explicitly not requested

- **A `find_package`-based option for Amanuensis** — the existing `FetchContent`/submodule
  approach is fine; item 1 only asks that it not collide with a sibling copy.
- **Changes to Firefly's `.json` sink** (README already documents it as "partially implemented,
  currently outputs CSV-style text") — Cinder does not use file-based sinks at all, only the
  console + default-extension file path, so this doesn't block Cinder.
- **Thread-safety guarantees for `LogRegistry`/`Logger`** — Cinder never logs from a real-time
  audio callback thread (all Firefly calls happen on the main/game thread during setup, state
  transitions, and effect application), so this isn't currently a blocking concern, just noted in
  case a future consumer logs from a worker thread.
- **A `RegisterLogger` overload for "file + explicit console on/off"** — the current constructor
  already logs to both stdout and the file simultaneously (confirmed by reading
  `Logger::LogImpl`), which is what Cinder's driver needs; no API change required there.

---

## 4. What unblocks when this lands

With both fixes, Cinder's top-level `CMakeLists.txt` can `FetchContent` Firefly, Cimmerian, and
Amanuensis as three independent, explicitly-pinned dependencies — matching the dependency table in
`docs/cinder_spec.md` — and can build cleanly at `-Wall -Wextra` with zero warnings, satisfying two
of the spec's acceptance criteria directly. Until then, Cinder's `CMakeLists.txt` works around item
1 by skipping its own `amanuensis` `FetchContent_MakeAvailable` call behind `if(NOT TARGET
amanuensis)` and reusing the target Firefly defines internally (see the comment at the relevant
line in `CMakeLists.txt`); item 2 has no practical workaround from Cinder's side and is left as a
suppressed/accepted warning source until fixed upstream.
