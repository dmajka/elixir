/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the LXR-02's FX bus in fixed point: DRIVE (tube, fold, clip), RING modulator, COMPRESSOR and DELAY
 * (mono, ping pong). One effect at a time; the voices routed to FX feed it, its output joins the mix.
 * Ported from the LXR firmware, Core/Src/Soundengine/FxBus.c and DSPAudio/FX (Distortion.c, Ringmod.c,
 * Compressor.c, Delay.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only. (The LXR's reverb is compiled out
 * there, USE_REVERB 0; Elixir's is Felucca's, a bus of its own: reverb.c.) A voice's FX SEND sets how much of it
 * goes through the bus (voices.c mix_voice). Elixir adds two types after the LXR's (0.3): PHASER and FLANGER. */

enum { FX_OFF, FX_DRIVE, FX_RING, FX_COMP, FX_DELAY, FX_PHASER, FX_FLANGER, NFX };
enum { DIST_TUBE, DIST_FOLD, DIST_RAT };
enum { DELAY_NORMAL, DELAY_PINGPONG };            /* (21: TAPE, TAPE PP after them) */

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
    uint8_t freeze;                                  /* PERF: FREEZE (the delay loops) */
    int32_t push;                                    /* .. FB PUSH, 0 .. 65536 (0.13: the global LFOs' amount) */
    int32_t lfo_fb, lfo_time;                        /* 19c: the global LFOs on the delay's feedback and time, -65536 ..
                                                      * 65536 */
    uint32_t lfo_n;                                  /* .. blocks since the time was last moved */
    /* 21: SYNC (TIME as note values), PITCH (DP_*), THROW REV's echoes too; SATURATE 0..127, ECHO > REVERB 0..65536,
     * GLITCH's chance 0..65535 */
    uint8_t d_sync, d_pitch, throw_echo;
    int32_t d_sat, d_echorev, d_glitch;
} fx;
/* 21b: SYNC's note values (TIME's 12 steps): 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/2T, 1/4., 1/2,
 * in ticks (96 a quarter note) */
#define NSYNCV 12u
static const uint16_t SYNC_T[NSYNCV] = {12, 16, 24, 32, 36, 48, 64, 72, 96, 128, 144, 192};
static uint32_t fx_bpm_x10 = 1200u;                  /* (seq.c: the tempo, as it changes) */
/* note value i's full-rate samples at the tempo, Q16 (32-bit sums: no 64-bit division) */
static uint64_t sync_len(uint32_t i)
{
    uint32_t x = fx_bpm_x10 ? fx_bpm_x10 : 1200u, n = SYNC_T[i < NSYNCV ? i : NSYNCV - 1u] * (60u * 10u * FS / 96u);
    return (uint64_t)(n / x) << 16 | ((n % x) << 16) / x;
}
/* a time t (Q16 0..1) as a note value's full-rate samples, Q16, halved until it fits in max (Q16) */
static uint32_t sync_samples(uint32_t t, uint64_t max)
{
    uint64_t s = sync_len((uint32_t)(((uint64_t)t * NSYNCV) >> 16));
    while (s > max)
        s >>= 1;
    return (uint32_t)s;
}


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
    if (fx.d_sync)                                   /* (21b: a note value, RANGE aside) */
        return sync_samples(t, hi);
    uint64_t mn = D_LOW_MIN + (((uint64_t)(D_LOW_MAX - D_LOW_MIN) * fx.d_range) >> 16);
    uint64_t mx = D_LOW_MAX + (((hi - D_LOW_MAX) * fx.d_range) >> 16);
    return (uint32_t)(mn + (((mx - mn) * cv) >> 16));
}
static uint32_t delay_mask(void) { return fx.delay_type == DELAY_NORMAL ? FXBUF_MASK : FXBUF_MASK >> 1; }
static void delay_set_at(uint32_t t, uint32_t w)    /* Delay_setDelayTime, the write head at w */
{
    if (fx.d_interp <= 0) {
        uint32_t off = delay_samples(t);
        fx.d_time = t;
        fx.d_fnew = off & 0xFFFFu;
        fx.d_new = (w - 1u - (off >> 16)) & delay_mask();
        fx.d_interp = 1 << 24;
        fx.d_step = (1 << 24) / 441;                 /* a 10 ms crossfade */
    } else {
        fx.d_pending = t;
        fx.d_has_pending = 1;
    }
}
static void delay_set_time(uint32_t t) { delay_set_at(t, fx.d_write); }
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
/* 18b: the delay in two halves. delay_reads reads the block's echoes (the read heads, their crossfade and a time
 * change waiting for it, as the LXR's loop does sample by sample); fx_delay writes the line (the input, the echoes
 * through the tone filter and the feedback) and mixes. When every read of the block is of a sample written before
 * it (the heads a block or more behind the write head: from ~1.5 ms; delay_far), the reads run early, on core 0
 * while the second core renders D1-D3 (voices.c fx_early), and the echoes are exactly the same as read in the loop:
 * no latency */
