/* ============================================================
 * helpers.c
 *
 * Implementazione funzioni helper per browse e lettura
 * proprietà OPC UA.
 * ============================================================ */

#include "helpers.h"

/* ============================================================
 * printSeparator
 * ============================================================ */

void printSeparator(const char *title) {
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
 * readStringProperty
 * ============================================================ */

char *readStringProperty(UA_Client *client, UA_NodeId parentNode,
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
 * readUInt32Property
 * ============================================================ */

UA_Boolean readUInt32Property(UA_Client *client, UA_NodeId parentNode,
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
 * browseNode
 * ============================================================ */

UA_BrowseResponse browseNode(UA_Client *client, UA_NodeId nodeId,
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
