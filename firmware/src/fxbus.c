/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the LXR-02's FX bus in fixed point: DRIVE (tube, fold, clip), RING modulator, COMPRESSOR and DELAY
 * (mono, ping pong). One effect at a time; the voices routed to FX feed it, its output joins the mix.
 * Ported from the LXR firmware, Core/Src/Soundengine/FxBus.c and DSPAudio/FX (Distortion.c, Ringmod.c,
 * Compressor.c, Delay.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only. (The LXR's reverb is compiled out
 * there, USE_REVERB 0; Kettle's is Felucca's, a bus of its own: reverb.c.) A voice's FX SEND sets how much of it
 * goes through the bus (voices.c mix_voice). Kettle adds two types after the LXR's (0.3): PHASER and FLANGER. */

enum { FX_OFF, FX_DRIVE, FX_RING, FX_COMP, FX_DELAY, FX_PHASER, FX_FLANGER, NFX };
enum { DIST_TUBE, DIST_FOLD, DIST_RAT };
enum { DELAY_NORMAL, DELAY_PINGPONG };

#define FXBUF_SIZE 0x8000u                          /* 32768 samples: 0.74 s mono, 2 x 0.37 s ping pong */
#define FXBUF_MASK (FXBUF_SIZE - 1u)
static int16_t fxbuf[FXBUF_SIZE] POOL;

static struct {
    uint8_t type, route, dist_type, ratio, ring_wave, delay_type;
    int32_t dw;                                      /* dry / wet, Q15 (the setting / 100) */
    int32_t p[4];                                    /* P1..P4, 0..1 Q16 */
    /* drive */
    int32_t lp3[2], hp1[2], lp1[2], hp3[2], lp2[2], slew[2], last[2];   /* Q16 */
    /* ring */
    osc_t ring;
    /* compressor (log2 domain, Q16) */
    int32_t c_env, c_thr, c_atk, c_rel, c_gain, c_ratio;
    /* delay */
    uint32_t d_old, d_new, d_write;
    uint32_t d_fold, d_fnew;                         /* read fractions, Q16 */
    int32_t d_interp, d_step;                        /* crossfade, Q24 */
    uint32_t d_time, d_range, d_pending;             /* Q16 */
    uint8_t d_has_pending;
    int32_t d_tone, d_fb;                            /* Q16 */
    int32_t d_lp[2];                                 /* the tone filters, Q8 */
    /* phaser, flanger */
    uint8_t last_type;                               /* the type the bus ran last block (a change clears) */
    uint32_t m_ph;                                   /* their LFO's phase */
    int32_t ph_s[2][6], ph_fb[2];                    /* the allpasses' states, the chains' last outputs */
    int32_t fl_d[2];                                 /* the flanger's delay at the block's end, samples Q16 */
    uint32_t fl_w;                                   /* .. its write index */
    uint8_t freeze, push;                            /* PERF: FREEZE (the delay loops), FB PUSH (perform.c) */
} fx;

/* ----------------------------------------------------- fixed log / exp --- */
static int32_t exp2_q16(int32_t y)                   /* 2^(y / 65536), Q16; y clamped to -16..15 */
{
    int32_t n, f;
    uint32_t m;
    if (y < -(16 << 16))
        return 0;
    if (y > (15 << 16) - 1)
        y = (15 << 16) - 1;
    n = y >> 16;                                     /* floor */
    f = y & 0xFFFF;
    m = EXP2_T[f >> 8] + (uint32_t)((((int32_t)EXP2_T[(f >> 8) + 1] - (int32_t)EXP2_T[f >> 8]) * (f & 255)) >> 8);
    return n >= 0 ? (int32_t)(m << n) : (int32_t)(m >> -n);
}
static int32_t log2_q16(uint32_t v)                  /* log2(v), Q16; v >= 1 */
{
    uint32_t e = 31u - (uint32_t)__builtin_clz(v), m, lo = 0, hi = 256;
    m = e >= 16u ? v >> (e - 16u) : v << (16u - e); /* mantissa 1.x, Q16 (65536 .. 131071) */
    while (hi - lo > 1u) {                           /* EXP2_T inverse: the segment of m */
        uint32_t mid = (lo + hi) >> 1;
        if (EXP2_T[mid] <= m)
            lo = mid;
        else
            hi = mid;
    }
    return (int32_t)(e << 16) + (int32_t)(lo << 8) +
           (int32_t)(((m - EXP2_T[lo]) << 8) / (EXP2_T[lo + 1u] - EXP2_T[lo]));
}
/* exp(-1000 / (ms x FS)), Q16: a one-pole's coefficient for a time in ms (Q16) */
static int32_t time_coef(uint32_t ms_q16)
{
    /* -1000 / (ms FS) / ln 2 in log2 units: 1000 / 0.693147 / FS = 0.032715 */
    uint32_t y = (2144u << 16) / (ms_q16 ? ms_q16 : 1u);  /* 0.032715 x 65536 = 2144: / ms -> Q16 (fits 32 bits) */
    if (y > (16u << 16))
        y = 16u << 16;
    return exp2_q16(-(int32_t)y);
}

/* ------------------------------------------------------- parameters --- */
static void delay_set_time(uint32_t t);

static void fxbus_update(void)                       /* FxBus_updateParameter */
{
    const int32_t *p = fx.p;
    switch (fx.type) {
    case FX_RING:                                    /* Ringmod_setFreq: f x 880 Hz */
        fx.ring.note_inc = (uint32_t)(((uint64_t)p[0] * 880u * 97391u) >> 16);   /* (2^32 / FS = 97391.5) */
        osc_set_freq(&fx.ring, 65536u);
        break;
    case FX_COMP: {
        /* attack 0.03 + a x 100 ms, release 25 + r x 175 ms, threshold -96 + t x 96 dB, gain 1 + g x 2 */
        fx.c_atk = time_coef((uint32_t)(1966 + (int64_t)p[0] * 100));
        fx.c_rel = time_coef((uint32_t)((25 << 16) + (int64_t)p[1] * 175));
        fx.c_thr = (int32_t)(((int64_t)(-96 * 65536 + (int64_t)p[2] * 96) * 10885) >> 16);   /* dB -> log2 (/6.0206) */
        fx.c_gain = 65536 + 2 * p[3];
        break;
    }
    case FX_DELAY:
        fx.d_range = (uint32_t)p[1];
        delay_set_time(fx.d_time);                   /* (Delay_setRange re-sets the time) */
        delay_set_time((uint32_t)p[0]);
        fx.d_tone = (int32_t)(((int64_t)p[2] * p[2]) >> 16);   /* Delay_setFilter: f^2 */
        fx.d_fb = p[3];
        break;
    default:
        break;
    }
}

static void fxbus_init(void)                         /* FxBus_init */
{
    uint32_t i;
    fx.type = FX_OFF;
    fx.dw = 0;
    fx.ring.wave = W_REC;
    fx.ring.midi_freq = 70u << 8 | 64u;
    fx.c_ratio = 65536;
    fx.c_atk = fx.c_rel = 0;
    fx.d_write = FXBUF_SIZE / 20u - 10u;
    fx.d_fb = 45875;                                 /* 0.7 */
    for (i = 0; i < FXBUF_SIZE; i++)
        fxbuf[i] = 0;
}

/* --------------------------------------------------------- dry / wet --- */
static inline int32_t drywet(int32_t dry, int32_t wet) { return dry + sat16(((wet - dry) * fx.dw) >> 15); }

/* the bus output into the mix: stereo (St1 / St2) or one side (L1 R1 L2 R2) */
static int fx_stereo(void) { return fx.route <= R_ST2; }
static int32_t *fx_side(void) { return fx.route == R_R1 || fx.route == R_R2 ? mix_r : mix_l; }

/* ----------------------------------------------------------- DRIVE --- */
static int32_t foldback(int32_t x)                   /* Distortion.c foldback(x, 1), Q16 */
{
    if (x > 65536 || x < -65536) {
        int32_t t = (x - 65536) % (4 << 16);         /* fmod: the sign of the dividend */
        if (t < 0)
            t = -t;
        t -= 2 << 16;
        if (t < 0)
            t = -t;
        x = t - 65536;
    }
    return x;
}
static int32_t tube(int32_t in, int32_t *lp, int32_t coff, int32_t outmod, int32_t vol)
{
    int32_t s = sat16(in), itg = (s >> 9) + 64, frac = in & 511;
    int32_t a = LXR_ECC83[itg], b = LXR_ECC83[itg + 1 > 128 ? 128 : itg + 1];
    int32_t val = (a + (((b - a) * frac) >> 9)) << 1;          /* Q15 -> Q16 */
    *lp += (int32_t)(((int64_t)coff * (val - *lp)) >> 16);
    return sat16((int32_t)(((((int64_t)*lp * outmod) >> 16) * vol) >> 17));
}
static int32_t diode(int32_t x)                      /* the 1N914 curve over -1..1 (Q16) */
{
    int32_t idx, frac;
    x = clamp32(x, -65536, 65536);
    idx = (x + 65536) >> 9;                          /* 0..256 */
    frac = (x + 65536) & 511;
    if (idx >= 256)
        return LXR_1N914[256] << 1;
    return (LXR_1N914[idx] + (((LXR_1N914[idx + 1] - LXR_1N914[idx]) * frac) >> 9)) << 1;
}
static int32_t rat(int32_t in, uint32_t c, int32_t coff, int32_t gain)   /* one channel of the clip (RAT) */
{
    const int32_t *p = fx.p;
    int32_t s = in << 1;                             /* Q16 */
    s += (int32_t)(((int64_t)fx.last[c] * p[1]) >> 19);        /* + last x color x 0.125 */
    fx.hp1[c] += (int32_t)(((int64_t)42019 * (s - fx.hp1[c])) >> 16);   /* 7.2 Hz high pass (0.64117) */
    s -= fx.hp1[c];
    fx.lp1[c] += (int32_t)(((int64_t)58816 * (s - fx.lp1[c])) >> 16);   /* 16 kHz low pass (0.89746) */
    s = fx.lp1[c];
    fx.hp3[c] += (int32_t)(((int64_t)557 * (s - fx.hp3[c])) >> 16);     /* 60 Hz high pass (0.0085044) */
    s -= fx.hp3[c];
    fx.lp2[c] += (int32_t)(((int64_t)4502 * (s - fx.lp2[c])) >> 16);    /* 500 Hz low pass (0.0686996) */
    s = (int32_t)(((int64_t)fx.lp2[c] * gain) >> 16);
    {                                                /* slew limit 6.7966 a sample, rails +-4.2 */
        int32_t d = s - fx.slew[c];
        d = clamp32(d, -445421, 445421);
        fx.slew[c] = clamp32(fx.slew[c] + d, -275251, 275251);
    }
    s = diode(fx.slew[c]);
    fx.lp3[c] += (int32_t)(((int64_t)coff * (s - fx.lp3[c])) >> 16);
    fx.last[c] = (int32_t)(((int64_t)fx.lp3[c] * p[3]) >> 16);
    return sat16(fx.last[c] >> 1);
}
static void fx_drive(void)                           /* Distortion_process */
{
    const int32_t *p = fx.p;
    int32_t coff = 4285 + (int32_t)(((int64_t)p[2] * 60562) >> 16);   /* 0.06538 + tone x 0.92411 */
    int st = fx_stereo();
    int32_t *side = fx_side();
    uint32_t i;
    if (fx.dist_type == DIST_TUBE) {
        int32_t g = 65536 + (st ? 4 : 1) * p[0];     /* (1 + drive x 4); the LXR's mono path: 1 + drive */
        int32_t outmod = 65536 + (int32_t)(((int64_t)p[1] * 72090) >> 16);   /* 1 + 1.1 x color */
        for (i = 0; i < BLOCK; i++) {
            int32_t l = tube((fx_l[i] * g) >> 16, &fx.lp3[0], coff, outmod, p[3]);
            if (st) {
                int32_t r = tube((fx_r[i] * g) >> 16, &fx.lp3[1], coff, outmod, p[3]);
                mix_l[i] += drywet(fx_l[i], l);
                mix_r[i] += drywet(fx_r[i], r);
            } else {
                side[i] += drywet(fx_l[i], l);
            }
        }
    } else if (fx.dist_type == DIST_FOLD) {
        int32_t g = 65536 + 4 * p[0];
        for (i = 0; i < BLOCK; i++) {
            uint32_t c, nc = st ? 2u : 1u;
            for (c = 0; c < nc; c++) {
                int32_t in = c ? fx_r[i] : fx_l[i];
                int32_t v = (int32_t)(((int64_t)in * 2 * g) >> 16) + p[1];
                int32_t w;
                fx.lp3[c] += (int32_t)(((int64_t)coff * (v - fx.lp3[c])) >> 16);
                w = sat16((int32_t)(((int64_t)foldback(fx.lp3[c]) * p[3]) >> 17));
                if (!st)
                    side[i] += drywet(in, w);
                else if (c)
                    mix_r[i] += drywet(in, w);
                else
                    mix_l[i] += drywet(in, w);
            }
        }
    } else {
        int32_t gain = 65536 + 2304 * p[0];
        for (i = 0; i < BLOCK; i++) {
            if (st) {
                mix_l[i] += drywet(fx_l[i], rat(fx_l[i], 0, coff, gain));
                mix_r[i] += drywet(fx_r[i], rat(fx_r[i], 1, coff, gain));
            } else {
                side[i] += drywet(fx_l[i], rat(fx_l[i], 0, coff, gain));
            }
        }
    }
}

/* ------------------------------------------------------------ RING --- */
static void fx_ring(void)                            /* Ringmod_process */
{
    int16_t o[BLOCK];
    uint32_t i;
    int st = fx_stereo();
    int32_t *side = fx_side();
    fx.ring.wave = fx.ring_wave < NWAVE ? fx.ring_wave : W_REC;
    osc_block(&fx.ring, o, 32768, 0, 0, 19661);      /* (pwm 0.3) */
    for (i = 0; i < BLOCK; i++) {
        int32_t l = (fx_l[i] * o[i]) >> 16;
        if (st) {
            mix_l[i] += drywet(fx_l[i], l);
            mix_r[i] += drywet(fx_r[i], (fx_r[i] * o[i]) >> 16);
        } else {
            side[i] += drywet(fx_l[i], l);
        }
    }
}

/* ------------------------------------------------------ COMPRESSOR --- */
static const int32_t RATIO_M1[6] = {0, -32768, -43691, -49152, -54613, -57344};   /* ratio - 1, Q16 */
static void fx_comp(void)                            /* Compressor_process, in log2 units */
{
    uint32_t i;
    int st = fx_stereo();
    int32_t *side = fx_side();
    int32_t rm1 = RATIO_M1[fx.ratio < 6 ? fx.ratio : 0];
    for (i = 0; i < BLOCK; i++) {
        int32_t in = st ? (fx_l[i] + fx_r[i]) >> 1 : fx_l[i];
        uint32_t a = (uint32_t)iabs(in);
        int32_t lv = a ? log2_q16(a) - (15 << 16) : -(40 << 16);   /* log2 of |in| / 32768 */
        int32_t over = lv - fx.c_thr, coeff, gr;
        int64_t g;
        if (over < 0)
            over = 0;
        coeff = over > fx.c_env ? fx.c_atk : fx.c_rel;
        fx.c_env = over + (int32_t)(((int64_t)coeff * (fx.c_env - over)) >> 16);
        gr = exp2_q16((int32_t)(((int64_t)fx.c_env * rm1) >> 16));
        g = ((int64_t)gr * fx.c_gain) >> 16;         /* Q16 */
        if (st) {
            mix_l[i] += drywet(fx_l[i], sat16((int32_t)((fx_l[i] * g) >> 16)));
            mix_r[i] += drywet(fx_r[i], sat16((int32_t)((fx_r[i] * g) >> 16)));
        } else {
            side[i] += drywet(fx_l[i], sat16((int32_t)((fx_l[i] * g) >> 16)));
        }
    }
}

/* ----------------------------------------------------------- DELAY --- */
/* calcDelayTime: the delay in samples (Q16) of a time 0..1 x the range (the LXR scales it by the range twice) */
#define D_LOW_MIN (26u << 16 | 30147u)               /* 0.0006 s */
#define D_LOW_MAX (882u << 16)                       /* 0.02 s = HI_MIN */
#define D_HI_MAX (32767u << 16)
static uint32_t delay_samples(uint32_t t)
{
    uint64_t cv = ((uint64_t)t * fx.d_range) >> 16;
    uint64_t hi = fx.delay_type == DELAY_NORMAL ? D_HI_MAX : (uint64_t)D_HI_MAX * 48 / 100;
    uint64_t mn = D_LOW_MIN + (((uint64_t)(D_LOW_MAX - D_LOW_MIN) * fx.d_range) >> 16);
    uint64_t mx = D_LOW_MAX + (((hi - D_LOW_MAX) * fx.d_range) >> 16);
    return (uint32_t)(mn + (((mx - mn) * cv) >> 16));
}
static uint32_t delay_mask(void) { return fx.delay_type == DELAY_NORMAL ? FXBUF_MASK : FXBUF_MASK >> 1; }
static void delay_set_time(uint32_t t)               /* Delay_setDelayTime */
{
    if (fx.d_interp <= 0) {
        uint32_t off = delay_samples(t);
        fx.d_time = t;
        fx.d_fnew = off & 0xFFFFu;
        fx.d_new = (fx.d_write - 1u - (off >> 16)) & delay_mask();
        fx.d_interp = 1 << 24;
        fx.d_step = (1 << 24) / 441;                 /* a 10 ms crossfade */
    } else {
        fx.d_pending = t;
        fx.d_has_pending = 1;
    }
}
static inline int32_t delay_read(uint32_t base, uint32_t pos, uint32_t frac, uint32_t mask)
{
    int32_t a = fxbuf[base + pos], b = fxbuf[base + ((pos + 1u) & mask)];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)frac) >> 16);
}
static inline int32_t tone(int32_t *lp, int32_t in)  /* the feedback's one-pole (Q8 state) */
{
    *lp += (int32_t)(((int64_t)fx.d_tone * ((in << 8) - *lp)) >> 16);
    return *lp >> 8;
}
static void fx_delay(void)                           /* Delay_process / Delay_processPingPong */
{
    uint32_t i, mask = delay_mask(), half = FXBUF_SIZE >> 1;
    int pp = fx.delay_type != DELAY_NORMAL, st = fx_stereo();
    int32_t *side = fx_side(), fb = fx.push ? 62259 : fx.d_fb;   /* (FB PUSH: 0.95) */
    for (i = 0; i < BLOCK; i++) {
        int32_t ol, orr;
        if (fx.d_interp <= 0) {
            ol = delay_read(0, fx.d_old, fx.d_fold, mask);
            orr = pp ? delay_read(half, fx.d_old, fx.d_fold, mask) : ol;
            fx.d_old = (fx.d_old + 1u) & mask;
            if (fx.d_has_pending) {
                fx.d_has_pending = 0;
                delay_set_time(fx.d_pending);
            }
        } else {
            int32_t al = delay_read(0, fx.d_old, fx.d_fold, mask), bl = delay_read(0, fx.d_new, fx.d_fnew, mask);
            int32_t w = fx.d_interp >> 9;            /* Q15 */
            ol = (al * w + bl * (32768 - w)) >> 15;
            if (pp) {
                int32_t ar = delay_read(half, fx.d_old, fx.d_fold, mask), br = delay_read(half, fx.d_new, fx.d_fnew, mask);
                orr = (ar * w + br * (32768 - w)) >> 15;
            } else {
                orr = ol;
            }
            fx.d_old = (fx.d_old + 1u) & mask;
            fx.d_new = (fx.d_new + 1u) & mask;
            fx.d_interp -= fx.d_step;
            if (fx.d_interp <= 0) {
                fx.d_old = fx.d_new;
                fx.d_fold = fx.d_fnew;
            }
        }
        if (fx.freeze) {                             /* FREEZE: what the line holds, round and round */
            if (pp) {
                fxbuf[half + fx.d_write] = (int16_t)sat16(ol);
                fxbuf[fx.d_write] = (int16_t)sat16(orr);
            } else {
                orr = ol;
                fxbuf[fx.d_write] = (int16_t)sat16(ol);
            }
        } else if (pp) {
            ol = tone(&fx.d_lp[0], ol);
            orr = tone(&fx.d_lp[1], orr);
            fxbuf[half + fx.d_write] = (int16_t)sat16(fx_l[i] + (int32_t)(((int64_t)ol * fb) >> 16));
            fxbuf[fx.d_write] = (int16_t)sat16(fx_r[i] + (int32_t)(((int64_t)orr * fb) >> 16));
        } else {
            ol = tone(&fx.d_lp[0], ol);
            orr = ol;
            fxbuf[fx.d_write] = (int16_t)sat16(sat16(fx_l[i] + fx_r[i]) + (int32_t)(((int64_t)ol * fb) >> 16));
        }
        fx.d_write = (fx.d_write + 1u) & mask;
        if (st) {
            mix_l[i] += sat16(drywet(fx_l[i], ol));
            mix_r[i] += sat16(drywet(fx_r[i], orr));
        } else {
            side[i] += sat16(drywet(fx_l[i], ol));
        }
    }
}

