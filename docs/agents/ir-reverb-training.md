# Convolved-IR reverb training — TONE3000

> **Load before:** tuning any *algorithmic* reverb mode (Plate today; the
> same recipe is the intended path for Spring / Digital / Chamber / Hall) by
> comparing it against a **convolved IR reference**; adding a reference-IR
> test, a comb/decay metric, or a plate-vs-conv A/B bench. Also when a
> "closer to the reference" change is proposed for `plugin/include/Reverb.h`.
> **The one line that matters:** the reference is convolved **through the
> house `BudgetConvolver`** (not an external FFT conv), **both sides are
> peak-normalised before any shape metric**, and the **reference's OWN numbers
> are measured first** — those become the guard's guardrail, because the
> reference will violate whatever ideal you assume (the EMT 140 itself drifts
> +4.85 dB and carries 4.3 dB of steady comb).
> **OpenWiki mirrors (descriptive):** [`openwiki/systems/effects-invariants.md`](../../openwiki/systems/effects-invariants.md)
> (the plate/"140" law + CONSIDERED & DECLINED ledger) and
> [`openwiki/systems/ir-convolution.md`](../../openwiki/systems/ir-convolution.md) (the house BudgetConvolver the reference is run through) — this file stays the TRAINING-PROTOCOL rule set.

(AGENTS.md sub-rule — the index is the repo-root `AGENTS.md`. The plate
instance of this training is documented in `docs/tickets/complete/plate-combing.md`
(tickets are untracked review material) and its close-out.)

## What "training" means here
Closing the gap between a hand-written FIB/comb reverb mode (a set of delay
lines + feedback + diffusion) and a real plate captured as an impulse response,
using the reference IR — driven through our own convolution tail — as the
ground-truth model. The deliverable is a **measured A/B** (metric table vs
baseline AND vs the convolved reference) plus **guard tests** that the
reference itself satisfies, plus a **CPU table**. Not a vibes pass.

## Step 1 — Pick + ground the reference (never trust a path from memory)
- The reference IR lives on the Windows `C:` drive, seen from WSL as
  `/mnt/c/...`. A representative plate:
  `C:\Impulse Responses\Convolution Reverb IRs\Nevo Studios\Nevo Plates & Springs\
   Nevo Studios - Plates & Springs - WAV\EMT 140 - Plate\NEVO - EMT 140, 2.0s.wav`
  (pick the mode's own reference; keep the exact file + duration noted in the
  ticket so the numbers are reproducible).
- **Confirm it exists before building the test** (`juce::File(...).existsAsFile()`);
  if absent, `GTEST_SKIP()` the reference tests so the plate-only invariants
  still run (a hard fail on CI for a missing artist file is a harness bug).

