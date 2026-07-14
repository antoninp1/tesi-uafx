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
encryptedSksClient(clientCreds *creds) {
    UA_ClientConfig *cc = (UA_ClientConfig *)UA_calloc(1, sizeof(UA_ClientConfig));
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    UA_ClientConfig_setDefaultEncryption(cc, creds->clientCert, creds->clientKey, &creds->caCert, 1, &creds->crl, 1);
    cc->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
    UA_String_clear(&cc->clientDescription.applicationUri);
    cc->clientDescription.applicationUri = creds->applicationUri;

    UA_X509IdentityToken *x509Token = UA_X509IdentityToken_new();
    UA_ByteString_copy(&creds->clientCert, &x509Token->certificateData);
    UA_ExtensionObject_clear(&cc->userIdentityToken);
    cc->userIdentityToken.encoding = UA_EXTENSIONOBJECT_DECODED;
    cc->userIdentityToken.content.decoded.type = &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN];
    cc->userIdentityToken.content.decoded.data = x509Token;
    return cc;
}

UA_StatusCode
registerToLdsSecurely(UA_Server *server, clientCreds *creds) {
    
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);

    cc.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    cc.securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
    UA_ClientConfig_setDefaultEncryption(&cc, creds->clientCert, creds->clientKey, &creds->caCert, 1, &creds->crl, 1);

    UA_String_clear(&cc.clientDescription.applicationUri);
    cc.clientDescription.applicationUri = creds->applicationUri;

    UA_X509IdentityToken *x509Token = UA_X509IdentityToken_new();
    UA_ByteString_copy(&creds->clientCert, &x509Token->certificateData);
    UA_ExtensionObject_clear(&cc.userIdentityToken);
    cc.userIdentityToken.encoding = UA_EXTENSIONOBJECT_DECODED;
    cc.userIdentityToken.content.decoded.type = &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN];
    cc.userIdentityToken.content.decoded.data = x509Token;

    UA_String discoveryUrl = UA_String_fromChars(creds->ldsUrl);
    UA_StatusCode retval = UA_Server_registerDiscovery(server, &cc, discoveryUrl, UA_STRING_NULL);

    UA_String_clear(&cc.securityPolicyUri);

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
resolveSksUrlFromLds(clientCreds *creds) {
    const char * sksApplicationUri = "urn:example:uafx:sks-server";
    UA_Client *client = UA_Client_new();
    UA_ClientConfig *cc = UA_Client_getConfig(client);
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    UA_ClientConfig_setDefaultEncryption(cc, creds->clientCert, creds->clientKey, NULL, 0, &creds->crl, 1);

    size_t n = 0;
    UA_ApplicationDescription *servers = NULL;
    UA_StatusCode sc = UA_Client_findServers(client, creds->ldsUrl, 0, NULL,
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

char *
buildCertPath(const char *certDir, const char *filename) {
    if(!certDir || !filename)
        return NULL;

    size_t dirLen = strlen(certDir);
    UA_Boolean needsSlash = (dirLen > 0 && certDir[dirLen - 1] != '/');
    size_t totalLen = dirLen + (needsSlash ? 1 : 0) + strlen(filename) + 1;

    char *path = (char *)malloc(totalLen);
    if(!path)
        return NULL;

    int written = snprintf(path, totalLen, "%s%s%s",
                           certDir, needsSlash ? "/" : "", filename);
    if(written < 0 || (size_t)written >= totalLen) {
        free(path);
        return NULL;
    }
    return path;
}

UA_String
makeUsernamePolicyId(const UA_String *securityPolicyUri) {
        UA_Byte *hash = NULL;
    for(UA_Byte *b = securityPolicyUri->data + securityPolicyUri->length - 1;
        b >= securityPolicyUri->data; b--) {
        if(*b == '#') { hash = b; break; }
    }
    const char *prefix = "username-policy";
    size_t prefixLen = strlen(prefix);
    size_t postfixLen = hash ?
        (size_t)(securityPolicyUri->data + securityPolicyUri->length - hash) : 0;
    UA_String policyId;
    policyId.length = prefixLen + postfixLen;
    policyId.data = (UA_Byte *)UA_malloc(policyId.length);
    memcpy(policyId.data, prefix, prefixLen);
    if(hash)
        memcpy(policyId.data + prefixLen, hash, postfixLen);
    return policyId;   
}

void
addCertificateTokenPolicy(UA_ServerConfig *config) {
    for (size_t i=0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ep = &config->endpoints[i];

        UA_UserTokenPolicy *newArray = (UA_UserTokenPolicy *)
            UA_realloc(ep->userIdentityTokens,
                      (ep->userIdentityTokensSize + 1) * sizeof(UA_UserTokenPolicy));
        if (!newArray)
            continue;
        ep->userIdentityTokens = newArray;

        UA_UserTokenPolicy *utp = &ep->userIdentityTokens[ep->userIdentityTokensSize];
        UA_UserTokenPolicy_init(utp);
        utp->tokenType = UA_USERTOKENTYPE_CERTIFICATE;
        utp->policyId = makeUsernamePolicyId(&ep->securityPolicyUri);
        ep->userIdentityTokensSize++;
        UA_String_copy(&ep->securityPolicyUri, &utp->securityPolicyUri);
    }
}


UA_StatusCode
activateSession(UA_Server *server, UA_AccessControl *ac,
                     const UA_EndpointDescription *endpointDescription,
                     const UA_ByteString *secureChannelRemoteCertificate,
                     const UA_NodeId *sessionId,
                     const UA_ExtensionObject *userIdentityToken,
                     void **sessionContext) {
   if (userIdentityToken->content.decoded.type != &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN])
        return UA_STATUSCODE_BADUSERACCESSDENIED;
    
    UA_X509IdentityToken *token =
        (UA_X509IdentityToken *)userIdentityToken->content.decoded.data;
    
    
    if (!UA_ByteString_equal(&token->certificateData, secureChannelRemoteCertificate))
        return UA_STATUSCODE_BADUSERACCESSDENIED;

    /* No session context needed: access control on the SecurityGroup
     * is done via the node context (see getUserExecutableOnObject_sks
     * below), not via the session context. We leave sessionContext as
     * NULL so as not to interfere with the lifecycle
     * (closeSession/clear) of the default AccessControl plugin, which
     * manages its own internal structure. */
    *sessionContext = (void *)1; /* non-NULL to indicate success */
    return UA_STATUSCODE_GOOD;
}

UA_Boolean
getUserExecutableOnObject_app(UA_Server *server, UA_AccessControl *ac,
                              const UA_NodeId *sessionId, void *sessionContext,
                              const UA_NodeId *methodId, void *methodContext,
                              const UA_NodeId *objectId, void *objectContext) {
    /* Only allow Method calls from sessions that passed
     * activateSession_sks (sessionContext != NULL means the
     * X.509 certificate was verified). Anonymous sessions
     * (sessionContext == NULL) are denied. */
    return sessionContext != NULL;
}


static void
periodicLdsRegisterCallback(UA_Server *server, void *data) {
    clientCreds *ctx = (clientCreds *)data;
    UA_StatusCode rc = registerToLdsSecurely(server, ctx);
    if(rc != UA_STATUSCODE_GOOD)
        printf("[WARNING] LDS registration failed: %s\n", UA_StatusCode_name(rc));
}

UA_StatusCode
startPeriodicLdsRegistration(UA_Server *server,
                             clientCreds *creds,
                             UA_Double intervalMs,
                             UA_UInt64 *callbackId,
                             void **ctxOut) {
    periodicLdsRegisterCallback(server, creds); /* enregistrement immédiat */

    UA_StatusCode rc = UA_Server_addRepeatedCallback(server, periodicLdsRegisterCallback,
                                                     creds, intervalMs, callbackId);
    *ctxOut = creds;
    return rc;
}

void
stopPeriodicLdsRegistration(UA_Server *server, UA_UInt64 callbackId, void *ctx) {
    UA_Server_removeRepeatedCallback(server, callbackId);
}


UA_StatusCode
loadClientCredentials(const char *ldsUrl, const char *certDir, const char *deviceName, const char *applicationUri, clientCreds *out) {
    out->ldsUrl = strdup(ldsUrl);
    out->caCert     = loadFile(buildCertPath(certDir, "ca.cert.der"));
    out->crl        = loadFile(buildCertPath(certDir, "crl.der"));
    char certFile[128], keyFile[128];
    snprintf(certFile, sizeof(certFile), "%s.cert.der", deviceName);
    snprintf(keyFile, sizeof(keyFile), "%s.key.der", deviceName);
    out->clientCert = loadFile(buildCertPath(certDir, certFile));
    out->clientKey  = loadFile(buildCertPath(certDir, keyFile));
    out->applicationUri = UA_String_fromChars(applicationUri);
    if(out->caCert.length == 0 || out->crl.length == 0 ||
       out->clientCert.length == 0 || out->clientKey.length == 0)
        return UA_STATUSCODE_BADARGUMENTSMISSING;
    return UA_STATUSCODE_GOOD;
}

void
clearClientCredentials(clientCreds *creds) {
    free(creds->ldsUrl);
    UA_ByteString_clear(&creds->caCert);
    UA_ByteString_clear(&creds->crl);
    UA_ByteString_clear(&creds->clientCert);
    UA_ByteString_clear(&creds->clientKey);
    UA_ByteString_clear(&creds->applicationUri);
}