#pragma once
// Reverb: a digital comb-bank reverb, ChainBlockType::EFFECT (EffectKind::Reverb).
//
// Like the delay/chorus, model-less: process() replaces the dry in the buffer
// with the wet reverb tail and the surrounding machinery (dry copy, input gain,
// EQ, Mix, Out Gain) does the rest, exactly as a NAM/IR block hands its model
// the dry input and reads the wet back (Mix 0 = dry, Mix 1 = pure reverb).
//
// Engine: N parallel feedback comb delay lines with incommensurate delay times
// (the classic "digital reverb" character), summed. Each line is the recursive
//   e[n] = fb * lp(e[n - d])  +  x[n]
// i.e. a feedback tail that decays naturally, with the dry input mixed in. The
// output per line is the *delayed* tap e[n - d] (the tail) — NOT the current
// write, which would leak the dry input straight into the wet. Summing the N
// delayed taps and normalising by (1 - fb) keeps the level stable as Decay
// changes.
//
// Stereo Width: each channel runs its OWN comb bank, and the two banks use
// slightly different tap lengths (the right bank runs a little longer than the
// left, scaled by Width) so their tails drift apart. Width = 0 gives identical
// taps on both channels (L == R, mono) while Width = 1 maximally decorrelates
// them (wide) — the same per-channel decorrelation the chorus uses for spread.
//
//   Decay -> feedback (tail length);  Tone -> low-pass on the feedback;
//   Size  -> scales all delay times;   Pre -> offsets all taps (pre-delay);
//   Width -> decorrelates the L/R comb banks (0 = mono, 1 = wide).
//
// Threading, like Delay: prepare() from prepareToPlay sizes the rings;
// setParams() on the message thread under chainMutex; process() on the audio
// thread with zero allocation.
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "juce_audio_basics/juce_audio_basics.h"

class Reverb {
 public:
  static constexpr int kMaxChannels = 2;
  static constexpr int kNumLines = 8;
  static constexpr double kMinDecayMs = 50.0, kMaxDecayMs = 5000.0;
  // Per-mode max decay (ms): the boundary the user hears. The user's ears-pass
  // measured these onsets (the point where that mode's sound starts to break --
  // metallic sheen for Plate/Spring, slow RINGING for Digital/Chamber, or a
  // sound boundary for Room/Hall). We clamp EXACTLY at the user's stated value
  // (the onset) -- not beyond it -- because we can't currently damp the
  // high-decay resonance below the user's ears without touching the sound below
  // the onset. The user confirmed the modes are "solid" below these caps.
  static constexpr double kMaxDecayMsByMode[6] = {
      2500.0,  // Plate   (onset 2500) -- the metallic sheen starts here
      2500.0,  // Spring  (onset 2500) -- the metallic build-up starts here
      2000.0,  // Room    (onset 2000) -- a sound boundary (stable above)
      3000.0,  // Chamber (onset 3000) -- the slow RINGING starts here
      3500.0,  // Hall    (onset 3500) -- a sound boundary (stable above)
      2750.0   // Digital (onset 2750) -- the slow RINGING starts here
  };
  static inline double maxDecayForMode(int mode) {
    return kMaxDecayMsByMode[juce::jlimit(0, 5, mode)];
  }
  // High-decay HF softener: above the mode's onset, the wash's low-pass gets a
  // little darker (a decay-gated extra low-pass) so the long tail stays smooth.
  // The coefficient ramps 0..1 across [onset, cap]; at the cap it's full
  // (kSoftenerAmt). Below the onset the softener is 0 (the sound is preserved).
  // This is what lets the instability modes (Digital/Spring/Chamber) use a cap
  // past their onset without the comb's tap-periodic RINGING (the "build up").
  static constexpr double kSoftenerAmt = 0.12;   // a gentle extra low-pass (a few dB at high decay)
  // Decay knob TUNED region is [50, 3000] ms (the original ceiling); the whole
  // ears-pass history is pinned there. Extending the ceiling to 5000 leaves
  // [50, 3000] bit-identical and only adds a gentle top segment that ramps the
  // tail a little further (0.99 -> 0.999 fb), still strictly |fb| < 1.
  static constexpr double kDecayTunedCeil = 3000.0;
  static constexpr double kMinPreMs = 0.0, kMaxPreMs = 60.0;
  static constexpr double kMinTone = 0.0, kMaxTone = 1.0;  // 0 = bright, 1 = dark
  static constexpr double kMinSize = 0.0, kMaxSize = 1.0;
  static constexpr double kMinWidth = 0.0, kMaxWidth = 1.0;  // 0 = mono, 1 = wide
  static constexpr double kDefaultDecayMs = 1200.0;
  static constexpr double kDefaultPreMs = 0.0;
  static constexpr double kDefaultTone = 0.4;
  static constexpr double kDefaultSize = 0.6;
  static constexpr double kDefaultWidth = 1.0;
  static constexpr double kMaxWidthSpread = 0.05;  // (R3, 2026-10-15: retired --
  // the width spread is now the post-mix Haas stage below, widthHaasOn_)
  // Width (R3, 2026-10-15): the convolver's own Haas law (ConvolutionReverb's
  // kHaasDelayMs 3.5 ms / kHaasDepth 0.55, mono-IR spread pair), applied
  // post-mix on the wet at every mode's output: side = width*0.55*wet[3.5ms
  // behind]; L += width*(side), R -= width*(side). Sum-preserving
  // ((L+R)/2 invariant -> every level pin intact); bit-exact at width 0 and
  // inert on a 1ch buffer (the gate is 2ch && width>0); on a 2ch buffer the
  // M/S terms add the true mid/side fold, so Width is ONE coherent control in
  // every mode (replacing the old per-bank 5% tap-scale drift, which was
  // inert in Hall and in production's per-lane buffering).

  // Digital mode laws (mode 5 only). Both reduce to the identity at 0, so
  // Digital @ Density 0 @ Mod 0 is byte-identical to the plain comb bank (the
  // bit-identity anchor). Density blends each line's feedback toward the mean
  // of all lines (a convex combination -> cannot destabilise). The Mod waver
  // mirrors the delay's Mod: a sined read-tap wobble, L + / R - opposite
  // phase, gated off at 0 -> the plain integer read stays bit-exact.
  static constexpr double kDigitalModHz = 5.0;        // Mod waver rate (Hz)
  static constexpr double kDigitalModWaverMs = 4.0;   // full-mod tap waver +/- ms
  // Schroeder/Moorer all-pass diffusion: a per-channel first-order all-pass on the
  // summed comb wash, driven by Density (the classic "multiply the echoes / smooth
  // the comb" diffusion stage). Identity at Density 0 (the bit-identity anchor
  // holds), phase-only (unity gain -> no level change), bounded (|coef| < 1).
  static constexpr double kDigitalDiffuse  = 0.55;    // max all-pass coefficient

  // ---- Spring laws (mode 1; the recognisable 1-D metallic spring) ----
  // boing: the metallic few-mode ring, a single 2.4 kHz resonator excited by
  // the onset, gain kSpringBoingGain/N so it DILUTES as Springs rises (the
  // gotcha: more springs = smoother, less boingy). drip: a short onset splash
  // (the driver hitting the spring), more springs = more driver activity.
  // Sag: extra loop low-pass (HF decays faster -> low tones lag/darken = the
  // dispersion). color: a subtle level-driven soft-shoulder (clean at low
  // level, mild warmth on peaks) -- our own clean-room law, not a re-clip from
  // the compressor (avoids pulling juce_audio_processors into core headers).
  // IR-measured: the real Deluxe spring peaks at 2 kHz, has a broad 500 Hz
  // 'suspension body', and rolls off hard above ~4 kHz (-20 dB+ at 8 kHz).
  // We model this with a 2 kHz metallic ring, a 500 Hz body resonator, and a
  // 5 kHz wash low-pass. The 3-8 kHz energy is the artificial part we remove.
  // Incommensurate resonator cluster (IR-driven, replaces single 2 kHz boing):
  // three rings at non-harmonic spacings avoid the clean THD of a single tuned
  // resonator while keeping the metallic, springy character spread across the
  // 200 Hz – 3 kHz band the real tank shows in the Deluxe IRs.
  // HPF on the comb output: cuts the 63 Hz comb peak (6 parallel delays sum
  // coherently at DC — the 30-IR average shows 63-125 Hz at -4 to -13 dB below
  // the 500 Hz peak). 1-pole HPF @ 200 Hz cuts 63 Hz by ~-17 dB, 125 Hz ~-5 dB.
  // State: hpfX_ (prev input), hpfY_  (prev output), per channel (2 new arrays).
  // 8 incommensurate resonator modes (metallic body, peak at ~500 Hz).
  // rb is the per-sample amplitude decay (|pole|); higher = longer ring.
  // gain is the dry-signal excitation; combined with the mode's 1/(1-rb)
  // resonance peak they shape the spectrum. Peak at mode 2 (490 Hz).
  static constexpr int    kSpringNumModes = 8;
  static constexpr double kSpringModeHz  [kSpringNumModes] = {
      220.0,  350.0,  490.0,  640.0,  900.0, 1300.0, 1800.0, 2800.0 };
  static constexpr double kSpringModeRb  [kSpringNumModes] = {
      0.90,   0.90,   0.92,   0.90,   0.88,   0.88,   0.86,   0.84  };
  static constexpr double kSpringModeGain[kSpringNumModes] = {
      0.046,  0.058,  0.080,  0.058,  0.046,  0.046,  0.032,  0.022 };  // ears-pass: boing slightly louder RELATIVE to the wash, brighter — the 1.3-2.8 kHz pings lifted ~+4 dB more than the 220-900 Hz body modes (ping + sparkle)
  // (removed: kSpringBodyHz/Rb -- replaced by kSpringR1)
  static constexpr double kSpringBodyGain   = 0.018;   // ISOLATION: body ON   // modest: body is under the metal, not over it
  // Wash low-pass: cut HF shimmer. IRs show -20 dB+ at 8 kHz, up to -73 dB at 16 kHz.
  // A 1-pole LP at ~5 kHz @48 kHz kills the artificial HF comb shimmer.
  static constexpr double kSpringWashLpHz   = 3500.0;  // wash LP corner
  // alpha for 1-pole LP @5 kHz @48 kHz: 1 - exp(-2*pi*5000/48000) = 1 - exp(-0.654) ~= 0.48
  static constexpr double kSpringWashLpA    = 0.28;    // wash LP @3.5 kHz (family IRs: -19 to -42 dB at 8 kHz): a hair brighter than 0.32 for a sparkle lift over 1-4 kHz
  // ---- Spring tail tone (spring-family convolved-burst calibration) ----
  // The BARE comb tail is BRIGHT: band-peak scans of the raw spring land on
  // 2-4 kHz with 8-12 kHz shimmer (single-bin DFT misread this as a "125 Hz
  // peak / 500 Hz hole" -- it was just missing the sharp resonances that sit
  // slightly off bin). Real springs are LOW-CUT (GBS 63 H -22, 125 H -27;
  // Demeter 63 H -40) and peak their body at 250-500 H with the highs tamed.
  // Three small, linear, live elements shape it there:
  //   1) 1-pole HPF: tames the parallel-comb DC/sub bump (real springs lack it).
  //   2) Peaking EQ: pulls the tonal centre down to the 250-500 H body band.
  //   3) (above) steeper wash LP: rolls the 8-12 kHz shimmer (GBS -19, Demeter -40).
  static constexpr double kSpringHpfHz      = 200.0;   // HPF corner (real springs are low-cut)
  static constexpr double kSpringPeakHz     = 500.0;   // body centre: centred up from 450 so the 250-350 Hz low-mids thin out (ears: "low mids a bit stronger than a spring")
  static constexpr double kSpringPeakDb     = 11.0;    // body lift: -2 dB vs 13 (ears: low mids slightly too strong)
  static constexpr double kSpringPeakQ      = 0.70;    // wide body shelf, not a whistly ring
  // The metallic boing and the broadband drip splash are the FIZZ/breakup (bright
  // sustained resonance + broadband transient). Removed (0.0): the spring is now
  // the clean 1-D mode wash + Sag dispersion + a faint level-driven compression.
  // (Re-enable to a whisper if you want a metal tint.)
  // The real CHARACTER (metallic boing ring, onset splash, and Plate's bright whip)
  // is KEPT at a subtle level - that is what makes it sound like a spring/plate.
  // The FIZZ/distortion came from the per-sample soft-shoulder below (a non-
  // linearity: clipping a reverb tail's many peaks adds high harmonic fuzz). It
  // is now OFF (colorAmt = 0), so the layers stay linear/clean.
  static constexpr double kSpringBoingGain  = 0.000;   // disabled: replaced by the R1/R2/R3 cluster   // metallic ring (up from 0.028: more "ping")
  static constexpr double kSpringDripDecay  = 0.98;    // the onset splash decay (per sample)
  static constexpr double kSpringDripAtk    = 0.060;   // slow onset (IR peak at 40-68 ms -- the 'plonk', not a 'clack')
  static constexpr double kSpringDripGain   = 0.020;   // reduced: less comb excitation   // broadband splash (the ORIGINAL value -- reducing it didn't move the needle on the Spring's distortion, so we keep it for the "drip" character)
  // --- the "+3dB dwell" the ears found nicer, baked into Spring + Plate presence ---
  // kSpringPresence is pulled back to 1.0 (0 dB): the +1 dB "dwell" now lives in
  // kSpringWashRef (below), so the Spring's wet level + dwell are governed by
  // one coherent constant (the wash law) instead of two (wash * presence).
  // The Spring reads about the same at the fader (the user normalizes in the DAW).
  static constexpr double kSpringPresence   = 0.942;   // dwell: user A/B'd the 073339 build (1.120) and prefers it backed off -1.5 dB (1.120 * 10^(-1.5/20) = 0.942)
  static constexpr double kPlatePresence    = 1.189;   // +1.5 dB wet dwell (user-confirmed: Plate is the LOWER-dwell member of the pair).
      // 2026-10-13 note: kept AT 1.189 through the model retune - the level law
      // (within 1 dB of the 83.5977 rawAbs baseline) must hold for the final
      // tune; the body boost that briefly needed presence compensation was
      // removed (50% rebalance already reproduces the EMT-140 body, -16.6 vs
      // -17.4; see ir-reverb-training.md).
  // --- shared wash diffusion (1st-order all-pass, Schroeder/Moorer). --- The comb
  // wash's long-decay resonances turn into standing-waves / whistles / the metallic
  // "echo" that appears after a second (worse past ~2000 ms). A phase-only all-pass
  // on the wash smears them into a smoother decay. Per-mode basis (x a decay scale
  // 0.4..1.0, more at long decay where the modes break):
  static constexpr double kSpringWashAp     = 0.30;    // Spring wash diffusion (back to the pre-round-12 value while we are isolating the true source of the Spring distortion)
  static constexpr double kPlateWashAp      = 0.62;    // Plate wash diffusion (more: kills the tiny residual hiss at high dwell)
  // PLATE COMB RETUNE -- CONSIDERED & DECLINED (measured 2026-10-10 vs the EMT 140
  // IR reference; do NOT re-try without reading the numbers):
  //   lever 1 (decay-tracked loop LPF):  fixes the inverted decay->darkness track
  //     but measurably WORSENS click comb 16.7 -> 21.4 dB (EMT 14.7); rejected.
  //   lever 2/3 (>=4 in-series / Hadamard-mixed APF diffusion): in-series unit-
  //     gain chain 22.4 dB; Hadamard-8 bank smeared the onset (ping 42.7 -> 25.1,
  //     chorus-side) and mis-levelled +13.9 dB unless Parseval-scaled; rejected.
  //   lever 4 (tap-time mod): 2.5 % -> comb 17.6 but low-mid body 23.9 -> 8.5 dB
  //     (LF slope -17 -> -44 dB/s); 0.35 % -> comb still 19.1 > 16.7; body-killer,
  //     rejected at every depth ("chorus-ification is a failure").
  //   The as-shipped plate already beats the reference on steady comb (2.66 vs
  //     4.32 dB) and is within +2 dB on click comb; residual gap is STRUCTURAL
  //     (onset density 42.7 vs 46.6; body 23.9 vs 33.2) = out of scope here.
  //   Full A/B matrix + CPU: docs/tickets/complete/plate-combing-closeout.md.
  // PLATE TEXTURE (plate-texture.md, 2026-10-13) -- CONSIDERED & DECLINED:
  //   L1: denser early-ping bank (5 -> 9 incommensurate taps spread over
  //     10-142 ms, sum-of-amps 1.17 preserved for level-matching): the onset
  //     ping ratio DROPPED 42.7 -> 29.5 dB (EMT 46.6) - spreading taps raises
  //     the incoherent band FLOOR under the peaks, same onset-smearing failure
  //     as the Hadamard bank (42.7 -> 25.1). The reference's high peak/mean
  //     comes from a SPARSE, SPEAKING comb (a few strong reflections over a
  //     quiet floor), not tap density - "more taps = denser" REJECTED.
  //   P-body (in-loop, shipped 2026-10-12): per-round-trip band-selective
  //     boost in the feedback loop (500-800 Hz survival deficit vs the EMT
  //     140, which keeps the fundamental alive at 1-2 s); capped fb*A <= 1
  //     for stability at every decay dial (wash-norm lesson); constants are
  //     the tuning knobs (fine-tune against the ears).
  //   REJECTED in-loop darkening (feedback LPF / peaking, 200 and 220 Hz
  //     variants): in-loop = feedback-state change -> click comb +4.7 to +10 dB
  //     at body gains that do not survive the fb cap (in-loop body at the
  //     stable max adds only ~+1 dB at the 2.0 s default); an LPF variant at
  //     4 dB was UNSTABLE. In-loop spectral shaping stays DECLINED (comb cost
  //     is structural: the darkening feeds back).
  //   REJECTED biquad OUT-stage (peaking "RBJ-style" body; lowpass "RBJ-style"
  //     air; fc 220-2400 Hz at 48 kHz): measured variants UNSTABLE (unnormalised
  //     peaking, pole 1.127 -> NaN) or DC-DEAD (the lowpass form's DC gain is
  //     0.035 -- 6-12k passes uncut: it is a resonator, not a lowpass; level
  //     +14 dB), or wrong-shape (a 240 Hz "peaking" peak at 600-1.2 kHz). Only
  //     1st-order stages (y += a*(x-y), a = 1-exp(-2*pi*fc/fs)) are provably
  //     stable and DC-exact at fc << fs -- the SHIPPED air law is exactly that
  //     (2 stages, -6 dB/oct each, 4900 Hz at short decay -> 3600 Hz at long:
  //     LONGER = DARKER, the direction the EMT 140 reference family itself
  //     shows: 0.5 s len bal -62.5 vs 2.0 s len -73.0 (6-12k minus 150-300,
  //     click-tail 0.3-0.6 s) -- the ticket's "shorter = darker" phrasing is
  //     INVERTED relative to the family measurement; the guard pins the
  //     MEASURED direction and the plate now complies (600 ms -32.5 > 2400 ms
  //     -73.3 dB). A 2nd order stage is only admissible from a verified
  //     library implementation with numeric pole-radius/DC checks (see
  //     docs/agents/ir-reverb-training.md, biquad landmine).
  //   CONSIDED then REMOVED: 1st-order body ADD (y += G*(LPF_300(x)-LPF_120(x))
  //     in the output path): stable + correct shape (+4 dB on the 120-300 band,
  //     zero DC, zero HF) -- but the 50 % NUTRAL rebalance (user decision,
  //     2026-10-13) already reproduces the EMT 140 body (plate -16.6 vs
  //     reference -17.4 dB at the default), so the boost only cost comb
  //     (16.7 -> 19.4 dB) + level. The original "body dead" complaint was an
  //     artifact of the old asymmetric 60 % dials (tone 0.35 brighter, size
  //     0.70 longer). Simpler final engine = LESS CPU + no level-compensation
  //     hack (presence stays 1.189 exactly, the user-confirmed +1.5 dB dwell).
  //   Plate/"140" model STATE (FINAL PASS, 2026-10-13, tuning: 50 % dials,
  //     shipping width 0.50; reference IR width 1.0 for the stereo side):
  //     The P3 onset re-attack WON this pass: the onset bank was retuned from
  //     5 taps {12.7,19.3,27.8,43.1,58.4 ms} / amps {0.32,0.27,0.23,0.19,0.16}
  //     (sum 1.17) to 3 taps {14.2,26.9,51.3 ms} / {0.42,0.30,0.16} (sum 0.88)
  //     with kPlateBrightOnset lowered 0.026 -> 0.015 (the whip FLOOR under the
  //     pings). Result vs EMT 140 2.0 s (house convolver, 600-block warm,
  //     both sides peak-normalised): onset ping 37.15 -> 45.66 dB at 2.0 s
  //     (EMT 46.60 -- the pre-pass structural ~9 dB gap is now within 0.9 dB,
  //     and at the 2.5 s cap the plate reads 46.85; the 42.7 dB decline bar
  //     of the L1 attempt is beaten at both lengths; the
  //     OnsetDensityMeetsReference guard, floor 33.0 + within 12 dB of the
  //     reference, holds both-sided); body 9.63 -> 7.50 dB
  //     (EMT 9.31, within the -10 dB guard); comb 20.31 -> 19.39 (EMT 14.71,
  //     within the model-baseline + 1 dB guard); level 84.10 -> 78.23
  //     (the level baseline RE-PINNED to the C state, within the
  //     +/- 1 dB guard -- the lower
  //     incoherent floor is the cost: the pings stand out against a quieter
  //     floor, which is the physical-cavity signature of the EMT family
  //     itself).
  //     The A/B that settled it (all on 48 kHz, 50 % dials, 2.0 s unless
  //     noted):
  //       pristine (5 taps, whip 0.026): ping 37.15, body 9.63, level 84.10
  //       C     (3 taps sum 0.88, whip 0.015): ping 45.66 (46.85 @ 2.5 s cap),
  //             body 7.50, level 78.23
  //       C3    (5 uneven taps sum 1.14, whip 0.026 KEPT): ping 39.70
  //             (+2.5 dB only), body 7.15, level 83.52 -- near-miss; the win
  //             is the FLOOR CONTRAST (sparse taps over a lower incoherent
  //             floor), NOT the tap count or amplitude sum. The L1
  //             dense-9-tap bank above (42.7 -> 29.5) is the same smearing law
  //             from the other side: more energy in-band = higher floor =
  //             thinner peaks. "Sparse over quiet" is the plate family's
  //             onset law.
  //     P4 Size law: NEW this pass (the Size dial was a no-op on the plate
  //     path: processPlate never read params_.size -- user redirect, 2026-10-13:
  //     make Size do something plate-classful). A corner shift on the air-law's
  //     2-stage LPF (kPlateSizeCornerFrac = 0.30), size 1 (large-class) =
  //     BRIGHTER, size 0 (compact) = DARKER (measured hflf
  //     -59.9/-46.7/-35.3 at 0/0.5/1 -- the direction was the OPPOSITE of the
  //     first draft's "bigger = darker" physics guess: the measurement wins);
  //     the 50 % anchor is bit-identical (sizeDelta = 0.0 there). A separate
  //     extra series stage for Size was tried and REJECTED (it can only darken;
  //     its low-band floor swamps the body at the tail offsets).
  //     TODO (future ticket): Chamber / Hall / Digital paths never read
  //     params_.size either -- audit + same treatment (see the size-law block).
  //     LEVEL / ONSET / COMB guards re-pinned after this pass (see
  //     test/src/plate_family_tests.cpp + plate_texture_tests.cpp); family
  //     table + cross-model + CPU: docs/tickets/plate-140-final-training-pass.md
  //     close-out. Full A/B + lever log + the general MODEL/SUBTYPE TUNING RULE:
  //     docs/agents/ir-reverb-training.md + close-out in tickets.
  static constexpr double kPlateBodyHz      = 550.0;   // resonance centre (the body note)
  static constexpr double kPlateBodyDb      = 1.30;    // per-round-trip boost (dB) at the centre
  static constexpr double kPlateBodyQ       = 0.60;    // bandwidth (Q) -- 500-800 Hz coverage