## Step 2 — Convolve with the HOUSE engine (this is the whole point)
Use `ConvolutionReverb` (head = JUCE uniform, tail = `BudgetConvolver`), NOT a
foreign FFT convolver. Reason: you are proving the algorithmic mode matches the
plate *as perceived through our own convolution path*, and you control for
convolver artifacts, latency, and CPU. Loading (the pattern that works):
```cpp
juce::AudioFormatManager fm; fm.registerBasicFormats();
auto reader = fm.createReaderFor(juce::File(wavPath));          // may be 1- or 2-ch
juce::AudioBuffer<float> src(std::min(2, reader->numChannels),
                             (int)reader->lengthInSamples);      // JUCE 9: lengthInSamples
reader->read(&src, 0, src.getNumSamples(), 0 /*startSample*/, true /*avoidClickUpfront*/, true /*allowResampling*/);
ConvolutionReverb fx;
fx.loadBuffer(src, reader->sampleRate);   // stereo IR -> L=left, R=right, internally
fx.prepare(sampleRate);
// warm the OLA schedule to steady state BEFORE measuring anything:
juce::AudioBuffer<float> buf(1, 128);
for (int b = 0; b < 600; ++b) { buf.clear(); fx.process(buf); }
```
Drive it **mono** (a 1-channel `AudioBuffer` in `process`) exactly like the
production lane; that is the apples-to-apples path for the algorithmic mode,
which is also mono in the test. (If a mono IR is loaded into a stereo engine,
the missing channel's pointer is null — see the null-safety landed in
`ConvolutionReverb.cpp`; don't `getRight()` a mono source and deref it.)

## Step 3 — Drive set (same input into BOTH the mode and the convolver)
Three drives catch three different failure classes. Generate them once, reuse
the exact same vector for both sides, and keep a deterministic PRNG (xorshift,
fixed seed) so the numbers reproduce:
- **Click** — a short ~30-sample burst. Exposes **aligned-tap comb** and the
  **onset** ("ping" density in the 2–150 ms region). The metallic ringing the
  ear complains about usually shows up here first.
- **Noise** — ~1 s of white noise into the reverb, analyse the ~1 s tail.
  Exposes **steady-state comb depth** (spectral flatness) — the sustained
  "shimmery/notchy" character.
- **Sweep** — a slow 40 Hz → 15 kHz log sweep. Exposes **band-level character**
  (body at 150–800 Hz, HF rolloff) — where "the plate loses body / is too dark"
  shows up as band levels, not peakiness.

## Step 4 — Measure the REFERENCE FIRST (the guardrail step)
Before touching the engine, print the reference's own numbers on every metric.
**Two of the laws you want will be things the reference itself violates**, and
your guard tests must be written to *pass for the reference*, otherwise you will
"fix" the mode away from the target or write an assertion no real plate can
satisfy. (Plate session: "no growing comb" and "flattest comb wins" both failed
the EMT 140 — so the guards became `growth ≤ captured baseline + 1 dB` rather
than "zero growth".)

## Step 5 — Metric families (several; each catches a different bug)
All on **peak-normalised** copies (level is a separate pin, never mixed into a
shape metric). Analysis: JUCE `juce::dsp::FFT<11>` (2048, Hann, 50 % overlap,
200 Hz…14 kHz window). JUCE 9 real-window API:
```cpp
fft.performFrequencyOnlyForwardTransform(d.data(), /*useDeInterleavedInput=*/true);
// d[0..N) is the windowed time data; read MAGNITUDE bins d[1..N/2] (k=1 is the
// DC-adjacent bin, skip it).  bin k  <->  freq k*fs/N.
```
Metric families worth pinning (name the one that carries the assertion):
- **comb depth / peakiness** — `mean |dB_spectra - median|` over averaged
  windows (a diffuse plate = a few dB; an aligned comb = tens of dB).
- **HF/LF slope** — dB/s of a HF band (e.g. 6–12 kHz) vs an LF band (120–300 Hz)
  after the drive. *Law: a real plate's HF decays faster than its LF* — this is
  the single most diagnostic "is it plate-like" number.
- **onset ping ratio** — 2–150 ms region, peak-vs-band energy. Catches
  diffusion that smeared the whip (too much = toward chorus, a failure).
- **body level** — 150–300 Hz (or 500–800 Hz) survival at 1–2 s. Catches a
  retune that "flattened the comb" by just **killing the lows** (a flat comb
  that came from dropping level or destroying the body is not a win).
- **decay-track (parameter response)** — shorten decay, the tail must get
  DARKER (lower HF/LF). A mode where shorter decay is *brighter* is inverted.
Bake the **baseline** (current mode) fingerprints at full precision **before**
edits, and 15-digit pins for every OTHER reverb mode (they share `Reverb.h`;
an incidental change shows up in a pin, not in the plate metric).

## Step 6 — Apply ONE lever at a time (A/B discipline)
The ticket's ranked levers for a plate (generalises as "damping → diffusion →
mixing → modulation"), in order of impact:
1. **Frequency-dependent damping** — an LPF in the feedback loop, cutoff
   tracking decay (flat feedback = metallic HF combs ringing out).
2. **Diffusion density** — more APF on the output (2 → 4–6 per channel).
3. **Mixing matrix** — Hadamard / orthogonal mix between combs and APFs.
4. **Slight delay-time modulation** — depth well under 1 %, few Hz; too much =
   **chorus, a declared failure**.
5. **Dispersion** — bands with different delay/damping.
Rules that keep the A/B honest:
- **Level-match by construction** (unit-gain APFs, Parseval-scaled matrices),
  not by hunting a gain constant — otherwise a "flatter comb" is just quieter,
  and a "better" number is a level artifact, not a shape win.
- One lever at a time against **both** the baked baseline AND the convolved
  reference. A number only counts if body (Step 5) and level are preserved.
- `git stash push -- plugin/include/Reverb.h` → rebuild → measure → `git stash
  pop` is the fast baseline/retune swing; scope the stash to the ONE engine
  file so the test harness stays in place.
- Record the DECLINED levers with their numbers in the same place as the law
  (the `CONSIDERED & DECLINED` block, e.g. beside `kPlateWashAp` in
  `Reverb.h`) + the close-out, so nobody re-tries a rejected lever blind.

## Step 7 — CPU bench (block-cost, both rates; convolver included)
Warm 64 then time 256 **block** calls (avg / p95 / max), 48 kHz AND 96 kHz ×
block 64 / 128 / 256, for: the algorithmic mode, the convolved-IR reference,
and (if relevant) plate+IR. A block is *not* a fixed cost — p95 can be ~9× avg
on the convolver — so report both. Compare µs **and** µs/sample. Note the
algorithmic plate is typically ~10× cheaper than the very convolver it is
compared against, so "CPU headroom is fine" is almost never the limiter.

## Step 8 — Definition of done (commit the guard, note the table)
- **Measurable improvement** vs the baked baseline AND vs the reference,
  stated with the metric name + numbers.
- **Guard tests** that the *reference itself satisfies* (so they can't reject
  a truly-plate-like mode); a stale total is a bug, not a pass.
- **Other modes bit-identical** (pins unchanged) — the retune is scoped to the
  one mode.
- **CPU table** (per-lever + final) recorded in the close-out.
- **Full DspTests green** (never trust a "green" unless the "Running N tests"
  line is non-zero — see the silent-0 trap) **and** the GUI links (DspTests pulls in only
  `plugin/ui/core/Labels.cpp`; build `TONE3000_Standalone`).
- Close-out written (A/B matrix + CONSIDERED & DECLINED + CPU); commit the
  guard tests, keep the close-out untracked unless asked.
- **AGENTS.md “On ticket completion”:** on close-out, also run the OpenWiki
  UPDATE (`openwiki_begin` mode="update") so `openwiki/` reflects the change —
  the update path is still UNDER CONSTRUCTION as of 2026-10-10.

## Harness landmines (hit in the plate session — each cost time)
- **gtest silent-0:** `--gtest_filter='A*,B*'` (comma list) can run **0 tests
  with exit 0** while a single `A*` runs. Confirm the "Running N tests" line is
  non-zero before believing any result.
- **JUCE 9 API drift:** FFT = `performFrequencyOnlyForwardTransform`
  (NOT `performRealForward` / `realForwardUnwindowed`); reader =
  `lengthInSamples` (NOT `lengthOfDataInFrames`).
- **C++:** no local `function` definitions inside a `TEST(...)` body (use
  lambdas); a lambda capturing `bool ok` **by value** while "repairing" an
  out-param makes the caller see `false` — pass the flag by reference; mix of
  `float`/`double` in `std::max` needs `static_cast`; accumulate `EXPECT_*` and
  assert **after** a loop (loop guards).
- **Build env:** cmake-4.2.3 bin dir MUST lead `PATH` + `LD_LIBRARY_PATH` must
  be exported (see `linux-build-deep.md`); after a Windows-side edit of a WSL
  repo file, `touch` it (9p mtime) or ninja says "no work to do" and you test a
  stale binary. `sudo` via `echo '…' | sudo -S -p '' cmd 2>/dev/null`.

## MODEL/SUBTYPE (type) tuning rule -- GENERAL (any effect mode or subtype)
- A mode (effect) and its model/subtype (type: the `Params::type[mode]` letters,
  `numTypes()`, `defaultDialsForType()`, and the UI type chip (e.g. the reverb
  "140" plate chip = the EMT 140 model)) MAY carry their own knob defaults and
  their own tone law, tuned against that model's reference -- permissive (CAN,
  not MUST: a type may perfectly well share the generic mechanism). When a model
  HAS been tuned to a reference, its rules are kept SEPARATE:
  - model-specific constants/laws live on that mode's path (plate = `case 2` +
    `kPlate*`); if a second type of the mode appears, its tuning goes on that
    type (`type[mode]` / `defaultDialsForType()`) and the first type's (the
    mode's generic) path stays untouched;
  - it never overrides the generic/shared mechanism (shared comb lines, norm /
    wash, the convolver, the OTHER modes -- bit-identical pins keep them
    separate) and never leaks into another mode;
  - the SAME rule applies to every effect family: delay subtypes
    (Straight/Mod/BBD/Mem), the compressor subtypes, chorus/tape subtypes --
    each mode/subtype may have its own defaults and law (tuned to ITS reference:
    IR family or published spec) provided the generic path stays separate.
