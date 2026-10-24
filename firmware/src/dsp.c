/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the LXR-02's DSP building blocks in fixed point (the FM-1 has no FPU).
 * Ported from the LXR firmware, Core/Src/Soundengine/DSPAudio (Oscillator.c, transientGenerator.c, SlopeEg2.c,
 * Decay.c, snapEg.c, ResonantFilter.c, distortion.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only.
 *
 * Number formats: audio int16 (the LXR's own); gains Q15 (32768 = 1); envelope values Q24; filter states and
 * coefficients Q16 (65536 = 1.0 = a full-scale sample). Each block is BLOCK (64) samples: the LXR computes
 * envelopes, LFOs and coefficients once a block and the oscillators, filters and shapers per sample, and so does
 * this port. Where the LXR's float code relies on a hardware quirk (a negative float converted to unsigned is 0 on
 * the Cortex-M7), the port does the same so the voices sound as they do there. */

static inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t mulq16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* a 1025-point table over a 0..1 input (Q16, 0..65536), linear between points */
static inline int32_t lut1024(const int32_t *t, uint32_t q16)
{
    uint32_t i, f;
    if (q16 >= 65536u)
        return t[1024];
    i = q16 >> 6;
    f = q16 & 63u;
    return t[i] + (int32_t)(((int64_t)(t[i + 1] - t[i]) * (int32_t)f) >> 6);
}
static inline uint32_t lut1024u(const uint32_t *t, uint32_t q16)
{
    uint32_t i, f;
    if (q16 >= 65536u)
        return t[1024];
    i = q16 >> 6;
    f = q16 & 63u;
    return t[i] + (uint32_t)(((int64_t)((int64_t)t[i + 1] - t[i]) * (int32_t)f) >> 6);
}

/* ------------------------------------------------------------ random --- */
static uint32_t noise_rng = 0x9E3779B9u;            /* the LXR's hardware RNG: here xorshift32 */
static inline uint32_t noise_next(void)
{
    uint32_t x = noise_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    noise_rng = x;
    return x;
}

/* -------------------------------------------------------- oscillator --- */
enum { W_SINE, W_TRI, W_SAW, W_REC, W_NOISE, W_PWM, NWAVE };
#define SEQ_DEFAULT_NOTE 60

typedef struct {
    uint32_t phase, inc;
    uint32_t note_inc;           /* the played note with its fine tune (osc_recalcFreq's freq), before pitchMod */
    uint16_t midi_freq;          /* coarse << 8 | fine (0..127 each) */
    uint8_t wave, tab, base_note;
    int16_t out;                 /* noise: the value held until the phase wraps */
} osc_t;

static const int16_t *const WT[] = {0, LXR_TRI[0], LXR_SAW[0], LXR_REC[0], 0, LXR_SAW[0]};

/* MidiParser.c midiParser_calcDetune: 1 + (fine / 127 - 0.5) x (2^(1/12) - 1), Q16 */
static inline uint32_t fine_q16(uint32_t fine) { return 65536u + (uint32_t)(((int32_t)fine * 2 - 127) * 3897 / 254); }

static void osc_recalc(osc_t *o)                     /* Oscillator.c osc_recalcFreq */
{
    int32_t n = (int32_t)(o->midi_freq >> 8) + (int32_t)o->base_note - SEQ_DEFAULT_NOTE;
    n = clamp32(n, 0, 127);
    o->note_inc = (uint32_t)(((uint64_t)NOTE_INC[n] * fine_q16(o->midi_freq & 0xFFu)) >> 16);
}
static void osc_base_note(osc_t *o, uint32_t note)  /* osc_setBaseNote */
{
    o->base_note = (uint8_t)note;
    osc_recalc(o);
}
/* osc_setFreq: the block's increment of the note x pitchMod (Q16), and the overtone table freqToTableIndex picks */
static void osc_set_freq(osc_t *o, uint32_t pmod)
{
    uint64_t inc = ((uint64_t)o->note_inc * pmod) >> 16;
    uint32_t t = 4, lim = INC_440;
    o->inc = inc > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)inc;    /* (the LXR's float -> uint32 saturates) */
    while (t < 10u && o->inc >= lim) {
        t++;
        lim = lim < 0x80000000u ? lim << 1 : 0xFFFFFFFFu;
    }
    o->tab = (uint8_t)t;
}

