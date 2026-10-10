---
type: build-and-ops
title: DSP test suite (DspTests)
description: The GoogleTest suite under test/ that compiles the real plugin sources against shipped fixtures — the canonical ./script/test-dsp.sh loop, the gtest silent-0 comma-filter trap, and the a2-amp / cab-ir / reverb-ir fixture set.
tags: [testing, dsp-tests, googletest, fixtures, script]
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T18:58:45.842Z
sources:
  - id: openwiki-source-fbe20d668cd183c9fb53adb2
    resource: repo://script/test-dsp.sh
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
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
`plate_comb_tests`, `plate_texture_tests`), IR convolver cost and block-size
law (`budget_convolver_cost_tests`, `ir_block_size_tests`), the worker pool
(`multicore_tests`, `worker_pool_tests`), latency/PDC
(`latency_tests`, `pitch_shift_tests`), chain behavior
(`branch_tests`, `duplicate_tests`, `dual_mono_tests`, `swap_fade_tests`,
`chain_test_helpers.h`), presets and state (`preset_tests`,
`factory_preset_tests`, `state_cache_tests`, `clipboard_tests`), MIDI
(`midi_map_tests`), host-program behavior (`host_program_tests`), and more.

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

The suite grows with features (~376 tests as of 2026-10-07). On a failure
or count mismatch, **never assert a stale total** — check the build RC and
that the test names you expect actually ran.
