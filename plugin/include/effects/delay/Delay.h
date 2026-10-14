#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Feedback delay: a ChainBlockType::EFFECT block (EffectKind::Delay).
 *
 * The block is model-less - it produces the block's wet signal (the echo
 * train) in place and the surrounding machinery (dry copy, input gain, EQ,
 * Mix, Out Gain) does the rest, exactly as a NAM/IR block hands its model the
 * dry input and reads the wet back. So process() replaces the dry in the
 * buffer with the wet echo; the block's Mix blends it against the dry copy it
 * already made (Mix 0 = silent dry, Mix 1 = pure echo).
 *
 * Engine: one feedback comb per channel. Each ring carries a decaying echo
 * train e[n] = x[n] + fb * e[n - D]; the tap D behind the write head is the
 * wet. D is the delay time, fb the feedback (echo decay per pass). |fb| < 1
 * keeps it stable, so it is capped well below 1. The echo train decays naturally (each pass scaled by the feedback amount), so feedback only lengthens the tail rather than muting the repeats.
 *
 * Click-free time changes: the dialed time (delaySamples_) is the latency
 * target; the actual tap (currentDelaySamples_) is slewed toward it inside
 * process(). So turning Time (or BPM/Div while Synced) sweeps the tap instead
 * of jumping it - a jump would yank the feedback tail and click.
 *
 * Five-character mode set (the Compressor pattern; design + provenance in
 * plugin/docs/delay-modes.md; materials in the host's ~/.research/):
 *   0  Digital   clean reference comb (this engine as-is) + Ping: the echo
 *                train morphs from parallel (both ears same tap) to chained
 *                L<->R alternation (stereo routing signature - inert on
 *                single-channel engines). Provenance: JUCE delay-line
 *                tutorial, DSPRelated feedback-comb analysis, PASP
 *                time-varying delay, Tonalux comb notes (the host's
 *                .research/digital/; ticket 2 in plugin/docs/delay-modes.md).
 *   1  Tape      idealized tape delay, Space-Echo character: organic
 *                wow/flutter on a fractional tap, multi-head even-interval
 *                reads, per-pass thinning + soft saturation (music-dsp 2002,
 *                hiSE faust thread, Boss RE-202 article, RE-201 owner's
 *                manual, dllim/anotherdelay).
 *   2  BBD       bucket-brigade: a LAW mode (unlike the amount modes, its
 *                cleanest setting is still coloured, not the Digital body).
 *                The time<->tone law is the floor -- the loop's low-pass
 *                cutoff falls as 1/T, so a 200 ms echo is inherently darker
 *                than a 50 ms one and a long BBD echo can never be clean.
 *                Chip adds vintage on top: it darkens the law, soft-clips the
 *                loop (drive) and adds per-pass loss (time buys loss).
 *                Provenance: Strymon dBucket whitepaper, DAFX'25 BLEP paper,
 *                Raffel & Smith DAFx'10, Holters & Parker DAFx'18, Chowdhury
 *                BBDDelay (ticket 4 in plugin/docs/delay-modes.md).
 *   3  Mod       vibrato / duo delay: the tap wavers on the mode's RATE
 *                (the mode's UNIQUE real-Hz knob, 0.5-30) with the shared
 *                Mod knob as DEPTH, L + / R - opposite phase ("two heads
 *                drifting apart"), dry feed untouched (PASP time-varying
 *                delay, chorus row). Phase 2 (2026-10-07): the ACTIVE repeats
 *                run a kModCeilHz ceiling whose alpha the waver sways by
 *                +/- kModBrightCouple at full depth (the tape-less line's
 *                brightness swell; bit-identical to the pre-Phase-2 body at
 *                depth 0).
 *   4  Magnetic  warbly tape: the Tape head+core law (NAB E -> tanh core -> D,
 *                ~15 kHz ceiling) around a tap that WOVES -- the classic
 *                Echorec-style tape waver: the mode's UNIQUE real-Hz WOBBLE
 *                knob (0.5-30; slow = wow drift, fast = flutter) with the
 *                shared Mod knob as WOBBLE DEPTH. The tone is tape; the
 *                repeats ride. Provenance: Binson Echorec 6B1 (public
 *                schematic + public warble analyses; the physical-stages
 *                spec).
 *   5  MemGuy    the BBD-line memory mod: the echo line runs the BBD
 *                time<->tone law (Chip-0 baseline: longer = darker, no drive
 *                -- high headroom, tonal-not-harmonic) and the repeats waver
 *                on the mode's UNIQUE real-Hz RATE knob (slow = chorus, fast
 *                = vibrato) with the shared Mod knob as DEPTH -- the BBD
 *                version of Mod (2026-10-06). Phase 2 (2026-10-07): the waver
 *                runs the deep Doppler depth (kDopplerWobbleMs) so the
 *                Memory Man's pitch-swell (Doppler: pitch wavers with delay
 *                time) is the character, not a hint. Provenance: the MN3005
 *                BBD-array + clock-mod reference (the physical-stages spec,
 *                the user-approved display name).
 *   Shared with all modes (scaffold): an always-on 1-pole DC blocker (~80 Hz)
 *   in the feedback path (Tonalux "DC blocking") and the variable-tap
 *   subsystem (linear-interpolating ring reads + per-lane tap law) that the
 *   tap-moving modes drive. At neutral settings every mode's read path
 *   reduces to today's integer tap, so the Digital engine stays bit-
 *   identical to the pre-scaffold comb (protected by test pins).
 *
 * Threading, like PitchShift/BlockEq: prepare() from prepareToPlay sizes the
 * rings; setParams() on the message thread under chainMutex; process() on the
 * audio thread with zero allocation.
 */
