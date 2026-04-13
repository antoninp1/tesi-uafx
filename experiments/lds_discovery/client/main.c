/* ============================================================
 * main.c
 *
 * Client OPC UA FX con Discovery via LDS (TCP).
 *
 * Flusso:
 *   1. FindServers() sull'LDS → lista server
 *   2. Per ogni server:
 *      - connect
 *      - browseServerUAFX → popola TopologyNode
 *      - aggiungi al TopologyGraph
 *      - accoda i vicini LLDP nella coda BFS (per fasi future)
 *   3. Stampa il grafo finale
 *
 * Compilazione:
 *   make
 * ============================================================ */

#include "common.h"
#include "helpers.h"
#include "discovery.h"
#include "browse.h"
#include "model.h"
#include "tde.h"
#include <signal.h>
#include <time.h>
#include "link_builder.h"

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
     * Inizializza il grafo topologico
     * ═══════════════════════════════════════════════════════════ */
    TopologyGraph graph;
    topologyGraphInit(&graph);

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
     * FASE 2: Connessione, Browse e popolamento TopologyGraph
     * ═══════════════════════════════════════════════════════════ */

    printSeparator("FASE 2: Browse e popolamento del modello");

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

        /* ─── Prepara un nuovo TopologyNode ────────────── */
        TopologyNode node;
        memset(&node, 0, sizeof(TopologyNode));
        node.type = NODE_TYPE_UAFX_SERVER;

        /* ─── Browse: popola il TopologyNode ───────────── */
        browseServerUAFX(client, discovered.urls[i], &node);

        /* ─── Marca come visitato e aggiungi al grafo ─── */
        node.visited = true;

        /* Se non e' stato impostato un id (chassisId mancante),
         * usa l'applicationUri come fallback per identificarlo */
        if(node.id[0] == '\0' && node.applicationUri[0])
            strncpy(node.id, node.applicationUri, MAX_STR_LEN - 1);

        int nodeIdx = topologyAddNode(&graph, &node);
        if(nodeIdx < 0) {
            printf("  WARNING: TopologyGraph pieno, nodo non aggiunto.\n");
        } else {
            printf("  Nodo aggiunto al grafo (idx=%d, id=%s)\n",
                   nodeIdx, node.id);
        }

        /* ─── Accoda i vicini LLDP per visite future ──── */
        for(size_t k = 0; k < node.interfacesCount; k++) {
            const NetworkInterface *iface = &node.interfaces[k];
            for(size_t n = 0; n < iface->neighborsCount; n++) {
                const LldpNeighbor *nbr = &iface->neighbors[n];
                if(nbr->chassisId[0] == '\0')
                    continue;

                DiscoveryQueueEntry entry;
                memset(&entry, 0, sizeof(entry));
                strncpy(entry.chassisId, nbr->chassisId, MAX_STR_LEN - 1);
                strncpy(entry.mgmtAddress, nbr->mgmtAddress, MAX_STR_LEN - 1);
                strncpy(entry.sysName, nbr->sysName, MAX_STR_LEN - 1);
                strncpy(entry.capabilities, nbr->systemCapabilities, MAX_STR_LEN - 1);

                if(discoveryEnqueue(&graph, &entry))
                    printf("  + Vicino accodato: %s (%s) @ %s\n",
                           nbr->sysName, nbr->chassisId, nbr->mgmtAddress);
            }
        }

        UA_Client_disconnect(client);
        printf("  Disconnesso.\n");
    }

    UA_Client_delete(client);

    /* ═══════════════════════════════════════════════════════════
     * FASE 3: BFS sui vicini via TDE
     *
     * Processa la coda dei vicini scoperti via LLDP. Per ognuno,
     * la TDE sceglie l'adapter giusto (SSH+lldpcli per Relyum,
     * altri vendor in futuro) e ne estrae i dati LLDP.
     * I nuovi vicini scoperti vengono accodati a loro volta.
     * ═══════════════════════════════════════════════════════════ */

    printSeparator("FASE 3: BFS via TDE");

    DiscoveryQueueEntry entry;
    while(running && discoveryDequeue(&graph, &entry)) {
        printf("\n[BFS] Visiting: %s (%s) @ %s\n",
               entry.sysName, entry.chassisId, entry.mgmtAddress);

        /* Skip se gia' visitato (puo' succedere se popolato
         * durante una visita precedente) */
        if(discoveryIsVisited(&graph, entry.chassisId)) {
            printf("  (gia' visitato, skip)\n");
            continue;
        }

        TdeQueryResult tdeResult;
        bool ok = tdeQueryDevice(&entry, &tdeResult);

        if(!ok) {
            /* Aggiungi comunque il nodo come PHANTOM (visto via LLDP
             * ma non raggiungibile direttamente) */
            TopologyNode phantom;
            memset(&phantom, 0, sizeof(phantom));
            strncpy(phantom.id, entry.chassisId, MAX_STR_LEN - 1);
            strncpy(phantom.name, entry.sysName, MAX_STR_LEN - 1);
            strncpy(phantom.mgmtAddress, entry.mgmtAddress, MAX_STR_LEN - 1);
            phantom.type = NODE_TYPE_PHANTOM_SWITCH;
            phantom.reachable = false;
            phantom.visited = true;
            topologyAddNode(&graph, &phantom);
            continue;
        }

        /* Stampa il risultato in JSON (debug) */
        tdeResultPrintJson(&tdeResult);

        /* Costruisci il nodo dal risultato TDE */
        TopologyNode tdeNode;
        memset(&tdeNode, 0, sizeof(tdeNode));
        tdeApplyResultToNode(&tdeResult, &tdeNode);

        int idx = topologyAddNode(&graph, &tdeNode);
        printf("  Nodo TDE aggiunto al grafo (idx=%d)\n", idx);

        /* Accoda i nuovi vicini scoperti */
        for(size_t k = 0; k < tdeResult.neighborsCount; k++) {
            const TdeNeighborInfo *nbr = &tdeResult.neighbors[k];
            if(nbr->chassisId[0] == '\0') continue;

            DiscoveryQueueEntry newEntry;
            memset(&newEntry, 0, sizeof(newEntry));
            strncpy(newEntry.chassisId, nbr->chassisId, MAX_STR_LEN - 1);
            strncpy(newEntry.mgmtAddress, nbr->mgmtAddress, MAX_STR_LEN - 1);
            strncpy(newEntry.sysName, nbr->sysName, MAX_STR_LEN - 1);
            strncpy(newEntry.capabilities, nbr->systemCapabilities, MAX_STR_LEN - 1);

            if(discoveryEnqueue(&graph, &newEntry))
                printf("  + Nuovo vicino accodato: %s\n", nbr->sysName);
        }
    }

    /* ═══════════════════════════════════════════════════════════
     * Timestamp dello scan
     * ═══════════════════════════════════════════════════════════ */
    graph.lastScanTime = time(NULL);