static inline int32_t wt_at(const int16_t *t, uint32_t ph)      /* 1024-point table, 22-bit fraction */
{
    uint32_t i = ph >> 22, f = (ph >> 7) & 0x7FFFu;
    int32_t a = t[i], b = t[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)f) >> 15);
}
static inline int32_t sine_at(uint32_t ph)                      /* 4096-point table, 20-bit fraction */
{
    uint32_t i = ph >> 20, f = (ph >> 5) & 0x7FFFu;
    int32_t a = LXR_SINE[i], b = LXR_SINE[i + 1u];
    return a + (((b - a) * (int32_t)f) >> 15);
}

/* one block of an oscillator, x gain (Q15). fm: the modulator (0: none), its depth fmq (Q15, -1: the full
 * sample as the cymbal's 1.0). pwm: the second saw's offset, Q16 of half a cycle */
static void osc_block(osc_t *o, int16_t *buf, int32_t gain, const int16_t *fm, int32_t fmq, uint32_t pwm)
{
    uint32_t i, ph = o->phase, inc = o->inc;
    uint32_t sh = o->wave == W_SINE ? 17u : 19u;
    uint32_t pw = (uint32_t)(((uint64_t)pwm * 0x7FFFFFFFu) >> 16);
    switch (o->wave) {
    case W_SINE:
        for (i = 0; i < BLOCK; i++) {
            uint32_t idx = ph;
            if (fm) {
                int32_t m = fmq < 0 ? fm[i] : (fm[i] * fmq) >> 15;
                if (m > 0)                                  /* (uint32_t)(negative float) == 0 on the LXR */
                    idx += (uint32_t)m << sh;
            }
            buf[i] = (int16_t)((sine_at(idx) * gain) >> 15);
            ph += inc;
        }
        break;
    case W_TRI:
    case W_SAW:
    case W_REC: {
        const int16_t *t = WT[o->wave] + o->tab * 1024u;
        for (i = 0; i < BLOCK; i++) {
            uint32_t idx = ph;
            if (fm) {
                int32_t m = fmq < 0 ? fm[i] : (fm[i] * fmq) >> 15;
                if (m > 0)
                    idx += (uint32_t)m << sh;
            }
            buf[i] = (int16_t)((wt_at(t, idx) * gain) >> 15);
            ph += inc;
        }
        break;
    }
    case W_PWM: {                                           /* calcPwmBlock: saw - saw shifted by pwm */
        const int16_t *t = LXR_SAW[o->tab];
        for (i = 0; i < BLOCK; i++) {
            uint32_t idx = ph;
            if (fm) {
                int32_t m = fmq < 0 ? fm[i] : (fm[i] * fmq) >> 15;
                if (m > 0)
                    idx += (uint32_t)m << sh;
            }
            uint32_t idx2 = idx + pw;
            if (idx2 < idx)                             /* the LXR adds in float: a sum past 2^32 saturates */
                idx2 = 0xFFFFFFFFu;
            buf[i] = (int16_t)((sat16(wt_at(t, idx) - wt_at(t, idx2)) * gain) >> 15);
            ph += inc;
        }
        break;
    }
    default: {                                              /* NOISE: a new random value each time the phase wraps */
        int32_t out = o->out;
        for (i = 0; i < BLOCK; i++) {
            uint32_t last = ph;
            ph += inc;
            if (last > ph)
                out = (int16_t)noise_next();
            buf[i] = (int16_t)((out * gain) >> 15);
        }
        o->out = (int16_t)out;
        break;
    }
    }
    o->phase = ph;
}

/* the start phase on a trigger (Drum_trigger, Snare_trigger ..): offset 0..1 (Q16) moves it from the zero
 * crossing (1) towards the waveform's start (0) */
