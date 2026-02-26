# Experiment 2: Random 8-bit Programs

## Setup

- **1,000,000 programs**, each exactly **64 bytes** long
- Each byte is chosen **uniformly at random from all 256 possible values**
- Bytes that are not BF instructions (`+ - > < . , [ ]`) are **no-ops** and do not count toward the step total
- Step limit: **1,000,000** instructions per program

This contrasts with Experiment 1, where programs were random-length sequences drawn only from the 8 BF opcodes.

## Results

```
Halted normally: 998,255
Timed out:         1,745
Zero steps:      507,084

=== Run length histogram ===
              0 | ################################################## 507084
      1 -       9 | ################################################ 491066
     10 -      99 |  2
    100 -     999 |  88
   1000 -    9999 |  3
  10000 -   99999 |  0
 100000 -  999999 |  12
       > 1000000 |  1745
```

## Analysis

### Expected instruction density

Only 8 of 256 byte values are BF instructions, so each byte has a **1/32 chance** of being effective. In a 64-byte program, the expected number of real instructions is:

> 64 × (8/256) = **2 instructions** per program on average

### The 0-step bucket (~50.7%)

Half of all programs execute zero BF instructions. This bucket has two components:

**1. Programs with no BF bytes at all.**
The probability that a single byte is a no-op is 248/256. For all 64 bytes to be no-ops:

> (248/256)^64 ≈ 12.8% → ~128,000 programs

**2. Programs with instructions but unbalanced brackets (~37.9%).**
The remaining ~379,000 programs in the 0-step bucket had at least one BF instruction byte, but their `[` and `]` bytes were unmatched, causing the bracket validator to reject the program before execution. With `[` and `]` each appearing at random, even a single pair has only a 50% chance of being in the right order, and the probability of all brackets being balanced falls off rapidly with more brackets.

### The 1–9 step bucket (~49.1%)

This is the largest non-zero bucket. These are programs that had at least one instruction but no unbalanced brackets — either because they had no brackets at all, or because all `[` instructions were skipped (the tape starts zeroed, so `[` always skips on first encounter unless preceded by incrementing instructions). With an average of only 2 effective instructions and the tape starting at zero, most such programs terminate in just a handful of steps.

### The gap at 10–99 and 1,000–9,999

Only 2 and 3 programs respectively landed in these buckets. This is not a true gap but a consequence of low statistics — programs in the 100–999 and 100,000–999,999 range are those that happened to contain a functional loop structure (instructions that increment the tape before a `[`), which requires an unlikely combination of bytes in the right order. The sample is too small to fill every bucket smoothly.

### Long-running programs (~0.18% timeout)

1,745 programs exceeded 1,000,000 steps. These contain at least one effective loop that cycles for a large number of iterations — a rare but non-negligible outcome even with random bytes.

### Comparison with Experiment 1

| | Experiment 1 | Experiment 2 |
|---|---|---|
| Program length | 1–128 bytes (uniform) | 64 bytes (fixed) |
| Byte space | 8 opcodes only | All 256 values |
| Malformed / zero-step | ~95.4% | ~50.7% |
| Peak bucket | 1–9 steps | 0 steps and 1–9 steps (near-equal) |
| Timeouts | ~0.46% | ~0.17% |

The much lower malformed rate in Experiment 2 reflects the lower density of bracket characters — with brackets at 2/256 rather than 2/8 of the byte space, most programs have no brackets at all and trivially pass validation.
