/* ============================================================
 * browse.h
 *
 * Funzioni di browse dell'Address Space UAFX.
 * Popolano le struct definite in model.h e (opzionalmente)
 * stampano l'albero su stdout.
 * ============================================================ */

#ifndef UAFX_BROWSE_H
#define UAFX_BROWSE_H

#include "common.h"
#include "model.h"

/* Browse completo della struttura UAFX di un server.
 * Popola un TopologyNode (NODE_TYPE_UAFX_SERVER):
 *   - applicationUri, applicationName
 *   - automationComponents[] (FxRoot)
 *   - interfaces[] (NetworkInterfaces)
 *   - id (chassisId estratto da LldpData/LocalSystemData) */
void browseServerUAFX(UA_Client *client, const char *endpoint,
                      TopologyNode *node);

/* Browse di un AutomationComponent. Popola la struct AC. */
void browseAutomationComponent(UA_Client *client, UA_NodeId acNodeId,
                               const char *acName,
                               AutomationComponent *ac);

/* Browse della cartella Assets/. Popola ac->assets[]. */
void browseAssets(UA_Client *client, UA_NodeId assetsFolderNodeId,
                  AutomationComponent *ac);

/* Browse di una singola FunctionalEntity. Popola la struct FE. */
void browseFunctionalEntity(UA_Client *client, UA_NodeId feNodeId,
                            const char *feName,
                            FunctionalEntity *fe);

/* Browse di una cartella dati (OutputData / InputData).
 * Popola un array di DataVariable, restituendo il count via *countOut. */
void browseDataFolder(UA_Client *client, UA_NodeId folderNodeId,
                      const char *folderName, const char *indent,
                      DataVariable *vars, size_t maxVars, size_t *countOut);

/* Browse della cartella ComponentCapabilities/. Popola ac->capabilities. */
void browseComponentCapabilities(UA_Client *client,
                                 UA_NodeId capFolderNodeId,
                                 ComponentCapabilities *caps);

/* Browse NetworkInterfaces/ (Part 82, 6.5.2).
 * Popola node->interfaces[] e setta node->id dal LocalSystemData. */
void browseNetworkInterfaces(UA_Client *client, UA_NodeId niFolderNodeId,
                             TopologyNode *node);

/* Browse di una singola interfaccia di rete.
 * Popola la struct NetworkInterface. */
void browseNetworkInterface(UA_Client *client, UA_NodeId ifNodeId,
                            const char *ifName,
                            NetworkInterface *iface);

/* Browse LldpData/ di un'interfaccia.
 * Popola iface->localData e iface->neighbors[]. */
void browseLldpData(UA_Client *client, UA_NodeId lldpFolderNodeId,
                    const char *indent,
                    NetworkInterface *iface);

/* Browse di un singolo RemoteSystem. Popola la struct LldpNeighbor. */
void browseLldpRemoteSystem(UA_Client *client, UA_NodeId rsNodeId,
                            const char *rsName, const char *indent,
                            LldpNeighbor *neighbor);

#endif /* UAFX_BROWSE_H */
