#!/bin/bash
# win-standalone.sh -- build the Windows standalone exe and stage it into
# Windows' Downloads as  TONE3000-win-YYYYMMDD-HHMMSS  (the RUN timestamp;
# no descriptive suffixes). Run from WSL:
#   bash /home/jambo/dev/tone3000-plugin-main/script/win-standalone.sh
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

# Optional Authenticode signing (default OFF): T3K_SIGN=1 bash script/win-standalone.sh
# Infra lives outside the repo at /home/jambo/signing/ (certs are never committed).
# Env: T3K_SIGN=1 (on)  T3K_SIGN_PASS=<p12 pw>  T3K_SIGN_CERT=certs/<name>.p12
# (default cert: TONE3000.p12 = your CA cert; TONE3000-selfsigned.p12 = pipeline test).
# We sign the STAGED copy, not the build-tree exe, so the build artifact stays
# pristine (a signed exe is never re-linked/verified against cache mtimes).
# Without a CA-issued cert the signature is cosmetic -- see /home/jambo/signing/README.md.
SIGN=0
if [ "${T3K_SIGN:-0}" = "1" ]; then
  SIGN=1
  CERT_ARG=(--cert="${T3K_SIGN_CERT:-TONE3000.p12}")
  SIGN_PASS="${T3K_SIGN_PASS:-}"
  # the demo self-signed p12 has a fixed local password
  case "${T3K_SIGN_CERT:-}" in *selfsigned*) [ -z "$SIGN_PASS" ] && SIGN_PASS=tone3000 ;; esac
  [ -n "$SIGN_PASS" ] && CERT_ARG+=(--pass="$SIGN_PASS")
  SIGN_ARGS=("${CERT_ARG[@]}" --verify)
fi

echo '=== source ==='
git branch --show-current; git log --oneline -1

TS=$(date +%Y%m%d-%H%M%S)
DEST=/mnt/c/Users/jambo/Downloads/TONE3000-win-$TS
n=2
while [ -e "$DEST" ]; do DEST="${DEST}-${n}"; n=$((n + 1)); done
mkdir -p "$DEST" && cp -f "$EXE" "$DEST/TONE3000.exe"

if [ "$SIGN" = "1" ]; then
  echo '=== signing staged exe (T3K_SIGN=1) ==='
  bash /home/jambo/signing/sign-exe.sh "$DEST/TONE3000.exe" "${SIGN_ARGS[@]}" || {
    echo "SIGNING FAILED -- staged exe left unsigned"; exit 1; }
  md5sum "$DEST/TONE3000.exe"
fi

echo '=== staged (run '"$TS"') ==='
ls -lh "$DEST"; md5sum "$DEST/TONE3000.exe"
