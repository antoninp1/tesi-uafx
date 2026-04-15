/* ============================================================
 * discovery.c
 *
 * Discovery via LDS (TCP): chiama FindServers() sull'LDS,
 * filtra i DiscoveryServer e raccoglie gli endpoint
 * dei server applicativi.
 * ============================================================ */

#include "discovery.h"

UA_StatusCode
runLdsDiscovery(DiscoveryList *list, const char *ldsUrl) {

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    printf("  Connessione all'LDS: %s\n", ldsUrl);

    size_t serverArraySize = 0;
    UA_ApplicationDescription *serverArray = NULL;

    UA_StatusCode retval = UA_Client_findServers(
        client, ldsUrl,
        0, NULL,    /* serverUriFilter */
        0, NULL,    /* localeIdFilter  */
        &serverArraySize, &serverArray);

    if(retval != UA_STATUSCODE_GOOD) {
        printf("  FindServers fallito: %s\n", UA_StatusCode_name(retval));
        UA_Client_delete(client);
        return retval;
    }

    printf("  FindServers ha restituito %zu risultati\n\n", serverArraySize);

    for(size_t i = 0; i < serverArraySize && list->count < MAX_DISCOVERED_SERVERS; i++) {
        UA_ApplicationDescription *app = &serverArray[i];

        /* Salta l'LDS stesso */
        if(app->applicationType == UA_APPLICATIONTYPE_DISCOVERYSERVER) {
            printf("  [skip] %.*s (DiscoveryServer)\n",
                   (int)app->applicationName.text.length,
                   app->applicationName.text.data);
            continue;
        }

        /* Prendi il primo discoveryUrl disponibile */
        if(app->discoveryUrlsSize == 0)
            continue;

        UA_String *url = &app->discoveryUrls[0];
        if(url->length == 0 || url->length >= MAX_STR_LEN)
            continue;

        /* Salva URL */
        memcpy(list->urls[list->count], url->data, url->length);
        list->urls[list->count][url->length] = '\0';

        /* Salva nome */
        size_t nameLen = app->applicationName.text.length;
        if(nameLen >= MAX_STR_LEN) nameLen = MAX_STR_LEN - 1;
        memcpy(list->names[list->count], app->applicationName.text.data, nameLen);
        list->names[list->count][nameLen] = '\0';

        printf("  [+] Scoperto: %-30s  -->  %s\n",
               list->names[list->count],
               list->urls[list->count]);

        list->count++;
    }

    UA_Array_delete(serverArray, serverArraySize,
                    &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);
    UA_Client_delete(client);
    return UA_STATUSCODE_GOOD;
}
