#define _POSIX_C_SOURCE 200809L

#include "bf.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

/* Number of programs each worker grabs per atomic increment */
#define BATCH_SIZE 64

/* -------------------------------------------------------------------------
 * Interpreter
 * -------------------------------------------------------------------------*/

void bf_run(const BFProgram *prog, BFResult *result) {
    uint8_t tape[256];
    memset(tape, 0, sizeof(tape));

    /* Jump table: jumps[i] = index of matching bracket */
    uint8_t jumps[BF_MAX_SRC];

    /* Pass 1: build jump table */
    uint8_t stack[64];
    uint8_t sp = 0;
    for (uint8_t i = 0; i < prog->len; i++) {
        if (prog->src[i] == '[') {
            stack[sp++] = i;
        } else if (prog->src[i] == ']') {
            if (sp == 0) {
                /* Unmatched ']' — program is malformed */
                result->out_len = 0;
                result->halted = 0;
                return;
            }
            uint8_t open = stack[--sp];
            jumps[open] = i;
            jumps[i]    = open;
        }
    }
    if (sp != 0) {
        /* Unmatched '[' */
        result->out_len = 0;
        result->halted = 0;
        return;
    }

    /* Pass 2: execute */
    uint8_t  ip       = 0;
    uint8_t  dp       = 128;   /* start tape pointer in the middle */
    uint8_t  out_len  = 0;
    uint32_t steps    = 0;
    uint32_t max_steps = prog->max_steps;

    while (ip < prog->len) {
        if (max_steps && steps >= max_steps) {
            result->out_len = out_len;
            result->halted  = 0;   /* timeout */
            result->steps   = steps;
            return;
        }
        switch (prog->src[ip]) {
            case '+': tape[dp]++;  break;
            case '-': tape[dp]--;  break;
            case '>': dp++;        break;
            case '<': dp--;        break;
            case '.':
                if (out_len < BF_MAX_OUT)
                    result->out[out_len++] = tape[dp];
                break;
            case ',':
                /* No stdin in batch mode — treat as zero */
                tape[dp] = 0;
                break;
            case '[':
                if (!tape[dp]) ip = jumps[ip];
                break;
            case ']':
                if ( tape[dp]) ip = jumps[ip];
                break;
            default:
                break;
        }
        ip++;
        steps++;
    }

    result->out_len = out_len;
    result->halted  = 1;
    result->steps   = steps;
}

/* -------------------------------------------------------------------------
 * Thread pool
 * -------------------------------------------------------------------------*/

typedef struct {
    const BFProgram  *programs;
    BFResult         *results;
    size_t            n;
    _Atomic size_t   *next;
} WorkerArgs;

static void *worker(void *arg) {
    WorkerArgs *a = (WorkerArgs *)arg;

    for (;;) {
        size_t base = atomic_fetch_add(a->next, BATCH_SIZE);
        if (base >= a->n) break;

        size_t end = base + BATCH_SIZE;
        if (end > a->n) end = a->n;

        for (size_t i = base; i < end; i++) {
            bf_run(&a->programs[i], &a->results[i]);
        }
    }

    return NULL;
}

void bf_run_batch(const BFProgram *programs, BFResult *results, size_t n, int nthreads) {
    if (n == 0) return;

    if (nthreads <= 0) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = (cpus > 0) ? (int)cpus : 1;
    }

    _Atomic size_t next = 0;

    WorkerArgs args = {
        .programs = programs,
        .results  = results,
        .n        = n,
        .next     = &next,
    };

    /* Don't spin up threads we don't need */
    if (nthreads > (int)n) nthreads = (int)n;

    pthread_t *threads = malloc((size_t)nthreads * sizeof(pthread_t));
    if (!threads) {
        /* Fallback: single-threaded */
        for (size_t i = 0; i < n; i++)
            bf_run(&programs[i], &results[i]);
        return;
    }

    for (int t = 0; t < nthreads; t++) {
        if (pthread_create(&threads[t], NULL, worker, &args) != 0) {
            /* If thread creation fails, run remaining work inline */
            for (size_t i = atomic_fetch_add(&next, BATCH_SIZE);
                 i < n;
                 i = atomic_fetch_add(&next, BATCH_SIZE)) {
                size_t end = i + BATCH_SIZE;
                if (end > n) end = n;
                for (size_t j = i; j < end; j++)
                    bf_run(&programs[j], &results[j]);
            }
            nthreads = t;
            break;
        }
    }

    for (int t = 0; t < nthreads; t++)
        pthread_join(threads[t], NULL);

    free(threads);
}

#ifdef BF_MAIN
/* -------------------------------------------------------------------------
 * Main: read programs from stdin (text format), run, print results
 *
 * Input format (one program per line, plain BF source):
 *   ++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.
 *
 * Output format (one result per line):
 *   OK <hex bytes>        — halted normally, output as hex
 *   OK (no output)        — halted normally, no output
 *   ERR                   — malformed program
 * -------------------------------------------------------------------------*/

int main(int argc, char *argv[]) {
    int nthreads = 0;   /* 0 = auto */
    if (argc >= 2) {
        nthreads = atoi(argv[1]);
    }

    /* Read all programs from stdin */
    size_t capacity = 4096;
    size_t count    = 0;
    BFProgram *programs = malloc(capacity * sizeof(BFProgram));
    BFResult  *results  = NULL;
    if (!programs) { perror("malloc"); return 1; }

    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline / whitespace */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
                           line[len-1] == ' '  || line[len-1] == '\t'))
            len--;
        if (len == 0) continue;   /* skip blank lines */
        if (len > BF_MAX_SRC) len = BF_MAX_SRC;

        if (count == capacity) {
            capacity *= 2;
            BFProgram *tmp = realloc(programs, capacity * sizeof(BFProgram));
            if (!tmp) { perror("realloc"); free(programs); return 1; }
            programs = tmp;
        }

        memcpy(programs[count].src, line, len);
        programs[count].len       = (uint8_t)len;
        programs[count].max_steps = 0;   /* unlimited */
        count++;
    }

    if (count == 0) {
        free(programs);
        return 0;
    }

    results = malloc(count * sizeof(BFResult));
    if (!results) { perror("malloc"); free(programs); return 1; }

    bf_run_batch(programs, results, count, nthreads);

    /* Print results */
    for (size_t i = 0; i < count; i++) {
        if (!results[i].halted) {
            puts("ERR");
            continue;
        }
        if (results[i].out_len == 0) {
            puts("OK (no output)");
            continue;
        }
        printf("OK");
        for (uint8_t j = 0; j < results[i].out_len; j++)
            printf(" %02x", results[i].out[j]);
        putchar('\n');
    }

    free(programs);
    free(results);
    return 0;
}
#endif /* BF_MAIN */
