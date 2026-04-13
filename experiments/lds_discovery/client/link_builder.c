/* ============================================================
 * link_builder.c
 *
 * Implementazione della costruzione dei link fisici dal
 * modello topologico popolato.
 * ============================================================ */

#include "link_builder.h"
#include <stdio.h>
#include <string.h>

/* Helper: copia sicura con troncamento */
static void safeStr(char *dst, const char *src, size_t dstSize) {
    if(!dst || dstSize == 0) return;
    if(!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

void buildLinksFromNeighbors(TopologyGraph *graph) {
    if(!graph) return;

    printf("\n[LinkBuilder] Building topology links from LLDP neighbors...\n");

    size_t linksBefore = graph->linksCount;

    /* Itera ogni nodo del grafo */
    for(size_t n = 0; n < graph->nodesCount; n++) {
        TopologyNode *node = &graph->nodes[n];

        /* Per ogni interfaccia del nodo */
        for(size_t i = 0; i < node->interfacesCount; i++) {
            NetworkInterface *iface = &node->interfaces[i];

            /* PortId locale: preferisci il localData.portId (quello
             * annunciato via LLDP, matcha col neighbor dell'altro lato),
             * fallback sul nome dell'interfaccia */
            const char *localPortId =
                iface->localData.portId[0] ? iface->localData.portId
                                           : iface->name;
            const char *localPortDescr =
                iface->name[0] ? iface->name : localPortId;

            /* Per ogni vicino visto su questa interfaccia */
            for(size_t k = 0; k < iface->neighborsCount; k++) {
                LldpNeighbor *nbr = &iface->neighbors[k];

                if(nbr->chassisId[0] == '\0' || nbr->portId[0] == '\0')
                    continue;  /* dati incompleti, salta */

                /* Costruisci il link */
                TopologyLink link;
                memset(&link, 0, sizeof(link));

                /* Endpoint A: il nodo corrente */
                safeStr(link.endpointA.chassisId, node->id, MAX_STR_LEN);
		safeStr(link.endpointA.sysName,   node->name, MAX_STR_LEN);
                safeStr(link.endpointA.portId,    localPortId, MAX_STR_LEN);
                safeStr(link.endpointA.portDescr, localPortDescr, MAX_STR_LEN);
                link.endpointA.nodeIndex = (int)n;

                /* Endpoint B: il vicino */
                safeStr(link.endpointB.chassisId, nbr->chassisId, MAX_STR_LEN);
		safeStr(link.endpointB.sysName,   nbr->sysName, MAX_STR_LEN);
                safeStr(link.endpointB.portId,    nbr->portId, MAX_STR_LEN);
                safeStr(link.endpointB.portDescr,
                        nbr->portDescr[0] ? nbr->portDescr : nbr->portId,
                        MAX_STR_LEN);
                /* Cerca il nodo B nel grafo per ottenere il suo index */
                link.endpointB.nodeIndex =
                    topologyFindNodeByChassisId(graph, nbr->chassisId);

                link.confirmedBidirectional = false;

                /* topologyAddLink gestisce automaticamente la
                 * deduplicazione e il match bidirezionale */
                int idx = topologyAddLink(graph, &link);
                if(idx < 0) {
                    printf("  [!] TopologyGraph.links pieno, link non aggiunto\n");
                    continue;
                }

                printf("  + %s:%s <-> %s:%s%s\n",
                       node->name[0] ? node->name : node->id,
                       localPortDescr,
                       nbr->sysName[0] ? nbr->sysName : nbr->chassisId,
                       link.endpointB.portDescr,
                       graph->links[idx].confirmedBidirectional
                           ? " (confirmed bidirectional)" : "");
            }
        }
    }

    size_t added = graph->linksCount - linksBefore;
    printf("[LinkBuilder] Done: %zu links in graph (%zu new)\n\n",
           graph->linksCount, added);
}
