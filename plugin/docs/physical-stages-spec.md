# Physical stages — clean-room design spec (next big ticket)

Status: DESIGN INPUT, not code. Supersedes the flat "four constant-sets of one
soft-clipper" coloration in `Compressor.h`. The `clipDepth(clean, normal, amt)`
knob law (CLIP 0 / noon 1 / hot 2) is preserved and re-pinned on top of the
physical chain. NO AGPL code: felitronics-core (Desktop, AGPL-3.0) is read as
reference ONLY. MIT-safe sources are published physics + the part inventories
below + our own tuned constants + our own measured acceptance pins.

Where does felitronics-core actually apply? (verified in the repo, 2026-10-06)
- **Transformer (comp)**: yes — its Transformer curve = paper spec for the flux
  model shape (READ, never link/copy).
- **Tape (delay)**: yes — its `Tape` curve = paper spec validating our
  NAB-emphasis→core→de-emphasis shape (READ, never link/copy).
- **BBD (delay)**: NO — the library has NO BBD/bucket-brigade model at all
  (grep: only FFT hash-bucket tables). BBD's signature is tonal darkening + HF
  loss + SNR, not a saturation curve. The Memory Man parts list + the existing
  time↔tone law ARE the BBD reference.
- **Compressor GR / VCA**: NO — felitronics has no compressor/VCA/GR module;
  those laws are 100% ours (SSL DBX202/THAT2150 + 6386 var-mu + 2N5457 JFET).

## THE KEY FINDING (corrects an earlier draft assumption)
The real differentiator between the four bold units is the **GR device** and
the **color/breakup stage**, NOT the "knee". The parts lists show:
  - **1176 = solid-state** (no tubes at all). GR element is a **2N5457 JFET**
    pair used as a voltage divider. A JFET's drain law is **quadratic**
    (`i_D = I_DSS·(1 − V_GS/V_GSoff)²`) — a smooth C¹ soft rolloff that never
    hard-clips. That is the 1176's musical grab: **JFET-soft knee**, not hard.
  - **670 and STA both use the 6386 remote-cutoff VARIABLE-MU triode** as the
    signal-path GR element → smooth, deep, no-harsh-cutoff GR (variable-pitch
    grid). Their "grab/punch" is NOT a stiff knee — it is the **control
    circuit speed + GR amount + hot transformer body**.
So the six modes resolve into three GR families plus body/flux:
  JFET-soft (1176) · optical (2A) · variable-mu (670, STA)   +   shared flux body.
SCOPE NOTE: TRANSFORMER FLUX LIVES IN THE FOUR BOLD COMPRESSOR MODES ONLY.
Both DELAY color modes (Tape, BBD) are flux-free by hardware history (neither
the RE-201 nor the DM-2 has an audio-path transformer). VCA is flux-free too.

## Hard constraints
- ZERO added latency. Flux integrator is causal (one float/channel, no
  differentiator, no lookahead). NAB pre/de-emphasis is exact inverses
  (net phase zero; two floats/channel, bilinear, prewarped). NO oversampling —
  keep drives in the gentle band so harmonic alias stays small; the alias
  budget is a MEASURED acceptance pin, not a hope.
- RT-safe / alloc-free after prepare. State per channel: flux = 1 float,
  NAB pair = 2 floats, gated/flushed like our existing per-channel state.
- One shared causal TRansFORMER flux section gives the per-unit BODY / low-end;
  the per-mode GR device (JFET / optical / variable-mu) + color stage give the
  character. CLIP (0/1/2) rides the whole chain.
- The VCA (mode 0) stays the clean benchmark — NO stage, NO transformer, KNEE only.

