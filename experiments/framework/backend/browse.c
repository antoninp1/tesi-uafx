/* ============================================================
 * browse.c
 *
 * Implementazione del browse dell'Address Space UAFX.
 * Ogni funzione (a) stampa su stdout per debug e (b) popola
 * le struct dati in model.h.
 * ============================================================ */

#include "browse.h"
#include "helpers.h"

/* Helper interno: copia stringa con troncamento sicuro */
static void safeStrCopy(char *dst, const char *src, size_t dstSize) {
    if(!dst || dstSize == 0) return;
    if(!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

/* Helper: legge stringa e la copia direttamente in un buffer */
static void readStringInto(UA_Client *client, UA_NodeId parent,
                           const char *propName, char *dst, size_t dstSize) {
    char *val = readStringProperty(client, parent, propName);
    if(val) {
        safeStrCopy(dst, val, dstSize);
        free(val);
    }
}

/* ============================================================
 * resolveChildByName
 *
 * Esegue un browse dei figli di parentId e restituisce il NodeId
 * del primo figlio con browseName uguale a name.
 * ============================================================ */

UA_NodeId resolveChildByName(UA_Client *client,
                              UA_NodeId parentId,
                              const char *name) {
    UA_BrowseRequest req;
    UA_BrowseRequest_init(&req);
    req.requestedMaxReferencesPerNode = 0;
    req.nodesToBrowse = UA_BrowseDescription_new();
    req.nodesToBrowseSize = 1;
    req.nodesToBrowse[0].nodeId = parentId;
    req.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    req.nodesToBrowse[0].referenceTypeId =
        UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    req.nodesToBrowse[0].includeSubtypes = true;
    req.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_BROWSENAME;

    UA_BrowseResponse resp = UA_Client_Service_browse(client, req);
    UA_BrowseRequest_clear(&req);

    UA_NodeId result = UA_NODEID_NULL;

    if(resp.responseHeader.serviceResult != UA_STATUSCODE_GOOD ||
       resp.resultsSize == 0) {
        UA_BrowseResponse_clear(&resp);
        return result;
    }

    for(size_t i = 0; i < resp.results[0].referencesSize; i++) {
        UA_ReferenceDescription *ref = &resp.results[0].references[i];
        char refName[256] = {0};
        snprintf(refName, sizeof(refName), "%.*s",
                 (int)ref->browseName.name.length,
                 ref->browseName.name.data);

        if(strcmp(refName, name) == 0) {
            UA_NodeId_copy(&ref->nodeId.nodeId, &result);
            break;
        }
    }

    UA_BrowseResponse_clear(&resp);
    return result;
}

/* ============================================================
 * browseDataFolder
 *
 * Stampa le variabili e le inserisce in vars[].
 * ============================================================ */

void browseDataFolder(UA_Client *client, UA_NodeId folderNodeId,
                      const char *folderName, const char *indent,
                      DataVariable *vars, size_t maxVars, size_t *countOut) {
    printf("%s|\n", indent);
    printf("%s+-- %s/\n", indent, folderName);

    if(countOut) *countOut = 0;

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

                /* Salta EngineeringUnits: e' una proprieta' figlia,
                 * non una variabile di dato (viene letta a parte) */
                if(strcmp(varName, "EngineeringUnits") == 0)
                    continue;

                UA_Variant value;
                UA_Variant_init(&value);
                UA_StatusCode rc = UA_Client_readValueAttribute(
                    client, ref->nodeId.nodeId, &value);

                printf("%s    +-- %s", indent, varName);

                /* Slot di destinazione nella struct */
                DataVariable *slot = NULL;
                if(vars && countOut && *countOut < maxVars) {
                    slot = &vars[*countOut];
                    memset(slot, 0, sizeof(DataVariable));
                    safeStrCopy(slot->name, varName, MAX_STR_LEN);
                    slot->type = DATATYPE_UNKNOWN;
                    // Salva il NodeId della variabile
                    UA_String nodeIdStr = UA_STRING_NULL;
                    UA_NodeId_print(&ref->nodeId.nodeId, &nodeIdStr);
                    snprintf(slot->nodeId, sizeof(slot->nodeId), "%.*s",
                            (int)nodeIdStr.length, nodeIdStr.data);
                    UA_String_clear(&nodeIdStr);
                }

                if(rc == UA_STATUSCODE_GOOD) {
                    if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
                        UA_Float fv = *(UA_Float *)value.data;
                        printf(": %.2f", fv);
                        char *units = readStringProperty(client,
                                          ref->nodeId.nodeId, "EngineeringUnits");
                        if(units) {
                            printf(" %s", units);
                            if(slot) safeStrCopy(slot->engineeringUnits, units, MAX_STR_LEN);
                            free(units);
                        }
                        printf(" [Float]");
                        if(slot) {
                            slot->type = DATATYPE_FLOAT;
                            slot->value.fVal = fv;
                        }
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
                        UA_Double dv = *(UA_Double *)value.data;
                        printf(": %.4f [Double]", dv);
                        if(slot) {
                            slot->type = DATATYPE_DOUBLE;
                            slot->value.dVal = dv;
                        }
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
                        UA_Int32 iv = *(UA_Int32 *)value.data;
                        printf(": %d [Int32]", iv);
                        if(slot) {
                            slot->type = DATATYPE_INT32;
                            slot->value.i32Val = iv;
                        }
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
                        UA_UInt32 uv = *(UA_UInt32 *)value.data;
                        printf(": %u [UInt32]", uv);
                        if(slot) {
                            slot->type = DATATYPE_UINT32;
                            slot->value.u32Val = uv;
                        }
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
                        UA_Boolean bv = *(UA_Boolean *)value.data;
                        printf(": %s [Boolean]", bv ? "true" : "false");
                        if(slot) {
                            slot->type = DATATYPE_BOOLEAN;
                            slot->value.bVal = bv;
                        }
                    } else if(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_STRING])) {
                        UA_String *s = (UA_String *)value.data;
                        printf(": %.*s [String]", (int)s->length, s->data);
                        if(slot) {
                            slot->type = DATATYPE_STRING;
                            size_t len = s->length < MAX_STR_LEN - 1 ? s->length : MAX_STR_LEN - 1;
                            memcpy(slot->value.sVal, s->data, len);
                            slot->value.sVal[len] = '\0';
                        }
                    } else {
                        printf(" [tipo non gestito]");
                    }
                } else {
                    printf(" (lettura fallita: %s)", UA_StatusCode_name(rc));
                    slot = NULL;  /* non incrementare il counter */
                }
                printf("\n");
                UA_Variant_clear(&value);

                if(slot && countOut)
                    (*countOut)++;
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
                            const char *feName,
                            FunctionalEntity *fe) {
    printf("\n      +-- FunctionalEntity: %s\n", feName);

    if(fe) {
    memset(fe, 0, sizeof(FunctionalEntity));
    safeStrCopy(fe->name, feName, MAX_STR_LEN);

    UA_String nodeIdStr = UA_STRING_NULL;
    UA_NodeId_print(&feNodeId, &nodeIdStr);
    snprintf(fe->nodeId, sizeof(fe->nodeId), "%.*s",
             (int)nodeIdStr.length, nodeIdStr.data);
    UA_String_clear(&nodeIdStr);
}

    /* Identificazione */
    char *authorUri  = readStringProperty(client, feNodeId, "AuthorUri");
    char *identifier = readStringProperty(client, feNodeId, "AuthorAssignedIdentifier");
    char *version    = readStringProperty(client, feNodeId, "AuthorAssignedVersion");

    if(authorUri) {
        printf("      |   AuthorUri:                %s\n", authorUri);
        if(fe) safeStrCopy(fe->authorUri, authorUri, MAX_STR_LEN);
        free(authorUri);
    }
    if(identifier) {
        printf("      |   AuthorAssignedIdentifier: %s\n", identifier);
        if(fe) safeStrCopy(fe->authorAssignedIdentifier, identifier, MAX_STR_LEN);
        free(identifier);
    }
    if(version) {
        printf("      |   AuthorAssignedVersion:    %s\n", version);
        if(fe) safeStrCopy(fe->authorAssignedVersion, version, MAX_STR_LEN);
        free(version);
    }

    UA_UInt32 opHealth;
    if(readUInt32Property(client, feNodeId, "OperationalHealth", &opHealth)) {
        printf("      |   OperationalHealth:        %u\n", opHealth);
        if(fe) fe->operationalHealth = opHealth;
    }

    /* Naviga i figli della FE */
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, feNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String outputStr = UA_STRING("OutputData");
                if(UA_String_equal(&ref->browseName.name, &outputStr)) {
                    browseDataFolder(client, ref->nodeId.nodeId,
                                     "OutputData", "      ",
                                     fe ? fe->outputData : NULL,
                                     MAX_DATA_VARIABLES,
                                     fe ? &fe->outputDataCount : NULL);
                }

                UA_String inputStr = UA_STRING("InputData");
                if(UA_String_equal(&ref->browseName.name, &inputStr)) {
                    browseDataFolder(client, ref->nodeId.nodeId,
                                     "InputData", "      ",
                                     fe ? fe->inputData : NULL,
                                     MAX_DATA_VARIABLES,
                                     fe ? &fe->inputDataCount : NULL);
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

                                char ceName[MAX_STR_LEN];
                                snprintf(ceName, sizeof(ceName), "%.*s",
                                         (int)ce->browseName.name.length,
                                         ce->browseName.name.data);
                                printf("          +-- %s\n", ceName);

                               if(fe && fe->connectionEndpointsCount < MAX_CONN_ENDPOINTS) {
                                    ConnectionEndpoint *ceInfo =
                                        &fe->connectionEndpoints[fe->connectionEndpointsCount];
                                    memset(ceInfo, 0, sizeof(ConnectionEndpoint));
                                    safeStrCopy(ceInfo->name, ceName, MAX_STR_LEN);
                                    UA_NodeId_copy(&ce->nodeId.nodeId, &ceInfo->nodeId);
                                    fe->connectionEndpointsCount++;
                                }
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

void browseAssets(UA_Client *client, UA_NodeId assetsFolderNodeId,
                  AutomationComponent *ac) {
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

                Asset *asset = NULL;
                if(ac && ac->assetsCount < MAX_ASSETS) {
                    asset = &ac->assets[ac->assetsCount];
                    memset(asset, 0, sizeof(Asset));
                    safeStrCopy(asset->name, assetName, MAX_STR_LEN);
                    ac->assetsCount++;
                }

                /* Lettura proprieta' DI con stampa */
                struct { const char *name; char *dst; size_t dstSize; } props[] = {
                    {"Manufacturer",     asset ? asset->manufacturer     : NULL, MAX_STR_LEN},
                    {"ManufacturerUri",  asset ? asset->manufacturerUri  : NULL, MAX_STR_LEN},
                    {"Model",            asset ? asset->model            : NULL, MAX_STR_LEN},
                    {"ProductCode",      asset ? asset->productCode      : NULL, MAX_STR_LEN},
                    {"HardwareRevision", asset ? asset->hardwareRevision : NULL, MAX_STR_LEN},
                    {"SoftwareRevision", asset ? asset->softwareRevision : NULL, MAX_STR_LEN},
                    {"DeviceClass",      asset ? asset->deviceClass      : NULL, MAX_STR_LEN},
                    {"SerialNumber",     asset ? asset->serialNumber     : NULL, MAX_STR_LEN},
                };

                for(size_t p = 0; p < sizeof(props)/sizeof(props[0]); p++) {
                    char *val = readStringProperty(client, ar->nodeId.nodeId, props[p].name);
                    if(val) {
                        printf("    |       %-18s %s\n", props[p].name, val);
                        if(props[p].dst)
                            safeStrCopy(props[p].dst, val, props[p].dstSize);
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
                                 UA_NodeId capFolderNodeId,
                                 ComponentCapabilities *caps) {
    printf("    |\n");
    printf("    +-- ComponentCapabilities/\n");

    if(caps) memset(caps, 0, sizeof(ComponentCapabilities));

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
                    UA_UInt32 uv = *(UA_UInt32 *)value.data;
                    printf(": %u", uv);
                    if(caps) {
                        if(strcmp(name, "MaxConnections") == 0)
                            caps->maxConnections = uv;
                        else if(strcmp(name, "MinConnections") == 0)
                            caps->minConnections = uv;
                    }
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
                               const char *acName,
                               AutomationComponent *ac) {
    printf("\n    +-- AutomationComponent: %s\n", acName);

    if(ac) {
        memset(ac, 0, sizeof(AutomationComponent));
        safeStrCopy(ac->name, acName, MAX_STR_LEN);
         // Salva il NodeId dell'AC
        UA_String nodeIdStr = UA_STRING_NULL;
        UA_NodeId_print(&acNodeId, &nodeIdStr);
        snprintf(ac->nodeId, sizeof(ac->nodeId), "%.*s",
                (int)nodeIdStr.length, nodeIdStr.data);
        UA_String_clear(&nodeIdStr);
    }

    


    char *conformance = readStringProperty(client, acNodeId, "ConformanceName");
    if(conformance) {
        printf("    |   ConformanceName: %s\n", conformance);
        if(ac) safeStrCopy(ac->conformanceName, conformance, MAX_STR_LEN);
        free(conformance);
    }

    UA_UInt32 aggHealth;
    if(readUInt32Property(client, acNodeId, "AggregatedHealth", &aggHealth)) {
        printf("    |   AggregatedHealth: %u\n", aggHealth);
        if(ac) ac->aggregatedHealth = aggHealth;
    }

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
                    browseAssets(client, ref->nodeId.nodeId, ac);
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

                                FunctionalEntity *fePtr = NULL;
                                if(ac && ac->functionalEntitiesCount < MAX_FUNCTIONAL_ENTITIES) {
                                    fePtr = &ac->functionalEntities[ac->functionalEntitiesCount];
                                    ac->functionalEntitiesCount++;
                                }

                                // Salva il NodeId della FE come stringa
                                if(fePtr) {
                                    UA_String nodeIdStr = UA_STRING_NULL;
                                    UA_NodeId_print(&fr->nodeId.nodeId, &nodeIdStr);
                                    snprintf(fePtr->nodeId, sizeof(fePtr->nodeId), "%.*s",
                                            (int)nodeIdStr.length, nodeIdStr.data);
                                    UA_String_clear(&nodeIdStr);
                                }

                                browseFunctionalEntity(client,
                                    fr->nodeId.nodeId, feName, fePtr);
                            }
                        }
                    }
                    UA_BrowseResponse_clear(&bResp2);
                    UA_BrowseRequest_clear(&bReq2);
                }
                else if(UA_String_equal(&ref->browseName.name, &capStr)) {
                    browseComponentCapabilities(client, ref->nodeId.nodeId,
                                                ac ? &ac->capabilities : NULL);
                }
            }
        }
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);
}