static void osc_reset_phase(osc_t *o, uint32_t offset, int drum)
{
    if (o->wave == W_SINE)
        o->phase = drum ? 1024u + (uint32_t)(((uint64_t)((0x3FFu << 20) - 1024u) * offset) >> 16)
                        : (uint32_t)(((uint64_t)(0x3FFu << 20) * offset) >> 16);
    else if (o->wave > W_SINE && o->wave <= W_REC)
        o->phase = (uint32_t)(((uint64_t)(0xFFu << 22) * offset) >> 16);
    else
        o->phase = 0;
}

/* -------------------------------------------------- transient one-shot --- */
enum { TR_SNAP, TR_OFFSET };    /* 2..13: the samples */
typedef struct {
    uint32_t phase, inc;         /* 20-bit fraction: inc 1 << 20 = the recorded pitch */
    int32_t vol;                 /* Q15 */
    uint8_t wave;
} trans_t;
#define TR_END (2204u << 20)

static void trans_block(trans_t *t, int16_t *buf)   /* transient_calcBlock */
{
    uint32_t i;
    const int8_t *d;
    if (t->wave <= TR_OFFSET || t->phase >= TR_END) {
        for (i = 0; i < BLOCK; i++)
            buf[i] = 0;
        return;
    }
    d = LXR_TRANS[t->wave - 2u];
    for (i = 0; i < BLOCK; i++) {
        if (t->phase < TR_END) {
            buf[i] = (int16_t)((d[t->phase >> 20] * 256 * t->vol) >> 15);
            t->phase += t->inc;
        } else {
            buf[i] = 0;
        }
    }
}

/* snapEg.c: value 1 at the trigger, falls 0.2 x the transient's pitch a block; out = value^2 x 24 (Q16) */
static int32_t snap_calc(int32_t *v, uint32_t pitch_q20)
{
    int32_t r = 0;
    if (*v > 0) {
        r = (int32_t)(((int64_t)*v * *v * 24) >> 16);
        *v -= (int32_t)(((uint64_t)13107u * pitch_q20) >> 20);       /* 0.2 x pitch, Q16 */
    }
    return r;
}

/* ------------------------------------------------------- envelopes --- */
#define EG_ONE (1 << 24)
enum { EG_STOPPED, EG_A, EG_D, EG_REPEAT };
typedef struct {
    int32_t value;               /* Q24 */
    uint32_t atk, dec;           /* steps a block, Q24 */
    int32_t k, ik;               /* slope and the attack's inverse slope, Q16 */
    uint8_t repeat, rcnt, state;
} slope_eg_t;
typedef struct {
    int32_t value;               /* Q24 */
    uint32_t dec;
    int32_t k;
} decay_eg_t;

/* (1 + k) v / (1 + k |v|), v Q24, k Q16 -> Q24 (SlopeEg2 / DecayEg calcSlopeValue) */
static int32_t eg_shape(int32_t v, int32_t k)
{
    int64_t num = (int64_t)(65536 + (int64_t)k) * v;
    int32_t den = 65536 + (int32_t)(((int64_t)k * (v < 0 ? -v : v)) >> 24);   /* (fits: a 32-bit divisor, the
                                                                                 * pi32v2's native 64 / 32 divide) */
    if (den <= 0)
        return v;
    return (int32_t)(num / den);
}

static void slope_trigger(slope_eg_t *e)            /* slopeEg2_trigger */
{
    if (!e->repeat) {
        e->state = EG_A;
    } else {
        e->state = EG_REPEAT;
        e->value = EG_ONE;
    }
    e->rcnt = e->repeat;
}
static int32_t slope_calc(slope_eg_t *e)            /* slopeEg2_calc, once a block */
{
    int32_t v = e->value;
    switch (e->state) {
    case EG_REPEAT:
        if (v > 0) {
            v -= (int32_t)e->atk;
        } else {
            e->rcnt--;
            if (e->rcnt == 0)
                e->state = EG_D;
            v = EG_ONE;
        }
        e->value = v;
        return eg_shape(v, e->k);
    case EG_A:
        v += (int32_t)e->atk;
        if (v >= EG_ONE) {
            v = EG_ONE;
            e->state = EG_D;
        }
        e->value = v;
        return eg_shape(v, e->ik);
    case EG_D:
        if (v > 0) {
            v -= (int32_t)e->dec;
            e->value = v;
            return eg_shape(v, e->k);
        }
        e->value = 0;
        return 0;
    default:
        return 0;
    }
}
static int32_t decay_calc(decay_eg_t *e)            /* DecayEg_calc */
{
    int32_t v = e->value;
    v = v > 0 ? v - (int32_t)e->dec : 0;
    e->value = v;
    return eg_shape(v, e->k);
}

