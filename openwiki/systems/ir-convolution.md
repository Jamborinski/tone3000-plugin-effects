---
type: "Reference"
title: "IR convolution and `BudgetConvolver`"
openwiki_generated: true
sources:
  - id: openwiki-source-e2fb14576702431774114c7c
    resource: repo://plugin/include/ChainBlock.h
  - id: openwiki-source-1b69ab64c1d4a1a653d87502
    resource: repo://plugin/include/ChainDomain.h
  - id: openwiki-source-49cc2f4683cd66fe93498aed
    resource: repo://plugin/include/effects/conv/BudgetConvolver.h
  - id: openwiki-source-98413be46f92c72f6215e812
    resource: repo://plugin/include/effects/conv/ConvolutionReverb.h
  - id: openwiki-source-d8041e84a03828e0d221b0ce
    resource: repo://plugin/src/effects/conv/ConvolutionReverb.cpp
  - id: openwiki-source-fd29254520bef2ca5ddb33d0
    resource: repo://plugin/src/ProcessorChain.cpp
  - id: openwiki-source-8a517d0ba4e262c409b9866c
    resource: repo://plugin/src/ProcessorModelLoader.cpp
  - id: openwiki-source-323fe87bd1df0bede6a732eb
    resource: repo://test/src/convolution_reverb_tests.cpp
  - id: openwiki-source-a95d9a96cf87c30a610c9c3b
    resource: repo://test/src/processor_tests.cpp
generated: { by: "pi", at: "2026-10-18T23:18:48.824Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-20T22:14:33.483Z
---


# IR convolution and `BudgetConvolver`

TONE3000's IR path must convolve kernels from a few ms (cabinet IRs) to
several seconds (reverb plates) with **zero added latency** and a **bounded
per-call CPU cost** — no multi-millisecond RT spikes, no growing delay as the
IR gets longer.

## `BudgetConvolver` (`plugin/include/effects/conv/BudgetConvolver.h`)

The house long-kernel OLA tail. It does the **same per-pair math as JUCE's
non-uniform engine** (real-only FFT 16384; the identical packed-domain
product from `juce_Convolution.cpp`: `ConvolutionEngine::
convolutionProcessingAndAccumulate / prepareForConvolution /
updateSymmetricFrequencyDomainData`, copied verbatim) — the difference is
**schedule only**:

| | JUCE non-uniform (`processSamplesWithAddedLatency`) | `BudgetConvolver` |
|--|--|--|
| **Per boundary call** | FWD FFT + **all (N−1) outstanding frame/segment products** + IFFT in **one** call, once per 8192 input samples (~5.9 Hz at 48 kHz). That is the measured multi-ms spike, scales with N — worse the longer the IR. | One 16384-point real transform + **one** new (frame × seg0) product, in **one** call. IFFT deferred to **next** call. |
| **Steady-state calls** | — | **≤ K old segment products** (K = `ceil(M / callsMax)`, `M = tailSamples/8192 + 1`) per call, bounded by construction — **no call carries more than K products**. |
| **Heaviest call cost** | FWD + N×products + IFFT (all in one call) | **one 16384-point real transform (~0.35 ms 2-ch)**, well under the 1.0 ms hard budget. |

Grid: `kFrame = 8192` samples (input frame = kernel segment),
`kFFT = 16384` (product FFT size), `kMaxCall = 256` (max RT feed slice
per call, from `ChainBlock.h::kIrConvolverMaxBlockSize`).

### Alignment with JUCE (verified verbatim)

The tail delivers block `b` (b ≥ 0) during frame `b+1`; its first 8192
emitted samples are **silent**, so the zero-latency wet onset at sample 0
comes from the **head engine** (the first 8192 kernel taps, served
separately by a JUCE uniform Convolution in `ConvolutionReverb`). Summation
**order** of the N pair products differs from JUCE's ring (that is the point
of the spread schedule); the result is the same sum of the same N products,
so results agree to float re-association — the v1 "same audible result"
contract. One irreducible residual: FWD and IFFT land at two distinct
(period-128-related) subcall offsets, so the ACF of the cost at the old
8192-sample lag is not driven to ~0 — but the rescoped gate
(`max block < 1.0 ms + no block over the old max at the old cadence`)
holds.

### API

```cpp
BudgetConvolver(tailL, tailR, channels, tailSamples)  // tail = full kernel minus head; energy-normalised
process(wet)  // adds wet (tail conv) into buffer; wet.getNumSamples() <= kMaxCall
```

## `ChainBlock.h` — the 256-sample RT block cap

`kIrConvolverMaxBlockSize = 256` (`ChainBlock.h:98`) bounds the RT feed to
`BudgetConvolver::process` — `processConvolverInChunks`
(`ChainBlock.h:107-116`) splits every host-sized block into slices ≤ 256
samples so the **per-call cost cannot inflate** with the host's block size
(the host block is at most ~4096 but the per-callback OLA product budget is
still bounded by K regardless; the slice count just multiplies the cheap
steady-state calls, not the expensive ones).

## Amplitude law: JUCE `Normalise` (energy-based)

**JUCE `Normalise` = energy-based: `0.125 / sqrt(hottest channel energy)`**
— the IR's absolute level means nothing. Two IRs with the same peak
transient but different tails (one short spike, one long diffuse tail)
normalise to the **same perceived level** because energy, not peak, sets
the gain. This is different from the amp-IR path, which uses a **−18 dB
output pad** for short IRs (see
`plugin/src/ProcessorModelLoader.cpp::computeIrNormalizationGain`) and
**no pad** for long IRs.

