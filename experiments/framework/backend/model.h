/* ============================================================
 * model.h
 *
 * Strutture dati per il modello topologico UAFX.
 *
 * Tre layer:
 *   1. Layer fisico:  nodi (dispositivi) + link (connessioni)
 *   2. Layer logico:  connessioni PubSub (fase futura)
 *   3. Layer TSN:     schedule di rete  (fase futura)
 *
 * Questo file copre il layer fisico completo + le strutture
 * applicative UAFX (AutomationComponent, FunctionalEntity,
 * Asset) che il client popola durante il browse.
 * ============================================================ */

#ifndef UAFX_MODEL_H
#define UAFX_MODEL_H
#include "open62541.h"
#include "common.h"
#include <stdbool.h>

/* ============================================================
 * Limiti degli array statici
 *
 * Usati per semplicita' al posto di allocazione dinamica.
 * Sufficienti per un testbed di dimensioni tipiche.
 * ============================================================ */

#define MAX_DATA_VARIABLES       8
#define MAX_FUNCTIONAL_ENTITIES  4
#define MAX_ASSETS               4
#define MAX_CONN_ENDPOINTS       4
#define MAX_AUTOMATION_COMPS      2
#define MAX_NETWORK_INTERFACES    16
#define MAX_LLDP_NEIGHBORS        8
#define MAX_TOPOLOGY_NODES       16
#define MAX_TOPOLOGY_LINKS       64
#define MAX_LOGICAL_CONNECTIONS  32
#define MAX_DISCOVERY_QUEUE      32
#define MAX_LOCAL_PORTS		 16
/* ============================================================
 * DataVariable
 *
 * Una variabile dentro OutputData/ o InputData/.
 * Contiene il nome, il tipo OPC UA e il valore corrente
 * come unione.
 * ============================================================ */

typedef enum {
    DATATYPE_UNKNOWN = 0,
    DATATYPE_FLOAT,
    DATATYPE_DOUBLE,
    DATATYPE_INT32,
    DATATYPE_UINT32,
    DATATYPE_BOOLEAN,
    DATATYPE_STRING
} DataType;

typedef struct {
    char     name[MAX_STR_LEN];
    char nodeId[MAX_STR_LEN]; 

    DataType type;
    union {
        float       fVal;
        double      dVal;
        int32_t     i32Val;
        uint32_t    u32Val;
        bool        bVal;
        char        sVal[MAX_STR_LEN];
    } value;
    char     engineeringUnits[MAX_STR_LEN];   /* es. "°C", "bar" */
} DataVariable;

/* ============================================================
 * ConnectionEndpoint
 *
 * Rappresenta un ConnectionEndpoint UAFX (Part 81, 6.5).
 * Popolato sia dal browse (Phase 1/3) sia da
 * establishConnection (nuove connessioni da frontend).
 * ============================================================ */



typedef struct {
    char      name[MAX_STR_LEN];
    UA_NodeId nodeId;                        /* NodeId del CE sul server */

    /* Ruolo — letto da Mode sul server */
    uint32_t  mode;                          /* 0=Publisher 1=Subscriber 2=Both */

    /* Stato operativo */
    char      status[MAX_STR_LEN];           /* es. "Operational", "Ready" */
    bool      isPersistent;

    /* Partner — da RelatedEndpoint */
    char      relatedEndpoint[MAX_STR_LEN];

    /* URI del ConnectionManager che ha creato il CE */
    char      connectionManagerUri[MAX_STR_LEN];

    /* Variabile collegata (output per pub input per sub) */
    char      linkedVariable[MAX_STR_LEN];


    /* Reference ai nodi PubSub sul server */
    UA_NodeId dataSetWriterRef;              /* ToDataSetWriter (Publisher) */
    UA_NodeId dataSetReaderRef;              /* ToDataSetReader (Subscriber) */
} ConnectionEndpoint;

/* ============================================================
 * FunctionalEntity
 *
 * Entita' funzionale UAFX (Part 81, 6.4).
 * Contiene identificazione, dati I/O e gli endpoint
 * di connessione (usati nelle fasi future per PubSub).
 * ============================================================ */

typedef struct {
    char         name[MAX_STR_LEN];
    char nodeId[MAX_STR_LEN]; 
    /* Identificazione (Part 81, IFunctionalEntityType) */
    char         authorUri[MAX_STR_LEN];
    char         authorAssignedIdentifier[MAX_STR_LEN];
    char         authorAssignedVersion[MAX_STR_LEN];
    uint32_t     operationalHealth;

    /* Dati di processo */
    DataVariable outputData[MAX_DATA_VARIABLES];
    size_t       outputDataCount;

    DataVariable inputData[MAX_DATA_VARIABLES];
    size_t       inputDataCount;

    /* ConnectionEndpoints (nomi dei CE presenti) */
    ConnectionEndpoint         connectionEndpoints[MAX_CONN_ENDPOINTS];
    size_t       connectionEndpointsCount;
} FunctionalEntity;

