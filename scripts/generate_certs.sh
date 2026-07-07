#!/bin/sh

mkdir certs
cp ../lib/open62541/tools/certs/* certs/
cd certs
python create_self-signed.py . -u "urn:example:uafx:temperature-server" -c temperature_server
python create_self-signed.py . -u "urn:example:uafx:density-server" -c density_server
python create_self-signed.py . -u "urn:example:uafx:publisher" -c publisher
python create_self-signed.py . -u "urn:example:uafx:subscriber" -c subscriber
python create_self-signed.py . -u "urn:example:uafx:sks-server" -c sks_server
python create_self-signed.py . -u "urn:example:uafx:lds" -c lds_server
python create_self-signed.py . -u "urn:example:uafx:asyncua" -c asyncua
