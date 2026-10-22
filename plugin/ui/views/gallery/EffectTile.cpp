#include "EffectTile.h"
#include "KernelPlot.h"

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "GalleryGeometry.h"
#include "EffectParams.h"
#include "Compressor.h"
#include "Reverb.h"

namespace t3k::ui {

namespace {

// A labelled, help-tipped secondary knob (BlockCard builds In / Out / Mix the
// same way).
// Tight (stereo) tiles use a smaller knob face so two rows fit under the
// single header line.
inline constexpr int kCompactKnobFace = 32;

Knob::Options knob(const juce::String& label, const KnobScale& scale, float defaultValue,
                   help::Key help, int faceSize = theme::kKnobSizeSecondary) {
  Knob::Options o;
  o.label = label;
  o.size = faceSize;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = defaultValue;
  o.help = help;
  o.labelOnTop = true;
  // One notch smaller type on the tight tile: at 14-px "Shape"/"Width" touch
  // in 38-px columns (verified in build 095503); at 12-px both clear. Mono
  // tiles (36-px face) keep the 14-px default.
  if (faceSize != theme::kKnobSizeSecondary) o.labelSize = 12.0f;
  return o;
}

juce::String compactModeName(int compMode) {
  static const char* names[5] = {"VCA", "TUB", "OP", "VM", "FET"};  // VCA / Tube / Opto / Vari-Mu / FET (compact)
  return names[juce::jlimit(0, 4, compMode)];
}

// The compressor's per-mode TYPE faces (a sub-model within a mode, mirrored
// from the reverb type axis; Compressor::numTypes owns the count, today 1 per
// mode). Type 0 = that mode's own engine (VCA/OPT/TUB/VMU/FET). A second type
// is a new row per mode (id + blurb + engine branch + count bump).
static const char* compactCompTypeName(int m, int t) {
  // ENGINE-index order (0=VCA 1=Tube 2=Opto 3=FET 4=Vari-Mu) -- NOT the mode
  // cycler's display order, which puts Vari-Mu before FET.
  static const char* self[Compressor::kNumModes] = {"VCA", "TUB", "OPT", "FET", "VMU"};
  (void)t;  // one self-type per mode today
  return self[juce::jlimit(0, Compressor::kNumModes - 1, m)];
}
static const char* compTypeBlurb(int m, int t) {
  (void)t;
  static const char* b[Compressor::kNumModes] = {
      "general-purpose VCA -- smooth RMS soft-knee compression",
      "Tube-STA -- warm tube detector, the output-stage saturation",
      "Opto-2A -- two-stage optical cells, ratio-dependent slow attack",
      "FET -- four-amp sum, fast transient grab + stage coloration",
      "Vari-Mu 670 -- release-position ladder, feedback detection, odd-only crunch"};
  return b[juce::jlimit(0, Compressor::kNumModes - 1, m)];
}
// The block's remembered type for the compressor (a single per-block field,
// the compMode precedent).
static int compTypeOf(const ChainItem& b) {
  return b.compType;
}

// The delay's five modes, compressed for the compact-tile cycle button.
juce::String compactDelayModeName(int m) {
  static const char* names[6] = {"DIG", "TAPE", "BBD", "MOD", "MAG", "MEM"};
  return names[juce::jlimit(0, 5, m)];
}

// The delay mode's signature param key (the block field carries the value).
const char* delaySigParam(int m) {
  static const char* p[6] = {"delayPing", "delayHeads", "delayChip", "delayRateHz",
                             "delayMagRateHz", "delayMmRateHz"};
  return p[juce::jlimit(0, 5, m)];
}

// The signature knob's per-mode face: label, held value scale, help key.
// PING/CHIP are continuous 0..100% amounts stored 0..1 (fraction01 --
// the tile writes the scale's STORAGE value, knobToStored(scale, v), into the
// 0..1 param; a percent() storage mapping would blow it past the clamp).
// HEADS is the engine's own 4-detent map
// (Delay::headsFromNormalized): they store the
// normalised 0..1 fraction (toStored = identity) but DISPLAY the head count /
// semitones, so the two mappings differ -- that is exactly what the storage/
// display split in KnobScale exists for.
// The Mod-mode slot is now the unique RATE (Hz), not the shared Mod knob
// (depth): delayRateHz is a REAL-unit Hz store (the delayTimeMs/chorusRateHz
// class -- scales::modRateHz's display domain IS its stored domain, so the
// toStored/fromStored identity is correct and a resync reads it back through
// fromDisplay, the round-trip the snap-back split exists for).
const char* delaySigLabel(int m) {
  static const char* names[6] = {"Ping", "Heads", "Chip", "Rate", "Wobble", "Rate"};
  return names[juce::jlimit(0, 5, m)];
}
const KnobScale* delaySigScaleForMode(int m) {
  switch (juce::jlimit(0, 5, m)) {
    case 1:
      return &scales::delayHeads();
    case 3:
      return &scales::modRateHz();  // Mod: real-Hz log face (default 5 Hz)
    case 4:
      return &scales::modRateHz();  // Magnetic: WOBBLE-rate law (slow = wow, fast = flutter)
    case 5:
      return &scales::modRateHz();  // MemGuy: RATE law (chorus<->vibrato)
    default:
      return &scales::fraction01();  // Ping / Chip: continuous 0..100%
  }
}
help::Key delaySigHelp(int m) {
  static const help::Key keys[6] = {help::Key::delayPing, help::Key::delayHeads,
                                    help::Key::delayChip, help::Key::delayRate,
                                    help::Key::delayRate, help::Key::delayRate};
  return keys[juce::jlimit(0, 5, m)];
}

// ---- Reverb mode sig slots: (mode, localSlot 0=Sig A/1=B) -> param/scale/
// label/help. Plate Bright/Bloom, Spring Springs/Sag, Room Early/Air,
// Chamber Volley/Bass, Hall Build/Space, Digital Density/Mod. All stored 0..1;
// Springs is the single stepped (1..6) sig. (plugin/docs/reverb-modes.md.)
// The bodies now live in EffectParams.cpp (shared with the node strip —
// EffectParams.h); EffectTile keeps using them from there.
static juce::String compactReverbModeName(int m) {
  static const char* n[6] = {"DAT", "SPR", "RM", "CHM", "HALL", "DIG"};
  return n[juce::jlimit(0, 5, m)];
}
// ---- Reverb TYPE faces (a sub-model within a mode; Reverb::numTypes owns
// the count, today 1 per mode). Type 0 = the mode's MODELED character --
// the blurb describes that tonal identity in words ONLY (never the modeled
// product name). A second type = a new row per mode (id + blurb + engine
// branch + count bump; the plate pass is the reference implementation).
static const char* compactReverbTypeName(int m, int t) {
  // The mode's TYPE face is the subtype's own short name. Mode 5 (DIG) owns
  // six character subtypes (DIG/SPR/PLT/SML/CHM/HAL); the other modes each
  // carry their own single self-type (same name as the mode today).
  static const char* digTypes[6] = {"DIG", "SPR", "PLT", "SML", "CHM", "HAL"};
  if (m == 5) return digTypes[juce::jlimit(0, 5, t)];
  static const char* self[Reverb::kNumModes] = {"DAT", "SPR", "SML", "CHM", "HAL", "DIG"};
  return self[juce::jlimit(0, Reverb::kNumModes - 1, m)];
}
static const char* reverbTypeBlurb(int m, int t) {
  static const char* b[Reverb::kNumModes] = {
      "dense flat-plate body -- deep low-mid sustain, bright early slap, huge smooth tail",
      "single-can spring tank -- crisp metallic boing, high bite, long shimmer tail",
      "small room -- fast early set, short decay, drier than it looks",
      "open stone chamber -- tight body, long bright tail, LF blooming off",
      "concert hall -- slow to build, long and wide, low end hangs"};
  static const char* digBlurb[6] = {
      "clean comb field -- flat to the top, dense, even repeats (digital)",
      "single-can spring tank -- crisp metallic boing, high bite, long shimmer tail",
      "dense flat-plate body -- deep low-mid sustain, bright early slap, huge smooth tail",
      "small room -- fast early set, short decay, drier than it looks",
      "open stone chamber -- tight body, long bright tail, LF blooming off",
      "concert hall -- slow to build, long and wide, low end hangs"};
  if (m == 5) return digBlurb[juce::jlimit(0, 5, t)];
  return b[juce::jlimit(0, Reverb::kNumModes - 1, m)];
}
// The block's remembered type for a mode (per-mode field, the sig pattern).
static int reverbTypeOf(const ChainItem& b, int m) {
  switch (juce::jlimit(0, Reverb::kNumModes - 1, m)) {
    case 0: return b.reverbType0;
    case 1: return b.reverbType1;
    case 2: return b.reverbType2;
    case 3: return b.reverbType3;
    case 4: return b.reverbType4;
    default: return b.reverbType5;
  }
}

}  // namespace

EffectTile::EffectTile(Services& services, const ChainItem& block, int size)
    : GalleryTile(services, block.blockId, size),
      block_(block),
      delaySynced_(block.effectKind == "delay" && block.delaySynced),
      numParams_(block.effectKind == "compressor" ? 7
                 : (block.effectKind == "convolution"
                    ? ((size <= gallery::kStereoTileSize) ? 5 : 9)
                    : (block.effectKind == "reverb"
                       ? ((size <= gallery::kStereoTileSize) ? 5 : 7) // 2026-10-17 PLT face v2: 7 base params (A,B,C,D,E,H,F incl. LoCut; Space off the face, engine-pinned) + 2 sig slots (Air/Early, bound via reverbSigParam)
                       : (block.effectKind == "delay" ||
                          block.effectKind == "chorus" ||
                          block.effectKind == "tremolo" ? 5
                          : (block.effectKind == "wave" ? 2 : (block.effectKind == "volume" ? 1 : 3)))))),
      compact_(size <= gallery::kStereoTileSize),
      mix_(knob("Mix", scales::percent(), 1.0f, help::Key::effectMix,
                (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      input_(knob("In", scales::gainDb(), 0.5f, help::Key::effectInput,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      output_(knob("Out", scales::gainDb(), 0.5f, help::Key::effectOutput,
                   (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobA_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobB_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobC_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobD_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobE_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobF_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobG_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      knobH_(knob("P", scales::percent(), 0.5f, help::Key::effectMix,
                  (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      power_(Icon::Power, ChromeIconButton::Tone::power, help::Key::blockPower),
      remove_(Icon::Trash2, ChromeIconButton::Tone::plain, help::Key::removeBlock),
      // Delay only (built here like the P placeholders; the delay branch
      // wires it and syncDelayMode swaps in the current mode's identity):
      sigKnob_(knob("Sig", scales::fraction01(), 0.0f, help::Key::delayPing,
                    (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)),
      // Delay only: the SHARED Mod knob (delayMod) -- a modulation on the
      // repeat path in EVERY mode (each mode's own law, Delay::modWobbleHz/Ms).
      // Neutral 0 = a straight tap (bit-identical read); in Mod mode (3) it
      // IS the mode's signature control.
      modKnob_(knob("Mod", scales::fraction01(), 0.0f, help::Key::delayMod,
                    (compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary)) {
  // Conv tile: the shared In knob reads as "Dwell" on this block, on the
  // +-18 dB conv window (stored law stays 0..1, 0.5 = 0 dB, like everywhere).
  if (block_.effectKind == "convolution") {
    input_.setLabel("Dwell");
    input_.setScale(&scales::convDwell());
    input_.setHelp(help::Key::reverbDwell);
  }
  mix_.onChange = [this](float v) {
    this->services().chain.setBlockParam(blockId(), "mix", (double)v);
  };
  input_.onChange = [this](float v) {
    // Every block's "In" knob is a plain input/drive gain (the compressor's
    // threshold now has its own Thresh param knob).
    this->services().chain.setBlockParam(blockId(), "inputGain", (double)v);
  };
  output_.onChange = [this](float v) {
    this->services().chain.setBlockParam(blockId(), "outputGain", (double)v);
  };

  Knob* knobs[5] = {&knobA_, &knobB_, &knobC_, &knobD_, &knobE_};
  const std::vector<EffectParams> ps = paramsFor(block_, compact_);
  auto knobAt = [=](int i) -> Knob* {
    // Reverb full tile: index 5 = shared Mod -> knobH_ (row 1, 6-col even
    // layout), index 6 = Dry -> knobF_ (row 2) -- NOT sigKnob_/modKnob_,
    // which carry the per-mode Sig A/B (wired in resized(), not the param list).
    if (block_.effectKind == "reverb" && !compact_ && i == 5)
      return &knobH_;  // Mod (row 1, right end)
    if (block_.effectKind == "reverb" && !compact_ && i == 6)
      return &knobF_;  // LoCut — 2026-10-17 PLT face (v2: same slot, Space dropped)
    return i < 5 ? knobs[i] : (i == 5 ? &sigKnob_ : (i == 6 ? &modKnob_
                    : (i == 7 ? &knobF_ : (i == 8 ? &knobG_ : &knobH_))));
  };
  for (int i = 0; i < numParams_ && i < (int)ps.size(); ++i) {
    Knob* k = knobAt(i);
    k->setScale(ps[i].scale);
    k->setSteps(ps[i].scale->steps);  // detent-click for stepped scales (Ratio/LFO/Div/PUNCH)
    k->setLabel(ps[i].label);
    k->setHelp(ps[i].help);
    k->setValue((float)knobFromStored(*ps[i].scale, ps[i].raw));
    k->onChange = [this, sc = ps[i].scale, param = ps[i].param](float v) {
      const double real = knobToStored(*sc, (double)v);
      this->services().chain.setBlockParam(blockId(), param, real);
      if (param == "delayBpm") {
        this->block_.delayBpm = real;
        this->updateSyncLabel();
      }
      if (param == "delaySubdivision")
        this->block_.delaySubdivision = (int)real;
    };
  }
  // Volume has 1 param, wave 2 — the A/B/C slots are only meaningful for the
  // 3+ param faces, so guard the reads (scaleA_/paramA_ & friends are
  // assigned-only legacy; nothing re-reads them).
  const bool hasA = ps.size() > 0, hasB = ps.size() > 1, hasC = ps.size() > 2;
  scaleA_ = hasA ? ps[0].scale : nullptr;
  scaleB_ = hasB ? ps[1].scale : nullptr;
  scaleC_ = hasC ? ps[2].scale : nullptr;
  scaleD_ = (numParams_ >= 4 && ps.size() > 3) ? ps[3].scale : nullptr;
  scaleE_ = (numParams_ >= 5 && ps.size() > 4) ? ps[4].scale : nullptr;
  paramA_ = hasA ? ps[0].param : juce::String();
  paramB_ = hasB ? ps[1].param : juce::String();
  paramC_ = hasC ? ps[2].param : juce::String();
  paramD_ = (ps.size() > 3) ? ps[3].param : juce::String();
  paramE_ = (ps.size() > 4) ? ps[4].param : juce::String();

  // Wave/Volume faces: NO Mix/In/Out trio (user m08349) — the mix plumbing
  // stays internal (both kinds default to 100% mix). Face = paramsFor knobs
  // only (wave: Output + Shape, volume: Volume), so the trio knobs are
  // built and sync'd but never added.
  const bool noTrioFace =
      (block_.effectKind == "wave" || block_.effectKind == "volume");
  if (!noTrioFace) {
    addAndMakeVisible(mix_);
    addAndMakeVisible(input_);
    // 2026-10-17 PLT face v2: Dwell/In re-shown in row 2 col 1 (always visible now).
    addAndMakeVisible(output_);
  }
  for (int i = 0; i < numParams_ && i < (int)ps.size(); ++i)
    addAndMakeVisible(*knobAt(i));  // comp row 2: SC + PUNCH ride sigKnob_/modKnob_
  // Waveform strip (Phase C): full conv tiles only. Refreshed on every
  // tile re-sync (the chain resync lands here after any rebuild/load).
  if (block_.effectKind == "convolution" && !compact_) {
    addAndMakeVisible(convPlot_);
    pullConvPreview();
  }

  // Power + remove chrome (declared but previously never shown / wired).
  addAndMakeVisible(power_);
  addAndMakeVisible(remove_);
  power_.setOn(enabled_);
  power_.onClick = [this] {
    enabled_ = !enabled_;
    this->services().chain.setBlockParam(blockId(), "enabled", enabled_);
    power_.setOn(enabled_);
    repaint();
  };
  remove_.onClick = [this] { this->services().chain.removeBlock(blockId()); };

  if (block_.effectKind == "compressor") {
    // The compressor's "In" knob is a plain input/drive gain; its "Out" knob is
    // the make-up gain. The threshold is its own Thresh param knob (slot 3 =
    // knobD_), whose Alt-click reset lands on the current mode's default.
    double defDb = -32.0;
    if (Compressor::defaultThresholdForMode(block_.compMode, defDb))
      knobE_.setDefaultValue((float)scales::compThreshold().fromDisplay(defDb));  // Thresh is knob E
    output_.setHelp(help::Key::compMakeup);
    input_.setHelp(help::Key::compIn);
    // Item id == mode + 1 (the handler reads getSelectedId() - 1; the engine
    // enum is 0=VCA 1=Tube 2=Opto 3=FET 4=Vari-Mu). Display order matches
    // the compact cycle button's order; the ids must follow the engine order.
    modeCombo_.addItem("VCA", 1);
    modeCombo_.addItem("Tube", 2);
    modeCombo_.addItem("Opto", 3);
    modeCombo_.addItem("Vari-Mu", 5);
    modeCombo_.addItem("FET", 4);
    modeCombo_.setSelectedId(juce::jlimit(0, 4, block_.compMode) + 1,
                             juce::dontSendNotification);
    modeCombo_.onChange = [this] {
      const int m = modeCombo_.getSelectedId() - 1;
      if (m < 0) return;
      this->enterMode(m);
    };
    modeCombo_.setHelpText(help::text(help::Key::compMode));
    addAndMakeVisible(modeCombo_);
    // Tight tiles: the header row has room for a short cycle button, not the
    // combo's own row. Keep the combo constructed (and authoritative on mono
    // tiles) but hidden here.
    modeCombo_.setVisible(!compact_);

    // PUNCH: the parallel-blend toggle (the compressor's icon slot is this button).
    mbcToggle_.setButtonText(compact_ ? "PCH" : "PUNCH");
    mbcToggle_.setClickingTogglesState(true);
    mbcToggle_.setToggleState(block_.compMbc, juce::dontSendNotification);
    mbcToggle_.setHelpText(help::text(help::Key::compMbc));
    mbcToggle_.onClick = [this] {
      block_.compMbc = mbcToggle_.getToggleState();
      this->services().chain.setBlockParam(blockId(), "compMbc",
                                           block_.compMbc ? 1.0 : 0.0);
    };
    addAndMakeVisible(mbcToggle_);
    syncCompSig(block_.compMode);  // slot right of SC: KNEE (VCA) / CLIP (rest)

    // The per-mode TYPE cycler, BOTH views: right of the mode cycler on the
    // compact tile; right of the mode SELECTOR on the full tile. It cycles
    // the mode's sub-models (Compressor::numTypes; 1 per mode today -- the
    // face is the scaffolding for the mode's other, differently-built types).
    const int ct0 = compTypeOf(block_);
    typeCycle_.setButtonText(compactCompTypeName(block_.compMode, ct0));
    typeCycle_.setHelpText(
        help::text(help::Key::compType) + " -- " + compTypeBlurb(block_.compMode, ct0) +
        " -- click to choose " + compactModeName(block_.compMode) + " type");
    typeCycle_.onClick = [this] {
      const int m = this->block_.compMode;
      const int cur = compTypeOf(this->block_);
      const int n = Compressor::numTypes(m);
      juce::PopupMenu menu;
      for (int i = 0; i < n; ++i)
        menu.addItem(i + 1, juce::String(compactCompTypeName(m, i)) +
                                juce::String(juce::CharPointer_UTF8(" \u2014 ")) + compTypeBlurb(m, i),
                     true, (i == cur));
      menu.showMenuAsync(
          juce::PopupMenu::Options().withTargetComponent(typeCycle_),
          [this] (int choice) {
            if (choice > 0)
              this->enterCompType(choice - 1);
          });
    };
    // Compact tile: hidden. The compact header has no free slot (28..62 is the
    // mode cycler, 65..95 the PCH toggle -- reverb's type cycler fits at 66
    // because reverb has no PCH), and with one self-type per mode the cycler
    // is a one-entry no-op until a mode gains a second type. Full tile only.
    typeCycle_.setVisible(!compact_);
    addAndMakeVisible(typeCycle_);

    if (compact_) {
      // Character mode as a cycle button: click steps to the next mode (and
      // its default timing/threshold), left to right: VCA -> STA -> 2A -> VM
      // -> FET -> VCA.
      modeCycle_.setButtonText(compactModeName(block_.compMode));
      modeCycle_.setHelpText(
          help::text(help::Key::compMode) + " -- click to switch to the next mode");
      modeCycle_.onClick = [this] { this->enterMode((this->block_.compMode + 1) % 5); };
      addAndMakeVisible(modeCycle_);
    }
  }

  if (block_.effectKind == "delay") {
    syncToggle_.setButtonText(compact_ ? "SNC" : "Sync");
    syncToggle_.setClickingTogglesState(true);
    syncToggle_.setToggleState(delaySynced_, juce::dontSendNotification);
    syncToggle_.onClick = [this] {
      delaySynced_ = syncToggle_.getToggleState();
      this->services().chain.setBlockParam(blockId(), "delaySynced",
                                           delaySynced_ ? 1.0 : 0.0);
      this->applySync();
    };
    addAndMakeVisible(syncToggle_);
    updateSyncLabel();
    // Five-character mode set (the Compressor pattern; design in
    // plugin/docs/delay-modes.md): the mode combo + the mode's ONE
    // signature control live in the free y=44 row of the full tile.
    for (int m = 0; m < Delay::kNumModes; ++m)
      modeCombo_.addItem(Delay::modeName(m), m + 1);
    modeCombo_.setSelectedId(juce::jlimit(0, Delay::kNumModes - 1, block_.delayMode) + 1,
                             juce::dontSendNotification);
    modeCombo_.onChange = [this] {
      const int m = modeCombo_.getSelectedId() - 1;
      if (m >= 0) this->enterDelayMode(m);
    };
    modeCombo_.setHelpText(help::text(help::Key::delayMode));
    addAndMakeVisible(modeCombo_);
    modeCombo_.setVisible(!compact_);

    // The mode's UNIQUE signature control: one KNOB. PING/CHIP are continuous
    // 0..100%, HEADS steps 1/2/3/4, RISE steps +0/+3/+7/+12; each mode's
    // value persists per param (delayPing/delayHeads/delayChip/delayMmRateHz),
    // only the slot swaps. Alt-click resets to the mode's starting point
    // (enterDelayMode sets the default). syncDelayMode below lands the
    // label/scale/steps/value on the current mode.
    sigScale_ = delaySigScaleForMode(block_.delayMode);
    sigKnob_.onChange = [this](float v) {
      const auto* sc = this->sigScale_ ? this->sigScale_ : delaySigScaleForMode(this->block_.delayMode);
      const double real = knobToStored(*sc, (double)v);
      this->services().chain.setBlockParam(blockId(), delaySigParam(this->block_.delayMode), real);
    };
    addAndMakeVisible(sigKnob_);

    // The SHARED Mod knob (delayMod): a modulation on the repeat path in
    // EVERY mode -- Digital/Mod the classic 5 Hz vibrato, Tape a slow flutter
    // drift, BBD a deep spacey wobble, MemGuy a mid-sweep chorus/vibrato waver
    // warble (Delay::modWobbleHz/Ms). 0 = a straight tap (bit-identical to
    // the pre-wobble read in every mode). In Mod mode (3) it IS the signature
    // control, so the unique sig knob hides there.
    modKnob_.onChange = [this](float v) {
      const double real = knobToStored(scales::fraction01(), (double)v);
      this->services().chain.setBlockParam(blockId(), "delayMod", real);
      this->block_.delayMod = real;
    };
    addAndMakeVisible(modKnob_);

    // Width (knobE_) hides on the compact (4x2) delay; the sig slot fills its
    // place there (the unique -- or the Mod knob, which is the sig in Mod
    // mode).
    knobE_.setVisible(!compact_);
    syncDelayMode();

    if (compact_) {
      // Header cycle button (the compressor precedent). The signature
      // control lives on the full tile's slot; the compact affordance
      // is a documented open item (plugin/docs/delay-modes.md).
      modeCycle_.setButtonText(compactDelayModeName(block_.delayMode));
      modeCycle_.setHelpText(help::text(help::Key::delayMode) +
                             " -- click to switch to the next mode");
      modeCycle_.onClick = [this] {
        this->enterDelayMode((this->block_.delayMode + 1) % Delay::kNumModes);
      };
      addAndMakeVisible(modeCycle_);
    }
  }

  if (block_.effectKind == "reverb") {
    // Reverb-only: surface the shared "In" (inputGain) knob as "Dwell" -- a
    // product-copy choice for this block. Only the label + tooltip change here
    // (Delay/Chorus/Compressor keep their "In" / input-level copy).
    input_.setLabel("Dwell");
    input_.setHelp(help::Key::reverbDwell);

    // Six-character mode set (the compressor/delay pattern; design in
    // plugin/docs/reverb-modes.md). Full tile: the mode combo + the mode's TWO
    // signature knobs (Sig A right of Pre in row 1, Sig B right of Width in
    // row 2). Compact: a mode-cycle button; Sig A stays (right of Decay),
    // Sig B + Pre hide.
    for (int m = 0; m < Reverb::kNumModes; ++m)
      modeCombo_.addItem(juce::String(Reverb::modeName(m).data()), m + 1);
    modeCombo_.setSelectedId(juce::jlimit(0, Reverb::kNumModes - 1, block_.reverbMode) + 1,
                             juce::dontSendNotification);
    modeCombo_.onChange = [this] {
      const int m = modeCombo_.getSelectedId() - 1;
      if (m >= 0) this->enterReverbMode(m);
    };
    modeCombo_.setHelpText(help::text(help::Key::reverbMode));
    addAndMakeVisible(modeCombo_);
    modeCombo_.setVisible(!compact_);

    // Sig A (sigKnob_) -- the mode's primary character. Mode-aware so the
    // write lands on the live mode's Sig A (density/springs/bright/...).
    sigKnob_.onChange = [this](float v) {
      const int ch = Reverb::typeLawMode(this->block_.reverbMode, reverbTypeOf(this->block_, this->block_.reverbMode));
      const auto* sc = reverbSigScaleForMode(ch, 0);
      const double real = knobToStored(*sc, (double)v);
      this->services().chain.setBlockParam(blockId(), reverbSigParam(ch, 0), real);
    };
    addAndMakeVisible(sigKnob_);

    // Sig B (modKnob_) -- the mode's secondary character (full tile only).
    modKnob_.onChange = [this](float v) {
      const int ch = Reverb::typeLawMode(this->block_.reverbMode, reverbTypeOf(this->block_, this->block_.reverbMode));
      const auto* sc = reverbSigScaleForMode(ch, 1);
      const double real = knobToStored(*sc, (double)v);
      this->services().chain.setBlockParam(blockId(), reverbSigParam(ch, 1), real);
    };
    addAndMakeVisible(modKnob_);

    syncReverbMode();

    // The per-mode TYPE cycler, BOTH views: right of the mode cycler on the
    // compact tile; right of the mode SELECTOR on the full tile. It cycles the
    // mode's sub-models (Reverb::numTypes; 1 per mode today -- the face is
    // the scaffolding for the mode's other, differently-built types).
    const int tm0 = block_.reverbMode;
    const int tt0 = reverbTypeOf(block_, tm0);
    typeCycle_.setButtonText(compactReverbTypeName(tm0, tt0));
    typeCycle_.setHelpText(help::text(help::Key::reverbType) + " -- " +
                           reverbTypeBlurb(tm0, tt0) +
                           " -- click to choose " +
                           juce::String(Reverb::modeName(tm0).data()) + " type");
    typeCycle_.onClick = [this] {
      const int m = this->block_.reverbMode;
      const int cur = reverbTypeOf(this->block_, m);
      const int n = Reverb::numTypes(m);
      juce::PopupMenu menu;
      for (int i = 0; i < n; ++i)
        menu.addItem(i + 1, juce::String(compactReverbTypeName(m, i)) + " — " + reverbTypeBlurb(m, i),
                     true, (i == cur));
      menu.showMenuAsync(
          juce::PopupMenu::Options().withTargetComponent(typeCycle_),
          [this, m] (int choice) {
            if (choice > 0)
              this->enterReverbType(m, choice - 1);
          });
    };
    addAndMakeVisible(typeCycle_);

    if (compact_) {
      modeCycle_.setButtonText(compactReverbModeName(block_.reverbMode));
      modeCycle_.setHelpText(help::text(help::Key::reverbMode) +
                             " -- click to switch to the next mode");
      modeCycle_.onClick = [this] {
        this->enterReverbMode((this->block_.reverbMode + 1) % Reverb::kNumModes);
      };
      addAndMakeVisible(modeCycle_);
    }
  }

  // Convolution only: the IR picker + the loaded kernel's readout. Our own
  // control set (not the tone-tile load menu) -- installing a kernel onto an
  // effect block is a different act from swapping a model tone, so a plain
  // FileChooser here, wired to the block's loadConvIr, and a live name/length
  // readout. The five knobs (Gain/Width/Start/End/Length) come from knobs().
  if (block_.effectKind == "convolution") {
    convIrButton_.setButtonText(block_.convIrName.isNotEmpty() ? "Change IR" : "Load IR");
    convIrButton_.setHelpText(help::text(help::Key::convIrLoad));
    convIrButton_.onClick = [this] { this->enterConvIrFile(); };
    addAndMakeVisible(convIrButton_);

    convIrLabel_.setFont(Fonts::sans(11));
    convIrLabel_.setColour(juce::Label::textColourId, theme::kGray);
    addAndMakeVisible(convIrLabel_);

    // E-2: the engine's real length gets its own line under the filename
    // (the NEVO plates are mis-labelled — a "5.0 s" file is ~10 s).
    convIrSeconds_.setFont(Fonts::sans(10));
    convIrSeconds_.setColour(juce::Label::textColourId, theme::kGray);
    addAndMakeVisible(convIrSeconds_);
    updateConvIrLabel();

    if (!compact_) {
      // Export bakes the linear stages (trim, stretch, fades, Pre, Gain,
      // Width) to a bare-RIFF WAV (default 24-bit / 48 kHz); Mod + the wet
      // clip are nonlinear, stay live, and are deliberately not flagged.
      convExportButton_.setButtonText("Export");
      convExportButton_.setHelpText(juce::String(juce::CharPointer_UTF8(
          "Export the baked kernel as WAV (default 24-bit / 48 kHz); the "
          "save dialog's filename is editable")));
      convExportButton_.onClick = [this] { this->enterConvExport(); };
      addAndMakeVisible(convExportButton_);

      // Curve bands (right-click / long-press F In / F Out): one-knob
      // popovers for the fade-curve params. The anchor knob is armed
      // (brand-yellow label) while its band is open (gate-deck pattern).
      curveInBand_ =
          std::make_unique<ConvCurveBand>("InCrv", scales::convFade(),
                                          help::Key::convInCurve);
      curveOutBand_ =
          std::make_unique<ConvCurveBand>("OutCrv", scales::convFade(),
                                          help::Key::convOutCurve);
      curveInBand_->knob().onChange = [this](float v) {
        this->services().chain.setBlockParam(
            this->blockId(), "convInCurve",
            knobToStored(scales::convFade(), (double)v));
      };
      curveOutBand_->knob().onChange = [this](float v) {
        this->services().chain.setBlockParam(
            this->blockId(), "convOutCurve",
            knobToStored(scales::convFade(), (double)v));
      };
      curveInBand_->onDismiss = [this] { knobF_.setArmed(false); };
      curveOutBand_->onDismiss = [this] { knobG_.setArmed(false); };
      // Resync the band knobs from the current block (open() re-reads).
      curveInBand_->knob().setValue(
          (float)knobFromStored(scales::convFade(), block_.convInCurve));
      curveOutBand_->knob().setValue(
          (float)knobFromStored(scales::convFade(), block_.convOutCurve));
      // Touch: long-press the anchor knob opens the band (same as the gate).
      knobF_.onLongPress = [this] { this->openConvCurveBand(&knobF_); };
      knobG_.onLongPress = [this] { this->openConvCurveBand(&knobG_); };
    }
  }

  const bool isDelay = block_.effectKind == "delay";
  const bool isTrem = block_.effectKind == "tremolo";
  const bool isComp = block_.effectKind == "compressor";
  const bool isReverb = block_.effectKind == "reverb";
  const bool isConv = block_.effectKind == "convolution";
  const bool isWave = block_.effectKind == "wave";
  const bool isVol = block_.effectKind == "volume";
  setTitle(isDelay ? "Delay"
           : isTrem ? "Tremolo" : (isComp ? "Compressor" : (isReverb ? "Reverb" : (isConv ? "Convolver" : (isWave ? "Wave" : (isVol ? "Volume" : "Chorus"))))));
  setHelpText(help::text(isDelay ? help::Key::effectDelay
                  : isTrem ? help::Key::effectTremolo
                  : isComp ? help::Key::effectCompressor
                  : isReverb ? help::Key::effectReverb
                  : isConv ? help::Key::effectConvolution
                  : isWave ? help::Key::effectWaveGen
                  : isVol ? help::Key::effectVolume
                           : help::Key::effectChorus));

  syncKnobs();
  applySync();
  resized();  // the base set the size before these children existed
}

void EffectTile::setBlock(const ChainItem& block) {
  block_ = block;
  enabled_ = block.params.enabled;
  power_.setOn(enabled_);
  if (block_.effectKind == "compressor")
    mbcToggle_.setToggleState(block_.compMbc, juce::dontSendNotification);
  if (block_.effectKind == "convolution") {
    updateConvIrLabel();  // pick up the loaded-kernel name / button caption
    // A re-hydrated IR (E-2: state carries path+length but never bytes) only
    // re-appears in the preview/strip if we pull it here — otherwise the
    // waveform goes stale until the user loads the file in the UI again.
    if (!compact_) {
      pullConvPreview();
      // Curve-band knobs mirror the stored params; refresh so an external
      // edit (other view / preset / strip) lands in the bands too.
      curveInBand_->knob().setValue(
          (float)knobFromStored(scales::convFade(), block_.convInCurve));
      curveOutBand_->knob().setValue(
          (float)knobFromStored(scales::convFade(), block_.convOutCurve));
    }
  }
  syncKnobs();
  applySync();
  updateSyncLabel();
  repaint();
}

void EffectTile::pullConvPreview() {
  if (block_.effectKind != "convolution") return;
  const juce::var v = services().chain.convPreview(blockId());
  auto* obj = v.getDynamicObject();
  const float fi = (float)block_.convFadeIn, fo = (float)block_.convFadeOut;
  const double ei = KernelPlot::fadeExponent(block_.convInCurve);
  const double eo = KernelPlot::fadeExponent(block_.convOutCurve);
  if (obj == nullptr) {
    convPlot_.clearEnvelope();
    convPlot_.setFades(fi, fo, ei, eo);
    return;
  }
  // NOTE: const locals keep the array pointers (into these vars) alive.
  const juce::var lVar = obj->getProperty("envL");
  const juce::var rVar = obj->getProperty("envR");
  std::vector<float> L, R;
  if (auto* a = lVar.getArray())
    for (int i = 0; i < a->size(); ++i)
      L.push_back((float)a->getReference(i));
  if (auto* a = rVar.getArray())
    for (int i = 0; i < a->size(); ++i)
      R.push_back((float)a->getReference(i));
  const double sr = std::max(1.0, (double)obj->getProperty("sampleRate"));
  convPlot_.setEnvelope(L, R, (double)obj->getProperty("length") / sr);
  convPlot_.setFades(fi, fo, ei, eo);
}

void EffectTile::open() {}  // built-in effects have no detail view

std::vector<ContextMenu::Item> EffectTile::menuItems() {
  std::vector<ContextMenu::Item> items{
      {"Power", Icon::Power, help::Key::blockPower, [this] { togglePower(); }},
      {"Remove", Icon::Trash2, help::Key::removeBlock,
       [this] { this->services().chain.removeBlock(blockId()); }},
  };
  return items;
}

void EffectTile::togglePower() {
  enabled_ = !enabled_;
  power_.setOn(enabled_);
  this->services().chain.setBlockParam(blockId(), "enabled", enabled_);
  repaint();
}

void EffectTile::syncKnobs() {
  mix_.setValue((float)block_.params.mix);
  // Every block's In knob is a plain (normalized) drive gain; the compressor's
  // threshold is a Thresh param knob, synced by the generic loop below.
  input_.setValue((float)block_.params.inputGain);
  output_.setValue((float)block_.params.outputGain);
  Knob* slots[10] = {&knobA_, &knobB_, &knobC_, &knobD_, &knobE_,
                    &sigKnob_, &modKnob_, &knobF_, &knobG_, &knobH_};
  // Slot order mirrors the constructor's knobAt: param i lives in slot i,
  // with i>=5 riding sig/mod/F/G/H (the compact conv's Dry/Width land there,
  // not in knobD_/E -- keep the two passes agreeing).
  const std::vector<EffectParams> ps = paramsFor(block_, compact_);
  for (int i = 0; i < numParams_ && i < (int)ps.size(); ++i) {
    // 2026-10-17 PLT face v2 (full tile): i=5 Mod->knobH_, i=6 LoCut->knobF_
    // (Space off the face); sig slots Air/Early bound separately via reverbSigParam.
    Knob* k = slots[i];
    if (block_.effectKind == "reverb" && !compact_) {
      if (i == 5) k = &knobH_;      // Mod
      else if (i == 6) k = &knobF_; // LoCut
    }
    k->setValue((float)knobFromStored(*ps[i].scale, ps[i].raw));
  }
  if (block_.effectKind == "compressor") {
    modeCombo_.setSelectedId(juce::jlimit(0, 4, block_.compMode) + 1,
                             juce::dontSendNotification);
    if (compact_)
      modeCycle_.setButtonText(compactModeName(block_.compMode));
    // Vari-Mu (670): the release dial travels the 670's real release ladder
    // (0.04 -> 25 s, see Compressor::recalc) -- speak the readout in true
    // seconds and say so in the label. Other modes keep the literal ms scale.
    const bool is670 = (block_.compMode == 4);
    knobC_.setScale(is670 ? &scales::compRelease670() : &scales::compRelease());
    knobC_.setLabel(is670 ? "Rel 670" : "Rel");
  }
  if (block_.effectKind == "delay")
    syncDelayMode();
}

void EffectTile::enterMode(int m) {
  if (m < 0 || m > 4) return;
  // Selecting a mode lands you on that type's characteristic attack/release
  // AND threshold as a starting point; dial from there. Update the model, push
  // it to the chain, then refresh the knobs to match.
  double atk = 0.0, rel = 0.0;
  if (Compressor::defaultTimingForMode(m, atk, rel)) {
    block_.compAttackMs = atk;
    block_.compReleaseMs = rel;
    services().chain.setBlockParam(blockId(), "compAttackMs", atk);
    services().chain.setBlockParam(blockId(), "compReleaseMs", rel);
  }
  double thr = 0.0;
  if (Compressor::defaultThresholdForMode(m, thr)) {
    block_.compThresholdDb = thr;
    services().chain.setBlockParam(blockId(), "compThresholdDb", thr);
    knobE_.setDefaultValue((float)scales::compThreshold().fromDisplay(thr));  // Thresh is knob E
  }
  services().chain.setBlockParam(blockId(), "compMode", (double)m);
  block_.compMode = m;
  // Land the character control at its NOON (the mode's unedited sound):
  // KNEE -> 6 dB (VCA), CLIP -> 100% (modes 1-4). Then rebind the slot
  // (scale/label/help/default) to the mode's knob.
  double clip = 1.0, knee = 6.0;
  if (Compressor::defaultClipForMode(m, clip)) {
    block_.compClip = clip;
    services().chain.setBlockParam(blockId(), "compClip", clip);
  }
  if (Compressor::defaultKneeForMode(m, knee)) {
    block_.compKnee = knee;
    services().chain.setBlockParam(blockId(), "compKnee", knee);
  }
  syncCompSig(m);
  syncCompTypeButton(m);
  syncKnobs();
}

void EffectTile::syncCompTypeButton(int m) {
  const int t = compTypeOf(block_);
  typeCycle_.setButtonText(compactCompTypeName(m, t));
  typeCycle_.setHelpText(
      help::text(help::Key::compType) + " -- " + compTypeBlurb(m, t) +
      " -- click to choose " + compactModeName(m) + " type");
}

void EffectTile::enterCompType(int type) {
  if (block_.effectKind != "compressor") return;
  const int m = block_.compMode;
  type = juce::jlimit(0, Compressor::numTypes(m) - 1, type);
  if (type == compTypeOf(block_)) return;
  block_.compType = type;  // state keeps the selection verbatim; the engine clamps
  services().chain.setBlockParam(blockId(), "compType", (double)type);
  syncCompTypeButton(m);
}

void EffectTile::enterDelayMode(int m) {
  if (m < 0 || m >= Delay::kNumModes) return;
  // Selecting a mode lands you on that signature control's starting
  // point (scaffold: neutral for all modes; each mode's ticket owns its
  // characteristic defaults), the same convention as enterMode's
  // per-mode A/R/threshold. The mode + sig are pushed to the chain; the
  // other sig fields keep their values (the slot just swaps, state
  // persists).
  const double sig = Delay::defaultSignatureForMode(m);
  const double realSig = (m == 3)    ? block_.delayRateHz
                        : (m == 4)   ? block_.delayMagRateHz
                        : (m == 5)   ? block_.delayMmRateHz
                                    : sig;
  switch (m) {
    case 0: block_.delayPing = sig; break;
    case 1: block_.delayHeads = sig; break;
    case 2: block_.delayChip = sig; break;
    case 3: block_.delayRateHz = Delay::kRateModDefaultHz; break;           // Mod RATE (1.5 Hz)
    case 4: block_.delayMagRateHz = Delay::kRateWobbleDefaultHz; break;     // Magnetic WOBBLE (1.0 Hz)
    case 5: block_.delayMmRateHz = Delay::kRateMmDefaultHz; break;          // MemGuy RATE (0.8 Hz)
  }
  services().chain.setBlockParam(blockId(), delaySigParam(m), realSig);
  // The shared Mod knob persists with the block like Time/Fb -- so entry
  // must NOT clobber the Mod the user set for the other modes (frequently
  // zero). Each mode remembers the Mod value that was live while it last
  // was, and reentry restores it; the 35% landing point still applies on
  // the very first entry to Mod mode, and alt-click always snaps to it.
  if (m != block_.delayMode) {
    const double cur = block_.delayMod;
    modByMode_[block_.delayMode] = cur;
    modByModeHas_[block_.delayMode] = true;
    const double next = modByModeHas_[m] ? modByMode_[m] : (m >= 3 ? sig : cur);
    if (next != cur) {
      block_.delayMod = next;
      services().chain.setBlockParam(blockId(), "delayMod", next);
    }
  }
  // Rate is a plain setting: entry never touches it, so the user's 2 Hz is
  // what comes back when they switch back.
  block_.delayMode = m;
  services().chain.setBlockParam(blockId(), "delayMode", (double)m);
  syncDelayMode();
  // Alt-click reset lands on the mode's starting point, on whichever knob
  // carries it (both dials in Mod mode, the unique sig knob elsewhere);
  // syncDelayMode just swapped sigScale_ to this mode's scale.
  if (m >= 3) {  // rate-law modes: Mod (3) / Magnetic (4) / MemGuy (5)
    modKnob_.setDefaultValue((float)knobFromStored(scales::fraction01(), sig));
    sigKnob_.setDefaultValue((float)knobFromStored(scales::modRateHz(),
                                (m == 4) ? Delay::kRateWobbleDefaultHz
                                 : (m == 5) ? Delay::kRateMmDefaultHz
                                              : Delay::kRateModDefaultHz));
  } else {
    sigKnob_.setDefaultValue((float)knobFromStored(*sigScale_, sig));
  }
}

void EffectTile::syncDelayMode() {
  if (block_.effectKind != "delay") return;
  const int m = juce::jlimit(0, Delay::kNumModes - 1, block_.delayMode);
  modeCombo_.setSelectedId(m + 1, juce::dontSendNotification);
  if (compact_)
    modeCycle_.setButtonText(compactDelayModeName(m));
  // Shared Mod knob (all modes): value from the shared delayMod field.
  modKnob_.setValue((float)knobFromStored(scales::fraction01(), block_.delayMod));
  // Every mode has a UNIQUE sig now (Mod mode's is the RATE -- delayRateHz,
  // real Hz), so the sig knob is always labelled/scaled/value-sync'd; the
  // compact tile simply PARKS it for Mod mode (below), where the shared Mod
  // keeps the slot -- the compact non-Mod tiles are unchanged.
  const double raw = (m == 0) ? block_.delayPing
    : (m == 1) ? block_.delayHeads
    : (m == 2) ? block_.delayChip
    : (m == 3) ? block_.delayRateHz
    : (m == 4) ? block_.delayMagRateHz
    : block_.delayMmRateHz;
  const auto* sc = delaySigScaleForMode(m);  // %/1..4; Mod/Mag/MemGuy: real Hz
  sigScale_ = sc;
  sigKnob_.setScale(sc);
  sigKnob_.setSteps(sc->steps);  // HEADS/RISE detent-click; %/Hz continuous
  sigKnob_.setLabel(delaySigLabel(m));
  sigKnob_.setHelp(delaySigHelp(m));
  sigKnob_.setValue((float)knobFromStored(*sc, raw));
  // Visibility: the full tile shows BOTH Mod-mode dials (Mod depth + Rate).
  // The compact Mod tile keeps its standing rule -- the shared Mod (depth,
  // the mode's master: 0 turns the wobble off) holds the slot and Rate
  // parks behind it; the other compact modes show their unique, as before.
  sigKnob_.setVisible(compact_ ? (m != 3) : true);
  modKnob_.setVisible(!compact_ || (m == 3));
  resized();  // the full-tile sig slot is filled for every mode now
}

void EffectTile::syncCompSig(int m) {
  if (block_.effectKind != "compressor") return;
  const bool vca = (m == 0);
  // The slot right of SC (modKnob_) is this mode's character: the VCA's
  // soft-knee width (dB), or the harmonic colouring depth (%) for modes 1-4.
  // Both sit at NOON = the mode's unedited default sound.
  const auto* sc = vca ? &scales::compKnee() : &scales::compClip();
  const double raw = vca ? block_.compKnee : block_.compClip;
  const char* param = vca ? "compKnee" : "compClip";
  modKnob_.setScale(sc);
  modKnob_.setSteps(sc->steps);
  modKnob_.setLabel(vca ? "KNEE" : "CLIP");
  modKnob_.setHelp(vca ? help::Key::compKnee : help::Key::compClip);
  modKnob_.setDefaultValue((float)knobFromStored(*sc, vca ? 6.0 : 1.0));
  modKnob_.setValue((float)knobFromStored(*sc, raw));
  modKnob_.onChange = [this, sc, param, vca](float v) {
    const double real = knobToStored(*sc, (double)v);
    const double lo = vca ? Compressor::kMinKneeDb : Compressor::kMinClip;
    const double hi = vca ? Compressor::kMaxKneeDb : Compressor::kMaxClip;
    const double clamped = juce::jlimit(lo, hi, real);
    if (param == "compKnee") this->block_.compKnee = clamped;
    else this->block_.compClip = clamped;
    this->services().chain.setBlockParam(blockId(), param, clamped);
  };
  modKnob_.setVisible(true);
}

void EffectTile::enterReverbMode(int m) {
  if (m < 0 || m >= Reverb::kNumModes) return;
  // Selecting a mode lands you on that mode's shared dials + its two
  // signatures as a starting point (the compressor enterMode contract), then
  // dial from there. Push the mode + the five dials + this mode's sigs to the
  // chain; the other modes' sigs keep their values (distinct, untouched
  // fields).
  double decay = 0.0, pre = 0.0, tone = 0.0, size = 0.0, width = 0.0;
  Reverb::defaultDialsForMode(m, decay, pre, tone, size, width);
  block_.reverbDecayMs = decay;  services().chain.setBlockParam(blockId(), "reverbDecayMs", decay);
  block_.reverbPreMs = pre;      services().chain.setBlockParam(blockId(), "reverbPreMs", pre);
  block_.reverbTone = tone;      services().chain.setBlockParam(blockId(), "reverbTone", tone);
  block_.reverbSize = size;      services().chain.setBlockParam(blockId(), "reverbSize", size);
  block_.reverbWidth = width;    services().chain.setBlockParam(blockId(), "reverbWidth", width);

  const double sigA = Reverb::defaultSigForMode(m, 0);  // 0 for Digital (the anchor)
  const double sigB = Reverb::defaultSigForMode(m, 1);
  services().chain.setBlockParam(blockId(), reverbSigParam(m, 0), sigA);
  services().chain.setBlockParam(blockId(), reverbSigParam(m, 1), sigB);
  block_.reverbMode = m;
  services().chain.setBlockParam(blockId(), "reverbMode", (double)m);
  syncReverbMode();

  // Alt-click lands on the mode's starting point: the five shared dials + both
  // signatures. syncReverbMode just set sigKnob_/modKnob_ to the mode's scale.
  knobA_.setDefaultValue((float)knobFromStored(scales::reverbDecay(), decay));
  knobB_.setDefaultValue((float)knobFromStored(scales::reverbPre(), pre));
  knobC_.setDefaultValue((float)knobFromStored(scales::reverbTone(), tone));
  knobD_.setDefaultValue((float)knobFromStored(scales::reverbSize(), size));
  knobE_.setDefaultValue((float)knobFromStored(scales::reverbWidth(), width));
  sigKnob_.setDefaultValue((float)knobFromStored(*reverbSigScaleForMode(m, 0), sigA));
  modKnob_.setDefaultValue((float)knobFromStored(*reverbSigScaleForMode(m, 1), sigB));
}

void EffectTile::syncReverbTypeButton(int m) {
  if (block_.effectKind != "reverb") return;
  m = juce::jlimit(0, Reverb::kNumModes - 1, m);
  const int t = reverbTypeOf(block_, m);
  typeCycle_.setButtonText(compactReverbTypeName(m, t));
  typeCycle_.setHelpText(help::text(help::Key::reverbType) + " -- " +
                         reverbTypeBlurb(m, t) + " -- click to choose " +
                         juce::String(Reverb::modeName(m).data()) + " type");
}

void EffectTile::enterReverbType(int mode, int type) {
  if (block_.effectKind != "reverb") return;
  const int m = juce::jlimit(0, Reverb::kNumModes - 1, mode);
  const int n = Reverb::numTypes(m);
  type = ((type % n) + n) % n;  // wrap within the mode's type set

  // A type is a SUB-MODEL, not a preset: the dials stay as the user left
  // them unless the type defines its own characteristic starting points
  // (defaultDialsForType -> true; none do in the scaffold).
  double decay = 0.0, pre = 0.0, tone = 0.0, size = 0.0, width = 0.0;
  if (Reverb::defaultDialsForType(m, type, decay, pre, tone, size, width)) {
    block_.reverbDecayMs = decay;  services().chain.setBlockParam(blockId(), "reverbDecayMs", decay);
    block_.reverbPreMs = pre;      services().chain.setBlockParam(blockId(), "reverbPreMs", pre);
    block_.reverbTone = tone;      services().chain.setBlockParam(blockId(), "reverbTone", tone);
    block_.reverbSize = size;      services().chain.setBlockParam(blockId(), "reverbSize", size);
    block_.reverbWidth = width;    services().chain.setBlockParam(blockId(), "reverbWidth", width);
    knobA_.setDefaultValue((float)knobFromStored(scales::reverbDecay(), decay));
    knobB_.setDefaultValue((float)knobFromStored(scales::reverbPre(), pre));
    knobC_.setDefaultValue((float)knobFromStored(scales::reverbTone(), tone));
    knobD_.setDefaultValue((float)knobFromStored(scales::reverbSize(), size));
    knobE_.setDefaultValue((float)knobFromStored(scales::reverbWidth(), width));

    // Land the CHARACTER's sig pair: 50 % for the mapped character's
    // non-DIG pair; the DIG-own Density/Mod pair is always cleared (so the
    // engine only routes through the selected character's law branch).
    const int lawChar = Reverb::typeLawMode(m, type);
    for (int chm = 0; chm < Reverb::kNumModes; ++chm) {
      // DIG-own (chm==5): always 0 (Mod off / Density off), regardless.
      if (chm == 5) {
        services().chain.setBlockParam(blockId(), reverbSigParam(5, 0), 0.0);
        services().chain.setBlockParam(blockId(), reverbSigParam(5, 1), 0.0);
        continue;
      }
      // Non-DIG characters: 50 % if this is the active character, else 0.
      const double v = (chm == lawChar) ? 0.5 : 0.0;
      services().chain.setBlockParam(blockId(), reverbSigParam(chm, 0), v);
      services().chain.setBlockParam(blockId(), reverbSigParam(chm, 1), v);
    }
    if (lawChar < 5) {  // non-DIG character: set the knob default values for alt-click
      const auto* sca = reverbSigScaleForMode(lawChar, 0);
      const auto* sb  = reverbSigScaleForMode(lawChar, 1);
      sigKnob_.setDefaultValue((float)knobFromStored(*sca, 0.5));
      modKnob_.setDefaultValue((float)knobFromStored(*sb, 0.5));
    }
  }

  // Remember the choice PER MODE (its own field, the sig-family pattern).
  switch (m) {
    case 0: block_.reverbType0 = type; break;
    case 1: block_.reverbType1 = type; break;
    case 2: block_.reverbType2 = type; break;
    case 3: block_.reverbType3 = type; break;
    case 4: block_.reverbType4 = type; break;
    default: block_.reverbType5 = type; break;
  }
  services().chain.setBlockParam(blockId(), ("reverbType" + juce::String(m)), (double)type);
  // Re-sync the sig knobs to the ACTIVE CHARACTER (mode 5's type arms a
  // different character's pair; other modes have the same label/scale).
  const int lawChar = Reverb::typeLawMode(m, type);
  const char* lblA = reverbSigLabel(lawChar, 0);
  const char* lblB = reverbSigLabel(lawChar, 1);
  const auto* sca = reverbSigScaleForMode(lawChar, 0);
  const auto* sb  = reverbSigScaleForMode(lawChar, 1);
  sigKnob_.setScale(sca);  sigKnob_.setLabel(lblA); sigKnob_.setHelp(reverbSigHelp(lawChar, 0));
  sigKnob_.setValue((float)knobFromStored(*sca, reverbSigRaw(block_, lawChar, 0)));
  modKnob_.setScale(sb);   modKnob_.setLabel(lblB); modKnob_.setHelp(reverbSigHelp(lawChar, 1));
  modKnob_.setValue((float)knobFromStored(*sb, reverbSigRaw(block_, lawChar, 1)));
  syncReverbTypeButton(m);
}

void EffectTile::syncReverbMode() {
  if (block_.effectKind != "reverb") return;
  const int m = juce::jlimit(0, Reverb::kNumModes - 1, block_.reverbMode);
  modeCombo_.setSelectedId(m + 1, juce::dontSendNotification);
  if (compact_)
    modeCycle_.setButtonText(compactReverbModeName(m));
  syncReverbTypeButton(m);  // the type face remembers its own per-mode choice (both views)

  // Sig A (sigKnob_, slot 0) + Sig B (modKnob_, slot 1) -- scale/label/steps/
  // value follow the ACTIVE CHARACTER (for mode 5 the type's law mode; all
  // other modes are their own character).
  const int lawChar = Reverb::typeLawMode(m, reverbTypeOf(block_, m));
  const char* lblA = reverbSigLabel(lawChar, 0);
  const auto* sca = reverbSigScaleForMode(lawChar, 0);
  sigKnob_.setScale(sca);
  sigKnob_.setSteps(sca->steps);
  sigKnob_.setLabel(lblA);
  sigKnob_.setHelp(reverbSigHelp(lawChar, 0));
  sigKnob_.setValue((float)knobFromStored(*sca, reverbSigRaw(block_, lawChar, 0)));
  const char* lblB = reverbSigLabel(lawChar, 1);
  const auto* sb = reverbSigScaleForMode(lawChar, 1);
  modKnob_.setScale(sb);
  modKnob_.setSteps(sb->steps);
  modKnob_.setLabel(lblB);
  modKnob_.setHelp(reverbSigHelp(lawChar, 1));
  modKnob_.setValue((float)knobFromStored(*sb, reverbSigRaw(block_, lawChar, 1)));

  syncKnobs();  // refresh the shared dials (Decay/Pre/Tone/Size/Width, + Dry on the full tile)

  // Per-mode contextual label: Spring's Size reads as the spring's "Length".
  knobD_.setLabel(lawChar == 1 ? "Length" : "Size");

  // Visibility: Sig A is always shown (the full-tile row-1 slot / the compact
  // right-of-Decay slot); Sig B + Pre are full-tile only.
  sigKnob_.setVisible(true);
  modKnob_.setVisible(!compact_);
  knobB_.setVisible(!compact_);  // Pre: full row 1, hidden compact
  resized();
}

void EffectTile::enterConvIrFile() {
  if (block_.effectKind != "convolution")
    return;
  // Re-entry guard: one picker at a time (a request while a dialog is up is
  // resolved as cancelled), exactly like LocalFiles. launchAsync is the only
  // launch in this JUCE build (no showOkDialog), so the chooser is a member
  // outliving the callback and a WeakReference guards it against us vanishing.
  if (convIrChooser_ != nullptr)
    return;
  // Mono / stereo / quad wav (the quad law folds the rear pair on load);
  // aiff too - both decode through the house basic-formats reader.
  convIrChooser_ = std::make_unique<juce::FileChooser>("Load IR", juce::File{},
                                                       juce::String("*.wav;*.wave;*.aiff;*.aif"));
  const auto target = blockId();
  juce::WeakReference<EffectTile> self(this);
  convIrChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [self, target](const juce::FileChooser& chooser) {
                                if (self == nullptr)
                                  return;
                                // Release the chooser once its callback unwinds (it is the caller).
                                juce::MessageManager::callAsync([self] {
                                  if (self != nullptr)
                                    self->convIrChooser_.reset();
                                });
                                const auto results = chooser.getResults();
                                if (results.isEmpty())
                                  return;
                                const juce::var res =
                                    self->services().chain.loadConvIr(target, results.getReference(0));
                                const juce::String error = res["error"].toString();
                                if (error.isNotEmpty()) {
                                  self->services().toast.show(error);
                                  return;
                                }
                                const juce::String name = res["irName"].toString();
                                self->services().toast.show(
                                    name.isEmpty() ? juce::String("IR loaded") : name + juce::String(" loaded"));
                                // A load resyncs the chain (it bumps the revision); pull the
                                // readout forward so the tile shows the kernel at once.
                                self->updateConvIrLabel();
                              });
}

void EffectTile::updateConvIrLabel() {
  if (block_.effectKind != "convolution")
    return;
  const bool hasName = block_.convIrName.isNotEmpty();
  const juce::String secondsText = (block_.convSeconds > 0.0005)
      ? juce::String(block_.convSeconds, 2) + juce::String(" s")
      : juce::String();
  // The IR's type (mono/stereo) appears next to the length readout so the
  // Width knob's meaning is unambiguous (mono IR: width SPREADS the output
  // via a Haas pair; stereo IR: width FOLDS the M/S spread).
  const juce::String irTypeText =
      (block_.convIrChannels == 1)
          ? juce::String(" Mono")
      : (block_.convIrChannels == 2)
          ? juce::String(" Stereo")
          : juce::String();
  if (block_.convIrLoaded && hasName) {
    // E-2 + length readout. Full tile: the length gets its OWN line under the
    // filename (the file's label is not the truth — a "5.0 s" NEVO plate
    // renders ~10 s, so the engine's real seconds carry it). Compact tile:
    // no height for a second line — append "· N.NN s mono" to the name line.
    const juce::String metaText = secondsText + irTypeText;
    if (compact_ && metaText.isNotEmpty())
      convIrLabel_.setText(block_.convIrName +
                               juce::String(juce::CharPointer_UTF8 (" \u00B7 ")) + metaText,
                           juce::dontSendNotification);
    else
      convIrLabel_.setText(block_.convIrName, juce::dontSendNotification);
    const juce::String secondsLine = secondsText + irTypeText;
    convIrSeconds_.setText(secondsLine, juce::dontSendNotification);
    convIrSeconds_.setVisible(!compact_ && metaText.isNotEmpty());
    convIrButton_.setButtonText("Change IR");
  } else if (hasName) {
    // E-2: identity persisted but engine not hydrated (file missing on this
    // machine) — the Load-IR button re-hydrates. No length (engine empty).
    // NOTE: the em-dash goes in via CharPointer_UTF8 — in this JUCE fork
    // String(const char*) decodes byte-per-char (ASCII/Latin-1), so a plain
    // \u2014 literal renders as "â□□" (the E-1 mojibake users saw).
    convIrLabel_.setText(juce::String(juce::CharPointer_UTF8 ("IR FILE MISSING \u2014 ")) + block_.convIrName,
                         juce::dontSendNotification);
    convIrSeconds_.setVisible(false);
    convIrButton_.setButtonText("Load IR");
  } else {
    convIrLabel_.setText("No IR loaded", juce::dontSendNotification);
    convIrSeconds_.setVisible(false);
    convIrButton_.setButtonText("Load IR");
  }
  convIrButton_.setHelpText(help::text(help::Key::convIrLoad));
}

void EffectTile::openConvCurveBand(Knob* anchor) {
  if (block_.effectKind != "convolution" || compact_ || anchor == nullptr)
    return;
  auto* host = findParentComponentOfClass<OverlayHost>();
  if (host == nullptr)
    return;
  ConvCurveBand* band = (anchor == &knobF_) ? curveInBand_.get()
                                            : curveOutBand_.get();
  if (band == nullptr)
    return;
  // Close the other band (only one at a time) and resync this one's knob.
  closeConvCurveBands();
  const double stored = (anchor == &knobF_) ? block_.convInCurve
                                            : block_.convOutCurve;
  band->knob().setValue((float)knobFromStored(scales::convFade(), stored));
  knobF_.setArmed(anchor == &knobF_);
  knobG_.setArmed(anchor == &knobG_);
  band->open(*anchor, Popover::Align::left, 6, 0, Popover::Placement::above);
}

void EffectTile::closeConvCurveBands() {
  if (curveInBand_ != nullptr && curveInBand_->isOpen())
    curveInBand_->dismiss();
  if (curveOutBand_ != nullptr && curveOutBand_->isOpen())
    curveOutBand_->dismiss();
}

void EffectTile::secondaryPressImpl(const juce::MouseEvent& e) {
  if (block_.effectKind != "convolution" || compact_)
    return;
  if (e.eventComponent == &knobF_)
    openConvCurveBand(&knobF_);
  else if (e.eventComponent == &knobG_)
    openConvCurveBand(&knobG_);
}

void EffectTile::secondaryPress(const juce::MouseEvent& e) {
  secondaryPressImpl(e);
}

void EffectTile::mouseDown(const juce::MouseEvent& e) {
  // Conv full tile: a secondary press on F In / F Out opens the curve band
  // (the knob forwarded it up here via SecondaryPressTarget). Any other
  // secondary press falls through to the tile's action menu.
  if (isSecondaryPress(e) && block_.effectKind == "convolution" && !compact_
      && (e.eventComponent == &knobF_ || e.eventComponent == &knobG_)) {
    secondaryPressImpl(e);
    return;
  }
  GalleryTile::mouseDown(e);
}

void EffectTile::enterConvExport() {
  if (block_.effectKind != "convolution")
    return;
  if (convExporting_)
    return;
  if (!block_.convIrLoaded) {
    services().toast.show("No IR loaded in this block");
    return;
  }
  if (convExportChooser_ != nullptr)
    return;
  // Default name: <irBase>_t3k_<rate>k<bit>  -- the user edits it in the
  // save dialog (launchAsync saveMode: the field is typed, not fixed).
  const juce::String base =
      block_.convIrName.isNotEmpty()
          ? juce::File(block_.convIrName).getFileNameWithoutExtension()
          : juce::String("conv_ir");
  const juce::String defName =
      base + "_exported_48k24" + juce::File::getSeparatorString() + "untitled.wav";
  convExporting_ = true;
  convExportChooser_ =
      std::make_unique<juce::FileChooser>("Export IR", juce::File{}, "*.wav");
  const auto target = blockId();
  juce::WeakReference<EffectTile> self(this);
  convExportChooser_->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::canSelectFiles,
      [self, target, defName](const juce::FileChooser& chooser) {
        if (self == nullptr)
          return;
        juce::MessageManager::callAsync([self] {
          if (self != nullptr) {
            self->convExportChooser_.reset();
            self->convExporting_ = false;
          }
        });
        const juce::File dest0 = chooser.getResult();
        if (dest0 == juce::File())  // cancelled
          return;
        // Default 24-bit / 48 kHz; the chosen path's extension decides the
        // format (WAV is the only writer we ship for this).
        const juce::var res =
            self->services().chain.exportConvIr(target, dest0, 24, 48000.0);
        const juce::String error = res["error"].toString();
        if (error.isNotEmpty()) {
          self->services().toast.show("Export failed: " + error);
          return;
        }
        self->services().toast.show("IR exported");
      });
}

void EffectTile::applySync() {
  if (block_.effectKind != "delay")
    return;
  const bool synced = delaySynced_;
  // Time knob active when NOT synced; BPM + Div active when synced.
  knobA_.setEnabled(!synced);
  knobA_.setAlpha(synced ? 0.35f : 1.0f);
  knobB_.setEnabled(synced);
  knobB_.setAlpha(synced ? 1.0f : 0.35f);
  knobC_.setEnabled(synced);
  knobC_.setAlpha(synced ? 1.0f : 0.35f);
}

void EffectTile::updateSyncLabel() {
  if (block_.effectKind != "delay")
    return;
  syncToggle_.setButtonText(compact_ ? "SNC" : "Sync");
}

void EffectTile::resized() {
  const int W = getWidth();
  const int chrome = theme::kIconBoxSize;
  power_.setBounds(4, 4, chrome, chrome);
  remove_.setBounds(W - 4 - chrome, 4, chrome, chrome);

  const bool delay = block_.effectKind == "delay";
  const bool compressor = block_.effectKind == "compressor";
  if (delay)
    syncToggle_.setBounds(28, 4, compact_ ? 30 : 36, 20);
  if (block_.effectKind == "reverb" && !compact_) {
    // The type cycler sits RIGHT OF THE mode selector (the user's call):
    // both share the y=44 band, combo slides left to make room.
    modeCombo_.setBounds(W - 14 - 44 - 8 - 120, 44, 120, 26);
    typeCycle_.setBounds(W - 14 - 44, 44, 44, 26);
  } else if (compressor && !compact_) {
    // The type cycler sits RIGHT OF THE mode selector (the user's call, same
    // as reverb): both share the y=44 band, combo slides left to make room.
    modeCombo_.setBounds(W - 14 - 44 - 8 - 120, 44, 120, 26);
    typeCycle_.setBounds(W - 14 - 44, 44, 44, 26);
  } else if (delay && !compact_)
    modeCombo_.setBounds(W - 14 - 120, 44, 120, 26);
  if (compressor)
    mbcToggle_.setBounds(compact_ ? 65 : 28, 4, compact_ ? 30 : 50, 20);
  if (compressor && compact_) {
    modeCycle_.setBounds(28, 4, 34, 20);
    // typeCycle_ is hidden in compact (see the ctor): the header's free slot
    // (65..95) belongs to the PCH toggle; it appears in full tiles only.
  }
  if (delay && compact_)
    modeCycle_.setBounds(62, 4, 34, 20);  // right of the Sync toggle
  if (block_.effectKind == "reverb" && compact_) {
    modeCycle_.setBounds(28, 4, 34, 20);  // the icon slot (reverb has no Sync): the mode cycler (34px, the comp/delay width so the title clears)
    typeCycle_.setBounds(66, 4, 34, 20); // the per-mode TYPE cycler (3-char id), right of it
  } else if (block_.effectKind == "convolution" && !compact_) {
    // Full conv tile: the Load/Change IR + Export buttons sit RIGHT of the
    // title (the user's call: "both can be moved to the right of the
    // Convolution bar"). The y=25 band is the kernel readout (filename +
    // engine length, E-2) and the y=50 band the waveform strip.
    convIrButton_.setBounds(76, 4, 68, 20);
    convExportButton_.setBounds(148, 4, 52, 20);
    convIrLabel_.setBounds(4, 25, W - 8, 14);
    convIrSeconds_.setBounds(4, 38, W - 8, 12);
  } else if (block_.effectKind == "convolution") {
    // Compact conv: ONE label line (seconds appended to the name in
    // updateConvIrLabel — no height for a second line + waveform), then a
    // clean 4x2 below. The old 30px rows collided with that band. No
    // settings chrome on the 160px tile (too crowded) — the node strip
    // carries the conv settings/export for compact tiles.
    convIrButton_.setBounds(28, 4, 68, 20);
    convIrLabel_.setBounds(4, 25, W - 8, 14);
    convIrSeconds_.setVisible(false);
  }

  const int knobH =
      Knob::heightFor((compact_ || block_.effectKind == "convolution" || block_.effectKind == "reverb") ? kCompactKnobFace : theme::kKnobSizeSecondary);
  const bool convFullTile = (block_.effectKind == "convolution") && !compact_;
  const bool convCompactTile = (block_.effectKind == "convolution") && compact_;
  const int rowGap = compact_ ? 6 : 8;
  // Full conv: the waveform strip owns y=46..90, so the knob rows start lower.
  // Compact conv: one label line (y 25..39) then row 1 at 44.
  const int row1Y = convFullTile ? 94 : (convCompactTile ? 44 : (compact_ ? 30 : 92));
  const int row2Y = row1Y + knobH + rowGap;
  const bool reverb = block_.effectKind == "reverb";
  const bool chorus = block_.effectKind == "chorus";
  const bool five = delay || compressor || reverb || chorus || (block_.effectKind == "tremolo") || (block_.effectKind == "convolution");
  // The full delay tile carries a seventh and eighth knob (the shared Mod +
  // the mode's unique sig), so it runs a 5-column grid: row 1 =
  // Mix/Time/BPM/Div/Fb, row 2 = In/Width/Mod/[unique]/Out (the unique
  // slot stays, empty, in Mod mode where the shared Mod knob IS the sig).
  // The compact (4x2) delay keeps its 4 columns: Mix/Time/BPM/Div over
  // In/Fb/[unique]/Out -- Width and the non-sig Mod are hidden there.
  const bool delayFull = delay && !compact_;
  const bool compFull = compressor && !compact_;
  const bool reverbFull = reverb && !compact_;
  const bool reverbCompact = reverb && compact_;
  const bool conv = (block_.effectKind == "convolution");
  const bool convFull = conv && !compact_;
  const bool convCompact = conv && compact_;
  const int cols = convFull ? 7
                            : ((delayFull || compFull) ? 5 : (reverbFull ? 6 : (five ? 4 : 3)));  // 2026-10-17 PLT face: 6-col even grid, rows use 6 (Mix on row 2, row 1 = 6 effect slots)
  // Wave/Volume: trio-free faces (user m08349) — wave = Output|Shape on
  // row 1, volume = Volume alone; nothing on row 2. Early-out before the
  // per-kind row chains (convPlot is convFull-only, safe to skip).
  const bool waveTile = (block_.effectKind == "wave");
  const bool volumeTile = (block_.effectKind == "volume");
  if (waveTile || volumeTile) {
    const int colsW = waveTile ? 2 : 1;
    const int colWW = (W - 8) / colsW;
    knobA_.setBounds(4, row1Y, colWW, knobH);  // Output (wave) / Volume
    if (waveTile)
      knobB_.setBounds(4 + colWW, row1Y, colWW, knobH);  // Shape
    return;
  }
  const int colW = (W - 8) / cols;

  // Row 1: Mix, p0, p1, p2 [p3 on the full delay tile]. Reverb full =
  // Decay/Pre/LoCut/Tone/Air/Mod (PLT face 2026-10-17, Mix on row 2);
  // compact = Mix/Decay/SigA/Tone (Pre + SigB + Mod hide).
  int x = 4;
  if (reverbFull) {
    // 2026-10-17 PLT face v2 row 1 (6 slots): Mix | Decay(A) | LoCut(F) | Tone(C) | Air(sigKnob_) | Mod(H)
    mix_.setBounds(x, row1Y, colW, knobH); x += colW;     // Mix (row 1 this round, per user)
    knobA_.setBounds(x, row1Y, colW, knobH); x += colW;   // Decay
    knobF_.setBounds(x, row1Y, colW, knobH); x += colW;   // LoCut
    knobC_.setBounds(x, row1Y, colW, knobH); x += colW;   // Tone
    sigKnob_.setBounds(x, row1Y, colW, knobH); x += colW; // Air (mode-0 sig A)
    knobH_.setBounds(x, row1Y, colW, knobH);              // Mod
  } else {
    mix_.setBounds(x, row1Y, colW, knobH); x += colW;
    if (convFull)
      input_.setBounds(x, row1Y, colW, knobH); x += colW;  // Dwell (full conv)
    knobA_.setBounds(x, row1Y, colW, knobH); x += colW;  // Decay / Dry
    if (reverbCompact) {
      sigKnob_.setBounds(x, row1Y, colW, knobH); x += colW; // Sig A (right of Decay)
      knobC_.setBounds(x, row1Y, colW, knobH); x += colW;   // Tone
    } else {
      knobB_.setBounds(x, row1Y, colW, knobH); x += colW;
      knobC_.setBounds(x, row1Y, colW, knobH); x += colW;
    }
  }
  if (delayFull) {
    knobD_.setBounds(x, row1Y, colW, knobH);  // Fb joins row 1
  }
  if (compFull) {
    knobD_.setBounds(x, row1Y, colW, knobH);  // Tone sits right of Release
  }
  if (convFull) {
    knobD_.setBounds(x, row1Y, colW, knobH); x += colW;  // F Out
    knobE_.setBounds(x, row1Y, colW, knobH);  // Tone
  }

  // Row 2: In, [Width, Mod, (unique unless Mod mode), (sig slot always,
  // empty in Mod mode), Out (full delay)] / [Fb, (Mod in Mod mode else
  // unique), Out (compact delay)] / [p3, p4, Out] / [p2, Out].
  x = 4;
  if (!convFull) {
    // Dwell/In on row 2 (full reverb re-shown it after the Mix move); the
    // full conv tile parks Dwell on row 1, so row 2 starts at Width.
    input_.setBounds(x, row2Y, colW, knobH);
    x += colW;
  }
  if (delayFull) {
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;    // Width
    modKnob_.setBounds(x, row2Y, colW, knobH); x += colW;  // the shared Mod
    sigKnob_.setBounds(x, row2Y, colW, knobH);  // the mode's unique (Rate in Mod mode)
    x += colW;
  } else if (reverbFull) {
    // 2026-10-17 PLT face v2 row 2 effect slots: Pre(B) | Early(modKnob_) | Size(D) | Width(E)
    // (Space dropped from the face; LoCut on row 1; Out ends the row)
    knobB_.setBounds(x, row2Y, colW, knobH); x += colW;   // Pre (moved down from row 1)
    modKnob_.setBounds(x, row2Y, colW, knobH); x += colW; // Early (mode-0 sig B)
    knobD_.setBounds(x, row2Y, colW, knobH); x += colW;   // Size
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;   // Width
  } else if (reverbCompact) {
    knobD_.setBounds(x, row2Y, colW, knobH); x += colW;   // Size
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;   // Width
  } else if (compFull) {
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;   // Thresh
    sigKnob_.setBounds(x, row2Y, colW, knobH); x += colW; // SC
    modKnob_.setBounds(x, row2Y, colW, knobH); x += colW; // KNEE (VCA) / CLIP (1-4)
  } else if (compressor) {
    // Compact comp: SC and Tone are hidden; the mode's character stays.
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;   // Thresh
    modKnob_.setBounds(x, row2Y, colW, knobH);             // KNEE (VCA) / CLIP (1-4)
  } else if (delay) {
    knobD_.setBounds(x, row2Y, colW, knobH); x += colW;    // Fb
    if (block_.delayMode == 3)
      modKnob_.setBounds(x, row2Y, colW, knobH);           // Mod IS the sig
    else
      sigKnob_.setBounds(x, row2Y, colW, knobH);           // the mode's unique
    x += colW;
  } else if (convFull) {
    // Row 2 = Width(sig) | Mod | F In(F) | F Out(G) | Out; InCrv/OutCrv
    // live in the curve bands (right-click F In / F Out).
    sigKnob_.setBounds(x, row2Y, colW, knobH); x += colW;  // Width
    modKnob_.setBounds(x, row2Y, colW, knobH); x += colW;  // Mod
    knobF_.setBounds(x, row2Y, colW, knobH); x += colW;    // F In
    knobG_.setBounds(x, row2Y, colW, knobH); x += colW;    // F Out
  } else if (convCompact) {
    // Compact conv's Dry/Width live in knobD_/knobE_ (paramsFor maps the
    // compact list to slots A..E — sig_/mod_ are NOT children of the compact
    // tile, so bounds on them would land on nothing).
    knobD_.setBounds(x, row2Y, colW, knobH); x += colW;  // Dry (wet level)
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;  // Width
  } else if (five) {
    knobD_.setBounds(x, row2Y, colW, knobH); x += colW;
    knobE_.setBounds(x, row2Y, colW, knobH); x += colW;
  } else {
    knobC_.setBounds(x, row2Y, colW, knobH); x += colW;
  }
  output_.setBounds(x, row2Y, colW, knobH);
  if (convFullTile) {
    convPlot_.setBounds(4, 50, W - 8, 40);
  }
}

void EffectTile::paint(juce::Graphics& g) {
  const int W = getWidth();
  const int H = getHeight();
  const bool delay = block_.effectKind == "delay";
  const bool tremolo = block_.effectKind == "tremolo";
  const bool compressor = block_.effectKind == "compressor";
  const bool reverb = block_.effectKind == "reverb";
  const bool convolution = block_.effectKind == "convolution";
  paint::fill(g, juce::Rectangle<float>(0, 0, W, H), gallery::kTileCorner,
              enabled_ ? theme::kSurfaceRaised : theme::kSurface);
  if (!enabled_)
    g.fillAll(juce::Colours::black.withAlpha(0.30f));  // bypassed: dim the face

  // Effect-type icon: right of the power button, power-button sized (20x20). The
  // compressor has no drawn glyph -- its icon slot is the PUNCH toggle button.
  // The compact reverb likewise puts its mode cycler in the icon slot.
  if (!compressor && !delay && !(reverb && compact_) && !convolution) {
    const float gs = 20.0f;
    const float iconX = 28.0f;
    const float iconY = 4.0f;
    const Icon icon = (tremolo ? Icon::Volume2 : (reverb ? Icon::Share : Icon::Copy));
    Icons::draw(g, icon, juce::Rectangle<float>(iconX, iconY, gs, gs), theme::kWhite,
                /*strokeWidth=*/1.75f);
  }

  // Effect name: top-centred, between the power (left) and remove (right) icons.
  // Tight tiles: smaller font, starts after the header buttons (mode/PCH), and
  // the compressor is abbreviated to fit.
  const int chrome = theme::kIconBoxSize;
  const int titleX0 =
      compact_ ? (compressor ? 100 : (delay ? 100 : (reverb ? 106
                                                    : (convolution ? 102 : 50))))
                : (convolution ? 30 : (28 + 36 + 6));
  juce::Font font(compact_ ? 13.0f : 16.0f, juce::Font::bold);
  const juce::String title =
      compact_ && compressor ? "Comp" : (compact_ && reverb ? "Rev"
                             : (compact_ && convolution ? "Conv"
                             : (delay ? "Delay" : (tremolo ? "Tremolo"
                               : (compressor ? "Compressor" : (reverb ? "Reverb"
                               : (convolution ? "Convolver"
                                  : (block_.effectKind == "wave" ? "Wave"
                                  : (block_.effectKind == "volume" ? "Volume"
                                                                    : "Chorus")))))))));
  paint::text(g, title,
              juce::Rectangle<int>(titleX0, 4, W - titleX0 - (4 + chrome), chrome),
              font, theme::kMuted, juce::Justification::centred);
}

}  // namespace t3k::ui
