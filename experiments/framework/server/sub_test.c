/* ============================================================
 * uafx_temperature_server.c
 *
 * OPC UA FX server with correctly typed UAFX nodes:
 *   - AutomationComponent instantiated as AutomationComponentType (ns=FX/AC; i=2)
 *   - Asset instantiated as FxAssetType                           (ns=FX/AC; i=3)
 *   - FunctionalEntity instantiated as FunctionalEntityType       (ns=FX/AC; i=4)
 *
 * Includes NetworkInterfaces with LLDP data for topology discovery
 * per OPC 10000-82 sections 6.5.2 and 7.3.2.
 *
 * Build:
 *   gcc -o temp_server uafx_temperature_server.c my_uafx_types.c open62541.c -pthread
 * ============================================================ */

#define _GNU_SOURCE
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/server_pubsub.h>
#include <open62541/client_config_default.h>
#include <open62541/plugin/securitypolicy_default.h>
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
#include "rt_functions.h"
#include "cli.h"
#include "sks_helpers.h"
#include "uafx_common.h"

/* ─── FX/AC namespace index in the server ─────────────────── */
#define FXAC_NS_URI   "http://opcfoundation.org/UA/FX/AC/"

#define SKS_SERVER_URL_FALLBACK          "opc.tcp://192.168.17.112:4850"
#define SKS_APPLICATION_URI     "urn:example:uafx:sks-server"
#define DEMO_SECURITYGROUPNAME  "UafxSecurityGroup"

/* NodeIds of the UAFX types (fixed numeric id from the nodeset XML) */
#define FXAC_ID_AUTOMATIONCOMPONENTTYPE  2
#define FXAC_ID_FXASSETTYPE              3
#define FXAC_ID_FUNCTIONALENTITYTYPE     4

#define NS_LOCAL 1
#define SERVER_PUBLIC_URL "opc.tcp://edge-up-4:4941"

static volatile UA_Boolean running = true;
static UA_NodeId readerGroupIdent;
static UA_ClientConfig *sksClientConfigGlobal = NULL;
static char *sksServerUrl = NULL;

typedef struct {
    CliOptions *opts;
    clientCreds *creds;
} PubSubCtx;

static void stopHandler(int sig) {
    printf("\n[SERVER] Shutdown signal received\n");
    running = false;
}

static void
sksPullRequestCallback(UA_Server *server, UA_StatusCode sksPullRequestStatus,
                       void *context) {
    UA_PubSubState state = UA_PUBSUBSTATE_OPERATIONAL;
    UA_Server_getReaderGroupState(server, readerGroupIdent, &state);
    if(sksPullRequestStatus == UA_STATUSCODE_GOOD) {
        UA_StatusCode rcKey = UA_Server_setReaderGroupActivateKey(server, readerGroupIdent);
        printf("[SERVER] SKS: encryption key activated for ReaderGroup "
           "(activateKey status: %s)\n", UA_StatusCode_name(rcKey));
        UA_StatusCode rcOp = UA_Server_setReaderGroupOperational(server, readerGroupIdent);
        printf("[SERVER] SKS: ReaderGroup enable requested "
           "(enable status: %s)\n", UA_StatusCode_name(rcOp));
    } else if(sksPullRequestStatus != UA_STATUSCODE_GOOD) {
        printf("[SERVER] SKS: pull request FAILED: %s\n",
               UA_StatusCode_name(sksPullRequestStatus));
    }
}
 
/* ═══════════════════════════════════════════════════════════
 * Density Variable with Dynamic Callback
 * ═══════════════════════════════════════════════════════════ */

/* Simulated density: 998.0 ± 5.0 kg/m^3 (typical for water) */
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
 * Per OPC 10000-82 section 6.5.2:
 * - NetworkInterfaces/ folder under Objects
 * - Each physical interface as an object with
 *   IetfBaseNetworkInterfaceType-like properties (AdminStatus, PhysAddress, Speed)
 * - LldpData/ holding LLDP neighbor data (Part 82, 7.3.2)
 *
 * Layout:
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

    /* ============ enp43s0 → neighbor edge-up-3 ============ */
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

    /* ============ enp0s31f6 → neighbor RELY-10TSN12 ============ */
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

    /* ============ wlp44s0 → no neighbor ============ */
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