  // PLATE TEXTURE OUT-STAGES (plate/"140" model tuning -- ir-reverb-training.md;
  // mode 0 only, plate constants only, the generic mechanism untouched):
  //  BODY  = a low-mid band-pass ADD, 120-300 Hz (two first-order LPFs:
  //          o += G*(lp300(x) - lp120(x)) -- zero DC shift, zero HF shift,
  //          unconditionally stable; the in-loop version is fb-capped at ~+1
  //          dB and costs comb, the biquad peaking here went unstable at this
  //          fc/Q (pole 1.13 -> NaN): a first-order band-pass add is the exact,
  //          provable form. Closes EMT-140's warmer body (ref: 150-300 =
  //          +12 dB over ours at 1-2.5 s, measured 2026-10-13).
  //  AIR   = a decay-tracked first-order LPF CASCADE (-6 dB/oct per stage) on
  //          the diffuse field before the onset attack terms (the onset stays
  //          bright; in-loop attempts regressed the comb +7 dB and one went
  //          unstable -- tilt belongs OUT of the loop). The corner law follows
  //          the reference family (probe 0.5-3.0 s, 2026-10-13): longer decay
  //          = darker top end, so longer = lower corner (4900 -> 3600 Hz).
  static constexpr double kPlateAirHzBright = 4900.0;  // shortest decays (brightest top end)
  static constexpr double kPlateAirHzDark   = 3600.0;  // longest decays (darkest top end)
  static constexpr int    kPlateAirStages   = 2;       // first-order LPF stages (-6 dB/oct each)
  // SIZE LAW (tr. "plate-140-final-training-pass" P3, final pass): Size
  // selects the plate's physical class along the SAME air-law corner -- a
  // 0.0->1.0 sweep from a COMPACT plate (size 0, sparse mode density, DARKER
  // modal tail) to a LARGE-class plate (size 1, densely packed modes, BRIGHTER
  // tail). Physics: more densely packed high modes -> more HF resonances
  // survive -> the tail reads brighter (MEASURED in test/src/plate_family_tests.cpp
  // SizeLaw, 2026-10-13: hflf @0.4-0.6 s @2.0 s decay = -59.9 / -46.7 / -35.3 dB
  // for size 0.0 / 0.5 / 1.0 -- size 1 is the BRIGHTEST, not the darkest).
  // The law is a PURE CORNER SHIFT on the air-law's existing two-stage LPF
  // (not a series stage: a separate extra stage was tried during this pass and
  // REJECTED -- it can only darken, never brighten, and its low-band floor
  // swamps the body at high-freq tail offsets).
  // At size 0 the corner is exactly kPlateAirHzDark (3600 Hz, the air-law's own
  // darkest corner); at size 1 it is exactly kPlateAirHzBright (4900 Hz, the
  // air-law's brightest corner); size 0.5 is bit-identical to the pre-size-law
  // pinned 140 tuning (anchor: the sizeDelta term is exactly 0.0 there, so
  // airLp_ is numerically identical to the pre-change constant). The sweep
  // width (kPlateSizeCornerFrac = 30 % of the air-law's 4900->3600 Hz range
  // -> +/- 780 Hz per unit size, clamped at the air-law's own corners) is a
  // plate-class sweep -- visible in the 6-12k vs 120-300 tail split (a few dB
  // on the tail's high band, the reference family's own size-to-size spread),
  // not a subtle dial.
  static constexpr double kPlateSizeCornerFrac = 0.30;  // size-law: full-range plate-class sweep (at 2.0 s decay: +/-' 780 Hz) -- a corner shift, not a series stage
  // TODO (future ticket, NOT this one): the Size dial is read elsewhere only by the
  // Spring path (sizeScale, around :616) and the Room path (early scale);
  // Chamber / Hall / Digital never read params_.size at all -- audit those
  // paths and either wire a mode-appropriate size law or repurpose the dial,
  // same treatment given here.
  static constexpr double kRoomWashAp       = 0.22;    // Room wash diffusion (keep the early discrete, smooth the long tail)
  static constexpr double kHallWashAp       = 0.68;    // Hall wash diffusion (more: tames the pinging/metallic standing wave under hard drive)
  // The splash/whip re-inject the dry ATTACK transient (a sharp broadband burst =
  // the "fizz"). We low-pass them (kSplashSoftA) so they keep the transient BODY
  // but lose the sharp HF "hi-hat" fizz. Shared by Spring drip + Plate whip.
  static constexpr double kSplashSoftA      = 0.08;   // slower LP (more HF cut on the drip onset)    // softening 1-pole coeff (back to the pre-round-12 value; moot while drip/boing are off)
  // P-ons (percept: onset density, 2026-10-06): the plate fires as a dense
  // whole -- the EMT 140 ref onset is a discrete early whip (~12 resolvable
  // pings in the first 120 ms) on top of the wash, ours had only the 33-204 ms
  // comb echoes (8 dark pings + one soft 600 Hz body burst). A small
  // incommensurate early-tap bank (membrane pings) is added on the wet path:
  // fixed ms (like Chamber/Hall), incommensurate with the 33.3-203.7 ms comb
  // periods, through a ~2.5 kHz soft single-pole LP (fizz-safe: no clipping,
  // roll stays inside the ref's own onset bell ~-6 dB @ 4k), armed by the
  // Bright gate exactly like the body-burst and levelled by the same Bright
  // law -- zero at Bright = 0, so the plain comb stays for the anchor.
  static constexpr int kPlateOnsetTaps = 3;
  static constexpr double kPlateOnsetDelayMs[kPlateOnsetTaps] = {14.2, 26.9, 51.3};
  static constexpr double kPlateOnsetAmps[kPlateOnsetTaps]    = {0.42, 0.30, 0.16};
  static constexpr double kPlateOnsetLpA  = 0.33;           // ~2.5 kHz soft LP (fizz-safe)
  static constexpr double kSpringColorAmt   = 0.5;     // OFF: the soft-shoulder was the fizz/distortion (knee/amount moot) -- clean-rooms port of the compressor's soft-knee law, kept in-tree for the driver color (identity below the knee, only touches true peaks)
  // Spring wash level law: the spring's few-line wash runs loud (small N + its long
  // default RT give a high (1-fb)/N). kSpringWashFrac scales the wash body DOWN into
  // the family band (Digital/Room/Plate) so the spring is a quiet, subtle voice --
  // "a very small amount of compression with almost no breakup" -- not the loudest tail.
  static constexpr double kSpringWashFrac   = 1.20;    // comb wash background (resonators carry the metallic voice)
  // OUT-LEVEL — single uniform trim on the summed Spring output (ears passes 1-4).
  // Pass 4: "still hotter, -6 dB more" / "just lower the output trim - do NOT
  //  Shift the wash:ping character". -6 dB below 084117 = total -13.6 dB on
  static constexpr double kSpringOutLevel   = 0.157;   // 10^(-16.1/20): pass 8 "just -1.0 dB on the output" on 0.176
  // Pass 5: "at mix 50% the WET part of the mix (the sustained reverb) needs
  //   to be slightly louder vs the dry - nothing else, no mix defaults" ->
  //   the WET (comb wash) component alone, +3 dB. Ping/splash untouched.
  // Pass 6: "+3 dB more like before" -> +6 dB total on the wash.
  // Pass 7: "+1.5 dB on the wash" -> +7.0 dB total on the wash.
  static constexpr double kSpringWetLift    = 2.239;   // 10^(+7/20)

