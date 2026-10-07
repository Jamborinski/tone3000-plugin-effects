# Windows builds (MinGW cross) — TONE3000

> **Load before:** building or staging any Windows `.exe`.
> **The one line that matters:** stage with `scripts/win-standalone.sh` →
> `Downloads\TONE3000-win-YYYYMMDD-HHMMSS` (**run-timestamp only, never
> descriptive suffixes**); **ASIO is a HARD user requirement**; keep the two
> `T3K_MINGW_*` CMake patches.

(AGENTS.md sub-rule — the index is the repo-root `AGENTS.md`.)

## If you're also touching…
- The Windows build is recompiling UI sources you just changed (new knob/param)
  → also open `docs/agents/ui-wiring.md`.

`build-win/` is pre-configured (`CMAKE_SYSTEM_NAME=Windows`,
`x86_64-w64-mingw32-g++-posix`, Ninja, Release) — a plain build just works:
```bash
cmake --build build-win --target TONE3000_Standalone    # also TONE3000_VST3
# artifact: build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
```

## Rules
- Exe is self-contained (static MinGW runtime) — verify: `file` =
  `PE32+ … x86-64`; `objdump -x | grep 'DLL Name'` lists **only** Windows API
  DLLs (no `libstdc++*`/`libgcc*`/`libwinpthread*`).
- **ASIO is a HARD user requirement** — never ship it off; if a build lost it,
  fix the build. ON by default via JUCE's bundled ASIO SDK (no external SDK to
  install).
- Two configure-time patches in ROOT `CMakeLists.txt` (idempotent) — **keep,
  never delete**:
  - `T3K_MINGW_ASIO_SEH`: C SEH (`__try/__except`) not accepted in C++/MinGW →
    patched to `if/else` (`juce_audio_devices/asio/WINASIO.cpp`); ASIO needs it.
  - `T3K_MINGW_DWRITE_CRP`: MinGW DirectWrite header hides the 6-arg
    `IDWriteFactory::CreateCustomRenderingParams` behind the interface's COM
    vtable; route the call through the base interface
    (patched in `juce_graphics/fonts/juce_DirectWriteTypeface.cpp`).

- MinGW's libstdc++ has **no** `std::tanhf`/`std::floorf` — use
  `std::tanh(x)`/`std::floor(x)` with float args instead (the rest of the
  codebase's style). Windows-only build error
  `no matching function for 'tanhf'` = this.

## Staging to Windows Downloads
`bash /home/jambo/dev/tone3000-plugin-main/scripts/win-standalone.sh` →
`C:\Users\jambo\Downloads\TONE3000-win-YYYYMMDD-HHMMSS\TONE3000.exe`.
Older descriptive folders in Downloads are historical A/B references — do not
touch them. Naming is **run-timestamp only** (user preference).

## In-place reconfigure (fast path, after `.env` / source changes)
A plain build does NOT pick up a changed `.env` (publishable key) or new
sources — it needs a **reconfigure** (the GLOB re-runs). ~2 min, JUCE objects
rebuild (no re-download):
```bash
source /home/jambo/buildkit/winbuild_env.sh     # toolchain on PATH + LD_LIBRARY_PATH
MROOT=/home/jambo/buildkit/mingw/root
cmake -S . -B build-win -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
  -DCMAKE_C_COMPILER="$MROOT/usr/bin/x86_64-w64-mingw32-gcc-posix" \
  -DCMAKE_CXX_COMPILER="$MROOT/usr/bin/x86_64-w64-mingw32-g++-posix" \
  -DCMAKE_RC_COMPILER="$MROOT/usr/bin/x86_64-w64-mingw32-windres" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_AAX=OFF -DBUILD_LV2=OFF -DBUILD_CLAP=OFF
cmake --build build-win --target TONE3000_Standalone --parallel "$(nproc)"
```
Full reconfigure (`rm -rf build-win`, ~30 min download included) is only for a
corrupted cache: `/home/jambo/buildkit/build-win-standalone.sh`.

## Wine smoke test (WSLg display `:0`, `wine64` in the 26.04 repos)
```bash
WINEPREFIX=/home/jambo/wine-test WINEDEBUG=-all DISPLAY=:0 wineboot -u
timeout --signal=KILL 30 wine64 build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
WINEPREFIX=/home/jambo/wine-test wineserver -k
```
**exit 124 = success** (had to SIGKILL a live app). `ALSA … /dev/snd/seq …
No such file` is expected (no audio device) and graceful. Wine has NO real
ASIO driver — this proves launch, not the ASIO dropdown entry (only real
Windows verifies the combo). Static proof the type is compiled in:
`strings TONE3000.exe | grep -i ASIOAudioIODevice`.