static int32_t d_ol[BLOCK] POOL, d_or[BLOCK] POOL;   /* the block's echoes, left and right (mono: left) */
static uint8_t d_read;                               /* .. read already this block */
static int delay_far(void)
{
    uint32_t mask = delay_mask();
    if (((fx.d_write - fx.d_old) & mask) <= BLOCK + 1u)
        return 0;
    if (fx.d_interp > 0 && ((fx.d_write - fx.d_new) & mask) <= BLOCK + 1u)
        return 0;
    return !fx.d_has_pending || (delay_samples(fx.d_pending) >> 16) >= BLOCK + 1u;
}
static void delay_step(uint32_t w, int32_t *ol, int32_t *orr)   /* one sample's echoes, the write head at w */
{
    uint32_t mask = delay_mask(), half = FXBUF_SIZE >> 1;
    int pp = fx.delay_type != DELAY_NORMAL;
    if (fx.d_interp <= 0) {
        *ol = delay_read(0, fx.d_old, fx.d_fold, mask);
        *orr = pp ? delay_read(half, fx.d_old, fx.d_fold, mask) : *ol;
        fx.d_old = (fx.d_old + 1u) & mask;
        if (fx.d_has_pending) {
            fx.d_has_pending = 0;
            delay_set_at(fx.d_pending, w);
        }
    } else {
        int32_t al = delay_read(0, fx.d_old, fx.d_fold, mask), bl = delay_read(0, fx.d_new, fx.d_fnew, mask);
        int32_t g = fx.d_interp >> 9;                /* Q15 */
        *ol = (al * g + bl * (32768 - g)) >> 15;
        if (pp) {
            int32_t ar = delay_read(half, fx.d_old, fx.d_fold, mask), br = delay_read(half, fx.d_new, fx.d_fnew, mask);
            *orr = (ar * g + br * (32768 - g)) >> 15;
        } else {
            *orr = *ol;
        }
        fx.d_old = (fx.d_old + 1u) & mask;
        fx.d_new = (fx.d_new + 1u) & mask;
        fx.d_interp -= fx.d_step;
        if (fx.d_interp <= 0) {
            fx.d_old = fx.d_new;
            fx.d_fold = fx.d_fnew;
        }
    }
}
#ifdef FX_NOW
static uint32_t d_early_blocks;                      /* (host tests: the blocks read early) */
#endif
static void delay_reads(void)                        /* the block's echoes, early (delay_far) */
{
    uint32_t i;
#ifdef FX_NOW
    d_early_blocks++;
#endif
    for (i = 0; i < BLOCK; i++)
        delay_step((fx.d_write + i) & delay_mask(), &d_ol[i], &d_or[i]);
    d_read = 1;
}

/* ------------------------------------------------------------ TAPE --- */
/* 21: TAPE and TAPE PP, two DELAY types of Elixir's beside the LXR's NORMAL and PINGPONG (which are as they were).
 * The line is stored at half rate (22.05 kHz: 1.49 s mono, 0.74 s a side, in the same fxbuf); the block's input
 * pairs averaged in, the echoes read between samples and interpolated back up. P1 TIME (10 ms .. the most, or with
 * SYNC a note value at the tempo), P2 WOW (wow: a slow random drift, flutter: ~6 Hz, on the read head), P3 TONE (the
 * feedback's low-pass), P4 FEEDBACK (to 115%). In the loop: a low cut (~100 Hz), the tone, PITCH's shifter, then the
 * tape's soft saturation (SATURATE: from a gentle round-off at full scale down to a 1/4 ceiling), so feedback past
 * 100% grows into a roar, not a clip. A time change glides the read head (the pitch bends, as a tape's motor).
 * GLITCH: the chance a repeat is changed: STUTTER (it loops its first 1/32 or 1/64), REVERSE, SKIP (an earlier
 * slice), HALF (half speed: an octave down), faded in and out over 32 samples */
