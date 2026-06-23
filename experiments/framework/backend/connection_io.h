/* ============================================================
 * connection_io.h
 *
 * Deserializzazione della ConnectionRequest da JSON e
 * serializzazione della ConnectionResponse in JSON.
 * ============================================================ */

#ifndef CONNECTION_IO_H
#define CONNECTION_IO_H

#include <stdbool.h>
#include "model.h"
#include "connection_manager.h"
/* ============================================================
 * API
 * ============================================================ */

/* Deserializza il body JSON in una ConnectionRequest.
 * Ritorna true se tutti i campi obbligatori sono presenti. */
bool connectionRequestFromJson(const char *bodyStr, ConnectionRequest *out);

/* Serializza una ConnectionResponse in JSON.
 * Il chiamante deve liberare la stringa con free(). */
char *connectionResponseToJson(const ConnectionResponse *resp);

#endif /* CONNECTION_IO_H */