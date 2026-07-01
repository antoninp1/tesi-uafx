/* ============================================================
 * sks_server.c
 *
 * Security Key Service (SKS) for OPC UA PubSub encryption.
 *
 * Role: manages a SecurityGroup and distributes the symmetric keys
 * (signing/encrypting/nonce) to Publishers/Subscribers that call the
 * GetSecurityKeys Method, subject to authentication.
 *
 * Usage:
 *   ./sks_server <server-cert.der> <server-key.der> \
 *       --trustlist <publisher-cert.der> <subscriber-cert.der> \
 *       [--port 4850]
 *
 * Build: see CMakeLists.txt (target "sks_server")
 * ============================================================ */

#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/securitypolicy_default.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINUTE_SECONDS 60
#define MILLI_SECONDS 1000
#define MAX_OPERATION_LIMIT 10000

/* Must be identical on the Publisher and Subscriber side
 * (UA_WriterGroupConfig / UA_ReaderGroupConfig . securityGroupId) */
#define SECURITY_POLICY_URI "http://opcfoundation.org/UA/SecurityPolicy#PubSub-Aes256-CTR"
#define DEMO_SECURITYGROUPNAME "UafxSecurityGroup"

/* Key lifetime before rollover. Kept short for demo/lab purposes so
 * the rollover mechanism can be observed; lengthen it (e.g. 30-60 min)
 * for real-world use. */
#define DEMO_KEYLIFETIME_MINUTES 1
#define DEMO_MAXFUTUREKEYCOUNT 0
#define DEMO_MAXPASTKEYCOUNT 1

/* Account used by the SKS clients (Publisher/Subscriber) to
 * authenticate against this server. Must match what is configured in
 * pub_test.c / sub_test.c (encryptedSksClient()). */
#define SKS_USERNAME "uafx-sks-client"
#define SKS_PASSWORD "ChangeThisPasswordInLab"

/* ─── File loading (cert/key DER) ────────────────────────────────── */
static UA_ByteString
loadFile(const char *const path) {
    UA_ByteString fileContents = UA_STRING_NULL;

    FILE *fp = fopen(path, "rb");
    if(!fp) {
        fprintf(stderr, "[SKS] Cannot open file %s\n", path);
        return fileContents;
    }

    if(fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return fileContents;
    }
    long length = ftell(fp);
    if(length < 0) {
        fclose(fp);
        return fileContents;
    }

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

/* ─── Only allow authenticated connections (no anonymous access) ──── */
static void
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

static UA_String
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

static void
addUsernamePasswordTokenPolicy(UA_ServerConfig *config) {
    for(size_t i = 0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ep = &config->endpoints[i];
 
        UA_UserTokenPolicy *newArray = (UA_UserTokenPolicy *)
            UA_realloc(ep->userIdentityTokens,
                      (ep->userIdentityTokensSize + 1) * sizeof(UA_UserTokenPolicy));
        if(!newArray)
            continue;
        ep->userIdentityTokens = newArray;
 
        UA_UserTokenPolicy *utp = &ep->userIdentityTokens[ep->userIdentityTokensSize];
        UA_UserTokenPolicy_init(utp);
        utp->tokenType = UA_USERTOKENTYPE_USERNAME;
        utp->policyId = makeUsernamePolicyId(&ep->securityPolicyUri);
        /* Leave securityPolicyUri empty: per spec, this means "use the
         * SecureChannel's own security policy" -- works uniformly across
         * every endpoint regardless of its policy/mode. The framework
         * already strips this back out on SecurityMode=None endpoints
         * on its own (see the "Removing a UserTokenPolicy that would
         * allow the password to be transmitted without encryption"
         * log line you saw earlier). */
        ep->userIdentityTokensSize++;
        UA_String_copy(&ep->securityPolicyUri, &utp->securityPolicyUri);
    }
}

/* ─── Only allow encrypted endpoints (drop SecurityMode=None) ─────── */
static void
disableUnencrypted(UA_ServerConfig *config) {
    for(size_t i = 0; i < config->endpointsSize; i++) {
        UA_EndpointDescription *ep = &config->endpoints[i];
        if(ep->securityMode != UA_MESSAGESECURITYMODE_NONE)
            continue;
        UA_EndpointDescription_clear(ep);
        if(i + 1 < config->endpointsSize) {
            config->endpoints[i] = config->endpoints[config->endpointsSize - 1];
            i--;
        }
        config->endpointsSize--;
    }
    if(config->endpointsSize == 0) {
        UA_free(config->endpoints);
        config->endpoints = NULL;
    }
}

/* ─── Simple username/password authentication ──────────────────────── */
static UA_StatusCode
activateSession_sks(UA_Server *server, UA_AccessControl *ac,
                     const UA_EndpointDescription *endpointDescription,
                     const UA_ByteString *secureChannelRemoteCertificate,
                     const UA_NodeId *sessionId,
                     const UA_ExtensionObject *userIdentityToken,
                     void **sessionContext) {
    if(userIdentityToken->content.decoded.type != &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN])
        return UA_STATUSCODE_BADUSERACCESSDENIED;

    UA_UserNameIdentityToken *token =
        (UA_UserNameIdentityToken *)userIdentityToken->content.decoded.data;

    UA_String expectedUser = UA_STRING(SKS_USERNAME);
    UA_String expectedPass = UA_STRING(SKS_PASSWORD);

    if(!UA_String_equal(&token->userName, &expectedUser) ||
       !UA_String_equal(&token->password, &expectedPass))
        return UA_STATUSCODE_BADUSERACCESSDENIED;

    /* No session context needed: access control on the SecurityGroup
     * is done via the node context (see getUserExecutableOnObject_sks
     * below), not via the session context. We leave sessionContext as
     * NULL so as not to interfere with the lifecycle
     * (closeSession/clear) of the default AccessControl plugin, which
     * manages its own internal structure. */
    *sessionContext = NULL;
    return UA_STATUSCODE_GOOD;
}

