// Display scales for knobs (port of knobScale.ts). Every knob's value is
// normalised 0..1 (APVTS / chain params); a KnobScale maps that to real
// units for the drag readout and the double-click text entry, and back
// again when the user types a value.
#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <optional>

#include "Labels.h"

namespace t3k::ui {

struct KnobScale {
  // Normalised -> real units (dB, %, ms, ...).
  std::function<double(double)> toDisplay;
  // Real units -> normalised. Caller clamps to the knob's min/max.
  std::function<double(double)> fromDisplay;
  // ---- STORAGE contract: what actually goes into / comes out of the block
  // param, separate from the DISPLAY unit above. A knob can store one unit
  // and show another (tape Heads stores a 0..1 fraction but shows 1..4).
  // Leave these empty --
  // the common case -- and the storage domain IS the display domain, which is
  // correct for every real-unit-stored param (ms/Hz/BPM/ratio/dB) and the
  // identity-stored fraction params. The tile must route its write/read
  // through knobToStored()/knobFromStored(), never toDisplay/fromDisplay.
  std::function<double(double)> toStored;
  std::function<double(double)> fromStored;
  // Full readout string, units included (e.g. "-3.2 dB").
  std::function<juce::String(double)> format;
  // Text-entry prefill (number only, no unit, which is easier to retype).
  std::function<juce::String(double)> editText;
  // Optional number of evenly spaced detents across the knob's 0..1 travel.
  // When set (>=2) the knob clicks between these positions (Knob::Options::steps).
  std::optional<int> steps;
};

// Storage-contract accessors: the value that actually goes into the block
// param (knobToStored) / back out of it (knobFromStored). They fall back to
// the DISPLAY mapping when a scale declares no explicit storage pair, i.e.
// exactly the legacy behaviour (stored unit == shown unit) -- so every knob
// that never needed the split stays bit-identical. The tile's write (onChange)
// and resync (setValue) route through these and must NOT call toDisplay/
// fromDisplay directly: that is the recurring "knob changes sound then snaps
// back" glitch, where a knob with a normalised store but a human-scale display
// (Heads) wrote its display unit (1..4) straight into the 0..1
// param, got it clamped to 1.0, and the next resync snapped the knob to max.
inline double knobToStored(const KnobScale& s, double normalised) {
  return s.toStored ? s.toStored(normalised) : s.toDisplay(normalised);
}
inline double knobFromStored(const KnobScale& s, double stored) {
  return s.fromStored ? s.fromStored(stored) : s.fromDisplay(stored);
}

namespace scales {

inline KnobScale make(std::function<double(double)> toDisplay,
                      std::function<double(double)> fromDisplay, juce::String unit, int decimals) {
  KnobScale s;
  s.toDisplay = toDisplay;
  s.fromDisplay = std::move(fromDisplay);
  s.format = [toDisplay, unit, decimals](double n) {
    return labels::toFixed(toDisplay(n), decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
  };
  s.editText = [toDisplay, decimals](double n) { return labels::toFixed(toDisplay(n), decimals); };
  return s;
}

// Straight-line map from normalised 0..1 to [min..max] display units.
inline KnobScale linear(double min, double max, juce::String unit = {}, int decimals = 1) {
  return make([=](double n) { return min + n * (max - min); },
              [=](double d) { return (d - min) / (max - min); }, std::move(unit), decimals);
}

// 0..1 -> 0..100 %. Default for knobs that don't declare a scale.
inline const KnobScale& percent() {
  static const KnobScale s = make([](double n) { return n * 100; }, [](double d) { return d / 100; },
                                  "%", 0);
  return s;
}

// Main / per-block gain: normalised 0.5 = unity, full range ±24 dB.
inline const KnobScale& gainDb() {
  static const KnobScale s = linear(-24, 24, "dB", 1);
  return s;
}

// Stereo balance trim: 0.5 = centred, ±12 dB per channel at the ends.
inline const KnobScale& balanceDb() {
  static const KnobScale s = linear(-12, 12, "dB", 1);
  return s;
}

// Gate threshold: normalised spans -100..0 dB.
inline const KnobScale& gateDb() {
  static const KnobScale s = linear(-100, 0, "dB", 0);
  return s;
}

// Gate deck. These mirror the APVTS ranges in Processor.cpp (the parameters
// are stored in real units): release is a log map over 5-500 ms so the
// tight end has resolution, hold and range are linear.
inline const KnobScale& gateReleaseMs() {
  static const KnobScale s = make([](double n) { return 5.0 * std::pow(100.0, n); },
                                  [](double d) { return std::log(d / 5.0) / std::log(100.0); },
                                  "ms", 0);
  return s;
}
inline const KnobScale& gateHoldMs() {
  static const KnobScale s = linear(0, 200, "ms", 0);
  return s;
}
// Depth of the closed gate as positive attenuation, so clockwise gates
// harder (80 dB is the full mute the gate shipped with).
inline const KnobScale& gateRangeDb() {
  static const KnobScale s = linear(20, 80, "dB", 0);
  return s;
}

// Pitch shift: bipolar semitones, centre = 0, ±24. The parameter is
// continuous; with the deck's STEP on the knob detents to whole semitones
// and the readout shows them whole ("+3 st"), with STEP off it sweeps and
// reads to a tenth ("+2.5 st"). The sign is spelled out so "+3 st" and
// "-3 st" can't be confused at a glance.
inline const KnobScale& semitones() {
  static const KnobScale s = [] {
    KnobScale c;
    const auto st = [](double n) { return -24 + n * 48; };
    const auto text = [st](double n) {
      const double v = st(n);
      const double whole = std::round(v);
      const bool isWhole = std::abs(v - whole) < 0.05;
      const juce::String digits = isWhole ? juce::String(static_cast<int>(whole)) : labels::toFixed(v, 1);
      return ((isWhole ? whole : v) > 0 ? "+" : "") + digits;
    };
    c.toDisplay = st;
    c.fromDisplay = [](double d) { return (d + 24) / 48; };
    c.format = [text](double n) { return text(n) + " st"; };
    c.editText = [text](double n) { return text(n).trimCharactersAtStart("+"); };
    return c;
  }();
  return s;
}

// Pitch deck. The tonality limit rides a log map whose top end reads
// "Off" (a pure shift; the processor treats the end value the same way);
// the buffer is the four detents in PitchShift.h (20 / 30 / 40 / 60 ms),
// read out as the buffer size itself. (The latency each reports to the
// host, 11 / 16 / 21 / 31 ms, is the help text's business.)
inline const KnobScale& tonalityHz() {
  static const KnobScale s = [] {
    KnobScale c;
    const auto hz = [](double n) { return 1000.0 * std::pow(20.0, n); };
    c.toDisplay = hz;
    c.fromDisplay = [](double d) { return std::log(d / 1000.0) / std::log(20.0); };
    c.format = [hz](double n) {
      if (n >= 1.0 - 1e-6) return juce::String("Off");
      return labels::toFixed(hz(n) / 1000.0, 1) + " kHz";
    };
    c.editText = [hz](double n) { return juce::String(juce::roundToInt(hz(n))); };
    return c;
  }();
  return s;
}
inline const KnobScale& bufferMs() {
  static const KnobScale s = [] {
    KnobScale c;
    static constexpr int kMs[] = {20, 30, 40, 60};  // static: the lambdas index it without a capture
    const auto index = [](double n) { return juce::jlimit(0, 3, juce::roundToInt(n * 3)); };
    c.toDisplay = [index](double n) { return kMs[index(n)]; };
    // Snaps typed values to the nearest detent.
    c.fromDisplay = [](double d) { return d < 25 ? 0.0 : d < 35 ? 1.0 / 3 : d < 50 ? 2.0 / 3 : 1.0; };
    c.format = [index](double n) { return juce::String(kMs[index(n)]) + " ms"; };
    c.editText = [index](double n) { return juce::String(kMs[index(n)]); };
    return c;
  }();
  return s;
}

// Faceplate tone stack knobs: 0..10, 5 = flat.
inline const KnobScale& tone() {
  static const KnobScale s = linear(0, 10, {}, 1);
  return s;
}

// Bipolar one-sided delay: centre = 0 ms, ends reach ±maxMs. Display shows
// the magnitude plus the delayed side ("15.0 ms R").
inline KnobScale sidedMs(double maxMs) {
  const double span = 2 * maxMs;
  KnobScale s;
  s.toDisplay = [span](double n) { return (n - 0.5) * span; };
  s.fromDisplay = [span](double d) { return 0.5 + d / span; };
  s.format = [span](double n) {
    const double ms = (n - 0.5) * span;
    if (std::abs(ms) < 0.05) return juce::String("0 ms");
    return labels::toFixed(std::abs(ms), 1) + " ms " + (ms < 0 ? "L" : "R");
  };
  s.editText = [span](double n) { return labels::toFixed((n - 0.5) * span, 1); };
  return s;
}

// Offset (bipolar), shared by the mono-mode Spread lag and the stereo-mode
// Align delay: centre = 0 ms, ends reach 24 ms toward L or R.
inline const KnobScale& offsetMs() {
  static const KnobScale s = sidedMs(24);
  return s;
}

// Deck crossover cutoff: log map over 32.5-520 Hz, centre = 130 Hz.
inline const KnobScale& crossoverHz() {
  static const KnobScale s = [] {
    KnobScale c;
    const auto hz = [](double n) { return 32.5 * std::pow(16.0, n); };
    c.toDisplay = hz;
    c.fromDisplay = [](double d) { return std::log(d / 32.5) / std::log(16.0); };
    c.format = [hz](double n) { return juce::String(juce::roundToInt(hz(n))) + " Hz"; };
    c.editText = [hz](double n) { return juce::String(juce::roundToInt(hz(n))); };
    return c;
  }();
  return s;
}

// Chain pan halves. The left knob covers normalised 0..0.5 (hard left ..
// centre), the right 0.5..1 (centre .. hard right). Display is the pan
// amount toward the side, 100 = hard, 0 = centre.
inline const KnobScale& pan(bool left) {
  static const auto make = [](bool l) {
    KnobScale s;
    const auto toDisplay = [l](double n) { return l ? (0.5 - n) * 200 : (n - 0.5) * 200; };
    s.toDisplay = toDisplay;
    s.fromDisplay = [l](double d) { return l ? 0.5 - d / 200 : 0.5 + d / 200; };
    s.format = [toDisplay, l](double n) {
      const int amount = juce::roundToInt(toDisplay(n));
      return amount == 0 ? juce::String("C") : juce::String(amount) + (l ? "L" : "R");
    };
    s.editText = [toDisplay](double n) { return juce::String(juce::roundToInt(toDisplay(n))); };
    return s;
  };
  static const KnobScale leftScale = make(true), rightScale = make(false);
  return left ? leftScale : rightScale;
}

// Built-in effect knobs (Delay / Chorus). The chain stores each in real units
// (ms / Hz / fraction); toDisplay() maps the knob's normalised 0..1 onto those
// stored units (the tile writes toDisplay(v) straight to the param) and
// format()/editText() render the same numbers for the readout / type-in.
inline const KnobScale& delayTimeMs() {
  static const KnobScale s = linear(5.0, 1000.0, "ms", 0);
  return s;
}
inline const KnobScale& chorusRateHz() {
  static const KnobScale s = linear(0.05, 5.0, "Hz", 2);
  return s;
}
inline const KnobScale& chorusDepthMs() {
  static const KnobScale s = linear(0.0, 5.0, "ms", 2);
  return s;
}
// Mod mode's unique RATE (delayRateHz): the wobble SPEED, stored real Hz
// (0.5 = super-slow drift .. 30 = fast flutter). Log face so the classic
// 5 Hz (Delay::kModWobbleHz / kRateDefaultHz) sits just right of centre;
// the display domain IS the stored domain (a real-unit store, the
// delayTimeMs/chorusRateHz class), so no toStored/fromStored override is
// needed and the 5 Hz default keeps Mod mode bit-identical to the pre-Rate
// law.
inline const KnobScale& modRateHz() {
  static const KnobScale s = make(
      [](double n) { return 0.5 * std::pow(60.0, juce::jlimit(0.0, 1.0, n)); },
      [](double d) { return std::log(std::max(d, 0.5) / 0.5) / std::log(60.0); }, "Hz", 1);
  return s;
}
// Delay feedback: stored as a 0..0.9 fraction, shown as a 0..90% percentage.
inline const KnobScale& delayFeedback() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n * 0.9; };
    c.fromDisplay = [](double d) { return d / 0.9; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 90.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 90.0)); };
    return c;
  }();
  return s;
}
// Delay damping: stored as a 0..1 fraction (0 = bright, 1 = dark), shown 0..100%.
inline const KnobScale& delayDamping() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 100.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }();
  return s;
}
// Chorus tone: stored 0..1 (0 = darkest/warmest, 0.5 = flat/neutral, 1 =
// brightest); shown as dB. Left half is a low-pass on the wet copy (1.2 kHz
// corner -> transparent at noon); right half a high shelf up to +12 dB.
inline const KnobScale& chorusTone() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return (juce::jlimit(0.0, 1.0, n) - 0.5) * 36.0; };  // -18..+18 uniform
    c.fromDisplay = [](double d) { return 0.5 + juce::jlimit(-18.0, 18.0, d) / 36.0; };
    c.format = [](double n) {
      const int d = juce::roundToInt((n - 0.5) * 36.0);
      return (d > 0 ? juce::String("+") : juce::String()) + juce::String(d) + " dB";
    };
    c.editText = [](double n) {
      const int d = juce::roundToInt((n - 0.5) * 36.0);
      return (d > 0 ? juce::String("+") : juce::String()) + juce::String(d);
    };
    return c;
  }();
  return s;
}
// Chorus spread: stored as a 0..1 fraction (0 = mono, 1 = wide), shown 0..100%.
inline const KnobScale& chorusSpread() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 100.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }();
  return s;
}
// Chorus LFO shape: the same 5 detents / waveform set as the tremolo LFO,
// so "Shape" means the same thing on both tiles.
inline const KnobScale& chorusWave() {
  static const char* const names[5] = {"Sine", "Triangle", "Saw", "Saw (Down)", "Square"};
  static const KnobScale s = [] {
    const int n = 4;  // 5 detents -> normalised 0, 1/4, 1/2, 3/4, 1
    auto idx = [n](double v) { int i = (int)std::round(v * n); return i < 0 ? 0 : (i > n ? n : i); };
    KnobScale c;
    c.toDisplay = [n](double v) { return std::round(v * n); };
    c.fromDisplay = [n](double d) { return d / n; };
    c.format = [n, idx](double v) { return juce::String(names[idx(v)]); };
    c.editText = [n, idx](double v) { return juce::String(names[idx(v)]); };
    c.steps = n + 1;  // 5 detents -> the knob clicks between them
    return c;
  }();
  return s;
}

