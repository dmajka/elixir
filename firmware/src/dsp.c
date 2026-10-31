/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the LXR-02's DSP building blocks in fixed point (the FM-1 has no FPU).
 * Ported from the LXR firmware, Core/Src/Soundengine/DSPAudio (Oscillator.c, transientGenerator.c, SlopeEg2.c,
 * Decay.c, snapEg.c, ResonantFilter.c, distortion.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only.
 *
 * Number formats: audio int16 (the LXR's own); gains Q15 (32768 = 1); envelope values Q24; filter states and
 * coefficients Q16 (65536 = 1.0 = a full-scale sample). Each block is BLOCK (64) samples: the LXR computes
 * envelopes, LFOs and coefficients once a block and the oscillators, filters and shapers per sample, and so does
 * this port. Where the LXR's float code relies on a hardware quirk (a negative float converted to unsigned is 0 on
 * the Cortex-M7), the port does the same so the voices sound as they do there. */

/* 17a: the load by part (audio.c, CONFIG > LOAD). LOAD_MARK(p) ends part p: the time since the last mark is its.
 * Only on the FM-1 (elixir.c: LOAD_CLOCK, TIMER4 less TIMER5's nested ticks); the host and the emulator count none.
 * 17c: with two cores LD_D13 is the second core's time (voices.c), and LD_WAIT core 0's wait for it */
enum { LD_SEQ, LD_D13, LD_SNHH, LD_MIX, LD_FX, LD_REV, LD_WAIT, NLD };
#ifdef LOAD_CLOCK
static uint32_t ld_t, ld_acc[NLD];
#define LOAD_MARK(p) do { uint32_t n_ = LOAD_CLOCK(); \
                             ld_acc[p] += (int32_t)(n_ - ld_t) > 0 ? n_ - ld_t : 0u; ld_t = n_; } while (0)
#else
#define LOAD_MARK(p) ((void)0)
#endif

