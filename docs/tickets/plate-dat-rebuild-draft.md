# Ticket — Plate: Dattorro-tank ("DAT") engine rebuild (new plate type, default)

Status: **DRAFT / GREENLIT-in-principle** (user, 2026-10-13) — plate engine
rebuild is its OWN ticket, separate from spring and from the
reverb-dry-mod-width-swing work. Research complete: `research/plate-dat.md`
(DAT = second plate TYPE on the el-visio tank, idea-level read; rebuild
authorization: "if you have to move away from the 8 comb + 4 all-pass
approach for the plate I do not care at all, rebuild it if you must").
DAT becomes the **new plate default**; the 140 comb voice stays available as
a type and must remain bit-identical.

## Scope (draft — to be tightened when the ticket is opened)
- New plate engine (Dattorro-tank class, 8-delay FDN), idea-level from
  `research/plate-dat.md`; constants re-derived/tuned per
  `docs/agents/ir-reverb-training.md` (peak-normalised reference, measure
  the reference's own numbers first).
- Plate type cycler gains DAT (new default) / 140 (existing, bit-identical);
  type plumbing already exists (`reverbType[]`, `numTypes()`,
  `defaultDialsForType()`).
- Guards: 140 type bit-identical; other five modes bit-identical; CPU on the
  48 kHz / 128-block budget; DspTests + Standalone + UiTestbed selftest.

## Out of scope (own tickets)
- Spring rebuild (see `docs/tickets/spring-mode-rebuild-draft.md`),
  tape delay (see `docs/tickets/tape-delay-draft.md`), the eventual
  six-voices-under-one-mode consolidation (research/plate-dat.md follow-up 2 —
  may be re-scoped once plate and spring each land).

## Closeout protocol (standard)
DspTests full-green, TONE3000_Standalone build, UiTestbed --selftest,
OpenWiki update, Windows standalone exe stage (`scripts/win-standalone.sh`).

## Measured reference — Bricasti M7 plate IR set (user, measured 2026-10-14)
`research/probe_bricasti_plates.py` → `research/briplate-data.txt` (19 files,
every row labeled MONO/STEREO — all 19 are TRUE stereo, 96 kHz/32f, 1.5-3.1 s).
- **Onset ≈5.4-5.5 ms** (clean tap, much earlier than a spring's input-echo)
- **ER lattice ≈6 ms spacing** in 17/19 files (Crystal 17 deviates:
  30→43→64 ms gaps = sparser, likely more diffusive)
- **Stereo: uniformly WIDE** — full-signal L/R corr −0.06 to +0.06,
  onset-relative early/late corr ≈ ±0.06 (no narrow variant in this set)
- **dBrel (early/mid/late vs peak):** −14 to −19 / −21 to −35 / −54 to −71;
  the late window is consistently ~3× the mid window (strong decay)
- **Spectral slope dB/dec (early→late):** early range −29 to +2; late
  range −23 to +14 → bright plates (Bright 25, Vocal 22, Silver 28, Rich 29)
  stay bright into the tail (late slope ≈ 0 to +1.4, some positive = late
  BRIGHTENS = the "modern" bright-plate signature); dark plates
  (CD Plate B 26, London 23, Small 16, Dense 23) darken progressively
  (late slope −16 to −23) — the 140 family's LONGER=dARKER rule is
  CONFIRMED as a general plate-class law (not EMT-140-specific);
  **EXCEPTION, honestly logged: the Nevo EMT-240 set goes the other way**
  (see below) — class-level direction, not universal per device
- **Late-tail tonal beating** (7 files with tail ≥ 25 % of file, EMT140
  3.0 s method): flatness −0.61 to −2.35 dB vs EMT140 ref −1.3 dB —
  Bricasti M7 plates BEAT as tonally as the EMT 140, or slightly less so;
  top-6 beat frequencies mostly 3-15 Hz (harmonic ladder: 15/13/10.8/8.7/6.5/4.3)
- **Guard-set for the DAT rebuild:** the uniform wide stereo image is a
  guard (the DAT/Dattorro-tank plate should default to wide, not narrow);
  the bright- vs dark-plate spread (slope early → late) is the knob-law
  spread to span; the tonal beating is what a swirl/mod LFO should
  reproduce if/when it is applied, at ~3-15 Hz.

## Measured reference — Nevo EMT 140 / EMT 240 plate IR sets (user, measured 2026-10-14)
`research/probe_nevo_emt_plates.py` → `research/nevo-emt-plate-data.txt`
(10 × EMT 140 0.5-4.5 s + 7 × EMT 240 0.5-5.0 s, every row labeled —
all 17 TRUE stereo, 48 kHz/32f; ER lattice ≈6 ms in all).
- **Length axis (the point of this set):** mid-window dBrel 0.5→4.5 s
  140: −37.2 → −21.2 (decay slower with plate size, expected); 240:
  −35.9 → −21.8 (same). **Measured T60 ≈ 2× the label** (140: 0.5 s label
  → 1.33 s measured, 4.5 s → 8.53 s) — the decay label is a knob setting,
  not the free-decay T60; keep in mind when pinning our own decay knob
  against these references.
- **Slope-vs-length, split between the two families (honest nuance to the
  class-level law):** EMT 240 (gold-foil, denser plate) darkens clearly
  with length: late slope −6.3 (0.5 s) → **−31.0 (2.0 s)** (saturates at
  −26 to −31 through 5 s = LONGER=dARKER strongly); EMT 140 (Nevo capture)
  goes the OTHER way: early slope −3.17 (0.5 s) → **+1.86 (4.5 s)** with
  late slope mild (−2 to −7) — 140-keep-brightness-with-length = closer
  to the Bricasti bright-plate signature; class-level direction =
  longer-plate = darker; per-device direction = family-dependent; when
  pinning the 140 type's Tone/Length law use the 140 set's direction,
  when pinning the 240/gold family use the EMT-240 set.
- **Tonal beating cross-validation:** EMT 140 3.0 s file beat flatness
  = **−1.3 dB — identical to the house EMT 140 3.0 s (em140-swirl-data.txt)**
  → method cross-validated by 3 independent 140-family captures;
  flatness range across all sets: −0.61 to −4.17 dB (short files flatter =
  less tonal, longer files more tonal).
- **NEW FINDING — beat speed drops with length:** 140 3.0 s top-6 beat =
  13.9/6.9/13.7/6.8/7.1/14.0 Hz (tonal, ~4-14 Hz); 140 4.5 s + 240 5.0 s
  top-6 = **0.27/0.54/1.35/2.16/2.97 Hz** (a "slow swell" at the very
  long lengths) → if/when the swirl/mod knob is modeled as beat frequency,
  expect longer plates to want SLOWER swirl; ~10-15 Hz at 2-3 s,
  ~1-3 Hz at 4.5-5 s is the measured spread across both EMT families.
- **Onset grows with length** (140: 1.77 ms @0.5 s → 7.15 ms @4.5 s;
  240: 0.75-14.7 ms) — the physical strike→response latency lengthens
  with tank size, a small but measurable effect on early-reflection timing;
  worth a note in the P3 onset/re-attack ticket context if we ever model
  the plate tank as a physical structure.
- **Stereo: uniformly WIDE in all 17** (L/R corr ≈ −0.05 to +0.07) —
  re-confirms the Bricasti guard (plate = wide by default, not narrow).

## Research references (gitignored, local-only)
`research/plate-dat.md`, `research/README.md` vet table,
`research/em140-swirl-data.txt`, `research/probe_emt_swirl.py`,
`research/probe_bricasti_plates.py`, `research/briplate-data.txt`,
`research/probe_nevo_emt_plates.py`, `research/nevo-emt-plate-data.txt`.
