/* ============================================================
 * uafx_temperature_server.c  (Kathara / live-LLDP edition)
 *
 * Server OPC UA FX con tipi UAFX corretti:
 *   - AutomationComponent istanziato come AutomationComponentType (ns=FX/AC; i=2)
 *   - Asset istanziato come FxAssetType                           (ns=FX/AC; i=3)
 *   - FunctionalEntity istanziata come FunctionalEntityType       (ns=FX/AC; i=4)
 *
 * Differenze rispetto alla versione "hardcoded":
 *   - Identità (applicationUri, endpointUrl, porta, nome, LDS, iface PubSub)
 *     parametrizzata via variabili d'ambiente -> ogni istanza e' univoca.
 *   - ChassisId locale derivato dal MAC reale dell'interfaccia
 *     (/sys/class/net/<if>/address) -> niente phantom node.
 *   - RemoteSystemsData popolata leggendo i vicini REALI da
 *     `lldpcli show neighbors -f json` -> la topologia emerge dalla rete
 *     (es. dai collision domain di Kathara), non e' piu' cablata nel sorgente.
 *
 * Variabili d'ambiente riconosciute (con default):
 *   UAFX_APP_URI      urn:kathara:uafx:node
 *   UAFX_APP_NAME     UAFX Kathara Node
 *   UAFX_PUBLIC_URL   opc.tcp://0.0.0.0:4841
 *   UAFX_LDS_URL      opc.tcp://127.0.0.1:4840
 *   UAFX_PORT         4841
 *   UAFX_SYS_NAME     uafx-node
 *   UAFX_MGMT_ADDR    0.0.0.0
 *   UAFX_PUBSUB_IFACE eth0
 *
 * Compilazione:
 *   gcc -o temp_server uafx_temperature_server.c my_uafx_types.c \
 *       cJSON.c open62541.c -pthread
 * ============================================================ */

#include "open62541.h"
#include "types_di_generated.h"
#include "types_uafx_data_generated.h"
#include "types_uafx_ac_generated.h"
#include "my_uafx_model.h"
//#include "establish_connection2.h"
#include "establish_connection.h"
#include "cJSON.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

/* ─── Namespace index di FX/AC nel server ─────────────────── */
#define FXAC_NS_URI   "http://opcfoundation.org/UA/FX/AC/"

/* NodeId dei tipi UAFX (numeric id fisso da nodeset XML) */
#define FXAC_ID_AUTOMATIONCOMPONENTTYPE  2
#define FXAC_ID_FXASSETTYPE              3
#define FXAC_ID_FUNCTIONALENTITYTYPE     4

#define NS_LOCAL 1

static UA_NodeId connectionIdent, publishedDataSetIdent, writerGroupIdent,
    dataSetWriterIdent;
static UA_String transportProfile;
static UA_NetworkAddressUrlDataType networkAddressUrl;
static volatile UA_Boolean running = true;
static UA_NodeId temperatureNodeId = {0};

static void stopHandler(int sig) {
    printf("\n[SERVER] Shutdown signal received\n");
    running = false;
}

/* ═══════════════════════════════════════════════════════════
 * Helper: ambiente e introspezione di sistema
 * ═══════════════════════════════════════════════════════════ */

/* env var con default se assente o vuota */
static const char *env_or(const char *key, const char *fallback) {
    const char *v = getenv(key);
    return (v && *v) ? v : fallback;
}

/* Legge il MAC reale di un'interfaccia da /sys/class/net/<if>/address.
 * Ritorna 1 e riempie out (formato "aa:bb:cc:dd:ee:ff") se ok. */
static int read_iface_mac(const char *ifname, char *out, size_t outlen) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifname);
    FILE *f = fopen(path, "r");
    if(!f) return 0;
    char buf[64] = {0};
    if(!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    if(buf[0] == 0) return 0;
    snprintf(out, outlen, "%s", buf);
    return 1;
}

