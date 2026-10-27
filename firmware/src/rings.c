/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 0.9: the DRUM voices' MODEL section, a port of Mutable Instruments Rings' resonators (rings/dsp: part.cc,
 * resonator.cc, string.cc, fm_voice.cc, plucker.h, follower.h, limiter.h, and stmlib/dsp; Copyright 2015 Emilie
 * Gillet, MIT licence) to fixed point (the FM-1 has no FPU). Three of Rings' models:
 *   MODAL   Rings' modal resonator: a bank of band-pass modes, their spread STRUCTURE (stiffness), their decay DECAY
 *           (Rings' DAMPING), the high ones' BRIGHT, their levels POSITION (where it is struck)
 *   STRING  Rings' string: a Karplus-Strong line with its dispersion (STRUCTURE: a curved bridge below 0.25, a stiff
 *           string above), its damping filters (DECAY, BRIGHT) and its pickup (POSITION)
 *   FM      Rings' FM voice: a modulator (its ratio STRUCTURE, quantized) on a carrier, its index BRIGHT, its
 *           feedback POSITION, its envelopes DECAY
 * Each is excited as Rings' internal exciter excites it (a filtered pulse, a noise burst through a comb, an
 * envelope) at the voice's hit, and by the voice's own sound (the LXR's oscillators, transient and amp EG), as
 * Rings is by its audio input: EXCITER sets the share (0: Rings' alone, 127: the voice's alone). The pitch is the
 * voice's (its pitch EG, random, TENSION: up at the hit, falling back). Rings' ODD and EVEN outputs are summed (its
 * output with one jack patched), through its limiter. Left out: the sympathetic strings (several strings a voice:
 * the CPU), Rings' reverb (Elixir has its own) and its polyphony (a drum voice is one).
 *
 * The modes a voice runs: 32, 24 with two voices on MODAL, 16 with three (Rings: 60, 28, 12 by polyphony). The
 * string's line holds 43 Hz and up; lower, the string runs at a lower rate, as Rings' does below 23 Hz. Signals Q20
 * (1.0: Rings' full scale), Rings' parameters Q16, coefficients Q24 and Q30; the per-block math (Rings' per 24
 * samples) runs per BLOCK. Where Rings' float has room and Q20 hasn't, a state keeps more bits (the modes' error
 * fed back, the SVFs' and DC blockers' Q26, the string's 32-bit lines, the FM envelopes' Q28): a rounding there
 * otherwise holds a decaying tail at a level, or detunes. tests/rings_compare.cc holds it to Rings' own float code
 * (correlation 0.9998 and up, the error -35 dB and under). */

#include "rings_tables.h"

enum { MD_LXR, MD_MODAL, MD_STRING, MD_FM, MD_PL0, NMODEL = MD_PL0 + 16 };   /* (0.15: Plaits' 16, plaits.c) */
#define RG_MODES 32u
#define RG_LINE 1024u                                /* the string (Rings: 2048 at 48 kHz; lower, it runs slower) */
#define RG_STRETCH 512u                              /* .. its dispersion's all-pass */
#define RG_COMB 256u                                 /* the plucker's comb */
#define RG_ONE (1 << 20)
static int32_t exp2_q16(int32_t y);                  /* (fxbus.c) */

typedef struct { int32_t g, rg, h, s1, s2; } rg_svf_t;   /* stmlib's Svf: g, r + g (Q24), h (Q30); states Q26 (in */
                                                     /* Q20: a low cutoff's g hp would round to nothing) */
typedef struct {
    uint8_t type, dec, damp, mat, pos, exc, tens;    /* the knobs: DECAY BRIGHT STRUCTURE POSITION EXCITER TENSION */
    uint8_t want;                                    /* 0.15: the kit's model (sound.c); type: the one that plays
                                                      * (model_limit) */
    uint8_t ran, strum;                              /* the model its state is for; a hit to excite at the block */
    int32_t vel, ten;                                /* the hit's velocity Q15; TENSION's pitch, Q16 over 1 */
    int32_t peak;                                    /* the limiter's, Q20 */
    rg_svf_t exf;                                    /* the excitation filter */
    /* MODAL */
    int32_t a1[RG_MODES], a2[RG_MODES], b[RG_MODES], y1[RG_MODES], y2[RG_MODES], e1[RG_MODES], e2[RG_MODES];
    int32_t x1, x2;
    /* STRING */
    uint32_t sw, tw, cw, dly, cpos, pl_n, pl_per;    /* write indexes; the delay, pickup (Q16), plucker */
    int32_t dcomp, fdamp, fbright, fx1, fx2, dn, cb, dcx, dcy, ex_x, ex_y, pl_g, ce;
    int32_t src, so0, so1;                           /* below the line's lowest: its rate's phase (Q24), outputs */
    rg_svf_t iir, plf;
    /* FM */
    uint32_t cph, mph, cinc, minc;
    int32_t prev, gain, fma, aenv, benv, fbr, ffb, det[3], cent;   /* (aenv, benv: Q28) */
    int32_t flp[2], fbp[2];                          /* the follower's two filters (stmlib's NaiveSvf) */
} model_t;
static model_t mdl[3];
#ifdef RINGS_TAP
static int32_t rg_tap[BLOCK];
#endif
/* the string's lines are 32 bits (Q20): in 16, a long decay's tail sticks where the loss is under a step. (18 KB,
 * in the RAM: the audio's every sample, and the pool's short) */
static int32_t rg_line[3][RG_LINE], rg_stretch[3][RG_STRETCH];
static int16_t rg_comb[3][RG_COMB] POOL;

/* ------------------------------------------------------------ helpers --- */
/* 1 / x = r 2^(sh - 48), r ~17 bits: x > 0 */
static uint32_t rg_rcp(uint32_t x, int32_t *sh)
{
    int32_t s = __builtin_clz(x);
    *sh = s;
    return 0xFFFFFFFFu / ((x << s) >> 16);
}
/* 1 / x = r 2^(sh - 94), r ~30 bits (Q30, of 1 / x's mantissa: a Newton step on rg_rcp's): x > 0 */
static uint32_t rg_rcp64(uint64_t x, int32_t *sh)
{
    int32_t s = __builtin_clzll(x);
    uint32_t xn = (uint32_t)((x << s) >> 32), r0 = (0xFFFFFFFFu / (xn >> 16)) << 14;
    uint32_t e = (uint32_t)(((uint64_t)xn * r0) >> 32);
    *sh = s;
    return (uint32_t)(((uint64_t)r0 * ((1u << 31) - e)) >> 30);
}
/* x r >> k, x to 64 bits (the product's to fit in 64) */
static uint64_t rg_mulsh(uint64_t x, uint32_t r, int32_t k)
{
    uint64_t hi = (x >> 32) * r;
    return (((x & 0xFFFFFFFFu) * r) >> k) + (k >= 32 ? hi >> (k - 32) : hi << (32 - k));
}
static int32_t rg_mul(int32_t a, int32_t b, int32_t q) { return (int32_t)(((int64_t)a * b) >> q); }
/* .. rounded: in a loop (the string's), a floor's bias builds up into an offset */
static int32_t rg_mulr(int32_t a, int32_t b, int32_t q) { return (int32_t)(((int64_t)a * b + (1ll << (q - 1))) >> q); }
static int32_t rg_semi(int32_t st)                   /* 2^(st / 12), st semitones Q16: Q16 */
{   /* stmlib's SemitonesToRatio: its tables' 1/256 of a semitone (the step, floored, as Rings plays it) */
    return exp2_q16((int32_t)(((int64_t)(st & ~255) * 715827883) >> 33));
}
/* .. finer, for the FM voice's modulator (whose phase errors its index multiplies): Q28, st -96 .. +48 */
static uint32_t rg_semi28(int32_t st)
{
    static const uint32_t SEMI[12] = {   /* 2^(n / 12), Q30 */
        1073741824u, 1137589835u, 1205234447u, 1276901417u, 1352829926u, 1433273380u, 1518500250u, 1608794974u,
        1704458901u, 1805811301u, 1913190429u, 2026954652u};
    int32_t s8 = (st >> 8) + 3072 * 8, oct = s8 / 3072 - 8, rem = s8 % 3072;
    int64_t u = ((int64_t)(rem & 255) * 248087039) >> 10, u2 = (u * u) >> 30, u3 = (u2 * u) >> 30;   /* k ln 2 / 3072 */
    uint64_t e = (uint64_t)((1ll << 30) + u + (u2 >> 1) + ((u3 * 178956971) >> 30) + ((((u3 * u) >> 30) * 44739243) >> 30));
                                                     /* its exp, Q30 (1/6, 1/24: no 64-bit division here) */
    uint64_t r = ((SEMI[rem >> 8] * e) >> 30) >> 2;
    return (uint32_t)(oct >= 0 ? r << oct : r >> -oct);
}
static uint32_t rg_fmul(uint32_t f, int32_t r)      /* a frequency (Q32) x a ratio (Q16), at most 0.499 */
{
    uint64_t x = ((uint64_t)f * (uint32_t)r) >> 16;
    return x > 2143188436u ? 2143188436u : (uint32_t)x;
}
/* stmlib's OnePole::tan(f): tan(pi f), f = f32 / 2^32 < 0.5, by Rings' approximations. Q24 */
enum { TAN_DIRTY, TAN_FAST, TAN_ACCURATE };
static int32_t rg_tan(uint32_t f32, uint32_t mode)
{
    int64_t f = f32 >> 2, f2 = (f * f) >> 30, p;    /* Q30 */
    if (mode == TAN_DIRTY)
        p = 194346347;
    else if (mode == TAN_FAST)
        p = 169584874 + ((935957068 * f2) >> 30);
    else
        p = 173398663 + (((684857702 + (((2704576354ll + (((1450592285 + ((46974249843ll * f2) >> 30)) * f2) >> 30)) *
                                          f2) >> 30)) * f2) >> 30);
    p = 52707179 + ((p * f2) >> 30);                /* pi + .. */
    return (int32_t)((p * f) >> 30);
}
/* Svf::set_f_q: r = 1 / Q (Q24) */
static void rg_svf_set(rg_svf_t *s, uint32_t f32, int32_t r, uint32_t mode)
{
    int32_t g = rg_tan(f32, mode), sh;
    uint32_t d = (uint32_t)(16777216 + rg_mul(r, g, 24) + rg_mul(g, g, 24)), rc = rg_rcp(d, &sh);
    s->g = g;
    s->rg = r + g;
    s->h = (int32_t)(rc << (sh + 6));
}
enum { SVF_LP, SVF_BP, SVF_HP };
static inline int32_t rg_svf(rg_svf_t *s, int32_t in, uint32_t mode)
{
    int32_t hp = (int32_t)(((((int64_t)in << 6) - ((((int64_t)s->rg * s->s1) + (1 << 23)) >> 24) - s->s2) * s->h +
                            (1 << 29)) >> 30);
    int32_t t = rg_mulr(s->g, hp, 24), bp = t + s->s1, u, lp;
    s->s1 = t + bp;
    u = rg_mulr(s->g, bp, 24);
    lp = u + s->s2;
    s->s2 = u + lp;
    return ((mode == SVF_LP ? lp : mode == SVF_BP ? bp : hp) + 32) >> 6;
}
static uint32_t rg_rng = 0x2545F491u;                /* (the models' own: D1-D3 only, one core renders them in order) */
static int32_t rg_noise(void) { return (int32_t)xs32(&rg_rng) >> 11; }   /* -1 .. 1, Q20 */
static int32_t soft_clip(int32_t x)                 /* linear to 0.75 of full scale, then bending to it */
{
    int32_t a = x < 0 ? -x : x, d;
    if (a <= 24576)
        return x;
    d = a - 24576;
    a = 24576 + (int32_t)((uint32_t)d * 8191u / (uint32_t)(d + 8192));
    return x < 0 ? -a : a;
}

/* ------------------------------------------------------------ MODAL --- */
/* Resonator::ComputeFilters (f: the pitch, Q32; s b d p: STRUCTURE, BRIGHTNESS (squared, as Part sets it), DAMPING,
 * POSITION, Q16): the modes' coefficients, their levels (Rings' CosineOscillator over POSITION) folded in. Rings'
 * SVF band-pass in the direct form its bilinear transform is: y = b (x - x2) + a1 y1 - a2 y2, so
 * b = g / D, a1 = 2 - 2 (r g + 2 g^2) / D, a2 = 1 - 2 r g / D, D = 1 + r g + g^2 (the small terms apart: a1's and
 * a2's distance from 2 and 1 sets the pitch and the decay, kept to the last bit). The modes in use: their number */
static uint32_t modal_coefs(model_t *m, uint32_t f, int32_t s, int32_t b, int32_t d, int32_t p, uint32_t nmax)
{
    int32_t idx = s >> 8, fr = s & 255, sh;
    int32_t stiff = RG_STIFF[idx] + (((RG_STIFF[idx + 1 < 257 ? idx + 1 : 256] - RG_STIFF[idx]) * fr) >> 8);
    int32_t att = 65536 - s, br, qloss, qrate, stretch = 1 << 24;   /* stiff, stretch: Q24 */
    int64_t y0, y1c, iir;                            /* the cosine oscillator, Q29 */
    uint64_t harmonic = f, q;
    uint32_t n = 0, k;
    att = rg_mul(att, att, 16);
    att = rg_mul(att, att, 16);
    att = rg_mul(att, att, 16);
    br = rg_mul(b, 65536 - rg_mul(13107, att, 16), 16);
    qloss = rg_mul(rg_mul(br, 131072 - br, 16), 55705, 16) + 9830;   /* x 0.85 + 0.15 */
    qrate = rg_mul(rg_mul(s, 131072 - s, 16), 6554, 16);            /* x 0.1 */
    q = (uint64_t)500u * (uint32_t)exp2_q16(rg_mul(d, 870817, 16)) << 8;   /* 500 x 10^(4 d), Q24 (log2 10 x 4) */
    {   /* CosineOscillator::InitApproximate(p), Start */
        int64_t fq = (int64_t)p - 16384, sign = 16;  /* Q16 */
        if (fq < 0)
            fq = -fq;
        else if (fq > 32768)
            fq -= 32768;
        else
            sign = -16;
        iir = (sign * fq * (65536 - 2 * fq)) >> 3;   /* Q29 */
        y1c = iir >> 2;
        y0 = 1 << 28;
    }
    for (k = 0; k < nmax; k++) {
        uint64_t pf64 = ((harmonic >> 4) * (uint32_t)stretch) >> 20;
        uint32_t pf, rc, rcd;
        int32_t g, rg, shd, amp;
        int64_t g2, t1, t2, temp;
        if (pf64 >= 2104533975u) {                   /* 0.49: clamped, and not counted */
            pf = 2104533975u;
        } else {
            pf = (uint32_t)pf64;
            n = k + 1u;
        }
        g = rg_tan(pf, TAN_FAST);
        {   /* r g = g / (1 + pf q) */
            uint64_t den = (((uint64_t)pf * (q >> 16)) >> 32) + 256u;   /* Q8 */
            rc = rg_rcp64(den, &sh);
            rg = (int32_t)(((uint64_t)(uint32_t)g * rc) >> (80 - sh));   /* Q30 */
        }
        g2 = ((int64_t)g * g) >> 18;                 /* Q30 */
        rcd = rg_rcp64((1ull << 30) + (uint32_t)rg + (uint64_t)g2, &shd);   /* 1 / D (D Q30) */
        t1 = (int64_t)rg_mulsh((uint64_t)rg + 2 * (uint64_t)g2, rcd, 63 - shd);   /* (2 r g + 4 g^2) / D */
        t2 = (int64_t)(((uint64_t)(uint32_t)rg * rcd) >> (63 - shd));
        temp = y0;                                   /* the mode's level: CosineOscillator::Next() */
        y0 = ((iir * y0) >> 29) - y1c;
        y1c = temp;
        amp = (int32_t)((temp + (1 << 28)) >> 14);  /* Q15 */
        m->b[k] = (int32_t)(((((uint64_t)(uint32_t)g * rcd) >> (58 - shd)) * (uint32_t)amp) >> 15);
        m->a1[k] = (int32_t)(t1 >= (1ll << 32) ? -(1ll << 31) + 1 : (1ll << 31) - (t1 < 1 ? 1 : t1));
        m->a2[k] = (int32_t)((1 << 30) - t2);
        stretch += stiff;
        stiff = rg_mul(stiff, stiff < 0 ? 998579896 : 1052266988, 30);   /* x 0.93, 0.98 */
        qloss += rg_mul(qrate, 65536 - qloss, 16);
        harmonic += f;
        q = (q * (uint32_t)qloss) >> 16;
    }
    return n;
}
/* Resonator::Process: in (Q20, x 1/8 here) through the modes, their sum added to out */
static void modal_run(model_t *m, const int32_t *in, int32_t *out, uint32_t nm)
{
    int32_t xd[BLOCK], x1 = m->x1, x2 = m->x2;
    uint32_t i, k;
    for (i = 0; i < BLOCK; i++) {
        int32_t x = in[i] >> 3;
        xd[i] = x - x2;
        x2 = x1;
        x1 = x;
    }
    m->x1 = x1;
    m->x2 = x2;
    for (k = 0; k < nm; k++) {
        int32_t a1 = m->a1[k], a2 = m->a2[k], b = m->b[k], y1 = m->y1[k], y2 = m->y2[k];
        int32_t e1 = m->e1[k], e2 = m->e2[k], c = a1 < 0 ? -2 : 2;
        for (i = 0; i < BLOCK; i++) {
            /* the rounding's error fed back through (1 -+ z^-1)^2: the poles sit by z = 1 (or -1, above a quarter
             * of the rate), and a plain truncation there turns into a hum that never decays */
            int64_t acc = (int64_t)b * xd[i] + (int64_t)a1 * y1 - (int64_t)a2 * y2 + (int64_t)c * e1 - e2;
            int32_t y = (int32_t)(acc >> 30);
            e2 = e1;
            e1 = (int32_t)(acc & 0x3FFFFFFF);
            y = y > (1 << 29) ? 1 << 29 : y < -(1 << 29) ? -(1 << 29) : y;   /* (a mode run away: held) */
            y2 = y1;
            y1 = y;
            out[i] += y;
        }
        m->y1[k] = y1;
        m->y2[k] = y2;
        m->e1[k] = e1;
        m->e2[k] = e2;
    }
    for (; k < RG_MODES; k++)                        /* (modes out of use: quiet when they come back) */
        m->y1[k] = m->y2[k] = m->e1[k] = m->e2[k] = 0;
}

/* ------------------------------------------------------------ STRING --- */
/* stmlib's DelayLine: written backwards, read at + delay */
static inline int32_t rg_line_rd(const int32_t *l, uint32_t n, uint32_t w, uint32_t d) { return l[(w + d) % n]; }
static int32_t rg_hermite(const int32_t *l, uint32_t n, uint32_t w, uint32_t d16)   /* ReadHermite, Q16 delay */
{
    uint32_t t = w + (d16 >> 16) + n;
    int32_t xm1 = l[(t - 1u) % n], x0 = l[t % n], x1 = l[(t + 1u) % n], x2 = l[(t + 2u) % n];
    int32_t f = (int32_t)(d16 & 0xFFFFu) >> 1;      /* Q15 */
    int32_t c = (x1 - xm1) >> 1, v = x0 - x1, w2 = c + v, a = w2 + v + ((x2 - x0) >> 1), bn = w2 + a;
    int64_t y = ((((((((int64_t)a * f + 16384) >> 15) - bn) * f + 16384) >> 15) + c) * f + 16384) >> 15;
    return (int32_t)y + x0;
}
static int32_t rg_line_lin(const int32_t *l, uint32_t n, uint32_t w, uint32_t d16)   /* Read(float) */
{
    uint32_t t = w + (d16 >> 16);
    int32_t a = l[t % n], b = l[(t + 1u) % n];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)((d16 & 0xFFFFu) >> 2) + 8192) >> 14);
}
static void rg_line_wr(int32_t *l, uint32_t n, uint32_t *w, int32_t x)
{
    l[*w] = x;
    *w = *w ? *w - 1u : n - 1u;
}
/* the plucker's comb, 16 bits (Q20 >> 5): the write's rounding error fed into the next (e) */
static int32_t rg_comb_lin(const int16_t *l, uint32_t w, uint32_t d16)
{
    uint32_t t = w + (d16 >> 16);
    int32_t a = l[t % RG_COMB], b = l[(t + 1u) % RG_COMB];
    return (a << 5) + (int32_t)(((int64_t)((b - a) << 5) * (int32_t)((d16 & 0xFFFFu) >> 2) + 8192) >> 14);
}
static void rg_comb_wr(int16_t *l, uint32_t *w, int32_t x, int32_t *e)
{
    x += *e;
    *e = x & 31;
    x >>= 5;
    l[*w] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    *w = *w ? *w - 1u : RG_COMB - 1u;
}
/* String::ProcessInternal (dispersion on): in (Q20) excites the string, its sum (the string and its pickup) added
 * to out. f: the pitch (Q32); s b d p: STRUCTURE BRIGHTNESS DAMPING POSITION (Q16) */
