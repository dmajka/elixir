/* Elixir: the core-0 filters (1.4, docs/TO_DO_PERF_VOICES.md A): the master filter's RING, the FX bus PHASER, the
 * voice filter (svf_block, every type), the FREQ SHIFT, the TAPE delays, MONO and PING PONG hashed over a knob grid, one hash each (the bit-identical check).
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

/* PHASER: fx_phaser over its knobs (P1 rate, P2 depth, P3 feedback, P4 centre, FB PUSH, DRY/WET ramping), stereo
 * (one chain on the sum) and the mono routes; the mix and the states hashed */
static uint64_t bench_phaser(uint32_t ncase)
{
    static const uint8_t RT[] = {R_ST1, R_ST2, R_L1, R_R1};
    for (uint32_t c = 0; c < ncase; c++) {
        uint32_t lvl = c & 3u;
        memset(fx.ph_s, 0, sizeof fx.ph_s);
        memset(fx.ph_a, 0, sizeof fx.ph_a);
        fx.route = RT[(c >> 2) & 3u];
        fx.m_ph = rnd();
        for (int j = 0; j < 4; j++)
            fx.p[j] = (int32_t)(knob() * 65536u / 127u);
        fx.push = (rnd() & 1u) ? 0 : (int32_t)(rnd() >> 15);
        fx.dw = (int32_t)(knob() * 32768u / 127u);
        fx.dw_last = (rnd() & 1u) ? fx.dw : (int32_t)(knob() * 32768u / 127u);
        for (uint32_t b = 0; b < 40; b++) {
            switch (b) {
            case 8: fx.p[1] = (int32_t)(knob() * 65536u / 127u); break;
            case 12: fx.p[2] = (int32_t)(knob() * 65536u / 127u); break;
            case 16: fx.p[3] = (int32_t)(knob() * 65536u / 127u); break;
            case 20: fx.p[0] = (int32_t)(knob() * 65536u / 127u); break;
            case 24: fx.dw = (int32_t)(knob() * 32768u / 127u); break;
            case 30: fx.push = (int32_t)(rnd() >> 15); break;
            }
            sig_in(fx_l, b, lvl);
            sig_in(fx_r, b + 3u, lvl);
            for (int i = 0; i < BLOCK; i++)
                mix_l[i] = (int32_t)(rnd() >> 20) - 2048, mix_r[i] = (int32_t)(rnd() >> 20) - 2048;
            fx_phaser();
            hash32(mix_l, BLOCK);
            hash32(mix_r, BLOCK);
            fx.dw_last = fx.dw;
        }
        hash32(&fx.ph_s[0][0], 10);
        hash32(fx.ph_a, 2);
    }
    return H;
}

/* SHIFT: fx_shift over its knobs (P1 shift, P2 feedback, P3 delay, P4 spread, FB PUSH, DRY/WET ramping), stereo and
 * the mono routes; the mix, the line and the states hashed */
