import asyncio
from asyncua import Client

SERVER_URL = "opc.tcp://192.168.17.92:4841"


async def list_nodes(node, prefix=""):
    for child in await node.get_children():
        name = await child.read_browse_name()
        print(f"{prefix}{name.NamespaceIndex}:{name.Name}")
        await list_nodes(child, prefix + "  ")


async def main():
    async with Client(url=SERVER_URL) as client:
        fxroot = await client.nodes.root.get_child(["0:Objects", "4:FxRoot"])
        await list_nodes(fxroot)


if __name__ == "__main__":
    asyncio.run(main())
