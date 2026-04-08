/* ============================================================
 * model.c
 *
 * Implementazione delle funzioni di gestione del modello
 * topologico UAFX.
 * ============================================================ */

#include "model.h"
#include <time.h>

/* ============================================================
 * topologyGraphInit
 * ============================================================ */

void topologyGraphInit(TopologyGraph *graph) {
    memset(graph, 0, sizeof(TopologyGraph));
    graph->queueHead = 0;
    graph->queueTail = 0;
    graph->lastScanTime = 0;
}

/* ============================================================
 * topologyGraphClear
 *
 * Con array statici e' equivalente a init.
 * Predisposto per deallocazione dinamica futura.
 * ============================================================ */

void topologyGraphClear(TopologyGraph *graph) {
    topologyGraphInit(graph);
}

/* ============================================================
 * topologyFindNodeByChassisId
 *
 * Ricerca lineare per chassisId (== TopologyNode.id).
 * Restituisce indice o -1.
 * ============================================================ */

int topologyFindNodeByChassisId(const TopologyGraph *graph,
                                 const char *chassisId) {
    if(!chassisId || chassisId[0] == '\0')
        return -1;

    for(size_t i = 0; i < graph->nodesCount; i++) {
        if(strcmp(graph->nodes[i].id, chassisId) == 0)
            return (int)i;
    }
    return -1;
}

/* ============================================================
 * topologyFindNodeByEndpoint
 * ============================================================ */

int topologyFindNodeByEndpoint(const TopologyGraph *graph,
                                const char *endpointUrl) {
    if(!endpointUrl || endpointUrl[0] == '\0')
        return -1;

    for(size_t i = 0; i < graph->nodesCount; i++) {
        if(strcmp(graph->nodes[i].endpointUrl, endpointUrl) == 0)
            return (int)i;
    }
    return -1;
}

/* ============================================================
 * topologyAddNode
 * ============================================================ */

int topologyAddNode(TopologyGraph *graph, const TopologyNode *node) {
    if(graph->nodesCount >= MAX_TOPOLOGY_NODES)
        return -1;

    size_t idx = graph->nodesCount;
    graph->nodes[idx] = *node;
    graph->nodesCount++;
    return (int)idx;
}

/* ============================================================
 * topologyFindLink
 *
 * Cerca un link tra due porte. Il confronto e' bidirezionale:
 * (A,B) matcha sia (A,B) che (B,A).
 * ============================================================ */

int topologyFindLink(const TopologyGraph *graph,
                      const char *chassisIdA, const char *portIdA,
                      const char *chassisIdB, const char *portIdB) {
    for(size_t i = 0; i < graph->linksCount; i++) {
        const TopologyLink *link = &graph->links[i];

        /* Match diretto: A-B */
        if(strcmp(link->endpointA.chassisId, chassisIdA) == 0 &&
           strcmp(link->endpointA.portId, portIdA) == 0 &&
           strcmp(link->endpointB.chassisId, chassisIdB) == 0 &&
           strcmp(link->endpointB.portId, portIdB) == 0)
            return (int)i;

        /* Match inverso: B-A */
        if(strcmp(link->endpointA.chassisId, chassisIdB) == 0 &&
           strcmp(link->endpointA.portId, portIdB) == 0 &&
           strcmp(link->endpointB.chassisId, chassisIdA) == 0 &&
           strcmp(link->endpointB.portId, portIdA) == 0)
            return (int)i;
    }
    return -1;
}

/* ============================================================
 * topologyAddLink
 *
 * Aggiunge un link. Se esiste gia' nell'altra direzione,
 * lo marca come confermato bidirezionale.
 * ============================================================ */

int topologyAddLink(TopologyGraph *graph, const TopologyLink *link) {
    /* Cerca se esiste gia' */
    int existing = topologyFindLink(graph,
        link->endpointA.chassisId, link->endpointA.portId,
        link->endpointB.chassisId, link->endpointB.portId);

    if(existing >= 0) {
        /* Link gia' presente — confermato bidirezionale */
        graph->links[existing].confirmedBidirectional = true;
        return existing;
    }

    /* Nuovo link */
    if(graph->linksCount >= MAX_TOPOLOGY_LINKS)
        return -1;

    size_t idx = graph->linksCount;
    graph->links[idx] = *link;
    graph->linksCount++;
    return (int)idx;
}

/* ============================================================
 * discoveryEnqueue
 *
 * Accoda un dispositivo per la visita BFS.
 * Controlla prima se il chassisId e' gia' stato visitato
 * o e' gia' in coda.
 * ============================================================ */

bool discoveryEnqueue(TopologyGraph *graph,
                       const DiscoveryQueueEntry *entry) {
    /* Gia' visitato? */
    if(discoveryIsVisited(graph, entry->chassisId))
        return false;

    /* Gia' in coda? Scan lineare della coda circolare */
    for(size_t i = graph->queueHead; i != graph->queueTail;
        i = (i + 1) % MAX_DISCOVERY_QUEUE) {
        if(strcmp(graph->queue[i].chassisId, entry->chassisId) == 0)
            return false;
    }

    /* Coda piena? */
    size_t nextTail = (graph->queueTail + 1) % MAX_DISCOVERY_QUEUE;
    if(nextTail == graph->queueHead)
        return false;

    graph->queue[graph->queueTail] = *entry;
    graph->queueTail = nextTail;
    return true;
}

