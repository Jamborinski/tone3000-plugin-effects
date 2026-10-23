// Effect-kind + subtype -> approved two-tone artwork (AudioIcons.h).
// Subtype ints come from ChainBlock; their code-side names are authoritative
// (Delay.h kNumModes names, Compressor.h modeName, Reverb.h modeName).
#pragma once

#include <ChainBlock.h>

#include "AudioIcons.h"

namespace t3k::ui {

// By block type: the non-effect faces. nullptr = no two-tone artwork (the
// caller falls back to its single-tone glyph).
inline const char* audioIconFor(ChainBlockType type) {
  if (type == ChainBlockType::NAM) return audio_icons::kAmp;
  if (type == ChainBlockType::IR) return audio_icons::kIrSpeaker;
  return nullptr;  // INSERT: none; EFFECT: use the EffectKind overload.
}

// By EffectKind + the subtype ints (ChainBlock's axis). Chorus/tremolo carry
// no subtype int, so they resolve to the family artwork until a mode field
// exists. Wave stays single-tone (kFxWave) by returning nullptr.
inline const char* audioIconFor(EffectKind kind, int delayMode, int compMode, int reverbMode) {
  if (kind == EffectKind::Delay) {
    // Delay.h: 0 Digital,1 Tape,2 BBD,3 Mod,4 Magnetic,5 MemGuy
    if (delayMode == 1) return audio_icons::kAnalogDelay;  // the tape-delay artwork
    if (delayMode == 2) return audio_icons::kBbd;
    if (delayMode == 3) return audio_icons::kModDelay;
    if (delayMode == 4 || delayMode == 5) return audio_icons::kMagEcho;
    return audio_icons::kDigitalDelay;
  }
  if (kind == EffectKind::Compressor) {
    // Compressor.h modeName: 1 Tube,2 Opto,3 FET,4 Vari-Mu, else VCA
    if (compMode == 1) return audio_icons::kTubeCompressor;
    if (compMode == 2) return audio_icons::kOptoCompressor;
    if (compMode == 3) return audio_icons::kFetCompressor;
    if (compMode == 4) return audio_icons::kVariMuCompressor;
    return audio_icons::kVcaCompressor;
  }
  if (kind == EffectKind::Reverb) {
    // Reverb.h modeName: 0 Plate,1 Spring,2 Room,3 Chamber,4 Hall,5 Digital
    if (reverbMode == 1) return audio_icons::kSpringReverb;
    if (reverbMode == 2) return audio_icons::kRoomReverb;
    if (reverbMode == 3) return audio_icons::kChamberReverb;
    if (reverbMode == 4) return audio_icons::kHallReverb;
    if (reverbMode == 5) return audio_icons::kDigitalReverb;
    return audio_icons::kPlateReverb;
  }
  if (kind == EffectKind::Chorus) return audio_icons::kChorus;
  if (kind == EffectKind::Tremolo) return audio_icons::kTremolo;
  if (kind == EffectKind::Convolution) return audio_icons::kConvolution;
  if (kind == EffectKind::Volume) return audio_icons::kVolume;
  return nullptr;
}

// By effectKind string (ChainItem's axis) + the same subtype ints.
inline const char* audioIconFor(const juce::String& effectKind, int delayMode, int compMode,
                                int reverbMode) {
  return audioIconFor(effectKindFromString(effectKind), delayMode, compMode, reverbMode);
}

// ChainBlock convenience: pulls the subtype ints off the block.
inline const char* audioIconFor(const ChainBlock& block) {
  return audioIconFor(block.effectKind, block.delayMode, block.compMode, block.reverbMode);
}

// Node-view (low-detail) variant of the same lookup: the *Node bodies from
// AudioIcons.h for the ~30px strip cells. Same mode maps as audioIconFor.
inline const char* audioNodeIconFor(EffectKind kind, int delayMode, int compMode,
                                    int reverbMode) {
  if (kind == EffectKind::Delay) {
    if (delayMode == 1) return audio_icons::kAnalogDelayNode;
    if (delayMode == 2) return audio_icons::kBbdNode;
    if (delayMode == 3) return audio_icons::kModDelayNode;
    if (delayMode == 4 || delayMode == 5) return audio_icons::kMagEchoNode;
    return audio_icons::kDigitalDelayNode;
  }
  if (kind == EffectKind::Compressor) {
    if (compMode == 1) return audio_icons::kTubeCompressorNode;
    if (compMode == 2) return audio_icons::kOptoCompressorNode;
    if (compMode == 3) return audio_icons::kFetCompressorNode;
    if (compMode == 4) return audio_icons::kVariMuCompressorNode;
    return audio_icons::kVcaCompressorNode;
  }
  if (kind == EffectKind::Reverb) {
    if (reverbMode == 1) return audio_icons::kSpringReverbNode;
    if (reverbMode == 2) return audio_icons::kRoomReverbNode;
    if (reverbMode == 3) return audio_icons::kChamberReverbNode;
    if (reverbMode == 4) return audio_icons::kHallReverbNode;
    if (reverbMode == 5) return audio_icons::kDigitalReverbNode;
    return audio_icons::kPlateReverbNode;
  }
  if (kind == EffectKind::Chorus) return audio_icons::kChorusNode;
  if (kind == EffectKind::Tremolo) return audio_icons::kTremoloNode;
  if (kind == EffectKind::Convolution) return audio_icons::kConvolutionNode;
  if (kind == EffectKind::Volume) return audio_icons::kVolumeNode;
  return nullptr;
}

inline const char* audioNodeIconFor(const juce::String& effectKind, int delayMode,
                                    int compMode, int reverbMode) {
  return audioNodeIconFor(effectKindFromString(effectKind), delayMode, compMode, reverbMode);
}

inline const char* audioNodeIconFor(const ChainBlock& block) {
  return audioNodeIconFor(block.effectKind, block.delayMode, block.compMode, block.reverbMode);
}

}  // namespace t3k::ui
