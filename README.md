# tesi-uafx

Beware : This fork is only compatible with GNU/Linux machines due to the usage of the process scheduler and core selection mechanism

## Testing the project

Clone the repository :
```bash
git clone --recurse-submodules https://github.com/antoninp1/tesi-uafx
cd tesi-uafx
```

Create build directory and compile the project :
```bash
mkdir build
cmake -S . -B build
cmake --build build -j$(nproc)
```

Install the certificates :
```bash
cd scripts
python -m venv .
source bin/activate
pip install asyncua
./generate_certs.sh
```

Applying `open62541` patches :
```bash
cd lib/open62541
git apply ../../patches/[patchname].patch
```

Running the applications inside systemd services :
```bash
useradd -r -s /usr/sbin/nologin -d /opt/uafx -m uafx # Create the user who executes the service
mkdir /opt/uafx/bin /opt/uafx/certs
cp ./build/experiments/framework/server/pub_test ./build/experiments/framework/server/sub_test /opt/uafx/bin/ # Copy the binaries to user directory
cp ./scripts/certs/* /opt/uafx/certs/ # Copy the certs to the user directory
chown -R uafx:uafx /opt/uafx/ # Change the owner to uafx
chmod -R 550 /opt/uafx/ # Give only execution and reading rights to uafx user on its files
cp services/* /etc/systemd/system/ # Copy the services to the service system directory
systemctl start uafx-[publisher/subscriber] # Start the application
journalctl -u uafx-[publisher/subscriber] -f # Show continuous application logs
```

The fork main modifications are present in the directory `experiments/framework/server`, which contains LDS and SKS server implementation, publisher and subscriber implementation with encryption and real-time features configurable through command line arguments.

This fork also upgraded `open62541` version to `v1.5.4` to get SKS feature working.

Please note that the encryption module requires MBEDTLS library that can be installed on Debian systems with the following command : `apt install libmbedtls-dev`.