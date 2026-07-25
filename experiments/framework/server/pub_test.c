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
#include <sched.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include "establish_connection.h"
#include "rt_functions.h"
#include "cli.h"
#include "sks_helpers.h"
#include "uafx_common.h"


/* ─── Namespace index di FX/AC nel server ─────────────────── */
#define FXAC_NS_URI   "http://opcfoundation.org/UA/FX/AC/"

#define SKS_SERVER_URL_FALLBACK         "opc.tcp://192.168.17.112:4850"
#define SKS_APPLICATION_URI     "urn:example:uafx:sks-server"
#define DEMO_SECURITYGROUPNAME  "UafxSecurityGroup"

/* NodeId dei tipi UAFX (numeric id fisso da nodeset XML) */
#define FXAC_ID_AUTOMATIONCOMPONENTTYPE  2
#define FXAC_ID_FXASSETTYPE              3
#define FXAC_ID_FUNCTIONALENTITYTYPE     4

#define NS_LOCAL 1
#define SERVER_PUBLIC_URL "opc.tcp://edge-up-3:4941"
#define APPLICATION_URI "urn:example:uafx:temperature-sensor-1"

#define ETH_TRANSPORT_PROFILE "http://opcfoundation.org/UA-Profile/Transport/pubsub-eth-uadp"

#define CLOCKID CLOCK_TAI
#define DEFAULT_SOCKET_PRIORITY 6

static UA_NodeId connectionIdent, publishedDataSetIdent, writerGroupIdent,
    dataSetWriterIdent;
static UA_String transportProfile;
static UA_NetworkAddressUrlDataType networkAddressUrl;
static volatile UA_Boolean running = true;
static UA_NodeId temperatureNodeId = {0};
static UA_ClientConfig *sksClientConfigGlobal = NULL;
static char *sksServerUrl = NULL;

static UA_UInt32 socketPriority    = DEFAULT_SOCKET_PRIORITY;
static UA_Boolean disableSoTxtime = true;
static UA_Int32 clockSource = CLOCK_TAI;

static UA_Float     temperatureValue      = 20.0f;
static UA_DataValue *temperatureDataValueRT = NULL;

static timer_t writerGroupTimer;

static UA_Server *server = NULL;

static volatile int sksKeyActive = 0; 

typedef struct {
    CliOptions *opts;
    clientCreds *creds;
} PubSubCtx;

static void stopHandler(int sig) {
    printf("\n[SERVER] Shutdown signal received\n");
    running = false;
}

static struct timespec lastTrigger = {0};

static void
writerGroupPublishTrigger(union sigval signal) {
    if (!__atomic_load_n(&sksKeyActive, __ATOMIC_SEQ_CST))
        return;
    struct timespec now;
    clock_gettime(CLOCKID, &now);
    if (lastTrigger.tv_sec != 0) {
        long deltaNs = (now.tv_sec - lastTrigger.tv_sec) * 1000000000L
                     + (now.tv_nsec - lastTrigger.tv_nsec);
        printf("[JITTER] delta=%ld ns\n", deltaNs);
    }
    lastTrigger = now;
    UA_Server_triggerWriterGroupPublish(server, writerGroupIdent);
}

static UA_StatusCode
writerGroupStateMachine(UA_Server *server, const UA_NodeId componentId,
                        void *componentContext, UA_PubSubState *state,
                        UA_PubSubState targetState) {
    UA_WriterGroupConfig config;
    struct itimerspec interval;
    memset(&interval, 0, sizeof(interval));

    if (targetState == *state)
        return UA_STATUSCODE_GOOD;

    switch (targetState) {
        case UA_PUBSUBSTATE_ERROR:
        case UA_PUBSUBSTATE_DISABLED:
        case UA_PUBSUBSTATE_PAUSED:
            timer_settime(writerGroupTimer, 0, &interval, NULL); /* interval=0 -> arrêt */
            *state = targetState;
            break;

        case UA_PUBSUBSTATE_PREOPERATIONAL:
        case UA_PUBSUBSTATE_OPERATIONAL:
            if (*state == UA_PUBSUBSTATE_OPERATIONAL)
                break;
            UA_Server_getWriterGroupConfig(server, writerGroupIdent, &config);
            interval.it_interval.tv_sec = (time_t)(config.publishingInterval / 1000);
            interval.it_interval.tv_nsec =
                ((long long)(config.publishingInterval * 1000 * 1000)) % (1000 * 1000 * 1000);
            interval.it_value = interval.it_interval;
            UA_WriterGroupConfig_clear(&config);
            if (timer_settime(writerGroupTimer, 0, &interval, NULL) != 0)
                return UA_STATUSCODE_BADINTERNALERROR;
            *state = UA_PUBSUBSTATE_OPERATIONAL;
            break;

        default:
            return UA_STATUSCODE_BADINTERNALERROR;
    }
    return UA_STATUSCODE_GOOD;
}





