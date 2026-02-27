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
/* IP starts here; bytes 0-1 are reserved for head positions */
#define BFF_IP_START      2

/*
 * Run the BFF interpreter on a 128-byte combined tape.
 *
 * The tape is modified in place (instructions and data share the same tape).
 * tape[0] encodes the starting position of head0 (read head, explicit control).
 * tape[1] encodes the starting position of head1 (write head, auto-advances).
 *
 * head0: controlled explicitly by '<' (−1) and '>' (+1). Does not auto-advance.
 * head1: auto-advances by +1 after every instruction.
 * Both wrap modulo BFF_TAPE_LEN.
 *
 * Instruction set:
 *   '<'  head0 -= 1 (wraps)
 *   '>'  head0 += 1 (wraps)
 *   '+'  tape[head0]++
 *   '-'  tape[head0]--
 *   ','  tape[head1] = tape[head0]; head1 += 1  (copy and advance write head)
 *   '['  push IP to stack (unconditionally)
 *   ']'  if tape[head0]!=0: jump to stack top; else pop; empty stack: terminate
 *
 * Terminates on: step limit, ']' with empty stack, or stack overflow (depth 64).
 */
void bff_run(uint8_t tape[BFF_TAPE_LEN]);

/*
 * Count the number of valid BFF instruction bytes in a BFF_HALF_LEN-byte tape.
 * Valid instructions: < > + - , [ ]  (7 distinct byte values)
 */
int bff_count_ops(const uint8_t *half_tape);
