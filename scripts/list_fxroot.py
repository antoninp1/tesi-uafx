import asyncio
from asyncua import Client, ua
from asyncua.crypto.security_policies import SecurityPolicyBasic256Sha256

SERVER_URL = "opc.tcp://192.168.17.92:4941"

async def list_nodes(node, prefix=""):
    for child in await node.get_children():
        name = await child.read_browse_name()
        print(f"{prefix}{name.NamespaceIndex}:{name.Name}")
        await list_nodes(child, prefix + "  ")


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
        fxroot = await client.nodes.root.get_child(["0:Objects", "4:FxRoot"])
        await list_nodes(fxroot)


if __name__ == "__main__":
    asyncio.run(main())
