#!/bin/bash
# generate_device_certs.sh

set -e

LIST_FILE="${1:-list.txt}"

if [ ! -f "$LIST_FILE" ]; then
    echo "ERROR: $LIST_FILE introuvable" >&2
    exit 1
fi

if [ ! -f ca.conf ] || [ ! -f ca.key.pem ] || [ ! -f ca.cert.pem ]; then
    echo "ERROR: ca.conf / ca.key.pem / ca.cert.pem missing in $(pwd)" >&2
    exit 1
fi

while read -r DEVICE URI; do
    [ -z "$DEVICE" ] && continue
    [ -z "$URI" ] && continue

    echo "=== $DEVICE ($URI) ==="

    openssl genrsa -out "${DEVICE}.key.pem" 2048 2>/dev/null

    openssl req -new -key "${DEVICE}.key.pem" -out "${DEVICE}.csr" \
        -subj "/CN=${DEVICE}" \
        -addext "subjectAltName=URI:${URI}"

    openssl ca -batch -config ca.conf \
        -in "${DEVICE}.csr" -out "${DEVICE}.cert.pem" \
        -extfile <(printf "subjectAltName=URI:%s\nkeyUsage=critical,digitalSignature,nonRepudiation,keyEncipherment\nextendedKeyUsage=clientAuth,serverAuth\n" "${URI}")

    openssl x509 -in "${DEVICE}.cert.pem" -outform DER -out "${DEVICE}.cert.der"
    openssl pkcs8 -topk8 -nocrypt -in "${DEVICE}.key.pem" -outform DER -out "${DEVICE}.key.der"

    echo "  -> $(openssl x509 -in "${DEVICE}.cert.der" -inform DER -noout -ext subjectAltName | tail -1)"

    rm -f "${DEVICE}.csr"

    echo
done < "$LIST_FILE"

echo "=== Finished. Generated certificates : ==="
ls -1 *.cert.der 2>/dev/null
