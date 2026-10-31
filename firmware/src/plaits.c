/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 0.15 (Phase 16): the DRUM voices' MODEL section, part two: a port of Mutable Instruments Plaits'
 * sixteen original engines (plaits/dsp: the engines, oscillators, drums, noise, physical models, chords, speech and
 * fx they use, and stmlib/dsp; Copyright 2016-2023 Emilie Gillet, MIT licence) to fixed point (the FM-1 has no FPU).
 *
 * Each engine renders as Plaits' does, struck at the voice's hits (Plaits with its TRIGGER patched): the voice's
 * pitch is its note, the knobs are HARMONICS, TIMBRE and MORPH, the hit's velocity its accent. Plaits' outputs are
 * OUT and AUX; AUX blends from one to the other. The engines that envelope themselves (the drums, the string, modal)
 * play as they are; the others go through the voice's amp EG, and COLOUR turns that into Plaits' low-pass gate (0: a
 * plain VCA; up: the filter closes with the EG, as a vactrol's). ENV is how far the amp EG opens TIMBRE and MORPH
 * (Plaits' internal envelope).
 *
 * Signals and most values Q24 (1.0 = 2^24: Plaits' float, with 128 of headroom), phases and frequencies Q32 (cycles a
 * sample), notes and semitones Q16, envelopes that decay for long Q30. Plaits runs at 48 kHz in blocks of 12; here at
 * 44.1 kHz in blocks of PLSUB (16), four to Elixir's BLOCK. The random generator is stmlib's (an LCG), so a host test
 * (tests/plaits_compare.cc) holds each engine to Plaits' own float code sample by sample. Left out: Plaits' sustain
 * modes (no TRIGGER patched: the voice is always struck), its user data, its 1.2 engines. The string engine plays one
 * string a voice (Plaits: three, overlapping), in the memory a voice's Rings string has; the particle engine's diffuser
 * (16 KB) is one, for the first voice on PARTICLE; the others play it dry. */

#include "plaits_tables.h"

#define PL1 (1 << 24)
#define PLSUB 16u
#define PL_L2C 341464474                             /* log2(2^32 x 55 / 4 / FS), Q24: Plaits' NoteToFrequency's a0 / 4 */
enum {
    PE_VA, PE_WSHP, PE_FM, PE_GRAIN, PE_ADD, PE_WTBL, PE_CHORD, PE_SPEECH,
    PE_SWARM, PE_NOISE, PE_PART, PE_STRING, PE_MODAL, PE_BD, PE_SD, PE_HH, NPE
};
/* Plaits' post-processing per engine: OUT's and AUX's gains (Q24; negative: through its limiter), and whether the
 * engine envelopes itself (voice.cc's RegisterInstance) */
static const int32_t PE_OUTG[NPE] = {13421773, 11744051, 10066330, 11744051, 13421773, 10066330, 13421773, -11744051,
                                     -50331648, -16777216, -33554432, -16777216, -16777216, 13421773, 13421773, 13421773};
static const int32_t PE_AUXG[NPE] = {13421773, 10066330, 10066330, 10066330, 13421773, 10066330, 13421773, 13421773,
                                     16777216, -16777216, 16777216, 13421773, 13421773, 13421773, 13421773, 13421773};
static const uint8_t PE_ENV[NPE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1};

/* ------------------------------------------------------------ helpers --- */
static inline int32_t pm(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 24); }
static inline int32_t pmr(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 23)) >> 24); }
static inline int32_t pclamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline int32_t pabs(int32_t x) { return x < 0 ? -x : x; }
static inline int32_t pmin(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t pmax(int32_t a, int32_t b) { return a > b ? a : b; }
static uint32_t pl_rng;                               /* stmlib::Random */
static inline uint32_t pl_word(void) { return pl_rng = pl_rng * 1664525u + 1013904223u; }
static inline int32_t pl_randf(void) { return (int32_t)(pl_word() >> 8); }   /* GetFloat: [0, 1), Q24 */

/* 2^(y / 2^24), y Q24 octaves, in Q(q); saturates */
static uint32_t pl_exp2(int32_t y, int32_t q)
{
    int32_t n = y >> 24, sh;
    uint32_t fr = (uint32_t)y & 0xFFFFFFu, i = fr >> 16, f = fr & 0xFFFFu;
    uint64_t m = PL_E2[i] + (((uint64_t)(PL_E2[i + 1] - PL_E2[i]) * f) >> 16);   /* Q30 */
    sh = n + q - 30;
    if (sh >= 0) {
        if (sh >= 32 || (m << sh) > 0xFFFFFFFFull)
            return 0xFFFFFFFFu;
        return (uint32_t)(m << sh);
    }
    return sh <= -40 ? 0u : (uint32_t)(m >> -sh);
}
/* stmlib's SemitonesToRatio: 2^(st / 12), st Q16 semitones, in Q(q) */
static inline uint32_t pl_semi(int32_t st, int32_t q) { return pl_exp2((int32_t)(((int64_t)st * 1398101) >> 16), q); }
/* log2(x), Q24, x > 0 */
static int32_t pl_log2(uint32_t x)
{
    int32_t z = __builtin_clz(x);
    uint32_t m = x << z, i = (m >> 23) & 255u, f = (m >> 7) & 0xFFFFu;
    return ((31 - z) << 24) + PL_L2[i] + (int32_t)(((int64_t)(PL_L2[i + 1] - PL_L2[i]) * f) >> 16);
}
/* NoteToFrequency: a note (Q16 semitones, MIDI's) to a frequency, Q32 */
static uint32_t pl_note_f(int32_t note)
{
    int32_t n = pclamp(note - 9 * 65536, -128 * 65536, 127 * 65536);
    return pl_exp2(PL_L2C + (int32_t)(((int64_t)n * 1398101) >> 16), 0);
}
static int32_t pl_f_note(uint32_t f) { return 9 * 65536 + (int32_t)(((int64_t)(pl_log2(f) - PL_L2C) * 12) >> 8); }
/* a / b, Q24 (by rg_rcp's 17 bits: b's sign kept) */
static int32_t pl_div(int32_t a, int32_t b)
{
    int32_t sh, k, neg = (b < 0) ^ (a < 0);
    uint32_t r;
    int64_t p;
    if (!b)
        return a >= 0 ? 0x7FFFFFFF : -0x7FFFFFFF;
    r = rg_rcp((uint32_t)pabs(b), &sh);
    p = (int64_t)pabs(a) * r;
    k = 24 - sh;
    if (k < 0) {
        p = p >= (1ll << (62 + k)) ? 0x7FFFFFFF : p << -k;
    } else {
        p >>= k;
    }
    if (p > 0x7FFFFFFF)
        p = 0x7FFFFFFF;
    return neg ? -(int32_t)p : (int32_t)p;
}
/* 1 / x of a Q8 value, as Q24 (Svf's r = 1 / resonance) */
static int32_t pl_inv8(uint32_t q8)
{
    int32_t sh;
    uint32_t r = rg_rcp(q8 ? q8 : 1u, &sh);
    return sh >= 16 ? (int32_t)(r << (sh - 16)) : (int32_t)(r >> (16 - sh));
}
static uint32_t pl_isqrt64(uint64_t x)               /* floor(sqrt(x)) */
{
    uint64_t r = 0, b = 1ull << 62;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (uint32_t)r;
}
static inline int32_t pl_sqrt(int32_t x) { return x <= 0 ? 0 : (int32_t)pl_isqrt64((uint64_t)x << 24); }   /* Q24 */
/* Sine(phase): sin(2 pi phase), Q24, by Elixir's 4096-point table */
static inline int32_t pl_sin(uint32_t ph)
{
    uint32_t i = ph >> 20, f = (ph >> 5) & 0x7FFFu;
    int32_t a = RG_SINE[i], b = RG_SINE[i + 1u];
    return (a << 9) + (((b - a) * (int32_t)f) >> 6);
}
static inline uint32_t pl_fq(int32_t x) { return (uint32_t)x << 8; }   /* a Q24 fraction of a cycle as Q32 */
/* SoftLimit, SoftClip (stmlib) */
static int32_t pl_softlimit(int32_t x)
{
    int32_t x2 = pm(x, x);
    return pl_div(pm(x, 27 * PL1 + x2), 27 * PL1 + 9 * x2);
}
static inline int32_t pl_softclip(int32_t x) { return x <= -3 * PL1 ? -PL1 : x >= 3 * PL1 ? PL1 : pl_softlimit(x); }
/* polyBLEP (stmlib), t Q24 */
static inline int32_t blep_this(int32_t t) { return pm(t, t) >> 1; }
static inline int32_t blep_next(int32_t t) { t = PL1 - t; return -(pm(t, t) >> 1); }
static inline int32_t iblep_next(int32_t t)
{
    int32_t t1 = t >> 1, t2 = pm(t1, t1), t4 = pm(t2, t2);
    return 3145728 - t1 + t2 + (t2 >> 1) - t4;
}
static inline int32_t iblep_this(int32_t t) { return iblep_next(PL1 - t); }
/* t = phase / frequency (both Q32, phase < frequency), Q24 */
static inline int32_t pl_frac(uint32_t ph, uint32_t f)
{
    int32_t sh;
    uint32_t r = rg_rcp(f ? f : 1u, &sh);
    uint64_t p = ((uint64_t)ph * r) >> (24 - (sh < 24 ? sh : 24));
    p = sh > 24 ? p << (sh - 24) : p;
    return p >= PL1 ? PL1 - 1 : (int32_t)p;
}
/* ONE_POLE, SLOPE */
#define PL_ONE_POLE(o, i, c) ((o) += pm((c), (i) - (o)))
static inline void pl_slope(int32_t *o, int32_t in, int32_t up, int32_t down)
{
    int32_t e = in - *o;
    *o += pm(e > 0 ? up : down, e);
}
/* an envelope's decay: x (Q30) x keep (Q32, 1 - its loss) */
static inline int32_t pl_keep(int32_t x, uint32_t keep) { return (int32_t)(((int64_t)x * keep) >> 32); }
static inline uint32_t pl_keepq(int32_t loss) { return loss >= PL1 ? 0u : 0xFFFFFFFFu - ((uint32_t)loss << 8); }

/* stmlib's Svf: g r Q24, h Q30, states Q24 */
typedef struct { int32_t g, r, rg, h, s1, s2; } pl_svf_t;
static void pl_svf_gr(pl_svf_t *s, int32_t g, int32_t r)
{
    int32_t sh;
    uint32_t d = (uint32_t)(PL1 + pm(r, g) + pm(g, g)), rc = rg_rcp(d, &sh);
    s->g = g;
    s->r = r;
    s->rg = r + g;
    s->h = (int32_t)(rc << (sh + 6));
}
/* set_f_q: f Q32 (< 0.5), resonance Q8 */
static inline void pl_svf_fq(pl_svf_t *s, uint32_t f, uint32_t q8, uint32_t mode)
{
    pl_svf_gr(s, rg_tan(f > 2143188436u ? 2143188436u : f, mode), pl_inv8(q8));
}
static inline void pl_svf_run(pl_svf_t *s, int32_t in, int32_t *lp, int32_t *bp, int32_t *hp)
{
    int64_t t = ((int64_t)in << 24) - (int64_t)s->rg * s->s1 - ((int64_t)s->s2 << 24);
    int32_t h = (int32_t)((((t + (1 << 23)) >> 24) * s->h + (1 << 29)) >> 30);
    int32_t gh = pmr(s->g, h), b = gh + s->s1, gb, l;
    s->s1 = gh + b;
    gb = pmr(s->g, b);
    l = gb + s->s2;
    s->s2 = gb + l;
    if (lp)
        *lp = l;
    if (bp)
        *bp = b;
    if (hp)
        *hp = h;
}
static inline int32_t pl_svf_lp(pl_svf_t *s, int32_t in) { int32_t y; pl_svf_run(s, in, &y, 0, 0); return y; }
static inline int32_t pl_svf_bp(pl_svf_t *s, int32_t in) { int32_t y; pl_svf_run(s, in, 0, &y, 0); return y; }
static inline int32_t pl_svf_hp(pl_svf_t *s, int32_t in) { int32_t y; pl_svf_run(s, in, 0, 0, &y); return y; }
/* stmlib's OnePole: g Q24, gi = 1 / (1 + g) Q24 */
typedef struct { int32_t g, gi, s; } pl_op_t;
static void pl_op_f(pl_op_t *o, uint32_t f, uint32_t mode)
{
    o->g = rg_tan(f > 2143188436u ? 2143188436u : f, mode);
    o->gi = pl_div(PL1, PL1 + o->g);
}
static inline int32_t pl_op_lp(pl_op_t *o, int32_t in)
{
    int32_t lp = pm(pm(o->g, in) + o->s, o->gi);
    o->s = pm(o->g, in - lp) + lp;
    return lp;
}
/* the stmlib Limiter (out's gain negative): x pre-gain, the peak's follower, x 0.8 */
static int32_t pl_limit(int32_t *peak, int32_t x, int32_t pre)
{
    int32_t s = pm(x, pre);
    pl_slope(peak, pabs(s), 838861, 336);            /* 0.05, 0.00002 */
    if (*peak > PL1)
        s = pl_div(s, *peak);
    return pm(s, 13421773);
}

/* the engines' parameters, a block (EngineParameters) */
typedef struct {
    int32_t note;                                    /* Q16 semitones */
    uint32_t f0;                                     /* NoteToFrequency(note), Q32 */
    int32_t harm, timbre, morph, accent;             /* Q24 0..1 */
    uint8_t trig;                                    /* a rising edge at this block */
} pl_par_t;

/* -------------------------------------------------- analog bass drum --- */
static inline int32_t pl_diode(int32_t x)
{
    if (x >= 0)
        return x;
    x *= 2;
    return pl_div(pm(11744051, x), PL1 + pabs(x));   /* 0.7 x / (1 + |x|) */
}
typedef struct {
    int32_t prem, frem, pulse, ph, plp, flp, retrig, lp_out, tone_lp;
    pl_svf_t res;
    int32_t od_pre, od_post;
    /* the synthetic one (AUX) */
    uint32_t sph, sf0;
    int32_t pnoise, fm, fm_lp, benv, benv_lp, tenv, tenv_lp, stone_lp, bpw, fpw;   /* envelopes Q30 */
    int32_t c_lp, c_hp, n_lp, n_hp;
    pl_svf_t click;
} pl_bd_t;
static int32_t bd_dsine(uint32_t ph, int32_t pn, int32_t dirt)   /* SyntheticBassDrum::DistortedSine */
{
    uint32_t p = ph + pl_fq(pm(pn, dirt));
    int32_t x = (int32_t)(p >> 8), tri = (x < (PL1 >> 1) ? x : PL1 - x) * 4 - PL1;
    int32_t sine = pl_div(2 * tri, PL1 + pabs(tri)), clean = pl_sin(p + 0xC0000000u);
    return sine + pm(PL1 - dirt, clean - sine);
}
static inline int32_t bd_vca(int32_t s, int32_t g)       /* TransistorVCA */
{
    s = pm(s - 10066330, g);
    return pl_div(3 * s, 2 * PL1 + pabs(s)) + pm(g, 5033165);
}
static void bd_init(pl_bd_t *d)
{
    memset(d, 0, sizeof *d);
    pl_svf_fq(&d->click, (uint32_t)(5000ull * 4294967296ull / FS), 512, TAN_FAST);
}
static void bd_render(pl_bd_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    uint32_t i, f0 = p->f0;
    int32_t h4 = pmin(p->harm * 4, PL1), afm = h4, sfm = pclamp(p->harm * 4 - PL1, 0, PL1);
    int32_t drive = pm(pmax(p->harm * 2 - PL1, 0), pmax(PL1 - (int32_t)((uint64_t)f0 >> 4), 0));
    int32_t scale, exl, tone_f, sh;
    uint32_t q8;
    uint64_t tf;
    {   /* analog: scale = 0.001 / f0; q = 1500 2^(decay 80 / 12) */
        uint32_t r = rg_rcp(f0 > 33554u ? f0 : 33554u, &sh);
        scale = (int32_t)(((uint64_t)r * 4294967u) >> (24 - sh));
    }
    q8 = (uint32_t)(((uint64_t)pl_semi(pm(p->morph, 80 << 16) >> 0, 16) * 1500u * 256u) >> 16);
    tf = ((uint64_t)pl_semi(pm(p->timbre, 108 << 16), 16) * f0) >> 14;   /* 4 f0 2^(tone 9), Q32 */
    tone_f = tf >= (1ull << 32) ? PL1 : (int32_t)(tf >> 8);
    exl = pm(1342177, p->timbre + (PL1 >> 2));       /* 0.08 (tone + 0.25) */
    if (p->trig) {
        d->prem = 44;                                /* 1 ms */
        d->frem = 264;                               /* 6 ms */
        d->ph = 3 * PL1 + pm(7 * PL1, p->accent);
        d->lp_out = 0;
    }
    for (i = 0; i < n; i++) {
        int32_t pulse, fmp = 0, punch, ro, factor;
        uint32_t fr;
        if (d->prem) {
            --d->prem;
            pulse = d->prem ? d->ph : d->ph - PL1;
            d->pulse = pulse;
        } else {
            d->pulse -= pm(d->pulse, 1902251);        /* 1 / (0.2 ms) */
            pulse = d->pulse;
        }
        PL_ONE_POLE(d->plp, pulse, 3804357);          /* 1 / (0.1 ms) */
        pulse = pl_diode((pulse - d->plp) + pm(pulse, 738198));   /* x 0.044 */
        if (d->frem) {
            --d->frem;
            fmp = PL1;
            d->retrig = d->frem ? 0 : -13421773;      /* -0.8 */
        } else {
            d->retrig -= pm(d->retrig, 7609);         /* 1 / 50 ms */
        }
        PL_ONE_POLE(d->flp, fmp, 3804357);
        punch = 11744051 + pl_diode(10 * d->lp_out - PL1);
        factor = PL1 + pm(pm(d->flp, 28521267), afm) + pm(pm(punch, 1342177), sfm);   /* 1.7, 0.08 */
        fr = (uint32_t)(((uint64_t)f0 * (uint32_t)pmax(factor, 0)) >> 24);
        fr = fr > 1717986918u ? 1717986918u : fr;    /* 0.4 */
        {
            uint64_t qq = 256u + (((uint64_t)q8 * fr) >> 32);
            pl_svf_fq(&d->res, fr, qq > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)qq, TAN_DIRTY);
        }
        pl_svf_run(&d->res, pm(pulse - pm(d->retrig, 3355443), scale), &d->lp_out, &ro, 0);
        PL_ONE_POLE(d->tone_lp, pm(pulse, exl) + ro, tone_f);
        out[i] = d->tone_lp;
    }
    {   /* Overdrive (0.5 + 0.5 drive) */
        int32_t dr = (PL1 >> 1) + (drive >> 1), d2 = pm(dr, dr), pa = dr >> 1, pb = pm(pm(pm(d2, d2), dr), 24 * PL1);
        int32_t pre = pa + pm(pb - pa, d2), sq = pm(dr, 2 * PL1 - dr);
        int32_t post = pl_div(PL1, pl_softclip(5536481 + pm(sq, pre - 5536481)));   /* 0.33 */
        int32_t pst = (pre - d->od_pre) / (int32_t)n, qst = (post - d->od_post) / (int32_t)n;
        for (i = 0; i < n; i++) {
            d->od_pre += pst;
            d->od_post += qst;
            out[i] = pm(pl_softclip(pm(d->od_pre, out[i])), d->od_post);
        }
        d->od_pre = pre;
        d->od_post = post;
    }
    {   /* the synthetic one */
        int32_t dec = pm(p->morph, p->morph), dirt, fmamt = pmin(p->harm * 2, PL1);
        int32_t fmd = pmax(p->harm * 2 - PL1, 0), f0step, tl = p->timbre;
        uint32_t kfm, kbody, ktr = pl_keepq(76087);  /* 1 / (5 ms) */
        uint64_t fq;
        int32_t stone;
        fmd = pm(fmd, fmd);
        dirt = pm(6710886 - pm(4194304, dec), pmax(PL1 - (int32_t)((uint64_t)f0 >> 5), 0));   /* (0.4 - 0.25 m^2) */
        kfm = pl_keepq(pl_div(47554, PL1 + 4 * fmd));       /* 1 / (8 ms (1 + 4 d)) */
        kbody = pl_keepq((int32_t)(((int64_t)19021 * pl_semi(-pm(dec, 60 << 16), 24)) >> 24));   /* 1 / 20 ms */
        fq = ((uint64_t)pl_semi(pm(p->timbre, 108 << 16), 16) * f0) >> 14;
        stone = fq >= (1ull << 32) ? PL1 : (int32_t)(fq >> 8);
        if (p->trig) {
            d->fm = 1 << 30;
            d->benv = d->tenv = (int32_t)(((int64_t)(5033165 + pm(11744051, p->accent))) << 6);   /* Q30 */
            d->bpw = 44;
            d->fpw = 57;
        }
        f0step = (int32_t)(f0 - d->sf0) / (int32_t)n;
        for (i = 0; i < n; i++) {
            int32_t mix = 0, body, tr, x;
            PL_ONE_POLE(d->pnoise, pl_randf() - (PL1 >> 1), 33554);   /* 0.002 */
            if (d->fpw) {
                --d->fpw;
                d->sph = 0x40000000u;
            } else {
                uint64_t inc;
                d->fm = pl_keep(d->fm, kfm);
                d->sf0 += (uint32_t)f0step;          /* (the ramp stands still in the FM pulse, as Plaits') */
                inc = ((uint64_t)d->sf0 * (uint32_t)(PL1 + pm(pm(fmamt, 58720256), d->fm_lp))) >> 24;   /* 3.5 */
                d->sph += inc > 0x80000000ull ? 0x80000000u : (uint32_t)inc;
            }
            if (d->bpw) {
                --d->bpw;
            } else {
                d->benv = pl_keep(d->benv, kbody);
                d->tenv = pl_keep(d->tenv, ktr);
            }
            PL_ONE_POLE(d->benv_lp, d->benv >> 6, 1677722);   /* 0.1 */
            PL_ONE_POLE(d->tenv_lp, d->tenv >> 6, 1677722);
            PL_ONE_POLE(d->fm_lp, d->fm >> 6, 1677722);
            body = bd_dsine(d->sph, d->pnoise, dirt);
            x = d->bpw ? 0 : PL1;                    /* the click: SLOPE, ONE_POLE, a low-pass */
            pl_slope(&d->c_lp, x, PL1 >> 1, 1677722);
            PL_ONE_POLE(d->c_hp, d->c_lp, 671089);   /* 0.04 */
            tr = pl_svf_lp(&d->click, d->c_lp - d->c_hp);
            PL_ONE_POLE(d->n_lp, pl_randf(), 838861);     /* the attack noise: 0.05, 0.005 */
            PL_ONE_POLE(d->n_hp, d->n_lp, 83886);
            tr += d->n_lp - d->n_hp;
            mix -= bd_vca(body, d->benv_lp);
            mix -= pm(pm(tr, d->tenv_lp), tl);
            PL_ONE_POLE(d->stone_lp, mix, stone);
            aux[i] = d->stone_lp;
        }
    }
}

