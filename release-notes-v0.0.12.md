# TONE3000 v0.0.12 — Compressor Overhaul

**Release date:** 2026-10-05
**Build:** `0359eee` · based on upstream **v0.0.12-beta**

The compressor got a full, circuit-anchored pass across all five detector
personalities — each now behaves like the hardware it emulates — with a new
**PUNCH** control, a clean **VCA** added to the lineup, and the old generic
Opto replaced by a **Tube-STA** (the Gates STA-level's real tube character).

---

## What's new

### New: PUNCH (was "MBC")
A parallel, lighter comp path (50/50 blend, 3× release, ~half the gain
reduction) available in **every mode**. The body opens back up while the fast
path still grabs peaks — the classic parallel-compression "punch."

- **FET:** slow amp pair opens fully (peak clamp stays, body runs clean, steady
  compression halves).
- **Opto-2A:** a parallel light stage on the hot gain path — lighter reduction
  drives the stage harder, so the body opens *louder and creamier*.
- **Tube-STA:** the Retro "TRIPLE" treatment — the light leg clamps the fast
  transients while the main leg evens the body.

### FET / 1176 (mode 3)
Faithful 1176 model: always-parallel 4-amp channels (fast pair = the transient
clamp, slow pair = body), odd-harmonic saturation that stays clean when quiet,
feedback detection, and a program-dependent slow-pair ratio (up to +3 dB deeper
on loud material). The ratio knob is **depth** (higher = deeper reduction),
anchored on the 4:1 keeper setting.

### Tube-STA / Gates STA-level (mode 1) — replaces the old Opto
The old generic Opto slot is now the 1956 Gates STA-level, per the factory
M5167 manual: a real tube (6386) gain stage whose rectifier taps the *output*
(feedback detection, like the FET and 670). The signature is its
**program-controlled release** — brief peaks recover on the dialed release,
but sustained highs charge a hold that drains ~2.5× slower, so it evens a loud
program without pumping. Ratio = depth (detent 4:1 the keeper), soft-kneed at
the factory's 3.3:1 anchor and saturating toward the unit's ~40 dB ceiling.
The warmth is a light touch: a faint even-leaning tube/transformer body that
grows with the work (spec THD ≤1% even at 30 dB GR — "still sounds like an
acoustic"), and it passes quiet signals clean. Defaults 50 ms / 1 s, from the
manual's measured bands.

### Opto-2A / LA-2A (mode 2)
Now a gain **stage**, not an attenuator — gain reduction happens *inside* a
saturating hot stage (the real 2A wiring), with work-driven even-harmonic
cream, tuned to the real-unit AAM measurements, and −3 dB output trim so it
mixes at musical level. Default attack 40 ms.

### Vari-Mu / Fairchild 670 (mode 4)
A bold 670 character: feedback detection, ratio = depth rolling to a level
ceiling, and odd coloration that *grows* with gain reduction (even-cancel
stage). The release dial is the 670's time-switch range (0.04–25 s, with a
true-seconds readout) and a program-dependent hold above the halfway.

### VCA (mode 0) — new
The classic general-purpose VCA: two-stage RMS detection (50 ms detector on
power, attack/release on the recovered level), feedforward gain, a textbook
soft knee, and a clean multiplier with no added harmonics. The transparent
bus-compressor you'd expect from a true VCA.

---

## Under the hood

- **Detector accuracy:** the 2A cell and FET slow pair now meter **power (x²)**,
  recovered to level — circuit-faithful (a photocell is driven by light ∝ power;
  1176 slow channels are rectifier + RC averages). Sustained material now sits
  ~2–2.5 dB gentler at the same settings, and transients punch through more.
- **Ratio direction fixed** on the soft-law modes (higher = deeper reduction).
- Portable `std::log10` (MSVC-safe), upstream-merge playbook documented, and a
  Windows standalone staging script (`scripts/win-standalone.sh`).
- **Based on upstream v0.0.12-beta** (unified scroll surfaces, EQ chain-rate
  and latency fixes).
- Tests: **292/292 passing** (plus four new Tube-STA behaviors pinned by
  measurement: feedback tap, program-hold recovery, PUNCH body-open,
  coloration band).
