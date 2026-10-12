# Ticket — Spring: mode rebuild with a real spring character

Status: **DRAFT / GREENLIT-in-principle** (user, 2026-10-13) — spring
rebuild is its OWN ticket, separate from plate and from the
reverb-dry-mod-width-swing work. Research complete: `research/spring-ventris.md`
(170 lines of source analysis + a tape-delay section belonging to the
tape ticket). No code yet; today's Spring mode is one of the six constant
sets on the shared 8-comb+4-AP engine — which is precisely the comb
structure the Ventris/Chidlaw source identifies as the "metal" artifact.

## Candidate architectures (resourced, idea-level, all clean-room)
1. **PARKER 2011 parametric spring (STRONGEST — added 2026-10-14 from two
   academic papers, `research/spring-papers.md`):** 2-band model —
   low band = plain delay below the geometry-determined transition
   frequency fC, high band = stretched all-pass "spectral delay" cascades
   (the chirp); chirp-straightening (k→2k coefficient stretch + sign flip)
   halves the cascade; Linkwitz-Riley crossover at fC/2 with
   time-aligned all-passes; low band can run multirate at fs/4 (10th-order
   2 dB Chebyshev LP anti-alias/reconstruction); high band is 30+ dB below
   the main chirps → optional drop tier. Paper's own cost: 1023 →
   355 mults/sample — fits the 48 kHz / 128-block budget easily (plate
   guard today 4.95 µs).
2. **Ventris-style electronic:** 2 detuned springs (30/41 ms class) +
   crosstalk, per-spring delay banks, HFD all-pass cascades (expensive,
   ⅓–½ of the compute per Chidlaw) — implementable as two parallel
   Parker chains with different geometry parameters.
3. **Compounding-HFD loop (cheapest first step):** feedback loop with
   progressive ~4 kHz low-pass per round trip → high-frequency lag
   compounds per cycle; measure this against a spring IR first
   (mechanism-1 in spring-ventris.md synthesis).
4. **Detuned-resonant-loop model** (local reference structural read):
   4 incommensurate loops + shared allpass cascade + multirate loop +
   fixed Chebyshev tone LP + soft tanh — cheapest of the full models,
   anti-comb by incommensurate detune.
5. **NOT our lane:** mechanical nonlinear resonator model (Pulsar lane) —
   our plugin stays linear (AGENTS.md hard rule).

## Measured reference — user IR set (2026-10-14)
`research/probe_spring_refs.py` → `research/spring-refs-data.txt` (41 files,
every row labeled MONO/STEREO, onset-relative windows).

**File inventory:**
- Nevo BX20 — 12 mono (Tank 1/2 × 2.0-4.5 s, 48k/32f) + 8 stereo
  (2.0-4.5 s + SPL-TD ×2, 48k/32f)
- Cupwise Demeter RV1 — 6 mono (MonoMix 1/2 × (1)/(2)/(3), HPoff, 96k/32f)
  + 3 stereo (St × (1)/(2)/(3), HPoff, 96k/32f); **NOTE:** ~7.4 s silent
  preamble before the echo; usable signal window is very short
  (probe flags as onset-relative)
- Fender 1964 Super Reverb — 8 mono (Mono1-8, 44.1k/24b) + 4 stereo
  (FenderSuperReverb1-4, 44.1k/24b); TAPE files excluded

**Shared comb structure (all 3 families, all files):**
- First arrival (onset): 0.02-10 ms (Nevo 1-11 ms; Fender 0.02-1.84 ms;
  RV1 = end-of-file, see note)
- Spacings: **near-periodic ≈6 ms ladder** in every file (6.0-6.3 ms)
  — this is the spring comb, NOT an incommensurate non-repeating lattice;
  the "metal" artifact = this even comb, which the ticket wants to break
  with chirp/detune
- dBrel early (t0+5..+60 ms) vs peak:
  Nevo Tank1 −11.7 to −16.1; Tank2 −13.8 to −19.3; Fender −13.7 to −17.4;
  RV1 mono −25.5 to −26.5 (darker)
- dBrel late (t0+400..+1500 ms) vs peak:
  Nevo −25.6 to −31.2; Fender −20.7 to −26.3; RV1 not measurable (file too short)
- Spectral slope dB/dec early (60-1200 ms after onset):
  Nevo Tank1 −4.4 to −7.8; Tank2 −15.1 to −19.8 (darker); Fender −27.9 to −35.6
  (darkest) — the HFD signature: Fender darkens fast (HFD = the
  compound-LP mechanism in spring-ventris.md synthesis), Tank1 stays
  brighter → matches Ventris "HFD ≈ ⅓-½ of budget" claim for Tank1

**Stereo channel character (L/R contrast):**
- Nevo STEREO (all 8 files): **TRUE stereo**, full-signal L/R corr ≈ 0.00
  (−0.017 to +0.000), early-window corr −0.008 to −0.036, late-window corr
  −0.034 to +0.019 → fully decorrelated channels = wide width / two
  independent tanks; R-first vs L-first = −0.04 to +0.02 ms (no per-head offset)
- Cupwise RV1 STEREO (all 3): TRUE stereo (corr ≈ 0.0007), R−L = −0.19 ms;
  usable signal window too short for reliable corr measurement (file ends at onset)
- Fender STEREO (all 4 files): TRUE stereo (full-signal corr 0.18-0.27),
  L/R early-window corr: **−0.21 to None**, late-window corr 0.74 to 0.75
  → late tail HIGHLY correlated (R≈L) = narrow/mono-ish width, opposite of Nevo;
  R−L = +0.11 ms (stereo 1, 2, 4) / −1.11 ms (stereo 3, different onset timing)

**Tank1 vs Tank2 (dual-tank signature, Nevo mono pair):**
Both have the same ~6 ms comb; Tank 2 is darker (slope −15 to −20 dB/dec
vs Tank1 −4 to −8) and has slightly slower spacing (6.10-6.13 vs 6.00-6.06 ms)
= incommensurate detune confirmed by measurement, not just Ventris claim.

**Guard set (for tuning):** the ~6 ms comb spacing is NOT the guard; the
guard is the chirp shape (freq-dependent delay slope), Tank vs Tank detune,
stereo decorrelation contrast (Nevo wide vs Fender narrow), and the HFD
curvature (Fender −35 dB/dec vs Tank1 −8 dB/dec = HFD range).

## Discipline
Local reference: **reference freely, don't copy** (user, 2026-10-13) —
paths/mechanisms/idea-level constants may be referenced in research notes;
verbatim copying into the plugin is the line; comments rewritten; constants
re-derived against our own measurements.

## Out of scope (own tickets)
Plate rebuild (`docs/tickets/plate-dat-rebuild-draft.md`), tape delay
(`docs/tickets/tape-delay-draft.md`), mode consolidation (plate-dat.md
follow-up 2 — re-scoped once plate + spring land).

## Closeout protocol (standard)
DspTests full-green, TONE3000_Standalone build, UiTestbed --selftest,
OpenWiki update, Windows standalone exe stage.

## Research references (gitignored, local-only)
`research/spring-ventris.md` (3 video sources + local structural read),
`research/README.md` vet table.
