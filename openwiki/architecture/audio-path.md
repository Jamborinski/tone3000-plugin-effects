---
type: "Reference"
title: "Audio path: chain domain, lanes, block types"
openwiki_generated: true
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T18:58:45.842Z
sources:
  - id: openwiki-source-e2fb14576702431774114c7c
    resource: repo://plugin/include/ChainBlock.h
  - id: openwiki-source-1b69ab64c1d4a1a653d87502
    resource: repo://plugin/include/ChainDomain.h
  - id: openwiki-source-732fd101392931455320572e
    resource: repo://plugin/include/Processor.h
  - id: openwiki-source-f34a3221e7d4d261ce33d341
    resource: repo://plugin/include/RtWorkerPool.h
  - id: openwiki-source-fd29254520bef2ca5ddb33d0
    resource: repo://plugin/src/ProcessorChain.cpp
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
---


# Audio path: chain domain, lanes, block types

TONE3000 processes a user-built chain of blocks (NAM models, impulse
responses, inserts, and self-contained effects) inside a **48 kHz chain
domain** that is decoupled from the host's sample rate, on **left/right
lanes** that a realtime **worker pool** forks and joins. The whole design is
built around one contract: the plugin is **zero-latency** (or at worst the
host-measurable latency it declares), with no hidden buffering.

## The chain-domain boundary

- `plugin/include/ChainDomain.h` fixes the base rate:
  `kChainBaseSampleRate = 48000.0`. The effective chain rate is that times
  the current oversampling factor
  (`TONE3000Processor::chainSampleRate`).
- The boundary resampler is a **stereo Lanczos (filter size 12)**
  `dsp::ResamplingContainer<float, 2, 12>` — "only instantiated when the
  host rate differs from 48 kHz". At 48 kHz hosts the boundary is a
  passthrough, so the chain sees exactly the host stream.
- The boundary + oversampler latency is reported to the host explicitly
  (`Processor.h:~1042`, "Reports boundary + pitch shift latency to the
  host, message thread"), and the powered-off plugin "stays bit-exact and
  zero-latency".
- The boundary is a fixed 2-channel container; a mono host buffer gets a
  second silent channel handed to it (`Processor.h:1046–1047`).

## Oversampling

`ChainOversampler.h` sits between the boundary and the chain stage. Its
factor atomic makes the oversampling factor switchable per callback, with
**factor 1 = transparent passthrough**. The oversampler is min-phase and
contributes **no PDC change** — the PDC guarantee holds for every
supported rate.

## Chain blocks and lanes

`plugin/include/ChainBlock.h` defines the block universe:

- `enum class ChainBlockType { NAM, IR, INSERT, EFFECT }`
- `enum class EffectKind { Delay, Chorus, Tremolo, Compressor, Reverb,
  Convolution }`
- `enum class ChainSide { Left, Right }` — each lane is a parallel,
  independent chain (A/B) sharing the same block layout.
- `kMinLaneSlots = 5`: an empty lane displays 5 empty slots and
  `insertCount == max(kMinLaneSlots - toneCount, 1)` is an enforced
  invariant.
- `kWetFadeSeconds = 0.025` — block swaps, model downloads and preset
  splices are crossfaded with a 25 ms wet fade; `ProcessorChain.cpp`
  performs "mute-splice over the wet fade" where no single block can fade
  (e.g. removing a whole running chain).

### IR chunking law

`kIrConvolverMaxBlockSize = 256` caps the convolution block size: hosts
above that get their stream split by `processConvolverInChunks()`
(`ChainBlock.h:107–116`) into ≤256-frame chunks feeding the convolver, so
the per-call cost of the JUCE convolver's non-uniform OLA tail is bounded.
Hosts at or below the cap get exactly the raw block.

## Scheduling: RtWorkerPool

`plugin/include/RtWorkerPool.h` is the realtime fork/join pool:

- The audio thread **forks** helper workers for (a) lane pairs and (b) NAM
  phase groups; a lane job may fork its NAM block's phases, so the pool
  supports **nesting depth 1**.
- **Deadlock-free by construction**: "a joiner never parks, it claims and
  runs its jobs inline" — if every worker is descheduled, the joiner steals
  jobs back and the callback still finishes on time. The worker/joiner
  race ("whoever wins runs the job") is the documented safety valve.
- Workers run at **realtime priority** (or fall back to normal
  highest-priority threads where RT start is refused), and, when the host
  provides one, they **join the audio thread's workgroup context** —
  re-joined from each worker loop whenever the host changes it (tokens are
  thread-affine), keeping them off efficiency cores.
- The state machine per worker: Free → Building → Armed → Claimed → Done,
  with CAS transitions; wakeups are precise (workers advertise themselves
  in a parked list), not broadcast.

## Zero-latency / PDC invariants (contract)

- The boundary engages **per host rate**, irrespective of chain contents —
  a chain's presence never alters PDC.
- A powered-off plugin is bit-exact and zero-latency
  (`Processor.h:1125`).
- The oversampler is min-phase with factor-1 passthrough: changing the
  factor never silently shifts phase beyond what the declared PDC covers.
- Wet fades are 25 ms and are an *audibility* mechanism (splices), not a
  *latency* mechanism — no block ever buffers ahead of the host frame.
