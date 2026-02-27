#include "bff.h"
#include <string.h>

/* Lookup table: 1 for valid BFF instruction bytes, 0 otherwise */
static const uint8_t IS_OP[256] = {
    ['<']=1, ['>']=1, ['{']=1, ['}']=1,
    ['+']=1, ['-']=1, ['.']=1, [',']=1,
    ['[']=1, [']']=1,
};

void bff_run(uint8_t tape[BFF_TAPE_LEN], uint8_t head0, uint8_t head1) {
    uint8_t  ip    = 0;
    uint8_t  stack[BFF_STACK_DEPTH];
    uint8_t  sp    = 0;
    uint32_t steps = 0;

    while (steps < BFF_MAX_STEPS) {
        steps++;
        switch (tape[ip]) {

        case '<': head0 = (head0 - 1) & (BFF_TAPE_LEN - 1); break;
        case '>': head0 = (head0 + 1) & (BFF_TAPE_LEN - 1); break;
        case '{': head1 = (head1 - 1) & (BFF_TAPE_LEN - 1); break;
        case '}': head1 = (head1 + 1) & (BFF_TAPE_LEN - 1); break;
        case '+': tape[head0]++; break;
        case '-': tape[head0]--; break;
        case '.': tape[head1] = tape[head0]; break;  /* write head0 value to head1 pos */
        case ',': tape[head0] = tape[head1]; break;  /* read head1 value into head0 pos */

        case '[':
            if (sp >= BFF_STACK_DEPTH) return;  /* stack overflow: terminate */
            stack[sp++] = ip;                   /* push '[' position unconditionally */
            break;

        case ']':
            if (sp == 0) return;         /* empty stack: terminate */
            if (tape[head0] != 0) {
                ip = stack[sp - 1];      /* loop: jump to '['; ip++ enters loop body */
            } else {
                sp--;                    /* exit loop */
            }
            break;

        default:
            break;                       /* all other bytes are no-ops */
        }

        ip = (ip + 1) & (BFF_TAPE_LEN - 1);
    }
    /* Step limit reached: silent termination */
}

int bff_count_ops(const uint8_t *half_tape) {
    int n = 0;
    for (int i = 0; i < BFF_HALF_LEN; i++)
        n += IS_OP[half_tape[i]];
    return n;
}