/* ============================================================
 * discoveryDequeue
 * ============================================================ */

bool discoveryDequeue(TopologyGraph *graph,
                       DiscoveryQueueEntry *out) {
    if(graph->queueHead == graph->queueTail)
        return false;  /* coda vuota */

    *out = graph->queue[graph->queueHead];
    graph->queueHead = (graph->queueHead + 1) % MAX_DISCOVERY_QUEUE;
    return true;
}

/* ============================================================
 * discoveryIsVisited
 *
 * Un chassisId e' visitato se esiste come nodo con
 * visited == true.
 * ============================================================ */

bool discoveryIsVisited(const TopologyGraph *graph,
                         const char *chassisId) {
    int idx = topologyFindNodeByChassisId(graph, chassisId);
    if(idx < 0)
        return false;
    return graph->nodes[idx].visited;
}

/* ============================================================
 * topologyGraphPrint
 *
 * Stampa di debug del grafo completo.
 * ============================================================ */

void topologyGraphPrint(const TopologyGraph *graph) {
    printf("\n");
    printf("========================================\n");
    printf("  Topology Graph\n");
    printf("  Nodes: %zu   Links: %zu\n", graph->nodesCount, graph->linksCount);
    if(graph->lastScanTime > 0) {
        char timeBuf[64];
        struct tm *tm = localtime(&graph->lastScanTime);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
        printf("  Last scan: %s\n", timeBuf);
    }
    printf("========================================\n");

    /* ─── Nodi ────────────────────────────────────────────── */
    for(size_t i = 0; i < graph->nodesCount; i++) {
        const TopologyNode *n = &graph->nodes[i];

        const char *typeStr = "UNKNOWN";
        switch(n->type) {
            case NODE_TYPE_UAFX_SERVER:    typeStr = "UAFX_SERVER";    break;
            case NODE_TYPE_SWITCH:         typeStr = "SWITCH";         break;
            case NODE_TYPE_PHANTOM_SWITCH: typeStr = "PHANTOM_SWITCH"; break;
            default: break;
        }

        printf("\n  [Node %zu] %s (%s)\n", i, n->name, typeStr);
        printf("    ChassisId:    %s\n", n->id);
        printf("    MgmtAddress:  %s\n", n->mgmtAddress);
        printf("    Reachable:    %s\n", n->reachable ? "yes" : "no");

        if(n->type == NODE_TYPE_UAFX_SERVER) {
            printf("    Endpoint:     %s\n", n->endpointUrl);
            printf("    AppUri:       %s\n", n->applicationUri);
            printf("    AppName:      %s\n", n->applicationName);

            /* AutomationComponents */
            for(size_t ac = 0; ac < n->automationComponentsCount; ac++) {
                const AutomationComponent *a = &n->automationComponents[ac];
                printf("    +-- AC: %s\n", a->name);
                printf("    |   Conformance: %s\n", a->conformanceName);
                printf("    |   Assets: %zu  FEs: %zu\n",
                       a->assetsCount, a->functionalEntitiesCount);

                for(size_t fe = 0; fe < a->functionalEntitiesCount; fe++) {
                    const FunctionalEntity *f = &a->functionalEntities[fe];
                    printf("    |   +-- FE: %s\n", f->name);
                    printf("    |       OutputData: %zu vars  InputData: %zu vars\n",
                           f->outputDataCount, f->inputDataCount);
                }
            }
        }

        /* Interfacce di rete */
        for(size_t ni = 0; ni < n->interfacesCount; ni++) {
            const NetworkInterface *iface = &n->interfaces[ni];
            printf("    +-- IF: %s (MAC=%s, Speed=%u, %s)\n",
                   iface->name, iface->physAddress,
                   iface->speed, iface->operStatus);

            for(size_t nb = 0; nb < iface->neighborsCount; nb++) {
                const LldpNeighbor *nbr = &iface->neighbors[nb];
                printf("    |   +-- Neighbor: %s (ChassisId=%s)\n",
                       nbr->sysName, nbr->chassisId);
                printf("    |       MgmtAddr=%s  Port=%s (%s)\n",
                       nbr->mgmtAddress, nbr->portDescr, nbr->portId);
            }
        }
    }

    /* ─── Link ────────────────────────────────────────────── */
    if(graph->linksCount > 0) {
        printf("\n  --- Links ---\n");
        for(size_t i = 0; i < graph->linksCount; i++) {
            const TopologyLink *l = &graph->links[i];
            printf("  [Link %zu] %s:%s  <-->  %s:%s  %s\n",
                   i,
                   l->endpointA.chassisId, l->endpointA.portDescr,
                   l->endpointB.chassisId, l->endpointB.portDescr,
                   l->confirmedBidirectional ? "(confirmed)" : "(unidirectional)");
        }
    }

    printf("\n========================================\n\n");
}
