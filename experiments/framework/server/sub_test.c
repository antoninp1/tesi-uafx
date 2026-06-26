/* ============================================================
 * uafx_temperature_server.c
 *
 * Server OPC UA FX con tipi UAFX corretti:
 *   - AutomationComponent istanziato come AutomationComponentType (ns=FX/AC; i=2)
 *   - Asset istanziato come FxAssetType                           (ns=FX/AC; i=3)
 *   - FunctionalEntity istanziata come FunctionalEntityType       (ns=FX/AC; i=4)
 *
 * Include NetworkInterfaces con dati LLDP per topology discovery
 * secondo OPC 10000-82 sezione 6.5.2 e 7.3.2.
 *
 * Compilazione:
 *   gcc -o temp_server uafx_temperature_server.c my_uafx_types.c open62541.c -pthread
 * ============================================================ */

#define _GNU_SOURCE
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/server_pubsub.h>
#include <open62541/client_config_default.h>
#include "types_di_generated.h"
#include "types_uafx_data_generated.h"
#include "types_uafx_ac_generated.h"
#include "namespace_di_generated.h"
#include "namespace_uafx_data_generated.h"
#include "namespace_uafx_ac_generated.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <sched.h>
#include <sys/mman.h>
#include <errno.h>
#include "establish_connection.h"
/* ─── Namespace index di FX/AC nel server ─────────────────── */
#define FXAC_NS_URI   "http://opcfoundation.org/UA/FX/AC/"

/* NodeId dei tipi UAFX (numeric id fisso da nodeset XML) */
#define FXAC_ID_AUTOMATIONCOMPONENTTYPE  2
#define FXAC_ID_FXASSETTYPE              3
#define FXAC_ID_FUNCTIONALENTITYTYPE     4

#define NS_LOCAL 1
#define LDS_URL          "opc.tcp://192.168.17.112:4840"
#define SERVER_PUBLIC_URL "opc.tcp://192.168.17.184:4841"

#define AUTOSTART_PUBSUB 1

#define SCHED_PRIORITY 80

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    printf("\n[SERVER] Shutdown signal received\n");
    running = false;
}

typedef struct {
    UA_Boolean rt;
    UA_Boolean rtLog;
    int rtCore;
    int schedPrio;
    UA_Boolean autostart;

} CliOptions;