static void setupSubscriber(UA_Server *server, PubSubCtx *subContext) {
    printf("[SERVER] Setting up PubSub Subscriber...\n");

    /* ─── 1. PubSubConnection (same multicast as the publisher) ── */
    UA_PubSubConnectionConfig connConfig;
    memset(&connConfig, 0, sizeof(connConfig));
    connConfig.name = UA_STRING("UDP Multicast Subscriber Connection");
    connConfig.transportProfileUri =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-eth-uadp");
    connConfig.enabled = true;

    UA_NetworkAddressUrlDataType addr;
    addr.networkInterface = UA_STRING(subContext->opts->iface);
    addr.url = UA_STRING(subContext->opts->url);

    UA_Variant_setScalar(&connConfig.address, &addr,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

    //connConfig.publisherIdType = UA_PUBLISHERIDTYPE_UINT16;
    //connConfig.publisherId.uint16 = 2234;  /* different from the publisher's connection id */

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

    if (subContext->opts->sks) {
        UA_ServerConfig *config = UA_Server_getConfig(server);
        rgConfig.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        rgConfig.securityGroupId = UA_STRING(DEMO_SECURITYGROUPNAME);
        rgConfig.securityPolicy = &config->pubSubConfig.securityPolicies[0];
    }

    UA_NodeId rgId;
    rc = UA_Server_addReaderGroup(server, connId, &rgConfig, &rgId);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[SERVER]   ReaderGroup FAILED: %s\n", UA_StatusCode_name(rc));
        return;
    }
    readerGroupIdent = rgId;
    printf("[SERVER]   + ReaderGroup\n");
    
    if (subContext->opts->sks) {
        sksServerUrl = resolveSksUrlFromLds(subContext->creds);
        if (sksServerUrl) {
            printf("[INFO] SKS server URL found in LDS: %s\n", sksServerUrl);
        } else {
            printf("[WARNING] SKS server URL not found in LDS, using fallback: %s\n", SKS_SERVER_URL_FALLBACK);
            sksServerUrl = (char *)SKS_SERVER_URL_FALLBACK;
        }
        UA_Server_setSksClient(server, rgConfig.securityGroupId,
                        sksClientConfigGlobal, sksServerUrl,
                        sksPullRequestCallback, &readerGroupIdent);
    }
    /* ─── 3. DataSetReader ────────────────────────────── */
    UA_DataSetReaderConfig dsrConfig;
    memset(&dsrConfig, 0, sizeof(dsrConfig));
    dsrConfig.name = UA_STRING("TemperatureReader");

    /* Filter: only accept messages from PublisherId 2234 (edge-up-3) */
    UA_UInt16 pubId = 2234;
    //UA_Variant_setScalar(&dsrConfig.publisherId, &pubId, &UA_TYPES[UA_TYPES_UINT16]);
    dsrConfig.publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
    dsrConfig.publisherId.id.uint16 = pubId;

    /* Must match the publisher's WriterGroup/DataSetWriter ids exactly. */
    dsrConfig.writerGroupId   = 100;
    dsrConfig.dataSetWriterId = 62541;
    /* DataSetMetaData: describes the DataSet's expected content */
    UA_DataSetMetaDataType_init(&dsrConfig.dataSetMetaData);
    dsrConfig.dataSetMetaData.name = UA_STRING("TemperatureDataSet");
    dsrConfig.dataSetMetaData.fieldsSize = 1;
    dsrConfig.dataSetMetaData.fields = (UA_FieldMetaData *)
        UA_calloc(1, sizeof(UA_FieldMetaData));

    UA_FieldMetaData *field = &dsrConfig.dataSetMetaData.fields[0];
    UA_FieldMetaData_init(field);
    field->builtInType = UA_NS0ID_FLOAT;
    field->dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    field->valueRank = -1;  /* scalar */
    field->name = UA_STRING("Temperature");

    /* UADP message settings: must match the publisher's exactly */
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

    /* ─── 4. TargetVariables: map the received field to the local variable ── */
    UA_FieldTargetDataType targetVar;
    memset(&targetVar, 0, sizeof(UA_FieldTargetDataType));
    targetVar.attributeId = UA_ATTRIBUTEID_VALUE;
    targetVar.targetNodeId =   UA_NODEID_NUMERIC(NS_LOCAL, 50001);/* InputData/Temperature node id */;
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
    /* With SKS, the ReaderGroup is made operational later by
     * sksPullRequestCallback, once the decryption key is actually active. */
    if (!subContext->opts->sks)
        UA_Server_setReaderGroupOperational(server, rgId);
}


 /* Callback executed when the client calls the method */
