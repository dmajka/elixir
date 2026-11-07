/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 1.0 (Phase 10c): NOISE, a MODEL on any voice, in Plaits' NOISE's number (dropped: ~25% of core 1 on the
 * unit for its two clocked noises, three filters and a divide and a square root a sample). Its sound replaces the
 * voice's oscillators, FM and transient; the voice's filter, amp EG and shape follow, as ENSEMBLE's.
 *   COLOUR (DECAY)  brown, pink, white, blue: -6, -3, 0, +3 dB an octave, each crossfading into the next (the tilt).
 *                   Each at the same level (RMS 0.25 of full scale, tests/ui_test.c measures it; brown's and pink's
 *                   rarest peaks, 4.6 x that, clip). Pink is Paul Kellet's economy filter (three one-poles, +-0.5 dB
 *                   10 Hz .. 20 kHz), brown a leaky integrator of white, blue pink's difference.
 *   RATE (BRIGHT)   127: off. Below: the white noise held, sample and hold, clocked at the voice's pitch x 1/4 .. x64
 *                   (2^(8 r - 2)): stepped, pitched noise that follows the note (its pitch EG, random, tape), then
 *                   coloured.
 *   ENV (STRUCT)    how far the amp EG moves COLOUR up (bright at the hit, darker in the tail).
 * Each voice its own noise (xorshift32), so the sound doesn't depend on the core or the order. Samples Q15; the
 * colours' states Q19 (white x 16); weights Q30 with each colour's gain in them. */

typedef struct {
    uint8_t on, col, rate, env;                      /* NOISE plays this voice; the knobs, 0..127 */
    uint32_t rng, ph;                                /* its noise; the hold's phase (Q32) */
    int32_t w;                                       /* the white sample held (Q15) */
    int32_t b0, b1, b2, br, pk;                      /* pink's three, brown, pink's last (blue: the difference) */
    int32_t wa, wb;                                  /* the last block's weights (Q30) */
    uint8_t seg, pad[3];                             /* .. and their pair: brown-pink, pink-white, white-blue */
} noise_t;
static noise_t nzo[NVOICE];

/* each colour's gain to RMS 8192 (0.25), Q30, from its own RMS (Q19 states; white Q15): brown 134,453, pink 903,484,
 * white 18,916, blue 541,436 (2^22 samples, scratch harness) */
#define NZ_GBR 65431142                              /* 0.060938 */
#define NZ_GPK 9736154                               /* 0.009068 */
#define NZ_GWH 465064428                             /* 0.433125 */
#define NZ_GBL 16247056                              /* 0.015131 */

static inline uint32_t nz_word(noise_t *z) { uint32_t x = z->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return z->rng = x; }
static void nz_knob(uint32_t vi, uint32_t k, uint8_t v)
{
    noise_t *z = &nzo[vi];
    if (k == 0u)
        z->col = v;
    else if (k == 1u)
        z->rate = v;
    else if (k == 2u)
        z->env = v;
}
/* COLOUR as a place on brown (0) .. blue (3), Q16, with ENV's amp EG (gain Q24) */
static int32_t nz_pos(const noise_t *z, int32_t gain)
{
    int32_t p = (int32_t)z->col * 196608 / 127 + (int32_t)(((int64_t)z->env * 1548 * (gain >> 8)) >> 16);
    return p < 0 ? 0 : p > 196608 ? 196608 : p;      /* (1548: 3 x 65536 / 127) */
}
/* pink as its states make it (blue's last, kept by the pairs that don't need it) */
static inline int32_t pink_last(int32_t b0, int32_t b1, int32_t b2, int32_t w)
{
    return b0 + b1 + b2 + (int32_t)(((int64_t)(w << 4) * 198427489) >> 30);
}
/* a block of pair seg (0 brown-pink, 1 pink-white, 2 white-blue: a constant, a loop each), weights wa wb stepping
 * by sa sb, the hold's increment inc (RATE off: 2^32 - 1, a new sample but one in 2^32: 27 hours). Brown only runs in its pair (its weight there starts from 0, so its
 * state resumes unheard), blue is pink's difference */
