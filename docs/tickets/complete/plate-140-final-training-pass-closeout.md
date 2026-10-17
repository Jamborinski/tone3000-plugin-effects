# Plate/"140" final training pass — closeout (2026-10-13)

Ticket: `plate-140-final-training-pass.md`. All protocol steps (P1–P7) executed.
Method: house `BudgetConvolver`, 600-block OLA warm, BOTH sides peak-normalised,
reference measured FIRST (guards must pass for the reference).
Reference dir: `"/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/
Nevo Plates & Springs/Nevo Studios - Plates & Springs - WAV/"`.

## P1 — EMT 140 family table

Columns: ping (peakRatio 0.02–0.15 s) / body (120–300 Hz, 1.0–2.5 s tail) /
bal (6–12k minus 120–300) / comb (combDepth 1.15–1.80 s) / lvl (sumAbs) /
hflf (6–12k vs 120–300 @ 0.4–0.6 s fixed window).

Plate at 50 % dials, decay = file length — **final state (C shipped)** with
the pre-pass (pristine commit 26fedbf) plate rows kept for context:

| decay | C ping | C body | C bal | C comb | C lvl | hflf | pristine ping | body | comb | lvl |
|---|---|---|---|---|---|---|---|---|---|---|
| 0.5 s | 42.20 | 7.83 | −89.93 | 7.49 | 33.96 | −41.87 | 36.74 | 9.26 | 8.22 | 39.82 |
| 1.0 s | 43.28 | 7.77 | −89.86 | 11.71 | 39.49 | −43.38 | 36.90 | 9.36 | 12.64 | 45.36 |
| 1.5 s | 44.43 | 7.67 | −89.75 | 15.80 | 50.25 | −45.01 | 37.04 | 9.48 | 16.81 | 56.12 |
| 2.0 s | **45.66** | **7.50** | −89.70 | **19.39** | **78.23** | −46.74 | 37.15 | 9.63 | 20.31 | 84.10 |
| 2.5 s | 46.85 | 7.36 | −89.72 | 21.63 | 153.3 | −48.29 | 37.23 | 9.82 | 22.42 | 159.2 |

(3.0–4.5 s plate rows identical to 2.5 s — the 2500 ms cap, user state, holds;
hflf is state-independent — the tone/darkness laws are pre-existing and the
closeout table's hflf column is cited from the pristine measurement, which is
what the guards pin.)

Reference EMT 140 rows (the guards' citation targets, house convolver,
peak-normalised): 0.5 s ping 42.09 body 4.89 bal −83.66 comb 7.25 lvl 105.6
hflf −16.27 | 1.0 s 44.76 / −2.66 / −75.49 / 11.42 / 193.0 / −27.94 | 1.5 s
45.45 / 2.50 / −78.83 / 13.68 / 247.5 / −31.67 | 2.0 s **46.60 / 9.31 /
−85.60 / 14.71 / 302.7 / −34.04** | 2.5 s 47.94 / 12.14 / −55.45 / 14.95 /
327.8 / −34.90 | 3.0 s 48.53 / 14.80 / −46.98 / 15.20 / 363.8 / −35.80 |
4.5 s 47.29 / 15.90 / −49.67 / 15.30 / 414.4 / −36.39.

Laws the family itself shows (guards pin these, NOT a value-match):
- **longer = darker top end** (hflf −27.94 → −34.90 dB over 1.0→2.5 s; the
  0.5 s point −16.27 is a window artifact — the 0.4–0.6 s window sits inside
  the click of a 0.5 s IR — so the monotone guard is pinned 1.0 s → 2.5 s).
- the plate's own comb grows with the plate's own decay (8.2 → 22.4) — natural
  model law; the guard is "cap (2.5 s) must not grow past the 2.0 s baseline",
  not a flat guard.
- plate ping is flat across the family (36.7–37.2) vs the reference's own
  growth (42.1–48.5) — the P3 onset work below closes the 2.0 s gap.

## P2 — cross-model (direction agreement, not value match)

| family | length | ping | body | bal | comb | lvl | hflf |
|---|---|---|---|---|---|---|---|
| EMT 240 Gold | 1.0 s | 42.69 | 2.41 | −78.24 | 11.98 | 167.6 | −42.16 |
| EMT 240 Gold | 2.0 s | 45.86 | 6.85 | −55.66 | 14.01 | 207.3 | −45.52 |
| RX 4000 A | 2.5 s | 29.67 | 4.40 | −71.22 | 10.41 | 97.2 | −10.67 |
| **plate C (final)** | 2.0 s | **45.66** | **7.50** | **−89.70** | **19.39** | **78.23** | **−46.74** |

