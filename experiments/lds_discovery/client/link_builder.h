/* ============================================================
 * link_builder.h
 *
 * Costruisce i TopologyLink del grafo a partire dai vicini
 * LLDP scoperti durante la fase di browse/BFS.
 *
 * Va chiamato DOPO che tutti i nodi sono stati aggiunti al
 * grafo, cosi' il matching sui chassisId dei vicini puo'
 * trovare i nodi corrispondenti.
 * ============================================================ */

#ifndef UAFX_LINK_BUILDER_H
#define UAFX_LINK_BUILDER_H

#include "model.h"

/* Scorre tutti i nodi del grafo, per ogni interfaccia di
 * ogni nodo estrae i vicini LLDP e costruisce i TopologyLink
 * corrispondenti. Il match bidirezionale e' gestito
 * automaticamente da topologyAddLink(). */
void buildLinksFromNeighbors(TopologyGraph *graph);

#endif /* UAFX_LINK_BUILDER_H */
