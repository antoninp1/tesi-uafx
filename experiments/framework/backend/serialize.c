/* ============================================================
 * serialize.c
 *
 * Implementazione della serializzazione JSON del TopologyGraph.
 * Usa cJSON per costruire l'albero dinamicamente.
 * ============================================================ */

#include "serialize.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Helper: timestamp ISO 8601 UTC
 * ============================================================ */

static void timeToIso8601(time_t t, char *buf, size_t bufSize) {
    if(t == 0) {
        buf[0] = '\0';        return;
    }
    struct tm *tm = gmtime(&t);
    strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%SZ", tm);
}

/* ============================================================
 * Helper: tipo di nodo come stringa
 * ============================================================ */

static const char *nodeTypeToString(TopologyNodeType t) {
    switch(t) {
        case NODE_TYPE_UAFX_SERVER:    return "uafx_server";
        case NODE_TYPE_SWITCH:         return "switch";
        case NODE_TYPE_PHANTOM_SWITCH: return "phantom";
        default:                        return "unknown";
    }
}

/* ============================================================
 * Helper: DataVariable → JSON
 * ============================================================ */

static cJSON *dataVariableToJson(const DataVariable *v) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", v->name);
    cJSON_AddStringToObject(obj, "nodeId", v->nodeId); 
    cJSON_AddStringToObject(obj, "units", v->engineeringUnits);

    switch(v->type) {
        case DATATYPE_FLOAT:
            cJSON_AddStringToObject(obj, "type", "float");
            cJSON_AddNumberToObject(obj, "value", v->value.fVal);
            break;
        case DATATYPE_DOUBLE:
            cJSON_AddStringToObject(obj, "type", "double");
            cJSON_AddNumberToObject(obj, "value", v->value.dVal);
            break;
        case DATATYPE_INT32:
            cJSON_AddStringToObject(obj, "type", "int32");
            cJSON_AddNumberToObject(obj, "value", v->value.i32Val);
            break;
        case DATATYPE_UINT32:
            cJSON_AddStringToObject(obj, "type", "uint32");
            cJSON_AddNumberToObject(obj, "value", v->value.u32Val);
            break;
        case DATATYPE_BOOLEAN:
            cJSON_AddStringToObject(obj, "type", "boolean");
            cJSON_AddBoolToObject(obj, "value", v->value.bVal);
            break;
        case DATATYPE_STRING:
            cJSON_AddStringToObject(obj, "type", "string");
            cJSON_AddStringToObject(obj, "value", v->value.sVal);
            break;
        default:
            cJSON_AddStringToObject(obj, "type", "unknown");
            cJSON_AddNullToObject(obj, "value");
            break;
    }
    return obj;
}

/* ============================================================
 * Helper: FunctionalEntity → JSON
 * ============================================================ */

static cJSON *functionalEntityToJson(const FunctionalEntity *fe) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name", fe->name);
    cJSON_AddStringToObject(obj, "nodeId", fe->nodeId);
    cJSON_AddStringToObject(obj, "authorUri", fe->authorUri);
    cJSON_AddStringToObject(obj, "authorAssignedIdentifier", fe->authorAssignedIdentifier);
    cJSON_AddStringToObject(obj, "authorAssignedVersion", fe->authorAssignedVersion);
    cJSON_AddNumberToObject(obj, "operationalHealth", fe->operationalHealth);

    cJSON *outputs = cJSON_AddArrayToObject(obj, "outputData");
    for(size_t i = 0; i < fe->outputDataCount; i++)
        cJSON_AddItemToArray(outputs, dataVariableToJson(&fe->outputData[i]));

    cJSON *inputs = cJSON_AddArrayToObject(obj, "inputData");
    for(size_t i = 0; i < fe->inputDataCount; i++)
        cJSON_AddItemToArray(inputs, dataVariableToJson(&fe->inputData[i]));

    cJSON *ceps = cJSON_AddArrayToObject(obj, "connectionEndpoints");
    for(size_t i = 0; i < fe->connectionEndpointsCount; i++)
    cJSON_AddItemToArray(ceps, cJSON_CreateString(fe->connectionEndpoints[i].name));
    return obj;
}

