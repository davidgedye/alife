#define _POSIX_C_SOURCE 200809L

#ifndef BF_LONGEST_RUN_TEST
#define BF_LONGEST_RUN_TEST
#endif
#include "bf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>

#define N_PROGRAMS     1000000
#define PROG_LEN       64
#define N_HIST_BUCKETS 6       /* log10 buckets: [1,9]..[100000,999999] */
#define BAR_WIDTH      50

static int steps_bucket(uint32_t steps) {
    int k = 0;
    uint32_t lo = 1;
    while (k < N_HIST_BUCKETS - 1 && steps >= lo * 10) {
        lo *= 10;
        k++;
    }
    return k;
}

int main(void) {
    srand((unsigned)time(NULL));

    BFProgram *programs = malloc(N_PROGRAMS * sizeof(BFProgram));
    BFResult  *results  = malloc(N_PROGRAMS * sizeof(BFResult));
    if (!programs || !results) { perror("malloc"); return 1; }

    /* Generate random programs: fixed 64 bytes, all 256 byte values equally likely */
    for (int i = 0; i < N_PROGRAMS; i++) {
        programs[i].len = PROG_LEN;
        for (int j = 0; j < PROG_LEN; j++)
            programs[i].src[j] = (uint8_t)(rand() & 0xFF);
    }

    fprintf(stderr, "Running %d programs of %d bytes (max %d steps each)...\n",
            N_PROGRAMS, PROG_LEN, BF_MAX_STEPS);

    bf_run_batch(programs, results, N_PROGRAMS, 0);

    /* Tally results */
    size_t   best_idx   = N_PROGRAMS;
    uint32_t best_steps = 0;
    size_t   n_halted   = 0;
    size_t   n_timeout  = 0;
    size_t   n_zero     = 0;   /* malformed or 0 effective instructions */
    size_t   hist[N_HIST_BUCKETS] = {0};

    for (int i = 0; i < N_PROGRAMS; i++) {
        if (results[i].halted) {
            if (results[i].steps == 0) {
                n_zero++;
            } else {
                n_halted++;
                hist[steps_bucket(results[i].steps)]++;
                if (results[i].steps > best_steps) {
                    best_steps = results[i].steps;
                    best_idx   = (size_t)i;
                }
            }
        } else if (results[i].steps > 0) {
            n_timeout++;
        } else {
            n_zero++;   /* malformed (bracket mismatch) */
        }
    }

    fprintf(stderr, "  Halted normally: %zu\n", n_halted + n_zero);
    fprintf(stderr, "  Timed out:       %zu\n", n_timeout);
    fprintf(stderr, "  Zero steps:      %zu\n", n_zero);

    /* Print histogram */
    size_t max_count = n_timeout > n_zero ? n_timeout : n_zero;
    for (int k = 0; k < N_HIST_BUCKETS; k++)
        if (hist[k] > max_count) max_count = hist[k];
    if (max_count == 0) max_count = 1;

    printf("\n=== Run length histogram ===\n");
    printf("              0 | ");
    int zero_bar = (int)(n_zero * BAR_WIDTH / max_count);
    for (int b = 0; b < zero_bar; b++) putchar('#');
    printf(" %zu\n", n_zero);
    uint32_t lo = 1;
    for (int k = 0; k < N_HIST_BUCKETS; k++) {
        uint32_t hi = lo * 10 - 1;
        int bar_len = (int)(hist[k] * BAR_WIDTH / max_count);
        printf("%7u - %7u | ", lo, hi);
        for (int b = 0; b < bar_len; b++) putchar('#');
        printf(" %zu\n", hist[k]);
        lo *= 10;
    }
    int bar_len = (int)(n_timeout * BAR_WIDTH / max_count);
    printf("       > %7u | ", (uint32_t)BF_MAX_STEPS);
    for (int b = 0; b < bar_len; b++) putchar('#');
    printf(" %zu\n", n_timeout);

    if (best_idx == N_PROGRAMS) {
        fprintf(stderr, "No program halted normally.\n");
        free(programs); free(results);
        return 1;
    }

    const BFProgram *winner = &programs[best_idx];
    const BFResult  *wr     = &results[best_idx];

    printf("\n=== Winner ===\n");
    printf("Program (%u bytes, hex): ", winner->len);
    for (int i = 0; i < winner->len; i++)
        printf("%02x", winner->src[i]);
    putchar('\n');

    printf("Program (printable):    ");
    for (int i = 0; i < winner->len; i++)
        putchar(isprint(winner->src[i]) ? winner->src[i] : '.');
    putchar('\n');

    printf("Steps: %u\n", wr->steps);

    printf("Output (%u bytes): ", wr->out_len);
    if (wr->out_len == 0) {
        printf("(none)\n");
    } else {
        for (int i = 0; i < wr->out_len; i++)
            printf("%02x ", wr->out[i]);
        putchar('\n');
    }

    free(programs);
    free(results);
    return 0;
}
