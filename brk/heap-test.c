/*
 * heap-test.c
 *
 * A simple utility to visualize memory allocation addresses and
 * the program break (sbrk) behavior.
 *
 * Note: Modern allocators (Scudo/jemalloc/glibc) often use mmap
 * for allocations, so sbrk(0) may not move strictly with malloc.
 */

/* Required for sbrk on some libc implementations */
#define _DEFAULT_SOURCE 

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

/* Utility macro for error checking */
#define CHECK_PTR(ptr) do { \
    if (ptr == NULL) { \
        perror("Allocation failed"); \
        exit(1); \
    } \
} while (0)

int main(void) {
    printf("PID: %d\n", getpid());

    /* Check program break before allocation */
    void* brk_before = sbrk(0);
    printf("sbrk(0) before malloc: %p\n", brk_before);

    /* 1. Standard Allocation */
    void* malloc_ptr = malloc(1024);
    CHECK_PTR(malloc_ptr);
    printf("malloc(1024):        %p\n", malloc_ptr);

    /* 2. Zero-initialized Allocation */
    void* calloc_ptr = calloc(4, 256);
    CHECK_PTR(calloc_ptr);
    printf("calloc(4, 256):      %p\n", calloc_ptr);

    /* 3. Reallocation */
    /* Note: realloc might move the block to a new address */
    void* realloc_ptr = realloc(malloc_ptr, 2048);
    CHECK_PTR(realloc_ptr);
    printf("realloc(2048):       %p\n", realloc_ptr);

    /* Check program break after allocation */
    void* brk_after = sbrk(0);
    printf("sbrk(0) after allocs:  %p\n", brk_after);

    printf("\nTo inspect maps, run in another terminal:\n");
    printf("  cat /proc/%d/maps | grep heap\n", getpid());
    
    printf("\nPress ENTER to free memory and exit...");
    getchar();

    /* Cleanup */
    free(realloc_ptr); /* malloc_ptr is invalidated by realloc success */
    free(calloc_ptr);

    return 0;
}