- **Tuning-state rule (reverb vs an IR reference):** tune at 50 % NEUTRAL dials
  (tone/size/width = 0.50; the old asymmetric 60 % starting row was an unsymmetric
  guess, not data), decay = the reference's own LENGTH (EMT 140 2.0 s = 2000 ms),
  and width per the reference's channel count at MEASUREMENT time (mono IR ->
  0.0, stereo IR -> 1.0; the EMT 140 reference is stereo, so the guards run at
  width 1.0 while the SHIPPING default stays 0.50). Scope = the reverb EFFECT
  path only -- the convolver / plate-tape stay bit-exact (user rule).
- **Plate/"140" model STATE (2026-10-13 vs EMT 140 2.0 s through the house
  BudgetConvolver, peak-normalised):** onset ping 35.4-37.2 vs reference 46.6
  (STRUCTURAL residual: our onset = whip burst + 5 onset pings + the early comb;
  the reference = a physical cavity with dense real early reflections; denser
  pings were MEASURED to thin the ping further (L1: 42.7 -> 29.5; Hadamard:
  42.7 -> 25.1) -- guarded at floor 33.0 + within 10 dB); body 120-300 = 9.63
  vs 9.31 (the plate BEATS the reference: the 50 % NUTRAL rebalance itself
  reproduces the EMT body, so a body BOOST was unneeded and REMOVED -- the
  original "body dead" complaint was an artifact of the old 60 % dials); band
  balance (6-12k minus 120-300) plate -93.0 vs -85.6 (the air law darkened the
  top band -- the plate is now COOLER than the reference); decay law: 600 ms
  tail -32.5 dB > 2400 ms tail -73.3 (shorter = brighter, the direction the
  REFERENCE FAMILY itself measures: 0.5 s len -62.5 vs 2.0 s len -73.0,
  click-tail 0.3-0.6 s; the ticket's "shorter = darker" phrasing is INVERTED
  against that measurement and the guard pins the MEASURED direction); click
  comb PEAKINESS 20.31 (the air law concentrates the click's energy into the
  low-mid comb band, so the peakiness-ratio metric rose from the pre-16.7 even
  though the tap-peak AMPLITUDES did not grow; EMT 140's 14.7 is a
  physical-cavity comb of a different construction; guard: model baseline +
  1 dB); level 84.10 (baseline 83.5977 within +/-1 dB -- the ticket's level law
  holds EXACTLY, no compensation needed: presence is still 1.189, the confirmed
  +1.5 dB dwell); CPU 48 kHz blk64 avg 1.844 us (within the committed baseline
  class; the two 1st-order air stages add ~0.19 us); full DspTests 454/454
  green; standalone GUI links.
