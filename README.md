# Artificial Life Experiments

Replication of Agüera y Arcas et al., "Computational Life: How Well-formed, Self-replicating
Programs Emerge from Simple Interaction" (arXiv 2406.19108).

---

## Experiments

### Experiment 1 — Longest-lived random Brainfuck program

See [experiment1.md](experiment1.md).

One million random BF programs (length 1–128, opcodes only). The winner — a 25-byte accidental
mod-256 lap counter — ran for 979,210 steps before halting.

### Experiment 2 — Random 8-bit programs

See [experiment2.md](experiment2.md).

One million 64-byte programs with bytes drawn uniformly from all 256 values (instruction density
~2 ops/program). ~50% execute zero steps; ~0.17% exceed 1M steps.

### Experiment 3 — BFF primordial soup

See [experiment3.md](experiment3.md) for earlier notes. Results and findings summarised below.

---

## The BFF Language (10-instruction variant)

An extension of Brainfuck where instructions and data share a single 128-byte tape.
Three pointers: instruction pointer (IP), head0, head1.

| Instruction | Effect |
|-------------|--------|
| `<` | head0 -= 1 (wraps mod 128) |
| `>` | head0 += 1 (wraps mod 128) |
| `{` | head1 -= 1 (wraps mod 128) |
| `}` | head1 += 1 (wraps mod 128) |
| `+` | tape[head0]++ (char field only) |
| `-` | tape[head0]-- (char field only) |
| `.` | tape[head1] = tape[head0]  (copy full token; head1 does not advance) |
| `,` | tape[head0] = tape[head1]  (copy full token; head0 does not advance) |
| `[` | push IP to stack unconditionally |
| `]` | if tape[head0] != 0: jump to stack top; else pop; empty stack: terminate |

**Key semantics:**
- `[` always pushes (no forward-scan). Only `]` tests the condition.
- IP starts at 0. **IP terminates when it advances past position 127** (does not wrap).
- head0 and head1 are random per interaction, passed as parameters.
- Step limit: 2^13 = 8192. Stack depth limit: 64.

---

## Token Format

Each tape cell is a `uint64_t`: `[id:32][epoch:16][reserved:8][char:8]`.

- `+`/`-` modify only the char field; id and epoch are preserved.
- `.` and `,` copy the full 64-bit token (char + id + epoch).
- `id` is a unique 32-bit identifier assigned at initialisation or mutation.
- `epoch` records when the token was created.

Lineage tracking: as `,`/`.` propagate tokens through the soup, unique ID counts measure
how many distinct lineages survive.

---

## Soup Setup

- **Population:** 2^17 = 131,072 tapes × 64 cells, uniform random initialisation.
- **Each epoch:** Fisher-Yates shuffle → random bijective pairing; every tape pairs once.
- **Interaction:** concatenate A||B into a 128-cell tape; run BFF; split at cell 64; write both
  halves back.
- **Mutation:** optional Poisson-sampled random byte flips (`--mutation <rate>`).
  Expected mutations/epoch = 8,388,608 × rate.
- **Thread pool:** persistent threads + pthread barriers. ~11 epochs/sec on 8 threads (WSL2).

---

## Implementation

| File | Purpose |
|------|---------|
| `bff_orig.h` / `bff_orig.c` | 10-instruction BFF interpreter |
| `soup_orig.c` | Primordial soup simulation (main experiment) |
| `bff.h` / `bff.c` | Simplified 7-instruction BFF (used in earlier exploration) |
| `soup.c` | Simplified soup (earlier exploration, now superseded) |
| `test_bff.c` | BFF interpreter correctness tests (20 cases) |
| `plot_stats.py` | Plot stats TSV output (ops, steps, unique IDs, modal lineage) |

**Build:** `make soup_orig` / `make test_bff`

**Run:** `./soup_orig --epochs N --seed S --mutation R --stats I`