// Sync-mode tempo: 60..240 BPM (a quarter note at 60 BPM lands on the 1000 ms
// max, so every subdivision fits at every tempo).
inline const KnobScale& delayBpm() {
  static const KnobScale s = linear(60.0, 240.0, "BPM", 0);
  return s;
}
// Sync-mode Time knob: nine fixed note values, fastest -> slowest (16th
// Triplet .. Whole). The knob detents to the 9 steps and the readout shows
// the note name; the index maps to Delay::noteDurationMs() on the chain side.
inline const KnobScale& subdivision() {
  static const char* const names[9] = {"1/16T", "1/16", "1/8T", "D8", "1/8", "1/4T", "1/4", "1/2", "1/1"};
  static const KnobScale s = [] {
    const int n = 8;  // 9 detents -> normalised positions 0, 1/8 .. 8/8
    auto idx = [n](double v) { int i = (int)std::round(v * n); return i < 0 ? 0 : (i > n ? n : i); };
    KnobScale c;
    c.toDisplay = [n](double v) { return std::round(v * n); };
    c.fromDisplay = [n](double d) { return d / n; };
    c.format = [n, idx](double v) { return juce::String(names[idx(v)]); };
    c.editText = [n, idx](double v) { return juce::String(names[idx(v)]); };
    c.steps = n + 1;  // 9 detents -> the knob clicks between them
    return c;
  }();
  return s;
}


