#include <open62541/server.h>
#include <open62541/client_config_default.h>

typedef struct {
    char * ldsUrl;
    UA_ByteString caCert;
    UA_ByteString crl;
    UA_ByteString clientCert;
    UA_ByteString clientKey;
    UA_String applicationUri;
} clientCreds;


UA_StatusCode loadClientCredentials(const char *ldsUrl, const char *certDir, const char *deviceName, const char *applicationUri, clientCreds *out);

void clearClientCredentials(clientCreds *creds);

UA_ByteString loadFile(const char *const path);

UA_ClientConfig *encryptedSksClient(clientCreds *creds);

UA_StatusCode registerToLdsSecurely(UA_Server *server, clientCreds *creds);

void disableAnonymous(UA_ServerConfig *config);

char *resolveSksUrlFromLds(clientCreds *creds);

char *buildCertPath(const char *certDir, const char *filename);

UA_String makeUsernamePolicyId(const UA_String *securityPolicyUri);

void addCertificateTokenPolicy(UA_ServerConfig *config);

UA_StatusCode
activateSession(UA_Server *server, UA_AccessControl *ac,
                     const UA_EndpointDescription *endpointDescription,
                     const UA_ByteString *secureChannelRemoteCertificate,
                     const UA_NodeId *sessionId,
                     const UA_ExtensionObject *userIdentityToken,
                     void **sessionContext);


UA_Boolean
getUserExecutableOnObject_app(UA_Server *server, UA_AccessControl *ac,
                              const UA_NodeId *sessionId, void *sessionContext,
                              const UA_NodeId *methodId, void *methodContext,
                              const UA_NodeId *objectId, void *objectContext);

UA_StatusCode
startPeriodicLdsRegistration(UA_Server *server,
                             clientCreds *creds,
                             UA_Double intervalMs,
                             UA_UInt64 *callbackId, 
                             void **ctxOut);

void
stopPeriodicLdsRegistration(UA_Server *server, UA_UInt64 callbackId, void *ctx);