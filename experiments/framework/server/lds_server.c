/* ============================================================
 * lds_server.c
 *
 * Local Discovery Server (LDS) per OPC UA FX
 *
 * Funziona come punto di rendezvous per tutti i server UAFX
 * presenti sulla rete. I server si registrano periodicamente
 * tramite UA_Server_addPeriodicServerRegisterCallback().
 * I client si connettono all'LDS via TCP (funziona su VPN)
 * e chiamano FindServers() per ottenere la lista dei server.
 *
 * Requisiti:
 *   open62541 compilato con:
 *     -DUA_ENABLE_DISCOVERY=ON
 *     -DUA_ENABLE_DISCOVERY_MULTICAST=ON  (opzionale, per rete locale)
 *
 * Compilazione:
 *   gcc -o lds_server lds_server.c open62541.c -pthread
 *
 * Utilizzo:
 *   ./lds_server              → porta 4840 (default LDS standard)
 *   ./lds_server 4840         → porta esplicita
 * ============================================================ */

#include "open62541.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* ─── Configurazione ─────────────────────────────────────── */
#define LDS_DEFAULT_PORT    4840
#define LDS_APPLICATION_URI "urn:example:uafx:lds"

/* Timeout: un server viene rimosso dal registro se non si
 * ri-registra entro questo intervallo (ms). */
#define LDS_SERVER_TIMEOUT_MS  60000   /* 60 secondi */

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    (void)sig;
    printf("\n[LDS] Segnale di shutdown ricevuto\n");
    running = false;
}

/* ============================================================
 * Callback: server scoperto via mDNS (se abilitato)
 *
 * Permette all'LDS di scoprire autonomamente server sulla
 * rete locale tramite mDNS, senza aspettare la registrazione.
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

    /* Porta da riga di comando (opzionale) */
    UA_UInt16 port = LDS_DEFAULT_PORT;
    if(argc >= 2) {
        int p = atoi(argv[1]);
        if(p > 0 && p < 65536)
            port = (UA_UInt16)p;
    }

    printf("\n");
    printf("╔═════════════════════════════════════════════════════╗\n");
    printf("║  OPC UA Local Discovery Server (LDS)                ║\n");
    printf("╚═════════════════════════════════════════════════════╝\n\n");
    printf("[LDS] Porta: %d\n", port);
    printf("[LDS] Timeout server inattivi: %d ms\n\n", LDS_SERVER_TIMEOUT_MS);

    /* ─── Creazione server ───────────────────────────────────── */
    UA_Server *server = UA_Server_new();
    if(!server) {
        fprintf(stderr, "[LDS] Errore: impossibile creare UA_Server\n");
        return EXIT_FAILURE;
    }

    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(config, port, NULL);
    config->applicationDescription.discoveryUrlsSize = 1;
    config->applicationDescription.discoveryUrls = (UA_String *)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    config->applicationDescription.discoveryUrls[0] = UA_String_fromChars("opc.tcp://192.168.17.75:4840");
        config->applicationDescription.discoveryUrls[0] = UA_String_fromChars("opc.tcp://192.168.100.3:4840");
    config->mdnsConfig.serverCapabilitiesSize = 0;

    // Specifica gli IP delle interfacce su cui fare mDNS
    UA_UInt32 mdnsIPs[2];
    mdnsIPs[0] = inet_addr("192.168.17.75");
    mdnsIPs[1] = inet_addr("192.168.100.3");
    config->mdnsIpAddressList = mdnsIPs;
    config->mdnsIpAddressListSize = 2;
    /* ─── Identità applicazione ──────────────────────────────── */
    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri =
        UA_String_fromChars(LDS_APPLICATION_URI);

    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Local Discovery Server");

    /* L'LDS si identifica come DiscoveryServer, non come Server */
    config->applicationDescription.applicationType =
        UA_APPLICATIONTYPE_DISCOVERYSERVER;

    /* ─── Abilita Discovery ───────────────────────────────────── */
#ifdef UA_ENABLE_DISCOVERY
    /* Timeout: rimuove dal registro i server che smettono
     * di registrarsi entro LDS_SERVER_TIMEOUT_MS */
//    config->discovery.mdnsEnable = false;   /* solo TCP per ora */


#else
    printf("[LDS] ATTENZIONE: open62541 compilato senza UA_ENABLE_DISCOVERY\n");
    printf("      L'LDS non sarà funzionale.\n");
    printf("      Ricompilare con -DUA_ENABLE_DISCOVERY=ON\n\n");
#endif

    /* ─── mDNS opzionale ─────────────────────────────────────── */
#ifdef UA_ENABLE_DISCOVERY_MULTICAST
    config->mdnsEnabled = UA_TRUE;
    UA_String_clear(&config->mdnsConfig.mdnsServerName);
    config->mdnsConfig.mdnsServerName =
        UA_String_fromChars("UAFX-LDS");

    UA_Server_setServerOnNetworkCallback(server, onServerOnNetwork, NULL);
    printf("[LDS] mDNS: ABILITATO (_opcua-tcp._tcp.local)\n");
#else
    printf("[LDS] mDNS: DISABILITATO (solo discovery TCP/IP)\n");
#endif

    /* ─── Avvio ──────────────────────────────────────────────── */
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

    /* ─── Loop principale ────────────────────────────────────── */
    while(running) {
        UA_Server_run_iterate(server, true);
    }

    /* ─── Shutdown ───────────────────────────────────────────── */
    printf("\n[LDS] Shutdown in corso...\n");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    printf("[LDS] Fermato.\n\n");

    return EXIT_SUCCESS;
}