static uint64_t bench_shift(uint32_t ncase)
{
    static const uint8_t RT[] = {R_ST1, R_ST2, R_L1, R_R1};
    for (uint32_t c = 0; c < ncase; c++) {
        uint32_t lvl = c & 3u;
        memset(fx.sh_s, 0, sizeof fx.sh_s);
        memset(fl_buf, 0, sizeof fl_buf);
        fx.sh_q1 = 0, fx.sh_d = 0;
        fx.route = RT[(c >> 2) & 3u];
        fx.sh_ph = rnd();
        fx.fl_w = rnd() >> 20;
        for (int j = 0; j < 4; j++)
            fx.p[j] = (int32_t)(knob() * 65536u / 127u);
        if (rnd() & 1u)
            fx.p[1] = 0;                             /* (no feedback: its own path) */
        if (rnd() & 1u)
            fx.p[3] = 0;
        fx.push = (rnd() & 3u) ? 0 : (int32_t)(rnd() >> 15);
        fx.dw = (int32_t)(knob() * 32768u / 127u);
        fx.dw_last = (rnd() & 1u) ? fx.dw : (int32_t)(knob() * 32768u / 127u);
        for (uint32_t b = 0; b < 40; b++) {
            switch (b) {
            case 8: fx.p[1] = (rnd() & 1u) ? 0 : (int32_t)(knob() * 65536u / 127u); break;
            case 12: fx.p[2] = (int32_t)(knob() * 65536u / 127u); break;
            case 16: fx.p[3] = (rnd() & 1u) ? 0 : (int32_t)(knob() * 65536u / 127u); break;
            case 20: fx.p[0] = (int32_t)(knob() * 65536u / 127u); break;
            case 24: fx.dw = (int32_t)(knob() * 32768u / 127u); break;
            case 30: fx.push = (int32_t)(rnd() >> 15); break;
            }
            sig_in(fx_l, b, lvl);
            sig_in(fx_r, b + 3u, lvl);
            for (int i = 0; i < BLOCK; i++)
                mix_l[i] = (int32_t)(rnd() >> 20) - 2048, mix_r[i] = (int32_t)(rnd() >> 20) - 2048;
            fx_shift();
            hash32(mix_l, BLOCK);
            hash32(mix_r, BLOCK);
            fx.dw_last = fx.dw;
        }
        hash32(&fx.sh_s[0][0], 32);
        hash32(&fx.sh_q1, 1);
        hash32(&fx.sh_d, 1);
        hash32((const int32_t *)&fx.sh_ph, 1);
        hash32((const int32_t *)&fx.fl_w, 1);
        hash16(fl_buf, 2u * FL_LEN);
    }
    return H;
}

/* the voice filter: svf_block, each type (the LXR's, LP2, Elixir's, the character types) over cutoff, Reso, Drive
 * (as sound.c sets them), the note, FILTER X and the EG; cutoff, Reso, Drive moved, a hit (svf_reset) and a switch
 * of type along the way; the samples and the states hashed */
static int16_t svf_line[FLT_LINE];
static void svf_knob(svf_t *s, uint32_t which)
{
    int32_t f = (int32_t)(knob() * 65536u / 127u), q;
    switch (which) {
    case 0: s->fval = (uint32_t)f; s->dirty = 1; break;
    case 1: q = 65536 - f; s->q = q < 6554 ? 1311 : q; s->rk = f; s->dirty = 1; break;
    case 2: s->drive = 1638 + (int32_t)(((int64_t)f * f * 6) >> 20); break;
    case 3: s->xamt = f; break;
    }
}
static uint64_t bench_svf(uint32_t ncase, uint32_t only)
{
    for (uint32_t c = 0; c < ncase; c++) {
        svf_t s;
        int16_t buf[BLOCK];
        uint32_t t = 1u + c % F_LAST, lvl = (c / F_LAST) & 3u;
        if (only && t != only)
            continue;
        memset(&s, 0, sizeof s);
        svf_init(&s);
        memset(svf_line, 0, sizeof svf_line);
        s.line = svf_line;
        for (uint32_t k = 0; k < 4; k++)
            svf_knob(&s, k);
        s.key = (int8_t)((int32_t)(rnd() >> 27) - 16);
        for (uint32_t b = 0; b < 30; b++) {
            switch (b) {
            case 5: svf_knob(&s, 0); break;
            case 9: svf_knob(&s, 1); break;
            case 12: svf_knob(&s, 2); break;
            case 15: svf_reset(&s); break;
            case 18: svf_knob(&s, 0); svf_knob(&s, 1); break;
            case 21: svf_knob(&s, 3); s.key = (int8_t)((int32_t)(rnd() >> 27) - 16); s.dirty = 1; break;
            }
            s.eg = (int32_t)(65536u >> (b >> 2));
            sig_in(buf, b, lvl);
            svf_block(&s, b >= 24 && (c & 1u) ? (t % F_LAST) + 1u : t, buf);   /* (a switch of type at 24, half the
                                                                               * cases) */
            hash16(buf, BLOCK);
        }
        hash32(&s.s1, 4);
        hash32(s.e, 6);
        hash32(&s.fb, 1);
    }
    return H;
}