static CliOptions parseArgs(int argc, char **argv) {
    CliOptions opts = { .rt = false, .rtLog = false, .rtCore = 2};
    static struct option longOpts[] = {
        {"rt",      no_argument,       0, 'r'},
        {"rt-log",      no_argument,       0, 'l'},
        {"rt-core", required_argument, 0, 'c'},
        {"schedule-priority", required_argument, 0, 'p'},
        {"autostart", no_argument, 0, 'a'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    while((opt = getopt_long(argc, argv, "rc:h", longOpts, NULL)) != -1) {
        switch(opt) {
            case 'r': opts.rt = true; break;
            case 'l': opts.rtLog = true; break;
            case 'c': opts.rtCore = atoi(optarg); break;
            case 'p': opts.schedPrio = atoi(optarg); break;
            case 'a': opts.autostart = true; break;
            case 'h':
                printf("Usage: %s [--rt] [--rt-log] [--rt-core=N] [--schedule-priority=N] [--autostart]\n", argv[0]);
                exit(0);
            default:
                fprintf(stderr, "Unknown option. Please use --help.\n");
                exit(1);
        }
    }
    return opts;
}

static void setupRealtime(int rtCore) {
    if(mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        perror("[RT] mlockall a échoué (lancé en root ?)");

    struct sched_param sp = { .sched_priority = 80 };
    if(sched_setscheduler(0, SCHED_FIFO, &sp) != 0)
        perror("[RT] sched_setscheduler a échoué (lancé en root ?)");

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(rtCore, &cpuset);   /* ajuste selon ta machine */
    if(sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
        perror("[RT] sched_setaffinity a échoué");
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

/* ═══════════════════════════════════════════════════════════
 * Density Variable with Dynamic Callback
 * ═══════════════════════════════════════════════════════════ */

/* Density dinamica: 998.0 ± 5.0 kg/m^3 (tipico per acqua) */
static void readDensity(UA_Server *server, const UA_NodeId *sessionId,
                        void *sessionContext, const UA_NodeId *nodeId,
                        void *nodeContext, const UA_NumericRange *range,
                        const UA_DataValue *data) {
    UA_Float density = 998.0f + ((rand() % 1000) - 500) / 100.0f;
    UA_Variant value;
    UA_Variant_setScalar(&value, &density, &UA_TYPES[UA_TYPES_FLOAT]);
    UA_Server_writeValue(server, *nodeId, value);
}

static UA_NodeId addDensityVariable(UA_Server *server, UA_NodeId parent,
                                    UA_UInt16 ns, const char *name) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = lt(name);
    attr.description = lt("Current density reading in kg/m^3");

    UA_Float initialValue = 998.0f;
    UA_Variant_setScalar(&attr.value, &initialValue, &UA_TYPES[UA_TYPES_FLOAT]);
    attr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addVariableNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, NULL, &newNode);

    UA_ValueCallback callback;
    callback.onRead  = readDensity;
    callback.onWrite = NULL;
    UA_Server_setVariableNode_valueCallback(server, newNode, callback);

    addStringVariable(server, newNode, ns, "EngineeringUnits", "kg/m^3");
    return newNode;
}

static void logReceivedUpdate(UA_Server *server, const UA_NodeId *sessionId,
                               void *sessionContext, const UA_NodeId *nodeId,
                               void *nodeContext, const UA_NumericRange *range,
                               const UA_DataValue *data) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[RX] %ld.%09ld\n", (long)ts.tv_sec, (long)ts.tv_nsec);
}

static UA_NodeId addInputVariable(UA_Server *server, UA_NodeId parent, UA_UInt16 ns, const char *name, UA_Boolean logging){
    UA_VariableAttributes inputAttr = UA_VariableAttributes_default;
     inputAttr.displayName = lt(name);
    inputAttr.description = lt("Temperature recived in C");
    UA_Float initTemp = 0.0f;
    UA_Variant_setScalar(&inputAttr.value, &initTemp, &UA_TYPES[UA_TYPES_FLOAT]);
    inputAttr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    inputAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_NodeId receivedTempNodeId = UA_NODEID_NULL;
    UA_Server_addVariableNode(server, UA_NODEID_NUMERIC(NS_LOCAL, 50001),
        parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        qn(ns, "Temperature"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        inputAttr, NULL, &receivedTempNodeId);

    if (logging) {
        UA_ValueCallback rxCallback;
        rxCallback.onRead = NULL;
        rxCallback.onWrite = logReceivedUpdate;
        UA_Server_setVariableNode_valueCallback(server, receivedTempNodeId, rxCallback);
    }

    addStringVariable(server, receivedTempNodeId, NS_LOCAL, "EngineeringUnits", "\xC2\xB0""C");
return receivedTempNodeId;
}

/* ═══════════════════════════════════════════════════════════
 * Build NetworkInterfaces with LLDP data
 *
 * Secondo OPC 10000-82 sezione 6.5.2:
 * - NetworkInterfaces/ folder sotto Objects
 * - Ogni interfaccia fisica come oggetto con proprieta'
 *   IetfBaseNetworkInterfaceType-like (AdminStatus, PhysAddress, Speed)
 * - LldpData/ con i dati dei vicini LLDP (Part 82, 7.3.2)
 *
 * Struttura:
 * Objects/
 * +-- NetworkInterfaces/
 *     +-- enp0s31f6/
 *         +-- AdminStatus: "up"
 *         +-- OperStatus:  "up"
 *         +-- PhysAddress:  "e8:6a:64:53:81:a9"
 *         +-- Speed:        1000
 *         +-- LldpData/
 *             +-- LocalSystemData/
 *             |   +-- ChassisId, ChassisIdSubtype, SysName, ...
 *             +-- RemoteSystemsData/
 *                 +-- RemoteSystem_1/
 *                     +-- ChassisId, SysName, MgmtAddress, PortId, ...
 * ═══════════════════════════════════════════════════════════ */
static void addLocalSystemData(UA_Server *server, UA_NodeId parent,
                               const char *portId, UA_UInt32 portIdSubtype) {
    UA_NodeId local = addBaseObject(server, parent, NS_LOCAL,
                                    "LocalSystemData",
                                    "LLDP Local System (edge-up-4)");
    addStringVariable(server, local, NS_LOCAL, "ChassisId",          "00:07:32:ae:79:1d");
    addUInt32Variable(server, local, NS_LOCAL, "ChassisIdSubtype",   4);
    addStringVariable(server, local, NS_LOCAL, "SysName",            "edge-up-4");
    addStringVariable(server, local, NS_LOCAL, "SysDescr",
                      "Ubuntu 24.04.4 LTS Linux 6.8.1-1015-realtime x86_64");
    addStringVariable(server, local, NS_LOCAL, "MgmtAddress",        "192.168.100.4");
    addStringVariable(server, local, NS_LOCAL, "SystemCapabilities", "Bridge,Router,Wlan");
    addStringVariable(server, local, NS_LOCAL, "PortId",             portId);
    addUInt32Variable(server, local, NS_LOCAL, "PortIdSubtype",      portIdSubtype);
}

static void buildNetworkInterfaces(UA_Server *server) {
    printf("[SERVER] Building NetworkInterfaces (edge-up-4)...\n");

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId niFolder = addFolder(server, objects, NS_LOCAL, "NetworkInterfaces");

    /* ============ enp43s0 → vicino edge-up-3 ============ */
    {
        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        "enp43s0", "Physical interface enp43s0");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  "up");
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", "00:07:32:ae:79:1d");
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 1000);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        addLocalSystemData(server, lldp, "00:07:32:ae:79:1d", 3);

        UA_NodeId rsFolder = addFolder(server, lldp, NS_LOCAL, "RemoteSystemsData");
        UA_NodeId rs = addBaseObject(server, rsFolder, NS_LOCAL,
                                     "RemoteSystem_1", "LLDP neighbor on enp43s0");
        addStringVariable(server, rs, NS_LOCAL, "ChassisId",          "00:07:32:ae:79:13");
        addUInt32Variable(server, rs, NS_LOCAL, "ChassisIdSubtype",   4);
        addStringVariable(server, rs, NS_LOCAL, "SysName",            "edge-up-3");
        addStringVariable(server, rs, NS_LOCAL, "SysDescr",
                          "Ubuntu 24.04.4 LTS Linux 6.8.1-1015-realtime x86_64");
        addStringVariable(server, rs, NS_LOCAL, "MgmtAddress",        "192.168.100.3");
        addStringVariable(server, rs, NS_LOCAL, "PortId",             "00:07:32:ae:79:13");
        addUInt32Variable(server, rs, NS_LOCAL, "PortIdSubtype",      3);
        addStringVariable(server, rs, NS_LOCAL, "PortDescr",          "enp43s0");
        addStringVariable(server, rs, NS_LOCAL, "SystemCapabilities", "Bridge,Router,Wlan");
        addUInt32Variable(server, rs, NS_LOCAL, "TimeToLive",         120);
    }

    /* ============ enp0s31f6 → vicino RELY-10TSN12 ============ */
    {
        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        "enp0s31f6", "Physical interface enp0s31f6");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  "up");
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", "00:07:32:ae:79:1c");
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 1000);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        addLocalSystemData(server, lldp, "00:07:32:ae:79:1c", 3);

        UA_NodeId rsFolder = addFolder(server, lldp, NS_LOCAL, "RemoteSystemsData");
        UA_NodeId rs = addBaseObject(server, rsFolder, NS_LOCAL,
                                     "RemoteSystem_1", "LLDP neighbor on enp0s31f6");
        addStringVariable(server, rs, NS_LOCAL, "ChassisId",          "fe:16:0b:0c:54:0e");
        addUInt32Variable(server, rs, NS_LOCAL, "ChassisIdSubtype",   4);
        addStringVariable(server, rs, NS_LOCAL, "SysName",            "RELY-10TSN12");
        addStringVariable(server, rs, NS_LOCAL, "SysDescr",           "RELY-10TSN12");
        addStringVariable(server, rs, NS_LOCAL, "MgmtAddress",        "10.0.100.1");
        addStringVariable(server, rs, NS_LOCAL, "PortId",             "70:f8:e7:d0:54:56");
        addUInt32Variable(server, rs, NS_LOCAL, "PortIdSubtype",      3);
        addStringVariable(server, rs, NS_LOCAL, "PortDescr",          "PORT_3");
        addStringVariable(server, rs, NS_LOCAL, "SystemCapabilities", "Bridge");
        addUInt32Variable(server, rs, NS_LOCAL, "TimeToLive",         40);
    }

    /* ============ wlp44s0 → nessun vicino ============ */
    {
        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        "wlp44s0", "Wireless interface wlp44s0");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  "up");
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", "4c:b0:4a:9e:28:a2");
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 0);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        addLocalSystemData(server, lldp, "4c:b0:4a:9e:28:a2", 3);
        addFolder(server, lldp, NS_LOCAL, "RemoteSystemsData");
    }

    printf("[SERVER] + NetworkInterfaces: enp43s0, enp0s31f6, wlp44s0\n");
    printf("[SERVER]   ChassisId (shared): 00:07:32:ae:79:1d\n\n");
}

