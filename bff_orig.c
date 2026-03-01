#include "bff_orig.h"

/* Lookup table: 1 for valid BFF instruction bytes, 0 otherwise */
static const uint8_t IS_OP[256] = {
    ['<']=1, ['>']=1, ['{']=1, ['}']=1,
    ['+']=1, ['-']=1, ['.']=1, [',']=1,
    ['[']=1, [']']=1,
};

uint32_t bffo_run(uint64_t tape[BFFO_TAPE_LEN], uint8_t head0, uint8_t head1) {
    uint8_t  ip    = 0;
    uint8_t  stack[BFFO_STACK_DEPTH];
    uint8_t  sp    = 0;
    uint32_t steps = 0;

    while (steps < BFFO_MAX_STEPS) {
        steps++;
        switch (BFFO_TOKEN_CHAR(tape[ip])) {

        case '<': head0 = (head0 - 1) & (BFFO_TAPE_LEN - 1); break;
        case '>': head0 = (head0 + 1) & (BFFO_TAPE_LEN - 1); break;
        case '{': head1 = (head1 - 1) & (BFFO_TAPE_LEN - 1); break;
        case '}': head1 = (head1 + 1) & (BFFO_TAPE_LEN - 1); break;
        case '+': tape[head0] = (tape[head0] & ~0xFFULL) | ((BFFO_TOKEN_CHAR(tape[head0]) + 1) & 0xFF); break;
        case '-': tape[head0] = (tape[head0] & ~0xFFULL) | ((BFFO_TOKEN_CHAR(tape[head0]) - 1) & 0xFF); break;
        case '.': tape[head1] = tape[head0]; break;  /* copy full token head0 → head1 */
        case ',': tape[head0] = tape[head1]; break;  /* copy full token head1 → head0 */

        case '[':
            if (sp >= BFFO_STACK_DEPTH) return steps;  /* stack overflow: terminate */
            stack[sp++] = ip;                           /* push unconditionally */
            break;

        case ']':
            if (sp == 0) return steps;                  /* empty stack: terminate */
            if (BFFO_TOKEN_CHAR(tape[head0]) != 0) {
                ip = stack[sp - 1];                     /* loop: jump to '[' */
            } else {
                sp--;                                   /* exit loop */
            }
            break;

        default:
            break;
        }

        if (ip + 1 >= BFFO_TAPE_LEN) return steps;
        ip++;
    }
    return steps;  /* step limit reached */
}

int bffo_count_ops(const uint64_t *half_tape) {
    int n = 0;
    for (int i = 0; i < BFFO_HALF_LEN; i++)
        n += IS_OP[BFFO_TOKEN_CHAR(half_tape[i])];
    return n;
}
