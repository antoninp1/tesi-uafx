/* ============================================================
 * establish_connection.c
 *
 * Implementazione lato server del metodo OPC UA EstablishConnections
 * con firma UAFX-compliant (Part 81 §6.2.4).
 *
 * Firma:
 *   IN:  [0] CommandMask                       : UInt32
 *        [1] AssetVerifications                : array (ignorato)
 *        [2] ConnectionEndpointConfigurations  : array CE config
 *        [3] ReserveCommunicationIds           : array (ignorato)
 *        [4] CommunicationConfigurations       : array PubSub config
 *
 *   OUT: [0] AssetVerificationResults                : vuoto
 *        [1] ConnectionEndpointConfigurationResults  : popolato
 *        [2] ReserveCommunicationIdsResults          : vuoto
 *        [3] CommunicationConfigurationResults       : popolato
 *
 * Per ogni coppia (CE config, PubSub config) la callback:
 *   1. Estrae i campi direttamente dagli ExtensionObject decodificati
 *   2. Crea il ConnectionEndpoint come nodo OPC UA sotto la FE
 *   3. Costruisce PubSubConnection + WG/RG + DSW/DSR usando i dati estratti
 *   4. Abilita il WriterGroup/ReaderGroup
 *
 * Comandi gestiti tramite CommandMask:
 *   bit 2  → CreateConnectionEndpointCmd       (crea nodo CE)
 *   bit 7  → SetCommunicationConfigurationCmd  (setup PubSub)
 *   bit 8  → EnableCommunicationCmd            (freeze + setOperational)
 * Altri bit: loggati come no-op.
 * ============================================================ */

#include "establish_connection.h"
#include "types_uafx_data_generated.h"
#include "types_uafx_ac_generated.h"
#include <stdio.h>
#include <string.h>

/* ── Bit della FxCommandMask ─────────────────────────────── */
#define FX_CMD_CREATE_CE        (1u << 2)
#define FX_CMD_SET_COMM_CONFIG  (1u << 7)
#define FX_CMD_ENABLE_COMM      (1u << 8)

/* ── ConnectionEndpointStatusEnum (Part 81 §10.17) ───────── */
#define CE_STATUS_INITIAL          0
#define CE_STATUS_READY            1
#define CE_STATUS_PREOPERATIONAL   2
#define CE_STATUS_OPERATIONAL      3
#define CE_STATUS_ERROR            4

/* ============================================================
 * Cache namespace UAFX (risolti al primo utilizzo)
 * ============================================================ */
static UA_UInt16 g_nsFxAc   = 0;
static UA_UInt16 g_nsFxData = 0;
static bool      g_nsResolved = false;

static UA_UInt16 resolveNs(UA_Server *server, const char *uri) {
    UA_Variant v;
    UA_Variant_init(&v);
    UA_NodeId id = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_NAMESPACEARRAY);
    if(UA_Server_readValue(server, id, &v) != UA_STATUSCODE_GOOD ||
       !UA_Variant_hasArrayType(&v, &UA_TYPES[UA_TYPES_STRING])) {
        UA_Variant_clear(&v); return 0;
    }
    UA_String *arr = (UA_String *)v.data;
    UA_UInt16 found = 0;
    for(size_t i = 0; i < v.arrayLength; i++) {
        UA_String t = UA_STRING((char *)uri);
        if(UA_String_equal(&arr[i], &t)) { found = (UA_UInt16)i; break; }
    }
    UA_Variant_clear(&v);
    return found;
}

static bool ensureNs(UA_Server *server) {
    if(g_nsResolved) return true;
    g_nsFxAc = resolveNs(server, FXAC_NS_URI);
    g_nsFxData = resolveNs(server, FXDATA_NS_URI);
    if(g_nsFxAc == 0) return false;
    g_nsResolved = true;
    printf("[EC] Namespaces: FX/AC=%u FX/Data=%u\n", g_nsFxAc, g_nsFxData);
    return true;
}

/* ============================================================
 * Helper basici
 * ============================================================ */
static UA_LocalizedText lt(const char *t) {
    return UA_LOCALIZEDTEXT("en-US", (char *)t);
}