static void string_run(model_t *m, uint32_t vi, uint32_t f, int32_t s, int32_t b, int32_t d, int32_t p,
                       const int32_t *in, int32_t *out)
{
    int32_t *ln = rg_line[vi], *st = rg_stretch[vi];
    int32_t sh, disp, lfd, rt60, rtb, dcoef, bright, nfilt, dcut, stp, namt, bc, acb, apg, cpos, dcomp, ratio;
    uint32_t delay, df, i, rc;
    uint64_t period;
    int32_t dly_step, cpos_step, dcomp_step, fd_step, fb_step, nmul;
    rc = rg_rcp(f, &sh);                             /* the period, Q16: 2^48 / f */
    period = (uint64_t)rc << sh;
    delay = period < (4u << 16) ? 4u << 16 : period > ((RG_LINE - 4u) << 16) ? (RG_LINE - 4u) << 16 : (uint32_t)period;
    /* longer than the line: the string at a lower rate, delay f of it (Rings' src_ratio), its output interpolated */
    ratio = period > delay ? (int32_t)(((uint64_t)delay * f) >> 24) : 1 << 24;   /* Q24 */
    if (ratio >= 16775538)                           /* 0.9999 */
        ratio = m->src = 1 << 24;
    disp = s < 15729 ? rg_mul(s - 15729, 273023, 16) : s > 17039 ? rg_mul(s - 17039, 88562, 16) : 0;   /* Part */
    cpos = 32768 - rg_mul(64225, (p > 32768 ? p - 32768 : 32768 - p), 16);   /* 0.5 - 0.98 |p - 0.5| */
    lfd = rg_mul(d, 131072 - d, 16);
    rt60 = (int32_t)(((int64_t)exp2_q16(lfd * 8) * (int32_t)(FS * 7 / 100)) >> 16);   /* 0.07 x 2^(8 lfd) FS */
    {   /* -120 delay / rt60, Q16 semitones (rt60 >= 3087) */
        int32_t sr;
        uint32_t rr = rg_rcp((uint32_t)rt60, &sr);
        rtb = -(int32_t)((120u * (period > delay ? period : delay) * rr) >> (48 - sr));   /* (delay / src_ratio) */
    }
    rtb = rtb < -127 * 65536 ? -127 * 65536 : rtb;
    dcoef = rg_semi(rtb);
    bright = rg_mul(b, b, 16);
    nfilt = rg_semi((b - 65536) * 48);
    dcut = 24 * 65536 + rg_mul(rg_mul(d, d, 16), 48 * 65536, 16) + rg_mul(bright, 24 * 65536, 16);
    dcut = dcut > 84 * 65536 ? 84 * 65536 : dcut;
    df = rg_fmul(f, rg_semi(dcut));
    if (d >= 62259) {                                /* 0.95: towards no decay */
        int32_t ti = (d - 62259) * 20;
        dcoef += rg_mul(ti, 65536 - dcoef, 16);
        bright += rg_mul(ti, 65536 - bright, 16);
        df += (uint32_t)(((int64_t)ti * (int64_t)(2147054240u - df)) >> 16);
        dcut += rg_mul(ti, 128 * 65536 - dcut, 16);
    }
    rg_svf_set(&m->iir, df, 33554432, TAN_ACCURATE);   /* (Q 0.5) */
    {
        int32_t ix = dcut >> 16, fx = dcut & 0xFFFF;
        dcomp = 65536 - (RG_SVF_SHIFT[ix] + (((RG_SVF_SHIFT[ix < 256 ? ix + 1 : 256] - RG_SVF_SHIFT[ix]) * fx) >> 16));
    }
    stp = disp <= 0 ? 0 : rg_mul(rg_mul(disp, 131072 - disp, 16), 31130, 16);   /* x 0.475 */
    namt = disp > 49152 ? (disp - 49152) * 4 : 0;
    namt = rg_mul(rg_mul(namt, namt, 16), 1638, 16);    /* x 0.025 */
    bc = disp < 0 ? -disp : 0;
    acb = bc;
    bc = rg_mul(rg_mul(bc, bc, 16), 655, 16);           /* x 0.01 */
    {   /* -0.618 disp / (0.15 + |disp|) */
        int32_t den = 9830 + (disp < 0 ? -disp : disp);
        apg = -(int32_t)(((int64_t)40501 * disp) >> 4) / (den >> 4);
    }
    nmul = (int32_t)(0xFFFFFFFFu / (uint32_t)(13107 + nfilt));   /* 1 / (0.2 + filter), Q16 */
    if (!m->dly) {                                   /* (the first block: no ramps from nothing) */
        m->dly = delay;
        m->cpos = (uint32_t)cpos;
        m->dcomp = dcomp;
        m->fdamp = dcoef;
        m->fbright = bright;
    }
    dly_step = ((int32_t)delay - (int32_t)m->dly) / (int32_t)BLOCK;
    cpos_step = (cpos - (int32_t)m->cpos) / (int32_t)BLOCK;
    dcomp_step = (dcomp - m->dcomp) / (int32_t)BLOCK;
    fd_step = (dcoef - m->fdamp) / (int32_t)BLOCK;
    fb_step = (bright - m->fbright) / (int32_t)BLOCK;
    for (i = 0; i < BLOCK; i++) {
        int32_t sm, sac, val, h0, h1, y, nz;
        uint32_t dl, cd, apd, mdl_;
        m->src += ratio;
        if (m->src <= 1 << 24) {
            out[i] += m->so1 + rg_mul(m->so0 - m->so1, m->src, 24);
            continue;
        }
        m->src -= 1 << 24;
        m->dly += (uint32_t)dly_step;
        m->cpos += (uint32_t)cpos_step;
        m->dcomp += dcomp_step;
        m->fdamp += fd_step;
        m->fbright += fb_step;
        cd = (uint32_t)(((uint64_t)m->dly * m->cpos) >> 16);
        dl = (uint32_t)(((uint64_t)m->dly * (uint32_t)m->dcomp) >> 16) - 65536u;   /* (the IIR's delay, the FIR's) */
        nz = rg_mul(rg_noise(), nmul, 16);           /* noise / (0.2 + its filter) */
        m->dn += rg_mul(nfilt, nz - m->dn, 16);
        {
            int32_t fm = 65536 + rg_mul(m->dn >> 4, namt, 16) - rg_mul(m->cb >> 4, bc, 16);   /* (dn, cb: Q20) */
            dl = (uint32_t)(((uint64_t)dl * (uint32_t)fm) >> 16);
        }
        apd = (uint32_t)(((uint64_t)dl * (uint32_t)stp) >> 16);
        mdl_ = dl - apd;
        if (apd >= (4u << 16) && mdl_ >= (4u << 16) && (apd >> 16) < RG_STRETCH) {
            int32_t rd, wr;
            sm = rg_hermite(ln, RG_LINE, m->sw, mdl_);
            rd = rg_line_rd(st, RG_STRETCH, m->tw, apd >> 16);   /* DelayLine::Allpass */
            wr = sm + rg_mul(apg, rd, 16);
            rg_line_wr(st, RG_STRETCH, &m->tw, wr);
            sm = rd - rg_mul(wr, apg, 16);
        } else {
            sm = rg_hermite(ln, RG_LINE, m->sw, dl);
        }
        sac = sm;                                    /* DCBlocker (1 - 20 / FS); its state Q26: a Q20 one's */
        {                                            /* rounding holds it ~-54 dB off zero */
            int32_t ox = m->dcx;
            m->dcx = sac;
            m->dcy = rg_mulr(m->dcy, (1 << 30) - (int32_t)((20ll << 30) / FS), 30) + (sac - ox) * 64;
            sac = (m->dcy + 32) >> 6;
        }
        sm += rg_mul(acb, sac - sm, 16);
        val = (sm < 0 ? -sm : sm) - 26214;           /* |s| - 0.025 */
        m->cb = val > 0 ? (sm > 0 ? 2 * val : -3 * val) : 0;
        sm += in[i];
        h0 = (65536 + m->fbright) >> 1;              /* DampingFilter */
        h1 = (65536 - m->fbright) >> 2;
        y = rg_mulr(m->fdamp, rg_mulr(h0, m->fx1, 16) + rg_mulr(h1, sm + m->fx2, 16), 16);
        m->fx2 = m->fx1;
        m->fx1 = sm;
        sm = rg_svf(&m->iir, y, SVF_LP);
        rg_line_wr(ln, RG_LINE, &m->sw, sm);
        m->so1 = m->so0;
        m->so0 = sm + rg_line_lin(ln, RG_LINE, m->sw, cd);
        out[i] += m->so1 + rg_mul(m->so0 - m->so1, m->src, 24);
    }
    m->dly = delay;
    m->cpos = (uint32_t)cpos;
    m->dcomp = dcomp;
    m->fdamp = dcoef;
    m->fbright = bright;
}
/* Plucker::Trigger: a noise burst `ratio` of a period, through a comb at it and a low-pass */
static void pluck_trigger(model_t *m, uint32_t f, uint32_t cut, int32_t p)
{
    int32_t sh, ratio = rg_mul(p, 58982, 16) + 3277;   /* p 0.9 + 0.05 */
    uint32_t rc = rg_rcp(f, &sh), per = sh > 14 ? (RG_LINE - 4u) << 16 : rc << sh;   /* Q16 */
    per = (uint32_t)(((uint64_t)per * (uint32_t)ratio) >> 16);
    m->pl_n = per >> 16;
    while (per >= (255u << 16))
        per >>= 1;
    m->pl_per = per;
    m->pl_g = rg_mul(65536 - p, 52429, 16);         /* (1 - p) 0.8 */
    rg_svf_set(&m->plf, cut > 2143188436u ? 2143188436u : cut, 16777216, TAN_DIRTY);
}
static int32_t pluck_next(model_t *m, uint32_t vi, int32_t amp)
{
    int32_t x = 0, y;
    if (m->pl_n) {
        x = rg_mul(rg_noise(), amp, 15);
        m->pl_n--;
    }
    y = x + rg_mul(m->pl_g, rg_comb_lin(rg_comb[vi], m->cw, m->pl_per), 16);
    rg_comb_wr(rg_comb[vi], &m->cw, y, &m->ce);
    return rg_svf(&m->plf, y, SVF_LP);
}