/* ---------------------------------------------------------- filter --- */
enum { F_LP = 1, F_HP, F_BP, F_UBP, F_NOTCH, F_PEAK, F_LP2, F_OFF };
typedef struct {
    int32_t s1, s2, a, b;        /* states, Q16 */
    uint32_t fval;               /* the cutoff setting 0..1, Q16 */
    int32_t f, g, q, drive;      /* f, g, q Q16; drive Q12 */
    int32_t R;                   /* the damping in use (1 at the top of the range) */
    /* the block's coefficients (svf_coef) */
    int32_t cx, cs1, cs2, g0, gg0, g2, r2, lp2f, lp2q;
    uint8_t dirty;
} svf_t;

#define SC_MAX (32 << 16)
static inline int32_t softclip(int32_t x)            /* softClipTwo, x Q16 */
{
    int32_t a = x < 0 ? -x : x, r;
    if (a >= SC_MAX) {
        r = SOFTCLIP[1024] + (a - SC_MAX) / 15;      /* (the Pade form grows as x / 15 out there) */
    } else {
        uint32_t i = (uint32_t)a >> 11, f = (uint32_t)a & 2047u;
        r = SOFTCLIP[i] + (int32_t)(((int64_t)(SOFTCLIP[i + 1u] - SOFTCLIP[i]) * (int32_t)f) >> 11);
    }
    return x < 0 ? -r : r;
}
static inline int32_t fast_tanh(int32_t x)           /* ResonantFilter.c fastTanh, Q16 */
{
    int32_t a = x < 0 ? -x : x, r;
    if (a >= (2 << 16)) {
        r = 65536;
    } else {
        uint32_t i = (uint32_t)a >> 8, f = (uint32_t)a & 255u;
        r = FASTTANH[i] + (((FASTTANH[i + 1u] - FASTTANH[i]) * (int32_t)f) >> 8);
    }
    return x < 0 ? -r : r;
}
static inline int32_t q16_to_s16(int32_t v)          /* x 0x7FFF, saturated (FILTER_GAIN, __SSAT) */
{
    if (v >= 65537)
        return 32767;
    if (v <= -65537)
        return -32768;
    return sat16((v * 32767) >> 16);
}

static void svf_init(svf_t *s)                       /* SVF_init */
{
    s->s1 = s->s2 = s->a = s->b = 0;
    s->q = (int32_t)(0.9 * 65536);
    s->drive = (int32_t)(0.315 * 4096);
    s->fval = 0x4000;                                /* SVF_directSetFilterValue(0.25) */
    s->dirty = 1;
}
static void svf_reset(svf_t *s) { s->s1 = s->s2 = s->a = s->b = 0; }

static void svf_coef(svf_t *s)                       /* SVF_recalcFreq + the per-block terms of SVF_calcBlockZDF */
{
    int64_t g, g0, f1, d;
    s->f = lut1024(FILT_F, s->fval);
    s->g = lut1024(FILT_G, s->fval);
    s->R = s->f >= 29485 ? 65536 : s->q;             /* f >= 0.4499: the LXR fixes R at 1 (unstable otherwise) */
    g = s->g;
    g0 = (int64_t)(((uint64_t)1 << 32) / (uint32_t)(65536 + ((2 * (int64_t)s->R * g) >> 16)));
    f1 = (((g * g) >> 16) * g0) >> 16;
    d = (int64_t)(((uint64_t)1 << 32) / (uint32_t)(65536 + f1));
    s->cx = (int32_t)((f1 * d) >> 16);
    s->cs2 = (int32_t)d;
    s->gg0 = (int32_t)((g * g0) >> 16);
    s->cs1 = (int32_t)(((int64_t)s->gg0 * d) >> 16);
    s->g0 = (int32_t)g0;
    s->g2 = (int32_t)(2 * g);
    s->r2 = 2 * s->R;
    {                                                /* the naive 2-pole LP2: f x 2.21 and its feedback */
        int64_t fl = ((int64_t)s->f * 144835) >> 16;
        int64_t oq = 65536 - s->q;
        if (fl > 65000)
            fl = 65000;
        s->lp2f = (int32_t)fl;
        s->lp2q = (int32_t)(((oq * 91750) >> 16) + (oq << 16) / (int32_t)(65536 - fl));
    }
    s->dirty = 0;
}