static UA_NodeId findChild(UA_Server *server, UA_NodeId parent,
                           UA_UInt16 ns, const char *name) {
    UA_RelativePathElement el;
    memset(&el, 0, sizeof(el));
    el.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    el.includeSubtypes = true;
    el.targetName = UA_QUALIFIEDNAME(ns, (char *)name);
    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = parent;
    bp.relativePath.elementsSize = 1;
    bp.relativePath.elements = &el;
    UA_BrowsePathResult bpr = UA_Server_translateBrowsePathToNodeIds(server, &bp);
    UA_NodeId r = UA_NODEID_NULL;
    if(bpr.statusCode == UA_STATUSCODE_GOOD && bpr.targetsSize > 0)
        UA_NodeId_copy(&bpr.targets[0].targetId.nodeId, &r);
    UA_BrowsePathResult_clear(&bpr);
    return r;
}

/* Risolve la cartella ConnectionEndpoints sotto la FE.
 * Prova ns=g_nsFxAc, poi ns=1, poi fallback creazione. */
static UA_NodeId resolveCeFolder(UA_Server *server, UA_NodeId fe) {
    UA_NodeId f = findChild(server, fe, g_nsFxAc, "ConnectionEndpoints");
    if(!UA_NodeId_isNull(&f)) return f;
    f = findChild(server, fe, 1, "ConnectionEndpoints");
    if(!UA_NodeId_isNull(&f)) return f;
    /* Fallback */
    UA_ObjectAttributes oa = UA_ObjectAttributes_default;
    oa.displayName = lt("ConnectionEndpoints");
    UA_NodeId nf = UA_NODEID_NULL;
    UA_Server_addObjectNode(server, UA_NODEID_NULL, fe,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, "ConnectionEndpoints"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), oa, NULL, &nf);
    return nf;
}

/* Cerca la prima FunctionalEntity sotto l'AC */
static UA_NodeId findFE(UA_Server *server, UA_NodeId acNode) {
    UA_NodeId feFolder = findChild(server, acNode, 1, "FunctionalEntities");
    if(UA_NodeId_isNull(&feFolder))
        feFolder = findChild(server, acNode, g_nsFxAc, "FunctionalEntities");
    if(UA_NodeId_isNull(&feFolder)) return UA_NODEID_NULL;

    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = feFolder;
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECT;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResult br = UA_Server_browse(server, 100, &bd);
    UA_NodeId fe = UA_NODEID_NULL;
    for(size_t i = 0; i < br.referencesSize; i++) {
        if(br.references[i].nodeClass == UA_NODECLASS_OBJECT) {
            UA_NodeId_copy(&br.references[i].nodeId.nodeId, &fe);
            break;
        }
    }
    UA_BrowseResult_clear(&br);
    return fe;
}

/* ============================================================
 * Crea il nodo CE come BaseObjectType con Properties standard.
 * Le Properties seguono il modello UAFX (Status, Mode, ecc.) ma
 * il tipo è BaseObjectType per evitare problemi di default-init
 * dei mandatory children di PubSubConnectionEndpointType in
 * open62541 v1.3.
 * ============================================================ */
static UA_NodeId createCeNode(UA_Server *server, UA_NodeId feNode,
                              const char *ceName, UA_UInt32 mode) {
    UA_NodeId ceFolder = resolveCeFolder(server, feNode);
    if(UA_NodeId_isNull(&ceFolder)) return UA_NODEID_NULL;

    /* Object CE */
    UA_ObjectAttributes oa = UA_ObjectAttributes_default;
    oa.displayName = lt(ceName);
    oa.description = lt("UAFX ConnectionEndpoint");
    UA_NodeId ce = UA_NODEID_NULL;
    UA_StatusCode rc = UA_Server_addObjectNode(server, UA_NODEID_NULL, ceFolder,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, (char *)ceName),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), oa, NULL, &ce);
    if(rc != UA_STATUSCODE_GOOD) {
        printf("[EC]   addObjectNode FAILED: %s\n", UA_StatusCode_name(rc));
        return UA_NODEID_NULL;
    }

    /* Helper inline per le 4 Properties */
    #define ADD_PROP_U32(NAME, VAL) do { \
        UA_VariableAttributes va = UA_VariableAttributes_default; \
        va.displayName = lt(NAME); \
        UA_UInt32 v = (VAL); \
        UA_Variant_setScalar(&va.value, &v, &UA_TYPES[UA_TYPES_UINT32]); \
        va.dataType = UA_TYPES[UA_TYPES_UINT32].typeId; \
        va.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE; \
        UA_Server_addVariableNode(server, UA_NODEID_NULL, ce, \
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY), \
            UA_QUALIFIEDNAME(1, NAME), \
            UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), va, NULL, NULL); \
    } while(0)

    ADD_PROP_U32("Status", CE_STATUS_INITIAL);
    ADD_PROP_U32("Mode", mode);

    /* IsPersistent (Boolean) */
    {
        UA_VariableAttributes va = UA_VariableAttributes_default;
        va.displayName = lt("IsPersistent");
        UA_Boolean v = false;
        UA_Variant_setScalar(&va.value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
        va.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
        va.accessLevel = UA_ACCESSLEVELMASK_READ;
        UA_Server_addVariableNode(server, UA_NODEID_NULL, ce,
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY),
            UA_QUALIFIEDNAME(1, "IsPersistent"),
            UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), va, NULL, NULL);
    }

    #undef ADD_PROP_U32
    return ce;
}

