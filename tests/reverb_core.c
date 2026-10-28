/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir host test, the fixed-point side of tests/reverb_compare.cc: reverb.c's PLATE (the emulator's build of the
 * firmware), its LFOs' swings off (PL_SW1, PL_SW2 = 0, as the compare's copy of Clouds). */
#define PL_SW1 0u
#define PL_SW2 0u
#include "../web/emu/elixir_web.c"

/* the send in (n a multiple of BLOCK, Q15 audio as float -1..1), PLATE's mid and side out (the reverb's return before
 * LEVEL: Clouds' wet x 2 at PL_OUT) */
void rv_core(int size, int damp, const float *in, float *mid, float *side, int n)
{
    int32_t x[BLOCK], o[BLOCK];
    int k, i;
    rev_clear();
    memset(&plt, 0, sizeof plt);
    rv.size = (uint8_t)size;
    rv.damp = (uint8_t)damp;
    rv.rtype = 3;
    for (k = 0; k + (int)BLOCK <= n; k += BLOCK) {
        for (i = 0; i < (int)BLOCK; i++) {
            x[i] = (int32_t)(in[k + i] * 32768.0f);
            o[i] = 0;
        }
        rev_plate(x, o, BLOCK);
        for (i = 0; i < (int)BLOCK; i++) {
            mid[k + i] = (float)o[i] / 32768.0f;
            side[k + i] = (float)rev_side[i] / 32768.0f;
        }
    }
}
