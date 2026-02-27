#include "bff.h"

#include <stdio.h>
#include <string.h>

/* Convention: tape[0] = H0_POS (head0 start), tape[1] = H1_POS (head1 start).
 * Program bytes go at positions BFF_IP_START (2) onwards.
 * H0_POS and H1_POS are well away from the program area and each other. */
#define H0_POS 50
#define H1_POS 70  /* must not be a BFF instruction byte; 70 is safe */

static int passed = 0, failed = 0;

static void check(const char *name, int cond) {
    if (cond) { printf("PASS: %s\n", name); passed++; }
    else       { printf("FAIL: %s\n", name); failed++; }
}

/* Zero the tape, set head positions, write program starting at BFF_IP_START. */
static void make_tape(uint64_t tape[BFF_TAPE_LEN], const char *prog) {
    for (int i = 0; i < BFF_TAPE_LEN; i++) tape[i] = MAKE_TOKEN(0, 0, 0);
    tape[0] = MAKE_TOKEN(0, 0, H0_POS);
    tape[1] = MAKE_TOKEN(0, 0, H1_POS);
    for (int i = 0; prog[i] && BFF_IP_START + i < BFF_TAPE_LEN; i++)
        tape[BFF_IP_START + i] = MAKE_TOKEN(0, 0, (uint8_t)prog[i]);
}

