// A built-in effect slot (Delay / Chorus / Tremolo) as a gallery tile: a dark
// surface with the effect's identity, its knobs (Mix plus the effect's params),
// and power + remove chrome. Delay carries a right-aligned "Sync (BPM)" toggle
// that switches the Time knob between free ms and BPM + subdivision (note)
// mode, greying out whichever knobs don't apply to the current mode. The knobs
// drive the block's params live and resync from the chain the way BlockCard's
// In / Out / Mix do.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "GalleryTile.h"
#include "KernelPlot.h"
#include "core/KnobScale.h"
#include "Delay.h"
#include "model/ChainState.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/Knob.h"

namespace t3k::ui {

class EffectTile : public GalleryTile {
 public:
  JUCE_DECLARE_WEAK_REFERENCEABLE(EffectTile)
 public:
  EffectTile(Services& services, const ChainItem& block, int size);

  // A fresh snapshot of the same block (resync).
  void setBlock(const ChainItem& block);
  const ChainItem& block() const { return block_; }

  void paint(juce::Graphics& g) override;
  void resized() override;

 protected:
  void open() override;
  std::vector<ContextMenu::Item> menuItems() override;
  // Pull the currently-serving kernel's strip data (empty before any load).
  void pullConvPreview();

 private:
  void syncKnobs();
  void togglePower();
  void applySync();
  void updateSyncLabel();
  // Delay: switch to mode m (combo / compact cycle) and land on
  // that mode's signature starting point (plugin/docs/delay-modes.md).
  void enterDelayMode(int m);
  // Delay: refresh the mode combo + signature knob (scale/label/steps/value)
  // + help from block_.
  void syncDelayMode();
  // Reverb: switch to character mode m (combo / compact cycle) and land on
  // that mode's dials + signature starting points (docs/reverb-modes.md).
  void enterReverbMode(int m);
  void enterReverbType(int mode, int type);
  void syncReverbTypeButton(int mode);
  // Reverb: refresh the mode combo + sig knobs (scale/label/steps/value) +
  // the Size->Length label from block_.
  void syncReverbMode();
  // Compressor: switch to character mode m (combo selection / tight-tile
  // cycle button); loads that mode's default timing + threshold.
  void enterMode(int m);
  void syncCompSig(int m);  // bind the slot right of SC: KNEE (VCA) / CLIP (1-4)
  void syncCompTypeButton(int m);  // re-sync the per-mode TYPE cycler after a mode change
  void enterCompType(int type);

  // Convolution: the IR load picker (our-own, not copied from a tone tile --
  // a tone swap vs. a kernel install are different acts, so this is a plain
  // FileChooser here). On OK, installs the IR through the chain store and
  // flashes the toast on success/failure.
  void enterConvIrFile();
  void updateConvIrLabel();
  // Convolution: flip the full tile's settings view (grid <-> knob face).
  void setConvSettings(bool on);
  // Convolution: resync the settings-grid knobs from the chain state
  // (setBlock refresh; both views mirror the same stored params).
  void updateConvSettingsKnobs();
  // Convolution: the Export save-picker (same async-guard pattern as the IR
  // load picker); on OK, chain.exportConvIr writes the baked kernel WAV.
  void enterConvExport();
  // The settings-view child set (toggle, export, 7 knobs, note) for
  // show/hide flips.
  juce::Array<juce::Component*> convSettingsChildren();

  ChainItem block_;
  bool enabled_ = true;  // optimistic; native converges via the resync
  // Stereo lanes get the small (160-px) tile: a tighter single-header
  // layout, smaller knob faces, abbreviated toggle labels. Mono (224-px)
  // tiles are laid out exactly as before.
  bool compact_ = false;

  // The effect's param knobs. Delay: Time/BPM/Div/Fb/Damp (5). Chorus/Tremolo:
  // Rate/Depth/Spread-or-LFO (3). Each knob's scale maps its normalised value
  // to the stored real units; the readout is the scale's format().
  Knob mix_, input_, output_;
  Knob knobA_, knobB_, knobC_, knobD_, knobE_;
  // Extra grid slots (the convolver's 9-knob surface needs F/G; the full
  // reverb's 7-knob surface (R2: +Mod +Dry) needs F/G/H).
  Knob knobF_, knobG_, knobH_;
  // Waveform strip (Phase C): edited-kernel envelope + fade curves.
  // Full tile only.
  KernelPlot convPlot_;
  const KnobScale* scaleA_ = nullptr;
  const KnobScale* scaleB_ = nullptr;
  const KnobScale* scaleC_ = nullptr;
  const KnobScale* scaleD_ = nullptr;
  const KnobScale* scaleE_ = nullptr;
  juce::String paramA_, paramB_, paramC_, paramD_, paramE_;

