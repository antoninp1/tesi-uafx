/* ============================================================
 * connection_manager.c
 *
 * Implementazione del ConnectionManager esterno (Part 81 §5.6).
 *
 * Flusso per ogni connessione:
 *   1. Risolve endpoint URL dal TopologyGraph tramite chassisId
 *   2. Genera PublisherId, WriterGroupId, DataSetWriterId univoci
 *   3. Sceglie indirizzo multicast
 *   4. Costruisce UA_PubSubCommunicationConfigurationDataType
 *      per il Publisher (con PDS + WriterGroup + DataSetWriter)
 *   5. Chiama EstablishConnections sul server Publisher
 *   6. Costruisce la configurazione speculare per il Subscriber
 *      (con ReaderGroup + DataSetReader + TargetVariables)
 *   7. Chiama EstablishConnections sul server Subscriber
 *   8. Aggiorna TopologyGraph con la connessione logica
 * ============================================================ */
#include "open62541.h"
#include "connection_manager.h"
#include "model.h"
#include "browse.h"        /* resolveChildByName */
#include "types_uafx_data_generated.h"
#include "types_uafx_ac_generated.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Transport profile URI ───────────────────────────────────── */
#define TRANSPORT_URI \
    "http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp"

/* ── Generatore di ID univoci ────────────────────────────────── */
/* Contatore statico — in produzione si userebbe
 * ReserveCommunicationIds sul server               */
static UA_UInt16 s_idCounter = 1000;

static UA_UInt16 nextId(void) {
    return s_idCounter++;
}

/* ── Costruzione della commandmask ────────────────── */
static UA_UInt32
buildCommandMask(void) {

    return
        (1u << 2) |  /* CreateConnectionEndpointCmd */
        (1u << 7) |  /* SetCommunicationConfigurationCmd */
        (1u << 8);   /* EnableCommunicationCmd */
}


/* ── Scelta dell'indirizzo multicast ─────────────────────────── */
/* Ogni connessione usa un indirizzo multicast diverso
 * nel range 224.0.5.x per evitare interferenze         */
static UA_UInt16 s_multicastCounter = 1;

static void buildMulticastUrl(char *buf, size_t bufLen) {
    snprintf(buf, bufLen, "opc.udp://224.0.5.%u:4840/",
             s_multicastCounter++);
}



static UA_ConnectionEndpointConfigurationDataType
buildEndpointConfiguration(
    const char *endpointName,
    const char *variableName,
    bool isPublisher
) {
    UA_ConnectionEndpointConfigurationDataType cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* connectionEndpoint è una union discriminata:
     * switchField=PARAMETER usa fields.parameter (ExtensionObject),
     * switchField=NODE      usa fields.node (NodeId).
     * Per ora lasciamo NONE — il server userà il functionalEntityNode. */
    cfg.connectionEndpoint.switchField =
        UA_CONNECTIONENDPOINTDEFINITIONDATATYPESWITCH_NONE;

    /* communicationLinks è un UA_ExtensionObject che wrappa
     * UA_PubSubCommunicationLinkConfigurationDataType */
    UA_PubSubCommunicationLinkConfigurationDataType link;
    memset(&link, 0, sizeof(link));
    /* link.dataSetWriterRef / dataSetReaderRef rimangono vuoti per ora */

    UA_ExtensionObject_setValueCopy(
        &cfg.communicationLinks,
        &link,
        &UA_TYPES_UAFX_DATA[
            UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONLINKCONFIGURATIONDATATYPE
        ]
    );

    return cfg;
}

/* ════════════════════════════════════════════════════════════
 * buildPublisherConfig
 *
 * Costruisce la UA_PubSubConfiguration2DataType per il lato
 * Publisher. Contiene:
 *   - PubSubConnection con indirizzo multicast e publisherId
 *   - WriterGroup con writerGroupId e publishingInterval
 *   - DataSetWriter collegato al PDS
 *   - PublishedDataSet con il campo da pubblicare
 *
 * Il NodeId della variabile da pubblicare viene passato
 * come ExtensionObject nel DataSetWriter — il server lo
 * risolve internamente nella sua callback.
 *
 * NOTA: il chiamante deve liberare la struttura con
 *       UA_PubSubConfiguration2DataType_clear(&cfg)
 * ════════════════════════════════════════════════════════════ */