#define S_LIM (128 << 16)                        /* the states' rail (+-64 would read 0xFFC00000: build.py's ROM guard) */
static void svf_block(svf_t *s, uint32_t type, int16_t *buf)   /* SVF_calcBlockZDF */
{
    uint32_t i;
    if (type == F_OFF || type < F_LP || type > F_OFF)
        return;
    if (s->dirty)
        svf_coef(s);
    if (type == F_LP2) {
        int32_t a = s->a, b = s->b, f = s->lp2f, q = s->lp2q;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12);
            int64_t t = (int64_t)(x - a) + (((int64_t)q * (a - b)) >> 16);
            a += (int32_t)(((int64_t)f * t) >> 16);
            a = clamp32(a, -65536, 65536);
            b += (int32_t)(((int64_t)f * (a - b)) >> 16);
            b = clamp32(b, -65536, 65536);
            buf[i] = (int16_t)q16_to_s16(b);
        }
        s->a = a;
        s->b = b;
        return;
    }
    {
        int32_t s1 = s->s1, s2 = s->s2;
        const int32_t cx = s->cx, cs1 = s->cs1, cs2 = s->cs2, g0 = s->g0, gg0 = s->gg0, g2 = s->g2, r2 = s->r2;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12);
            int32_t y1 = (int32_t)(((int64_t)cx * x + (int64_t)cs2 * s2 + (int64_t)cs1 * s1) >> 16);
            int32_t xx = x - y1, sc = softclip(s1);
            int32_t y0 = (int32_t)(((int64_t)g0 * sc + (int64_t)gg0 * xx) >> 16);
            int32_t ugb = mulq16(r2, y0), o;
            s1 = sc + (int32_t)(((int64_t)g2 * (xx - ugb)) >> 16);
            s2 = s2 + (int32_t)(((int64_t)g2 * y0) >> 16);
            s1 = clamp32(s1, -S_LIM, S_LIM);
            s2 = clamp32(s2, -S_LIM, S_LIM);
            switch (type) {
            case F_LP:
                o = fast_tanh(y1);
                break;
            case F_HP:
                o = x - ugb - y1;
                break;
            case F_BP:
                o = y0;
                break;
            case F_UBP:
                o = ugb;
                break;
            case F_NOTCH:
                o = x - ugb;
                break;
            default:                                 /* F_PEAK: y1 - hp */
                o = y1 - (x - ugb - y1);
                break;
            }
            buf[i] = (int16_t)q16_to_s16(o);
        }
        s->s1 = s1;
        s->s2 = s2;
    }
}

/* ------------------------------------------------------- waveshaper --- */
/* distortion.c: y = (1 + k) x / (1 + k |x|), k = 2 s / (1 - s), s = the drive - 1/128. k Q8 */
static int32_t dist_k(uint32_t q16)
{
    int32_t s = (int32_t)q16 - 512;                  /* - 1/128 (Q16) */
    int32_t den = 65536 - s;
    if (den < 64)
        den = 64;
    return (2 * s << 8) / den;                      /* |2 s 256| < 2^25: 32 bits */
}
static inline int32_t dist_sample(int32_t x, int32_t k8)
{
    int32_t den = 256 + ((k8 * iabs(x)) >> 15);     /* (k8 -4 .. 65024, |x| <= 32768: fits) */
    if (den <= 0)
        return x;
    return sat16(((256 + k8) * x) / den);
}
static void dist_block(int16_t *buf, int32_t k8)     /* calcDistBlock */
{
    uint32_t i;
    for (i = 0; i < BLOCK; i++)
        buf[i] = (int16_t)dist_sample(buf[i], k8);
}