**Stats output columns:** `epoch`, `mean_ops`, `median_ops`, `mean_steps`, `max_steps`,
`unique_ids`, `modal_id`, `representative_tape (modal_count)`

---

## Bug Found: IP Wrapping

The original `bff_orig.c` (and our derived `bff.c`) advanced the IP as:

```c
ip = (ip + 1) & (BFFO_TAPE_LEN - 1);  // wraps from 127 back to 0
```

This caused programs to execute multiple passes over the tape rather than terminating at the
end. This was never specified and is not described in the paper. We fixed it to:

```c
if (ip + 1 >= BFFO_TAPE_LEN) return steps;
ip++;
```

This had a large effect on soup dynamics: with IP wrapping, trivial all-comma tapes could
replicate in 4–5 epochs by using multiple passes to overwrite the partner tape. Without
wrapping, replication requires more sophisticated programs.

---

## Results

All runs use seed 12345.

### Baseline: no mutation, 5000 epochs (`soup` / 7-instruction BFF, reversed `,`)

After fixing IP wrapping and trying various `,` direction conventions, instruction density
declined slowly and the dominant strategy was a tape consisting entirely of zeros (the
"empty tape" replicator). No interesting dynamics.

### soup_orig, mutation 1e-5, 30,000 epochs

~84 mutations/epoch across 8.4M cells.

| Epoch | mean_ops | median_ops | mean_steps | unique_ids | modal count |
|-------|----------|------------|------------|------------|-------------|
| 0 | 2.5 | 2.0 | — | 8,388,608 | 1 |
| 5,000 | 6.5 | 5.0 | ~720 | ~400,000 | ~8,000 |
| 10,000 | 7.7 | 6.0 | ~760 | ~100,000 | ~17,000 |
| 20,000 | 8.5 | 7.0 | ~780 | ~27,000 | ~25,000 |
| 30,000 | 8.7 | 7.0 | ~790 | ~23,000 | ~28,000 |

- Instruction density rises steadily and genuinely — selection favours more complex programs.
- Median steps up in discrete jumps, suggesting population-level phase transitions.
- Unique IDs collapse from 8.4M to ~23K — strong lineage consolidation.
- Modal lineage grows to ~28K copies (~21% of soup) but never triggers an explosive takeover.
- Max steps is always 8192 (step limit hit every epoch) throughout.

### soup_orig, mutation 1e-3, 30,000 epochs

~8,400 mutations/epoch.

- mean_ops reaches ~7.9 (slightly lower — mutation disrupts complex programs faster than
  selection builds them).
- Unique IDs stabilise at ~186K — mutation injects new IDs faster than copying consolidates them.
- Modal lineage only ~12K copies (~9%) with much higher volatility.
- No takeover event.

---

## What We Did Not Reproduce

**The paper reports a dramatic phase transition** in which soup complexity rises sharply and
a self-replicating program takes over, causing unique-ID counts to collapse and mean
instruction density to spike. In their figures, this appears as a sharp discontinuity
within a few thousand epochs.

**We did not observe this** in any of our runs:

- 30,000 epochs at mutation 1e-5: slow, steady rise in complexity; no phase change.
- 30,000 epochs at mutation 1e-3: similar trajectory with higher diversity; no phase change.
- Instruction density rose from ~2.5 to ~8.7 ops/tape but did not spike.
- Unique IDs declined slowly but did not collapse catastrophically.
- The dominant lineage grew gradually (linear, not exponential) and remained below ~25% of
  the soup.

**Possible explanations:**

1. **Seed dependence.** The paper reports ~40% of runs show emergence within 16,000 epochs.
   Seed 12345 may simply be an unlucky 60%. Multiple seeds would be needed to assess this.

2. **Parameter mismatch.** The exact mutation rate, step limit, soup size, and head
   initialisation used in the paper are not fully specified. Small differences can matter.

3. **Longer runs needed.** Emergence may require more than 30,000 epochs with these parameters.
