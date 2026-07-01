#define _GNU_SOURCE
#include <open62541/server.h>
#include <stdio.h>
#include <getopt.h>

#define NO_SCHED_PRIO -1
#define NO_RT_CORE -1

typedef struct {
    struct option gnu_opt;
    const char *arg_name;
    const char *description;
} ExtOption;

typedef struct {
    UA_Boolean rt;
    UA_Boolean rtLog;
    int rtCore;
    int schedPrio;
    long cycleTime;
    char* url;
    char* iface;
    UA_Boolean autostart;
    UA_Boolean sks;
    char * certificate;
    char * key;
} CliOptions;

enum {
    OPT_HELP,
    OPT_RT,
    OPT_RT_LOG,
    OPT_CYCLE_TIME,
    OPT_URL,
    OPT_IFACE,
    OPT_RT_CORE,
    OPT_SCHED_PRIO,
    OPT_SKS,
    OPT_CERT,
    OPT_KEY,
    OPT_AUTOSTART
};

CliOptions parseArgs(int argc, char **argv);
void printUsage(char *program_name);