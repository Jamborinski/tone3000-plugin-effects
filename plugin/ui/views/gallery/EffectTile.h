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
#include "core/KnobScale.h"
#include "Delay.h"
#include "model/ChainState.h"
#include "widgets/ChromeIconButton.h"
#include "widgets/Knob.h"

namespace t3k::ui {

class EffectTile : public GalleryTile {
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
  // Reverb: refresh the mode combo + sig knobs (scale/label/steps/value) +
  // the Size->Length label from block_.
  void syncReverbMode();
  // Compressor: switch to character mode m (combo selection / tight-tile
  // cycle button); loads that mode's default timing + threshold.
  void enterMode(int m);
  void syncCompSig(int m);  // bind the slot right of SC: KNEE (VCA) / CLIP (1-4)

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
