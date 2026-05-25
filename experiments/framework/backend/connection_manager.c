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


/* NodeId del PubSubConnectionEndpointType nel nodeset FX/AC.
 * Lo standard fissa questo valore: ns=FX/AC; i=1005. */
#define FXAC_PUBSUBCONNECTIONENDPOINTTYPE_ID  1005
 
/* Forward: il chiamante deve risolvere a runtime il namespace index
 * di FX/AC per costruire il NodeId corretto. Per semplicità qui
 * usiamo ns=4 come default visto nel server pub_test (FX/AC=4).
 * In una versione robusta andrebbe risolto via NamespaceArray. */
#define FXAC_NAMESPACE_INDEX  4

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
    snprintf(buf, bufLen, "opc.udp://224.0.23.%u:4840/",
             s_multicastCounter++);
}



 
/* ============================================================
 * buildEndpointConfiguration
 *
 * Costruisce una UA_ConnectionEndpointConfigurationDataType
 * completa, pronta per essere serializzata nel param 2 del
 * metodo EstablishConnections.
 *
 * @param ceName        Nome univoco del CE da creare (es. "CE_Pub_001")
 * @param variableId    NodeId della variabile da legare al CE
 *                      (OutputData se Publisher, InputData se Subscriber)
 * @param isPublisher   true → Mode=Publisher(2), variableId in OutputVariableIds
 *                      false → Mode=Subscriber(3), variableId in InputVariableIds
 *
 * Il chiamante deve liberare la struct con
 * UA_ConnectionEndpointConfigurationDataType_clear(&cfg).
 * ============================================================ */
