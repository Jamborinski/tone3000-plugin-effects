// TileShot -- golden-screenshot tool for the gallery tiles.
//
// Renders ONE gallery tile with a deterministic fixture state straight into
// a juce::Image (same createComponentSnapshot path as UiTestbed --capture;
// no window is opened), so a layout change -- knob moved, label band
// collided, slot swapped -- shows up in the PNG before it shows up in a
// user's DAW. The approved baseline lives in goldens/ next to this file;
// approval is a human act (--regen), never CI. See docs/agents/ui-snapshots.md.
//
//   TileShot --list
//   TileShot --tile conv-full [--size 224] [--scale 2] [--out shot.png]
//   TileShot --golden shot.png goldens/conv-full.png [--tol 24] [--diff d.png]
//       exit 0: identical within tolerance
//       exit 1: mismatching pixels (bounds + % reported)
//       exit 2: usage / IO / size mismatch
//   TileShot --regen goldens/conv-full.png --from shot.png   (deliberate re-approval)
//
// Fixture states are built in code (no external files): the "loaded IR"
// kernels are the deterministic envelope below; the backend/session shape
// reuses the testbed fixtures (scenarios.json) exactly like --capture does.

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

#include "Compare.h"
#include "MockBackend.h"
#include "MockSession.h"
#include "model/ChainState.h"
#include "services/Services.h"
#include "views/gallery/EffectTile.h"
#include "views/gallery/GalleryGeometry.h"
#include "views/gallery/ToneTile.h"

