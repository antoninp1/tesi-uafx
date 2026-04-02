/* ============================================================
 * helpers.h
 *
 * Funzioni helper per browse e lettura proprietà OPC UA.
 * ============================================================ */

#ifndef UAFX_HELPERS_H
#define UAFX_HELPERS_H

#include "common.h"

/* Stampa separatore visuale con titolo opzionale */
void printSeparator(const char *title);

/* Legge una proprietà stringa da un nodo figlio con BrowseName == propertyName.
 * Restituisce stringa allocata con malloc (caller deve fare free) o NULL. */
char *readStringProperty(UA_Client *client, UA_NodeId parentNode,
                         const char *propertyName);

/* Legge una proprietà UInt32 da un nodo figlio con BrowseName == propertyName.
 * Restituisce UA_TRUE se trovata, UA_FALSE altrimenti. */
UA_Boolean readUInt32Property(UA_Client *client, UA_NodeId parentNode,
                              const char *propertyName, UA_UInt32 *out);

/* Browse forward generico dei figli di un nodo.
 * Il chiamante DEVE fare UA_BrowseResponse_clear() e UA_BrowseRequest_clear(). */
UA_BrowseResponse browseNode(UA_Client *client, UA_NodeId nodeId,
                              UA_BrowseRequest *bReq);

#endif /* UAFX_HELPERS_H */
