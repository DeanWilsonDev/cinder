---
name: feature-request
description: Write up a capability gap or feature idea found while working on Pharos as a new doc in docs/, following this repo's established requirements-doc house style. Use when a gap traces to Penumbra (framework), Prism (the analysis-data generator), or is a Pharos-side feature buildable in application code. Triggers on "write this up as a feature request", "file a requirements doc for this", "this looks like a Penumbra gap", "spec this out", "document this gap before we move on".
---

# Feature Request / Requirements Doc

Pharos already has six docs in `docs/` built to one house style:
`penumbra_requirements.md`, `penumbra_dpi_requirements.md`,
`penumbra_theming_requirements.md`, `penumbra_text_blur_requirements.md`,
`pharos_feature_requests.md`, `pharos_tree_lens_feature_request.md`. This
skill produces a new one that matches them — not a generic "feature idea"
memo. Read one or two of the existing docs above before writing if you
haven't seen this repo's style recently; the shape matters more than any
template below.

## 0. Figure out who the ask is actually directed at

Before writing anything, classify the gap. This determines the filename,
the tone, and — critically — whether you stop after writing or keep going:

| Gap traces to... | File goes to | After writing, you... |
|---|---|---|
| Penumbra has no API/widget for this | `docs/penumbra_<topic>_requirements.md` | **Stop.** Penumbra is developed by the same person in the sibling `penumbra-proto` repo; they extend it, then tell you to continue. Do not implement the Penumbra side yourself. |
| Buildable entirely in Pharos's own application code | `docs/pharos_<topic>_feature_request.md` | Stop and hand back the doc *unless the user explicitly asks you to implement it* — then go ahead in Pharos's own source, same as any other task. |
| Prism (the sibling data-generation tool) emits/omits something | Don't write a "Prism bug" doc on reflex. | **Ask the user first** (`AskUserQuestion`) whether the behavior is actually a bug, before assuming it — see step 3. It may be intentional (cross-language compatibility, a deliberate simplification, etc.), in which case the real fix is usually a Pharos-side feature request instead (read `docs/pharos_tree_lens_feature_request.md` for a worked example of exactly this pivot: Prism's `physical_parent` collapsing to file-per-node turned out intentional, so the doc proposed a Pharos lens toggle rather than a Prism patch). |

Also check, before concluding it's a framework gap: **is this actually
buildable today via composition** (subclassing `Box`, a small
`FixedLeadingStrip`-style helper, retained-mode mutation)? Most of Pharos's
own panels exist because the answer was yes. Only write a `penumbra_*` doc
for a genuine dead end.

## 1. Ground every claim in real source — never guess

Every existing doc in this repo cites exact `file.cpp:line` locations and
quotes real code, not a paraphrase. Before writing a single section:

- Read the actual Pharos source that surfaces the symptom.
- If the gap might be Penumbra's, read `penumbra-proto`'s source directly
  (it's a sibling checkout — same machine, same way this repo's own
  investigations found `Box::UpdateInteractionState`'s reverse-iteration
  short-circuit, `ContentRectFrom`, etc.). Quote the actual method, not what
  you assume it does.
- If the gap might be Prism's, read `prism`'s source the same way (e.g.
  `graph-builder.cpp`, `metrics-engine.cpp`, `parser.cpp`) — don't infer
  from the JSON output alone; find the line that produces the behavior.
- A proposed API's shape must match the real codebase's existing
  conventions (naming, `Point`/`Rect` vs raw floats, `Color` not
  `SDL_Color`, etc.) — copy the pattern of a nearby real class, don't invent
  a new style.

If you can't pin the root cause to a specific line, say so explicitly in
the doc rather than writing a plausible-sounding guess.

## 2. Each investigation gets its own file

Never append a newly-found gap to an existing, previously-closed doc —
even one that covers a related area. `pharos_feature_requests.md`'s own
item 7 explicitly flagged the tree-lens idea as a "nice-to-have" and it
still got its own new file (`pharos_tree_lens_feature_request.md`) once it
became a real investigation, rather than being expanded in place. Pick a
short, specific topic slug for the filename (`_dpi_`, `_text_blur_`,
`_tree_lens_`, not `_misc_` or `_more_`).

## 3. Section shape

Both doc flavors (`penumbra_*_requirements.md` and
`pharos_*_feature_request.md`) share this skeleton. Match whichever
existing doc is the closer analog for tone (a *blocking framework gap* reads
differently from a *nice-to-have feature idea* — compare
`penumbra_requirements.md`'s urgency to `pharos_tree_lens_feature_request.md`'s
more exploratory framing).

```markdown
# <Penumbra|Prism|Pharos> — <Title> for Pharos

> **Scope:** what was investigated and against what commit/state.
> **Status:** blocking? a real reported bug? a nice-to-have? be honest —
>   don't inflate a minor polish item into "CRITICAL".
> **Not in scope:** (or **Trigger:**, if that reads more naturally) what
>   this doc deliberately excludes, or what prompted the investigation.

---

## 0. Context

**Symptom:** what a user/dev actually observes.

Root cause, traced through the real source, with file:line citations and
quoted code -- not paraphrased. If more than one contributing cause,
number them (see penumbra_text_blur_requirements.md's "Cause 1"/"Cause 2").

Explain *why* this can't be worked around in Pharos's own code today (or,
for a Pharos-side feature, why the current single-tree/single-view
assumption is the limiting factor).

---

## 1. REQUIRED — <short name of the first fix/feature>

Problem statement specific to this item.

### Proposed API  (or "Proposed fix")

```cpp
// exact file path this would live in
... concrete, compilable-looking signature, matching real naming conventions ...
```

### Required changes elsewhere  (omit if none)

Bullet the other call sites/files that must change in step with this one.

### What unblocks in Pharos

Concrete: which Pharos file/behavior starts working once this lands.

---

## 2. REQUIRED — <second item, if any>
...same shape...

---

## N. Explicitly not requested

Bulleted list of adjacent asks you deliberately did NOT include, each with
a one-line reason. This section is not optional filler — every doc in this
repo has one, because it's what stops the doc from being reinterpreted
later as asking for more than it did.

---

## N+1. What unblocks when this lands

One paragraph, concrete, tying back to the original symptom in section 0.
```

## 4. Before handing it back

- Re-read section 0's root cause against the real source one more time —
  did you cite a line that actually says what you claim?
- Confirm the filename doesn't collide with / silently supersede an
  existing doc.
- If you're about to write a `penumbra_*` doc, make sure you actually
  checked composition first (section 0 of this skill) — don't write one
  just because it was the first idea.
- Stop. Per the table in step 0, don't start implementing a Penumbra-side
  or Prism-side change after writing the doc. For a `pharos_*_feature_request.md`,
  only proceed to implement if the user explicitly asks (as a separate,
  later instruction) — writing the doc and implementing it are two
  distinct asks even when they happen in the same conversation.
