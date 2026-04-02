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

#endif /* UAFX_BROWSE_H */