/* Legge lo stato operativo di un'interfaccia: "up"/"down"/"unknown" */
static void read_iface_operstate(const char *ifname, char *out, size_t outlen) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifname);
    snprintf(out, outlen, "%s", "unknown");
    FILE *f = fopen(path, "r");
    if(!f) return;
    char buf[32] = {0};
    if(fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        snprintf(out, outlen, "%s", buf);
    }
    fclose(f);
}

/* ═══════════════════════════════════════════════════════════
 * Risolve il namespace index per una URI data
 * ═══════════════════════════════════════════════════════════ */
static UA_UInt16 resolveNamespaceIndex(UA_Server *server, const char *uri) {
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

/* ═══════════════════════════════════════════════════════════
 * Helper Functions
 * ═══════════════════════════════════════════════════════════ */

static UA_QualifiedName qn(UA_UInt16 ns, const char *name) {
    return UA_QUALIFIEDNAME(ns, (char *)name);
}

static UA_LocalizedText lt(const char *text) {
    return UA_LOCALIZEDTEXT("en-US", (char *)text);
}

static UA_NodeId addFolder(UA_Server *server, UA_NodeId parent,
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
static UA_NodeId addTypedObject(UA_Server *server, UA_NodeId parent,
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
static UA_NodeId addBaseObject(UA_Server *server, UA_NodeId parent,
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

static UA_NodeId addStringVariable(UA_Server *server, UA_NodeId parent,
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

static UA_NodeId addUInt32Variable(UA_Server *server, UA_NodeId parent,
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

static UA_NodeId resolveChildByNameServer(UA_Server *server,
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
 * Temperature Variable with Dynamic Callback
 * ═══════════════════════════════════════════════════════════ */

static void readTemperature(UA_Server *server, const UA_NodeId *sessionId,
                            void *sessionContext, const UA_NodeId *nodeId,
                            void *nodeContext, const UA_NumericRange *range,
                            const UA_DataValue *data) {
    UA_Float temperature = 20.0f + ((rand() % 1000) - 500) / 100.0f;
    UA_Variant value;
    UA_Variant_setScalar(&value, &temperature, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, *nodeId, value);
}

static UA_NodeId addTemperatureVariable(UA_Server *server, UA_NodeId parent,
                                        UA_UInt16 ns, const char *name) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = lt(name);
    attr.description = lt("Current temperature reading in degrees Celsius");

    UA_Float initialValue = 20.0f;
    UA_Variant_setScalar(&attr.value, &initialValue, &UA_TYPES[UA_TYPES_FLOAT]);
    attr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addVariableNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, NULL, &newNode);

    UA_ValueCallback callback;
    callback.onRead  = readTemperature;
    callback.onWrite = NULL;
    UA_Server_setVariableNode_valueCallback(server, newNode, callback);

    addStringVariable(server, newNode, ns, "EngineeringUnits", "\xC2\xB0""C");

    return newNode;
}

/* ═══════════════════════════════════════════════════════════
 * Build NetworkInterfaces with LIVE LLDP data
 *
 * Secondo OPC 10000-82 sezione 6.5.2 / 7.3.2:
 * - NetworkInterfaces/ folder sotto Objects
 * - Ogni interfaccia fisica come oggetto con AdminStatus/OperStatus/PhysAddress/Speed
 * - LldpData/ con LocalSystemData (questo nodo) e RemoteSystemsData (vicini reali)
 *
 * A differenza della versione precedente, i vicini NON sono cablati nel codice:
 * vengono letti a runtime da `lldpcli show neighbors -f json`, quindi la
 * topologia rispecchia la rete reale (collision domain di Kathara).
 * ═══════════════════════════════════════════════════════════ */

static void addLocalSystemData(UA_Server *server, UA_NodeId parent,
                               const char *chassisId, const char *sysName,
                               const char *mgmtAddr,
                               const char *portId, UA_UInt32 portIdSubtype) {
    UA_NodeId local = addBaseObject(server, parent, NS_LOCAL,
                                    "LocalSystemData", "LLDP Local System");
    addStringVariable(server, local, NS_LOCAL, "ChassisId",          chassisId);
    addUInt32Variable(server, local, NS_LOCAL, "ChassisIdSubtype",   4);
    addStringVariable(server, local, NS_LOCAL, "SysName",            sysName);
    addStringVariable(server, local, NS_LOCAL, "SysDescr",           "Kathara UAFX test node");
    addStringVariable(server, local, NS_LOCAL, "MgmtAddress",        mgmtAddr);
    addStringVariable(server, local, NS_LOCAL, "SystemCapabilities", "Bridge,Router");
    addStringVariable(server, local, NS_LOCAL, "PortId",             portId);
    addUInt32Variable(server, local, NS_LOCAL, "PortIdSubtype",      portIdSubtype);
}

/* Estrae un valore "value" da un nodo lldpcli che puo' essere
 * { "value": "x" }, direttamente "x", oppure [ {...} ]. */
static const char *lldp_str(const cJSON *node) {
    if(!node) return NULL;
    if(cJSON_IsString(node)) return node->valuestring;
    if(cJSON_IsArray(node) && cJSON_GetArraySize(node) > 0)
        node = cJSON_GetArrayItem(node, 0);
    const cJSON *v = cJSON_GetObjectItem(node, "value");
    if(cJSON_IsString(v)) return v->valuestring;
    return NULL;
}

/* Aggiunge i RemoteSystem_N per una interfaccia, leggendo da lldpcli JSON.
 * Ritorna il numero di vicini aggiunti.
 *
 * NOTA: l'annidamento JSON di lldpcli usa chiavi dinamiche (nome interfaccia
 * e SysName del vicino come chiavi). Il parsing qui e' difensivo; se la tua
 * versione di lldpd usa una struttura diversa (es. -f json0), va adattato sul
 * JSON reale prodotto nel container. */
static int addRemoteSystemsFromLldp(UA_Server *server, UA_NodeId rsFolder,
                                    const char *ifname) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "lldpcli show neighbors ports %s -f json 2>/dev/null", ifname);
    FILE *fp = popen(cmd, "r");
    if(!fp) return 0;

    char *buf = NULL; size_t cap = 0, len = 0; int c;
    while((c = fgetc(fp)) != EOF) {
        if(len + 1 >= cap) {
            cap = cap ? cap * 2 : 4096;
            char *nb = (char *)realloc(buf, cap);
            if(!nb) { free(buf); pclose(fp); return 0; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    pclose(fp);
    if(!buf || len == 0) { free(buf); return 0; }
    buf[len] = 0;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!root) return 0;

    int added = 0;
    cJSON *lldp   = cJSON_GetObjectItem(root, "lldp");
    cJSON *ifaces = lldp ? cJSON_GetObjectItem(lldp, "interface") : NULL;

    int isArr = cJSON_IsArray(ifaces);
    int n = isArr ? cJSON_GetArraySize(ifaces) : (ifaces ? 1 : 0);

    for(int i = 0; i < n; i++) {
        cJSON *ifEntry = isArr ? cJSON_GetArrayItem(ifaces, i) : ifaces;
        /* l'interfaccia e' una chiave dinamica: itera i figli */
        for(cJSON *ifBody = ifEntry ? ifEntry->child : NULL;
            ifBody; ifBody = ifBody->next) {

            cJSON *chassis = cJSON_GetObjectItem(ifBody, "chassis");
            cJSON *port    = cJSON_GetObjectItem(ifBody, "port");
            if(!chassis) continue;

            /* chassis puo' essere { "<SysName>": { "id": {...}, "mgmt-ip": ... } }
             * oppure direttamente { "id": {...}, ... } */
            cJSON *chBody = chassis->child ? chassis->child : chassis;
            const char *sysName = chassis->child ? chassis->child->string : NULL;

            const char *chassisId = lldp_str(cJSON_GetObjectItem(chBody, "id"));
            const char *mgmtIp    = lldp_str(cJSON_GetObjectItem(chBody, "mgmt-ip"));
            const char *portId    = port ? lldp_str(cJSON_GetObjectItem(port, "id")) : NULL;
            const char *portDescr = port ? lldp_str(cJSON_GetObjectItem(port, "descr")) : NULL;

            if(!chassisId) continue;

            added++;
            char rsName[32];
            snprintf(rsName, sizeof(rsName), "RemoteSystem_%d", added);
            UA_NodeId rs = addBaseObject(server, rsFolder, NS_LOCAL, rsName,
                                         "LLDP neighbor (live)");
            addStringVariable(server, rs, NS_LOCAL, "ChassisId",          chassisId);
            addUInt32Variable(server, rs, NS_LOCAL, "ChassisIdSubtype",   4);
            addStringVariable(server, rs, NS_LOCAL, "SysName",            sysName ? sysName : "");
            addStringVariable(server, rs, NS_LOCAL, "MgmtAddress",        mgmtIp ? mgmtIp : "");
            addStringVariable(server, rs, NS_LOCAL, "PortId",             portId ? portId : "");
            addUInt32Variable(server, rs, NS_LOCAL, "PortIdSubtype",      3);
            addStringVariable(server, rs, NS_LOCAL, "PortDescr",          portDescr ? portDescr : "");
            addStringVariable(server, rs, NS_LOCAL, "SystemCapabilities", "Bridge");
            addUInt32Variable(server, rs, NS_LOCAL, "TimeToLive",         120);
        }
    }

    cJSON_Delete(root);
    return added;
}

static void buildNetworkInterfaces(UA_Server *server) {
    printf("[SERVER] Building NetworkInterfaces (live LLDP)...\n");

    const char *sysName  = env_or("UAFX_SYS_NAME", "uafx-node");
    const char *mgmtAddr = env_or("UAFX_MGMT_ADDR", "0.0.0.0");

    UA_NodeId objects  = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId niFolder = addFolder(server, objects, NS_LOCAL, "NetworkInterfaces");

    DIR *d = opendir("/sys/class/net");
    if(!d) {
        printf("[SERVER] WARNING: cannot open /sys/class/net\n");
        return;
    }

    struct dirent *de;
    while((de = readdir(d)) != NULL) {
        const char *ifname = de->d_name;
        if(ifname[0] == '.') continue;
        if(strcmp(ifname, "lo") == 0) continue;   /* salta loopback */

        char mac[64];
        if(!read_iface_mac(ifname, mac, sizeof(mac))) continue;
        if(strcmp(mac, "00:00:00:00:00:00") == 0) continue;

        char oper[32];
        read_iface_operstate(ifname, oper, sizeof(oper));

        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        ifname, "Physical interface");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  oper);
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", mac);
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 1000);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        /* ChassisId locale = MAC reale di QUESTA interfaccia -> niente phantom node */
        addLocalSystemData(server, lldp, mac, sysName, mgmtAddr, mac, 3);

        UA_NodeId rsFolder = addFolder(server, lldp, NS_LOCAL, "RemoteSystemsData");
        int nb = addRemoteSystemsFromLldp(server, rsFolder, ifname);
        printf("[SERVER]   %s (mac=%s, oper=%s): %d LLDP neighbor(s)\n",
               ifname, mac, oper, nb);
    }
    closedir(d);
    printf("[SERVER] + NetworkInterfaces built from live system\n\n");
}

/* -----7. Pub Static implementation exemple  */
        //addPubSubCOnnections
    static void addPubSubConnection(UA_Server *server, UA_String *transportProfile,
                    UA_NetworkAddressUrlDataType *networkAddressUrl){
      UA_PubSubConnectionConfig connectionConfig;
      memset(&connectionConfig, 0, sizeof(connectionConfig));
      connectionConfig.name = UA_STRING("UDP Connection 1");
      connectionConfig.transportProfileUri = *transportProfile;
      UA_Variant_setScalar(&connectionConfig.address, networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    /* Changed to static publisherId from random generation to identify
     * the publisher on Subscriber side */
        connectionConfig.publisherIdType = UA_PUBLISHERIDTYPE_UINT16;
        connectionConfig.publisherId.uint16 = 2234;
        connectionConfig.enabled = true;
        UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);
}

     //addPublishedDataset
    static void addPublishedDataSet(UA_Server *server) {
        /* The PublishedDataSetConfig contains all necessary public
        * information for the creation of a new PublishedDataSet */
        UA_PublishedDataSetConfig publishedDataSetConfig;
        memset(&publishedDataSetConfig, 0, sizeof(UA_PublishedDataSetConfig));
        publishedDataSetConfig.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
        publishedDataSetConfig.name = UA_STRING("First PDS");
        UA_Server_addPublishedDataSet(server, &publishedDataSetConfig, &publishedDataSetIdent);
}

//AddDataSetField

static void addDataSetField(UA_Server *server) {
    /* Add a field to the previous created PublishedDataSet */
    UA_NodeId dataSetFieldIdent;
    UA_DataSetFieldConfig dataSetFieldConfig;
    memset(&dataSetFieldConfig, 0, sizeof(UA_DataSetFieldConfig));
    dataSetFieldConfig.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
    dataSetFieldConfig.field.variable.fieldNameAlias = UA_STRING("Temperature C");
    dataSetFieldConfig.field.variable.promotedField = false;
    dataSetFieldConfig.field.variable.publishParameters.publishedVariable = temperatureNodeId;
    dataSetFieldConfig.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
    UA_Server_addDataSetField(server, publishedDataSetIdent,
                              &dataSetFieldConfig, &dataSetFieldIdent);
}

        static void addWriterGroup(UA_Server *server) {
    /* Now we create a new WriterGroupConfig and add the group to the existing
     * PubSubConnection. */
    UA_WriterGroupConfig writerGroupConfig;
    memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
    writerGroupConfig.name = UA_STRING("Demo WriterGroup");
    writerGroupConfig.publishingInterval = 5000;
    writerGroupConfig.writerGroupId = 100;
    writerGroupConfig.enabled = true;
    writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_UADP;

    /* Change message settings of writerGroup to send PublisherId,
     * WriterGroupId in GroupHeader and DataSetWriterId in PayloadHeader
     * of NetworkMessage */
    UA_UadpWriterGroupMessageDataType writerGroupMessage;
    UA_UadpWriterGroupMessageDataType_init(&writerGroupMessage);
    writerGroupMessage.networkMessageContentMask =
        (UA_UadpNetworkMessageContentMask)(UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER);

    /* The configuration flags for the messages are encapsulated inside the
     * message- and transport settings extension objects. These extension
     * objects are defined by the standard. e.g.
     * UadpWriterGroupMessageDataType */
    UA_ExtensionObject_setValue(&writerGroupConfig.messageSettings, &writerGroupMessage,
                                &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE]);

    UA_Server_addWriterGroup(server, connectionIdent, &writerGroupConfig, &writerGroupIdent);
}


static void
addDataSetWriter(UA_Server *server) {
    /* We need now a DataSetWriter within the WriterGroup. This means we must
     * create a new DataSetWriterConfig and add call the addWriterGroup function. */
    UA_DataSetWriterConfig dataSetWriterConfig;
    memset(&dataSetWriterConfig, 0, sizeof(UA_DataSetWriterConfig));
    dataSetWriterConfig.name = UA_STRING("Demo DataSetWriter");
    dataSetWriterConfig.dataSetWriterId = 62541;
    dataSetWriterConfig.keyFrameCount = 10;
    UA_Server_addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent,
                               &dataSetWriterConfig, &dataSetWriterIdent);
}

 /* callback eseguita quando il client chiama il metodo */
