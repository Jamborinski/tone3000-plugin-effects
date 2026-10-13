# Ticket — Plate (PLT): replace the default engine with the Dattorro-tank ("DAT") build

Status: **DONE (2026-10-13, T-DAT-001)** — shipped and closed out (log at bottom).

## Ship log (T-DAT-001)
- **Engine** — `plugin/include/Reverb.h`: the DAT (Dattorro) plate tank is
  the **default + only** engine for Plate (mode 0): clean-room **MIT port**
  of `RSAlgorithmicVerb-main` `Source/DattorroVerb.cpp` (MIT — attribution +
  AGENTS.md MIT-port rule at the port site ~:2225), restructured to repo
  style (per-side mono tanks, `DatLine`/`DatChan` rings, `processDat`),
  zero-latency verified (every read is a past write). Dispatch: mode 0 →
  `processDat` + width-gated M/S side stage; the **legacy comb PLT voice
  survives bit-identical** at the Digital PLT subtype (mode 5, type[5]==2,
  Bright/Bloom armed) — pins 93.95 rawAbs @2000 ms / 21.98 comb @2500 ms
  re-targeted there via `plateLegacyP()`/`plateLegacyAt()`.
- **Decay knob law** — `datDec_ = 0.15 + (0.82 − 0.15) ·
  log10(clamp(decayMs,50,2500)/50)/log10(2500/50)`, the knob driving all six
  loop decay sites with a **uniform 0.82 cap** (stability boundary measured
  at 0.88 by 6–16 s energy-growth slope at sizes 0.0/0.5/1.0 — render-length
  dependent; short-render peak caps lie for marginal poles). Allpass gains
  capped <1 (z=−1 pole rule). The chase's "size-0 runaway" root cause was an
  **unzeroed AudioBuffer in the probe harnesses**, not the engine — probe
  buffers are always `clear()`ed now (standing rule in
  `docs/agents/audio-testing.md`). Scaffolding (`s_datDecDrive`, 11 probe
  test files, `s_killMask`) all removed; the 6/6 **DatGuards** suite in
  `test/src/dat_probe_tests.cpp` is the real guard.
- **Face** — the 6x2 PLT face (Row 1 Decay|Pre|LoCut|Tone|Air|Mod; Row 2
  Mix|Early|Size|Space|Width|Out): 8-param stream, LoCut the only new param
  (45 Hz one-pole HPF, 0 = bit-pass; four state places in
  ChainBlock / ProcessorChain ×5 / ProcessorState / ChainState), Dwell hidden
  on the full face, Dry dropped (redundant with Mix+Out), Air/Early as the
  sig slots, `numParams_` full = 8. Goldens re-shot + re-approved (human
  review first).
- **Closeout gates (all green, 2026-10-13):** DspTests **509/509**
  (`./script/test-dsp.sh`); GUI `TONE3000_Standalone` links clean; UiTestbed
  `--selftest` 0 failed (xvfb); TileShot goldens 0 px mismatch; OpenWiki
  update run completed (8 pages reconciled + prose hand-edits in
  `openwiki/systems/effects-invariants.md` and `docs/agents/{dsp-invariants,
  ui-wiring}.md`). CPU ceiling (23 µs) **waived** for this engine (user).
- **Windows stage** — `scripts/win-standalone.sh`: staged
  `/mnt/c/Users/jambo/Downloads/TONE3000-win-20261013-145050/TONE3000.exe`
  (30,100,687 bytes; contains the `reverbLoCut` plumbing string — verified
  with `strings`); **md5 `6bd2266cbb3ac9c22c73c72b314b4921`**.

## Scope
- New PLT engine (Dattorro-tank class, 8-delay FDN), idea-level from
  `research/plate-dat.md`; constants re-derived/tuned per
  `docs/agents/ir-reverb-training.md` (peak-normalised reference, measure
  the reference's own numbers first) — the 2026-10-16 measured Bricasti/EMT
  plate sets below are the tuning ground truth (family-not-IR law per the
  user's reverb directive: same family, not a tight match to any one IR).
- **DAT becomes the PLT mode's DEFAULT engine and REPLACES the current
  comb-based PLT voice in that mode** (user spec, 2026-10-16): the PLT mode
  carries ONLY the new engine — there is NO retained "COMB" type inside the
  PLT mode.
- The legacy comb PLT voice stays reachable through the **Digital mode's
  subtype host**: Digital's PLT entry (`typeLawMode(5, 2) -> 0`) is where
  the current comb PLT lives after the swap. The same split applies to SPR
  (digital's SPR entry keeps the legacy comb SPR). Digital (mode 5) is thus
  the home of the legacy comb voices, and the other five modes become
  their own unique engines in turn.
- Type plumbing is ALREADY SHIPPED (2026-10-16 subtype-host pass, commit
  ca301da): `Reverb::numTypes(mode)` / `typeLawMode(mode,type)` /
  `defaultDialsForType()`, per-mode `Params::type[kNumModes]` (four state
  places done — see `/openwiki/systems/param-chain-wiring.md`), the UI
  `typeCycle_` dropdown (`EffectTile.cpp`, JUCE-9 async popup).
  Today PLT is single-type and PLT type 0 IS the comb engine with the
  140-law tuned constants — the rebuild swaps the PLT-mode law to the new
  DAT engine class (exactly the way mode 5's host law map works today),
  while Digital's own PLT entry keeps the legacy comb law branch.
- **Ripple to check when landing:** Digital's PLT entry must SNAPSHOT the
  legacy comb PLT voice (bit-identical to today's engine) — that is its
  job per the user's spec; decide in the ticket how the routing works when
  the PLT-mode law branch and the digital-mode PLT subtype both exist
  (digital PLT = legacy comb voice, PLT-mode = DAT).
- **HARD RULE — ZERO LATENCY (user, 2026-10-16):** the new DAT engine must
  be zero-latency like everything else in this repo (causal flux-estimator
  OK; NO oversampling). The Dattorro-tank 8-delay FDN is a delay network —
  if the proposed design CANNOT be made zero-latency (e.g. requires
  look-ahead, filtering that adds group delay, or oversampling), **STOP and
  inform the user for a go/no-go decision before building** (per user
  instruction).

## Guards (closeout, in force now after the 2026-10-16 P2.1 density pass)
- **Legacy comb PLT voice** (now living at Digital's PLT entry): bit-
  identical to today's engine (guards:
  plate_texture / plate_family / plate_comb suites as re-pinned 2026-10-16:
  level-law 93.95 @2000 ms, comb cap 21.98 @2500 ms).
- SML/CHM/HAL host + mode entries stay bit-identical
  (`PlateComb.OtherModesStayBitIdentical` pins); note SPR is also re-
  anchored by its own rebuild ticket — its Digital-host snapshot gets its
  own re-pin there.
- New DAT engine: CPU ≤ mode ceiling 23 µs/block@128 (P0 pin), the wide-
  stereo guard from the Bricasti set, the bright-vs-dark slope spread, and
  a measured onset/ER-lattice ladder against the reference sets.
- DspTests full-green, TONE3000_Standalone build, UiTestbed --selftest.

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
