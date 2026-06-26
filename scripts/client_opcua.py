import asyncio
from asyncua import Client

async def main():
    url = "opc.tcp://192.168.17.184:4841"

    async with Client(url=url) as client:
        node = client.get_node("ns=1;i=50001")

        print("[CLIENT] Reading ReceivedTemperature node (ns=1;i=50001)...")

        for i in range(10):
            try:
                value = await node.read_value()
                print(f"[{i+1}] Temperature: {value}")
            except Exception as e:
                print(f"[ERROR] {e}")

            await asyncio.sleep(0.5)

if __name__ == "__main__":
    asyncio.run(main())
