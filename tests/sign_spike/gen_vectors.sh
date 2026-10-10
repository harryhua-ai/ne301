#!/bin/sh
# =============================================================================
# gen_vectors.sh — regenerate non-production test vectors for sign_spike
#
# ALL key material in this directory is generated on-the-fly by this script,
# is NOT a production key, and MUST NOT be used for anything other than this
# offline feasibility spike (Issue #32). The committed PEM files are throwaway
# test-only keys whose private halves are also embedded as constants in
# test_sign_spike.c (marked NON-PRODUCTION).
#
# Requires: openssl >= 3.x on PATH (tested with homebrew openssl 3.6.2, macOS).
# =============================================================================
set -eu

OPENSSL=${OPENSSL:-openssl}
HERE=$(cd "$(dirname "$0")" && pwd)
VEC="$HERE/vectors"
mkdir -p "$VEC"

# Fixed 64-byte message under test (byte-exact match with test_sign_spike.c MSG)
python3 - "$VEC/spike_message.bin" <<'EOF'
import sys
msg = b"NE301 app-signing spike fixed byte string #1 0123456789abcdef"[:64]
msg = msg.ljust(64, b"#")
assert len(msg) == 64, len(msg)
open(sys.argv[1], "wb").write(msg)
EOF

# Non-production P-256 test key #1 (primary signer) + SPKI DER public key
"$OPENSSL" ecparam -name prime256v1 -genkey -noout -out "$VEC/ecdsa_p256_spike_testkey1.pem"
"$OPENSSL" pkey -in "$VEC/ecdsa_p256_spike_testkey1.pem" -pubout -outform DER \
    -out "$VEC/ecdsa_p256_spike_testkey1_pub.spki.der"

# Non-production P-256 test key #2 (wrong-public-key negative case)
"$OPENSSL" ecparam -name prime256v1 -genkey -noout -out "$VEC/ecdsa_p256_spike_testkey2.pem"
"$OPENSSL" pkey -in "$VEC/ecdsa_p256_spike_testkey2.pem" -pubout -outform DER \
    -out "$VEC/ecdsa_p256_spike_testkey2_pub.spki.der"

# Committed ECDSA-SHA256 signature (DER, randomized k -> one specific instance)
"$OPENSSL" dgst -sha256 -sign "$VEC/ecdsa_p256_spike_testkey1.pem" \
    -out "$VEC/spike_message.sig.der" "$VEC/spike_message.bin"

echo "== NON-PRODUCTION test scalars to embed in test_sign_spike.c (d1, d2) =="
for k in 1 2; do
    echo "--- testkey${k} priv ---"
    "$OPENSSL" ec -in "$VEC/ecdsa_p256_spike_testkey${k}.pem" -text -noout \
        | sed -n '/priv:/,/pub:/p' | grep -o '[0-9a-f][0-9a-f]' | tail -n +1 \
        | head -32
done
echo "done. Files in $VEC:"
ls -l "$VEC"
