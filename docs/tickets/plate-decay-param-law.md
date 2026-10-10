# Ticket — Plate decay parameter: fix the decay→darkness law (parameter response)

Status: **DONE** (2026-10-13) — closed together with `plate-texture.md`.
Finding: the EMT 140 reference FAMILY itself (0.5 s / 2.0 s / 3.0 s lengths,
click-tail 0.3-0.6 s band balance) shows LONGER = DARKER
(0.5 s len -62.5 vs 2.0 s len -73.0 in 6-12 k minus 150-300; the 3.0 s file
is in between) -- i.e. the plate's ticket-side phrasing "shorter = darker"
is INVERTED against the measured family, and the guard now pins the
MEASURED direction. The plate now COMPLIES: 600 ms tail -32.5 dB > 2400 ms
tail -73.3 dB (shorter = brighter, per the family). Mechanism: the
frequency-dependent air corner is an OUT-stage 1st-order LPF cascade
(2 x -6 dB/oct), 4900 Hz at the short decay -> 3600 Hz at the long (fc is the
decay law). In-loop darkening/body remains REJECTED (measured comb cost
+4.7 to +10 dB, fb-capped gain, one unstable variant); out-stage 2nd-order
biquads were REJECTED on measurement (unstable pole 1.13, DC-dead "lowpass"
gain 0.035, wrong-shape 240 Hz peaking) -- only 1st-order stages are
provably stable + DC-exact at fc << fs, and those are what shipped.
Declines + full numbers: CONSIDERED & DECLINED block in
`plugin/include/Reverb.h` (PLATE TEXTURE, 2026-10-13),
`docs/agents/ir-reverb-training.md` (biquad landmine + plate "140" state),
`docs/tickets/plate-texture.md` (DONE status).
Original status (2026-10-10): READY. Companion to (split off
from) `docs/tickets/plate-texture.md`: that ticket fixes the *shape at default*
(sustained balance + onset density); **this ticket fixes the DIAL law** — that
the decay parameter correctly drives *darkening* — and does so **without the
comb cost** that killed the first measured attempt.
Priority: plate parameter response, mode 2 only.

## Problem (measured parameter-response defect)
A real plate gets **darker** as decay shortens (its HF damps faster). The
as-shipped TONE3000 plate measures the **opposite direction**:

```
tail HF/LF band ratio:   600 ms → 3.6e-5  >  2400 ms → 4.8e-6
i.e. shorter decay = BRIGHTER (more HF in the tail) — INVERTED.
```

(measured probe: `PlateComb.HFDecaysFasterAndDecayTracks`, printed in the
suite — the "tail band ratio" line; reference-IR method per
`docs/agents/ir-reverb-training.md`.)

**Root cause (measured)**: the decay law is a broadband feedback scalar whose
diffusion wash is *coupled to the same scalar* (`decayDiffFrac`) — shortening
the decay removes diffusion wash faster than it removes HF energy, so the
residual aligned comb stands out and reads brighter/more metallic, while the
true "HF damps faster" law is absent. The dial currently controls *level +
comb visibility*, not *darkness*.

## The DECLINED fix and its cost (what this ticket must BEAT)
The first correct attempt was a **decay-tracked in-loop LPF** (lever 1 in
`docs/tickets/complete/plate-combing.md`). It **fixed the direction** but paid
**+4.7 dB comb** (click comb 16.7 → 21.4; EMT 14.7 reference) — measured
*before* the 6-APF chain level fix, with onset/body intact. Recorded
`CONSIDERED & DECLINED` beside `kPlateWashAp` in `plugin/include/Reverb.h`.
The open problem of this ticket is exactly that trade: take back the correct
darkening law **without paying comb**.

## Acceptance basis (already in the repo, re-runnable)
- Reference IR + the same `PlateComb.*` / `PlateCombRef.*` / `PlateCombCpu.*`
  harness as `plate-texture.md` (one shared harness; different acceptance axis).
- The decay→darkness probe is **already printed** in
  `PlateComb.HFDecaysFasterAndDecayTracks` (600 ms vs 2400 ms tail HF/LF) —
  the acceptance assertion is a one-line direction check on that existing line.
- Prior lever A/B matrix + the DECLINED L1 numbers:
  `docs/tickets/complete/plate-combing-closeout.md` (the Config-A column).
- Workflow: `docs/agents/ir-reverb-training.md`.

## Levers (candidates — measure, don't assume)
- **Decay-tracked in-loop damping, decoupled from wash level** — the L1 LPF
  with the diffusion wash *uncoupled* from the decay scalar, so "shorter
  decay" maps to *darker* instead of to *less wash / more comb*. This is the
  direct fix for the measured root cause.
- **Top-weighted sustain damping, monotonic in decay** — a dedicated
  "shorter decay ⇒ steeper top roll-off" law, applied where it removes HF
  energy *without* removing diffuse wash.
- **Split the decay scalar** (level path vs colour path) if the single scalar
  proves to be the coupling artifact.
(each candidate A/B in isolation, level-matched **by construction**;
level-mismatch is a rejected result, not data — per the Hadamard-bank confound
in the close-out.)

## Definition of done
- **Direction fixed + monotonic:** tail HF/LF ratio strictly lower at 600 ms
  than at 2400 ms AND monotonically decreasing as decay shortens across the
  whole dial (no inversion anywhere in the sweep).
- **Comb FLOORED (the L1 failure):** click comb ≤ 16.7 dB and steady comb
  ≤ 2.66 dB (as-shipped baselines) — the correct darkness must NOT be bought
  by combing.
- The DoD slope law holds (HF decays faster than LF at default).
- Confirmed level/sheen (2500 Hz onset) preserved — a shape win, not a level
  artifact.
- Other five reverb modes **bit-identical**
  (`PlateComb.OtherModesStayBitIdentical` pins unchanged).
- Full DspTests green (non-zero "Running N") + TONE3000 GUI links; per-lever
  + final CPU table (48/96 kHz × 64/128/256); re-shot A/B table (plate vs
  as-shipped vs DECLINED-L1 vs reference).
- CLOSE-OUT: new-vs-L1-vs-baseline-vs-reference + any levers
  CONSIDERED & DECLINED + their numbers.

## Sibling ticket / boundary
- `plate-texture.md` owns the **static shape at default** (sustained balance +
  onset density). This ticket owns the **parameter law** (how the decay dial
  moves that shape). They share the comb-floor fence and may share a
  mechanism; they are accepted on *different* axes (shape vs response) so a
  win on one is not a win on the other.
- Out of scope: other modes; re-litigating the comb/diffusion/damping layers
  (declined in `complete/plate-combing.md`).

## When to start
On your call. The decay→darkness probe is already in the harness, so this is
a pure retune-and-measure loop with a one-line new assertion.
