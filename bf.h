#pragma once

#include <stdint.h>
#include <stddef.h>

/* Maximum program source length */
#define BF_MAX_SRC   128
/* Maximum output bytes per program (bounded by step limit) */
#define BF_MAX_OUT   128

/* Maximum steps per program (0 = unlimited). Override with -DBF_MAX_STEPS=N */
#ifndef BF_MAX_STEPS
#define BF_MAX_STEPS 1000000
#endif

typedef struct {
    uint8_t  src[BF_MAX_SRC];
    uint8_t  len;
} BFProgram;

typedef struct {
    uint8_t  out[BF_MAX_OUT];
    uint8_t  out_len;
    uint8_t  halted;     /* 1 = normal termination, 0 = error/timeout */
#ifdef BF_LONGEST_RUN_TEST
    uint32_t steps;      /* number of instructions executed */
#endif
} BFResult;

/* Run a single program. Thread-safe; uses only stack memory. */
void bf_run(const BFProgram *prog, BFResult *result);

/* Run n programs in parallel using nthreads worker threads.
 * Pass nthreads <= 0 to auto-detect (uses all logical CPUs). */
void bf_run_batch(const BFProgram *programs, BFResult *results, size_t n, int nthreads);