/* ============================================================
 * Helper: Asset → JSON
 * ============================================================ */

static cJSON *assetToJson(const Asset *a) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name",             a->name);
    cJSON_AddStringToObject(obj, "manufacturer",     a->manufacturer);
    cJSON_AddStringToObject(obj, "manufacturerUri",  a->manufacturerUri);
    cJSON_AddStringToObject(obj, "model",            a->model);
    cJSON_AddStringToObject(obj, "productCode",      a->productCode);
    cJSON_AddStringToObject(obj, "hardwareRevision", a->hardwareRevision);
    cJSON_AddStringToObject(obj, "softwareRevision", a->softwareRevision);
    cJSON_AddStringToObject(obj, "deviceClass",      a->deviceClass);
    cJSON_AddStringToObject(obj, "serialNumber",     a->serialNumber);
    return obj;
}

/* ============================================================
 * Helper: AutomationComponent → JSON
 * ============================================================ */

static cJSON *automationComponentToJson(const AutomationComponent *ac) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name",             ac->name);
    cJSON_AddStringToObject(obj, "nodeId", ac->nodeId);
    cJSON_AddStringToObject(obj, "conformanceName",  ac->conformanceName);
    cJSON_AddNumberToObject(obj, "aggregatedHealth", ac->aggregatedHealth);

    cJSON *assets = cJSON_AddArrayToObject(obj, "assets");
    for(size_t i = 0; i < ac->assetsCount; i++)
        cJSON_AddItemToArray(assets, assetToJson(&ac->assets[i]));

    cJSON *fes = cJSON_AddArrayToObject(obj, "functionalEntities");
    for(size_t i = 0; i < ac->functionalEntitiesCount; i++)
        cJSON_AddItemToArray(fes, functionalEntityToJson(&ac->functionalEntities[i]));

    cJSON *caps = cJSON_AddObjectToObject(obj, "capabilities");
    cJSON_AddNumberToObject(caps, "maxConnections", ac->capabilities.maxConnections);
    cJSON_AddNumberToObject(caps, "minConnections", ac->capabilities.minConnections);

    return obj;
}

/* ============================================================
 * Helper: LldpNeighbor → JSON
 * ============================================================ */

static cJSON *lldpNeighborToJson(const LldpNeighbor *n) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "chassisId",           n->chassisId);
    cJSON_AddNumberToObject(obj, "chassisIdSubtype",    n->chassisIdSubtype);
    cJSON_AddStringToObject(obj, "sysName",             n->sysName);
    cJSON_AddStringToObject(obj, "sysDescr",            n->sysDescr);
    cJSON_AddStringToObject(obj, "mgmtAddress",         n->mgmtAddress);
    cJSON_AddStringToObject(obj, "portId",              n->portId);
    cJSON_AddNumberToObject(obj, "portIdSubtype",       n->portIdSubtype);
    cJSON_AddStringToObject(obj, "portDescr",           n->portDescr);
    cJSON_AddStringToObject(obj, "systemCapabilities",  n->systemCapabilities);
    cJSON_AddNumberToObject(obj, "timeToLive",          n->timeToLive);
    return obj;
}

/* ============================================================
 * Helper: NetworkInterface → JSON
 * ============================================================ */

