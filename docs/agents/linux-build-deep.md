# Linux build deep-dive — TONE3000 (GUI link deps, reconfigure, VST3 helper)

> **Load before:** fresh `cmake` configure of the full plugin, GUI (Standalone/
> VST3/CLAP/LV2) link failures (`snd_*`/`Xau*`/`freetype`), VST3 helper build,
> or adding/removing any source file.
> **The one line that matters:** ALSA `-lasound` comes from **pkg-config**
> (`stage/usr/bin/pkg-config` is a *shim* — **do NOT revert it**), `FindX11`
> needs explicit `-DX11_X11_*`, and the VST3 **helper** is a separate CMake
> project that must be reconfigured with `--sysroot` itself.
> **OpenWiki mirrors (descriptive):** [`openwiki/build-and-ops/dsp-test-suite.md`](../../openwiki/build-and-ops/dsp-test-suite.md) (run loop, real-source rule,
> silent-0 trap, fixtures) — this file keeps the link-dep / configure / helper RULES.

(AGENTS.md sub-rule — the index is the repo-root `AGENTS.md`.)

## If you're also touching…
- A GUI-link error that came from adding a new knob/param/field in the same
  session → also open `docs/agents/ui-wiring.md`; the link error and the wiring
  contract often share one root cause (the param never made it through the
  four state places).

## GUI (Standalone / VST3 / CLAP / LV2) build
```bash
cmake --build build --target TONE3000_Standalone   # also _VST3 / _CLAP / _LV2
# artifact: build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000
```
This build compiles the audio-device + X11 backends, so its link deps matter
(the DspTests target does not — it stays pure-DSP and link-light):

| Dep | How it resolves here |
|---|---|
| **X11** | Headers + `-lX11` via `/home/jambo/x11dev` (symlinks→stage). `libxcb.so.1` from stage. **`libXau.so.6` + `libXdmcp.so.6`** must be resolvable at link (copied from `/usr/lib/x86_64-linux-gnu` into the stage lib dir). |
| **ALSA** | A REAL link dep for this build; `-lasound` comes from **pkg-config** `alsa.pc` (NOT from find_library). Undefined `snd_*` / `Xau*` at link ⇒ ALSA/X11 below, **never** curl/ALSA-jack. |
| **JACK & curl** | LAZY (dlopen at runtime) → headers only, NO `-l` entry needed. |

`FindX11` won't infer `--sysroot`: pass `X11_X11_INCLUDE_PATH/DIR` +
`X11_X11_LIB` explicitly (as the configure recipes below do).

**Running the GUI build** needs the stage runtime libs + X11:
`LD_LIBRARY_PATH=<stage>/usr/lib/x86_64-linux-gnu:/home/jambo/x11dev
<…>/Standalone/TONE3000` — in a headless session it reaches ALSA init and
blocks on display/audio — **that is expected** (audio stack working), not a
build failure. Health = `ldd` (0 "not found") + `readelf -d`.

## pkg-config shim (do NOT revert)
`stage/usr/bin/pkg-config` is a self-sufficient **shim** (the real binary is
preserved as `pkg-config.real`) that sets `LD_LIBRARY_PATH` + `PKG_CONFIG_PATH`
before exec'ing it. Exists because CMake cleans `LD_LIBRARY_PATH` from the
sub-process env and JUCE's `juceaide` sub-invoke re-detects pkg-config in its
OWN CMake — which can't take `-DPKG_CONFIG_EXECUTABLE`.

## Adding sources / reconfigure traps
Any change to the source GLOBs (adding a `.cpp`) re-runs `juceaide`, which
needs the vendored freetype on its search path (no system freetype dev was
installed): `export CPATH=/home/jambo/dev/tone3000-plugin-main/libs/freetype/include`.
**Never hand-edit `build/build.ninja`** — reconfigure regenerates it and your
edit is silently gone. In `plugin/CMakeLists.txt`, the `nam` lib **must** list
`nam_file.cpp` + `wav.cpp` (else `validate_nam_file` / `detail::load_wav_ir`
link errors). `FindX11` won't infer `--sysroot` → pass `X11_X11_INCLUDE_PATH/DIR`
+ `X11_X11_LIB` explicitly.

