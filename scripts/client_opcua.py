import asyncio
from asyncua import Client, ua
from asyncua.crypto.security_policies import SecurityPolicyBasic256Sha256

async def main():
    url = "opc.tcp://192.168.17.184:4941"
    client = Client(url=url)
    client.application_uri = "urn:example:uafx:asyncua"
    await client.set_security(
        SecurityPolicyBasic256Sha256,
        certificate="certs/asyncua.cert.der",
        private_key="certs/asyncua.key.der",
        mode=ua.MessageSecurityMode.SignAndEncrypt
    )
    await client.load_private_key("certs/asyncua.key.der")
    await client.load_client_certificate("certs/asyncua.cert.der")

    async with client:
        node = client.get_node("ns=1;i=50001")

        print("[CLIENT] Reading ReceivedTemperature node (ns=1;i=50001)...")

        for i in range(100):
            try:
                value = await node.read_value()
                print(f"[{i+1}] Temperature: {value}")
            except Exception as e:
                print(f"[ERROR] {e}")

            await asyncio.sleep(5)

if __name__ == "__main__":
    asyncio.run(main())