static void setupSubscriber(UA_Server *server) {
    printf("[SERVER] Setting up PubSub Subscriber...\n");

    /* ─── 1. PubSubConnection (stessa multicast del publisher) ── */
    UA_PubSubConnectionConfig connConfig;
    memset(&connConfig, 0, sizeof(connConfig));
    connConfig.name = UA_STRING("UDP Multicast Subscriber Connection");
    connConfig.transportProfileUri =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-eth-uadp");
    connConfig.enabled = true;

    UA_NetworkAddressUrlDataType addr;
    addr.networkInterface = UA_STRING("enp43s0");
    addr.url = UA_STRING("opc.eth://03-00-00-00-00-03:10.6");

    UA_Variant_setScalar(&connConfig.address, &addr,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

    //connConfig.publisherIdType = UA_PUBLISHERIDTYPE_UINT16;
    //connConfig.publisherId.uint16 = 2234;  /* diverso dal publisher */

    UA_NodeId connId;
    UA_StatusCode rc = UA_Server_addPubSubConnection(server, &connConfig, &connId);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[SERVER]   Subscriber PubSubConnection FAILED: %s\n",
               UA_StatusCode_name(rc));
        return;
    }
    printf("[SERVER]   + PubSubConnection (subscriber, opc.udp://239.0.0.1:4840)\n");

    /* ─── 2. ReaderGroup ──────────────────────────────── */
    UA_ReaderGroupConfig rgConfig;
    memset(&rgConfig, 0, sizeof(rgConfig));
    rgConfig.name = UA_STRING("TemperatureReaderGroup");

    UA_NodeId rgId;
    rc = UA_Server_addReaderGroup(server, connId, &rgConfig, &rgId);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[SERVER]   ReaderGroup FAILED: %s\n", UA_StatusCode_name(rc));
        return;
    }
    printf("[SERVER]   + ReaderGroup\n");

    /* ─── 3. DataSetReader ────────────────────────────── */
    UA_DataSetReaderConfig dsrConfig;
    memset(&dsrConfig, 0, sizeof(dsrConfig));
    dsrConfig.name = UA_STRING("TemperatureReader");

    /* Filtro: accetta solo messaggi dal PublisherId 1 (edge-up-3) */
    UA_UInt16 pubId = 2234;
    //UA_Variant_setScalar(&dsrConfig.publisherId, &pubId, &UA_TYPES[UA_TYPES_UINT16]);
    dsrConfig.publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
    dsrConfig.publisherId.id.uint16 = pubId;

    dsrConfig.writerGroupId   = 100;
    dsrConfig.dataSetWriterId = 62541;  // non 1
    /* DataSetMetaData: descrive il contenuto atteso del DataSet */
    UA_DataSetMetaDataType_init(&dsrConfig.dataSetMetaData);
    dsrConfig.dataSetMetaData.name = UA_STRING("TemperatureDataSet");
    dsrConfig.dataSetMetaData.fieldsSize = 1;
    dsrConfig.dataSetMetaData.fields = (UA_FieldMetaData *)
        UA_calloc(1, sizeof(UA_FieldMetaData));

    UA_FieldMetaData *field = &dsrConfig.dataSetMetaData.fields[0];
    UA_FieldMetaData_init(field);
    field->builtInType = UA_NS0ID_FLOAT;
    field->dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    field->valueRank = -1;  /* scalare */
    field->name = UA_STRING("Temperature");

    /* Message settings UADP: deve matchare quelli del publisher */
    UA_UadpDataSetReaderMessageDataType dsrMsgConfig;
    memset(&dsrMsgConfig, 0, sizeof(dsrMsgConfig));
    dsrMsgConfig.networkMessageContentMask =
        (UA_UadpNetworkMessageContentMask)
        (UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
         UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
         UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
         UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER);

    dsrConfig.messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    dsrConfig.messageSettings.content.decoded.type =
        &UA_TYPES[UA_TYPES_UADPDATASETREADERMESSAGEDATATYPE];
    dsrConfig.messageSettings.content.decoded.data = &dsrMsgConfig;

    UA_NodeId dsrId;
    rc = UA_Server_addDataSetReader(server, rgId, &dsrConfig, &dsrId);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[SERVER]   DataSetReader FAILED: %s\n", UA_StatusCode_name(rc));
        UA_free(dsrConfig.dataSetMetaData.fields);
        return;
    }
    printf("[SERVER]   + DataSetReader (filter: pubId=1, wgId=100, dswId=1)\n");

    /* ─── 4. TargetVariables: mappa il campo ricevuto alla variabile locale ── */
    UA_FieldTargetDataType targetVar;
    memset(&targetVar, 0, sizeof(UA_FieldTargetDataType));
    targetVar.attributeId = UA_ATTRIBUTEID_VALUE;
    targetVar.targetNodeId =   UA_NODEID_NUMERIC(NS_LOCAL, 50001);/* il tuo NodeId target */;
    //rc=UA_Server_DataSetReader_createTargetVariables(server, dsrId, 1, &targetVar);
    rc=UA_Server_setDataSetReaderTargetVariables(server, dsrId, 1, &targetVar);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[SERVER]   TargetVariables FAILED: %s\n", UA_StatusCode_name(rc));
    } else {
        printf("[SERVER]   + TargetVariable → ns=%d;i=50001 (ReceivedTemperature)\n",
               NS_LOCAL);
    }

    UA_Server_enableDataSetReader(server, dsrId);
    UA_Server_enablePubSubConnection(server, connId);
    UA_Server_setReaderGroupOperational(server, rgId);
}


 /* callback eseguita quando il client chiama il metodo */
