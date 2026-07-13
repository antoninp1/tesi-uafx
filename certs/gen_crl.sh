#!/bin/sh

openssl ca -config ca.conf -gencrl -out crl.pem
openssl crl -in crl.pem -outform DER -out crl.der
