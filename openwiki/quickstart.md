---
type: quickstart
title: Quickstart
description: What TONE3000 is — a cross-platform JUCE audio plugin that loads Neural Amp Model (NAM) captures and impulse responses (IRs) from the TONE3000 catalog or local files into a user-built signal chain — and how to configure, build, and test the repo.
tags: [quickstart, build, audio-plugin, nam, impulse-response, cmake, dsp-tests]
sources:
  - id: openwiki-source-5f5b95b3d6a215fa02ceb945
    resource: repo://.env.example
  - id: openwiki-source-8037e2358a2c4f9b2c722a11
    resource: repo://AGENTS.md
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-7bffef5a8b4bf505c090b70d
    resource: repo://docs/agents/dsp-invariants.md
  - id: openwiki-source-54aa3f4134fa6c214762f246
    resource: repo://docs/agents/windows-build.md
  - id: openwiki-source-e2fb14576702431774114c7c
    resource: repo://plugin/include/ChainBlock.h
  - id: openwiki-source-732fd101392931455320572e
    resource: repo://plugin/include/Processor.h
  - id: openwiki-source-e592877ce1adba0c2539910f
    resource: repo://plugin/ui/NativeUi.cmake
  - id: openwiki-source-01229dcf87d08fe26802a88a
    resource: repo://plugin/ui/README.md
  - id: openwiki-source-3f6e8a6ed2edd60ef99f8565
    resource: repo://plugin/ui/testbed/CMakeLists.txt
  - id: openwiki-source-23775c3de52f3ab95a13cb8b
    resource: repo://README.md
  - id: openwiki-source-fbe20d668cd183c9fb53adb2
    resource: repo://script/test-dsp.sh
  - id: openwiki-source-4f7fb275f8268e0506e67d23
    resource: repo://scripts/win-standalone.sh
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
  - id: openwiki-source-0368da5a39fa50284e393846
    resource: repo://test/src/plate_family_tests.cpp
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T20:30:10.043Z
---

# TONE3000

