#!/bin/bash
# win-standalone.sh -- build the Windows standalone exe and stage it into
# Windows' Downloads as  TONE3000-win-YYYYMMDD-HHMMSS  (the RUN timestamp;
# no descriptive suffixes). Run from WSL:
#   bash /home/jambo/dev/tone3000-plugin-main/scripts/win-standalone.sh
set -o pipefail
cd "$(dirname "$0")/.."

export PATH=/home/jambo/buildkit/cmake-4.2.3-linux-x86_64/bin:/home/jambo/buildkit/hostbin:/home/jambo/buildkit/stage/usr/bin:$PATH
export LD_LIBRARY_PATH=/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu

echo "== building TONE3000_Standalone =="
cmake --build build-win --target TONE3000_Standalone 2>&1 | tail -6
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then echo "BUILD FAILED rc=$rc"; exit "$rc"; fi

EXE=build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
[ -f "$EXE" ] || { echo "missing $EXE"; exit 1; }
echo '=== exe (build tree) ==='
ls -lh "$EXE"; md5sum "$EXE"
echo '=== source ==='
git branch --show-current; git log --oneline -1

TS=$(date +%Y%m%d-%H%M%S)
DEST=/mnt/c/Users/jambo/Downloads/TONE3000-win-$TS
n=2
while [ -e "$DEST" ]; do DEST="${DEST}-${n}"; n=$((n + 1)); done
mkdir -p "$DEST" && cp -f "$EXE" "$DEST/TONE3000.exe"
echo '=== staged (run '"$TS"') ==='
ls -lh "$DEST"; md5sum "$DEST/TONE3000.exe"
