#include <open62541/server.h>
#include <open62541/client_config_default.h>

UA_ByteString loadFile(const char *const path);

UA_ClientConfig *encryptedSksClient(const char *username, const char *password,
                                           const char *applicationUri,
                                           UA_ByteString certificate,
                                           UA_ByteString privateKey);