class Delay {
 public:
  static constexpr int kMaxChannels = 2;
  static constexpr double kMinTimeMs = 5.0;
  static constexpr double kMaxTimeMs = 10000.0;   // internal engine max (10 s); the delay ring is sized for this
  static constexpr double kKnobMaxTimeMs = 1000.0; // the free-time knob's max settable (1 s)
  static constexpr double kMinFeedback = 0.0;
  static constexpr double kMaxFeedback = 0.9;
  static constexpr double kMinDamping = 0.0;  // 0 = bright, 1 = dark (low-pass on feedback)
  static constexpr double kMaxDamping = 1.0;
  static constexpr double kMinSpread = 0.0;   // 0 = L/R same time (legacy, mono-centred)
  static constexpr double kMaxSpread = 1.0;   // 1 = L at 0.5x the time, R at 1.5x (max width)
  static constexpr double kSpreadDepth = 0.5; // half-offset as a fraction of the base time
  // How long the tap takes to sweep to a new time when the time changes
  // mid-stream (see setParams); short enough to feel instant, long enough to
  // be a smooth sweep rather than a jump.
  static constexpr double kTimeSlewMs = 24.0;

  // --- Five-character mode set (see the class comment; plugin/docs/
  //     delay-modes.md). Selecting a mode applies the mode's characteristic
  //     defaults as a starting point; an unknown/legacy value clamps to the
  //     range on the way in (the Compressor precedent). ---
  static constexpr int kNumModes = 6;  // 0=Digital,1=Tape,2=BBD,3=Mod,4=Magnetic,5=MemGuy
  static const char* modeName(int index) {
    static const char* names[6] = {"Digital", "Tape", "BBD", "Mod", "Magnetic", "MemGuy"};
    return names[juce::jlimit(0, kNumModes - 1, index)];
  }
  // Each mode's one signature control, stored normalised 0..1 (see the class
  // comment for what each does; each engine is a follow-on ticket).
  static constexpr double kMinSig = 0.0;
  static constexpr double kMaxSig = 1.0;
  // Tape "Heads": the multi-head count is 1..4, quantised from the 0..1 field.
  static constexpr int kMinHeads = 1;
  static constexpr int kMaxHeads = 4;
  // Tape wow/flutter + softness (ticket 3; provenance: hiSE faust thread,
  // Boss RE-202 article, RE-201 owner's manual, dllim/anotherdelay, the
  // music-dsp 2002 space-echo thread - the host's .research/tape/).
  static constexpr double kTapeWowHz = 1.2;   // flutter rate (organic wobble)
  static constexpr double kTapeWowMs = 1.5;   // flutter depth in ms
  // Tape HEAD+CORE law (2026-10-06 physical stages; RE-201 reference,
  // clean-room; plugin/docs/physical-stages-spec.md). The loop runs:
  // NAB emphasis E -> tanh core -> exact de-emphasis D = 1/E. The NAB pair
  // (zero 750 Hz, pole 3180 Hz, boost 3180/750 ~ +6.5 dB) makes the HIGH
  // S reach the core LOUDER, so they saturate FIRST (the replayable
  // "highs thinned first" fingerprint); D undoes the boost exactly (net
  // gain 1, so the loop level/loop-gain stays the loop's). A ~15 kHz
  // ceiling (tape-limited band) rides the loop for the per-pass thin-out.
  // bilinear 1st-order -> causal, zero sample delay.
  static constexpr double kTapeCoreGain = 1.25;  // noon head drive (gentle band)
  // Magnetic (4) capstan coupling: real tape speed waver moves the HF
  // ceiling as well as the tap (Echorec warble). The ceiling alpha modulates
  // +/- this * depth (sigMod) with the SAME phase as the tap wobble, so
  // depth 0 stays bit-exact. Magnetic only (Tape keeps its intrinsic flutter).
  static constexpr float kMagToneCouple = 0.20f;
  static constexpr double kTapeCeilHz = 15000.0; // ~15 kHz tape-limited ceiling
  static constexpr double kTapeNabHzZero = 750.0;   // NAB emphasis ZERO (low)
  static constexpr double kTapeNabHzPole = 3180.0;  // NAB emphasis POLE (high)
  // BBD (mode 2): the time<->tone law is the mode's floor (present even at
  // Chip 0 = the cleanest BBD; Chip adds vintage on top). fc is refHz at the
  // refMs tap and falls as 1/T (Strymon "time buys loss"), chip darkens the
  // whole family by (1 - chipTone), and chip adds drive (max drive gain before
  // the tanh) + per-pass loop loss (Strymon per-stage loss, Huovilainen
  // BBDCompander, Chowdhury BBDDelay; host .research/bbd/). The cutoff is
  // converted to a 1-pole alpha with its -3 dB at ~fc in setParams/process
  // (see the class comment + ticket 4 in delay-modes.md).
  static constexpr double kBBDToneRefHz = 5000.0;   // cutoff at the reference tap
  static constexpr double kBBDToneRefMs = 250.0;    // the reference tap (fc = refHz here)
  static constexpr double kBBDChipTone = 0.6;       // chip darkens the law: fc *= (1 - this*chip)
  static constexpr double kBBDDriveMax = 1.5;       // chip drive: gain = 1 + this*chip before tanh
  static constexpr double kBBDLossMax = 0.35;       // chip loss: loop gain = fb * (1 - this*chip)
  static constexpr double kBBDMinToneHz = 50.0;     // the darkest the law may get
  // Shared Mod knob (delayMod, live in EVERY mode since 2026-10-06): the tap
  // wavers on a sine LFO, L + / R - opposite phase ("two heads drifting
  // apart"), dry feed untouched. The PARAM is shared across modes; the LAW
  // (rate/depth below) carries each mode's character rather than one fixed
  // law -- tape flutter is not a 5 Hz vibrato. Depth maps the 0..1 field to
  // ±ms; 0 is a straight tap = bit-identical to the pre-wobble read path in
  // every mode (the scaffold neutral pins hold across the board). Provenance:
  // PASP time-varying delay (chorus row); host .research/mod/ (classic law).
  static constexpr double kModWobbleHz = 5.0;   // classic vibrato rate (Digital/Mod)
  static constexpr double kModWobbleMs = 4.0;   // classic full-depth wobble ±ms
  // Phase 2 (2026-10-07; user confirmed the mechanism against the Memory
  // Man / Echorec references BEFORE the build -- it is NOT a cumulative
  // per-pass transpose, it is the Doppler of the tap waver: pitch is a
  // function of delay time, so the waver IS the pitch float):
  //   Magnetic(4) + MemGuy(5): run the waver DEEPER so the siren is the
  //   character (10 ms full depth ~ +/-0.8 st at a 250 ms tap; the classic
  //   4 ms reads as a hint). Rate is still each mode's own knob.
  //   Mod(3): a BRIGHTNESS waver -- the active repeats run a ceiling
  //   (kModCeilHz) that the waver LFO sways by +/- kModBrightCouple at
  //   full depth (the tape-less line's Echorec-style swell; no pitch law).
  static constexpr double kDopplerWobbleMs = 10.0; // Magnetic/MemGuy full-depth waver ±ms
  static constexpr double kModCeilHz = 8000.0;     // Mod (3) active ceiling
  static constexpr float kModBrightCouple = 0.10f; // +/-10% alpha sway at full depth
  // Per-mode Mod law (taste constants, ears pass). Digital and Mod keep the
  // classic 5 Hz / ±4 ms (so Mod mode stays bit-identical to the pre-shared
  // build); Tape a slow flutter drift (kept off the intrinsic 1.2 Hz wow
  // frequency); BBD deep + slow (spacey); Magnetic and MemGuy carry their own
  // real-Hz rate knobs (below).
  static double modWobbleHz(int mode) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 1: return 1.0;    // Tape: slow flutter drift
      case 2: return 0.8;    // BBD: slow, spacey
      default: return kModWobbleHz;  // Digital / Mod: classic vibrato
      // (sigMagRate / sigMmRate) instead -- they never reach this law.
    }
  }
  static double modWobbleMs(int mode) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 1: return 2.0;    // Tape: gentle (the intrinsic wow already drifts)
      case 2: return 5.0;    // BBD: deep
      case 4: return kDopplerWobbleMs;  // Magnetic: the Echorec warble (deep Doppler)
      case 5: return kDopplerWobbleMs;  // MemGuy: the Memory Man siren (deep Doppler)
      default: return kModWobbleMs;  // Digital / Mod: the classic law
    }
  }
  // The RATE knobs (delayRateHz on Mod 3, delayMagRateHz on Magnetic 4,
  // delayMmRateHz on MemGuy 5): each mode's UNIQUE signature -- the
  // wobble SPEED -- stored REAL
  // HZ (clamped ProcessorChain-side; setParams clamps the engine side).
  // slow = chorus, fast = vibrato (the clock-modulated waver law). The
  // other modes keep their OWN fixed laws above (giving them a rate knob
  // would collapse them into one -- tape flutter is not the same control as
  // vibrato speed). The default lands exactly on the classic 5 Hz; MemGuy's
  // entry landing point is a mid-sweep chorus rate (defaultSignatureForMode).
  static constexpr double kRateMinHz = 0.5;              // super-slow drift (chorus)
  static constexpr double kRateMaxHz = 30.0;             // fast flutter (vibrato)
  static constexpr double kRateDefaultHz = kModWobbleHz;    // classic 5 Hz (knob mid-point reference)
  static constexpr double kRateWobbleDefaultHz = 1.0;      // Magnetic (4): slow WOBBLE (wow drift)
  static constexpr double kRateModDefaultHz = 1.5;         // Mod (3) stock: user ear 2026-10-07
  static constexpr double kRateMmDefaultHz = 0.8;          // MemGuy (5) stock: user ear 2026-10-07
  static int headsFromNormalized(double n) {
    const double t = juce::jlimit(0.0, 1.0, n) * (kMaxHeads - 1);
    return juce::jlimit(kMinHeads, kMaxHeads,
                        static_cast<int>(std::lround(t)) + kMinHeads);
  }
  /** The signature starting point applied when a mode is SELECTED
      (enterDelayMode pushes it, and the sig knob's alt-click reset lands on
      it). Chosen to land on each mode's recognisable character instead of
      the all-neutral scaffold (2026-10-06, "a good default, not 0-ish"):
      2026-10-06 physical-stages defaults (user-set): Digital **Ping 0**
      (neutral -- the clean body: the mode's character IS the routing
      signature), Tape **Heads 1** (single head: the cleanest Space-Echo
      body), BBD a light vintage, Mod an audible wobble depth, MemGuy a
      MID-SWEEP chorus RATE (normalised 0.5 -> ~3.9 Hz on the real-Hz log
      face -- the stock chorus/vibrato landing). Magnetic (rate + tape tone)
      lands the same way (classic 5 Hz wobble). The engine's state defaults
      are unchanged, so existing saves are untouched -- these are the
      mode-SELECTION starting points. */
  static double defaultSignatureForMode(int mode) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 0: return 0.0;             // Ping: NEUTRAL (user default 2026-10-06)
      case 1: return 0.0;             // Heads: 1   (user default 2026-10-06)
      case 2: return 0.25;            // Chip: light vintage
      case 3: return 0.35;            // Mod: audible wobble depth
      case 4: return 0.5;             // Magnetic: wobble-rate placeholder -- the
                                      // UI lands on the classic 5 Hz
      case 5: return 0.5;             // MemGuy: mid-sweep chorus rate (Rate knob)
      default: return 0.0;
    }
  }
  // DC blocker corner in the feedback path (scaffold, all modes): a ~1-pole
  // high-pass so DC can't accumulate into the loop (Tonalux comb notes).
  static constexpr double kDcBlockHz = 80.0;

  // --- BPM / subdivision (sync) mode ---
  // Tempo bounds (BPM). The slowest subdivision (a whole note) at 60 BPM is
  // 4000 ms, inside the 10000 ms internal max, so every subdivision fits
  // within the range at every tempo.
  static constexpr double kMinBpm = 60.0;
  static constexpr double kMaxBpm = 240.0;
  static constexpr double kDefaultBpm = 120.0;
  // The Time knob's nine note durations, fastest -> slowest (UI order):
  // 16th Triplet, 16th, 8th Triplet, Dotted 8th, 8th, Quarter Triplet,
  // Quarter, Half, Whole. Each is a fraction of a whole note (a whole note = 4 beats).
  static constexpr int kNumSubdivisions = 9;
  static constexpr double kSubdivisionFraction[9] = {
      1.0 / 24.0, 1.0 / 16.0, 1.0 / 12.0, 3.0 / 16.0, 1.0 / 8.0, 1.0 / 6.0,
      1.0 / 4.0, 1.0 / 2.0, 1.0 / 1.0};
  static constexpr int kDefaultSubdivision = 6;  // Quarter
  static const char* subdivisionName(int index) {
    static const char* names[9] = {"1/16T", "1/16", "1/8T", "D8", "1/8",
                                   "1/4T", "1/4", "1/2", "1/1"};
    const int i = juce::jlimit(0, kNumSubdivisions - 1, index);
    return names[i];
  }
  static double noteDurationMs(double bpm, int subdivisionIndex) {
    const int i = juce::jlimit(0, kNumSubdivisions - 1, subdivisionIndex);
    const double b = juce::jlimit(kMinBpm, kMaxBpm, bpm);
    return juce::jlimit(kMinTimeMs, kMaxTimeMs,
                        kSubdivisionFraction[i] * 240000.0 / b);
  }

  struct Params {
    double timeMs = 250.0;    // base echo time
    double feedback = 0.35;   // echo decay per pass
    double damping = 0.0;     // low-pass on the feedback (0 = bright, 1 = dark); UI PARKED (2026-10-06)
    double spread = 0.0;      // 0 = L/R same time; 1 = L 0.5x, R 1.5x the base time (stereo width)
    // --- Mode set (class comment; plugin/docs/delay-modes.md) ---
    int mode = 0;             // 0=Digital,1=Tape,2=BBD,3=Mod,4=Magnetic,5=MemGuy
    double sigPing = 0.0;     // Digital: depth 0..1, parallel -> chained L<->R
    double sigHeads = 0.0;    // Tape: multi-head count (1..4, see headsFromNormalized)
    double sigChip = 0.0;     // BBD: drive + per-pass loss + time-couled tone
    double sigMod = 0.0;      // shared Mod knob: wobble DEPTH, live in
                              // EVERY mode (law: modWobbleHz/Ms(mode)); on the
                              // RATE modes (Mod / Magnetic / MemGuy) it is the
                              // depth of that mode's rate-law waver
    double sigRate = kRateModDefaultHz;  // Mod (mode 3) UNIQUE: wobble speed in
                              // HZ (kRateMinHz..kRateMaxHz; stock 1.5, inert in
                              // every other mode)
    double sigMagRate = kRateWobbleDefaultHz;  // Magnetic (mode 4) UNIQUE:
                              // WOBBLE speed in Hz (kRateMinHz..kRateMaxHz;
                              // default 1.0 Hz -- slow wow drift, twist up to flutter)
    double sigMmRate = kRateMmDefaultHz;       // MemGuy (mode 5) UNIQUE: the Memory Man
                              // RATE pot -- chorus<->vibrato speed in HZ
                              // (kRateMinHz..kRateMaxHz; stock 0.8, slow
                              // chorus drift)
    bool dcBlock = false;     // scaffold DC blocker ON; the production chain sets
                              // true (ChainBlock::delayParams) while the standalone
                              // engine defaults it off so the pure-comb math stays
                              // pinnable bit-for-bit in tests
  };

  void prepare(double sampleRate);
  void reset();
  void setParams(const Params& params);
  /** Lane hint: 0 = this block's signal belongs to the LEFT side of the
      channel pair, 1 = the RIGHT side. Mono-chain mode leaves it at 0 and
      L/R come from the buffer channels; in stereo-chain mode each lane is
      mono, so the hint selects which side's spread formula this signal runs
      (left = base - offset, right = base + offset), keeping the split
      consistent in both modes. */
  void setLane(int lane);
  void process(juce::AudioBuffer<float>& buffer);
  int latencySamples() const;

  const Params& params() const { return params_; }

 private:
  struct Ring {
    std::vector<float> buf;
    uint32_t size = 0, mask = 0, write = 0;
    float lp = 0.0f;  // low-pass state for the feedback path (damping)
    float dc = 0.0f, dcin = 0.0f;  // 1-pole DC blocker state (scaffold)
    void init(uint32_t minSize) {
      uint32_t n = 1;
      while (n < minSize) n <<= 1;
      size = n;
      mask = n - 1;
      buf.assign(static_cast<size_t>(n), 0.0f);
      write = 0;
      lp = 0.0f;
      dc = 0.0f;
      dcin = 0.0f;
    }
    void clear() {
      std::fill(buf.begin(), buf.end(), 0.0f);
      write = 0;
      lp = 0.0f;
      dc = 0.0f;
      dcin = 0.0f;
    }
  };

  // Variable-tap subsystem (scaffold; the tap-moving modes build on this): a
  // ring read at a fractional tap with linear interpolation between the two
  // samples behind the write head; at an integer tap this is exactly the
  // pre-scaffold read `data[(w-d)&mask]`.
  float readAt(const Ring& ring, uint32_t writePos, float tapSamples) const;
  /** Per-lane tap law (scaffold): @p baseTapSamples is this sample's post-slew,
      post-spread tap for the lane; @p lane 0=left, 1=right; @p sampleIndex the
      block sample. The modes later add their wobble here (Tape organic,
      Mod / MemGuy rate-LFO wobble). It must return exactly the
      argument at neutral signatures, keeping the zero-modulation read path
      bit-identical to the pre-scaffold integer-tap read. */
  float tapFor(float baseTapSamples, int lane, int sampleIndex) const {
    (void)baseTapSamples;
    (void)lane;
    (void)sampleIndex;
    return baseTapSamples;
  }

  std::vector<Ring> rings_;
  double sampleRate_ = 0.0;
  double delaySamples_ = 0.0;        // target delay time in samples (dialed)
  double currentDelaySamples_ = 0.0; // actual tap; slewed toward the target
  double slewStep_ = 0.0;           // per-sample step while a slew is in flight
  int slewLeft_ = 0;                // remaining slew samples (0 = at rest)
  bool primed_ = false;             // true once process() has run at least once
  double feedback_ = 0.0;
  float pingDepth_ = 0.0f;  // Digital (mode 0) Ping depth 0..1; 0 = parallel
  // Tape (mode 1): multi-head count + wow/flutter (depth in samples; 0 = off
  // and single-tap read, bit-exact). See process() for the head geometry.
  int heads_ = 1;
  float wowDepthSamples_ = 0.0f;
  double flutterInc_ = 0.0;   // per-sample phase advance of the wow LFO
  double flutterPhase_ = 0.0; // cross-block wow phase
  // BBD (mode 2, + MemGuy 5 at Chip 0) law state: the tone cutoff is the mode's
  // floor (present at Chip 0), so a BBD run is NOT bit-identical to Digital even at neutral
  // (unlike the amount modes). See the class comment + ticket 4.
  bool bbdOn_ = false;
  float bbdChip_ = 0.0f;      // clamped Chip; drives (when >0) the loop tanh
  float bbdDrive_ = 1.0f;     // chip drive gain before tanh = 1 + kBBDDriveMax*chip
  float bbdLoss_ = 1.0f;      // per-pass loss = 1 - kBBDLossMax*chip (==1 at chip 0)
  double bbdLawNorm_ = 0.0;   // 2*pi*fc/sr of the law (chip folded in); alpha = norm/(1+norm)
  double bbdNormMin_ = 0.0;   // the dB clamp band in normed form (== kBBDMinToneHz..Nyquist)
  double bbdNormMax_ = 0.0;
  // Shared Mod knob (delayMod) state: a wobble on the read tap, L + / R -
  // opposite via the lane side, in EVERY mode. The law (rate/depth) is the
  // mode's (modWobbleHz/Ms). modOn_ is false at depth 0 in every mode, so
  // every mode's read path stays bit-identical to its pre-wobble read.
  bool modOn_ = false;
  float modDepthSamples_ = 0.0f;  // full-mod wobble depth (|tap| wobble, samples)
  double modInc_ = 0.0;           // per-sample LFO phase advance = 2*pi*rate/sr
  double modPhase_ = 0.0;         // cross-block LFO phase (modulate with the audio)
  // Tape (1) + Magnetic (4) HEAD+CORE law (2026-10-06 physical stages; RE-201
  // NAB emphasis E, the ~15 kHz ceiling on the shared lp line, the tanh core,
  // and the exact de-emphasis D = 1/E (two 1st-order sections -> two floats
  // each: x[n-1], y[n-1]). Causal, zero sample delay (bilinear 1st-order).
  // tape* b0/b1/a1 are computed in setParams; tapeCores_ is per-channel state
  // (cleared in prepare/reset, RT-safe; allocated once, grown on resize).
  bool tapeCoreOn_ = false;
  float tapeCeilAlpha_ = 1.0f;   // ceiling LP alpha (1 -> bypass); 15 kHz law
  float modCeilAlpha_ = 1.0f;    // Mod (3) brightness-waver ceiling alpha (Phase 2)
  // NAB E/D coefficients (bilinear 1st-order, y = b0 x + b1 x1 - a1 y1):
  // DOUBLES -- the b/a spread (~1e-9) is far finer than float.
  double tEb0 = 1.0, tEb1 = 0.0, tEa1 = 0.0;   // NAB E (record, zero 750)
  double tDb0 = 1.0, tDb1 = 0.0, tDa1 = 0.0;   // NAB D (= 1/E, zero 3180)
  struct TapeCore { double eX1 = 0.0, eY1 = 0.0, dX1 = 0.0, dY1 = 0.0; };  // double state
  std::vector<TapeCore> tapeCores_;
  float tapeCoreStep(TapeCore& tc, float x);  // E -> tanh core -> D (per-sample)
  // L/R half-offset (samples): left tap = base - offset, right = base + offset.
  // Slewed exactly like the base tap so a spread change can't yank the tails.
  double targetOffsetSamples_ = 0.0;
  double offsetSamples_ = 0.0;
  double offStep_ = 0.0;
  int offLeft_ = 0;
  int lane_ = 0;
  double dcCoeff_ = 0.0;  // DC blocker coefficient (1 - exp(-2pi fc / fs)); ready when params_.dcBlock
  Params params_;
};
