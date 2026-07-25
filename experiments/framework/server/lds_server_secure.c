/* ============================================================
 * lds_server.c
 *
 * Local Discovery Server (LDS) for OPC UA FX
 *
 * Acts as the rendezvous point for all UAFX servers on the
 * network. Servers register periodically via
 * UA_Server_addPeriodicServerRegisterCallback(). Clients connect
 * to the LDS over TCP (works over VPN) and call FindServers() to
 * get the list of servers.
 *
 * Requirements:
 *   open62541 built with:
 *     -DUA_ENABLE_DISCOVERY=ON
 *     -DUA_ENABLE_DISCOVERY_MULTICAST=ON  (optional, for the local network)
 *
 * Build:
 *   gcc -o lds_server lds_server.c open62541.c -pthread
 *
 * Usage:
 *   ./lds_server <cert-dir>              → port 4840 (standard LDS default)
 *   ./lds_server <cert-dir> <port>       → explicit port
 *
 * <cert-dir> must contain lds_server.cert.der, lds_server.key.der,
 * ca.cert.der and crl.der (see buildCertPath() in sks_helpers.c).
 * ============================================================ */

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/securitypolicy_default.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "sks_helpers.h"

/* ─── Configuration ─────────────────────────────────────── */
#define LDS_DEFAULT_PORT    4840
#define LDS_APPLICATION_URI "urn:example:uafx:lds"

/* Timeout: a server is dropped from the registry if it doesn't
 * re-register within this interval (ms). */
#define LDS_SERVER_TIMEOUT_MS  60000   /* 60 seconds */

#define LDS_SERVER_IP_ADDR "192.168.17.112"

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    (void)sig;
    printf("\n[LDS] Segnale di shutdown ricevuto\n");
    running = false;
}

/* ============================================================
 * Callback: server discovered via mDNS (when enabled)
 *
 * Lets the LDS autonomously discover servers on the local
 * network via mDNS, without waiting for their registration.
 * ============================================================ */
#ifdef UA_ENABLE_DISCOVERY_MULTICAST
static void
onServerOnNetwork(const UA_ServerOnNetwork *son,
                  UA_Boolean isServerAnnounce,
                  UA_Boolean isTxtReceived,
                  void *data) {
    (void)data;
    if(!isServerAnnounce) return;

    printf("[LDS] mDNS: %s  →  %.*s\n",
           isTxtReceived ? "Annuncio" : "Annuncio (TXT pend.)",
           (int)son->discoveryUrl.length,
           son->discoveryUrl.data);
}
#endif

/* ============================================================
 * MAIN
 * ============================================================ */
int main(int argc, char **argv) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);

    /* Port from the command line (optional) */
    UA_UInt16 port = LDS_DEFAULT_PORT;
    if(argc >= 3) {
        int p = atoi(argv[2]);
        if(p > 0 && p < 65536)
            port = (UA_UInt16)p;
    }

    UA_ByteString crl = loadFile(buildCertPath(argv[1], "crl.der"));
    UA_ByteString cert = loadFile(buildCertPath(argv[1], "lds_server.cert.der"));
    UA_ByteString key = loadFile(buildCertPath(argv[1], "lds_server.key.der"));
    UA_ByteString caCert = loadFile(buildCertPath(argv[1], "ca.cert.der"));

    printf("\n");
    printf("╔═════════════════════════════════════════════════════╗\n");
    printf("║  OPC UA Local Discovery Server (LDS)                ║\n");
    printf("╚═════════════════════════════════════════════════════╝\n\n");
    printf("[LDS] Porta: %d\n", port);
    printf("[LDS] Timeout server inattivi: %d ms\n\n", LDS_SERVER_TIMEOUT_MS);

    /* ─── Create server ───────────────────────────────────── */
    //UA_Server *server = UA_Server_new();
    UA_ServerConfig config;
    memset(&config, 0, sizeof(UA_ServerConfig));

    //UA_ServerConfig_setMinimal(config, port, NULL);
    UA_StatusCode res = UA_ServerConfig_setDefaultWithSecurityPolicies(
        &config, port, &cert, &key, &caCert, 1, NULL, 0, &crl, 1);

    config.applicationDescription.discoveryUrlsSize = 1;
    config.applicationDescription.discoveryUrls = (UA_String *)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    char discoveryUrlBuf[64];
    snprintf(discoveryUrlBuf, sizeof(discoveryUrlBuf), "opc.tcp://%s:4840", LDS_SERVER_IP_ADDR);
    config.applicationDescription.discoveryUrls[0] = UA_String_fromChars(discoveryUrlBuf);
    config.mdnsConfig.serverCapabilitiesSize = 0;


    // Specify the interface IPs to run mDNS on
    /*
    UA_UInt32 mdnsIPs[2];
    mdnsIPs[0] = inet_addr("192.168.17.92");
    mdnsIPs[1] = inet_addr("192.168.100.3");
    config->mdnsIpAddressList = mdnsIPs;
    config->mdnsIpAddressListSize = 2;*/
    config.mdnsInterfaceIP = UA_String_fromChars(LDS_SERVER_IP_ADDR);
    /* ─── Application identity ──────────────────────────────── */
    UA_String_clear(&config.applicationDescription.applicationUri);
    config.applicationDescription.applicationUri = UA_String_fromChars(LDS_APPLICATION_URI);

    UA_LocalizedText_clear(&config.applicationDescription.applicationName);
    config.applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Local Discovery Server");

    /* The LDS identifies itself as a DiscoveryServer, not a Server */
    config.applicationDescription.applicationType = UA_APPLICATIONTYPE_DISCOVERYSERVER;

    /* ─── Enable Discovery ───────────────────────────────────── */
