# Ticket — Long-tail convolution: budgeted boundary cost engine

Status: **COMPLETE** (2026-10-10) — the budgeted-boundary long-IR engine landed as
`BudgetConvolver` (spread-OLA) + the promoted cost probes in
`test/src/budget_convolver_cost_tests.cpp`, per the in-source notes that cite this
ticket. Created 2026-10-08.
Priority: robustness improvement for long IRs (20–120 s)

## Problem

The long-IR engine (JUCE `dsp::Convolution`, non-uniform head 8192 samples,
tail at 8192-sample grid cells) does a heavy multi-segment accumulation once
per grid cell — **once every 8192 input samples (≈170 ms, 5.9 Hz at 48 k)**.
Offline measurement (DspTests probes `CostPeriodProbe` / `CostSpikeCadence`,
5.0 s EMT 240 Gold Plate = really 9.98 s, Length 400 % → 39.93 s engine,
220 Hz tone, blockSize 64, 48 kHz):

| setting | avg/block | p95 | max | max/avg | spike cadence |
|---|---|---|---|---|---|
| x1 (9.98 s) | 31.1 µs | 25.3 µs | 1.33 ms | 43× | — |
| x4 (39.93 s) | 45.3 µs | 25.4 µs | 3.40 ms | 75× | **exactly every 128 blocks (128×64 = 8192 samples)** |

The SPIKE COST SCALES WITH KERNEL LENGTH (more segments in the boundary
re-accumulation): x4's boundary block is ~2.4× x1's.

### Why this matters

- Average load is fine (~0.7 %), but the **worst block** is a 3.1–3.4 ms
  event on a 170 ms cadence.
- The host's target block is ~5 ms: the spike alone is ~62 % of that window.
  In a loaded DAW (other plugins + host overhead in the same window) this is
  the one mechanism that can produce a **periodic** dropped-buffer sputter,
  "worse the longer the IR" — matching the field report on stretched long
  plates.
- The DSP itself is clean (ACF of the served 39.93 s kernel is flat at all
  lags; sustained-tone wet is periodic-free) — this is a **timing/budget**
  ticket, not a signal-quality ticket.

## Goal

Flat per-block cost for long kernels: no multi-ms boundary bursts; average
cost no worse than today; same convolution result (bit-compat within float
re-association). **v1 must not change the audible result.**

## Approaches (evaluate in this order of cheap→heavy)

1. **Partition re-tune** of the non-uniform engine (grid cell ≠ 8192): changes
   spike frequency + average, may not lower the max spike — **benchmark first**,
   keep only if the max improves measurably.
2. **House long-tail engine** with budgeted boundary work: pre-split the
   kernel-segment accumulation across the grid cell (spread the per-boundary
   O(segments) pass over its samples), or a chunked/rotating accumulation that
   never exceeds a per-block budget (e.g. ≤ 300 µs at blockSize 64). Same FFT-OLA
   math; different scheduling.
3. (Fallback, NOT v1) tail at reduced sample rate: **audibly a quality change**
   → only if 1/2 prove infeasible, and only with the export note below and a
   per-length quality setting.

## Export-quality requirement (user directive 2026-10-08)

When export (off-line render) is implemented later: the export path **must
maintain full quality**. If the chosen approach here changes the rendered
waves at all (beyond float re-association) — e.g. any rate/partition/level
law — the implementation must leave a clearly marked
`TODO(export): note the export-quality carve-out here` comment at the point
of divergence, to be **removed after the export feature is implemented**.
A pure timing re-schedule (approaches 1/2) changes nothing audible and needs
no note.

## Definition of done

1. DspTests regression: the cost probes are promoted from TEMP diagnostics to
   permanent tests asserting, for the reference long-kernel config
   (9.98 s IR @ 4×, 48 kHz, block 64/128/256):
   - max block < 1.0 ms (down from 3.40 ms),
   - avg ≤ 50 µs (from 45.3 µs),
   - spike-cadence ACF at the old 8192-cadence lag < 0.05.
2. Existing ConvolutionReverb + ProcessorChain suites green (signals unchanged).
3. Full app + Windows standalone build green.
4. Ticket's benchmark table re-measured and pasted at the close-out commit
   (before/after avg/p95/max/cadence).
5. Export note present **iff** the shipped approach diverges audibly
   (approach 3) — removed when export lands.

## When to start

- A loaded-DAW periodic-sputter report on long IRs reproduces, or
- the export feature ships (long-IR render headroom matters most there), or
- user explicitly schedules it.

## Measurement recipe (repro)

`DspTests --gtest_filter=*CostSpikeCadence*` / `*CostPeriodProbe*` (currently
TEMP diagnostics in `test/src/convolution_reverb_tests.cpp`; the reference
IR is converted to `/tmp/t3kprobe/EMT240_50_raw`. Promote + vendor a small
reference IR fixture with the tests when the ticket starts.)