/* -------------------------------------------------- snare drum --- */
typedef struct {
    int32_t prem, pulse, ph, plp, nenv;              /* nenv Q29 */
    pl_svf_t res[5], nf;
    /* the synthetic one */
    int32_t phs[2];                                  /* Q30 (Plaits' 909 coupling sends them below 0) */
    int32_t damp, samp, fm, hold;                    /* Q30 */
    pl_op_t dlp, shp;
    pl_svf_t slp;
} pl_sd_t;
static void sd_init(pl_sd_t *d) { memset(d, 0, sizeof *d); }
static inline int32_t sd_dsine(int32_t ph)
{
    int32_t x = ph >> 6, tri = (x < (PL1 >> 1) ? x : PL1 - x) * 4 - 21810381;   /* - 1.3 */
    return pl_div(2 * tri, PL1 + pabs(tri));
}
static void sd_render(pl_sd_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    static const int32_t MF[5] = {16777216, 33554432, 53351547, 69793219, 94287954};   /* 1, 2, 3.18, 4.16, 5.62 */
    uint32_t i, k, f0 = p->f0, f[5], q8;
    int32_t dec = p->morph, sn = p->harm, tone = p->timbre, dxt, exl, gain[5];
    uint32_t knoise;
    dxt = pm(dec, PL1 + pm(dec, dec - PL1));
    q8 = (uint32_t)(((uint64_t)pl_semi(pm(dxt, 84 << 16), 16) * 2000u * 256u) >> 16);
    knoise = pl_keepq((int32_t)(((int64_t)28521 * pl_semi(-pm(dec, (50 << 16) + pm(sn, 10 << 16)), 24)) >> 24));   /* 0.0017 */
    exl = pm(pm(sn, 2 * PL1 - sn), 1677722);
    sn = pclamp(pm(sn, 18454938) - 838861, 0, PL1);  /* x 1.1 - 0.05 */
    if (p->trig) {
        d->prem = 44;
        d->ph = 3 * PL1 + pm(7 * PL1, p->accent);
        d->nenv = 1 << 30;                           /* 2.0 */
    }
    for (k = 0; k < 5; k++) {
        uint64_t fk = ((uint64_t)f0 * (uint32_t)MF[k]) >> 24;
        uint32_t qk = k ? q8 >> 2 : q8;
        f[k] = fk > 2143188436u ? 2143188436u : (uint32_t)fk;
        pl_svf_fq(&d->res[k], f[k], 256u + (uint32_t)(((uint64_t)qk * f[k]) >> 32), TAN_FAST);
    }
    if (tone < 11184811) {                           /* 808 (2 modes) */
        int32_t t = pm(tone, 25165824), u = PL1 - t;
        gain[0] = 25165824 + pm(pm(u, u), 75497472);
        gain[1] = 2 * t + 2516582;
        gain[2] = gain[3] = gain[4] = 0;
    } else {
        int32_t t = (tone - 11184811) * 3;
        gain[0] = 25165824 - (t >> 1);
        gain[1] = 36071014 - pm(t, 11744051);
        for (k = 2; k < 5; k++) {
            gain[k] = t;
            t = pm(t, t);
        }
    }
    {
        uint64_t fn = (uint64_t)f0 * 16u;
        uint32_t fnn = fn > 2143188436u ? 2143188436u : (uint32_t)fn;
        pl_svf_fq(&d->nf, fnn, 256u + (uint32_t)(((uint64_t)fnn * 384u) >> 32), TAN_FAST);   /* 1 + 1.5 f */
    }
    for (i = 0; i < n; i++) {
        int32_t pulse, shell = 0, noise;
        if (d->prem) {
            --d->prem;
            pulse = d->prem ? d->ph : d->ph - PL1;
            d->pulse = pulse;
        } else {
            d->pulse -= pm(d->pulse, 3804357);        /* 1 / (0.1 ms) */
            pulse = d->pulse;
        }
        PL_ONE_POLE(d->plp, pulse, 12582912);         /* 0.75 */
        for (k = 0; k < 5; k++) {
            int32_t ex = k == 0 ? (pulse - d->plp) + pm(pulse, 100663) : pm(pulse, 436208);   /* 0.006, 0.026 */
            shell += pm(gain[k], pl_svf_bp(&d->res[k], ex) + pm(ex, exl));
        }
        shell = pl_softclip(shell);
        noise = 2 * pl_randf() - PL1;
        noise = noise < 0 ? 0 : noise;
        d->nenv = pl_keep(d->nenv, knoise);
        noise = pm(pm(noise, d->nenv >> 5), sn) * 2;
        noise = pl_svf_bp(&d->nf, noise);
        out[i] = noise + pm(shell, PL1 - sn);
    }
    {   /* the synthetic one */
        int32_t fma = pm(p->timbre, p->timbre), sn2 = p->harm, dlev, slev, rna;
        uint32_t kd, ks, kf = pl_keepq(54347), fmin, fmax;   /* 1 / 7 ms */
        kd = pl_keepq((int32_t)(((int64_t)25365 * pl_semi(-pm(dxt, 72 << 16) - pm(fma, 12 << 16) + pm(sn2, 7 << 16), 24)) >> 24));
        ks = pl_keepq((int32_t)(((int64_t)38048 * pl_semi(-pm(dec, 60 << 16) - pm(sn2, 7 << 16), 24)) >> 24));
        sn2 = pclamp(pm(sn2, 18454938) - 838861, 0, PL1);
        dlev = pl_sqrt(PL1 - sn2);
        slev = pl_sqrt(sn2);
        fmin = (uint32_t)pmin((int32_t)(((uint64_t)f0 * 10u) >> 8), PL1 >> 1);
        fmax = (uint32_t)pmin((int32_t)(((uint64_t)f0 * 35u) >> 8), PL1 >> 1);
        pl_op_f(&d->shp, pl_fq((int32_t)fmin), TAN_FAST);
        pl_svf_fq(&d->slp, pl_fq((int32_t)fmax), 128u + (uint32_t)(sn2 >> 15), TAN_FAST);   /* 0.5 + 2 snappy */
        pl_op_f(&d->dlp, (uint32_t)pmin((int32_t)(((uint64_t)f0 * 3u) >> 8), PL1 >> 1) << 8, TAN_FAST);
        if (p->trig) {
            d->samp = d->damp = (int32_t)(((int64_t)(5033165 + pm(11744051, p->accent))) << 6);
            d->fm = 1 << 30;
            d->phs[0] = d->phs[1] = 0;
            d->hold = (int32_t)((((int64_t)(671089 + pm(dec, 503316))) * FS) >> 24);   /* (0.04 + 0.03 d) s */
        }
        rna = pclamp((PL1 >> 3) - (int32_t)(f0 >> 8), 0, PL1 >> 3) * 8;
        rna = pm(pm(rna, rna), fma);
        for (i = 0; i < n; i++) {
            int32_t rn = 0, drum, noise, snare, fq, sz = (int32_t)(n - i), ff;
            d->damp = (d->damp > 32212255 || !((sz - 1) & 1)) ? pl_keep(d->damp, kd) : d->damp;   /* 0.03 */
            if (d->hold)
                --d->hold;
            else
                d->samp = pl_keep(d->samp, ks);
            d->fm = pl_keep(d->fm, kf);
            rn += d->phs[0] > (1 << 29) ? -PL1 : PL1;
            rn += d->phs[1] > (1 << 29) ? -PL1 : PL1;
            rn = pm(rn, pm(rna, 419430));            /* x 0.025 */
            fq = PL1 + pm(fma, 4 * (d->fm >> 6));
            ff = (int32_t)(((uint64_t)f0 * (uint32_t)fq) >> 26);   /* Q30 */
            d->phs[0] += ff;
            d->phs[1] += pm(ff, 24662507);           /* x 1.47 */
            if (rna > 1677722) {                     /* 0.1: the oscillators reset each other */
                int32_t lim = (1 << 30) + (rn << 6), k2;
                for (k2 = 0; k2 < 2; k2++)
                    if (d->phs[k2] >= lim)
                        d->phs[k2] = (1 << 30) - d->phs[k2];
            } else {
                if (d->phs[0] >= 1 << 30)
                    d->phs[0] -= 1 << 30;
                if (d->phs[1] >= 1 << 30)
                    d->phs[1] -= 1 << 30;
            }
            drum = -1677722 + pm(sd_dsine(d->phs[0]), 10066330) + pm(sd_dsine(d->phs[1]), 4194304);
            drum = pm(pm(drum, d->damp >> 6), dlev);
            drum = pl_op_lp(&d->dlp, drum);
            noise = pl_randf();
            snare = pl_svf_lp(&d->slp, noise);
            snare -= pl_op_lp(&d->shp, snare);
            snare = pm(pm(snare + 1677722, (d->samp >> 6) + (d->fm >> 6)), slev);
            aux[i] = snare + drum;
        }
    }
}

