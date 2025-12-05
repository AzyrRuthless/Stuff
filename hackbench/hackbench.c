/*
 * hackbench.c
 *
 * Benchmark for scheduler and unix-socket/pipe performance.
 *
 * Originally based on code by Ingo Molnar and Rusty Russell.
 * Refactored for modern Linux environments.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <limits.h>
#include <getopt.h>
#include <signal.h>
#include <setjmp.h>
#include <sched.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

/* Defaults */
static unsigned int datasize = 100;
static unsigned int loops = 100;
static unsigned int num_groups = 10;
static unsigned int num_fds = 20;
static bool use_fifo = false;
static bool use_pipes = false;
static bool process_mode = true;

struct sender_context {
    unsigned int num_fds;
    int ready_out;
    int wakefd;
    int out_fds[]; /* C99 Flexible Array Member */
};

struct receiver_context {
    unsigned int num_packets;
    int in_fds[2];
    int ready_out;
    int wakefd;
};

typedef union {
    pthread_t threadid;
    pid_t pid;
} childinfo_t;

static childinfo_t *child_tab = NULL;
static unsigned int total_children = 0;
static volatile sig_atomic_t signal_caught = 0;
static jmp_buf jmpbuf;

/* Helper to handle fatal errors */
static void panic(const char *msg) {
    if (!signal_caught) {
        fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    }
    exit(1);
}

static void print_usage(void) {
    printf("Usage: hackbench [options]\n"
           "Options:\n"
           "  -p, --pipe       Use pipes instead of socketpairs\n"
           "  -s, --datasize   Message size in bytes (default: 100)\n"
           "  -l, --loops      Number of loops (default: 100)\n"
           "  -g, --groups     Number of groups (default: 10)\n"
           "  -f, --fds        File descriptors per group (default: 20)\n"
           "  -T, --threads    Use threads (default: processes)\n"
           "  -P, --process    Use processes (default)\n"
           "  -F, --fifo       Use SCHED_FIFO (realtime)\n"
           "  -h, --help       Show this help\n");
    exit(1);
}

static void fdpair(int fds[2]) {
    if (use_pipes) {
        if (pipe(fds) == 0) return;
    } else {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) return;
    }
    panic("Creating fdpair");
}

/* Block until we're ready to go */
static void ready(int ready_out, int wakefd) {
    char dummy = '*';
    struct pollfd pfd = { .fd = wakefd, .events = POLLIN };

    /* Tell main we're ready */
    if (write(ready_out, &dummy, 1) != 1)
        panic("CLIENT: ready write");

    /* Wait for "GO" signal */
    if (poll(&pfd, 1, -1) != 1)
        panic("poll");
}

/* Reset signal handlers for workers */
static void reset_worker_signals(void) {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
}

static void *sender(struct sender_context *ctx) {
    char data[datasize];
    unsigned int i, j;

    reset_worker_signals();
    ready(ctx->ready_out, ctx->wakefd);
    memset(&data, '-', datasize);

    for (i = 0; i < loops; i++) {
        for (j = 0; j < ctx->num_fds; j++) {
            int ret, done = 0;
            while (done < (int)datasize) {
                ret = write(ctx->out_fds[j], data + done, datasize - done);
                if (ret < 0) panic("SENDER: write");
                done += ret;
            }
        }
    }
    return NULL;
}

static void *receiver(struct receiver_context *ctx) {
    unsigned int i;

    reset_worker_signals();
    if (process_mode)
        close(ctx->in_fds[1]);

    ready(ctx->ready_out, ctx->wakefd);

    for (i = 0; i < ctx->num_packets; i++) {
        char data[datasize];
        int ret, done = 0;
        while (done < (int)datasize) {
            ret = read(ctx->in_fds[0], data + done, datasize - done);
            if (ret < 0) panic("RECEIVER: read");
            done += ret;
        }
    }
    free(ctx);
    return NULL;
}

static int create_worker(childinfo_t *child, void *ctx, void *(*func)(void *)) {
    pthread_attr_t attr;
    int err;

    if (process_mode) {
        switch ((child->pid = fork())) {
            case -1: return -1;
            case 0:  (*func)(ctx); exit(0);
        }
    } else {
        if (pthread_attr_init(&attr) != 0) return -1;
        if (pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN) != 0) return -1;
        if ((err = pthread_create(&child->threadid, &attr, func, ctx)) != 0) return -1;
    }
    return 0;
}

static unsigned int reap_workers(childinfo_t *child, unsigned int totchld, bool dokill) {
    unsigned int i, rc = 0;
    int status;

    if (dokill) {
        fprintf(stderr, "Sending SIGTERM to all child processes\n");
        signal(SIGTERM, SIG_IGN);
        for (i = 0; i < totchld; i++) kill(child[i].pid, SIGTERM);
    }

    for (i = 0; i < totchld; i++) {
        if (process_mode) {
            int pid = wait(&status);
            if (pid == -1 && errno == ECHILD) break;
            if (!WIFEXITED(status)) rc++;
        } else {
            if (pthread_join(child[i].threadid, NULL) != 0) rc++;
        }
    }
    return rc;
}