static inline int32_t sat16(int32_t v){ return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
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
/* the LXR's hardware RNG: here xorshift32. noise_next for the sequencer and the LFOs (core 0, before the voices
 * render); 0.15: each oscillator and each model has its own state, so a voice's noise doesn't depend on which voices
 * rendered before it or on which core (17c: D1-D3 render on the second core) */
static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static uint32_t noise_rng = 0x9E3779B9u;
static inline uint32_t noise_next(void) { return xs32(&noise_rng); }

/* -------------------------------------------------------- oscillator --- */
enum { W_SINE, W_TRI, W_SAW, W_REC, W_NOISE, W_PWM, NWAVE };
#define SEQ_DEFAULT_NOTE 60

typedef struct {
    uint32_t phase, inc;
    uint32_t note_inc;           /* the played note with its fine tune (osc_recalcFreq's freq), before pitchMod */
    uint16_t midi_freq;          /* coarse << 8 | fine (0..127 each) */
    uint8_t wave, tab, base_note;
    int16_t out;                 /* noise: the value held until the phase wraps */
    uint32_t rng;                /* noise: its own generator (voices_init seeds it) */
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

/* the table's row (34: tables 0..3 aren't kept; a voice before its first osc_set_freq plays table 4) */
static inline uint32_t osc_lv(const osc_t *o) { return o->tab > LXR_TAB0 ? o->tab - LXR_TAB0 : 0u; }
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
        const int16_t *t = WT[o->wave] + osc_lv(o) * 1024u;
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
        const int16_t *t = LXR_SAW[osc_lv(o)];
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
                out = (int16_t)xs32(&o->rng);
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
enum { EG_STOPPED, EG_A, EG_D, EG_REPEAT, EG_H };
typedef struct {
    int32_t value;               /* Q24 */
    uint32_t atk, dec;           /* steps a block, Q24 */
    int32_t k, ik;               /* slope and the attack's inverse slope, Q16 */
    uint8_t repeat, rcnt, state;
    uint8_t hold;                /* 19b: GATE: the attack's top held (EG_H) until the note's end lets go */
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
                e->state = e->hold ? EG_H : EG_D;
            v = EG_ONE;
        }
        e->value = v;
        return eg_shape(v, e->k);
    case EG_A:
        v += (int32_t)e->atk;
        if (v >= EG_ONE) {
            v = EG_ONE;
            e->state = e->hold ? EG_H : EG_D;
        }
        e->value = v;
        return eg_shape(v, e->ik);
    case EG_H:
        return eg_shape(e->value, e->k);
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
/* 28: Elixir's types after the LXR's OFF (a kit's value: the type - 1; the LXR's 0..7 keep their meaning) */
enum { F_LP = 1, F_HP, F_BP, F_UBP, F_NOTCH, F_PEAK, F_LP2, F_OFF, F_LP4, F_COMBP, F_COMBN, F_APF, F_VOWEL, F_DUB,
       F_RESO, F_LAST = F_RESO };
#define FLT_LINE 512u            /* 28: COMB's line per voice (voices.c flt_line, .ram_buf): down to FS / 512, 86 Hz */
typedef struct {
    int32_t s1, s2, a, b;        /* states, Q16 */
    uint32_t fval;               /* the cutoff setting 0..1, Q16 */
    int32_t f, g, q, drive;      /* f, g, q Q16; drive Q12 */
    int32_t R;                   /* the damping in use (1 at the top of the range) */
    /* the block's coefficients (svf_coef) */
    int32_t cx, cs1, cs2, g0, gg0, g2, r2, lp2f, lp2q;
    /* 28: Elixir's types. e: LP4's and ALLPASS's four stages, VOWEL's two and RESONATOR's three band-passes (lp, bp
     * each), COMB's loop filter; fb: the last output fed back. k1..k4: their coefficients (svf_coef) */
    int32_t e[6], fb, k1, k2, k3, k4;
    int16_t *line;               /* COMB's line (none: the DJ filter's, which never runs one) */
    uint16_t wp;
    int8_t key;                  /* the played note - 60: COMB and RESONATOR follow it */
    uint8_t was, clr;            /* the type the states are for; clear them at the next block (a hit, a switch) */
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

#define S_LIM (128 << 16)                        /* the states' rail (+-64 would read 0xFFC00000: build.py's ROM guard) */

/* ------------------------------------------- 28: Elixir's filter types --- */
/* After the LXR's eight, on the same knobs: CUTOFF, RESONANCE and DRIVE (the input's soft clip, as theirs).
 *   LP4       four one-poles (zero-delay, g / (1 + g)) with tanh on the input and the 4th fed back (the loop solved
 *             without a sample's delay: stable at any cutoff): the ladder's squelch, ringing on the edge of
 *             oscillation at the top of RESONANCE. The pass band made up by 1 + k / 2, into a tanh
 *   COMB+     a comb at the cutoff's pitch: 86 Hz (CUTOFF 0) up 5 octaves, and the played note moves it (it rings in
 *   COMB-     the scale). RESONANCE is the feedback (0..0.93), through a one-pole that softens the ringing. COMB+
 *             rings at the pitch's harmonics, COMB- (negative feedback) at the odd ones of the octave below: hollow
 *   ALLPASS   four first-order allpasses at the cutoff with RESONANCE as feedback, mixed with the dry: a phaser's
 *             two notches, standing still until an LFO moves the cutoff
 *   VOWEL     two band-passes at a vowel's first two formants; CUTOFF sweeps A E I O U, RESONANCE is their Q
 *   DUB BP    the LXR's BP into a tanh driven by DRIVE: the ring of a dub chord stab
 *   RESONATOR three band-passes at the note, a fifth and an octave (the pitch as COMB's), RESONANCE their Q
 * VOWEL's and RESONATOR's band-passes are Chamberlin's (2 sin(w / 2), stable below ~7 kHz, where they're clamped) */
static int32_t exp2_q16(int32_t y);                  /* (fxbus.c) */
static inline int32_t mulr16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + 32768) >> 16); }   /* (the
                                                       * loops round: truncating left a DC and a limit cycle) */
