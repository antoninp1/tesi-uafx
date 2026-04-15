/* ============================================================
 * main.c
 *
 * UAFX Discovery Client — HTTP server mode.
 *
 * Il client espone un'API REST sulla porta 8080 (default).
 * All'avvio NON esegue discovery automatica: aspetta che un
 * client HTTP invochi POST /api/discovery/run.
 *
 * Modello: single-thread cooperativo. Il main loop chiama
 * httpServerPoll() ripetutamente.
 *
 * Uso:
 *   ./discovery_client               → porta 8080 default
 *   ./discovery_client 9090          → porta custom
 *   source .env && ./discovery_client
 *
 * Test rapido:
 *   curl http://localhost:8080/api/health
 *   curl -X POST http://localhost:8080/api/discovery/run
 *   curl http://localhost:8080/api/topology
 * ============================================================ */

#include "common.h"
#include "model.h"
#include "http_server.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_HTTP_PORT 8080

static volatile int running = 1;

static void stopHandler(int sig) {
    (void)sig;
    running = 0;
    printf("\n[MAIN] Shutdown requested\n");
}

int main(int argc, char **argv) {
    signal(SIGINT,  stopHandler);
    signal(SIGTERM, stopHandler);

    int port = DEFAULT_HTTP_PORT;
    if(argc >= 2) {
        port = atoi(argv[1]);
        if(port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    printf("\n");
    printf("========================================================\n");
    printf("  UAFX Discovery Client — HTTP Server Mode\n");
    printf("========================================================\n\n");

    /* Alloca il TopologyGraph su heap (evita segfault da stack) */
    TopologyGraph *graph = calloc(1, sizeof(TopologyGraph));
    if(!graph) {
        fprintf(stderr, "[MAIN] Failed to allocate TopologyGraph\n");
        return EXIT_FAILURE;
    }
    topologyGraphInit(graph);
    printf("[MAIN] TopologyGraph initialized (empty)\n");

    if(!httpServerInit(port, graph)) {
        fprintf(stderr, "[MAIN] HTTP server init failed\n");
        free(graph);
        return EXIT_FAILURE;
    }

    printf("\n[MAIN] Ready. Waiting for HTTP requests...\n");
    printf("[MAIN] Press Ctrl+C to stop.\n\n");

    /* Main loop cooperativo */
    while(running) {
        httpServerPoll(100);
    }

    printf("\n[MAIN] Shutting down...\n");
    httpServerShutdown();
    topologyGraphClear(graph);
    free(graph);
    printf("[MAIN] Stopped cleanly\n\n");

    return EXIT_SUCCESS;
}