  // The shared Mod knob is block-wide (one per delay), but the FIRST entry
  // into Mod mode lands it on its 35% starting point -- and that landing must
  // not clobber the Mod value the user dialed in while in the OTHER modes.
  // So: per mode, remember the Mod value live when that mode last was, and
  // restore it on reentry (35% only the very first time Mod mode is entered).
  std::array<double, Delay::kNumModes> modByMode_{};
  std::array<bool, Delay::kNumModes> modByModeHas_{};
  int numParams_ = 3;

  // Delay only: the right-aligned "Sync (BPM)" toggle + grey-out state.
  bool delaySynced_ = false;
  juce::TextButton syncToggle_;

  ChromeIconButton power_, remove_;

  // Compressor only: discrete character mode (VCA / Tube-STA / Opto-2A / FET / Vari-Mu).
  juce::ComboBox modeCombo_;

  // Compressor only: PUNCH parallel-blend toggle (sits right of the power button).
  juce::TextButton mbcToggle_;

  // Compressor only, TIGHT tiles: the character-mode cycle button that
  // replaces the mode combo (compact header has no room for the combo row);
  // clicks step to the next mode with that mode's defaults.
  juce::TextButton modeCycle_;

  // Reverb + compressor, TIGHT tiles: the per-mode TYPE cycler (a sub-model
  // within the mode -- a distinct character/law set, not a preset;
  // Reverb::numTypes / Compressor::numTypes). Sits right of the mode cycler;
  // each mode remembers its own type (block_.reverbType0..5 / block_.compType,
  // the sig-family precedent). reverb-modes.md.
  juce::TextButton typeCycle_;

  // Convolution only: the IR load control + the loaded kernel's readout
  // (name + seconds). Not a KnobScale (there's no param to write); it just
  // surfaces the session IR and offers the picker.
  juce::TextButton convIrButton_;
  juce::Label convIrLabel_;
  juce::Label convIrSeconds_;  // E-2: the engine's real length, its own line under the name
  // The picker lives between the open and its async callback (launchAsync
  // is the only launch in this JUCE build -- no showOKDialog), so it is a
  // member, guarded exactly like LocalFiles' chooser.
  std::unique_ptr<juce::FileChooser> convIrChooser_;

  // Convolution only, FULL tile: the IR-settings view. The Settings toggle
  // swaps the plot strip + main knob grid for a 7-knob settings grid
  // (Start / End trim + Pre / fades / curves -- mirror views over the same
  // stored params the face writes, so both stay in sync for free) and an
  // Export button (baked kernel -> bare-RIFF WAV, default 24-bit/48 kHz).
  // Toggling back resyncs the main knobs from the chain. Compact tiles
  // (160 px) keep the face only; Export reaches them via the strip.
  bool convSettings_ = false;
  juce::TextButton convSettingsToggle_;
  juce::TextButton convExportButton_;
  Knob convStartKnob_, convEndKnob_, convPreKnob_;
  Knob convFadeInKnob_, convFadeOutKnob_;
  Knob convInCurveKnob_, convOutCurveKnob_;
  juce::Label convExportNote_;
  bool convExporting_ = false;
  std::unique_ptr<juce::FileChooser> convExportChooser_;

  // Delay only: the mode-set signature KNOB -- the mode's UNIQUE control.
  // PING/CHIP are continuous 0..100% (scales::fraction01), HEADS steps 1/2/3/4
  // and a real-Hz RATE face (scales::delayHeads / scales::modRateHz). In
  // MOD mode it carries the mode's unique RATE -- delayRateHz, stored REAL Hz
  // (0.5..30, scales::modRateHz; the classic 5 Hz = Delay::kRateDefaultHz is
  // exactly the pre-Rate law, so the stock sound is bit-identical) -- while
  // the shared Mod knob below carries the wobble's DEPTH; Mod is the only
  // mode with two signature dials (both on the full tile; the compact Mod
  // tile parks Rate behind the shared Mod, which keeps its slot rule). The
  // scale is swapped per mode in syncDelayMode; onChange resolves the
  // CURRENT mode's param + scale (sigScale_).
  Knob sigKnob_;
  const KnobScale* sigScale_ = nullptr;
  // Delay only: the SHARED Mod knob (delayMod) -- a modulation on the repeat
  // path live in EVERY mode (each mode's own law: classic vibrato on Digital/
  // Mod, a slow flutter drift on Tape, a deep spacey wobble on BBD, a mid
  // waver on MemGuy; Delay::modWobbleHz/Ms). 0 = a straight tap (bit-
  // identical to the pre-wobble read). On the full tile it sits at
  // In/Width/Mod/[unique]/Out; on the compact (4x2) tile it takes the
  // unique's slot only in Mod mode (depth is that mode's master -- 0 turns
  // the wobble off -- and the unique Rate parks behind it), else the unique
  // does and it hides.
  Knob modKnob_;
};

}  // namespace t3k::ui
