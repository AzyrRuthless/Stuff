/*
 * callbench.c
 *
 * Benchmark for clock_gettime syscall overhead and vDSO performance.
 *
 * Copyright (c) 2018-2020 Danny Lin <danny@kdrag0n.dev>
 * Licensed under the MIT License.
 */

#define _POSIX_C_SOURCE 200809L

#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>

#define TEST_READ_PATH "/dev/zero"
#define TEST_READ_LEN 65536

#define NS_PER_SEC 1000000000

#if defined(__linux__)
#define CLOCK_GETTIME_SYSCALL_NR __NR_clock_gettime
#elif defined(__APPLE__)
#define NO_DIRECT_SYSCALL
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#define CLOCK_GETTIME_SYSCALL_NR SYS_clock_gettime
#elif defined(__NetBSD__)
#define CLOCK_GETTIME_SYSCALL_NR SYS___clock_gettime50
#else
#error Unsupported platform: missing clock_gettime syscall number!
#endif

typedef void (*bench_impl)(void);

static char test_read_buf[TEST_READ_LEN];

/**
 * ts_to_ns
 *
 * Converts a timespec structure to nanoseconds.
 *
 * @param ts The timespec structure to convert.
 * @return The total time in nanoseconds.
 */
static long ts_to_ns(struct timespec ts) {
    return ts.tv_nsec + (ts.tv_sec * NS_PER_SEC);
}

#ifndef NO_DIRECT_SYSCALL
/**
 * time_syscall_mb
 *
 * Microbenchmark function that invokes the clock_gettime syscall directly.
 * Used to measure the overhead of a direct syscall without vDSO.
 */
static void time_syscall_mb(void) {
    struct timespec ts;
    syscall(CLOCK_GETTIME_SYSCALL_NR, CLOCK_MONOTONIC, &ts);
}
#endif

/**
 * time_libc_mb
 *
 * Microbenchmark function that invokes clock_gettime via libc.
 * This typically uses the vDSO if available, which is faster than a direct syscall.
 */
static void time_libc_mb(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
}

/**
 * getpid_syscall_mb
 *
 * Microbenchmark function that invokes the getpid syscall directly.
 * Used as a baseline for a simple syscall.
 */
static void getpid_syscall_mb(void) {
    syscall(__NR_getpid);
}

/**
 * mmap_mb
 *
 * Microbenchmark function that maps a file into memory, copies data, and unmaps it.
 * Used to measure the performance of mmap-based file access.
 */
static void mmap_mb(void) {
    int fd = open(TEST_READ_PATH, O_RDONLY);
    int len = TEST_READ_LEN;

    void *data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    memcpy(test_read_buf, data, len);

    munmap(data, len);
    close(fd);
}

/**
 * file_mb
 *
 * Microbenchmark function that reads data from a file using the read() syscall.
 * Used to measure the performance of read-based file access.
 */
static void file_mb(void) {
    int fd = open(TEST_READ_PATH, O_RDONLY);
    long len = TEST_READ_LEN;

    if (read(fd, test_read_buf, len) < 0) {
        /* Ignore read errors during benchmark */
    }

    close(fd);
}

/**
 * run_bench_ns
 *
 * Runs a benchmark for a specific implementation function.
 * Calculates the best average execution time per call over multiple rounds.
 *
 * @param inner_call The function pointer to the microbenchmark to run.
 * @param calls The number of calls to perform per loop.
 * @param loops The number of loops to perform per round.
 * @param rounds The number of rounds to execute.
 * @return The best average execution time in nanoseconds.
 */
static long run_bench_ns(bench_impl inner_call, int calls, int loops, int rounds) {
    long best_ns1 = LONG_MAX;
    struct timespec req = {0, 125000000}; /* 125ms delay */

    for (int round = 0; round < rounds; round++) {
        long best_ns2 = LONG_MAX;

        for (int loop = 0; loop < loops; loop++) {
            struct timespec before;
            clock_gettime(CLOCK_MONOTONIC, &before);

            for (int call = 0; call < calls; call++) {
                inner_call();
            }

            struct timespec after;
            clock_gettime(CLOCK_MONOTONIC, &after);

            long elapsed_ns = ts_to_ns(after) - ts_to_ns(before);
            if (elapsed_ns < best_ns2) {
                best_ns2 = elapsed_ns;
            }
        }

        best_ns2 /= calls;

        if (best_ns2 < best_ns1) {
            best_ns1 = best_ns2;
        }

        putchar('.');
        fflush(stdout);
        nanosleep(&req, NULL);
    }

    return best_ns1;
}

/**
 * default_arg
 *
 * Returns a default value if the argument provided is -1.
 *
 * @param arg The argument value to check.
 * @param def The default value to return if arg is -1.
 * @return The argument value or the default value.
 */
static int default_arg(int arg, int def) {
    return arg == -1 ? def : arg;
}

/**
 * bench_time
 *
 * Runs benchmarks related to time retrieval syscalls (clock_gettime, getpid).
 * Compares direct syscalls (if supported) against libc implementations.
 *
 * @param calls Number of calls per loop (overridden if -1).
 * @param loops Number of loops per round (overridden if -1).
 * @param rounds Number of rounds (overridden if -1).
 */
