#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

/* Arch-specific definitions for older NDK/libc headers */
#ifndef __NR_close_range
  #if defined(__aarch64__) || defined(__arm__) || defined(__x86_64__) || defined(__i386__)
    #define __NR_close_range 436
  #endif
#endif

#ifndef __NR_epoll_pwait2
  #if defined(__aarch64__) || defined(__arm__) || defined(__x86_64__) || defined(__i386__)
    #define __NR_epoll_pwait2 441
  #endif
#endif

/* ARM64 requires epoll_create1 (syscall 20) */
#ifndef __NR_epoll_create1
  #if defined(__aarch64__)
    #define __NR_epoll_create1 20
  #endif
#endif

int main() {
    printf("[*] Verifying Backported Syscalls...\n");

    /* --- TEST 1: close_range (436) --- */
    #ifdef __NR_close_range
    printf("[+] Testing close_range (__NR_close_range = %d)...\n", __NR_close_range);

    int test_fd = open("/dev/null", O_RDONLY);
    if (test_fd < 0) return 1;

    long ret = syscall(__NR_close_range, test_fd, test_fd, 0);

    if (ret == 0) {
        printf("    [PASS] close_range works! (Return: 0)\n");
        /* Verify FD is actually closed */
        if (close(test_fd) == -1 && errno == EBADF) {
             printf("           (Verified: FD actually closed)\n");
        }
    } else if (errno == ENOSYS) {
        printf("    [FAIL] close_range NOT FOUND (ENOSYS).\n");
    } else {
        printf("    [WARN] close_range present, error: %s\n", strerror(errno));
    }
    #else
    printf("[!] __NR_close_range undefined for this arch.\n");
    #endif

    /* --- TEST 2: epoll_pwait2 (441) --- */
    #ifdef __NR_epoll_pwait2
    printf("\n[+] Testing epoll_pwait2 (__NR_epoll_pwait2 = %d)...\n", __NR_epoll_pwait2);

    int epfd = -1;
    
    /* Try epoll_create1 first (modern standard) */
    #ifdef __NR_epoll_create1
        epfd = syscall(__NR_epoll_create1, 0);
    #endif

    /* Fallback to legacy epoll_create ONLY if defined (missing on arm64) */
    if (epfd < 0) {
        #ifdef __NR_epoll_create
            epfd = syscall(__NR_epoll_create, 1);
        #endif
    }

    if (epfd < 0) {
        perror("    [-] Failed to create epoll instance");
    } else {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 };

        /* Pass NULL events to check syscall existence */
        ret = syscall(__NR_epoll_pwait2, epfd, NULL, 0, &ts, NULL, 0);

        if (ret == 0) {
            printf("    [PASS] epoll_pwait2 works! (Return: 0)\n");
        } else if (errno == ENOSYS) {
            printf("    [FAIL] epoll_pwait2 NOT FOUND (ENOSYS).\n");
        } else {
            printf("    [PASS] epoll_pwait2 detected (Response: %s)\n", strerror(errno));
        }
        close(epfd);
    }
    #else
    printf("[!] __NR_epoll_pwait2 undefined for this arch.\n");
    #endif

    return 0;
}
