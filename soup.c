#define _POSIX_C_SOURCE 200809L

#include "bff.h"

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
static uint8_t  soup[SOUP_SIZE][BFF_HALF_LEN];
static uint32_t perm[SOUP_SIZE];  /* shuffle buffer for pairing */

/* -------------------------------------------------------------------------
 * XorShift64 RNG (one global + per-thread variants seeded from it)
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
 * Mutation: Poisson-sampled random byte flips across the whole soup.
 *
 * Expected mutations per epoch = SOUP_SIZE * BFF_HALF_LEN * rate (~8 at 1e-6).
 * We draw k ~ Poisson(lambda) using Knuth's method, then scatter k writes at
 * uniformly random positions. Much cheaper than rolling a coin per byte.
 *
 * Position encoding: the flat index i in [0, SOUP_SIZE*BFF_HALF_LEN) maps to
 * soup[i / BFF_HALF_LEN][i % BFF_HALF_LEN]. Since both dimensions are powers
 * of two (2^17 and 2^6), bit ops replace division/modulo.
 * -------------------------------------------------------------------------*/
#define SOUP_TOTAL_BYTES  ((uint32_t)(SOUP_SIZE) * BFF_HALF_LEN)  /* 2^23 */
#define SOUP_BYTE_MASK    (SOUP_TOTAL_BYTES - 1)                   /* 0x7FFFFF */

static void mutate_soup(double rate) {
    if (rate <= 0.0) return;

    /* Sample k ~ Poisson(lambda) via Knuth's algorithm */
    double lambda = SOUP_TOTAL_BYTES * rate;
    double L = exp(-lambda);
    double p = 1.0;
    uint32_t k = 0;
    do {
        k++;
        /* uniform in (0,1]: top 53 bits of RNG output */
        p *= (double)(xorshift64(&global_rng) >> 11) * (1.0 / (double)(1ULL << 53));
    } while (p > L);
    k--;

    /* Apply k mutations at uniformly random positions */
    for (uint32_t m = 0; m < k; m++) {
        uint64_t r   = xorshift64(&global_rng);
        uint32_t pos = (uint32_t)(r >> 41) & SOUP_BYTE_MASK; /* 23-bit index */
        uint8_t  val = (uint8_t)(r & 0xFF);
        soup[pos >> 6][pos & (BFF_HALF_LEN - 1)] = val;
    }
}

/* -------------------------------------------------------------------------
 * Thread pool
 * -------------------------------------------------------------------------*/
typedef struct {
    uint32_t start;
    uint32_t end;
} WorkerArgs;

static WorkerArgs        worker_args[MAX_THREADS];
static pthread_barrier_t barrier_start;
static pthread_barrier_t barrier_end;
static volatile int      pool_shutdown = 0;
static int               g_nthreads    = 0;

