/* ============================================================
 * http_server.c
 *
 * Implementazione del server HTTP con Mongoose.
 * Gestisce routing, CORS, serializzazione e dispatch verso
 * la logica di discovery/browse/TDE.
 * ============================================================ */

#include "http_server.h"
#include "serialize.h"
#include "mongoose.h"
#include "discovery.h"
#include "browse.h"
#include "tde.h"
#include "link_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
/* ============================================================
 * Stato globale del modulo
 * ============================================================ */

static struct mg_mgr g_mgr;
static TopologyGraph *g_graph = NULL;
static bool g_initialized = false;

/* ============================================================
 * Helper: costruisce gli header CORS standard
 * ============================================================ */

static const char *CORS_HEADERS =
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
    "Access-Control-Max-Age: 86400\r\n";

/* ============================================================
 * Helper: risposta JSON con free automatico del payload
 * ============================================================ */

static void replyJson(struct mg_connection *c, int status, char *json) {
    if(!json) {
        mg_http_reply(c, 500, CORS_HEADERS,
                      "{\"error\":\"internal\",\"message\":\"null payload\"}");
        return;
    }
    mg_http_reply(c, status, CORS_HEADERS, "%s", json);
    free(json);
}

/* ============================================================
 * Helper: risposta di errore
 * ============================================================ */

static void replyError(struct mg_connection *c, int status,
                       const char *code, const char *message) {
    char *json = errorToJson(code, message);
    replyJson(c, status, json);
}

/* ============================================================
 * Helper: risposta 501 per endpoint non ancora implementati
 * ============================================================ */

static void replyNotImplemented(struct mg_connection *c, const char *phase) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Endpoint planned for phase %s", phase);
    replyError(c, 501, "not_implemented", msg);
}

/* ============================================================
 * runFullDiscovery
 *
 * Esegue la pipeline completa di discovery sincrona:
 *   1. LDS → lista server UAFX
 *   2. Browse di ogni server → popola TopologyNode
 *   3. BFS via TDE sui vicini
 *   4. Link building
 *
 * Bloccante: puo' durare diversi secondi. Durante l'esecuzione
 * il server HTTP e' fermo (modello cooperativo).
 * ============================================================ */

