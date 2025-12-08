# Linux Benchmarking Tools

This repository contains a collection of benchmarking tools for Linux systems. These tools are designed to measure various aspects of system performance, including memory allocation, syscall overhead, scheduler performance, and pipe latency/throughput.

## Tools Included

### 1. `heap-test`
Located in `brk/heap-test.c`.

**Purpose**: A simple utility to visualize memory allocation addresses and the program break (sbrk) behavior. It demonstrates how `malloc`, `calloc`, and `realloc` affect the heap and the program break.

**Usage**:
```bash
gcc -o heap-test brk/heap-test.c
./heap-test
```

### 2. `callbench`
Located in `callbench/callbench.c`.

**Purpose**: Benchmarks the overhead of `clock_gettime` syscalls and vDSO performance. It also includes benchmarks for `getpid` and file I/O (`mmap` vs `read`).

**Usage**:
```bash
gcc -o callbench callbench/callbench.c
./callbench [options]
```
**Options**:
- `-m, --mode`: Tests to run: `time`, `file`, or `all` (default: `all`).
- `-c, --calls`: Syscalls per loop.
- `-l, --loops`: Loops per round.
- `-r, --rounds`: Benchmark rounds.

### 3. `hackbench`
Located in `hackbench/hackbench.c`.

**Purpose**: A benchmark for the scheduler and Unix socket/pipe performance. It creates groups of senders and receivers that communicate via pipes or sockets, stressing the scheduler and IPC mechanisms.

**Usage**:
```bash
gcc -pthread -o hackbench hackbench/hackbench.c
./hackbench [options]
```
**Options**:
- `-p, --pipe`: Use pipes instead of socketpairs.
- `-s, --datasize`: Message size in bytes.
- `-l, --loops`: Number of loops.
- `-g, --groups`: Number of groups.
- `-f, --fds`: File descriptors per group.
- `-T, --threads`: Use threads instead of processes.
- `-P, --process`: Use processes (default).
- `-F, --fifo`: Use SCHED_FIFO (realtime).

### 4. `pipe-latency`
Located in `pipe-latency/pipe-latency.c`.

**Purpose**: Measures the latency and context-switching overhead of pipe operations. It ping-pongs data between threads or processes using pipes.

**Usage**:
```bash
gcc -pthread -o pipe-latency pipe-latency/pipe-latency.c
./pipe-latency [options]
```
**Options**:
- `-l, --loop`: Number of loops.
- `-T, --threaded`: Use threads instead of processes.

### 5. `pipebench`
Located in `pipebench/pipebench.c`.

**Purpose**: Measures the speed of stdin/stdout communication. It sits in a pipeline and reports the throughput.

**Usage**:
```bash
gcc -o pipebench pipebench/pipebench.c
cat largefile | ./pipebench > /dev/null
```
**Options**:
- `-b`: Buffer size.
- `-q`: Quiet mode.
- `-s`: Status file.

## Building

To build all tools, you can run the following commands:

```bash
gcc -o heap-test brk/heap-test.c
gcc -o callbench callbench/callbench.c
gcc -pthread -o hackbench hackbench/hackbench.c
gcc -pthread -o pipe-latency pipe-latency/pipe-latency.c
gcc -o pipebench pipebench/pipebench.c
```

## License

Please refer to the individual source files for license information.