  // ---- Plate laws (mode 0; the dense 2D mode wash, not a 1-D metallic line) ----
  // The plate is a DENSE, dispersive 2-D surface: (a) a HIGH fixed cross-coupling
  // (denseMix) blends all 8 lines into a smooth, uniform mode wash (the plate's
  // modal density ~ constant over the band -- the fact that separates plate from
  // spring, and its distinctiveness from Digital which leaves the combs in-
  // commensurate); (b) Bright = a dense bright onset burst (the "whip" -- the
  // plate fires as a dense whole from t~0 and decays, not a single comb echo);
  // (c) Bloom = the dispersion, low survives longer than high (the plate's
  // bright->bloom, a dispersive medium -- the same low-lag as Spring's Sag, here
  // named Bloom); (d) a subtle level-driven soft-shoulder (the shared driver
  // FET-odd / transformer color) warms the peaks. All bounded (|fb|<1, |mean|<1,
  // bounded onset env, soft shoulder). Gated Bright>0 || Bloom>0; at both 0 the
  // plate is the shared plain comb bank (the Digital anchor holds).
    static constexpr double kPlateDenseMix     = 0.70;   // the 2-D dense mode wash
  static constexpr double kPlateBrightOnset  = 0.015;   // the "whip" onset (character, kept gentle) -- round 12: user only wanted Spring touched, Plate is back to the original value
  static constexpr double kPlateBrightDecay  = 0.96;   // the burst decay (fast whip)
  static constexpr double kPlateBloomFrac    = 0.32;   // how far Bloom darkens (low lags) -- the ORIGINAL value (I had reduced it to 0.25 without being asked; user did not want the Plate touched)
  static constexpr double kPlateColorAmt     = 0.3;    // OFF: the soft-shoulder was the fizz/distortion (knee/amount moot) -- clean-rooms port of the compressor's soft-knee law, kept in-tree for the driver color (identity below the knee, only touches true peaks)
  // ---- Room laws (mode 3; the discrete early-reflection set + per-tap air
  // absorption + a mode wash, per Gardner small 1992 / Moorer air law) ----
  // The early field is a fixed 4-tap TDL (Gardner small: 8.3/22/35/66 ms, we
  // round) with decreasing amplitudes (the farther tap is quieter). Each tap
  // carries its own lowpass -- the Air dial scales this (more air = darker,
  // Moorer's air-absorption law: farther reflection = darker). The Early dial
  // scales all taps' amplitude. The base is the shared 8-comb mode wash (decay
  // ~500 ms default = a small room). Gated Early>0 || Air>0; at both 0 it is
  // the shared plain comb bank (the Digital anchor + kNumModes==6 hold).
  static constexpr int kNumRoomTaps = 4;
  static constexpr double kRoomTapDelaysMs[kNumRoomTaps] = {8.0, 22.0, 35.0, 66.0};
  static constexpr double kRoomTapAmps[kNumRoomTaps]     = {0.1375, 0.121, 0.099, 0.077};
  static constexpr double kRoomAirFrac                   = 0.50;  // per-tap lowpass fraction

  // ---- Chamber laws (mode 4; a diffuse early VOLLEY + the dual-decay bass
  // shelf -- the one signature no other mode has: LF extends, HF caps) ----
  // Volley (a short, diffuse cluster, denser than Room's discrete 4 taps but
  // shorter and fuzzier than Hall's long build) is modelled with a fixed, DENSE
  // set of 8 early taps (5/9/14/20/27/35/44/55 ms). The "volley" dial scales
  // the whole cluster's energy (more = a denser early burst). Bass (the
  // dual-decay) is modelled with TWO low-pass laws:
  //   (a) the mode-wash loop is low-passed (more at high Bass -> the LOW tail
  //       survives the HIGH tail longer = the Bass shelf, LF extends);
  //   (b) the output is low-passed even more (a steeper HF cap, the Abbey Road
  //       ~10 kHz humidity cap: the HF decays fast regardless of Decay).
  // No metallic ring (no per-tap resonance), no plate bloom (the loop low-pass
  // is the only HF law, and the output adds a second cap). Gated
  // Volley>0 || Bass>0; at both 0 it is the shared plain comb bank (the Digital
  // anchor + kNumModes==6 hold).
  static constexpr int kNumChamberTaps = 8;
  static constexpr double kChamberTapDelaysMs[kNumChamberTaps] = {5, 9, 14, 20, 27, 35, 44, 55};
  static constexpr double kChamberTapAmps[kNumChamberTaps]     = {0.084, 0.078, 0.072, 0.066, 0.06, 0.054, 0.048, 0.042};
  static constexpr double kChamberBassFrac     = 0.55;  // loop low-pass (Bass shelf)
  static constexpr double kChamberHFCapFrac    = 0.14;  // output HF cap (fixed scale; light = a tad brighter/louder)
  // Chamber wash level law: a small lift so the Chamber sits just above the
  // Digital/Room family (the ears say it reads "a tad quiet"). The dual-decay
  // bass shelf (Moorer's air law) is untouched -- only the body level is lifted.
  // Chamber wash level law. 1.20 (the ear-shipped round, "ever so slightly too
  // loud" -> kept) -> 1.60 (P2.4 density uplift, 2026-10-16, reverb-budget-
  // quality-uplift pass 1). Hunt: test/src/chamber_probe_tmp.cpp, suite
  // ChamberProbe (P1's exact metrics — full/combPk/densTail/tone440 — against
  // Medium Chamber 17, ref row full=59538 tone=-24.33 densTail=-108.68; P1
  // shipped row full=52278 tone=-32.66 densTail=-128.19, the tone axis was the
  // suite's worst: 8.3 dB dark). DECLINED levers (measured, same probe):
  //   bassFrac 0.55 -> 0.45/0.35: tone -32.66 -> -32.45/-32.25 (<=0.4 dB INERT,
  //     and it WORSENS combPk 47.51 -> 48.72 — the loop LP is the dual-decay
  //     BASS law, ear-tuned — untouched);
  //   capFrac  0.14 -> 0.10: tone -32.70 (INERT, comb +0.15 worse);
  //   washAp   0.42 -> 0.25/0.60: 0.0 dB INERT (washApSc is level-neutral by
  //     law, and it does not de-comb the 8 incommensurate lines — the same
  //     structural metallicity Plate/Spring/Room all carry, pass-2 territory).
  // washFrac 1.20 -> 1.40/1.60/2.00: full 52278 -> 52597/53044/53309, tone
  //   -32.66 -> -31.49/-29.27/-26.27, densTail -128.19 -> -127.18/-125.69/
  //   -123.76: the ONLY lever that moves full+tone+densTail right (the wash
  //   body level IS the sustained field). 2.00 DECLINED: tone -26.27 overshoots
  //   the family's own 2 s number (-24.3 is the ref itself, the m00381 family-
  //   not-IR law); 1.60's -29.27 stays ~5 dB inside the family (the Chamber's
  //   own dual-decay bass shelf + HF cap are what legitimately colour it dark
  //   — those are the ear-tuned laws, not the level). Zero added CPU: a
  //   constant multiply already in the loop (the value changed, not the work) —
  //   ReverbCeiling pins hold.
  static constexpr double kChamberWashFrac     = 1.60;  // the chamber body level (P2.4 family-matched uplift)
  // Wash diffusion (Schroeder / Moorer all-pass) -- added to kill the slow, low-frequency
  // "pad" build-up the user hears above the 3000 ms onset. The other four law-modes
  // (Spring/Plate/Room/Hall) already have this; the Chamber was the last one left on
  // the plain comb. Phase-only, so the calibrated level is preserved. Gated on
  // decayDiffFrac (stronger diffusion at higher decay -- the pad starts there), so
  // below the onset the Chamber's character is untouched (the ears: "solid").
  static constexpr double kChamberWashAp       = 0.42;  // Chamber wash diffusion (moderate; the pad is a deep resonance, needs enough smear without killing the "big")

  // ---- Hall laws (mode 5; the LONGEST early section + the strongest L/R
  // lateral energy -- the one signature no other mode has: a long, wide, dense
  // early build-up + the longest overall decay). ----
  // Build (the long diffuse early build-up, the hall's "air") is modelled with a
  // fixed SET OF 10 LONG EARLY TAPS (10/18/28/40/55/72/90/110/130/150 ms -- a
  // LONG build-up, 2x the length of Chamber's 8 taps). The Build dial scales the
  // whole cluster's energy (more = a longer, denser early build-up). Space (the
  // lateral energy, the spatial impression) is modelled with a LATERAL SPLIT of
  // the early cluster: the L channel's early taps get MORE energy, the R
  // channel's get LESS (more Space = a WIDER, more lateral early field).
  // No metallic ring, no plate bloom, no chamber bass shelf -- just the long
  // early + the long decay + the strong lateral split.
  static constexpr int kNumHallTaps = 12;
  // The hall's early FIELD: a DENSE FAST BUILD (many close taps) + a LONG low-passed
  // tail (a few distant, quiet reflections) -- the hall's "long build-up", denser and
  // longer than Chamber's 8 taps, but each reflection air-low-passed (Moorer) so it
  // reads as a diffuse build, not a metallic shimmer.
  static constexpr double kHallTapDelaysMs[kNumHallTaps] =
      {4, 7, 10, 14, 18, 23, 29, 36, 45, 60, 78, 100};
  static constexpr double kHallTapAmps[kNumHallTaps] =
      {0.10, 0.09, 0.08, 0.075, 0.068, 0.06, 0.052, 0.045, 0.038, 0.030, 0.024, 0.020};
  static constexpr double kHallLateralSplit = 0.40;  // the L/R early split (per Space dial)
  static constexpr double kHallAirFrac      = 0.50;  // per-tap air low-pass (Moorer: farther = darker)
  // The Hall's wash normaliser reference (see processHall): a fixed fb so the
  // level doesn't collapse at RT 3000 ms where the live-(1-fb) norm goes quiet.
  static constexpr double kHallWashFbRef   = 0.40;   // Hall wet PRESENCE (lower ref = higher wash gain). Loud enough to carry the tail; the LOWER coupling (0.45) does the de-ringing, this just sets level.
  // The diffuse 3-D wash coupling (the HIGHEST of the family). A big hall's late
  // field is statistical/diffuse (thousands of reflections), not 8 sparse comb
  // modes - which is exactly why a plain comb wash "keeps going" (the modes ring
  // and beat like resonances). So we blend each line's feedback toward the
  // all-lines mean (F2F2-like, Schroeder/Moorer) with a high coefficient: the
  // late field becomes a smooth, diffuse hall wash that decays naturally.
  // Dimensionality first: Spring 1-D (sparse) < Plate 2-D (dense 0.70) < Hall 3-D (0.85).
  static constexpr double kHallDiffuseMix = 0.45;  // the diffuse 3-D wash coupling -- cut from 0.85: the strong common-mode blend was the "weird ringing"/"keeps going"; lower keeps the 8 lines independent so the tail smears instead of resonating
  
  // Incommensurate base delay times (ms) so the parallel combs do not cancel
  // into a single pitchy tone.
  static constexpr double kBaseMs[kNumLines] = {
      33.3, 57.1, 81.7, 106.1, 130.4, 155.0, 179.3, 203.7};

