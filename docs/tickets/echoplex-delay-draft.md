# Ticket — Echoplex delay type (tuned subtype of the tape/delay block)

Status: **DRAFT — user material received** (user, 2026-10-14: "lets also
make a ticket for echoplex delay type"). Per the user's standing decision
(m00551, "we can just add echorec as a subtype for tape even if it
technically isnt" — the same principle the tape ticket already applies to
RE-201/Echoplex), the Echoplex **ships as a reserved typed slot on the
tape/delay block** — currently `type 2` in
`docs/tickets/tape-delay-draft.md`. This ticket is its research/spec
home (same shape as `magnetic-delay-echorec-draft.md` and the EMT-140
plate-type relationship): blueprint + material + measurement pass live
here, implementation is the tape ticket's Echoplex slot.

## Character (the thing to preserve, per the heard record)
The Echoplex is a **tape** delay (single tape path, speed-type — same
lane as the generic tape ticket, NOT the Echorec's locked-matrix lane),
famous for:
- a **single continuous delay time** set by tape travel (the classic
  "Delay" handle; the original 160's travel is a fraction of a second —
  exact range to be measured, not assumed),
- a **13-position Repeats selector** (0 = no feedback, 1–13 = coarser-to-
  lusher feedback; the coarse quantization IS the signature — a switch,
  not a continuous knob),
- a **Spring** toggle: tape path + spring tank — our chain already has
  the spring block for that lane (cross-link, don't duplicate),
- its flutter/motor character (tape = wow/flutter present, the OPPOSITE
  of the Echorec's locked-tap lane — the flutter/wow LFO pattern from
  the generic tape ticket type carries over),
- soft harmonic character on the tape print (the saturation lane —
  nonlinear, out of the initial linear scope, same exclusion rule as
  every other ticket).
Historical signature for the heard-guard: the Echoplex is the classic
slapback/repeats + spring guitar unit of the 60s and the "tape echo"
sound of countless guitar records (the guard recording to be chosen at
the measurement pass per `ir-reverb-training.md`).

## Initial linear scope (when it goes live, as `type 2`)
- **Speed-type core** = the generic tape ticket's engine (Zavalishin &
  Parker DAFX-18), so the type adds *configuration*, not a new
  algorithm: per-type delay range tuned to the Echoplex's travel,
  per-type flutter/wow defaults (the generic type's LFO pattern),
  feedback expressed as the **13-position coarse switch** (our cycler
  pattern + `defaultDialsForType()` carries the 0–13 ladder as named
  positions; the coarse quantization is the character — do NOT ship it
  as a continuous 0..1 knob and forget to expose the ladder), tone LP on
  the repeats path (the "Tone" control), mix/dry (0.5 = 0 dB
  convention), spring toggle = chain-routing pointer to the spring
  block (not an internal tank).
- **Out:** the tape-saturation nonlinear lane (same exclusion as the
  other tickets; `AnalogTapeModel` is the reference if that lane ever
  goes live), the spring tank (spring ticket's territory).

## Material logged this ticket (2026-10-14, user-sourced)
- **Scribd paper** "A Digital Model of The Echoplex Tape Delay"
  (scribd.com/document/1039119085) — **PARTIAL (abstract-gated)**:
  what the public abstract states: read/write/erase pointers along a
  circular buffer; time-varying delays driven by the **Delay Handle**
  position; anti-aliasing to prevent distortion; model based on
  measurements + simulations of the unit's sound. Full constants
  (head/tap times, flutter specs, 13-step ladder values, tone
  frequencies) are **NOT readable** through the public pages — if the
  user has the document locally, point at the file and it will be
  parsed with `document_parse`; until then the numbers it would add are
  UNVERIFIED and must not be quoted as known.
- **jatinchowdhury18/AnalogTapeModel** (GPL v3, CHOW Tape, DAFX-19
  physical tape model) — the nonlinear tape-saturation lane reference
  (same as the Echorec ticket logged it); structure/ideas only per the
  house rule; OUT of the initial linear scope.
- **charlesvestal/schwung-space-delay** (**MIT**, charlesvestal,
  Ableton Move module) — the cheapest reference in the batch, read in
  full (`src/dsp/spacecho.c`, 724 L): single circular delay line,
  time 20–2000 ms, feedback 0–0.95, exponential tone LP 500 Hz–12 kHz
  (`500·24^n`), tempo-sync division multipliers (1/1…1/16t), **ping-pong
  L/R width with a documented width-level-comp** `1/sqrt(1−0.5·width)`
  (capped ×1.333) and mixed ping-pong input routing — a clean,
  permissive template for the width/mix/feedback plumbing on ANY of our
  delay types (generic, RE-201, Echoplex, Echorec); deliberately a
  *utility* delay (~0.013 ms/block) vs its project's separate
  "Tape Echo 2" (3-head + spring, ~0.37 ms/block) — the two-tier
  utility-vs-character split is a useful packaging precedent for how
  many *types* a delay block can carry before CPU becomes the issue.
  MIT house rule still applies: adapt, rewrite comments, re-derive
  constants against our own references; no name in shipped code.
- The real unit's 13-position Repeats ladder + Delay-handle travel +
  flutter spec are **measurement-pass items** (the `ir-reverb-training.md`
  guard rule: measure the reference first; the reference's own numbers
  must pass any guard we set around it).

## Measured reference — user IR set "Maestro Echoplex (Tube)" (2026-10-14, user-supplied)
`C:/Impulse Responses/Convolution Reverb IRs/Past to Future Reverbs/PastToFutureReverbs Maestro Echoplex Tube Tape Delay IRs! Impulse Responses (IRs) WAV`
— 11 files, 24-bit/96 kHz (pack PDF is marketing-only: "11 IRs, LED
ZEPPELIN", no position→delay map; no head/tap spec). Measured (probe
`research/probe_echoplex_tape.py`, data `research/echoplex-tape-data.txt`):

- **DELAY HANDLE LADDER (base echoes, L channel, measured):**
  **57.5 / 95.4 / 145.0 / 225.3 / 295.4 / 427.2 / 472.9 / 573.6–574.8 ms**
  (files 1, 11, 2, 3, 5, 7, 8, 9/10 — pos 9 and 10 measure 574.8/573.6,
  effectively one top end). This is a **choosing-ladder** (8 discrete
  positions), NOT a continuous delay handle — the 160's actual behavior
  was a continuous travel knob, but this capture set quantizes to these
  8 stops. Ship these measured stops as the type-2 grid (re-derive our
  own against our own reference at launch per clean-room); do NOT invent
  a new grid.
- **Per-position tail**: each echo decays as a multi-bounce tail with
  bounces **~8 ms apart** (the tape's own feedback repetition; this is
  the "Feedback" loop, not a separate repeats IR).
- **Stereo**: pos 1,2,3,5,8,9,10,11 have **R = L (R−L = 0)**; pos 4s, 6s,
  7 carry a **R-EARLIER echo** (R at 28.7/29.3/46.5 ms, i.e. −179/−266/
  −399 ms vs L) — an early cross-bounce on R, a genuine stereo variant,
  NOT a fixed Haas offset like RE-201's R-is-later. So this family has
  **NO uniform stereo law**: default stereo = mono-duplicate; the
  R-earlier variant only on the "S" (4s/6s) + pos-7 files.
- **~3.5 ms pre-gain print** on L in nearly every file = record-head
  capture artifact (same as the RE-201 packs); the real echo is the next
  >10%-of-peak local max. Optional to ship (decision point, not locked).
- **Repeats 13-position ladder**: NOT in this IR set (each file is fixed
  delay + its own feedback tail; there is no repeats-quantization data).
  The 13-position coarse switch stays a spec-side item (Scribd abstract
  mentions it; values UNVERIFIED) — implement as a cycler of feedback
  values at `type 2`, per the initial-scope note above.

**What this locks**: the Echoplex `type 2` Delay-handle grid = the 8
measured stops above (or the user's own unit measurement at launch);
tail = feedback loop (~8 ms/bounce); stereo default = mono-duplicate;
repeats = cycler (values TBD per Scribd/measurement), NOT a measured
ladder. What stays open: continuous vs 8-stop delay (this IR set says
8-stop), 13-position values, tone-LP cutoff, flutter/wow magnitudes on
this specific unit.

## Discipline (standard)
Clean-room (AGENTS.md): blueprint + papers are the *what*; our own
implementation with constants re-derived against measured references is
the *how*; comments in our voice; GPL (AnalogTapeModel) =
structure/ideas only; MIT (schwung) = adaptation allowed, comments +
constants re-derived, no name in shipped code.

## Closeout protocol (standard)
DspTests full-green, TONE3000_Standalone build, UiTestbed --selftest,
OpenWiki update, Windows standalone exe stage.

## Research references (gitignored, local-only)
- The three user sources above (Scribd abstract-gated; AnalogTapeModel;
  schwung-space-delay), already mirrored in `research/README.md` and
  cross-linked here.
- **User IR set "Maestro Echoplex (Tube)"** (2026-10-14): `C:/Impulse
  Responses/Convolution Reverb IRs/Past to Future Reverbs/PastToFutureReverbs
  Maestro Echoplex Tube Tape Delay IRs! Impulse Responses (IRs) WAV`
  — 11 files, 24-bit/96 kHz; measured delay ladder + stereo behavior in
  the "Measured reference" section above; probe `research/probe_echoplex_tape.py`,
  data `research/echoplex-tape-data.txt`.
- `docs/tickets/tape-delay-draft.md` § `type 2` — the implementation slot.
