#include "Help.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "Design.h"

namespace t3k::ui::help {
namespace {

using juce::String;

constexpr bool kTouch = design::kCoarsePointer;

// The copy is full of typographic characters; juce::String(const char*)
// asserts on non-ASCII, so every literal goes through here.
String U(const char* utf8) { return String::fromUTF8(utf8); }

// OS-correct modifier chords: glyphs + hyphen on Apple platforms, spelled
// out + plus elsewhere.
String chord(const char* macGlyph, const char* name, const char* gesture) {
  return design::kAppleModifierGlyphs ? U(macGlyph) + "-" + gesture : String(name) + "+" + gesture;
}
String shift(const char* gesture) { return chord("\u21e7", "Shift", gesture); }
String alt(const char* gesture) { return chord("\u2325", "Alt", gesture); }

// Shared legend for every knob (touch has no modifiers or second button).
const String& knobKeys() {
  static const String keys =
      kTouch ? U("drag up or down: adjust · double tap: reset · tap the name: type")
             : shift("drag") + U(": fine · double-click: type · ") + alt("click") + ": reset";
  return keys;
}

String knobDesktop(const char* name, const char* desc) {
  return U(name) + ": " + U(desc) + " " + knobKeys();
}

// Desktop pointer vocabulary rewritten for touch. `Right-click` first, since
// it contains `click`; everything a right-click reaches answers a touch and
// hold on a touch screen.
String touchify(String s) {
  if (!kTouch) return s;
  return s.replace("Right-click", "Touch and hold")
      .replace("right-click", "touch and hold")
      .replace("Click", "Tap")
      .replace("click", "tap");
}

std::map<Key, String> buildTable() {
  std::map<Key, String> t;

  // Faceplate: gains
  t[Key::inputLevel] = knobDesktop("Input", "chain input level, ±24 dB.");
  t[Key::inputMode] = U("Input Mode: how the stereo source feeds the chain. Click: choose.");
  // Menu rows. The first two are the same routing (the natural one for the
  // chain mode), worded for what it does there: a mono chain sums, stereo
  // chains take one channel each.
  t[Key::inputModeSum] = U("Stereo SUM (L+R): both channels folded to mono into the chain.");
  t[Key::inputModeStereo] = U("Stereo: channel 1 feeds the Left chain, channel 2 the Right.");
  t[Key::inputModeDualMono] =
      U("Dual Mono (L&R): L and R each run the chain separately, identical stereo chains. "
        "2× CPU (spread across cores with Multi-core). Spread off.");
  t[Key::inputModeLeft] = U("Left: channel 1 on both sides.");
  t[Key::inputModeRight] = U("Right: channel 2 on both sides.");
  t[Key::outputLevel] = knobDesktop("Output", "master output level, ±24 dB.");
  t[Key::outputBalance] = knobDesktop("Balance", "level trim between chains, ±12 dB (pre-pan). Center: off.");
  t[Key::autoBalance] = U("Auto Balance: click, play ~2 s to match chain levels. Click again: cancel.");

  // Faceplate: gate, tone stack, stereo image
  t[Key::gate] = knobDesktop("Gate", "noise gate threshold, -100 to 0 dB. Right-click: advanced.");
  // Same touch caveat as the spread power below: the deck answers a hold on
  // the Gate knob only.
  t[Key::gatePower] = kTouch ? U("Gate Power: noise gate on/off.")
                             : U("Gate Power: noise gate on/off. Right-click: advanced.");
  t[Key::gateRelease] =
      knobDesktop("Release", "how fast the gate closes, 5-500 ms. Short: tight · long: natural tails.");
  t[Key::gateHold] = knobDesktop("Hold", "time the gate stays open after the signal drops, 0-200 ms.");
  t[Key::gateRange] = knobDesktop("Range", "how deep the gate closes, 20-80 dB. 80: mute · 20: tame.");
  t[Key::pitch] = knobDesktop("Pitch Shift",
                              "transpose the input ±24 semitones. STEP off: whammy sweep. Right-click: advanced.");
  t[Key::pitchPower] = kTouch ? U("Pitch Power: pitch shift on/off. On adds latency.")
                              : U("Pitch Power: pitch shift on/off. On adds latency. Right-click: advanced.");
  t[Key::pitchStep] = U("STEP: snap the shift to whole semitones (transpose). Off: smooth sweep, like a whammy.");
  t[Key::pitchTonality] =
      knobDesktop("Tonality", "highs above this stay put, 1-20 kHz. Keeps pick attack bright; Off: pure shift.");
  t[Key::pitchWindow] = knobDesktop(
      "Buffer", "shifter delay buffer, 20-60 ms. Short: less latency · long: cleaner, more latency.");
  t[Key::toneBass] = knobDesktop("Bass", "tone stack lows, 0-10: ±20 dB shelf at 150 Hz.");
  t[Key::toneMiddle] = knobDesktop("Middle", "tone stack mids, 0-10: ±15 dB bell at 425 Hz.");
  t[Key::toneTreble] = knobDesktop("Treble", "tone stack highs, 0-10: ±10 dB shelf at 1.8 kHz.");
  t[Key::tonePower] = U("Tone Stack Power: Bass/Middle/Treble on/off.");
  t[Key::spreadOffset] =
      knobDesktop("Offset", "double-track lag, ≤24 ms toward L or R. Center: off. Right-click: advanced.");
  t[Key::spreadWobble] = knobDesktop("Wobble", "humanizing delay drift, up to ±1.2 ms.");
  t[Key::spreadWobblePower] =
      U("Wobble Power: delay drift on/off. Off: a static, more comb-like double.");
  t[Key::spreadCrossover] = knobDesktop("Crossover", "lows below the cutoff stay dual-mono, 33-520 Hz.");
  t[Key::spreadCrossoverPower] =
      U("Crossover Power: off doubles the full band (lows lose mono safety).");
  t[Key::spreadDiffuse] = U("Diffuse Power: phase-decorrelates the lagged side. Off: a pure delay.");
  t[Key::spreadAdvert] = U("Spread: mono-to-stereo double via a wobbling short lag. Click: enable.");
  // On touch the advanced deck answers a hold on the Offset knob only, so the
  // power rows drop the tail touchify would turn into a false promise.
  t[Key::spreadPower] = kTouch ? U("Spread Power: spread off; collapses its controls.")
                               : U("Spread Power: spread off; collapses its controls. Right-click: advanced.");
  t[Key::imageCorrelation] =
      U("Mono safety: dim: safe · yellow: caution · red: cancellation on mono sum.");
  t[Key::spreadMonoOutput] =
      U("Spread: unavailable, the output is mono (mono track or one-channel output device).");
  t[Key::spreadDualMono] =
      U("Spread off in Dual Mono: the chain already outputs two real channels.");
  t[Key::alignOffset] = knobDesktop(
      "Offset", "corrective chain alignment, ≤24 ms toward L or R. Center: off. Right-click: advanced.");
  t[Key::alignWobble] = knobDesktop("Wobble", "humanizing drift of the align delay, up to ±1.2 ms.");
  t[Key::alignWobblePower] = U("Wobble Power: drifts the delayed chain like an ADT double-track.");
  t[Key::alignCrossover] = knobDesktop("Crossover", "lows below the cutoff skip the deck, 33-520 Hz.");
  t[Key::alignCrossoverPower] = U("Crossover Power: on keeps lows out of the delay and diffusion.");
  t[Key::alignDiffuse] = U("Diffuse Power: phase-decorrelates the delayed chain for width.");
  t[Key::alignAdvert] = U("Align: corrective chain time alignment. Click: enable.");
  t[Key::alignPower] = kTouch ? U("Align Power: align off; collapses its controls.")
                              : U("Align Power: align off; collapses its controls. Right-click: advanced.");
  t[Key::autoAlign] =
      U("Auto Align: a ½ s internal sweep time-aligns the chains and fixes inverted polarity. Click again: cancel.");

  // Top bar
  t[Key::tuner] = U("Tuner: chromatic tuner. Click again: back.");
  t[Key::undo] = U("Undo: revert last chain edit.");
  t[Key::redo] = U("Redo: re-apply undone edit.");
  t[Key::settings] = U("Settings: plugin and audio options.");
  t[Key::account] = U("Account: settings and TONE3000 sign-out.");
  t[Key::monoMode] = U("Mono: one chain, both outputs.");
  t[Key::stereoMode] = U("Stereo: independent Left/Right chains.");

  // Presets
  t[Key::presetPrev] = U("Previous Preset: step back through the list.");
  t[Key::presetNext] = U("Next Preset: step forward through the list.");
  t[Key::presetBrowse] = U("Presets: browse factory and user presets.");
  t[Key::presetSave] = U("Save Preset: store the current chain. Same name: overwrite.");
  t[Key::presetNew] = U("New: clear the chain and reset every control to its default.");
  t[Key::presetRename] = U("Rename: edit name. Enter: commit · Esc: cancel.");
  t[Key::presetDelete] = U("Delete: remove this preset.");
  t[Key::presetReorder] = U("Reorder: drag presets into a custom order. Prev/Next and MIDI follow it.");
  t[Key::presetDrag] = U("Drag: move this preset within its section.");
  t[Key::presetPcToggle] =
      U("MIDI PC: show each preset’s program change number. Prev/Next and PC follow the list order.");
  t[Key::presetPc] = U("PC: the MIDI program change number that loads this preset.");

  // Tone browser
  t[Key::browserSearch] = U("Search: find tones on TONE3000 by title, gear, tags or creator.");
  t[Key::browserSearchProfile] = U("Search: find tones in this list by title.");
  t[Key::browserMoreFilters] = U("Filters: sort, and narrow by format, tags, makes, creators or calibration.");
  t[Key::browserFewerFilters] = U("Fewer filters: fold these away. They stay applied.");
  t[Key::browserVerified] = U("Verified: only tones from verified creators.");
  t[Key::browserProfile] = U("Profile: your recently used, favorited or created tones.");
  t[Key::browserGear] = U("Gear: only tones of this type. Click again: all types.");
  t[Key::browserSort] = U("Sort: order the results.");
  t[Key::browserFormat] = U("Format: only NAM captures or impulse responses.");
  t[Key::browserTags] = U("Tags: only tones carrying any of the chosen tags.");
  t[Key::browserMakes] = U("Makes: only tones of the chosen makes and models.");
  t[Key::browserCreators] = U("Creators: only tones by the chosen creators.");
  t[Key::browserCalibrated] = U("Calibrated: only tones with a calibrated model.");
  t[Key::browserClearFilter] = U("Click the name: change this filter · ×: clear it.");
  t[Key::browserProfileLocked] =
      U("Unavailable while a profile filter is set: your own tones list by title and gear only. Clear the profile to use it.");
  t[Key::browserCalibratedIr] = U("Not for impulse responses: cabinets and spaces carry no calibration data.");
  t[Key::browserBackToTrending] = U("Back: to the trending tones.");

  // Sign-in screen
  t[Key::signInBack] = U("Back: stop signing in and return.");
  t[Key::signInCopyLink] = U("Copy Link: copy the sign-in link to paste into any browser on this machine.");
  t[Key::signInPhone] =
      U("Sign in on a different device: scan a QR code or type a code on your phone, no browser needed here.");
  t[Key::signInNewCode] = U("New code: get a fresh code for your phone.");
  t[Key::signInRetry] = U("Try again: start the sign-in over.");
  t[Key::signInDismiss] = U("Dismiss: drop the error and return.");

  // Chain gallery
  t[Key::addTile] =
      U("Add Tone: browse TONE3000 for this slot, or drop a .nam or IR .wav file (or a folder of them). Right-click: paste / load file · drag: move.");
  t[Key::closeToneBrowser] = U("Close: back to the chain.");
  t[Key::copyBlock] = U("Copy: copy this block (tone, model and all settings).");
  t[Key::pasteBlock] = U("Paste: add a copy of the copied block in this slot.");
  t[Key::addEffectDelay] =
      U("Add Delay: insert a built-in delay in this slot. Right-click it to tune time and feedback.");
  t[Key::addEffectChorus] =
      U("Add Chorus: insert a built-in chorus in this slot. Right-click it to tune rate and depth.");
  t[Key::addEffectTremolo] =
      U("Add Tremolo: insert a built-in tremolo in this slot. Right-click it to tune rate, depth and LFO shape.");
  t[Key::addEffectCompressor] =
      U("Add Compressor: insert a built-in multi-mode compressor in this slot. Right-click to tune ratio, attack, release, tone and mode.");
  t[Key::addEffectReverb] =
      U("Add Reverb: insert a digital comb-bank reverb in this slot. Right-click to tune decay, pre-delay, tone, size and modulation.");
  t[Key::effectReverb] = U("Reverb setting: adjust the value for this block.");
  t[Key::reverbDecay] = knobDesktop("Decay", "reverb tail length, 50-3000 ms.");
  t[Key::reverbPre] = knobDesktop("Pre", "reverb pre-delay, 0-60 ms (echoes the dry before the tail).");
  t[Key::reverbTone] = knobDesktop("Tone", "reverb brightness, 0 bright to 100% dark.");
  t[Key::reverbSize] = knobDesktop("Size", "reverb size, scales all delay times for a bigger or smaller space.");
  t[Key::reverbWidth] = knobDesktop("Width", "reverb stereo width — 0 = mono (L == R), 1 = wide (decorrelated channels).");
  t[Key::reverbDwell] = knobDesktop("Dwell", "increases the dwell of the reverb.");
  t[Key::reverbMode] = knobDesktop("Mode", "reverb type: Digital (clean 8-comb), Spring (1D metallic), Plate (dense 2D wash), Room (short early set), Chamber (diffuse + LF tail), Hall (long, wide, spatial). Select to apply that mode's starting dials; the two signature knobs follow the mode.");
  t[Key::reverbDensity] = knobDesktop("Density", "Digital: how thick the tail sounds -- low is a crisp dry echo, high blends into a big dense wash.");
  t[Key::reverbMod] = knobDesktop("Mod", "Digital: a slow waver on the taps (subtle motion on the tail).");
  t[Key::reverbSprings] = knobDesktop("Springs", "Spring: how many spring lines blend -- more = smoother, less metallic (1-6).");
  t[Key::reverbSag] = knobDesktop("Sag", "Spring: the low-end sag + the transducer-attack drip (low tones lag and creep longer).");
  t[Key::reverbBright] = knobDesktop("Bright", "Plate: the dense bright onset of the mode wash.");
  t[Key::reverbBloom] = knobDesktop("Bloom", "Plate: the dispersive bright-to-bloom (highs arrive first, low blooms later).");
  t[Key::reverbEarly] = knobDesktop("Early", "Room: the short sharp early-reflection set.");
  t[Key::reverbAir] = knobDesktop("Air", "Room: per-tap air-absorption darkening (the tail darkens over time).");
  t[Key::reverbVolley] = knobDesktop("Volley", "Chamber: the short diffuse early cluster (a dense volley of reflections).");
  t[Key::reverbBass] = knobDesktop("Bass", "Chamber: the extended low-frequency tail (bass outlasts the high end).");
  t[Key::reverbBuild] = knobDesktop("Build", "Hall: the long wide early build-up (lateral energy = the spatial impression).");
  t[Key::reverbSpace] = knobDesktop("Space", "Hall: the air / lateral width of the space.");
  t[Key::effectDelay] = U("Delay setting: adjust the value for this block.");
  t[Key::effectChorus] = U("Chorus setting: adjust the value for this block.");
  t[Key::effectTremolo] = U("Tremolo setting: adjust the value for this block.");
  t[Key::effectCompressor] = U("Compressor setting: adjust the value for this block.");
  t[Key::effectMix] = knobDesktop("Mix", "dry/wet blend of the effect.");
  t[Key::effectTime] = knobDesktop("Time", "delay time, 5-1000 ms. Toggle: sync to a note value.");
  t[Key::effectFeedback] = knobDesktop("Feedback", "echo repeats (longer tail), 0-90%.");
  t[Key::effectRate] = knobDesktop("Rate", "chorus LFO speed, 0.05-5 Hz.");
  t[Key::effectDepth] = knobDesktop("Depth", "chorus depth, 0-5 ms.");
  t[Key::effectInput] = knobDesktop("Input", "pre-effect input level.");
  t[Key::effectOutput] = knobDesktop("Output", "post-effect output level.");
  t[Key::effectDamping] = knobDesktop("Damping", "low-pass on the delay feedback, 0-100%. (parked: not on the face)");
  t[Key::delaySpread] = knobDesktop("Width", "Offset between the left and right echoes: 0% = same time on both (centred), 100% = left half the time, right 1.5x -- a wide stereo delay.");
  t[Key::effectSpread] = knobDesktop("Width", "stereo width of the chorus, 0-100%.");
  t[Key::effectTone] = knobDesktop("Tone", "chorus tone, neutral at noon: LEFT warms/darkens the chorus copy (up to ~-18 dB at full left), RIGHT brightens it (up to +18 dB high shelf at full right).");
  t[Key::effectWave] = knobDesktop("Shape", "chorus LFO shape: Sine, Triangle, Saw, Saw (Down) or Square.");
  t[Key::effectBpm] = knobDesktop("BPM", "tempo for synced notes, 60-240 BPM.");
  t[Key::tremoloRate] = knobDesktop("Rate", "Tremolo LFO rate in Hz; higher = faster.");
  t[Key::tremoloDepth] = knobDesktop("Depth", "How hard the tremolo modulates; 0 steady, 100 full gate.");
  t[Key::tremoloLfo] = knobDesktop("Shape", "Tremolo LFO shape: Sine, Triangle, Saw, Saw (Down) or Square.");
  t[Key::tremoloTone] = knobDesktop("Tone", "Tremolo tone (works like the Chorus tone): neutral at 0 dB. LEFT warms/darkens by up to -18 dB (low-pass), RIGHT brightens by up to +18 dB (high shelf).");
  t[Key::tremoloSpread] = knobDesktop("Width", "Phase offset between the left and right LFOs: 0% = both channels pulse together (tight, centred), 100% = 180 deg out of phase, left up while right is down -- auto-pan swish.");
  t[Key::compRatio] = knobDesktop("Ratio", "compression ratio, 1:1 to 20:1.");
  t[Key::compAttack] = knobDesktop("Atk", "time before gain reduction starts, 0.1-500 ms.");
  t[Key::compRelease] = knobDesktop("Rel", "time to stop gain reduction, 20-2000 ms. In Vari-Mu the dial is the unit\u2019s release POSITION: full travel maps to a 0.04\u201325 s ladder (halfway = 1 s; the readout shows the true seconds), and in the upper half a sustained high level keeps it pinned longer while it lasts (program-dependent release).");
  t[Key::compTone] = knobDesktop("Tone", "treble: + adds highs, - cuts them, 0 = flat.");
  t[Key::compScHp] = knobDesktop("SC", "side-chain high-pass, 20-8000 Hz.");
  t[Key::compMode] = knobDesktop("Mode", "the detector/character circuit. Each option is a genuinely different detector algorithm, not just a different set of defaults, and they are listed in order of coloration, cleanest to most characterful: VCA (classic soft-knee, level-driven RMS, clean and transparent), Tube (vintage tube variable-mu: warm, smooth and musical; evens a program without pumping), Opto (the optical cell: warm, slow, classic optical colour), Vari-Mu (tube, smooth, slow, musical), FET (transistor stage: punchy, aggressive). Selecting a mode also sets that detector's default attack/release/threshold as a starting point.");
  t[Key::compThreshold] = knobDesktop("Thresh", "the level (dBFS) above which the detector engages and gain reduction starts; turn it down to pull compression in earlier, or up so only the loudest peaks are reduced. All modes default to -18 dBFS; double-tap or Alt-click snaps back to that default.");
  t[Key::compMakeup] = knobDesktop("Makeup", "post-compression gain to bring the level back up after the compressor has reduced it; the block's output gain (make-up gain).");
  t[Key::compIn] = knobDesktop("In", "input drive into the compressor; set it higher to increase the amount of compression, or lower to decrease it.");
  t[Key::delayMode] = knobDesktop("Mode", "the delay character. Each mode is a genuinely different engine, not a preset: Digital (the clean reference comb), Tape (idealised tape delay: mechanical wow/flutter, multi-head even-interval repeats, soft saturation -- Space-Echo character), BBD (chip delay: tone and loss fall as time goes up -- you cannot get a long BBD echo clean), Mod (vibrato/duo: the repeats waver, the dry note stays put), Magnetic (warbly tape: the tone is the tape head+core law and the repeats WAVE on the mode's wobble-rate face, the shared Mod knob is the wobble depth), MemGuy (the BBD-line memory: the repeats run the time-buys-loss tone law plus a slow=chorus / fast=vibrato waver on the shared rate face). Selecting a mode also lands you on its signature starting point.");
  t[Key::delayPing] = knobDesktop("Ping", "stereo motion of the echo train: 0% = both ears hear the same echo time (centred), 100% = strict left/right alternation -- first echo left, second right, and so on. A continuous 0..100% dial.");
  t[Key::delayHeads] = knobDesktop("Heads", "how many heads the multi-head tape echo plays with, at even intervals -- 1 = the plain single echo, more = the wider Space-Echo style repeat cluster (half the time is the slowest head, so a synced subdivision always lands on the beat).");
  t[Key::delayChip] = knobDesktop("Chip", "how vintage the bucket-brigade runs: drive + per-pass loss + darkening, all scaled together. 0% = the cleanest BBD (still darkening with time -- that is the chip), 100% = maximum drive and muddiness.");
  t[Key::delayMod] = knobDesktop("Mod", "modulation depth on the repeat path, in EVERY delay mode: the echoes waver in pitch around the dialed time while the dry note stays put. Each mode moves it its own way -- Digital and Mod the classic 5 Hz vibrato (left and right out of phase, the wide duo motion), Tape a slow flutter drift, BBD a deep spacey wobble, Magnetic a tape-style warble, MemGuy a mid-sweep chorus/vibrato waver. 0 = the straight tap (no waver), 1 = maximum waver.");
  t[Key::delayRate] = knobDesktop("Rate", "wobble SPEED for the Mod, Magnetic and MemGuy delays (each mode's UNIQUE rate control): how fast the repeats waver in pitch around the dialed time. 0.5 Hz = a super-slow drift, 30 Hz = fast flutter; MemGuy (the Memory Man RATE pot) and Mod start on the classic 5 Hz mid-sweep; Magnetic starts on the slow 1 Hz wow -- sweep is chorus-slow down to fast-vibrato either way. The wobble\'s DEPTH is the shared Mod knob beside it (0 = straight taps).");
  t[Key::compClip] = knobDesktop("CLIP", "CLIP (harmonic coloration depth, every compressor mode except VCA): how strongly the mode's stage colours the tone once compression takes hold. Noon (100%) is that mode's normal breakup -- the unedited sound. Turn down toward 0% and the colouring fades out: the compression still moves the body, only the added harmonics are removed. Turn up past 100% to drive the stage hotter than stock -- more breakup, more ceiling. VCA is clean by design; that mode shows KNEE instead.");
  t[Key::compKnee] = knobDesktop("KNEE", "KNEE (VCA soft-knee width): how abruptly gain reduction engages. Low is hard -- close to ratio control, with a sharper grab on transients; noon 6 dB is the classic VCA transition (the unedited sound); high is very soft -- the compressor glides in over a wider input range before it fully takes hold. The other four modes use CLIP, not KNEE.");
  t[Key::compMbc] = knobDesktop("PUNCH", "PUNCH (parallel-blend comp): runs a second, lighter parallel path (3x the release, about half the gain reduction) alongside the main detector, blended 50/50 -- the classic parallel-comp punch: the body opens back up while the fast path still grabs peaks. In FET mode the fast and slow amp pairs are always active (as in the real FET unit's four parallel channels); there PUNCH opens the slow pair fully (1:1) -- peak clamp stays, the body runs clean, steady compression halves. In Opto mode it is a parallel LIGHT stage on the optical unit's hot gain path: lighter gain reduction drives it harder still (the real optical unit is a gain stage that saturates into its own ceiling), so the body OPENS -- louder and more colored, while the lagging optical cell lets the transient punch through. Works in every mode.");
  t[Key::effectSubdivision] =
      knobDesktop("Time", "synced note length: 1/16T, 1/16, 1/8T, D8, 1/8, 1/4T, 1/4, 1/2, 1/1.");
  t[Key::loadFileTile] =
      U("Load File: pick a local .nam or IR .wav file to load here. No account needed.");
  t[Key::loadFolderTile] =
      U("Load Folder: pick a folder of .nam or .wav files; loads as one multi-model block.");
  t[Key::blockPower] = U("Power: bypass this block.");
  t[Key::retryLoad] = U("Retry: re-download this model.");
  t[Key::swapTone] = U("Swap: replace this tone, keeping its slot.");
  t[Key::removeBlock] = U("Remove: delete this block.");
  t[Key::panLeft] = knobDesktop("Pan L", "Left chain, hard left ↔ center.");
  t[Key::panRight] = knobDesktop("Pan R", "Right chain, center ↔ hard right.");
  t[Key::panLink] = U("Link Pans: mirror both pan knobs.");
  t[Key::monoSum] =
      U("Mono output: both chains summed to one channel. Bal/S/Ø still shape the blend; pans are off.");
  t[Key::panMonoSum] =
      U("Pan: unavailable, the output is mono. The chains are summed instead (see MONO).");
  t[Key::soloLeft] = U("Solo L: hear the Left chain alone.");
  t[Key::soloRight] = U("Solo R: hear the Right chain alone.");
  t[Key::invertLeft] =
      U("Invert L: flip the Left chain polarity. Fixes chains that hollow out or cancel.");
  t[Key::invertRight] =
      U("Invert R: flip the Right chain polarity. Fixes chains that hollow out or cancel.");
  t[Key::swapChains] = U("Swap Chains: exchange Left/Right chains.");
  t[Key::branchGap] = U("Branch: feed the other chain from this point in this chain.");
  t[Key::branchJunction] = U("Branch Point: the other chain starts here. Click: make chains independent.");

  // Block card
  t[Key::blockIn] = knobDesktop("In", "block input gain, ±24 dB.");
  t[Key::blockOut] = knobDesktop("Out", "block output gain, ±24 dB.");
  t[Key::blockOutIr] = knobDesktop("Out", "block output gain, ±24 dB (IR pre-trimmed -18 dB).");
  t[Key::blockMix] = knobDesktop("Mix", "dry/wet blend.");
  t[Key::blockNormalize] = U("Normalize: level this block’s loudness. Off: raw capture level.");
  t[Key::blockNormalizeOverridden] =
      U("Normalize: overridden — calibration hands this model’s true output level to the next NAM block.");
  t[Key::blockSize] = U("NAM Size: LITE saves CPU · FULL is highest quality. Sets this block only.");
  t[Key::blockSizeChip] =
      U("NAM Size: this block’s size differs from your default. To choose per block, enable it in Settings.");
  t[Key::blockCalibrated] =
      U("Calibration: active — levels set from this model’s calibration data.");
  t[Key::blockUncalibrated] = U("Calibration: inactive — this model has no calibration data.");
  t[Key::eqToggle] = U("EQ: 6-band EQ editor. Outline: EQ shaping the sound.");
  t[Key::toneInfo] = U("Info: tone description, makes, and tags from TONE3000.");
  t[Key::toneInfoLogin] = U("Log In: sign in to TONE3000 to see tone details.");
  t[Key::viewOnT3k] = U("View on TONE3000: open this tone in your browser.");
  t[Key::favoriteTone] = U("Favorite: save this tone to your TONE3000 favorites.");
  t[Key::unfavoriteTone] = U("Favorited: click to remove from your TONE3000 favorites.");
  t[Key::eqSlidersView] = U("Sliders: gain-only fader view.");
  t[Key::eqCurveView] = U("Curve: parametric freq/gain/Q editor.");
  t[Key::eqReset] = U("Reset EQ: all bands flat, position post.");
  t[Key::eqPre] = U("PRE: EQ before the model. Off: after the model (wet only).");
  t[Key::eqPower] = U("EQ Power: bypass EQ, keep settings.");
  t[Key::shareTone] = U("Share: copy TONE3000 link.");
  t[Key::modelSelectSignedOut] = U("Models: sign in to TONE3000 to switch models.");
  t[Key::backToChain] = U("Back: chain overview.");

  // EQ editor
  t[Key::eqFader] = kTouch ? U("Band Fader: gain, ±15 dB. drag: adjust · double tap: reset.")
                           : U("Band Fader: gain, ±15 dB. ") + shift("drag") +
                                 U(": fine · double-click / ") + alt("click") + ": reset.";
  t[Key::eqFaderPass] = U("Pass Band: no gain. Shape it in Curve view.");
  t[Key::eqDot] = kTouch ? U("Band Dot: drag: freq + gain · double tap: reset. Q: use the Q chip.")
                         : U("Band Dot: drag: freq + gain · scroll: Q · ") + shift("drag") +
                               U(": fine · ") + alt("click") + ": reset.";
  t[Key::eqFreqChip] = U("Freq: click to type (“800”, “1.2k”). Enter: commit · Esc: cancel.");
  t[Key::eqGainChip] = U("Gain: click to type, ±15 dB. Enter: commit · Esc: cancel.");
  t[Key::eqQChip] = kTouch ? U("Q: tap to type. Enter: commit · Esc: cancel.")
                           : "Q: scroll the graph (" + shift("scroll") + ": fine) or click to type.";

  // Meters
  t[Key::clipDot] = U("Clip: latches on clipping. Click: clear.");

  // The hint bar itself
  t[Key::cpuLoad] = U("CPU: audio engine load.");
  t[Key::hideHints] = U("Hide Info Bar: hide this bar. Re-enable in Settings.");

  for (auto& [key, value] : t) value = touchify(value);
  return t;
}

const std::map<Key, String>& table() {
  static const auto t = buildTable();
  return t;
}

}  // namespace

const String& text(Key key) {
  auto& t = table();
  auto it = t.find(key);
  jassert(it != t.end());
  static const String empty;
  return it == t.end() ? empty : it->second;
}

String toneTile(const String& title) {
  return kTouch ? title + U(". Tap: open · drag: reorder · touch and hold: menu.")
                : title + U(". Click: open · drag: reorder · ") + alt("drag") +
                      ": duplicate · right-click: copy / load file.";
}

String bandType(const String& label) { return label + ": band curve shape."; }

String lead(const String& hint) {
  const int colon = hint.indexOf(": ");
  return (colon > 0 ? hint.substring(0, colon) : hint).trim();
}

void announce(const String& text) {
  if (text.isNotEmpty())
    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

}  // namespace t3k::ui::help
