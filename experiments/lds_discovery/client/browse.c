/* ============================================================
 * browse.c
 *
 * Implementazione del browse dell'Address Space UAFX.
 * Include browse di:
 *   - FxRoot → AutomationComponent → Assets, FE, Capabilities
 *   - NetworkInterfaces → LldpData (Part 82, 6.5.2 / 7.3.2)
 * ============================================================ */

#include "browse.h"
#include "helpers.h"

/* ============================================================
 * browseDataFolder
 *
 * Legge il valore di ogni variabile figlio e lo stampa.
 * ============================================================ */

void browseDataFolder(UA_Client *client, UA_NodeId folderNodeId,
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
 * browseFunctionalEntity
 * ============================================================ */

void browseFunctionalEntity(UA_Client *client, UA_NodeId feNodeId,
                            const char *feName) {
    printf("\n      +-- FunctionalEntity: %s\n", feName);

    char *authorUri  = readStringProperty(client, feNodeId, "AuthorUri");
    char *identifier = readStringProperty(client, feNodeId, "AuthorAssignedIdentifier");
    char *version    = readStringProperty(client, feNodeId, "AuthorAssignedVersion");

    if(authorUri)  { printf("      |   AuthorUri:                %s\n", authorUri);  free(authorUri); }
    if(identifier) { printf("      |   AuthorAssignedIdentifier: %s\n", identifier); free(identifier); }
    if(version)    { printf("      |   AuthorAssignedVersion:    %s\n", version);    free(version); }

    UA_UInt32 opHealth;
    if(readUInt32Property(client, feNodeId, "OperationalHealth", &opHealth))
        printf("      |   OperationalHealth:        %u\n", opHealth);

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, feNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String outputStr = UA_STRING("OutputData");
                if(UA_String_equal(&ref->browseName.name, &outputStr)) {
                    browseDataFolder(client, ref->nodeId.nodeId,
                                     "OutputData", "      ");
                }

                UA_String inputStr = UA_STRING("InputData");
                if(UA_String_equal(&ref->browseName.name, &inputStr)) {
                    browseDataFolder(client, ref->nodeId.nodeId,
                                     "InputData", "      ");
                }

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
 * browseAssets
 * ============================================================ */

void browseAssets(UA_Client *client, UA_NodeId assetsFolderNodeId) {
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
 * browseComponentCapabilities
 * ============================================================ */

void browseComponentCapabilities(UA_Client *client,
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
 * browseAutomationComponent
 * ============================================================ */

void browseAutomationComponent(UA_Client *client,
                               UA_NodeId acNodeId,
                               const char *acName) {
    printf("\n    +-- AutomationComponent: %s\n", acName);

    char *conformance = readStringProperty(client, acNodeId, "ConformanceName");
    if(conformance) {
        printf("    |   ConformanceName: %s\n", conformance);
        free(conformance);
    }

    UA_UInt32 aggHealth;
    if(readUInt32Property(client, acNodeId, "AggregatedHealth", &aggHealth))
        printf("    |   AggregatedHealth: %u\n", aggHealth);

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
 * browseLldpRemoteSystem
 *
 * Legge i TLV LLDP di un singolo vicino (RemoteSystem_N).
 * Campi conformi a Part 82 §7.3.2.2.4:
 *   ChassisId, ChassisIdSubtype, SysName, SysDescr,
 *   MgmtAddress, PortId, PortIdSubtype, PortDescr,
 *   SystemCapabilities, TimeToLive
 * ============================================================ */

void browseLldpRemoteSystem(UA_Client *client, UA_NodeId rsNodeId,
                            const char *rsName, const char *indent) {
    printf("%s+-- %s\n", indent, rsName);

    /* Campi stringa dei TLV LLDP */
    const char *lldpStringFields[] = {
        "ChassisId",
        "SysName", "SysDescr",
        "MgmtAddress",
        "PortId", "PortDescr",
        "SystemCapabilities"
    };
    size_t numStringFields = sizeof(lldpStringFields) / sizeof(lldpStringFields[0]);

    for(size_t f = 0; f < numStringFields; f++) {
        char *val = readStringProperty(client, rsNodeId, lldpStringFields[f]);
        if(val) {
            printf("%s    %-22s %s\n", indent, lldpStringFields[f], val);
            free(val);
        }
    }

    /* Campi UInt32 */
    const char *lldpUint32Fields[] = {
        "ChassisIdSubtype", "PortIdSubtype", "TimeToLive"
    };
    size_t numUint32Fields = sizeof(lldpUint32Fields) / sizeof(lldpUint32Fields[0]);

    for(size_t f = 0; f < numUint32Fields; f++) {
        UA_UInt32 uval;
        if(readUInt32Property(client, rsNodeId, lldpUint32Fields[f], &uval))
            printf("%s    %-22s %u\n", indent, lldpUint32Fields[f], uval);
    }
}

/* ============================================================
 * browseLldpData
 *
 * Naviga LldpData/ di un'interfaccia di rete.
 * Struttura attesa:
 *   LldpData/
 *   +-- LocalSystemData/   (dati LLDP locali)
 *   +-- RemoteSystemsData/ (vicini LLDP)
 *       +-- RemoteSystem_1/
 *       +-- RemoteSystem_2/
 *       ...
 * ============================================================ */

void browseLldpData(UA_Client *client, UA_NodeId lldpFolderNodeId,
                    const char *indent) {
    printf("%s|\n", indent);
    printf("%s+-- LldpData/\n", indent);

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, lldpFolderNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                char name[MAX_STR_LEN];
                snprintf(name, sizeof(name), "%.*s",
                         (int)ref->browseName.name.length,
                         ref->browseName.name.data);

                char childIndent[64];
                snprintf(childIndent, sizeof(childIndent), "%s    ", indent);

                UA_String localStr  = UA_STRING("LocalSystemData");
                UA_String remoteStr = UA_STRING("RemoteSystemsData");

                if(UA_String_equal(&ref->browseName.name, &localStr)) {
                    /* ─── LocalSystemData ──────────────────── */
                    printf("%s    +-- LocalSystemData/\n", indent);

                    const char *localFields[] = {
                        "ChassisId", "SysName", "SysDescr",
                        "MgmtAddress", "SystemCapabilities", "PortId"
                    };
                    size_t numLocal = sizeof(localFields) / sizeof(localFields[0]);

                    for(size_t f = 0; f < numLocal; f++) {
                        char *val = readStringProperty(client,
                                        ref->nodeId.nodeId, localFields[f]);
                        if(val) {
                            printf("%s        %-22s %s\n", indent, localFields[f], val);
                            free(val);
                        }
                    }

                    UA_UInt32 subtype;
                    if(readUInt32Property(client, ref->nodeId.nodeId,
                                         "ChassisIdSubtype", &subtype))
                        printf("%s        %-22s %u\n", indent, "ChassisIdSubtype", subtype);
                    if(readUInt32Property(client, ref->nodeId.nodeId,
                                         "PortIdSubtype", &subtype))
                        printf("%s        %-22s %u\n", indent, "PortIdSubtype", subtype);

                } else if(UA_String_equal(&ref->browseName.name, &remoteStr)) {
                    /* ─── RemoteSystemsData ────────────────── */
                    printf("%s    +-- RemoteSystemsData/\n", indent);

                    UA_BrowseRequest bReq2;
                    UA_BrowseResponse bResp2 = browseNode(client,
                                                    ref->nodeId.nodeId, &bReq2);

                    if(bResp2.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                        for(size_t k = 0; k < bResp2.resultsSize; k++) {
                            for(size_t l = 0; l < bResp2.results[k].referencesSize; l++) {
                                UA_ReferenceDescription *rs =
                                    &bResp2.results[k].references[l];

                                char rsName[MAX_STR_LEN];
                                snprintf(rsName, sizeof(rsName), "%.*s",
                                         (int)rs->browseName.name.length,
                                         rs->browseName.name.data);

                                char rsIndent[64];
                                snprintf(rsIndent, sizeof(rsIndent), "%s        ", indent);
                                browseLldpRemoteSystem(client,
                                    rs->nodeId.nodeId, rsName, rsIndent);
                            }
                        }
                    }

                    if(bResp2.resultsSize == 0 ||
                       (bResp2.resultsSize > 0 &&
                        bResp2.results[0].referencesSize == 0))
                        printf("%s        (nessun vicino LLDP)\n", indent);

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
 * browseNetworkInterface
 *
 * Browse di una singola interfaccia di rete (es. enp0s31f6).
 * Legge le proprieta' IetfBaseNetworkInterface-like e
 * naviga LldpData/ se presente.
 * ============================================================ */

void browseNetworkInterface(UA_Client *client, UA_NodeId ifNodeId,
                            const char *ifName) {
    printf("    +-- %s\n", ifName);

    /* Proprieta' dell'interfaccia (Part 82, 6.5.3) */
    const char *ifStringProps[] = {
        "AdminStatus", "OperStatus", "PhysAddress"
    };
    size_t numIfProps = sizeof(ifStringProps) / sizeof(ifStringProps[0]);

    for(size_t p = 0; p < numIfProps; p++) {
        char *val = readStringProperty(client, ifNodeId, ifStringProps[p]);
        if(val) {
            printf("    |   %-18s %s\n", ifStringProps[p], val);
            free(val);
        }
    }

    UA_UInt32 speed;
    if(readUInt32Property(client, ifNodeId, "Speed", &speed))
        printf("    |   %-18s %u Mbps\n", "Speed", speed);

    /* Cerca LldpData/ tra i figli */
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, ifNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String lldpStr = UA_STRING("LldpData");
                if(UA_String_equal(&ref->browseName.name, &lldpStr)) {
                    browseLldpData(client, ref->nodeId.nodeId, "    ");
                }
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * browseNetworkInterfaces
 *
 * Browse della cartella NetworkInterfaces/ sotto Objects.
 * Per ogni interfaccia figlia chiama browseNetworkInterface().
 * (Part 82, 6.5.2 / OPC 10000-22)
 * ============================================================ */

void browseNetworkInterfaces(UA_Client *client, UA_NodeId niFolderNodeId) {
    printf("\n  +-- NetworkInterfaces/\n");

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, niFolderNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                char ifName[MAX_STR_LEN];
                snprintf(ifName, sizeof(ifName), "%.*s",
                         (int)ref->browseName.name.length,
                         ref->browseName.name.data);

                browseNetworkInterface(client, ref->nodeId.nodeId, ifName);
            }
        }
    }

    if(bResp.resultsSize == 0 ||
       (bResp.resultsSize > 0 && bResp.results[0].referencesSize == 0))
        printf("    (nessuna interfaccia)\n");

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * browseServerUAFX
 *
 * Browse completo di un server UAFX:
 *   Objects/
 *   +-- FxRoot/  → AutomationComponent(s)
 *   +-- NetworkInterfaces/  → interfacce + LLDP
 * ============================================================ */

void browseServerUAFX(UA_Client *client, const char *endpoint) {
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

    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, objectsFolder, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                /* ─── FxRoot ─────────────────────────────── */
                UA_String fxRootStr = UA_STRING("FxRoot");
                if(UA_String_equal(&ref->browseName.name, &fxRootStr)) {
                    printf("  +-- FxRoot/\n");

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

                /* ─── NetworkInterfaces (Part 82, 6.5.2) ─── */
                UA_String niStr = UA_STRING("NetworkInterfaces");
                if(UA_String_equal(&ref->browseName.name, &niStr)) {
                    browseNetworkInterfaces(client, ref->nodeId.nodeId);
                }
            }
        }
    } else {
        printf("  (browse Objects fallito: %s)\n",
               UA_StatusCode_name(bResp.responseHeader.serviceResult));
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}