static UA_PubSubConfiguration2DataType
buildPublisherConfig(UA_UInt16 publisherId,
                     UA_UInt16 writerGroupId,
                     UA_UInt16 dataSetWriterId,
                     const char *multicastUrl,
                     const char *networkInterface,
                     const char *variableName,
                     double publishingInterval) {

    UA_PubSubConfiguration2DataType cfg;
    UA_PubSubConfiguration2DataType_init(&cfg);

    /* ── 1. PubSubConnection ───────────────────────────────── */
    cfg.connectionsSize = 1;
    cfg.connections = UA_calloc(1, sizeof(UA_PubSubConnectionDataType));
    UA_PubSubConnectionDataType *conn = &cfg.connections[0];
    UA_PubSubConnectionDataType_init(conn);

    conn->name    = UA_STRING_ALLOC("CM_Publisher_Connection");
    conn->enabled = true;
    conn->transportProfileUri = UA_STRING_ALLOC(TRANSPORT_URI);

    /* publisherId come UInt16 in Variant */
    UA_UInt16 pid = publisherId;
    UA_Variant_setScalarCopy(&conn->publisherId, &pid,
                             &UA_TYPES[UA_TYPES_UINT16]);

    /* address come ExtensionObject contenente
     * UA_NetworkAddressUrlDataType               */
    UA_NetworkAddressUrlDataType addr;
    addr.networkInterface = UA_STRING((char*)networkInterface);
    addr.url              = UA_STRING((char*)multicastUrl);
    UA_ExtensionObject_setValue(&conn->address, &addr,
        &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

    /* ── 2. WriterGroup ────────────────────────────────────── */
    conn->writerGroupsSize = 1;
    conn->writerGroups = UA_calloc(1, sizeof(UA_WriterGroupDataType));
    UA_WriterGroupDataType *wg = &conn->writerGroups[0];
    UA_WriterGroupDataType_init(wg);

    wg->name               = UA_STRING_ALLOC("CM_WriterGroup");
    wg->enabled            = true;
    wg->writerGroupId      = writerGroupId;
    wg->publishingInterval = publishingInterval;

    /* NetworkMessageContentMask — include tutti gli header
     * necessari per il filtro lato Subscriber              */
    UA_UadpWriterGroupMessageDataType wgMsg;
    UA_UadpWriterGroupMessageDataType_init(&wgMsg);
    wgMsg.networkMessageContentMask =
        (UA_UadpNetworkMessageContentMask)
        (UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID   |
         UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER   |
         UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
         UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER);
    UA_ExtensionObject_setValue(&wg->messageSettings, &wgMsg,
        &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE]);

    /* ── 3. DataSetWriter ──────────────────────────────────── */
    wg->dataSetWritersSize = 1;
    wg->dataSetWriters = UA_calloc(1, sizeof(UA_DataSetWriterDataType));
    UA_DataSetWriterDataType *dsw = &wg->dataSetWriters[0];
    UA_DataSetWriterDataType_init(dsw);

    dsw->name            = UA_STRING_ALLOC("CM_DataSetWriter");
    dsw->enabled         = true;
    dsw->dataSetWriterId = dataSetWriterId;
    dsw->dataSetName     = UA_STRING_ALLOC(variableName); /* nome del PDS */
    dsw->keyFrameCount   = 10;

    /* ── 4. PublishedDataSet ───────────────────────────────── */
    cfg.publishedDataSetsSize = 1;
    cfg.publishedDataSets =
        UA_calloc(1, sizeof(UA_PublishedDataSetDataType));
    UA_PublishedDataSetDataType *pds = &cfg.publishedDataSets[0];
    UA_PublishedDataSetDataType_init(pds);

    pds->name = UA_STRING_ALLOC(variableName);

    /* MetaData — descrive il campo al Subscriber */
    pds->dataSetMetaData.fieldsSize = 1;
    pds->dataSetMetaData.fields =
        UA_calloc(1, sizeof(UA_FieldMetaData));
    UA_FieldMetaData *field = &pds->dataSetMetaData.fields[0];
    UA_FieldMetaData_init(field);
    field->name        = UA_STRING_ALLOC(variableName);
    field->builtInType = UA_NS0ID_FLOAT;
    field->dataType    = UA_TYPES[UA_TYPES_FLOAT].typeId;
    field->valueRank   = -1; /* scalare */

    /* DataSetSource: PublishedItems con il nome della variabile.
     * Il server risolve il NodeId reale nella sua callback
     * usando questo nome per cercare nell'OutputData della FE. */
    UA_PublishedDataItemsDataType items;
    UA_PublishedDataItemsDataType_init(&items);
    items.publishedDataSize = 1;
    items.publishedData = UA_calloc(1, sizeof(UA_PublishedVariableDataType));
    UA_PublishedVariableDataType_init(&items.publishedData[0]);
    /* NodeId lasciato a null — il server lo risolve per nome */
    items.publishedData[0].attributeId = UA_ATTRIBUTEID_VALUE;

    UA_ExtensionObject_setValue(&pds->dataSetSource, &items,
        &UA_TYPES[UA_TYPES_PUBLISHEDDATAITEMSDATATYPE]);

    return cfg;
}

