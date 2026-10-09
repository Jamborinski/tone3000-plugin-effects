# HANDOFF — Convolution Reverb (TONE3000)

Session handoff, 2026-07 (pi, jambo). Everything below was verified this
session. Read `AGENTS.md` (index → `docs/agents/*.md`) first and open the
matching sub-rule(s) before touching files.

## Where things stand

- **Engine + tests: DONE and GREEN.** New effect block `ConvolutionReverb`
  (wraps `juce::dsp::Convolution`, house policy) with full DSP contract
  suite: **11/11 pass**, and the full 419-test DspTests binary is green
  (no regressions).
- **Not yet wired into the chain.** No EffectKind, no Processor dispatch,
  no ProcessorState, no UI. That is the primary remaining work item.
- **Untracked (not committed — user has not said "commit"):**
  - `plugin/include/ConvolutionReverb.h`
  - `plugin/src/ConvolutionReverb.cpp`
  - `test/src/convolution_reverb_tests.cpp`
  - modified: `plugin/CMakeLists.txt` (lines ~328–329), `test/CMakeLists.txt`

## Root cause of the long debug (don't re-investigate)

The multi-hour "per-binary dead-zero / first-instance zero / heap lottery"
saga on 1-tap IRs was **our own bug**, not JUCE's:

- `ir.copyFrom(0, 0, eL.data(), 0, outLen)` is a **5-arg call** and binds
  to JUCE's `copyFrom(dest, start, const float*, numSamples, gain)`
  overload **with numSamples = 0** → silent early return → **nothing
  copied**. The "IR" fed to `loadImpulseResponse` was uninitialized heap.
  Contents varied per binary/instance → zero kernel in some, 0.125 garbage
  reuse in others. Explains every anomaly (K/P/Q/M dead binaries,
  first-in-process pattern, R1–R4 "clean" binaries).
- **Fixed** in `plugin/src/ConvolutionReverb.cpp` (`makeEditedState`,
  2× `copyFrom(ch, 0, data, outLen)`) and in the test helper.
- **Standing trap:** JUCE `AudioBuffer::copyFrom` has both 4-arg
  `(dest, start, const float*, numSamples)` and 5-arg
  `(dest, start, const float*, numSamples, gain)`, plus 6-arg buffer
  overloads. If samples "disappeared", suspect overload resolution first.
- All JUCE internals investigated (`isClear` flag, resampler path,
  engine-factory sequence, CrossoverMixer, FFT/OLA sizes) were **cleared
  as suspects**. JUCE is fine. Do not patch vendored JUCE.

## Laws the tests encode (the real contract)

- **Amplitude law:** JUCE normalises per channel by
  `0.125f / sqrt(sum-of-squares)`. For the 2-channel unit delta IR
  (`0.125` tap per channel), each channel's factor is exactly **0.125**,
  so **wet = 0.125 · input** — not identity. Tests assert this.
- **Zero latency** on both engines (uniform short-IR and two-stage
  long-IR); first sample carries h[0]·x[0].
- **Gain knob** is a `juce::SmoothedValue` (house style; `juce::Smoother`
  does not exist in this build) — per-sample exactness is NOT the law;
  tests use anchor + bounds + settled-state.
- Mono IR fans to both channels; quad IR downmixes
  L=(c0+c2)/√2, R=(c1+c3)/√2 (JUCE alone would drop c2/c3).
- Long IR (> `kShortIrMaxSeconds` = 1.0 s) engages the two-stage engine;
  house block cap `kIrConvolverMaxBlockSize = 256` (ChainBlock.h),
  chunked feed via `processConvolverInChunks`.
- `elapseInstallFade` runs **only for long IRs**; do NOT call
  `conv->reset()` after it.
- Export exclusions (runtime-only, not part of edited IR): Pre, Width,
  Mix, dry path.

## Build / test (WSL is truth)

- Build: `bash tmp_convbuild.sh` (repo root) — configure + DspTests, log
  `build/convbuild.log`. **Check `BUILD_RC`, not absence of errors.**
  The script's `BIN=` line grabs a wrong artifact — run the binary
  directly:
  `./build/test/DspTests_artefacts/Release/DspTests --gtest_filter='ConvolutionReverb.*'`
- Stale-binary hazard is real on this 9p/WSL setup: if results
  contradict recent source, verify the rebuild recompiled
  (`ninja` tail shows the .o) before trusting the binary.
- Diagnostic prints in tests: use `stderr` (gtest swallows stdout on
  success).

## API surface (current, from the header)

```
ConvolutionReverb                     // default ctor
bool loadBuffer(juce::AudioBuffer<float>&, double sampleRate, juce::String* error=nullptr)
bool prepare(double sampleRate)       // rebuild + engine at this rate
void process(juce::AudioBuffer<float>&)             // in-place, wet in, wet out
void setParams(Params)                // gain (0dB=0.5?), width, trim window, pitch, fades...
Params struct                         // + static dbToGain(), scaleToPitch()
int   rawChannelCount() const
double editedSeconds() const
bool  usesUniformEngine() const
int   wetLatencySamples() const       // 0 by construction
juce::String lastError() const
```

## Next steps (in order)

1. **Wire into chain** — follow `docs/agents/ui-wiring.md`:
   EffectKind enum, Processor.cpp dispatch, ProcessorState round-trip,
   EffectTile UI (UI is **rolled our own**, not copied from existing
   effects). Add Processor-level tests (defaults, round-trip, bit-exact
   off-bypass) mirroring how Reverb/Pitch decks were wired (see
   `ProcessorTest.*`, `StateCacheTest.*` patterns in test/src).
2. **Spring reverb survey** (parked; evidence in
   `tmp_rv1_survey.txt` + `plugin/docs/reverb-modes.md` +
   `/home/jambo/dev/.research/reverb/spring/RESEARCH.md`). The
   convolution block is now a trustworthy IR ground-truth tool for it
   (`SpringCompare*` tests already compare algo vs IR refs).
3. **Commit** only when the user says so; push only on "push". The many
   `tmp_*` scratch files are untracked deliberately — don't commit them.

## Session rules that bit before (11 rules, condensed)

1. Ground truth = convolved-IR + measurement; ears last.
2. One percept per edit.
3. No binary without the full evidence chain.
4. M8: verify designed gain before chasing "wrong level".
5. Feedback stability: fb·A < 1.
6. Digital@Density0@Mod0 bit-identity anchor.
7. WSL is truth; /tmp is not persistent across sides; NO shell `$`
   inlined in `wsl -e bash -c` (script files instead).
8. Commit only on "commit"; push only on "push".
9. Rule 11: start with the §3.1 measurement pass AS-IS before any change.
10. Never kill/restart Ollama.
11. `timeout` ceiling ~5 min per tool call; long jobs detached + polled.
