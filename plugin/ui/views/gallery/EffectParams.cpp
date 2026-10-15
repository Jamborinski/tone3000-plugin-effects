// The per-kind effect param table (declared in EffectParams.h). Hoisted
// verbatim from EffectTile.cpp's anonymous namespace (2026-10-17, node-view
// ticket) so the tile face and the node strip share one table and can never
// drift apart.
#include "EffectParams.h"

namespace t3k::ui {

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
    // Full 6x2 surface (FogConvolver-2 parity): 12 knobs = the fixed
    // Mix/In/Out trio + these nine, in slot order (A..E, sig, mod, F, G).
    // "Dry" is the DRY-path level (a gain on the dry term of the Mix crossfade);
    // the shared In knob is re-labelled "Dwell" on the conv tile (wet input gain).
    const std::vector<EffectParams> full = {
        {&scales::convPitch(), "convPitch", "Time", b.convPitch, help::Key::convPitch},
        {&scales::convPre(), "convPreMs", "Pre", b.convPreMs, help::Key::convPre},
        {&scales::convFade(), "convFadeIn", "F In", b.convFadeIn, help::Key::convFadeIn},
        {&scales::convFade(), "convFadeOut", "F Out", b.convFadeOut, help::Key::convFadeOut},
        {&scales::convTone(), "convToneDb", "Tone", b.convToneDb, help::Key::convTone},
        {&scales::convGain(), "convDry", "Dry", b.convDry, help::Key::convDry},
        {&scales::convWidth(), "convWidth", "Width", b.convWidth, help::Key::convWidth},
        {&scales::convFade(), "convInCurve", "InCrv", b.convInCurve, help::Key::convInCurve},
        {&scales::convFade(), "convOutCurve", "OutCrv", b.convOutCurve, help::Key::convOutCurve},
    };
    if (compact) {
      // 4x2: Mix/Length/Pre/Tone over Dwell/Dry/Width/Out.
      const int sel[5] = {0, 1, 4, 5, 6};
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
