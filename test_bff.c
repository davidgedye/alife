#include "bff.h"

#include <stdio.h>
#include <string.h>

/* Convention: program bytes go at the start of the tape (positions 0..N).
 * Data heads are placed at position 50 (head0) and 60 (head1) by default,
 * safely away from the program area, and initially zero. */
#define H0 50
#define H1 60

static int passed = 0, failed = 0;

static void check(const char *name, int cond) {
    if (cond) { printf("PASS: %s\n", name); passed++; }
    else       { printf("FAIL: %s\n", name); failed++; }
}

/* Zero the tape, then write a program string starting at position 0. */
static void make_tape(uint8_t tape[BFF_TAPE_LEN], const char *prog) {
    memset(tape, 0, BFF_TAPE_LEN);
    for (int i = 0; prog[i] && i < BFF_TAPE_LEN; i++)
        tape[i] = (uint8_t)prog[i];
}

int main(void) {
    uint8_t t[BFF_TAPE_LEN];

    /* -----------------------------------------------------------------------
     * Basic instructions
     * ----------------------------------------------------------------------- */

    /* '+' increments tape[head0] */
    make_tape(t, "+]");
    bff_run(t, H0, H1);
    check("'+' increments tape[head0]", t[H0] == 1);

    /* '-' decrements tape[head0] (wraps 0 -> 255) */
    make_tape(t, "-]");
    bff_run(t, H0, H1);
    check("'-' decrements tape[head0] (wraps to 255)", t[H0] == 255);

    /* '>' moves head0 right */
    make_tape(t, ">+]");
    bff_run(t, H0, H1);
    check("'>' moves head0 right", t[H0] == 0 && t[H0 + 1] == 1);

    /* '<' moves head0 left */
    make_tape(t, "<+]");
    bff_run(t, H0, H1);
    check("'<' moves head0 left", t[H0] == 0 && t[H0 - 1] == 1);

    /* '}' moves head1 right */
    make_tape(t, "+}.]");
    bff_run(t, H0, H1);
    check("'}' moves head1 right", t[H1 + 1] == 1);

    /* '{' moves head1 left */
    make_tape(t, "+{.]");
    bff_run(t, H0, H1);
    check("'{' moves head1 left", t[H1 - 1] == 1);

    /* '.' copies tape[head0] to tape[head1] */
    make_tape(t, "+.]");
    bff_run(t, H0, H1);
    check("'.' copies tape[head0] to tape[head1]", t[H0] == 1 && t[H1] == 1);

    /* ',' copies tape[head1] to tape[head0] */
    make_tape(t, ",]");
    t[H1] = 42;
    bff_run(t, H0, H1);
    check("',' copies tape[head1] to tape[head0]", t[H0] == 42);

    /* -----------------------------------------------------------------------
     * Termination conditions
     * ----------------------------------------------------------------------- */

    /* ']' with empty stack terminates immediately; tape is otherwise unmodified */
    make_tape(t, "]+");
    bff_run(t, H0, H1);
    check("']' with empty stack terminates (subsequent '+' not reached)", t[H0] == 0);

    /* Step limit: '-' at ip=0, rest no-ops. ip=0 is visited 8192/128=64 times.
     * Decrementing from 0 gives values 255,254,...,192 — all above any BFF
     * instruction byte (max is '}'=125), so self-modification never fires. */
    make_tape(t, "-");
    bff_run(t, H0, H1);
    check("step limit: '-' at ip=0 executes 64 times, tape[H0]=(uint8_t)-64=192",
          t[H0] == 192);

    /* Stack overflow: BFF_STACK_DEPTH '[' fill the stack, the next '[' overflows.
     * Positions 0..BFF_STACK_DEPTH are all '['; position BFF_STACK_DEPTH+1 is '+'.
     * head0=100 (zero, stays zero if we terminate before '+' runs). */
    memset(t, '[', BFF_TAPE_LEN);
    t[BFF_STACK_DEPTH + 1] = '+';  /* would increment t[100] if reached */
    t[100] = 0;
    bff_run(t, 100, H1);
    check("stack overflow at depth 64 terminates before '+'", t[100] == 0);

    /* -----------------------------------------------------------------------
     * Loop semantics
     * ----------------------------------------------------------------------- */

    /* Basic countdown loop: '[-]]' starting with tape[H0]=5 runs 5 times */
    make_tape(t, "[-]]");
    t[H0] = 5;
    bff_run(t, H0, H1);
    check("countdown loop '[-]]' exits when tape[head0] reaches 0", t[H0] == 0);

    /* NEW SEMANTICS: '[' pushes unconditionally even when tape[head0]==0.
     * '[,]]' with tape[H0]=0, tape[H1]=99:
     *   - '[' pushes (tape[H0]==0 but we push anyway)
     *   - ',' sets tape[H0]=tape[H1]=99
     *   - ']' sees tape[H0]=99 != 0, loops — hits step limit
     * Under OLD semantics '[' would skip the body and tape[H0] would stay 0. */
    make_tape(t, "[,]]");
    t[H1] = 99;
    bff_run(t, H0, H1);
    check("'[' pushes unconditionally: body runs even when tape[head0]==0 initially",
          t[H0] == 99);

    /* -----------------------------------------------------------------------
     * Pointer wrap-around
     * ----------------------------------------------------------------------- */

    /* head0 wraps from 127 to 0: '+' increments t[0] which starts as '>' (62) */
    make_tape(t, ">+]");
    uint8_t t0_init = t[0];
    bff_run(t, 127, H1);
    check("head0 wraps from 127 to 0", t[0] == (uint8_t)(t0_init + 1));

    /* head0 wraps from 0 to 127 */
    make_tape(t, "<+]");
    bff_run(t, 0, H1);
    check("head0 wraps from 0 to 127", t[127] == 1);

    /* -----------------------------------------------------------------------
     * Self-modification
     * ----------------------------------------------------------------------- */

    /* '+' sets tape[H0]=1; '.' writes it to head1=2, overwriting the ',' there.
     * When ip reaches 2, it sees 0x01 (not a BFF instruction) — a no-op.
     * Then ']' at position 3 terminates.
     * If self-modification didn't work, ',' at ip=2 would copy tape[H1] into tape[H0]. */
    make_tape(t, "+.,]");
    /* tape[2]=','; head1 points at position 2 */
    t[H1] = 77;   /* if ',' runs it would set t[H0]=77; if overwritten it stays 1 */
    bff_run(t, H0, 2);
    check("'.' overwrites an instruction byte (self-modification)",
          t[2] == 1 && t[H0] == 1);

    /* -----------------------------------------------------------------------
     * Summary
     * ----------------------------------------------------------------------- */
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
