 /* ============================================================
 * discovery_client.c
 *
 * Client OPC UA FX con Discovery via LDS (TCP)
 *
 * Fase 1: si connette all'LDS via TCP e chiama FindServers()
 *         per ottenere la lista dei server registrati.
 * Fase 2: per ogni server scoperto, si connette e naviga
 *         l'Address Space UAFX.
 *
 * Compilazione:
 *   gcc -o discovery_client discovery_client.c open62541.c -pthread
 * ============================================================ */

#include "open62541.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

/* ============================================================
 * Costanti e strutture
 * ============================================================ */

#define MAX_DISCOVERED_SERVERS 16
#define MAX_STR_LEN            256
#define LDS_URL                "opc.tcp://192.168.17.75:4840"

typedef struct {
    char   urls[MAX_DISCOVERED_SERVERS][MAX_STR_LEN];
    char   names[MAX_DISCOVERED_SERVERS][MAX_STR_LEN];
    size_t count;
} DiscoveryList;

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    (void)sig;
    running = false;
}

/* ============================================================
 * Helper: stampa separatore
 * ============================================================ */

static void printSeparator(const char *title) {
    printf("\n");
    for(int i = 0; i < 56; i++) printf("=");
    printf("\n");
    if(title) {
        printf("  %s\n", title);
        for(int i = 0; i < 56; i++) printf("=");
        printf("\n");
    }
}


/* ============================================================
 * Helper: legge una proprietà stringa da un nodo
 *
 * Naviga i figli di `parentNode`, cerca quello con BrowseName
 * == `propertyName`, legge il valore come UA_String.
 * Restituisce una stringa allocata con malloc (o NULL).
 * ============================================================ */

static char *readStringProperty(UA_Client *client, UA_NodeId parentNode,
                                const char *propertyName) {
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = parentNode;
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResponse bResp = UA_Client_Service_browse(client, bReq);
    char *result = NULL;

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize && !result; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];
                UA_String target = UA_STRING((char *)propertyName);
                if(!UA_String_equal(&ref->browseName.name, &target))
                    continue;

                UA_Variant value;
                UA_Variant_init(&value);
                UA_StatusCode rc = UA_Client_readValueAttribute(
                    client, ref->nodeId.nodeId, &value);

                if(rc == UA_STATUSCODE_GOOD &&
                   UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_STRING])) {
                    UA_String *str = (UA_String *)value.data;
                    result = (char *)malloc(str->length + 1);
                    if(result) {
                        memcpy(result, str->data, str->length);
                        result[str->length] = '\0';
                    }
                }
                UA_Variant_clear(&value);
                break;
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
    return result;
}

/* ============================================================
 * Helper: legge una proprietà UInt32 da un nodo
 * ============================================================ */

static UA_Boolean readUInt32Property(UA_Client *client, UA_NodeId parentNode,
                                     const char *propertyName, UA_UInt32 *out) {
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = parentNode;
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResponse bResp = UA_Client_Service_browse(client, bReq);
    UA_Boolean found = UA_FALSE;

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize && !found; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];
                UA_String target = UA_STRING((char *)propertyName);
                if(!UA_String_equal(&ref->browseName.name, &target))
                    continue;

                UA_Variant value;
                UA_Variant_init(&value);
                UA_StatusCode rc = UA_Client_readValueAttribute(
                    client, ref->nodeId.nodeId, &value);

                if(rc == UA_STATUSCODE_GOOD &&
                   UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
                    *out = *(UA_UInt32 *)value.data;
                    found = UA_TRUE;
                }
                UA_Variant_clear(&value);
                break;
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
    return found;
}

/* ============================================================
 * Browse generico dei figli di un nodo (helper)
 *
 * Esegue un browse forward e restituisce la BrowseResponse.
 * Il chiamante DEVE fare UA_BrowseResponse_clear() e
 * UA_BrowseRequest_clear() dopo l'uso.
 * ============================================================ */

static UA_BrowseResponse browseNode(UA_Client *client, UA_NodeId nodeId,
                                     UA_BrowseRequest *bReq) {
    UA_BrowseRequest_init(bReq);
    bReq->requestedMaxReferencesPerNode = 0;
    bReq->nodesToBrowse = UA_BrowseDescription_new();
    bReq->nodesToBrowseSize = 1;
    bReq->nodesToBrowse[0].nodeId = nodeId;
    bReq->nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq->nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;

    return UA_Client_Service_browse(client, *bReq);
}