/* Checks that only the expected user can call GetSecurityKeys on this
 * SecurityGroup (the nodeContext is set by addSecurityGroup below). */
static UA_Boolean
getUserExecutableOnObject_sks(UA_Server *server, UA_AccessControl *ac,
                              const UA_NodeId *sessionId, void *sessionContext,
                              const UA_NodeId *methodId, void *methodContext,
                              const UA_NodeId *objectId, void *objectContext) {
    if(!objectContext)
        return true; /* no restriction set on this object */
    const char *allowedUser = (const char *)objectContext;
    return strcmp(allowedUser, SKS_USERNAME) == 0;
}

static void
addSecurityGroup(UA_Server *server, UA_NodeId *outNodeId) {
    UA_SecurityGroupConfig config;
    memset(&config, 0, sizeof(UA_SecurityGroupConfig));
    config.keyLifeTime = DEMO_KEYLIFETIME_MINUTES * MINUTE_SECONDS * MILLI_SECONDS;
    config.securityPolicyUri = UA_STRING(SECURITY_POLICY_URI);
    config.securityGroupName = UA_STRING(DEMO_SECURITYGROUPNAME);
    config.maxFutureKeyCount = DEMO_MAXFUTUREKEYCOUNT;
    config.maxPastKeyCount = DEMO_MAXPASTKEYCOUNT;

    UA_NodeId securityGroupParent = UA_NS0ID(PUBLISHSUBSCRIBE_SECURITYGROUPS);
    UA_StatusCode rc = UA_Server_addSecurityGroup(server, securityGroupParent,
                                                  &config, outNodeId);
    if(rc != UA_STATUSCODE_GOOD) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "Failed to add SecurityGroup: %s", UA_StatusCode_name(rc));
        exit(EXIT_FAILURE);
    }

    /* Restrict access to the SecurityGroup to SKS_USERNAME only */
    UA_Server_setNodeContext(server, *outNodeId, (void *)SKS_USERNAME);
}

static void
usage(const char *progname) {
    fprintf(stderr,
        "Usage: %s <server-cert.der> <server-key.der>\n"
        "       [--port <port>]                  (default: 4850)\n"
        "       [--trustlist <cert1.der> <cert2.der> ...]\n"
        "\n"
        "--trustlist must contain the Publisher's and Subscriber's\n"
        "application certificates, so that their SecureChannel to\n"
        "this SKS server gets accepted (SignAndEncrypt is required).\n",
        progname);
}

