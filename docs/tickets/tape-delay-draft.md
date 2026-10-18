# Ticket — Tape delay (new effect block)

Status: **DRAFT — scope decision made** (user, 2026-10-14):
**"the tape delay will be a more generic approach to start with (like the
spring) — we can use the RE201 as the main inspiration but we can later add
a tuned tape subtype for the RE 201 and Echoplex."**

⇒ The block ships as a **generic tape delay** (type 0), RE-201 as the primary inspiration. A **`type` axis** is the intended end-state (same mechanism as our reverb block's type axis: `Params::type[mode]`, `numTypes()`, `defaultDialsForType()`), with slots reserved for tuned later subtypes — **RE-201** (3-head matrix + spring-tank-after) and **Echoplex** (2-head, ~0.45 s class, spring + 13-step feedback selector, per public material) — implemented later per-tuned-subtype (measure-first, same lane as the EMT-140 "140" plate type). **INITIAL TICKET = generic type 0 ONLY** (user, 2026-10-14: "for the initial tape ticket we are just doing the generic type based on our research") — the type axis is documented as the end-state, not wired in this ticket. The generic default is shaped with the end-state in mind (delay range, dry-wet, tone chosen to stay reusable when the tuned types land). This is the same pattern the plate track uses ("140" = a plate type, generic plate = default) and the pattern AGENTS.md's mode/subtype rule explicitly endorses ("a mode + model may carry its own neutral defaults + reference law — it must keep the generic/shared mechanism").

## Staged research (idea-level, clean-room)
- **Tape-position delay, not fixed line + tap:** moving read head with
  sinc-kernel interpolation → any real delay length, no click.
- **Drift-as-fraction-of-speed:** wow LFO (~0.24 Hz class) with depth
  scaling as a fraction of current tape speed (stays proportional across
  delay lengths); speed target slewed before modulation applies (no zipper).
- **Direction-flip modes:** forward / back-forth (same line, read backwards
  past one tape-length) / reverse (own feedback history, summed) — the
  back-forth case needs no second delay line.
- **Mode/type switching = Hann crossfade** between two read taps (no click on
  switch) — reusable pattern for any block's mode cycler.
- **Saturation + feedback joint stability:** soft-clip with feedback
  pre-reduced by the saturation dB/2; loop can't run away at any drive.
- **LP + HP around the path** for head tone rolloff.
- Transferable ideas for R2 Mod (current ticket): modulation depth
  proportional to tail/delay length; slew the modulated target.

### User resource 2026-10-13: RE-201 hardware blueprint (web article "RE-201 Space Echo Architectural Blueprint")
Hardware topology confirmed by the user-supplied resource: **one continuous
tape loop, one record head, three FIXED playback heads (Tap 1/2/3), spring
tank after the matrix** — the three echo times are physical distances
off the record head, not three independent delays. Concrete architecture
rules (article + code-structure sketch; used as spec, not as source):
- **Single ring buffer + fixed ratios:** one buffer, three read taps at
  locked physical-distance ratios (the article's numbers = 1.0 / 1.45 /
  2.12 of a base tape length; the fixed 1×/2×/3× in RE201models is the
  minimal same-pattern form). Changing the delay time = **spinning the
  tape**, so ONE parameter (speed/delay length) moves all three taps
  together — the pitch-bend glitch is a feature, not a bug (the taps stay
  in ratio while the base scales).
- **Interpolation:** fractional-tap reads need 3rd/5th-order Hermite
  (or Lagrange-class) — the modulated-read interpolation rule from
  Parker 2011 and the khoin DAT port (cubic) apply here too.
- **Wow flutter bands (this source's split, differs from the local ref's
  single-LFO form):** Wow = 0.5–6 Hz slow LFO into the read pointer
  (pitch warp); Flutter = 15–100 Hz micro-modulation (sideband blur);
  plus **motor torque ballistics**: the user's knob target passes through
  a heavy 1-pole (inertia) before it becomes the tape speed — the
  ballistics-before-offset rule again (matches the local ref's 0.7 s slew
  and RE201models' α=0.5 one-pole).
- **Tape wear / TSDE:** high-pass in front, low-pass after; the
  low-pass (feedback-loop wear) means each re-pass gets darker —
  frequency-dependent loss compounds per loop (already in spec: LP/HP
  around the path + saturation; the per-pass compounding is the
  RE201models TSDE coupling to confirm against the real-thing
  behaviour).
- **Saturation options:** erf/tanh sigmoid (cheap — matches our local
  ref's fasttanh route) vs WDF preamp sag vs physical Langevin
  hysteresis (out of linear scope — separate future lane). The article
  also names **WDF transistor-preamp modeling** (bass sag when pushed)
  as a distinct stage worth keeping out of the linear delay spec.
- **Spring tank after the matrix** (article framing) — the real RE-201's
  spring reverb sits downstream of the delay matrix; our chain already
  treats reverb as a separate block, so this is architectural context,
  not a code requirement.
- **12-position selector knob** as the head-matrix selector (article:
  per-head on/off summed) — maps onto our mode/type cycler if the tape
  ticket wants multiple echo patterns (Tap 1 alone, 1+2, 1+3, 2+3, all
  three, with/without spring) — a natural `type` axis candidate, not a
  decision yet.

## Discipline
Local reference: **reference freely, don't copy** (user, 2026-10-13);
comments rewritten; constants re-derived against our own references.

## Staged research — code repos (2026-10-14)
- **julian-parker/DAFX-Tape** (audio files only, supplementary to the
  Zavalishin & Parker DAFX-18 paper): test recordings
  `dly_test_{vl,vs,h,p,p_aa}.wav` of a feedback delay with the delay core
  implemented by (VL)=variable-length ringbuffer, (VS)=variable-sample-
  rate, (H)=Huovilainen, (p)=the proposed method, (p_aa)=proposed with
  speedup antialiasing, under a documented tape-speed automation curve
  (speedup/slowdown jumps + gradual change). **Keep as A/B reference
  audio** for the eventual tape implementation: `p[p_aa]` is the
tape-correct pitch-gliding behaviour to hear against; `vl`/`vs`/`h`
are the wrong-sounding variants (the paper's whole point). Source
paper: DAFX-18 §4 reference list [1]–[7]; `p_aa` = O(K) antialiased
speedups.
- **cyrusasfa/TapeDelay** (JUCE, 2019, **no LICENSE file**, self-described
  WIP with bugs — do NOT use as a code reference; recorded for
  provenance only). Structure worth noting: parameters include
  `readHeadMix` (RE-201 3-head matrix), `density` (head separation),
  `lfoRate/lfoAmount` (flutter/wow), `tapeSpeed`; linear interpolation
  (their own note: "replace with 3rd-order Hermite"), single delay
  buffer with `dpw - rampedDelayTime·sampleRate` read offset — i.e. the
  **length-type** implementation the DAFX-18 paper explicitly contrasts
  against the speed-type behaviour a real RE-201 has. Useful as the
  "common wrong-answer" to know what NOT to ship.
- **dllim/anotherdelay** (**MIT**): LFO-based flutter (2.5–5.0 Hz,
  depth ±0.2 samples) + wow (0–2.5 Hz, depth ±0.2) as two independent
  `Oscillator` instances summed into the read offset — the simplest
  "flutter+wow as LFO modulation of the read pointer" pattern; saturation
  via a separate distortion stage (erf/hard-clip, out of linear scope);
  reverb is JUCE's built-in Reverb class (convolution-style, not our
  parametric path). Same lane as RE201models for the flutter/wow detail;
  less interesting than the DAFX-18 paper for the core algorithm.
- **DAFx-18 paper (Zavalishin & Parker, Native Instruments, Aveiro 2018)** —
  THE core algorithm for this ticket; open (DAFX proceedings are freely
  available). Key results extracted:
  - **Length-type vs speed-type** delay: length-type (variable-length
    ringbuffer, delay-time knob) has erratic overlapping pitch changes
    under feedback; speed-type (Space Echo, Speed-style: tape speed
    knob) has pitch shift defined by the **ratio of entry-to-exit speed**
    → consistent, musical pitch-glide on speed changes. RE-201 is
    speed-type → we must implement speed semantics, not delay-time
    semantics, for the modulated core.
  - **Tape equation** (the continuous-time core): ∫v(τ)dτ (over the
    signal's in-transit window) = L (fixed head distance). In discrete
    time, track the tape-coordinate V[n] = cumulative sum of v[n]
    alongside the audio in the same ringbuffer element; read position
    found by solving `V[nr] < xw - L ≤ V[nr+1]` → linear interp for
    the fractional read.
  - **Complexity**: O(1) per sample in steady speed / slowdown (one or
    two array-index probes), O(log K) during a speedup by a factor of
    K (binary-search the exponential-doubling range), O(K) if antialias
    of speedups is wanted. Beats variable-sample-rate and Huovilainen
    (both O(v)) — and the paper's measured numbers (TSC/sample, 48 kHz
    class): proposed method ~39 flat regardless of speed, vs 21→2531
    scaling for the length-type baseline as speed goes x1→x100.
  - **Split read/write**: read the delay first, THEN pass through
    feedback, THEN write — avoids the implicit unit delay in the
    feedback loop without needing to solve an implicit equation; cost
    is a one-sample floor on minimum delay.
  - **Antialiasing speedups**: stretch the interpolation kernel by
    factor K (time-stretch + 1/K amplitude scale on the kernel) to
    lower the effective cutoff below Nyquist during a speed-up —
    implementable because the read speed at each sample is known (it
    IS the tape speed), so "resample by ratio" is exact per-sample.
  - **Initialization**: seed `V[0]=-L, V[1]=0` (linear interp) or
    `V[0]=V[1]=-L, V[2]=V[3]=0` (cubic) and set `nr` accordingly →
    read head interpolates across zero-filled elements = "clean tape",
    no buffer-fill needed.
  - **Fixed-point tape coordinate**: V[n] is unbounded in principle;
    2.61 (or 11.52 for extreme speed range) signed fixed-point with
    modulo-8 arithmetic handles this cleanly without overflow — the
    integer bits give headroom, modular range arithmetic handles the
    wrap-around index comparison. For our purposes: use the fixed-point
    representation for V[n] and the read/write indices; audio can stay
    float32 in its own lane.
  - **Dattorro reference** [5] in the paper's bibliography: "Effect
    design, part 2: Delay line modulation and chorus", JAES 45(10)
    1997 — same Dattorro as the plate references already in scope;
    this is the delay-modulation section (R2 Mod territory for the
    plate/spring tracks, but relevant here too).
- **HAL link (hal-05096060)**: page is bot-gated (challenge page
  returned, no PDF content). Could not read. **Do NOT block the ticket
  on it** — note it as an unverified user-provided reference; if the
  user can identify the paper's title/author from the HAL ID, it can be
  re-fetched from a non-gated mirror (e.g. the author's university page
  or ResearchGate). Logged here so it's not silently dropped.


## Scope (2026-10-14 scope decision, user — generic-first, RE-201-inspired,
tunable types later)
- **Block type**: new chain block, `Tape` (its own tile, its own block in
  `ChainBlock.h` + the four state places, same wiring as the conv delay
  block + reverb block's type axis).
- **Default (generic) type — the core, ships this ticket**:
  - **Speed-type delay** (Zavalishin & Parker DAFX-18 algorithm), not
    length-type: delay parameter = tape speed; pitch-glide on speed
    change = entry/exit speed ratio; split read→feedback→write; tape
    coordinate V[n] in-line with audio; O(1) steady/slowdown, O(log K)
    speedup probe; optional speedup antialias (the `p_aa` variant).
  - **Motor ballistics**: knob target → heavy 1-pole (inertia) → tape
    speed (sluggish, no zipper) — the "slew before mod" rule, same lane
    as R2 Mod in the current reverb ticket.
  - **Flutter + wow**: two independent low-amplitude LFOs on the read
    pointer (flutter high, wow low — the `dllim/anotherdelay` pattern,
    the simplest correct form); optional noise component (RE201models
    ×0.35 class) as a detent later.
  - **Feedback + tone**: feedback knob (with the saturation/feedback
    joint-stability rule), LP + HP around the echo path (wear per loop),
    saturation soft-knee (erf/tanh class — the cheapest correct lane;
    WDF preamp + Langevin hysteresis are out of scope for this ticket,
    staged as a nonlinear-lane follow-up, same treatment as the
    spring ticket's nonlinear-exclusion decision).
  - **Dry/wet**: `dry` knob with 0.5 = 0 dB (convDry convention, the
    same bit-identical guard this ticket (reverb ticket R1) uses for
    the reverb block's new `dry` knob — consistent semantics across the
    chain).
- **`type` axis (END-STATE, reserved — NOT wired in the initial ticket)**:
  - `type 0` (THIS ticket): generic 1-head tape delay, speed-type core
    as above, flutter/wow/feedback/tone/saturation/dry.
  - `type 1` (later, RE-201): 3 read taps **1 : ~2 : ~3 of a base T**,
    **T ≈ 107 ms (MEASURED, 2026-10-14)** — user-supplied head IR set
    (PastToFutureReverbs, see `research/re201-heads-data.txt` +
    `research/probe_re201_heads.py`): dominant peaks H1 107.08 ms,
    H2 210.15 ms (1.96×), H3 307.81 ms (2.87×) → musical
    subdivision, closest match to the blueprint's 108 ms (T), NOT the
    Abbey Road Institute's 74 ms (that's a Binson-class unit, the
    Echorec lane) and NOT an exact 1/2/3. **Head 4 is UNRESOLVED**: the
    vendor "Head 4" file carries TWO strong arrivals (~207 and ~307 ms)
    = composite/odd, so 4T is an **open slot, not a default**. Stereo
    law is **per-head R-is-later** (R−L = +10.8 / +32.4 / +40.7 / +19.7
    ms for H1–H4) — model L/R as two distinct heads with per-head
    constants (NOT a single Haas offset); re-derive ours against our
    own reference at launch (clean-room). ~3.5 ms record-head print on
    every file = capture artifact, optional to ship (decision point).
    12-position-class head matrix (or a simplified on/off-per-tap +
    spring-tank-after flag), spring-tank routing (as a chain position
    option, not the block's own tail — our chain already has a spring
    block for that lane).
  - `type 2` (later, Echoplex): **tape lane** (speed-type core = the
    generic type's engine, i.e. per-type configuration, not a new
    algorithm): per-type delay **8-stop ladder MEASURED 2026-10-14 from
    the user's "Maestro Echoplex (Tube)" IR set: 57.5 / 95.4 / 145.0 /
    225.3 / 295.4 / 427.2 / 472.9 / 573.6–574.8 ms** (`research/
    echoplex-tape-data.txt`, probe `research/probe_echoplex_tape.py`) —
    this capture is a *choosing* ladder, NOT a continuous handle; ship
    the measured stops (re-derive our own against our reference at
    launch), per-type flutter/wow defaults (the LFO pattern carries
    over), tail = the engine's **Feedback loop** (bounces ~8 ms apart
    in the reference — the "Feedback" knob, not a repeated-IR), feedback
    exposed as the **13-position coarse Repeats ladder** (0 = none;
    coarse quantization IS the signature — the cycler +
    `defaultDialsForType()` positions; ladder values UNVERIFIED — Spec
    side, see Scribd + measurement pass), Tone LP on the repeats path,
    **stereo default = mono-duplicate** (most measured files have R = L;
    pos 4s/6s/7 carry an R-EARLIER cross-bounce, not the RE-201 R-is-
    later law), spring = chain-routing pointer to the spring block (not
    an internal tank). Full research/spec home + material:
    `docs/tickets/echoplex-delay-draft.md`.
  - (Note, 2026-10-14): the **Echorec/magnetic (wire)** type is a SEPARATE
    ticket now (`docs/tickets/magnetic-delay-echorec-draft.md`), but the
    user resolved the block-vs-type question the same day: **"we can just
    add echorec as a subtype for tape even if it technically isnt"** (m00551)
    — i.e. when it ships it lands as a type slot on THIS delay block's type
    axis, wire-vs-tape distinction acknowledged and waived for UX
    simplicity. Reserved as `type 3` below; its own measurement pass +
    blueprint material live in the Echorec ticket as the research home.
  - `type 3` (later, Echorec / magnetic): **locked** 4-tap ladder
    1×/2×/3×/4× of a base T (user blueprint: T ≈ 108 ms → 108/216/324/
    432 ms; Abbey Road Institute measured hardware: 74/148/222/296 ms —
    TWO DIFFERENT T's, both musically-rational 1:2:3:4; real unit = the
    measurement pass decides + Dr Bini's musical mapping: tap4 = quarter
    note, tap3 = dotted 8th, tap2 = 8th, tap1 = 16th). 12-position head
    switch (tap combinations) + 3-mode selector (Echo = 50s slapback /
    Repeat = standard / Swell = overlapping-toward-reverb) — both
    selectors map onto a combined type/position matrix; feedback matrix
    = ALL active playback heads summed back into the record head
    ("swell", the compounding-cascade signature); head tone = brickwall
    LP ~5–10 kHz (early Binson vs later Photovox heads) + HP, write-side
    (user blueprint); AC bias (50–60 kHz) = linear-print dither →
    linear-lane stand-in = the brickwall LP (+ optional soft-knee). Tube
    preamp / bias / wire magnetic saturation = nonlinear lane, EXCLUDED
    (own ticket, same exclusion as the other tickets); **wow/flutter is
    an artefact of poor maintenance, NOT the Binson sound** (Effectrode:
    a properly setup unit has inaudible pitch fluctuation + good FR) —
    the Echorec lane keeps delay LOCKED, no LFO; motor is AC-mains-
    synchronous (delay-time stability tied to 50/60 Hz — no speed knob;
    the delay length is a mechanical constant). Reference material:
    Effectrode Binson knowledge base (technical docs + service info, free),
    Abbey Road Institute (hardware + head-spacing + controls),
    `AnalogTapeModel` (GPL v3, CHOW Tape DAFX-19 physical model — nonlinear
    lane, ideas/structure only), `chowdsp_wdf` (BSD-3 WDF — tube preamp WDF
    building blocks, reference only per house rule). See
    `docs/tickets/magnetic-delay-echorec-draft.md`.
  - Type switching = **Hann crossfade** between the two tap layouts
    (no click on switch) — the same crossfade rule the local
    spring-tape-ref's mode switch uses; the type axis reuses the mode
    axis's cycler machinery (the `reverbType0..5` + `defaultDialsForType`
    plumbing already in the chain state). NOTE: this crossfade/type-axis
    machinery is **reserved for the future**, not built in the initial
    ticket.
- **CPU budget**: DAFX-18 is ~39 TSC/sample flat for the core (measured
  in the paper); the flutter/wow LP/HP sat/dry path is trivial by
  comparison. Budget to be confirmed at implementation time against the
  same 48 kHz / 128-block class used for the reverb ticket's Mod
  budget (plate avg 3.56 µs / guard 4.95 µs).
- **IN THIS ticket (INITIAL SCOPE — generic type ONLY, confirmed by user
  2026-10-14: "for the initial tape ticket we are just doing the generic
  type based on our research")**: the `type 0` generic 1-head tape delay
  (speed-type core + flutter/wow/feedback/tone/saturation/dry) above. That
  is the whole initial scope. The `type 1`/`type 2` tuned slots are
  **reserved / future** only — the type-axis machinery is NOT implemented
  or wired in this ticket; it is documented here as the intended
  end-state so the generic default can be shaped with it in mind
  (e.g. delay range, dry-wet knob, tone knob chosen to be reusable when
  the tuned types land with their own reference-IR material + measurement
  passes, same lane as the EMT-140 plate type did).
- **NOT in this ticket (follow-up / own lanes)**:
  - RE-201 and Echoplex tuned subtypes (their own tickets, own
    measurement passes — user confirmed they come later, after the generic
    type ships).
  - WDF preamp / Langevin hysteresis (nonlinear lane — out of linear-
    scope, same exclusion treatment as the spring ticket's
    nonlinear-exclusion decision).
  - The RE-201 spring tank itself (the spring ticket's territory, cross-
    linked).

## Closeout protocol (standard)
DspTests full-green, TONE3000_Standalone build, UiTestbed --selftest,
OpenWiki update, Windows standalone exe stage.

## Research references (gitignored, local-only)
- **Head IR set (2026-10-14, user-supplied, measured)**:
  `C:\Impulse Responses\Convolution Reverb IRs\Past to Future Reverbs\` —
  RE-201 `Head N(.wav / Stereo.wav)` N=1..4 (with-Spring excluded per user)
  + RE-301 `delay 1..10.wav` (context only, chorus-echo lane, NOT a type
  slot). Measured taps above; probe + data: `research/probe_re201_heads.py`
  → `research/re201-heads-data.txt`.
`research/spring-ventris.md` § TAPE-DELAY processor, `research/README.md`
vet table.
