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
./generate_certs.sh
```

Run the testing scripts :
```bash
cd scripts
python3 -m venv .
source bin/activate
pip install asyncua
```

The fork main modifications are present in the directory `experiments/framework/server`, which contains LDS and SKS server implementation, publisher and subscriber implementation with encryption and real-time features configurable through command line arguments.

This fork also upgraded `open62541` version to `v1.5.4` to get SKS feature working.

Please note that the encryption module requires MBEDTLS library that can be installed on Debian systems with the following command : `apt install libmbedtls-dev`.