## `ConvolutionReverb` — the IR-driven reverb

`ConvolutionReverb.h` is the creative layer on top of this engine — the
house plate reverb (vs the FIB-mode Plate in `Reverb.h`), sharing the
same `BudgetConvolver` tail and the same JUCE `Normalise` amplitude law.
Its own additions:

- **Block-size cap** (`kIrConvolverMaxBlockSize = 256` + chunked RT feed)
  — house CPU law, same as the amp-IR path.
- **Load sequence:** load → prepare (drains JUCE's engine build) → **~150 ms
  install-fade warmup** (JUCE's internal dry crossfade elapses off the
  live path, not at first wet — the fresh tail is fed one silence frame at
  rebuild, `ConvolutionReverb.cpp:325-327`; the dedicated message-thread
  `primeSilence` warm-up was removed in the 2026-10-20 optimization-findings
  Stage 5 as a zero-caller).
- **Explicit Start/End trim window** in **seconds** of the raw IR
  (JUCE's built-in Trim is silence-stripping only — the user can cut to a
  chosen time window).
- **User time-stretch** (25 % .. 400 % length scale, log-uniform; JUCE
  re-samples the edited IR to the engine rate — no home resampler).
- **Fade in / Fade out** fractions of the edited IR with a curve exponent
  per ramp.
- **Pre** (0..100 ms wet pre-delay), **Width** (M/S fold), smoothed **Gain**
  (0.5 = 0 dB) — all live on the wet path, none baked into the kernel.
- **Wet soft-knee peak limiter** (the final wet stage, `ConvolutionReverb.cpp:823-827`,
  gated by `clipEnabled_`, NOT applied on the dying crossfade tail): a 1024-entry
  LUT over `log|x|` in `[kClipLogLo, kClipLogHi]` with the smoothstep
  ramp/floor/ceiling law (`kClipFloorDb` lift at low levels, knee at
  `kClipKneeX`, `kClipCeilingGain` ceiling — constants at `ConvolutionReverb.h:177-190`).
  No user knob, not part of state, and deliberately **excluded from IR export**.
  Guarded by `WetClipCurveLaw` + `WetClipInPath` in
  `test/src/convolution_reverb_tests.cpp` (plain 0.3 dB tolerance; the 7
  pre-clip contract tests call `setClipEnabledForTest(false)`).
- **4.0 (quad) → stereo downmix law** at load:
  `L = (c0 + c2)/√2, R = (c1 + c3)/√2` — JUCE itself reads only channels
  0/1 and would silently drop the rear pair, so the fold happens here first.

## Kernel export (`exportKernel`, commit 89afb4e)

`ConvolutionReverb::exportKernel(targetRate, &error)` (`ConvolutionReverb.h:328`,
`ConvolutionReverb.cpp:390`) produces the IR exactly as the live engine sees
it, at the target sample rate: it re-runs the SAME linear `makeEditedState`
pipeline — Start/End trim, Time stretch, fade in/out + curves — at the raw
file rate, resamples to the target, then bakes the remaining linear stages:
**Pre** zero-pad, **Gain** (`pow(10, gainToDb/20)`), **Width** (stereo M/S
fold; a mono IR becomes the Haas interaural-delay pair). **Mod** and the wet
soft clip are live-only and deliberately not baked. The result is always
2-channel (mono duplicated). `TONE3000Processor::exportConvIr`
(`plugin/src/ProcessorChain.cpp:481`) wraps it for the UI: bits clamped
16..32, `WavAudioFormat` writer over the file stream (the
`juce::MemoryOutputStream` 0-byte trap class), the file deleted on failure.
Guarded by `ConvIrExport*` tests (`test/src/convolution_reverb_tests.cpp`) and
an `exportConvIr` WAV round-trip (`test/src/processor_tests.cpp`).

## IR short/long classification

From `plugin/src/ProcessorModelLoader.cpp`:

- **`kMaxIrSeconds = 10.0`** — hard cap on loaded IR length (bounds memory
  and engine-build time for arbitrary downloads while comfortably covering
  any published reverb IR).
- **`kShortIrMaxSeconds = 1.0`** (→ `kShortIrMaxBaseSamples = 1 × 48 kHz =
  48 000` samples at the base rate) — IRs at or below this are **short**
  (cabs): uniform zero-latency engine, **−18 dB output pad**, **100 %
  default mix**. Above 1 s they are **long** (reverbs): non-uniform engine,
  **no output pad**, **50 % default mix**.
- **`kIrNonUniformHeadSamples = 8192`** (~170 ms) — the head engine size
  (the first 8192 taps, served separately by a JUCE uniform Convolution;
  the tail beyond that goes to `BudgetConvolver`).
- **The tone's catalog gear tag wins where it is unambiguous**
  (`irIsLongFor`): a tone tagged `"cab"` is always short however much room
  tail it carries (github issue #89: a 389 ms chamber, and a cab whose
  fade-out dipped under the trim floor just inside the cutoff, both flipped
  on length alone and landed 18 dB and a mix default apart from their
  siblings). Everything untagged falls back to length.

## Cross-references

- `/openwiki/architecture/audio-path.md` — the 48 k chain domain, oversampler,
  lanes, and how the IR convolver is a base-rate island inside it.
- `/openwiki/systems/effects-invariants.md` — the algorithmic Reverb (6
  FIB modes) and the plate/"140" tuning against the house convolver.
- `docs/tickets/longtail-conv-cost.md` — the original CPU-cost ticket and
  its definition of done (both hold: `max block < 1.0 ms` + `no block over
  the old max at the old cadence`).
