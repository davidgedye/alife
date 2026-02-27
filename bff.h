#pragma once

#include <stdint.h>

/* Combined tape length (two 64-byte programs concatenated) */
#define BFF_TAPE_LEN    128
/* Single program length */
#define BFF_HALF_LEN     64
/* Step limit per execution: 2^13 */
#define BFF_MAX_STEPS  8192
/* Maximum bracket nesting depth */
#define BFF_STACK_DEPTH  64

/*
 * Run the BFF interpreter on a 128-byte combined tape.
 *
 * The tape is modified in place (instructions and data share the same tape).
 * head0 and head1 are the initial positions of the two data heads [0..127].
 * The instruction pointer always starts at 0.
 * All pointers wrap modulo BFF_TAPE_LEN.
 *
 * Terminates after BFF_MAX_STEPS steps, on an unmatched ']' with empty stack,
 * on a '[' whose matching ']' cannot be found, or on stack overflow.
 */
void bff_run(uint8_t tape[BFF_TAPE_LEN], uint8_t head0, uint8_t head1);

/*
 * Count the number of valid BFF instruction bytes in a BFF_HALF_LEN-byte tape.
 * Valid instructions: < > { } + - . , [ ]  (10 distinct byte values)
 */
int bff_count_ops(const uint8_t *half_tape);