/* Aggiorna Status del CE */
static void setStatus(UA_Server *server, UA_NodeId ce, UA_UInt32 s) {
    UA_NodeId n = findChild(server, ce, 1, "Status");
    if(UA_NodeId_isNull(&n)) return;
    UA_Variant v;
    UA_Variant_setScalar(&v, &s, &UA_TYPES[UA_TYPES_UINT32]);
    UA_Server_writeValue(server, n, v);
}

/* ============================================================
 * Estrai parametri dalla PubSubConfiguration2DataType
 *
 * La struct arriva già decodificata da open62541 — qui leggo i campi
 * che mi servono e li metto in variabili locali.
 * ============================================================ */
typedef struct {
    UA_UInt32  publisherId;
    UA_UInt16  writerGroupId;
    UA_UInt16  dataSetWriterId;
    char       url[256];
    char       iface[64];
    UA_Double  publishingInterval;
    bool       isPublisher;
    UA_NodeId  variableId;  /* OutputVariableIds[0] o InputVariableIds[0] dal CE */
} PsParams;

static bool extractPubSub(const UA_PubSubConfiguration2DataType *cfg,
                          PsParams *p) {
    printf("[EC] extractPubSub: cfg=%p\n", (void*)cfg);
    if(!cfg) { printf("[EC]   cfg is NULL\n"); return false; }
    printf("[EC]   connectionsSize=%zu publishedDataSetsSize=%zu\n",
           cfg->connectionsSize, cfg->publishedDataSetsSize);
    if(cfg->connectionsSize == 0) return false;
    
    if(!cfg || cfg->connectionsSize == 0) return false;
    const UA_PubSubConnectionDataType *conn = &cfg->connections[0];

    /* publisherId (Variant) */
    if(conn->publisherId.type == &UA_TYPES[UA_TYPES_UINT16])
        p->publisherId = *(UA_UInt16*)conn->publisherId.data;
    else if(conn->publisherId.type == &UA_TYPES[UA_TYPES_UINT32])
        p->publisherId = *(UA_UInt32*)conn->publisherId.data;

    /* address: ExtensionObject che wrappa NetworkAddressUrlDataType */
    printf("[EC]   address encoding=%d type=%s data=%p\n",
       (int)conn->address.encoding,
       conn->address.content.decoded.type
         ? conn->address.content.decoded.type->typeName : "NULL",
       conn->address.content.decoded.data);

if((conn->address.encoding == UA_EXTENSIONOBJECT_DECODED ||
    conn->address.encoding == UA_EXTENSIONOBJECT_DECODED_NODELETE) &&
   conn->address.content.decoded.type ==
       &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE] &&
   conn->address.content.decoded.data != NULL) {
    const UA_NetworkAddressUrlDataType *a =
        (const UA_NetworkAddressUrlDataType *)conn->address.content.decoded.data;
    size_t n = a->url.length < sizeof(p->url) - 1
               ? a->url.length : sizeof(p->url) - 1;
    memcpy(p->url, a->url.data, n); p->url[n] = '\0';
    n = a->networkInterface.length < sizeof(p->iface) - 1
        ? a->networkInterface.length : sizeof(p->iface) - 1;
    memcpy(p->iface, a->networkInterface.data, n); p->iface[n] = '\0';
    printf("[EC]   parsed: iface='%s' url='%s'\n", p->iface, p->url);
} else {
    printf("[EC]   address NOT parsed (encoding mismatch or NULL)\n");
}

    /* Pub vs Sub */
    if(conn->writerGroupsSize > 0) {
        p->isPublisher = true;
        const UA_WriterGroupDataType *wg = &conn->writerGroups[0];
        p->writerGroupId = wg->writerGroupId;
        p->publishingInterval = wg->publishingInterval;
        if(wg->dataSetWritersSize > 0)
            p->dataSetWriterId = wg->dataSetWriters[0].dataSetWriterId;
    } else if(conn->readerGroupsSize > 0) {
        p->isPublisher = false;
        const UA_ReaderGroupDataType *rg = &conn->readerGroups[0];
        if(rg->dataSetReadersSize > 0) {
            const UA_DataSetReaderDataType *dsr = &rg->dataSetReaders[0];
            p->writerGroupId = dsr->writerGroupId;
            p->dataSetWriterId = dsr->dataSetWriterId;
            if(dsr->publisherId.type == &UA_TYPES[UA_TYPES_UINT16])
                p->publisherId = *(UA_UInt16*)dsr->publisherId.data;
        }
    } else return false;

    return true;
}

