---
type: build-and-ops
title: DSP test suite (DspTests)
description: The GoogleTest suite under test/ that compiles the real plugin sources against shipped fixtures — the canonical ./script/test-dsp.sh loop, the gtest silent-0 comma-filter trap, and the a2-amp / cab-ir / reverb-ir fixture set.
tags: [testing, dsp-tests, googletest, fixtures, script]
sources:
  - id: openwiki-source-8037e2358a2c4f9b2c722a11
    resource: repo://AGENTS.md
  - id: openwiki-source-55524a0481117f837a7bb064
    resource: repo://docs/tickets/complete/reverb-budget-quality-uplift.md
  - id: openwiki-source-c80500302f4c953f647993ed
    resource: repo://docs/tickets/plate-dat-rebuild-draft.md
  - id: openwiki-source-fbe20d668cd183c9fb53adb2
    resource: repo://script/test-dsp.sh
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-22db8812ec3771683e6a7ddc
    resource: repo://test/src/dat_probe_tests.cpp
  - id: openwiki-source-9122ffeb8c7fc8e2457955ce
    resource: repo://test/src/effect_tests.cpp
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
generated: { by: "pi", at: "2026-10-12T23:07:19.188Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-15T16:15:57.360Z
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

The suite grows with features — **503 tests as of the 2026-10-16
reverb-budget-quality-uplift + Digital-subtype closeout** (487 after the
2026-10-15 R2 final cut; 493 after the P2.1 plate density uplift; 503 after
the full pass 1 + the `TypeScaffold` re-scope). On a failure or count
mismatch, **never assert a stale total** — check the build RC and that the
test names you expect actually ran.