/* ============================================================
 * browseLldpRemoteSystem
 * ============================================================ */

void browseLldpRemoteSystem(UA_Client *client, UA_NodeId rsNodeId,
                            const char *rsName, const char *indent,
                            LldpNeighbor *neighbor) {
    printf("%s+-- %s\n", indent, rsName);

    if(neighbor) memset(neighbor, 0, sizeof(LldpNeighbor));

    /* Lettura campi stringa con popolamento struct */
    struct { const char *name; char *dst; size_t dstSize; } strFields[] = {
        {"ChassisId",          neighbor ? neighbor->chassisId          : NULL, MAX_STR_LEN},
        {"SysName",            neighbor ? neighbor->sysName            : NULL, MAX_STR_LEN},
        {"SysDescr",           neighbor ? neighbor->sysDescr           : NULL, MAX_STR_LEN},
        {"MgmtAddress",        neighbor ? neighbor->mgmtAddress        : NULL, MAX_STR_LEN},
        {"PortId",             neighbor ? neighbor->portId             : NULL, MAX_STR_LEN},
        {"PortDescr",          neighbor ? neighbor->portDescr          : NULL, MAX_STR_LEN},
        {"SystemCapabilities", neighbor ? neighbor->systemCapabilities : NULL, MAX_STR_LEN},
    };

    for(size_t f = 0; f < sizeof(strFields)/sizeof(strFields[0]); f++) {
        char *val = readStringProperty(client, rsNodeId, strFields[f].name);
        if(val) {
            printf("%s    %-22s %s\n", indent, strFields[f].name, val);
            if(strFields[f].dst)
                safeStrCopy(strFields[f].dst, val, strFields[f].dstSize);
            free(val);
        }
    }

    /* Campi UInt32 */
    UA_UInt32 uval;
    if(readUInt32Property(client, rsNodeId, "ChassisIdSubtype", &uval)) {
        printf("%s    %-22s %u\n", indent, "ChassisIdSubtype", uval);
        if(neighbor) neighbor->chassisIdSubtype = uval;
    }
    if(readUInt32Property(client, rsNodeId, "PortIdSubtype", &uval)) {
        printf("%s    %-22s %u\n", indent, "PortIdSubtype", uval);
        if(neighbor) neighbor->portIdSubtype = uval;
    }
    if(readUInt32Property(client, rsNodeId, "TimeToLive", &uval)) {
        printf("%s    %-22s %u\n", indent, "TimeToLive", uval);
        if(neighbor) neighbor->timeToLive = uval;
    }
}

