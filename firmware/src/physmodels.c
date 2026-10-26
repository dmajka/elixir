/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 0.6 - 0.8: the physical models of the DRUM voices (SKIN, BAR, BELL, PLATE, TUBE), as voices.c had them.
 * Not built since 0.9 (rings.c took the MODEL section: the user didn't like how these sounded); kept for reference,
 * and to be put back as they were if wanted: include this in voices.c in rings.c's place, with sound.c's MODEL
 * plumbing and the 0.8 tests (git history: tests/ui_test.c phase5d). */

/* 0.6: physical models for the DRUM voices (the MODEL section: sound.c sets mdl[]). A struck body instead of the
 * LXR's oscillators: 6 two-pole resonators (MEMBRANE, BAR, BELL, PLATE: their modes' ratios to the voice's pitch,
 * stretched by MATERIAL; each excited by POSITION's share and the hit's brightness; decaying in DECAY, the higher
 * ones DAMPING times faster), or a Karplus-Strong line (TUBE). The exciter: a half-sine pulse (soft: long; hard: a
 * click) or a noise burst, its area the velocity. TENSION: the pitch up at the hit, falling back (a drum head). The
 * voice's filter, shaping, sends and the rest follow as for the LXR's engine; the amp EG does not apply (DECAY is
 * the model's). Coefficients each block: y = x b + a1 y1 - a2 y2, a1 = 2 r cos w, a2 = r^2 (Q30), the states Q8 */
enum { MD_LXR, MD_MEMBRANE, MD_BAR, MD_BELL, MD_PLATE, MD_TUBE, NMODEL };
#define MD_NMODE 6u
static int32_t exp2_q16(int32_t y);                  /* (fxbus.c) */
#define TUBE_LEN 1104u                               /* TUBE's line: down to ~40 Hz */
static const int32_t MD_LOG2[4][MD_NMODE] = {           /* log2 of the modes' ratios, Q16 */
    {0, 43845, 71933, 78750, 92143, 101317},         /* MEMBRANE: 1 1.59 2.14 2.30 2.65 2.92 */
    {0, 95851, 159516, 207038, 244953, 276584},      /* BAR: 1 2.76 5.40 8.93 13.34 18.64 */
    {-65536, 0, 15889, 38714, 65536, 87162},         /* BELL: 0.5 1 1.18 1.51 2 2.51 */
    {0, 33154, 62168, 81185, 97686, 113739}};        /* PLATE: 1 1.42 1.93 2.36 2.81 3.33 */
#define OM_MS ((uint32_t)(7417460000000ull / FS))    /* 1 - r (Q30) x the decay in ms: 6.908 / (T FS) */
typedef struct {
    uint8_t type, dec, damp, mat, pos, exc, tens;
    int32_t y1[MD_NMODE], y2[MD_NMODE], a1[MD_NMODE], a2[MD_NMODE], b[MD_NMODE];
    int32_t amp[MD_NMODE];                              /* this hit's share of each mode, Q15 */
    uint32_t ex_n, ex_len;                           /* the exciter: samples left, its length */
    int32_t ex_amp, ten;                             /* .. its peak (noise: x 1); TENSION's pitch, Q16 over 1 */
    int32_t ex_lp, ex_k;                             /* .. the noise's low-pass (TUBE: the pluck's brightness) */
    uint8_t ex_noise;
    uint32_t tw;                                     /* TUBE: the write index, the loop's low-pass */
    int32_t tlp;
} model_t;
static model_t mdl[3];
static int16_t tube_line[3][TUBE_LEN] POOL;

/* sin x for x = ph / 2^32 of a turn, 0 <= ph <= 2^30 (to a quarter), Q30: a Taylor series to x^9 */
static int32_t sin_q30(uint32_t ph)
{
    int64_t x = ((int64_t)ph * 1686629713) >> 30, x2 = (x * x) >> 30, t;   /* x: radians, Q30 */
    t = (1 << 30) - ((x2 * 14913081) >> 30);         /* 1 - x^2 / 72 */
    t = (1 << 30) - ((((x2 * 25565282) >> 30) * t) >> 30);   /* 1 - x^2 / 42 (..) */
    t = (1 << 30) - ((((x2 * 53687091) >> 30) * t) >> 30);
    t = (1 << 30) - ((((x2 * 178956971) >> 30) * t) >> 30);
    return (int32_t)((x * t) >> 30);
}
static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
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
    return r;
}
/* a mode's ratio to the pitch, MATERIAL's stretch applied (Q16) */
static int32_t model_ratio(const model_t *m, uint32_t k)
{
    int32_t mat = 65536 + ((int32_t)m->mat - 64) * 310;   /* MATERIAL: the ratios to the power 0.7 .. 1.3, Q16 */
    return exp2_q16((int32_t)(((int64_t)MD_LOG2[(m->type - 1u) & 3u][k] * mat) >> 16));
}
/* 0.7: a hit's level no longer depends on the pitch, the exciter or POSITION (0.6 ranged from 0.04 to 6x full
 * scale, clipped hard). The exciter is sized to the pitch's period (inc: the hit's, without the pitch EG): a soft
 * pulse lasts up to 3/4 of a period and keeps its area, a noise burst keeps its energy (1 / sqrt of its length);
 * the modes' shares add up to MD_SUM; a new hit damps what still rings by half (the mallet on the body). TUBE: a
 * pluck one period long (a half-sine, or noise darkened by EXCITER), at a fixed peak. */