- **Biquad landmine (2026-10-13, cost hours):** hand-written 2nd-order biquads
  at fc << fs (48 kHz) are unreliable: "RBJ-style" LOWPASS and peaking forms,
  written from memory, measured UNSTABLE (unnormalised peaking, pole 1.127 ->
  NaN), or DC-DEAD (the lowpass form's DC gain is 0.035 -- it is a resonator,
  not a lowpass; 6-12k passed uncut while the level swung +14 dB), or
  wrong-shape (a 240 Hz "peaking" peaking at 600-1.2 kHz). Only 1st-order
  stages (`y += a*(x-y)`, `a = 1-exp(-2*pi*fc/fs)`) are PROVABLY stable and
  DC-exact at these numbers -- the SHIPPED air law is exactly that (2 stages,
  -6 dB/oct each). If a 2nd-order stage is ever required: coefficients from a
  verified library (JUCE `BiquadCoefficients`), and a NUMERIC check of pole
  radius < 1 AND DC gain before it ships. In-loop variants of the same filters
  were separately REJECTED on the comb (+4.7 to +10 dB; one unstable).

## Cross-references
- `docs/agents/dsp-invariants.md` — per-mode laws + CONSIDERED & DECLINED
  contracts (this training's laws and declines land there / beside the mode).
- `docs/agents/linux-build-deep.md` — the canonical build env + mtime/traps.
- `docs/tickets/complete/plate-combing.md` + its close-out — the first instance of this
  training (reference path, metric table, A/B, CPU) as a worked example.
- `plugin/include/BudgetConvolver.h` / `plugin/src/ConvolutionReverb.cpp` —
  the house convolution tail + the mono null-safety fix.