/* ============================================================
 * Browse una cartella di dati (OutputData / InputData)
 *
 * Legge il valore di ogni variabile figlio e lo stampa.
 * ============================================================ */

static void browseDataFolder(UA_Client *client, UA_NodeId folderNodeId,
                             const char *folderName, const char *indent) {
    printf("%s|\n", indent);
    printf("%s+-- %s/\n", indent, folderName);

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, folderNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                char varName[MAX_STR_LEN];
                snprintf(varName, sizeof(varName), "%.*s",
                         (int)ref->browseName.name.length,
                         ref->browseName.name.data);

                UA_Variant value;
                UA_Variant_init(&value);
                UA_StatusCode rc = UA_Client_readValueAttribute(
                    client, ref->nodeId.nodeId, &value);

                printf("%s    +-- %s", indent, varName);

                if(rc == UA_STATUSCODE_GOOD) {
                    if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
                        UA_Float fv = *(UA_Float *)value.data;
                        printf(": %.2f", fv);
                        /* Tenta di leggere EngineeringUnits */
                        char *units = readStringProperty(client,
                                          ref->nodeId.nodeId, "EngineeringUnits");
                        if(units) { printf(" %s", units); free(units); }
                        printf(" [Float]");
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
                        printf(": %.4f [Double]", *(UA_Double *)value.data);
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
                        printf(": %d [Int32]", *(UA_Int32 *)value.data);
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
                        printf(": %u [UInt32]", *(UA_UInt32 *)value.data);
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
                        printf(": %s [Boolean]",
                               *(UA_Boolean *)value.data ? "true" : "false");
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_STRING])) {
                        UA_String *s = (UA_String *)value.data;
                        printf(": %.*s [String]", (int)s->length, s->data);
                    } else {
                        printf(" [tipo non gestito]");
                    }
                } else {
                    printf(" (lettura fallita: %s)", UA_StatusCode_name(rc));
                }
                printf("\n");
                UA_Variant_clear(&value);
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * Browse FunctionalEntity
 *
 * Legge le proprietà della FE e naviga OutputData / InputData.
 * ============================================================ */

