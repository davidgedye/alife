# Experiment 1: Longest-Lived Random Brainfuck Program

## Setup

We generated **1,000,000 random Brainfuck programs** and ran them through a parallel
interpreter to find the one that executed the most steps before halting.

**Constraints:**
- Program length: 1–128 bytes, drawn uniformly at random
- Opcodes sampled uniformly from all 8 BF characters: `+ - > < . , [ ]`
- Step limit: 1,000,000 (programs exceeding this are classified as "timed out")
- `,` (read input) treated as a **no-op** — no stdin in batch mode
- Tape: 256 bytes (uint8_t, wraps), pointer starts at cell 128

## Population results

| Category | Count |
|---|---|
| Halted normally | 41,931 (4.2%) |
| Timed out (>1M steps) | 5,716 (0.6%) |
| Malformed (unmatched brackets) | 953,666 (95.4%) |

The high malformation rate is expected: `[` and `]` are 2 of 8 opcodes, so ~25% of
characters are brackets, and random strings are rarely balanced.

## Winner

```
>,>,.>,+[.+>+<-,>,-><,+]-
```

- **Length:** 25 bytes
- **Steps:** 979,210
- **Output:** `\x00` followed by 127 × `\x01` (128 bytes total, filling the output buffer)

## Analysis

### Stripping the no-ops

Since `,` is a no-op, the program's effective behaviour is:

```
> > . > + [ . + > + < - > - > < + ] -
```

### Setup phase

Starting with all tape cells at 0 and the data pointer at dp=128:

```
> >    → dp = 130
.      → output tape[130] = 0x00
>      → dp = 131
+      → tape[131] = 1
[      → tape[131] ≠ 0, enter loop
```

### Loop body

Each iteration begins with the data pointer at cell d and tape[d] = 1.

| Instruction | Effect |
|---|---|
| `.` | output tape[d] |
| `+` | tape[d] → 2 |
| `>` | dp = d+1 |
| `+` | tape[d+1]++ |
| `<` | dp = d |
| `-` | tape[d] → 1 |
| `>` | dp = d+1 |
| `-` | tape[d+1]-- |
| `>` | dp = d+2 |
| `<` | dp = d+1 |
| `+` | tape[d+1]++ |
| `]` | check tape[d+1] |

**Net effect per iteration:**
- tape[d]: unchanged (incremented then decremented)
- tape[d+1]: net **+1**
- dp: advances from d to **d+1**
- `]` checks tape[d+1], which is now ≥ 1 → loop continues

### Why it keeps running

The data pointer advances one cell rightward per iteration. Each "next" cell has just been
set to at least 1, so `]` always sees a nonzero value and loops back.

Note: because `]` jumps to the instruction *after* `[` (not `[` itself), the opening
bracket is only evaluated once at loop entry. The loop condition is checked exclusively
at `]`, at the *end* of each iteration.

### Why it eventually halts: the lap counter

The tape is 256 bytes and the pointer is a `uint8_t`, so it wraps: after cell 255 comes
cell 0. After **256 iterations** the pointer returns to cell 131 — the cell that was
initialised to 1 before the loop.

Each time the pointer laps past cell 131 (as the "d+1" cell), the loop body adds a net
**+1** to it. Cell 131 started at 1, so after k full laps its value is:

```
tape[131] = (1 + k) mod 256
```

After **255 laps**, tape[131] = (1 + 255) mod 256 = **0**. The `]` instruction sees 0
and exits the loop.

### Step count

| Phase | Count |
|---|---|
| Setup (before loop) | 9 steps |
| Loop body: 255 laps × 256 iterations × 15 instructions | 979,200 steps |
| Post-loop (`-`) | 1 step |
| **Total** | **979,210 steps** |

This matches the interpreter's measurement exactly.

### Output

The first output byte is `0x00` from the setup `.` on tape[130].

Inside the loop, each iteration outputs tape[d] at the moment of the `.` instruction.
On the first lap (iterations 1–256), each cell is visited fresh with value 1, so the
loop outputs `0x01` repeatedly. The output buffer holds 128 bytes total; after the
initial `0x00`, 127 × `0x01` fill it before it is exhausted.

(On subsequent laps, cells would output higher values as they accumulate increments —
but the buffer is already full by then.)

### Summary

The winning program is an accidental **mod-256 lap counter**. The data pointer cycles
around the 256-cell tape, and the "pivot" cell (131) tallies completed laps in modular
arithmetic. The loop terminates precisely when the lap count overflows: after
255 × 256 = 65,280 loop iterations and 979,210 total steps.

It is remarkable that this behaviour emerged from a random 25-byte string, and that
such a compact program manages to be one of the longest-lived among a million candidates.
