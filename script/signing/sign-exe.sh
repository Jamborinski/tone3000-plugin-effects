#!/bin/bash
# sign-exe.sh -- Authenticode-sign a Windows PE file via osslsigncode.
#
# Usage:
#   bash script/signing/sign-exe.sh <file> [--cert=<name.p12>] [--pass=<pw>]
#        [--ts=<url>] [--hash sha256|sha1] [--no-ts] [--verify] [--selfsigned]
#
#   <file>         PE file to sign (exe/dll); signed in place
#   --cert=<name>  cert basename in $T3K_CERTS_DIR (default: TONE3000.p12)
#   --pass=<pw>    p12 password (default: env T3K_SIGN_PASS, else none)
#   --ts=<url>     RFC3161 timestamp URL (default: http://timestamp.sectigo.com)
#   --no-ts        skip timestamping (offline testing ONLY -- the timestamp is
#                  what keeps the signature valid after the cert expires)
#   --hash alg     hash: sha256 (default) | sha1 (legacy-only images)
#   --verify       after signing, verify the full chain (local roots via their
#                  .pem siblings in the certs dir, CA certs via system store)
#   --selfsigned   use the generated demo cert (TONE3000-selfsigned.p12)
#
# Environment (see .env.example):
#   T3K_SIGNING_DIR  signing infra dir (default: $HOME/signing)
#                    -- holds extracted/usr/bin/osslsigncode + certs/
#   T3K_CERTS_DIR    certs dir (default: $T3K_SIGNING_DIR/certs)
#   T3K_OSSL_SIGNCODE  explicit path to an osslsigncode binary (overrides all)
#   T3K_SIGN_PASS    p12 password
#
# Certs/keys NEVER live in the repo -- only this script does.
set -uo pipefail

SIGNING_DIR="${T3K_SIGNING_DIR:-$HOME/signing}"
CERTS="${T3K_CERTS_DIR:-$SIGNING_DIR/certs}"

# locate osslsigncode: explicit env > PATH > local extracted install
BIN="${T3K_OSSL_SIGNCODE:-}"
if [ -z "$BIN" ]; then
  BIN="$(command -v osslsigncode || true)"
fi
if [ -z "$BIN" ]; then
  BIN="$SIGNING_DIR/extracted/usr/bin/osslsigncode"
fi

TS_URL="http://timestamp.sectigo.com"
FILE=""
CERT="TONE3000.p12"
PASS="${T3K_SIGN_PASS:-}"
VERIFY=0
TS_ON=1
HASH=sha256

while [ $# -gt 0 ]; do
  case "$1" in
    --cert=*)     CERT="${1#--cert=}" ;;
    --pass=*)     PASS="${1#--pass=}" ;;
    --ts=*)       TS_URL="${1#--ts=}" ;;
    --no-ts)      TS_ON=0 ;;
    --hash)       shift; HASH="$1" ;;
    --hash=*)     HASH="${1#--hash=}" ;;
    --selfsigned) CERT="TONE3000-selfsigned.p12"; PASS="${PASS:-tone3000}" ;;
    --verify)     VERIFY=1 ;;
    -*)           echo "unknown flag: $1"; exit 2 ;;
    *)            FILE="$1" ;;
  esac
  shift
done

[ -n "$FILE" ] || { echo "usage: sign-exe.sh <file> [--cert=x.p12] [--pass=pw] [--ts=url] [--no-ts] [--hash sha256|sha1] [--verify] [--selfsigned]"; exit 2; }
case "$HASH" in sha256|sha1) ;; *) echo "bad --hash: $HASH (sha256|sha1)"; exit 2;; esac
[ -f "$BIN" ]  || { echo "missing osslsigncode at $BIN (install: apt-get download osslsigncode && dpkg-deb -x it under $SIGNING_DIR/extracted/)"; exit 1; }
[ -f "$FILE" ] || { echo "missing file: $FILE"; exit 1; }

P12="$CERTS/$CERT"
[ -f "$P12" ] || { echo "missing cert $P12 (certs dir: $CERTS -- put your CA .p12 there, or use --selfsigned after 'bash script/signing/make-demo-cert.sh')"; exit 1; }

P12PASS_OPT=()
# NB: in osslsigncode 2.13 -p means *proxy*; the p12 password is -pass
[ -n "$PASS" ] && P12PASS_OPT=(-pass "$PASS")

TS_OPT=()
if [ "$TS_ON" = "1" ]; then TS_OPT=(-ts "$TS_URL"); else echo "(timestamping disabled -- --no-ts)"; fi
echo "== signing $FILE (cert: $CERT, hash: $HASH, ts: $([ $TS_ON = 1 ] && echo "$TS_URL" || echo none)) =="
# osslsigncode signs -in X -out Y; it refuses to overwrite a pre-existing -out
# path, so atomically claim a fresh temp file, sign into it, then move into place.
WORKDIR="$(mktemp -d /tmp/t3k-sign.XXXXXX)"; trap 'rm -rf "$WORKDIR"' EXIT
OUT_TMP="$WORKDIR/out.bin"
"$BIN" sign \
    -pkcs12 "$P12" \
    "${P12PASS_OPT[@]}" \
    -h "$HASH" \
    -n "TONE3000 Plugin" \
    -i "https://github.com/Jamborinski/tone3000-plugin-effects" \
    "${TS_OPT[@]}" \
    -in "$FILE" \
    -out "$OUT_TMP"
rc=$?
if [ $rc -ne 0 ]; then
  echo "OSLSIGNCODE FAILED rc=$rc"; exit $rc; fi
chmod 600 "$OUT_TMP"
mv -f "$OUT_TMP" "$FILE"
echo "signed OK: $FILE"

if [ "$VERIFY" -eq 1 ]; then
  echo "== verifying (osslsigncode, trusted chain) =="
  # Trusted-chain verification in one shot: proves (a) the bytes are signed by
  # the p12's key -- any tamper fails PKCS7_verify -- and (b) the chain reaches
  # a trusted root: certs/<name>.pem for local roots (self-signed demo,
  # mini-CA), the system store for commercial CA certs (no .pem sibling).
  CAFILE="$CERTS/$(basename "$CERT" .p12).pem"
  CA_OPT=()
  [ -f "$CAFILE" ] && CA_OPT=(-CAfile "$CAFILE")
  if ! "$BIN" verify -in "$FILE" "${CA_OPT[@]}"; then
    echo "VERIFY FAILED: signature/chain (tamper, expired, or untrusted root)"; exit 1; fi
  echo "VERIFY OK: signature valid, chain trusted (root: ${CAFILE:-system store})"
fi
