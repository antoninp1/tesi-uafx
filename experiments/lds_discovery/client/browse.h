/* ============================================================
 * browse.h
 *
 * Funzioni di browse dell'Address Space UAFX:
 * FxRoot → AutomationComponent → Assets, FunctionalEntities,
 * ComponentCapabilities, OutputData, InputData.
 * ============================================================ */

#ifndef UAFX_BROWSE_H
#define UAFX_BROWSE_H

#include "common.h"

/* Browse completo della struttura UAFX di un server.
 * Percorso: Objects → FxRoot → AutomationComponent(s) */
void browseServerUAFX(UA_Client *client, const char *endpoint);

/* Browse di un AutomationComponent e dei suoi sotto-nodi */
void browseAutomationComponent(UA_Client *client, UA_NodeId acNodeId,
                               const char *acName);

/* Browse della cartella Assets/ di un AC */
void browseAssets(UA_Client *client, UA_NodeId assetsFolderNodeId);

/* Browse di una singola FunctionalEntity */
void browseFunctionalEntity(UA_Client *client, UA_NodeId feNodeId,
                            const char *feName);

/* Browse di una cartella dati (OutputData / InputData) */
void browseDataFolder(UA_Client *client, UA_NodeId folderNodeId,
                      const char *folderName, const char *indent);

/* Browse della cartella ComponentCapabilities/ */
void browseComponentCapabilities(UA_Client *client, UA_NodeId capFolderNodeId);

/* Browse NetworkInterfaces/ sotto Objects (Part 82, 6.5.2)
 * Naviga interfacce fisiche e relativi dati LLDP */
void browseNetworkInterfaces(UA_Client *client, UA_NodeId niFolderNodeId);

/* Browse di una singola interfaccia di rete (es. enp0s31f6) */
void browseNetworkInterface(UA_Client *client, UA_NodeId ifNodeId,
                            const char *ifName);

/* Browse LldpData/ di un'interfaccia: LocalSystemData + RemoteSystemsData */
void browseLldpData(UA_Client *client, UA_NodeId lldpFolderNodeId,
                    const char *indent);

/* Browse di un singolo RemoteSystem (vicino LLDP) */
void browseLldpRemoteSystem(UA_Client *client, UA_NodeId rsNodeId,
                            const char *rsName, const char *indent);

#endif /* UAFX_BROWSE_H */