static void
sksPullRequestCallback(UA_Server *server, UA_StatusCode sksPullRequestStatus,
                       void *context) {
    UA_NodeId writerGroupIdent = *(UA_NodeId *)context;
    UA_PubSubState state = UA_PUBSUBSTATE_OPERATIONAL;
    UA_Server_getWriterGroupState(server, writerGroupIdent, &state);
    if(sksPullRequestStatus == UA_STATUSCODE_GOOD) {
        UA_Server_setWriterGroupActivateKey(server, writerGroupIdent);
        __atomic_store_n(&sksKeyActive, 1, __ATOMIC_SEQ_CST);
        printf("[SERVER] SKS: encryption key activated for WriterGroup\n");
    } else if(sksPullRequestStatus != UA_STATUSCODE_GOOD) {
        printf("[SERVER] SKS: pull request FAILED: %s\n",
               UA_StatusCode_name(sksPullRequestStatus));
    }
}


/* ═══════════════════════════════════════════════════════════
 * Temperature Variable with Dynamic Callback
 * ═══════════════════════════════════════════════════════════ */

static void updateTemperatureRT(void) {
    temperatureValue = 20.0f + ((rand() % 1000) - 500) / 100.0f;
}

static UA_NodeId addTemperatureVariable(UA_Server *server, UA_NodeId parent,
                                        UA_UInt16 ns, const char *name) {
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = lt(name);
    attr.description = lt("Current temperature reading in degrees Celsius");

    UA_Variant_setScalar(&attr.value, &temperatureValue, &UA_TYPES[UA_TYPES_FLOAT]);
    attr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_NodeId newNode = UA_NODEID_NULL;
    UA_Server_addVariableNode(server, UA_NODEID_NULL, parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), qn(ns, name),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, NULL, &newNode);

    temperatureDataValueRT = UA_DataValue_new();
    UA_Variant_setScalar(&temperatureDataValueRT->value, &temperatureValue,
                          &UA_TYPES[UA_TYPES_FLOAT]);
    temperatureDataValueRT->hasValue = true;

    UA_StatusCode retval = UA_Server_setVariableNode_externalValueSource(server, newNode, &temperatureDataValueRT, NULL);
    if (retval != UA_STATUSCODE_GOOD) {
        printf("[ERROR] externalValueSource failed: %s\n", UA_StatusCode_name(retval));
    }

    addStringVariable(server, newNode, ns, "EngineeringUnits", "\xC2\xB0""C");

    return newNode;
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
                                    "LLDP Local System (edge-up-3)");
    addStringVariable(server, local, NS_LOCAL, "ChassisId",          "00:07:32:ae:79:13");
    addUInt32Variable(server, local, NS_LOCAL, "ChassisIdSubtype",   4);
    addStringVariable(server, local, NS_LOCAL, "SysName",            "edge-up-3");
    addStringVariable(server, local, NS_LOCAL, "SysDescr",
                      "Ubuntu 24.04.4 LTS Linux 6.8.1-1015-realtime x86_64");
    addStringVariable(server, local, NS_LOCAL, "MgmtAddress",        "192.168.100.3");
    addStringVariable(server, local, NS_LOCAL, "SystemCapabilities", "Bridge,Router,Wlan");
    addStringVariable(server, local, NS_LOCAL, "PortId",             portId);
    addUInt32Variable(server, local, NS_LOCAL, "PortIdSubtype",      portIdSubtype);
}

