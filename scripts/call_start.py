import asyncio
from asyncua import Client, ua
from asyncua.crypto.security_policies import SecurityPolicyBasic256Sha256
import sys

SUB_ADDR = "192.168.17.184"
PUB_ADDR = "192.168.17.92"

NODE_PATH_SUB = ["0:Objects", "4:FxRoot", "1:DensitySensor", "1:StartSubscriber"]
NODE_PATH_PUB = ["0:Objects", "4:FxRoot", "1:TemperatureSensor", "1:StartPublisher"]

args = sys.argv


if "-p" in args:
    SERVER_URL = "opc.tcp://%s:4941"%PUB_ADDR
    NODE_PATH = NODE_PATH_PUB
elif "-s" in args:
    SERVER_URL = "opc.tcp://%s:4941"%SUB_ADDR
    NODE_PATH = NODE_PATH_SUB
else:
    quit()



async def main():
    client = Client(url=SERVER_URL)
    client.application_uri = "urn:example:uafx:asyncua"
    await client.set_security(
        SecurityPolicyBasic256Sha256,
        certificate="../certs/asyncua.cert.der",
        private_key="../certs/asyncua.key.der",
        mode=ua.MessageSecurityMode.SignAndEncrypt
    )
    await client.load_private_key("../certs/asyncua.key.der")
    await client.load_client_certificate("../certs/asyncua.cert.der")

    async with client:
        objects = client.get_objects_node()
        fxroot = await objects.get_child(["4:FxRoot"])
        sensor = await fxroot.get_child([NODE_PATH[2]])
        start_pub = await sensor.get_child([NODE_PATH[3]])
        result = await sensor.call_method(start_pub.nodeid)
        print("Résultat:", result)


if __name__ == "__main__":
    asyncio.run(main())
