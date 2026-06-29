#define _GNU_SOURCE
#include <open62541/server.h>
#include <stdio.h>
#include <getopt.h>

#ifdef IS_PUBLISHER
    #define OPT_STRING "rlc:p:ah"
#else
    #define OPT_STRING "c:p:ah"
#endif

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
    UA_Boolean autostart;

} CliOptions;

CliOptions parseArgs(int argc, char **argv);
void printUsage(char *program_name);