// Tremolo: LFO rate (Hz), modulation depth (0..1), and the LFO shape
// (Sine/Triangle, a 2-step detented knob).
inline const KnobScale& tremoloRate() {
  static const KnobScale s = linear(0.1, 10.0, "Hz", 2);
  return s;
}
inline const KnobScale& tremoloDepth() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double d) { return juce::String(juce::roundToInt(d * 100.0)); };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }();
  return s;
}
inline const KnobScale& tremoloLfo() {
  static const char* const names[5] = {"Sine", "Triangle", "Saw", "Saw (Down)", "Square"};
  static const KnobScale s = [] {
    const int n = 4;  // 5 detents -> normalised 0, 1/4, 1/2, 3/4, 1
    auto idx = [n](double v) { int i = (int)std::round(v * n); return i < 0 ? 0 : (i > n ? n : i); };
    KnobScale c;
    c.toDisplay = [n](double v) { return std::round(v * n); };
    c.fromDisplay = [n](double d) { return d / n; };
    c.format = [n, idx](double v) { return juce::String(names[idx(v)]); };
    c.editText = [n, idx](double v) { return juce::String(names[idx(v)]); };
    c.steps = n + 1;  // 5 detents -> the knob clicks between them
    return c;
  }();
  return s;
}

// Compressor (see Compressor.h). In/Out/Mix are the block-level gains (the
// existing gainDb()/percent() scales); these are the compressor-specific knobs.

