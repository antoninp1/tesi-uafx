/* ============================================================
 * establish_connection.h
 *
 * Modulo unificato per il metodo OPC UA EstablishConnections
 * lato server, conforme alla firma UAFX standard (Part 81 §6.2.4).
 *
 * Espone una sola funzione di registrazione del metodo.
 * Tutta la logica (decodifica payload UAFX, creazione CE,
 * setup PubSub, transizioni di stato) vive nel .c.
 * ============================================================ */

#ifndef ESTABLISH_CONNECTION_H
#define ESTABLISH_CONNECTION_H

#include <open62541/server.h>

/* URI dei namespace UAFX (per risoluzione runtime) */
#define FXAC_NS_URI    "http://opcfoundation.org/UA/FX/AC/"
#define FXDATA_NS_URI  "http://opcfoundation.org/UA/FX/Data/"

/* Registra il metodo EstablishConnections sull'AutomationComponent.
 * Da chiamare dentro buildUAFXAddressSpace dopo aver creato l'acNode. */
void registerEstablishConnectionsMethod(UA_Server *server, UA_NodeId acNode);

#endif
