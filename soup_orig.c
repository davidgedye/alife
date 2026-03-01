#define _POSIX_C_SOURCE 200809L

#include "bff_orig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Soup parameters
 * -------------------------------------------------------------------------*/
#define SOUP_SIZE   (1 << 17)   /* 131072 tapes */
#define NPAIRS      (SOUP_SIZE / 2)
#define MAX_THREADS 256

/* -------------------------------------------------------------------------
 * Global soup state
 * -------------------------------------------------------------------------*/
static uint64_t soup[SOUP_SIZE][BFFO_HALF_LEN];  /* 64 MB in BSS */
static uint32_t perm[SOUP_SIZE];                  /* shuffle buffer for pairing */

/* Monotonically increasing token ID assigned at init and mutation */
static uint32_t next_token_id = 0;

/* Instruction lookup for tape display */
static const uint8_t SOUP_IS_OP[256] = {
    ['<']=1, ['>']=1, ['{']=1, ['}']=1,
    ['+']=1, ['-']=1, ['.']=1, [',']=1,
    ['[']=1, [']']=1,
};

/* -------------------------------------------------------------------------
 * XorShift64 RNG
 * -------------------------------------------------------------------------*/
static uint64_t global_rng;

static inline uint64_t xorshift64(uint64_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

/* -------------------------------------------------------------------------
 * Fisher-Yates shuffle of perm[0..SOUP_SIZE-1]
 * -------------------------------------------------------------------------*/
static void shuffle_perm(void) {
    for (uint32_t i = 0; i < SOUP_SIZE; i++) perm[i] = i;
    for (uint32_t i = SOUP_SIZE - 1; i > 0; i--) {
        uint32_t j = (uint32_t)(xorshift64(&global_rng) % (i + 1));
        uint32_t tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    }
}

/* -------------------------------------------------------------------------
 * Mutation
 * -------------------------------------------------------------------------*/
#define SOUP_TOTAL_BYTES  ((uint32_t)(SOUP_SIZE) * BFFO_HALF_LEN)  /* 2^23 */
#define SOUP_BYTE_MASK    (SOUP_TOTAL_BYTES - 1)                    /* 0x7FFFFF */

static void mutate_soup(double rate, int epoch) {
    if (rate <= 0.0) return;

    double lambda = SOUP_TOTAL_BYTES * rate;
    double L = exp(-lambda);
    double p = 1.0;
    uint32_t k = 0;
    do {
        k++;
        p *= (double)(xorshift64(&global_rng) >> 11) * (1.0 / (double)(1ULL << 53));
    } while (p > L);
    k--;

    for (uint32_t m = 0; m < k; m++) {
        uint64_t r   = xorshift64(&global_rng);
        uint32_t pos = (uint32_t)(r >> 41) & SOUP_BYTE_MASK;
        uint8_t  val = (uint8_t)(r & 0xFF);
        soup[pos >> 6][pos & (BFFO_HALF_LEN - 1)] =
            BFFO_MAKE_TOKEN(next_token_id++, (uint16_t)epoch, val);
    }
}

/* -------------------------------------------------------------------------
 * Thread pool
 * -------------------------------------------------------------------------*/
typedef struct {
    uint32_t start;
    uint32_t end;
    uint64_t rng;
} WorkerArgs;

static WorkerArgs        worker_args[MAX_THREADS];
static pthread_barrier_t barrier_start;
static pthread_barrier_t barrier_end;
static volatile int      pool_shutdown = 0;
static int               g_nthreads    = 0;
static uint32_t          pair_steps[NPAIRS];

static void *worker_thread(void *arg) {
    WorkerArgs *a = (WorkerArgs *)arg;
    uint64_t combined[BFFO_TAPE_LEN];

    for (;;) {
        pthread_barrier_wait(&barrier_start);
        if (pool_shutdown) break;

        uint64_t rng = a->rng;
        for (uint32_t i = a->start; i < a->end; i++) {
            uint32_t ai = perm[i];
            uint32_t bi = perm[i + NPAIRS];

            memcpy(combined,                  soup[ai], BFFO_HALF_LEN * sizeof(uint64_t));
            memcpy(combined + BFFO_HALF_LEN,  soup[bi], BFFO_HALF_LEN * sizeof(uint64_t));

            /* head0 and head1 are random per pair */
            uint8_t h0 = (uint8_t)(xorshift64(&rng) & (BFFO_TAPE_LEN - 1));
            uint8_t h1 = (uint8_t)(xorshift64(&rng) & (BFFO_TAPE_LEN - 1));

            pair_steps[i] = bffo_run(combined, h0, h1);

            memcpy(soup[ai], combined,                  BFFO_HALF_LEN * sizeof(uint64_t));
            memcpy(soup[bi], combined + BFFO_HALF_LEN,  BFFO_HALF_LEN * sizeof(uint64_t));
        }

        pthread_barrier_wait(&barrier_end);
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Run one epoch: shuffle, seed workers, sync
 * -------------------------------------------------------------------------*/
static void soup_epoch(void) {
    shuffle_perm();
    for (int t = 0; t < g_nthreads; t++)
        worker_args[t].rng = xorshift64(&global_rng);
    pthread_barrier_wait(&barrier_start);
    pthread_barrier_wait(&barrier_end);
}

/* -------------------------------------------------------------------------
 * Comparator for qsort
 * -------------------------------------------------------------------------*/
static int cmp_uint32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* -------------------------------------------------------------------------
 * Statistics
 * -------------------------------------------------------------------------*/
static void soup_stats(double *mean_out, double *median_out, uint32_t *unique_out,
                       uint32_t *modal_id_out, uint32_t *modal_count_out,
                       char rep_str[BFFO_HALF_LEN + 1]) {
    uint32_t freq[BFFO_HALF_LEN + 1];
    memset(freq, 0, sizeof(freq));

    uint64_t total = 0;
    for (uint32_t i = 0; i < SOUP_SIZE; i++) {
        int ops = bffo_count_ops(soup[i]);
        freq[ops]++;
        total += (uint64_t)ops;
    }

    *mean_out = (double)total / SOUP_SIZE;

    uint32_t pos_lo = SOUP_SIZE / 2 - 1;
    uint32_t pos_hi = SOUP_SIZE / 2;
    uint32_t cumul  = 0;
    int lo_val = -1, hi_val = -1;
    for (int v = 0; v <= BFFO_HALF_LEN; v++) {
        cumul += freq[v];
        if (lo_val < 0 && cumul > pos_lo) lo_val = v;
        if (hi_val < 0 && cumul > pos_hi) hi_val = v;
        if (lo_val >= 0 && hi_val >= 0) break;
    }
    *median_out = (lo_val + hi_val) / 2.0;

    static uint32_t ids[SOUP_SIZE * BFFO_HALF_LEN];
    uint32_t n = 0;
    for (uint32_t i = 0; i < SOUP_SIZE; i++)
        for (int j = 0; j < BFFO_HALF_LEN; j++)
            ids[n++] = BFFO_TOKEN_ID(soup[i][j]);
    qsort(ids, n, sizeof(uint32_t), cmp_uint32);
    uint32_t unique = 0;
    for (uint32_t i = 0; i < n; i++)
        if (i == 0 || ids[i] != ids[i - 1]) unique++;
    *unique_out = unique;

    uint32_t modal_id = ids[0], modal_count = 0;
    uint32_t cur_id = ids[0], cur_count = 1;
    for (uint32_t i = 1; i < n; i++) {
        if (ids[i] == cur_id) {
            cur_count++;
        } else {
            if (cur_count > modal_count) { modal_count = cur_count; modal_id = cur_id; }
            cur_id = ids[i];
            cur_count = 1;
        }
    }
    if (cur_count > modal_count) { modal_count = cur_count; modal_id = cur_id; }
    *modal_id_out    = modal_id;
    *modal_count_out = modal_count;

    uint32_t best_tape = 0, best_count = 0;
    for (uint32_t i = 0; i < SOUP_SIZE; i++) {
        uint32_t cnt = 0;
        for (int j = 0; j < BFFO_HALF_LEN; j++)
            cnt += (BFFO_TOKEN_ID(soup[i][j]) == modal_id);
        if (cnt > best_count) { best_count = cnt; best_tape = i; }
    }

    for (int j = 0; j < BFFO_HALF_LEN; j++) {
        uint8_t ch = BFFO_TOKEN_CHAR(soup[best_tape][j]);
        rep_str[j] = SOUP_IS_OP[ch] ? (char)ch : ' ';
    }
    rep_str[BFFO_HALF_LEN] = '\0';
}

/* -------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    int      epochs         = 10000;
    int      nthreads       = 0;
    uint64_t seed           = 0;
    int      stats_interval = 100;
    double   mutation_rate  = 0.0;
    const char *runlog_path = NULL;

    for (int i = 1; i < argc - 1; i++) {
        if      (!strcmp(argv[i], "--epochs"))   epochs         = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads"))  nthreads       = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed"))     seed           = (uint64_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--stats"))    stats_interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mutation")) mutation_rate  = strtod(argv[++i], NULL);
        else if (!strcmp(argv[i], "--runlog"))   runlog_path    = argv[++i];
        else { fprintf(stderr, "Unknown argument: %s\n", argv[i]); return 1; }
    }

    if (nthreads <= 0) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = (cpus > 1) ? (int)cpus : 1;
    }
    if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;

    global_rng = seed ? seed : (uint64_t)(uintptr_t)&global_rng ^ 0xdeadbeefcafe1234ULL;
    for (int i = 0; i < 32; i++) xorshift64(&global_rng);

    /* Initialise soup: each element is a fresh token with a unique ID */
    for (uint32_t i = 0; i < SOUP_SIZE; i++)
        for (int j = 0; j < BFFO_HALF_LEN; j++) {
            uint8_t ch = (uint8_t)(xorshift64(&global_rng) & 0xFF);
            soup[i][j] = BFFO_MAKE_TOKEN(next_token_id++, 0, ch);
        }

    fprintf(stderr, "BFF-orig soup: %d tapes x %d bytes, %d epochs, %d threads, "
                    "stats every %d, mutation rate %.2g\n",
            SOUP_SIZE, BFFO_HALF_LEN, epochs, nthreads, stats_interval, mutation_rate);
    fprintf(stderr, "Seed: %llu\n", (unsigned long long)global_rng);

    g_nthreads = nthreads;
    uint32_t chunk = NPAIRS / (uint32_t)nthreads;
    for (int t = 0; t < nthreads; t++) {
        worker_args[t].start = (uint32_t)t * chunk;
        worker_args[t].end   = (t == nthreads - 1) ? NPAIRS : worker_args[t].start + chunk;
        worker_args[t].rng   = 0;
    }

    pthread_barrier_init(&barrier_start, NULL, (unsigned)(nthreads + 1));
    pthread_barrier_init(&barrier_end,   NULL, (unsigned)(nthreads + 1));

    pthread_t tids[MAX_THREADS];
    for (int t = 0; t < nthreads; t++)
        pthread_create(&tids[t], NULL, worker_thread, &worker_args[t]);

    FILE *runlog = NULL;
    if (runlog_path) {
        runlog = fopen(runlog_path, "wb");
        if (!runlog) { perror(runlog_path); return 1; }
        fprintf(stderr, "Run-length log: %s\n", runlog_path);
    }

    double mean, median;
    uint32_t unique, modal_id, modal_count;
    char rep_str[BFFO_HALF_LEN + 1];
    printf("%-10s\t%-12s\t%-12s\t%-12s\t%-12s\t%-12s\t%-10s\t%s\n",
           "epoch", "mean_ops", "median_ops", "mean_steps", "max_steps",
           "unique_ids", "modal_id", "representative_tape (modal_count)");
    soup_stats(&mean, &median, &unique, &modal_id, &modal_count, rep_str);
    printf("%-10d\t%-12.4f\t%-12.1f\t%-12.1f\t%-12u\t%-12u\t%-10u\t|%s| (%u)\n",
           0, mean, median, 0.0, 0u, unique, modal_id, rep_str, modal_count);
    fflush(stdout);

    for (int epoch = 1; epoch <= epochs; epoch++) {
        soup_epoch();
        mutate_soup(mutation_rate, epoch);
        if (runlog)
            fwrite(pair_steps, sizeof(uint32_t), NPAIRS, runlog);
        if (epoch % stats_interval == 0) {
            double step_sum = 0.0;
            uint32_t step_max = 0;
            for (uint32_t i = 0; i < NPAIRS; i++) {
                step_sum += pair_steps[i];
                if (pair_steps[i] > step_max) step_max = pair_steps[i];
            }
            double mean_steps = step_sum / NPAIRS;
            soup_stats(&mean, &median, &unique, &modal_id, &modal_count, rep_str);
            printf("%-10d\t%-12.4f\t%-12.1f\t%-12.1f\t%-12u\t%-12u\t%-10u\t|%s| (%u)\n",
                   epoch, mean, median, mean_steps, step_max,
                   unique, modal_id, rep_str, modal_count);
            fflush(stdout);
        }
    }

    if (runlog) fclose(runlog);

    pool_shutdown = 1;
    pthread_barrier_wait(&barrier_start);
    for (int t = 0; t < nthreads; t++)
        pthread_join(tids[t], NULL);
    pthread_barrier_destroy(&barrier_start);
    pthread_barrier_destroy(&barrier_end);

    return 0;
}