static void *worker_thread(void *arg) {
    WorkerArgs *a = (WorkerArgs *)arg;
    uint8_t combined[BFF_TAPE_LEN];

    for (;;) {
        pthread_barrier_wait(&barrier_start);
        if (pool_shutdown) break;

        for (uint32_t i = a->start; i < a->end; i++) {
            uint32_t ai = perm[i];
            uint32_t bi = perm[i + NPAIRS];

            memcpy(combined,                soup[ai], BFF_HALF_LEN);
            memcpy(combined + BFF_HALF_LEN, soup[bi], BFF_HALF_LEN);

            bff_run(combined);  /* head positions read from combined[0] and combined[1] */

            memcpy(soup[ai], combined,                BFF_HALF_LEN);
            memcpy(soup[bi], combined + BFF_HALF_LEN, BFF_HALF_LEN);
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
    pthread_barrier_wait(&barrier_start);  /* release workers */
    pthread_barrier_wait(&barrier_end);    /* wait for completion */
}

/* -------------------------------------------------------------------------
 * Statistics: mean and median op count across all tapes
 * -------------------------------------------------------------------------*/
static void soup_stats(double *mean_out, double *median_out) {
    uint32_t freq[BFF_HALF_LEN + 1];
    memset(freq, 0, sizeof(freq));

    uint64_t total = 0;
    for (uint32_t i = 0; i < SOUP_SIZE; i++) {
        int ops = bff_count_ops(soup[i]);
        freq[ops]++;
        total += (uint64_t)ops;
    }

    *mean_out = (double)total / SOUP_SIZE;

    /* Median via counting sort */
    uint32_t pos_lo = SOUP_SIZE / 2 - 1;
    uint32_t pos_hi = SOUP_SIZE / 2;
    uint32_t cumul  = 0;
    int lo_val = -1, hi_val = -1;
    for (int v = 0; v <= BFF_HALF_LEN; v++) {
        cumul += freq[v];
        if (lo_val < 0 && cumul > pos_lo) lo_val = v;
        if (hi_val < 0 && cumul > pos_hi) hi_val = v;
        if (lo_val >= 0 && hi_val >= 0) break;
    }
    *median_out = (lo_val + hi_val) / 2.0;
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

    for (int i = 1; i < argc - 1; i++) {
        if      (!strcmp(argv[i], "--epochs"))   epochs         = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads"))  nthreads       = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed"))     seed           = (uint64_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--stats"))    stats_interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mutation")) mutation_rate  = strtod(argv[++i], NULL);
        else { fprintf(stderr, "Unknown argument: %s\n", argv[i]); return 1; }
    }

    if (nthreads <= 0) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = (cpus > 1) ? (int)cpus : 1;
    }
    if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;

    global_rng = seed ? seed : (uint64_t)(uintptr_t)&global_rng ^ 0xdeadbeefcafe1234ULL;
    for (int i = 0; i < 32; i++) xorshift64(&global_rng);

    /* Initialise soup: uniform random bytes */
    for (uint32_t i = 0; i < SOUP_SIZE; i++)
        for (int j = 0; j < BFF_HALF_LEN; j++)
            soup[i][j] = (uint8_t)(xorshift64(&global_rng) & 0xFF);

    fprintf(stderr, "BFF soup: %d tapes x %d bytes, %d epochs, %d threads, stats every %d, mutation rate %.2g\n",
            SOUP_SIZE, BFF_HALF_LEN, epochs, nthreads, stats_interval, mutation_rate);
    fprintf(stderr, "Seed: %llu\n", (unsigned long long)global_rng);

    /* Set up thread pool */
    g_nthreads = nthreads;
    uint32_t chunk = NPAIRS / (uint32_t)nthreads;
    for (int t = 0; t < nthreads; t++) {
        worker_args[t].start = (uint32_t)t * chunk;
        worker_args[t].end   = (t == nthreads - 1) ? NPAIRS : worker_args[t].start + chunk;
    }

    pthread_barrier_init(&barrier_start, NULL, (unsigned)(nthreads + 1));
    pthread_barrier_init(&barrier_end,   NULL, (unsigned)(nthreads + 1));

    pthread_t tids[MAX_THREADS];
    for (int t = 0; t < nthreads; t++)
        pthread_create(&tids[t], NULL, worker_thread, &worker_args[t]);

    /* Run */
    double mean, median;
    printf("%-10s\t%-12s\t%s\n", "epoch", "mean_ops", "median_ops");
    soup_stats(&mean, &median);
    printf("%-10d\t%-12.4f\t%.1f\n", 0, mean, median);
    fflush(stdout);

    for (int epoch = 1; epoch <= epochs; epoch++) {
        soup_epoch();
        mutate_soup(mutation_rate);
        if (epoch % stats_interval == 0) {
            soup_stats(&mean, &median);
            printf("%-10d\t%-12.4f\t%.1f\n", epoch, mean, median);
            fflush(stdout);
        }
    }

    /* Shut down thread pool */
    pool_shutdown = 1;
    pthread_barrier_wait(&barrier_start);
    for (int t = 0; t < nthreads; t++)
        pthread_join(tids[t], NULL);
    pthread_barrier_destroy(&barrier_start);
    pthread_barrier_destroy(&barrier_end);

    return 0;
}