static UA_StatusCode startSubscriberCallback(
        UA_Server *server, const UA_NodeId *sessionId,
        void *sessionContext, const UA_NodeId *methodId,
        void *methodContext, const UA_NodeId *objectId,
        void *objectContext, size_t inputSize,
        const UA_Variant *input, size_t outputSize,
        UA_Variant *output) {

    PubSubCtx *subContext = (PubSubCtx *)(methodContext);
    printf("[SERVER] StartSubscriber called — configuring PubSub...\n");

    setupSubscriber(server, subContext);

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

static void buildUAFXAddressSpace(UA_Server *server, PubSubCtx *subContext) {
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
        startSubscriberCallback, 0, NULL, 0, NULL, subContext, NULL);

    /* ─── 3. Assets/ — use the folder instantiated by the type ─────── */
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

    /* ─── 4. FunctionalEntities/ — use the folder instantiated by the type ── */
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

    /* OutputData/InputData/ConnectionEndpoints are not instantiated by the type → create manually */
    UA_NodeId outputFolder = addFolder(server, feNode, NS_LOCAL, "OutputData");
    addDensityVariable(server, outputFolder, NS_LOCAL, "Density");
    printf("[SERVER]     + OutputData/Density\n");

    UA_NodeId inputFolder = addFolder(server, feNode, NS_LOCAL, "InputData");
    addInputVariable(server, inputFolder, NS_LOCAL, "Temperature", subContext->opts->rtLog);
    printf("[SERVER]     + InputData/Temperature\n");

    addFolder(server, feNode, NS_LOCAL, "ConnectionEndpoints");

    /* ─── 5. ComponentCapabilities/ — use the folder instantiated by the type ── */
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

    if (opts.rt)
        lockMemoryRT();

    clientCreds creds;
    loadClientCredentials(opts.ldsUrl, opts.certDir, "subscriber", "urn:example:uafx:density-sensor-1", &creds);

    /* ─── Create server ────────────────────────────────────────── */
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);

    /* UA_DataTypeArray is a singly linked list (each node's first field points
     * to the next); chain AC -> Data -> DI so the server can decode all three
     * custom type sets. */
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

    char *serverCertPath = buildCertPath(opts.certDir, "density_server.cert.der");
    char *serverKeyPath  = buildCertPath(opts.certDir, "density_server.key.der");
    UA_ByteString serverCert = loadFile(serverCertPath);
    UA_ByteString serverKey  = loadFile(serverKeyPath);
    
    UA_ServerConfig_setDefaultWithSecurityPolicies(config, 4941, &serverCert, &serverKey, &creds.caCert, 1, &creds.crl, 1, NULL, 0);

    /* AccessControl: X.509 certificate authentication + restrict
     * Method calls to authenticated sessions only. Uses the same
     * activateSession_sks() already validated on the SKS server --
     * channel cert must equal user token cert, both verified against
     * the trustlist above. No anonymous access, no passwords. */
    static const char *operatorDeviceNames[] = { "asyncua" };
    UafxRoleConfig roleConfig;
    loadRoleConfig(opts.certDir, operatorDeviceNames, 1, &roleConfig);

    config->accessControl.activateSession = activateSession;
    config->accessControl.getUserExecutableOnObject = getUserExecutableOnObject_app;
    config->accessControl.getUserAccessLevel = getUserAccessLevel_app;
    setRoleConfig(&roleConfig);

    UA_String hostname = UA_String_fromChars(SERVER_PUBLIC_URL);
    config->applicationDescription.applicationType = UA_APPLICATIONTYPE_SERVER;

    if (opts.sks) {
        config->pubSubConfig.securityPolicies = (UA_PubSubSecurityPolicy *)UA_malloc(sizeof(UA_PubSubSecurityPolicy));
        config->pubSubConfig.securityPoliciesSize = 1;
        UA_PubSubSecurityPolicy_Aes256Ctr(config->pubSubConfig.securityPolicies, config->logging);
        sksClientConfigGlobal = encryptedSksClient(&creds);
    }

    UA_String_clear(&config->applicationDescription.applicationUri);
    UA_String_copy(&creds.applicationUri, &config->applicationDescription.applicationUri);

    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Density Sensor");
    config->applicationDescription.discoveryUrlsSize = 1;
    config->applicationDescription.discoveryUrls = (UA_String*)UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]);
    config->applicationDescription.discoveryUrls[0] = hostname;

    
    config->mdnsEnabled = UA_FALSE;
    /*
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
#endif*/

    /* ─── Load UAFX types from the generated nodeset ────────── */
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

    PubSubCtx subContext;
    subContext.opts = &opts;
    subContext.creds = &creds;
    /* ─── Build AddressSpace ────────────────────────────── */
    buildUAFXAddressSpace(server, &subContext);

    if (opts.autostart)
        setupSubscriber(server, &subContext);

    /* ─── Start server ───────────────────────────────────────── */
    retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("[ERROR] Server startup failed: %s\n",
               UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }
	/* Static subscriber setup (unused, kept for reference) */
	//setupSubscriber(server);


    /* ─── LDS registration ──────────────────────────────── */
    /*UA_StatusCode rc = registerToLdsSecurely(server, &creds);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[WARNING] Shared LDS registration init failed: %s\n", UA_StatusCode_name(rc));
    }*/
    UA_UInt64 ldsRegisterCallbackId = 0;
    void *ldsRegisterCtx = NULL;
    startPeriodicLdsRegistration(server, &creds, 5*60*1000.0, &ldsRegisterCallbackId, &ldsRegisterCtx);



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

    /* ─── Main loop ────────────────────────────────────── */
    while(running) {
        UA_Server_run_iterate(server, true);
    }

    printf("\n[SERVER] Shutting down...\n");
    if(ldsRegisterCallbackId != 0)
        stopPeriodicLdsRegistration(server, ldsRegisterCallbackId, ldsRegisterCtx);

    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    clearClientCredentials(&creds);
    clearRoleConfig(&roleConfig);
    printf("[SERVER] Stopped cleanly\n\n");

    return EXIT_SUCCESS;
}