## Part inventory (user-provided reference; read-only, nothing copied)
### UREI 1176 (all solid-state)
| Part | Role | Note |
|---|---|---|
| **2N5457** ×2 | GR element (voltage divider) | JFET, quadratic soft rolloff. The soft/fast grab. |
| 2N3391A ×4 | Preamp + output amp | 1108-style Darlington NPN. |
| 2N3053 / 2N3707 | Class-A / output PNP | output stage. |
| 2N5088 ×4 | Control amplifier | NPN Si. |
| T1 UTC O-12 (1.58:1), T2 UA-5002 (Class-A feedback loop); **Rev G = NE5532 op-amp input** | in/out | UREI removed transformer color over time (Rev G) → mildest body. |
**Model:** JFET soft-knee GR; BJT push-pull odd-dominant; mildest flux; aggression
= speed + JFET softness.

### Teletronix LA-2A (optical)
| Part | Role | Note |
|---|---|---|
| **Photocell** driven by 12AX7 control amp (V3) | GR element | slow time constants. |
| 12AX7A ×2 (V1, V3) | signal amp + control amp | sharp cutoff, mu 100, rp ~80 kΩ. |
| **12BH7A (V2) + 6AQ5 (V4)** push-pull | output stage | ~12 W, "hot output stage". |
| HA-100X (80 % Ni, early-sat core), A-24 15 kΩ:600 Ω pp | in/out | 80 % Ni → lower saturation flux → saturates EARLY. |
**Model:** optical GR; push-pull pentode/hexode "hot stage"; moderate flux,
early-saturating core.

### Fairchild 670 (variable-mu)
| Part | Role | Note |
|---|---|---|
| **6386 ×8 (4/ch)** remote-cutoff variable-mu | signal-path GR | smooth deep GR, var-pitch grid. |
| 12AX7 ×2 | control amp voltage gain | |
| 12BH7 ×2 | control amp current gain | |
| **6973 ×4 (2/ch)** high-current triode pp | control-amp output | the PUNCH. |
| EF806 (15 W) or EL34 (20 W) | power amp | output. |
| T101 A-26 (backwards, shield can), **T102 HS-52 Class A 20 kΩ @ 12.5 mA, +26 dBm** | in/out | signal-path transformer run HOT; backwards-in-a-can = thump. |
**Model:** variable-mu GR (smooth); punch = fast control circuit (6973 pp) +
GR amount + hot transformer body; biggest body, low-end thump.

### Gates STA-Level (variable-mu, 1956)
| Part | Role | Note |
|---|---|---|
| **6386** (1, dual triode, var-mu) | signal-path GR | same variable-mu family as 670. |
| 12AT7 (dual triode pp) | driver between 6386 and 6V6s | low noise. |
| **6V6GT ×2 push-pull** | output stage | ~12 W/tube, drives 600 Ω line; power-amp grade. |
| 6AL5 (control rectifier, feeds 6386 grids), OB2 (plate reg), 5Y3GT | control/power | |
| AI-10386T 600 Ω:~10 kΩ (to 6386 grid, +24 dBm), AO-11302T 5–8 kΩ pp:600 Ω | in/out | **two custom Triad windings + a POWER-AMP output transformer = richest body.** |
**Model:** variable-mu GR (same as 670); push-pull 6V6 output; RICHEST flux body,
most low-end first ("Gates thump").

