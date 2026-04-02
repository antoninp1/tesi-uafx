/* ============================================================
 * main.c
 *
 * Client OPC UA FX con Discovery via LDS (TCP)
 *
 * Fase 1: si connette all'LDS via TCP e chiama FindServers()
 *         per ottenere la lista dei server registrati.
 * Fase 2: per ogni server scoperto, si connette e naviga
 *         l'Address Space UAFX.
 *
 * Compilazione:
 *   gcc -o discovery_client main.c browse.c discovery.c \
 *       helpers.c open62541.c -pthread
 * ============================================================ */

#include "common.h"
#include "helpers.h"
#include "discovery.h"
#include "browse.h"
#include <signal.h>

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    (void)sig;
    running = false;
}

int main(int argc, char **argv) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);

    printf("\n");
    printSeparator("OPC UA FX Discovery Client - LDS TCP");
    printf("\n");

    const char *ldsUrl = LDS_URL;
    if(argc >= 2)
        ldsUrl = argv[1];

    /* ═══════════════════════════════════════════════════════════
     * FASE 1: Discovery via LDS
     * ═══════════════════════════════════════════════════════════ */

    printf("[FASE 1] LDS Discovery via TCP\n");
    for(int i = 0; i < 56; i++) printf("-");
    printf("\n");

    DiscoveryList discovered;
    memset(&discovered, 0, sizeof(discovered));

    UA_StatusCode retval = runLdsDiscovery(&discovered, ldsUrl);

    if(retval != UA_STATUSCODE_GOOD || discovered.count == 0) {
        printf("\n  Nessun server scoperto tramite LDS.\n");
        printf("  Verifica che l'LDS sia in esecuzione e che i server siano registrati.\n\n");
        return EXIT_FAILURE;
    }

    printf("\n  Trovati %zu server tramite LDS.\n\n", discovered.count);

    /* ═══════════════════════════════════════════════════════════
     * FASE 2: Connessione e Browse UAFX
     * ═══════════════════════════════════════════════════════════ */

    printSeparator("FASE 2: Connessione e Browse UAFX");

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    for(size_t i = 0; i < discovered.count && running; i++) {
        printf("\n[Server %zu/%zu] %s\n",
               i + 1, discovered.count, discovered.names[i]);

        retval = UA_Client_connect(client, discovered.urls[i]);

        if(retval != UA_STATUSCODE_GOOD) {
            printf("  Connessione fallita: %s\n",
                   UA_StatusCode_name(retval));
            UA_Client_disconnect(client);
            continue;
        }

        printf("  Connesso.\n");
        browseServerUAFX(client, discovered.urls[i]);
        UA_Client_disconnect(client);
        printf("  Disconnesso.\n");
    }

    UA_Client_delete(client);

    printSeparator("Discovery completato");
    printf("\n");

    return EXIT_SUCCESS;
}
