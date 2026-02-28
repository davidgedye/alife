#pragma once

#include <stdint.h>

/* Combined tape length (two 64-byte programs concatenated) */
#define BFFO_TAPE_LEN    128
/* Single program length */
#define BFFO_HALF_LEN     64
/* Step limit per execution: 2^13 */
#define BFFO_MAX_STEPS   8192
/* Maximum bracket nesting depth */
#define BFFO_STACK_DEPTH  64

/*
 * Token format: [id:32][epoch:16][reserved:8][char:8]
 *
 * Same token semantics as the current BFF variant.
 * Arithmetic (+/-) modifies only the char field; id and epoch are preserved.
 * Copy (. and ,) propagates the entire 64-bit token.
 */
#define BFFO_TOKEN_CHAR(t)           ((uint8_t)((t) & 0xFF))
#define BFFO_TOKEN_EPOCH(t)          ((uint16_t)(((t) >> 16) & 0xFFFF))
#define BFFO_TOKEN_ID(t)             ((uint32_t)((t) >> 32))
#define BFFO_MAKE_TOKEN(id, ep, ch)  (((uint64_t)(uint32_t)(id) << 32) \
                                     | ((uint64_t)(uint16_t)(ep) << 16) \
                                     | (uint8_t)(ch))

/*
 * Run the original 10-instruction BFF interpreter on a 128-element token tape.
 *
 * The tape is modified in place.  head0 and head1 are passed in as explicit
 * parameters (randomly chosen by the caller); they are NOT read from the tape.
 * The instruction pointer always starts at 0 (no reserved cells).
 *
 * Instruction set (dispatched on BFFO_TOKEN_CHAR of each tape element):
 *   '<'  head0 -= 1 (wraps)
 *   '>'  head0 += 1 (wraps)
 *   '{'  head1 -= 1 (wraps)
 *   '}'  head1 += 1 (wraps)
 *   '+'  BFFO_TOKEN_CHAR(tape[head0])++  (id/epoch preserved)
 *   '-'  BFFO_TOKEN_CHAR(tape[head0])--  (id/epoch preserved)
 *   '.'  tape[head1] = tape[head0]  (copy full token; head1 does NOT advance)
 *   ','  tape[head0] = tape[head1]  (copy full token; head0 does NOT advance)
 *   '['  push IP to stack (unconditionally)
 *   ']'  if BFFO_TOKEN_CHAR(tape[head0])!=0: jump to stack top; else pop;
 *        empty stack: terminate
 *
 * Terminates on: step limit, ']' with empty stack, or stack overflow.
 * Returns the number of steps executed.
 */
uint32_t bffo_run(uint64_t tape[BFFO_TAPE_LEN], uint8_t head0, uint8_t head1);

/*
 * Count the number of valid BFF instruction bytes in a BFFO_HALF_LEN-element tape.
 * Valid instructions: < > { } + - . , [ ]  (10 distinct byte values)
 */
int bffo_count_ops(const uint64_t *half_tape);
