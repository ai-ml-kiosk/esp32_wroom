#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CERT_DIR="${PROJECT_ROOT}/certs"
GENERATED_DIR="${PROJECT_ROOT}/include/generated"
APP_CONFIG="${PROJECT_ROOT}/include/AppConfig.h"
HOSTNAME="esp32-status.local"

mkdir -p "${CERT_DIR}" "${GENERATED_DIR}"

CERT_PEM="${CERT_DIR}/status-server-cert.pem"
KEY_PEM="${CERT_DIR}/status-server-key.pem"
CERT_HEADER="${GENERATED_DIR}/StatusServerCertPem.h"
KEY_HEADER="${GENERATED_DIR}/StatusServerKeyPem.h"

TMP_CONFIG="$(mktemp)"
TMP_CERT_BIN="$(mktemp)"
TMP_KEY_BIN="$(mktemp)"
trap 'rm -f "${TMP_CONFIG}" "${TMP_CERT_BIN}" "${TMP_KEY_BIN}"' EXIT

USE_STATIC_IP="$(
  sed -nE 's/.*kUseStaticStationIp = (true|false).*/\1/p' "${APP_CONFIG}" | head -n1
)"
STATIC_IP="$(
  sed -nE 's/.*kStationStaticIp\(([0-9]+), *([0-9]+), *([0-9]+), *([0-9]+)\).*/\1.\2.\3.\4/p' "${APP_CONFIG}" | head -n1
)"

ALT_NAMES=$(
  cat <<EOF
[alt_names]
DNS.1 = ${HOSTNAME}
DNS.2 = esp32-status
IP.1 = 192.168.4.1
EOF
)

if [[ "${USE_STATIC_IP}" == "true" && -n "${STATIC_IP}" && "${STATIC_IP}" != "192.168.4.1" ]]; then
  ALT_NAMES+=$'\n'"IP.2 = ${STATIC_IP}"
fi

cat > "${TMP_CONFIG}" <<EOF
[req]
default_bits = 2048
prompt = no
default_md = sha256
distinguished_name = dn
x509_extensions = v3_req

[dn]
CN = ${HOSTNAME}
O = ESP32 Local Development

[v3_req]
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

${ALT_NAMES}
EOF

openssl req \
  -x509 \
  -nodes \
  -newkey rsa:2048 \
  -sha256 \
  -days 825 \
  -keyout "${KEY_PEM}" \
  -out "${CERT_PEM}" \
  -config "${TMP_CONFIG}"

cat "${CERT_PEM}" > "${TMP_CERT_BIN}"
printf '\0' >> "${TMP_CERT_BIN}"
cat "${KEY_PEM}" > "${TMP_KEY_BIN}"
printf '\0' >> "${TMP_KEY_BIN}"

xxd -i -n certs_status_server_cert_pem "${TMP_CERT_BIN}" > "${CERT_HEADER}"
xxd -i -n certs_status_server_key_pem "${TMP_KEY_BIN}" > "${KEY_HEADER}"

echo "Generated:"
echo "  ${CERT_PEM}"
echo "  ${KEY_PEM}"
echo "  ${CERT_HEADER}"
echo "  ${KEY_HEADER}"
