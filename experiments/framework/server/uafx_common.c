#include "uafx_common.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════
 * Helper Functions
 * ═══════════════════════════════════════════════════════════ */

UA_QualifiedName qn(UA_UInt16 ns, const char *name) {
    return UA_QUALIFIEDNAME(ns, (char *)name);
}

UA_LocalizedText lt(const char *text) {
    return UA_LOCALIZEDTEXT("en-US", (char *)text);
}

UA_NodeId addFolder(UA_Server *server, UA_NodeId parent,
                            UA_UInt16 ns, const char *name) {
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = lt(name);

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addObjectNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), attr, NULL, &newNode);

    return newNode;
}

/* Aggiunge un oggetto con typeDefinition esplicito (UAFX typed) */
UA_NodeId addTypedObject(UA_Server *server, UA_NodeId parent,
                                UA_UInt16 nsInstance, const char *name,
                                const char *description,
                                UA_UInt16 nsType, UA_UInt32 typeId) {
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = lt(name);
    attr.description = lt(description);

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addObjectNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qn(nsInstance, name),
        UA_NODEID_NUMERIC(nsType, typeId), attr, NULL, &newNode);

    return newNode;
}

/* Aggiunge un oggetto con BaseObjectType */
UA_NodeId addBaseObject(UA_Server *server, UA_NodeId parent,
                               UA_UInt16 ns, const char *name,
                               const char *description) {
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = lt(name);
    attr.description = lt(description);

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addObjectNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), attr, NULL, &newNode);

    return newNode;
}

UA_NodeId addStringVariable(UA_Server *server, UA_NodeId parent,
                                    UA_UInt16 ns, const char *name, const char *value) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = lt(name);
    UA_String val = UA_STRING((char *)value);
    UA_Variant_setScalar(&attr.value, &val, &UA_TYPES[UA_TYPES_STRING]);
    attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addVariableNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), attr, NULL, &newNode);

    return newNode;
}

UA_NodeId addUInt32Variable(UA_Server *server, UA_NodeId parent,
                                    UA_UInt16 ns, const char *name, UA_UInt32 value) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = lt(name);
    UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_UINT32]);
    attr.dataType = UA_TYPES[UA_TYPES_UINT32].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addVariableNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, NULL, &newNode);

    return newNode;
}

UA_NodeId resolveChildByNameServer(UA_Server *server,
                                           UA_NodeId parentId,
                                           const char *name) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = parentId;
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    bd.includeSubtypes = true;
    bd.resultMask = UA_BROWSERESULTMASK_BROWSENAME;

    UA_BrowseResult br = UA_Server_browse(server, 0, &bd);
    UA_NodeId result = UA_NODEID_NULL;

    for(size_t i = 0; i < br.referencesSize; i++) {
        UA_ReferenceDescription *ref = &br.references[i];
        if(ref->browseName.name.length == strlen(name) &&
           memcmp(ref->browseName.name.data, name, strlen(name)) == 0) {
            UA_NodeId_copy(&ref->nodeId.nodeId, &result);
            break;
        }
    }
    UA_BrowseResult_clear(&br);
    return result;
}


/* ═══════════════════════════════════════════════════════════
 * Risolve il namespace index per una URI data
 * ═══════════════════════════════════════════════════════════ */
UA_UInt16 resolveNamespaceIndex(UA_Server *server, const char *uri) {
    UA_Variant nsArrayVar;
    UA_Variant_init(&nsArrayVar);

    UA_NodeId nsArrayId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_NAMESPACEARRAY);
    UA_StatusCode rc = UA_Server_readValue(server, nsArrayId, &nsArrayVar);
    if(rc != UA_STATUSCODE_GOOD || !UA_Variant_hasArrayType(&nsArrayVar, &UA_TYPES[UA_TYPES_STRING])) {
        UA_Variant_clear(&nsArrayVar);
        printf("[SERVER] WARNING: could not read NamespaceArray, defaulting ns=1\n");
        return 1;
    }

    UA_String *nsArray = (UA_String *)nsArrayVar.data;
    UA_UInt16 found = 0;
    for(size_t i = 0; i < nsArrayVar.arrayLength; i++) {
        UA_String target = UA_STRING((char *)uri);
        if(UA_String_equal(&nsArray[i], &target)) {
            found = (UA_UInt16)i;
            break;
        }
    }

    UA_Variant_clear(&nsArrayVar);
    return found;
}