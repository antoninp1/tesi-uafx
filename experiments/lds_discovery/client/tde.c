/* ============================================================
 * tde.c
 *
 * Implementazione della Topology Discovery Entity.
 *
 * Dispatch:
 *   - sysName contiene "RELY" → queryRelyumTSN() (SSH + lldpcli -f json)
 *   - altri vendor: aggiungere qui in futuro
 *
 * Parser: utilizza cJSON per parsare l'output JSON di lldpcli.
 *
 * Prerequisiti:
 *   - sshpass installato (apt install sshpass)
 *   - cJSON.c / cJSON.h nella cartella del progetto
 *     (scaricabili da github.com/DaveGamble/cJSON)
 * ============================================================ */

#include "tde.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ============================================================
 * Configurazione Relyum
 * ============================================================ */

/* Due comandi separati da un marker, entrambi in JSON */
#define RELYUM_LLDP_CMD \
    "echo sys-admin | sudo -S lldpcli show chassis -f json 2>/dev/null; " \
    "echo '---SEPARATOR---'; " \
    "echo sys-admin | sudo -S lldpcli show neighbors -f json 2>/dev/null"

#define SSH_OUTPUT_BUFFER_SIZE  32768

/* ============================================================
 * Helper
 * ============================================================ */

static void safeCopy(char *dst, const char *src, size_t dstSize) {
    if(!dst || dstSize == 0) return;
    if(!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

/* Converte port.id.type stringa → subtype numerico */
static uint32_t portIdTypeToSubtype(const char *type) {
    if(!type) return 0;
    if(strcmp(type, "mac") == 0)    return 3;
    if(strcmp(type, "ifname") == 0) return 5;
    if(strcmp(type, "local") == 0)  return 7;
    return 0;
}

/* Converte chassis.id.type stringa → subtype numerico */
static uint32_t chassisIdTypeToSubtype(const char *type) {
    if(!type) return 0;
    if(strcmp(type, "mac") == 0) return 4;
    return 0;
}

/* Legge il primo IPv4 da un campo "mgmt-ip" che puo' essere
 * stringa singola o array di stringhe. Salta IPv6. */
static void extractMgmtIp(cJSON *mgmtIp, char *dst, size_t dstSize) {
    if(!mgmtIp || !dst) return;

    if(cJSON_IsString(mgmtIp)) {
        /* Stringa singola — usa direttamente se e' IPv4 */
        const char *val = mgmtIp->valuestring;
        if(val && strstr(val, ":") == NULL)  /* no ":" → IPv4 */
            safeCopy(dst, val, dstSize);
        else if(val)
            safeCopy(dst, val, dstSize);  /* fallback: prendi anche IPv6 */
        return;
    }

    if(cJSON_IsArray(mgmtIp)) {
        /* Array — cerca il primo IPv4 */
        cJSON *item;
        cJSON_ArrayForEach(item, mgmtIp) {
            if(cJSON_IsString(item) && item->valuestring) {
                if(strstr(item->valuestring, ":") == NULL) {
                    /* IPv4 trovato */
                    safeCopy(dst, item->valuestring, dstSize);
                    return;
                }
            }
        }
        /* Nessun IPv4 — prendi il primo qualsiasi */
        cJSON *first = cJSON_GetArrayItem(mgmtIp, 0);
        if(first && cJSON_IsString(first))
            safeCopy(dst, first->valuestring, dstSize);
    }
}

/* Estrae le capabilities da un campo che puo' essere
 * un singolo oggetto { "type": "Bridge", "enabled": true }
 * o un array di tali oggetti. Concatena i tipi con "," */
static void extractCapabilities(cJSON *capNode, char *dst, size_t dstSize) {
    if(!capNode || !dst) return;
    dst[0] = '\0';

    if(cJSON_IsObject(capNode)) {
        /* Singolo oggetto */
        cJSON *type = cJSON_GetObjectItem(capNode, "type");
        cJSON *enabled = cJSON_GetObjectItem(capNode, "enabled");
        if(type && cJSON_IsString(type)) {
            if(!enabled || cJSON_IsTrue(enabled))
                safeCopy(dst, type->valuestring, dstSize);
        }
        return;
    }

    if(cJSON_IsArray(capNode)) {
        cJSON *item;
        cJSON_ArrayForEach(item, capNode) {
            cJSON *type = cJSON_GetObjectItem(item, "type");
            cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
            if(!type || !cJSON_IsString(type)) continue;
            if(enabled && cJSON_IsFalse(enabled)) continue; /* solo enabled */

            if(dst[0] != '\0')
                strncat(dst, ",", dstSize - strlen(dst) - 1);
            strncat(dst, type->valuestring, dstSize - strlen(dst) - 1);
        }
    }
}

/* ============================================================
 * runSshCommand
 * ============================================================ */

static bool runSshCommand(const char *user, const char *password,
                          const char *host, const char *remoteCmd,
                          char *output, size_t outputSize,
                          char *errorMsg, size_t errorMsgSize) {

    char fullCmd[2048];
    snprintf(fullCmd, sizeof(fullCmd),
             "sshpass -p '%s' ssh "
             "-o StrictHostKeyChecking=no "
             "-o UserKnownHostsFile=/dev/null "
             "-o ConnectTimeout=10 "
             "-o LogLevel=ERROR "
             "%s@%s '%s' 2>&1",
             password, user, host, remoteCmd);

    FILE *fp = popen(fullCmd, "r");
    if(!fp) {
        snprintf(errorMsg, errorMsgSize, "popen failed: %s", strerror(errno));
        return false;
    }

    size_t total = 0;
    output[0] = '\0';
    while(total < outputSize - 1) {
        size_t n = fread(output + total, 1, outputSize - 1 - total, fp);
        if(n == 0) break;
        total += n;
    }
    output[total] = '\0';

    int rc = pclose(fp);
    if(rc != 0) {
        snprintf(errorMsg, errorMsgSize,
                 "ssh exit code %d: %.200s", rc, output);
        return false;
    }

    return true;
}

/* ============================================================
 * parseLocalChassis
 *
 * Parsa il JSON di "lldpcli show chassis -f json".
 *
 * Struttura:
 * { "local-chassis": { "chassis": { "<sysName>": {
 *     "id": { "type": "mac", "value": "..." },
 *     "descr": "...",
 *     "mgmt-ip": [...] o "...",
 *     "capability": {...} o [...]
 * }}}}
 * ============================================================ */

static void parseLocalChassis(const char *jsonStr, TdeQueryResult *result) {
    cJSON *root = cJSON_Parse(jsonStr);
    if(!root) {
        printf("[TDE] JSON parse error (local chassis)\n");
        return;
    }

    cJSON *localChassis = cJSON_GetObjectItem(root, "local-chassis");
    if(!localChassis) { cJSON_Delete(root); return; }

    cJSON *chassis = cJSON_GetObjectItem(localChassis, "chassis");
    if(!chassis) { cJSON_Delete(root); return; }

    /* Il sysName e' la chiave del primo (unico) figlio di "chassis" */
    cJSON *sysNode = chassis->child;
    if(!sysNode) { cJSON_Delete(root); return; }

    safeCopy(result->localSysName, sysNode->string, MAX_STR_LEN);

    /* id.type + id.value */
    cJSON *id = cJSON_GetObjectItem(sysNode, "id");
    if(id) {
        cJSON *idType = cJSON_GetObjectItem(id, "type");
        cJSON *idValue = cJSON_GetObjectItem(id, "value");
        if(idValue && cJSON_IsString(idValue))
            safeCopy(result->localChassisId, idValue->valuestring, MAX_STR_LEN);
        if(idType && cJSON_IsString(idType))
            result->localChassisIdSubtype = chassisIdTypeToSubtype(idType->valuestring);
    }

    /* descr */
    cJSON *descr = cJSON_GetObjectItem(sysNode, "descr");
    if(descr && cJSON_IsString(descr))
        safeCopy(result->localSysDescr, descr->valuestring, MAX_STR_LEN);

    /* mgmt-ip */
    cJSON *mgmtIp = cJSON_GetObjectItem(sysNode, "mgmt-ip");
    extractMgmtIp(mgmtIp, result->localMgmtAddress, MAX_STR_LEN);

    /* capabilities */
    cJSON *cap = cJSON_GetObjectItem(sysNode, "capability");
    extractCapabilities(cap, result->localSystemCapabilities, MAX_STR_LEN);

    cJSON_Delete(root);
}

/* ============================================================
 * parseNeighbors
 *
 * Parsa il JSON di "lldpcli show neighbors -f json".
 *
 * Struttura:
 * { "lldp": { "interface": [
 *   { "<PORT_N>": {
 *       "chassis": { "<sysName>": {
 *           "id": {...}, "descr": "...", "mgmt-ip": ...,
 *           "capability": ...
 *       }},
 *       "port": {
 *           "id": { "type": "mac"|"ifname"|"local", "value": "..." },
 *           "descr": "...",
 *           "ttl": "120"
 *       }
 *   }},
 *   ...
 * ]}}
 * ============================================================ */

static void parseNeighbors(const char *jsonStr, TdeQueryResult *result) {
    cJSON *root = cJSON_Parse(jsonStr);
    if(!root) {
        printf("[TDE] JSON parse error (neighbors)\n");
        return;
    }

    cJSON *lldp = cJSON_GetObjectItem(root, "lldp");
    if(!lldp) { cJSON_Delete(root); return; }

    cJSON *interfaces = cJSON_GetObjectItem(lldp, "interface");
    if(!interfaces || !cJSON_IsArray(interfaces)) { cJSON_Delete(root); return; }

    cJSON *ifaceEntry;
    cJSON_ArrayForEach(ifaceEntry, interfaces) {
        if(result->neighborsCount >= MAX_LLDP_NEIGHBORS) break;

        /* Ogni elemento dell'array e' un oggetto con una sola chiave:
         * il nome della porta locale (es. "PORT_3") */
        cJSON *portObj = ifaceEntry->child;
        if(!portObj) continue;

        const char *localPortName = portObj->string;  /* "PORT_3" */

        TdeNeighborInfo *nbr = &result->neighbors[result->neighborsCount];
        memset(nbr, 0, sizeof(*nbr));
        safeCopy(nbr->localPort, localPortName, MAX_STR_LEN);

        /* ─── chassis.<sysName> ──────────────────────── */
        cJSON *chassis = cJSON_GetObjectItem(portObj, "chassis");
        if(chassis) {
            cJSON *sysNode = chassis->child;
            if(sysNode) {
                safeCopy(nbr->sysName, sysNode->string, MAX_STR_LEN);

                cJSON *id = cJSON_GetObjectItem(sysNode, "id");
                if(id) {
                    cJSON *idType = cJSON_GetObjectItem(id, "type");
                    cJSON *idValue = cJSON_GetObjectItem(id, "value");
                    if(idValue && cJSON_IsString(idValue))
                        safeCopy(nbr->chassisId, idValue->valuestring, MAX_STR_LEN);
                    if(idType && cJSON_IsString(idType))
                        nbr->chassisIdSubtype = chassisIdTypeToSubtype(idType->valuestring);
                }

                cJSON *descr = cJSON_GetObjectItem(sysNode, "descr");
                if(descr && cJSON_IsString(descr))
                    safeCopy(nbr->sysDescr, descr->valuestring, MAX_STR_LEN);

                cJSON *mgmtIp = cJSON_GetObjectItem(sysNode, "mgmt-ip");
                extractMgmtIp(mgmtIp, nbr->mgmtAddress, MAX_STR_LEN);

                cJSON *cap = cJSON_GetObjectItem(sysNode, "capability");
                extractCapabilities(cap, nbr->systemCapabilities, MAX_STR_LEN);
            }
        }

        /* ─── port ───────────────────────────────────── */
        cJSON *port = cJSON_GetObjectItem(portObj, "port");
        if(port) {
            cJSON *pid = cJSON_GetObjectItem(port, "id");
            if(pid) {
                cJSON *pidType = cJSON_GetObjectItem(pid, "type");
                cJSON *pidValue = cJSON_GetObjectItem(pid, "value");
                if(pidValue && cJSON_IsString(pidValue))
                    safeCopy(nbr->portId, pidValue->valuestring, MAX_STR_LEN);
                if(pidType && cJSON_IsString(pidType))
                    nbr->portIdSubtype = portIdTypeToSubtype(pidType->valuestring);
            }

            cJSON *pdescr = cJSON_GetObjectItem(port, "descr");
            if(pdescr && cJSON_IsString(pdescr))
                safeCopy(nbr->portDescr, pdescr->valuestring, MAX_STR_LEN);

            cJSON *ttl = cJSON_GetObjectItem(port, "ttl");
            if(ttl) {
                if(cJSON_IsString(ttl))
                    nbr->timeToLive = (uint32_t)atoi(ttl->valuestring);
                else if(cJSON_IsNumber(ttl))
                    nbr->timeToLive = (uint32_t)ttl->valueint;
            }
        }

        result->neighborsCount++;
    }

    cJSON_Delete(root);
}

/* ============================================================
 * queryRelyumTSN
 *
 * Adapter per switch Relyum RELY-10TSN12.
 * Lancia due comandi lldpcli in formato JSON separati da
 * "---SEPARATOR---", poi parsa ciascuna parte.
 * ============================================================ */

static bool queryRelyumTSN(const DiscoveryQueueEntry *entry,
                           TdeQueryResult *result) {
    (void)entry;

    /* Lettura configurazione dall'ambiente, con fallback per sviluppo */
    const char *host     = getenv("RELYUM_HOST");
    const char *user     = getenv("RELYUM_USER");
    const char *password = getenv("RELYUM_PASSWORD");

    if(!host)     host     = "rely10tsn12-370524014.mmwunibo.it";
    if(!user)     user     = "sys-admin";
    if(!password) {
        result->success = false;
        safeCopy(result->errorMessage,
                 "RELYUM_PASSWORD env variable not set", MAX_STR_LEN);
        printf("[TDE] ERROR: RELYUM_PASSWORD not set. "
               "Use 'source .env && ./discovery_client'\n");
        return false;
    }

    printf("[TDE] Querying Relyum TSN switch (%s@%s)...\n", user, host);

    char output[SSH_OUTPUT_BUFFER_SIZE];
    char errMsg[MAX_STR_LEN];

    bool ok = runSshCommand(user, password, host, RELYUM_LLDP_CMD,
                            output, sizeof(output),
                            errMsg, sizeof(errMsg));

    if(!ok) {
        result->success = false;
        safeCopy(result->errorMessage, errMsg, MAX_STR_LEN);
        printf("[TDE] SSH error: %s\n", errMsg);
        return false;
    }

    char *separator = strstr(output, "---SEPARATOR---");
    if(!separator) {
        result->success = false;
        safeCopy(result->errorMessage, "Separator not found in SSH output", MAX_STR_LEN);
        return false;
    }

    *separator = '\0';
    char *part1 = output;
    char *part2 = separator + strlen("---SEPARATOR---");
    while(*part2 == '\n' || *part2 == '\r' || *part2 == ' ') part2++;

    parseLocalChassis(part1, result);
    parseNeighbors(part2, result);

    result->success = true;
    printf("[TDE] OK: local=%s (chassisId=%s), neighbors=%zu\n",
           result->localSysName, result->localChassisId, result->neighborsCount);
    return true;
}
/* ============================================================
 * tdeQueryDevice (dispatch)
 * ============================================================ */

bool tdeQueryDevice(const DiscoveryQueueEntry *entry,
                    TdeQueryResult *result) {
    if(!entry || !result) return false;

    memset(result, 0, sizeof(TdeQueryResult));

    /* Caso 1: Relyum TSN */
    if(strstr(entry->sysName, "RELY") != NULL ||
       strstr(entry->sysName, "rely") != NULL ||
       strstr(entry->sysName, "RELY-10TSN") != NULL) {
        return queryRelyumTSN(entry, result);
    }

    /* Caso 2: aggiungere qui altri vendor */
    /* if(strstr(entry->sysName, "Aruba") != NULL)
     *     return queryArubaSwitch(entry, result);
     */

    /* Default: vendor non supportato */
    result->success = false;
    snprintf(result->errorMessage, MAX_STR_LEN,
             "No TDE adapter for device '%s' (%s)",
             entry->sysName, entry->mgmtAddress);
    printf("[TDE] %s\n", result->errorMessage);
    return false;
}

/* ============================================================
 * tdeResultPrintJson
 * ============================================================ */

void tdeResultPrintJson(const TdeQueryResult *result) {
    printf("{\n");
    printf("  \"success\": %s,\n", result->success ? "true" : "false");
    if(!result->success) {
        printf("  \"error\": \"%s\"\n", result->errorMessage);
        printf("}\n");
        return;
    }

    printf("  \"local\": {\n");
    printf("    \"chassisId\": \"%s\",\n", result->localChassisId);
    printf("    \"chassisIdSubtype\": %u,\n", result->localChassisIdSubtype);
    printf("    \"sysName\": \"%s\",\n", result->localSysName);
    printf("    \"sysDescr\": \"%s\",\n", result->localSysDescr);
    printf("    \"mgmtAddress\": \"%s\",\n", result->localMgmtAddress);
    printf("    \"systemCapabilities\": \"%s\"\n", result->localSystemCapabilities);
    printf("  },\n");

    printf("  \"neighbors\": [\n");
    for(size_t i = 0; i < result->neighborsCount; i++) {
        const TdeNeighborInfo *n = &result->neighbors[i];
        printf("    {\n");
        printf("      \"localPort\": \"%s\",\n", n->localPort);
        printf("      \"chassisId\": \"%s\",\n", n->chassisId);
        printf("      \"chassisIdSubtype\": %u,\n", n->chassisIdSubtype);
        printf("      \"sysName\": \"%s\",\n", n->sysName);
        printf("      \"mgmtAddress\": \"%s\",\n", n->mgmtAddress);
        printf("      \"portId\": \"%s\",\n", n->portId);
        printf("      \"portIdSubtype\": %u,\n", n->portIdSubtype);
        printf("      \"portDescr\": \"%s\",\n", n->portDescr);
        printf("      \"systemCapabilities\": \"%s\",\n", n->systemCapabilities);
        printf("      \"timeToLive\": %u\n", n->timeToLive);
        printf("    }%s\n", (i + 1 < result->neighborsCount) ? "," : "");
    }
    printf("  ]\n");
    printf("}\n");
}

/* ============================================================
 * tdeApplyResultToNode
 * ============================================================ */

void tdeApplyResultToNode(const TdeQueryResult *result, TopologyNode *node) {
    if(!result || !node || !result->success) return;

    /* Identificazione del nodo */
    safeCopy(node->id, result->localChassisId, MAX_STR_LEN);
    safeCopy(node->name, result->localSysName, MAX_STR_LEN);
    safeCopy(node->description, result->localSysDescr, MAX_STR_LEN);
    safeCopy(node->mgmtAddress, result->localMgmtAddress, MAX_STR_LEN);

    /* Tipo: Bridge → SWITCH */
    if(strstr(result->localSystemCapabilities, "Bridge") != NULL)
        node->type = NODE_TYPE_SWITCH;
    else
        node->type = NODE_TYPE_UNKNOWN;

    node->reachable = true;
    node->visited = true;

    /* Crea un'interfaccia aggregata con i vicini */
    if(node->interfacesCount < MAX_NETWORK_INTERFACES) {
        NetworkInterface *iface = &node->interfaces[node->interfacesCount];
        memset(iface, 0, sizeof(NetworkInterface));
        safeCopy(iface->name, "tde-aggregate", MAX_STR_LEN);

        /* Local data */
        safeCopy(iface->localData.chassisId, result->localChassisId, MAX_STR_LEN);
        iface->localData.chassisIdSubtype = result->localChassisIdSubtype;
        safeCopy(iface->localData.sysName, result->localSysName, MAX_STR_LEN);
        safeCopy(iface->localData.sysDescr, result->localSysDescr, MAX_STR_LEN);
        safeCopy(iface->localData.mgmtAddress, result->localMgmtAddress, MAX_STR_LEN);
        safeCopy(iface->localData.systemCapabilities,
                 result->localSystemCapabilities, MAX_STR_LEN);
        iface->hasLocalData = true;

        /* Vicini */
        size_t maxN = result->neighborsCount;
        if(maxN > MAX_LLDP_NEIGHBORS) maxN = MAX_LLDP_NEIGHBORS;
        for(size_t i = 0; i < maxN; i++) {
            const TdeNeighborInfo *src = &result->neighbors[i];
            LldpNeighbor *dst = &iface->neighbors[i];
            memset(dst, 0, sizeof(*dst));
            safeCopy(dst->chassisId, src->chassisId, MAX_STR_LEN);
            dst->chassisIdSubtype = src->chassisIdSubtype;
            safeCopy(dst->sysName, src->sysName, MAX_STR_LEN);
            safeCopy(dst->sysDescr, src->sysDescr, MAX_STR_LEN);
            safeCopy(dst->mgmtAddress, src->mgmtAddress, MAX_STR_LEN);
            safeCopy(dst->portId, src->portId, MAX_STR_LEN);
            dst->portIdSubtype = src->portIdSubtype;
            safeCopy(dst->portDescr, src->portDescr, MAX_STR_LEN);
            safeCopy(dst->systemCapabilities, src->systemCapabilities, MAX_STR_LEN);
            dst->timeToLive = src->timeToLive;
        }
        iface->neighborsCount = maxN;

        node->interfacesCount++;
    }
}
