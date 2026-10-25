/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host test, the fixed-point side of tests/rings_compare.cc: rings.c's three resonators (the emulator's
 * build of the firmware), fed a float signal, their output as float. */
#define RINGS_TAP
#include "../web/emu/kettle_web.c"

/* model 1 MODAL, 2 STRING, 3 FM; f: cycles a sample; s b d p: Rings' STRUCTURE BRIGHTNESS DAMPING POSITION (0..1);
 * env: FM's internal envelope triggered at the start. n: a multiple of BLOCK */
void rg_core(int model, double f, double s, double b, double d, double p, int env, const float *in, int n, float *out)
{
    model_t *m = &mdl[1];
    uint32_t fq = (uint32_t)(f * 4294967296.0), k, i;
    int32_t S = (int32_t)(s * 65536 + 0.5), B = (int32_t)(b * 65536 + 0.5), D = (int32_t)(d * 65536 + 0.5),
            P = (int32_t)(p * 65536 + 0.5);
    memset(m, 0, sizeof *m);
    m->type = m->ran = (uint8_t)model;
    model_reset(m, 1);
    if (env)
        m->aenv = m->benv = RG_ONE << 8;
    for (k = 0; k + BLOCK <= (uint32_t)n; k += BLOCK) {
        int32_t x[BLOCK], acc[BLOCK];
        for (i = 0; i < BLOCK; i++) {
            x[i] = (int32_t)(in[k + i] * RG_ONE);
            acc[i] = 0;
        }
        if (model == MD_MODAL)
            modal_run(m, x, acc, modal_coefs(m, fq, S, rg_mul(B, B, 16), D, P, RG_MODES));
        else if (model == MD_STRING)
            string_run(m, 1, fq, S, B, D, P, x, acc);
        else
            fm_run(m, fq, S, B, D, P, x, acc);
        for (i = 0; i < BLOCK; i++)
            out[k + i] = (float)acc[i] / RG_ONE;
    }
}