/* ============================================================
 * Asset
 *
 * Asset fisico o logico UAFX (Part 81, 6.3 / FxAssetType).
 * Proprieta' DI standard da IVendorNameplateType e
 * ITagNameplateType.
 * ============================================================ */

typedef struct {
    char name[MAX_STR_LEN];

    /* Proprieta' DI (OPC 10000-100 / IVendorNameplateType) */
    char manufacturer[MAX_STR_LEN];
    char manufacturerUri[MAX_STR_LEN];
    char model[MAX_STR_LEN];
    char productCode[MAX_STR_LEN];
    char hardwareRevision[MAX_STR_LEN];
    char softwareRevision[MAX_STR_LEN];
    char deviceClass[MAX_STR_LEN];
    char serialNumber[MAX_STR_LEN];
} Asset;

/* ============================================================
 * ComponentCapabilities
 *
 * Capacita' dell'AutomationComponent (Part 81, 6.2).
 * ============================================================ */

typedef struct {
    uint32_t maxConnections;
    uint32_t minConnections;
} ComponentCapabilities;

/* ============================================================
 * AutomationComponent
 *
 * Componente di automazione UAFX (Part 81, 6.2).
 * Contiene Assets, FunctionalEntities e Capabilities.
 * ============================================================ */

typedef struct {
    char                  name[MAX_STR_LEN];
    char nodeId[MAX_STR_LEN]; 

    char                  conformanceName[MAX_STR_LEN];
    uint32_t              aggregatedHealth;

    Asset                 assets[MAX_ASSETS];
    size_t                assetsCount;

    FunctionalEntity      functionalEntities[MAX_FUNCTIONAL_ENTITIES];
    size_t                functionalEntitiesCount;

    ComponentCapabilities capabilities;
} AutomationComponent;

/* ============================================================
 * LldpNeighbor
 *
 * Dati LLDP di un vicino ricevuti su una porta.
 * Campi conformi ai TLV obbligatori definiti in
 * Part 82 §7.3.2.2.4.
 * ============================================================ */

typedef struct {
    /* Chassis TLV (Part 82, 7.3.2.2.4.2) */
    char     chassisId[MAX_STR_LEN];        /* MAC o altro ID */
    uint32_t chassisIdSubtype;              /* 4 = MAC address */

    /* Sistema */
    char     sysName[MAX_STR_LEN];
    char     sysDescr[MAX_STR_LEN];

    /* Management Address TLV (Part 82, 7.3.2.2.4.5) */
    char     mgmtAddress[MAX_STR_LEN];      /* IPv4 */

    /* Port ID TLV (Part 82, 7.3.2.2.4.3) */
    char     portId[MAX_STR_LEN];           /* MAC o ifName */
    uint32_t portIdSubtype;                 /* 3=MAC, 5=ifName */
    char     portDescr[MAX_STR_LEN];        /* es. "PORT_4" */

    /* System Capabilities TLV (Part 82, 7.3.2.2.4.4) */
    char     systemCapabilities[MAX_STR_LEN]; /* "Bridge", "Station Only", etc. */

    /* Time To Live */
    uint32_t timeToLive;
} LldpNeighbor;

/* ============================================================
 * LldpLocalData
 *
 * Dati LLDP che il dispositivo locale annuncia.
 * Corrispondono al LLDP Local System YANG (Part 82, 7.3.2.2.5).
 * ============================================================ */

typedef struct {
    char     chassisId[MAX_STR_LEN];
    uint32_t chassisIdSubtype;
    char     sysName[MAX_STR_LEN];
    char     sysDescr[MAX_STR_LEN];
    char     mgmtAddress[MAX_STR_LEN];
    char     systemCapabilities[MAX_STR_LEN];
    char     portId[MAX_STR_LEN];
    uint32_t portIdSubtype;
} LldpLocalData;

/* ============================================================
 * NetworkInterface
 *
 * Interfaccia di rete fisica con dati LLDP.
 * Corrisponde a un'istanza di IetfBaseNetworkInterfaceType
 * nella cartella NetworkInterfaces/ (Part 82, 6.5.2 / 6.5.3).
 * ============================================================ */