static cJSON *networkInterfaceToJson(const NetworkInterface *iface) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name",        iface->name);
    cJSON_AddStringToObject(obj, "adminStatus", iface->adminStatus);
    cJSON_AddStringToObject(obj, "operStatus",  iface->operStatus);
    cJSON_AddStringToObject(obj, "physAddress", iface->physAddress);
    cJSON_AddNumberToObject(obj, "speed",       iface->speed);

    if(iface->hasLocalData) {
        cJSON *local = cJSON_AddObjectToObject(obj, "localData");
        cJSON_AddStringToObject(local, "chassisId",          iface->localData.chassisId);
        cJSON_AddNumberToObject(local, "chassisIdSubtype",   iface->localData.chassisIdSubtype);
        cJSON_AddStringToObject(local, "sysName",            iface->localData.sysName);
        cJSON_AddStringToObject(local, "sysDescr",           iface->localData.sysDescr);
        cJSON_AddStringToObject(local, "mgmtAddress",        iface->localData.mgmtAddress);
        cJSON_AddStringToObject(local, "systemCapabilities", iface->localData.systemCapabilities);
        cJSON_AddStringToObject(local, "portId",             iface->localData.portId);
        cJSON_AddNumberToObject(local, "portIdSubtype",      iface->localData.portIdSubtype);
    } else {
        cJSON_AddNullToObject(obj, "localData");
    }

    cJSON *neighbors = cJSON_AddArrayToObject(obj, "neighbors");
    for(size_t i = 0; i < iface->neighborsCount; i++)
        cJSON_AddItemToArray(neighbors, lldpNeighborToJson(&iface->neighbors[i]));

    return obj;
}

/* ============================================================
 * Helper: TopologyNode → JSON (per la vista fisica)
 * ============================================================ */

static cJSON *topologyNodeToJson(const TopologyNode *node) {
    cJSON *obj = cJSON_CreateObject();

    /* Campi "React Flow friendly" (id + label + type) */
    cJSON_AddStringToObject(obj, "id",          node->id);
    cJSON_AddStringToObject(obj, "label",       node->name);
    cJSON_AddStringToObject(obj, "type",        nodeTypeToString(node->type));

    /* Dati applicativi */
    cJSON_AddStringToObject(obj, "description",    node->description);
    cJSON_AddStringToObject(obj, "mgmtAddress",    node->mgmtAddress);
    cJSON_AddStringToObject(obj, "endpointUrl",    node->endpointUrl);
    cJSON_AddBoolToObject  (obj, "reachable",      node->reachable);
    cJSON_AddStringToObject(obj, "applicationUri", node->applicationUri);
    cJSON_AddStringToObject(obj, "applicationName",node->applicationName);

    /* Sotto-alberi */
    cJSON *acs = cJSON_AddArrayToObject(obj, "automationComponents");
    for(size_t i = 0; i < node->automationComponentsCount; i++)
        cJSON_AddItemToArray(acs, automationComponentToJson(&node->automationComponents[i]));

    cJSON *ifaces = cJSON_AddArrayToObject(obj, "interfaces");
    for(size_t i = 0; i < node->interfacesCount; i++)
        cJSON_AddItemToArray(ifaces, networkInterfaceToJson(&node->interfaces[i]));

    return obj;
}

/* ============================================================
 * Helper: TopologyLink → JSON (per la vista fisica)
 *
 * Usa nomenclatura React Flow: source/target invece di A/B.
 * ============================================================ */

static cJSON *topologyLinkToJson(const TopologyLink *link, size_t idx) {
    cJSON *obj = cJSON_CreateObject();

    char linkId[32];
    snprintf(linkId, sizeof(linkId), "link_%zu", idx);
    cJSON_AddStringToObject(obj, "id", linkId);

    /* Source (endpoint A) */
    cJSON_AddStringToObject(obj, "source",       link->endpointA.chassisId);
    cJSON_AddStringToObject(obj, "sourceName",   link->endpointA.sysName);
    cJSON_AddStringToObject(obj, "sourcePortId", link->endpointA.portId);
    cJSON_AddStringToObject(obj, "sourcePort",   link->endpointA.portDescr);

    /* Target (endpoint B) */
    cJSON_AddStringToObject(obj, "target",       link->endpointB.chassisId);
    cJSON_AddStringToObject(obj, "targetName",   link->endpointB.sysName);
    cJSON_AddStringToObject(obj, "targetPortId", link->endpointB.portId);
    cJSON_AddStringToObject(obj, "targetPort",   link->endpointB.portDescr);

    cJSON_AddBoolToObject(obj, "confirmed", link->confirmedBidirectional);
    return obj;
}