// Ratio: detented to the classic compression ratios, shown as "x:1".
inline const KnobScale& compRatio() {
  static const double steps[9] = {1.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 16.0, 20.0};
  static const KnobScale s = [] {
    const int n = 8;  // 9 detents -> normalised positions 0, 1/8 .. 8/8
    auto idx = [n](double v) { int i = (int)std::round(v * n); return i < 0 ? 0 : (i > n ? n : i); };
    KnobScale c;
    c.toDisplay = [idx, &steps](double v) { return steps[idx(v)]; };
    c.fromDisplay = [n, &steps](double d) {
      int best = 0; double bestDiff = 1e9;
      for (int i = 0; i < 9; ++i) {
        const double diff = std::fabs(steps[i] - d);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
      }
      return best / (double)n;
    };
    c.format = [idx, &steps](double v) { return juce::String(juce::roundToInt(steps[idx(v)])) + ":1"; };
    c.editText = [idx, &steps](double v) { return juce::String(juce::roundToInt(steps[idx(v)])); };
    c.steps = n + 1;  // 9 detents -> the knob clicks between them
    return c;
  }();
  return s;
}
// Attack: log over 0.1..500 ms so the fast end has resolution.
inline const KnobScale& compAttack() {
  static const KnobScale s = make([](double n) { return 0.1 * std::pow(5000.0, n); },
                                  [](double d) { return std::log(d / 0.1) / std::log(5000.0); }, "ms", 1);
  return s;
}
// Release: log over 20..2000 ms.
inline const KnobScale& compRelease() {
  static const KnobScale s = make([](double n) { return 20.0 * std::pow(100.0, n); },
                                  [](double d) { return std::log(d / 20.0) / std::log(100.0); }, "ms", 0);
  return s;
}
// Vari-Mu (670) release: the SAME knob geometry and chain value as compRelease
// (the 20-2000 ms "position"), but the readout/text-entry speak the 670 release
// ladder that Compressor::recalc() applies in Vari-Mu mode -- full travel maps
// to 0.04 -> 25 s (tau = 0.04*(25/0.04)^p, p = (ms/1000 - 0.02)/1.98, the
// knob halfway maps to exactly 1.0 s). The second
// half of the knob additionally engages the slow, program-dependent hold (the
// 670's "multiple-peak" / "sustained" positions). Display-only: the chain still
// stores the 20-2000 position.
inline const KnobScale& compRelease670() {
  static const KnobScale s = [ ] {
    auto tauSec = [](double n) {
      const double p670 = juce::jlimit(0.0, 1.0, (20.0 * std::pow(100.0, n) * 0.001 - 0.02) / 1.98);
      return 0.04 * std::pow(25.0 / 0.04, p670);
    };
    return make(tauSec,
                [](double sec) {
                  const double p670 = std::log(std::max(sec, 0.04) / 0.04) / std::log(25.0 / 0.04);
                  return std::log(1.0 + 99.0 * p670) / std::log(100.0);
                }, "s", 2);
  }();
  return s;
}
// Tone: treble shelf, -12..+12 dB (0 = flat; + adds treble, - cuts it).
inline const KnobScale& compTone() {
  static const KnobScale s = linear(-12.0, 12.0, "dB", 1);
  return s;
}
// Side-chain high-pass: log over 20..8000 Hz.
inline const KnobScale& compScHp() {
  static const KnobScale s = make([](double n) { return 20.0 * std::pow(400.0, n); },
                                  [](double d) { return std::log(d / 20.0) / std::log(400.0); }, "Hz", 0);
  return s;
}

  // PUNCH: two detents (compMbc is a bool) -- the knob clicks between OFF and
  // ON and the stored value is exactly 0 or 1, the way the chain expects.
  inline const KnobScale& compPunch() {
    static const KnobScale s = [] {
      KnobScale c;
      c.toDisplay = [](double v) { return v > 0.5 ? 1.0 : 0.0; };
      c.fromDisplay = [](double d) { return d > 0.0 ? 1.0 : 0.0; };
      c.format = [](double v) { return v > 0.5 ? "ON" : "OFF"; };
      c.editText = [](double v) { return v > 0.5 ? "on" : "off"; };
      c.steps = 2;  // the knob clicks between the two
      return c;
    }();
    return s;
  }