static UA_StatusCode startSubscriberCallback(
        UA_Server *server, const UA_NodeId *sessionId,
        void *sessionContext, const UA_NodeId *methodId,
        void *methodContext, const UA_NodeId *objectId,
        void *objectContext, size_t inputSize,
        const UA_Variant *input, size_t outputSize,
        UA_Variant *output) {

    printf("[SERVER] StartSubscriber called — configuring PubSub...\n");

    /* chiama le funzioni già scritte nel server */
    setupSubscriber(server);

    printf("[SERVER] Subscriber started\n");
    return UA_STATUSCODE_GOOD;
}


/* ═══════════════════════════════════════════════════════════
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
 *       +-- enp0s31f6/
 *           +-- LldpData/
 *               +-- LocalSystemData/
 *               +-- RemoteSystemsData/
 *                   +-- RemoteSystem_1/ (RELY-10TSN12)
 * ═══════════════════════════════════════════════════════════ */

static void buildUAFXAddressSpace(UA_Server *server, UA_Boolean logging) {
    printf("[SERVER] Building UAFX AddressSpace...\n");

    UA_UInt16 nsFxAc = resolveNamespaceIndex(server, FXAC_NS_URI);
    printf("[SERVER]   Namespace FX/AC resolved: %d\n", nsFxAc);

    if(nsFxAc == 0) {
        printf("[SERVER] ERROR: FX/AC namespace not found.\n");
        return;
    }

    /* ─── 1. FxRoot ──────────────────────────────────────────── */
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId fxRoot = addFolder(server, objectsFolder, nsFxAc, "FxRoot");

    /* ─── 2. AutomationComponent ─────────────────────────────── */
    UA_NodeId acNode = addTypedObject(server, fxRoot,
                                      NS_LOCAL, "DensitySensor",
                                      "Density Sensor AutomationComponent",
                                      nsFxAc, FXAC_ID_AUTOMATIONCOMPONENTTYPE);
    printf("[SERVER]   + AutomationComponent: DensitySensor\n");

    addStringVariable(server, acNode, NS_LOCAL, "ConformanceName",
                      "urn:example:uafx:density-sensor:v1.0");
    addUInt32Variable(server, acNode, NS_LOCAL, "AggregatedHealth", 0);

    registerEstablishConnectionsMethod(server, acNode);

    UA_MethodAttributes methAttr = UA_MethodAttributes_default;
    methAttr.displayName = lt("StartSubscriber");
    methAttr.executable = true;
    methAttr.userExecutable = true;
    UA_Server_addMethodNode(server, UA_NODEID_NULL, acNode,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        qn(NS_LOCAL, "StartSubscriber"), methAttr,
        startSubscriberCallback, 0, NULL, 0, NULL, NULL, NULL);

    /* ─── 3. Assets/ — usa cartella istanziata dal tipo ─────── */
    UA_NodeId assetsFolder = resolveChildByNameServer(server, acNode, "Assets");

    UA_NodeId assetNode = addTypedObject(server, assetsFolder,
                                         NS_LOCAL, "SensorHardware",
                                         "Physical density sensor hardware",
                                         nsFxAc, FXAC_ID_FXASSETTYPE);
    addStringVariable(server, assetNode, NS_LOCAL, "Manufacturer",      "AcmeCorp");
    addStringVariable(server, assetNode, NS_LOCAL, "ManufacturerUri",   "https://www.acmecorp-sensors.com");
    addStringVariable(server, assetNode, NS_LOCAL, "Model",             "DenSensor-1000");
    addStringVariable(server, assetNode, NS_LOCAL, "ProductCode",       "TS-1000-V2");
    addStringVariable(server, assetNode, NS_LOCAL, "HardwareRevision",  "2.0");
    addStringVariable(server, assetNode, NS_LOCAL, "SoftwareRevision",  "1.3.5");
    addStringVariable(server, assetNode, NS_LOCAL, "DeviceClass",       "DensitySensor");
    addStringVariable(server, assetNode, NS_LOCAL, "SerialNumber",      "SN-12345-ABCD");

    /* ─── 4. FunctionalEntities/ — usa cartella istanziata dal tipo ── */
    UA_NodeId feFolder = resolveChildByNameServer(server, acNode, "FunctionalEntities");
    UA_NodeId feNode = addTypedObject(server, feFolder,
                                      NS_LOCAL, "DensityReadingFE",
                                      "Density reading functional entity",
                                      nsFxAc, FXAC_ID_FUNCTIONALENTITYTYPE);
    printf("[SERVER]   + FunctionalEntity: DensityReadingFE\n");

    addStringVariable(server, feNode, NS_LOCAL, "AuthorUri",
                      "https://www.acmecorp-sensors.com");
    addStringVariable(server, feNode, NS_LOCAL, "AuthorAssignedIdentifier",
                      "TempSensor-FE-v1.0");
    addStringVariable(server, feNode, NS_LOCAL, "AuthorAssignedVersion",
                      "1.0.0.0");
    addUInt32Variable(server, feNode, NS_LOCAL, "OperationalHealth", 0);

    /* OutputData/InputData/ConnectionEndpoints non istanziate dal tipo → crea manualmente */
    UA_NodeId outputFolder = addFolder(server, feNode, NS_LOCAL, "OutputData");
    addDensityVariable(server, outputFolder, NS_LOCAL, "Density");
    printf("[SERVER]     + OutputData/Density\n");

    UA_NodeId inputFolder = addFolder(server, feNode, NS_LOCAL, "InputData");
    addInputVariable(server, inputFolder, NS_LOCAL, "Temperature", logging);
    printf("[SERVER]     + InputData/Temperature\n");

    addFolder(server, feNode, NS_LOCAL, "ConnectionEndpoints");

    /* ─── 5. ComponentCapabilities/ — usa cartella istanziata dal tipo ── */
    UA_NodeId capFolder = addFolder(server, acNode, NS_LOCAL, "ComponentCapabilities");
    addUInt32Variable(server, capFolder, NS_LOCAL, "MaxConnections", 4);
    addUInt32Variable(server, capFolder, NS_LOCAL, "MinConnections", 0);
    printf("[SERVER] + UAFX AddressSpace build complete\n\n");

    buildNetworkInterfaces(server);
}



