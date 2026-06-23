/* ============================================================
 * lds_server.c  (Kathara edition)
 *
 * Local Discovery Server (LDS) per OPC UA FX.
 * Punto di rendezvous TCP per i server UAFX: i temp_server si
 * registrano qui via UA_Server_registerDiscovery(), e il backend
 * (discovery) interroga l'LDS con FindServers() per ottenere la
 * lista dei server da scoprire.
 *
 * Differenze rispetto alla versione originale:
 *   - Nessun IP hardcoded: il discoveryUrl annunciato e' preso da
 *     LDS_ADVERTISE_URL (default opc.tcp://10.0.0.1:4840), adatto al lab.
 *   - mDNS DISABILITATO: nei container il multicast mDNS fallisce e non
 *     serve, la discovery e' puramente TCP via LDS.
 *
 * Compilazione (in container bookworm per compatibilita' glibc):
 *   gcc -o lds_server lds_server.c open62541.c -pthread
 *
 * Uso:
 *   ./lds_server            -> porta 4840 (default)
 *   ./lds_server 4840       -> porta esplicita
 *   LDS_ADVERTISE_URL="opc.tcp://10.0.0.1:4840" ./lds_server 4840
 * ============================================================ */

#include "open62541.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Configurazione --- */
#define LDS_DEFAULT_PORT    4840
#define LDS_APPLICATION_URI "urn:example:uafx:lds"

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    (void)sig;
    printf("\n[LDS] Segnale di shutdown ricevuto\n");
    running = false;
}

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

    /* URL annunciato: da env, con default per il lab Kathara */
    const char *ldsAdv = getenv("LDS_ADVERTISE_URL");
    if(!ldsAdv || !*ldsAdv) ldsAdv = "opc.tcp://10.0.0.1:4840";

    printf("\n");
    printf("========================================================\n");
    printf("  OPC UA Local Discovery Server (LDS) - Kathara\n");
    printf("========================================================\n\n");
    printf("[LDS] Porta:        %d\n", port);
    printf("[LDS] Advertise URL: %s\n\n", ldsAdv);

    /* --- Creazione server --- */
    UA_Server *server = UA_Server_new();
    if(!server) {
        fprintf(stderr, "[LDS] Errore: impossibile creare UA_Server\n");
        return EXIT_FAILURE;
    }

    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimal(config, port, NULL);

    /* discoveryUrl annunciato (niente IP hardcoded) */
    config->applicationDescription.discoveryUrlsSize = 1;
    config->applicationDescription.discoveryUrls =
        (UA_String *)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    config->applicationDescription.discoveryUrls[0] = UA_String_fromChars(ldsAdv);

    /* mDNS disabilitato: solo discovery TCP nel lab */
    config->mdnsConfig.serverCapabilitiesSize = 0;

    /* --- Identita' applicazione --- */
    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri =
        UA_String_fromChars(LDS_APPLICATION_URI);

    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Local Discovery Server");

    /* L'LDS si identifica come DiscoveryServer, non come Server */
    config->applicationDescription.applicationType =
        UA_APPLICATIONTYPE_DISCOVERYSERVER;

#ifndef UA_ENABLE_DISCOVERY
    printf("[LDS] ATTENZIONE: open62541 compilato senza UA_ENABLE_DISCOVERY\n");
    printf("      L'LDS non sara' funzionale.\n");
    printf("      Ricompilare con -DUA_ENABLE_DISCOVERY=ON\n\n");
#endif

    printf("[LDS] mDNS: DISABILITATO (solo discovery TCP/IP)\n");

    /* --- Avvio --- */
    UA_StatusCode retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "[LDS] Errore avvio: %s\n",
                UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    printf("\n");
    printf("========================================================\n");
    printf("  LDS IN ESECUZIONE\n");
    printf("========================================================\n");
    printf("  Endpoint:       opc.tcp://0.0.0.0:%d\n", port);
    printf("  ApplicationUri: %s\n", LDS_APPLICATION_URI);
    printf("  Ctrl+C per fermare\n");
    printf("========================================================\n\n");
    printf("[LDS] In attesa di registrazioni dai server UAFX...\n\n");

    /* --- Loop principale --- */
    while(running) {
        UA_Server_run_iterate(server, true);
    }

    /* --- Shutdown --- */
    printf("\n[LDS] Shutdown in corso...\n");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    printf("[LDS] Fermato.\n\n");

    return EXIT_SUCCESS;
}
