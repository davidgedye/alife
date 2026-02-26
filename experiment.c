#define _POSIX_C_SOURCE 200809L

#define BF_LONGEST_RUN_TEST
#include "bf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>

#define N_PROGRAMS  1000000

static const char BF_OPS[] = "+-><.,[]";
#define N_OPS 8

int main(void) {
    srand((unsigned)time(NULL));

    BFProgram *programs = malloc(N_PROGRAMS * sizeof(BFProgram));
    BFResult  *results  = malloc(N_PROGRAMS * sizeof(BFResult));
    if (!programs || !results) { perror("malloc"); return 1; }

    /* Generate random programs from all 8 BF opcodes */
    for (int i = 0; i < N_PROGRAMS; i++) {
        int len = 1 + rand() % BF_MAX_SRC;
        programs[i].len       = (uint8_t)len;
        for (int j = 0; j < len; j++)
            programs[i].src[j] = (uint8_t)BF_OPS[rand() % N_OPS];
    }

    fprintf(stderr, "Running %d programs (max %d steps each)...\n",
            N_PROGRAMS, BF_MAX_STEPS);

    bf_run_batch(programs, results, N_PROGRAMS, 0);

    /* Find the program that ran the most steps before halting normally */
    size_t   best_idx   = N_PROGRAMS; /* sentinel: none found */
    uint32_t best_steps = 0;
    size_t   n_halted   = 0;
    size_t   n_timeout  = 0;
    size_t   n_err      = 0;

    for (int i = 0; i < N_PROGRAMS; i++) {
        /* halted=1: normal termination; halted=0 covers both ERR and timeout.
         * Distinguish: steps==BF_MAX_STEPS means timeout, steps<BF_MAX_STEPS means err. */
        if (results[i].halted) {
            n_halted++;
            if (results[i].steps > best_steps) {
                best_steps = results[i].steps;
                best_idx   = (size_t)i;
            }
        } else if (results[i].steps > 0) {
            n_timeout++;  /* ran some steps then timed out */
        } else {
            n_err++;      /* malformed (bracket mismatch) */
        }
    }

    fprintf(stderr, "  Halted normally: %zu\n", n_halted);
    fprintf(stderr, "  Timed out:       %zu\n", n_timeout);
    fprintf(stderr, "  Malformed:       %zu\n", n_err);

    if (best_idx == N_PROGRAMS) {
        fprintf(stderr, "No program halted normally.\n");
        free(programs); free(results);
        return 1;
    }

    const BFProgram *winner = &programs[best_idx];
    const BFResult  *wr     = &results[best_idx];

    printf("\n=== Winner ===\n");
    printf("Program (%u bytes): ", winner->len);
    for (int i = 0; i < winner->len; i++)
        putchar(winner->src[i]);
    putchar('\n');

    printf("Steps: %u\n", wr->steps);

    printf("Output: ");
    if (wr->out_len == 0) {
        printf("(none)\n");
    } else {
        /* Print as ASCII where printable, else hex escape */
        printf("\"");
        for (int i = 0; i < wr->out_len; i++) {
            uint8_t b = wr->out[i];
            if (isprint(b))
                putchar(b);
            else
                printf("\\x%02x", b);
        }
        printf("\"\n");
        printf("Output bytes (%u): ", wr->out_len);
        for (int i = 0; i < wr->out_len; i++)
            printf("%02x ", wr->out[i]);
        putchar('\n');
    }

    free(programs);
    free(results);
    return 0;
}