int main(void) {
    uint64_t t[BFF_TAPE_LEN];

    /* -----------------------------------------------------------------------
     * Initialisation
     * ----------------------------------------------------------------------- */

    /* head0 and head1 are read from tape[0] and tape[1] */
    make_tape(t, "+]");
    bff_run(t);
    check("head0 initialised from tape[0]", TOKEN_CHAR(t[H0_POS]) == 1);

    make_tape(t, ",]");
    t[H0_POS] = MAKE_TOKEN(0, 0, 42);
    bff_run(t);
    check("head1 initialised from tape[1]: ',' writes tape[head0] to tape[H1_POS]",
          TOKEN_CHAR(t[H1_POS]) == 42);

    /* IP starts at BFF_IP_START=2 (bytes 0-1 not executed as code) */
    make_tape(t, "+]");
    t[0] = MAKE_TOKEN(0, 0, H0_POS);   /* head0 position — not an instruction */
    t[1] = MAKE_TOKEN(0, 0, H1_POS);   /* head1 position — not an instruction */
    bff_run(t);
    check("IP starts at 2: '+' at tape[2] executes, tape[0]/tape[1] are data",
          TOKEN_CHAR(t[H0_POS]) == 1 && TOKEN_CHAR(t[0]) == H0_POS);

    /* -----------------------------------------------------------------------
     * Basic instructions
     * ----------------------------------------------------------------------- */

    make_tape(t, "+]");
    bff_run(t);
    check("'+' increments tape[head0]", TOKEN_CHAR(t[H0_POS]) == 1);

    make_tape(t, "-]");
    bff_run(t);
    check("'-' decrements tape[head0] (wraps to 255)", TOKEN_CHAR(t[H0_POS]) == 255);

    make_tape(t, ">+]");
    bff_run(t);
    check("'>' moves head0 right", TOKEN_CHAR(t[H0_POS]) == 0 && TOKEN_CHAR(t[H0_POS + 1]) == 1);

    make_tape(t, "<+]");
    bff_run(t);
    check("'<' moves head0 left", TOKEN_CHAR(t[H0_POS]) == 0 && TOKEN_CHAR(t[H0_POS - 1]) == 1);

    /* ',' copies tape[head0] to tape[head1] */
    make_tape(t, ",]");
    t[H0_POS] = MAKE_TOKEN(0, 0, 77);
    bff_run(t);
    check("',' copies tape[head0] to tape[head1]", TOKEN_CHAR(t[H1_POS]) == 77);

    /* -----------------------------------------------------------------------
     * head0 does NOT auto-advance; head1 does
     * ----------------------------------------------------------------------- */

    /* head0 stays fixed: two consecutive '+' both hit H0_POS */
    make_tape(t, "++]");
    bff_run(t);
    check("head0 does not auto-advance: '++' increments tape[H0_POS] twice",
          TOKEN_CHAR(t[H0_POS]) == 2 && TOKEN_CHAR(t[H0_POS + 1]) == 0);

    /* head1 auto-advances: two consecutive ',' write to H1_POS and H1_POS+1 */
    make_tape(t, ",,]");
    t[H0_POS] = MAKE_TOKEN(0, 0, 7);
    bff_run(t);
    check("head1 auto-advances: ',,' writes tape[H0_POS] to H1_POS and H1_POS+1",
          TOKEN_CHAR(t[H1_POS]) == 7 && TOKEN_CHAR(t[H1_POS + 1]) == 7);

    /* -----------------------------------------------------------------------
     * Termination conditions
     * ----------------------------------------------------------------------- */

    /* ']' with empty stack terminates before '+' */
    make_tape(t, "]+");
    bff_run(t);
    check("']' with empty stack terminates (subsequent '+' not reached)", TOKEN_CHAR(t[H0_POS]) == 0);

    /* Step limit: '-' at ip=2, head0 fixed at H0_POS.
     * ip=2 is visited every 128 steps; in 16384 steps that is 128 times.
     * Starting from 0, 128 decrements give (uint8_t)(0-128) = 128.
     * Values 128-255 are all non-instructions, so no self-modification fires. */
    make_tape(t, "-");
    bff_run(t);
    check("step limit: '-' at ip=2 executes 16384/128=128 times, tape[H0_POS]=128",
          TOKEN_CHAR(t[H0_POS]) == 128);

    /* Stack overflow: 64 '[' fill the stack; the 65th '[' overflows and terminates.
     * head0=100 is outside the '[' region so the chain is unbroken.
     * If '+' at position 67 is reached it would increment t[100] from '[' (91) to 92. */
    for (int i = 0; i < BFF_TAPE_LEN; i++) t[i] = MAKE_TOKEN(0, 0, '[');
    t[0] = MAKE_TOKEN(0, 0, 100);
    t[1] = MAKE_TOKEN(0, 0, H1_POS);
    t[BFF_IP_START + BFF_STACK_DEPTH + 1] = MAKE_TOKEN(0, 0, '+');  /* unreachable if overflow fires */
    bff_run(t);
    check("stack overflow at depth 64 terminates before '+'", TOKEN_CHAR(t[100]) == '[');

    /* -----------------------------------------------------------------------
     * Loop semantics
     * ----------------------------------------------------------------------- */

    /* Countdown loop: '[-]]' with tape[H0_POS]=5 decrements to 0 and exits */
    make_tape(t, "[-]]");
    t[H0_POS] = MAKE_TOKEN(0, 0, 5);
    bff_run(t);
    check("countdown loop '[-]]' exits when tape[head0] reaches 0", TOKEN_CHAR(t[H0_POS]) == 0);

    /* '[' pushes unconditionally even when tape[head0]==0.
     * '[,]]' with tape[H0_POS]=0 and tape[H1_POS]=99:
     *   '[' pushes (head1 stays at H1_POS); ',' writes tape[H0_POS]=0 to tape[H1_POS];
     *   head1→H1_POS+1; ']' pops; ']' terminates → tape[H1_POS] = 0 (body ran, overwrote 99).
     *   If '[' had skipped the body, ',' would never run and tape[H1_POS] would stay 99. */
    make_tape(t, "[,]]");
    t[H1_POS] = MAKE_TOKEN(0, 0, 99);  /* head1 starts here; ',' writes to H1_POS */
    bff_run(t);
    check("'[' pushes unconditionally: body runs and overwrites tape[H1_POS]",
          TOKEN_CHAR(t[H1_POS]) == 0);

    /* -----------------------------------------------------------------------
     * Pointer wrap-around
     * ----------------------------------------------------------------------- */

    make_tape(t, ">+]");
    t[0] = MAKE_TOKEN(0, 0, 127);   /* head0 starts at 127; '>' wraps it to 0 */
    bff_run(t);
    check("head0 wraps from 127 to 0 via '>'", TOKEN_CHAR(t[0]) == 128);
    /* tape[0] starts as 127; after '>' head0=0; '+' increments tape[0] from 127 to 128 */

    make_tape(t, "<+]");
    t[0] = MAKE_TOKEN(0, 0, 0);    /* head0 starts at 0; '<' wraps it to 127 */
    bff_run(t);
    check("head0 wraps from 0 to 127 via '<'", TOKEN_CHAR(t[127]) == 1);

    /* -----------------------------------------------------------------------
     * Token semantics
     * ----------------------------------------------------------------------- */

    /* '+' modifies char field only; TOKEN_ID is preserved */
    make_tape(t, "+]");
    t[H0_POS] = MAKE_TOKEN(99, 0, 0);
    bff_run(t);
    check("'+' preserves TOKEN_ID",
          TOKEN_ID(t[H0_POS]) == 99 && TOKEN_CHAR(t[H0_POS]) == 1);

    /* '-' modifies char field only; TOKEN_ID is preserved */
    make_tape(t, "-]");
    t[H0_POS] = MAKE_TOKEN(77, 0, 5);
    bff_run(t);
    check("'-' preserves TOKEN_ID",
          TOKEN_ID(t[H0_POS]) == 77 && TOKEN_CHAR(t[H0_POS]) == 4);

    /* ',' copies the full token (id + epoch + char) */
    make_tape(t, ",]");
    t[H0_POS] = MAKE_TOKEN(42, 3, 77);
    bff_run(t);
    check("',' copies full token: TOKEN_ID and TOKEN_CHAR preserved",
          TOKEN_ID(t[H1_POS]) == 42 && TOKEN_CHAR(t[H1_POS]) == 77);

    /* -----------------------------------------------------------------------
     * Summary
     * ----------------------------------------------------------------------- */
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