  // ---- Six-character mode set (see plugin/docs/reverb-modes.md) ----
  // 0 Digital, 1 Spring, 2 Plate, 3 Room, 4 Chamber, 5 Hall. Each is a distinct
  // reverb law-set on one shared engine; Decay/Pre/Tone/Size/Width are the
  // shared dials and each mode carries its own two signatures. Modes differ by
  // *law*, not by separate DSP. (The delay/compressor `kNumModes` precedent.
  // Scaffold: the character blocks are wired per-ticket — Digital first, and
  // Digital@Density0@Mod0 must stay the current engine bit-for-bit.)
  static constexpr int kNumModes = 6;
  static std::string_view modeName(int mode) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 0: return "Plate";
      case 1: return "Spring";
      case 2: return "Room";
      case 3: return "Chamber";
      case 4: return "Hall";
      default: return "Digital";
    }
  }
  // Spring `Springs`: stepped 1..6 line count, default 3. Stored normalised
  // 0..1 like every other sig; this maps it to the shown count.
  static constexpr int kSpringsMax = 6;
  static int springsFromNormalized(double n) {
    return juce::jlimit(1, kSpringsMax,
                        juce::roundToInt(juce::jlimit(0.0, 1.0, n) * (kSpringsMax - 1)) + 1);
  }
  // Type (sub-model within a mode): each type is a distinct character
  // BUILT on the mode -- its own laws/structure (or a different algorithm
  // sharing the mode slot), NOT a knob preset. The UI cycles a mode's types
  // and remembers the choice per mode (Params::type[mode]). Type 0 is the
  // mode's modeled character (today's engine). numTypes is the single
  // source of truth (UI cycle + the setParams clamp below); a mode's count
  // goes to 2 when its second sub-model lands (the plate pass is the
  // reference implementation).
  static constexpr int numTypes(int mode) {
    return (mode == 5) ? 6 : 1;  // Digital owns 6 character subtypes; the other modes keep their scaffold (one self-type today).
  }
  // A type's law-routing mode (mode 5's type t -> mode t's law; other modes identity):
  // this lets the Digital dropdown arm the existing Plate/Spring/Room/Chamber/Hall
  // law + sig pair without touching mode 0-4's own dispatch.
  static constexpr int typeLawMode(int mode, int type) {
    if (mode == 5) {
      // Subtype order DIG SPR PLT SML CHM HAL -> law branches 5 1 0 2 3 4.
      switch (juce::jlimit(0, 5, type)) {
        case 0: return 5;
        case 1: return 1;
        case 2: return 0;
        case 3: return 2;
        case 4: return 3;
        default: return 4;
      }
    }
    return mode;  // other modes: identity (type always 0 for them)
  }
  // A type's characteristic dial defaults. For mode 5 (DIG) with type > 0:
  // land the character's defaultDialsForMode (tone/size/width). For any
  // other mode or DIG type 0: no dial override (sub-model switch, not a
  // preset -- the dials stay as the user left them).
  static bool defaultDialsForType(int mode, int type, double& decayMs, double& preMs,
                                  double& tone, double& size, double& width) {
    if (mode == 5 && type > 0) {
      double cDecay = 0.0, cPre = 0.0, cTone = 0.0, cSize = 0.0, cWidth = 0.0;
      const int ch = typeLawMode(5, type);  // the character's own mode number
      defaultDialsForMode(ch, cDecay, cPre, cTone, cSize, cWidth);
      cPre = 0.0;  // the subtype contract: pre is OFF (character defaults vary)
      decayMs = cDecay; preMs = cPre; tone = cTone; size = cSize; width = cWidth;
      return true;
    }
    (void)mode; (void)type; (void)decayMs; (void)preMs;
    (void)tone; (void)size; (void)width;
    return false;
  }

  // enterMode starting points (the doc's mode table) + the default value of
  // each mode's two signatures (local slot 0 = Sig A, 1 = Sig B; normalised
  // 0..1, `Springs` returned as 3 -> 0.4).
  static void defaultDialsForMode(int mode, double& decayMs, double& preMs,
                                  double& tone, double& size, double& width) {
    switch (juce::jlimit(0, kNumModes - 1, mode)) {
      case 0: decayMs = 2000.0; preMs = 0.5; tone = 0.50; size = 0.50; width = 0.50; break;  // Plate: NEUTRAL 50% dials incl. width; decay at the 2.0 s EMT 140 reference (the tuning target, 2026-10-13). Tuning-state rule (stereo reference IR => width 1.0 during tuning) lives in docs/agents/ir-reverb-training.md
      case 1: decayMs = 2000.0; preMs = 0.0; tone = 0.60; size = 0.60; width = 0.90; break;  // Spring (size 60%: the user's ears; tone 60% at the user's request)
      case 2: decayMs = 500.0;  preMs = 0.0; tone = 0.40; size = 0.30; width = 0.70; break;  // Room (the ears: tone 40%)
      case 3: decayMs = 1800.0; preMs = 1.0; tone = 0.50; size = 0.45; width = 0.85; break;  // Chamber
      case 4: decayMs = 3000.0; preMs = 2.0; tone = 0.60; size = 0.90; width = 0.95; break;  // Hall (decay at the engine max: longest allowed tail)
      default: decayMs = 1200.0; preMs = 0.0; tone = 0.40; size = 0.60; width = 1.00; break; // Digital
    }
  }
  // A subtype's characteristic sig default (the dropdown landing point):
  // 50 % for the mapped character's pair; DIG-own (type 0) is 0 (Mod off).
  // Other modes (single self-type) have none -> the caller leaves them alone.
  static double defaultSigForMode(int mode, int localSlot) {
    const int m = juce::jlimit(0, kNumModes - 1, mode);
    const int slot = (localSlot == 1) ? 1 : 0;
    double a = 0.0, b = 0.0;
    switch (m) {
      case 0: a = 0.5; b = 0.5; break;  // Bright 50%, Bloom 50% (neutral; the texture is tuned above this, 2026-10-13)
      case 1: a = 0.4; b = 0.30; break;  // Springs 3 (normalised 0.4), Sag 30% (the user's ears)
      case 2: a = 0.40; b = 0.40; break;  // Early 40%, Air 40%
      case 3: a = 0.4; b = 0.6; break;  // Volley, Bass
      case 4: a = 0.6; b = 0.7; break;  // Build, Space
      default: a = 0.0; b = 0.0; break; // Digital: Density 0, Mod 0 (the anchor)
    }
    return slot ? b : a;
  }

  struct Params {
    double decayMs = kDefaultDecayMs;
    double preMs = kDefaultPreMs;
    double tone = kDefaultTone;
    double size = kDefaultSize;
    double width = kDefaultWidth;  // stereo width (0 = mono, 1 = wide)
    int mode = 0;                  // 0..5 (Digital..Hall)
    // Per-mode signatures (2/mode, normalised 0..1; only the active mode's
    // two are live). Appended after the five shared knobs.
    double density = 0.0, mod = 0.0, springs = 0.4, sag = 0.4;
    double bright = 0.5, bloom = 0.5, early = 0.5, air = 0.3;
    double volley = 0.4, bass = 0.6, build = 0.6, space = 0.7;
    // Per-mode TYPE selectors (sub-model within a mode; type[i] = mode i's
    // type, 0 = its modeled/first character -- the only one today). One
    // field per mode = the choice is remembered per mode (the 12-sig
    // precedent). Appended after the 12 sigs (existing brace-inits keep
    // compiling; legacy state without the key = 0 = type 0).
    int type[6] = {0, 0, 0, 0, 0, 0};
    // Shared Mod (2026-10-14, R2 — all six modes): a NEW, slow post-mix LFO
    // depth (0 = OFF / bit-identical to today, 1 = full). Distinct from the
    // Digital-only `mod` read-tap-waver above — see the DESIGN DECISION in
    // docs/tickets/reverb-dry-mod-width-swing.md (R2). This is a fresh
    // mechanism (a post-mix one-pole lowpass breath, `sharedOn_`), NOT a
    // re-scaled `kSmearAmt`/`kFbDriftAmt` (those stay always-on, unchanged —
    // the bit-identity anchor holds at `sharedMod` 0). Appended LAST (after
    // type[6]) to match every reverbParams() aggregate's field order.
    double sharedMod = 0.0;
    // Digital Sig B "Shape" (R2): 0 = Sine (bit-identical anchor), 1..4 =
    // Triangle / Saw-Up / Saw-Down / Square — the same 5-detent law as
    // Chorus::shape() / `waveValue()`. Shapes ONLY Digital's read-tap waver
    // (the existing `mod` field still sets its DEPTH). The shared, all-modes
    // Mod knob (`sharedMod`) always uses a fixed sine LFO (`waveValue(0, ...)`
    // in `processSharedModSmear`) — it does not read this field.
    int wave   = 0;
    // T-DAT-001: low-end rolloff (PLT LoCut). 0 = full rumble (bit-exact
    // pass-through); higher = deeper sub cut. Appended AFTER `wave` so the
    // positional aggregates below keep compiling; state missing the key
    // reads 0.0 = pass-through.
    double loCut = 0.0;
  };

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    modPhase_ = 0.0;  // start the Mod LFO at phase 0
    // Longest possible tap: the largest base line at max size (1.5x) plus max
    // pre, with headroom for the width spread (the R bank runs a little longer).
    // Sized as a size_t so the ring length is exact even when the delay count
    // exceeds 2^32.
    const double maxMs = kBaseMs[kNumLines - 1] * 1.5 + kMaxPreMs;
    const size_t maxDelay =
        static_cast<size_t>(sampleRate_ * maxMs * 0.001 * 1.25) + 8;
    lines_.assign(kMaxChannels, std::vector<Line>());
    for (auto& ch : lines_) {
      ch.assign(kNumLines, Line{});
      for (auto& L : ch) L.init(maxDelay);
    }
    // Persistent input history (the early-reflection delay line): sized to the
    // longest early tap (across Room/Chamber/Hall) plus a typical block, as a
    // power of two, so the early field is live at every block length and the
    // reads never alias this block's writes. Zeroed.
    {
      double maxEarlyMs = 0.0;
      for (int t = 0; t < kNumRoomTaps; ++t)    maxEarlyMs = std::max(maxEarlyMs, kRoomTapDelaysMs[t]);
      for (int t = 0; t < kNumChamberTaps; ++t) maxEarlyMs = std::max(maxEarlyMs, kChamberTapDelaysMs[t]);
      for (int t = 0; t < kNumHallTaps; ++t)    maxEarlyMs = std::max(maxEarlyMs, kHallTapDelaysMs[t]);
      const size_t minSize =
          static_cast<size_t>(sampleRate_ * (maxEarlyMs * 0.001) + 8192.0) + 8;
      size_t n = 1;
      while (n < minSize) n <<= 1;
      for (int c = 0; c < kMaxChannels; ++c) inHist_[c].assign(n, 0.0f);
      inHistMask_ = static_cast<uint32_t>(n - 1);
      inHistWrite_ = 0;
    }
    // Shared-Mod chorus rings (R2): one power-of-two ring per channel, long
    // enough for the deepest voice (kSharedModDepthMs) + a block of headroom
    // so the fractional read never aliases this block's writes; cleared in
    // reset(). Four delay voices share each channel's ring (distinct taps).
    {
      size_t minSize = static_cast<size_t>(sampleRate_ * kSharedModDepthMs * 0.001 + 4096.0) + 8;
      size_t cn = 1;
      while (cn < minSize) cn <<= 1;
      for (int c = 0; c < kMaxChannels; ++c) chRing_[c].assign(cn, 0.0f);
      chMask_ = static_cast<uint32_t>(cn - 1);
      for (int c = 0; c < kMaxChannels; ++c) chWrite_[c] = 0;
      chDelayMax_ = static_cast<uint32_t>(sampleRate_ * kSharedModDepthMs * 0.001);
    }
    // Width-Haas mid ring (R3): power of two, the 3.5 ms delay headroom + a
    // block of margin so the fractional-tail read never aliases this block's
    // writes; cleared in reset().
    {
      size_t minSize = static_cast<size_t>(sampleRate_ * kWidthHaasDelayMs * 0.001) + 4096;
      size_t wn = 1;
      while (wn < minSize) wn <<= 1;
      whRing_.assign(wn, 0.0f);
      whMask_ = static_cast<uint32_t>(wn - 1);
      whWrite_ = 0;
    }
    // T-DAT-001: PLT tank rings -- sized for the max loop delays at the
    // widest roomSize (rs = 0.25+1.5, size 1): dly max 5505*1.75 + head,
    // apm max 1343*1.75 + LFO 24, ap5 3931*1.75, apIn 561*1.75; pre per the
    // pre-ceiling (60 ms). Powers of two, cleared by the fill below.
    {
      // FIXED SIZES (the delay bases are SAMPLE counts at 44.1 k class, not ms):
      // dly max 6590*1.75 = 11532 -> 16384; ap5 3931*1.75 = 6879 -> 8192;
      // apm 1343*1.75+24 = 2352 -> 4096; apIn 561*1.75 = 982 -> 1024;
      // pre = 60 ms ceiling per the pre law (ms-based is right THERE).
      const size_t kDly  = 16384;
      const size_t kApm  = 4096;
      const size_t kAp5  = 8192;
      const size_t kApIn = 1024;
      const size_t kPre  = static_cast<size_t>(kMaxPreMs * 1e-3 * sampleRate_) + 64;
      for (auto& ch : dat_) {
        for (int i = 0; i < 4; ++i) { ch.apIn[i].init(kApIn); ch.dly[i].init(kDly); }
        for (int i = 0; i < 2;  ++i) { ch.apm[i].init(kApm); }
        ch.ap5.init(kAp5); ch.ap6.init(kAp5); ch.pre.init(kPre);
      }
    }
    setParams(params_);  // recompute taps now that the rate is known
  }

  void reset() {
    for (auto& ch : lines_)
      for (auto& L : ch) L.clear();
    modPhase_ = 0.0;  // restart the Mod LFO
    whWrite_ = 0;  // R3: width-Haas mid history ring
    for (size_t i = 0; i < whRing_.size(); ++i) whRing_[i] = 0.0f;
    for (int c = 0; c < kMaxChannels; ++c) {
      boingRe_[c] = 0.0f; boingIm_[c] = 0.0f; dripEnv_[c] = 0.0f; splashLp_[c] = 0.0f;
      onsetEnv_[c] = 0.0f; whipLp_[c] = 0.0f; plateOnsetLp_[c] = 0.0f;
      for (int t = 0; t < kNumRoomTaps; ++t) roomTapsLp_[c][t] = 0.0f;
      for (int t = 0; t < kNumChamberTaps; ++t) chamberTapsLp_[c][t] = 0.0f;
      chamberBassLp_[c] = 0.0f;
      chamberHFCap_[c] = 0.0f;
      for (int t = 0; t < kNumHallTaps; ++t) hallTapsLp_[c][t] = 0.0f;
      digApX_[c] = 0.0f; digApY_[c] = 0.0f;
      smearPhase_[c] = 0.0f;
      fbDriftPhase_[c] = 0.5f*M_PI_F;   // quarter-cycle offset from smearPhase_
      r1Re_[c] = 0.0f; r1Im_[c] = 0.0f; r2Re_[c] = 0.0f; r2Im_[c] = 0.0f; r3Re_[c] = 0.0f; r3Im_[c] = 0.0f;
      r4Re_[c]=0.0f; r4Im_[c]=0.0f; r5Re_[c]=0.0f; r5Im_[c]=0.0f;
      r6Re_[c]=0.0f; r6Im_[c]=0.0f; r7Re_[c]=0.0f; r7Im_[c]=0.0f;
      r8Re_[c]=0.0f; r8Im_[c]=0.0f;
      washLp_[c] = 0.0f;
      washLp2_[c] = 0.0f;
      for (int ln = 0; ln < kNumLines; ++ln) { bodyW1_[c][ln] = 0.0f; bodyW2_[c][ln] = 0.0f; }
      airL1_[c] = 0.0f; airL2_[c] = 0.0f;
      hpfX_[c] = 0.0f; hpfY_[c] = 0.0f;
      for (int j = 0; j < 4; ++j) peakEq_[c][j] = 0.0f;
    }
    for (int c = 0; c < kMaxChannels; ++c)
      std::fill(inHist_[c].begin(), inHist_[c].end(), 0.0f);
    inHistWrite_ = 0;
    for (int c = 0; c < kMaxChannels; ++c)
      std::fill(chRing_[c].begin(), chRing_[c].end(), 0.0f);
    for (int c = 0; c < kMaxChannels; ++c)
      chWrite_[c] = 0;
    for (int c = 0; c < kMaxChannels; ++c) {
      smPh_[c] = 0.0;
      for (int v = 1; v < kModVoices; ++v) {
        smBqX1_[c][v] = 0.0; smBqX2_[c][v] = 0.0; smBqY1_[c][v] = 0.0; smBqY2_[c][v] = 0.0;
      }
    }
    for (auto& ch : dat_) {                      // T-DAT-001: PLT tank
      for (int i = 0; i < 4; ++i) { ch.apIn[i].clear(); ch.dly[i].clear(); }
      for (int i = 0; i < 2; ++i) ch.apm[i].clear();
      ch.ap5.clear(); ch.ap6.clear(); ch.pre.clear();
      ch.inLp = ch.dmpA = ch.dmpB = ch.sumB = 0.0f;
      ch.airLp = ch.loX = ch.loY = 0.0f;
    }
  }

  void setParams(const Params& p) {
    params_.decayMs = juce::jlimit(kMinDecayMs, kMaxDecayMs, p.decayMs);
    params_.preMs = juce::jlimit(kMinPreMs, kMaxPreMs, p.preMs);
    params_.tone = juce::jlimit(kMinTone, kMaxTone, p.tone);
    params_.size = juce::jlimit(kMinSize, kMaxSize, p.size);
    params_.width = juce::jlimit(kMinWidth, kMaxWidth, p.width);
    params_.mode = juce::jlimit(0, kNumModes - 1, p.mode);
    // Per-mode cap: the decay knob is clamped to the active mode's stability /
    // sound boundary (the user's ears-pass onset). E.g. Spring 2750, Plate 2500,
    // Hall 3500. The global 5000 ms ceiling still applies as the outer clamp.
    // Cap the decay by the ACTIVE CHARACTER's per-mode max (mode 5 with a
    // nonzero type routes to that character's law branch -> that character's
    // own decay cap applies).
    {
      const int chCap = (params_.mode == 5) ? typeLawMode(5, params_.type[5]) : params_.mode;
      if (params_.decayMs > maxDecayForMode(chCap))
        params_.decayMs = maxDecayForMode(chCap);
    }
    params_.density = juce::jlimit(0.0, 1.0, p.density);
    params_.mod = juce::jlimit(0.0, 1.0, p.mod);
    params_.springs = juce::jlimit(0.0, 1.0, p.springs);
    params_.sag = juce::jlimit(0.0, 1.0, p.sag);
    params_.bright = juce::jlimit(0.0, 1.0, p.bright);
    params_.bloom = juce::jlimit(0.0, 1.0, p.bloom);
    params_.early = juce::jlimit(0.0, 1.0, p.early);
    params_.air = juce::jlimit(0.0, 1.0, p.air);
    params_.volley = juce::jlimit(0.0, 1.0, p.volley);
    params_.bass = juce::jlimit(0.0, 1.0, p.bass);
    params_.build = juce::jlimit(0.0, 1.0, p.build);
    params_.space = juce::jlimit(0.0, 1.0, p.space);
    params_.sharedMod = juce::jlimit(0.0, 1.0, p.sharedMod);
    params_.wave = static_cast<int>(juce::jlimit(0, kNumWaves - 1, p.wave));
    params_.loCut = juce::jlimit(0.0, 1.0, p.loCut);
    const double t60s = std::max(0.05, params_.decayMs * 0.001);

    // ------------------------------------------------------------ PLT tank
    // Tank constants (modified Dattorro plate topology, 2026-10-16):
    //   input AP  210 158 561 410      gains 1.0 1.0 0.83 0.83
    //   loop AP   apm1 1343 (0.93)  ap5 3931 (0.93)  apm2 995 (0.67)  ap6 2664 (0.67)
    //   fig-8 dly 6241 6590 4641 5505         (x roomSize = 0.25 + 1.5*size)
    //   taps      d1 +394 +4401/-3124  ap5 -2831/-496  d2 +2954/-179
    //             d3 -2945/+522 +5368  ap6 -277/-1817  d4 -1578/+3956  (x0.6)
    // Ours: T60->fb, Pre, Early (2/5/9 ms taps), Tone in-loop damping
    // (800 Hz..12.8 kHz), LoCut one-pole HPF, Air shelf (~4.5 kHz, 0.9*air),
    // Space-scaled diffusion gains. The Mod knob is a post-wet chorus layer
    // (processSharedModSmear) driven by reverbSharedMod -- NOT an in-loop LFO.
    {
      const double rs    = 0.25 + 1.5 * params_.size;
      const double sr    = sampleRate_;
      // Space is OFF THE FACE (user 2026-10-17: "not really useful ... fix it
      // to its default") — pinned at the shipped default 0.7's diffusion scale
      // (0.1 + 0.9*0.7 = 0.73) for every mode-0 dial. params_.space stays in
      // the param/state path (presets round-trip) but no longer moves the mix.
      const float space = 0.73f;
      const int dIn_[4]  = { 210, 158, 561, 410 };
      const int loop_[4] = { 1343, 995, 3931, 2664 };
      const int dly_[4]  = { 6241, 6590, 4641, 5505 };
      const int tap_[14] = { 394, 4401, 3124, 2831, 496, 2954, 179,
                             2945, 522, 5368, 277, 1817, 1578, 3956 };
      const int earlyMs_[3] = { 2, 5, 9 };
      // INPUT allpasses: FIXED sub-unity (stability law, 2026-10-16). The
      // a g = 1.0 (apIn0/apIn1) parks a z=-1 leaky-integrator pole that
      // a long drive burst pumps to runaway at small sizes; cap at 0.98. They
      // do NOT set the T60 -- they sit outside the feedback cycle.
      const float gIn01 = 0.98f, gInLp = 0.90f, gInHp = 0.83f;
      for (int i = 0; i < 4; ++i) datApInLen_[i] = (int)(dIn_[i] * rs) + 1;
      datDiffIn_[0] = gIn01 * space;
      datDiffIn_[1] = gIn01 * space;
      datDiffIn_[2] = gInHp * space;
      datDiffIn_[3] = gInLp * space;   // input chain: diffusion gains (feed-forward, not in loop)
      datApLen_[0]  = (int)(loop_[0] * rs) + 1;
      datDiffDec_[0] = 0.92f * space;   // apm1 (loop) — 0.60 control scale (2026-10-17)
      datApLen_[1]  = (int)(loop_[1] * rs) + 1;
      datDiffDec_[1] = 0.67f * space;   // apm2 (loop)
      datDecLen_[0] = (int)(loop_[2] * rs) + 1;
      datDiffDec_[2] = 0.92f * space;   // ap5 (loop)
      datDecLen_[1] = (int)(loop_[3] * rs) + 1;
      datDiffDec_[3] = 0.67f * space;   // ap6 (loop)
      for (int i = 0; i < 4; ++i) datDlyLen_[i] = (int)(dly_[i] * rs) + 1;
      // One full A-B-A fig-8 traversal (delay bases sum:
      // 1343+995+3931+2664+6241+6590+4641+5505 = 28955) at the current size.
      // ASSIGNED BEFORE datDec_ (ordering bug 2026-10-16).
      datLoopTot_ = (int)(28955.0 * rs) + 4;
      // DAT fig-8 T60 law (OURS, final 2026-10-17, echo-blind T60 measurements).
      // datDec_ is the fig-8 decay multiplier applied at every loop read site
      // (A-entry sumB carry, d0/d1/d2 damping pops, B-entry carry, d3 read).
      // It MUST be < 1; the fig-8 loop's intrinsic gain (passive
      // passbands on apm1/apm2/ap5/ap6 plus node-tap leaks) pushes the loop
      // toward its instability boundary, and dDec is the knob that keeps it a
      // distance away. The closer it comes to the boundary, the longer the
      // tail (loop's slowest pole is near-unity), so LONGER T60 = LARGER dDec.
      //
      // Measured boundary (per-second energy-GROWTH slope, 6..16 s tail, the
      // FULL guard stack — bright/bloom ON, pre default — with CLEANED input
      // buffers; earlier "runaway at size>=0.25" results were heap garbage
      // streaming in from an unzeroed AudioBuffer, NOT the loop):
      //   size 0.0: decaying through d=0.92 (boundary off-grid, >=0.92)
      //   size 0.5: decaying at d<=0.84, GROWING +0.6/s at d=0.88
      //   size 1.0: decaying at d<=0.84, GROWING +0.3/s at d=0.88
      // => safe universal ceiling d=0.82 (boundary 0.84 minus margin). The
      // Decay knob maps LOG-uniformly from the floor (0.15) at decayMs=50 to
      // 0.82 at decayMs=2500; longer tail = loop's near-unity pole, still
      // monotone in decayMs at every size.
      const double hi = 0.82;
      {
        const double decayMs = std::max(50.0, std::min(2500.0, params_.decayMs));
        const double tnorm   = std::max(0.0, std::min(1.0,
            std::log10(decayMs / 50.0) / std::log10(2500.0 / 50.0)));
        datDec_ = (float)(0.15 + (hi - 0.15) * tnorm);
      }
      for (int i = 0; i < 14; ++i) datTap_[i] = (int)(tap_[i] * rs) + 1;
      for (int i = 0; i < 3; ++i) datPreTap_[i] = (int)(earlyMs_[i] * 1e-3 * sr) + 1;
      datPre_   = (float)(params_.preMs * 1e-3 * sr);
      // T-DAT-001 (2026-10-17, user: "tone seems to be wired backwards, it
      // gets brighter as you decrease it") -- DIRECTION FIX: the original law
      // scaled the ONE-POLE DAMPING with tone (high tone -> high fc -> MORE
      // lowpass -> darker), opposite to the knob intuition of every face we
      // have (Tone up = brighter). Inverted: the damping amount fades to ZERO
      // at tone = 1 (bright, undamped) and is strongest at tone = 0 (dark),
      // sweeping the cutoff from 12.8 kHz down to 800 Hz as tone is lowered.
      // c(tone) = (1-tone) * (1 - exp(-w * 800 * 16^(1-tone)))  (tone 0:
      // full 12.8 kHz lowpass, tone 1: exactly no damping -> bit-equal to no
      // damping path).
      {
        const double tinv = 1.0 - (double)params_.tone;
        datDmpA_ = (float)(tinv * (1.0 - std::exp(-6.283185307179586 * (800.0 * std::pow(16.0, tinv)) / sr)));
      }
      datAirA_  = (float)(1.0 - std::exp(-6.283185307179586 * 4500.0 / sr));
      datInA_   = (float)(1.0 - std::exp(-6.283185307179586 * 13500.0 / sr));
      datLoA_   = (float)(1.0 - std::exp(-6.283185307179586 * 45.0 / sr));
    }
    if (sampleRate_ > 0.0) {
      // Size scales every line (0 = half base), 1 = large (1.5x base); Pre
      // offsets every tap.
      const double sizeScale = 0.5 + 1.0 * params_.size;
    // Per-mode type selectors (sub-model within a mode). Clamped per mode
    // by numTypes -- the engine law: state/UI may name any value, the
    // engine never exceeds the mode's type count.
    for (int i = 0; i < kNumModes; ++i)
      params_.type[i] = static_cast<int>(juce::jlimit(0, numTypes(i) - 1, p.type[i]));

    // NOTE: Digital (mode 5) law routing is the lawCh gate in process() --
    // a subtype arms ONLY its character's sig pair (stored values, driven by
    // the sig knobs), so the character stays fully dialable after the
    // dropdown lands its starting point. No sig coercion happens here.

      const double preSamples = params_.preMs * 0.001 * sampleRate_;
      for (int i = 0; i < kNumLines; ++i)
        taps_[i] = kBaseMs[i] * 0.001 * sampleRate_ * sizeScale + preSamples;
      // Room early-field taps (Gardner: a larger room spaces its early
      // reflections further apart). At size 0 the base spacing; it widens gently
      // with Size (bounded). Independent of pre.
      const double roomEarlyScale = 1.0 + 0.25 * params_.size;
      for (int t = 0; t < kNumRoomTaps; ++t)
        roomTapsSamples_[t] =
            std::max(1.0, kRoomTapDelaysMs[t] * 0.001 * sampleRate_ * roomEarlyScale);
      // Chamber early-volley taps (fixed ms, independent of size/pre): compute
      // the per-tap sample offsets now (they depend on the live sample rate).
      for (int t = 0; t < kNumChamberTaps; ++t)
        chamberTapsSamples_[t] = std::max(1.0, kChamberTapDelaysMs[t] * 0.001 * sampleRate_);
      // Hall early build-up taps (fixed ms, independent of size/pre): the 10
      // long taps (10–150 ms), computed from the live sample rate now.
      for (int t = 0; t < kNumHallTaps; ++t)
        hallTapsSamples_[t] = std::max(1.0, kHallTapDelaysMs[t] * 0.001 * sampleRate_);
      // Plate onset pings (P-ons, fixed ms -- the membrane taps, see constants).
      for (int t = 0; t < kPlateOnsetTaps; ++t)
        plateOnsetSamples_[t] = std::max(1.0, kPlateOnsetDelayMs[t] * 0.001 * sampleRate_);
      // P-body: the peaking coefficients (biquad, peaking @kPlateBodyHz),
      // fb-capped so the round-trip gain never crosses 1.0 at any decay dial.
      if (kPlateBodyDb > 0.0 && sampleRate_ > 0.0) {
        const double A = std::min(std::pow(10.0, kPlateBodyDb / 40.0), 0.990 / plateFb(params_.decayMs, plateCeilTest_, plateLiftTest_));
        const double w0 = 2.0 * M_PI * kPlateBodyHz / sampleRate_;
        // Exact peaking (zeros at +/-1 -> unity at DC/Nyquist): B(z) = C(1-z^-2)/d(z),
        // poles at r e(+/−j w0), C chosen so B(w0) = 1 exactly, and
        // H(z) = 1 + (A-1)B(z) -> H(w0) = A exactly (the +kPlateBodyDb bump).
        const double r = 1.0 - (w0 / (2.0 * kPlateBodyQ));
        const double d0 = (1.0 - r) * (1.0 - r) + 2.0 * r * (1.0 - std::cos(w0));
        const double C = d0 / (2.0 * std::sin(w0));
        const double dA = (A - 1.0) * C;
        bodyB0_ = 1.0 + dA;        bodyB1_ = -2.0 * r * std::cos(w0);
        bodyB2_ = r * r - dA;      bodyA1_ = bodyB1_;        bodyA2_ = r * r;
      } else {
        bodyB0_ = 1.0; bodyB1_ = 0.0; bodyB2_ = 0.0; bodyA1_ = 0.0; bodyA2_ = 0.0;
      }
      // PLATE TEXTURE OUT-STAGE (first-order stages -- see the constants'
      // NOTE): body = a band-pass ADD centred 120-300 Hz (zero DC, zero HF:
      // exact by construction); air = a decay-tracked first-order LPF cascade
      // (each stage -6 dB/oct, corner = the bright/dark law above).
      if (sampleRate_ > 0.0) {
        auto al = [this](double f) { return 1.0 - std::exp(-2.0 * M_PI * f / sampleRate_); };
        const double frac = decayDiffFrac(params_.decayMs);
        // Size law: a corner SHIFT on the air-law (not a series stage). The
        // anchor: at size 0.5 sizeDelta = 0.0 -> airLp_ is bit-identical to
        // the pre-size-law value (the pinned 140 tuning).
        // Direction (MEASURED, 2026-10-13): size 1 (large-class) = BRIGHTER,
        // size 0 (compact) = DARKER -- a larger plate's denser high-mode
        // spectrum survives the air-law tilt and reads brighter.
        const double sizeDelta = (0.5 - params_.size) * 2.0 * kPlateSizeCornerFrac;
        const double airFrac = juce::jlimit(0.0, 1.0, frac - sizeDelta);
        const double fc = kPlateAirHzBright - (kPlateAirHzBright - kPlateAirHzDark) * airFrac;
        airLp_ = al(fc);
      } else {
        airLp_ = 0.0;
      }
    }
    // Digital Mod (mode 5 only): a sined waver on the read tap. Gated off at
    // Mod 0 so the plain integer read stays bit-exact (the anchor). Only the
    // depth scales with the knob (the delay's sigMod convention); the rate is
    // the Digital law. Not Digital -> off, so no other mode ever touches it.
    // modOn_: DIG only (mode 5, type 0 -> DIG-own law); other modes' types
    // never arm the Digital Mod (those types own their own sig pair).
    {
      const int chMod = (params_.mode == 5) ? typeLawMode(5, params_.type[5]) : params_.mode;
      modOn_ = (chMod == 5) && (params_.mod > 0.0) && (sampleRate_ > 0.0);
    }
    if (modOn_) {
      modDepthSamples_ = static_cast<float>(
          kDigitalModWaverMs * 0.001 * sampleRate_ * params_.mod);
      modInc_ = 2.0 * M_PI * kDigitalModHz / sampleRate_;
    } else {
      modDepthSamples_ = 0.0f;
      modInc_ = 0.0;
    }
    // Shared Mod (R2, all modes): a NEW, slow post-mix LFO on the wet sum
    // (one-pole lowpass whose cutoff breathes with the LFO). Gated OFF at 0
    // -> the whole processSharedModSmear path is never entered, so every mode
    // stays bit-identical to today at the default (the R2 anchor). Strictly
    // separate from the Digital read-tap waver (modOn_) and from the always-
    // on kSmearAmt/kFbDriftAmt signature laws (those are the mode's own
    // character and do not move with this knob).
    sharedOn_ = (params_.sharedMod > 0.0) && (sampleRate_ > 0.0);
    if (sharedOn_) {
      smInc_ = 2.0 * M_PI * kSharedModRate * smTestRateScale_ / sampleRate_;
      // Biquad low-pass (RBJ, 12 dB/oct), one per delayed voice: fc =
      // kModVoiceLpfHz, Q 0.707 (Butterworth). A musical, uniform darkening
      // of each line's thin comb content (replaces the earlier one-poles).
      const double lpfHz = smTestLpfHz_ > 0.0 ? smTestLpfHz_ : kModVoiceLpfHz;
      const double w0 = 2.0 * M_PI * lpfHz / sampleRate_;
      const double cw = std::cos(w0);
      const double al = std::tan(w0 / 2.0) / 0.70710678;  // Q = 0.707
      const double a0 = 1.0 + al + cw;
      smB0_ = ((1.0 - cw) / 2.0) / a0; smB1_ = (1.0 - cw) / a0; smB2_ = smB0_;
      smA1_ = (-2.0 * cw) / a0;        smA2_ = (1.0 - al + cw) / a0;
    } else {
      smInc_ = 0.0;
      smB0_ = smB1_ = smB2_ = smA1_ = smA2_ = 0.0;
    }
    // Width (R3, 2026-10-15): the Haas ring depth is a fixed 3.5 ms of the
    // mid wet (recomputed with the sample rate; sampleRate_ is set before
    // this setParams call in prepare()). Gate: 2ch && width > 0 -- at width 0
    // or on a 1ch buffer applyWidthHaas is a bit-exact no-op (zero reads,
    // zero writes) -- so the width-0 anchors stay exact by construction.
    widthHaasOn_ = (params_.width > 0.0) && (sampleRate_ > 0.0);
    widthHaasDelay_ = static_cast<size_t>(
        std::max(1, static_cast<int>(kWidthHaasDelayMs * 0.001 * sampleRate_)));  // 168 @48k
  }

  // Width (R3, 2026-10-15) -- the convolver's own Haas law, shared by every
  // mode. Post-mix on the wet, 2-channel buffers ONLY (a 1ch buffer -- the
  // production per-lane path -- is untouched, matching the convolver's 1ch
  // gain-only width behavior). The mid history m[n]=(L+R)/2 is ringed and the
  // pair law (ConvolutionReverb, kHaasDelayMs 3.5 ms / kHaasDepth 0.55):
  //   side = width * 0.55 * m[n - 3.5ms] * 0.5   (half-folded so the pair sums
  //   exactly cancel: outL + outR = L + R - the sum-preserving law)
  //   L' = L + width*side,  R' = R - width*side
  // Bit-exact no-op when width == 0 or on 1ch (never entered).
  static constexpr double kWidthHaasDelayMs = 3.5;   // mirrors ConvolutionReverb.h kHaasDelayMs
  static constexpr double kWidthHaasDepth = 0.55;    // mirrors ConvolutionReverb.h kHaasDepth
  // R3, 1ch buffers (production per-lane) AND 2ch buffers: the tap scale is
  // the SAME as pre-R3 (ch-0 shorter by 0.05*width, ch-1 longer) so the
  // comb taps are unchanged. The 2ch buffer additionally gets the convolver
  // post-mix Haas M/S stage (applyWidthHaas) which is the ticket's "match
  // the convolver" ask; the tap scaling remains as the spectral decorrelation
  // baseline (it was bit-exact to ship, keep it). 1ch has no Haas stage.
  double widthTapScaleFor(int ch, int numChannels) const {
    const double widthSpread = kMaxWidthSpread * params_.width;
    return (ch == 0) ? (1.0 - widthSpread) : (1.0 + widthSpread);
  }
  void applyWidthHaas(juce::AudioBuffer<float>& buffer) {
    if (!widthHaasOn_ || buffer.getNumChannels() < 2) return;
    const int n = buffer.getNumSamples();
    const float w = static_cast<float>(params_.width);
    const float depth = static_cast<float>(kWidthHaasDepth);
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    float* wr = whRing_.data();
    uint32_t wpos = whWrite_;
    const uint32_t msk = whMask_;
    const uint32_t d = static_cast<uint32_t>(widthHaasDelay_);
    for (int i = 0; i < n; ++i) {
      const float mid = 0.5f*(L[i] + R[i]);
      const uint32_t rd = (wpos + 1u + msk - d) & msk;  // exactly d samples behind the mid just written
      const float half = 0.5f * w * depth * wr[rd] * 0.5f;
      L[i] += 2.0f * half;
      R[i] -= 2.0f * half;
      wr[wpos] = mid;
      wpos = (wpos + 1u) & msk;
    }
    whWrite_ = wpos;
  }

  int latencySamples() const {
    const double sr = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    return static_cast<int>(params_.preMs * 0.001 * sr);
  }

  const Params& params() const { return params_; }

  void process(juce::AudioBuffer<float>& buffer) {
    if (lines_.empty() || sampleRate_ <= 0.0) return;
    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;
    // ACTIVE CHARACTER: mode 5 + nonzero type -> that character's law branch;
    // other modes -> identity (type is always 0 for them today). This single
    // value drives every per-mode law arm below, so a DIG/SPR/PLT/SML/CHM/HAL
    // subtype routes through the correct processXxx + sig arm without touching
    // the other modes' dispatch.
    const int lawCh = (params_.mode == 5) ? typeLawMode(5, params_.type[5]) : params_.mode;

    // Feedback (Decay): see decayFb() -- [50,3000] bit-identical to before, the
    // 3000..5000 top segment nudges the tail just a touch longer (still |fb| < 1).
    const double fb = decayFb(params_.decayMs);
    // Normalise by a FIXED ref (not the live (1-fb), which collapses at long decay).
    // See the Wash LEVEL reference block above for why.
    const double norm = kDigitalWashRef / kNumLines;
    // Tone: one-pole low-pass on the feedback path (tone 0 = no filter, bright;
    // tone 1 = fully dark), matching the delay's damping convention.
    const double dampAlpha = 1.0 - params_.tone;
    // Width: the shared post-mix Haas stage (see widthHaasOn_ and
    // applyWidthHaas below) -- see the Width design note in the constants
    // block. The old per-bank tap-scale spread is gone (R3, 2026-10-15).

    // Digital laws are live in mode 5 only. Density (feedback coupling toward
    // the per-lines mean) and Mod (sined read-tap waver) both reduce to the
    // identity at 0, so when NEITHER is on the plain comb-bank path below is
    // byte-identical to the current engine (and every other mode uses it too
    // -> the law-inert anchor).
    const bool densityOn = (lawCh == 5) && (params_.density > 0.0);
    // Spring (mode 1) is law-live only when armed (springs>0 or sag>0); at both
    // 0 it runs the shared plain path (the anchor + the law-inert scaffold pin
    // stay intact). Plate/Room/Chamber/Hall are still law-inert (their sigs do
    // nothing yet) -> they too fall through to the plain path.
    const bool springOn = (lawCh == 1) &&
        (params_.springs > 0.0 || params_.sag > 0.0);
    if (springOn) {
      processSpring(buffer);
      applyWidthHaas(buffer);
      if (sharedOn_) processSharedModSmear(buffer);
      return;
    }
    // T-DAT-001: mode 0 (PLT) always runs the DAT tank (default + only
    // engine; no Bright/Bloom arm). The legacy comb-plate voice moves to the
    // Digital PLT entry (mode 5, type[5] == 2, the Digital subtype order
    // DIG SPR PLT SML CHM HAL) and keeps the Bright/Bloom arm there.
    if (params_.mode == 0) {
      processDat(buffer);
      // DAT-specific width (R3, M/S side-gain law, 2026-10-17): the tank
      // already produces a per-side stereo image (two tanks with different
      // node-tap splits). The width knob modulates how much of that image is
      // passed: 0 = mono (L=R=mid), 1 = full image. The legacy applyWidthHaas
      // (shared post-mix Haas) is a DIFFERENT mechanic (adds a 3.5 ms echo);
      // we don't apply both here -- width in the DAT arm is the M/S side-gain.
      // (Legacy plate arm at mode 5/type 2 STILL uses applyWidthHaas; it is
      // bit-exact there by design and not reachable from the PLT face.)
      if (buffer.getNumChannels() >= 2) {
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        const int ns = buffer.getNumSamples();
        const float w = params_.width;  // w=0 -> mono (L=R=mid), w=1 -> full per-side image
        for (int i = 0; i < ns; ++i) {
          const float mid  = 0.5f * (L[i] + R[i]);
          const float side = 0.5f * (L[i] - R[i]) * w;
          L[i] = mid + side;
          R[i] = mid - side;
        }
      }
      if (sharedOn_) processSharedModSmear(buffer);
      return;
    }
    const bool plateOn = (lawCh == 0) && (params_.mode == 5) &&
        (params_.type[5] == 2) &&
        (params_.bright > 0.0 || params_.bloom > 0.0);
    if (plateOn) {
      processPlate(buffer);
      applyWidthHaas(buffer);
      if (sharedOn_) processSharedModSmear(buffer);
      return;
    }
    // Room (mode 3) is law-live when armed (Early>0 or Air>0); at both 0 it runs
    // the shared plain path (the Digital anchor + kNumModes==6 hold).
    const bool roomOn = (lawCh == 2) &&
        (params_.early > 0.0 || params_.air > 0.0);
    if (roomOn) {
      processRoom(buffer);
      applyWidthHaas(buffer);
      if (sharedOn_) processSharedModSmear(buffer);
      return;
    }
    // Chamber (mode 4) is law-live when armed (Volley>0 or Bass>0); at both 0 it
    // runs the shared plain path (the Digital anchor + kNumModes==6 hold).
    const bool chamberOn = (lawCh == 3) &&
        (params_.volley > 0.0 || params_.bass > 0.0);
    if (chamberOn) {
      processChamber(buffer);
      applyWidthHaas(buffer);
      if (sharedOn_) processSharedModSmear(buffer);
      return;
    }
    // Hall (mode 5) is law-live when armed (Build>0 or Space>0); at both 0 it
    // runs the shared plain path (the Digital anchor + kNumModes==6 hold).
    // Hall is the LONGEST RT by design (default 3000 ms, within its 50..5000
    // range); at both 0 it shares the plain path (the anchor holds at any decay).
    const bool hallOn = (lawCh == 4) &&
        (params_.build > 0.0 || params_.space > 0.0);
    if (hallOn) {
      processHall(buffer);
      applyWidthHaas(buffer);
      if (sharedOn_) processSharedModSmear(buffer);
      return;
    }
    if (!densityOn && !modOn_) {
      for (int ch = 0; ch < numChannels; ++ch) {
        auto& lines = lines_[static_cast<size_t>(ch)];
        const double tapScale = widthTapScaleFor(ch, numChannels);
        float* out = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
          const float dry = out[i];
          float acc = 0.0f;
          for (int ln = 0; ln < kNumLines; ++ln) {
            auto& L = lines[ln];
            const int d = static_cast<int>(std::max(1.0, taps_[ln] * tapScale)) & L.mask;
            const float delayed = L.ring[(L.write - d) & L.mask];  // e[n - d] tail
            L.lp += dampAlpha * (delayed - L.lp);                 // low-pass it
            L.ring[L.write] = dry + static_cast<float>(fb) * L.lp; // e[n]
            L.write = (L.write + 1) & L.mask;
            acc += delayed;  // wet: the tail (not the current input)
          }
          out[i] = static_cast<float>(acc * norm);
        }
      }
      applyWidthHaas(buffer);
      if (sharedOn_)
        processSharedModSmear(buffer);
      return;
    }

    // ---- Digital law path: Density (feedback coupling) and/or Mod (waver) ----
    const float depth = modDepthSamples_;
    const double inc = modInc_;
    const double phase0 = modPhase_;
    const double density = params_.density;
    const double oneMinusDensity = 1.0 - density;
    // Schroeder/Moorer all-pass coefficient (0 at Density 0 -> identity; the
    // bit-identity anchor holds). Phase-only, so the level we calibrated is kept.
    const float diffCoef = densityOn ? static_cast<float>(kDigitalDiffuse * density) : 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
      auto& lines = lines_[static_cast<size_t>(ch)];
      const double tapScale = widthTapScaleFor(ch, numChannels);
      const float side = (ch == 0) ? +1.0f : -1.0f;  // L + / R - (opposite phase)
      float* out = buffer.getWritePointer(ch);
      float delayed[kNumLines];
      float selfLp[kNumLines];
      for (int i = 0; i < numSamples; ++i) {
        const float dry = out[i];
        // Pass A: read each line's tail (wobbled read when Mod is live -> a
        // smooth interpolated tap; the plain integer read otherwise) and update
        // its low-pass state. Reads and writes are split so a line's write can
        // only affect later samples of ITS OWN ring (lines have separate rings,
        // so the order is exactly the plain path's at Density 0).
        const float wob = modOn_
            ? side * depth
              * static_cast<float>(waveValue(params_.wave, fmod(phase0 + inc * i, 2.0 * M_PI) / (2.0 * M_PI)))
            : 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const double base = std::max(1.0, taps_[ln] * tapScale);
          float tail;
          if (modOn_) {
            const double tapF = std::max(1.0, base + static_cast<double>(wob));
            const int d0 = static_cast<int>(tapF);  // floor(tapF)
            const float frac = static_cast<float>(tapF - static_cast<double>(d0));
            const uint32_t iNew = static_cast<uint32_t>(((L.write - d0) & L.mask));
            const uint32_t iOld = static_cast<uint32_t>(((L.write - d0 - 1) & L.mask));
            tail = L.ring[iNew] + frac * (L.ring[iOld] - L.ring[iNew]);
          } else {
            const int d = static_cast<int>(base) & L.mask;
            tail = L.ring[static_cast<uint32_t>(((L.write - d) & L.mask))];
          }
          L.lp += dampAlpha * (tail - L.lp);
          delayed[ln] = tail;
          selfLp[ln] = L.lp;
        }
        // Density: blend each line's feedback toward the mean of all lines
        // (identity at Density 0). A convex combination of the tails -> stays
        // within their range, so it cannot destabilise (|mix| <= max|tail|).
        float mean = 0.0f;
        if (densityOn) {
          for (int k = 0; k < kNumLines; ++k) mean += selfLp[k];
          mean /= kNumLines;
        }
        // Pass B: write the (possibly coupled) feedback and sum the wet tails.
        float acc = 0.0f;
        for (int ln = 0; ln < kNumLines; ++ln) {
          auto& L = lines[ln];
          const float mix = densityOn
              ? (oneMinusDensity * selfLp[ln] + density * mean)
              : selfLp[ln];
          L.ring[L.write] = dry + static_cast<float>(fb) * mix;
          L.write = (L.write + 1) & L.mask;
          acc += delayed[ln];
        }
        // Schroeder/Moorer all-pass diffusion on the summed comb wash (identity at
        // Density 0, so the anchor holds; a linear first-order APF of unity gain,
        // blended in by the density coefficient -> live, bounded, phase-only).
        const float xv = static_cast<float>(acc * norm);   // the comb output (AP input)
        float yv = xv;
        if (diffCoef != 0.0f) {
          const float a = 0.5f;  // a fixed moderate AP coefficient (|a|<1)
          const float yAp = a * xv + digApX_[ch] - a * digApY_[ch];  // y=a*x+x[n-1]-a*y[n-1]
          digApX_[ch] = xv;          // x[n-1]
          digApY_[ch] = yAp;         // y[n-1]
          yv = static_cast<float>((1.0f - diffCoef) * xv + diffCoef * yAp);
        }
        out[i] = yv;
      }
    }
    if (modOn_)
      modPhase_ = phase0 + inc * numSamples;  // advance the LFO once per block
    applyWidthHaas(buffer);
    if (sharedOn_)
      processSharedModSmear(buffer);  // R2: the shared, knob-gated post-mix swell
  }

  // Subtle level-driven soft-shoulder for the spring driver color: identity
  // (clean) below the knee, a mild bounded roll-off above (warms the peaks,
  // so it is level-dependent by construction). Our own law (knee + shoulder).
  // This is the clean-rooms port of the compressor's soft-knee law, applied
  // (gated off by default) so the Spring/Plate can get a faint driver color.
  // The user's "distortion on the front" is the onset splash (drip / whip), not
  // this shoulder -- the shoulder is identity below the knee and the knee is set
  // well above the tail body, so it only ever touches true peaks.
  static float springShoulder(float x) {
    // A TAD of clean compression: identity (clean) up to a HIGH knee (well above the
  // tail body, so the reverb never distorts), then a very gentle bounded roll on
  // real peaks. Clean level-softening, not a hard clip - so it adds presence,
  // not harmonic breakup (no "fizz").
    const float a = std::fabs(x), knee = 0.55f, k = 0.5f;
    if (a <= knee) return x;
    return std::copysign(knee + (a - knee) / (1.0f + (a - knee) / k), x);
  }


  // ---- Wash LEVEL reference (the "reverb dies as the decay knob tops out" fix) ----
  // The wash wet was levelled by a LIVE "(1.0 - fb)". That COLLAPSES as decay -> max:
  // with the tone low-pass in the loop the comb's real growth stops scaling like
  // 1/(1-fb), so (1-fb) over-normalises and a long reverb goes silent (measured:
  // Shared-Mod (R2, all modes) LFO-swell constants — see DESIGN DECISION in
  // docs/tickets/reverb-dry-mod-width-swing.md. This is a NEW, post-mix, knob-
  // gated mechanism (distinct from the always-on kSmearAmt/kFbDriftAmt used
  // inside the Plate/Spring signature paths, which stay bit-identical and
  // knob-independent). OFF (sharedMod 0, the default) -> the entire code path
  // is never entered, so the bit-exact anchor is preserved by construction.
  // Shared-Mod chorus constants (R2, 2026-10-15). Four VOICES per channel
  // (the classic multi-voice chorus pedal): undelayed + three delay lines with
  // distinct depth ratios / phase offsets / LFO-rate factors, so the
  // instantaneous comb spacing differs per voice and the beatings smear into
  // a liquid swirl instead of one moving comb (single-voice = the "crispy
  // static" artifact the user rejected). Each delay tap is low-passed
  // (one-pole, fixed 6 kHz) to kill the thin high-beating residue. Total mixed
  // level stays <= 1.0 in magnitude at full depth, so it never feeds a
  // downstream saturator (the lesson of the level cuts).
  static constexpr int kModVoices = 7;  // 6 delay lines (v=1..6) + the undelayed voice -- TENTH CUT: more incoherent-averaged voice lines = lower peak sharpness at the same layer level (measured)
  static constexpr double kSharedModRate = 0.5;    // Hz, base LFO rate (a full warble per 2 s)
  static constexpr double kSharedModDepthMs = 18.0; // max depth of the DEEPEST voice (at sharedMod 1)
  // MEASURED (R2_ModClickSharpnessSweep, 2026-10-15, ninth cut): the
  // residual "clickly transients" are the thin comb residue of the moving
  // delayed voices -- darkening is the lever (the tap floor sweep was flat,
  // so the floor 0.30 stays). 6k -> 4 kHz cuts the layer sharpness -42 % at
  // only -5.9 dB of layer body; lower still buys little more per dB
  // (3 kHz: -61 % sharp at -10 dB body = hollow). 4 kHz = the best
  // trade; amt 1.8 keeps the net body close to the 8th-cut ship.
  static constexpr double kModVoiceLpfHz = 6000.0;
  // per-line WRITE-side one-pole time constant (ms) -- ELEVENTH CUT (2026-10-15,
  // the click obliteration): R2_ModChordClickHunt (user methodology: loud
  // sustained chords at max Mod, 5 amps) = click index 0.132 @ body x0.98
  // vs the 10th-cut reference 0.217, at EVERY input amp (0.25/0.5/0.75/1.0);
  // tau 2.0 = 0.218 (nearly no change) -- the delayed tap structurally cannot
  // re-emit the wet's transients once the ring contents are smoothed.
  static constexpr double kModWriteTauMs = 80.0;   // THIRTEENTH CUT (2026-10-15): tau ladder measured to the floor (12/40/80 ms); user A/B picked 80
  // Chorus LAYER gain: the delayed voices ADD ON TOP of the full (unity)
  // undelayed wet -- this is the "lush modulation ADDED to the reverb"
  // (2026-10-15) and is what kills BOTH the level drop and the clicking.
  // The earlier 50/50 crossfade (0.5*wet + 0.5*chorus) is the root of both
  // complaints: (a) the darkened/coherent delayed copy replaces half the
  // fresh wet on the dense early reflections -> the volume drop; (b) every
  // LFO pass where the taps anti-phase the early reflections -> a sharp
  // pumping dip that reads as a click. A layer on top of full-level wet has
  // neither effect. MEASURED (R2_ModSonicSweep, 2026-10-15): the chorus
  // layer sits ~15-16 dB under the wet reverb before it reads as clearly
  // "chorusy" to the ear -- the 0.30/0.45 cuts were -23..-30 dB (inaudible,
  // "really hard to tell it's doing anything"). 1.8 lands at ~-15.5 dB with
  // output +1.2 dB over the wet and sharpness ratio 1.03 (no added click
  // energy) -- chosen by measurement, not vibes.
  // MEASURED (R2_ModClickSharpnessSweep + R2_ModSonicProbe, ninth cut,
  // 2026-10-15): 4 kHz darkening cuts layer sharpness -42% but costs -5.9 dB
  // body, so compensate with amount. Probe sweep: amt 1.8 -> sharp x1.030,
  // amt 3.5 -> x1.075 (the 8th-cut ship). 2.8 = sharp x1.05 (~7% below the
  // 8th-cut transient energy = the "lessened clickly transients") at the
  // same ~-13 dB body the eighth cut measured.
  static constexpr double kModChorusAmt = 3.0;   // TENTH CUT: 1.8 x 1.63 (re-compensate for the added avg lines; measured body level)
  static constexpr double kModVoiceFloor = 0.30;   // taps sweep 30..100 % of the voice depth
                                                   // (gives the tap a real depth of motion
                                                   // while keeping it in the classic chorus 1..4 ms band)
  // Per-voice depth ratio (share of the max depth), LFO rate factor, and phase offset:
  // incommensurate rate factors + spread phase offsets -> the beatings smear
  // into a slow, exaggerated swirl (single voice: "crispy static"; the
  // crossfade sum of the 3-voice 1.2 Hz cut: "clicking + early-volume
  // drop -- both rejected 2026-10-15").
  // TENTH CUT (measured spike lesson, 2026-10-15): 8 incoherent averaged
  // delay voices in place of 4 -- the SUM of N decorrelated lines has
  // peaks ~1/sqrt(N) but RMS unchanged, so averaging is the one anti-click
  // lever that costs no body (darkening/rate/floor measured: no help).
  static constexpr double kModVoiceD[]  = {0.0, 1.00, 0.72, 0.50, 0.34, 0.61, 0.42};  // index 0 unused (v=1..6)
  static constexpr double kModVoiceR[]  = {1.00, 1.31, 0.77, 1.64, 1.93, 0.58};
  static constexpr double kModVoiceO[]  = {0.00, 0.62, 1.55, 2.55, 3.40, 4.18};   // rad, spread the peaks
  // Mixing law (the CHORD, not a crossfade): out = 0.5*wet + sum of the
  // delayed voices / (kModVoices-1). Undelayed 0.5 + 4 * 0.25 * (unity-DC
  // biquad) = 1.00 worst-coherent, and on real tails the lines are
  // decorrelated so the level STAYS at the reverb's level -- the modulation
  // is a lush layer ADDED ON TOP (the 2026-10-15 ask), never an amplitude
  // trade, so it can't kill the early reflections or feed a saturator.

  static constexpr int kNumWaves = 5;  // same 5-detent law as Chorus/Tremolo

  // The shared Mod is a MULTI-VOICE CHORUS on the wet reverb sum (EMT-140 /
  // spring-verb chorus-pedal class, the user's explicit 2026-10-15 ask): the
  // tail is re-voiced through kModVoices delay lines of differing depth / rate
  // / phase and mixed back level-preservingly (0.30*undelayed + 0.175*each
  // delayed*0.707 = 0.854 peak, never amplifying). The undelayed voice keeps
  // the fresh attack intact (a level-swell cut it -- rejected); the incommensurate
  // per-voice LFO factors smear the beatings into a swirling texture (single
  // voice gave "crispy static" -- rejected). Per-channel independent rings,
  // fractional-tap interpolation (no zipper), the delayed voice is low-passed
  // to kill the thin comb residue. Gated fully by `sharedOn_` (sharedMod>0);
  // `wave` still shapes only Digital's Sig-B read-tap waver. Pinned by
  // `ReverbTest.R2_SharedModChorusesTheTailNotTheAttack` (level law + periodic
  // RMS swing > 1.4x).

  // One LFO shape over [0,1) phase, in [-1,1]. wave 0 = Sine (bit-identical
  // to the pre-R2 fixed-shape behaviour, the anchor); waves 1..4 match
  // Chorus::shape() (Tremolo's own law) exactly — copy-paste that formula
  // verbatim so the same "Shape" face means the same thing on the reverb tile.
  static double waveValue(int wave, double phase) {
    switch (wave) {
      case 1:  // Triangle
        return (phase < 0.25) ? (phase * 4.0)
             : (phase < 0.75) ? (2.0 - phase * 4.0)
                              : (phase * 4.0 - 4.0);
      case 2:  // Saw-up
        return phase * 2.0 - 1.0;
      case 3:  // Saw-down
        return 1.0 - 2.0 * phase;
      case 4:  // Square
        return (phase < 0.5) ? -1.0 : 1.0;
      default:  // Sine (0) — the anchor, EXACT std::sin, not a re-derivation
        return std::sin(2.0 * M_PI * phase);
    }
  }

  // Digital 0.045 @1.5s -> 0.004 @3.0s; Plate 0.066 -> 0.007; Spring 0.038 -> 0.008).
  // Each mode now levels its wet from a FIXED reference (= that mode's (1-fb) at its
  // DEFAULT decay), so the default level the ears-pass set is PRESERVED and the level
  // stays flat across the whole 50..5000 knob instead of dying. The Hall already used
  // a fixed ref (kHallWashFbRef); these bring the other five in line.
  static constexpr double kDigitalWashRef = 0.431;  // (1-fb) at Digital's 1200 ms default
  static constexpr double kSpringWashRef  = 0.244;  // (1-fb) at Spring's  2000 ms default -- back to the ORIGINAL value: the +1 dB "dwell" was pushing the peaks higher and the user was hearing the DAW's outboard saturate on the transient (the "distortion on the front"). The user can get the "dwell" character by pulling the fader up 1 dB instead (same energy, no added peak).  // +1 dB dwell baked in (0.244 * 1.2589 = 0.307). The tail is +1 dB at every time point along its decay (the user's "bake +1 dB of dwell into the wet", normalizing the level with the DAW fader).
  static constexpr double kPlateWashRef   = 0.244;  // (1-fb) at Plate's   2000 ms default
  // P1 (percept: tail length, 2026-10-06): Plate gets its own curve on top of the
  // shared decayFb -- the SAME scope-lift pattern the Spring already uses
  // (decayFb(d)*1.04, spring-scoped only), so every other mode stays bit-identical.
  // WHY: burst-probe (M2/M4, vs the Nevo EMT 140 convolved reference, both
  // peak-normalised): at our 2000 ms default the shared curve rings ~40 % short
  // (our T30 0.89 s; the EMT 140 2.0s file measures T30 1.38 s). The +10.4 % fb
  // lift brings 2000 ms to T30 ~1.35 s / T60 ~2.7 s (the reference tail) and,
  // with the dial kept honest, gives the 2500 ms ceiling real reach (T60 ~5.6 s
  // instead of ~3.7 s -- the "does the cap go long enough" question).
  // STABILITY: hard ceiling 0.940; with the +/-3 % fb-drift the loop tops out at
  // 0.940 * 1.03 = 0.968 < 1, so the Plate cannot run away at any dial setting.
  static constexpr double kPlateDecayLift = 1.15;
  static constexpr double kPlateFbCeiling = 0.940;   // legacy name; plateFb() now caps at kPlateFbCeilingUp
  // P2.1 (reverb-budget-quality-uplift, 2026-10-16): the PLATE DENSITY UPLIFT
  // (family-target pass against the Bricasti M7 plate family; the EMT 140
  // family's DIRECTION anchor -- longer = darker -- stays the law). Sweep
  // numbers (probe, 2000 ms dial, click, peak-normalised; reference Fat Plate
  // 15 at 48 kHz: t1000 rel -55 dB / t2000 rel -70 dB):
  //   lift 1.104 (shipped): full=27.2  comb(0.3-0.8s)=35.8  t1000=-52.6  t2000=-66.5
  //   lift 1.13:            full=29.9  comb=35.2            t1000=-51.1  t2000=-63.3
  //   lift 1.15:            full=32.5  comb=34.8            t1000=-50.0  t2000=-60.9
  //   lift 1.20:            full=46.6  comb=33.4            t1000=-46.3  t2000=-52.8 (overshoots the family)
  // SHIP: lift 1.15 -- fullness +19 %, BOTH comb windows drop (de-comb as a
  // by-product: the denser wash beats out the mid-window eigentones), the
  // 2000 ms dial now lands at the reference's 1.0 s rel level (slightly FULLER
  // than Fat Plate 15 itself -- inside the family, per the user's
  // "same family, not a tight IR match" directive). NOT overshoot (1.20
  // measured as the overshoot boundary, DECLINED-with-numbers above).
  // CEILING 0.965 (was 0.940): drift-safe (0.965 * 1.03 = 0.994 < 1 under the
  // +/-3 % fb-drift), body-cap 0.990/fb = 1.0259 -> the kPlateBodyDb bump is
  // now ALWAYS fb-capped (was gain-limited at 1.104 lift: 0.990/0.94=1.053,
  // 10^(1.3/40)=1.076 -> capped to 1.053; now capped to 1.0259: the body
  // bump SHRINKS 1.3 -> ~1.01 dB at these dials -- re-pinned below, and it is
  // the RIGHT direction: a fuller wash needs less per-round-trip body boost).
  // Stability proven: |fb| <= 0.965*1.03 < 1 at every dial; ceiling binding
  // from ~2400 ms dial upward (the long-plate reach).
  // PLATE-SCOPED ONLY: plateFb() is the sole caller -> the other five modes
  // stay bit-identical (the contract).
  static constexpr double kPlateFbCeilingUp = 0.965;   // density uplift ceiling (drift-safe: 0.965*1.03 < 1)
  double plateCeilTest_ = 0.0;   // test hook: 0 = kPlateFbCeilingUp; else override
  double plateLiftTest_ = 0.0;   // test hook: 0 = kPlateDecayLift; else override
  static double plateFb(double decayMs, double ceilTest = 0.0, double liftTest = 0.0) {
    const double ceil = ceilTest > 0.0 ? ceilTest : kPlateFbCeilingUp;
    const double lift = liftTest > 0.0 ? liftTest : kPlateDecayLift;
    return juce::jmin(ceil, decayFb(decayMs) * lift);
  }
  // Room wash level ref. 0.595 (shipped 2026-10-16: the (1-fb) at Room's 500 ms
  // default) -> 1.20 (P2.3 density uplift, 2026-10-16, reverb-budget-quality-uplift
  // pass, ticket P2 step 3). The P1 hunt (test/src/room_probe_tmp.cpp, RoomProbe)
  // mapped the fullness deficit against Studio C (Bricasti M7) as a FLAT 6.7 dB
  // sustained-wash level floor (600-2400 ms: mode -10.0 vs ref -3.3 dB/peak) with
  // the early field 4.6 dB under; every other lever is the wrong axis (washAp
  // inert; fb lift RAISES the comb peaks -> shrinks sumAbs/peak; Early capped
  // ~1.0; decay dial moves the wash the wrong way at the fixed norm). wr=1.20
  // lands the early field (0-80 ms) AT the reference's -51.5 (-50.92 measured)
  // -- the family-matched pin (the 140-family m00381 law: the mode sits IN the
  // reference family, not at any single IR's number). The sat knee at 1.8-2.13
  // (full 34764, +3.7 % only) is DECLINED: it overshoots the family, the
  // concave curve is peak-dominated sumAbs, and every RoomTest law stays
  // direction-identical (washRef is a pure gain; the early taps / air LP /
  // boundedness laws are unaffected). CPU cost: zero (a constant multiply is
  // already in the loop -- the value changed, not the work).
  static constexpr double kRoomWashRef    = 1.20;
  double roomNormTest_ = 0.0;   // test hook: 0 = kRoomWashRef; else override (fullness law probe)
  double roomApTest_   = 0.0;   // test hook: 0 = kRoomWashAp; else override (diffusion probe)
  double roomLiftTest_ = 0.0;   // test hook: 0 = 1.0 (no lift); else decayFb lift factor (fullness probe)
  void setRoomLiftTest(double lift) { roomLiftTest_ = lift; }
  static constexpr double kChamberWashRef = 0.291;  // (1-fb) at Chamber's 1800 ms default
  double chamberBassFracTest_ = 0.0;  // test hook: 0 = kChamberBassFrac
  double chamberCapFracTest_ = 0.0;   // test hook: 0 = kChamberHFCapFrac
  double chamberApTest_     = 0.0;    // test hook: 0 = kChamberWashAp
  double chamberWashFracTest_ = 0.0; // test hook: 0 = kChamberWashFrac (P2.4 wash-level hunt)

  // Feedback-from-Decay (see kDecayTunedCeil): [50,3000] -> [0.30,0.99] EXACTLY as
  // before (bit-identical over the tuned region); the new top segment [3000,5000]
  // continues 0.99 -> 0.999 (a little longer, strictly |fb| < 1, no runaway).
  static double decayFb(double decayMs) {
    const double d = juce::jlimit(kMinDecayMs, kMaxDecayMs, decayMs);
    if (d <= kDecayTunedCeil)
      return 0.30 + 0.69 * (d - kMinDecayMs) / (kDecayTunedCeil - kMinDecayMs);
    return 0.99 + 0.009 * (d - kDecayTunedCeil) / (kMaxDecayMs - kDecayTunedCeil);
  }
  // Diffusion-blend frac (washApSc): [50,3000] -> [0.0,1.0] unchanged; held at 1.0
  // in the oversize top segment -- the wash is already fully diffused at long decay.
  static double decayDiffFrac(double decayMs) {
    const double d = juce::jlimit(kMinDecayMs, kMaxDecayMs, decayMs);
    return juce::jmin(1.0, (d - kMinDecayMs) / (kDecayTunedCeil - kMinDecayMs));
  }

  // 1st-order all-pass on the comb WASH (exact Digital-mode form: fixed a=0.5 APF
  // blended in by a diffusion coefficient). Phase-only (unity gain): it spreads the
  // wash so the comb's long-decay resonances -- standing waves / whistles / the
  // metallic "echo" after a second -- smear into a smoother decay. Amount =
  // base*(0.4+0.6*decayFrac): more diffusion at long decay, where the modes break
  // (past ~2000 ms). Identity at 0. Reuses the shared wash-diffuser state.
  float washApSc(float x, double base, int ch) {
    const float dfrac = static_cast<float>(decayDiffFrac(params_.decayMs));
    // LFO-smear: slowly wobble diffCoef by ±12 % so the all-pass's peak/valley
    // position drifts. This spreads the comb's periodic eigenfrequencies across
    // a small band and beats them out (the Spin Semi "spread the eigentones"
    // technique) instead of smoothing one fixed set of modes to the metallic
    // shimmer you hear on a sustained tone. Gated: base = 0 (Digital anchor at
    // Density 0) still falls through to identity below, so the bit-identity
    // anchor holds.
    constexpr float kSmearAmt = 0.12f;   // ±12 % wobble depth
    const float smear = kSmearAmt * std::sin(smearPhase_[ch]);
    smearPhase_[ch] = fmodf(smearPhase_[ch] + kSmearRate / static_cast<float>(sampleRate_), 2.0f*M_PI_F);
    const float diffCoef = static_cast<float>(base * (0.4f + 0.6f * dfrac) * (1.0f + smear));
    float& x1 = digApX_[ch]; float& y1 = digApY_[ch];
    if (diffCoef <= 0.0005f) { x1 = x; y1 = x; return x; }
    const float a = 0.5f;
    const float yAp = a * x + x1 - a * y1;          // y = a*x + x[n-1] - a*y[n-1]
    x1 = x; y1 = yAp;
    return (1.0f - diffCoef) * x + diffCoef * yAp; // blend dry input + APF by amount
  }

  void processSpring(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_Spring.h

  void processPlate(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_Plate.h

  void processRoom(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_Room.h

  void processChamber(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_Chamber.h

  void processHall(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_Hall.h

 private:
  struct Line {
    std::vector<float> ring;
    uint32_t size = 0, mask = 0;
    uint32_t write = 0;
    float lp = 0.0f;
    void init(size_t minSize) {
      size_t n = 1;
      while (n < minSize) n <<= 1;
      size = static_cast<uint32_t>(n);
      mask = static_cast<uint32_t>(n - 1);
      ring.assign(n, 0.0f);
    }
    void clear() {
      std::fill(ring.begin(), ring.end(), 0.0f);
      write = 0;
      lp = 0.0f;
    }
  };

  std::vector<std::vector<Line>> lines_;  // lines_[channel][line]
  double taps_[kNumLines] = {};
  // Persistent INPUT-history ring per channel (a cross-block early-reflection
  // delay line). The early taps (Room/Chamber/Hall) read their reflections from
  // this input history, not the in-block output buffer, so they fire at every
  // audio block length (the Schroeder/Gardner feedforward early field, not a
  // block-boundary-dependent read). Sized in prepare(), zeroed in reset().
  std::vector<float> inHist_[kMaxChannels];
  uint32_t inHistMask_ = 0;
  uint32_t inHistWrite_ = 0;
  double sampleRate_ = 0.0;
  Params params_{};
  // Digital Mod waver state (mirrors the delay's modOn_/modDepth/modInc/modPhase):
  // a sined read-tap wobble, L + / R - opposite, gated off at Mod 0 so the plain
  // integer read stays bit-exact.
  bool modOn_ = false;
  float modDepthSamples_ = 0.0f;  // full-mod wobble depth (samples)
  double modInc_ = 0.0;          // LFO increment per sample (2*pi*rate/sr)
  double modPhase_ = 0.0;        // cross-block LFO phase
  // Digital all-pass diffusion state (Schroeder/Moorer): prior x and y samples per
  // channel. Identity at Density 0; reset in reset().
  float digApX_[kMaxChannels] = {};
  float digApY_[kMaxChannels] = {};
  // LFO-smear (Spin Semi technique): slowly drift the wash all-pass coefficient
  // so the comb's periodic eigenfrequencies spread and beat out, instead of
  // reading as a metallic shimmer on a sustained tone. Rate 0.25 Hz -> one full
  // wobble per 4 s, inaudible as a wobble but enough to decorrelate the ripple.
  static constexpr double kSmearRate = 0.25;              // Hz
  static constexpr float  M_PI_F     = static_cast<float>(M_PI);
  float smearPhase_[kMaxChannels] = {};   // per-channel LFO phase (radians)
  // Fb-drift: modulates the comb's feedback directly (not the post-comb all-pass).
  // Rate 0.18 Hz (one wobble every ~5.5 s), depth ±3 % of the raw fb value.
  // This shifts each line's resonance frequency by ±3 % per cycle, enough to
  // smear the standing-wave peaks so a sustained tone no longer lands on a bright
  // resonance. Gated: OFF when kFbDriftAmt = 0 (Digital anchor stays bit-identical).
  static constexpr double kFbDriftRate = 0.18;
  static constexpr double kFbDriftAmt  = 0.03;         // ISOLATION: drift ON ±3%   // ±3 %
  float fbDriftPhase_[kMaxChannels] = {};        // offset by quarter-cycle from smearPhase_
  // Shared Mod (R2, 2026-10-14, all six modes): a NEW, slow, post-mix LFO on the
  // wet reverb sum (one-pole lowpass whose cutoff breathes with the LFO), gated
  // OFF at `sharedMod` 0 by construction — see DESIGN DECISION in
  // docs/tickets/reverb-dry-mod-width-swing.md. Strictly separate from
  // smearPhase_/fbDriftPhase_ (the always-on per-mode signature laws, untouched)
  // and from modOn_/modPhase_ (Digital's separate read-tap waver, untouched).
      bool sharedOn_ = false;
  double smInc_         = 0.0;                 // base LFO phase increment (rad/sample)
  // TEST/SWEEP KNOBS (measure-the-law, 2026-10-15): scales on the chorus
  // layer amount and max tap depth -- 1.0 always (the shipped constants),
  // only tests may move them, to sweep combinations by MEASUREMENT before
  // they become committed constants. Never read by the UI.
  double smTestAmtScale_  = 1.0;
  double smTestDepthScale_ = 1.0;
  double smPh_[kMaxChannels] = {};           // persistent base phase (radians) — per-voice offsets derived from it
  // Biquad low-pass (12 dB/oct) on the summed delayed voices — a musical,
  // smooth darkening instead of three cheap one-poles. Coefficients set in
  // setParams(); state (x1, x2, y1, y2) per channel, zeroed in reset().
  double smB0_ = 0.0, smB1_ = 0.0, smB2_ = 0.0, smA1_ = 0.0, smA2_ = 0.0;
  double smBqX1_[kMaxChannels][kModVoices] = {}; double smBqX2_[kMaxChannels][kModVoices] = {};
  double smBqY1_[kMaxChannels][kModVoices] = {}; double smBqY2_[kMaxChannels][kModVoices] = {};
  // Chorus stage (R2, 2026-10-15 multi-voice rewrite): the shared Mod is a
  // MULTI-VOICE CHORUS on the wet reverb (kModVoices delay lines of distinct
  // depth / rate / phase per channel, mixed level-preservingly with the
  // undelayed voice), NOT a level swell. One ring per channel (power-of-two),
  // sized for the deepest voice, written once per sample; all voices read from
  // it. Zeroed on reset().
  std::array<std::vector<float>, kMaxChannels> chRing_;
  uint32_t chMask_      = 0;
  std::array<uint32_t, kMaxChannels> chWrite_{};
  // Width-Haas (R3): mid-wet history ring + gate/delay (see applyWidthHaas).
  std::vector<float> whRing_;
  uint32_t whMask_    = 0;
  uint32_t whWrite_   = 0;
  bool widthHaasOn_   = false;
  size_t widthHaasDelay_ = 0;
  uint32_t chDelayMax_  = 0;    // max delay samples (at current sample rate, sharedMod 1)

  // ---------------------------------------------------------------- PLT ("T-DAT-001")
  // The "DAT" plate engine -- a MODIFIED port of the Dattorro plate
  // topology (Dattorro 1997, the published algorithm): two independent mono
  // tanks (one per channel, user-locked design), each a fig-8 chain of four
  // input allpasses -> A half (ap, d0, ap5, d1) -> B half (ap, d2, ap6, d3),
  // fig-8 cross-feed, per-side Schroeder-I taps. OUR LAYERS: T60->fb Decay,
  // Pre ring, Early ladder, Tone damping corner, LoCut HPF, Air shelf,
  // Space-scaled diffusion, the per-side stereo split, and the Mod
  // post-wet chorus layer.
  struct DatLine {
    std::vector<float> ring_;
    uint32_t w    = 0;
    uint32_t mask = 1;
    void init(size_t minSize) {
      size_t n = 2;
      while (n < minSize) n <<= 1;
      ring_.assign(n, 0.0f);
      mask = static_cast<uint32_t>(n - 1);
      w    = 0;
    }
    void clear() { std::fill(ring_.begin(), ring_.end(), 0.0f); }
    void push(float v) { ring_[w & mask] = v; ++w; }
    float readAt(uint32_t off) const { return ring_[(w + mask - off) & mask]; }
  };
  struct DatChan {
    DatLine apIn[4], apm[2], ap5, ap6, dly[4], pre;
    float inLp = 0.0f, dmpA = 0.0f, dmpB = 0.0f, sumB = 0.0f;
    float airLp = 0.0f, loX = 0.0f, loY = 0.0f;
  };
  std::array<DatChan, kMaxChannels> dat_  = {};
  // coefficients (setParams-frozen):
  int   datApInLen_[4]  = {};
  int   datApLen_[2]    = {};    // apm1, apm2
  int   datDecLen_[2]   = {};    // ap5, ap6
  int   datDlyLen_[4]   = {};
  int   datTap_[14]     = {};
  int   datPreTap_[3]   = {};
  float datDiffIn_[4]   = {};
  float datDiffDec_[4]  = {};
  float datPre_         = 0.0f;
  float datDmpA_ = 0.0f, datAirA_ = 0.0f, datInA_ = 0.0f, datLoA_ = 0.0f;
  int   datLoopTot_ = 1;    // one full A-B-A fig-8 traversal (samples, at current size)
  float datDec_ = 1.0f;     // fig-8 loop decay multiplier (Decay knob, 0.82 cap; all 6 decay sites)

  // T-DAT-001: one Schroeder-I allpass pass over L with per-tap gain g.
  // Form: a = pop; fb = a*g; ff = -x - a*g; push(x+fb); y = a + ff.
  inline float datApf(DatLine& L, int off, float g, float x) {
    const float a = L.readAt(static_cast<uint32_t>(off));
    const float f = -a * g - x;
    L.push(x + a * g);
    return a + f;
  }

  void processDat(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_DAT.h

  // ---------------------------------------------------------------- PLT ("T-DAT-001")

  // Measured-tune accessor (R2, 2026-10-15): sweep the chorus layer
  // amount/depth by real render in DspTests before committing constants.
  double smTestLpfHz_ = 0.0;  // >0: override kModVoiceLpfHz (measured-tune hook)
  double smTestFloor_ = 0.0;  // >0: override kModVoiceFloor (measured-tune hook)
  double smTestRateScale_ = 1.0;  // !=1: scale kSharedModRate (measured-tune hook)
  int smTestVoices_ = 0;  // >1: number of delay voices to average (measured-tune hook)
  double smTestWriteTauMs_ = 0.0; // >0: ms time constant on the ring WRITE (eleventh-cut hook)
  float smWriteCoef_ = 0.0f;      // per-sample one-pole on the ring write (0 = off -> bit-exact anchor)
  std::array<float, kMaxChannels> smWritePrev_{};
  friend struct ReverbChorusSweep;
  // Spring state: the metallic boing resonator (2.4 kHz) per channel + the
  // onset-splash (drip) env per channel. Both are bounded (|r|<1, decaying env)
  // and reset in reset(). Only used when the Spring law path is live.
  float boingRe_[kMaxChannels] = {};
  float boingIm_[kMaxChannels] = {};
  float dripEnv_[kMaxChannels] = {};
  float splashLp_[kMaxChannels] = {};  // softened (low-pass) drip splash, per channel
  float whipLp_[kMaxChannels] = {};    // softened (low-pass) plate whip, per channel
  // Spring: 8 incommensurate resonator mode states (220-2800 Hz metallic body)
  float r1Re_[kMaxChannels] = {}; float r1Im_[kMaxChannels] = {};   // 220 Hz
  float r2Re_[kMaxChannels] = {}; float r2Im_[kMaxChannels] = {};   // 350 Hz
  float r3Re_[kMaxChannels] = {}; float r3Im_[kMaxChannels] = {};   // 490 Hz (peak)
  float r4Re_[kMaxChannels] = {}; float r4Im_[kMaxChannels] = {};   // 640 Hz
  float r5Re_[kMaxChannels] = {}; float r5Im_[kMaxChannels] = {};   // 900 Hz
  float r6Re_[kMaxChannels] = {}; float r6Im_[kMaxChannels] = {};   // 1300 Hz
  float r7Re_[kMaxChannels] = {}; float r7Im_[kMaxChannels] = {};   // 1800 Hz
  float r8Re_[kMaxChannels] = {}; float r8Im_[kMaxChannels] = {};   // 2800 Hz
  // Spring: 5 kHz wash low-pass state (kills the artificial 8+ kHz comb shimmer).
  float washLp_[kMaxChannels] = {};
  float washLp2_[kMaxChannels] = {};   // 2nd LP pole (steeper HF rolloff: -12 dB/oct total)
  float hpfX_[kMaxChannels] = {};   // 1-pole HPF prev input (per channel)
  float hpfY_[kMaxChannels] = {};   // 1-pole HPF prev output (per channel)
  // Spring body peaking-EQ state (biquad DF-II: x1, x2, y1, y2 per channel).
  float peakEq_[kMaxChannels][4] = {};

  // Plate state: the dense "whip" onset burst envelope per channel (bounded,
  // decaying, reset in reset()). The dense wash + bloom low-pass + color are
  // stateless (they use the shared comb lines + the existing Line.lp).
  float onsetEnv_[kMaxChannels] = {};
  // P-ons (onset density): the softened membrane-ping bank state per channel
  // (zero-initialized, reset in reset()); tap offsets computed in setParams.
  float plateOnsetLp_[kMaxChannels] = {};
  double plateOnsetSamples_[kPlateOnsetTaps] = {};
  // P-body (percept: low-mid body): the per-line peaking state (biquad w1/w2,
  // zero-initialized, reset in reset()) + the fb-capped coefficients
  // (setParams-time, sample-rate dependent).
  float bodyW1_[kMaxChannels][kNumLines] = {};
  float bodyW2_[kMaxChannels][kNumLines] = {};
  // plate/"140" air law: the two in-series first-order LPF stages (per channel)
  // (the size law is a pure alpha shift on these, not a separate state --
  //  see the SIZE LAW block above).
  float airL1_[kMaxChannels] = {}, airL2_[kMaxChannels] = {};
  double bodyB0_ = 1.0, bodyB1_ = 0.0, bodyB2_ = 0.0;
  double bodyA1_ = 0.0, bodyA2_ = 0.0;
  double airLp_ = 0.0;                     // out-stage air first-order alpha (decay-tracked corner)
  // Room state: per-tap lowpass state (the "air" damping), 4 taps per channel
  // (zero-initialized, reset in reset()). The early field + mode wash are
  // stateless (they use the shared comb lines + existing state).
  float roomTapsLp_[kMaxChannels][kNumRoomTaps] = {};
  // Room early-field tap sample offsets (the 4 fixed taps, setParams time).
  double roomTapsSamples_[kNumRoomTaps] = {};
  // Chamber state: per-tap lowpass (the diffuse early volley's per-reflection
  // damping, like Room's air but applied to the cluster taps) + the shared
  // loop lowpass state (the Bass shelf, the low tail extends) + the output HF-cap
  // lowpass state (the ~10 kHz humidity cap, the high tail caps).
  float chamberTapsLp_[kMaxChannels][kNumChamberTaps] = {};
  float chamberBassLp_[kMaxChannels] = {};
  float chamberHFCap_[kMaxChannels] = {};
  // Chamber early-volley tap sample offsets (the 8 diffuse taps, setParams time).
  double chamberTapsSamples_[kNumChamberTaps] = {};
  // Hall state: the per-tap early cluster is a STATELESS ADDITION (each tap is a
  // low-passed read of the input buffer, the diffuse long build-up); the L/R
  // LATERAL SPLIT (more on L, less on R, a function of the Space dial) is
  // applied at the sum; the tap sample offsets are computed from the live sample
  // rate in setParams().
  // Hall state: per-tap air low-pass state (the diffuse build's per-reflection
  // damping, like Room/Chamber -- this is what kills the bright recursive shimmer).
  float hallTapsLp_[kMaxChannels][kNumHallTaps] = {};
  double hallTapsSamples_[kNumHallTaps] = {};

  void processSharedModSmear(juce::AudioBuffer<float>& buffer);  // impl: effects/reverb/Reverb_SharedMod.h
};

#include "Reverb_Spring.h"
#include "Reverb_Plate.h"
#include "Reverb_Room.h"
#include "Reverb_Chamber.h"
#include "Reverb_Hall.h"
#include "Reverb_DAT.h"
#include "Reverb_SharedMod.h"
