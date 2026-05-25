/* ============================================================
 * cm_test.c
 *
 * Test end-to-end del connection_manager esterno.
 *
 * Setup multi-server reale:
 *   - edge-up-3 (192.168.17.73:4841): Publisher con AC
 *     "TemperatureSensor", FE "TemperatureReadingFE",
 *     OutputData/Temperature (variabile dinamica float).
 *
 *   - edge-up-4 (192.168.17.184:4841): Subscriber con AC
 *     "DensitySensor", FE "DensityReadingFE",
 *     InputData/Temperature (variabile target NodeId statico).
 *
 * Flusso:
 *   1. Discovery: NodeId di AC e variabile su entrambi i server
 *   2. Popola TopologyGraph con i due nodi
 *   3. Costruisce ConnectionRequest
 *   4. establishConnection(): chiama EstablishConnections su
 *      entrambi i server (Publisher poi Subscriber)
 *   5. Verifica empirica: tcpdump + UaExpert + lettura InputData
 * ============================================================ */

#include "open62541.h"
#include "connection_manager.h"
#include "model.h"
#include "browse.h"          /* resolveChildByName */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Configurazione del test
 * ============================================================ */

/* ── Publisher (edge-up-3) ── */
#define PUB_SERVER_URL  "opc.tcp://192.168.17.73:4841"
#define PUB_CHASSIS_ID  "00:07:32:ae:79:13"
#define PUB_NAME        "edge-up-3"
#define PUB_AC_NAME     "TemperatureSensor"
#define PUB_FE_NAME     "TemperatureReadingFE"
#define PUB_VAR_FOLDER  "OutputData"
#define PUB_VAR_NAME    "Temperature"

/* ── Subscriber (edge-up-4) ── */
#define SUB_SERVER_URL  "opc.tcp://192.168.17.184:4841"
#define SUB_CHASSIS_ID  "00:07:32:ae:79:1d"
#define SUB_NAME        "edge-up-4"
#define SUB_AC_NAME     "DensitySensor"
#define SUB_FE_NAME     "DensityReadingFE"
#define SUB_VAR_FOLDER  "InputData"
#define SUB_VAR_NAME    "Temperature"

/* ============================================================
 * Helper: discovery dei NodeId
 * ============================================================ */
typedef struct {
    UA_NodeId acNode;
    UA_NodeId varNode;
    bool      ok;
} DiscoveredIds;

static DiscoveredIds discoverServer(const char *url,
                                    const char *acName,
                                    const char *feName,
                                    const char *varFolder,
                                    const char *varName) {
    DiscoveredIds r;
    memset(&r, 0, sizeof(r));

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    printf("[TEST] Connecting to %s ...\n", url);
    if(UA_Client_connect(client, url) != UA_STATUSCODE_GOOD) {
        printf("[TEST]   Connection FAILED\n");
        UA_Client_delete(client);
        return r;
    }

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId fxRoot  = resolveChildByName(client, objects, "FxRoot");
    if(UA_NodeId_isNull(&fxRoot)) {
        printf("[TEST]   FxRoot not found\n");
        goto cleanup;
    }
   
    UA_NodeId acNode = resolveChildByName(client, fxRoot, acName);
    if(UA_NodeId_isNull(&acNode)) {
        printf("[TEST]   AC '%s' not found\n", acName);
        goto cleanup;
    }

    UA_NodeId feFolder = resolveChildByName(client, acNode, "FunctionalEntities");
    UA_NodeId feNode   = resolveChildByName(client, feFolder, feName);
    if(UA_NodeId_isNull(&feNode)) {
        printf("[TEST]   FE '%s' not found\n", feName);
        goto cleanup;
    }

    UA_NodeId folder = resolveChildByName(client, feNode, varFolder);
    UA_NodeId varNode = resolveChildByName(client, folder, varName);
    if(UA_NodeId_isNull(&varNode)) {
        printf("[TEST]   Variable '%s/%s' not found\n", varFolder, varName);
        goto cleanup;
    }

    UA_NodeId_copy(&acNode, &r.acNode);
    UA_NodeId_copy(&varNode, &r.varNode);
    r.ok = true;
    printf("[TEST]   AC '%s' = ns=%u;i=%u\n", acName,
           r.acNode.namespaceIndex, r.acNode.identifier.numeric);
    printf("[TEST]   Var '%s/%s' = ns=%u;i=%u\n", varFolder, varName,
           r.varNode.namespaceIndex, r.varNode.identifier.numeric);

cleanup:
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    return r;
}