static void buildNetworkInterfaces(UA_Server *server) {
    printf("[SERVER] Building NetworkInterfaces (edge-up-3)...\n");

    UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId niFolder = addFolder(server, objects, NS_LOCAL, "NetworkInterfaces");

    /* ============ enp43s0 → vicino edge-up-4 ============ */
    {
        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        "enp43s0", "Physical interface enp43s0.10");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  "up");
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", "00:07:32:ae:79:13");
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 1000);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        addLocalSystemData(server, lldp, "00:07:32:ae:79:13", 3);

        UA_NodeId rsFolder = addFolder(server, lldp, NS_LOCAL, "RemoteSystemsData");
        UA_NodeId rs = addBaseObject(server, rsFolder, NS_LOCAL,
                                     "RemoteSystem_1", "LLDP neighbor on enp43s0");
        addStringVariable(server, rs, NS_LOCAL, "ChassisId",          "00:07:32:ae:79:1d");
        addUInt32Variable(server, rs, NS_LOCAL, "ChassisIdSubtype",   4);
        addStringVariable(server, rs, NS_LOCAL, "SysName",            "edge-up-4");
        addStringVariable(server, rs, NS_LOCAL, "SysDescr",
                          "Ubuntu 24.04.4 LTS Linux 6.8.1-1015-realtime x86_64");
        addStringVariable(server, rs, NS_LOCAL, "MgmtAddress",        "192.168.100.4");
        addStringVariable(server, rs, NS_LOCAL, "PortId",             "00:07:32:ae:79:1d");
        addUInt32Variable(server, rs, NS_LOCAL, "PortIdSubtype",      3);
        addStringVariable(server, rs, NS_LOCAL, "PortDescr",          "enp43s0");
        addStringVariable(server, rs, NS_LOCAL, "SystemCapabilities", "Bridge,Router,Wlan");
        addUInt32Variable(server, rs, NS_LOCAL, "TimeToLive",         120);
    }

    /* ============ enp0s31f6 → vicino RELY-10TSN12 PORT_4 ============ */
    {
        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        "enp0s31f6", "Physical interface enp0s31f6");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  "up");
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", "00:07:32:ae:79:12");
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 1000);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        addLocalSystemData(server, lldp, "00:07:32:ae:79:12", 3);

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
        addStringVariable(server, rs, NS_LOCAL, "PortDescr",          "PORT_4");
        addStringVariable(server, rs, NS_LOCAL, "SystemCapabilities", "Bridge");
        addUInt32Variable(server, rs, NS_LOCAL, "TimeToLive",         40);
    }

    /* ============ wlp44s0 → nessun vicino ============ */
    {
        UA_NodeId iface = addBaseObject(server, niFolder, NS_LOCAL,
                                        "wlp44s0", "Wireless interface wlp44s0");
        addStringVariable(server, iface, NS_LOCAL, "AdminStatus", "up");
        addStringVariable(server, iface, NS_LOCAL, "OperStatus",  "up");
        addStringVariable(server, iface, NS_LOCAL, "PhysAddress", "4c:b0:4a:9e:28:84");
        addUInt32Variable(server, iface, NS_LOCAL, "Speed", 0);

        UA_NodeId lldp = addFolder(server, iface, NS_LOCAL, "LldpData");
        addLocalSystemData(server, lldp, "4c:b0:4a:9e:28:84", 3);
        addFolder(server, lldp, NS_LOCAL, "RemoteSystemsData");
    }

    printf("[SERVER] + NetworkInterfaces: enp43s0, enp0s31f6, wlp44s0\n");
    printf("[SERVER]   ChassisId (shared): 00:07:32:ae:79:13\n\n");
}