### SSL Bus Compressor (THE VCA-MODE REFERENCE — confirms VCA stays clean)
Our VCA (mode 0) is exactly this architecture. The part list locks in what
"clean" means physically:
| Part | Role | Note |
|---|---|---|
| **DBX 202XT / THAT 2150 (or THAT 2180)** | VCA IC — GR element, current-in/current-out | **~6 mV/dB control sensitivity, >80 dB dynamic range.** This is the real GR-law constant; the GR FETs are INSIDE the IC. |
| **NE5532 / NE5534** BIFET op-amps | in de-balancing + out **current-to-voltage converter** + rebalance | VCA output stage = op-amp CVT, NOT a tube/BJT stage → **essentially no added harmonics; transparency is architectural.** |
| **TL072 / TL074** JFET-input op-amps | sidechain peak detect + full-wave rectifier | low bias current → the smooth, transparent SSL response. |
**Model:** IC soft-GR (anchor the VCA soft-knee law to the SSL **6 mV/dB** constant)
+ **transparent stage by construction** (op-amp CVT, no driven stage) + **no transformer**
+ GR character from the detector-chain smoothness. This CONFIRMS the existing
decision: VCA = mode 0 = clean benchmark, KNEE only, NO stage, NO coloration,
NO flux. Our `vcaLawDb` soft-knee law is the right shape. **felitronics-core
provides NOTHING for the VCA** (it has no compressor/VCA/GR module) — so there is
nothing to adopt and nothing AGPL there; the VCA law stays in our own engine,
which is already correct. The library only helps the *saturation/flux* side of
the four bold modes (transformer + tape), which is exactly where I intend to
read it (as a reference ONLY).

### Roland RE-201 Space Echo (THE TAPE-DELAY REFERENCE — head+tape, NO flux)
Our Tape delay mode is "idealized Space Echo." The RE-201 data confirms the
model and sets the tone details:
- **Fully direct-coupled solid-state (2SC1000/2SA493 complementary pairs).
  NO signal transformers** (only the PSU) — "a deliberate design choice for
  flat frequency response and low distortion." → the tape mode's color is
  HEAD + TAPE only; the transformer-flux section does NOT go here. PIN THIS
  (the one exception to the shared-flux architecture) so a future session
  never "helpfully" adds flux to the tape delay.
- Color sources + our ticket actions:
  | RE-201 detail | current mode | action |
  |---|---|---|
  | Tape head + tape saturation | `kTapeSoft=1.25` flat drive gain in feedback | replace with the head+core LAW: NAB emphasis → tanh core → exact de-emphasis (highs saturate FIRST) |
  | Playback preamp NFB is frequency-dependent (attenuates HF) | no per-loop tone | per-loop HF rolloff as repeats thin out — the Space-Echo tail |
  | 40 Hz - 15 kHz tape-limited band | no explicit band | band the chain toward a ~15 kHz measured ceiling pin |
  | Baxandall-style treble/bass filter (FL-7 board) | none | fixed subtle Baxandall-family shelf pair (baked character, NOT knobs — no tone dials on delay today; knobs = later ticket) |
  | 3 playback heads x 12 combos; motor speed = delay time | heads 1-4 + time law | heads already cover it; pin 12-combo flavor as head-count spread, no new control |
  | Direct-coupled, no flux | correct | KEEP — one place flux does NOT go |
- Already in the mode (KEEP as-is): organic 1.2 Hz wow (kTapeWowHz), 1.5 ms
  flutter depth (kTapeWowMs), heads 1-4, provenance (.research/tape/). The new
  work REPLACES `kTapeSoft` flat gain with the physical head+core+band chain
  around the same wow/flutter + heads.

### EHX Memory Man (the BBD-DELAY REFERENCE — BBD darkening, NO flux, mild drive)
[Re-anchored to the MEMORY MAN per the user (2026-10-06) — supersedes the
earlier Boss DM-2 anchor. Same BBD family, so the model is the same, but the
Memory Man is now the primary reference.] Our BBD delay mode (mode 2) is
already "time buys loss" (cutoff 5000 Hz @ 250 ms falling to a 50 Hz floor,
chip darkening + gentle tanh drive + per-pass loss); the Memory Man data
CONFIRMS that and refines it:
- **Fully solid-state, direct-coupled, no audio-path transformers.** → the BBD
  mode is a FLUX-FREE exception (like the tape). PIN IT.