/* ------------------------------------------------------------ FM --- */
static int32_t rg_sinefm(uint32_t ph, int32_t fm)   /* sin(2 pi (ph + fm)), fm Q20 turns: Q20 */
{
    uint32_t t = ph + ((uint32_t)fm << 12), i = t >> 20, f = (t >> 5) & 0x7FFFu;
    int32_t a = RG_SINE[i], b = RG_SINE[i + 1u];
    return (a + (((b - a) * (int32_t)f) >> 15)) << 5;
}
/* FMVoice::Process: in drives its envelopes (Follower); out += carrier + modulator */
static void fm_run(model_t *m, uint32_t f, int32_t s, int32_t b, int32_t d, int32_t p, const int32_t *in, int32_t *out)
{
    /* Follower::Init(8, 160, 1600 Hz): its detectors' attack and decay (Q20), its filters' 2 pi f (Q16) */
    static const int32_t FA[3] = {3804, 12030, 141235}, FD[3] = {851, 3804, 12030}, FF[2] = {1494, 14940};
    int32_t env_amt = d < 58982 ? 65536 : (65536 - d) * 10;
    int32_t art = (int32_t)(((int64_t)exp2_q16(d * 8) * (FS / 10)) >> 16);   /* the envelopes' rt60, samples */
    int32_t brt = (int32_t)(((int64_t)exp2_q16(d * 7) * (FS / 10)) >> 16);
    int32_t adec = 1854286438 / art, bdec = 1854286438 / brt;   /* 6.908 / rt60, Q28 (on Q28 envelopes: a Q20 */
                                                     /* one's floor ran them down early) */
    int32_t ix = s >> 9, fx = s & 511, ratio, fb = (p - 32768) * 2, bstep, fbstep, cstep, mstep;
    uint32_t minc, i;
    ratio = RG_FM_RATIO[ix] + (int32_t)(((int64_t)(RG_FM_RATIO[ix + 1 < 129 ? ix + 1 : 128] - RG_FM_RATIO[ix]) * fx) >> 9);
    {
        uint64_t mi = ((uint64_t)f * rg_semi28(ratio)) >> 28;
        minc = mi > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)mi;
    }
    if (!m->cinc) {                                  /* (the first block: no ramps from nothing) */
        m->cinc = f;
        m->minc = minc;
        m->fbr = b;
        m->ffb = fb;
    }
    cstep = (int32_t)(f - m->cinc) / (int32_t)BLOCK;
    mstep = (int32_t)(minc - m->minc) / (int32_t)BLOCK;
    bstep = (b - m->fbr) / (int32_t)BLOCK;
    fbstep = (fb - m->ffb) / (int32_t)BLOCK;
    for (i = 0; i < BLOCK; i++) {
        int32_t bands[3], tot = 0, w = 0, fq = 0, k, ae, be, bv, fmin, fmax, fe, fma, pf, mfb, mod, car, g, err;
        /* Follower::Process */
        m->flp[1] += rg_mul(FF[1], m->fbp[1], 16);   /* (NaiveSvf, damping 1 / 0.5) */
        bands[2] = in[i] - 2 * m->fbp[1] - m->flp[1];
        m->fbp[1] += rg_mul(FF[1], bands[2], 16);
        m->flp[0] += rg_mul(FF[0], m->fbp[0], 16);
        bands[1] = m->flp[1] - 2 * m->fbp[0] - m->flp[0];
        m->fbp[0] += rg_mul(FF[0], bands[1], 16);
        bands[0] = m->flp[0];
        for (k = 0; k < 3; k++) {
            int32_t a = bands[k] < 0 ? -bands[k] : bands[k], e2 = a - m->det[k];
            m->det[k] += rg_mul(e2 > 0 ? FA[k] : FD[k], e2, 20);
            w += rg_mul(m->det[k], fq, 16);
            tot += m->det[k];
            fq += 32768;
        }
        {   /* weighted / (total + 0.001), Q20 */
            int32_t sh;
            uint32_t r = rg_rcp((uint32_t)(tot + 1049), &sh);
            err = (int32_t)(((int64_t)w * r) >> (28 - sh)) - m->cent;
        }
        m->cent += rg_mul(err, err > 0 ? 3277 : 66, 16);
        ae = tot;
        be = rg_mul(rg_mul(m->cent, 2 * ae, 20), 2 * RG_ONE - ae, 20);
        err = ae * 256 - m->aenv;                    /* SLOPE */
        m->aenv += err > 0 ? rg_mul(err, 3277, 16) : rg_mul(err, adec, 28);
        err = be * 256 - m->benv;
        m->benv += err > 0 ? rg_mul(err, 655, 16) : rg_mul(err, bdec, 28);
        m->fbr += bstep;
        bv = rg_mul(m->fbr, m->fbr, 16);              /* Q16 */
        fmin = bv < 32768 ? 0 : bv * 2 - 65536;
        fmax = bv < 32768 ? 2 * bv : 65536;
        fe = 32768 + rg_mul(env_amt, (m->benv >> 12) - 32768, 16);
        fma = (fmin + rg_mul(fmax, fe, 16)) * 2;
        err = fma - m->fma;                          /* SLEW */
        {
            int32_t dl = 328 + rg_mul(fmax, 983, 16);
            m->fma += err > dl ? dl : err < -dl ? -dl : err;
        }
        m->ffb += fbstep;
        pf = m->ffb < 0 ? rg_mul(m->ffb, m->ffb, 17) : 0;   /* 0.5 fb^2 */
        m->minc += (uint32_t)mstep;
        m->cinc += (uint32_t)cstep;
        m->mph += (uint32_t)(((uint64_t)m->minc * (uint32_t)((1 << 30) + (int32_t)(((int64_t)m->prev * pf) >> 6))) >> 30);
        m->cph += m->cinc;
        mfb = m->ffb > 0 ? rg_mul(m->ffb, m->ffb, 18) : 0;  /* 0.25 fb^2 */
        mod = rg_sinefm(m->mph, rg_mul(mfb, m->prev, 16));
        car = rg_sinefm(m->cph, rg_mul(m->fma, mod, 16));
        m->prev += (car - m->prev) / 10;
        g = RG_ONE + rg_mul(env_amt, (m->aenv >> 8) - RG_ONE, 16);
        m->gain += rg_mul(g - m->gain, 83886 + rg_mul(754975, m->fma, 16), 24);   /* 0.005 + 0.045 fm, Q24 */
        out[i] += rg_mul(car + mod, m->gain, 20);
    }
    m->cinc = f;
    m->minc = minc;
    m->fbr = b;
    m->ffb = fb;
}