/* ============================================================
 * Estrai parametri dal ConnectionEndpointConfigurationDataType
 * ============================================================ */
typedef struct {
    char       ceName[128];
    UA_UInt32  mode;     /* 2=Pub, 3=Sub */
    UA_NodeId  variableId;
} CeParams;

static bool extractCe(const UA_ConnectionEndpointConfigurationDataType *ce,
                      CeParams *out) {
    memset(out, 0, sizeof(*out));
    if(ce->connectionEndpoint.switchField !=
       UA_CONNECTIONENDPOINTDEFINITIONDATATYPESWITCH_PARAMETER) return false;

    const UA_ExtensionObject *ext = &ce->connectionEndpoint.fields.parameter;
    if(ext->encoding != UA_EXTENSIONOBJECT_DECODED &&
       ext->encoding != UA_EXTENSIONOBJECT_DECODED_NODELETE) return false;
    if(ext->content.decoded.type !=
       &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBCONNECTIONENDPOINTPARAMETERDATATYPE])
        return false;

    const UA_PubSubConnectionEndpointParameterDataType *p =
        (const UA_PubSubConnectionEndpointParameterDataType *)ext->content.decoded.data;

    size_t n = p->name.length < sizeof(out->ceName) - 1
               ? p->name.length : sizeof(out->ceName) - 1;
    memcpy(out->ceName, p->name.data, n); out->ceName[n] = '\0';

    out->mode = (UA_UInt32)p->mode;

    if(p->mode == UA_PUBSUBCONNECTIONENDPOINTMODEENUM_PUBLISHER &&
       p->outputVariableIdsSize > 0)
        UA_NodeId_copy(&p->outputVariableIds[0], &out->variableId);
    else if(p->mode == UA_PUBSUBCONNECTIONENDPOINTMODEENUM_SUBSCRIBER &&
            p->inputVariableIdsSize > 0)
        UA_NodeId_copy(&p->inputVariableIds[0], &out->variableId);
    else return false;

    return true;
}

/* ============================================================
 * Setup PubSub: crea Connection + WG/RG + DSW/DSR usando i
 * parametri estratti. Restituisce il NodeId del WG/RG creato
 * (per il successivo enable).
 * ============================================================ */