/* ------------------------------------------------- PHASER, FLANGER --- */
/* their LFO: P1 0.05 .. ~9 Hz (exponential), its phase advanced a block (hz Q16 x BLOCK x 2^16 / FS: x 95.1) */
static void mod_lfo_step(void)
{
    int32_t hz = (int32_t)(((int64_t)3277 * exp2_q16((int32_t)(((int64_t)fx.p[0] * 7643) >> 10))) >> 16);
    fx.m_ph += (uint32_t)(((int64_t)hz * (int32_t)(BLOCK * 65536.0 * 1024.0 / FS)) >> 10);
}
static void fx_out(uint32_t c, uint32_t i, int32_t y)   /* a channel's sample of the bus into the mix */
{
    if (!fx_stereo())
        fx_side()[i] += y;
    else if (c)
        mix_r[i] += y;
    else
        mix_l[i] += y;
}
/* PHASER: 6 first-order allpasses a channel, their corner swept by the LFO (the right a quarter turn on) round a
 * CENTRE (P4: 100 Hz .. 3.2 kHz) over DEPTH (P2: up to +-3 octaves), FEEDBACK (P3: 0 .. 0.9) from the chain's end
 * to its start. DRY/WET 50%: the dry and the shifted halves, the notches deepest */
static void fx_phaser(void)
{
    uint32_t i, c, k, nc = fx_stereo() ? 2u : 1u;
    int32_t fb = fx.push ? 58982 : (int32_t)(((int64_t)fx.p[2] * 58982) >> 16);   /* Q16, 0 .. 0.9 */
    mod_lfo_step();
    for (c = 0; c < nc; c++) {
        /* the corner: 100 Hz x 2^(centre x 5 + depth x 3 x sin); w = 2 pi f / FS (Q16), a = (1 - w) / (1 + w) */
        int32_t oct = fx.p[3] * 5 + (int32_t)(((int64_t)sine_at(fx.m_ph + c * 0x40000000u) * fx.p[1] * 3) >> 15);
        int32_t f = (int32_t)(((int64_t)100 * exp2_q16(clamp32(oct, -65536, 6 * 65536))) >> 16);
        uint32_t w = (uint32_t)clamp32(f, 30, 6000) * 9u;
        int32_t a = (int32_t)((((65536u - w) << 15) / (65536u + w)) << 1);   /* Q16 */
        const int16_t *in = c ? fx_r : fx_l;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = in[i] + (int32_t)(((int64_t)fx.ph_fb[c] * fb) >> 16), y = 0;
            for (k = 0; k < 6u; k++) {               /* y = -a x + s, s = x + a y */
                y = fx.ph_s[c][k] - (int32_t)(((int64_t)a * x) >> 16);
                fx.ph_s[c][k] = clamp32(x + (int32_t)(((int64_t)a * y) >> 16), -(1 << 20), 1 << 20);
                x = y;
            }
            fx.ph_fb[c] = clamp32(y, -65536, 65536);
            fx_out(c, i, drywet(in[i], sat16(y)));
        }
    }
}
/* FLANGER: a short delay in the FX buffer (FL_LEN samples a channel), swept by the LFO between DELAY (P4: 0.2 ..
 * 10 ms) and DELAY x (1 - DEPTH) (P2), FEEDBACK (P3: -0.9 at 0, none at the middle, +0.9 at the top); the delay
 * glides across the block, read between samples */