/* ------------------------------------------------------------ the voice --- */
static void model_reset(model_t *m, uint32_t vi)
{
    uint8_t keep[8];                                 /* (the knobs and want: the kit's) */
    memcpy(keep, &m->type, 8);
    memset(m, 0, sizeof *m);
    memcpy(&m->type, keep, 8);
    m->ran = m->type;
    m->peak = RG_ONE >> 1;
    m->src = 1 << 24;                                /* (a hit's first sample, at once) */
    m->sw = m->tw = m->cw = 0;
    memset(rg_line[vi], 0, sizeof rg_line[vi]);
    memset(rg_stretch[vi], 0, sizeof rg_stretch[vi]);
    memset(rg_comb[vi], 0, sizeof rg_comb[vi]);
}
/* a hit: the internal exciter goes at the next block; TENSION up (to an octave at full velocity) */
static void model_trigger(uint32_t vi, uint32_t vel, uint32_t inc)
{
    model_t *m = &mdl[vi];
    (void)inc;
    m->strum = 1;
    m->vel = (int32_t)(vel * 32767u / 127u);
    m->ten = (int32_t)(m->tens * (uint32_t)m->vel >> 7);
}
/* 0.15 (17c): at most model_cap models play at once, the first in D1, D2, D3 order; the others play as LXR voices.
 * 2 (D1-D3 render on the second core, voices.c); 1 on one core (main.c), where two can pass 100% */
