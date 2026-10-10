---
type: "Reference"
title: "Windows release pipeline"
openwiki_generated: true
sources:
  - id: openwiki-source-d44494ef3e497fea81240ef8
    resource: repo://CMakeLists.txt
  - id: openwiki-source-54aa3f4134fa6c214762f246
    resource: repo://docs/agents/windows-build.md
  - id: openwiki-source-4f7fb275f8268e0506e67d23
    resource: repo://scripts/win-standalone.sh
  - id: openwiki-source-4b8e7dce368774e92d99ea30
    resource: repo://test/CMakeLists.txt
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T20:30:10.043Z
---


# Windows release pipeline

TONE3000 ships a standalone Windows `.exe` that is cross-compiled from WSL
with MinGW (the shipped Windows build; building from native MSVC on Windows
is a separate, untested path). The authoritative reference for every detail
below is
`docs/agents/windows-build.md` — read it before building or staging.

## The build tree

`build-win/` is a pre-configured CMake build tree:
`CMAKE_SYSTEM_NAME=Windows`, `x86_64-w64-mingw32-g++-posix` (the **posix**
variant, from `/home/jambo/buildkit/mingw/root`), Ninja, Release. A plain
build just works:

```bash
cmake --build build-win --target TONE3000_Standalone   # also TONE3000_VST3
# artifact: build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
```

The exe is **self-contained** (static MinGW runtime). Verify with:
`file` → `PE32+ executable … x86-64`; `objdump -x | grep 'DLL Name'` → only
Windows API DLLs (kernel32, d3d11, dwmapi, …), **no** `libstdc++*`,
`libgcc*`, or `libwinpthread*`.

## Two configure-time patches — keep, never delete

ROOT `CMakeLists.txt` carries two idempotent workarounds for MinGW:

- **`T3K_MINGW_ASIO_SEH`** — the ASIO SDK's C-style SEH (`__try/__except`) is
  not accepted in C++ under MinGW; it is patched to `if/else`
  (`juce_audio_devices/asio/WINASIO.cpp`). ASIO cannot load without it.
- **`T3K_MINGW_DWRITE_CRP`** — the MinGW DirectWrite header hides the 6-arg
  `IDWriteFactory::CreateCustomRenderingParams` behind the interface's COM
  vtable; the call is routed through the base interface (patched in
  `juce_graphics/fonts/juce_DirectWriteTypeface.cpp`).

Related MinGW constraint: its libstdc++ has **no** `std::tanhf`/`std::floorf`
— the codebase uses `std::tanh(x)`/`std::floor(x)` with `float` args instead.
A Windows-only build error `no matching function for 'tanhf'` is this.

## ASIO is a hard requirement

**ASIO is a HARD user requirement — never ship it off.** It is ON by default
via JUCE's bundled ASIO SDK (no external SDK to install). If a build lost it,
fix the build. Static proof it is compiled in:
`strings TONE3000.exe | grep -i ASIOAudioIODevice`.

## Staging to Windows Downloads

```bash
bash /home/jambo/dev/tone3000-plugin-main/scripts/win-standalone.sh
```

Stages `TONE3000.exe` to
`C:\Users\jambo\Downloads\TONE3000-win-YYYYMMDD-HHMMSS\TONE3000.exe`.
The naming is **run-timestamp only** (user preference) — never append
descriptive suffixes. The script:
builds `TONE3000_Standalone`, confirms the artifact + computes an md5, prints
the git branch and HEAD commit for provenance, then copies the exe to the
timestamped Downloads folder (with `-2`, `-3`, … de-duplication if already
present). Older descriptive folders in Downloads are historical A/B
references — do not touch them.

## In-place reconfigure (fast path)

A plain build does **not** pick up a changed `.env` (publishable key) or new
sources — it needs a **reconfigure** (the GLOB re-runs), ~2 min, JUCE objects
rebuild (no re-download). The recipe (with `buildkit/winbuild_env.sh` on PATH)
is in `docs/agents/windows-build.md`. A full reconfigure
(`rm -rf build-win`, ~30 min with the download) is only for a corrupted
cache.

## Wine smoke test

Launches the exe on the WSLg display with the system `wine64`:

```bash
WINEPREFIX=/home/jambo/wine-test WINEDEBUG=-all DISPLAY=:0 wineboot -u
timeout --signal=KILL 30 wine64 build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
WINEPREFIX=/home/jambo/wine-test wineserver -k
```

**exit 124 = success** (the app kept running and had to be SIGKILLed). An
`ALSA … /dev/snd/seq … No such file` line is expected (no audio device) and
graceful. Caveat: Wine has **no real ASIO driver** — this proves launch, not
the ASIO dropdown entry; only real Windows verifies the combo.
