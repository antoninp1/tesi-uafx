#define _GNU_SOURCE
#include <open62541/server.h>
#include <stdio.h>
#include <getopt.h>
#include "cli.h"


static ExtOption LONG_OPTS[] = {
#ifdef IS_PUBLISHER
    {{"rt",      no_argument,       0, 'r'}, NULL, "Enable real-time scheduling for the publisher"},
    {{"rt-log",      no_argument,       0, 'l'}, NULL, "Enable real-time logging"},
    {{"cycle-time", required_argument, 0, 'c'}, NULL, "Define the cycle time for real-time scheduling in ms"},
#endif
    {{"rt-core", required_argument, 0, 'c'}, NULL, "Define the CPU core for real-time scheduling"},
    {{"sched-prio", required_argument, 0, 'p'}, NULL, "Define the scheduling priority for real-time tasks"},
    {{"autostart", no_argument, 0, 'a'}, NULL, "Automatically start the server without waiting for user input"},
    {{"help",    no_argument,       0, 'h'}, NULL, "Display this help message"},
    {{0, 0, 0, 0}, NULL, NULL}
};

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
    CliOptions opts = { .rt = false, .rtLog = false, .rtCore = 2, .schedPrio = 80, .cycleTime = 1000000L, .autostart = false};

    int num_opts = sizeof(LONG_OPTS) / sizeof(ExtOption);
    struct option long_options[num_opts];
    for (int i = 0; i < num_opts; i++) {
        long_options[i] = LONG_OPTS[i].gnu_opt;
    }

    int opt;
    while((opt = getopt_long(argc, argv, OPT_STRING, long_options, NULL)) != -1) {
        switch(opt) {
        #ifdef IS_PUBLISHER
            case 'r': opts.rt = true; break;
            case 'l': opts.rtLog = true; break;
            case 't': opts.cycleTime = atol(optarg)*1000*1000000L; break;
        #endif
            case 'c': opts.rtCore = atoi(optarg); break;
            case 'p': opts.schedPrio = atoi(optarg); break;
            case 'a': opts.autostart = true; break;
            case 'h':
                printUsage(argv[0]);
                exit(0);
            default:
                fprintf(stderr, "Unknown option. Please use --help.\n");
                exit(1);
        }
    }
    return opts;
}