static void browseFunctionalEntity(UA_Client *client, UA_NodeId feNodeId,
                                   const char *feName) {
    printf("\n      +-- FunctionalEntity: %s\n", feName);

    /* Proprietà della FE */
    char *authorUri  = readStringProperty(client, feNodeId, "AuthorUri");
    char *identifier = readStringProperty(client, feNodeId, "AuthorAssignedIdentifier");
    char *version    = readStringProperty(client, feNodeId, "AuthorAssignedVersion");

    if(authorUri)  { printf("      |   AuthorUri:                %s\n", authorUri);  free(authorUri); }
    if(identifier) { printf("      |   AuthorAssignedIdentifier: %s\n", identifier); free(identifier); }
    if(version)    { printf("      |   AuthorAssignedVersion:    %s\n", version);    free(version); }

    UA_UInt32 opHealth;
    if(readUInt32Property(client, feNodeId, "OperationalHealth", &opHealth))
        printf("      |   OperationalHealth:        %u\n", opHealth);

    /* Naviga i figli della FE */
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, feNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                /* OutputData */
                UA_String outputStr = UA_STRING("OutputData");
                if(UA_String_equal(&ref->browseName.name, &outputStr)) {
                    browseDataFolder(client, ref->nodeId.nodeId,
                                     "OutputData", "      ");
                }

                /* InputData */
                UA_String inputStr = UA_STRING("InputData");
                if(UA_String_equal(&ref->browseName.name, &inputStr)) {
                    browseDataFolder(client, ref->nodeId.nodeId,
                                     "InputData", "      ");
                }

                /* ConnectionEndpoints */
                UA_String ceStr = UA_STRING("ConnectionEndpoints");
                if(UA_String_equal(&ref->browseName.name, &ceStr)) {
                    printf("      |\n");
                    printf("      +-- ConnectionEndpoints/\n");

                    UA_BrowseRequest bReq2;
                    UA_BrowseResponse bResp2 = browseNode(client,
                                                    ref->nodeId.nodeId, &bReq2);
                    if(bResp2.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                        for(size_t k = 0; k < bResp2.resultsSize; k++) {
                            for(size_t l = 0; l < bResp2.results[k].referencesSize; l++) {
                                UA_ReferenceDescription *ce =
                                    &bResp2.results[k].references[l];
                                printf("          +-- %.*s\n",
                                       (int)ce->browseName.name.length,
                                       ce->browseName.name.data);
                            }
                        }
                    }
                    if(bResp2.resultsSize == 0 ||
                       (bResp2.resultsSize > 0 &&
                        bResp2.results[0].referencesSize == 0))
                        printf("          (vuoto)\n");

                    UA_BrowseResponse_clear(&bResp2);
                    UA_BrowseRequest_clear(&bReq2);
                }
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * Browse Assets/
 *
 * Per ogni asset figlio legge le proprietà DI standard:
 * Manufacturer, Model, SerialNumber, HardwareRevision, ecc.
 * ============================================================ */

static void browseAssets(UA_Client *client, UA_NodeId assetsFolderNodeId) {
    printf("    |\n");
    printf("    +-- Assets/\n");

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, assetsFolderNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ar = &bResp.results[i].references[j];

                char assetName[MAX_STR_LEN];
                snprintf(assetName, sizeof(assetName), "%.*s",
                         (int)ar->browseName.name.length,
                         ar->browseName.name.data);

                printf("    |   +-- %s\n", assetName);

                /* Proprietà DI dell'asset */
                const char *assetProps[] = {
                    "Manufacturer", "ManufacturerUri", "Model",
                    "ProductCode", "HardwareRevision", "SoftwareRevision",
                    "DeviceClass", "SerialNumber"
                };
                size_t numProps = sizeof(assetProps) / sizeof(assetProps[0]);

                for(size_t p = 0; p < numProps; p++) {
                    char *val = readStringProperty(client,
                                    ar->nodeId.nodeId, assetProps[p]);
                    if(val) {
                        printf("    |       %-18s %s\n", assetProps[p], val);
                        free(val);
                    }
                }
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * Browse ComponentCapabilities/
 * ============================================================ */

static void browseComponentCapabilities(UA_Client *client,
                                        UA_NodeId capFolderNodeId) {
    printf("    |\n");
    printf("    +-- ComponentCapabilities/\n");

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, capFolderNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                char name[MAX_STR_LEN];
                snprintf(name, sizeof(name), "%.*s",
                         (int)ref->browseName.name.length,
                         ref->browseName.name.data);

                UA_Variant value;
                UA_Variant_init(&value);
                UA_StatusCode rc = UA_Client_readValueAttribute(
                    client, ref->nodeId.nodeId, &value);

                printf("        +-- %s", name);
                if(rc == UA_STATUSCODE_GOOD &&
                   UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
                    printf(": %u", *(UA_UInt32 *)value.data);
                }
                printf("\n");
                UA_Variant_clear(&value);
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * Browse AutomationComponent
 *
 * Legge le proprietà dell'AC e delega ai sotto-browser:
 *   Assets/ → browseAssets()
 *   FunctionalEntities/ → browseFunctionalEntity()
 *   ComponentCapabilities/ → browseComponentCapabilities()
 * ============================================================ */

static void browseAutomationComponent(UA_Client *client,
                                      UA_NodeId acNodeId,
                                      const char *acName) {
    printf("\n    +-- AutomationComponent: %s\n", acName);

    /* Proprietà dirette dell'AC */
    char *conformance = readStringProperty(client, acNodeId, "ConformanceName");
    if(conformance) {
        printf("    |   ConformanceName: %s\n", conformance);
        free(conformance);
    }

    UA_UInt32 aggHealth;
    if(readUInt32Property(client, acNodeId, "AggregatedHealth", &aggHealth))
        printf("    |   AggregatedHealth: %u\n", aggHealth);

    /* Naviga i figli */
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, acNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String assetsStr = UA_STRING("Assets");
                UA_String feStr     = UA_STRING("FunctionalEntities");
                UA_String capStr    = UA_STRING("ComponentCapabilities");

                if(UA_String_equal(&ref->browseName.name, &assetsStr)) {
                    browseAssets(client, ref->nodeId.nodeId);
                }
                else if(UA_String_equal(&ref->browseName.name, &feStr)) {
                    printf("    |\n");
                    printf("    +-- FunctionalEntities/\n");

                    UA_BrowseRequest bReq2;
                    UA_BrowseResponse bResp2 = browseNode(client,
                                                    ref->nodeId.nodeId, &bReq2);
                    if(bResp2.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                        for(size_t k = 0; k < bResp2.resultsSize; k++) {
                            for(size_t l = 0; l < bResp2.results[k].referencesSize; l++) {
                                UA_ReferenceDescription *fr =
                                    &bResp2.results[k].references[l];

                                char feName[MAX_STR_LEN];
                                snprintf(feName, sizeof(feName), "%.*s",
                                         (int)fr->browseName.name.length,
                                         fr->browseName.name.data);
                                browseFunctionalEntity(client,
                                    fr->nodeId.nodeId, feName);
                            }
                        }
                    }
                    UA_BrowseResponse_clear(&bResp2);
                    UA_BrowseRequest_clear(&bReq2);
                }
                else if(UA_String_equal(&ref->browseName.name, &capStr)) {
                    browseComponentCapabilities(client, ref->nodeId.nodeId);
                }
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * Browse struttura UAFX di un singolo server
 *
 * Percorso: Objects → FxRoot → AutomationComponent(s)
 * ============================================================ */

static void browseServerUAFX(UA_Client *client, const char *endpoint) {
    printf("\n  Endpoint: %s\n", endpoint);

    /* FindServers per info applicazione */
    size_t serverArraySize = 0;
    UA_ApplicationDescription *serverArray = NULL;
    UA_StatusCode retval = UA_Client_findServers(
        client, endpoint, 0, NULL, 0, NULL,
        &serverArraySize, &serverArray);

    if(retval == UA_STATUSCODE_GOOD && serverArraySize > 0) {
        printf("  ApplicationUri:  %.*s\n",
               (int)serverArray[0].applicationUri.length,
               serverArray[0].applicationUri.data);
        printf("  ApplicationName: %.*s\n",
               (int)serverArray[0].applicationName.text.length,
               serverArray[0].applicationName.text.data);
        UA_Array_delete(serverArray, serverArraySize,
                        &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);
    }

    printf("\n  UAFX Address Space:\n");
    printf("  -------------------\n");
    printf("  Objects/\n");

    /* Browse Objects folder cercando FxRoot */
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, objectsFolder, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String fxRootStr = UA_STRING("FxRoot");
                if(!UA_String_equal(&ref->browseName.name, &fxRootStr))
                    continue;

                printf("  +-- FxRoot/\n");

                /* Dentro FxRoot: cerca gli AutomationComponent */
                UA_BrowseRequest bReq2;
                UA_BrowseResponse bResp2 = browseNode(client,
                                                ref->nodeId.nodeId, &bReq2);

                if(bResp2.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                    for(size_t k = 0; k < bResp2.resultsSize; k++) {
                        for(size_t l = 0; l < bResp2.results[k].referencesSize; l++) {
                            UA_ReferenceDescription *acRef =
                                &bResp2.results[k].references[l];

                            char acName[MAX_STR_LEN];
                            snprintf(acName, sizeof(acName), "%.*s",
                                     (int)acRef->browseName.name.length,
                                     acRef->browseName.name.data);

                            browseAutomationComponent(client,
                                acRef->nodeId.nodeId, acName);
                        }
                    }
                }
                UA_BrowseResponse_clear(&bResp2);
                UA_BrowseRequest_clear(&bReq2);
            }
        }
    } else {
        printf("  (browse Objects fallito: %s)\n",
               UA_StatusCode_name(bResp.responseHeader.serviceResult));
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * Fase 1: Discovery via LDS (TCP)
 *
 * Chiama FindServers() sull'LDS. Filtra via l'LDS stesso
 * (applicationType == DiscoveryServer) e raccoglie solo
 * i server applicativi.
 * ============================================================ */

static UA_StatusCode
runLdsDiscovery(DiscoveryList *list, const char *ldsUrl) {

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    printf("  Connessione all'LDS: %s\n", ldsUrl);

    size_t serverArraySize = 0;
    UA_ApplicationDescription *serverArray = NULL;

    UA_StatusCode retval = UA_Client_findServers(
        client, ldsUrl,
        0, NULL,    /* serverUriFilter */
        0, NULL,    /* localeIdFilter  */
        &serverArraySize, &serverArray);

    if(retval != UA_STATUSCODE_GOOD) {
        printf("  FindServers fallito: %s\n", UA_StatusCode_name(retval));
        UA_Client_delete(client);
        return retval;
    }

    printf("  FindServers ha restituito %zu risultati\n\n", serverArraySize);

    for(size_t i = 0; i < serverArraySize && list->count < MAX_DISCOVERED_SERVERS; i++) {
        UA_ApplicationDescription *app = &serverArray[i];

        /* Salta l'LDS stesso */
        if(app->applicationType == UA_APPLICATIONTYPE_DISCOVERYSERVER) {
            printf("  [skip] %.*s (DiscoveryServer)\n",
                   (int)app->applicationName.text.length,
                   app->applicationName.text.data);
            continue;
        }

        /* Prendi il primo discoveryUrl disponibile */
        if(app->discoveryUrlsSize == 0)
            continue;

        UA_String *url = &app->discoveryUrls[0];
        if(url->length == 0 || url->length >= MAX_STR_LEN)
            continue;

        /* Salva URL */
        memcpy(list->urls[list->count], url->data, url->length);
        list->urls[list->count][url->length] = '\0';

        /* Salva nome */
        size_t nameLen = app->applicationName.text.length;
        if(nameLen >= MAX_STR_LEN) nameLen = MAX_STR_LEN - 1;
        memcpy(list->names[list->count], app->applicationName.text.data, nameLen);
        list->names[list->count][nameLen] = '\0';

        printf("  [+] Scoperto: %-30s  -->  %s\n",
               list->names[list->count],
               list->urls[list->count]);

        list->count++;
    }

    UA_Array_delete(serverArray, serverArraySize,
                    &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);
    UA_Client_delete(client);
    return UA_STATUSCODE_GOOD;
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(int argc, char **argv) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);

    printf("\n");
    printSeparator("OPC UA FX Discovery Client - LDS TCP");
    printf("\n");

    const char *ldsUrl = LDS_URL;
    if(argc >= 2)
        ldsUrl = argv[1];

    /* ═══════════════════════════════════════════════════════════
     * FASE 1: Discovery via LDS
     * ═══════════════════════════════════════════════════════════ */

    printf("[FASE 1] LDS Discovery via TCP\n");
    for(int i = 0; i < 56; i++) printf("-");
    printf("\n");

    DiscoveryList discovered;
    memset(&discovered, 0, sizeof(discovered));

    UA_StatusCode retval = runLdsDiscovery(&discovered, ldsUrl);

    if(retval != UA_STATUSCODE_GOOD || discovered.count == 0) {
        printf("\n  Nessun server scoperto tramite LDS.\n");
        printf("  Verifica che l'LDS sia in esecuzione e che i server siano registrati.\n\n");
        return EXIT_FAILURE;
    }

    printf("\n  Trovati %zu server tramite LDS.\n\n", discovered.count);

    /* ═══════════════════════════════════════════════════════════
     * FASE 2: Connessione e Browse UAFX (identica all'originale)
     * ═══════════════════════════════════════════════════════════ */

    printSeparator("FASE 2: Connessione e Browse UAFX");

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    for(size_t i = 0; i < discovered.count; i++) {
        printf("\n[Server %zu/%zu] %s\n",
               i + 1, discovered.count, discovered.names[i]);

        retval = UA_Client_connect(client, discovered.urls[i]);

        if(retval != UA_STATUSCODE_GOOD) {
            printf("  Connessione fallita: %s\n",
                   UA_StatusCode_name(retval));
            UA_Client_disconnect(client);
            continue;
        }

        printf("  Connesso.\n");
        browseServerUAFX(client, discovered.urls[i]);
        UA_Client_disconnect(client);
        printf("  Disconnesso.\n");
    }

    UA_Client_delete(client);

    printSeparator("Discovery completato");
    printf("\n");

    return EXIT_SUCCESS;
}
