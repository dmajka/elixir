/* Elixir: the core-0 filters (1.4, docs/TO_DO_PERF_VOICES.md A): the master filter's RING, the FX bus PHASER, the
 * voice filter (svf_block, every type) hashed over a knob grid, one hash each (the bit-identical check).
 *   cc -O2 -w -I build/gen -I firmware/src -I tests tests/bench/filters_bench.c -o build/filters_bench && build/filters_bench
 * Each case: fresh state, a few dozen blocks of noise and hits (loud enough to clip), its inputs moved one at a time
 * along the way (a cache of coefficients must see each), some blocks with none moved. Host ns are no measure of the
 * device's cost (count instructions: tools/dis_loops.py). */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "engine_host.h"
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
static uint32_t rs = 12345u;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static uint8_t knob(void)
{
    static const uint8_t K[] = {0, 1, 20, 37, 50, 63, 64, 65, 77, 90, 101, 115, 126, 127};
    uint32_t r = rnd() >> 16;
    return r & 1 ? K[(r >> 1) % sizeof K] : (uint8_t)((r >> 4) % 128u);
}
static uint64_t H = 1469598103934665603ull;
static void hash16(const int16_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        H ^= (uint16_t)b[i];
        H *= 1099511628211ull;
    }
}
static void hash32(const int32_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        H ^= (uint32_t)b[i];
        H *= 1099511628211ull;
    }
}
static void sig_in(int16_t *b, uint32_t blk, uint32_t lvl)   /* noise, a hit every 6 blocks, lvl 0 .. 3 */
{
    static const int32_t L[] = {600, 6000, 20000, 32767};
    for (uint32_t i = 0; i < BLOCK; i++) {
        int32_t v = (int32_t)(rnd() >> 16) - 32768;
        v = v * L[lvl] / 32768;
        if (blk % 6u == 0u)
            v += (int32_t)((32767 - (int32_t)i * 300) * ((i & 4u) ? 1 : -1));
        b[i] = (int16_t)sat16(v);
    }
}

/* RING: ring_block over its knobs (TUNE, SLOPE, SPREAD, TONE, WET, Reso as dj_rs, the position d, the LFOs on WET
 * and TONE); the states hashed at the end */
static uint64_t bench_ring(uint32_t ncase)
{
    for (uint32_t c = 0; c < ncase; c++) {
        int16_t l[BLOCK], r[BLOCK];
        int32_t d;
        uint32_t lvl = c & 3u;
        memset(pf.rg_s, 0, sizeof pf.rg_s);
        memset(pf.rg_xh, 0, sizeof pf.rg_xh);
        memset(pf.rg_yh, 0, sizeof pf.rg_yh);
        pfs.dj_ftype = DJT_RING;
        pfs.dj_tune = (uint8_t)(rnd() >> 16) % (NRTUNE + 1u);   /* (NRTUNE: out of range, as RC_NOTE) */
        pfs.dj_slope = knob(), pfs.dj_spread = knob(), pfs.dj_tone = knob(), pfs.dj_wet = knob();
        pf.dj_rs = (int32_t)(rnd() >> 16) % 32513;
        for (uint32_t g = 0; g < NGBI; g++)
            gl.bi[g] = (rnd() & 3u) ? 0 : (int32_t)(rnd() >> 15) - 65536;
        d = (int32_t)(rnd() >> 15) - 65536;
        for (uint32_t b = 0; b < 30; b++) {
            int32_t a, w;
            switch (b) {                             /* (one input at a time) */
            case 3: d += (int32_t)(rnd() >> 22) - 512; break;
            case 6: d = (int32_t)(rnd() >> 15) - 65536; break;
            case 9: pf.dj_rs = (int32_t)(rnd() >> 16) % 32513; break;
            case 10: pf.dj_rs += 3; break;
            case 12: pfs.dj_slope = knob(); break;
            case 14: pfs.dj_tone = knob(); break;
            case 16: pfs.dj_tune = (uint8_t)(rnd() >> 16) % NRTUNE; break;
            case 18: pfs.dj_spread = knob(); break;
            case 20: pfs.dj_wet = knob(); break;
            case 22: gl.bi[gd_bi(GD_RTONE)] = (int32_t)(rnd() >> 15) - 65536; break;
            case 24: gl.bi[gd_bi(GD_RWET)] = (int32_t)(rnd() >> 15) - 65536; break;
            case 26: d = (c & 1u) ? 65536 : -65536; break;
            case 28: d = (int32_t)(rnd() >> 24) - 128; break;   /* (near the middle: w < 1) */
            }
            a = iabs(d);
            if (!a)
                d = a = 1;
            w = a >= 8192 ? 32768 : a * 4;
            sig_in(l, b, lvl);
            sig_in(r, b + 1u, lvl);
            ring_block(l, r, d, w);
            hash16(l, BLOCK);
            hash16(r, BLOCK);
        }
        hash32(&pf.rg_s[0][0], 16);
    }
    return H;
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : "";
    uint32_t ncase = argc > 2 ? (uint32_t)atoi(argv[2]) : 3000;
    uint64_t all = 1469598103934665603ull, t0;
    tables_unpack();
    if (!*only || !strcmp(only, "ring")) {
        H = 1469598103934665603ull, rs = 1u, t0 = now();
        printf("RING     %016llx  %.1f us/case\n", (unsigned long long)bench_ring(ncase), (now() - t0) / 1000.0 / ncase);
        all = (all ^ H) * 1099511628211ull;
    }
    printf("all      %016llx\n", (unsigned long long)all);
    return 0;
}
