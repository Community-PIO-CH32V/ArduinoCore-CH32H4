#!/bin/sh
# Regenerate the test certificates. They are committed, so this is only needed
# when they expire (2045) or when the shape of the test changes.
#
# Three things are made here, and each one exists to test a different path:
#
#   server.crt/.key   what EthernetServerSecure presents. Self-signed, so it
#                     is also its own trust anchor -- CA:TRUE is what lets the
#                     test client verify against it directly.
#   clientca.crt/.key a CA that signs nothing the server presents. It is what
#                     the server is told to trust for CLIENT certificates.
#   client.crt/.key   a client certificate under that CA, for mutual TLS.
#
# EC P-256 rather than RSA-2048, for two reasons: the handshake is far cheaper
# on a part with no big-integer accelerator, and MBEDTLS_SSL_OUT_CONTENT_LEN is
# 2048 here -- mbedtls cannot fragment an outgoing handshake message across
# records, so an RSA chain can be too large to send at all.
#
# The SAN is a DNS name, not the board's address, because DHCP decides the
# address. The test connects by IP and passes server_hostname=ch32h4.local,
# which is exactly the setHostname() case on the client side.
set -e
cd "$(dirname "$0")"

# On Git Bash, MSYS rewrites anything that looks like a path -- including
# "/CN=ch32h4.local", which becomes "C:/Program Files/Git/CN=...".
export MSYS2_ARG_CONV_EXCL="*"

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -days 7300 -keyout server.key -out server.crt \
    -subj "/CN=ch32h4.local" \
    -addext "subjectAltName=DNS:ch32h4.local" \
    -addext "basicConstraints=critical,CA:TRUE"

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -days 7300 -keyout clientca.key -out clientca.crt \
    -subj "/CN=ch32h4 test client CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"

openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout client.key -out client.csr -subj "/CN=ch32h4 test client"

# An extension file rather than /dev/stdin: the OpenSSL that ships with Git for
# Windows is a native build and cannot open MSYS's /dev/stdin.
cat > client.ext <<'EXT'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=clientAuth
EXT
openssl x509 -req -in client.csr -CA clientca.crt -CAkey clientca.key \
    -CAcreateserial -days 7300 -out client.crt -extfile client.ext
rm -f client.csr client.ext clientca.srl

python ../../../../tools/pem_to_header.py \
    --out ../src/certs.h \
    server_cert_pem=server.crt \
    server_key_pem=server.key \
    client_ca_pem=clientca.crt

echo "regenerated; commit certs/* and src/certs.h"
