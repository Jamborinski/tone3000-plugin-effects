#!/bin/bash
# add-cert.sh -- fold CA intermediate PEM(s) into a code-signing p12, so the
# signed output carries the full chain and Windows can resolve the publisher
# name without downloading intermediates.
#
# Usage:  bash script/signing/add-cert.sh <cert.p12> <pass> <intermediate-1.pem> [<intermediate-2.pem> ...]
#         (writes <cert>-full.p12; original untouched)
#
# Commercial CAs (DigiCert, Sectigo, GlobalSign...) give you the intermediate
# bundle as PEM/CER at purchase -- run this once, then point the staging at
# the -full p12:  T3K_SIGN_CERT=<cert>-full.p12
set -euo pipefail
[ $# -ge 3 ] || { echo "usage: add-cert.sh <cert.p12> <pass> <intermediate.pem> [...]"; exit 2; }
CERT="$1"; PASS="$2"; shift 2

LEAF="$(mktemp)"; KEY="$(mktemp)"; trap 'rm -f "$LEAF" "$KEY"' EXIT
# extract leaf + key, then re-export with the intermediates attached
openssl pkcs12 -in "$CERT" -passin "pass:$PASS" -nokeys -clcerts -out "$LEAF"
openssl pkcs12 -in "$CERT" -passin "pass:$PASS" -nocerts -nodes -nomacver -out "$KEY"
openssl pkcs12 -export -out "${CERT%.p12}-full.p12" \
    -inkey "$KEY" -in "$LEAF" -passout "pass:$PASS" "$@"
echo "wrote ${CERT%.p12}-full.p12 (leaf + $# intermediate(s))"