static UA_StatusCode startPublisherCallback(
        UA_Server *server, const UA_NodeId *sessionId,
        void *sessionContext, const UA_NodeId *methodId,
        void *methodContext, const UA_NodeId *objectId,
        void *objectContext, size_t inputSize,
        const UA_Variant *input, size_t outputSize,
        UA_Variant *output) {

    printf("[SERVER] StartPublisher called — configuring PubSub...\n");

    /* chiama le funzioni già scritte nel server */
    addPubSubConnection(server, &transportProfile, &networkAddressUrl);
    addPublishedDataSet(server);
    addDataSetField(server);
    addWriterGroup(server);
    addDataSetWriter(server);
    UA_Server_freezeWriterGroupConfiguration(server, writerGroupIdent);
    UA_Server_setWriterGroupOperational(server, writerGroupIdent);
    //addConnectionEndpoint(server);

    printf("[SERVER] Publisher started\n");
    return UA_STATUSCODE_GOOD;
}


/*========══════════════════════════════════════════════════════
 * Build UAFX AddressSpace
 *
 * Objects/
 *   +-- FxRoot/
 *   |   +-- TemperatureSensor/  [AutomationComponentType]
 *   |       +-- Assets/
 *   |       |   +-- SensorHardware/  [FxAssetType]
 *   |       +-- FunctionalEntities/
 *   |       |   +-- TemperatureReadingFE/  [FunctionalEntityType]
 *   |       |       +-- OutputData/Temperature (dynamic)
 *   |       |       +-- ConnectionEndpoints/
 *   |       +-- ComponentCapabilities/
 *   +-- NetworkInterfaces/
 *       +-- <iface reali>/
 *           +-- LldpData/
 *               +-- LocalSystemData/
 *               +-- RemoteSystemsData/ (vicini reali da lldpcli)
 * ═══════════════════════════════════════════════════════════ */

