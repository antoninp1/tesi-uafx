/* ============================================================
 * tde.h
 *
 * Topology Discovery Entity (TDE)
 *
 * Astrazione "black box" per interrogare dispositivi di rete
 * che non espongono OPC UA (switch, router, ecc.).
 *
 * Concettualmente: il client passa un DiscoveryQueueEntry
 * (chassisId/mgmtAddress/sysName scoperto via LLDP da un altro
 * dispositivo) e la TDE si occupa di contattarlo nel modo piu'
 * appropriato (SSH/CLI, NETCONF, SNMP, ecc.) e restituire i
 * suoi dati LLDP locali + vicini in formato standardizzato.
 *
 * Il dispatch e' fatto in tdeQueryDevice() in base al nome
 * o all'indirizzo del dispositivo.
 *
 * NOTA: per i dispositivi Relyum richiede 'sshpass' installato
 * sul sistema host (apt install sshpass).
 * ============================================================ */

#ifndef UAFX_TDE_H
#define UAFX_TDE_H

#include "common.h"
#include "model.h"
#include <stdbool.h>

/* ============================================================
 * TdeNeighborInfo
 *
 * Mirror semplificato di LldpNeighbor per i risultati TDE.
 * Mantenuto separato dal model.h per disaccoppiare la TDE
 * dal modello applicativo del client.
 * ============================================================ */

typedef struct {
    char chassisId[MAX_STR_LEN];
    uint32_t chassisIdSubtype;
    char sysName[MAX_STR_LEN];
    char sysDescr[MAX_STR_LEN];
    char mgmtAddress[MAX_STR_LEN];
    char portId[MAX_STR_LEN];
    uint32_t portIdSubtype;
    char portDescr[MAX_STR_LEN];
    char systemCapabilities[MAX_STR_LEN];
    uint32_t timeToLive;
    char localPort[MAX_STR_LEN]; /* porta locale su cui e' visto il vicino */
} TdeNeighborInfo;

/* Una porta locale del dispositivo (da lldpcli show interfaces) */
typedef struct {
    char     name[MAX_STR_LEN];     /* "PORT_3", "enp43s0", ecc. */
    char     portId[MAX_STR_LEN];   /* MAC o ifname */
    uint32_t portIdSubtype;         /* 3=MAC, 5=ifName, 7=local */
    char     portDescr[MAX_STR_LEN];
    uint32_t ttl;
} TdeLocalPort;

/* ============================================================
 * TdeQueryResult
 *
 * Risultato completo di una query TDE: dati locali del
 * dispositivo + array di vicini scoperti via LLDP.
 * ============================================================ */

typedef struct {
    bool success;
    char errorMessage[MAX_STR_LEN];

    /* Dati locali del dispositivo interrogato */
    char     localChassisId[MAX_STR_LEN];
    uint32_t localChassisIdSubtype;
    char     localSysName[MAX_STR_LEN];
    char     localSysDescr[MAX_STR_LEN];
    char     localMgmtAddress[MAX_STR_LEN];
    char     localSystemCapabilities[MAX_STR_LEN];
    TdeLocalPort localPorts[MAX_LOCAL_PORTS]; 
    size_t       localPortsCount;

    /* Vicini LLDP visti da questo dispositivo */
    TdeNeighborInfo neighbors[MAX_LOCAL_PORTS];
    size_t          neighborsCount;
} TdeQueryResult;

/* ============================================================
 * API
 * ============================================================ */

/* Funzione principale di dispatch.
 * Prende un elemento della coda BFS (con sysName, mgmtAddress,
 * capabilities) e in base a essi sceglie il metodo di
 * comunicazione, esegue la query, popola il result.
 *
 * Ritorna true se la query e' andata a buon fine, false altrimenti
 * (in tal caso result->errorMessage contiene la causa). */
bool tdeQueryDevice(const DiscoveryQueueEntry *entry,
                    TdeQueryResult *result);

/* Stampa il risultato in formato JSON su stdout (debug). */
void tdeResultPrintJson(const TdeQueryResult *result);

/* Applica il risultato della query a un TopologyNode.
 * Popola id, name, mgmtAddress, e crea un'interfaccia "virtuale"
 * con i vicini estratti dal result. */
void tdeApplyResultToNode(const TdeQueryResult *result, TopologyNode *node);

#endif /* UAFX_TDE_H */