/* -----7. Pub Static implementation exemple  */
	//addPubSubCOnnections
    static void addPubSubConnection(UA_Server *server, UA_String *transportProfile,
                    UA_NetworkAddressUrlDataType *networkAddressUrl){
      UA_PubSubConnectionConfig connectionConfig;
      memset(&connectionConfig, 0, sizeof(connectionConfig));
      connectionConfig.name = UA_STRING("ETH Connection 1");
      connectionConfig.transportProfileUri = *transportProfile;
      UA_Variant_setScalar(&connectionConfig.address, networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    /* Changed to static publisherId from random generation to identify
     * the publisher on Subscriber side */
    	connectionConfig.publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
    	connectionConfig.publisherId.id.uint16 = 2234;

            /* Connection options are given as Key/Value Pairs - Sockprio and Txtime */
        UA_KeyValueMap_setScalar(&connectionConfig.connectionProperties,
                UA_QUALIFIEDNAME(0, "priority"), &socketPriority, &UA_TYPES[UA_TYPES_UINT32]);
        UA_KeyValueMap_setScalar(&connectionConfig.connectionProperties,
                UA_QUALIFIEDNAME(0, "txtime-enable"), &disableSoTxtime, &UA_TYPES[UA_TYPES_BOOLEAN]);

	    connectionConfig.enabled = false;
    	UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);

        UA_KeyValueMap_clear(&connectionConfig.connectionProperties);
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

static void addWriterGroup(UA_Server *server, void *context) {
    PubSubCtx *pubContext = (PubSubCtx *)context;
    UA_WriterGroupConfig writerGroupConfig;
    memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
    writerGroupConfig.name = UA_STRING("Demo WriterGroup");
    writerGroupConfig.publishingInterval = (UA_Duration)pubContext->opts->cycleTime/1000000;
    writerGroupConfig.writerGroupId = 100;
    writerGroupConfig.enabled = false;
    writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
    writerGroupConfig.customStateMachine = writerGroupStateMachine;


    if (pubContext->opts->sks) {
        UA_ServerConfig *config = UA_Server_getConfig(server);
        writerGroupConfig.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
        writerGroupConfig.securityGroupId = UA_STRING(DEMO_SECURITYGROUPNAME);
        writerGroupConfig.securityPolicy = &config->pubSubConfig.securityPolicies[0];
    }


    UA_UadpWriterGroupMessageDataType writerGroupMessage;
    UA_UadpWriterGroupMessageDataType_init(&writerGroupMessage);
    writerGroupMessage.networkMessageContentMask =
        (UA_UadpNetworkMessageContentMask)(UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_SEQUENCENUMBER |
                                           UA_UADPNETWORKMESSAGECONTENTMASK_TIMESTAMP);

    static UA_Double publishingOffsetValue;
    publishingOffsetValue = 0.05; //(pubContext->opts->cycleTime / 1000000.0) * 0.6;
    writerGroupMessage.publishingOffset = &publishingOffsetValue;
    writerGroupMessage.publishingOffsetSize = 1;
    
    UA_ExtensionObject_setValue(&writerGroupConfig.messageSettings, &writerGroupMessage,
                                &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE]);

    UA_Server_addWriterGroup(server, connectionIdent, &writerGroupConfig, &writerGroupIdent);

    if (pubContext->opts->sks) {
        UA_Server_setSksClient(server, writerGroupConfig.securityGroupId,
                        sksClientConfigGlobal, sksServerUrl,
                        sksPullRequestCallback, &writerGroupIdent);
    }

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

    /* Enable DataSetMessage sequenceNumber so the subscriber can detect
     * replayed messages (anti-replay check in ua_pubsub_reader.c). */
    UA_UadpDataSetWriterMessageDataType writerMessageSettings;
    UA_UadpDataSetWriterMessageDataType_init(&writerMessageSettings);
    writerMessageSettings.dataSetMessageContentMask = (UA_UadpNetworkMessageContentMask)(
        UA_UADPDATASETMESSAGECONTENTMASK_SEQUENCENUMBER);
    UA_ExtensionObject_setValue(&dataSetWriterConfig.messageSettings,
                                &writerMessageSettings,
                                &UA_TYPES[UA_TYPES_UADPDATASETWRITERMESSAGEDATATYPE]);

    UA_Server_addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent,
                               &dataSetWriterConfig, &dataSetWriterIdent);

}