static bool runFullDiscovery(TopologyGraph *graph, const char *gdsUrl) {
    printf("\n[HTTP] Discovery triggered via POST /api/discovery/run\n");

    /* Reset del grafo per partire puliti */
    topologyGraphClear(graph);
    topologyGraphInit(graph);

    /* ─── Fase 1: LDS ─────────────────────────────────── */
    DiscoveryList discovered;
    memset(&discovered, 0, sizeof(discovered));

    UA_StatusCode rc = runLdsDiscovery(&discovered, gdsUrl);
    if(rc != UA_STATUSCODE_GOOD || discovered.count == 0) {
        printf("[HTTP] LDS discovery failed or empty\n");
        return false;
    }
    printf("[HTTP] LDS: %zu servers discovered\n", discovered.count);

    /* ─── Fase 2: browse dei server UAFX ──────────────── */
    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    for(size_t i = 0; i < discovered.count; i++) {
        if(UA_Client_connect(client, discovered.urls[i]) != UA_STATUSCODE_GOOD) {
            printf("[HTTP]   Skip %s (connect failed)\n", discovered.urls[i]);
            UA_Client_disconnect(client);
            continue;
        }

        TopologyNode node;
        memset(&node, 0, sizeof(node));
        node.type = NODE_TYPE_UAFX_SERVER;
        browseServerUAFX(client, discovered.urls[i], &node);
        node.visited = true;

        if(node.id[0] == '\0' && node.applicationUri[0])
            strncpy(node.id, node.applicationUri, MAX_STR_LEN - 1);

        topologyAddNode(graph, &node);

        /* Accoda vicini LLDP per il BFS */
        for(size_t k = 0; k < node.interfacesCount; k++) {
            const NetworkInterface *iface = &node.interfaces[k];
            for(size_t n = 0; n < iface->neighborsCount; n++) {
                const LldpNeighbor *nbr = &iface->neighbors[n];
                if(nbr->chassisId[0] == '\0') continue;

                DiscoveryQueueEntry entry;
                memset(&entry, 0, sizeof(entry));
                strncpy(entry.chassisId,   nbr->chassisId,          MAX_STR_LEN - 1);
                strncpy(entry.mgmtAddress, nbr->mgmtAddress,        MAX_STR_LEN - 1);
                strncpy(entry.sysName,     nbr->sysName,            MAX_STR_LEN - 1);
                strncpy(entry.capabilities,nbr->systemCapabilities, MAX_STR_LEN - 1);
                discoveryEnqueue(graph, &entry);
            }
        }

        UA_Client_disconnect(client);
    }
    UA_Client_delete(client);

    /* ─── Fase 3: BFS via TDE ─────────────────────────── */
    DiscoveryQueueEntry entry;
    while(discoveryDequeue(graph, &entry)) {
        if(discoveryIsVisited(graph, entry.chassisId)) continue;

        TdeQueryResult tdeResult;
        if(!tdeQueryDevice(&entry, &tdeResult)) {
            /* Phantom per dispositivi non raggiungibili via TDE */
            TopologyNode phantom;
            memset(&phantom, 0, sizeof(phantom));
            strncpy(phantom.id,          entry.chassisId,   MAX_STR_LEN - 1);
            strncpy(phantom.name,        entry.sysName,     MAX_STR_LEN - 1);
            strncpy(phantom.mgmtAddress, entry.mgmtAddress, MAX_STR_LEN - 1);
            phantom.type = NODE_TYPE_PHANTOM_SWITCH;
            phantom.reachable = false;
            phantom.visited = true;
            topologyAddNode(graph, &phantom);
            continue;
        }

        TopologyNode tdeNode;
        memset(&tdeNode, 0, sizeof(tdeNode));
        tdeApplyResultToNode(&tdeResult, &tdeNode);
        topologyAddNode(graph, &tdeNode);

        /* Accoda nuovi vicini scoperti dalla TDE */
        for(size_t k = 0; k < tdeResult.neighborsCount; k++) {
            const TdeNeighborInfo *nbr = &tdeResult.neighbors[k];
            if(nbr->chassisId[0] == '\0') continue;

            DiscoveryQueueEntry newEntry;
            memset(&newEntry, 0, sizeof(newEntry));
            strncpy(newEntry.chassisId,   nbr->chassisId,          MAX_STR_LEN - 1);
            strncpy(newEntry.mgmtAddress, nbr->mgmtAddress,        MAX_STR_LEN - 1);
            strncpy(newEntry.sysName,     nbr->sysName,            MAX_STR_LEN - 1);
            strncpy(newEntry.capabilities,nbr->systemCapabilities, MAX_STR_LEN - 1);
            discoveryEnqueue(graph, &newEntry);
        }
    }

    /* ─── Fase 4: Link building ───────────────────────── */
    buildLinksFromNeighbors(graph);

    graph->lastScanTime = time(NULL);

    printf("[HTTP] Discovery complete: %zu nodes, %zu links\n",
           graph->nodesCount, graph->linksCount);
    return true;
}

/*Creazione PubSub Dinamica*/
bool triggerServerStart(const char *endpointUrl, const char *methodName, const char *acName) {
    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    UA_StatusCode rc = UA_Client_connect(client, endpointUrl);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[CM] Cannot connect to %s: %s\n",
               endpointUrl, UA_StatusCode_name(rc));
        UA_Client_delete(client);
        return false;
    }

    /* naviga: Objects → FxRoot → TemperatureSensor → metodo */
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId fxRoot        = resolveChildByName(client, objectsFolder, "FxRoot");
    UA_NodeId acNode        = resolveChildByName(client, fxRoot, acName);
    UA_NodeId methodNodeId  = resolveChildByName(client, acNode, methodName);

    if(UA_NodeId_isNull(&methodNodeId)) {
        printf("[CM] Method '%s' not found\n", methodName);
        return false;
    }

    /* chiama il metodo senza argomenti */
    size_t outputSize = 0;
    UA_Variant *output = NULL;
    rc = UA_Client_call(client,
                        acNode, methodNodeId,
                        0, NULL,
                        &outputSize, &output);

    printf("[CM] %s on %s: %s\n",
           methodName, endpointUrl, UA_StatusCode_name(rc));

    UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    return rc == UA_STATUSCODE_GOOD;
}