- BBD color is TONAL (darkening), not a strong harmonic core:
  | Memory Man detail | current mode | action |
  |---|---|---|
  | **Progressive HIGH-FREQUENCY loss in successive repeats** (cap charge leakage; BBD darkening) | 1-pole low-pass (time↔tone law) | keep — the existing cutoff-falls law IS the BBD "darkens each repeat" sound; this is the canonical (not bandpass) reading |
  | NE570 compandor lifts delay-line **SNR** (~20 dB compress before, expand after) | not modeled | a NOISE-FLOOR model, NOT a harmonic drive — the Memory Man's "clean" is noise, not color. Keep the BBD's harmonic drive MILD |
  | **−15 V rail (LM7915) gives ~2× headroom over 9 V BBDs** → lower distortion | not pinned | pin the BBD as a HIGH-HEADROOM, low-distortion unit (milder drive than a 9 V BBD) — cleaner, less-colored repeats |
  | **Clock-modulated chorus/vibrato** (4558 LFO → CD4047 clock → pitch/time modulation; chorus slow, vibrato fast) | our shared **Mod** knob (LFO tap wobble) | the Mod knob IS the Memory Man's chorus/vibrato — provenance now the Memory Man; chorus=slow, vibrato=fast |
  | **2× MN3005 cascaded (Deluxe) = 8,192 stages, ~550 ms** | heads/time range | the long-delay (Deluxe) character = more cascaded BBD passes → more cumulative loss; the time↔tone law already gives "longer = darker" |
  | 2SK30A JFET input buffer, 4558 BIFET op-amps (boost/mix/feedback/LFO) | — | transparent signal path; no added color beyond the BBD line + darkening |
- BBD vs TAPE contrast (both flux-free, different color):
  TAPE = head+tape, **highs saturate first**, a real (mild) harmonic core, NAB pair.
  BBD = capacitor line, **darkens each repeat (HF loss)**, high headroom,
  tonal-not-harmonic, MILD drive. Neither takes flux.