/* ═══════════════════════════════════════════════════════════
 * FASE 2.6: costruzione dei link fisici dai vicini LLDP
 * ═══════════════════════════════════════════════════════════ */

    printSeparator("FASE 4: Costruzione Links");
    buildLinksFromNeighbors(&graph);	


    /* ═══════════════════════════════════════════════════════════
     * FASE 5: Stampa il grafo popolato
     * ═══════════════════════════════════════════════════════════ */

    printSeparator("FASE 5: Modello popolato");
    topologyGraphPrint(&graph);

    /* Coda BFS rimanente (verra' processata in iterazioni future) */
    if(graph.queueHead != graph.queueTail) {
        printf("Vicini in coda BFS (non ancora visitati):\n");
        size_t idx = graph.queueHead;
        while(idx != graph.queueTail) {
            DiscoveryQueueEntry *e = &graph.queue[idx];
            printf("  - %-25s ChassisId=%s  MgmtAddr=%s  Caps=%s\n",
                   e->sysName, e->chassisId, e->mgmtAddress, e->capabilities);
            idx = (idx + 1) % MAX_DISCOVERY_QUEUE;
        }
        printf("\n");
    }

    printSeparator("Discovery completato");
    printf("\n");

    topologyGraphClear(&graph);
    return EXIT_SUCCESS;
}