static UA_StatusCode setupPubSub(UA_Server *server, const PsParams *p,
                                 UA_NodeId varId, UA_NodeId *outGroup,
                                 UA_NodeId *outDswOrDsr) {
    UA_StatusCode rc;

    /* PubSubConnection */
    UA_PubSubConnectionConfig cc;
    memset(&cc, 0, sizeof(cc));
    cc.name = p->isPublisher
        ? UA_STRING("UAFX_Pub_Conn") : UA_STRING("UAFX_Sub_Conn");
    cc.transportProfileUri =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    cc.enabled = true;

    UA_NetworkAddressUrlDataType addr;
    addr.networkInterface = UA_STRING((char *)p->iface);
    addr.url = UA_STRING((char *)p->url);
    UA_Variant_setScalar(&cc.address, &addr,
        &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

    cc.publisherIdType = UA_PUBLISHERIDTYPE_UINT32;
    cc.publisherId.uint32 = p->isPublisher ? p->publisherId : p->publisherId + 1000;

    UA_NodeId connId;
    rc = UA_Server_addPubSubConnection(server, &cc, &connId);
    if(rc != UA_STATUSCODE_GOOD) return rc;
    printf("[EC]     + PubSubConnection\n");

    if(p->isPublisher) {
        /* PDS */
        UA_PublishedDataSetConfig pds;
        memset(&pds, 0, sizeof(pds));
        pds.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
        pds.name = UA_STRING("UAFX_PDS");
        UA_NodeId pdsId = UA_NODEID_NULL;
        UA_AddPublishedDataSetResult pdsR =
            UA_Server_addPublishedDataSet(server, &pds, &pdsId);
        if(pdsR.addResult != UA_STATUSCODE_GOOD) return pdsR.addResult;

        /* DSF sulla variabile */
        UA_DataSetFieldConfig dsf;
        memset(&dsf, 0, sizeof(dsf));
        dsf.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
        dsf.field.variable.fieldNameAlias = UA_STRING("Field");
        dsf.field.variable.publishParameters.publishedVariable = varId;
        dsf.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
        UA_Server_addDataSetField(server, pdsId, &dsf, NULL);

        /* WG */
        UA_WriterGroupConfig wgc;
        memset(&wgc, 0, sizeof(wgc));
        wgc.name = UA_STRING("UAFX_WG");
        wgc.publishingInterval = p->publishingInterval;
        wgc.writerGroupId = p->writerGroupId;
        wgc.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
        wgc.messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
        wgc.messageSettings.content.decoded.type =
            &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE];
        UA_UadpWriterGroupMessageDataType *wgm =
            UA_UadpWriterGroupMessageDataType_new();
        wgm->networkMessageContentMask =
            UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
            UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
            UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
            UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER;
        wgc.messageSettings.content.decoded.data = wgm;

        rc = UA_Server_addWriterGroup(server, connId, &wgc, outGroup);
        UA_UadpWriterGroupMessageDataType_delete(wgm);
        if(rc != UA_STATUSCODE_GOOD) return rc;
        printf("[EC]     + WriterGroup id=%u\n", p->writerGroupId);

        /* DSW */
        UA_DataSetWriterConfig dswc;
        memset(&dswc, 0, sizeof(dswc));
        dswc.name = UA_STRING("UAFX_DSW");
        dswc.dataSetWriterId = p->dataSetWriterId;
        dswc.keyFrameCount = 10;
        rc = UA_Server_addDataSetWriter(server, *outGroup, pdsId, &dswc, outDswOrDsr);
        if(rc != UA_STATUSCODE_GOOD) return rc;
        printf("[EC]     + DataSetWriter id=%u\n", p->dataSetWriterId);
    } else {
        /* RG */
        UA_ReaderGroupConfig rgc;
        memset(&rgc, 0, sizeof(rgc));
        rgc.name = UA_STRING("UAFX_RG");
        rc = UA_Server_addReaderGroup(server, connId, &rgc, outGroup);
        if(rc != UA_STATUSCODE_GOOD) return rc;
        printf("[EC]     + ReaderGroup\n");

        /* DSR */
        UA_DataSetReaderConfig drc;
        memset(&drc, 0, sizeof(drc));
        drc.name = UA_STRING("UAFX_DSR");
        UA_UInt32 pid = p->publisherId;
        drc.publisherId.type = &UA_TYPES[UA_TYPES_UINT32];
        drc.publisherId.data = &pid;
        drc.writerGroupId = p->writerGroupId;
        drc.dataSetWriterId = p->dataSetWriterId;

        UA_FieldTargetVariable *tv =
            (UA_FieldTargetVariable *)UA_calloc(1, sizeof(UA_FieldTargetVariable));
        tv[0].targetVariable.attributeId = UA_ATTRIBUTEID_VALUE;
        UA_NodeId_copy(&varId, &tv[0].targetVariable.targetNodeId);
        drc.subscribedDataSet.subscribedDataSetTarget.targetVariablesSize = 1;
        drc.subscribedDataSet.subscribedDataSetTarget.targetVariables = tv;
        /* DataSetMetaData: 1 field di tipo Float */
        UA_DataSetMetaDataType_init(&drc.dataSetMetaData);
        drc.dataSetMetaData.name = UA_STRING("UAFX_DataSet");
        drc.dataSetMetaData.fieldsSize = 1;
        drc.dataSetMetaData.fields =
            (UA_FieldMetaData *)UA_calloc(1, sizeof(UA_FieldMetaData));
        UA_FieldMetaData_init(&drc.dataSetMetaData.fields[0]);
        drc.dataSetMetaData.fields[0].name        = UA_STRING("Field");
        drc.dataSetMetaData.fields[0].builtInType = UA_NS0ID_FLOAT;
        drc.dataSetMetaData.fields[0].dataType    = UA_TYPES[UA_TYPES_FLOAT].typeId;
        drc.dataSetMetaData.fields[0].valueRank   = -1;
        UA_UadpDataSetReaderMessageDataType dsrMsg;
        memset(&dsrMsg, 0, sizeof(dsrMsg));
        dsrMsg.networkMessageContentMask =
            UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID   |
            UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER   |
            UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
            UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER;
        drc.messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
        drc.messageSettings.content.decoded.type =
            &UA_TYPES[UA_TYPES_UADPDATASETREADERMESSAGEDATATYPE];
        drc.messageSettings.content.decoded.data = &dsrMsg;

        rc = UA_Server_addDataSetReader(server, *outGroup, &drc, outDswOrDsr);
        UA_NodeId_clear(&tv[0].targetVariable.targetNodeId);
        UA_free(tv);
        if(rc != UA_STATUSCODE_GOOD) return rc;
        printf("[EC]     + DataSetReader (filter pubId=%u wg=%u dsw=%u)\n",
               p->publisherId, p->writerGroupId, p->dataSetWriterId);
    }
    return UA_STATUSCODE_GOOD;
}