// CLIP (stage-coloration depth, compressor modes 1-4): STORAGE is the raw
// 0..2 depth the engine clamps to (1.0 = noon = that mode's normal breakup;
// 0 = clean, >1 = hotter) -- NOT the 0..200 % face. Same split class as
// Heads: without toStored the tile writes the display unit into a 0..2
// param, it clamps, and the knob snaps back.
inline const KnobScale& compClip() {
  static const KnobScale s = [] {
    KnobScale c = make([](double n) { return n * 200.0; },   // knob 0..1 -> 0..200 %
                       [](double d) { return d / 200.0; },   // % display -> knob
                       "%", 0);
    c.toStored = [](double n) { return n * 2.0; };           // knob -> raw 0..2
    c.fromStored = [](double d) { return d / 2.0; };         // raw -> knob
    return c;
  }();
  return s;
}

// KNEE (VCA soft-knee width, dB): 1..11; noon 6 = the classic 6 dB transition.
inline const KnobScale& compKnee() {
  static const KnobScale s = linear(1.0, 11.0, "dB", 1);
  return s;
}

// Threshold: where gain reduction starts (dBFS, -48..+6; default -18 for all modes).
inline const KnobScale& compThreshold() {
  static const KnobScale s = linear(-48.0, 6.0, "dB", 1);
  return s;
}

