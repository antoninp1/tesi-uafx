#!/bin/sh

openssl genrsa -out ca.key.pem 4096
openssl req -x509 -new -nodes -key ca.key.pem -sha256 -days 3650 \
    -out ca.cert.pem -subj "/CN=UAFX Lab CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl x509 -in ca.cert.pem -outform DER -out ca.cert.der
mkdir -p newcerts; touch index.txt; echo 1000 > serial; echo 1000 > crlnumber
