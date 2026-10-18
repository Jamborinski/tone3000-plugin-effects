# Ticket — Magnetic delay type (Echorec-based wire-delay rewrite)

Status: **DRAFT — scope decision made** (user, 2026-10-14):
- "lets open a ticket for a magnetic delay type rewrite (based on the
  echorec)" — the Echorec is its own tracked ticket for research/spec,
  landing later as a tuned subtype.
- **Block-vs-type question RESOLVED** (user, same day, m00551): **"we
  can just add echorec as a subtype for tape even if it technically
  isnt"** — the Echorec ships as a **type slot on the tape/delay
  block's `type` axis** (see `docs/tickets/tape-delay-draft.md`, now
  reserved as the Echorec slot), NOT as a separate block. Wire-vs-tape
  physical distinction (locked taps + matrix vs speed-type modulation)
  is acknowledged and waived for UX simplicity — one delay block, the
  type cycler carries the differences (same mechanism as the reverb
  block's type axis: `Params::type[mode]`, `numTypes()`,
  `defaultDialsForType()`).

This ticket is the Echorec lane's **research/spec home**: its blueprint,
material, and measurement pass live here; when it goes live it is
implemented as the reserved type slot in the tape ticket. That is the
same shape as the EMT-140 plate type (its own research home, shipped
as a type, not a block).