static void buildUAFXAddressSpace(UA_Server *server) {
    UA_UInt16 nsFxAc = resolveNamespaceIndex(server, FXAC_NS_URI);

    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId fxRoot = addFolder(server, objectsFolder, nsFxAc, "FxRoot");

    /* Crea AC — questo istanzia automaticamente Assets/, FunctionalEntities/, ecc. */
    UA_NodeId acNode = addTypedObject(server, fxRoot,
                                      NS_LOCAL, "TemperatureSensor",
                                      "Temperature Sensor AutomationComponent",
                                      nsFxAc, FXAC_ID_AUTOMATIONCOMPONENTTYPE);

    addStringVariable(server, acNode, NS_LOCAL, "ConformanceName",
                      "urn:example:uafx:temperature-sensor:v1.0");
    addUInt32Variable(server, acNode, NS_LOCAL, "AggregatedHealth", 0);

    /* Aggiunta metodi */
    UA_MethodAttributes methAttr = UA_MethodAttributes_default;
    methAttr.displayName = lt("StartPublisher");
    methAttr.executable = true;
    methAttr.userExecutable = true;
    UA_Server_addMethodNode(server, UA_NODEID_NULL, acNode,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        qn(NS_LOCAL, "StartPublisher"), methAttr,
        startPublisherCallback, 0, NULL, 0, NULL, NULL, NULL);
    registerEstablishConnectionsMethod(server, acNode);

    /* ── Trova Assets/ già creata dal tipo e popolala ── */
    UA_NodeId assetsFolder = resolveChildByNameServer(server, acNode, "Assets");
    UA_NodeId assetNode = addTypedObject(server, assetsFolder,
                                         NS_LOCAL, "SensorHardware",
                                         "Physical temperature sensor hardware",
                                         nsFxAc, FXAC_ID_FXASSETTYPE);
    addStringVariable(server, assetNode, NS_LOCAL, "Manufacturer",      "AcmeCorp");
    addStringVariable(server, assetNode, NS_LOCAL, "ManufacturerUri",   "https://www.acmecorp-sensors.com");
    addStringVariable(server, assetNode, NS_LOCAL, "Model",             "TempSensor-1000");
    addStringVariable(server, assetNode, NS_LOCAL, "ProductCode",       "TS-1000-V2");
    addStringVariable(server, assetNode, NS_LOCAL, "HardwareRevision",  "2.0");
    addStringVariable(server, assetNode, NS_LOCAL, "SoftwareRevision",  "1.3.5");
    addStringVariable(server, assetNode, NS_LOCAL, "DeviceClass",       "TemperatureSensor");
    addStringVariable(server, assetNode, NS_LOCAL, "SerialNumber",      "SN-12345-ABCD");

    /* ── Trova FunctionalEntities/ già creata dal tipo e popolala ── */
    UA_NodeId feFolder = resolveChildByNameServer(server, acNode, "FunctionalEntities");
    UA_NodeId feNode = addTypedObject(server, feFolder,
                                      NS_LOCAL, "TemperatureReadingFE",
                                      "Temperature reading functional entity",
                                      nsFxAc, FXAC_ID_FUNCTIONALENTITYTYPE);
    addStringVariable(server, feNode, NS_LOCAL, "AuthorUri",
                      "https://www.acmecorp-sensors.com");
    addStringVariable(server, feNode, NS_LOCAL, "AuthorAssignedIdentifier",
                      "TempSensor-FE-v1.0");
    addStringVariable(server, feNode, NS_LOCAL, "AuthorAssignedVersion",
                      "1.0.0.0");
    addUInt32Variable(server, feNode, NS_LOCAL, "OperationalHealth", 0);

    /* ── OutputData/ ── */
    UA_NodeId outputFolder = addFolder(server, feNode, NS_LOCAL, "OutputData");
    temperatureNodeId = addTemperatureVariable(server, outputFolder, NS_LOCAL, "Temperature");

    UA_NodeId inputFolder = addFolder(server, feNode, NS_LOCAL, "InputData");
    (void)inputFolder;
   // addInputVariable(server,inputFolder, NS_LOCAL, "Density");

    addFolder(server, feNode, NS_LOCAL, "ConnectionEndpoints");

    /* ───  ComponentCapabilities/ ──────────────────────────── */
    UA_NodeId capFolder = addFolder(server, acNode, NS_LOCAL, "ComponentCapabilities");
    addUInt32Variable(server, capFolder, NS_LOCAL, "MaxConnections", 4);
    addUInt32Variable(server, capFolder, NS_LOCAL, "MinConnections", 0);

    printf("[SERVER] + UAFX AddressSpace build complete\n\n");

    /* ─── NetworkInterfaces con LLDP live (Part 82, 6.5.2) ────── */
    buildNetworkInterfaces(server);
}