/* ════════════════════════════════════════════════════════════
 * buildSubscriberConfig
 *
 * Costruisce la UA_PubSubConfiguration2DataType per il lato
 * Subscriber. Contiene:
 *   - PubSubConnection con stesso indirizzo multicast
 *   - ReaderGroup
 *   - DataSetReader con i tre ID di filtro
 *   - MetaData compatibile con il Publisher
 *
 * Il NodeId della variabile target (InputData) viene passato
 * nel nome — il server lo risolve nella sua callback.
 * ════════════════════════════════════════════════════════════ */
static UA_PubSubConfiguration2DataType
buildSubscriberConfig(UA_UInt16 publisherId,
                      UA_UInt16 writerGroupId,
                      UA_UInt16 dataSetWriterId,
                      const char *multicastUrl,
                      const char *networkInterface,
                      const char *sourceVariableName,
                      const char *targetVariableName) {

    UA_PubSubConfiguration2DataType cfg;
    UA_PubSubConfiguration2DataType_init(&cfg);

    /* ── 1. PubSubConnection ───────────────────────────────── */
    cfg.connectionsSize = 1;
    cfg.connections = UA_calloc(1, sizeof(UA_PubSubConnectionDataType));
    UA_PubSubConnectionDataType *conn = &cfg.connections[0];
    UA_PubSubConnectionDataType_init(conn);

    conn->name    = UA_STRING_ALLOC("CM_Subscriber_Connection");
    conn->enabled = true;
    conn->transportProfileUri = UA_STRING_ALLOC(TRANSPORT_URI);

    UA_NetworkAddressUrlDataType addr;
    addr.networkInterface = UA_STRING((char*)networkInterface);
    addr.url              = UA_STRING((char*)multicastUrl);
    UA_ExtensionObject_setValue(&conn->address, &addr,
        &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

    /* ── 2. ReaderGroup ────────────────────────────────────── */
    conn->readerGroupsSize = 1;
    conn->readerGroups = UA_calloc(1, sizeof(UA_ReaderGroupDataType));
    UA_ReaderGroupDataType *rg = &conn->readerGroups[0];
    UA_ReaderGroupDataType_init(rg);

    rg->name    = UA_STRING_ALLOC("CM_ReaderGroup");
    rg->enabled = true;

    /* ── 3. DataSetReader ──────────────────────────────────── */
    rg->dataSetReadersSize = 1;
    rg->dataSetReaders = UA_calloc(1, sizeof(UA_DataSetReaderDataType));
    UA_DataSetReaderDataType *dsr = &rg->dataSetReaders[0];
    UA_DataSetReaderDataType_init(dsr);

    dsr->name = UA_STRING_ALLOC("CM_DataSetReader");

    /* I tre filtri — devono coincidere esattamente con il Publisher */
    UA_UInt16 pid = publisherId;
    UA_Variant_setScalarCopy(&dsr->publisherId, &pid,
                             &UA_TYPES[UA_TYPES_UINT16]);
    dsr->writerGroupId   = writerGroupId;
    dsr->dataSetWriterId = dataSetWriterId;

    /* MessageSettings — deve matchare il NetworkMessageContentMask
     * del WriterGroup Publisher                                    */
    UA_UadpDataSetReaderMessageDataType dsrMsg;
    memset(&dsrMsg, 0, sizeof(dsrMsg));
    dsrMsg.networkMessageContentMask =
        (UA_UadpNetworkMessageContentMask)
        (UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID   |
         UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER   |
         UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
         UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER);
    dsr->messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    dsr->messageSettings.content.decoded.type =
        &UA_TYPES[UA_TYPES_UADPDATASETREADERMESSAGEDATATYPE];
    dsr->messageSettings.content.decoded.data = &dsrMsg;

    /* MetaData — deve corrispondere al PDS del Publisher */
    dsr->dataSetMetaData.name = UA_STRING_ALLOC(sourceVariableName);
    dsr->dataSetMetaData.fieldsSize = 1;
    dsr->dataSetMetaData.fields =
        UA_calloc(1, sizeof(UA_FieldMetaData));
    UA_FieldMetaData *field = &dsr->dataSetMetaData.fields[0];
    UA_FieldMetaData_init(field);
    field->name        = UA_STRING_ALLOC(sourceVariableName);
    field->builtInType = UA_NS0ID_FLOAT;
    field->dataType    = UA_TYPES[UA_TYPES_FLOAT].typeId;
    field->valueRank   = -1;

    /* SubscribedDataSet: TargetVariables.
     * Il server risolve il NodeId della variabile target
     * per nome (targetVariableName) nella sua callback.
     * Passiamo il nome come proprietà aggiuntiva.        */
    UA_TargetVariablesDataType tvds;
    UA_TargetVariablesDataType_init(&tvds);
    tvds.targetVariablesSize = 1;
    tvds.targetVariables = UA_calloc(1, sizeof(UA_FieldTargetDataType));
    UA_FieldTargetDataType_init(&tvds.targetVariables[0]);
    tvds.targetVariables[0].attributeId = UA_ATTRIBUTEID_VALUE;
    /* NodeId target: lasciato null, il server lo risolve
     * cercando targetVariableName nell'InputData della FE */

    UA_ExtensionObject_setValue(&dsr->subscribedDataSet, &tvds,
        &UA_TYPES[UA_TYPES_TARGETVARIABLESDATATYPE]);

    return cfg;
}

/* ════════════════════════════════════════════════════════════
 * callEstablishConnections
 *
 * Si connette al server, naviga fino al metodo
 * EstablishConnections sull'AutomationComponent, e lo chiama
 * con la configurazione PubSub passata come input argument.
 * ════════════════════════════════════════════════════════════ */
static EstablishResult
callEstablishConnections(
    const char *endpointUrl,
    const char *acName,

    UA_ConnectionEndpointConfigurationDataType *epCfg,
    UA_PubSubCommunicationConfigurationDataType *commCfg
){

    EstablishResult result = { false, "" };

    UA_Client *client = UA_Client_new();
    UA_ClientConfig *config = UA_Client_getConfig(client);

    static UA_DataTypeArray customDataTypesAC = {
        .next = NULL,
        .typesSize = UA_TYPES_UAFX_AC_COUNT,
        .types = UA_TYPES_UAFX_AC
    };

    static UA_DataTypeArray customDataTypesData = {
        .next = &customDataTypesAC,
        .typesSize = UA_TYPES_UAFX_DATA_COUNT,
        .types = UA_TYPES_UAFX_DATA
    };

    static UA_DataTypeArray customDataTypesDI = {
        .next = &customDataTypesData,
        .typesSize = UA_TYPES_DI_COUNT,
        .types = UA_TYPES_DI
    };

    config->customDataTypes = &customDataTypesDI;


    UA_StatusCode rc = UA_Client_connect(client, endpointUrl);
    if(rc != UA_STATUSCODE_GOOD) {
        snprintf(result.errorMessage, sizeof(result.errorMessage),
                 "Cannot connect to %s: %s",
                 endpointUrl, UA_StatusCode_name(rc));
        UA_Client_delete(client);
        return result;
    }

     /* Naviga: Objects → FxRoot → <acName> → ConnectionManager → EstablishConnections */
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId fxRoot  = resolveChildByName(client, objectsFolder, "FxRoot");
    UA_NodeId acNode  = resolveChildByName(client, fxRoot, acName);
    UA_NodeId cm      = resolveChildByName(client, acNode, "ConnectionManager");
    UA_NodeId method  = resolveChildByName(client, cm, "EstablishConnections");

    if(UA_NodeId_isNull(&method)) {
        snprintf(result.errorMessage, sizeof(result.errorMessage),
                 "EstablishConnections not found under ConnectionManager on AC '%s'",
                 acName);
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        return result;
    }
    UA_Variant inputArgs[5];

    for(size_t i = 0; i < 5; i++)
        UA_Variant_init(&inputArgs[i]);

    /* ---------------------------------------------------------
    * [0] CommandMask
    * --------------------------------------------------------- */

    UA_UInt32 cmdMask = buildCommandMask();

    UA_Variant_setScalarCopy(
        &inputArgs[0],
        &cmdMask,
        &UA_TYPES[UA_TYPES_UINT32]
    );

    /* ---------------------------------------------------------
    * [1] AssetVerifications
    * Empty
    * --------------------------------------------------------- */

    UA_Variant_init(&inputArgs[1]);

    /* ---------------------------------------------------------
    * [2] ConnectionEndpointConfigurations
    * --------------------------------------------------------- */

    UA_Variant_setArrayCopy(
        &inputArgs[2],
        epCfg,
        1,
        &UA_TYPES_UAFX_DATA[
            UA_TYPES_UAFX_DATA_CONNECTIONENDPOINTCONFIGURATIONDATATYPE
        ]
    );

    /* ---------------------------------------------------------
    * [3] ReserveCommunicationIds
    * Empty
    * --------------------------------------------------------- */

    UA_Variant_init(&inputArgs[3]);

    /* ---------------------------------------------------------
    * [4] CommunicationConfigurations
    * --------------------------------------------------------- */

    UA_Variant_setArrayCopy(
        &inputArgs[4],
        commCfg,
        1,

        &UA_TYPES_UAFX_DATA[
            UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONCONFIGURATIONDATATYPE
        ]
    );

    size_t outputSize = 0;
    UA_Variant *output = NULL;

    rc = UA_Client_call(
        client,
        cm,
        method,

        5,
        inputArgs,

        &outputSize,
        &output
    );

    if(rc != UA_STATUSCODE_GOOD) {
        snprintf(result.errorMessage, sizeof(result.errorMessage),
                 "EstablishConnections failed on %s: %s",
                 endpointUrl, UA_StatusCode_name(rc));
    } else {
        result.ok = true;

        if(outputSize >= 3 && output) {

            /* ── output[2]: ConnectionEndpointResults ── */
            UA_ConnectionEndpointConfigurationResultDataType *epRes =
                (UA_ConnectionEndpointConfigurationResultDataType *)
                output[2].data;
            if(epRes) {
                UA_NodeId_copy(&epRes->connectionEndpointId,
                               &result.connectionEndpointId);
            }

            /* ── output[3]: CommunicationConfigResults ── */
            if(outputSize >= 4) {
                UA_PubSubCommunicationConfigurationResultDataType *commRes =
                    (UA_PubSubCommunicationConfigurationResultDataType *)
                    output[3].data;
                if(commRes) {
                    if(commRes->result != UA_STATUSCODE_GOOD) {
                        result.ok = false;
                        snprintf(result.errorMessage,
                                 sizeof(result.errorMessage),
                                 "Server returned error: %s",
                                 UA_StatusCode_name(commRes->result));
                    } else if(commRes->configurationObjectsSize > 0) {
                        /* configurationObjects[0] = NodeId del DSW/DSR creato */
                        UA_NodeId_copy(&commRes->configurationObjects[0],
                                       &result.dataSetWriterNodeId);
                    }
                }
            }
        }

        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }

    UA_Client_disconnect(client);
    UA_Client_delete(client);
    return result;
}

/* ════════════════════════════════════════════════════════════
 * establishConnection — funzione pubblica
 * ════════════════════════════════════════════════════════════ */
bool establishConnection(TopologyGraph *graph,
                         const ConnectionRequest *req, PubSubConnection *connOut) {

    /* ── 1. Risolvi i nodi nel TopologyGraph ─────────────── */
    int pubIdx = topologyFindNodeByChassisId(graph, req->publisherChassisId);
    int subIdx = topologyFindNodeByChassisId(graph, req->subscriberChassisId);

    if(pubIdx < 0 || subIdx < 0) {
        printf("[CM] ERROR: chassis ID not found in topology graph\n");
        return false;
    }

    TopologyNode *pubNode = &graph->nodes[pubIdx];  
    TopologyNode *subNode = &graph->nodes[subIdx];

    if(!pubNode->reachable || !subNode->reachable) {
        printf("[CM] ERROR: one or both nodes are not reachable\n");
        return false;
    }

    /* ── 2. Genera gli ID PubSub ─────────────────────────── */
    UA_UInt16 publisherId     = nextId();
    UA_UInt16 writerGroupId   = nextId();
    UA_UInt16 dataSetWriterId = nextId();

    char multicastUrl[64];
    buildMulticastUrl(multicastUrl, sizeof(multicastUrl));

    printf("[CM] New connection:\n");
    printf("[CM]   PublisherId=%u WriterGroupId=%u DataSetWriterId=%u\n",
           publisherId, writerGroupId, dataSetWriterId);
    printf("[CM]   Multicast: %s\n", multicastUrl);
    printf("[CM]   Interval:  %.0f ms\n", req->publishingInterval);

    /* ── 3. Costruisci configurazione Publisher ───────────── */
    UA_PubSubConfiguration2DataType pubCfg =
        buildPublisherConfig(publisherId, writerGroupId, dataSetWriterId,
                             multicastUrl, "enp0s31f6",
                             req->publisherVariable,
                             req->publishingInterval);

    UA_PubSubCommunicationConfigurationDataType pubCommCfg;
    memset(&pubCommCfg, 0, sizeof(pubCommCfg));
    pubCommCfg.pubSubConfiguration     = pubCfg;
    pubCommCfg.requireCompleteUpdate   = false;

    /* ── 4. Chiama EstablishConnections sul Publisher ─────── */
    printf("[CM] Calling EstablishConnections on Publisher (%s)...\n",
           pubNode->endpointUrl);

    UA_ConnectionEndpointConfigurationDataType pubEpCfg =
    buildEndpointConfiguration(
        "PublisherEndpoint",
        req->publisherVariable,
        true
    );

    EstablishResult pubResult =
    callEstablishConnections(
        pubNode->endpointUrl,
        req->publisherAcName,

        &pubEpCfg,
        &pubCommCfg
    );

    UA_PubSubConfiguration2DataType_clear(&pubCfg);

    if(!pubResult.ok) {
        printf("[CM] Publisher failed: %s\n", pubResult.errorMessage);
        return false;
    }
    printf("[CM] Publisher: OK\n");

    /* ── 5. Costruisci configurazione Subscriber ──────────── */
    UA_PubSubConfiguration2DataType subCfg =
        buildSubscriberConfig(publisherId, writerGroupId, dataSetWriterId,
                              multicastUrl, "enp0s31f6",
                              req->publisherVariable,
                              req->subscriberVariable);

    UA_PubSubCommunicationConfigurationDataType subCommCfg;
    memset(&subCommCfg, 0, sizeof(subCommCfg));
    subCommCfg.pubSubConfiguration   = subCfg;
    subCommCfg.requireCompleteUpdate = false;

    /* ── 6. Chiama EstablishConnections sul Subscriber ────── */
    printf("[CM] Calling EstablishConnections on Subscriber (%s)...\n",
           subNode->endpointUrl);
    
    UA_ConnectionEndpointConfigurationDataType subEpCfg =
    buildEndpointConfiguration(
        "SubscriberEndpoint",
        req->subscriberVariable,
        false
    );

    EstablishResult subResult =
    callEstablishConnections(
        subNode->endpointUrl,
        req->subscriberAcName,

        &subEpCfg,
        &subCommCfg
    );

    UA_PubSubConfiguration2DataType_clear(&subCfg);

    if(!subResult.ok) {
        printf("[CM] Subscriber failed: %s\n", subResult.errorMessage);
        return false;
    }
    printf("[CM] Subscriber: OK\n");

    /* ── 7. Aggiorna il TopologyGraph ─────────────────────── */
   /* ── 7. Costruisci ConnectionEndpointInfo e PubSubConnection ── */
    ConnectionEndpoint pubCeInfo;
    memset(&pubCeInfo, 0, sizeof(pubCeInfo));
    strncpy(pubCeInfo.name, "PublisherEndpoint", MAX_STR_LEN - 1);
    pubCeInfo.mode   = 0;  /* Publisher */
    UA_NodeId_copy(&pubResult.connectionEndpointId, &pubCeInfo.nodeId);
    UA_NodeId_copy(&pubResult.dataSetWriterNodeId,  &pubCeInfo.dataSetWriterRef);
    strncpy(pubCeInfo.status, "Operational", MAX_STR_LEN - 1);
    strncpy(pubCeInfo.linkedVariable, req->publisherVariable, MAX_STR_LEN - 1);

    ConnectionEndpoint subCeInfo;
    memset(&subCeInfo, 0, sizeof(subCeInfo));
    strncpy(subCeInfo.name, "SubscriberEndpoint", MAX_STR_LEN - 1);
    subCeInfo.mode   = 1;  /* Subscriber */
    UA_NodeId_copy(&subResult.connectionEndpointId, &subCeInfo.nodeId);
    UA_NodeId_copy(&subResult.dataSetWriterNodeId,  &subCeInfo.dataSetReaderRef);
    /* dataSetWriterRef del subscriber punta al DSW del publisher */
    UA_NodeId_copy(&pubResult.dataSetWriterNodeId,  &subCeInfo.dataSetWriterRef);
    strncpy(subCeInfo.status, "Operational", MAX_STR_LEN - 1);
    strncpy(subCeInfo.linkedVariable, req->subscriberVariable, MAX_STR_LEN - 1);

    PubSubConnection conn;
    memset(&conn, 0, sizeof(conn));
    conn.pub              = pubCeInfo;
    conn.sub              = subCeInfo;
    conn.publisherId      = publisherId;
    conn.writerGroupId    = writerGroupId;
    conn.dataSetWriterId  = dataSetWriterId;
    conn.publishingInterval = req->publishingInterval;
    strncpy(conn.multicastUrl, multicastUrl, MAX_STR_LEN - 1);

   int connIdx = topologyAddLogicalConnection(graph, &conn);
    if(connIdx < 0) {
        printf("[CM] WARNING: logical connections array full\n");
    } else {
        printf("[CM] Logical connection added at index %d\n", connIdx);
    }

    /* Restituisci la connessione creata al chiamante */
    if(connOut)
        *connOut = conn;

    printf("[CM] Connection established successfully\n");
    return true;
}