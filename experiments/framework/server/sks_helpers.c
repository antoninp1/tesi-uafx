#define _GNU_SOURCE

#include <stdio.h>
#include "sks_helpers.h"

UA_ByteString
loadFile(const char *const path) {
    UA_ByteString fileContents = UA_STRING_NULL;
    FILE *fp = fopen(path, "rb");
    if(!fp) {
        printf("[SERVER] ERROR: cannot open %s\n", path);
        return fileContents;
    }
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    if(length < 0) { fclose(fp); return fileContents; }
    fileContents.length = (size_t)length;
    fileContents.data = (UA_Byte *)UA_malloc(fileContents.length);
    if(fileContents.data) {
        fseek(fp, 0, SEEK_SET);
        size_t read = fread(fileContents.data, 1, fileContents.length, fp);
        if(read != fileContents.length)
            UA_ByteString_clear(&fileContents);
    } else {
        fileContents.length = 0;
    }
    fclose(fp);
    return fileContents;
}

UA_ClientConfig *
encryptedSksClient(const char *applicationUri, UA_ByteString certificate, UA_ByteString privateKey) {
    UA_ClientConfig *cc = (UA_ClientConfig *)UA_calloc(1, sizeof(UA_ClientConfig));
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    UA_ClientConfig_setDefaultEncryption(cc, certificate, privateKey, NULL, 0, NULL, 0);
    cc->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
    UA_String_clear(&cc->clientDescription.applicationUri);
    cc->clientDescription.applicationUri = UA_String_fromChars(applicationUri);

    UA_X509IdentityToken *x509Token = UA_X509IdentityToken_new();
    UA_ByteString_copy(&certificate, &x509Token->certificateData);
    UA_ExtensionObject_clear(&cc->userIdentityToken);
    cc->userIdentityToken.encoding = UA_EXTENSIONOBJECT_DECODED;
    cc->userIdentityToken.content.decoded.type = &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN];
    cc->userIdentityToken.content.decoded.data = x509Token;
    return cc;
}

UA_StatusCode
registerToLdsSecurely(UA_Server *server, const char *ldsUrl,
                         const char *clientCertPath, const char *clientKeyPath,
                         const char *applicationUri) {
    
    const char * ldsCertPath = "scripts/certs/lds_server.cert.der";
    UA_ByteString ldsCert = loadFile(ldsCertPath);
    
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);

    UA_ByteString clientCert = loadFile(clientCertPath);
    UA_ByteString clientKey  = loadFile(clientKeyPath);

    if (clientCert.length == 0 || clientKey.length == 0) {
        UA_ByteString_clear(&clientCert);
        UA_ByteString_clear(&clientKey);
        return UA_STATUSCODE_BADARGUMENTSMISSING;
    }

    // 1. Forcer le chiffrement strict exigé par notre LDS sécurisé
    cc.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    cc.securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
    UA_ClientConfig_setDefaultEncryption(&cc, clientCert, clientKey, &ldsCert, 1, NULL, 0);

    // 2. Aligner l'URI applicative du client interne avec le certificat X509
    UA_String_clear(&cc.clientDescription.applicationUri);
    cc.clientDescription.applicationUri = UA_String_fromChars(applicationUri);

    // 3. Injecter le jeton d'identité X509 (contourne l'accès Anonyme interdit)
    UA_X509IdentityToken *x509Token = UA_X509IdentityToken_new();
    UA_ByteString_copy(&clientCert, &x509Token->certificateData);
    UA_ExtensionObject_clear(&cc.userIdentityToken);
    cc.userIdentityToken.encoding = UA_EXTENSIONOBJECT_DECODED;
    cc.userIdentityToken.content.decoded.type = &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN];
    cc.userIdentityToken.content.decoded.data = x509Token;

    // 4. Appel du service d'enregistrement périodique d'open62541
    UA_String discoveryUrl = UA_STRING((char*)ldsUrl);
    UA_StatusCode retval = UA_Server_registerDiscovery(server, &cc, discoveryUrl, UA_STRING_NULL);

    // Nettoyage de la mémoire temporaire locale
    UA_ByteString_clear(&clientCert);
    UA_ByteString_clear(&clientKey);
    UA_String_clear(&cc.securityPolicyUri);
    // Note: cc.userIdentityToken et cc.clientDescription.applicationUri sont copiés ou libérés par la stack interne

    return retval;
}

/* ─── Only allow authenticated connections (no anonymous access) ──── */
void
disableAnonymous(UA_ServerConfig *config) {
    for(size_t i = 0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ep = &config->endpoints[i];
        for(size_t j = 0; j < ep->userIdentityTokensSize; j++) {
            UA_UserTokenPolicy *utp = &ep->userIdentityTokens[j];
            if(utp->tokenType != UA_USERTOKENTYPE_ANONYMOUS)
                continue;
            UA_UserTokenPolicy_clear(utp);
            if(j + 1 < ep->userIdentityTokensSize) {
                ep->userIdentityTokens[j] =
                    ep->userIdentityTokens[ep->userIdentityTokensSize - 1];
                j--;
            }
            ep->userIdentityTokensSize--;
        }
        if(ep->userIdentityTokensSize == 0) {
            UA_free(ep->userIdentityTokens);
            ep->userIdentityTokens = NULL;
        }
    }
}

char *
resolveSksUrlFromLds(const char *ldsUrl, const char *sksApplicationUri,
                     const char *clientCertPath, const char *clientKeyPath) {
    UA_ByteString cert = loadFile(clientCertPath);
    UA_ByteString key  = loadFile(clientKeyPath);
    if(cert.length == 0 || key.length == 0) {
        printf("[SKS-RESOLVE] Impossible de charger cert/clé pour la résolution\n");
        UA_ByteString_clear(&cert);
        UA_ByteString_clear(&key);
        return NULL;
    }

    UA_Client *client = UA_Client_new();
    if(!client) {
        UA_ByteString_clear(&cert);
        UA_ByteString_clear(&key);
        return NULL;
    }
    UA_ClientConfig *cc = UA_Client_getConfig(client);
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    UA_ClientConfig_setDefaultEncryption(cc, cert, key, NULL, 0, NULL, 0);

    UA_ByteString_clear(&cert);
    UA_ByteString_clear(&key);

    size_t n = 0;
    UA_ApplicationDescription *servers = NULL;
    UA_StatusCode sc = UA_Client_findServers(client, ldsUrl, 0, NULL,
                                             0, NULL, &n, &servers);
    if(sc != UA_STATUSCODE_GOOD) {
        printf("[SKS-RESOLVE] FindServers on LDS failed : %s\n",
               UA_StatusCode_name(sc));
        UA_Client_delete(client);
        return NULL;
    }

    UA_String target = UA_STRING((char *)sksApplicationUri);
    char *result = NULL;
    for(size_t i = 0; i < n; i++) {
        if(!UA_String_equal(&servers[i].applicationUri, &target))
            continue;
        if(servers[i].discoveryUrlsSize == 0)
            continue;
        UA_String *durl = &servers[i].discoveryUrls[0];
        result = (char *)UA_malloc(durl->length + 1);
        if(result) {
            memcpy(result, durl->data, durl->length);
            result[durl->length] = '\0';
        }
        break;
    }

    if(!result)
        printf("[SKS-RESOLVE] No server with ApplicationUri '%s' "
              "Found from LDS\n", sksApplicationUri);

    UA_Array_delete(servers, n, &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);
    UA_Client_delete(client);
    return result;
}