/* ============================================================
 * Vista logica: genera nodi = FunctionalEntities flattened
 *
 * Per la vista logica, ogni FunctionalEntity di ogni AC
 * di ogni TopologyNode diventa un "nodo" del grafo logico.
 * L'id e' costruito come "<chassisId>/<acName>/<feName>"
 * per essere univoco.
 *
 * I link logici (connessioni PubSub) saranno popolati in
 * fase 2. Per ora l'array resta vuoto.
 * ============================================================ */

static cJSON *buildLogicalView(const TopologyGraph *graph) {
    cJSON *logical = cJSON_CreateObject();
    cJSON *lnodes = cJSON_AddArrayToObject(logical, "nodes");
    cJSON *llinks = cJSON_AddArrayToObject(logical, "links");

    for(size_t n = 0; n < graph->nodesCount; n++) {
        const TopologyNode *node = &graph->nodes[n];
        if(node->type != NODE_TYPE_UAFX_SERVER) continue;

        for(size_t a = 0; a < node->automationComponentsCount; a++) {
            const AutomationComponent *ac = &node->automationComponents[a];

            for(size_t f = 0; f < ac->functionalEntitiesCount; f++) {
                const FunctionalEntity *fe = &ac->functionalEntities[f];

                cJSON *feNode = cJSON_CreateObject();

                char id[512];
                snprintf(id, sizeof(id), "%s/%s/%s",
                         node->id, ac->name, fe->name);
                cJSON_AddStringToObject(feNode, "feNodeId", fe->nodeId);
                cJSON_AddStringToObject(feNode, "endpointUrl", node->endpointUrl);


                cJSON_AddStringToObject(feNode, "id", id);
                cJSON_AddStringToObject(feNode, "label", fe->name);
                cJSON_AddStringToObject(feNode, "type", "functional_entity");

                /* Riferimenti per il frontend: parent device, AC */
                cJSON_AddStringToObject(feNode, "parentChassisId", node->id);
                cJSON_AddStringToObject(feNode, "parentDeviceName", node->name);
                cJSON_AddStringToObject(feNode, "acNodeId", ac->nodeId);

                cJSON_AddStringToObject(feNode, "parentAcName", ac->name);

                /* FE metadata */
                cJSON_AddStringToObject(feNode, "authorAssignedIdentifier",
                                        fe->authorAssignedIdentifier);
                cJSON_AddStringToObject(feNode, "authorAssignedVersion",
                                        fe->authorAssignedVersion);

                /* "Handles" disponibili: un handle per ogni DataVariable
                 * di output (publisher) o input (subscriber).
                 * Il frontend React Flow li usa come punti di connessione. */
                cJSON *outputs = cJSON_AddArrayToObject(feNode, "outputs");
                for(size_t i = 0; i < fe->outputDataCount; i++) {
                    cJSON *h = cJSON_CreateObject();
                    cJSON_AddStringToObject(h, "name", fe->outputData[i].name);
                    cJSON_AddStringToObject(h, "nodeId", fe->outputData[i].nodeId);
                    cJSON_AddStringToObject(h, "units", fe->outputData[i].engineeringUnits);
                    cJSON_AddItemToArray(outputs, h);
                }

                cJSON *inputs = cJSON_AddArrayToObject(feNode, "inputs");
                for(size_t i = 0; i < fe->inputDataCount; i++) {
                    cJSON *h = cJSON_CreateObject();
                    cJSON_AddStringToObject(h, "name", fe->inputData[i].name);
                    cJSON_AddStringToObject(h, "nodeId", fe->inputData[i].nodeId);
                    cJSON_AddStringToObject(h, "units", fe->inputData[i].engineeringUnits);
                    cJSON_AddItemToArray(inputs, h);
                }

                cJSON_AddItemToArray(lnodes, feNode);
            }
        }
    }

    /* llinks resta vuoto: le connessioni PubSub arriveranno in fase 2 */
    /* ── connessione statica di test ─────────────────────────
 * Rimuovere quando correlateConnections() sarà implementato.
 * I due feId devono corrispondere agli id dei nodi logici
 * già presenti nell'array nodes sopra.                    */
/*cJSON *testConn = cJSON_CreateObject();
cJSON_AddStringToObject(testConn, "id",
    "test_conn_1");
cJSON_AddStringToObject(testConn, "publisherFE",
    "00:07:32:ae:79:13/TemperatureSensor/TemperatureReadingFE");
cJSON_AddStringToObject(testConn, "subscriberFE",
    "00:07:32:ae:79:1d/DensitySensor/DensityReadingFE");
cJSON_AddStringToObject(testConn, "outputVariable",
    "output-Temperature");
cJSON_AddStringToObject(testConn, "inputVariable",
    "input-ReceivedTemperature");
cJSON_AddNumberToObject(testConn, "publisherEndpointStatus",  3);
cJSON_AddNumberToObject(testConn, "subscriberEndpointStatus", 2);
cJSON_AddNumberToObject(testConn, "publishingInterval",       5000.0);
cJSON_AddBoolToObject  (testConn, "existing",                 true);
cJSON_AddItemToArray(llinks, testConn);
*/
    return logical;
}

