
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

#include "open62541.h"
#include "types_di_generated.h"
#include "types_uafx_data_generated.h"
#include "types_uafx_ac_generated.h"
#include "my_uafx_model.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ─── Namespace index di FX/AC nel server ─────────────────── */
#define FXAC_NS_URI   "http://opcfoundation.org/UA/FX/AC/"

/* NodeId dei tipi UAFX (numeric id fisso da nodeset XML) */
#define FXAC_ID_AUTOMATIONCOMPONENTTYPE  2
#define FXAC_ID_FXASSETTYPE              3
#define FXAC_ID_FUNCTIONALENTITYTYPE     4

#define NS_LOCAL 1
#define LDS_URL          "opc.tcp://192.168.17.73:4840"
#define SERVER_PUBLIC_URL "opc.tcp://192.168.17.73:4841"

static UA_NodeId connectionIdent, publishedDataSetIdent, writerGroupIdent,
    dataSetWriterIdent;
static volatile UA_Boolean running = true;
static UA_NodeId temperatureNodeId = {0};
static void stopHandler(int sig) {
    printf("\n[SERVER] Shutdown signal received\n");
    running = false;
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
                                        "enp43s0", "Physical interface enp43s0");
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

static void buildUAFXAddressSpace(UA_Server *server) {
    printf("[SERVER] Building UAFX AddressSpace...\n");

    UA_UInt16 nsFxAc = resolveNamespaceIndex(server, FXAC_NS_URI);
    printf("[SERVER]   Namespace FX/AC resolved: %d\n", nsFxAc);

    if(nsFxAc == 0) {
        printf("[SERVER] ERROR: FX/AC namespace not found. "
               "Did you call my_uafx_model() before buildUAFXAddressSpace()?\n");
        return;
    }

    /* ─── 1. FxRoot ──────────────────────────────────────────── */
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId fxRoot = addFolder(server, objectsFolder, nsFxAc, "FxRoot");
    printf("[SERVER]   + FxRoot created\n");

    /* ─── 2. AutomationComponent: TemperatureSensor ──────────── */
    UA_NodeId acNode = addTypedObject(server, fxRoot,
                                      NS_LOCAL, "TemperatureSensor",
                                      "Temperature Sensor AutomationComponent",
                                      nsFxAc, FXAC_ID_AUTOMATIONCOMPONENTTYPE);
    printf("[SERVER]   + AutomationComponent: TemperatureSensor "
           "[AutomationComponentType ns=%d;i=%d]\n",
           nsFxAc, FXAC_ID_AUTOMATIONCOMPONENTTYPE);

    addStringVariable(server, acNode, NS_LOCAL, "ConformanceName",
                      "urn:example:uafx:temperature-sensor:v1.0");
    addUInt32Variable(server, acNode, NS_LOCAL, "AggregatedHealth", 0);

    /* ─── 3. Assets/ ─────────────────────────────────────────── */
    UA_NodeId assetsFolder = addFolder(server, acNode, NS_LOCAL, "Assets");

    UA_NodeId assetNode = addTypedObject(server, assetsFolder,
                                         NS_LOCAL, "SensorHardware",
                                         "Physical temperature sensor hardware",
                                         nsFxAc, FXAC_ID_FXASSETTYPE);
    printf("[SERVER]   + Asset: SensorHardware [FxAssetType ns=%d;i=%d]\n",
           nsFxAc, FXAC_ID_FXASSETTYPE);

    addStringVariable(server, assetNode, NS_LOCAL, "Manufacturer",      "AcmeCorp");
    addStringVariable(server, assetNode, NS_LOCAL, "ManufacturerUri",   "https://www.acmecorp-sensors.com");
    addStringVariable(server, assetNode, NS_LOCAL, "Model",             "TempSensor-1000");
    addStringVariable(server, assetNode, NS_LOCAL, "ProductCode",       "TS-1000-V2");
    addStringVariable(server, assetNode, NS_LOCAL, "HardwareRevision",  "2.0");
    addStringVariable(server, assetNode, NS_LOCAL, "SoftwareRevision",  "1.3.5");
    addStringVariable(server, assetNode, NS_LOCAL, "DeviceClass",       "TemperatureSensor");
    addStringVariable(server, assetNode, NS_LOCAL, "SerialNumber",      "SN-12345-ABCD");

    /* ─── 4. FunctionalEntities/ ─────────────────────────────── */
    UA_NodeId feFolder = addFolder(server, acNode, NS_LOCAL, "FunctionalEntities");

    UA_NodeId feNode = addTypedObject(server, feFolder,
                                      NS_LOCAL, "TemperatureReadingFE",
                                      "Temperature reading functional entity",
                                      nsFxAc, FXAC_ID_FUNCTIONALENTITYTYPE);
    printf("[SERVER]   + FunctionalEntity: TemperatureReadingFE "
           "[FunctionalEntityType ns=%d;i=%d]\n",
           nsFxAc, FXAC_ID_FUNCTIONALENTITYTYPE);

    addStringVariable(server, feNode, NS_LOCAL, "AuthorUri",
                      "https://www.acmecorp-sensors.com");
    addStringVariable(server, feNode, NS_LOCAL, "AuthorAssignedIdentifier",
                      "TempSensor-FE-v1.0");
    addStringVariable(server, feNode, NS_LOCAL, "AuthorAssignedVersion",
                      "1.0.0.0");

    UA_NodeId outputFolder = addFolder(server, feNode, NS_LOCAL, "OutputData");
    temperatureNodeId = addTemperatureVariable(server, outputFolder, NS_LOCAL, "Temperature");
    printf("[SERVER]     + OutputData/Temperature (dynamic)\n");

    addFolder(server, feNode, NS_LOCAL, "ConnectionEndpoints");
    addUInt32Variable(server, feNode, NS_LOCAL, "OperationalHealth", 0);

    /* ─── 5. ComponentCapabilities/ ──────────────────────────── */
    UA_NodeId capFolder = addFolder(server, acNode, NS_LOCAL, "ComponentCapabilities");
    addUInt32Variable(server, capFolder, NS_LOCAL, "MaxConnections", 4);
    addUInt32Variable(server, capFolder, NS_LOCAL, "MinConnections", 0);

    printf("[SERVER] + UAFX AddressSpace build complete\n\n");

    /* ─── 6. NetworkInterfaces con LLDP (Part 82, 6.5.2) ────── */
    buildNetworkInterfaces(server);
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


/* ═══════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════ */

int main(void) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);
    srand(time(NULL));

    printf("\n");
    printf("========================================================\n");
    printf("  OPC UA FX Temperature Server (with LLDP)\n");
    printf("========================================================\n\n");

    /* ─── Crea server ────────────────────────────────────────── */
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_String transportProfile =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    UA_NetworkAddressUrlDataType networkAddressUrl =
        {UA_STRING("enp0s31f6") , UA_STRING("opc.udp://224.0.0.22:4840/")};

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
        UA_String_fromChars("urn:example:uafx:temperature-sensor-1");

    UA_LocalizedText_clear(&config->applicationDescription.applicationName);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Temperature Sensor");
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

    /* Run the server */

    /* ─── Avvia server ───────────────────────────────────────── */
    retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        printf("[ERROR] Server startup failed: %s\n",
               UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

         /* ─── Crea Connessione PubSub ───────────────────────────── */
     /* Più pragmatico: aggiungi controllo diretto */
addPubSubConnection(server, &transportProfile, &networkAddressUrl);
printf("[PUBSUB] connectionIdent: ns=%d;i=%d\n",
       connectionIdent.namespaceIndex, connectionIdent.identifier.numeric);

addPublishedDataSet(server);
printf("[PUBSUB] publishedDataSetIdent: ns=%d;i=%d\n",
       publishedDataSetIdent.namespaceIndex, publishedDataSetIdent.identifier.numeric);

addDataSetField(server);

addWriterGroup(server);
printf("[PUBSUB] writerGroupIdent: ns=%d;i=%d\n",
       writerGroupIdent.namespaceIndex, writerGroupIdent.identifier.numeric);

addDataSetWriter(server);
printf("[PUBSUB] dataSetWriterIdent: ns=%d;i=%d\n",
       dataSetWriterIdent.namespaceIndex, dataSetWriterIdent.identifier.numeric);
UA_Server_freezeWriterGroupConfiguration(server, writerGroupIdent);
UA_Server_setWriterGroupOperational(server, writerGroupIdent);
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
    while(running) {
        UA_Server_run_iterate(server, true);
    }

    printf("\n[SERVER] Shutting down...\n");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    printf("[SERVER] Stopped cleanly\n\n");

    return EXIT_SUCCESS;
}