/* ============================================================
 * Processa una coppia (CE config, PubSub config).
 * Restituisce status code e popola ceNodeIdOut col NodeId del CE creato.
 * ============================================================ */
static UA_StatusCode processOnePair(UA_Server *server, UA_NodeId feNode,
                                    UA_UInt32 commandMask,
                                    const UA_ConnectionEndpointConfigurationDataType *ceCfg,
                                    const UA_PubSubCommunicationConfigurationDataType *commCfg,
                                    UA_NodeId *ceNodeIdOut) {
    UA_NodeId_init(ceNodeIdOut);

    /* Estrai dati */
    CeParams ce;
    if(!extractCe(ceCfg, &ce)) {
        printf("[EC]   extractCe FAILED\n");
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }
    PsParams ps;
    memset(&ps, 0, sizeof(ps));
    if(!extractPubSub(&commCfg->pubSubConfiguration, &ps)) {
        printf("[EC]   extractPubSub FAILED\n");
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    /* Sanity check Mode coerenza */
    bool ceIsPub = (ce.mode == UA_PUBSUBCONNECTIONENDPOINTMODEENUM_PUBLISHER);
    if(ceIsPub != ps.isPublisher) {
        printf("[EC]   role mismatch CE vs PubSub config\n");
        UA_NodeId_clear(&ce.variableId);
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    printf("[EC] processing CE='%s' role=%s pubId=%u wg=%u dsw=%u url=%s\n",
           ce.ceName, ceIsPub ? "Pub" : "Sub",
           ps.publisherId, ps.writerGroupId, ps.dataSetWriterId, ps.url);

    /* Default iface se mancante */
    if(ps.iface[0] == '\0') strcpy(ps.iface, "enp0s31f6");
    if(ps.publishingInterval <= 0) ps.publishingInterval = 1000.0;

    UA_NodeId ceNode = UA_NODEID_NULL;
    UA_NodeId groupNode = UA_NODEID_NULL;
    UA_NodeId dswDsrNode = UA_NODEID_NULL;

    /* ── CreateCE ── */
    if(commandMask & FX_CMD_CREATE_CE) {
        printf("[EC]   → CreateConnectionEndpointCmd\n");
        ceNode = createCeNode(server, feNode, ce.ceName, ce.mode);
        if(UA_NodeId_isNull(&ceNode)) {
            UA_NodeId_clear(&ce.variableId);
            return UA_STATUSCODE_BADINTERNALERROR;
        }
        UA_NodeId_copy(&ceNode, ceNodeIdOut);
        printf("[EC]     + CE '%s' (ns=%u;i=%u)\n",
               ce.ceName, ceNode.namespaceIndex, ceNode.identifier.numeric);
    }

    /* ── SetCommConfig ── */
    if(commandMask & FX_CMD_SET_COMM_CONFIG) {
        printf("[EC]   → SetCommunicationConfigurationCmd\n");
        UA_StatusCode rc = setupPubSub(server, &ps, ce.variableId,
                                       &groupNode, &dswDsrNode);
        if(rc != UA_STATUSCODE_GOOD) {
            if(!UA_NodeId_isNull(&ceNode)) setStatus(server, ceNode, CE_STATUS_ERROR);
            UA_NodeId_clear(&ce.variableId);
            return rc;
        }
        /* Link CE → DSW/DSR */
        if(!UA_NodeId_isNull(&ceNode) && !UA_NodeId_isNull(&dswDsrNode)) {
            UA_Server_addReference(server, ceNode,
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                UA_EXPANDEDNODEID_NUMERIC(dswDsrNode.namespaceIndex,
                                          dswDsrNode.identifier.numeric),
                true);
        }
        if(!UA_NodeId_isNull(&ceNode)) setStatus(server, ceNode, CE_STATUS_READY);
    }

    /* ── Enable ── */
    if(commandMask & FX_CMD_ENABLE_COMM) {
        printf("[EC]   → EnableCommunicationCmd\n");
        if(!UA_NodeId_isNull(&groupNode)) {
            if(ps.isPublisher) {
                UA_Server_freezeWriterGroupConfiguration(server, groupNode);
                UA_Server_setWriterGroupOperational(server, groupNode);
                printf("[EC]     + WriterGroup ENABLED\n");
            } else {
                UA_Server_freezeReaderGroupConfiguration(server, groupNode);
                UA_Server_setReaderGroupOperational(server, groupNode);
                printf("[EC]     + ReaderGroup ENABLED\n");
            }
            if(!UA_NodeId_isNull(&ceNode))
                setStatus(server, ceNode, CE_STATUS_PREOPERATIONAL);
        }
    }

    UA_NodeId_clear(&ce.variableId);
    return UA_STATUSCODE_GOOD;
}

/* ============================================================
 * Callback OPC UA
 * ============================================================ */
static UA_StatusCode
establishConnectionsCallback(UA_Server *server,
        const UA_NodeId *sessionId, void *sessionContext,
        const UA_NodeId *methodId, void *methodContext,
        const UA_NodeId *objectId, void *objectContext,
        size_t inputSize, const UA_Variant *input,
        size_t outputSize, UA_Variant *output) {
printf("[EC] *** CALLBACK ENTERED *** inSize=%zu outSize=%zu\n",
       inputSize, outputSize);
    (void)sessionId; (void)sessionContext;
    (void)methodId; (void)methodContext; (void)objectContext;

    if(inputSize != 5 || outputSize != 4)
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    if(!ensureNs(server)) return UA_STATUSCODE_BADINTERNALERROR;

    UA_UInt32 mask = *(UA_UInt32 *)input[0].data;
    printf("\n[EC] ═══ EstablishConnections called, mask=0x%03X ═══\n", mask);

    /* Count CE + Comm pairs */
    size_t nCe = input[2].arrayLength > 0 ? input[2].arrayLength
                  : (input[2].data ? 1 : 0);
    size_t nComm = input[4].arrayLength > 0 ? input[4].arrayLength
                    : (input[4].data ? 1 : 0);
    if(nCe == 0 || nCe != nComm) {
        printf("[EC] bad pairing nCe=%zu nComm=%zu\n", nCe, nComm);
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    const UA_ConnectionEndpointConfigurationDataType *ceArr =
        (const UA_ConnectionEndpointConfigurationDataType *)input[2].data;
    const UA_PubSubCommunicationConfigurationDataType *commArr =
        (const UA_PubSubCommunicationConfigurationDataType *)input[4].data;

    printf("[EC] commArr[0]: pubSubConfig connectionsSize=%zu, pdsSize=%zu, "
       "requireCompleteUpdate=%d\n",
       commArr[0].pubSubConfiguration.connectionsSize,
       commArr[0].pubSubConfiguration.publishedDataSetsSize,
       commArr[0].requireCompleteUpdate);
    /* Risolvi FE */
    UA_NodeId feNode = findFE(server, *objectId);
    if(UA_NodeId_isNull(&feNode)) {
        printf("[EC] FE not found under AC ns=%u;i=%u\n",
               objectId->namespaceIndex, objectId->identifier.numeric);
        return UA_STATUSCODE_BADNOTFOUND;
    }
    printf("[EC] FE = ns=%u;i=%u\n",
           feNode.namespaceIndex, feNode.identifier.numeric);

    /* Risultati */
    UA_ConnectionEndpointConfigurationResultDataType *ceRes =
        (UA_ConnectionEndpointConfigurationResultDataType *)UA_Array_new(nCe,
            &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_CONNECTIONENDPOINTCONFIGURATIONRESULTDATATYPE]);
    UA_PubSubCommunicationConfigurationResultDataType *commRes =
        (UA_PubSubCommunicationConfigurationResultDataType *)UA_Array_new(nCe,
            &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONCONFIGURATIONRESULTDATATYPE]);

    bool allOk = true;
    for(size_t i = 0; i < nCe; i++) {
        UA_NodeId ceId = UA_NODEID_NULL;
        UA_StatusCode sc = processOnePair(server, feNode, mask,
                                          &ceArr[i], &commArr[i], &ceId);
        ceRes[i].connectionEndpointResult = sc;
        UA_NodeId_copy(&ceId, &ceRes[i].connectionEndpointId);
        commRes[i].result = sc;
        if(sc == UA_STATUSCODE_GOOD && !UA_NodeId_isNull(&ceId)) {
            commRes[i].configurationObjectsSize = 1;
            commRes[i].configurationObjects =
                (UA_NodeId *)UA_Array_new(1, &UA_TYPES[UA_TYPES_NODEID]);
            UA_NodeId_copy(&ceId, &commRes[i].configurationObjects[0]);
        }
        if(sc != UA_STATUSCODE_GOOD) allOk = false;
        UA_NodeId_clear(&ceId);
    }
    UA_NodeId_clear(&feNode);

    /* Pack output */
    UA_Variant_init(&output[0]);  /* AssetVerification — vuoto */
    UA_Variant_setArray(&output[1], ceRes, nCe,
        &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_CONNECTIONENDPOINTCONFIGURATIONRESULTDATATYPE]);
    UA_Variant_init(&output[2]);  /* ReserveCommIds — vuoto */
    UA_Variant_setArray(&output[3], commRes, nCe,
        &UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONCONFIGURATIONRESULTDATATYPE]);

    printf("[EC] ═══ Done: %s ═══\n\n", allOk ? "all OK" : "some failed");
    return allOk ? UA_STATUSCODE_GOOD : UA_STATUSCODE_UNCERTAIN;
}

