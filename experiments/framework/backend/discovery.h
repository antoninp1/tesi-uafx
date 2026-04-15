/* ============================================================
 * discovery.h
 *
 * Discovery via LDS (TCP): FindServers e raccolta endpoint.
 * ============================================================ */

#ifndef UAFX_DISCOVERY_H
#define UAFX_DISCOVERY_H

#include "common.h"

#define LDS_URL "opc.tcp://192.168.17.73:4840"

typedef struct {
    char   urls[MAX_DISCOVERED_SERVERS][MAX_STR_LEN];
    char   names[MAX_DISCOVERED_SERVERS][MAX_STR_LEN];
    size_t count;
} DiscoveryList;

/* Esegue FindServers() sull'LDS e popola la lista.
 * Filtra automaticamente i DiscoveryServer. */
UA_StatusCode runLdsDiscovery(DiscoveryList *list, const char *ldsUrl);

#endif /* UAFX_DISCOVERY_H */
