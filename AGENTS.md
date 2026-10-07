# TONE3000 Plugin — AGENTS.md

Guidance for AI coding agents (pi, Cline) working in this repository.

## What this is
A JUCE **9.0.3** audio plugin (VST3 / AU / CLAP / LV2 / Standalone) that loads
**Neural Amp Modeler (NAM)** captures and **impulse responses (IRs)**. C++20,
CMake (Ninja). NAM + AudioDSPTools are in-tree
(`plugin/NeuralAmpModelerCore`, `plugin/AudioDSPTools` — the hand-populated
`Dependencies/` trees are load-bearing, don't delete them). Native JUCE UI
under `plugin/ui/`. DSP tests: GoogleTest (fetched to `libs/googletest`).

## Build environment (this machine — WSL, staged toolchain)
Repo: `/home/jambo/dev/tone3000-plugin-main` · WSL Ubuntu 26.04. Staged toolchain
+ deps under `/home/jambo/buildkit/stage/` (GCC-16, CMake 4.2.3, freetype,
fontconfig, X11, ALSA, brotli, …); MinGW cross under
`/home/jambo/buildkit/mingw/root/`.

### Canonical env (any manual cmake/ninja step)
```bash
export PATH=/home/jambo/buildkit/cmake-4.2.3-linux-x86_64/bin:/home/jambo/buildkit/hostbin:/home/jambo/buildkit/stage/usr/bin:$PATH
export LD_LIBRARY_PATH=/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
```
(cmake/ninja are NOT on default PATH; `cc1plus` dies
`libisl.so.23: cannot open shared object file` without `LD_LIBRARY_PATH`.)

### DSP tests
```bash
./script/test-dsp.sh                    # configure (if needed) -> build -> run
./script/test-dsp.sh 'DelayTest.*'      # gtest filter
# direct:
LD_LIBRARY_PATH=<stage lib dir> ./build/test/DspTests_artefacts/Release/DspTests
```
The suite **grows with features** (376 as of 2026-10-07) — never assert a
fixed count. **Stale-binary trap:** a failed reconfigure can still let
`cmake --build` "pass" and run the OLD binary (new tests read "0 tests ran");
check build RC AND that the new test names appear. Second form (header-only
DSP edit not recompiled): `rm -f build/test/CMakeFiles/DspTests.dir/src/<t>.cpp.o`
then rebuild.

### Linux GUI (Standalone / VST3 / CLAP / LV2)
```bash
cmake --build build --target TONE3000_Standalone   # also _VST3 / _CLAP / _LV2
# artifact: build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000
```
This build compiles audio-device + X11 backends, so the link deps matter:
- **X11**: headers + `-lX11` via `/home/jambo/x11dev` (symlinks→stage);
  `libxcb.so.1` from stage; **`libXau.so.6` + `libXdmcp.so.6`** must resolve at
  link (copied from `/usr/lib/x86_64-linux-gnu` into the stage lib dir).
- **ALSA**: a REAL link dep; `-lasound` comes from **pkg-config** `alsa.pc`
  (NOT find_library). Undefined `snd_*`/`Xau*` at link ⇒ ALSA/X11, **never**
  curl/ALSA-jack below.
- **JACK & curl**: LAZY (dlopen at runtime) → headers only, no `-l` entry.
- `FindX11` won't infer `--sysroot`: pass `X11_X11_INCLUDE_PATH/DIR` +
  `X11_X11_LIB` explicitly.
- Running: prepend the stage lib dir + `LD_LIBRARY_PATH=/home/jambo/x11dev`;
  in headless it reaches ALSA init and blocks on display/audio — **expected**,
  not a build failure. Health = `ldd` (0 "not found") + `readelf -d`.

### pkg-config shim (do NOT revert)
`stage/usr/bin/pkg-config` is a self-sufficient **shim** (real binary =
`pkg-config.real`) that sets `LD_LIBRARY_PATH`+`PKG_CONFIG_PATH` before exec.
Exists because CMake cleans `LD_LIBRARY_PATH` and JUCE's `juceaide`
sub-invoke re-detects pkg-config in its own CMake (which can't take
`-DPKG_CONFIG_EXECUTABLE`).

### Adding sources → reconfigure traps
Any source-GLOB change (new `.cpp`) re-runs `juceaide` → needs the vendored
freetype on the search path (no system freetype dev):
`export CPATH=/home/jambo/dev/tone3000-plugin-main/libs/freetype/include`.
**Never hand-edit `build/build.ninja`** (a reconfigure regenerates and drops
the edit). `plugin/CMakeLists.txt`: the `nam` lib **must** list
`nam_file.cpp` + `wav.cpp` (else `validate_nam_file`/`detail::load_wav_ir`
link errors).

### Fresh configure (only if the build dir is missing/stale)
DspTests:
```bash
STAGE=/home/jambo/buildkit/stage
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-gcc-16 \
  -DCMAKE_CXX_COMPILER=$STAGE/usr/bin/x86_64-linux-gnu-g++-16 \
  -DCMAKE_C_FLAGS="--sysroot=$STAGE" -DCMAKE_CXX_FLAGS="--sysroot=$STAGE" \
  -DX11_X11_INCLUDE_PATH=/usr/include -DX11_X11_LIB=/usr/lib/x86_64-linux-gnu/libX11.so
```
Full plugin (the three pkg-config knobs are what make ALSA resolve):
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

### VST3 helper (`TONE3000_vst3_helper`) — two persistent fixes (2026-10-03, keep)
It is a **separate CMake sub-project** (`build/vst3_helpers/TONE3000/`) that
does NOT inherit the main build's `CMAKE_CXX_FLAGS`:
1. Missing `--sysroot` → `bits/wordsize.h: No such file` (its include path has
   an incomplete glibc tree that shadows the stage's). Fix: reconfigure the
   helper sub-project WITH `-DCMAKE_CXX_FLAGS="--sysroot=$STAGE"` etc. (it
   persists in `build/vst3_helpers/TONE3000/CMakeCache.txt`; the main build's
   own rebuild of the helper does **not** pass the flag):
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
   absent; the plugin `.so` is shared and does not hit it). Fix once:
   `sudo cp $STAGE/usr/lib/x86_64-linux-gnu/libc_nonshared.a /usr/lib/x86_64-linux-gnu/`
   (sudo password: in the local global rules — never commit it to this repo).

## Windows cross-build (MinGW)
`build-win/` is pre-configured (`CMAKE_SYSTEM_NAME=Windows`,
`x86_64-w64-mingw32-g++-posix`, Ninja, Release) — a plain build just works:
```bash
cmake --build build-win --target TONE3000_Standalone    # also TONE3000_VST3
# artifact: build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
```
- Exe is self-contained (static MinGW runtime) — verify: `file` =
  `PE32+ … x86-64`; `objdump -x | grep 'DLL Name'` lists **only** Windows API
  DLLs (no `libstdc++*`/`libgcc*`/`libwinpthread*`).
- **ASIO is a HARD user requirement** — never ship it off; if a build lost it,
  fix the build. ON by default via JUCE's bundled ASIO SDK (no external SDK).
- Two configure-time patches in ROOT `CMakeLists.txt` (idempotent) — **keep**:
  `T3K_MINGW_ASIO_SEH` (C SEH `__try/__except` → if/else; ASIO needs it) and
  `T3K_MINGW_DWRITE_CRP` (6-arg `CreateCustomRenderingParams` via base
  `IDWriteFactory`; MinGW header hides the base overload).
- MinGW libstdc++ has **no** `std::tanhf`/`std::floorf` — use
  `std::tanh(x)`/`std::floor(x)` with float args (the rest of the codebase's style).
- **Staging to Windows Downloads (the only staging script):**
  `bash /home/jambo/dev/tone3000-plugin-main/scripts/win-standalone.sh` →
  `C:\Users\jambo\Downloads\TONE3000-win-YYYYMMDD-HHMMSS` — **run-timestamp
  ONLY, never descriptive suffixes**. Older descriptive folders in Downloads
  are historical A/B references — don't touch them.
- After `.env` (publishable key) or source changes, a plain build does not
  pick them up. Fast path (~2 min, keeps JUCE objects):
  ```bash
  source /home/jambo/buildkit/winbuild_env.sh
  MROOT=/home/jambo/buildkit/mingw/root
  cmake -S . -B build-win -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER="$MROOT/usr/bin/x86_64-w64-mingw32-gcc-posix" \
    -DCMAKE_CXX_COMPILER="$MROOT/usr/bin/x86_64-w64-mingw32-g++-posix" \
    -DCMAKE_RC_COMPILER="$MROOT/usr/bin/x86_64-w64-mingw32-windres" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_AAX=OFF -DBUILD_LV2=OFF -DBUILD_CLAP=OFF
  cmake --build build-win --target TONE3000_Standalone --parallel "$(nproc)"
  ```
  Full reconfigure (`rm -rf build-win`, ~30 min) only if the cache is
  corrupted: `/home/jambo/buildkit/build-win-standalone.sh`.
- **Wine smoke test** (WSLg display `:0`; `wine64` in the 26.04 repos):
  ```bash
  WINEPREFIX=/home/jambo/wine-test WINEDEBUG=-all DISPLAY=:0 wineboot -u
  timeout --signal=KILL 30 wine64 build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
  WINEPREFIX=/home/jambo/wine-test wineserver -k
  ```
  **exit 124 = success** (had to SIGKILL a live app). `ALSA … /dev/snd/seq …
  No such file` is expected (no audio device) and graceful. Wine has NO real
  ASIO driver — this proves launch, not the ASIO dropdown entry (needs real
  Windows). Static proof the type is compiled in:
  `strings TONE3000.exe | grep -i ASIOAudioIODevice`.

## Merging with upstream (two remotes)
- **`upstream`** = the real project `tone-3000/tone3000-plugin` — "sync" /
  "upstream has new commits" ⇒ `git fetch upstream` + compare
  `upstream/main`. **`origin`** = our fork
  `Jamborinski/tone3000-plugin-effects` (tags `compressor`/`reverb` are ours).
- Divergence: the effects suite (Delay/Chorus/Tremolo/Compressor/Reverb +
  `effectKind`) exists **only in our fork**; upstream brings architecture.
  Both directions have been done (2026-10-05, 2026-10-07).
- Strategy:
  1. Map the divergence: `git rev-list --left-right --count main...upstream/main`,
     `git log --oneline main..upstream/main`, name-only diffs on both sides
     from `git merge-base main upstream/main`.
  2. For each conflicted file **map the function map on BOTH sides first**
     (`git show main:file | grep -n 'X::('` vs upstream) — hunks are local,
     meaning is per-function. `Auto-merging` ≠ correct.
  3. Resolve **per function**: keep the newer/better structure, pull the other
     side's semantic change INTO it; direction is decided per merge
     ("their structure our content" AND "our structure their content" both
     worked). Both block-creation paths must size effect DSP
     (`prepareChainBlock` + upstream's helper) or the effect's rings stay
     unallocated and the effect is inaudible.
  4. **Verify before committing the merge:** Linux DspTests (compile gate +
     behaviour; count grows — don't assert old numbers) then the Windows
     cross-build link (or a full GUI build when the engine is untouched). THEN
     `git add` **only the conflicted files** (never `-a` — sweeps in standing
     dirt), commit, and `git merge --ff-only main` the feature branch(es)
     (or merge main into it if it has unique commits).
- Hygiene: `git commit` mid-merge refuses if ANY local file is dirty →
  `git stash push <file>` → commit → `git stash pop`. Multi-line messages via
  `git commit -F file`. Any command with shell variables/loops = **script
  file**. Standing dirt in this tree (modified `.gitignore`, untracked `&1`):
  keep out of commits. `main` being ahead of `origin` unpushed is normal;
  push only when told (see commit hygiene in the global rules).

## Wiring contracts (state round-trip — miss one and it regresses)
- A `ChainBlock` field round-trips only if it is in **all four** places, else
  the knob works sonically but **snaps back on resync**:
  1. `getChainState` (`plugin/src/ProcessorChain.cpp`) — `BlockRow` struct,
     the `copyLane` copy, AND `params->setProperty("<field>", …)` in
     `serializeChain`.
  2. `parseItem` (`plugin/ui/model/ChainState.cpp`) —
     `item.<field> = num(v["params"], "<field>", default)`.
  3. `updateBlockParam` (`ProcessorChain.cpp`) — the `param == "<field>"`
     branch (each delay param case calls `block->delay.setParams(
     block->delayParams())`).
  4. `ProcessorState.cpp` — `blockState.setProperty("<field>", …)` +
     `getProperty` restore.
  (This is how `delayMode` + the sig family + `delayMod` + the rate fields
  regressed — the save side wrote `compMode` but never the delay family.
  Round-trip is pinned in `StateCacheTest.DelayModeSetAndPunchSurviveSaveRestore`.
  New engine fields append AFTER existing `Params` members and **every**
  aggregate `setParams({...})` call site (8 in ProcessorChain.cpp, 1 in
  ProcessorState.cpp) is extended in order.)
- **KnobScale storage/display contract:** the value WRITTEN to a param = the
  scale's **storage** mapping (`KnobScale::toStored`/`fromStored` via
  `knobToStored()`/`knobFromStored()`); typed/SHOWN value = the **display**
  mapping. A knob whose stored domain is 0..1 but displays a human unit MUST
  declare `toStored`/`fromStored` (else the fallback writes the display unit
  into the 0..1 param and it **snaps back** after every resync — the Width
  `percent()` bug and a CLIP 0..200 face landing in 0..2 shipped broken that
  way). `linear(min,max,…)` maps raw across the stored range; the trailing arg
  is DECIMALS, `steps=` quantizes. Tile writes/resyncs never call
  toDisplay/fromDisplay directly. Pinned in
  `test/src/effect_ui_scale_tests.cpp`.
- **EffectTile gates:** `numParams_` must list EVERY effect kind that has >3
  knobs (a missing kind silently caps the tile at 3), and the layout's
  `five`/`cols` flag is a *separate* gate — update both.
- **DspTests never compiles `plugin/ui`** — verify UI changes with a GUI
  build (`ninja -C build TONE3000_Standalone` or the VST3 target) before
  committing.
- **This JUCE's API:** `ValueTree::isValid()` not `isObject()`; `juce::var`
  has no `toDouble()` — read numbers via `.toString().getDoubleValue()`;
  `TextButton::setButtonText(text)` is one-arg.

## DSP invariants (user A/B-calibrated — do NOT silently regress)
Rationale lives in the code comments (keep in sync) + `plugin/docs/`.

### Compressor (six signature modes)
- **Shared law scale: ratio = DEPTH** (higher = deeper GR = lower output):
  soft law `(n-1)*(R-1)/12`, PUNCH light path `(n-1)/24`. **Detent 4:1 is the
  bit-identical keeper anchor.** Do NOT "simplify" back to `(n-1)/R` (treated
  R as softness = INVERTED). 1:1 fully open, 20 leans limiting.
- **FET (1176, "good as shipped" 2026-10-05):** always-parallel 4-amp; SLOW
  pair = **POWER/RMS** meter (real 1176 slow channels are rectifier+RC
  averages), FAST pair = **PEAK** clamp; law = true hard-knee ratio;
  feedback detector; slow pair gets ≤ +3 dB program-dependent extra GR (fast
  pair stays at selected ratio); punch = slow pair fully open (contrast pin
  > 1.3, NOT 1.5/1.8 — feedback flattens the feed-forward spread and that is
  correct). **CONSIDERED & DECLINED: ratio↔attack/release detent coupling —
  A/R are absolute time constants; do not implement without an ask.**
- **Vari-Mu (670):** feedback detection; ratio = depth rolling into a level
  CEILING; odd-antisymmetric `fcClip` (colouration GROWS with GR); release =
  the 670 time-switch range **0.04 → 25 s continuous sweep** (chain stores the
  position; UI speaks true seconds via `compRelease670`; halfway = 1.0 s);
  above halfway: program-dependent hold (tau ≤ 1.5×). Default: 0.2 ms attack,
  position ~2.
- **Opto-2A (LA-2A):** GR inside a saturating hot stage (`twoAClip`,
  3rd-dominant colour tuned, monotonic, quiet-clean); −3 dB trim; LED→cell
  detector = **POWER (x²) meter**; attack default **40 ms** (user pick);
  release 600 ms single-pole (two-stage release REMOVED — unsupported);
  PUNCH = parallel LED→photocell light stage.
- **VCA:** two-stage RMS (IIR on x² with its OWN fixed 50 ms ballistics,
  DIALED A/R applied to that level — **never** apply dialed A/R directly to
  x² at audio rates: it tracks instantaneous power = a disguised peak
  detector, measured 2026-10-05); feedforward; textbook C1-continuous soft
  knee; multiplier is clean (NO added harmonics); PUNCH = parallel light path
  at HALF slope + 3× release.
- **Tube-STA:** rectifier BEHIND the gain stage (detection on the feedback
  tap); **program-controlled release** (brief peaks recover on dialed
  release, sustained highs drain at 2.5×); the same soft depth law; mild
  even-leaning warmth (distinct from 670 odd crunch / 2A 3rd); PUNCH =
  Retro TRIPLE mode (parallel light leg, half depth, 3× release).
- **Signature knobs (the per-mode unique slot):** VCA → **KNEE** (face 1..11
  dB, stored RAW dB, 6 dB = classic bit-exact); FET / Opto-2A / Tube-STA /
  Vari-Mu → **CLIP** (face 0..200 %, stored RAW 0..2, **1.0 = bit-identical
  legacy engine**). Engine: `Compressor::clipDepth(clean, normal, amt)`
  wraps every colourizing site; law functions public for pins.
- **Harmonic-measurement rule:** FFT windows MUST be whole cycles of the test
  tone (a rectangular window at non-integer cycles fakes D2/D3). DFT bins
  must be `freq·win/fs` EXACT for tone AND harmonic (or leakage reads as
  signal). All `harm()` windows in `effect_tests.cpp` are period-snapped.

### Modulation (Chorus + Tremolo)
- **Chorus (5 knobs):** Rate 0.05–5 Hz log, Depth 0–5 ms, **Tone** (stored
  0..1, **noon = bit-transparent**; left half low-pass 1.2 kHz → ~48 kHz,
  right half high-shelf +12 dB; shown as dB −18/0/+12), Spread 0–100 %,
  **Shape = 5 LFO detents**: Sine / Triangle / Saw(Up) / **Saw(Down)** /
  Square — Saw(Down) is index 3 (inserted 2026-10-05, Square moved to 4);
  legacy `chorusWave`/`tremoloWave` where 3 was Square **remaps 3→4 at
  load**; new state persists `…WaveV2`.
- LFO polarity: `delay = base + depth·(0.5+0.5·wave)` (saw-up rises, saw-down
  falls); `tremolo gain = 1 − depth·(0.5+0.5·wave)`.
- **Slew rule:** the delay position is rate-limited (`kMaxDelaySlew`,
  0.25 samples/sample/channel); tremolo wave has per-channel click-killing
  slew + R **re-seeds** to L's wave when spread changes — hard edges
  (saw/square) never teleport the read position.
- `Chorus::kNumWaves` AND `Tremolo::kNumWaves` **stay 5 together**
  (`setParams` clamps to `kNumWaves−1`; a stale 4 silently turns Square into
  saw-Down).
- **Tremolo (5 knobs):** Rate / Depth / Tone / Shape / **Spread** = R's LFO
  phase offset (0 in phase, 1 = 180° auto-pan; `aL+aR = 2-depth` at full
  spread on a sine). **Spread 0 = bit-identical to the pre-spread engine.**
  Tone = the Chorus design, symmetric ±18 dB; state stores REAL dB, the
  chain publishes `0.5 + dB/36`. `Tremolo::Params` order:
  `{rateHz, depth, spread, tone, wave}` (tone 0..1, spread 0..1).

### Delay — the 6-mode set (Digital/Tape/BBD/Mod/Magnetic/MemGuy, `kNumModes = 6`)
**Full design + provenance + per-mode tickets: `plugin/docs/delay-modes.md`**
(single source of truth). Invariants here:
- Each mode is a DISTINCT DSP engine, not a preset. Scaffold contract:
  **law-free modes (Digital, Mod) bit-identical to the Digital engine at
  neutral**; **tone/law modes (Tape, BBD, Magnetic, MemGuy) CARRY a body at
  every signature** (neutral IS the law floor — pin with `EXPECT_NE`).
- Signature knobs: Digital→**PING** (0..1, a *stereo routing* signature —
  inert on mono); Tape→**HEADS** 1..4 even-interval comb (1 = bit-exact
  Digital); BBD→**CHIP** (0 = the `fc ∝ 1/T` law floor, NOT the Digital
  body); Mod→own RATE + the brightness waver; Magnetic→**Rate** + capstan
  tone-coupling (`kMagToneCouple` 0.20, ceiling sways ±20 %, **Magnetic
  ONLY**); MemGuy→**Rate**, BBD line at **chip 0** (the law uses an
  *effective* chip: raw `sigChip` applies only on mode 2).
- **Shared Mod knob (delayMod) in every mode** = sined wobble on the read
  tap (L + / R − opposite phase), depth `kModWobbleMs·sigMod·…`; per-mode law
  (classic 5 Hz for Digital/Mod, Tape 1 Hz, BBD 0.8 Hz; the three rate
  modes use their own rates). `delayMod` 0 keeps `modOn_` false (bit-exact).
  Per-mode starting values are stashed (`EffectTile::modByMode_[]`): entering
  a mode restores that mode's Mod, the 35 % landing applies only on first
  entry, alt-click resets to the mode's default.
- **Rate knobs (one per rate mode):** `delayRateHz` (Mod) /
  `delayMagRateHz` (Magnetic) / `delayMmRateHz` (MemGuy) — **REAL-Hz
  storage** 0.5–30, shared log face `scales::modRateHz`, **stocks 1.5 / 1.0 /
  0.8 Hz** (user ears). Engine uses each mode's own sig rate; depth always
  from the shared Mod. Tiles: In / Width / Mod / Rate / Out.
- **Phase 2 (2026-10-07):** Magnetic/MemGuy ride the DEEP waver (`modWobbleMs`
  → `kDopplerWobbleMs` 10 ms; every other mode keeps the classic 4 ms).
  **Bessel-J trap:** at 220 Hz the classic ±4 ms sits on J₀'s FIRST ZERO (the
  carrier vanishes, smear is dark past f+18 Hz); the ±10 ms law peaks at
  J13 (f+26 Hz) and the carrier REAPPEARS (J₀ non-monotonic) — discriminate
  with the **FAR smear** (k9..k14), pinned ~180×, never the carrier. **Mod
  brightness waver:** while waver-on, the read carries an 8 kHz ceiling
  (`kModCeilHz`) whose corner the LFO sways ±`kModBrightCouple` (10 % at full
  depth — a FLOAT, pin with `EXPECT_FLOAT_EQ`); Mod = 0 removes it
  (bit-exact read).
- **Spread (the 5th knob; Damp is PARKED):** `delaySpread` 0..1 splits the
  tap: L = T(1−0.5s), R = T(1+0.5s), a second slewed scalar (never yanks the
  tail). **Spread 0 = bit-identical to the pre-spread engine** (pinned).
  Damp's engine/state/param handler all STAY — re-adding is a slot swap.
  `Params.damping` keeps its 3rd position; `spread` appended after it so
  legacy 1..3-arg `setParams({...})` still default to 0. (Compressor `compToneDb`
  is parked the same way.)
- **Lane-aware spread (Chorus / Tremolo / Delay):** each lane is a 1-ch
  engine, so the chain stamps `ChainBlock.setSpreadLane(lane)` →
  `engine.setLane(lane)`; side formula `side = (lane_ + ch) >= 1` (mono chain
  lane 0 keeps legacy ch0-left / ch1-right bit-exactly).
- **DC blocker** ~80 Hz in the feedback loop (`Params::dcBlock`) — ARMED in
  the production chain via `ChainBlock::delayParams()`, OFF in the standalone
  engine (bit-identity pins).
- **Variable-tap reader trap:** `readAt` must read relative to the
  LIVE write head of the in-flight process loop (`writePos` argument), not
  the committed `ring.write` (stale mid-call → literal zeros).
- **1-pole high-pass 80 Hz:** zero at z=1, POLE at `1-a`
  (a = 1−e^(−2πf/fs)); gain-normalized (1+a)/2: `y = (1-a)·y_prev + (1-a/2)·(x − x_prev)`.
  The tempting pole=`a` variant puts the corner near 7.6 kHz and crushes
  everything below (looked like a dead block).
- BBD law: `fc ∝ 1/T` from the *slewed* base tap, 5 kHz at the 250 ms
  reference, clamped 50 Hz..Nyquist, applied to the WET READ; `bbdLawNorm_`
  is the dimensionless `2π·fc_ref·refMs·0.001` (`norm = bbdLawNorm_/cur`
  reproduces `2π·fc/sr` because `sr` cancels).

### Clean-room licensing decision (2026-10-06, keep)
- **felitronics-core** (AGPL-3.0) = spec / measurement REFERENCE ONLY — never
  copy code, constants, curves, or tuned numbers into the MIT codebase. Law
  sources: published physics (transformer flux integrator + saturated
  derivative; NAB spec) + **our own** constants tuned to **our own**
  acceptance pins.
- **HARD constraint: ZERO LATENCY** — causal flux integrator OK; the NAB
  pre/de-emphasis pair are exact inverses (net phase zero); **NO
  oversampling** (keeps drives in the gentle band, harmonic alias small).
- **WingComp "LA-2A"** (Desktop) is UNUSABLE (no licence file, broken attack
  conditional) — do not pull anything from it.

## Conventions
- C++20, match the surrounding style — no drive-by refactors; keep changes
  scoped to the ticket.
- Pure-DSP unit tests live in `test/src/` and compile the REAL plugin sources
  (not copies).
- Do **not** commit scratch/review notes (benchmark reports, QA checklists,
  draft tickets).
- After any Windows-side edit of a WSL repo file: `touch` it before building
  (9p mtime can report "no work to do") and confirm it actually recompiled.
- `apt-get download` is unreliable here — `apt-get install --print-uris
  <pkg>` → `curl -fsSL`.
- Machine-wide agent rules (WSL/Git Bash split, long-job patterns, commit
  hygiene, credentials): **pi** → `C:\Users\jambo\.pi\agent\AGENTS.md`;
  **Cline** → `Documents\Cline\Rules\global.md`.