/* ============================================================
 * Registrazione del metodo sull'AC
 * ============================================================ */
void registerEstablishConnectionsMethod(UA_Server *server, UA_NodeId acNode) {
    UA_Argument inArgs[5];
    for(int i = 0; i < 5; i++) UA_Argument_init(&inArgs[i]);

    /* Input */
    inArgs[0].name = UA_STRING("CommandMask");
    inArgs[0].dataType = UA_TYPES[UA_TYPES_UINT32].typeId;
    inArgs[0].valueRank = UA_VALUERANK_SCALAR;

    inArgs[1].name = UA_STRING("AssetVerifications");
    inArgs[1].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_ASSETVERIFICATIONDATATYPE].typeId;
    inArgs[1].valueRank = UA_VALUERANK_ONE_DIMENSION;

    inArgs[2].name = UA_STRING("ConnectionEndpointConfigurations");
    inArgs[2].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_CONNECTIONENDPOINTCONFIGURATIONDATATYPE].typeId;
    inArgs[2].valueRank = UA_VALUERANK_ONE_DIMENSION;

    inArgs[3].name = UA_STRING("ReserveCommunicationIds");
    inArgs[3].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBRESERVECOMMUNICATIONIDSDATATYPE].typeId;
    inArgs[3].valueRank = UA_VALUERANK_ONE_DIMENSION;

    inArgs[4].name = UA_STRING("CommunicationConfigurations");
    inArgs[4].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONCONFIGURATIONDATATYPE].typeId;  /* ← FIX qui */
    inArgs[4].valueRank = UA_VALUERANK_ONE_DIMENSION;