/* ═══════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);
    srand(time(NULL));

    printf("\n");
    printf("========================================================\n");
    printf("  OPC UA FX Temperature Server (with LLDP)\n");
    printf("========================================================\n\n");
    CliOptions opts = parseArgs(argc, argv);

    /* ─── Crea server ────────────────────────────────────────── */
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);

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
    UA_ServerConfig_setMinimal(config, 4841, NULL);
    UA_String hostname = UA_String_fromChars(SERVER_PUBLIC_URL);
    config->applicationDescription.applicationType = UA_APPLICATIONTYPE_SERVER;

    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri =
        UA_String_fromChars("urn:example:uafx:density-sensor-1");

    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Density Sensor");
    config->applicationDescription.discoveryUrlsSize = 1;
    config->applicationDescription.discoveryUrls =
        (UA_String*)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    config->applicationDescription.discoveryUrls[0] =
        UA_String_fromChars(SERVER_PUBLIC_URL);

    config->mdnsEnabled = UA_TRUE;
    config->mdnsConfig.mdnsServerName =
        UA_String_fromChars("MioServer");

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
    UA_StatusCode retval     = namespace_di_generated(server);
    UA_StatusCode retval_data = namespace_uafx_data_generated(server);
    UA_StatusCode retval_ac   = namespace_uafx_ac_generated(server);

    if(retval != UA_STATUSCODE_GOOD || retval_data != UA_STATUSCODE_GOOD || retval_ac != UA_STATUSCODE_GOOD) {
        printf("[WARNING] Address Space loaded with some missing sub-nodes (Code: %s).\n",
               UA_StatusCode_name(retval));
        printf("[WARNING] This is normal for massive UAFX NodeSets. Continuing anyway...\n\n");
    } else {
        printf("[SERVER] + UAFX types loaded perfectly\n\n");
    }

    /* ─── Costruisci AddressSpace ────────────────────────────── */
    buildUAFXAddressSpace(server, opts.rtLog);

    if (opts.autostart)
        setupSubscriber(server);

    /* ─── Avvia server ───────────────────────────────────────── */
    retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("[ERROR] Server startup failed: %s\n",
               UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }
	/* Costruzione sub statico*/
	//setupSubscriber(server);


    /* ─── Registrazione all'LDS ──────────────────────────────── */
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);

    UA_String discoveryUrl = UA_STRING(LDS_URL);

    UA_StatusCode retval_lds = UA_Server_registerDiscovery(server, &cc, discoveryUrl, UA_STRING_NULL);
    if(retval_lds != UA_STATUSCODE_GOOD) {
        printf("[WARNING] LDS registration failed: %s\n", UA_StatusCode_name(retval_lds));
    } else {
        printf("[SERVER] + LDS registration OK\n");
    }

    printf("\n========================================================\n");
    printf("  SERVER RUNNING on %s\n", SERVER_PUBLIC_URL);
    printf("========================================================\n");
    printf("UAFX Structure:\n");
    printf("  Objects/\n");
    printf("  +-- FxRoot/\n");
    printf("  |   +-- DensitySensor/ [AutomationComponentType]\n");
    printf("  |       +-- Assets/\n");
    printf("  |       |   +-- SensorHardware/ [FxAssetType]\n");
    printf("  |       +-- FunctionalEntities/\n");
    printf("  |       |   +-- DensityReadingFE/ [FunctionalEntityType]\n");
    printf("  |       |       +-- OutputData/Density (dynamic)\n");
    printf("  |       +-- ComponentCapabilities/\n");
    printf("  +-- NetworkInterfaces/\n");
    printf("      +-- enp0s31f6/\n");
    printf("          +-- LldpData/\n");
    printf("              +-- LocalSystemData/\n");
    printf("              +-- RemoteSystemsData/\n");
    printf("                  +-- RemoteSystem_1/ (RELY-10TSN12)\n");
    printf("========================================================\n");
    printf("Press Ctrl+C to stop\n\n");

    /* ─── Loop principale ────────────────────────────────────── */
    if (opts.rt)
        setupRealtime(opts.rtCore);

    while(running) {
        UA_Server_run_iterate(server, true);
    }

    printf("\n[SERVER] Shutting down...\n");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    printf("[SERVER] Stopped cleanly\n\n");

    return EXIT_SUCCESS;
}