A JUCE 9 audio plugin (VST3, AU, CLAP, LV2, Standalone, plus iOS Standalone) that
loads **Neural Amp Modeler (NAM) A2/A3 captures** and **cabinet IRs (`.wav`)**
from the [TONE3000 catalog](https://www.tone3000.com) or from local files,
stacks them into a user-built signal chain, and applies per-block EQ, gain,
and mix. The UI is JUCE/C++ (namespace `t3k::ui`) drawn natively on every
platform — no browser engine, no web runtime, no extra install.

Processing comes from two in-tree C++ dependencies:
**NeuralAmpModelerCore** (`plugin/NeuralAmpModelerCore`) for the neural amp
model, and **AudioDSPTools** (`plugin/AudioDSPTools`) for resampling.
DSP is C++20.

## Repo layout, top level

| Directory / file | What it is |
|---|---|
| `plugin/include/`, `plugin/src/` | The processor and the DSP blocks (NAM, IR, Effect) |
| `plugin/ui/` | The native JUCE UI (namespace `t3k::ui`) — see `plugin/ui/README.md` |
| `plugin/ui/testbed/` | Standalone testbed app: renders `PluginRoot` on mock backend, pixel-diffs, `--selftest` |
| `plugin/NeuralAmpModelerCore/` | In-tree NAM DSP (fetched / hand-populated) |
| `plugin/AudioDSPTools/` | In-tree AudioDSPTools (resampling, wav, dsp) |
| `plugin/docs/` | Design notes per DSP block (reverb-modes.md, delay-modes.md, oversampling.md, …) |
| `docs/agents/` | Per-area rule corpus the agent must read before editing (dsp-invariants, ui-wiring, windows-build, …) |
| `test/`, `test/src/` | GoogleTest DSP suite (DspTests) that compiles the real plugin sources |
| `test/files/` | Fixture NAM models + IR WAVs (a2-amp-test.nam, cab-ir-test.wav, reverb-ir-mono/stereo wavs, em240-gold-plate-5s.wav) |
| `tools/` | `preset_tool.cpp` (regenerates shipped factory presets) |
| `script/test-dsp.sh` | One-liner: configure-if-needed → build → run DspTests |
| `CMakePresets.json`, `CMakeLists.txt` | CMake 3.22+ project (C++20, MSVC /MT static on Windows, Linux toolchain, iOS presets) |
| `.env.example` → `.env` | CMake configure-time keys (`T3K_PUBLISHABLE_KEY`, `T3K_API_DOMAIN`, `T3K_UPDATE_NOTICE`) — the plugin reads them via the generated `T3kConfig.h` |
| `VERSION` | Single source of truth for the plugin version; CMake reconfigures on change |

## Configure a build

Requirements: CMake ≥ 3.22.1, Git, a C++20 compiler (Xcode / MSVC / GCC-15+
/ Clang), and (on Linux) the dev packages listed in
`.github/workflows/build.yml` (`libgtk-3-dev`, `libasound2-dev`,
`libjack-jackd2-dev`, `libcurl4-openssl-dev`, X11 `-dev` set).

```sh
# 1. Submodules / in-tree dependencies
git submodule update --init --recursive

# 2. Configure (JUCE + FreeType + googletest are fetched into libs/ on first configure)
# Windows: use MSVC or the MinGW toolchain documented in docs/agents/windows-build.md
# Linux (recommended): use the project toolchain file
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/linux-toolchain.cmake

# Optional switches
#   -DHEADLESS=ON                       headless / no-GUI target
#   -DBUILD_AAX=OFF -DBUILD_LV2=OFF     drop formats you don't need
#   -DT3K_BUILD_UI_TESTBED=ON           add the UiTestbed app (scenario captures, pixel diffs)

# 3. Build
cmake --build build
```

Artefacts land under `build/plugin/TONE3000_artefacts/<config>/<format>/`.
Standalone lives at `build/plugin/TONE3000_artefacts/Release/Standalone/`.

**Linux runtime deps** (the release tarball's `install.sh --check` verifies):
GTK3 (file dialogs), ALSA, fontconfig, X11, and libcurl (loaded lazily by
SONAME — no `-dev` package needed on an end-user machine).

**FreeType is deliberately static** — built from source (CPM-pinned in the
root `CMakeLists.txt`) and linked into every Linux GUI binary with local
symbols. The reason: hosts like Ardour / Mixbus bundle an older FreeType in
`/opt/<host>/lib`, and resolving our 2.13 imports against their Debian-11
copy dies with `undefined symbol: FT_Get_Paint` (issue #181).

## TONE3000 account key (required for the cloud-browse feature)

The plugin reads `.env` (or `.env.local`, or an env variable) at
**configure time** into the generated `T3kConfig.h`.
Minimum required key:

```sh
# .env     (copy .env.example)
T3K_PUBLISHABLE_KEY=t3k_pub_...        # TONE3000 > Settings > API Keys
# Optional:
# T3K_API_DOMAIN=https://staging.tone3000.com
# T3K_UPDATE_NOTICE=true
```

Sign-in opens the system browser; the redirect URL is
`http://localhost:<ephemeral-port>/` and is auto-allowed for publishable
keys. The publishable key is the OAuth client ID; the token pair is stored
in the UiPrefs file per user.

## Run it

**Standalone:**

```sh
cd build/plugin/TONE3000_artefacts/Release/Standalone
open ./TONE3000.app       # macOS
./TONE3000                # Linux
./TONE3000.exe            # Windows
```

**Install into a DAW** (same folder the official installer writes to):

```sh
./script/install-plugin.sh VST3          # macOS / Linux
./script/install-plugin.sh VST3 Debug    # Debug build
```

| OS      | Format | Default install folder |
|---|---|---|
| macOS   | VST3   | `~/Library/Audio/Plug-Ins/VST3/` |
| macOS   | CLAP   | `~/Library/Audio/Plug-Ins/CLAP/` |
| Windows | VST3   | `C:\Program Files\Common Files\VST3\` |
| Linux   | VST3   | `~/.vst3/` |
| Linux   | LV2    | `~/.lv2/` |

Rescan the DAW afterwards.

## DSP tests (the fast loop)

```sh
./script/test-dsp.sh                        # everything in the suite
./script/test-dsp.sh 'ChainOversampler*'    # gtest filter
```

That's: configure-if-needed → `cmake --build build --target DspTests` →
`build/test/DspTests_artefacts/.../DspTests`.

The suite compiles **the real plugin sources** (not copies) against the
fixtures in `test/files/`. It only pulls in one UI file (
`plugin/ui/core/Labels.cpp`, the `KnobScale` readout formatter for the
scale-contract tests), so a green DspTests does not prove the UI links.

## UI testbed (optional, UI iteration)

```sh
cmake -S plugin/ui/testbed -B build-ui -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ui -j
# Run the app; use --selftest, --bench, or --compare <golden>
```

See the working guide in `plugin/ui/README.md` and the design record in
`plugin/docs/native-ui.md`.

## Windows (MinGW cross-compile)

Standalone `.exe` staging is in `scripts/win-standalone.sh`, and the
authoritative reference is `docs/agents/windows-build.md`. Staging produces
`TONE3000-win-YYYYMMDD-HHMMSS` (run-timestamp name only, no descriptive
suffix — user preference). ASIO is a hard requirement for the plugin; Wine
is available on this machine for smoke-testing the staged exe.

## Where to go next

- **Audio path** — `openwiki/architecture/audio-path.md`: the 48 kHz
  chain-domain boundary, oversampling, dual lanes, `ChainBlock` types, wet
  fades, and the IR base-rate island.
- **NAM / model loading** — `plugin/include/NamEngine.h`,
  `plugin/src/ProcessorModelLoader.cpp`.
- **IR + BudgetConvolver** — `plugin/src/BudgetConvolver.cpp` for the
  long-IR OLA-tail schedule; `test/files/cab-ir-test.wav` for the fixture.
- **Built-in effects** — `plugin/include/{Delay,Chorus,Tremolo,Compressor,Reverb,ConvolutionReverb}.h`
  together with `docs/agents/dsp-invariants.md` for the CONSIDERED &
  DECLINED contracts.
- **Knob-to-DSP wiring** — `plugin/ui/core/KnobScale.h`,
  `plugin/ui/services/ParamBinding.h`, and `docs/agents/ui-wiring.md` for
  the "four state places" rule on new block fields.
- **Presets** — `.t3kpreset` framing in `plugin/include/PresetFile.h`,
  `plugin/include/PresetManager.h`, `tools/preset_tool.cpp`.
- **TONE3000 Cloud session** — `plugin/ui/services/Tone3000Session.h`,
  `Tone3000Client.h`, `ConnectionGate.h`.
- **Native UI** — `plugin/ui/views/PluginRoot.h`, `plugin/ui/NativeEditor.h`,
  and the working guide at `plugin/ui/README.md`.
- **DSP test suite detail** — `openwiki/build-and-ops/dsp-test-suite.md`.
- **Windows release** — `openwiki/build-and-ops/windows-release.md`.
