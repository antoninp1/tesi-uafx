import asyncio
from asyncua import Client, ua

LDS_URL = "opc.tcp://192.168.17.112:4840"

async def main():
    async with Client(url=LDS_URL) as client:
        params = ua.FindServersParameters()
        params.EndpointUrl = ""          # vide -> pas de mirroring
        params.ServerUris = []
        servers = await client.uaclient.find_servers(params)

        print(f"=== {len(servers)} serveur(s) enregistré(s) ===")
        for s in servers:
            print(f"- {s.ApplicationName.Text} ({s.ApplicationUri})")
            print(f"    DiscoveryUrls: {s.DiscoveryUrls}")

if __name__ == "__main__":
    asyncio.run(main())
