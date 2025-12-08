/*
 * pipe-latency.c
 *
 * pipe: Benchmark for pipe() latency/context-switching
 *
 * Based on pipe-test-1m.c by Ingo Molnar <mingo@redhat.com>
 * http://people.redhat.com/mingo/cfs-scheduler/tools/pipe-test-1m.c
 *
 * Refactored for modern Linux/Android environments.
 */

#define _POSIX_C_SOURCE 199309L /* Required for clock_gettime */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

/**
 * BUG_ON
 *
 * Utility macro that checks a condition. If the condition is true, it prints
 * an error message and exits the program.
 *
 * @param condition The condition to check.
 */
#define BUG_ON(condition) do { \
	if (condition) { \
		fprintf(stderr, "Bug on: %s\n", #condition); \
		exit(1); \
	} \
} while (0)

#define USEC_PER_SEC 1000000
#define USEC_PER_MSEC 1000

struct thread_data {
	int nr;
	int pipe_read;
	int pipe_write;
	pthread_t pthread;
};

#define LOOPS_DEFAULT 1000000
static int loops = LOOPS_DEFAULT;
static bool threaded = false;

/**
 * print_usage
 *
 * Prints the usage information for the program, including available options.
 *
 * @param prog_name The name of the program (usually argv[0]).
 */
static void print_usage(const char *prog_name) {
	printf("Usage: %s [options]\n", prog_name);
	printf("Options:\n");
	printf("  -l, --loop <number>     Specify number of loops (default: %d)\n", LOOPS_DEFAULT);
	printf("  -T, --threaded          Use threads instead of processes\n");
}

/**
 * parse_options
 *
 * Parses command-line arguments to configure the benchmark.
 *
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line arguments.
 */
static void parse_options(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--loop") == 0) {
			if (i + 1 < argc) {
				loops = atoi(argv[++i]);
			} else {
				fprintf(stderr, "Error: --loop requires a value\n");
				print_usage(argv[0]);
				exit(1);
			}
		} else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--threaded") == 0) {
			threaded = true;
		} else {
			print_usage(argv[0]);
			exit(1);
		}
	}
}

/**
 * get_mono_time
 *
 * Gets the current time from the CLOCK_MONOTONIC clock.
 * Exits on failure.
 *
 * @param ts Pointer to a timespec structure to store the time.
 */
static void get_mono_time(struct timespec *ts) {
	if (clock_gettime(CLOCK_MONOTONIC, ts) == -1) {
		perror("clock_gettime");
		exit(1);
	}
}

/**
 * worker_thread
 *
 * The worker function that performs the pipe operations.
 * It alternates between reading and writing to simulate ping-pong communication.
 *
 * @param __tdata Pointer to the thread data structure.
 * @return NULL.
 */
static void *worker_thread(void *__tdata) {
	struct thread_data *td = (struct thread_data *)__tdata;
	int m = 0;
	ssize_t ret;

	for (int i = 0; i < loops; i++) {
		if (!td->nr) {
			/* Thread 0: Read -> Write */
			ret = read(td->pipe_read, &m, sizeof(int));
			BUG_ON(ret != sizeof(int));
			ret = write(td->pipe_write, &m, sizeof(int));
			BUG_ON(ret != sizeof(int));
		} else {
			/* Thread 1: Write -> Read */
			ret = write(td->pipe_write, &m, sizeof(int));
			BUG_ON(ret != sizeof(int));
			ret = read(td->pipe_read, &m, sizeof(int));
			BUG_ON(ret != sizeof(int));
		}
	}

	return NULL;
}

/**
 * main
 *
 * The main entry point of the pipe-latency benchmark.
 * Sets up the pipes and threads/processes, runs the benchmark, and reports the results.
 *
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line arguments.
 * @return Returns 0 on success.
 */
int main(int argc, char **argv) {
	struct thread_data threads[2];
	int pipe_1[2], pipe_2[2];
	struct timespec start, stop;
	double result_usec = 0;
	double diff_sec = 0;
	int nr_threads = 2;

	parse_options(argc, argv);

	BUG_ON(pipe(pipe_1));
	BUG_ON(pipe(pipe_2));

	for (int t = 0; t < nr_threads; t++) {
		threads[t].nr = t;
		if (t == 0) {
			threads[t].pipe_read = pipe_1[0];
			threads[t].pipe_write = pipe_2[1];
		} else {
			threads[t].pipe_write = pipe_1[1];
			threads[t].pipe_read = pipe_2[0];
		}
	}

	get_mono_time(&start);

	if (threaded) {
		for (int t = 0; t < nr_threads; t++) {
			int ret = pthread_create(&threads[t].pthread, NULL, worker_thread, &threads[t]);
			BUG_ON(ret);
		}

		for (int t = 0; t < nr_threads; t++) {
			int ret = pthread_join(threads[t].pthread, NULL);
			BUG_ON(ret);
		}
	} else {
		pid_t pid = fork();
		assert(pid >= 0);

		if (!pid) {
			worker_thread(&threads[0]);
			exit(0);
		} else {
			worker_thread(&threads[1]);
		}

		int wait_stat;
		pid_t retpid = waitpid(pid, &wait_stat, 0);
		assert((retpid == pid) && WIFEXITED(wait_stat));
	}

	get_mono_time(&stop);

	diff_sec = (stop.tv_sec - start.tv_sec) + 
		   (double)(stop.tv_nsec - start.tv_nsec) / 1e9;
	
	result_usec = diff_sec * USEC_PER_SEC;

	printf("# Executed %d pipe operations between two %s\n\n", 
		loops, threaded ? "threads" : "processes");

	printf(" %14s: %.3f [sec]\n\n", "Total time", diff_sec);
	printf(" %14.3f usecs/op\n", result_usec / (double)loops);
	printf(" %14.0f ops/sec\n", (double)loops / diff_sec);

	return 0;
}