namespace t3k::ui::testbed {

namespace {

// juce::var(ReferenceCountedObject*) is public (and owns: it incs the
// refcount); only the const-pointer overload is deleted, so keep the
// pointer non-const when we call it.
struct VarOfRef : juce::var {
  explicit VarOfRef(juce::ReferenceCountedObject* p) : juce::var(p) {}
};


// The deterministic fixture kernel -- a decaying exponential with a damped
// ~5 Hz bump across the first 150 ms (reads like a short plate; exact, no
// RNG, no file). L is the reference; R rides 14% below it so the stereo
// pair is legible in the plot; mono shots ship envL only.
juce::Array<juce::var> fixtureEnvelope(double seconds) {
  juce::Array<juce::var> env;
  const int points = 256;
  for (int i = 0; i < points; ++i) {
    const double t = seconds * i / (points - 1);
    const double bump = 0.25 * std::cos(2.0 * M_PI * 5.0 * t) * std::exp(-t / 0.15);
    env.add(juce::var(std::exp(-2.8 * t) * (1.0 + bump)));
  }
  return env;
}

juce::Array<juce::var> scaled(const juce::Array<juce::var>& in, double factor) {
  juce::Array<juce::var> out;
  for (int i = 0; i < in.size(); ++i)
    out.add(juce::var((double)in[i] * factor));
  return out;
}

// The backend { sampleRate, length, channels, envL, envR } shape
// (backend/Backend.h; length in SAMPLES -- the tile divides by the sample
// rate to read seconds).
juce::var fixtureKernel(double seconds, bool stereo) {
  const auto envL = fixtureEnvelope(seconds);
  auto* o = new juce::DynamicObject();
  o->setProperty("sampleRate", 48000.0);
  o->setProperty("length", 48000.0 * seconds);
  o->setProperty("channels", stereo ? 2 : 1);
  o->setProperty("envL", juce::var(envL));
  if (stereo)
    o->setProperty("envR", juce::var(scaled(envL, 0.86)));
  return VarOfRef(o);  // owning var; the deleted const* overload is avoided
}

constexpr const char* kFixtureIr = "NEVO - EMT 140, 1.5s.wav";

ChainItem convItem(bool loaded, double seconds, const char* name = kFixtureIr) {
  ChainItem item;
  item.blockId = "shot-conv";
  item.isInsert = false;
  item.isEffect = true;
  item.effectKind = "convolution";
  if (name != nullptr)
    item.convIrName = name;
  if (loaded) {
    item.convIrLoaded = true;
    item.convSeconds = seconds;
  }
  return item;
}

ChainItem effectItem(const juce::String& kind) {
  ChainItem item;
  item.blockId = "shot-" + kind.toStdString();
  item.isInsert = false;
  item.isEffect = true;
  item.effectKind = kind;
  return item;
}

ChainItem namItem(bool downloading) {
  ChainItem item;
  item.blockId = "shot-nam";
  item.isInsert = false;
  item.isEffect = false;  // isTone()
  item.tone.id = 1101;
  item.tone.title = "Mesa/Boogie Mark V";
  item.tone.format = "nam";
  item.tone.models.push_back({21, "40W", juce::String()});
  item.tone.models.push_back({22, "25W", juce::String()});
  item.tone.modelsCount = 2;
  item.tone.a2ModelsCount = 2;
  item.activeModelId = downloading ? 22 : 21;
  item.loaded = !downloading;
  item.modelLoading = downloading;
  return item;
}

struct Shot {
  juce::String name;
  int size;
  juce::String state;
  std::function<ChainItem()> item;
  std::function<juce::var()> kernel;  // {} = the engine has no built kernel
  bool tone = false;
};

std::vector<Shot> shots() {
  const auto noKernel = [] { return juce::var(); };
  return {
      {"conv-full", gallery::kTileSize, "IR loaded (1.5 s stereo fixture kernel)",
       [] { return convItem(true, 1.5); }, [] { return fixtureKernel(1.5, true); }, false},
      {"conv-full-empty", gallery::kTileSize, "no IR built",
       [] { return convItem(false, 0.0, nullptr); }, noKernel, false},
      {"conv-full-long", gallery::kTileSize, "IR loaded at 4x length (6.0 s)",
       [] { return convItem(true, 6.0); }, [] { return fixtureKernel(6.0, true); }, false},
      {"conv-full-missing", gallery::kTileSize, "name set, engine has no IR (file missing)",
       [] { return convItem(false, 1.5); }, noKernel, false},
      {"conv-compact", gallery::kStereoTileSize, "stereo lanes (160 px), IR loaded",
       [] { return convItem(true, 1.5); }, [] { return fixtureKernel(1.5, true); }, false},
      {"conv-compact-mono", gallery::kStereoTileSize,
       "stereo lanes, MONO-IR kernel (one plot line)",
       [] { return convItem(true, 1.5); }, [] { return fixtureKernel(1.5, false); }, false},
      {"delay", gallery::kTileSize, "delay defaults (Digital/Ping)",
       [] { return effectItem("delay"); }, noKernel, false},
      {"delay-shift", gallery::kTileSize, "delay shifted (Tape mode, 750 ms, Fb 65%)",
       [] {
         auto i = effectItem("delay");
         i.delayTimeMs = 750.0;
         i.delayFeedback = 0.65;
         i.delayMode = 1;
         return i;
       },
       noKernel, false},
      {"chorus", gallery::kTileSize, "chorus defaults", [] { return effectItem("chorus"); },
       noKernel, false},
      {"chorus-shift", gallery::kTileSize, "chorus shifted (6 Hz, depth 8 ms, saw)",
       [] {
         auto i = effectItem("chorus");
         i.chorusRateHz = 6.0;
         i.chorusDepthMs = 8.0;
         i.chorusWave = 2;
         return i;
       },
       noKernel, false},
      {"comp", gallery::kTileSize, "compressor defaults (VCA, KNEE)",
       [] { return effectItem("compressor"); }, noKernel, false},
      {"comp-shift", gallery::kTileSize, "compressor shifted (Opto -> CLIP, ratio 6, thr -12)",
       [] {
         auto i = effectItem("compressor");
         i.compMode = 2;
         i.compRatio = 6.0;
         i.compThresholdDb = -12.0;
         return i;
       },
       noKernel, false},
      {"reverb", gallery::kTileSize, "reverb defaults (Digital)",
       [] { return effectItem("reverb"); }, noKernel, false},
      {"reverb-shift", gallery::kTileSize, "reverb shifted (Springs mode, 2400 ms decay)",
       [] {
         auto i = effectItem("reverb");
         i.reverbMode = 3;
         i.reverbDecayMs = 2400.0;
         return i;
       },
       noKernel, false},
      {"nam", gallery::kTileSize, "NAM tone, model 40W loaded", [] { return namItem(false); },
       noKernel, true},
      {"nam-shift", gallery::kTileSize, "NAM tone, model 25W downloading (loading-dots state)",
       [] { return namItem(true); }, noKernel, true},
  };
}

const Shot* findShot(const std::vector<Shot>& all, const juce::String& name) {
  for (const auto& s : all)
    if (s.name == name)
      return &s;
  return nullptr;
}

// The one backend difference vs. MockBackend: TileShot drives the conv
// kernel itself (deterministic fixture per shot) instead of reading a live
// engine.
class ShotBackend : public MockBackend {
public:
  explicit ShotBackend(const juce::var& scenario) : MockBackend(scenario) {}
  void setConvPreview(const juce::var& v) { convPreview_ = v; }
  juce::var getConvPreview(const std::string&) override { return convPreview_; }

private:
  juce::var convPreview_;
};

// Services demands a Shell (the window that resizes for the chrome strips);
// a shot has no window, so the shell records nothing.
struct NoopShell : public Shell {
  void setExtraContentHeight(int, int) override {}
};

// The whole services stack kept alive for the duration of one shot.
struct ShotWorld {
  ShotBackend backend;
  MockSession session;
  NoopShell shell;
  UiPrefs prefs;
  Services services;