/* ═══════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════ */

int main(void) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);
    srand(time(NULL));

    printf("\n");
    printf("========================================================\n");
    printf("  OPC UA FX Temperature Server (live LLDP / Kathara)\n");
    printf("========================================================\n\n");

    /* ── Parametri da ambiente (univoci per istanza) ── */
    const char *appUri   = env_or("UAFX_APP_URI",   "urn:kathara:uafx:node");
    const char *appName  = env_or("UAFX_APP_NAME",  "UAFX Kathara Node");
    const char *pubUrl   = env_or("UAFX_PUBLIC_URL","opc.tcp://0.0.0.0:4841");
    const char *ldsUrl   = env_or("UAFX_LDS_URL",   "opc.tcp://127.0.0.1:4840");
    const char *pubIface = env_or("UAFX_PUBSUB_IFACE", "eth0");
    int port = atoi(env_or("UAFX_PORT", "4841"));
    if(port <= 0 || port > 65535) port = 4841;

    printf("[SERVER] Identity:\n");
    printf("           appUri    = %s\n", appUri);
    printf("           appName   = %s\n", appName);
    printf("           publicUrl = %s\n", pubUrl);
    printf("           ldsUrl    = %s\n", ldsUrl);
    printf("           port      = %d\n", port);
    printf("           pubIface  = %s\n\n", pubIface);

    /* ─── Crea server ────────────────────────────────────────── */
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);

    transportProfile =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    networkAddressUrl.networkInterface = UA_STRING((char *)pubIface);
    networkAddressUrl.url = UA_STRING("opc.udp://224.0.0.22:4840/");

    static UA_DataTypeArray customDataTypesAC = {
        NULL,
        UA_TYPES_UAFX_AC_COUNT,
        UA_TYPES_UAFX_AC
    };

    static UA_DataTypeArray customDataTypesData = {
        &customDataTypesAC,
        UA_TYPES_UAFX_DATA_COUNT,
        UA_TYPES_UAFX_DATA
    };

    static UA_DataTypeArray customDataTypesDI = {
        &customDataTypesData,
        UA_TYPES_DI_COUNT,
        UA_TYPES_DI
    };

    config->customDataTypes = &customDataTypesDI;
    UA_ServerConfig_setMinimal(config, (UA_UInt16)port, NULL);

    UA_String hostname = UA_String_fromChars(pubUrl);
    config->applicationDescription.applicationType = UA_APPLICATIONTYPE_SERVER;

    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri = UA_String_fromChars(appUri);

    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", (char *)appName);

    config->applicationDescription.discoveryUrlsSize = 1;
    config->applicationDescription.discoveryUrls =
        (UA_String*)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    config->applicationDescription.discoveryUrls[0] = UA_String_fromChars(pubUrl);

    config->mdnsEnabled = UA_TRUE;
    config->mdnsConfig.mdnsServerName = UA_String_fromChars(appName);

    config->mdnsConfig.serverCapabilitiesSize = 1;
    UA_String *caps = (UA_String *)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    caps[0] = UA_String_fromChars("UAFX");
    config->mdnsConfig.serverCapabilities = caps;
    config->mdnsInterfaceIP = hostname;