/* ============================================================
 * Handler degli endpoint
 * ============================================================ */

static void handleHealth(struct mg_connection *c) {
    replyJson(c, 200, healthToJson());
}

static void handleGetTopology(struct mg_connection *c) {
    replyJson(c, 200, topologyGraphToJson(g_graph));
}



static void handleRunDiscovery(struct mg_connection *c,
                               struct mg_http_message *hm) {
    /* Default se il body e' vuoto o invalido */
    char gdsUrl[256];
    snprintf(gdsUrl, sizeof(gdsUrl), "%s", LDS_URL);

    /* Parse body JSON se presente */
    if(hm->body.len > 0) {
        /* Copia il body in una stringa null-terminated per cJSON */
        char *bodyStr = malloc(hm->body.len + 1);
        if(!bodyStr) {
            replyError(c, 500, "internal", "malloc failed");
            return;
        }
        memcpy(bodyStr, hm->body.buf, hm->body.len);
        bodyStr[hm->body.len] = '\0';

        cJSON *root = cJSON_Parse(bodyStr);
        free(bodyStr);

        if(!root) {
            replyError(c, 400, "bad_request", "Invalid JSON in request body");
            return;
        }

        cJSON *hostNode = cJSON_GetObjectItem(root, "gdsHost");
        cJSON *portNode = cJSON_GetObjectItem(root, "gdsPort");

        const char *host = NULL;
        int port = 4840;  /* default OPC UA */

        if(hostNode && cJSON_IsString(hostNode))
            host = hostNode->valuestring;

        if(portNode) {
            if(cJSON_IsNumber(portNode))
                port = portNode->valueint;
            else if(cJSON_IsString(portNode))
                port = atoi(portNode->valuestring);
        }

        if(!host || host[0] == '\0') {
            cJSON_Delete(root);
            replyError(c, 400, "bad_request",
                       "Missing or empty 'gdsHost' field");
            return;
        }

        if(port <= 0 || port > 65535) {
            cJSON_Delete(root);
            replyError(c, 400, "bad_request",
                       "Invalid 'gdsPort' (must be 1-65535)");
            return;
        }

        snprintf(gdsUrl, sizeof(gdsUrl), "opc.tcp://%s:%d", host, port);
        cJSON_Delete(root);
    }

    /* Esegui la discovery con l'URL costruito (o il default) */
    if(!runFullDiscovery(g_graph, gdsUrl)) {
        replyError(c, 500, "discovery_failed",
                   "Unable to complete discovery, check GDS connectivity");
        return;
    }

    replyJson(c, 200, topologyGraphToJson(g_graph));
}

static void handleGetDevice(struct mg_connection *c, struct mg_str uri) {
    /* Estrai chassisId dopo "/api/devices/" */
    const char *prefix = "/api/devices/";
    size_t prefixLen = strlen(prefix);
    if(uri.len <= prefixLen) {
        replyError(c, 400, "bad_request", "Missing chassisId");
        return;
    }

    char chassisId[MAX_STR_LEN];
    size_t copyLen = uri.len - prefixLen;
    if(copyLen >= MAX_STR_LEN) copyLen = MAX_STR_LEN - 1;
    memcpy(chassisId, uri.buf + prefixLen, copyLen);
    chassisId[copyLen] = '\0';

    /* Gestione eventuali sotto-path tipo /api/devices/xxx/functional-entities */
    char *slash = strchr(chassisId, '/');
    if(slash) *slash = '\0';

    int idx = topologyFindNodeByChassisId(g_graph, chassisId);
    if(idx < 0) {
        replyError(c, 404, "not_found", "Device not in current graph");
        return;
    }

    replyJson(c, 200, deviceDetailToJson(&g_graph->nodes[idx]));
}

/* ============================================================
 * Routing principale
 * ============================================================ */

