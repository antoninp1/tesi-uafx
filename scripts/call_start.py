import asyncio
from asyncua import Client
import sys

SUB_ADDR = "192.168.17.184"
PUB_ADDR = "192.168.17.92"

NODE_PATH_SUB = ["0:Objects", "4:FxRoot", "1:DensitySensor", "1:StartSubscriber"]
NODE_PATH_PUB = ["0:Objects", "4:FxRoot", "1:TemperatureSensor", "1:StartPublisher"]

args = sys.argv


if "-p" in args:
    SERVER_URL = "opc.tcp://%s:4841"%PUB_ADDR
    NODE_PATH = NODE_PATH_PUB
elif "-s" in args:
    SERVER_URL = "opc.tcp://%s:4841"%SUB_ADDR
    NODE_PATH = NODE_PATH_SUB
else:
    quit()



async def main():
    client = Client(url=SERVER_URL)
    async with Client(url=SERVER_URL) as client:
        client.set_user("uafx-operator")
        client.set_password("ChangeThisOperatorPasswordInLab")
        objects = client.get_objects_node()
        fxroot = await objects.get_child(["4:FxRoot"])
        sensor = await fxroot.get_child([NODE_PATH[2]])
        start_pub = await sensor.get_child([NODE_PATH[3]])
        result = await sensor.call_method(start_pub.nodeid)
        print("Résultat:", result)


if __name__ == "__main__":
    asyncio.run(main())