typedef struct {
    char          name[MAX_STR_LEN];          /* es. "enp0s31f6" */

    /* Proprieta' interfaccia (Part 82, 6.5.3) */
    char          adminStatus[MAX_STR_LEN];   /* "up" / "down" */
    char          operStatus[MAX_STR_LEN];
    char          physAddress[MAX_STR_LEN];   /* MAC address */
    uint32_t      speed;                      /* Mbps */

    /* LLDP (Part 82, 7.3.2) */
    LldpLocalData localData;
    bool          hasLocalData;

    LldpNeighbor  neighbors[MAX_LLDP_NEIGHBORS];
    size_t        neighborsCount;
} NetworkInterface;

/* ============================================================
 * TopologyNodeType
 *
 * Tipo di nodo nel grafo topologico.
 * ============================================================ */

typedef enum {
    NODE_TYPE_UNKNOWN = 0,
    NODE_TYPE_UAFX_SERVER,     /* Dispositivo con server OPC UA FX */
    NODE_TYPE_SWITCH,          /* Switch di rete (es. RELY-10TSN12) */
    NODE_TYPE_PHANTOM_SWITCH   /* Switch inferito da LLDP ma non raggiungibile */
} TopologyNodeType;

/* ============================================================
 * TopologyNode
 *
 * Un dispositivo nella topologia di rete.
 * Puo' essere un server UAFX, uno switch fisico o uno
 * switch "phantom" inferito dal BFS LLDP.
 *
 * La chiave univoca e' il chassisId (Part 82, 7.3.2.2.4.2:
 * "shall contain the same value for all transmitted LLDPDUs
 * independent from the transmitting port").
 * ============================================================ */

typedef struct {
    /* Identificazione */
    char              id[MAX_STR_LEN];          /* chassisId = chiave univoca */
    char              name[MAX_STR_LEN];        /* sysName LLDP o applicationName */
    char              description[MAX_STR_LEN]; /* sysDescr LLDP */
    TopologyNodeType  type;

    /* Raggiungibilita' */
    char              mgmtAddress[MAX_STR_LEN]; /* IP di management (da LLDP MgmtAddress TLV) */
    char              endpointUrl[MAX_STR_LEN]; /* OPC UA endpoint (solo per UAFX_SERVER) */
    bool              reachable;                /* true se il dispositivo e' stato contattato */

    /* Dati applicativi UAFX (solo per NODE_TYPE_UAFX_SERVER) */
    char              applicationUri[MAX_STR_LEN];
    char              applicationName[MAX_STR_LEN];
    AutomationComponent automationComponents[MAX_AUTOMATION_COMPS];
    size_t              automationComponentsCount;

    /* Interfacce di rete con LLDP */
    NetworkInterface  interfaces[MAX_NETWORK_INTERFACES];
    size_t            interfacesCount;

    /* Flag per il BFS */
    bool              visited;
} TopologyNode;

/* ============================================================
 * PortEndpoint
 *
 * Identificativo univoco di una porta fisica nel grafo.
 * Una coppia (chassisId, portId) identifica univocamente
 * un capo di un link fisico.
 * ============================================================ */

typedef struct {
    char chassisId[MAX_STR_LEN];
    char sysName[MAX_STR_LEN];    /* ChassisId del nodo */
    char portId[MAX_STR_LEN];       /* PortId della porta */
    char portDescr[MAX_STR_LEN];    /* PortDescr (human-readable) */
    int  nodeIndex;                  /* indice nel TopologyGraph.nodes[] */
} PortEndpoint;

/* ============================================================
 * TopologyLink
 *
 * Un collegamento fisico tra due porte.
 * Scoperto tramite correlazione dei dati LLDP:
 *   A.RemoteSystemsData vede B → link A.portX ↔ B.portY
 *   B.RemoteSystemsData vede A → conferma bidirezionale
 * ============================================================ */

typedef struct {
    PortEndpoint endpointA;
    PortEndpoint endpointB;
    bool         confirmedBidirectional; /* true se visto da entrambi i lati */
} TopologyLink;

/* ============================================================
 * PubSubConnection
 *
 * Connessione logica PubSub tra due ConnectionEndpoint.
 * Popolata dal browse (Phase 3) o da establishConnection.
 * ============================================================ */

typedef struct {
    ConnectionEndpoint pub;              /* CE lato Publisher */
    ConnectionEndpoint sub;              /* CE lato Subscriber */

    /* Parametri PubSub condivisi */
    uint16_t  publisherId;
    uint16_t  writerGroupId;
    uint16_t  dataSetWriterId;
    char      multicastUrl[MAX_STR_LEN];
    double    publishingInterval;            /* ms */
} PubSubConnection;
/* ============================================================
 * DiscoveryQueueEntry
 *
 * Elemento della coda BFS per la discovery iterativa.
 * Contiene le informazioni minime per raggiungere e
 * identificare un dispositivo non ancora visitato.
 * ============================================================ */