static inline int32_t tanh20(int32_t x)              /* fast_tanh in Q20, in and out */
{
    int32_t a = x < 0 ? -x : x, r;
    if (a >= (2 << 20)) {
        r = 1 << 20;
    } else {
        uint32_t i = (uint32_t)a >> 12, f = (uint32_t)a & 4095u;
        r = (FASTTANH[i] << 4) + (((FASTTANH[i + 1u] - FASTTANH[i]) * (int32_t)f) >> 8);
    }
    return x < 0 ? -r : r;
}
static const int16_t VOWEL_F[5][2] = {{800, 1150}, {400, 1700}, {300, 2250}, {450, 800}, {330, 650}};   /* A E I O U */
static int32_t cham_coef(int32_t w)                  /* 2 sin(w / 2) ~ w - w^3 / 24, w Q16 rad/sample */
{
    if (w > 62000)                                   /* (~6.7 kHz) */
        w = 62000;
    return w - mulq16(mulq16(w, w), w) / 24;
}
static int32_t flt_octaves(const svf_t *s)           /* COMB's and RESONATOR's pitch over 86 Hz, Q16 octaves */
{
    return (int32_t)(((int64_t)s->fval * 5) + ((int32_t)s->key * 65536 / 12));
}
static void svf_coef(svf_t *s);
static void svf_coef28(svf_t *s)
{
    int32_t r = 65536 - s->q;                        /* RESONANCE 0 .. 0.98 */
    switch (s->was) {
    case F_LP4:
    case F_APF: {
        int32_t g4;
        s->k1 = (int32_t)(((uint64_t)(uint32_t)s->g << 16) / (uint32_t)(65536 + s->g));   /* G = g / (1 + g) */
        s->k2 = s->was == F_LP4 ? r * 4 - (r >> 4) : mulq16(r, 58982);   /* LP4: k ~3.9; ALLPASS: 0.9 */
        g4 = mulq16(mulq16(s->k1, s->k1), mulq16(s->k1, s->k1));
        s->k3 = g4;                                  /* LP4: G^4 and 1 / (1 + k G^4), the loop solved at once */
        s->k4 = (int32_t)(((uint64_t)1 << 32) / (uint32_t)(65536 + mulq16(s->k2, g4)));
        break;
    }
    case F_COMBP:
    case F_COMBN: {
        int32_t pd = exp2_q16(-flt_octaves(s)) << 9;     /* the period, Q16 samples: 512 at 86 Hz */
        int32_t fb = mulq16(r, 62259);               /* 0 .. 0.93 */
        s->k1 = clamp32(pd, 2 << 16, (int32_t)(FLT_LINE - 2u) << 16);
        s->k2 = s->was == F_COMBN ? -fb : fb;
        s->k3 = 65536 - mulq16(fb, 52429);           /* the output: 1 - 0.8 |fb| */
        break;
    }
    case F_VOWEL: {
        uint32_t x = s->fval * 4u, i = x >> 16, fr = x & 0xFFFFu;
        int32_t f1, f2;
        if (i >= 4u) {
            i = 3u;
            fr = 65536u;
        }
        f1 = VOWEL_F[i][0] + (int32_t)(((VOWEL_F[i + 1u][0] - VOWEL_F[i][0]) * (int32_t)fr) >> 16);
        f2 = VOWEL_F[i][1] + (int32_t)(((VOWEL_F[i + 1u][1] - VOWEL_F[i][1]) * (int32_t)fr) >> 16);
        s->k1 = cham_coef((int32_t)((uint32_t)f1 * 411775u / FS));   /* 2 pi f / FS, Q16 (2 pi = 411775) */
        s->k2 = cham_coef((int32_t)((uint32_t)f2 * 411775u / FS));
        s->k3 = 65536 - mulq16(r, 60293);            /* the damping: 1 .. 0.1 (Q 1 .. 10) */
        break;
    }
    case F_RESO: {
        int32_t w = (int32_t)(((int64_t)exp2_q16(flt_octaves(s)) * 804) >> 16);   /* 2 pi 86.13 / FS = 804 Q16 */
        s->k1 = cham_coef(w);
        s->k2 = cham_coef(w + (w >> 1));
        s->k3 = cham_coef(w * 2);
        s->k4 = 65536 - mulq16(r, 63570);            /* the damping: 1 .. 0.05 */
        break;
    }
    default:
        break;
    }
}
static inline int32_t cham_bp(int32_t *st, int32_t x, int32_t c, int32_t d)   /* st: lp, bp; returns the bp */
{
    int32_t lp = st[0] + mulr16(c, st[1]);
    int32_t hp = x - lp - mulr16(d, st[1]);
    int32_t bp = st[1] + mulr16(c, hp);
    st[0] = clamp32(lp, -S_LIM, S_LIM);
    st[1] = clamp32(bp, -S_LIM, S_LIM);
    return st[1];
}
static void svf_block28(svf_t *s, uint32_t type, int16_t *buf)
{
    uint32_t i;
    if (type != s->was || s->clr) {                  /* (a switch or a hit: from rest, the line silent) */
        memset(s->e, 0, sizeof s->e);
        s->fb = 0;
        if (s->line && (type == F_COMBP || type == F_COMBN))
            memset(s->line, 0, FLT_LINE * sizeof s->line[0]);
        s->clr = 0;
        if (type != s->was) {
            s->was = (uint8_t)type;
            s->dirty = 1;
        }
    }
    if (s->dirty)
        svf_coef(s);
    switch (type) {
    case F_LP4: {
        /* (Zavalishin's: each stage y = G u + (1 - G) s, so the 4th is G^4 u + S, S from the states alone; u = x - k y4
         * solved for this sample, then the tanh on u. The stages are bounded by the tanh's 1, and k < 4 rings out) */
        const int32_t G = s->k1, B = 65536 - s->k1, k = s->k2, g4 = s->k3, inv = s->k4, mk = 65536 + (k >> 1);
        int32_t *e = s->e;                           /* (Q20 inside: in Q16 the rounding rang on as a limit cycle) */
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12) << 4, u, j;
            int32_t S = mulr16(B, mulr16(G, mulr16(G, mulr16(G, e[0]) + e[1]) + e[2]) + e[3]);
            int32_t y4 = mulr16(mulr16(g4, x) + S, inv);
            u = tanh20(x - mulr16(k, y4));
            for (j = 0; j < 4; j++) {                /* (TPT one-poles) */
                int32_t v = mulr16(u - e[j], G);
                u = v + e[j];
                e[j] = u + v;
            }
            buf[i] = (int16_t)q16_to_s16(fast_tanh(mulr16((u + 8) >> 4, mk)));
        }
        break;
    }
    case F_APF: {
        const int32_t G = s->k1, fbk = s->k2;
        int32_t *e = s->e, y = s->fb;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12), u = clamp32(x + mulr16(fbk, y), -S_LIM, S_LIM), j;
            for (j = 0; j < 4; j++) {
                int32_t v = mulr16(u - e[j], G), lp = v + e[j];
                e[j] = clamp32(lp + v, -S_LIM, S_LIM);
                u = 2 * lp - u;
            }
            y = u;
            buf[i] = (int16_t)q16_to_s16((x + y) >> 1);
        }
        s->fb = y;
        break;
    }
    case F_COMBP:
    case F_COMBN: {
        const int32_t pd = s->k1, fbk = s->k2, og = s->k3;
        const uint32_t pi = (uint32_t)pd >> 16, pf = (uint32_t)pd & 0xFFFFu;
        int16_t *ln = s->line;
        uint32_t wp = s->wp;
        int32_t lp = s->e[0];
        if (!ln)
            return;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12), w;
            uint32_t r0 = (wp - pi) & (FLT_LINE - 1u), r1 = (r0 - 1u) & (FLT_LINE - 1u);
            int32_t d = (ln[r0] + (((ln[r1] - ln[r0]) * (int32_t)(pf >> 2)) >> 14)) << 3;   /* (Q13 -> Q16) */
            lp += mulr16(d - lp, 45875);             /* (the loop's one-pole, 0.7) */
            w = clamp32(x + mulr16(fbk, lp), -(4 << 16) + 8, (4 << 16) - 8);
            ln[wp] = (int16_t)(w >> 3);
            wp = (wp + 1u) & (FLT_LINE - 1u);
            buf[i] = (int16_t)q16_to_s16(fast_tanh(mulr16(w, og)));
        }
        s->wp = (uint16_t)wp;
        s->e[0] = lp;
        break;
    }
    case F_VOWEL: {
        const int32_t c1 = s->k1, c2 = s->k2, d = s->k3, og = (65536 + d) >> 1;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12);
            int32_t y = cham_bp(&s->e[0], x, c1, d) + mulr16(cham_bp(&s->e[2], x, c2, d), 39322);   /* (F2 x 0.6) */
            buf[i] = (int16_t)q16_to_s16(fast_tanh(mulr16(y, og)));
        }
        break;
    }
    case F_RESO: {
        const int32_t c1 = s->k1, c2 = s->k2, c3 = s->k3, d = s->k4, og = d + 8192;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12);
            int32_t y = cham_bp(&s->e[0], x, c1, d) + mulr16(cham_bp(&s->e[2], x, c2, d), 45875) +
                        mulr16(cham_bp(&s->e[4], x, c3, d), 39322);   /* (the root, the fifth x 0.7, the octave x 0.6) */
            buf[i] = (int16_t)q16_to_s16(fast_tanh(mulr16(y, og)));
        }
        break;
    }
    default:
        break;
    }
}

