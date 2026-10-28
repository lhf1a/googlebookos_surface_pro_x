#define _GNU_SOURCE
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    int cpu = atoi(argv[1]), seconds = atoi(argv[2]);
    if (cpu < 0 || cpu > 7 || seconds < 1 || seconds > 30) return 2;
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set)) { perror("affinity"); return 2; }
    volatile uint32_t seed = 1;
    unsigned long rounds = 0;
    double end = now() + seconds;
    do {
        uint32_t x = seed;
        for (unsigned i = 0; i < 100000; i++) x = x * 1664525u + 1013904223u;
        if (x != 694140833u) { fprintf(stderr, "CPU%d bad checksum %u\n", cpu, x); return 1; }
        rounds++;
    } while (now() < end);
    printf("CPU%d CHECK_PASS rounds=%lu seconds=%d\n", cpu, rounds, seconds);
    return 0;
}