typedef struct {
    char chassisId[MAX_STR_LEN];    /* chiave univoca per deduplicazione */
    char mgmtAddress[MAX_STR_LEN]; /* come raggiungerlo */
    char sysName[MAX_STR_LEN];      /* nome (informativo) */
    char capabilities[MAX_STR_LEN]; /* "Bridge", "Station Only", etc. */
} DiscoveryQueueEntry;

/* ============================================================
 * TopologyGraph
 *
 * Il grafo topologico completo. E' la singola fonte di verita'
 * per tutto il sistema: il browse popola i nodi, la correlazione
 * LLDP popola i link, e la serializzazione JSON esporta il tutto.
 *
 * Predisposto per le fasi future:
 *   - connections[] per le connessioni PubSub (fase 3)
 *   - tsnSchedule   per lo schedule TSN      (fase 4)
 * ============================================================ */

typedef struct {
    /* Layer fisico */
    TopologyNode  nodes[MAX_TOPOLOGY_NODES];
    size_t        nodesCount;

    TopologyLink  links[MAX_TOPOLOGY_LINKS];
    size_t        linksCount;

    PubSubConnection connections[MAX_LOGICAL_CONNECTIONS]; 
    size_t           connectionsCount;             
    /* Coda BFS per discovery iterativa */
    DiscoveryQueueEntry queue[MAX_DISCOVERY_QUEUE];
    size_t              queueHead;  /* indice del prossimo da processare */
    size_t              queueTail;  /* indice della prossima posizione libera */

    /* Timestamp dell'ultimo scan */
    time_t        lastScanTime;

    /* === Placeholder per fasi future === */
    /* PubSubConnection connections[MAX_CONNECTIONS]; */
    /* size_t           connectionsCount;             */
    /* TsnSchedule      tsnSchedule;                  */
} TopologyGraph;

/* ============================================================
 * Funzioni di gestione del modello
 * ============================================================ */

/* Inizializza un grafo vuoto (azzera tutto) */
void topologyGraphInit(TopologyGraph *graph);

/* Libera risorse del grafo (per ora no-op con array statici,
 * ma predisposto per allocazione dinamica futura) */
void topologyGraphClear(TopologyGraph *graph);

/* ─── Gestione nodi ──────────────────────────────────────── */

/* Cerca un nodo per chassisId. Restituisce l'indice in nodes[]
 * oppure -1 se non trovato. */
int topologyFindNodeByChassisId(const TopologyGraph *graph,
                                 const char *chassisId);

/* Cerca un nodo per endpoint URL OPC UA.
 * Restituisce l'indice oppure -1. */
int topologyFindNodeByEndpoint(const TopologyGraph *graph,
                                const char *endpointUrl);

/* Aggiunge un nodo al grafo. Restituisce l'indice del nodo
 * aggiunto oppure -1 se il grafo e' pieno.
 * NON controlla duplicati — il chiamante deve verificare
 * prima con topologyFindNodeByChassisId(). */
int topologyAddNode(TopologyGraph *graph, const TopologyNode *node);

/* ─── Gestione link ──────────────────────────────────────── */

/* Cerca un link tra due porte (in qualsiasi direzione).
 * Restituisce l'indice oppure -1. */
int topologyFindLink(const TopologyGraph *graph,
                      const char *chassisIdA, const char *portIdA,
                      const char *chassisIdB, const char *portIdB);

/* Aggiunge un link al grafo. Se il link esiste gia' nell'altra
 * direzione, lo marca come confirmedBidirectional.
 * Restituisce l'indice del link. */
int topologyAddLink(TopologyGraph *graph, const TopologyLink *link);

/*Aggiunge una connessione logica al grafo. Restituisce l'indice della connessione
 * oppure -1 se il grafo e' pieno. */

int topologyAddLogicalConnection(TopologyGraph *graph,
                                  const PubSubConnection *conn);

/* ─── Coda BFS ───────────────────────────────────────────── */

/* Accoda un dispositivo da visitare.
 * Ritorna true se accodato, false se gia' visitato o coda piena. */
bool discoveryEnqueue(TopologyGraph *graph,
                       const DiscoveryQueueEntry *entry);

/* Estrae il prossimo dispositivo dalla coda.
 * Ritorna true se estratto, false se coda vuota. */
bool discoveryDequeue(TopologyGraph *graph,
                       DiscoveryQueueEntry *out);

/* Controlla se un chassisId e' gia' stato visitato
 * (presente come nodo con visited == true). */
bool discoveryIsVisited(const TopologyGraph *graph,
                         const char *chassisId);

/* ─── Utility ────────────────────────────────────────────── */

/* Stampa il grafo su stdout (debug) */
void topologyGraphPrint(const TopologyGraph *graph);

#endif /* UAFX_MODEL_H */