#define FL_LEN 2048u
static void fx_flanger(void)
{
    uint32_t i, c, nc = fx_stereo() ? 2u : 1u;
    int32_t fb = (int32_t)(((int64_t)(fx.p[2] - 32768) * 117965) >> 16);   /* Q16, -0.9 .. 0.9 */
    if (fx.push)                                     /* (FB PUSH: the most, on the side it was) */
        fb = fb < 0 ? -58982 : 58982;
    int32_t dmax = 573 + (int32_t)(((int64_t)fx.p[3] * 28738) >> 16);       /* Q6 samples: 0.2 .. 10 ms */
    mod_lfo_step();
    for (c = 0; c < nc; c++) {
        const int16_t *in = c ? fx_r : fx_l;
        int16_t *b = fxbuf + c * FL_LEN;
        int32_t sw = 32768 - (sine_at(fx.m_ph + c * 0x40000000u) >> 1);      /* 0 .. 65535 */
        int32_t d1 = (dmax - (int32_t)(((int64_t)dmax * (((int64_t)fx.p[1] * sw) >> 16)) >> 16)) << 10;   /* Q16 */
        int32_t d0, dd;
        d1 = d1 < (2 << 16) ? 2 << 16 : d1;
        d0 = fx.fl_d[c] ? fx.fl_d[c] : d1;
        dd = (d1 - d0) / (int32_t)BLOCK;
        for (i = 0; i < BLOCK; i++, d0 += dd) {
            uint32_t w = fx.fl_w + i, di = (uint32_t)d0 >> 16, fr = (uint32_t)d0 & 0xFFFFu;
            int32_t a = b[(w - di) & (FL_LEN - 1u)], bb = b[(w - di - 1u) & (FL_LEN - 1u)];
            int32_t y = a + (int32_t)(((int64_t)(bb - a) * (int32_t)fr) >> 16);
            b[w & (FL_LEN - 1u)] = (int16_t)sat16(in[i] + (int32_t)(((int64_t)y * fb) >> 16));
            fx_out(c, i, drywet(in[i], y));
        }
        fx.fl_d[c] = d1;
    }
    fx.fl_w += BLOCK;
}

