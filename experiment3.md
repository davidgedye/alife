# Experiment 3: BFF Primordial Soup

## Goal

Replicate the first experiment from Agüera y Arcas et al., "Computational Life: How Well-formed,
Self-replicating Programs Emerge from Simple Interaction" (arXiv 2406.19108).

The paper studies how self-replicating programs emerge spontaneously in a "primordial soup" of
randomly initialised programs that interact by concatenation and execution.

## The BFF Language

An extension of Brainfuck where instructions and data share a single tape (no separate I/O streams).
Three pointers operate on the same byte array: instruction pointer (IP), head0, head1.

| Instruction | Effect |
|-------------|--------|
| `<`         | head0 -= 1 (wraps) |
| `>`         | head0 += 1 (wraps) |
| `{`         | head1 -= 1 (wraps) |
| `}`         | head1 += 1 (wraps) |
| `+`         | tape[head0]++ |
| `-`         | tape[head0]-- |
| `.`         | tape[head1] = tape[head0] |
| `,`         | tape[head0] = tape[head1] |
| `[`         | if tape[head0]==0: skip to matching `]` |
| `]`         | if tape[head0]!=0: jump back to matching `[` |

All other byte values are no-ops. Instructions are ASCII-encoded. The null byte (0x00) is the
"true zero" useful for exiting loops. All pointers wrap modulo tape length.

**Bracket semantics:** fully dynamic (no static analysis — code is self-modifying).
- `[` with tape==0: scan forward at runtime for matching `]`; terminate if not found.
- `]` with empty stack: terminate.
- Step limit: 2^13 = 8192 steps per execution.
- Stack overflow (depth > 64): terminate.

## Soup Setup

- **Population:** 2^17 = 131,072 tapes, each 64 bytes, uniform random initialisation.
- **Each epoch:** randomly split all tapes into two halves of 2^16; pair each tape in the first
  half with a random tape from the second half. Every tape participates in exactly one interaction
  per epoch.
- **Interaction:** concatenate pair A||B into a 128-byte tape; initialise IP=0, head0 and head1
  at random positions in [0,127]; execute up to 2^13 steps; split result back at byte 64 and
  write both halves back to the soup.
- **No mutation** (not yet added).

## Implementation

Files:
- `bff.h` / `bff.c` — BFF interpreter (`bff_run`, `bff_count_ops`)
- `soup.c` — primordial soup simulation (main program)
- `test_bff.c` — BFF interpreter correctness tests (16 cases; `make test_bff`)

Build: `make soup`
Run: `./soup [epochs] [threads] [seed] [stats_interval]`

Output: CSV to stdout — `epoch,mean_ops,median_ops` every `stats_interval` epochs (default 100).
`mean_ops` and `median_ops` are the mean and median number of valid instruction bytes per 64-byte
tape across the entire soup. Initially ~2.5 (= 64 × 10/256).

### BFF semantics note

`[` **always pushes** to the stack regardless of `tape[head0]`. Only `]` checks the condition:
- `]` with `tape[head0] != 0`: jump back to matching `[`
- `]` with `tape[head0] == 0`: pop stack (exit loop)
- `]` with empty stack: terminate

This replaces the earlier forward-scan approach (which was incorrect for self-modifying code, since
bytes between `[` and the scanned `]` may be overwritten mid-run).

### Performance

Thread pool (persistent threads + `pthread_barrier_t`) replaces per-epoch `pthread_create/join`.
Throughput: ~11 epochs/second on 8 threads (WSL2). 50,000 epochs ≈ 75 minutes.

## Results So Far

Run: 5000 epochs, seed 12345, 8 threads.

| Phase | Epochs | mean_ops | median_ops |
|-------|--------|----------|------------|
| Random | 0 | 2.5 | 2.0 |
| Rise | 0–440 | 2.5 → 26.7 | 2 → 19 |
| Decay | 440–1200 | 26.7 → 16.1 | 19 → 12 |
| Plateau | 1200–5000 | ~15.6 (stable) | 12.0 (locked) |

The soup settles at ~24% instruction density (vs 3.9% for uniform random), a genuine attractor
well above the noise floor. No runaway self-replicator takeover observed yet — the paper suggests
~40% of BFF runs show emergence within 16,000 epochs.

## Next Steps

- Run to 50,000 epochs to check for self-replicator emergence (paper: ~40% of runs within 16k).
- Add tooling to inspect the most instruction-dense tapes at any epoch.
- Add background mutation (random byte replacements).
- Consider tracking tape identity/frequency to detect lineage dominance.