/* -------------------------------------------------- oscillators --- */
/* Plaits' Oscillator (polyBLEP), its SAW and SQUARE at pw 0.5; f Q24 (< 0.25) */
typedef struct { uint32_t ph; int32_t next, f; uint8_t high; } pl_osc_t;
static void pl_osc_init(pl_osc_t *o) { o->ph = 0x80000000u; o->next = 0; o->f = 16777; o->high = 1; }
static void pl_osc_render(pl_osc_t *o, int32_t f, int saw, int32_t *out, uint32_t n)
{
    int32_t step, ns = o->next, pw = PL1 >> 1;
    uint32_t i;
    f = pclamp(f, 17, PL1 >> 2);
    step = (f - o->f) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t ts = ns, ff, ph;
        uint32_t old = o->ph;
        int wrap;
        ns = 0;
        o->f += step;
        ff = o->f;
        o->ph += (uint32_t)ff << 8;
        wrap = o->ph < old;
        ph = (int32_t)(o->ph >> 8);
        if (saw) {
            if (wrap) {
                int32_t t = pl_div(ph, ff);
                ts -= blep_this(t);
                ns -= blep_next(t);
            }
            ns += ph;
        } else {
            int hi = wrap || ph >= pw;               /* (the phase before its wrap, >= pw) */
            if (o->high ^ hi) {
                int32_t t = pl_div((wrap ? ph + PL1 : ph) - pw, ff);
                ts += blep_this(t);
                ns += blep_next(t);
                o->high = (uint8_t)hi;
            }
            if (wrap) {
                int32_t t = pl_div(ph, ff);
                ts -= blep_this(t);
                ns -= blep_next(t);
                o->high = 0;
            }
            ns += ph < pw ? 0 : PL1;
        }
        out[i] = 2 * ts - PL1;
    }
    o->f = f;
    o->next = ns;
}

