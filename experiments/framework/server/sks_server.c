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
 *   ./sks_server <server-cert.der> <server-key.der> <lds-server-cert.der> \
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
#include <signal.h>

#include "sks_helpers.h"

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
#define DEMO_KEYLIFETIME_MINUTES 5
#define DEMO_MAXFUTUREKEYCOUNT 2
#define DEMO_MAXPASTKEYCOUNT 2

#define LDS_URL "opc.tcp://192.168.17.143:4840"
#define LDS_REREGISTER_INTERVAL_MS 30000.0

static volatile UA_Boolean running = true;

static void stopHandler(int sig) {
    printf("\n[SERVER] Shutdown signal received\n");
    running = false;
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
}

static void
usage(const char *progname) {
    fprintf(stderr,
        "Usage: %s <server-cert.der> <server-key.der> <lds-cert.der>\n"
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
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);

    if(argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    clientCreds creds;
    loadClientCredentials(LDS_URL, argv[1], "sks_server", "urn:example:uafx:sks-server", &creds);

    UA_UInt16 port = 4850;

    UA_ServerConfig config;
    memset(&config, 0, sizeof(UA_ServerConfig));

    UA_StatusCode res = UA_ServerConfig_setDefaultWithSecurityPolicies(&config, port, &creds.clientCert, &creds.clientKey, &creds.caCert, 1, NULL, 0, &creds.crl, 1);
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

    addCertificateTokenPolicy(&config);

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

    /* certificate authentication + access control on the SecurityGroup */
    config.accessControl.activateSession = activateSession;

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

    UA_StatusCode retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "run_startup failed: %s", UA_StatusCode_name(retval));
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    disableUnencrypted(&config);
    disableAnonymous(&config);
    
    //registerToLdsSecurely(server, LDS_URL, argv[3], argv[1], argv[2], "urn:example:uafx:sks-server");
    UA_UInt64 ldsRegisterCallbackId = 0;
    void *ldsRegisterCtx = NULL;
    startPeriodicLdsRegistration(server, &creds, 5*60*1000.0, &ldsRegisterCallbackId, &ldsRegisterCtx);

    
    while (running)
        UA_Server_run_iterate(server, true);
    //UA_StatusCode retval = UA_Server_runUntilInterrupt(server);

    retval = UA_Server_run_shutdown(server);
    stopPeriodicLdsRegistration(server, ldsRegisterCallbackId, ldsRegisterCtx);
    UA_Server_delete(server);
    clearClientCredentials(&creds);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}