static UA_ConnectionEndpointConfigurationDataType
buildEndpointConfiguration(const char *ceName,
                           UA_NodeId variableId,
                           bool isPublisher) {
 
    UA_ConnectionEndpointConfigurationDataType cfg;
    memset(&cfg, 0, sizeof(cfg));
 
    /* ═══════════════════════════════════════════════════════════
     * 1. connectionEndpoint — switchField = PARAMETER
     *
     * La union ConnectionEndpointDefinitionDataType ha un solo
     * Field nel nodeset (Parameter di tipo
     * ConnectionEndpointParameterDataType, AllowSubTypes=true).
     * Per "creare un nuovo CE" si setta switchField a PARAMETER
     * e si popola fields.parameter con un ExtensionObject che
     * wrappa il sottotipo concreto PubSubConnectionEndpointParameterDataType.
     * ═══════════════════════════════════════════════════════════ */
 
    cfg.connectionEndpoint.switchField =
        UA_CONNECTIONENDPOINTDEFINITIONDATATYPESWITCH_PARAMETER;
 
    /* Costruisci il PubSubConnectionEndpointParameterDataType.
     * Eredita 8 campi da ConnectionEndpointParameterDataType:
     *   Name, ConnectionEndpointTypeId, InputVariableIds,
     *   OutputVariableIds, IsPersistent, CleanupTimeout,
     *   RelatedEndpoint, IsPreconfigured.
     * Aggiunge 1 campo proprio:
     *   Mode (PubSubConnectionEndpointModeEnum) */
    UA_PubSubConnectionEndpointParameterDataType *param =
    UA_calloc(1, sizeof(UA_PubSubConnectionEndpointParameterDataType));
    /* ── Campi ereditati da ConnectionEndpointParameterDataType ── */
 
    /* Name: nome del CE da creare */
    param->name = UA_STRING_ALLOC((char *)ceName);
 
    /* ConnectionEndpointTypeId: ns=FX/AC;i=1005 (PubSubConnectionEndpointType) */
    param->connectionEndpointTypeId =
        UA_NODEID_NUMERIC(FXAC_NAMESPACE_INDEX,
                          FXAC_PUBSUBCONNECTIONENDPOINTTYPE_ID);
 
    /* InputVariableIds / OutputVariableIds:
     * il Publisher pubblica una OutputVariable, il Subscriber
     * scrive su una InputVariable. Esattamente una delle due lista
     * ha un elemento, l'altra è vuota. */
    if(isPublisher) {
        param->outputVariableIdsSize = 1;
        param->outputVariableIds = (UA_NodeId *)UA_calloc(1, sizeof(UA_NodeId));
        UA_NodeId_copy(&variableId, &param->outputVariableIds[0]);
        param->inputVariableIdsSize = 0;
        param->inputVariableIds = NULL;
    } else {
        param->inputVariableIdsSize = 1;
        param->inputVariableIds = (UA_NodeId *)UA_calloc(1, sizeof(UA_NodeId));
        UA_NodeId_copy(&variableId, &param->inputVariableIds[0]);
        param->outputVariableIdsSize = 0;
        param->outputVariableIds = NULL;
    }
 
    /* IsPersistent: connessione dinamica, non sopravvive al riavvio */
    param->isPersistent = false;
 
    /* CleanupTimeout: 0 = nessun cleanup automatico */
    param->cleanupTimeout = 0.0;
 
    /* IsPreconfigured: false — il CE è creato dinamicamente,
     * non era preconfigurato nel modello del dispositivo */
    param->isPreconfigured = false;
 
    /* ── Campo proprio di PubSubConnectionEndpointParameterDataType ── */
 
    /* Mode: 2=Publisher, 3=Subscriber (dall'enum PubSubConnectionEndpointModeEnum) */
    param->mode = isPublisher
                 ? UA_PUBSUBCONNECTIONENDPOINTMODEENUM_PUBLISHER
                 : UA_PUBSUBCONNECTIONENDPOINTMODEENUM_SUBSCRIBER;
 
    /* Wrappa il PubSubConnectionEndpointParameterDataType in
     * un ExtensionObject e assegnalo a connectionEndpoint.fields.parameter */
    cfg.connectionEndpoint.fields.parameter.encoding =
    UA_EXTENSIONOBJECT_DECODED;
    cfg.connectionEndpoint.fields.parameter.content.decoded.type =
    &UA_TYPES_UAFX_DATA[
        UA_TYPES_UAFX_DATA_PUBSUBCONNECTIONENDPOINTPARAMETERDATATYPE
    ];
cfg.connectionEndpoint.fields.parameter.content.decoded.data = param;
 
 
    /* ═══════════════════════════════════════════════════════════
     * 2. controlGroups — vuoto (no multi-master in questa versione)
     * ═══════════════════════════════════════════════════════════ */
    cfg.controlGroupsSize = 0;
    cfg.controlGroups = NULL;
 
    /* ═══════════════════════════════════════════════════════════
     * 3. communicationLinks — array di 1 link
     *
     * PubSubCommunicationLinkConfigurationDataType ha 4 campi:
     *   DataSetReaderRef                  (PubSubConfigurationRefDataType)
     *   ExpectedSubscribedDataSetVersion  (ConfigurationVersionDataType)
     *   DataSetWriterRef                  (PubSubConfigurationRefDataType)
     *   ExpectedPublishedDataSetVersion   (ConfigurationVersionDataType)
     *
     * Per il Publisher popoliamo solo DataSetWriterRef
     * (e ExpectedPublishedDataSetVersion).
     * Per il Subscriber popoliamo solo DataSetReaderRef
     * (e ExpectedSubscribedDataSetVersion).
     *
     * Gli indici dentro PubSubConfigurationRefDataType si
     * riferiscono alla PubSubConfiguration2DataType che viene
     * mandata nel param 4 della chiamata EstablishConnections.
     * Visto che ne abbiamo sempre una sola per chiamata, e
     * dentro c'è sempre una sola connection con un solo
     * group con un solo writer/reader, gli indici sono tutti 0.
     * ═══════════════════════════════════════════════════════════ */
 
    UA_PubSubCommunicationLinkConfigurationDataType link;
     memset(&link, 0, sizeof(link));

    /* Popola la ref appropriata in base al ruolo */
    if(isPublisher) {
        /* DataSetWriterRef → primo DSW della prima connection */
        link.dataSetWriterRef.configurationMask =
            UA_PUBSUBCONFIGURATIONREFMASK_REFERENCEWRITER;
        link.dataSetWriterRef.connectionIndex = 0;
        link.dataSetWriterRef.groupIndex      = 0;
        link.dataSetWriterRef.elementIndex    = 0;
 
        /* ExpectedPublishedDataSetVersion: 0,0 = qualsiasi versione */
        link.expectedPublishedDataSetVersion.majorVersion = 0;
        link.expectedPublishedDataSetVersion.minorVersion = 0;
 
        /* DataSetReaderRef rimane vuoto (configurationMask=0) */
    } else {
        /* DataSetReaderRef → primo DSR della prima connection */
        link.dataSetReaderRef.configurationMask =
            UA_PUBSUBCONFIGURATIONREFMASK_REFERENCEREADER;
        link.dataSetReaderRef.connectionIndex = 0;
        link.dataSetReaderRef.groupIndex      = 0;
        link.dataSetReaderRef.elementIndex    = 0;
 
        link.expectedSubscribedDataSetVersion.majorVersion = 0;
        link.expectedSubscribedDataSetVersion.minorVersion = 0;
    }
 
    /* Costruisci l'array di ExtensionObject (1 elemento) */
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
                     double publishingInterval,UA_NodeId pubVarNodeId) {

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
    addr.networkInterface = UA_STRING_ALLOC((char*)networkInterface);
    addr.url              = UA_STRING_ALLOC((char*)multicastUrl);
    UA_ExtensionObject_setValueCopy(&conn->address, &addr,
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
    items.publishedData[0].publishedVariable = pubVarNodeId; 
    items.publishedData[0].attributeId = UA_ATTRIBUTEID_VALUE;

    UA_ExtensionObject_setValueCopy(&pds->dataSetSource, &items,
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
                      UA_NodeId subVarNodeId,
                      const char *targetVariableName
                      ) {

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
    addr.networkInterface = UA_STRING_ALLOC((char*)networkInterface);
    addr.url              = UA_STRING_ALLOC((char*)multicastUrl);
    UA_ExtensionObject_setValueCopy(&conn->address, &addr,
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
    UA_ExtensionObject_setValueCopy(&dsr->messageSettings, &dsrMsg,
        &UA_TYPES[UA_TYPES_UADPDATASETREADERMESSAGEDATATYPE]);

    // UA_ExtensionObject_setValue(&wg->messageSettings, &wgMsg,
        //&UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE]);

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
    tvds.targetVariables[0].targetNodeId = subVarNodeId;
    tvds.targetVariables[0].attributeId = UA_ATTRIBUTEID_VALUE;
    /* NodeId target: lasciato null, il server lo risolve
     * cercando targetVariableName nell'InputData della FE */

    UA_ExtensionObject_setValueCopy(&dsr->subscribedDataSet, &tvds,
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
    const UA_NodeId acNodeId,

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

     /* Naviga: Objects → FxRoot → <acName> → EstablishConnections */
    
    UA_NodeId method  = resolveChildByName(client, acNodeId, "EstablishConnections");

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

    /* [1] AssetVerifications — array vuoto del tipo corretto */
UA_Variant_setArray(&inputArgs[1], NULL, 0,
    &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_ASSETVERIFICATIONDATATYPE]);

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

    /* [3] ReserveCommunicationIds — array vuoto */
UA_Variant_setArray(&inputArgs[3], NULL, 0,
    &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBRESERVECOMMUNICATIONIDSDATATYPE]);

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
        acNodeId,
        method,

        5,
        inputArgs,

        &outputSize,
        &output
    );
    printf("[CM] UA_Client_call returned rc=0x%08X (%s), outputSize=%zu, output=%p\n",
       rc, UA_StatusCode_name(rc), outputSize, (void*)output);
       
    if(rc != UA_STATUSCODE_GOOD || !output || outputSize < 4) {
    snprintf(result.errorMessage, sizeof(result.errorMessage),
             "Bad response: rc=%s outputSize=%zu",
             UA_StatusCode_name(rc), outputSize);
    if(output) UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    return result;
} else {
    result.ok = true;

    /* ── output[1]: ConnectionEndpointConfigurationResults ── */
    if(output[1].arrayLength >= 1 && output[1].data &&
       output[1].type == &UA_TYPES_UAFX_DATA[
           UA_TYPES_UAFX_DATA_CONNECTIONENDPOINTCONFIGURATIONRESULTDATATYPE]) {
        UA_ConnectionEndpointConfigurationResultDataType *epRes =
            (UA_ConnectionEndpointConfigurationResultDataType *)output[1].data;
        UA_NodeId_copy(&epRes[0].connectionEndpointId,
                       &result.connectionEndpointId);
        printf("[CM] CE result statusCode=0x%08X\n",
               epRes[0].connectionEndpointResult);
        if(epRes[0].connectionEndpointResult != UA_STATUSCODE_GOOD) {
            result.ok = false;
            snprintf(result.errorMessage, sizeof(result.errorMessage),
                     "CE result error: %s",
                     UA_StatusCode_name(epRes[0].connectionEndpointResult));
        }
    } else {
        printf("[CM] WARNING: output[1] empty or wrong type\n");
    }

    /* ── output[3]: CommunicationConfigurationResults ── */
    if(output[3].arrayLength >= 1 && output[3].data &&
       output[3].type == &UA_TYPES_UAFX_DATA[
           UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONCONFIGURATIONRESULTDATATYPE]) {
        UA_PubSubCommunicationConfigurationResultDataType *commRes =
            (UA_PubSubCommunicationConfigurationResultDataType *)output[3].data;
        printf("[CM] Comm result statusCode=0x%08X configObjsSize=%zu\n",
               commRes[0].result, commRes[0].configurationObjectsSize);
        if(commRes[0].result != UA_STATUSCODE_GOOD) {
            result.ok = false;
            snprintf(result.errorMessage, sizeof(result.errorMessage),
                     "Comm result error: %s",
                     UA_StatusCode_name(commRes[0].result));
        } else if(commRes[0].configurationObjectsSize > 0) {
            UA_NodeId_copy(&commRes[0].configurationObjects[0],
                           &result.dataSetWriterNodeId);
        }
    } else {
        printf("[CM] WARNING: output[3] empty or wrong type\n");
    }
    printf("[CM] About to UA_Array_delete output\n");
    UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    printf("[CM] UA_Array_delete done\n");
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
                             req->publishingInterval,req->publisherVariableNodeId);

    UA_PubSubCommunicationConfigurationDataType pubCommCfg;
    memset(&pubCommCfg, 0, sizeof(pubCommCfg));
    pubCommCfg.pubSubConfiguration = pubCfg; 
    pubCommCfg.requireCompleteUpdate   = false;
    
    printf("[CM] BEFORE call: pubCommCfg.pubSubConfiguration.connectionsSize=%zu pdsSize=%zu\n",
       pubCommCfg.pubSubConfiguration.connectionsSize,
       pubCommCfg.pubSubConfiguration.publishedDataSetsSize);
       
    /* ── 4. Chiama EstablishConnections sul Publisher ─────── */
    printf("[CM] Calling EstablishConnections on Publisher (%s)...\n",
           pubNode->endpointUrl);

    UA_ConnectionEndpointConfigurationDataType pubEpCfg =
    buildEndpointConfiguration(
        "CE_Pub_001",
        req->publisherVariableNodeId,
        true
    );

    EstablishResult pubResult =
    callEstablishConnections(
        pubNode->endpointUrl,
        req->publisherAcName,
        req->publisherAcNodeId,

        &pubEpCfg,
        &pubCommCfg
    );
    //UA_PubSubConfiguration2DataType_clear(&pubCfg);

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
                              req->subscriberVariableNodeId,
                              req->subscriberVariable
                            );

    UA_PubSubCommunicationConfigurationDataType subCommCfg;
    memset(&subCommCfg, 0, sizeof(subCommCfg));
    subCommCfg.pubSubConfiguration   = subCfg;
    subCommCfg.requireCompleteUpdate = false;

    /* ── 6. Chiama EstablishConnections sul Subscriber ────── */
    printf("[CM] Calling EstablishConnections on Subscriber (%s)...\n",
           subNode->endpointUrl);
    
    UA_ConnectionEndpointConfigurationDataType subEpCfg =
    buildEndpointConfiguration(
        "CE_Sub_001",
        req->subscriberVariableNodeId,
        false
    );

    EstablishResult subResult =
    callEstablishConnections(
        subNode->endpointUrl,
        req->subscriberAcName,
        req->subscriberAcNodeId,

        &subEpCfg,
        &subCommCfg
    );
    //UA_PubSubConfiguration2DataType_clear(&subCfg);

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