/* -------------------------------------------------- hi-hat --- */
typedef struct {
    int32_t env, nclk, nsmp;                         /* env Q29; the clocked noise: its phase Q24 */
    pl_svf_t col, hpf;
} pl_hh1_t;
typedef struct {
    uint32_t sq[6];                                  /* SquareNoise's phases */
    pl_hh1_t h[2];
    pl_osc_t osc[6];                                 /* RingModNoise's */
} pl_hh_t;
static void hh_init(pl_hh_t *d)
{
    uint32_t k;
    memset(d, 0, sizeof *d);
    for (k = 0; k < 6; k++)
        pl_osc_init(&d->osc[k]);
}
static void hh_render1(pl_hh1_t *h, int which, pl_hh_t *d, const pl_par_t *p, int32_t *out, uint32_t n)
{
    int32_t dec = p->morph, tone = p->timbre, noisy = p->harm, nf, cut;
    uint32_t i, kenv, kcut, cutf;
    kenv = pl_keepq((int32_t)(((int64_t)50332 * pl_semi(-pm(dec, 84 << 16), 24)) >> 24));   /* 0.003 */
    kcut = pl_keepq((int32_t)(((int64_t)41943 * pl_semi(-pm(dec, 36 << 16), 24)) >> 24));  /* 0.0025 */
    if (p->trig)
        h->env = (int32_t)(((int64_t)pm(25165824 + ((PL1 - dec) >> 1), 5033165 + pm(11744051, p->accent))) << 5);
    if (which == 0) {                                /* SquareNoise at 2 f0 */
        static const uint32_t R[6] = {16777216, 21877490, 24595399, 29980885, 32413581, 42547020};
        uint32_t inc[6], k;
        for (k = 0; k < 6; k++) {
            uint64_t f = (((uint64_t)p->f0 * 2u) * R[k]) >> 24;
            inc[k] = f >= 2143188436u ? 2143188436u : (uint32_t)f;
        }
        for (i = 0; i < n; i++) {
            uint32_t nz = 0;
            for (k = 0; k < 6; k++) {
                d->sq[k] += inc[k];
                nz += d->sq[k] >> 31;
            }
            out[i] = (int32_t)nz * 5536481 - PL1;    /* 0.33 */
        }
    } else {                                         /* RingModNoise */
        static const int32_t FR[6] = {200, 7530, 510, 8075, 730, 10500};
        int32_t t1[PLSUB], t2[PLSUB], ratio, f2 = (int32_t)((uint64_t)p->f0 * 2u >> 8);
        uint32_t k;
        ratio = pl_div(f2, 167772 + f2);             /* f / (0.01 + f) */
        for (i = 0; i < n; i++)
            out[i] = 0;
        for (k = 0; k < 3; k++) {
            int32_t fa = pm((int32_t)(((int64_t)FR[2 * k] << 24) / FS), ratio);
            int32_t fb = pm((int32_t)(((int64_t)FR[2 * k + 1] << 24) / FS), ratio);
            pl_osc_render(&d->osc[2 * k], fa, 0, t1, n);
            pl_osc_render(&d->osc[2 * k + 1], fb, 1, t2, n);
            for (i = 0; i < n; i++)
                out[i] += pm(t1[i], t2[i]);
        }
    }
    cut = (int32_t)(((int64_t)pl_semi(pm(tone, 72 << 16), 16) * (int32_t)((150ll << 24) / FS)) >> 16);
    cut = pclamp(cut, 0, (int32_t)((16000ll << 24) / FS));
    cutf = pl_fq(cut);
    pl_svf_fq(&h->col, cutf, which == 0 ? 768u + (uint32_t)(pm(tone, 768 << 8) >> 8) : 256u, TAN_ACCURATE);
    for (i = 0; i < n; i++)
        out[i] = pl_svf_bp(&h->col, out[i]);
    noisy = pm(noisy, noisy);
    nf = pclamp((int32_t)(((uint64_t)p->f0 * (uint32_t)(16 * PL1 + 16 * (PL1 - noisy))) >> 32), 0, PL1 >> 1);
    for (i = 0; i < n; i++) {
        h->nclk += nf;
        if (h->nclk >= PL1) {
            h->nclk -= PL1;
            h->nsmp = pl_randf() - (PL1 >> 1);
        }
        out[i] += pm(noisy, h->nsmp - out[i]);
    }
    for (i = 0; i < n; i++) {
        h->env = pl_keep(h->env, (h->env > (1 << 28) || which == 0) ? kenv : kcut);
        if (which == 0) {                            /* SwingVCA */
            int32_t s = out[i];
            s = s > 0 ? s * 4 : pm(s, 1677722);
            s = pl_div(s, PL1 + pabs(s));
            out[i] = pm(s + 1677722, h->env >> 5);
        } else {
            out[i] = pm(out[i], h->env >> 5);
        }
    }
    pl_svf_fq(&h->hpf, cutf, 128u, TAN_ACCURATE);
    for (i = 0; i < n; i++)
        out[i] = pl_svf_hp(&h->hpf, out[i]);
}
static void hh_render(pl_hh_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    hh_render1(&d->h[0], 0, d, p, out, n);
    hh_render1(&d->h[1], 1, d, p, aux, n);
}

/* -------------------------------------------------- filtered noise --- */
typedef struct { int32_t ph, smp, next, f; } pl_cn_t;   /* ClockedNoise: phase and frequency Q24 (up to 1) */
typedef struct {
    pl_cn_t cn[2];
    pl_svf_t lphp, bp[2];
    int32_t pf0, pf1, pq, pmode;                     /* Q24; q Q16 */
} pl_nz_t;
static void nz_init(pl_nz_t *d)
{
    memset(d, 0, sizeof *d);
    d->cn[0].f = d->cn[1].f = 16777;                 /* 0.001 */
}
static void cn_render(pl_cn_t *c, int sync, int32_t f, int32_t *out, uint32_t n)
{
    int32_t ns = c->next, step;
    uint32_t i;
    f = pclamp(f, 0, PL1);
    step = (f - c->f) / (int32_t)n;
    if (sync)
        c->ph = PL1;
    for (i = 0; i < n; i++) {
        int32_t ts = ns, raw, ramt;
        ns = 0;
        c->f += step;
        raw = 2 * pl_randf() - PL1;
        ramt = pclamp(4 * (c->f - (PL1 >> 2)), 0, PL1);
        c->ph += c->f;
        if (c->ph >= PL1) {
            int32_t t, dis;
            c->ph -= PL1;
            t = pclamp(pl_div(c->ph, c->f), 0, PL1);
            dis = raw - c->smp;
            ts += pm(dis, blep_this(t));
            ns += pm(dis, blep_next(t));
            c->smp = raw;
        }
        ns += c->smp;
        out[i] = ts + pm(ramt, raw - ts);
    }
    c->f = f;
    c->next = ns;
}
static void nz_render(pl_nz_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t tmp[PLSUB], f0, f1, cf, q, s0, s1, sq, sm;
    uint32_t i, f0q = p->f0, f1q = pl_note_f(p->note + (int32_t)(((int64_t)p->harm * 48) >> 8) - (24 << 16));
    uint32_t cfq = pl_note_f((int32_t)(((int64_t)p->timbre * 152) >> 8) - (24 << 16));
    f0 = (int32_t)(f0q >> 8);
    f1 = (int32_t)(f1q >> 8);
    cf = (int32_t)(cfq >> 8);
    q = (int32_t)(pl_semi(pm(p->morph, 120 << 16), 16) >> 1);   /* 0.5 2^(morph 10), Q16 */
    cn_render(&d->cn[0], p->trig, cf, aux, n);
    cn_render(&d->cn[1], p->trig, (int32_t)(((int64_t)pl_div(f1, f0) * cf) >> 24), tmp, n);
    s0 = (f0 - d->pf0) / (int32_t)n;
    s1 = (f1 - d->pf1) / (int32_t)n;
    sq = (q - d->pq) / (int32_t)n;
    sm = (p->harm - d->pmode) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t g, in1, in2, lp, bp, hp, mode, hg, bg, lg;
        uint32_t q8;
        d->pf0 += s0;
        d->pf1 += s1;
        d->pq += sq;
        d->pmode += sm;
        q8 = (uint32_t)d->pq >> 8;
        if (i == 0 || s0 || s1 || sq) {             /* (the filters' coefficients: when they move) */
            pl_svf_fq(&d->lphp, pl_fq(d->pf0), q8, TAN_ACCURATE);
            d->bp[0] = (pl_svf_t){d->lphp.g, d->lphp.r, d->lphp.rg, d->lphp.h, d->bp[0].s1, d->bp[0].s2};
            pl_svf_fq(&d->bp[1], pl_fq(d->pf1), q8, TAN_ACCURATE);
        }
        {   /* 1 / sqrt((0.5 + q) 40 f0) */
            int64_t a = ((int64_t)(d->pq + 32768) * 40 * d->pf0) >> 16;   /* Q24 */
            g = pl_div(PL1, (int32_t)pl_isqrt64((uint64_t)(a < 1 ? 1 : a) << 24));
        }
        in1 = pm(aux[i], g);
        in2 = pm(tmp[i], g);
        pl_svf_run(&d->lphp, in1, &lp, &bp, &hp);
        mode = d->pmode;
        hg = pmin(-2 * mode + PL1, 0);
        bg = PL1 - 2 * pabs(mode - (PL1 >> 1));
        lg = pmax(PL1 - 2 * mode, 0);
        out[i] = pm(hg, hp) + pm(bg, bp) + pm(lg, lp);
        aux[i] = pl_svf_bp(&d->bp[0], in1) + pl_svf_bp(&d->bp[1], in2);
    }
}

