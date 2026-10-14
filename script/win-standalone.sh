#!/bin/bash
# win-standalone.sh -- build the Windows standalone exe and stage it into
# Windows' Downloads as  TONE3000-win-YYYYMMDD-HHMMSS  (the RUN timestamp;
# no descriptive suffixes). Run from WSL:
#   bash script/win-standalone.sh
set -o pipefail
cd "$(dirname "$0")/.."

# Machine-specific settings come from .env (gitignored; see .env.example).
# Env vars already set on the command line take precedence over .env.
if [ -f .env ]; then
  set -a; . ./.env; set +a
fi

# Toolchain: staged buildkit (GCC + CMake under $T3K_TOOLCHAIN) if present,
# else the system cmake/gcc on PATH.
TK="${T3K_TOOLCHAIN:-$HOME/buildkit}"
if [ -d "$TK/stage/usr/bin" ]; then
  for d in "$TK"/cmake-*/bin "$TK/hostbin" "$TK/stage/usr/bin"; do
    [ -d "$d" ] && export PATH="$d:$PATH"
  done
  export LD_LIBRARY_PATH="$TK/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

echo "== building TONE3000_Standalone =="
cmake --build build-win --target TONE3000_Standalone 2>&1 | tail -6
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then echo "BUILD FAILED rc=$rc"; exit "$rc"; fi

EXE=build-win/plugin/TONE3000_artefacts/Release/Standalone/TONE3000.exe
[ -f "$EXE" ] || { echo "missing $EXE"; exit 1; }
echo '=== exe (build tree) ==='
ls -lh "$EXE"; md5sum "$EXE"

# Optional Authenticode signing (default OFF): T3K_SIGN=1 bash script/win-standalone.sh
# Infra lives outside the repo (T3K_SIGNING_DIR, default $HOME/signing; certs never committed).
# Env (also settable in .env -- see .env.example):
#   T3K_SIGN=1 (on)  T3K_SIGN_CERT=<name>.p12  T3K_SIGN_PASS=<p12 pw>
#   T3K_SIGN_TS=<url> (default http://timestamp.sectigo.com)
#   T3K_SIGN_VERIFY=1 (default; 0 to skip the post-sign chain check)
# (default cert: TONE3000.p12 = your CA cert; TONE3000-selfsigned.p12 = pipeline test).
# We sign the STAGED copy, not the build-tree exe, so the build artifact stays
# pristine (a signed exe is never re-linked/verified against cache mtimes).
# Without a CA-issued cert the signature is cosmetic -- see the README in
# $T3K_SIGNING_DIR (default $HOME/signing) and docs/agents/windows-build.md.
SIGN=0
if [ "${T3K_SIGN:-0}" = "1" ]; then
  SIGN=1
  CERT_ARG=(--cert="${T3K_SIGN_CERT:-TONE3000.p12}")
  SIGN_PASS="${T3K_SIGN_PASS:-}"
  # the demo self-signed p12 has a fixed local password
  case "${T3K_SIGN_CERT:-}" in *selfsigned*) [ -z "$SIGN_PASS" ] && SIGN_PASS=tone3000 ;; esac
  [ -n "$SIGN_PASS" ] && CERT_ARG+=(--pass="$SIGN_PASS")
  SIGN_ARGS=("${CERT_ARG[@]}")
  [ -n "${T3K_SIGN_TS:-}" ] && SIGN_ARGS+=(--ts="${T3K_SIGN_TS}")
  [ "${T3K_SIGN_VERIFY:-1}" = "1" ] && SIGN_ARGS+=(--verify)
fi

echo '=== source ==='
git branch --show-current; git log --oneline -1

TS=$(date +%Y%m%d-%H%M%S)
# Staging dir: WSL Downloads auto-detected by default; override T3K_STAGE_DIR.
STAGE_ROOT="${T3K_STAGE_DIR:-$(ls -d /mnt/c/Users/*/Downloads 2>/dev/null | head -1)}"
STAGE_ROOT="${STAGE_ROOT:-$HOME/Downloads}"
DEST="$STAGE_ROOT/TONE3000-win-$TS"
n=2
while [ -e "$DEST" ]; do DEST="${DEST}-${n}"; n=$((n + 1)); done
mkdir -p "$DEST" && cp -f "$EXE" "$DEST/TONE3000.exe"

if [ "$SIGN" = "1" ]; then
  echo '=== signing staged exe (T3K_SIGN=1) ==='
  bash "${T3K_SIGNING_DIR:-$HOME/signing}/sign-exe.sh" "$DEST/TONE3000.exe" "${SIGN_ARGS[@]}" || {
    echo "SIGNING FAILED -- staged exe left unsigned"; exit 1; }
  md5sum "$DEST/TONE3000.exe"
fi

echo '=== staged (run '"$TS"') ==='
ls -lh "$DEST"; md5sum "$DEST/TONE3000.exe"