- TRANSISTOR-ROLE CLARIFICATION (both delay units ARE solid-state with transistors in
  the path — but their JOB differs from the 1176): the 1176's 2N5457 JFET **IS
  the GR element** (transistors ARE the coloration → we model its law). The
  RE-201's 2SC1000/2SA493 and the Memory Man's 2SK30A + 4558 are clean
  amps/buffers (direct-coupled, NFB-stabilized) → they give TRANSPARENCY + low
  noise (and the RE-201 playback NFB's HF attenuation, already captured); the
  SIGNATURE color is tape (head+core) / the BBD array (a MOSFET+capacitor chain
  inside the MN3005 chip). So: 1176 = transistor-LAW coloration; Tape = transistors clean, color = tape; BBD = transistors clean, color = BBD darkening.
  We are NOT "forgetting the transistors" — by design they are the transparent
  amps, not the core.
- (Cross-reference, DM-2 / same family: also MN3005 + NE570 + no flux. The
  DM-2 reading emphasised "low-end leakage"; the Memory Man reading — and the
  canonical BBD sound — is **high-end loss / darkening**. We model the
  canonical high-end darkening via the existing time↔tone law. Both units are
  flux-free + NE570-clean, which is the part that matters and is shared.)
- THE EXISTING BBD (mode 2) IS NOT REMODELED — the Memory Man data CONFIRMS
  its baseline (time↔tone darkening + mild drive + high headroom); we PIN it.

### MemMan (delay mode 4 — REPLACES SHIMMER; BBD line + chorus/vibrato Rate)
SHIMMER is removed (the only pitch-shift mode): the upward-shift DSP, `delayRise`,
`riseSemitonesFromNormalized`, `riseScale()`, the Rise knob + scales::delayRise +
help + its tests all go. Its slot becomes a MEMORY MAN mode — a Memory Man IS
exactly "a BBD line + clock-modulated chorus/vibrato" (2×MN3005 + NE570 + CD4047
clock mod). Mostly REUSES existing machinery (the BBD line + the Mod LFO) — a
re-model, not much new code:
- **Delay line** = the existing BBD time↔tone law at the Chip=0 baseline (natural
  "longer = darker" loss, no added drive — the Memory Man's clean character).
- **UNIQUE knob = RATE** — the chorus↔vibrato LFO rate (slow=chorus, fast=
  vibrato): the Memory Man's "Rate" pot, the slot that used to be Rise. Law
  reused from the Mod-rate LFO; **separate per-mode value** (e.g.
  `delayMemRateHz`) so MemMan and the plain Mod mode remember independent rates.
- **SHARED Mod knob = depth** (the Memory Man's "Depth") — the existing Mod-LFO
  tap-wobble, pointed at the BBD line.
- Distinction from mode 3 "Mod": mode 3 = clear/chorus (plain line); mode 4 =
  **BBD-color**/chorus-vibrato — differ only in line color.
- MODE-SELECTED DEFAULTS: Digital **Ping stays 0** (neutral) and Tape **Heads
  default 1** (single head) — per user 2026-10-06. MemMan Rate default = a
  mid-sweep chorus rate (≈ the current Mod-mode audible-wobble default).
- Same clean-room pins as the BBD: zero flux, zero added sample delay, mild
  drive, causal LFO (the Mod-LFO is already causal + zero-latency).

## Body / low-end (flux drive) ranking — the calibration target
```
1176 (mildest; solid-state, no tubes; UREI cut transformer color over time)
  <  2A (moderate; 80 %-Ni grid saturates early)
  <  670 (hot-working 20 kΩ signal transformer + thump, punch body)
  ≈  STA (richest; two custom Triad windings + power-amp transformer)
```
This is the axis the flat engine was completely missing — it is currently
frequency-FLAT, so it can't do low-end-first body at all.

## Per-mode character (GR device + color stage + flux)
- **1176 / FET** — JFET soft-knee GR (quadratic) + BJT push-pull (odd-dominant)
  + mildest flux. Clean below, smooth-fast, never a hard clip; aggression is
  speed. (NOT a pentode — the 1176 has no tubes.)
- **2A / Opto** — optical GR (photocell, medium/slow) + push-pull "hot output
  stage" (12BH7 + 6AQ5) + moderate early-saturating flux. Warm, weighty.
- **670 / Vari-Mu** — variable-mu GR (6386, smooth deep) + 6973 control-amp
  punch + hot flux/thump. Grabs first, biggest body.
- **STA / Tube** — variable-mu GR (6386, smooth deep) + 6V6GT push-pull output
  + RICHEST flux body. Most low-end.
- **VCA** — none added. Clean benchmark (SSL IC VCA: DBX202/THAT2150 GR @
  ~6 mV/dB soft law, NE5532/NE5534 transparent op-amp CVT stage, TL072/TL074
  JFET detector → no added harmonics, no transformer, no flux). KNEE only.

## Laws (clean-room, published/physics sources; our own constants)
- **Transformer flux**: leaky integrator `L += a·(x − L)` (a from a corner Hz at
  fs), output `y = sat(L) + sat'(L)·(x − L)` — the exact derivative of the
  saturated flux. Low end moves L the most → it saturates first. Causal, one
  float/channel, no differentiator. Per-unit body = a different flux `a` + drive
  in the ranking above.
- **JFET soft knee (1176)**: model the quadratic JFET drain law as the GR
  character — smooth soft rolloff, NOT a hard odd curve. (The "Transistor"
  curve for the FET mode is a JFET quadratic soft rolloff, corrected 2026-10-06
  from the all-solid-state part list.)
- **Variable-mu smooth GR (670, STA)**: the 6386 remote-cutoff gives a continuous,
  deep, no-hard-cutoff gain law; punch comes from speed + amount + body, not a
  stiff knee.
- **Tape (delay)**: NAB pre-emphasis `E(s)=(1+s/ω1)/(1+s/ω2)` → core → exact
  inverse `D=1/E`. Highs reach the core louder → saturate first. D is the exact
  de-emphasis. Two floats/channel, bilinear, prewarped, NO added latency.
  REPLACES the current flat `kTapeSoft=1.25` feedback gain. Around it: the
  RE-201 band (~15 kHz ceiling, per-loop HF roll-off, fixed subtle Baxandall
  shelves) + the EXISTING wow/flutter (1.2 Hz) + heads 1-4, kept unchanged. NO
  transformer flux here (RE-201 is direct-coupled by design — pinned as the one
  flux-free exception). All bilinear 1st/2nd-order → causal, zero sample
  delay. `clipDepth`-style CLIP knob: 0 = no head drive (clean tape, band+tone
  only), noon = historical head drive, 2 = hot heads.
- **Drive-compensation / makeup** already exist in our engine (autoMakeup).

## Acceptance pins (draft — finalise by MEASUREMENT, not by feel)
1. **Low-end-first flux**: at fixed CLIP, distortion at ~60 Hz > at ~500 Hz > at
   ~5 kHz, RATIO ordered by the body ranking (STA/670 widest gap, 1176 narrowest).
   The flat engine currently fails this (it is frequency-flat).
2. **GR-family character**: 1176 shows a JFET-soft (quadratic) knee with a
   fast, never-hard clip; 670/STA show smooth variable-mu deep GR with their
   "grab" reading as speed + body (not a hard corner). The 2A reads weighty.
3. **Zero-latency proof**: every stage's impulse output has zero leading samples;
   the NAB `D·E` product nulls to ~1e-9 (exact inverse).
4. **Alias budget**: full-scale noon drive, out-of-band harmonic leakage above
   20 kHz stays within a small measured bound — if any mode can't meet it in the
   gentle band, lower its default noon drive (do NOT add oversampling).
5. **CLIP law preserved**: `out(amt) = clean + amt·(normal−clean)` still holds
   for the composite stage (noon re-pinned to the new physical `normal`).
7. **BBD-delay pins** (Memory Man-based, DM-2 cross-check): darkening = **HF loss
   per repeat** (repeat N+1 high-end < repeat N) via the existing time↔tone law
   (5000 Hz @250 ms → 50 Hz floor, UNCHANGED); time↔tone keeps "longer = darker"
   (the Deluxe 2×-chips cascade reads as more cumulative loss); drive stays MILD
   (−15 V headroom → low distortion); chorus=slow / vibrato=fast = the Mod knob;
   **zero flux** in the BBD path (assert bit-identical to the law at Chip=0);
   zero added sample delay.
8. **Tape-delay pins** (RE-201-based): measured ceiling ~15 kHz at noon; HF
   content of repeat N+1 < repeat N (thin-out); wow/flutter (1.2 Hz) and
   heads 1-4 UNCHANGED; **zero flux** in the tape path (assert: the tape mode
   output at CLIP=0 equals the head-drive-free band+tone chain only); zero
   added sample delay (all bilinear).
6. **VCA unchanged**: bit-identical to `50ad93a` VCA (no stage, no transformer,
   KNEE only).

## Intentional default-sound change (flag for the user)
Replacing the flat "four constant-sets" with per-unit GR devices + color stages
+ shared flux body changes the **default (noon) coloration** of the four bold
modes — that IS the point (better, more unit-accurate breakup, low-end first).
The old bit-exact noon pins get re-pinned to the new physical `normal` in a
one-time, documented change. The VCA and the clean/neutral modes are untouched.

## NOT in scope (yet)
- 1176 T2 gain-loop feedback coloration as a DISTINCT control (currently folded
  into flux drive + speed + JFET softness). May become a future knob.
- 1176 **JFET vs BJT split as two knobs** (softness vs odd-dominant output):
  the JFET curve = the GR/knee path, the BJT pp = the output stage; a future
  ticket could expose the JFET softness separately.
- 2A "clean/hot" as a SEPARATE detented control (currently the CLIP knob). 
- 670 control-amp transformer (T103/PCO-150) — off the audio path; affects only
  detector speed, which attack/release already covers.
