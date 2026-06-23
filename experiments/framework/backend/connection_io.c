/* ============================================================
 * connection_io.c
 * ============================================================ */

#include "connection_io.h"
#include "cJSON.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * connectionRequestFromJson
 * ============================================================ */
bool connectionRequestFromJson(const char *bodyStr, ConnectionRequest *out) {
    if(!bodyStr || !out) return false;

    cJSON *root = cJSON_Parse(bodyStr);
    if(!root) return false;

    memset(out, 0, sizeof(ConnectionRequest));

    cJSON *pub = cJSON_GetObjectItem(root, "publisher");
    cJSON *sub = cJSON_GetObjectItem(root, "subscriber");
    cJSON *ps  = cJSON_GetObjectItem(root, "pubsub");

    if(!pub || !sub || !ps) {
        cJSON_Delete(root);
        return false;
    }

    /* ── Publisher ─────────────────────────────────────────── */
    cJSON *v;
    if((v = cJSON_GetObjectItem(pub, "endpointUrl"))    && cJSON_IsString(v))
        safeStrCopy(out->publisherEndpointUrl,    v->valuestring, sizeof(out->publisherEndpointUrl));
    if((v = cJSON_GetObjectItem(pub, "acNodeId"))       && cJSON_IsString(v))
        safeStrCopy(out->publisherAcNodeId,       v->valuestring, sizeof(out->publisherAcNodeId));
    if((v = cJSON_GetObjectItem(pub, "acName"))         && cJSON_IsString(v))
        safeStrCopy(out->publisherAcName,         v->valuestring, sizeof(out->publisherAcName));
    if((v = cJSON_GetObjectItem(pub, "feNodeId"))       && cJSON_IsString(v))
        safeStrCopy(out->publisherFeNodeId,       v->valuestring, sizeof(out->publisherFeNodeId));
    if((v = cJSON_GetObjectItem(pub, "feName"))         && cJSON_IsString(v))
        safeStrCopy(out->publisherFeName,         v->valuestring, sizeof(out->publisherFeName));
    if((v = cJSON_GetObjectItem(pub, "variableNodeId")) && cJSON_IsString(v))
        safeStrCopy(out->publisherVariableNodeId, v->valuestring, sizeof(out->publisherVariableNodeId));
    if((v = cJSON_GetObjectItem(pub, "variableName"))   && cJSON_IsString(v))
        safeStrCopy(out->publisherVariableName,   v->valuestring, sizeof(out->publisherVariableName));

    /* ── Subscriber ────────────────────────────────────────── */
    if((v = cJSON_GetObjectItem(sub, "endpointUrl"))    && cJSON_IsString(v))
        safeStrCopy(out->subscriberEndpointUrl,    v->valuestring, sizeof(out->subscriberEndpointUrl));
    if((v = cJSON_GetObjectItem(sub, "acNodeId"))       && cJSON_IsString(v))
        safeStrCopy(out->subscriberAcNodeId,       v->valuestring, sizeof(out->subscriberAcNodeId));
    if((v = cJSON_GetObjectItem(sub, "acName"))         && cJSON_IsString(v))
        safeStrCopy(out->subscriberAcName,         v->valuestring, sizeof(out->subscriberAcName));
    if((v = cJSON_GetObjectItem(sub, "feNodeId"))       && cJSON_IsString(v))
        safeStrCopy(out->subscriberFeNodeId,       v->valuestring, sizeof(out->subscriberFeNodeId));
    if((v = cJSON_GetObjectItem(sub, "feName"))         && cJSON_IsString(v))
        safeStrCopy(out->subscriberFeName,         v->valuestring, sizeof(out->subscriberFeName));
    if((v = cJSON_GetObjectItem(sub, "variableNodeId")) && cJSON_IsString(v))
        safeStrCopy(out->subscriberVariableNodeId, v->valuestring, sizeof(out->subscriberVariableNodeId));
    if((v = cJSON_GetObjectItem(sub, "variableName"))   && cJSON_IsString(v))
        safeStrCopy(out->subscriberVariableName,   v->valuestring, sizeof(out->subscriberVariableName));

    /* ── PubSub params ─────────────────────────────────────── */
    if((v = cJSON_GetObjectItem(ps, "publishingInterval"))    && cJSON_IsNumber(v))
        out->publishingInterval    = v->valuedouble;
    if((v = cJSON_GetObjectItem(ps, "messageReceiveTimeout")) && cJSON_IsNumber(v))
        out->messageReceiveTimeout = v->valuedouble;
    if((v = cJSON_GetObjectItem(ps, "address"))               && cJSON_IsString(v))
        safeStrCopy(out->address,     v->valuestring, sizeof(out->address));
    if((v = cJSON_GetObjectItem(ps, "qosCategory"))           && cJSON_IsString(v))
        safeStrCopy(out->qosCategory, v->valuestring, sizeof(out->qosCategory));

    /* Validazione campi obbligatori */
    if(out->publisherEndpointUrl[0]    == '\0' ||
       out->subscriberEndpointUrl[0]   == '\0' ||
       out->publisherVariableNodeId[0] == '\0' ||
       out->subscriberVariableNodeId[0]== '\0' ||
       out->address[0]                 == '\0') {
        cJSON_Delete(root);
        return false;
    }

    cJSON_Delete(root);
    return true;
}

/* ============================================================
 * Helper: ConnectionEndpoint → cJSON object
 * ============================================================ */
static cJSON *connectionEndpointToJson(const ConnectionEndpoint *ce) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "name",               ce->name);
    cJSON_AddStringToObject(obj, "nodeId",             ce->nodeId);
    cJSON_AddNumberToObject(obj, "mode",               ce->mode);
    cJSON_AddStringToObject(obj, "status",             ce->status);
    cJSON_AddBoolToObject  (obj, "isPersistent",       ce->isPersistent);
    cJSON_AddStringToObject(obj, "relatedEndpoint",    ce->relatedEndpoint);
    cJSON_AddStringToObject(obj, "linkedVariable",     ce->linkedVariable);
    cJSON_AddStringToObject(obj, "dataSetWriterRef",   ce->dataSetWriterRef);
    cJSON_AddStringToObject(obj, "dataSetReaderRef",   ce->dataSetReaderRef);
    return obj;
}

/* ============================================================
 * connectionResponseToJson
 * ============================================================ */
char *connectionResponseToJson(const ConnectionResponse *resp) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", resp->success);

    if(!resp->success) {
        cJSON_AddStringToObject(root, "error", resp->errorMessage);
    } else {
        const PubSubConnection *conn = &resp->connection;

        cJSON *connObj = cJSON_AddObjectToObject(root, "connection");

        /* Parametri PubSub */
        cJSON_AddNumberToObject(connObj, "publisherId",       conn->publisherId);
        cJSON_AddNumberToObject(connObj, "writerGroupId",     conn->writerGroupId);
        cJSON_AddNumberToObject(connObj, "dataSetWriterId",   conn->dataSetWriterId);
        cJSON_AddStringToObject(connObj, "multicastUrl",      conn->multicastUrl);
        cJSON_AddNumberToObject(connObj, "publishingInterval",conn->publishingInterval);

        /* ConnectionEndpoint Publisher */
        cJSON_AddItemToObject(connObj, "publisher",
                              connectionEndpointToJson(&conn->pub));

        /* ConnectionEndpoint Subscriber */
        cJSON_AddItemToObject(connObj, "subscriber",
                              connectionEndpointToJson(&conn->sub));
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}