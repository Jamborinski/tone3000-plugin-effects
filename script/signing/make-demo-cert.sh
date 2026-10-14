#!/bin/bash
# make-demo-cert.sh -- generate a SELF-SIGNED code-signing cert for local
# testing of the signing pipeline.
#
# A self-signed cert makes the exe "signed" (has an Authenticode block) and
# lets osslsigncode verify the chain locally, BUT Windows will NOT trust it:
# no CA root, no Smart App Control pass, no SmartScreen reputation. It only
# proves the pipeline works. For real distribution use a CA-issued cert
# (OV/EV) placed in the certs dir.
#
# Usage: bash script/signing/make-demo-cert.sh
# Writes: $T3K_CERTS_DIR/TONE3000-selfsigned.{p12,pem}
set -euo pipefail

CERTS="${T3K_CERTS_DIR:-${T3K_SIGNING_DIR:-$HOME/signing}/certs}"
PASS="${T3K_DEMO_CERT_PASS:-tone3000}"
OUT="$CERTS/TONE3000-selfsigned.p12"
mkdir -p "$CERTS"

W="$(mktemp -d /tmp/t3k-demo-cert.XXXXXX)"; trap 'rm -rf "$W"' EXIT

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 \
    -out "$W/key.pem" 2>/dev/null

openssl req -x509 -new -key "$W/key.pem" \
    -out "$W/cert.pem" -days 1825 \
    -sha256 -subj "/CN=TONE3000 Plugin (self-signed dev)" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=1.3.6.1.5.5.7.3.3" 2>/dev/null

openssl pkcs12 -export -out "$OUT" \
    -inkey "$W/key.pem" -in "$W/cert.pem" \
    -passout pass:"$PASS"

# keep a PEM copy so `verify -CAfile` can trust the self-signed root
openssl pkcs12 -in "$OUT" -passin pass:"$PASS" -nokeys -clcerts \
    -out "$CERTS/TONE3000-selfsigned.pem" 2>/dev/null

chmod 600 "$OUT" "$CERTS/TONE3000-selfsigned.pem"
echo "wrote $OUT (password: $PASS) -- demo/self-signed only"