/* -------------------------------------------------- particle noise --- */
/* the diffuser (fx/diffuser.h on fx_engine.h, 12-bit samples): one, for the first voice on PARTICLE */
#define DIF_LEN 8192u
static int16_t pl_dif_buf[DIF_LEN];
static struct { int32_t wp, lp, y0, y1, k, lfo; int8_t owner; } pl_dif = {0, 0, 0, 0, 0, 0, -1};   /* LFO Q30 */
static void dif_clear(void)
{
    int64_t f = (((int64_t)1 << 30) * 3 * 32) / (10 * 48000) - (1 << 28);   /* 0.3 / 48000 x 32 - 0.25, Q30 */
    int32_t sign = 16;
    memset(pl_dif_buf, 0, sizeof pl_dif_buf);
    pl_dif.wp = 0;
    pl_dif.lp = 0;
    if (f < 0)                                       /* CosineOscillator::InitApproximate */
        f = -f;
    else if (f > (1 << 29))
        f -= 1 << 29;
    else
        sign = -16;
    pl_dif.k = (int32_t)((sign * f * ((1 << 30) - 2 * f)) >> 30);
    pl_dif.y1 = pl_dif.k >> 2;
    pl_dif.y0 = 1 << 29;
    pl_dif.lfo = (pl_dif.y1 >> 6) + (PL1 >> 1);
}
static inline int32_t dif_rd(int32_t base, int32_t off) { return pl_dif_buf[(pl_dif.wp + base + off) & (DIF_LEN - 1)] << 12; }
static inline void dif_wr(int32_t base, int32_t off, int32_t x)
{
    x = x < 0 ? -(-x >> 12) : x >> 12;               /* (toward zero, as Plaits' Compress: no drift) */
    pl_dif_buf[(pl_dif.wp + base + off) & (DIF_LEN - 1)] = (int16_t)pclamp(x, -32768, 32767);
}
static void dif_process(int32_t amount, int32_t rt, int32_t *io, uint32_t n)
{
    /* the lines: ap1 126, ap2 180, ap3 269, ap4 444, dapa 1653, dapb 2010, del 3411 (each base: the ones before + 1) */
    enum { B1 = 0, B2 = 127, B3 = 308, B4 = 578, BA = 1023, BB = 2677, BD = 4688 };
    static const int32_t L[7] = {126, 180, 269, 444, 1653, 2010, 3411}, B[7] = {B1, B2, B3, B4, BA, BB, BD};
    const int32_t kap = 10485760, klp = 12582912;    /* 0.625, 0.75 */
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t acc, prev, wet, k;
        if (--pl_dif.wp < 0)
            pl_dif.wp += DIF_LEN;
        if ((pl_dif.wp & 31) == 0) {                 /* the LFO, every 32 samples */
            int32_t t = pl_dif.y0;
            pl_dif.y0 = (int32_t)(((int64_t)pl_dif.k * pl_dif.y0) >> 30) - pl_dif.y1;
            pl_dif.y1 = t;
            pl_dif.lfo = (t >> 6) + (PL1 >> 1);
        }
        acc = io[i];
        for (k = 0; k < 3; k++) {                    /* ap1..3: Read(TAIL, kap), WriteAllPass(-kap) */
            prev = dif_rd(B[k], L[k] - 1);
            acc += pm(prev, kap);
            dif_wr(B[k], 0, acc);
            acc = pm(acc, -kap) + prev;
        }
        {   /* ap4: Interpolate(400 + 43 lfo) */
            int32_t off = (400 << 16) + ((43 * pl_dif.lfo) >> 8), oi = off >> 16, of = (off & 0xFFFF) << 8;   /* Q16 */
            int32_t a = dif_rd(B4, oi), b = dif_rd(B4, oi + 1);
            prev = a + pm(b - a, of);
            acc += pm(prev, kap);
            dif_wr(B4, 0, acc);
            acc = pm(acc, -kap) + prev;
        }
        {   /* del: Interpolate(3070 + 340 lfo, rt), Lp */
            int32_t off = (3070 << 16) + ((340 * (pl_dif.lfo >> 4)) >> 4), oi = off >> 16, of = (off & 0xFFFF) << 8;
            int32_t a = dif_rd(BD, oi), b = dif_rd(BD, oi + 1);
            acc += pm(a + pm(b - a, of), rt);
            pl_dif.lp += pm(klp, acc - pl_dif.lp);
            acc = pl_dif.lp;
        }
        prev = dif_rd(BA, L[4] - 1);                 /* dapa: Read(-kap), WriteAllPass(kap) */
        acc += pm(prev, -kap);
        dif_wr(BA, 0, acc);
        acc = pm(acc, kap) + prev;
        prev = dif_rd(BB, L[5] - 1);                 /* dapb: Read(kap), WriteAllPass(-kap) */
        acc += pm(prev, kap);
        dif_wr(BB, 0, acc);
        acc = pm(acc, -kap) + prev;
        dif_wr(BD, 0, acc);                          /* Write(del, 2.0) */
        acc *= 2;
        wet = acc;
        io[i] += pm(amount, wet - io[i]);
    }
}
#define NPART 6
typedef struct {
    int32_t pre[NPART];                              /* pre-gains, Q16 */
    pl_svf_t f[NPART], post;
} pl_pt_t;
static void pt_init(pl_pt_t *d) { memset(d, 0, sizeof *d); }
static void pt_render(pl_pt_t *d, uint32_t vi, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    uint32_t ds = pl_note_f((60 << 16) + (int32_t)(((int64_t)pm(p->timbre, p->timbre) * 72) >> 8));
    uint32_t dens = (uint32_t)(((uint64_t)ds * ds) >> 32) / NPART, i, k;   /* Q32 */
    int32_t gsh, spread = (int32_t)(((int64_t)pm(p->harm, p->harm) * 48) >> 8);   /* Q16 semitones */
    uint32_t grc = rg_rcp(dens ? dens : 1u, &gsh);   /* 1 / density = grc 2^(gsh - 48) (in Q32 units: x 2^32) */
    int32_t qs = p->morph >= (PL1 >> 1) ? (int32_t)pl_semi((int32_t)(((int64_t)(p->morph - (PL1 >> 1)) * 120) >> 8), 16) : 65536;
    uint32_t q16 = (uint32_t)(32768 + (((uint64_t)qs * qs) >> 16));   /* 0.5 + q_sqrt^2, Q16 */
    int32_t rds = 2 * pabs(p->morph - (PL1 >> 1)), rd = pm(rds, rds), dif = p->morph < (PL1 >> 1) ? rd : 0;
    int32_t sqd = pl_sqrt((int32_t)(dens >> 8));     /* sqrt(density), Q24 */
    for (i = 0; i < n; i++)
        out[i] = aux[i] = 0;
    for (k = 0; k < NPART; k++) {
        uint32_t u = pl_word();
        int can = 1;
        if (p->trig)
            u = dens;
        for (i = 0; i < n; i++) {
            int32_t s = 0;
            if (u <= dens) {
                s = (int32_t)pmin((int32_t)(((uint64_t)u * grc) >> (24 - gsh)), PL1);   /* u / density */
                if (can) {
                    int32_t uu = 2 * pl_randf() - PL1;
                    uint64_t f = ((uint64_t)pl_semi((int32_t)(((int64_t)spread * uu) >> 24), 16) * p->f0) >> 16;
                    uint32_t fq = f > 0x40000000ull ? 0x40000000u : (uint32_t)f;   /* 0.25 */
                    int64_t qf = ((((int64_t)q16 << 8) * (int64_t)(fq >> 8)) >> 24);   /* q f, Q24 */
                    int32_t den = pl_sqrt((int32_t)pmin((int32_t)((qf * sqd) >> 24), 0x7FFFFFFF));
                    if (den > 0) {                   /* 0.5 / den, Q16 */
                        int32_t sh2;
                        uint64_t r = (uint64_t)rg_rcp((uint32_t)den, &sh2) << sh2;
                        d->pre[k] = (r >> 9) > 0x7FFFFFFFull ? 0x7FFFFFFF : (int32_t)(r >> 9);
                    } else {
                        d->pre[k] = 0x7FFFFFFF;
                    }
                    pl_svf_fq(&d->f[k], fq, q16 >> 8, TAN_DIRTY);
                    can = 0;
                }
            }
            aux[i] += s;
            {   /* (the filter in Q20 units: its gain can pass Q24's headroom) */
                int64_t x = ((int64_t)d->pre[k] * s) >> 20;
                x = x > 0x3FFFFFFF ? 0x3FFFFFFF : x < -0x3FFFFFFF ? -0x3FFFFFFF : x;
                out[i] += pclamp(pl_svf_bp(&d->f[k], (int32_t)x), -(1 << 26), 1 << 26) << 4;
            }
            u = pl_word();
        }
    }
    pl_svf_fq(&d->post, p->f0 < 2104533975u ? p->f0 : 2104533975u, 128u, TAN_DIRTY);
    for (i = 0; i < n; i++)
        out[i] = pl_svf_lp(&d->post, out[i]);
    if (pl_dif.owner < 0) {
        pl_dif.owner = (int8_t)vi;
        dif_clear();
    }
    if (pl_dif.owner == (int8_t)vi)
        dif_process(pm(13421773, pm(dif, dif)), (dif >> 1) + (PL1 >> 2), out, n);
}