## Fresh configure (only if the build dir is missing/stale)
DspTests:
```bash
STAGE=/home/jambo/buildkit/stage
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-gcc-16 \
  -DCMAKE_CXX_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-g++-16 \
  -DCMAKE_C_FLAGS="--sysroot=$STAGE" -DCMAKE_CXX_FLAGS="--sysroot=$STAGE" \
  -DX11_X11_INCLUDE_PATH=/usr/include -DX11_X11_LIB=/usr/lib/x86_64-linux-gnu/libX11.so
```
Full plugin (the three pkg-config knobs are what makes ALSA resolve):
```bash
STAGE=/home/jambo/buildkit/stage
LD_LIBRARY_PATH=$STAGE/usr/lib/x86_64-linux-gnu \
PKG_CONFIG_PATH=$STAGE/usr/lib/x86_64-linux-gnu/pkgconfig \
PATH=$STAGE/usr/bin:$PATH \
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-gcc-16 \
    -DCMAKE_CXX_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-g++-16 \
    -DCMAKE_C_FLAGS="--sysroot=$STAGE" -DCMAKE_CXX_FLAGS="--sysroot=$STAGE" \
    -DCMAKE_PREFIX_PATH=$STAGE -DPKG_CONFIG_EXECUTABLE=$STAGE/usr/bin/pkg-config \
    -DX11_X11_INCLUDE_DIR=/home/jambo/x11dev/include -DX11_X11_LIB=/home/jambo/x11dev/libX11.so
```

## VST3 helper (`TONE3000_vst3_helper`) — two persistent fixes (2026-10-03, keep)
It is a **separate CMake sub-project** (`build/vst3_helpers/TONE3000/`) that
does NOT inherit the main build's `CMAKE_CXX_FLAGS`:
1. Missing `--sysroot` → `bits/wordsize.h: No such file` (its includedir had
   an incomplete glibc tree shadowing the stage's). Fix: reconfigure the
   helper sub-project **with** `-DCMAKE_CXX_FLAGS="--sysroot=$STAGE"` etc. (it
   persists in `build/vst3_helpers/TONE3000/CMakeCache.txt`; the main build's
   own rebuild of the helper does NOT pass the flag):
   ```bash
   STAGE=/home/jambo/buildkit/stage; SR="--sysroot=$STAGE"; cd build/plugin
   cmake -GNinja -S ../../libs/juce/extras/Build/CMake/juce_vst3_helper \
     -B ../../build/vst3_helpers/TONE3000 -Dhelper_name=vst3_helper \
     -Dsource_file=../../libs/juce/modules/juce_audio_plugin_client/VST3/juce_VST3ManifestHelper.cpp \
     -Dshared_defs_file=../../build/vst3_helpers/TONE3000/shared_defs_Release.txt \
     -Dshared_incs_file=../../build/vst3_helpers/TONE3000/shared_incs_Release.txt \
     -DCMAKE_CXX_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-g++-16 \
     -DCMAKE_C_FLAGS="$SR" -DCMAKE_CXX_FLAGS="$SR" -DCMAKE_EXE_LINKER_FLAGS="$SR"
   cmake --build ../../build/vst3_helpers/TONE3000
   ```
2. Linking this *executable* needs host `libc_nonshared.a` (host `libc6-dev`
   absent; the plugin `.so` is shared and does not hit it). Fixed ONCE by
   copying it into the host dir (`libc_nonshared.a` from the stage into
   `/usr/lib/x86_64-linux-gnu`) — if it goes missing again, redo that one copy.
   (sudo password: in the local global rules — never commit it to this repo.)