enum { DELAY_TAPE = 2, DELAY_TAPEPP, NDELAY };
enum { DP_OFF, DP_UP12, DP_UP7, DP_DOWN12, DP_DETUNE, NDPITCH };
enum { GL_NONE, GL_STUTTER, GL_REVERSE, GL_SKIP, GL_HALF };
#define TP_MIN (220u << 16)                          /* 10 ms at half rate */
#define TP_FADE 32u
#define PS_LEN 2048u                                 /* the pitch shifter's window (mono; a side: half) */
static int16_t ps_buf[PS_LEN] POOL;
static struct {
    uint32_t w;                                      /* the write head (half-rate samples) */
    int32_t cur, tgt;                                /* the delay now and where it glides, Q16 half-rate samples */
    int32_t prev[2];                                 /* the last half-rate echo (the upsampling) */
    int32_t hp[2], lp[2];                            /* the low cut's and the tone's states, Q8 */
    int32_t wow, wowt;                               /* WOW: the drift and its target, Q16 samples */
    uint32_t wown, flph, rng;
    uint8_t gmode;                                   /* GLITCH: this repeat's change (GL_*) */
    uint32_t gleft, glen, gskip, gst;                /* .. half-rate samples left, its length, SKIP's slices, STUTTER's */
    uint32_t psw, psd;                               /* the shifter's write index, its delay (Q16) */
} tp;
static uint32_t tp_rand(void)
{
    tp.rng = tp.rng * 1664525u + 1013904223u;
    return tp.rng >> 8;
}
static int is_tape(void) { return fx.delay_type == DELAY_TAPE || fx.delay_type == DELAY_TAPEPP; }
static uint32_t tape_max(void) { return (fx.delay_type == DELAY_TAPE ? FXBUF_SIZE : FXBUF_SIZE >> 1) - 2u * BLOCK; }
static int32_t tape_target(void)                     /* P1 (and the global LFOs) as the delay, Q16 half-rate */
{
    uint32_t t = (uint32_t)clamp32(fx.p[0] + (fx.lfo_time >> 2), 0, 65536), mx = tape_max() << 16;
    if (fx.d_sync)
        return (int32_t)(sync_samples(t, (uint64_t)mx << 1) >> 1);
    return (int32_t)(TP_MIN + (uint32_t)(((uint64_t)(((uint64_t)t * t) >> 16) * (mx - TP_MIN)) >> 16));
}
static inline int32_t tape_read(uint32_t base, uint32_t mask, int32_t d)   /* the line d (Q16) behind the head */
{
    uint32_t p = (tp.w << 16) - (uint32_t)d, i = (p >> 16) & mask, f = p & 0xFFFFu;
    int32_t a = fxbuf[base + i], b = fxbuf[base + ((i + 1u) & mask)];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)f) >> 16);
}
/* the cubic soft clip (y = u - 4/27 u^3, flat at u = 1.5), its ceiling 32768 / g: x x g in, x 1/g out (gi: 2^32 / g) */
static inline int32_t tape_sat(int32_t x, int32_t g, uint32_t gi)
{
    int32_t u = (int32_t)(((int64_t)x * g) >> 16), u3;
    u = u > 49152 ? 49152 : u < -49152 ? -49152 : u;
    u3 = (int32_t)(((int64_t)u * u >> 15) * u >> 15);
    u -= u3 * 4 / 27;
    return (int32_t)(((int64_t)u * gi) >> 16);
}
static int32_t tape_shift(int32_t x, uint32_t c)     /* PITCH: two crossfaded heads over the window (side c) */
{
    static const uint32_t STEP[NDPITCH] = {0, 65536, 32657, 0, 456};   /* (ratio - 1), Q16; down: (1 - ratio) */
    uint32_t sh = fx.delay_type == DELAY_TAPE ? 11u : 10u, n = 1u << sh, base = c * (PS_LEN >> 1), m = n - 1u;
    uint32_t wm = (n << 16) - 1u, d0 = tp.psd & wm;
    int32_t y = 0, k;
    ps_buf[base + (tp.psw & m)] = (int16_t)sat16(x);
    for (k = 0; k < 2; k++) {
        uint32_t d = k ? (d0 + (n << 15)) & wm : d0, p = ((tp.psw & m) << 16) - d - (2u << 16);
        uint32_t ii = (p >> 16) & m, f = p & 0xFFFFu, tri = d >> sh;   /* (tri: 0 .. 65535 over the window) */
        int32_t a = ps_buf[base + ii], b = ps_buf[base + ((ii + 1u) & m)];
        int32_t s = a + (int32_t)(((int64_t)(b - a) * (int32_t)f) >> 16);
        int32_t wv = (int32_t)(tri < 32768u ? tri * 2u : (65535u - tri) * 2u);
        y += (int32_t)(((int64_t)s * wv) >> 16);
    }
    if (c == 0u || fx.delay_type == DELAY_TAPE)     /* (the heads move once a sample; down: the delay grows) */
        tp.psd = fx.d_pitch == DP_DOWN12 ? (d0 + 32768u) & wm : (d0 - STEP[fx.d_pitch < NDPITCH ? fx.d_pitch : 0]) & wm;
    return y;
}
static void tape_glitch(void)                        /* a repeat starts: changed, at GLITCH's chance */
{
    uint32_t len = (uint32_t)tp.cur >> 16;
    tp.glen = tp.gleft = len > 64u ? len : 64u;
    tp.gmode = GL_NONE;
    if (!fx.d_glitch || (tp_rand() & 0xFFFFu) >= fx.d_glitch)
        return;
    tp.gmode = (uint8_t)(GL_STUTTER + (tp_rand() & 3u));
    if (tp.gmode == GL_STUTTER) {                    /* (1/32 or 1/64 at the tempo, at half rate) */
        tp.gst = (60u * 10u * FS / 2u) / (fx_bpm_x10 ? fx_bpm_x10 : 1200u) / ((tp_rand() & 1u) ? 8u : 16u);
        tp.gst = tp.gst < 32u ? 32u : tp.gst;
    }
    tp.gskip = 1u + tp_rand() % 3u;
    if ((tp.gmode == GL_REVERSE && 3u * len + 64u > tape_max()) ||
        (tp.gmode == GL_SKIP && (tp.gskip + 1u) * len + 64u > tape_max()) || (tp.gmode == GL_HALF && 2u * len > tape_max()))
        tp.gmode = GL_NONE;                          /* (no room in the line for it) */
}
static void fx_tape(void)
{
    uint32_t h, c, mask = fx.delay_type == DELAY_TAPE ? FXBUF_MASK : FXBUF_MASK >> 1, half = FXBUF_SIZE >> 1;
    int pp = fx.delay_type == DELAY_TAPEPP, st = fx_stereo();
    int32_t *side = fx_side(), e[2], fb, g = 65536 + fx.d_sat * 3 * 516;   /* (SATURATE: 1 .. 4) */
    uint32_t gi = (uint32_t)(0xFFFFFFFFu / (uint32_t)g) + 1u;
    int32_t wowd = (int32_t)(((int64_t)fx.p[1] * (40 << 16)) >> 16), fld = (int32_t)(((int64_t)fx.p[1] * (3 << 16)) >> 16);
    int32_t er = fx.d_echorev, rv_sum = 0;
    fb = (int32_t)(((int64_t)fx.p[3] * 75366) >> 16);   /* (to 115%) */
    fb += (int32_t)(((int64_t)(75366 - fb) * fx.push) >> 16);
    if (fx.lfo_fb)
        fb = clamp32(fb + fx.lfo_fb, 0, 75366);
    tp.tgt = tape_target();
    if (!tp.cur)
        tp.cur = tp.tgt;
    for (h = 0; h < BLOCK / 2u; h++) {
        int32_t d, in[2], env = 65536;
        uint32_t k;
        tp.cur += (tp.tgt - tp.cur) >> 11;           /* (the glide: ~90 ms) */
        if (++tp.wown >= 4096u) {                    /* WOW: a new drift every ~0.19 s */
            tp.wown = 0;
            tp.wowt = (int32_t)(((int64_t)((int32_t)(tp_rand() & 0xFFFFu) - 32768) * wowd) >> 15);
        }
        tp.wow += (tp.wowt - tp.wow) >> 11;
        tp.flph += 1168730u;                         /* (~6 Hz at 22.05 kHz) */
        d = tp.cur + tp.wow + (int32_t)(((int64_t)sine_at(tp.flph) * fld) >> 15);
        if (!tp.gleft)
            tape_glitch();
        k = tp.glen - tp.gleft;
        tp.gleft--;
        switch (tp.gmode) {                          /* GLITCH: the repeat read another way */
        case GL_STUTTER: d += (int32_t)((k - k % tp.gst) << 16); break;
        case GL_REVERSE: d += (int32_t)((2u * k) << 16); break;
        case GL_SKIP: d += (int32_t)((tp.gskip * tp.glen) << 16); break;
        case GL_HALF: d += (int32_t)(k << 15); break;
        default: break;
        }
        if (tp.gmode != GL_NONE) {                   /* (faded in and out: no clicks) */
            uint32_t m = k < tp.gleft ? k : tp.gleft;
            env = m < TP_FADE ? (int32_t)(m * 65536u / TP_FADE) : 65536;
        }
        d = clamp32(d, (int32_t)(2u << 16), (int32_t)(mask << 16));
        e[0] = tape_read(pp ? half : 0u, mask, d);
        e[1] = pp ? tape_read(0, mask, d) : e[0];
        if (env < 65536) {
            e[0] = (int32_t)(((int64_t)e[0] * env) >> 16);
            e[1] = (int32_t)(((int64_t)e[1] * env) >> 16);
        }
        in[0] = (fx_l[2u * h] + fx_l[2u * h + 1u]) >> 1;
        in[1] = (fx_r[2u * h] + fx_r[2u * h + 1u]) >> 1;
        if (!pp)
            in[0] = sat16(in[0] + in[1]);
        for (c = 0; c < (pp ? 2u : 1u); c++) {       /* the feedback: low cut, tone, PITCH, onto the tape */
            int32_t x = e[pp ? 1u - c : c], y;      /* (PP: each side's echo feeds the other) */
            int32_t lo;
            if (fx.freeze) {
                y = x;
            } else {
                tp.hp[c] += (int32_t)(((int64_t)1843 * ((x << 8) - tp.hp[c])) >> 16);   /* (~100 Hz: 1 - 0.9719) */
                lo = tp.hp[c] >> 8;
                x -= lo;
                tp.lp[c] += (int32_t)(((int64_t)fx.d_tone * ((x << 8) - tp.lp[c])) >> 16);
                x = tp.lp[c] >> 8;
                if (fx.d_pitch)
                    x = tape_shift(x, c);
                y = tape_sat((pp ? in[c] : in[0]) + (int32_t)(((int64_t)x * fb) >> 16), g, gi);
            }
            fxbuf[(pp ? (c ? 0u : half) : 0u) + tp.w] = (int16_t)sat16(y);
        }
        if (fx.d_pitch)
            tp.psw++;
        tp.w = (tp.w + 1u) & mask;
        for (c = 0; c < 2u; c++) {                   /* back up to the full rate: halfway, then the new */
            int32_t a = (tp.prev[c] + e[c]) >> 1, b = e[c];
            uint32_t i = 2u * h;
            if (st) {
                int32_t *o = c ? mix_r : mix_l;
                const int16_t *dry = c ? fx_r : fx_l;
                o[i] += sat16(drywet(dry[i], a));
                o[i + 1u] += sat16(drywet(dry[i + 1u], b));
            } else if (!c) {
                side[i] += sat16(drywet(fx_l[i], a));
                side[i + 1u] += sat16(drywet(fx_l[i + 1u], b));
            }
            if (er) {                                /* 21c: ECHO > REVERB: the echoes into the reverb's send */
                rev_in[i] += (int32_t)(((int64_t)a * er) >> 17);
                rev_in[i + 1u] += (int32_t)(((int64_t)b * er) >> 17);
                rv_sum |= a | b;
            }
            tp.prev[c] = e[c];
        }
    }
    if (rv_sum)
        rev_fed = 1;
}