/* ============================================================
 * topologyGraphToJson
 *
 * Output principale per GET /api/topology.
 * Include entrambe le viste (fisica + logica).
 * ============================================================ */

char *topologyGraphToJson(const TopologyGraph *graph) {
    cJSON *root = cJSON_CreateObject();

    /* Metadata */
    char timebuf[64];
    timeToIso8601(graph ? graph->lastScanTime : 0, timebuf, sizeof(timebuf));
    cJSON_AddStringToObject(root, "lastScan", timebuf);

    if(!graph) {
        cJSON *phy = cJSON_AddObjectToObject(root, "physical");
        cJSON_AddArrayToObject(phy, "nodes");
        cJSON_AddArrayToObject(phy, "links");
        cJSON *log = cJSON_AddObjectToObject(root, "logical");
        cJSON_AddArrayToObject(log, "nodes");
        cJSON_AddArrayToObject(log, "links");
    } else {
        /* ─── Vista fisica ──────────────────────────────── */
        cJSON *physical = cJSON_AddObjectToObject(root, "physical");

        cJSON *pnodes = cJSON_AddArrayToObject(physical, "nodes");
        for(size_t i = 0; i < graph->nodesCount; i++)
            cJSON_AddItemToArray(pnodes, topologyNodeToJson(&graph->nodes[i]));

        cJSON *plinks = cJSON_AddArrayToObject(physical, "links");
        for(size_t i = 0; i < graph->linksCount; i++)
            cJSON_AddItemToArray(plinks, topologyLinkToJson(&graph->links[i], i));

        /* ─── Vista logica ──────────────────────────────── */
        cJSON *logical = buildLogicalView(graph);
        cJSON_AddItemToObject(root, "logical", logical);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* ============================================================
 * deviceDetailToJson
 *
 * Output per GET /api/devices/{chassisId}.
 * Dettaglio completo di un singolo nodo.
 * ============================================================ */

char *deviceDetailToJson(const TopologyNode *node) {
    if(!node) return errorToJson("not_found", "Device not found");
    cJSON *obj = topologyNodeToJson(node);
    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return out;
}

/* ============================================================
 * errorToJson
 * ============================================================ */

char *errorToJson(const char *code, const char *message) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "error", code ? code : "unknown");
    cJSON_AddStringToObject(obj, "message", message ? message : "");
    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return out;
}

/* ============================================================
 * healthToJson
 * ============================================================ */

char *healthToJson(void) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "status", "ok");
    cJSON_AddStringToObject(obj, "service", "uafx-discovery-client");
    cJSON_AddStringToObject(obj, "phase", "1");
    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return out;
}
