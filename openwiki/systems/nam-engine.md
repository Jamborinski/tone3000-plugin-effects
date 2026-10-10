---
type: "Reference"
title: "NAM engine and model loading"
openwiki_generated: true
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T18:58:45.842Z
sources:
  - id: openwiki-source-7972e348eb71f7e41c3dfcdf
    resource: repo://plugin/include/NamEngine.h
  - id: openwiki-source-b709e43bc3dbc648a9d60234
    resource: repo://plugin/NeuralAmpModelerCore/CMakeLists.txt
  - id: openwiki-source-44860b873f7c76816588a971
    resource: repo://plugin/src/NamEngine.cpp
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
---


# NAM engine and model loading

TONE3000 renders Neural Amp Modeler (NAM) captures through the in-tree
`NeuralAmpModelerCore` (`nam::DSP`). The engine wrapper is
`plugin/include/NamEngine.h`; loading lives in
`plugin/src/ProcessorModelLoader.cpp`.

## Chain-domain hosting

`NamEngine` is a **JUCE-compatible host** for a `nam::DSP` model, running in
the chain domain (`kChainBaseSampleRate × oversampleFactor`; see
`ChainDomain.h`). Sample-rate conversion is **not** this class's job — the
whole chain stage sits behind one resampling boundary + oversampler in the
processor. All `NamEngine` does:

- float ↔ double conversion (NAM models process `NAM_SAMPLE == double`);
- mono processing with fan-out to stereo buffers;
- slimmable (A2 container) tier selection;
- phase-interleaved oversampled processing.

## Phase-interleaved oversampling (the core identity)

> A NAM model oversampled by N with its convolution dilations scaled by N is
> mathematically **identical** to N independent copies of the *unscaled*
> model, each processing every Nth sample of the oversampled stream at the
> native rate: every scaled dilation lands taps exactly N samples apart
> (within one phase), and 1×1 convolutions/activations are per-sample.

So, rather than patching dilation scaling into `NeuralAmpModelerCore`, the
engine holds N instances of the same model and interleaves them. The receptive
field stays constant in seconds (the model sounds the same); its nonlinear
harmonics land in the widened band where the chain's decimation filter removes
them instead of letting them alias — the zero-latency, no-oversampling
contract of this project.

Eligibility (`namConfigIsPhaseSafe` in `plugin/src/ProcessorModelLoader.cpp`):
phase interleaving is exact only for pure (dilated) convolution —
`WaveNet`, `ConvNet`, `Linear`, and `SlimmableContainer` when **every**
submodel is. Recurrent models (LSTM) update state on consecutive samples and
can't phase-split; they get a single instance running time-scaled at the full
chain rate (a defensive path — the catalog and the local-file gate only admit
A2 WaveNets). The loader bakes the counts:
`phaseCount = phaseSafe ? oversampleFactor : 1`,
`instanceCount = phaseCount × voices`, and the apply path re-queues the build
if either requirement moved while the load was in flight.

## Voices (dual mono)

A NAM model is mono, so by default channel 0 goes through voice 0 and is
fanned out to channel 1. With two voices, a second independent set of phase
instances serves channel 1 — two separate mono signals through the same model
(the mono chain's "Dual Mono" input mode). The voice count is fixed at
construction: a dormant voice would resume with stale model history, and
`nam::DSP` has no RT-safe reset. `process(buffer, pool, dualMono)` uses
voice 1 only when asked *and* the buffer is stereo; otherwise it falls back
to the fan-out, so the two schedules never mix channels.

## Multi-core

Every `(voice, phase)` instance is fully independent (separate model,
disjoint I/O buffers), so `process()` forks them as one flat job set across
the processor's `RtWorkerPool` — the same pool that forks the stereo lanes
(one-deep nesting is the pool's supported max; a dual-mono engine runs in
mono chain mode, where no lane fork exists, so the voice×phase fork never
nests). Deinterleave/reinterleave stay on the calling thread; every job
writes only its own buffers, so **parallel output is bit-identical to
serial**. Passing no pool runs the sequential loop.

## A2 gate and slimmable tiers

The runtime is tuned around architecture-2 (A2) captures (48 kHz training
rate, the slimmable tiers, the fast path in
`plugin/NeuralAmpModelerCore/NAM/wavenet/a2_fast.h`), so the local-file
gate (`namConfigIsA2`) admits **only A2**: either a bare A2 WaveNet (validated
by `nam::wavenet::a2_fast::is_a2_shape`) or a `SlimmableContainer` whose
every submodel is one. Non-A2 local files are rejected at load time.

Slimmable size (0.0 = lite, 1.0 = full) is a property of A2 container
(`SlimmableContainer`) models — `setSlimmableSize` is a no-op for
non-container A2 files. NAM tier mappers assign the boundary value to the
tier **above** (a two-tier container selects lite for `[0, 0.5)` and full for
`[0.5, 1.0]`), so the lite request must be **0.0** — 0.5 would select full.
`setSlimmableSize` fans out to **every** phase instance so all phases always
run the same tier, and applies immediately if already prepared.

## Load flow (off-thread prepare / apply)

`TONE3000Processor::prepareBlockModelOffThread` (in
`plugin/src/ProcessorModelLoader.cpp`) runs the NAM work off the audio
thread:

1. Parses the `.nam` JSON **directly from the downloaded bytes** — it never
   round-trips through a temp file, because `nam::get_dsp(
   std::filesystem::path)` built from a JUCE UTF-8 string mis-decodes
   non-ASCII characters (model names, user temp dirs) on Windows and fails
   the load silently.
2. Decides phase/voice counts (`oversampleFactor`, `wantedNamVoices()`,
   `namConfigIsPhaseSafe(config)`), and builds `phaseCount × voices`
   `nam::get_dsp(config)` instances.
3. Enforces **1 input channel and 1 output channel** (a multi-channel NAM
   model is rejected).
4. Builds the `NamEngine`, sets the slimmable size, and calls
   `prepare(domainBlockSize)` — all off-thread; the prepared object is then
   handed to the audio thread via the swap-fade apply path.

## Local-file stash

Drop-loaded local NAM files are **content-addressed** into the app-data
`TONE3000/LocalModels` directory under `FNV-1a64(bytes)` + size (same
root as `PresetManager`), so cache-lost reloads (undo after remove, undo
across a tone swap) can read the stashed copy even if the user's original
file has since moved, and re-drops of the same bytes de-dupe. Validation
(only A2 NAM files, only `.nam` / `.wav` extensions) happens at load time,
not in the background loader: a file that can never load must not surface as
a retry badge. The same rules apply whether the bytes arrive as a base64
array (the DSP tests) or straight from disk (the UI's drop/picker in
`plugin/ui/services/LocalFiles`).