#ifdef UA_ENABLE_DISCOVERY
    /* Timeout: drops servers from the registry once they stop
     * re-registering within LDS_SERVER_TIMEOUT_MS */
//    config->discovery.mdnsEnable = false;   /* TCP only for now */


#else
    printf("[LDS] ATTENZIONE: open62541 compilato senza UA_ENABLE_DISCOVERY\n");
    printf("      L'LDS non sarà funzionale.\n");
    printf("      Ricompilare con -DUA_ENABLE_DISCOVERY=ON\n\n");
#endif

    /* ─── Optional mDNS ─────────────────────────────────────── */
    /*
#ifdef UA_ENABLE_DISCOVERY_MULTICAST
    // Disabling mDNS to avoid bad registration
    config.mdnsEnabled = UA_FALSE;
    UA_String_clear(&config.mdnsConfig.mdnsServerName);
    config.mdnsConfig.mdnsServerName =
        UA_String_fromChars("UAFX-LDS");

    UA_Server_setServerOnNetworkCallback(server, onServerOnNetwork, NULL);
    printf("[LDS] mDNS: ABILITATO (_opcua-tcp._tcp.local)\n");
#else
    printf("[LDS] mDNS: DISABILITATO (solo discovery TCP/IP)\n");
#endif*/

    /* ─── Startup ──────────────────────────────────────────────── */

    /* Only allow encrypted endpoints (drop SecurityMode=None) */
    for(size_t i = 0; i < config.endpointsSize; i++) {
        if(config.endpoints[i].securityMode == UA_MESSAGESECURITYMODE_NONE) {
            UA_EndpointDescription_clear(&config.endpoints[i]);
            if(i + 1 < config.endpointsSize) {
                config.endpoints[i] = config.endpoints[config.endpointsSize - 1];
                i--;
            }
            config.endpointsSize--;
        }
    }
    disableAnonymous(&config);
    config.accessControl.activateSession = activateSession; 
    UA_Server *server = UA_Server_newWithConfig(&config);

    if(!server) {
        fprintf(stderr, "[LDS] Errore: impossibile creare UA_Server\n");
        return EXIT_FAILURE;
    }

    UA_StatusCode retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "[LDS] Errore avvio: %s\n",
                UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    printf("\n");
    printf("════════════════════════════════════════════════════════\n");
    printf("  LDS IN ESECUZIONE\n");
    printf("════════════════════════════════════════════════════════\n");
    printf("  Endpoint:      opc.tcp://0.0.0.0:%d\n", port);
    printf("  ApplicationUri: %s\n", LDS_APPLICATION_URI);
    printf("  Ctrl+C per fermare\n");
    printf("════════════════════════════════════════════════════════\n\n");
    printf("[LDS] In attesa di registrazioni dai server UAFX...\n\n");

    /* ─── Main loop ────────────────────────────────────── */
    while(running) {
        UA_Server_run_iterate(server, true);
    }

    /* ─── Shutdown ────────────────────────────────────────────── */
    printf("\n[LDS] Shutdown in corso...\n");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    printf("[LDS] Fermato.\n\n");

    return EXIT_SUCCESS;
}
