/* ============================================================
 * connection_manager.h
 *
 * ConnectionManager lato backend — orchestra la creazione
 * di connessioni PubSub tra due server UAFX chiamando il
 * metodo EstablishConnections su ciascun AutomationComponent.
 *
 * Conforme a OPC UA FX Part 81 — ruolo di engineering tool
 * che agisce come ConnectionManager esterno (§5.6).
 * ============================================================ */

#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "model.h"
#include <stdbool.h>

/* ── Risultato di una singola operazione EstablishConnections ── */
typedef struct {
    bool      ok;
    char      errorMessage[256];
    UA_NodeId connectionEndpointId;   
    UA_NodeId dataSetWriterNodeId;    
} EstablishResult;
/* ── Descrizione di una connessione richiesta dal frontend ───── */
typedef struct {
    /* Lato Publisher */
    char   publisherChassisId[64];
    UA_NodeId publisherAcNodeId;
    char   publisherAcName[128];
    UA_NodeId publisherFeNodeId;
    char   publisherFeName[128];
    UA_NodeId publisherVariableNodeId;
    char   publisherVariable[128];

    /* Lato Subscriber */
    char   subscriberChassisId[64];
    UA_NodeId subscriberAcNodeId;
    char   subscriberAcName[128];
    UA_NodeId subscriberFeNodeId;
    char   subscriberFeName[128];
    UA_NodeId subscriberVariableNodeId;
    char   subscriberVariable[128];

    /* Parametri PubSub */
    double publishingInterval;    /* ms */
    char   qosCategory[32];       /* "PRIORITY" o "BEST_EFFORT" */
} ConnectionRequest;

/* ── Funzione principale ─────────────────────────────────────── */

/*
 * establishConnection
 *
 * Riceve la richiesta dal frontend, genera gli ID PubSub,
 * chiama EstablishConnections sul Publisher e sul Subscriber,
 * e aggiorna il TopologyGraph con la connessione logica creata.
 *
 * Restituisce true se entrambe le chiamate hanno avuto successo.
 */
bool establishConnection(TopologyGraph *graph,
                         const ConnectionRequest *req, PubSubConnection *connOut);

#endif /* CONNECTION_MANAGER_H */