/* -------------------------------------------------- inharmonic string --- */
/* StringVoice + String (one string; its lines: the voice's Rings string's, rg_line and rg_stretch) */
typedef struct {
    pl_svf_t exf, iir;
    uint32_t rem, sw, tw, dly;                       /* dly: Q16 */
    int32_t dn, cb, dcx, dcy, src, so0, so1;
    uint32_t dcr;
} pl_st_t;
static void st_init(pl_st_t *d, uint32_t vi)
{
    memset(d, 0, sizeof *d);
    d->dly = 100u << 16;
    memset(rg_line[vi], 0, sizeof rg_line[vi]);
    memset(rg_stretch[vi], 0, sizeof rg_stretch[vi]);
}
#define PL_SLINE 1024u
#define PL_SSTR 256u
static void st_string(pl_st_t *d, uint32_t vi, uint32_t f0, int32_t nl, int32_t b, int32_t dmp, const int32_t *in,
                      int32_t *out, uint32_t n)
{
    int32_t *ln = rg_line[vi], *st = rg_stretch[vi], sh, ratio, dc, dcomp, sp, scorr, nas, namt, nfil, bc, apg;
    uint32_t delay, df, i, target, rc;
    uint64_t period;
    int disp = nl > 0;
    int32_t step;
    if (!disp)
        nl = -nl;
    rc = rg_rcp(f0, &sh);
    period = (uint64_t)rc << sh;                     /* 1 / f0, Q16 */
    delay = period < (4u << 16) ? 4u << 16 : period > ((PL_SLINE - 4u) << 16) ? (PL_SLINE - 4u) << 16 : (uint32_t)period;
    ratio = (int32_t)(((uint64_t)delay * f0) >> 24); /* src_ratio, Q24 */
    if (ratio >= 16775538) {
        d->src = PL1;
        ratio = PL1;
    }
    dc = pmin((12 << 16) + (int32_t)(((int64_t)pm(dmp, dmp) * 60) >> 8) + (int32_t)(((int64_t)b * 24) >> 8), 84 << 16);
    {
        uint64_t ff = ((uint64_t)pl_semi(dc, 16) * f0) >> 16;
        df = ff > 2143188436u ? 2143188436u : (uint32_t)ff;
    }
    if (dmp >= 15938355) {                           /* 0.95: crossfade to infinite decay */
        int32_t ti = (dmp - 15938355) * 20;
        b += pm(ti, PL1 - b);
        df += (uint32_t)(((int64_t)ti * (int64_t)(2147054240u - df)) >> 24);   /* 0.4999 */
        dc += (int32_t)(((int64_t)ti * ((128 << 16) - dc)) >> 24);
    }
    pl_svf_fq(&d->iir, df, 128u, TAN_FAST);
    {
        int32_t ix = dc >> 16, fx = dc & 0xFFFF;
        dcomp = PL_SVF_SHIFT[ix] + (int32_t)(((int64_t)(PL_SVF_SHIFT[ix < 256 ? ix + 1 : 256] - PL_SVF_SHIFT[ix]) * fx) >> 16);
    }
    target = (uint32_t)(((uint64_t)delay * (uint32_t)dcomp) >> 16);
    step = ((int32_t)target - (int32_t)d->dly) / (int32_t)n;
    sp = pm(pm(nl, 2 * PL1 - nl), 3774874);          /* x 0.225 */
    scorr = pclamp((int32_t)(((int64_t)delay * 160 / FS) << 8), PL1, 35232154);   /* (160 / FS) delay, 1 .. 2.1 */
    nas = nl > 12582912 ? 4 * (nl - 12582912) : 0;
    namt = pm(pm(nas, nas), 1677722);
    nfil = 1006633 + pm(15770583, pm(b, b));         /* 0.06 + 0.94 b^2 */
    bc = pm(pm(nl, nl), 167772);                     /* 0.01 */
    apg = -pl_div(pm(10368320, nl), 2516582 + pabs(nl));   /* -0.618 nl / (0.15 + |nl|) */
    for (i = 0; i < n; i++) {
        d->src += ratio;
        if (d->src > PL1) {
            int32_t s, x, ox;
            uint32_t dl;
            d->src -= PL1;
            d->dly += (uint32_t)step;
            dl = d->dly;
            if (disp) {
                PL_ONE_POLE(d->dn, pl_randf() - (PL1 >> 1), nfil);
                dl = (uint32_t)(((uint64_t)dl * (uint32_t)(PL1 + pm(d->dn, namt))) >> 24);
                {
                    uint32_t apd = (uint32_t)(((uint64_t)dl * (uint32_t)sp) >> 24);
                    int32_t k = 6845104 - pm(sp, 5167383);   /* 0.408 - 0.308 sp */
                    int64_t md = (int64_t)dl - (int64_t)((((uint64_t)apd * (uint32_t)pmax(k, 0)) >> 24) * (uint32_t)scorr >> 24);
                    if (apd >= (4u << 16) && md >= (4 << 16)) {
                        int32_t rd, wr;
                        s = rg_line_lin(ln, PL_SLINE, d->sw, (uint32_t)md);
                        rd = rg_line_rd(st, PL_SSTR, d->tw, apd >> 16);   /* DelayLine::Allpass */
                        wr = s + pm(apg, rd);
                        rg_line_wr(st, PL_SSTR, &d->tw, wr);
                        s = rd - pm(wr, apg);
                    } else {
                        s = rg_hermite(ln, PL_SLINE, d->sw, dl);
                    }
                }
            } else {
                int32_t v;
                dl = (uint32_t)(((uint64_t)dl * (uint32_t)(PL1 - pm(d->cb, bc))) >> 24);
                s = rg_hermite(ln, PL_SLINE, d->sw, dl);
                v = pabs(s) - 419430;                /* |s| - 0.025 */
                v = pabs(v) + v;
                d->cb = s > 0 ? v : -(v + (v >> 1));
            }
            s += in[i];
            s = pclamp(s, -20 * PL1, 20 * PL1);
            {   /* DCBlocker (1 - 20 / FS): its loss's fraction carried (no tail held at a level) */
                int64_t pr = (int64_t)d->dcy * 1947939 + d->dcr;   /* 20 / FS, Q32 */
                ox = d->dcx;
                d->dcx = s;
                d->dcr = (uint32_t)pr;
                d->dcy += s - ox - (int32_t)(pr >> 32);
                x = d->dcy;
            }
            x = pl_svf_lp(&d->iir, x);
            rg_line_wr(ln, PL_SLINE, &d->sw, x);
            d->so1 = d->so0;
            d->so0 = x;
        }
        out[i] += d->so1 + pm(d->so0 - d->so1, d->src);
    }
}
static void st_render(pl_st_t *d, uint32_t vi, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t tmp[PLSUB], b = pm(p->timbre, p->timbre), dmp = p->morph, s = p->harm, nl;
    uint32_t i, f0 = p->f0;
    b += pm(p->accent >> 2, PL1 - b);
    dmp += pm(p->accent >> 2, PL1 - dmp);
    if (p->trig) {
        int32_t bb = pm(b, 2 * PL1 - b) - (PL1 >> 1), sh;
        uint64_t c = ((uint64_t)pl_semi((int32_t)(((int64_t)bb * 72) >> 8), 16) * f0) >> 14;   /* 4 f0 2^(.. 72) */
        uint32_t rc = rg_rcp(f0, &sh);
        pl_svf_fq(&d->exf, c > 2143188436u ? 2143188436u : (uint32_t)c, 128u, TAN_DIRTY);
        d->rem = (uint32_t)(((uint64_t)rc << sh) >> 16);   /* 1 / f0 samples */
    }
    for (i = 0; i < n; i++) {
        if (d->rem) {
            --d->rem;
            tmp[i] = 2 * pl_randf() - PL1;
        } else {
            tmp[i] = 0;
        }
    }
    for (i = 0; i < n; i++) {
        tmp[i] = pl_svf_lp(&d->exf, tmp[i]);
        aux[i] = tmp[i];
        out[i] = 0;
    }
    nl = s < 4026532 ? -pm(4026532 - s, 69893881) : s > 4362076 ? pm(s - 4362076, 22671716) : 0;   /* 4.166, 1.35135 */
    st_string(d, vi, f0, nl, b, dmp, tmp, out, n);
}

