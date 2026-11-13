/* Elixir: the DRUM voices' models (Rings' MODAL, STRING, FM in rings.c; Plaits' engines in plaits.c, plaits_va.c)
 * hashed over a knob grid, one hash a model (the bit-identical check, 1.4 docs/TO_DO_PERF_VOICES.md).
 *   cc -O2 -w -I build/gen -I firmware/src -I tests tests/bench/models_bench.c -o build/models_bench && build/models_bench
 * Each case: fresh state, a hit, 24 blocks with an amp EG falling, the knobs moved at block 8, a second hit at 16
 * (Rings: the voice's own sound in, by EXCITER). Host ns are no measure of the device's cost (count instructions). */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "engine_host.h"
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
static uint32_t rs = 12345u;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static uint8_t knob(void)
{
    static const uint8_t K[] = {0, 1, 20, 37, 50, 64, 77, 90, 101, 115, 126, 127};
    uint32_t r = rnd() >> 16;
    return r & 1 ? K[(r >> 1) % sizeof K] : (uint8_t)((r >> 4) % 128u);
}
static const char *mname(uint32_t md)
{
    static char b[16];
    static const char *PN[16] = {"P VA", "P WSHAPE", "P FM", "CLOUD", "P ADDTV", "P WTBL", "P CHORD", "P SPEECH",
                                 "P SWARM", "P NOISE", "RAIN", "P STRING", "P MODAL", "P BD", "P SD", "P HH"};
    if (md == MD_MODAL) return "MODAL";
    if (md == MD_STRING) return "STRING";
    if (md == MD_FM) return "FM";
    snprintf(b, sizeof b, "%s", PN[md - MD_PL0]);
    return b;
}
int main(int argc, char **argv)
{
    static const uint32_t INC[] = {3000000u, 25000000u, 90000000u, 400000000u};
    uint32_t only = argc > 1 ? (uint32_t)atoi(argv[1]) : 0, ncase = argc > 2 ? (uint32_t)atoi(argv[2]) : 2000;
    uint64_t all = 1469598103934665603ull;
    tables_unpack();
    for (uint32_t md = MD_MODAL; md < MD_PL0 + 16; md++) {
        uint64_t h = 1469598103934665603ull, t0 = now(), nz = 0, tot = 0;
        if (only && md != only)
            continue;
        if (md == MD_PL0 + 5 || md == MD_PL0 + 9 || md == MD_PL0 + 12)   /* (P WTBL parked, P NOISE, P MODAL: */
            continue;                                                     /* not Plaits' here) */
        rs = 12345u + md;
        for (uint32_t c = 0; c < ncase; c++) {
            uint32_t vi = c % 3u;                    /* (D1-D3: their own lines, comb) */
            voice_t v;
            int16_t buf[BLOCK];
            model_t *m = &mdl[vi];
            uint8_t k[6];
            memset(&v, 0, sizeof v);
            memset(mdl, 0, sizeof mdl);
            memset(pl, 0, sizeof pl);
            pl_rng = 0;
            for (int j = 0; j < 6; j++)
                k[j] = knob();
            m->want = m->type = (uint8_t)md;
            m->dec = k[0]; m->damp = k[1]; m->mat = k[2]; m->pos = k[3]; m->exc = k[4]; m->tens = k[5];
            v.osc.inc = INC[c & 3u] + (rnd() >> 12);
            v.vol_mod = (uint8_t)(c >> 2 & 1u);
            v.velo = 20000 + (int32_t)(rnd() >> 20);
            v.gain = 1 << 24;
            v.last_gain = 0;
            if (md >= MD_PL0) pl_trigger(vi, 64u + (c & 63u)); else model_trigger(vi, 64u + (c & 63u), 0);
            for (int b = 0; b < 24; b++) {
                if (b == 8) {
                    m->dec = knob(); m->damp = knob(); m->mat = knob(); m->pos = knob();
                    v.osc.inc += v.osc.inc >> 3;
                }
                if (b == 16) {
                    if (md >= MD_PL0) pl_trigger(vi, 127u); else model_trigger(vi, 127u, 0);
                    v.last_gain = v.gain;
                    v.gain = 1 << 24;
                }
                for (int i = 0; i < BLOCK; i++)    /* (Rings: the voice's own sound, EXCITER's share of it) */
                    buf[i] = b < 3 || (b >= 16 && b < 18) ? (int16_t)((rnd() >> 17) - 16384 + (i * 400)) : 0;
                if (md >= MD_PL0)
                    pl_block(vi, &v, buf);
                else
                    model_block(vi, &v, buf);
                for (int i = 0; i < BLOCK; i++) {
                    nz += buf[i] != 0;
                    tot++;
                    h ^= (uint16_t)buf[i];
                    h *= 1099511628211ull;
                }
                v.last_gain = v.gain;
                v.gain -= v.gain >> 3;
            }
        }
        printf("%-9s %016llx  %4.1f%% not 0  %.1f us/case\n", mname(md), (unsigned long long)h, 100.0 * nz / tot,
               (now() - t0) / 1000.0 / ncase);
        all = (all ^ h) * 1099511628211ull;
    }
    printf("all       %016llx\n", (unsigned long long)all);
    return 0;
}