static void svf_init(svf_t *s)                       /* SVF_init */
{
    s->s1 = s->s2 = s->a = s->b = 0;
    s->q = (int32_t)(0.9 * 65536);
    s->drive = (int32_t)(0.315 * 4096);
    s->fval = 0x4000;                                /* SVF_directSetFilterValue(0.25) */
    s->dirty = 1;
}
/* (28: Elixir's states and COMB's line at the voice's next block, on the core that renders it) */
static void svf_reset(svf_t *s) { s->s1 = s->s2 = s->a = s->b = 0; s->clr = 1; }

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
    if (s->was >= F_LP4)
        svf_coef28(s);
    s->dirty = 0;
}

static void svf_block(svf_t *s, uint32_t type, int16_t *buf)   /* SVF_calcBlockZDF */
{
    uint32_t i;
    if (type > F_OFF && type <= F_LAST && type != F_DUB) {
        svf_block28(s, type, buf);
        return;
    }
    if (type == F_OFF || type < F_LP || type > F_LAST)
        return;
    if (s->was || s->clr) {                          /* (28: back from one of Elixir's types: those states go) */
        s->was = 0;
        s->clr = 0;
    }
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
            int32_t ugb = mulr16(r2, y0), o;
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
            case F_DUB:                              /* (28: the BP into a tanh, 2 .. 14.8 x by DRIVE; below 24 (-69 */
                o = y0 - clamp32(y0, -24, 24);       /* .. dB) left out: the BP's own rounding hum, made loud) */
                o = fast_tanh((int32_t)(((int64_t)o * (8192 + 2 * s->drive)) >> 12));
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