/* -------------------------------------------------- modal resonator --- */
/* ModalVoice + Resonator (24 modes): the modes in rings.c's direct form (modal_run), in mdl[vi]'s */
#define PL_MODES 24u
typedef struct {
    pl_svf_t exf;
    int32_t hlp;
} pl_md_t;
static int16_t pl_mamp[PL_MODES];                    /* the modes' levels: CosineOscillator(0.015) x 0.25, Q15 */
static void md_amps(void)
{
    int64_t f = 16106127 - (1 << 28), sign = 16, k, y0, y1, t;   /* 0.015 - 0.25, Q30 */
    uint32_t i;
    if (pl_mamp[0])
        return;
    if (f < 0)
        f = -f;
    else if (f > (1 << 29))
        f -= 1 << 29;
    else
        sign = -16;
    k = (sign * f * ((1 << 30) - 2 * f)) >> 30;
    y1 = k >> 2;
    y0 = 1 << 29;
    for (i = 0; i < PL_MODES; i++) {
        t = y0;
        y0 = ((k * y0) >> 30) - y1;
        y1 = t;
        pl_mamp[i] = (int16_t)(((t + (1 << 29)) * 8192) >> 30);   /* (value + 0.5) x 0.25 */
    }
}
static void md_init(pl_md_t *d, uint32_t vi)
{
    memset(d, 0, sizeof *d);
    memset(mdl[vi].y1, 0, sizeof mdl[vi].y1);
    memset(mdl[vi].y2, 0, sizeof mdl[vi].y2);
    memset(mdl[vi].e1, 0, sizeof mdl[vi].e1);
    memset(mdl[vi].e2, 0, sizeof mdl[vi].e2);
    mdl[vi].x1 = mdl[vi].x2 = 0;
    md_amps();
}
/* Resonator::Process's coefficients (f Q32; s b d Q24), into m (b scaled by 8: modal_run takes in / 8) */
static void md_coefs(model_t *m, uint32_t f, int32_t s, int32_t b, int32_t d)
{
    int32_t idx = s >> 18, fr = s & 0x3FFFF, sh, st, qloss;
    uint64_t harmonic, q;
    int32_t stretch = PL1, k;
    st = PL_STIFF[idx] + (int32_t)(((int64_t)(PL_STIFF[idx < 64 ? idx + 1 : 64] - PL_STIFF[idx]) * fr) >> 18);
    {   /* f0 x NthHarmonicCompensation(3) */
        int32_t s1 = st, str = PL1 + s1;
        s1 = pm(s1, s1 < 0 ? 15602811 : 16441672);    /* 0.93, 0.98 */
        str += s1;
        f = (uint32_t)(((uint64_t)f * (uint32_t)pl_div(PL1, str)) >> 24);
    }
    harmonic = f;
    {   /* q = 500 q_sqrt^2, q_sqrt = 2^(damping 79.7 / 12); Q8 */
        uint64_t qs = pl_semi((int32_t)(((int64_t)d * 5223219) >> 24), 16);
        q = ((qs * qs) >> 24) * 500u;
    }
    b = pm(b, PL1 - pm(s, 5033165));
    b = pm(b, PL1 - pm(d, 5033165));
    qloss = pm(pm(b, 2 * PL1 - b), 14260634) + 2516582;   /* x 0.85 + 0.15 */
    for (k = 0; k < (int32_t)PL_MODES; k++) {
        uint64_t pf64 = (harmonic * (uint32_t)stretch) >> 24;
        uint32_t pf = pf64 >= 2143188436u ? 2143188436u : (uint32_t)pf64, rc, rcd;
        int32_t g, rg, shd, amp;
        int64_t g2, t1, t2;
        g = rg_tan(pf, TAN_FAST);
        {   /* r g = g / (1 + pf q) */
            uint64_t den = (((uint64_t)pf * q) >> 32) + 256u;   /* Q8 */
            rc = rg_rcp64(den, &sh);
            rg = (int32_t)(((uint64_t)(uint32_t)g * rc) >> (80 - sh));   /* Q30 */
        }
        g2 = ((int64_t)g * g) >> 18;
        rcd = rg_rcp64((1ull << 30) + (uint32_t)rg + (uint64_t)g2, &shd);
        t1 = (int64_t)rg_mulsh((uint64_t)rg + 2 * (uint64_t)g2, rcd, 63 - shd);
        t2 = (int64_t)(((uint64_t)(uint32_t)rg * rcd) >> (63 - shd));
        amp = (int32_t)(((int64_t)pl_mamp[k] * (int32_t)(PL1 - (int32_t)(pf >> 7))) >> 24);   /* x (1 - 2 f), Q15 */
        m->b[k] = (int32_t)(((int64_t)(((uint64_t)(uint32_t)g * rcd) >> (55 - shd)) * amp) >> 15);
        m->a1[k] = (int32_t)(t1 >= (1ll << 32) ? -(1ll << 31) + 1 : (1ll << 31) - (t1 < 1 ? 1 : t1));
        m->a2[k] = (int32_t)((1 << 30) - t2);
        stretch += st;
        st = pm(st, st < 0 ? 15602811 : 16441672);
        harmonic += f;
        q = (q * (uint32_t)qloss) >> 24;
    }
}
/* rings.c's modal_run, n samples */
static void md_run(model_t *m, const int32_t *in, int32_t *out, uint32_t n)
{
    int32_t xd[PLSUB], x1 = m->x1, x2 = m->x2;
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        int32_t x = in[i] >> 3;
        xd[i] = x - x2;
        x2 = x1;
        x1 = x;
    }
    m->x1 = x1;
    m->x2 = x2;
    for (k = 0; k < PL_MODES; k++) {
        int32_t a1 = m->a1[k], a2 = m->a2[k], b = m->b[k], y1 = m->y1[k], y2 = m->y2[k];
        int32_t e1 = m->e1[k], e2 = m->e2[k], c = a1 < 0 ? -2 : 2;
        for (i = 0; i < n; i++) {
            int64_t acc = (int64_t)b * xd[i] + (int64_t)a1 * y1 - (int64_t)a2 * y2 + (int64_t)c * e1 - e2;
            int32_t y = (int32_t)(acc >> 30);
            e2 = e1;
            e1 = (int32_t)(acc & 0x3FFFFFFF);
            y = y > (1 << 29) ? 1 << 29 : y < -(1 << 29) ? -(1 << 29) : y;
            y2 = y1;
            y1 = y;
            out[i] += y;
        }
        m->y1[k] = y1;
        m->y2[k] = y2;
        m->e1[k] = e1;
        m->e2[k] = e2;
    }
}
static void md_render(pl_md_t *d, uint32_t vi, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    model_t *m = &mdl[vi];
    int32_t tmp[PLSUB], b = p->timbre, dmp = p->morph, cut, bb;
    uint32_t i, f0 = p->f0, cq;
    uint64_t c;
    PL_ONE_POLE(d->hlp, p->harm, 167772);            /* 0.01 */
    b += pm(p->accent >> 2, PL1 - b);
    dmp += pm(p->accent >> 2, PL1 - dmp);
    bb = pm(b, 2 * PL1 - b) - (PL1 >> 1);
    c = ((uint64_t)pl_semi((int32_t)(((int64_t)bb * 60) >> 8), 16) * f0) >> 15;   /* 2 f0 2^(.. 60) */
    cq = c > 2143188436u ? 2143188436u : (uint32_t)c;
    cut = (int32_t)(cq >> 8);
    pl_svf_fq(&d->exf, cq, 384u, TAN_FAST);           /* (ResonatorSvf<1>: q 1.5) */
    tmp[0] = pl_svf_lp(&d->exf, 0);
    if (p->trig) {   /* the pulse A = amp 2^(2 cut^2) / cut: g A (bounded) = amp 2^(2 cut^2) (pi + cut^2 (a + b cut^2)) */
        int32_t amp = pm(2013266 + pm(1342177, p->accent), PL1 - (dmp >> 1)), c2 = pm(cut, cut), ga, h;
        int64_t gr = 52707179 + ((((int64_t)c2 << 6) * (169584874 + (((int64_t)935957068 * ((int64_t)c2 << 6)) >> 30))) >> 30);   /* g / cut, Q24 */
        ga = pm(pm(amp, (int32_t)pl_exp2(2 * c2, 24)), (int32_t)gr);
        h = (int32_t)((((int64_t)ga * d->exf.h) + (1 << 29)) >> 30);   /* g h A */
        tmp[0] += pm(d->exf.g, h);
        d->exf.s1 += 2 * h;
        d->exf.s2 += 2 * pm(d->exf.g, h);
    }
    for (i = 1; i < n; i++)
        tmp[i] = pl_svf_lp(&d->exf, 0);
    for (i = 0; i < n; i++) {
        aux[i] = tmp[i];
        out[i] = 0;
    }
    md_coefs(m, f0, d->hlp, b, dmp);
    md_run(m, tmp, out, n);
}

/* -------------------------------------------------- swarm --- */
#define NSWARM 8
typedef struct {
    int32_t from, itv, ph, fm, amp, psr, fc;         /* GrainEnvelope, Q24 */
    uint32_t sph;
    int32_t snext, sf, sg;                           /* AdditiveSawOscillator */
    int32_t x, y, eps, sa;                           /* FastSineOscillator, x y Q28 */
    int32_t rank;
} pl_swv_t;
typedef struct { pl_swv_t v[NSWARM]; } pl_sw_t;
static void sw_init(pl_sw_t *d)
{
    uint32_t i;
    memset(d, 0, sizeof *d);
    for (i = 0; i < NSWARM; i++) {
        pl_swv_t *v = &d->v[i];
        v->rank = ((int32_t)i - 3) * 5592405;      /* (i - n) / n, n = (8 - 1) / 2 = 3 (an int, in Plaits) */
        v->itv = PL1;
        v->ph = PL1;
        v->amp = PL1 >> 1;
        v->sf = 167772;                              /* 0.01 */
        v->x = 1 << 28;
    }
}
static void sw_render(pl_sw_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t dens = (int32_t)(((uint64_t)pl_note_f((int32_t)(((int64_t)p->timbre * 120) >> 8)) * 419430u * n) >> 32);   /* x 0.025 n, Q24 */
    int32_t spread = pm(pm(p->harm, p->harm), p->harm);
    int32_t sr = (int32_t)(pl_semi((int32_t)(((int64_t)(PL1 - p->morph) * 84) >> 8), 16) << 6);   /* 0.25 2^(.. 84 / 12), Q24 */
    uint32_t i, k;
    for (i = 0; i < n; i++)
        out[i] = aux[i] = 0;
    for (k = 0; k < NSWARM; k++) {
        pl_swv_t *v = &d->v[k];
        int32_t a, expo, lin, ta = PL1, step, gstep, e, estep, astep, amp, fr;
        uint64_t f;
        int rnd = 0;
        if (p->trig) {                               /* GrainEnvelope::Step */
            v->ph = PL1 >> 1;
            v->fm = 16 * PL1;
            rnd = 1;
        } else {
            v->ph += pm(dens, v->fm);
            if (v->ph >= PL1) {
                v->ph &= PL1 - 1;
                rnd = 1;
            }
        }
        if (rnd) {
            v->from += v->itv;
            v->itv = pl_randf() - v->from;
            v->fm = pm(v->fm, 13421773 + pm(3355443, pl_randf()));   /* x (0.8 + 0.2 u) */
        }
        expo = sr < PL1 ? 2 * (v->from + pm(v->itv, v->ph)) - PL1 : v->from;
        if (sr >= PL1) {                             /* amplitude() */
            int32_t ph = pclamp(pm(v->ph - (PL1 >> 1), sr), -PL1, PL1);
            int32_t es = pl_sin(pl_fq((ph >> 1) + PL1 + (PL1 >> 2)));
            ta = (es + PL1) >> 1;
        }
        if ((sr >= PL1) ^ (v->psr >= PL1))
            v->fc = PL1 >> 1;
        v->fc = pm(v->fc, 15938355);                 /* 0.95 */
        v->psr = sr;
        PL_ONE_POLE(v->amp, ta, (PL1 >> 1) - v->fc);
        a = v->amp >> 3;                             /* / 8 voices */
        f = ((uint64_t)pl_semi((int32_t)(((int64_t)pm(pm(expo, spread), v->rank) * 48) >> 8), 16) * p->f0) >> 16;
        lin = pm(pm(v->rank, v->rank + 167772), spread) >> 2;
        f = (f * (uint32_t)(PL1 + lin)) >> 24;
        fr = f >= 0x40000000ull ? PL1 >> 2 : (int32_t)(f >> 8);   /* Q24, kMaxFrequency 0.25 */
        /* the saw */
        step = (fr - v->sf) / (int32_t)n;
        gstep = (a - v->sg) / (int32_t)n;
        {
            int32_t ns = v->snext;
            for (i = 0; i < n; i++) {
                int32_t ts = ns;
                uint32_t old = v->sph;
                ns = 0;
                v->sf += step;
                v->sg += gstep;
                v->sph += (uint32_t)v->sf << 8;
                if (v->sph < old) {
                    int32_t t = pl_div((int32_t)(v->sph >> 8), v->sf);
                    ts -= blep_this(t);
                    ns -= blep_next(t);
                }
                ns += (int32_t)(v->sph >> 8);
                out[i] += pm(2 * ts - PL1, v->sg);
            }
            v->snext = ns;
            v->sf = fr;
            v->sg = a;
        }
        /* the sine (FastSineOscillator, ADDITIVE) */
        amp = a;
        if (fr >= (PL1 >> 2)) {
            fr = PL1 >> 2;
            amp = 0;
        } else {
            amp = pm(amp, PL1 - 4 * fr);
        }
        {   /* Fast2Sin(f) = f pi (2 - 0.32 (f pi)^2), Q28 */
            int32_t fp = pm(fr, 52707179), e28 = (int32_t)(((int64_t)pm(fp, 2 * PL1 - pm(5368709, pm(fp, fp)))) << 4);
            estep = (e28 - v->eps) / (int32_t)n;
            astep = (amp - v->sa) / (int32_t)n;
            {
                int64_t nrm = (((int64_t)v->x * v->x) >> 28) + (((int64_t)v->y * v->y) >> 28);   /* Q28 */
                if (nrm <= (1 << 27) || nrm >= (2 << 28)) {   /* (stmlib: fast_rsqrt_carmack) */
                    int32_t r = (int32_t)pl_isqrt64((uint64_t)nrm << 28);   /* sqrt, Q28 */
                    if (r > 0) {
                        int32_t sc = pl_div(PL1, r >> 4);
                        v->x = pm(v->x, sc);
                        v->y = pm(v->y, sc);
                    }
                }
            }
            for (i = 0; i < n; i++) {
                v->eps += estep;
                v->sa += astep;
                e = v->eps;
                v->x += (int32_t)(((int64_t)e * v->y) >> 28);
                v->y -= (int32_t)(((int64_t)e * v->x) >> 28);
                aux[i] += pm(v->sa, v->x >> 4);
            }
            v->eps = e28;
            v->sa = amp;
        }
        sr = pm(sr, 16273899);                       /* x 0.97 */
    }
}

