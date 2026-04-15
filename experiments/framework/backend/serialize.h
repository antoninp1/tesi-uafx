/* ============================================================
 * serialize.h
 *
 * Serializzazione del TopologyGraph in JSON per l'API HTTP.
 *
 * Il JSON prodotto e' pensato per essere consumato dal
 * frontend React Flow, che supporta sia la vista fisica
 * (nodi = dispositivi, link = connessioni fisiche) sia la
 * vista logica (nodi = FunctionalEntities, link = connessioni
 * PubSub — per ora sempre vuote, saranno popolate in fase 2).
 *
 * Formato (alto livello):
 *
 * {
 *   "lastScan": "2026-04-13T14:30:00Z",
 *   "physical": {
 *     "nodes": [ ... TopologyNode ... ],
 *     "links": [ ... TopologyLink ... ]
 *   },
 *   "logical": {
 *     "nodes": [ ... FunctionalEntity (flattened) ... ],
 *     "links": [ ... PubSub connections (vuoto in fase 1) ... ]
 *   }
 * }
 *
 * Ogni funzione restituisce una stringa malloc-ata che il
 * chiamante deve liberare con free().
 * ============================================================ */

#ifndef UAFX_SERIALIZE_H
#define UAFX_SERIALIZE_H

#include "model.h"

/* Serializza l'intero grafo con entrambe le viste (fisica + logica).
 * Questo e' l'output di GET /api/topology. */
char *topologyGraphToJson(const TopologyGraph *graph);

/* Serializza il dettaglio completo di un singolo nodo.
 * Output di GET /api/devices/{chassisId}. */
char *deviceDetailToJson(const TopologyNode *node);

/* Serializza un oggetto di errore nel formato standard.
 * Output tipico per 404, 501, 400. */
char *errorToJson(const char *code, const char *message);

/* Serializza un semplice {"status":"ok"} per health check. */
char *healthToJson(void);

#endif /* UAFX_SERIALIZE_H */