// ---- Reverb (see Reverb.h) ----
// Decay: tail length, 50..3000 ms.
inline const KnobScale& reverbDecay() {
  static const KnobScale s = linear(50.0, 3000.0, "ms", 0);
  return s;
}
// Pre-delay: 0..60 ms.
inline const KnobScale& reverbPre() {
  static const KnobScale s = linear(0.0, 60.0, "ms", 0);
  return s;
}
// Tone: 0..1 (0 bright, 1 dark), shown 0..100%.
inline const KnobScale& reverbTone() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 100.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }
();
  return s;
}
// Size: 0..1 (scales all delay times), shown 0..100%.
inline const KnobScale& reverbSize() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 100.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }();
  return s;
}
// Width: 0..1 (stereo width, 0 mono, 1 wide), shown 0..100%.
inline const KnobScale& reverbWidth() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 100.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }();
  return s;
}
// Springs (Spring mode Sig A): stepped 1..6 line count, stored normalised 0..1
// so `toStored` is the identity (the tile writes the stored fraction straight
// into the block param). Noon (knob 0.5) = Springs 3 (index 2). Mirrors the
// delay `Heads` split without toStored.
inline const KnobScale& springs() {
  static const KnobScale s = [] {
    // 6 detents: stored 0..1 snapped to 0, 1/5 .. 1 -> spring count 1..6.
    const auto index = [](double n) {
      return juce::jlimit(0, 5, juce::roundToInt(juce::jlimit(0.0, 1.0, n) * 5.0));
    };
    KnobScale c;
    c.toDisplay = [index](double n) { return 1.0 + (double)index(n); };
    // Snap typed values to the nearest detent (count -> stored 0..1).
    c.fromDisplay = [](double d) {
      return (d <= 1.5 ? 0.0 : d <= 2.5 ? 0.2 : d <= 3.5 ? 0.4 : d <= 4.5 ? 0.6 : d <= 5.5 ? 0.8 : 1.0);
    };
    c.toStored = [](double n) { return n; };
    c.fromStored = [](double d) { return d; };
    c.format = [index](double n) {
      const int v = 1 + index(n);
      return juce::String(v) + (v == 1 ? " spring" : " springs");
    };
    c.editText = [index](double n) { return juce::String(1 + index(n)); };
    c.steps = 6;  // the knob clicks between the six counts
    return c;
  }();
  return s;
}
// Stored 0..1 fraction (width/spread, or an effect signature amount), shown
// 0..100%. toDisplay MUST stay the identity: the tile writes toDisplay(v)
// straight into the block param, and these params are stored 0..1 and clamped
// there -- percent() would write x100 (knob at 50% -> 50 -> clamped to 1.0),
// i.e. the "Width changes sound then resets" snap-back (2026-10-06: the
// tremolo/delay Width knobs were on percent() until fixed).
inline const KnobScale& fraction01() {
  static const KnobScale s = [] {
    KnobScale c;
    c.toDisplay = [](double n) { return n; };
    c.fromDisplay = [](double d) { return d; };
    c.format = [](double n) { return juce::String(juce::roundToInt(n * 100.0)) + "%"; };
    c.editText = [](double n) { return juce::String(juce::roundToInt(n * 100.0)); };
    return c;
  }();
  return s;
}
// Tape delay signature: 4 detents = 1..4 heads (normalised 0, 1/3, 2/3, 1),
// the same map as Delay::headsFromNormalized -- the knob readout and the DSP
// must name the same head count.
inline const KnobScale& delayHeads() {
  static const KnobScale s = [] {
    KnobScale c;
    static constexpr int kHeads[4] = {1, 2, 3, 4};
    const auto index =
        [](double n) { return juce::jlimit(0, 3, juce::roundToInt(juce::jlimit(0.0, 1.0, n) * 3)); };
    c.toDisplay = [index](double n) { return kHeads[index(n)]; };
    // Snaps typed values to the nearest detent.
    c.fromDisplay = [](double d) { return d <= 1.5 ? 0.0 : d <= 2.5 ? 1.0 / 3 : d <= 3.5 ? 2.0 / 3 : 1.0; };
    // STORAGE is the normalised 0..1 fraction the engine clamps to (kMinSig..
    // kMaxSig) -- NOT the head count shown above. Without this split the tile
    // wrote toDisplay(v) (1..4) into a 0..1 param -> clamped to 1.0 -> the knob
    // snapped back to max on resync (the 2026-10-06 Heads glitch).
    c.toStored = [](double n) { return n; };
    c.fromStored = [](double d) { return d; };
    c.format = [index](double n) { return juce::String(kHeads[index(n)]); };
    c.editText = [index](double n) { return juce::String(kHeads[index(n)]); };
    c.steps = 4;  // the knob clicks between the four head counts
    return c;
  }();
  return s;
}

}  // namespace scales
}  // namespace t3k::ui