/* TAPE and TAPE PP: fx_tape over its knobs (TIME, FEEDBACK, TONE, LEVEL, WOW, SATURATE, GLITCH, PITCH, BAND, ECHO >
 * REVERB, SYNC, FB PUSH, the global LFOs, FREEZE), short times (echoes within the case), each moved alone along the
 * way; the mix, the reverb's send, and the states and line at the end hashed. (Chances from rnd()'s top bits: its
 * low bits repeat with a short period, the same each case) */
static uint32_t kq(void) { return knob() * 65536u / 127u; }
static uint64_t bench_tape(uint32_t ncase)
{
    for (uint32_t c = 0; c < ncase; c++) {
        uint32_t lvl = c & 3u;
        memset(&tp, 0, sizeof tp);
        tp.rng = rnd();                              /* (GLITCH's draws: not the same few from 0 every case) */
        memset(fx.bd, 0, sizeof fx.bd);
        memset(fxbuf, 0, sizeof fxbuf);
        memset(ps_buf, 0, sizeof ps_buf);
        fx.delay_type = (uint8_t)((c & 1u) ? DELAY_TAPEPP : DELAY_TAPE);
        fx.route = (c >> 1) % 5u == 4u ? R_L1 : R_ST1;
        fx.dl_t = (int32_t)(rnd() % 22000u);         /* (squared: to ~0.17 s) */
        fx.d_fb = (int32_t)kq();
        fx.d_tone = 1311 + (int32_t)(((int64_t)kq() * kq() * 64225) >> 32);
        fx.dl_lvl = (int32_t)(kq() >> 1);
        fx.dl_g = (rnd() >> 30) ? 32768 : (int32_t)(rnd() >> 17);
        fx.dl_e = (rnd() >> 31) ? fx.dl_lvl : 0;
        fx.d_wow = (rnd() >> 31) ? 0 : (int32_t)kq();
        fx.d_sat = knob();
        fx.d_glitch = (rnd() >> 31) ? 0 : knob() * 516;
        fx.d_pitch = (uint8_t)((rnd() >> 31) ? 0u : rnd() % NDPITCH);
        fx.band = (uint8_t)((rnd() >> 31) ? 0u : knob());
        fx.lfo_band = (rnd() >> 30) ? 0 : (int32_t)(rnd() >> 15) - 65536;
        fx.lfo_time = (rnd() >> 30) ? 0 : (int32_t)(rnd() % 8000u) - 4000;
        fx.lfo_fb = (rnd() >> 30) ? 0 : (int32_t)(rnd() >> 16) - 32768;
        fx.d_echorev = (rnd() >> 31) ? 0 : (int32_t)kq();
        fx.rev_fx = 0;
        fx.d_route = 64;
        fx.d_sync = (uint8_t)((rnd() >> 29) == 0u);
        fx.push = (rnd() >> 31) ? 0 : (int32_t)(rnd() >> 15);
        fx.freeze = 0;
        fx_bpm_x10 = 600u + rnd() % 1800u;
        for (uint32_t b = 0; b < 90; b++) {
            switch (b) {
            case 20: fx.d_fb = (int32_t)kq(); break;
            case 26: fx.d_tone = 1311 + (int32_t)(((int64_t)kq() * kq() * 64225) >> 32); break;
            case 32: fx.dl_t = (int32_t)(rnd() % 22000u); break;
            case 38: fx.d_sat = knob(); break;
            case 44: fx.band = (uint8_t)knob(); break;
            case 50: fx.d_pitch = (uint8_t)(rnd() % NDPITCH); break;
            case 56: fx.d_wow = (int32_t)kq(); break;
            case 60: fx.dl_lvl = (int32_t)(kq() >> 1); break;
            case 64: fx.d_glitch = knob() * 516; break;
            case 70: fx.freeze = (uint8_t)((rnd() >> 30) == 0u); break;
            case 76: fx.freeze = 0, fx.push = (int32_t)(rnd() >> 15); break;
            }
            sig_in(fx_l, b, lvl);
            sig_in(fx_r, b + 3u, lvl);
            if (b > 40 && (b & 7u) == 0u)
                memset(fx_l, 0, BLOCK * sizeof fx_l[0]), memset(fx_r, 0, BLOCK * sizeof fx_r[0]);
            for (int i = 0; i < BLOCK; i++)
                mix_l[i] = (int32_t)(rnd() >> 20) - 2048, mix_r[i] = (int32_t)(rnd() >> 20) - 2048, rev_in[i] = 0;
            fx_tape();
            hash32(mix_l, BLOCK);
            hash32(mix_r, BLOCK);
            hash32(rev_in, BLOCK);
        }
        hash32((const int32_t *)&tp, sizeof tp / 4u);
        hash32(&fx.bd[0][0], 4);
        hash16(fxbuf, FXBUF_SIZE);
        hash16(ps_buf, PS_LEN);
    }
    return H;
}

