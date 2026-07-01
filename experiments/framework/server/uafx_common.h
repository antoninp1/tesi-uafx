#include <open62541/server.h>

/* Namespace helpers */
UA_QualifiedName qn(UA_UInt16 ns, const char *name);
UA_LocalizedText  lt(const char *text);
UA_UInt16 resolveNamespaceIndex(UA_Server *server, const char *uri);
UA_NodeId resolveChildByNameServer(UA_Server *server, UA_NodeId parentId, const char *name);

/* Construction de nœuds */
UA_NodeId addFolder(UA_Server *server, UA_NodeId parent, UA_UInt16 ns, const char *name);
UA_NodeId addBaseObject(UA_Server *server, UA_NodeId parent, UA_UInt16 ns,
                         const char *name, const char *description);
UA_NodeId addTypedObject(UA_Server *server, UA_NodeId parent, UA_UInt16 nsInstance,
                          const char *name, const char *description,
                          UA_UInt16 nsType, UA_UInt32 typeId);
UA_NodeId addStringVariable(UA_Server *server, UA_NodeId parent, UA_UInt16 ns,
                             const char *name, const char *value);
UA_NodeId addUInt32Variable(UA_Server *server, UA_NodeId parent, UA_UInt16 ns,
                             const char *name, UA_UInt32 value);
