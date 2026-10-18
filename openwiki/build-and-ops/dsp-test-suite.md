---
type: build-and-ops
title: DSP test suite (DspTests)
description: The GoogleTest suite under test/ that compiles the real plugin sources against shipped fixtures — the canonical ./script/test-dsp.sh loop, the gtest silent-0 comma-filter trap, and the a2-amp / cab-ir / reverb-ir fixture set.
tags: [testing, dsp-tests, googletest, fixtures, script]
sources:
  - id: openwiki-source-55524a0481117f837a7bb064
    resource: repo://docs/tickets/complete/reverb-budget-quality-uplift.md
  - id: openwiki-source-6c72f2be6bf89ccc6f2c645f
    resource: repo://docs/tickets/juce-e2e-suite-draft.md
  - id: openwiki-source-c80500302f4c953f647993ed
    resource: repo://docs/tickets/plate-dat-rebuild-draft.md
  - id: openwiki-source-3f6e8a6ed2edd60ef99f8565
    resource: repo://plugin/ui/testbed/CMakeLists.txt
  - id: openwiki-source-face5087f131b3d99de9f7e7
    resource: repo://plugin/ui/testbed/e2e/node-drag.js
  - id: openwiki-source-fbe20d668cd183c9fb53adb2
    resource: repo://script/test-dsp.sh
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-a20d64964340cf417ace1f84
    resource: repo://test/src/add_at_slot_tests.cpp
  - id: openwiki-source-22db8812ec3771683e6a7ddc
    resource: repo://test/src/dat_probe_tests.cpp
  - id: openwiki-source-9122ffeb8c7fc8e2457955ce
    resource: repo://test/src/effect_tests.cpp
  - id: openwiki-source-949fdcdad6535ec560064216
    resource: repo://test/src/node_model_tests.cpp
  - id: openwiki-source-0368da5a39fa50284e393846
    resource: repo://test/src/plate_family_tests.cpp
  - id: openwiki-source-9ff2ef0f7a4a77aed9113ced
    resource: repo://test/src/plate_texture_tests.cpp
  - id: openwiki-source-a95d9a96cf87c30a610c9c3b
    resource: repo://test/src/processor_tests.cpp
  - id: openwiki-source-eb6e55b68712208f16dad484
    resource: repo://test/src/reverb_ceiling_p0_tests.cpp
  - id: openwiki-source-cbee6d7a85a49f4ea2d7a315
    resource: repo://test/src/reverb_quality_p1_tests.cpp
  - id: openwiki-source-95d19d59abc2626104f1ad90
    resource: repo://test/src/state_size_tests.cpp
  - id: openwiki-source-a9b10161f0531edad6c09d8e
    resource: repo://test/src/strip_knob_tests.cpp
