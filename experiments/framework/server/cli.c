#define _GNU_SOURCE
#include <open62541/server.h>
#include <stdio.h>
#include <getopt.h>
#include "cli.h"
#include <argp.h>

static ExtOption LONG_OPTS[] = {
#ifdef IS_PUBLISHER
    {{"rt",      no_argument,       0,  OPT_RT}, NULL, "Enable real-time scheduling for the publisher"},
    {{"rt-log",      no_argument,       0, OPT_RT_LOG}, NULL, "Enable real-time logging"},
    {{"cycle-time", required_argument, 0, OPT_CYCLE_TIME}, NULL, "Define the cycle time for real-time scheduling in ms"},
    {{"url", required_argument, 0, OPT_URL}, NULL, "Define the publisher URL"},
    {{"iface", required_argument, 0, OPT_IFACE}, NULL, "Define the interface for publishing"},
#endif
    {{"rt-core", required_argument, 0, OPT_RT_CORE}, NULL, "Define the CPU core for real-time scheduling"},
    {{"sched-prio", required_argument, 0, OPT_SCHED_PRIO}, NULL, "Define the scheduling priority for real-time tasks"},
    {{"sks", no_argument, 0, OPT_SKS}, NULL, "Enable encryption with SKS server"},
    {{"cert", required_argument, 0, OPT_CERT}, NULL, "Set the PubSub SKS client certificate"},
    {{"key", required_argument, 0, OPT_KEY}, NULL, "Set the PubSub SKS client private key"},
    {{"autostart", no_argument, 0, OPT_AUTOSTART}, NULL, "Automatically start the server without waiting for user input"},
    {{"help",    no_argument,       0, OPT_HELP}, NULL, "Display this help message"},
    {{0, 0, 0, 0}, NULL, NULL}
};

#define OPT_STRING ""

void printUsage(char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    for (int i = 0; LONG_OPTS[i].gnu_opt.name != NULL; i++) {
        printf("  --%s", LONG_OPTS[i].gnu_opt.name);
        if (LONG_OPTS[i].arg_name) {
            printf(" <%s>", LONG_OPTS[i].arg_name);
        }
        printf("\n      %s\n", LONG_OPTS[i].description);
    }
}


CliOptions parseArgs(int argc, char **argv) {
    CliOptions opts = { .rt = false, .rtLog = false, .rtCore = NO_RT_CORE, .schedPrio = NO_SCHED_PRIO, .cycleTime = 1000000L, 
        .url = "opc.eth://03-00-00-00-00-03:10.6", .iface = "enp43s0", .autostart = false, 
        .sks = false, .certificate = NULL, .key = NULL };

    int num_opts = sizeof(LONG_OPTS) / sizeof(ExtOption);
    struct option long_options[num_opts];
    for (int i = 0; i < num_opts; i++) {
        long_options[i] = LONG_OPTS[i].gnu_opt;
    }

    int opt;
    while((opt = getopt_long(argc, argv, OPT_STRING, long_options, NULL)) != -1) {
        switch(opt) {
        #ifdef IS_PUBLISHER
            case OPT_RT: opts.rt = true; break;
            case OPT_RT_LOG: opts.rtLog = true; break;
            case OPT_CYCLE_TIME: opts.cycleTime = atol(optarg)*1000000L; break;
            case OPT_URL: opts.url = strdup(optarg); break;
            case OPT_IFACE: opts.iface = strdup(optarg); break;
        #endif
            case OPT_RT_CORE: opts.rtCore = atoi(optarg); break;
            case OPT_SCHED_PRIO: opts.schedPrio = atoi(optarg); break;
            case OPT_SKS: opts.sks = true; break;
            case OPT_CERT: opts.certificate = strdup(optarg); break;
            case OPT_KEY: opts.key = strdup(optarg); break;
            case OPT_AUTOSTART: opts.autostart = true; break;
            case OPT_HELP:
                printUsage(argv[0]);
                exit(0);
            default:
                fprintf(stderr, "Unknown option. Please use --help.\n");
                exit(1);
        }
    }
    return opts;
}