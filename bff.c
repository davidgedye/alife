#include "bff.h"
#include <string.h>

/* Lookup table: 1 for valid BFF instruction bytes, 0 otherwise */
static const uint8_t IS_OP[256] = {
    ['<']=1, ['>']=1,
    ['+']=1, ['-']=1, [',']=1,
    ['[']=1, [']']=1,
};

uint32_t bff_run(uint64_t tape[BFF_TAPE_LEN]) {
    uint8_t  ip    = BFF_IP_START;
    uint8_t  head0 = TOKEN_CHAR(tape[0]) & (BFF_TAPE_LEN - 1);  /* read head: explicit control */
    uint8_t  head1 = TOKEN_CHAR(tape[1]) & (BFF_TAPE_LEN - 1);  /* write head: auto-advances  */
    uint8_t  stack[BFF_STACK_DEPTH];
    uint8_t  sp    = 0;
    uint32_t steps = 0;

    while (steps < BFF_MAX_STEPS) {
        steps++;
        switch (TOKEN_CHAR(tape[ip])) {

        case '<': head0 = (head0 - 1) & (BFF_TAPE_LEN - 1); break;
        case '>': head0 = (head0 + 1) & (BFF_TAPE_LEN - 1); break;
        case '+': tape[head0] = (tape[head0] & ~0xFFULL) | ((TOKEN_CHAR(tape[head0]) + 1) & 0xFF); break;
        case '-': tape[head0] = (tape[head0] & ~0xFFULL) | ((TOKEN_CHAR(tape[head0]) - 1) & 0xFF); break;
        case ',':
            tape[head1] = tape[head0];                    /* copy full token read→write */
            head1 = (head1 + 1) & (BFF_TAPE_LEN - 1);   /* write head advances on write */
            break;

        case '[':
            if (sp >= BFF_STACK_DEPTH) return steps;  /* stack overflow: terminate */
            stack[sp++] = ip;                          /* push unconditionally */
            break;

        case ']':
            if (sp == 0) return steps;               /* empty stack: terminate */
            if (TOKEN_CHAR(tape[head0]) != 0) {
                ip = stack[sp - 1];                  /* loop: jump to '[' */
            } else {
                sp--;                                /* exit loop */
            }
            break;

        default:
            break;
        }

        ip = (ip + 1) & (BFF_TAPE_LEN - 1);
    }
    return steps;  /* step limit reached */
}

int bff_count_ops(const uint64_t *half_tape) {
    int n = 0;
    for (int i = 0; i < BFF_HALF_LEN; i++)
        n += IS_OP[TOKEN_CHAR(half_tape[i])];
    return n;
}