generated: { by: "pi", at: "2026-10-18T23:18:48.824Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-18T23:18:48.824Z
---

# DSP test suite (DspTests)

TONE3000's DSP correctness lives in the GoogleTest suite
(`test/`), built as the `DspTests` target. It is the fast iteration loop for
every audio-side change — UI changes are **not** covered here
(see `/openwiki/systems/ui/`).

## Running it

The canonical entry point:

```bash
./script/test-dsp.sh                 # configure-if-needed → build --target DspTests → exec
./script/test-dsp.sh 'DelayTest.*'   # …with a gtest filter
```

`script/test-dsp.sh` is the whole recipe: configure the build directory if it
does not exist, `cmake --build ... --target DspTests`, then exec the
`DspTests` binary (with the staged-toolchain `LD_LIBRARY_PATH`) passing any
filter argument through.

You can also run the binary directly:

```bash
LD_LIBRARY_PATH=<stage lib dir> ./build/test/DspTests_artefacts/Release/DspTests 'DelayTest.*'
```

## What it covers

One file per subsystem, all in `test/src/`: NAM/IR loading and classification
(`local_load_tests`, `ir_classification_tests`, `padless_wav_tests`), the six
self-contained effects (`effect_tests`, `convolution_reverb_tests`,
`plate_comb_tests`, `plate_texture_tests`, `plate_family_tests`) plus the
2026-10-16 reverb quality probes — the P0 CPU-ceiling guard suite
(`reverb_ceiling_p0_tests`, suite `ReverbCeiling`, 4 tests pinning
23 µs/block@128 per mode with Spring ≤ 46), the P1 six-mode vs
reference-IR baseline table (`reverb_quality_p1_tests`,
`ReverbQuality.TableBaseline_SixModes`, reference IRs under
`/mnt/c/Impulse Responses/Convolution Reverb IRs/`), and the per-mode P2
probe suites (`reverb_probe_tmp.cpp` Plate lift-1.15 ship, `room_probe_tmp.cpp`
Room wash-norm-1.20, `chamber_probe_tmp.cpp` Chamber washFrac-1.60 + the
8.0 kHz cap sweep), — IR convolver cost and block-size law
(`budget_convolver_cost_tests`, `ir_block_size_tests`), the worker pool
(`multicore_tests`, `worker_pool_tests`), latency/PDC
(`latency_tests`, `pitch_shift_tests`), chain behavior
(`branch_tests`, `duplicate_tests`, `dual_mono_tests`, `swap_fade_tests`,
`chain_test_helpers.h`), presets and state (`preset_tests`,
`factory_preset_tests`, `state_cache_tests`, `clipboard_tests`), MIDI
(`midi_map_tests`), host-program behavior (`host_program_tests`), and more.

Reverb guard re-pins from the same pass (user-authorized leeway): the plate
level-law baseline is **93.95** @2000 ms (`plate_texture_tests`,
`plate_family_tests`) and the plate comb cap @2.5 s is **21.98**
(`plate_family_tests`); `plate_comb_tests`' `PlateComb.OtherModesStayBitIdentical`
carries the mode-3 (Room post-wash) pin **162470.64443779 /
162470.64443779 / 1109398.14947898**; `effect_tests.cpp`'s
`ReverbTest.TypeScaffoldIsOnePerModeAndClamped` guards the Digital subtype
scaffold (6 types on mode 5, `typeLawMode` map {5,1,0,2,3,4}, clamps,
`defaultDialsForType` landing contract, non-Digital modes stay
single-type).

**Real-source rule:** the suite compiles the *real* plugin sources
(`plugin/src/*.cpp`, `plugin/include` headers) against the fixtures — never
copies or mocks of the engines. In `test/CMakeLists.txt` the only UI file
pulled in is `plugin/ui/core/Labels.cpp` (the `KnobScale` readout formatter
the scale-contract tests link); all of `plugin/ui`'s GUI sources are excluded,
so a green `DspTests` run does **not** prove the UI links — verify UI changes
with a GUI build target.

## Fixture set

`test/files/` ships the ground truth:

- `a2-am-test-2.nam`, `a2-amp-test.nam`, `a2-amp-cab-test.nam` — NAM A2
  capture models (amp, amp+cab).
- `cab-ir-test.wav`, `cab-ir-test-2.wav` — cabinet impulse responses.
- `reverb-ir-mono-test.wav`, `reverb-ir-stereo-test.wav` — reverb IRs.
- `em240-gold-plate-5s.wav` — the EMT 240 gold-standard plate reference
  (5 s), the basis of the plate mode reference-IR law.

## Silent-0 gtest trap

A comma-separated filter list like
`--gtest_filter='A*,B*'` can run **0 tests and still exit 0** — a green
lie. A single-pattern filter (`'A*'`) always works. **After any gtest run,
confirm the "Running N tests" line is non-zero before trusting the result.**

## Suite size

The suite grows with features — **560 tests as of the 2026-10-20
optimization-findings state-size closeout** (commits `728e144` + `9f34a3f`:
8 `StateSize*` tests in `test/src/state_size_tests.cpp` — the DAW-host state
footprint baseline, the ModelLibrary dedup shape, the gzip flag layer, and
the v1-forward-compat restore — plus the shared `test/src/StateSizeBlob.h`
flag-aware blob parser now used by the state/processor/pitch/gate tests). 552
was the 2026-10-18 node-view bug-batch closeout (commit `72f48ea`: 4
`ProcessorTest` positional-grid tests); 548 / 79 was the 2026-10-17 node-view
bug-batch closeout (commits `0cb035e`–`22bbaf6`: 6 `StripGalleryParity` tests
in `test/src/strip_knob_tests.cpp` — the strip==gallery exact-knob-set law in
both directions + the ≤12 knob cap — and 5 `ProcessorTest.ConvIr*`
preset-persistence tests); 530 / 78 was the 2026-10-16 node-view batch-3
closeout (commit `5b9bdd9`: 8 `StripKnobPosition` tests in
`test/src/strip_knob_tests.cpp` — the identity-scale law, position == raw for
identity-stored Mix/In/Out, + per-kind drive round-trips); 530 / 78 was the 2026-10-16 node-view batch-2 closeout (7 StripKnob per-knob
readout tests in `test/src/strip_knob_tests.cpp` + 2 AddAtSlot lane-gap tests
in `test/src/add_at_slot_tests.cpp`); history: 487 after the 2026-10-15 R2 final cut; 493
after the P2.1 plate density uplift; 503 after the full pass 1 + the
`TypeScaffold` re-scope; 511 / 75 after the T-DAT-001 wave-3 closeout
(2026-10-17, plate/probe suites + StandaloneStateAutosave stub); 512 / 75
after the 2026-10-14 swappable-theme palette test; 521 after the 2026-10-18
node-view closeout (9 NodeModel tests + `views/gallery/EffectParams.cpp`
pulled into the compile)). On a failure or count mismatch, **never assert a
stale total** — check the build RC and that the test names you expect
actually ran.

## juce-end-to-end supplement (2026-10-18, `f0b06d4`)

A juce-end-to-end layer is an **additional UI test layer, not a
replacement** (spec spc-1792273651114-39383 — DspTests, the testbed
selftest, and TileShot goldens stay primary). The C++ client lives
in-tree at `libs/focusrite-e2e/` (wired via
`plugin/ui/testbed/CMakeLists.txt` `add_subdirectory` +
`focusrite-e2e::focusrite-e2e` link into UiTestbed); the JS client is
`plugin/ui/testbed/e2e/` (npm `package.json`, `node_modules/`
gitignored). First regression: `node-drag.js` (35 check() asserts as of the
m06828 batch, grown from the initial 13 — it catches the quarter-ghost /
no-ghost / top-left-box drag-ghost bugs plus the symmetric-swap, drop-glow,
right-press-survives-drag, and positional far-slot drop laws) runs with
`xvfb-run -a node plugin/ui/testbed/e2e/node-drag.js` (`T3K_E2E_APP` env
overrides the app binary). The shipped
plugin binary is untouched — only UiTestbed hosts the e2e port
(`--e2e-test-port=`).