#ifdef UA_ENABLE_DISCOVERY_MULTICAST
    printf("[SERVER] + mDNS Discovery: ENABLED\n\n");
#else
    printf("[SERVER] mDNS Discovery: DISABLED\n\n");
#endif

    /* ─── Carica i tipi UAFX dal nodeset generato ────────────── */
    printf("[SERVER] Loading UAFX nodesets...\n");
    UA_StatusCode retval = my_uafx_model(server);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("[WARNING] Address Space loaded with some missing sub-nodes (Code: %s).\n",
               UA_StatusCode_name(retval));
        printf("[WARNING] This is normal for massive UAFX NodeSets. Continuing anyway...\n\n");
    } else {
        printf("[SERVER] + UAFX types loaded perfectly\n\n");
    }

    /* ─── Costruisci AddressSpace ────────────────────────────── */
    buildUAFXAddressSpace(server);

    /* ─── Avvia server ───────────────────────────────────────── */
    retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("[ERROR] Server startup failed: %s\n",
               UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    /* ─── Registrazione all'LDS ──────────────────────────────── */
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);

    UA_String discoveryUrl = UA_STRING((char *)ldsUrl);
    UA_StatusCode retval_lds =
        UA_Server_registerDiscovery(server, &cc, discoveryUrl, UA_STRING_NULL);
    if(retval_lds != UA_STATUSCODE_GOOD) {
        printf("[WARNING] LDS registration failed: %s\n", UA_StatusCode_name(retval_lds));
    } else {
        printf("[SERVER] + LDS registration OK\n");
    }

    printf("\n========================================================\n");
    printf("  SERVER RUNNING on %s\n", pubUrl);
    printf("========================================================\n");
    printf("Press Ctrl+C to stop\n\n");

    /* ─── Loop principale ────────────────────────────────────── */
    while(running) {
        UA_Server_run_iterate(server, true);
    }

    printf("\n[SERVER] Shutting down...\n");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    printf("[SERVER] Stopped cleanly\n\n");

    return EXIT_SUCCESS;
}