static void bench_time(int calls, int loops, int rounds) {
    calls = default_arg(calls, 100000);
    loops = default_arg(loops, 32);
    rounds = default_arg(rounds, 5);

    printf("clock_gettime: ");
    fflush(stdout);

#ifndef NO_DIRECT_SYSCALL
    long best_ns_syscall = run_bench_ns(time_syscall_mb, calls, loops, rounds);
    long best_ns_getpid = run_bench_ns(getpid_syscall_mb, calls, loops, rounds);
#endif
    long best_ns_libc = run_bench_ns(time_libc_mb, calls, loops, rounds);

    putchar('\n');

#ifdef NO_DIRECT_SYSCALL
    printf("    syscall:\t<unsupported>\n");
#else
    printf("    syscall:\t%ld ns\n", best_ns_syscall);
    printf("    getpid:\t%ld ns\n", best_ns_getpid);
#endif
    printf("    libc:\t%ld ns\n", best_ns_libc);
}

/**
 * bench_file
 *
 * Runs benchmarks related to file I/O (mmap vs read).
 *
 * @param calls Number of calls per loop (overridden if -1).
 * @param loops Number of loops per round (overridden if -1).
 * @param rounds Number of rounds (overridden if -1).
 */
static void bench_file(int calls, int loops, int rounds) {
    calls = default_arg(calls, 100);
    loops = default_arg(loops, 128);
    rounds = default_arg(rounds, 5);

    printf("read file: ");
    fflush(stdout);

    long best_ns_mmap = run_bench_ns(mmap_mb, calls, loops, rounds);
    long best_ns_read = run_bench_ns(file_mb, calls, loops, rounds);

    printf("\n    mmap:\t%ld ns\n", best_ns_mmap);
    printf("    read:\t%ld ns\n", best_ns_read);
}

static char *short_options = "hm:c:l:r:";
static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"mode", required_argument, 0, 'm'},
        {"calls", required_argument, 0, 'c'},
        {"loops", required_argument, 0, 'l'},
        {"rounds", required_argument, 0, 'r'},
        {0, 0, 0, 0}
};

/**
 * print_help
 *
 * Prints the help message describing the program usage and options, then exits.
 *
 * @param prog_name The name of the program (usually argv[0]).
 */
static void print_help(char *prog_name) {
    printf("Usage: %s [options]\n"
           "\n"
           "Benchmark simple kernel syscalls (Time vs File I/O)\n"
           "\n"
           "Options:\n"
           "  -h, --help\tshow usage help\n"
           "  -m, --mode\ttests to run: time, file, or all (default: all)\n"
           "  -c, --calls\tsyscalls per loop\n"
           "  -l, --loops\tloops per round\n"
           "  -r, --rounds\tbenchmark rounds (default: 5)\n",
           prog_name);

    exit(1);
}

/**
 * parse_args
 *
 * Parses command-line arguments and updates the configuration variables.
 *
 * @param argc The argument count.
 * @param argv The argument vector.
 * @param do_time Pointer to boolean flag for time benchmark.
 * @param do_file Pointer to boolean flag for file benchmark.
 * @param calls Pointer to integer for number of calls.
 * @param loops Pointer to integer for number of loops.
 * @param rounds Pointer to integer for number of rounds.
 */
static void parse_args(int argc, char **argv, bool *do_time, bool *do_file, int *calls, int *loops, int *rounds) {
    while (1) {
        int c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
            case '?':
            case 'h':
                print_help(argv[0]);
                break;
            case 'm':
                if (!strcmp(optarg, "time")) {
                    *do_time = 1;
                    *do_file = 0;
                } else if (!strcmp(optarg, "file")) {
                    *do_time = 0;
                    *do_file = 1;
                } else if (!strcmp(optarg, "all")) {
                    *do_time = 1;
                    *do_file = 1;
                } else {
                    fprintf(stderr, "%s: invalid mode -- '%s'\n", argv[0], optarg);
                    print_help(argv[0]);
                }
                break;
            case 'c':
                *calls = atoi(optarg);
                break;
            case 'l':
                *loops = atoi(optarg);
                break;
            case 'r':
                *rounds = atoi(optarg);
                break;
        }
    }
}

/**
 * main
 *
 * The main entry point of the callbench utility.
 * Parses arguments and executes the selected benchmarks.
 *
 * @param argc The argument count.
 * @param argv The argument vector.
 * @return Returns 0 on successful execution.
 */
int main(int argc, char** argv) {
    bool do_time = 1;
    bool do_file = 1;
    int calls = -1;
    int loops = -1;
    int rounds = -1;

    parse_args(argc, argv, &do_time, &do_file, &calls, &loops, &rounds);

    if (do_time) {
        bench_time(calls, loops, rounds);
    }

    if (do_time && do_file) {
        putchar('\n');
    }

    if (do_file) {
        bench_file(calls, loops, rounds);
    }

    return 0;
}