int
main(int argc, char **argv) {
    if(argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    UA_ByteString certificate = loadFile(argv[1]);
    UA_ByteString privateKey = loadFile(argv[2]);
    if(certificate.length == 0 || privateKey.length == 0) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "Unable to load server certificate/key");
        return EXIT_FAILURE;
    }

    UA_UInt16 port = 4850;
    UA_ByteString trustList[100];
    size_t trustListSize = 0;

    for(int i = 3; i < argc; i++) {
        if(strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (UA_UInt16)atoi(argv[++i]);
            continue;
        }
        if(strcmp(argv[i], "--trustlist") == 0) {
            for(int j = i + 1; j < argc; j++) {
                if(trustListSize >= 100) {
                    UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                                 "Too many entries in trustlist");
                    return EXIT_FAILURE;
                }
                trustList[trustListSize] = loadFile(argv[j]);
                if(trustList[trustListSize].length == 0) {
                    UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                                 "Unable to load trustlist entry %s", argv[j]);
                    return EXIT_FAILURE;
                }
                trustListSize++;
                i = j;
            }
            continue;
        }
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if(trustListSize == 0) {
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
            "No --trustlist provided: NO client certificate will be "
            "trusted, the Publisher/Subscriber will fail to connect.");
    }

    UA_ServerConfig config;
    memset(&config, 0, sizeof(UA_ServerConfig));

    UA_StatusCode res = UA_ServerConfig_setDefaultWithSecurityPolicies(
        &config, port, &certificate, &privateKey,
        trustList, trustListSize,
        NULL, 0,   /* issuerList */
        NULL, 0);  /* revocationList */
    if(res != UA_STATUSCODE_GOOD) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "ServerConfig setup failed: %s", UA_StatusCode_name(res));
        return EXIT_FAILURE;
    }

    /* Application identity: MUST match the URI of the server
     * certificate (subjectAltName URI:...) generated for this SKS. */
    UA_String_clear(&config.applicationDescription.applicationUri);
    config.applicationDescription.applicationUri =
        UA_String_fromChars("urn:example:uafx:sks-server");
    UA_LocalizedText_clear(&config.applicationDescription.applicationName);
    config.applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "UAFX Security Key Service");

    disableUnencrypted(&config);
    disableAnonymous(&config);
    addUsernamePasswordTokenPolicy(&config);

    config.maxSecureChannels = 20;
    config.maxSessions = 20;
    config.shutdownDelay = 2000.0;

    config.maxNodesPerRead = MAX_OPERATION_LIMIT;
    config.maxNodesPerWrite = MAX_OPERATION_LIMIT;
    config.maxNodesPerMethodCall = MAX_OPERATION_LIMIT;
    config.maxNodesPerBrowse = MAX_OPERATION_LIMIT;

    /* PubSub security policy managed by this SKS (AES-256-CTR keys) */
    config.pubSubConfig.securityPolicies =
        (UA_PubSubSecurityPolicy *)UA_malloc(sizeof(UA_PubSubSecurityPolicy));
    config.pubSubConfig.securityPoliciesSize = 1;
    UA_PubSubSecurityPolicy_Aes256Ctr(config.pubSubConfig.securityPolicies,
                                      config.logging);

    /* Username/password authentication + access control on the SecurityGroup */
    config.accessControl.activateSession = activateSession_sks;
    config.accessControl.getUserExecutableOnObject = getUserExecutableOnObject_sks;

    UA_Server *server = UA_Server_newWithConfig(&config);
    if(!server) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "UA_Server_newWithConfig failed");
        return EXIT_FAILURE;
    }

    UA_NodeId securityGroupId;
    addSecurityGroup(server, &securityGroupId);

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                "SKS server running on opc.tcp://apons-radius.mmwunibo.it:%u  "
                "SecurityGroupId=%s  (keyLifeTime=%dmin)",
                port, DEMO_SECURITYGROUPNAME, DEMO_KEYLIFETIME_MINUTES);

    UA_Server_enableAllPubSubComponents(server);
    UA_StatusCode retval = UA_Server_runUntilInterrupt(server);

    UA_Server_delete(server);
    UA_ByteString_clear(&certificate);
    UA_ByteString_clear(&privateKey);
    for(size_t i = 0; i < trustListSize; i++)
        UA_ByteString_clear(&trustList[i]);

    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