/* ============================================================
 * MAIN
 * ============================================================ */
int main(void) {
    printf("\n══════════════════════════════════════════════════════\n");
    printf("  CM_TEST — Multi-server end-to-end\n");
    printf("  Pub (edge-up-3) → Sub (edge-up-4) via UAFX PubSub\n");
    printf("══════════════════════════════════════════════════════\n\n");

    /* ─── 1. Discovery ─── */
    printf("──────────── Discovery Publisher (edge-up-3) ────────────\n");
    DiscoveredIds pubIds = discoverServer(PUB_SERVER_URL,
        PUB_AC_NAME, PUB_FE_NAME, PUB_VAR_FOLDER, PUB_VAR_NAME);
    if(!pubIds.ok) {
        fprintf(stderr, "[TEST] FATAL: cannot discover Publisher\n");
        return EXIT_FAILURE;
    }

    printf("\n──────────── Discovery Subscriber (edge-up-4) ────────────\n");
    DiscoveredIds subIds = discoverServer(SUB_SERVER_URL,
        SUB_AC_NAME, SUB_FE_NAME, SUB_VAR_FOLDER, SUB_VAR_NAME);
    if(!subIds.ok) {
        fprintf(stderr, "[TEST] FATAL: cannot discover Subscriber\n");
        return EXIT_FAILURE;
    }

    /* ─── 2. Popola TopologyGraph ─── */
    printf("\n──────────── Setup TopologyGraph ────────────\n");
    TopologyGraph *graph = (TopologyGraph *)calloc(1, sizeof(TopologyGraph));
    graph->nodesCount = 2;

    TopologyNode *pub = &graph->nodes[0];
    memset(pub, 0, sizeof(*pub));
    strncpy(pub->id, PUB_CHASSIS_ID, MAX_STR_LEN - 1);
    strncpy(pub->name, PUB_NAME, MAX_STR_LEN - 1);
    strncpy(pub->endpointUrl, PUB_SERVER_URL, MAX_STR_LEN - 1);
    pub->type = NODE_TYPE_UAFX_SERVER;
    pub->reachable = true;

    TopologyNode *sub = &graph->nodes[1];
    memset(sub, 0, sizeof(*sub));
    strncpy(sub->id, SUB_CHASSIS_ID, MAX_STR_LEN - 1);
    strncpy(sub->name, SUB_NAME, MAX_STR_LEN - 1);
    strncpy(sub->endpointUrl, SUB_SERVER_URL, MAX_STR_LEN - 1);
    sub->type = NODE_TYPE_UAFX_SERVER;
    sub->reachable = true;

    printf("[TEST] Graph populated:\n");
    printf("[TEST]   [0] Pub: %s | %s\n", pub->name, pub->endpointUrl);
    printf("[TEST]   [1] Sub: %s | %s\n", sub->name, sub->endpointUrl);

    /* ─── 3. Costruisci ConnectionRequest ─── */
    printf("\n──────────── Build ConnectionRequest ────────────\n");
    ConnectionRequest req;
    memset(&req, 0, sizeof(req));

    /* Publisher (edge-up-3) */
    strncpy(req.publisherChassisId, PUB_CHASSIS_ID, sizeof(req.publisherChassisId) - 1);
    strncpy(req.publisherAcName,    PUB_AC_NAME,    sizeof(req.publisherAcName) - 1);
    strncpy(req.publisherFeName,    PUB_FE_NAME,    sizeof(req.publisherFeName) - 1);
    strncpy(req.publisherVariable,  PUB_VAR_NAME,   sizeof(req.publisherVariable) - 1);
    UA_NodeId_copy(&pubIds.acNode,  &req.publisherAcNodeId);
    UA_NodeId_copy(&pubIds.varNode, &req.publisherVariableNodeId);

    /* Subscriber (edge-up-4) */
    strncpy(req.subscriberChassisId, SUB_CHASSIS_ID, sizeof(req.subscriberChassisId) - 1);
    strncpy(req.subscriberAcName,    SUB_AC_NAME,    sizeof(req.subscriberAcName) - 1);
    strncpy(req.subscriberFeName,    SUB_FE_NAME,    sizeof(req.subscriberFeName) - 1);
    strncpy(req.subscriberVariable,  SUB_VAR_NAME,   sizeof(req.subscriberVariable) - 1);
    UA_NodeId_copy(&subIds.acNode,  &req.subscriberAcNodeId);
    UA_NodeId_copy(&subIds.varNode, &req.subscriberVariableNodeId);

    req.publishingInterval = 1000.0;
    strncpy(req.qosCategory, "BEST_EFFORT", sizeof(req.qosCategory) - 1);

    printf("[TEST] Request:\n");
    printf("[TEST]   Pub: %s/%s/%s @ %s\n",
           req.publisherAcName, req.publisherFeName,
           req.publisherVariable, pub->endpointUrl);
    printf("[TEST]   Sub: %s/%s/%s @ %s\n",
           req.subscriberAcName, req.subscriberFeName,
           req.subscriberVariable, sub->endpointUrl);
    printf("[TEST]   Interval: %.0f ms\n", req.publishingInterval);

    /* ─── 4. Invoca establishConnection ─── */
    printf("\n══════════════════════════════════════════════════════\n");
    printf("  establishConnection()\n");
    printf("══════════════════════════════════════════════════════\n");

    PubSubConnection conn;
    memset(&conn, 0, sizeof(conn));
    bool ok = establishConnection(graph, &req, &conn);

    printf("\n══════════════════════════════════════════════════════\n");
    if(ok) {
        printf("  ✓ TEST PASSED — Connection end-to-end\n");
        printf("══════════════════════════════════════════════════════\n");
        printf("[TEST] publisherId      = %u\n", conn.publisherId);
        printf("[TEST] writerGroupId    = %u\n", conn.writerGroupId);
        printf("[TEST] dataSetWriterId  = %u\n", conn.dataSetWriterId);
        printf("[TEST] multicast        = %s\n", conn.multicastUrl);
        printf("[TEST] interval         = %.0f ms\n", conn.publishingInterval);
        printf("\n[TEST] Verifica empirica:\n");
        printf("[TEST]   1) sudo tcpdump -i any -nn udp port 4840\n");
        printf("[TEST]      → pacchetti UDP da %s al multicast\n", pub->endpointUrl);
        printf("[TEST]   2) UaExpert su edge-up-3: CE_Pub_001 sotto\n");
        printf("[TEST]      %s/FunctionalEntities/%s/ConnectionEndpoints/\n",
               PUB_AC_NAME, PUB_FE_NAME);
        printf("[TEST]   3) UaExpert su edge-up-4: CE_Sub_001 sotto\n");
        printf("[TEST]      %s/FunctionalEntities/%s/ConnectionEndpoints/\n",
               SUB_AC_NAME, SUB_FE_NAME);
        printf("[TEST]   4) Leggi %s/%s/%s/%s su edge-up-4 — il valore deve\n",
               SUB_AC_NAME, SUB_FE_NAME, SUB_VAR_FOLDER, SUB_VAR_NAME);
        printf("[TEST]      seguire dinamicamente la Temperature di edge-up-3\n\n");
    } else {
        printf("  ✗ TEST FAILED\n");
        printf("══════════════════════════════════════════════════════\n\n");
    }

    /* ─── Cleanup ─── */
    UA_NodeId_clear(&pubIds.acNode);
    UA_NodeId_clear(&pubIds.varNode);
    UA_NodeId_clear(&subIds.acNode);
    UA_NodeId_clear(&subIds.varNode);
    UA_NodeId_clear(&req.publisherAcNodeId);
    UA_NodeId_clear(&req.publisherVariableNodeId);
    UA_NodeId_clear(&req.subscriberAcNodeId);
    UA_NodeId_clear(&req.subscriberVariableNodeId);
    free(graph);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
