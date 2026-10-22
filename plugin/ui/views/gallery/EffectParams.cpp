// The per-kind effect param table (declared in EffectParams.h). Hoisted
// verbatim from EffectTile.cpp's anonymous namespace (2026-10-17, node-view
// ticket) so the tile face and the node strip share one table and can never
// drift apart.
#include "EffectParams.h"

#include "effects/reverb/Reverb.h"

namespace t3k::ui {

// The reverb mode sig slots (hoisted verbatim from EffectTile's anonymous
// namespace on 2026-10-17 so the tile face and the node strip bind the SAME
// per-mode pair and can never drift apart: Plate = Air/Early, Spring =
// Springs/Sag, Room = Early/Air, Chamber = Volley/Bass, Hall = Build/Space,
// Digital = Density/Shape). All stored 0..1; Springs is the single stepped
// sig (1..6); Digital slot 1 = the 5-detent Shape.
// (plugin/docs/reverb-modes.md.)
const char* reverbSigParam(int mode, int localSlot) {
  static const char* p[Reverb::kNumModes][2] = {
      {"reverbAir", "reverbEarly"},  {"reverbSprings", "reverbSag"},  // 2026-10-17 PLT: sig A (row 1) = Air, sig B (row 2) = Early (Bright/Bloom off the face)
      {"reverbEarly", "reverbAir"},   {"reverbVolley", "reverbBass"},
      {"reverbBuild", "reverbSpace"}, {"reverbDensity", "reverbWave"}};
  return p[juce::jlimit(0, Reverb::kNumModes - 1, mode)][localSlot ? 1 : 0];
}
const char* reverbSigLabel(int mode, int localSlot) {
  static const char* n[Reverb::kNumModes][2] = {
      {"Air", "Early"},     {"Springs", "Sag"}, {"Early", "Air"},
      {"Volley", "Bass"}, {"Build", "Space"}, {"Density", "Shape"}};
  return n[juce::jlimit(0, Reverb::kNumModes - 1, mode)][localSlot ? 1 : 0];
}
const KnobScale* reverbSigScaleForMode(int mode, int localSlot) {
  const int m = juce::jlimit(0, Reverb::kNumModes - 1, mode);
  if (m == 1 && !localSlot)
    return &scales::springs();  // the only stepped sig (Springs 1..6, mode 1)
  if (m == 5 && localSlot)
    return &scales::reverbWave();  // R2: Digital Sig B is now the 5-detent Shape
  return &scales::fraction01();
}
help::Key reverbSigHelp(int mode, int localSlot) {
  static const help::Key k[Reverb::kNumModes][2] = {
      {help::Key::reverbAir, help::Key::reverbEarly},
      {help::Key::reverbSprings, help::Key::reverbSag},
      {help::Key::reverbEarly, help::Key::reverbAir},
      {help::Key::reverbVolley, help::Key::reverbBass},
      {help::Key::reverbBuild, help::Key::reverbSpace},
      {help::Key::reverbDensity, help::Key::reverbShape}};
  return k[juce::jlimit(0, Reverb::kNumModes - 1, mode)][localSlot ? 1 : 0];
}
// The mode's Sig A/B raw (0..1) block-field value for the display.
double reverbSigRaw(const ChainItem& b, int mode, int localSlot) {
  const int m = juce::jlimit(0, Reverb::kNumModes - 1, mode);
  const int s = localSlot ? 1 : 0;
  switch (m) {
    case 0: return s ? b.reverbEarly : b.reverbAir;     break;  // 2026-10-17 PLT face: slot0 (sigKnob_, row 1) = Air, slot1 (modKnob_, row 2) = Early
    case 1: return s ? b.reverbSag : b.reverbSprings;  break;
    case 2: return s ? b.reverbAir : b.reverbEarly;    break;
    case 3: return s ? b.reverbBass : b.reverbVolley;  break;
    case 4: return s ? b.reverbSpace : b.reverbBuild;  break;
    default: return s ? (double)b.reverbWave : b.reverbDensity; break;
  }
}
EffectParams reverbSigSlot(const ChainItem& b, int mode, int localSlot) {
  const int m = juce::jlimit(0, Reverb::kNumModes - 1, mode);
  return {reverbSigScaleForMode(m, localSlot), reverbSigParam(m, localSlot),
          reverbSigLabel(m, localSlot), reverbSigRaw(b, m, localSlot),
          reverbSigHelp(m, localSlot)};
}

std::vector<EffectParams> paramsFor(const ChainItem& b, bool compact) {
  const juce::String k = b.effectKind;
  if (k == "delay")
    return {
        {&scales::delayTimeMs(), "delayTimeMs", "Time", b.delayTimeMs, help::Key::effectTime},
        {&scales::delayBpm(), "delayBpm", "BPM", b.delayBpm, help::Key::effectBpm},
        {&scales::subdivision(), "delaySubdivision", "Div", (double)b.delaySubdivision,
         help::Key::effectSubdivision},
        {&scales::delayFeedback(), "delayFeedback", "Fb", b.delayFeedback,
         help::Key::effectFeedback},
        {&scales::fraction01(), "delaySpread", "Width", b.delaySpread,
         help::Key::delaySpread},  // stored 0..1: percent() (x100) wrote max and
                                   // snapped back on resync -- the reset glitch
        // Damp is PARKED (2026-10-06): engine + state stay intact (ProcessorChain
        // "delayDamping" handler); this slot can hold it again.
    };
  if (k == "tremolo")
    return {
        {&scales::tremoloRate(), "tremoloRateHz", "Rate", b.tremoloRateHz, help::Key::tremoloRate},
        {&scales::tremoloDepth(), "tremoloDepth", "Depth", b.tremoloDepth,
         help::Key::tremoloDepth},
        {&scales::chorusTone(), "tremoloTone", "Tone", b.tremoloTone,
         help::Key::tremoloTone},
        {&scales::tremoloLfo(), "tremoloWave", "Shape", (double)b.tremoloWave,
         help::Key::tremoloLfo},
        {&scales::fraction01(), "tremoloSpread", "Width", b.tremoloSpread,
         help::Key::tremoloSpread},  // stored 0..1: percent() (x100) wrote max and
                                     // snapped back on resync -- the reset glitch
    };
  if (k == "volume")
    return {
        {&scales::gainDb(), "volume", "Volume", b.volume, help::Key::volume},
    };
  if (k == "wave")
    return {
        {&scales::waveLevel(), "waveLevel", "Output", b.waveLevel, help::Key::waveLevel},
        {&scales::waveShape(), "waveShape", "Shape", (double)b.waveShape,
         help::Key::waveShape},
    };
  if (k == "compressor")
    return {
        {&scales::compRatio(), "compRatio", "Ratio", b.compRatio, help::Key::compRatio},
        {&scales::compAttack(), "compAttackMs", "Atk", b.compAttackMs,
         help::Key::compAttack},
        {&scales::compRelease(), "compReleaseMs", "Rel", b.compReleaseMs,
         help::Key::compRelease},
        // 5x2 face: Tone sits right of Release; Thresh + SC take row 2, and
        // the slot right of SC is the mode-dependent signature control:
        // KNEE for VCA, CLIP for the other four. syncCompSig() rebinds the
        // knob (scale/label/param/value) on every mode change and at build.
        // PUNCH is the header toggle.
        {&scales::compTone(), "compToneDb", "Tone", b.compToneDb, help::Key::compTone},
        {&scales::compThreshold(), "compThresholdDb", "Thresh", b.compThresholdDb, help::Key::compThreshold},
        {&scales::compScHp(), "compScHpHz", "SC", b.compScHpHz,
         help::Key::compScHp},
        (b.compMode == 0)
            ? EffectParams{&scales::compKnee(), "compKnee", "KNEE", b.compKnee,
                           help::Key::compKnee}
            : EffectParams{&scales::compClip(), "compClip", "CLIP", b.compClip,
                           help::Key::compClip},
    };
  if (k == "reverb")
    return {
        {&scales::reverbDecay(), "reverbDecayMs", "Decay", b.reverbDecayMs, help::Key::reverbDecay},
        {&scales::reverbPre(), "reverbPreMs", "Pre", b.reverbPreMs, help::Key::reverbPre},
        {&scales::reverbTone(), "reverbTone", "Tone", b.reverbTone, help::Key::reverbTone},
        {&scales::reverbSize(), "reverbSize", "Size", b.reverbSize, help::Key::reverbSize},
        {&scales::reverbWidth(), "reverbWidth", "Width", b.reverbWidth, help::Key::reverbWidth},
        // T-DAT-001 wave 3 (2026-10-17): the DAT Mod IS the shared post-wet
        // chorus layer (processSharedModSmear) -- params_.sharedMod, the
        // user-approved old-engine law (out = wet + amt*chorus, 6 incoherent
        // voices, tau-smoothed writes). The earlier in-loop LFO read
        // params_.mod and killed the wet (a tau there lowpassed the loop
        // itself); that path was removed. Same 0..1 scale + help key as
        // before (describes the Mod knob either way).
        {&scales::reverbSharedMod(), "reverbSharedMod", "Mod", b.reverbSharedMod, help::Key::reverbSharedMod},
        // 2026-10-17 PLT face v2: 7 base params (Decay, Pre, Tone, Size, Width,
        // Mod, LoCut) + 2 sig slots (Air/Early, bound via reverbSigParam).
        // Space dropped this round (engine-pinned at its 0.7 default) — LoCut
        // keeps its slot (knobF_ via knobAt) and gets its own gentle-then-
        // ramped taper (subtle over most of the sweep, ramps up near the top).
        {&scales::loCutTaper(), "reverbLoCut", "LoCut", b.reverbLoCut, help::Key::reverbLoCut},
    };
  if (k == "convolution") {
    // Full 7x2 surface: 12 knobs = the fixed Mix/Dwell/Out trio + these nine,
    // in slot order (A..E, sig, mod, F, G) matching the face rows:
    //   row 1: Mix  Dwell  Dry  Time  Pre  Start  End
    //   row 2: Width Mod  Out  F In  F Out
    // Start/End join the face (their old settings-view home is gone);
    // InCrv/OutCrv moved out of the face into the F In / F Out right-click
    // curve bands. "Dry" is the DRY-path level; the shared In knob is
    // re-labelled "Dwell" on the conv tile (wet input gain, +-18 dB window).
    const std::vector<EffectParams> full = {
        {&scales::convGain(), "convDry", "Dry", b.convDry, help::Key::convDry},
        {&scales::convPitch(), "convPitch", "Time", b.convPitch, help::Key::convPitch},
        {&scales::convPre(), "convPreMs", "Pre", b.convPreMs, help::Key::convPre},
        {&scales::convTrimScale(b.convRawSeconds), "convStartS", "Start",
         b.convStartS, help::Key::convStartS},
        {&scales::convTrimScale(b.convRawSeconds), "convEndS", "End",
         b.convEndS, help::Key::convEndS},
        {&scales::convWidth(), "convWidth", "Width", b.convWidth, help::Key::convWidth},
        {&scales::convMod(), "convMod", "Mod", b.convMod, help::Key::convMod},
        {&scales::convFade(), "convFadeIn", "F In", b.convFadeIn, help::Key::convFadeIn},
        {&scales::convFade(), "convFadeOut", "F Out", b.convFadeOut, help::Key::convFadeOut},
    };
    if (compact) {
      // 4x2: Time/Pre/Mod over Dwell/Width.
      const int sel[5] = {1, 2, 6, 0, 5};
      std::vector<EffectParams> c;
      for (int j = 0; j < 5; ++j) c.push_back(full[sel[j]]);
      return c;
    }
    return full;
  }
  return {
      {&scales::chorusRateHz(), "chorusRateHz", "Rate", b.chorusRateHz, help::Key::effectRate},
      {&scales::chorusDepthMs(), "chorusDepthMs", "Depth", b.chorusDepthMs,
       help::Key::effectDepth},
      {&scales::chorusTone(), "chorusTone", "Tone", b.chorusTone,
       help::Key::effectTone},
      {&scales::chorusWave(), "chorusWave", "Shape", (double)b.chorusWave,
       help::Key::effectWave},
      {&scales::chorusSpread(), "chorusSpread", "Width", b.chorusSpread,
       help::Key::effectSpread},
  };
}

}  // namespace t3k::ui
