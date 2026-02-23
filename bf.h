#pragma once

#include <stdint.h>
#include <stddef.h>

/* Maximum program source length */
#define BF_MAX_SRC   128
/* Maximum output bytes per program (bounded by step limit) */
#define BF_MAX_OUT   128

typedef struct {
    uint8_t src[BF_MAX_SRC];
    uint8_t len;
} BFProgram;

typedef struct {
    uint8_t out[BF_MAX_OUT];
    uint8_t out_len;
    uint8_t halted;     /* 1 = normal termination, 0 = error */
} BFResult;

/* Run a single program. Thread-safe; uses only stack memory. */
void bf_run(const BFProgram *prog, BFResult *result);

/* Run n programs in parallel using nthreads worker threads.
 * Pass nthreads <= 0 to auto-detect (uses all logical CPUs). */
void bf_run_batch(const BFProgram *programs, BFResult *results, size_t n, int nthreads);
