import asyncio
from asyncua import Client, ua
from asyncua.crypto.security_policies import SecurityPolicyBasic256Sha256

LDS_URL = "opc.tcp://192.168.17.112:4840"

async def main():
    client = Client(url=LDS_URL)
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
        params = ua.FindServersParameters()
        params.EndpointUrl = ""
        params.ServerUris = []
        servers = await client.uaclient.find_servers(params)

        print(f"=== {len(servers)} server(s) registered ===")
        for s in servers:
            print(f"- {s.ApplicationName.Text} ({s.ApplicationUri})")
            print(f"    DiscoveryUrls: {s.DiscoveryUrls}")

if __name__ == "__main__":
    asyncio.run(main())
