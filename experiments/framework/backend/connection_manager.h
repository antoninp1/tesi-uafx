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
    UA_NodeId dataSetReaderNodeId;    
  
} EstablishResult;
/* ── Descrizione di una connessione richiesta dal frontend ───── */
typedef struct {
    /* Lato Publisher */
    char   publisherChassisId[64];
    char publisherAcNodeId[64];
    char   publisherAcName[128];
    char publisherFeNodeId[64];
    char   publisherFeName[128];
    char publisherVariableNodeId[64];
    char   publisherVariableName[128];
    char  publisherEndpointUrl[256];

     /* Lato Subscriber */
    char   subscriberChassisId[64];
    char subscriberAcNodeId[64];
    char   subscriberAcName[128];
    char subscriberFeNodeId[64];
    char   subscriberFeName[128];
    char subscriberVariableNodeId[64];
    char   subscriberVariableName[128];
    char  subscriberEndpointUrl[256];
    /* Parametri PubSub */
    char address[256];              /* Multicast address o unicast URL */
    double publishingInterval;    /* ms */
    char   qosCategory[32];       /* "PRIORITY" o "BEST_EFFORT" */
    double messageReceiveTimeout; /* ms */
} ConnectionRequest;

/* ============================================================
 * ConnectionResponse
 * Serializzata in JSON e inviata al frontend
 * ============================================================ */
typedef struct {
    bool   success;
    char   errorMessage[256];
    PubSubConnection connection;  /* popolata solo se success=true */
} ConnectionResponse;
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