static void fx_delay(void)                           /* Delay_process / Delay_processPingPong */
{
    uint32_t i, mask = delay_mask(), half = FXBUF_SIZE >> 1;
    int pp = fx.delay_type != DELAY_NORMAL, st = fx_stereo();
    int32_t er = fx.throw_echo ? 65536 : fx.d_echorev, rv_sum = 0;
    if (is_tape()) {
        fx_tape();
        d_read = 0;
        return;
    }
    int32_t *side = fx_side(), fb = fx.d_fb + (int32_t)(((int64_t)(62259 - fx.d_fb) * fx.push) >> 16);   /* (FB PUSH) */
    if (fx.lfo_fb)                                   /* (19c: the global LFOs, either way) */
        fb = clamp32(fb + fx.lfo_fb, 0, 65536);
    for (i = 0; i < BLOCK; i++) {
        int32_t ol, orr;
        if (d_read) {
            ol = d_ol[i];
            orr = d_or[i];
        } else {
            delay_step(fx.d_write, &ol, &orr);       /* (near: read between the writes) */
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
        if (er) {                                    /* 21c: ECHO > REVERB */
            rev_in[i] += (int32_t)(((int64_t)(ol + orr) * er) >> 17);
            rv_sum |= ol | orr;
        }
    }
    if (rv_sum)
        rev_fed = 1;
    d_read = 0;
}
/* 21b: the tempo (seq.c, as it changes): a synced NORMAL or PINGPONG time again (crossfaded); TAPE glides there */
static void fx_tempo(uint32_t x10)
{
    if (x10 == fx_bpm_x10)
        return;
    fx_bpm_x10 = x10;
    if (fx.d_sync && fx.type == FX_DELAY && !is_tape())
        delay_set_time(fx.d_time);
}

/* 18b: what the bus can do before the mixer has its input (voices.c fx_early): the delay's reads, when far */
static void fxbus_early(void)
{
    d_read = 0;
    if (fx.type == FX_DELAY && !is_tape() && (fx.lfo_time || fx.lfo_n) && ++fx.lfo_n >= 16u && fx.d_interp <= 0) {
        uint32_t t = (uint32_t)clamp32(fx.p[0] + (fx.lfo_time >> 2), 0, 65536);   /* (19c: the LFOs on the time, +-1/4
                                                      * of its range, every 16 blocks: crossfaded) */
        fx.lfo_n = fx.lfo_time ? 1u : 0u;
        if (t != fx.d_time)
            delay_set_time(t);
    }
#ifdef FX_NOW
    if (fx_now)
        return;
#endif
    if (fx.type == FX_DELAY && fx.last_type == FX_DELAY && !is_tape() && delay_far())
        delay_reads();
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
    int32_t fb = (int32_t)(((int64_t)fx.p[2] * 58982) >> 16);   /* Q16, 0 .. 0.9 */
    fb += (int32_t)(((int64_t)(58982 - fb) * fx.push) >> 16);   /* (FB PUSH: to the most) */
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
    if (fx.push)                                     /* (FB PUSH: to the most, on the side it was) */
        fb += (int32_t)(((int64_t)((fb < 0 ? -58982 : 58982) - fb) * fx.push) >> 16);
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

/* ------------------------------------------------------------ DIRT --- */
/* 23b: the bus's DIRT (any type, before it: into the echo, the phaser ..): a gain of 1 .. 8 into soft3 (voices.c),
 * the level trimmed back as it rises. 23c: the master's DIRT, the mix before the DJ filter (engine.c): a TYPE and an
 * AMOUNT, each type's gain, tone (a one-pole high cut) and dry / wet set by the amount. TAPE (soft, darker as it
 * rises), TUBE (soft and biased: even harmonics, the DC taken out), FUZZ (a hard clip, parallel: the kick's punch
 * kept), DIODE (the LXR's 1N914 curve), FOLD (a sine fold, parallel). At 0, off: nothing touched. The wet part fades
 * in over the first quarter of the amount (no step at its start) */
enum { DT_TAPE, DT_TUBE, DT_FUZZ, DT_DIODE, DT_FOLD, NDIRT };
static struct {
    int32_t bus, amt;                                /* the amounts, 0 .. 65536 */
    uint8_t type;
    int32_t lp[2], dc[2];                            /* the master's tone, TUBE's DC follower (a channel each) */
} dirt;
/* each type: its gain at 0 and at the most (Q8), the wet share (Q15), the high cut's coefficient at 0 and at the most
 * (Q15: 32767 none, 0.5 ~5 kHz), the level trimmed at the most (Q15) */
static const struct { int32_t g0, g1, wet, c0, c1, trim; } DIRT_T[NDIRT] = {
    {256, 1536, 32768, 29000, 16384, 13107},         /* TAPE */
    {256, 2560, 32768, 32767, 24000, 13107},         /* TUBE */
    {512, 10240, 22938, 26000, 18000, 16384},        /* FUZZ */
    {256, 3072, 29491, 31000, 22000, 9830},          /* DIODE */
    {256, 1536, 19661, 32767, 28000, 4096},          /* FOLD */
};
static inline int32_t dirt_curve(uint32_t t, int32_t x, uint32_t c)
{
    switch (t) {
    case DT_TUBE: {
        int32_t y = soft3(x + 8192) - 8117;          /* (soft3 of the bias, 0.25) */
        dirt.dc[c] += (y - dirt.dc[c]) >> 10;
        return y - dirt.dc[c];
    }
    case DT_FUZZ:
        return (clamp32(x, -24576, 24576) * 4) / 3;
    case DT_DIODE:
        return diode(x * 2) >> 1;
    case DT_FOLD:
        return sine_at(((uint32_t)x << 15) + 0x40000000u);   /* (sin: voices.c SH_SFOLD) */
    default:                                         /* DT_TAPE */
        return soft3(x);
    }
}
static int32_t dirt_wet(int32_t a, int32_t wet)      /* the wet share: faded in over the amount's first quarter */
{
    return a >= 16384 ? wet : (int32_t)(((int64_t)wet * a) >> 14);
}
static void dirt_bus(void)
{
    uint32_t i, c, nc = fx_stereo() ? 2u : 1u;
    int32_t g = 256 + (int32_t)(((int64_t)dirt.bus * 1792) >> 16), w = dirt_wet(dirt.bus, 32768);
    int32_t tr = 32768 - (int32_t)(((int64_t)dirt.bus * 9830) >> 16);
    if (!dirt.bus)
        return;
    for (c = 0; c < nc; c++) {
        int16_t *x = c ? fx_r : fx_l;
        for (i = 0; i < BLOCK; i++) {
            int32_t y = (soft3((x[i] * g) >> 8) * tr) >> 15;
            x[i] = (int16_t)sat16(x[i] + (int32_t)(((int64_t)(y - x[i]) * w) >> 15));
        }
    }
}
static void dirt_master(int32_t *l, int32_t *r)      /* the mix (engine.c, before it is saturated to 16 bits) */
{
    uint32_t i, c, t = dirt.type < NDIRT ? dirt.type : DT_TAPE;
    int32_t a = dirt.amt, g, w, k, tr;
    if (!a)
        return;
    g = DIRT_T[t].g0 + (int32_t)(((int64_t)(DIRT_T[t].g1 - DIRT_T[t].g0) * a) >> 16);
    w = dirt_wet(a, DIRT_T[t].wet);
    k = DIRT_T[t].c0 + (int32_t)(((int64_t)(DIRT_T[t].c1 - DIRT_T[t].c0) * a) >> 16);
    tr = 32768 - (int32_t)(((int64_t)DIRT_T[t].trim * a) >> 16);
    for (c = 0; c < 2u; c++) {
        int32_t *x = c ? r : l, lp = dirt.lp[c];
        for (i = 0; i < BLOCK; i++) {
            int32_t d = clamp32(x[i], -65536, 65536), y = dirt_curve(t, clamp32((d * g) >> 8, -262144, 262144), c);
            lp += (int32_t)(((int64_t)(y - lp) * k) >> 15);
            x[i] = d + (int32_t)(((int64_t)((((int64_t)lp * tr) >> 15) - d) * w) >> 15);
        }
        dirt.lp[c] = lp;
    }
}

/* ------------------------------------------------------------ COMP --- */
/* 0.16: the master's COMPRESSOR (FX's COMP pages, the kit's), on the mix before the master's DIRT (engine.c).
 * THRESHOLD 0 .. -48 dB, RATIO (OFF: nothing touched; 1.5:1 .. LIMIT), ATTACK 0.1 .. 100 ms, RELEASE 20 ms .. 2 s,
 * MAKEUP 0 .. +24 dB, MIX (dry / wet: parallel at less than all), SIDECHAIN: the mix itself (a bus compressor), or a
 * voice (D1 .. HH): its sound (before its sends and pan) is what the compressor hears, and its own dry part is kept
 * out of the gain (voices.c mc_key), so a kick ducks everything else and keeps its punch. A peak follower each
 * sample, the gain (a 6 dB soft knee) every 8 samples, ramped between */
#define NCRATIO 8u
#define MC_SUB 8u
static const int32_t CR_K[NCRATIO] = {0, 21845, 32768, 43691, 49152, 54613, 58982, 65536};   /* 1 - 1/ratio, Q16 */
static struct {
    uint8_t ratio, src;                              /* RATIO's index (0: off), SIDECHAIN (0: the mix, 1 .. 6) */
    int32_t thr, makeup, mix;                        /* log2 of full scale, Q16 (-8 .. 0, 0 .. 4); MIX Q16 */
    int32_t atk, rel;                                /* the follower's coefficients, Q24 */
    int32_t env, g;                                  /* the follower (Q15 x 4096), the gain in use (Q16) */
    volatile int32_t gr;                             /* the last block's most gain reduction, log2 Q16 (the screen) */
} mc = {.g = 65536, .mix = 65536};
static int32_t mc_coeff(uint32_t t)                  /* a time in samples as the follower's coefficient */
{
    return (int32_t)((1u << 24) / (t + 1u));
}
static void mc_set(uint32_t p, uint32_t v)           /* (sound.c: the kit's values, 0 .. 127, the menus' index) */
{
    switch (p) {
    case 0: mc.ratio = (uint8_t)(v < NCRATIO ? v : 0u); break;
    case 1: mc.thr = -(int32_t)((127u - (v & 127u)) * 8u * 65536u / 127u); break;
    case 2:                                          /* (4.41 samples x 1000^(v / 127)) */
        mc.atk = mc_coeff((uint32_t)(((uint64_t)289014u * (uint32_t)exp2_q16((int32_t)(v & 127u) * 5143)) >> 32));
        break;
    case 3:                                          /* (882 samples x 100^(v / 127)) */
        mc.rel = mc_coeff((uint32_t)(((uint64_t)882u * (uint32_t)exp2_q16((int32_t)(v & 127u) * 3428)) >> 16));
        break;
    case 4: mc.src = (uint8_t)(v <= NVOICE ? v : 0u); break;
    case 5: mc.makeup = (int32_t)((v & 127u) * 4u * 65536u / 127u); break;
    default: mc.mix = (int32_t)((v & 127u) * 65536u / 127u); break;
    }
    mc_key = mc.ratio ? mc.src : 0u;                 /* (voices.c: that voice's dry part kept out) */
}
static void comp_master(int32_t *l, int32_t *r)      /* the mix (engine.c), then the key voice's dry part back in */
{
    uint32_t i, j;
    int32_t grmax = 0;
    if (mc.ratio) {
        const int16_t *key = mc.src ? vbuf[mc.src - 1u] : 0;
        for (i = 0; i < BLOCK; i += MC_SUB) {
            int32_t lv, over, eff, gt, dg;
            for (j = i; j < i + MC_SUB; j++) {       /* the follower */
                int32_t x = key ? iabs(key[j]) : iabs(l[j]) > iabs(r[j]) ? iabs(l[j]) : iabs(r[j]);
                int32_t d = (clamp32(x, 0, 262143) << 12) - mc.env;
                mc.env += (int32_t)(((int64_t)d * (d > 0 ? mc.atk : mc.rel)) >> 24);
            }
            lv = mc.env >= 4096 ? log2_q16((uint32_t)mc.env >> 12) - (15 << 16) : -(40 << 16);
            over = lv - mc.thr;
            eff = over <= -32768 ? 0 : over < 32768 ? (int32_t)(((int64_t)(over + 32768) * (over + 32768)) >> 17) : over;
            eff = (int32_t)(((int64_t)eff * CR_K[mc.ratio]) >> 16);
            grmax = eff > grmax ? eff : grmax;
            gt = exp2_q16(mc.makeup - eff);
            dg = (gt - mc.g) / (int32_t)MC_SUB;
            for (j = i; j < i + MC_SUB; j++) {       /* the gain, ramped; MIX: dry and wet */
                int32_t w;
                mc.g += dg;
                w = 65536 + (int32_t)(((int64_t)(mc.g - 65536) * mc.mix) >> 16);
                l[j] = (int32_t)(((int64_t)l[j] * w) >> 16);
                r[j] = (int32_t)(((int64_t)r[j] * w) >> 16);
            }
            mc.g = gt;
        }
    } else {
        mc.env = 0;
        mc.g = 65536;
    }
    mc.gr = grmax;
    if (mc_key)
        for (i = 0; i < BLOCK; i++) {
            l[i] += mc_kl[i];
            r[i] += mc_kr[i];
        }
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
    dirt_bus();
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
