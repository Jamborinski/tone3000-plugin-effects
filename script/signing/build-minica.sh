#!/bin/bash
# build-minica.sh -- build a LOCAL "mini-CA" for signing tests.
#
#   - a long-lived self-signed *root* (the trust anchor)
#   - a real *code-signing leaf* issued by that root (proper X509v3
#     code-signing EKU, 10-yr validity)
#   - the leaf exported as $T3K_CERTS_DIR/TONE3000-minica.p12 (NO password)
#   - TONE3000-minica.pem  = root + leaf (what `verify -CAfile` needs)
#   - TONE3000-minica-root.pem = root only (import into the Windows trust
#     store with `certutil -addstore ROOT` to show the publisher name on
#     your own machine -- NOT a substitute for a commercial CA)
#
# This is the strongest chain a local machine can produce: sign-exe.sh
# --verify then passes its full chain check. Windows does NOT trust the
# root unless imported; Smart App Control needs a commercial CA either way.
#
# Usage: bash script/signing/build-minica.sh
set -euo pipefail

CERTS="${T3K_CERTS_DIR:-${T3K_SIGNING_DIR:-$HOME/signing}/certs}"
mkdir -p "$CERTS"
W="$(mktemp -d /tmp/t3k-minica.XXXXXX)"; trap 'rm -rf "$W"' EXIT

echo "== 1/4 mini-CA root (25y) =="
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$W/root-key.pem"
openssl req -x509 -new -key "$W/root-key.pem" -sha256 -days 9131 \
    -subj "/CN=T3K Local Signing Root" -out "$W/root.pem" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

echo "== 2/4 code-signing leaf (10y, EKU codeSigning) =="
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$W/leaf-key.pem"
openssl req -new -key "$W/leaf-key.pem" -subj "/CN=TONE3000 Plugin" -out "$W/leaf.csr"
cat > "$W/leaf.ext" <<'EOF'
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature, nonRepudiation
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
EOF
openssl x509 -req -in "$W/leaf.csr" -CA "$W/root.pem" -CAkey "$W/root-key.pem" \
    -CAcreateserial -days 3650 -sha256 -extfile "$W/leaf.ext" -out "$W/leaf.pem"

echo "== 3/4 export leaf p12 (no password) + trust PEM (root + leaf) =="
openssl pkcs12 -export -out "$CERTS/TONE3000-minica.p12" \
    -inkey "$W/leaf-key.pem" -in "$W/leaf.pem" -certfile "$W/root.pem" \
    -passout pass:
cat "$W/root.pem" "$W/leaf.pem" > "$CERTS/TONE3000-minica.pem"
cp -f "$W/root.pem" "$CERTS/TONE3000-minica-root.pem"

echo "== 4/4 verify: leaf chains to root =="
openssl verify -CAfile "$W/root.pem" "$W/leaf.pem"

chmod 600 "$CERTS/TONE3000-minica.p12"
chmod 644 "$CERTS/TONE3000-minica.pem" "$CERTS/TONE3000-minica-root.pem"
echo "wrote $CERTS/TONE3000-minica.p12 (no password) + trust PEM + root PEM"
echo "test: bash script/signing/sign-exe.sh <exe> --cert=TONE3000-minica.p12 --verify"