static inline __attribute__((always_inline)) void nz_run(noise_t *z, int16_t *buf, uint32_t seg, int32_t wa,
                                                         int32_t wb, int32_t sa, int32_t sb, uint32_t inc)
{
    int32_t b0 = z->b0, b1 = z->b1, b2 = z->b2, br = z->br, pk = z->pk, w = z->w;
    uint32_t i, ph = z->ph, r = z->rng;
    for (i = 0; i < BLOCK; i++) {
        int32_t wq, pink, a, b, y;
        if ((ph += inc) < inc) {                     /* (a new sample: at the hold's wrap) */
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            w = (int32_t)r >> 16;
        }
        wq = w << 4;
        b0 = (int32_t)(((int64_t)b0 * 1071218531 + (int64_t)wq * 106349833) >> 30);   /* 0.99765, 0.0990460 */
        b1 = (int32_t)(((int64_t)b1 * 1034013377 + (int64_t)wq * 318382060) >> 30);   /* 0.96300, 0.2965164 */
        b2 = (int32_t)(((int64_t)b2 * 612032840 + (int64_t)wq * 1130318677) >> 30);   /* 0.57000, 1.0526913 */
        pink = b0 + b1 + b2 + (int32_t)(((int64_t)wq * 198427489) >> 30);            /* + 0.1848 white */
        if (seg == 0u) {
            br = (int32_t)(((int64_t)br * 1063004406 + ((int64_t)w << 30)) >> 30);     /* 0.99, + white */
            a = br;
            b = pink;
        } else if (seg == 1u) {
            a = pink;
            b = w;
        } else {
            a = w;
            b = pink - pk;
            pk = pink;
        }
        wa += sa;
        wb += sb;
        y = (int32_t)(((int64_t)a * wa + (int64_t)b * wb) >> 30);
        buf[i] = (int16_t)(y > 32767 ? 32767 : y < -32767 ? -32767 : y);
    }
    z->b0 = b0;
    z->b1 = b1;
    z->b2 = b2;
    z->br = br;
    z->pk = pink_last(b0, b1, b2, w);               /* (the last sample's pink, whichever pair) */
    z->w = w;
    z->ph = ph;
    z->rng = r;
    z->wa = wa;
    z->wb = wb;
}
/* the voice's block: its noise into buf (v: the pitch, the amp EG for ENV) */
static void nz_block(uint32_t vi, const voice_t *v, int16_t *buf)
{
    static const int32_t G[4] = {NZ_GBR, NZ_GPK, NZ_GWH, NZ_GBL};
    noise_t *z = &nzo[vi];
    int32_t pos = nz_pos(z, v->gain), f, wa, wb, sa, sb;
    uint32_t seg = (uint32_t)pos >> 16, inc = 0xFFFFFFFFu;
    if (!z->rng)                                     /* (each voice's own seed) */
        z->rng = 0x9E3779B9u * (vi + 1u);
    if (seg > 2u)
        seg = 2u;
    f = pos - (int32_t)(seg << 16);                  /* (0 .. 1, Q16, into the pair's second) */
    wa = (int32_t)(((int64_t)G[seg] * (65536 - f)) >> 16);
    wb = (int32_t)(((int64_t)G[seg + 1u] * f) >> 16);
    if (z->seg != seg) {                             /* (a new pair: no ramp from the old one's weights) */
        z->seg = (uint8_t)seg;
        z->wa = wa;
        z->wb = wb;
    }
    sa = (wa - z->wa) / (int32_t)BLOCK;
    sb = (wb - z->wb) / (int32_t)BLOCK;
    if (z->rate < 127u) {                            /* (the pitch x 2^(8 r - 2); every sample from there on) */
        uint64_t q = ((uint64_t)v->osc.inc * (uint32_t)exp2_q16((int32_t)z->rate * 4128 - 131072)) >> 16;
        inc = q >= 0xFFFFFFFFull ? 0xFFFFFFFFu : q ? (uint32_t)q : 1u;   /* (4128: 8 x 65536 / 127) */
    }
    if (seg == 0u)
        nz_run(z, buf, 0u, z->wa, z->wb, sa, sb, inc);
    else if (seg == 1u)
        nz_run(z, buf, 1u, z->wa, z->wb, sa, sb, inc);
    else
        nz_run(z, buf, 2u, z->wa, z->wb, sa, sb, inc);
}