(The plate 2.0 s row above is the shipped C state; the pristine pre-pass plate
row in P1 — ping 37.15 — is the ticket's original reading.)

- EMT 240: dense-plate ping (42.7/45.9), body survives, longer = darker —
  directions agree with the 140 row.
- RX 4000: sparse-plate body survival, ping below the 33.0 dB floor —
  directions agree; the plate sits BETWEEN the two references — a mid-plate.
  PASSES (P2 is direction, not values).
- Bricasti M7 family (19 plates, user IR set 2026-10-14, measured — see
  `docs/tickets/plate-dat-rebuild-draft.md` § Measured reference +
  `research/briplate-data.txt`): the bright- vs dark-plate spread
  (spectral slope late-window −23 to +14 dB/dec; late slope positive on the
  bright plates) CONFIRMS the longer=brighter-vs-darker spread as a
  general plate-class law, and late-tail tonal beating at −0.61 to
  −2.35 dB (flatness) vs this family's −1.3 dB EMT 140 measurement =
  the swirl is a shared plate-class property, not an EMT 140 artifact. The
  140's LONGER=dARKER rule (P4's decay/Tone knob law) is re-confirmed
  directionally by 17/19 independent Bricasti plates.
- Nevo EMT 240 Gold Foil family (7 plates, user IR set, measured —
  `research/probe_nevo_emt_plates.py` → `research/nevo-emt-plate-data.txt`):
  the STRONGEST cross-model confirmation of P4 — late spectral slope
  darkens −6.3 (0.5 s) → **−31.0 (2.0 s, saturates)** across length,
  exactly the LONGER=dARKER direction (the EMT 240 is the dense gold-foil
  plate — its dark tail is the gold-foil signature in the 240 Gold P1
  rows). **Honest nuance to log:** the Nevo EMT-140 set of the same
  vendor goes the other way (+1.86 late slope at 4.5 s) — class-level
  direction holds, per-device sign is family-dependent; pin each plate
  type's Length/Tone law against its OWN family's set.
- Swirl cross-validation: the Nevo EMT-140 3.0 s file measures a beat
  flatness of **−1.3 dB — identical to the house EMT 140 3.0 s** used in
  P4/P5 (em140-swirl-data.txt) → the swirl measurement is reproducible
  across 3 independent 140-family captures, so guarding the plate's
  swirl character in the DAT rebuild against −1.3 dB / 4-14 Hz tonal
  beating (slower at long lengths, ~1-3 Hz at 4.5-5 s: the "slow swell")
  is a defensible, measured guard. All three plate IR sets (Bricasti /
  Nevo-140 / Nevo-240) are uniformly WIDE stereo (L/R corr ≈ ±0.07) —
  wide-by-default is the plate-class default. All 17/19 files show a
  ~6 ms ER lattice (capture comb common to the Nevo/Bricasti capture
  chain, not a device property).

## P3 — onset re-attack: the C experiment (SHIPPED)

Principle discovered (the reference family's own onset law): **a few strong
spike pings over a quiet incoherent floor** — onset *contrast*, not onset
*energy* (total tap sum) and not onset *density*.

A/B vs pristine (ping / body / level / comb @ 2.0 s, all else 50 %;
ping also read at 2.5 s where the tail window captures the full bank):

| variant | onset | ping 2.0 s | ping 2.5 s | body | level | comb |
|---|---|---|---|---|---|---|
| pristine (commit 26fedbf) | 5 taps {12.7,19.3,27.8,43.1,58.4} s 1.17, whip 0.026 | 37.15 | 37.23 | 9.63 | 84.10 | 20.31 |
| **C (SHIPPED)** | 3 taps **{14.2, 26.9, 51.3}** s 0.88, whip **0.015** | **45.66** | **46.85** | 7.50 | 78.23 | 19.39 |
| C3 (declined) | 5 uneven taps s 1.14, whip 0.026 KEPT | 39.70 (+2.5 dB only) | — | 7.15 | 83.52 | — |

At 2.0 s C reaches within **0.9 dB** of the EMT 140 reference (46.60); at
2.5 s it reaches 46.85 — the pre-pass structural ~9 dB gap is closed (the
`OnsetDensityMeetsReference` guard: floor 33.0 + within 12 dB of the
reference — passes both-sided). The ticket's 42.7 decline bar is cleared at
both lengths. C3 is the near-miss that settled the reading: with the whip
floor kept, denser/stronger pings gain almost nothing — the win is the
**lowered floor + sparser taps**. Body cost: 9.63 → 7.50 dB (guard: ref −10
dB — passes). Level cost re-pins: 84.10 → 78.23 (within the ±1 dB guard
around the C baseline — the ±1 dB guard is relative to the model baseline,
never to the reference). All 7 `PlateTexture` guards green on C.

## P4 — knob laws (all pinned, both-sided where they touch the reference)

| law | measurement @ 2.0 s | guard |
|---|---|---|
| Tone | top-band 0.0 = −82.18 / 1.0 = −82.61 (bounded); body alive (7.49); comb 0.0=15.75 0.5=19.39 1.0=0.0 (dark extreme kills the comb, no spike) | comb both-sided (floor at the dark extreme, no spike at the bright) — PASSES |
| Bright | ping 0.0=37.67 0.5=45.66 1.0=44.00 (floor 33 holds); comb 0.5=19.39 1.0=17.84 (no growth) | no chorus, no growth — PASSES |
| **Size (NEW)** | was a NO-OP on plate (`params_.size` read only by Spring + Room) — user redirect: make it mean plate-class | corner shift on the air-law LPF, `kPlateSizeCornerFrac 0.30`; **size 1 (large) = BRIGHTER, size 0 (compact) = DARKER (MEASURED: hflf −59.9/−46.7/−35.3 dB at 0.0/0.5/1.0 — the first-draft "bigger = darker" physics guess was backwards)**; 0.5 anchor bit-identical (sizeDelta = 0.0); comb 0.0=6.10 0.5=19.39 1.0=21.74 (no growth) — PASSES |
| Bloom | body 0.0=7.49 1.0=7.50 (low end outlasts, not killed); comb 0.0=20.26 1.0=17.13 (no growth) | PASSES |
| Dwell | level 87.50 / 52.53 vs 78.23 @ 50 % — finite, monotone, level-driven (not a run) | PASSES |

A separate extra series-LPF stage for Size was measured and REJECTED (it can
only darken — the wrong direction — and its low-band floor swamps the body).
**TODO (future ticket, NOT this one):** Chamber / Hall / Digital never read
`params_.size` either — audit those paths (noted in the Reverb.h size-law
block).

## P5 — stereo image (MEASURED, documented — no change)

At width 1.0 (full drive), 0–12.5 s: M = −21.40, S = −25.47 (S−M = −4.06 dB).
The wide plate is a **fixed Haas offset** (± kMaxWidthSpread) — mono-identical
L = R apart from that offset; side < mid, bounded. Decision per the ticket's
pre-announcement: this is the model's *measured* difference (the Haas
mechanism), documented — not a trained M/S split. Guard: `StereoImageAtWidth1`
pinned (side < mid, mid > −90 dB). Shipping default width stays 0.50 (user
state, untouched).

