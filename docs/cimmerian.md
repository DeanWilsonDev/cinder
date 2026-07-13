# Cimmerian — Fixes Required for Cinder

> **Scope:** investigated while scaffolding Cinder (`docs/cinder_spec.md`), which uses
> `DeanWilsonDev/cimmerian` as its testing framework per the spec's dependency table.
> Investigated against Cimmerian `main` (cloned 2026-07-13).
> **Status:** item 1 (no epsilon-tolerant float assertion) is a nice-to-have, worked around inline
> (see §2). Item 2b (`IT` isn't actually variadic like `TEST`, despite the README documenting them
> as interchangeable aliases) is a real, reproduced compile failure — it broke Cinder's own test
> suite the first time a test body contained a multi-element container literal — worked around by
> using `TEST(...)` everywhere instead of `IT(...)`.
> **Not in scope:** Cimmerian's diff-output formatting, performance timing, or hook ordering — all
> work as documented and are unaffected by this gap.

---

## 0. Context

**Symptom:** `docs/cinder_spec.md`'s `test-stem-fading.cpp` requirement reads:

> After `Update(1.0f)` (half the fade duration), the stem's interpolated volume is **approximately**
> `0.4f`

The word "approximately" is load-bearing: fade interpolation is `currentVolume = start + (target -
start) * (elapsed / duration)`, computed in `float`. Repeated accumulation across many `Update`
calls (Cinder's driver ticks at 60 Hz, so a multi-second fade accumulates dozens of float additions
to `fadeElapsedSeconds`) will not reliably land on the exact bit pattern of a hand-written literal
like `0.4f`.

Root cause, in `cimmerian/include/cimmerian/test-assertions.hpp:155-166`:

```cpp
template <Formattable A, Formattable B>
  requires(!Iterable<A> && !Iterable<B>)
void assert_equal_impl(const A& expectedValue, const B& receivedValue, const char* file, int line)
{
  if (!(expectedValue == receivedValue)) {
    fail(file, line, "Values differ:");
    ...
  }
}
```

`ASSERT_EQUAL` (`cimmerian/include/cimmerian/test.hpp:115-118`) always resolves to this
exact-equality path for scalar types — there is no overload, macro, or parameter anywhere in
`test-assertions.hpp` or `test.hpp` that accepts a tolerance. Grepping the whole `include/`
tree for `epsilon`, `tolerance`, `abs`, or `near` (case-insensitive) turns up nothing.

This can't be worked around cleanly in Cinder's own test code today: the only option is to
hand-roll `ASSERT_TRUE(std::abs(a - b) < kEpsilon)` at every float-comparison call site, which
throws away Cimmerian's actual diff output (`+ 0.4` / `- 0.400013`) in favour of a bare boolean
`ASSERT_TRUE failed: std::abs(a - b) < kEpsilon` with no values printed on failure — exactly the
kind of unhelpful failure message `ASSERT_EQUAL`'s diff output exists to avoid.

---

## 1. REQUIRED — an epsilon-tolerant float/double equality assertion

### Proposed API

```cpp
// cimmerian/include/cimmerian/test-assertions.hpp
namespace Cimmerian::Assertions {

void assert_near_impl(
    double expectedValue, double receivedValue, double epsilon,
    const char* file, int line
);

} // namespace Cimmerian::Assertions
```

```cpp
// cimmerian/include/cimmerian/test.hpp
#define ASSERT_NEAR(a, b, epsilon)                                                                \
  do {                                                                                             \
    ::Cimmerian::Assertions::assert_near_impl((a), (b), (epsilon), __FILE__, __LINE__);          \
  } while (0)
```

`assert_near_impl` should fail when `std::abs(expectedValue - receivedValue) > epsilon`, and on
failure should reuse the existing `OutputDiffToStderr` path so the printed diff shows both actual
values (not just a stringified boolean expression) — matching the failure-message quality
`ASSERT_EQUAL` already provides for exact types.

A `REQUIRE_NEAR` halting variant, mirroring the existing `REQUIRE_EQUAL`, would be a natural
companion but isn't required by Cinder's test suite as currently scoped.

### Required changes elsewhere

