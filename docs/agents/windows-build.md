# Windows builds (MinGW cross) — TONE3000

> **Load before:** building or staging any Windows `.exe`.
> **The one line that matters:** stage with `script/win-standalone.sh` →
> `Downloads\TONE3000-win-YYYYMMDD-HHMMSS` (**run-timestamp only, never
> descriptive suffixes**); **ASIO is a HARD user requirement**; keep the two
> `T3K_MINGW_*` CMake patches.
> **OpenWiki mirror (descriptive):** [`openwiki/build-and-ops/windows-release.md`](../../openwiki/build-and-ops/windows-release.md)
> — the cross-build/staging/verification steps as evidence-linked claims; this file
> stays the authoritative RULE set.

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

## Log triage + closeout rule
- **Build logs: eigen `-Wzero-as-null-pointer-constant` warnings are NON-FATAL noise** — the real failure is the FIRST `error:` line; grep for it, don't chase the latest warning cluster.
- **CLOSEOUT RULE (user-mandated):** every ticket closeout ENDS by staging a fresh standalone Windows exe via `script/win-standalone.sh` after DspTests + selftest + goldens are green and the commit is in, recording the staging dir + md5 in the ticket log. Don't close a shipped ticket without it.

## Staging to Windows Downloads
`bash script/win-standalone.sh` (from the repo root; the script loads `.env`,
gitignored — keys/paths in `.env.example`) →
`C:\Users\jambo\Downloads\TONE3000-win-YYYYMMDD-HHMMSS\TONE3000.exe`.
Older descriptive folders in Downloads are historical A/B references — do not
touch them. Naming is **run-timestamp only** (user preference).

- **`T3K_STAGE_DIR` gotcha (set it in your local `.env`):** the script's default
  `ls -d /mnt/c/Users/*/Downloads | head -1` glob picks the FIRST alphabetical
  user dir, which on this box is `/mnt/c/Users/Default User/Downloads` (a
  broken/empty stub) — NOT `jambo`. Set
  `T3K_STAGE_DIR=/mnt/c/Users/jambo/Downloads` in your local `.env`
  (template line in `.env.example`) so every Stage A closeout lands in the
  real Downloads. Symptom if unset: the staging "succeeds" into the wrong user
  folder and the exe is missing from `C:\Users\jambo\Downloads`.
  (Fired 2026-10-20: `.env` had no key → the closeout exe landed nowhere;
  re-ran with the env override, recorded `TONE3000-win-20261018-163045`.
  A lasting fix would be to make the script exclude `Default User` from the glob.)

## Authenticode signing (optional, `T3K_SIGN=1` in `.env`)
Certs/keys live OUTSIDE the repo (`$T3K_SIGNING_DIR`, default `$HOME/signing`;
`*.p12`/`*.cer` are gitignored) and are **never committed**. Infra:
`script/signing/{sign-exe,build-minica,make-demo-cert,add-cert}.sh` —
env-driven, machine-path-free (setup README ships in `$T3K_SIGNING_DIR`).
- `T3K_SIGN_CERT` = p12 basename in the certs dir; `T3K_SIGN_PASS` = p12
  password (blank for passwordless minica p12s); `T3K_SIGN_TS` = RFC3161 URL
  (default `http://timestamp.sectigo.com`); `T3K_SIGN_VERIFY` = post-sign
  chain check (default 1). The script signs the STAGED copy after `cp`
  (never the build-tree exe).
- osslsigncode 2.13 API gotchas (verified live): password flag is `-pass`
  (`-p` = proxy); use `-in`/`-out`; `-out` refuses existing files (sign to
  temp, `mv` into place); self-signed roots need `verify -CAfile certs/<name>.pem`.
- Local trust chain = `build-minica.sh` (25 y root + 10 y codeSigning leaf,
  no-password p12 + `.pem`/`root.pem`); `make-demo-cert.sh` = plain
  self-signed pipeline test only. Windows-side trust (publisher name, no
  Smart Screen warning) requires a commercial CA intermediate folded in with
  `add-cert.sh` → `<cert>-full.p12`.

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

## Wine smoke test (WSLg display `:0`, system `wine64` in `/usr/bin`)
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