static void routeRequest(struct mg_connection *c, struct mg_http_message *hm) {
    struct mg_str method = hm->method;
    struct mg_str uri    = hm->uri;

    /* ─── CORS preflight ──────────────────────────────── */
    if(mg_strcmp(method, mg_str("OPTIONS")) == 0) {
        mg_http_reply(c, 204, CORS_HEADERS, "");
        return;
    }

    /* ─── GET /api/health ─────────────────────────────── */
    if(mg_strcmp(method, mg_str("GET")) == 0 &&
       mg_match(uri, mg_str("/api/health"), NULL)) {
        handleHealth(c);
        return;
    }

    /* ─── GET /api/topology ───────────────────────────── */
    if(mg_strcmp(method, mg_str("GET")) == 0 &&
       mg_match(uri, mg_str("/api/topology"), NULL)) {
        handleGetTopology(c);
        return;
    }

    /* ─── POST /api/discovery/run ─────────────────────── */
 
    if(mg_strcmp(method, mg_str("POST")) == 0 &&
       mg_match(uri, mg_str("/api/discovery/run"), NULL)) {
        handleRunDiscovery(c, hm);   /* ← passa anche hm */
        return;
    }

    /* ─── GET /api/devices/{id} ──────────────────────────── */
    if(mg_strcmp(method, mg_str("GET")) == 0 &&
       mg_match(uri, mg_str("/api/devices/#"), NULL)) {
        /* Check se e' un sotto-path speciale */
        if(mg_match(uri, mg_str("/api/devices/*/functional-entities"), NULL)) {
            replyNotImplemented(c, "2 (PubSub)");
            return;
        }
        handleGetDevice(c, uri);
        return;
    }

    /* ─── Fase 2: PubSub connections ──────────────────── */
    if(mg_match(uri, mg_str("/api/connections/test"), NULL)){
        triggerServerStart("opc.tcp://192.168.17.73:4841", "StartPublisher", "TemperatureSensor");
	    triggerServerStart("opc.tcp://192.168.17.184:4841", "StartSubscriber",  "DensitySensor");
        return;
    }

    /* ─── Fase 3: TSN configuration ───────────────────── */
    if(mg_match(uri, mg_str("/api/tsn/#"), NULL)) {
        replyNotImplemented(c, "3 (TSN)");
        return;
    }

    /* ─── 404 ─────────────────────────────────────────── */
    replyError(c, 404, "not_found", "Unknown endpoint");
}

/* ============================================================
 * Event handler di Mongoose
 * ============================================================ */

static void eventHandler(struct mg_connection *c, int ev, void *ev_data) {
    if(ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        printf("[HTTP] %.*s %.*s\n",
               (int)hm->method.len, hm->method.buf,
               (int)hm->uri.len,    hm->uri.buf);

        routeRequest(c, hm);
    }
}

/* ============================================================
 * API pubblica
 * ============================================================ */

bool httpServerInit(int port, TopologyGraph *graph) {
    if(g_initialized) {
        printf("[HTTP] Already initialized\n");
        return false;
    }

    g_graph = graph;
    mg_mgr_init(&g_mgr);

    char listenUrl[64];
    snprintf(listenUrl, sizeof(listenUrl), "http://0.0.0.0:%d", port);

    struct mg_connection *c = mg_http_listen(&g_mgr, listenUrl, eventHandler, NULL);
    if(!c) {
        printf("[HTTP] Failed to bind on port %d\n", port);
        mg_mgr_free(&g_mgr);
        return false;
    }

    g_initialized = true;
    printf("[HTTP] Server listening on %s\n", listenUrl);
    printf("[HTTP] Endpoints:\n");
    printf("         GET  /api/health\n");
    printf("         GET  /api/topology\n");
    printf("         POST /api/discovery/run\n");
    printf("         GET  /api/devices/{chassisId}\n");
    printf("         *    /api/connections/*  (501, fase 2)\n");
    printf("         *    /api/tsn/*          (501, fase 3)\n");
    return true;
}

void httpServerPoll(int timeoutMs) {
    if(!g_initialized) return;
    mg_mgr_poll(&g_mgr, timeoutMs);
}

void httpServerShutdown(void) {
    if(!g_initialized) return;
    mg_mgr_free(&g_mgr);
    g_initialized = false;
    printf("[HTTP] Server stopped\n");
}
