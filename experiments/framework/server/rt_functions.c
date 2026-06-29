#define _GNU_SOURCE
#include "rt_functions.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

void lockMemoryRT(void) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("[RT] mlockall failed (ran as root ?)");
    }
}

void setupSchedulePriority(int schedPrio) {
    struct sched_param sp = { .sched_priority = schedPrio };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("[RT] sched_setscheduler failed (ran as root ?)");
    }
}

void setupCpuAffinity(int rtCore) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(rtCore, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("[RT] sched_setaffinity failed");
    }
}