## Scope (2026-10-14, user-confirmed) — lands as a TYPE of the delay block
(research/spec home HERE; implementation = the tape ticket's reserved
Echorec slot)
- **Core (linear, in scope):**
  - **Locked 4-tap delay ladder**: heads at **T / 2T / 3T / 4T** — two
    candidate base values from the user's material (see below), BOTH
    musically-rational exact 1×/2×/3×/4×, the T itself to be decided
    by the measurement pass (Dr Bini spaced them deliberately: tap4 =
    quarter note, tap3 = dotted 8th, tap2 = 8th, tap1 = 16th — the
    "gold ratios"; musical mapping is the design intent, so whichever
    T wins should keep the subdivision story).
  - **12-position head switch** (user blueprint's "switcher matrix"): tap
    combinations (Tap 1 alone, 1+2, 2+4, all four, …) — the rhythmic-
    subdivision signature. Maps onto a `type`/`position` sub-matrix:
    our cycler pattern + `defaultDialsForType()` can carry the matrix
    combinations as named positions; the 12-way is the hardware's own
    selector (Abbey Road: "12 separate echo selections from one head
    alone to complex multi-tap effects"), not necessarily 12 distinct
    algorithmic states in the plugin.
  - **3-mode selector** (Abbey Road — the physical panel's three
    buttons): **Echo** (1950s slapback — single short tap) / **Repeat**
    (standard delay) / **Swell** (overlapping, "closer to a reverb" —
    the compounding-cascade feedback mode). The 3 modes × 12 tap
    combinations = the full panel; the plugin can expose the mode as its
    own detent/position and the tap matrix as a second axis, or fold
    both into one named-position cycler. **Design decision at
    implementation time; the mode's *sound* (Swell = lush, blooming,
    reverb-like tail from multi-tap feedback) is the fixed character
    to preserve either way.**
  - **Feedback matrix = ALL active playback heads summed back into the
    record head** (user blueprint: "Magnetic Swell" — "lush, blooming
    ambient reverb tank" vs "clean, distinct echo repeats"), same
    gain-matrix stability rule as the tape ticket's
    saturation+feedback joint-stability: the feedback network is one
    matrix, not N independent loops — analyze the combined loop gain,
    not per-tap gain.
  - **Head tone = brickwall LP + HP, write-side** (user blueprint's own
    DSP prescription: "place tight, high-order Biquad low-pass and
    high-pass filters right before the audio writes to the buffer,
    capturing that dark, warm, rolled-off characteristic tone"). Ceiling
    class: **~5–10 kHz** (early Binson heads vs later Photovox heads
    vary; the measurement pass picks one, or exposes it as a `tone`
    detent). The AC bias oscillator (50–60 kHz) is the hardware's
    dither-for-linear-print mechanism; in the linear lane its *audible
    consequence* is exactly the brickwall + clean (unclipped) print,
    so the brickwall LP (+ an optional soft-knee gentle saturation for
    the "linear without standard clipping" *feel*) is the linear-lane
    stand-in — the bias oscillator itself is NOT modeled.
  - **Delay length = a mechanical constant, not a knob**: the motor is
    AC-mains-synchronous (50 Hz Europe / 60 Hz US) — it has no speed
    knob, no wow/flutter to chase, delay length is fixed by the drum
    geometry + mains frequency. The delay TIME parameter in the plugin
    is therefore **a per-type constant (the measured T)**, not a user
    knob (or if exposed, a cosmetic re-scaled range around the true T,
    same as the plate '140' type's decay-constant-2.0-s approach).
  - **No wow/flutter in the default voice** (Effectrode, explicitly: the
    new-maintenance Binson sound has inaudible pitch fluctuation + good
    FR; wow/flutter is an *artefact of poor maintenance* — idler-wheel
    dents, oil, mains fluctuation). This is the OPPOSITE of the tape
    ticket's flutter/wow LFOs — the Echorec lane keeps the taps LOCKED
    (its signature is the fixed subdivisions + the matrix, not the
    drift). Optional "aged" mode (wow/flutter + mushy tone) is a
    possible future detent, NOT the default; it is a separate research
    pass either way.
  - **Dry/wet + feedback depth + (mode) selector + (position) matrix** =
    the full control surface; `dry` at 0.5 = 0 dB (the convDry
    convention, consistent with the reverb ticket R1 and the tape
    ticket's default type).
- **Out (nonlinear lane, own ticket / own research pass — same
  exclusion rule as the spring/tape tickets):** the tube preamp itself
  (the input tube's overdrive character, the 3-input/3-output channel
  structure, the AC bias dither mechanism, the wire's
  magnetic hysteresis/saturation). The user's material frames these as
  *what the hardware does*, not as a request to ship them — the linear
  lane (brickwall + matrix + swell + locked taps) is the initial scope.
- **Reference emulations to hear against (guard candidates, not code
  sources):** Catalinbread Echorec, Dawner (Prince) Boonar — the two
  named in the user's own material. Plus the original Binson recordings
  the Abbey Road article cites as the heard benchmark (Pink Floyd 'Time',
  'Shine On You Crazy Diamond', 'Interstellar Overdrive', 'Astronomy
  Domine'; Led Zeppelin 'When the Levee Breaks' drums; The Shadows /
  Hank Marvin — the historical signature users).

## Two candidate base delays (from the user's material, 2026-10-14)
- **User blueprint (Dr Bini's published/paraphrased values):** T ≈ **108
  ms** → 108 / 216 / 324 / 432 ms.
- **Abbey Road Institute (measured on a real unit):** 74 / 148 / 222 /
  296 ms (T ≈ 74 ms).
  Both are exact 1:2:3:4 (the musical mapping holds); the actual T is a
  hardware-variant question (drum size / mains frequency / unit age
  vary — the 50/60 Hz split alone would scale the period). The
  **measurement pass** (per `ir-reverb-training.md`) on a real Binson
  Echorec recording is what picks the shipped T; until then both stay
  logged as candidates, neither baked in. The musical-subdivision
  property is invariant to which T wins.

## Relationship to the other tickets
- `docs/tickets/tape-delay-draft.md` — the **generic tape** (speed-type,
  Zavalishin & Parker DAFX-18) with RE-201/Echoplex tuned types
  reserved. The **Echorec is NOT a tape** — it is wire, and its defining
  property (locked taps + matrix) is the *opposite* of the tape lane's
  defining property (speed-type modulation). If the user calls it a
  "type", both live under one delay block's type axis (generic tape =
  type 0, RE-201, Echoplex, Echorec/magnetic as further types); if a
  separate block, it gets its own tile. **Blocked on the user's
  word.** The 12-position-class matrix + multi-tap-frozen-taps pattern
  is reusable across whichever shape wins if it wants.
- `docs/tickets/plate-dat-rebuild-draft.md`,
  `docs/tickets/spring-mode-rebuild-draft.md` — no overlap; the
  "swell" mode's reverb-tank behaviour is a *sound* reference, not a
  request to reuse the reverb engine.

## New material added 2026-10-14 (user, m00551)
User links, read this turn:
- **Effectrode, "The Dream Machine: the Echorec 3°"** (effectrode.com
  knowledge base) — Binson technical documentation + a design-philosophy
  article. Key points folded into the spec above: (a) wow/flutter is an
  artefact of poor maintenance, not the Binson sound — a new/properly
  set-up machine has inaudible pitch fluctuation + good FR (→ the
  Echorec lane keeps taps LOCKED, no default LFO); (b) the head + drum
  + tube signal path together define the transfer function (→ the
  brickwall LP is the linear capture, the tube preamp is the nonlinear
  lane, same split); (c) pushing to self-oscillation, the Echorec is a
  sound synthesizer as well as a delay — the Swell mode's reverb-like
  tail is the design intent, not an accident; (d) the delay length is
  a physical constant set by mains frequency (50/60 Hz) + drum
  geometry, i.e. there is no "speed" knob on a real Echorec — the
  plugin's delay length is therefore a per-type constant (or a
  cosmetic detent around it), not a free knob.
- **Abbey Road Institute, "When you say Pink Floyd, you say Binson
  Echorec"** (abbeyroadinstitute.nl) — hardware + controls + musical
  mapping of the four taps (quarter / dotted-8th / 8th / 16th), the
  12-position head switch, the Echo/Repeat/Swell selector, and the
  3-input/3-output channel structure. Measured tap times on a real
  unit: **74 / 148 / 222 / 296 ms** (T ≈ 74 ms) — a second candidate
  base value, logged above against the blueprint's ≈108 ms (both exact
  1:2:3:4; measurement pass decides the shipped T).
- **jatinchowdhury18/AnalogTapeModel** (GPL v3, CHOW Tape, DAFX-19
  physical tape-saturation model) — the nonlinear-lane reference for
  the tape-saturation side (Langevin-class magnetization ODE); ideas/
  structure only per the house rule, NOT a source for the linear
  Echorec lane (which is the delay/matrix/tone, not the saturation).
- **Chowdhury-DSP/chowdsp_wdf** (BSD-3, Wave Digital Filters, header-
  only C++) — the library the Echorec's tube preamp would be modeled in,
  IF the nonlinear lane goes live; reference only (house rule: adapt,
  don't copy; comments rewritten; constants re-derived against our own
  references), and OUT of the initial linear scope.
- **mariusono/Multitap_delay** (JUCE, **no LICENSE file found** in the
  repo, self-described as based on a textbook block diagram —
  provenance/context only, treated as not-usable-as-a-code-source; the
  multi-tap delay topology is the one structural idea worth noting, and
  it's the same idea the user's own blueprint and the Abbey Road
  hardware description already establish — no new information for the
  spec).

## Discipline
Clean-room (AGENTS.md hard rule): no copying of any existing Echorec
emulation's code/curves — the user's blueprint and the two articles
above are the source of truth for *what*; our own implementation
(constants re-derived against any reference recording we obtain at the
measurement pass) is the source for *how*; comments in our voice; no
reference to a specific existing plugin by name in the shipped code.
The GPL (AnalogTapeModel) and the no-license (Multitap_delay) repos are
structure/idea-only references at most.

## Closeout protocol (standard)
DspTests full-green, TONE3000_Standalone build, UiTestbed --selftest,
OpenWiki update, Windows standalone exe stage.

## Research references (gitignored, local-only)
- User blueprint (m00551, in-ticket above) — 4-tap ladder T/2T/3T/4T,
  ~108 ms class, 12-position matrix, AC bias → ~5–10 kHz brickwall,
  Swell feedback, tube preamp (nonlinear, out), Catalinbread Echorec +
  Dawner Boonar as heard references.
- Effectrode Binson knowledge base (technical docs + service manuals, free,
  the definitive modern reference on real Binson internals — the most
  authoritative source for a real measurement pass later).
- Abbey Road Institute (hardware + controls + musical-mapping of the
  four taps + Echo/Repeat/Swell + measured 74/148/222/296 ms on a real
  unit).
- `AnalogTapeModel` (GPL v3) + `chowdsp_wdf` (BSD-3) — nonlinear lane
  (tube preamp / wire saturation) building blocks, reference only,
  OUT of the initial linear scope (see Discipline).
- Real Binson recordings (Pink Floyd 'Time', 'Shine On You Crazy Diamond',
  'Interstellar Overdrive', 'Astronomy Domine'; Led Zeppelin 'When the
  Levee Breaks' drums; Hank Marvin / The Shadows) — the heard
  guard-candidates for the measurement pass, per `ir-reverb-training.md`.

## Relationship to the other tickets (re-confirmed 2026-10-14)
- `docs/tickets/tape-delay-draft.md` — **the Echorec now lives as a
  reserved `type` slot there** (user, m00551); this ticket remains its
  research/spec home. The tape ticket's generic type (speed-type, DAFX-
  18 core) and the Echorec type (locked, mains-synced) are
  fundamentally different delay behaviours sharing one block + cycler —
  the same pattern the reverb block's type axis uses (different engines
  per type, same shared mechanism). If the user ever revisits the
  block-vs-type decision, both tickets' specs are written to survive
  either shape (the core algorithm + parameters are independent of
  whether they share a tile).
- `docs/tickets/plate-dat-rebuild-draft.md`,
  `docs/tickets/spring-mode-rebuild-draft.md` — no overlap; the Swell
  mode's reverb-tank *sound* reference does not request reusing the
  reverb engine.
