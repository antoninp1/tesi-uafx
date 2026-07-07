#include <open62541/server.h>
#include <open62541/client_config_default.h>

UA_ByteString loadFile(const char *const path);

UA_ClientConfig *encryptedSksClient(const char *applicationUri, UA_ByteString certificate, UA_ByteString privateKey);

UA_StatusCode registerToLdsSecurely(UA_Server *server, const char *ldsUrl,
                         const char *clientCertPath, const char *clientKeyPath,
                         const char *applicationUri);

void disableAnonymous(UA_ServerConfig *config);

char *resolveSksUrlFromLds(const char *ldsUrl, const char *sksApplicationUri,
                           const char *clientCertPath, const char *clientKeyPath);

char *buildCertPath(const char *certDir, const char *filename);

UA_String makeUsernamePolicyId(const UA_String *securityPolicyUri);

void addCertificateTokenPolicy(UA_ServerConfig *config);

UA_StatusCode
activateSession_sks(UA_Server *server, UA_AccessControl *ac,
                     const UA_EndpointDescription *endpointDescription,
                     const UA_ByteString *secureChannelRemoteCertificate,
                     const UA_NodeId *sessionId,
                     const UA_ExtensionObject *userIdentityToken,
                     void **sessionContext);