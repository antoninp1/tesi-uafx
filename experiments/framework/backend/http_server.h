/* ============================================================
 * http_server.h
 *
 * Web server HTTP per esporre il TopologyGraph via REST API.
 * Basato su Mongoose (github.com/cesanta/mongoose).
 *
 * Modello: on-demand, single-thread cooperativo.
 * Il main loop chiama httpServerPoll() periodicamente insieme
 * ad eventuali altre attivita'. Quando arriva una richiesta
 * HTTP, l'handler esegue in modo sincrono (anche la discovery,
 * che e' bloccante ma accettabile per il testbed).
 *
 * API esposta:
 *   GET  /api/health              → health check
 *   GET  /api/topology            → grafo completo (physical+logical)
 *   POST /api/discovery/run       → triggera discovery e restituisce grafo
 *   GET  /api/devices/{chassisId} → dettaglio di un nodo
 *
 *   GET  /api/devices/{id}/functional-entities  → 501 (fase 2)
 *   POST /api/connections                        → 501 (fase 2)
 *   GET  /api/connections                        → 501 (fase 2)
 *   DEL  /api/connections/{id}                   → 501 (fase 2)
 *   POST /api/tsn/compute-schedule               → 501 (fase 3)
 *   POST /api/tsn/deploy                         → 501 (fase 3)
 *   GET  /api/tsn/status                         → 501 (fase 3)
 *
 * CORS: Access-Control-Allow-Origin: * su tutte le risposte.
 * ============================================================ */

#ifndef UAFX_HTTP_SERVER_H
#define UAFX_HTTP_SERVER_H

#include "model.h"
#include <stdbool.h>

/* Inizializza il server HTTP sulla porta specificata.
 * Il parametro `graph` e' un puntatore al TopologyGraph globale
 * che verra' condiviso tra il server e le funzioni di discovery.
 * Ritorna true se l'inizializzazione e' andata a buon fine. */
bool httpServerInit(int port, TopologyGraph *graph);

/* Poll del server HTTP: processa tutti gli eventi pendenti
 * e ritorna entro `timeoutMs` millisecondi. Va chiamato
 * in un loop dal main. */
void httpServerPoll(int timeoutMs);

/* Shutdown pulito del server. */
void httpServerShutdown(void);

#endif /* UAFX_HTTP_SERVER_H */