/* Output */
UA_Argument outArgs[4];
for(int i = 0; i < 4; i++) UA_Argument_init(&outArgs[i]);
outArgs[0].name = UA_STRING("AssetVerificationResults");
outArgs[0].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_ASSETVERIFICATIONRESULTDATATYPE].typeId;
outArgs[0].valueRank = UA_VALUERANK_ONE_DIMENSION;

outArgs[1].name = UA_STRING("ConnectionEndpointConfigurationResults");
outArgs[1].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_CONNECTIONENDPOINTCONFIGURATIONRESULTDATATYPE].typeId;
outArgs[1].valueRank = UA_VALUERANK_ONE_DIMENSION;

outArgs[2].name = UA_STRING("ReserveCommunicationIdsResults");
outArgs[2].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBRESERVECOMMUNICATIONIDSRESULTDATATYPE].typeId;
outArgs[2].valueRank = UA_VALUERANK_ONE_DIMENSION;

outArgs[3].name = UA_STRING("CommunicationConfigurationResults");
outArgs[3].dataType = UA_TYPES_UAFX_DATA[UA_TYPES_UAFX_DATA_PUBSUBCOMMUNICATIONCONFIGURATIONRESULTDATATYPE].typeId;
outArgs[3].valueRank = UA_VALUERANK_ONE_DIMENSION;

    UA_MethodAttributes attr = UA_MethodAttributes_default;
    attr.displayName = lt("EstablishConnections");
    attr.description = lt("UAFX EstablishConnections (Part 81 §6.2.4)");
    attr.executable = true;
    attr.userExecutable = true;

    UA_Server_addMethodNode(server, UA_NODEID_NULL, acNode,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, "EstablishConnections"),
        attr, &establishConnectionsCallback,
        5, inArgs, 4, outArgs, NULL, NULL);

    printf("[SERVER] + Method EstablishConnections registered on AC\n");
}
