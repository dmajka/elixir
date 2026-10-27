/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir host test, the fixed-point side of tests/plaits_compare.cc: plaits.c's engines (the emulator's build of the
 * firmware), struck at the start, their OUT and AUX as float. */
#include "../web/emu/elixir_web.c"

/* eng: plaits.c's PE_*; note: MIDI; the knobs 0..1; n: a multiple of PLSUB; seed: stmlib's Random's */
void pl_core(int eng, double note, double harm, double timbre, double morph, double accent, unsigned seed, int n,
             float *out, float *aux)
{
    pl_t *v = &pl[0];
    pl_par_t p;
    int32_t o[PLSUB], a[PLSUB];
    int k, i;
    pl_rng = seed;
    pl_dif.owner = -1;
    memset(v, 0, sizeof *v);
    pl_engine_init(v, 0, (uint32_t)eng);
    p.note = (int32_t)(note * 65536.0 + 0.5);
    p.f0 = pl_note_f(p.note);
    p.harm = (int32_t)(harm * 16777216.0 + 0.5);
    p.timbre = (int32_t)(timbre * 16777216.0 + 0.5);
    p.morph = (int32_t)(morph * 16777216.0 + 0.5);
    p.accent = (int32_t)(accent * 16777216.0 + 0.5);
    for (k = 0; k + (int)PLSUB <= n; k += PLSUB) {
        p.trig = k == 0;
        pl_engine_render(v, 0, &p, o, a, PLSUB);
        for (i = 0; i < (int)PLSUB; i++) {
            out[k + i] = (float)o[i] / 16777216.0f;
            aux[k + i] = (float)a[i] / 16777216.0f;
        }
    }
}

/* the particle engine's diffuser alone: io in place, Q24 inside */
void pl_dif_test(double amount, double rt, float *io, int n)
{
    int32_t b[PLSUB];
    int k, i;
    pl_dif.owner = 0;
    dif_clear();
    for (k = 0; k + (int)PLSUB <= n; k += PLSUB) {
        for (i = 0; i < (int)PLSUB; i++)
            b[i] = (int32_t)(io[k + i] * 16777216.0f);
        dif_process((int32_t)(amount * 16777216.0), (int32_t)(rt * 16777216.0), b, PLSUB);
        for (i = 0; i < (int)PLSUB; i++)
            io[k + i] = (float)b[i] / 16777216.0f;
    }
}