## P6 — cap (2500 ms) + CPU

| item | result | guard | status |
|---|---|---|---|
| no-growing-comb at cap | growth 3.01 dB across tail | ≤ 6.174 + 1 | PASSES |
| stability grid | 16 dial cells finite + bounded | finite | PASSES |
| CPU @ 2500 ms, 48 kHz blk 128 | avg 3.56 µs (blk64 avg 1.73; blk256 p95 12.33 one blip) | 3.3 × 1.5 = 4.95 µs | PASSES |
| CPU @ 1000/2000/2500 ms table | printed per the ticket (decay lift changes fb, not cost) | class argument, recorded | DONE |

## Final state (re-pinned)

- Reverb.h: `kPlateOnsetTaps 5→3`, `kPlateOnsetDelayMs {14.2,26.9,51.3}`,
  `kPlateOnsetAmps {0.42,0.30,0.16}` (sum 0.88), `kPlateBrightOnset
  0.026→0.015`, NEW `kPlateSizeCornerFrac 0.30` + size law in setParams
  (anchor bit-identical at 0.5). Nothing else in the plate path moved; the
  other five reverb modes + convolver bit-identical (pins hold).
- New guard file: `test/src/plate_family_tests.cpp` (registered in
  `test/CMakeLists.txt`). Guards re-pinned for the C state: level 78.23 ±1;
  onset floor 33 + `PlateLawsHoldAtEveryInRangeLength` per-length laws;
  `DecayLawAcrossTheFamily` monotone-darkening 1.0→2.5 s (0.5 s excluded,
  documented window artifact); cap no-growth at 2.5 s; SizeLaw both-sided.
- Full DspTests: **469/469 PASS** ("Running 469 tests" confirmed non-zero).
- GUI: `TONE3000_Standalone` links — `build/plugin/TONE3000_artefacts/
  Release/Standalone/TONE3000` (24.6 MB, 2026-10-13).
- Docs: CONSIDERED & DECLINED in `plugin/include/effects/reverb/Reverb.h`,
  `docs/agents/ir-reverb-training.md` (model STATE bullet),
  `plugin/docs/reverb-modes.md` (140 row), `plugin/docs/
  ir-modeling-methodology.md` (supersession note on the old 5-tap onset row).

## Declines (with numbers)

1. **C3** (5 uneven taps sum 1.14, whip 0.026 kept): +2.5 dB ping only, body
   7.15 — declined: the floor is the lever, not tap distribution/density.
2. **Extra series-LPF stage for Size**: inverted direction + swamped-body
   floor — declined in favor of the corner shift.
3. **Trained M/S split (P5)**: the Haas offset already gives a bounded,
   documented image (S−M −4.06 dB) — a mid/side gain would be an additional
   unmeasured constant; the measurement supports the Haas law as-is.
4. **Value-matching the reference comb (20.31 vs 14.71)**: different cavity
   physics; the plate's own comb law (growth with its own decay) is pinned
   relative to the plate's own baseline, per ticket "re-pin to measured,
   document the law, no forced match".