  ShotWorld(const juce::var& scenario, const juce::var& fixtures)
      : backend(scenario),
        session(scenario, fixtures),
        services(backend, session, shell, prefs, /*updateNotice*/ false) {}
  void setKernel(const juce::var& k) { backend.setConvPreview(k); }
  Services& get() { return services; }
};

}  // namespace

int runList() {
  std::cout << "TileShot tiles:" << std::endl;
  for (const auto& s : shots())
    std::cout << "  " << s.name.paddedRight(' ', 20) << s.size << " px -- " << s.state << std::endl;
  return 0;
}

int runTile(const juce::StringArray& a) {
  const auto cwd = juce::File::getCurrentWorkingDirectory();
  juce::File out;
  int size = 0;
  float scale = 2.0f;  // retina, like --capture
  for (int i = 2; i < a.size(); ++i) {
    if (a[i] == "--out" && i + 1 < a.size())
      out = cwd.getChildFile(a[++i]);
    else if (a[i] == "--size" && i + 1 < a.size())
      size = a[++i].getIntValue();
    else if (a[i] == "--scale" && i + 1 < a.size())
      scale = (float)a[++i].getDoubleValue();
  }

  // The spec must outlive its lookups: shots() is a temporary, so copy it
  // first and point into the local (a findShot over shots() would dangle).
  const auto all = shots();
  const auto* spec = findShot(all, a[1]);
  if (spec == nullptr) {
    std::cerr << "unknown tile '" << a[1] << "' (TileShot --list)" << std::endl;
    return 2;
  }
  if (size <= 0)
    size = spec->size;
  if (out == juce::File())
    out = cwd.getChildFile(spec->name + ".png");

  // The testbed fixtures give the mock backend/session the same shape
  // --capture runs with; everything the tiles read is deterministic and
  // nothing touches the network.
  juce::var root, scenario;
  const auto fixturesFile =
      juce::File(juce::String(T3K_TESTBED_FIXTURES)).getChildFile("scenarios.json");
  if (fixturesFile.existsAsFile()) {
    if (const auto parsed = juce::JSON::parse(fixturesFile.loadFileAsString()); !parsed.isVoid()) {
      root = parsed;
      if (const auto* scenes = parsed.getProperty("scenarios", juce::var()).getArray())
        if (scenes->size() > 0)
          scenario = scenes->getReference(0);  // var(var) copy; var(const RCO*) is deleted here
    }
  }
  if (scenario.isVoid()) {
    juce::DynamicObject empty;
    scenario = VarOfRef(&empty);
  }
  if (root.isVoid())
    root = scenario;

  ShotWorld world(scenario, root);
  world.setKernel(spec->kernel());
  const auto item = spec->item();

  juce::Component host;
  std::unique_ptr<GalleryTile> tile;
  if (spec->tone)
    tile = std::make_unique<ToneTile>(world.get(), item, size);
  else
    tile = std::make_unique<EffectTile>(world.get(), item, size);
  tile->setBounds(0, 0, size, size);
  host.addAndMakeVisible(*tile);
  host.setSize(size, size);

  juce::MessageManager::getInstance()->runDispatchLoopUntil(400);  // async settle, like --capture
  const auto image = host.createComponentSnapshot(host.getLocalBounds(), true, scale);

  out.deleteFile();
  juce::PNGImageFormat png;
  if (juce::FileOutputStream stream(out); !stream.openedOk()) {
    std::cerr << "cannot write " << out.getFullPathName() << std::endl;
    return 2;
  } else if (!png.writeImageToStream(image, stream)) {
    std::cerr << "PNG encode failed for " << out.getFullPathName() << std::endl;
    return 2;
  }
  std::cout << spec->name << " -> " << out.getFullPathName() << " (" << image.getWidth() << "x"
            << image.getHeight() << ")" << std::endl;
  return 0;
}

// --dump <tile>: every child control's bounds, for layout bugs. The pixels
// are the final word, but the component tree says WHERE each control actually
// is — the fastest way to find an overlap without squinting.
static juce::var shotFixtures(juce::var& rootOut) {
  juce::var scenario;
  const auto fixturesFile =
      juce::File(juce::String(T3K_TESTBED_FIXTURES)).getChildFile("scenarios.json");
  if (fixturesFile.existsAsFile()) {
    if (const auto parsed = juce::JSON::parse(fixturesFile.loadFileAsString()); !parsed.isVoid()) {
      rootOut = parsed;
      if (const auto* scenes = parsed.getProperty("scenarios", juce::var()).getArray())
        if (scenes->size() > 0)
          scenario = scenes->getReference(0);  // var(var) copy; var(const RCO*) is deleted here
    }
  }
  if (scenario.isVoid()) {
    juce::DynamicObject empty;
    scenario = VarOfRef(&empty);
  }
  if (rootOut.isVoid())
    rootOut = scenario;
  return scenario;
}
int runDump(const juce::StringArray& a) {
  if (a.size() < 2) {
    std::cout << "usage: TileShot --dump <tile>\n";
    return 2;
  }
  const auto all = shots();
  const auto* spec = findShot(all, a[1]);
  if (spec == nullptr) {
    std::cerr << "unknown tile '" << a[1] << "' (TileShot --list)" << std::endl;
    return 2;
  }
  juce::var root;
  const auto scenario = shotFixtures(root);
  ShotWorld world(scenario, root);
  world.setKernel(spec->kernel());
  const auto item = spec->item();
  juce::Component host;
  std::unique_ptr<GalleryTile> tile;
  if (spec->tone)
    tile = std::make_unique<ToneTile>(world.get(), item, spec->size);
  else
    tile = std::make_unique<EffectTile>(world.get(), item, spec->size);
  tile->setBounds(0, 0, spec->size, spec->size);
  host.addAndMakeVisible(*tile);
  host.setSize(spec->size, spec->size);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(400);

  juce::String rep;
  std::function<void(juce::Component*, const juce::String&)> report =
      [&](juce::Component* c, const juce::String& indent) {
        juce::String tag = c->getName();
        if (tag.isEmpty())
          tag = c->getTitle();
        if (tag.isEmpty()) {
          auto* btn = dynamic_cast<juce::Button*>(c);
          if (btn != nullptr)
            tag = btn->getButtonText();
        }
        rep << indent << (tag.isEmpty() ? "(anon)" : tag)
            << "  @" << c->getX() << "," << c->getY() << "  " << c->getWidth() << "x" << c->getHeight()
            << (c->isVisible() ? "" : "  HIDDEN") << "\n";
        for (int i = 0; i < c->getNumChildComponents(); ++i)
          report(c->getChildComponent(i), indent + juce::String("  "));
      };
  report(tile.get(), juce::String());
  std::cout << rep.toStdString() << std::endl;
  return 0;
}

// runGolden / runRegen / TileShotApp continue the namespace t3k::ui::testbed

int runGolden(const juce::StringArray& a) {
  if (a.size() < 3) {
    std::cerr << "usage: TileShot --golden <candidate.png> <reference.png> [--tol N] [--diff d.png]"
              << std::endl;
    return 2;
  }
  const auto cwd = juce::File::getCurrentWorkingDirectory();
  int tolerance = 24;  // Compare's default: absorbs sub-pixel AA, not moved pixels
  juce::File diff;
  for (int i = 3; i < a.size(); ++i) {
    if (a[i] == "--tol" && i + 1 < a.size())
      tolerance = a[++i].getIntValue();
    else if (a[i] == "--diff" && i + 1 < a.size())
      diff = cwd.getChildFile(a[++i]);
  }
  const auto reference = cwd.getChildFile(a[2]);
  const auto candidate = cwd.getChildFile(a[1]);

  auto result = comparePngFiles(reference, candidate, diff == juce::File() ? juce::File() : diff);
  if (!result.ok) {
    std::cerr << "FAIL  " << result.error << std::endl;
    return 2;
  }
  const auto b = result.diffBounds();
  std::cout << juce::String::formatted(
                    "%d px mismatched of %dx%d (%.3f%%) | diff region (%d,%d)-(%d,%d) | "
                    "worst 64px tile %.1f%% at (%d,%d)",
                    static_cast<int>(result.mismatched), result.width, result.height,
                    result.mismatchPercent(), b.x0, b.y0, b.x1, b.y1, result.worstTilePercent,
                    result.worstTile.x, result.worstTile.y)
               << std::endl;
  if (result.mismatched > 0) {
    if (diff != juce::File())
      std::cout << "diff image: " << diff.getFullPathName() << std::endl;
    std::cout << "FAIL" << std::endl;
    return 1;
  }
  std::cout << "PASS" << std::endl;
  return 0;
}

int runRegen(const juce::StringArray& a) {
  juce::File golden;
  juce::File source;
  for (int i = 1; i < a.size(); ++i) {  // the golden's position is a[1]
    if (a[i] == "--from" && i + 1 < a.size())
      source = juce::File::getCurrentWorkingDirectory().getChildFile(a[++i]);
    else if (a[i] != "--regen" && golden == juce::File())
      golden = juce::File::getCurrentWorkingDirectory().getChildFile(a[i]);
  }
  if (golden == juce::File() || source == juce::File() || !source.existsAsFile()) {
    std::cerr << "usage: TileShot --regen <golden.png> --from <shot.png>" << std::endl;
    return 2;
  }
  if (golden == source) {
    std::cerr << "refusing to regen onto its own source" << std::endl;
    return 2;
  }
  golden.getParentDirectory().createDirectory();
  golden.deleteFile();
  if (!source.copyFileTo(golden)) {
    std::cerr << "copy failed: " << golden.getFullPathName() << std::endl;
    return 2;
  }
  std::cout << "golden re-approved: " << golden.getFullPathName() << std::endl;
  std::cout << "(deliberate act -- review the pixels before committing; --regen never runs in CI)"
            << std::endl;
  return 0;
}

class TileShotApp : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return "TileShot"; }
  const juce::String getApplicationVersion() override { return "1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String& commandLine) override {
    int code = 2;
    const auto args = juce::StringArray::fromTokens(commandLine, true);
    if (args.size() >= 2 && args[0] == "--tile")
      code = runTile(args);
    else if (args.size() >= 3 && args[0] == "--golden")
      code = runGolden(args);
    else if (args.size() >= 2 && args[0] == "--regen")
      code = runRegen(args);
    else if (args.size() >= 2 && args[0] == "--dump")
      code = runDump(args);
    else if (args.size() == 1 && args[0] == "--list")
      code = runList();
    else
      std::cerr << "TileShot --list | --tile <name> [--size 224] [--scale 2] [--out f.png]\n"
                   "          --dump <name>          (child-control bounds, for layout bugs)\n"
                   "          --golden <candidate> <reference> [--tol N] [--diff d.png]\n"
                   "          --regen <golden> --from <shot>\n"
              << std::endl;
    setApplicationReturnValue(code);
    quit();
  }
  void shutdown() override {}
};

}  // namespace t3k::ui::testbed

START_JUCE_APPLICATION(t3k::ui::testbed::TileShotApp)
