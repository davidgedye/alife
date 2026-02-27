#pragma once

#include <stdint.h>

/* Combined tape length (two 64-byte programs concatenated) */
#define BFF_TAPE_LEN    128
/* Single program length */
#define BFF_HALF_LEN     64
/* Step limit per execution: 2^14 */
#define BFF_MAX_STEPS  16384
/* Maximum bracket nesting depth */
#define BFF_STACK_DEPTH  64
/* IP starts here; bytes 0-1 are reserved for head positions */
#define BFF_IP_START      2

/*
 * Token format: [id:32][epoch:16][reserved:8][char:8]
 *
 * Each tape element is a 64-bit token.  The low 8 bits (char field) hold the
 * BFF byte value used for instruction dispatch and arithmetic.  The upper 56
 * bits carry lineage metadata:
 *   id    — unique 32-bit identifier assigned at creation (init or mutation)
 *   epoch — 16-bit epoch number at which the token was created
 *
 * Semantics:
 *   '+'  and '-' modify only the char field; id and epoch are preserved.
 *   ','  copies the entire 64-bit token (char + id + epoch) to tape[head1].
 *   All other instructions leave token metadata untouched.
 */
#define TOKEN_CHAR(t)           ((uint8_t)((t) & 0xFF))
#define TOKEN_EPOCH(t)          ((uint16_t)(((t) >> 16) & 0xFFFF))
#define TOKEN_ID(t)             ((uint32_t)((t) >> 32))
#define MAKE_TOKEN(id, ep, ch)  (((uint64_t)(uint32_t)(id) << 32) \
                                | ((uint64_t)(uint16_t)(ep) << 16) \
                                | (uint8_t)(ch))

/*
 * Run the BFF interpreter on a 128-element token tape.
 *
 * The tape is modified in place (instructions and data share the same tape).
 * TOKEN_CHAR(tape[0]) encodes the starting position of head0.
 * TOKEN_CHAR(tape[1]) encodes the starting position of head1.
 *
 * head0: controlled explicitly by '<' (−1) and '>' (+1). Does not auto-advance.
 * head1: auto-advances by +1 after every ',' instruction.
 * Both wrap modulo BFF_TAPE_LEN.
 *
 * Instruction set (dispatched on TOKEN_CHAR of each tape element):
 *   '<'  head0 -= 1 (wraps)
 *   '>'  head0 += 1 (wraps)
 *   '+'  TOKEN_CHAR(tape[head0])++  (id/epoch preserved)
 *   '-'  TOKEN_CHAR(tape[head0])--  (id/epoch preserved)
 *   ','  tape[head1] = tape[head0]; head1 += 1  (copy full token, advance write head)
 *   '['  push IP to stack (unconditionally)
 *   ']'  if TOKEN_CHAR(tape[head0])!=0: jump to stack top; else pop; empty stack: terminate
 *
 * Terminates on: step limit, ']' with empty stack, or stack overflow (depth 64).
 */
uint32_t bff_run(uint64_t tape[BFF_TAPE_LEN]);

/*
 * Count the number of valid BFF instruction bytes in a BFF_HALF_LEN-element token tape.
 * Valid instructions: < > + - , [ ]  (7 distinct byte values)
 */
int bff_count_ops(const uint64_t *half_tape);