#define MD_SUM 70000                                 /* the modes' shares together, Q15 (2.1) */
static void model_trigger(uint32_t vi, uint32_t vel, uint32_t inc)
{
    model_t *m = &mdl[vi];
    int32_t v15 = (int32_t)(vel * 32767u / 127u), pp = 1311 + m->pos * 245;   /* the strike point 0.02 .. 0.5, Q16 */
    uint32_t k, per = 0xFFFFFFFFu / (inc >> 8 ? inc >> 8 : 1u);   /* the period, Q8 samples */
    int32_t pos[MD_NMODE], sum = 0;
    per >>= 8;
    per = per < 2u ? 2u : per > TUBE_LEN - 2u ? TUBE_LEN - 2u : per;
    if (m->type == MD_TUBE) {                        /* a pluck: one period */
        m->ex_noise = m->exc >= 64u;
        m->ex_len = m->ex_noise ? per : 1u + (per - 1u) * (63u - m->exc) / 63u;
        m->ex_amp = v15 * 3 / 5;
        m->ex_k = m->ex_noise ? 3000 + (int32_t)(m->exc - 64u) * 472 : 32767;
        if (m->ex_noise) {                           /* (darker noise is quieter: up to x2.3 back) */
            int32_t d = (int32_t)(127u - m->exc) * 1024 / 63;
            m->ex_amp = (int32_t)(((int64_t)m->ex_amp * (1024 + (1331 * d >> 10) * d / 1024)) >> 10);
        }
        for (k = 0; k < TUBE_LEN; k++)
            tube_line[vi][k] >>= 1;
        m->tlp >>= 1;
    } else if (m->exc < 64u) {                       /* a pulse: 3/4 of a period (a thump, at most 300) .. 1 (a click) */
        uint32_t l = (per * 3u >> 2) * (63u - m->exc) / 63u;
        m->ex_len = l < 1u ? 1u : l > 300u ? 300u : l;
        m->ex_noise = 0;
        m->ex_amp = m->ex_len > 1u ? v15 * 51472 / (int32_t)(m->ex_len << 15) : v15;   /* (area v15: x pi / 2 / l) */
        {   /* a soft pulse rings the fundamental only: up to x4 (its share of the modes), by its length's square */
            int32_t fr = (int32_t)(m->ex_len * 1024u / (per * 3u >> 2 ? per * 3u >> 2 : 1u));   /* Q10 */
            fr = fr > 1024 ? 1024 : fr;
            m->ex_amp = (int32_t)(((int64_t)m->ex_amp * (1024 + (3 * fr * fr >> 10))) >> 10);
        }
    } else {                                         /* a noise burst: 1.5 .. 7 ms, its energy as a click's */
        m->ex_len = 64u + (m->exc - 64u) * 4u;
        m->ex_noise = 1;
        m->ex_amp = (int32_t)((uint32_t)v15 * 55u / isqrt32(m->ex_len << 10));   /* (1.73 / sqrt l: rms 0.58) */
        m->ex_k = 32767;
    }
    m->ex_n = m->ex_len;
    m->ex_lp = 0;
    for (k = 0; k < MD_NMODE; k++) {                 /* POSITION: sin(pi p (k + 1)) */
        int32_t a = sine_at(((uint32_t)pp * (k + 1u) << 15) + 0x40000000u);   /* (the table: -cos) */
        a = a < 0 ? -a : a;
        a = a < 3277 ? 3277 : a;
        if (m->type != MD_TUBE && ((uint64_t)inc * (uint32_t)model_ratio(m, k) >> 16) >= 1932735283u)
            a = 0;                                   /* (past 0.45 FS: silent, so not counted) */
        pos[k] = a;
        sum += a;
    }
    for (k = 0; k < MD_NMODE; k++) {                 /* .. shares adding to MD_SUM; harder: the higher modes louder */
        int32_t a = sum ? (int32_t)((int64_t)pos[k] * MD_SUM / sum) : 0;
        m->amp[k] = (int32_t)(((int64_t)a * (32768 - (32768 - v15) * (int32_t)k / 8)) >> 15);
        if (m->type != MD_TUBE) {                    /* a new hit damps what rings */
            m->y1[k] >>= 1;
            m->y2[k] >>= 1;
        }
    }
    m->ten = (int32_t)(m->tens * (uint32_t)v15 >> 7);   /* up to + an octave */
}
/* the block's coefficients from the voice's pitch (inc: its oscillator's, with the pitch EG and random) */
static void model_coefs(model_t *m, uint32_t inc)
{
    uint32_t k, om0 = OM_MS / (uint32_t)((20 * exp2_q16(m->dec * 3922)) >> 16), lim = 1932735283u;  /* 0.45 */
    if (m->type == MD_BELL || m->type == MD_PLATE)
        om0 >>= 1;                                   /* (they ring twice as long) */
    inc = (uint32_t)(((uint64_t)inc * (uint32_t)(65536 + m->ten)) >> 16);
    for (k = 0; k < MD_NMODE; k++) {
        int32_t ratio = model_ratio(m, k);
        uint64_t ik64 = ((uint64_t)inc * (uint32_t)ratio) >> 16;
        uint32_t ik = (uint32_t)ik64, om, r;
        int32_t sh, c, sw;
        if (ik64 >= lim) {                           /* (past 0.45 FS: silent) */
            m->b[k] = 0;
            m->a1[k] = m->a2[k] = 0;
            continue;
        }
        om = om0 + (uint32_t)((((uint64_t)om0 * (uint32_t)(ratio > 65536 ? ratio - 65536 : 0)) >> 16) * m->damp >> 5);
        om = om > (1u << 28) ? 1u << 28 : om;
        r = (1u << 30) - om;
        sh = sin_q30(ik >> 1);                       /* cos w = 1 - 2 sin^2 (w / 2): exact at low pitches */
        c = (1 << 30) - (int32_t)(((int64_t)sh * sh) >> 29);
        sw = sin_q30(ik <= (1u << 30) ? ik : (1u << 31) - ik);
        m->a1[k] = (int32_t)(((int64_t)r * c) >> 29);
        m->a2[k] = (int32_t)(((uint64_t)r * r) >> 30);
        m->b[k] = (int32_t)(((int64_t)m->amp[k] * sw) >> 15);   /* (Q30: the mode's peak its share of the hit's) */
    }
    m->ten -= m->ten >> 5;                           /* TENSION falls back (~45 ms) */
}
static int32_t model_exc(model_t *m)                 /* the exciter's next sample */
{
    int32_t x;
    if (!m->ex_n)
        return 0;
    m->ex_n--;
    if (m->ex_noise) {
        x = (int32_t)(int16_t)(noise_next() >> 16) * m->ex_amp >> 15;
        m->ex_lp += ((x - m->ex_lp) * m->ex_k) >> 15;
        x = m->ex_lp;
    } else
        x = m->ex_len == 1u ? m->ex_amp   /* (the table: -cos; a quarter on, sin) */
                            : (sine_at((m->ex_len - m->ex_n) * (0x80000000u / m->ex_len) + 0x40000000u) * m->ex_amp)
                                  >> 15;
    return x;
}
static int32_t soft_clip(int32_t x)                 /* linear to 0.75 of full scale, then bending to it */
{
    int32_t a = x < 0 ? -x : x, d;
    if (a <= 24576)
        return x;
    d = a - 24576;
    a = 24576 + (int32_t)((uint32_t)d * 8191u / (uint32_t)(d + 8192));
    return x < 0 ? -a : a;
}
static void model_block(uint32_t vi, const voice_t *v, int16_t *buf)
{
    model_t *m = &mdl[vi];
    uint32_t i, k;
    if (m->type == MD_TUBE) {                        /* Karplus-Strong: the line D = FS / f, a low-pass and a loss */
        uint32_t ik = v->osc.inc >> 8, d8, di, df;
        int32_t g, kd = 32767 - m->damp * 200;
        int32_t om0 = (int32_t)(OM_MS / (uint32_t)((20 * exp2_q16(m->dec * 3922)) >> 16));
        int16_t *ln = tube_line[vi];
        ik = (uint32_t)(((uint64_t)ik * (uint32_t)(65536 + m->ten)) >> 16);
        d8 = 0xFFFFFFFFu / (ik ? ik : 1u);           /* the period, Q8 samples */
        d8 = d8 < (2u << 8) ? 2u << 8 : d8 > ((TUBE_LEN - 2u) << 8) ? (TUBE_LEN - 2u) << 8 : d8;
        di = d8 >> 8;
        df = d8 & 255u;
        g = 32767 - (int32_t)(((uint32_t)om0 >> 8) * di >> 7);   /* the loss a pass: r^D */
        g = g < 16384 ? 16384 : g;
        for (i = 0; i < BLOCK; i++) {
            uint32_t a = (m->tw + TUBE_LEN - di) % TUBE_LEN, b = (a + TUBE_LEN - 1u) % TUBE_LEN;
            int32_t y = ln[a] + (((ln[b] - ln[a]) * (int32_t)df) >> 8);
            m->tlp += ((y - m->tlp) * kd) >> 15;
            y = sat16(((m->tlp * g) >> 15) + model_exc(m));
            ln[m->tw] = (int16_t)y;
            m->tw = m->tw + 1u < TUBE_LEN ? m->tw + 1u : 0u;
            buf[i] = (int16_t)y;
        }
        m->ten -= m->ten >> 5;
        return;
    }
    model_coefs(m, v->osc.inc);
    for (i = 0; i < BLOCK; i++) {
        int32_t x = model_exc(m) << 8, out = 0;
        for (k = 0; k < MD_NMODE; k++) {
            int32_t y = (int32_t)((((int64_t)m->a1[k] * m->y1[k]) - ((int64_t)m->a2[k] * m->y2[k]) +
                                   ((int64_t)x * m->b[k])) >> 30);
            m->y2[k] = m->y1[k];
            m->y1[k] = y;
            out += y;
        }
        buf[i] = (int16_t)soft_clip(out >> 10);
    }
}