static UA_StatusCode startPublisherCallback(
        UA_Server *server, const UA_NodeId *sessionId,
        void *sessionContext, const UA_NodeId *methodId,
        void *methodContext, const UA_NodeId *objectId,
        void *objectContext, size_t inputSize,
        const UA_Variant *input, size_t outputSize,
        UA_Variant *output) {
    PubSubCtx *pubContext = (PubSubCtx *)methodContext;
printf("[DEBUG] pubContext=%p pubContext->opts=%p cycleTime=%ld\n",
       (void*)pubContext, (void*)pubContext->opts, pubContext->opts->cycleTime);
    addPubSubConnection(server, &transportProfile, &networkAddressUrl);
    addPublishedDataSet(server);
    addDataSetField(server);
    sksServerUrl = resolveSksUrlFromLds(pubContext->creds);
    if (sksServerUrl) {
        printf("[INFO] SKS server URL found in LDS: %s\n", sksServerUrl);
    } else {
        printf("[WARNING] SKS server URL not found in LDS, using fallback: %s\n", SKS_SERVER_URL_FALLBACK);
        sksServerUrl = (char *)SKS_SERVER_URL_FALLBACK;
    }
    addWriterGroup(server, pubContext);
    addDataSetWriter(server);

    UA_Server_enableDataSetWriter(server, dataSetWriterIdent);
    UA_Server_enableWriterGroup(server, writerGroupIdent);
    //UA_Server_setWriterGroupOperational(server, writerGroupIdent);
    UA_Server_enablePubSubConnection(server, connectionIdent);
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
 *       +-- enp0s31f6/
 *           +-- LldpData/
 *               +-- LocalSystemData/
 *               +-- RemoteSystemsData/
 *                   +-- RemoteSystem_1/ (RELY-10TSN12)
 * ═══════════════════════════════════════════════════════════ */

static void buildUAFXAddressSpace(UA_Server *server, PubSubCtx *pubContext) {
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
        startPublisherCallback, 0, NULL, 0, NULL, pubContext, NULL);
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

    /* ── Trova OutputData/ già creata dall'istanziazione di FunctionalEntityType ── */
UA_NodeId outputFolder = addFolder(server, feNode, NS_LOCAL, "OutputData");
    temperatureNodeId = addTemperatureVariable(server, outputFolder, NS_LOCAL, "Temperature");

    addFolder(server, feNode, NS_LOCAL, "InputData");
   // addInputVariable(server,inputFolder, NS_LOCAL, "Density");
   // printf("[SERVER]   + InputData/ReceivedTemperature (target for PubSub subscriber)\n");

    addFolder(server, feNode, NS_LOCAL, "ConnectionEndpoints");

    /* ───  ComponentCapabilities/ ──────────────────────────── */
    UA_NodeId capFolder = addFolder(server, acNode, NS_LOCAL, "ComponentCapabilities");
    addUInt32Variable(server, capFolder, NS_LOCAL, "MaxConnections", 4);
    addUInt32Variable(server, capFolder, NS_LOCAL, "MinConnections", 0);

    printf("[SERVER] + UAFX AddressSpace build complete\n\n");

    /* ─── 6. NetworkInterfaces con LLDP (Part 82, 6.5.2) ────── */
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
    int rtResult;

    if (opts.rt) {
        rtResult = lockMemoryRT();
        if (rtResult == -1) {
            printf("Memory lock failed (ran as root ?)");
            return 1;
        }
    }

    if (opts.rtCore != NO_RT_CORE) {
        rtResult = setupCpuAffinity(opts.rtCore);
        if (rtResult == -1) {
            printf("Unable to change CPU affinity (ran as root ?)");
            return 1;
        }
    }

    if (opts.schedPrio != NO_SCHED_PRIO) {
        rtResult = setupSchedulePriority(opts.schedPrio);
        if (rtResult == -1) {
            printf("Unable to change schedule priority (ran as root ?)");
            return 1;
        }
    }


    clientCreds creds;
    loadClientCredentials(opts.ldsUrl, opts.certDir, "publisher", "urn:example:uafx:temperature-sensor-1", &creds);

    struct sigevent sigev;
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_THREAD;
    sigev.sigev_notify_function = writerGroupPublishTrigger;
    timer_create(CLOCKID, &sigev, &writerGroupTimer);

    /* ─── Crea server ────────────────────────────────────────── */
    server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
     transportProfile = UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-eth-uadp");
     networkAddressUrl.networkInterface = UA_STRING(opts.iface);
    networkAddressUrl.url = UA_STRING(opts.url);
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

    char *serverCertPath = buildCertPath(opts.certDir, "temperature_server.cert.der");
    char *serverKeyPath  = buildCertPath(opts.certDir, "temperature_server.key.der");
    UA_ByteString serverCert = loadFile(serverCertPath);
    UA_ByteString serverKey  = loadFile(serverKeyPath);

    UA_ServerConfig_setDefaultWithSecurityPolicies(config, 4941, &serverCert, &serverKey, &creds.caCert, 1, &creds.crl, 1, NULL, 0);

    UA_KeyValueMap_setScalar(&config->eventLoop->params, UA_QUALIFIEDNAME(0, "clock-source-monotonic"), &clockSource, &UA_TYPES[UA_TYPES_INT32]);

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
    config->applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Temperature Sensor");
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

    /* ─── Carica i tipi UAFX dal nodeset generato ────────────── */
    printf("[SERVER] Loading UAFX nodesets...\n");
    UA_StatusCode retval = namespace_di_generated(server);
    UA_StatusCode retval_data = namespace_uafx_data_generated(server);
    UA_StatusCode retval_ac = namespace_uafx_ac_generated(server);

    if(retval != UA_STATUSCODE_GOOD || retval_data != UA_STATUSCODE_GOOD || retval_ac != UA_STATUSCODE_GOOD) {
        printf("[WARNING] Address Space loaded with some missing sub-nodes (Code: %s).\n",
               UA_StatusCode_name(retval));
        printf("[WARNING] This is normal for massive UAFX NodeSets. Continuing anyway...\n\n");
    } else {
        printf("[SERVER] + UAFX types loaded perfectly\n\n");
    }

    PubSubCtx pubContext;
    pubContext.opts = &opts;
    pubContext.creds = &creds;
    /* ─── Costruisci AddressSpace ────────────────────────────── */
    buildUAFXAddressSpace(server, &pubContext);


    /* ─── Avvia server ───────────────────────────────────────── */
    retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("[ERROR] Server startup failed: %s\n",
               UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    if (opts.autostart) {
        addPubSubConnection(server, &transportProfile, &networkAddressUrl);
        addPublishedDataSet(server);
        addDataSetField(server);
        sksServerUrl = resolveSksUrlFromLds(&creds);
        if (sksServerUrl) {
            printf("[INFO] SKS server URL found in LDS: %s\n", sksServerUrl);
        } else {
            printf("[WARNING] SKS server URL not found in LDS, using fallback: %s\n", SKS_SERVER_URL_FALLBACK);
            sksServerUrl = (char *)SKS_SERVER_URL_FALLBACK;
        }
        addWriterGroup(server, &pubContext);
        addDataSetWriter(server);

        UA_Server_enableDataSetWriter(server, dataSetWriterIdent);
        UA_Server_enableWriterGroup(server, writerGroupIdent);
        UA_Server_enablePubSubConnection(server, connectionIdent);
        printf("[SERVER] Publisher started automatically\n");
    }

    /* ─── Registrazione all'LDS ──────────────────────────────── */
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
    printf("  |   +-- TemperatureSensor/ [AutomationComponentType]\n");
    printf("  |       +-- Assets/\n");
    printf("  |       |   +-- SensorHardware/ [FxAssetType]\n");
    printf("  |       +-- FunctionalEntities/\n");
    printf("  |       |   +-- TemperatureReadingFE/ [FunctionalEntityType]\n");
    printf("  |       |       +-- OutputData/Temperature (dynamic)\n");
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
    if (opts.rt) {
        struct timespec next;
        clock_gettime(CLOCKID, &next);
        while(running) {
            next.tv_nsec += opts.cycleTime;
            while(next.tv_nsec >= 1000000000L) {
                next.tv_nsec -= 1000000000L;
                next.tv_sec++;
            }

            int rc;
            do {
                rc = clock_nanosleep(CLOCKID, TIMER_ABSTIME, &next, NULL);
            } while(rc == EINTR);
            updateTemperatureRT();
            UA_Server_run_iterate(server, false);
        }
    } else {
        while(running) {
            UA_Server_run_iterate(server, true);
        }
    }

    printf("\n[SERVER] Shutting down...\n");
    if(ldsRegisterCallbackId != 0)
        stopPeriodicLdsRegistration(server, ldsRegisterCallbackId, ldsRegisterCtx);

    UA_Server_run_shutdown(server);

    if (temperatureDataValueRT)
        UA_DataValue_delete(temperatureDataValueRT);

    timer_delete(writerGroupTimer);
    UA_Server_delete(server);
    clearClientCredentials(&creds);
    clearRoleConfig(&roleConfig);
    printf("[SERVER] Stopped cleanly\n\n");

    return EXIT_SUCCESS;
}