/* ============================================================
 * browseLldpData
 * ============================================================ */

void browseLldpData(UA_Client *client, UA_NodeId lldpFolderNodeId,
                    const char *indent,
                    NetworkInterface *iface) {
    printf("%s|\n", indent);
    printf("%s+-- LldpData/\n", indent);

    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, lldpFolderNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String localStr  = UA_STRING("LocalSystemData");
                UA_String remoteStr = UA_STRING("RemoteSystemsData");

                if(UA_String_equal(&ref->browseName.name, &localStr)) {
                    /* ─── LocalSystemData ──────────────────── */
                    printf("%s    +-- LocalSystemData/\n", indent);

                    LldpLocalData *local = iface ? &iface->localData : NULL;
                    if(local) memset(local, 0, sizeof(LldpLocalData));

                    struct { const char *name; char *dst; size_t dstSize; } locFields[] = {
                        {"ChassisId",          local ? local->chassisId          : NULL, MAX_STR_LEN},
                        {"SysName",            local ? local->sysName            : NULL, MAX_STR_LEN},
                        {"SysDescr",           local ? local->sysDescr           : NULL, MAX_STR_LEN},
                        {"MgmtAddress",        local ? local->mgmtAddress        : NULL, MAX_STR_LEN},
                        {"SystemCapabilities", local ? local->systemCapabilities : NULL, MAX_STR_LEN},
                        {"PortId",             local ? local->portId             : NULL, MAX_STR_LEN},
                    };

                    for(size_t f = 0; f < sizeof(locFields)/sizeof(locFields[0]); f++) {
                        char *val = readStringProperty(client,
                                        ref->nodeId.nodeId, locFields[f].name);
                        if(val) {
                            printf("%s        %-22s %s\n", indent, locFields[f].name, val);
                            if(locFields[f].dst)
                                safeStrCopy(locFields[f].dst, val, locFields[f].dstSize);
                            free(val);
                        }
                    }

                    UA_UInt32 subtype;
                    if(readUInt32Property(client, ref->nodeId.nodeId,
                                         "ChassisIdSubtype", &subtype)) {
                        printf("%s        %-22s %u\n", indent, "ChassisIdSubtype", subtype);
                        if(local) local->chassisIdSubtype = subtype;
                    }
                    if(readUInt32Property(client, ref->nodeId.nodeId,
                                         "PortIdSubtype", &subtype)) {
                        printf("%s        %-22s %u\n", indent, "PortIdSubtype", subtype);
                        if(local) local->portIdSubtype = subtype;
                    }

                    if(iface) iface->hasLocalData = true;

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

                                LldpNeighbor *nbr = NULL;
                                if(iface && iface->neighborsCount < MAX_LLDP_NEIGHBORS) {
                                    nbr = &iface->neighbors[iface->neighborsCount];
                                    iface->neighborsCount++;
                                }

                                browseLldpRemoteSystem(client,
                                    rs->nodeId.nodeId, rsName, rsIndent, nbr);
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
 * ============================================================ */

void browseNetworkInterface(UA_Client *client, UA_NodeId ifNodeId,
                            const char *ifName,
                            NetworkInterface *iface) {
    printf("    +-- %s\n", ifName);

    if(iface) {
        memset(iface, 0, sizeof(NetworkInterface));
        safeStrCopy(iface->name, ifName, MAX_STR_LEN);
    }

    /* Proprieta' interfaccia */
    readStringInto(client, ifNodeId, "AdminStatus",
                   iface ? iface->adminStatus : NULL,
                   iface ? MAX_STR_LEN : 0);
    readStringInto(client, ifNodeId, "OperStatus",
                   iface ? iface->operStatus : NULL,
                   iface ? MAX_STR_LEN : 0);
    readStringInto(client, ifNodeId, "PhysAddress",
                   iface ? iface->physAddress : NULL,
                   iface ? MAX_STR_LEN : 0);

    if(iface) {
        if(iface->adminStatus[0]) printf("    |   %-18s %s\n", "AdminStatus", iface->adminStatus);
        if(iface->operStatus[0])  printf("    |   %-18s %s\n", "OperStatus",  iface->operStatus);
        if(iface->physAddress[0]) printf("    |   %-18s %s\n", "PhysAddress", iface->physAddress);
    }

    UA_UInt32 speed;
    if(readUInt32Property(client, ifNodeId, "Speed", &speed)) {
        printf("    |   %-18s %u Mbps\n", "Speed", speed);
        if(iface) iface->speed = speed;
    }

    /* Cerca LldpData/ tra i figli */
    UA_BrowseRequest bReq;
    UA_BrowseResponse bResp = browseNode(client, ifNodeId, &bReq);

    if(bResp.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
        for(size_t i = 0; i < bResp.resultsSize; i++) {
            for(size_t j = 0; j < bResp.results[i].referencesSize; j++) {
                UA_ReferenceDescription *ref = &bResp.results[i].references[j];

                UA_String lldpStr = UA_STRING("LldpData");
                if(UA_String_equal(&ref->browseName.name, &lldpStr)) {
                    browseLldpData(client, ref->nodeId.nodeId, "    ", iface);
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
 * Itera tutte le interfacce. Estrae il chassisId dal
 * primo LocalSystemData trovato e lo assegna come node->id
 * (chiave univoca BFS, per Part 82 §7.3.2.2.4.2 il chassisId
 * e' lo stesso su tutte le porte di una stessa station).
 * ============================================================ */

void browseNetworkInterfaces(UA_Client *client, UA_NodeId niFolderNodeId,
                             TopologyNode *node) {
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

                NetworkInterface *iface = NULL;
                if(node && node->interfacesCount < MAX_NETWORK_INTERFACES) {
                    iface = &node->interfaces[node->interfacesCount];
                    node->interfacesCount++;
                }

                browseNetworkInterface(client, ref->nodeId.nodeId, ifName, iface);

                /* Estrai chassisId per identificare il nodo (BFS key).
                 * Se non e' ancora stato impostato e abbiamo localData
                 * popolato, usa quello. */
                if(node && iface && iface->hasLocalData &&
                   node->id[0] == '\0' && iface->localData.chassisId[0]) {
                    safeStrCopy(node->id, iface->localData.chassisId, MAX_STR_LEN);
                }
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
 * ============================================================ */

void browseServerUAFX(UA_Client *client, const char *endpoint,
                      TopologyNode *node) {
    printf("\n  Endpoint: %s\n", endpoint);

    if(node) {
        node->type = NODE_TYPE_UAFX_SERVER;
        safeStrCopy(node->endpointUrl, endpoint, MAX_STR_LEN);
        node->reachable = true;
    }

    /* FindServers per info applicazione */
    size_t serverArraySize = 0;
    UA_ApplicationDescription *serverArray = NULL;
    UA_StatusCode retval = UA_Client_findServers(
        client, endpoint, 0, NULL, 0, NULL,
        &serverArraySize, &serverArray);

    if(retval == UA_STATUSCODE_GOOD && serverArraySize > 0) {
        UA_String *uri = &serverArray[0].applicationUri;
        UA_String *name = &serverArray[0].applicationName.text;

        printf("  ApplicationUri:  %.*s\n", (int)uri->length, uri->data);
        printf("  ApplicationName: %.*s\n", (int)name->length, name->data);

        if(node) {
            size_t l1 = uri->length < MAX_STR_LEN - 1 ? uri->length : MAX_STR_LEN - 1;
            memcpy(node->applicationUri, uri->data, l1);
            node->applicationUri[l1] = '\0';

            size_t l2 = name->length < MAX_STR_LEN - 1 ? name->length : MAX_STR_LEN - 1;
            memcpy(node->applicationName, name->data, l2);
            node->applicationName[l2] = '\0';

            /* Imposta name del nodo (sara' eventualmente sovrascritto
             * dal SysName LLDP se presente) */
            safeStrCopy(node->name, node->applicationName, MAX_STR_LEN);
        }

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

                                AutomationComponent *acPtr = NULL;
                                if(node && node->automationComponentsCount < MAX_AUTOMATION_COMPS) {
                                    acPtr = &node->automationComponents[node->automationComponentsCount];
                                    node->automationComponentsCount++;
                                }

                                browseAutomationComponent(client,
                                    acRef->nodeId.nodeId, acName, acPtr);
                            }
                        }
                    }
                    UA_BrowseResponse_clear(&bResp2);
                    UA_BrowseRequest_clear(&bReq2);
                }

                /* ─── NetworkInterfaces ──────────────────── */
                UA_String niStr = UA_STRING("NetworkInterfaces");
                if(UA_String_equal(&ref->browseName.name, &niStr)) {
                    browseNetworkInterfaces(client, ref->nodeId.nodeId, node);
                }
            }
        }
    } else {
        printf("  (browse Objects fallito: %s)\n",
               UA_StatusCode_name(bResp.responseHeader.serviceResult));
    }

    UA_BrowseResponse_clear(&bResp);
    UA_BrowseRequest_clear(&bReq);

    /* Se il nodo ha LocalSystemData con SysName, usalo come nome
     * preferito (sostituisce l'applicationName). */
    if(node) {
        for(size_t i = 0; i < node->interfacesCount; i++) {
            if(node->interfaces[i].hasLocalData &&
               node->interfaces[i].localData.sysName[0]) {
                safeStrCopy(node->name, node->interfaces[i].localData.sysName, MAX_STR_LEN);

                /* Anche mgmtAddress dal localData */
                if(node->interfaces[i].localData.mgmtAddress[0])
                    safeStrCopy(node->mgmtAddress,
                                node->interfaces[i].localData.mgmtAddress, MAX_STR_LEN);
                break;
            }
        }
    }
}