static unsigned int group(childinfo_t *child, unsigned int tab_offset,
                          unsigned int num_fds, int ready_out, int wakefd) {
    unsigned int i;
    struct sender_context *snd_ctx = malloc(sizeof(struct sender_context) + num_fds * sizeof(int));

    if (!snd_ctx) panic("malloc() [sender ctx]");

    for (i = 0; i < num_fds; i++) {
        int fds[2];
        struct receiver_context *ctx = malloc(sizeof(*ctx));
        if (!ctx) panic("malloc() [receiver ctx]");

        fdpair(fds);

        ctx->num_packets = num_fds * loops;
        ctx->in_fds[0] = fds[0];
        ctx->in_fds[1] = fds[1];
        ctx->ready_out = ready_out;
        ctx->wakefd = wakefd;

        if (create_worker(&child[tab_offset + i], ctx, (void *)(void *)receiver)) {
            panic("create_worker receiver");
        }

        snd_ctx->out_fds[i] = fds[1];
        if (process_mode) close(fds[0]);
    }

    snd_ctx->ready_out = ready_out;
    snd_ctx->wakefd = wakefd;
    snd_ctx->num_fds = num_fds;

    for (i = 0; i < num_fds; i++) {
        if (create_worker(&child[tab_offset + num_fds + i], snd_ctx, (void *)(void *)sender)) {
            panic("create_worker sender");
        }
    }

    if (process_mode) {
        for (i = 0; i < num_fds; i++) close(snd_ctx->out_fds[i]);
    }
    
    /* Intentionally leak snd_ctx (it's small and we exit soon) or free it if we were cleaner */
    /* free(snd_ctx); - dangerous in threads if passed pointer is still used */

    return num_fds * 2;
}

static void sigcatcher(int sig) {
    signal_caught = 1;
    fprintf(stderr, "Signal %d caught, exiting...\n", sig);
    signal(sig, SIG_IGN);
    longjmp(jmpbuf, 1);
}

static void get_mono_time(struct timespec *ts) {
    if (clock_gettime(CLOCK_MONOTONIC, ts) == -1) panic("clock_gettime");
}

int main(int argc, char *argv[]) {
    unsigned int i;
    struct timespec start, stop;
    double diff_sec;
    int readyfds[2], wakefds[2];
    char dummy;
    struct sched_param sp;

    while (1) {
        int optind = 0;
        static struct option longopts[] = {
            {"pipe", no_argument, NULL, 'p'},
            {"datasize", required_argument, NULL, 's'},
            {"loops", required_argument, NULL, 'l'},
            {"groups", required_argument, NULL, 'g'},
            {"fds", required_argument, NULL, 'f'},
            {"threads", no_argument, NULL, 'T'},
            {"processes", no_argument, NULL, 'P'},
            {"fifo", no_argument, NULL, 'F'},
            {"help", no_argument, NULL, 'h'},
            {NULL, 0, NULL, 0}
        };

        int c = getopt_long(argc, argv, "ps:l:g:f:TPFh", longopts, &optind);
        if (c == -1) break;

        switch (c) {
            case 'p': use_pipes = true; break;
            case 's': datasize = atoi(optarg); break;
            case 'l': loops = atoi(optarg); break;
            case 'g': num_groups = atoi(optarg); break;
            case 'f': num_fds = atoi(optarg); break;
            case 'T': process_mode = false; break;
            case 'P': process_mode = true; break;
            case 'F': use_fifo = true; break;
            case 'h': print_usage(); break;
            default: exit(1);
        }
    }

    printf("Running in %s mode with %d groups using %d file descriptors each (== %d tasks)\n",
           process_mode ? "process" : "threaded",
           num_groups, 2 * num_fds, num_groups * (num_fds * 2));
    printf("Each sender will pass %d messages of %d bytes\n", loops, datasize);
    fflush(NULL);

    child_tab = calloc(num_fds * 2 * num_groups, sizeof(childinfo_t));
    if (!child_tab) panic("main:malloc()");

    fdpair(readyfds);
    fdpair(wakefds);

    signal(SIGINT, sigcatcher);
    signal(SIGTERM, sigcatcher);
    signal(SIGHUP, SIG_IGN);

    if (setjmp(jmpbuf) == 0) {
        total_children = 0;
        for (i = 0; i < num_groups; i++) {
            int c = group(child_tab, total_children, num_fds, readyfds[1], wakefds[0]);
            total_children += c;
        }

        if (use_fifo) {
            sp.sched_priority = 1;
            if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
                panic("can't change to fifo in main");
        }

        /* Wait for workers to allow setup to settle */
        for (i = 0; i < total_children; i++) {
            if (read(readyfds[0], &dummy, 1) != 1)
                panic("Reading for readyfds");
        }

        get_mono_time(&start);

        /* Kick start */
        if (write(wakefds[1], &dummy, 1) != 1)
            panic("Writing to start senders");
    } else {
        reap_workers(child_tab, total_children, 1);
        free(child_tab);
        return 1;
    }

    reap_workers(child_tab, total_children, 0);

    get_mono_time(&stop);

    diff_sec = (stop.tv_sec - start.tv_sec) + 
               (double)(stop.tv_nsec - start.tv_nsec) / 1e9;

    printf("Time: %.3f s\n", diff_sec);

    free(child_tab);
    return 0;
}
