#!/usr/bin/env bash
# Generates a self-signed test CA and two unit certificates (unit-100,
# unit-101) signed by it, using the `openssl` CLI.
#
# This mirrors prototype/README.md's commands, adapted to mqtls::mq's
# identity scheme: CN/SAN = "unit-<id>" and EKU
# serverAuth+clientAuth on every unit certificate, because a unit may play
# either TLS role depending on which peer it talks to (D12). It exists so
# examples/ and manual two-terminal testing (T10) have ready-made PEM files
# without reaching for tests/support/test_pki (which is for programmatic,
# in-process test generation, not for files meant to persist on disk).
#
# All generated certificates are checked with `openssl verify` before this
# script reports success; `openssl verify` exits non-zero on a verification
# failure, and this script runs under `set -e`, so any problem aborts with a
# non-zero exit code.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/gen_certs.sh [output-dir]

Generates:
  <output-dir>/ca.pem,      ca.key       -- self-signed test CA (P-384/SHA-384)
  <output-dir>/unit-100.pem, unit-100.key -- unit 100's certificate/key
  <output-dir>/unit-101.pem, unit-101.key -- unit 101's certificate/key

output-dir defaults to "./certs" and is created if missing. Key files are
written with mode 0600. Every generated certificate is verified against the
CA with `openssl verify` before this script exits; a verification failure
aborts with a non-zero exit code.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

out_dir="${1:-certs}"
mkdir -p "$out_dir"
# Resolve to an absolute path so messages below are unambiguous regardless
# of the caller's current directory.
out_dir="$(cd "$out_dir" && pwd)"

ca_key="$out_dir/ca.key"
ca_cert="$out_dir/ca.pem"

echo "==> Generating test CA (P-384 / SHA-384) in $out_dir"
openssl ecparam -name secp384r1 -genkey -noout -out "$ca_key"
openssl req -new -x509 -key "$ca_key" -sha384 -days 3650 \
  -subj '/O=mqtls-test/CN=mqtls test CA' -out "$ca_cert"
chmod 0600 "$ca_key"

gen_unit_cert() {
  local id="$1"
  local name="unit-${id}"
  local key="$out_dir/${name}.key"
  local csr="$out_dir/${name}.csr"
  local cert="$out_dir/${name}.pem"
  local ext="$out_dir/${name}.ext"

  echo "==> Generating ${name} (signed by $(basename "$ca_cert"))"
  openssl ecparam -name secp384r1 -genkey -noout -out "$key"
  chmod 0600 "$key"
  openssl req -new -key "$key" -sha384 -subj "/O=mqtls-test/CN=${name}" -out "$csr"

  cat >"$ext" <<EXT
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=DNS:${name}
EXT

  openssl x509 -req -in "$csr" -CA "$ca_cert" -CAkey "$ca_key" -CAcreateserial \
    -days 365 -sha384 -extfile "$ext" -out "$cert"

  rm -f "$csr" "$ext"
}

gen_unit_cert 100
gen_unit_cert 101

echo "==> Verifying generated certificates against the test CA"
openssl verify -CAfile "$ca_cert" "$out_dir/unit-100.pem"
openssl verify -CAfile "$ca_cert" "$out_dir/unit-101.pem"

echo "==> Done. Files written under $out_dir:"
ls -l "$ca_cert" "$ca_key" "$out_dir/unit-100.pem" "$out_dir/unit-100.key" \
  "$out_dir/unit-101.pem" "$out_dir/unit-101.key"