/* ---------------------------------------------------------- the bus --- */
static void fxbus_process(void)                      /* FxBus_process */
{
    uint32_t i;
    if (fx.type != fx.last_type) {                   /* (another type: its buffers and states from silence) */
        if (fx.type == FX_FLANGER || fx.type == FX_DELAY)
            for (i = 0; i < FXBUF_SIZE; i++)
                fxbuf[i] = 0;
        memset(fx.ph_s, 0, sizeof fx.ph_s);
        fx.ph_fb[0] = fx.ph_fb[1] = fx.fl_d[0] = fx.fl_d[1] = 0;
        fx.last_type = fx.type;
    }
    switch (fx.type) {
    case FX_PHASER:
        fx_phaser();
        break;
    case FX_FLANGER:
        fx_flanger();
        break;
    case FX_DRIVE:
        fx_drive();
        break;
    case FX_RING:
        fx_ring();
        break;
    case FX_COMP:
        fx_comp();
        break;
    case FX_DELAY:
        fx_delay();
        break;
    default:
        if (fx_stereo()) {
            for (i = 0; i < BLOCK; i++) {
                mix_l[i] += fx_l[i];
                mix_r[i] += fx_r[i];
            }
        } else {
            int32_t *side = fx_side();
            for (i = 0; i < BLOCK; i++)
                side[i] += fx_l[i];
        }
        break;
    }
}