/* MONO and PING PONG: fxbus_early (TIME, the early reads when far) and fx_delay, as fxbus_process runs them, over
 * TIME (short ones: the reads in the loop; moves: the 10 ms crossfade, and a change waiting behind one), FEEDBACK,
 * TONE, LEVEL, BAND, ECHO > REVERB, FB PUSH, the LFOs, FREEZE, SYNC; the mix, the reverb's send, states and line hashed */
static uint32_t dtime(void)
{
    uint32_t r = rnd();
    return r >> 31 ? (r >> 8) % 3000u : (r >> 8) % 20000u;   /* (squared: to ~0.07 s; under ~800: the near reads) */
}
static uint64_t bench_delay(uint32_t ncase)
{
    for (uint32_t c = 0; c < ncase; c++) {
        uint32_t lvl = c & 3u;
        fx.delay_type = (uint8_t)((c & 1u) ? DELAY_PINGPONG : DELAY_NORMAL);
        fx.d_off = 0;
        fx.dt_clr = 0;
        fx.dl_hold = 0;
        memset(fxbuf, 0, sizeof fxbuf);
        memset(fx.d_lp, 0, sizeof fx.d_lp);
        memset(fx.bd, 0, sizeof fx.bd);
        fx.d_write = rnd() & delay_mask();
        fx.d_old = fx.d_new = fx.d_write;
        fx.d_fold = fx.d_fnew = 0;
        fx.d_interp = 0;
        fx.d_has_pending = 0;
        fx.d_time = 0xFFFFFFFFu;
        fx.route = (c >> 1) % 5u == 4u ? R_L1 : R_ST1;
        fx.dl_t = (int32_t)dtime();
        fx.d_fb = (int32_t)kq();
        fx.d_tone = 1311 + (int32_t)(((int64_t)kq() * kq() * 64225) >> 32);
        fx.dl_lvl = 1 + (int32_t)(kq() >> 1);
        fx.dl_g = (rnd() >> 30) ? 32768 : (int32_t)(rnd() >> 17);
        fx.dl_e = (rnd() >> 31) ? fx.dl_lvl : 0;
        fx.band = (uint8_t)((rnd() >> 31) ? 0u : knob());
        fx.lfo_band = (rnd() >> 30) ? 0 : (int32_t)(rnd() >> 15) - 65536;
        fx.lfo_time = (rnd() >> 30) ? 0 : (int32_t)(rnd() % 8000u) - 4000;
        fx.lfo_fb = (rnd() >> 30) ? 0 : (int32_t)(rnd() >> 16) - 32768;
        fx.d_echorev = (rnd() >> 31) ? 0 : (int32_t)kq();
        fx.throw_echo = (uint8_t)((rnd() >> 29) == 0u);
        fx.rev_fx = 0;
        fx.d_route = 64;
        fx.d_sync = (uint8_t)((rnd() >> 29) == 0u);
        fx.push = (rnd() >> 31) ? 0 : (int32_t)(rnd() >> 15);
        fx.freeze = 0;
        fx_bpm_x10 = 600u + rnd() % 1800u;
        for (uint32_t b = 0; b < 90; b++) {
            switch (b) {
            case 16: fx.d_fb = (int32_t)kq(); break;
            case 22: fx.dl_t = (int32_t)dtime(); break;
            case 24: delay_set_time(dtime()); break;   /* (mid-crossfade: waits) */
            case 30: fx.d_tone = 1311 + (int32_t)(((int64_t)kq() * kq() * 64225) >> 32); break;
            case 36: fx.band = (uint8_t)knob(); break;
            case 42: fx.dl_t = (int32_t)dtime(); break;
            case 50: fx.dl_lvl = 1 + (int32_t)(kq() >> 1); break;
            case 56: fx.lfo_time = (int32_t)(rnd() % 8000u) - 4000; break;
            case 64: fx.freeze = (uint8_t)((rnd() >> 30) == 0u); break;
            case 72: fx.freeze = 0, fx.push = (int32_t)(rnd() >> 15); break;
            case 80: fx.dl_t = (int32_t)dtime(); break;
            }
            sig_in(fx_l, b, lvl);
            sig_in(fx_r, b + 3u, lvl);
            if (b > 40 && (b & 7u) == 0u)
                memset(fx_l, 0, BLOCK * sizeof fx_l[0]), memset(fx_r, 0, BLOCK * sizeof fx_r[0]);
            for (int i = 0; i < BLOCK; i++)
                mix_l[i] = (int32_t)(rnd() >> 20) - 2048, mix_r[i] = (int32_t)(rnd() >> 20) - 2048, rev_in[i] = 0;
            fxbus_early();
            fx_delay();
            hash32(mix_l, BLOCK);
            hash32(mix_r, BLOCK);
            hash32(rev_in, BLOCK);
        }
        hash32((const int32_t *)fx.d_lp, 2);
        hash32(&fx.bd[0][0], 4);
        hash16(fxbuf, FXBUF_SIZE);
        H ^= fx.d_write ^ fx.d_old * 3u ^ fx.d_new * 5u ^ fx.d_fold * 7u ^ (uint32_t)fx.d_interp * 11u;
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
    if (!*only || !strcmp(only, "phaser")) {
        H = 1469598103934665603ull, rs = 2u, t0 = now();
        printf("PHASER   %016llx  %.1f us/case\n", (unsigned long long)bench_phaser(ncase), (now() - t0) / 1000.0 / ncase);
        all = (all ^ H) * 1099511628211ull;
    }
    if (!*only || !strcmp(only, "shift")) {
        H = 1469598103934665603ull, rs = 6u, t0 = now();
        printf("SHIFT    %016llx  %.1f us/case\n", (unsigned long long)bench_shift(ncase), (now() - t0) / 1000.0 / ncase);
        all = (all ^ H) * 1099511628211ull;
    }
    if (!*only || !strcmp(only, "svf")) {
        uint32_t t1 = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;
        H = 1469598103934665603ull, rs = 3u, t0 = now();
        printf("SVF      %016llx  %.1f us/case\n", (unsigned long long)bench_svf(ncase * 4u, t1), (now() - t0) / 1000.0 / ncase);
        all = (all ^ H) * 1099511628211ull;
    }
    if (!*only || !strcmp(only, "tape")) {
        H = 1469598103934665603ull, rs = 4u, t0 = now();
        printf("TAPE     %016llx  %.1f us/case\n", (unsigned long long)bench_tape(ncase / 3u), (now() - t0) / 1000.0 / ncase);
        all = (all ^ H) * 1099511628211ull;
    }
    if (!*only || !strcmp(only, "delay")) {
        H = 1469598103934665603ull, rs = 5u, t0 = now();
        printf("DELAY    %016llx  %.1f us/case\n", (unsigned long long)bench_delay(ncase / 3u), (now() - t0) / 1000.0 / ncase);
        all = (all ^ H) * 1099511628211ull;
    }
    printf("all      %016llx\n", (unsigned long long)all);
    return 0;
}