#include "plaits_va.c"

/* ------------------------------------------------------------ the voice --- */
typedef struct {
    uint8_t eng, on;                                 /* the engine its state is for (on: it is) */
    uint8_t trig;                                    /* a hit to strike at the next block */
    int32_t accent;                                  /* the hit's velocity, Q24 */
    int32_t opeak, apeak;                            /* the limiters' (OUT, AUX) */
    pl_svf_t lpg;                                    /* the low-pass gate's filter */
    int32_t lpg_g;                                   /* .. its gain, the last block's (Q24) */
    union {
        pl_bd_t bd;
        pl_sd_t sd;
        pl_hh_t hh;
        pl_nz_t nz;
        pl_pt_t pt;
        pl_st_t st;
        pl_md_t md;
        pl_sw_t sw;
        pl_va_t va;
        pl_ws_t ws;
        pl_fm_t fm;
        pl_gr_t gr;
        pl_add_t add;
        pl_wt_t wt;
        pl_ch_t ch;
        pl_sp_t sp;
    } u;
} pl_t;
static pl_t pl[3];

static void pl_engine_init(pl_t *v, uint32_t vi, uint32_t e)
{
    v->eng = (uint8_t)e;
    v->on = 1;
    v->opeak = v->apeak = PL1 >> 1;
    if (pl_dif.owner == (int8_t)vi)
        pl_dif.owner = -1;
    switch (e) {
    case PE_BD: bd_init(&v->u.bd); break;
    case PE_SD: sd_init(&v->u.sd); break;
    case PE_HH: hh_init(&v->u.hh); break;
    case PE_NOISE: nz_init(&v->u.nz); break;
    case PE_PART: pt_init(&v->u.pt); break;
    case PE_STRING: st_init(&v->u.st, vi); break;
    case PE_MODAL: md_init(&v->u.md, vi); break;
    case PE_SWARM: sw_init(&v->u.sw); break;
    case PE_VA: va_init(&v->u.va); break;
    case PE_WSHP: ws_init(&v->u.ws); break;
    case PE_FM: fm_init(&v->u.fm); break;
    case PE_GRAIN: gr_init(&v->u.gr); break;
    case PE_ADD: add_init(&v->u.add); break;
    case PE_WTBL: wt_init(&v->u.wt); break;
    case PE_CHORD: ch_init(&v->u.ch); break;
    case PE_SPEECH: sp_init(&v->u.sp); break;
    default: break;
    }
}
static void pl_engine_render(pl_t *v, uint32_t vi, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    switch (v->eng) {
    case PE_BD: bd_render(&v->u.bd, p, out, aux, n); break;
    case PE_SD: sd_render(&v->u.sd, p, out, aux, n); break;
    case PE_HH: hh_render(&v->u.hh, p, out, aux, n); break;
    case PE_NOISE: nz_render(&v->u.nz, p, out, aux, n); break;
    case PE_PART: pt_render(&v->u.pt, vi, p, out, aux, n); break;
    case PE_STRING: st_render(&v->u.st, vi, p, out, aux, n); break;
    case PE_MODAL: md_render(&v->u.md, vi, p, out, aux, n); break;
    case PE_SWARM: sw_render(&v->u.sw, p, out, aux, n); break;
    case PE_VA: va_render(&v->u.va, p, out, aux, n); break;
    case PE_WSHP: ws_render(&v->u.ws, p, out, aux, n); break;
    case PE_FM: fm_render(&v->u.fm, p, out, aux, n); break;
    case PE_GRAIN: gr_render(&v->u.gr, p, out, aux, n); break;
    case PE_ADD: add_render(&v->u.add, p, out, aux, n); break;
    case PE_WTBL: wt_render(&v->u.wt, p, out, aux, n); break;
    case PE_CHORD: ch_render(&v->u.ch, p, out, aux, n); break;
    case PE_SPEECH: sp_render(&v->u.sp, p, out, aux, n); break;
    default: memset(out, 0, n * sizeof out[0]); memset(aux, 0, n * sizeof aux[0]); break;
    }
}

/* a hit: the engine strikes at the next block (its accent: the velocity) */
static void pl_trigger(uint32_t vi, uint32_t vel)
{
    pl[vi].trig = 1;
    pl[vi].accent = (int32_t)(vel * 132104u);        /* (2^24 / 127) */
}
/* OUT's or AUX's gain (negative: through the limiter) */
static inline int32_t pl_post(int32_t x, int32_t g, int32_t *peak) { return g < 0 ? pl_limit(peak, x, -g) : pm(x, g); }
static inline int32_t pl_knob(uint8_t k) { return k >= 127u ? PL1 : (int32_t)(k * 132104u); }
/* the block of a DRUM voice on a Plaits engine (mdl[vi]'s knobs: HARMONICS TIMBRE MORPH COLOUR AUX ENV) */
static void pl_block(uint32_t vi, const voice_t *v, int16_t *buf)
{
    pl_t *d = &pl[vi];
    const model_t *m = &mdl[vi];
    uint32_t e = m->type - MD_PL0, k, i, f = v->osc.inc;
    int32_t o[PLSUB], a[PLSUB], env = pl_knob(m->tens), col = pl_knob(m->pos), mix = pl_knob(m->exc);
    int32_t g0 = v->last_gain, g1 = v->gain, gs, og = PE_OUTG[e], ag = PE_AUXG[e], selfenv;
    pl_par_t p;
    if (!d->on || d->eng != e) {
        pl_engine_init(d, vi, e);
        memset(&d->lpg, 0, sizeof d->lpg);
        d->lpg_g = 0;
    }
    if (v->vol_mod) {                                /* (VOL MOD: the velocity on the gain, as the voice's) */
        g0 = (int32_t)(((int64_t)g0 * v->velo) >> 15);
        g1 = (int32_t)(((int64_t)g1 * v->velo) >> 15);
    }
    f = f < 100000u ? 100000u : f > 2104533975u ? 2104533975u : f;   /* (1 Hz .. 0.49) */
    p.f0 = f;
    p.note = pl_f_note(f);
    p.harm = pl_knob(m->dec);
    p.timbre = pclamp(pl_knob(m->damp) + pm(env, g1), 0, PL1);   /* ENV: the amp EG on TIMBRE and MORPH */
    p.morph = pclamp(pl_knob(m->mat) + pm(env, g1), 0, PL1);
    p.accent = d->accent;
    gs = (g1 - g0) / (int32_t)(BLOCK / PLSUB);
    for (k = 0; k < BLOCK / PLSUB; k++) {
        int32_t gk = g0 + gs * (int32_t)(k + 1), gl, gst;
        p.trig = d->trig;
        d->trig = 0;
        pl_engine_render(d, vi, &p, o, a, PLSUB);
        selfenv = PE_ENV[e] || (e == PE_SPEECH && d->u.sp.env);
        if (!selfenv && col) {                       /* the low-pass gate: Plaits' vactrol, its colour 1 - COLOUR */
            int32_t hf = PL1 - col, g2 = pm(gk, gk), g4 = pm(g2, g2), t = PL1 - gk, t2 = pm(t, t);
            int32_t fr = 50332 + pm(5033165, g4) + pm(hf, 671089);   /* 0.003 + 0.3 g^4 + 0.04 hf */
            int32_t bleed = pm(pm(t2 + pm(PL1 - t2, hf), hf), hf);
            pl_svf_fq(&d->lpg, pl_fq(fr), 102, TAN_DIRTY);   /* (Q 0.4) */
            gl = d->lpg_g;
            gst = (gk - gl) / (int32_t)PLSUB;
            for (i = 0; i < PLSUB; i++) {
                int32_t x, lp;
                gl += gst;
                x = pl_post(o[i], og, &d->opeak);
                x = pm(x + pm(mix, pl_post(a[i], ag, &d->apeak) - x), gl);
                lp = pl_svf_lp(&d->lpg, x);
                o[i] = lp + pm(x - lp, bleed);
            }
        } else {
            gl = d->lpg_g;
            gst = (gk - gl) / (int32_t)PLSUB;
            for (i = 0; i < PLSUB; i++) {
                int32_t x;
                gl += gst;
                x = pl_post(o[i], og, &d->opeak);
                x += pm(mix, pl_post(a[i], ag, &d->apeak) - x);
                o[i] = selfenv ? x : pm(x, gl);
            }
        }
        d->lpg_g = gk;
        for (i = 0; i < PLSUB; i++) {
            int32_t y = o[i] >> 9;                   /* (Q24 to Q15) */
            buf[k * PLSUB + i] = (int16_t)(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
        }
    }
}