- None outside `test-assertions.hpp` / `test.hpp` — this is additive, no existing macro or
  function signature needs to change.

### What unblocks in Cinder

`tests/test-stem-fading.cpp` can write
`ASSERT_NEAR(stem.currentVolume, 0.4f, 0.001f)` and get a real diff on failure, instead of a
hand-rolled `ASSERT_TRUE(std::abs(...) < kEpsilon)` that reports no values when it fails.

---

## 2. Cinder's current workaround

Until this lands, `tests/test-stem-fading.cpp` uses:

```cpp
ASSERT_TRUE(std::abs(stem.currentVolume - 0.4f) < 0.001f);
```

This is functionally correct but loses Cimmerian's diff output on failure (the message is just
`ASSERT_TRUE failed: std::abs(...) < 0.001f`, not `+ 0.4` / `- <actual>`), which makes a failing
fade-interpolation test slower to debug than it needs to be.

---

## 2b. REQUIRED — make `IT` variadic like `TEST`

### Root cause

Reproduced while writing Cinder's own test suite: `cimmerian/include/cimmerian/test.hpp:67-93`
defines

```cpp
#define TEST(testName, ...)                                                                       \
  ...                                                                                              \
        _registry.RegisterTest(                                                                   \
            _group, (testName),                                                                    \
            +[](void* user) {                                                                      \
              (void)user;                                                                          \
              __VA_ARGS__                                                                          \
            },                                                                                     \
        ...

#define IT(testName, BODY) TEST(testName, BODY)
#define IT_FN(testName, FN) TEST_FN(testName, FN)
```

`TEST` is variadic (`...`/`__VA_ARGS__`), so any top-level comma inside its body argument — e.g. a
multi-element `std::vector<T>` initializer list, a structured binding, anything the C preprocessor
doesn't treat as parenthesis-protected — is fine, because it just becomes more of `__VA_ARGS__`.
`IT`, despite the README documenting it as "an alias — use whichever reads better," is a **fixed
two-parameter macro**. The same body that compiles fine under `TEST(...)` fails under `IT(...)`
with `error: too many arguments provided to function-like macro invocation`, because the
preprocessor is now trying to match a 2-parameter macro against 3+ comma-separated pieces:

```cpp
IT("queues correct target volumes when transitioning to Chapter1", {
  std::vector<StemFileEntry> stemsToLoad = {
      {"ambience", "ambience.wav"}, {"melody", "melody.wav"}, {"harmony", "harmony.wav"},
      {"percussion", "percussion.wav"}};
  ...
});
```
error: too many arguments provided to function-like macro invocation
note: macro 'IT' defined here
```

This isn't a rare pattern — any test body that builds a container literal with more than one
element (a very common thing to do in a test) hits it. `IT_FN`/`TEST_FN` don't have this problem
since they take a function pointer, not an inline body.

### Proposed fix

```cpp
#define IT(testName, ...) TEST(testName, __VA_ARGS__)
```

One-line change, matches `TEST`'s existing variadic signature exactly.

### Cinder's current workaround

Cinder's test suite uses `TEST(...)` everywhere instead of `IT(...)`, purely to route around this;
there's no functional reason to prefer one name over the other otherwise.

---

## 3. Explicitly not requested

- **Changes to `ASSERT_EQUAL`'s existing exact-match behaviour** — exact equality is correct and
  expected for the state-transition and effect-parameter tests, which compare literals against
  literals with no arithmetic in between. Only a *new*, opt-in tolerant assertion is requested.
- **Container-level near-equality** (e.g. comparing two `std::vector<float>` element-wise within
  tolerance) — Cinder's test suite has no such case; scalar `ASSERT_NEAR` is sufficient.
- **A global/default epsilon** — an explicit `epsilon` argument per call site is preferable and
  matches how most C++ testing frameworks (Catch2's `Approx`, GoogleTest's
  `EXPECT_NEAR`) expose this.

---

## 4. What unblocks when this lands

`tests/test-stem-fading.cpp`'s float-interpolation assertions get Cimmerian's real diff output on
failure instead of a bare boolean, matching the debugging experience the rest of the suite already
has via `ASSERT_EQUAL`.