static uint8_t model_cap = 2;
static void model_limit(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < 3u; k++) {
        uint8_t t = n < model_cap ? mdl[k].want : 0u;
        n += t != 0u;
        mdl[k].type = t;
    }
}
static uint32_t modal_budget(void)                   /* the modes a MODAL voice runs (Rings: by its polyphony) */
{
    uint32_t k, n = 0;
    for (k = 0; k < 3u; k++)
        n += mdl[k].type == MD_MODAL;
    return n <= 1u ? 32u : n == 2u ? 24u : 16u;
}
/* the block: buf in (the voice's own sound, the share EXCITER gives it; 0 when it has none), the model's out */
static void model_block(uint32_t vi, const voice_t *v, int16_t *buf)
{
    model_t *m = &mdl[vi];
    int32_t in[BLOCK], acc[BLOCK], s = m->mat * 516, b = m->damp * 516, d = m->dec * 516, p = m->pos * 516;
    int32_t ex = m->exc * 516, cut, gain, vint;
    uint32_t f, fc, i;
    if (m->ran != m->type)
        model_reset(m, vi);
    s = s > 65535 ? 65535 : s;
    b = b > 65535 ? 65535 : b;
    d = d > 65535 ? 65535 : d;
    p = p > 65535 ? 65535 : p;
    ex = ex > 65536 ? 65536 : ex;
    f = (uint32_t)(((uint64_t)v->osc.inc * (uint32_t)(65536 + m->ten)) >> 16);
    f = f < 1000000u ? 1000000u : f > 2104533975u ? 2104533975u : f;   /* (10 Hz .. 0.49) */
    m->ten -= m->ten >> 5;                           /* TENSION falls back (~45 ms) */
    for (i = 0; i < BLOCK; i++) {
        in[i] = rg_mul(buf[i] << 5, ex, 16);        /* (Q20) */
        acc[i] = 0;
    }
    vint = rg_mul(m->vel, 65536 - ex, 16);           /* the internal exciter's level, Q15 */
    /* Part::Process: the excitation filter's cutoff follows the pitch and the brightness */
    cut = rg_mul(b, 131072 - b, 16);
    fc = rg_fmul(f, exp2_q16((cut - 32768) * 8));
    if (m->type == MD_FM) {
        if (m->strum) {                              /* TriggerInternalEnvelope (its level: the hit's) */
            m->aenv = m->benv = vint << 13;
            m->strum = 0;
        }
        fm_run(m, f, s, b, d, p, in, acc);
        gain = 45875;                                /* Part::model_gains_: 0.7 */
    } else if (m->type == MD_MODAL) {
        uint32_t nm;
        rg_svf_set(&m->exf, fc, 11184811, TAN_DIRTY);   /* (Q 1.5) */
        in[0] = rg_svf(&m->exf, in[0], SVF_LP);
        if (m->strum) {   /* the pulse A = 0.25 2^(2 fc^2) / fc into the filter: its first output g^2 h A, its states
                           * 2 g h A and 2 g^2 h A (g A = 0.25 2^(2 fc^2) (pi + a fc^2): bounded, where A is not) */
            int64_t fq = (int64_t)(fc >> 2), f2 = (fq * fq) >> 30;   /* Q30 */
            int64_t ga = ((int64_t)exp2_q16((int32_t)(f2 >> 13)) * (52707179 + ((194346347 * f2) >> 30))) >> 22;
            int32_t k = (int32_t)((((ga * vint) >> 15) * m->exf.h) >> 30), gk = rg_mul(m->exf.g, k, 24);
            in[0] += gk;
            m->exf.s1 += 128 * k;                    /* (the states Q26) */
            m->exf.s2 += 128 * gk;
            m->strum = 0;
        }
        for (i = 1; i < BLOCK; i++)
            in[i] = rg_svf(&m->exf, in[i], SVF_LP);
        nm = modal_coefs(m, f, s, rg_mul(b, b, 16), d, p, modal_budget());
        modal_run(m, in, acc, nm);
        gain = 91750;                                /* 1.4 */
    } else {
        rg_svf_set(&m->exf, fc, 11184811, TAN_DIRTY);
        if (m->strum) {
            uint64_t c8 = (uint64_t)fc * 8u;
            pluck_trigger(m, f, c8 > 2143188436u ? 2143188436u : (uint32_t)c8, p);
            m->strum = 0;
        }
        for (i = 0; i < BLOCK; i++) {
            int32_t x = rg_svf(&m->exf, rg_mul(in[i], 46341, 16), SVF_LP) + pluck_next(m, vi, vint), ox = m->ex_x;
            m->ex_x = x;                             /* DCBlocker (1 - 10 / FS), its state Q26 (as the string's) */
            m->ex_y = rg_mulr(m->ex_y, (1 << 30) - (int32_t)((10ll << 30) / FS), 30) + (x - ox) * 64;
            in[i] = (m->ex_y + 32) >> 6;
        }
        string_run(m, vi, f, s, b, d, p, in, acc);
        gain = 91750;
    }
#ifdef RINGS_TAP
    memcpy(rg_tap, acc, sizeof acc);                 /* (tests/rings_compare.cc: before the limiter) */
#endif
    for (i = 0; i < BLOCK; i++) {                    /* rings::Limiter: the peak above full scale down, x 0.8 */
        int32_t x = rg_mul(acc[i], gain, 16), a = x < 0 ? -x : x, e = a - m->peak, g = RG_ONE;
        m->peak += e > 0 ? rg_mul(e, 3277, 16) : -(int32_t)(((int64_t)-e * 21) >> 20);   /* 0.05, 0.00002 */
        if (m->peak > RG_ONE) {                      /* 2^40 / peak */
            int32_t sh;
            uint32_t r = rg_rcp((uint32_t)m->peak, &sh);
            g = (int32_t)(sh >= 8 ? r << (sh - 8) : r >> (8 - sh));
        }
        x = rg_mul(rg_mul(x, g, 20), 52429, 16) >> 5;   /* (Q15) */
        buf[i] = (int16_t)soft_clip(x);
    }
}
