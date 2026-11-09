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
/* 1.0 (diagnostic, ELIXIR_LDDIAG=1 builds): SEQ, MOD split into its parts on the LOAD page, in place of the stacks:
 * snd_service, MIDI + keys + PERF, the sequencer, the voices' modulation before core 1 starts */
#ifndef ELIXIR_LDDIAG
#define ELIXIR_LDDIAG 0
#endif
#if defined(LOAD_CLOCK) && ELIXIR_LDDIAG
enum { DG_SND, DG_IN, DG_SEQ, DG_PRE, NDG };
static uint32_t dg_t, dg_acc[NDG];
#define DG_START() (dg_t = LOAD_CLOCK())
#define DG_MARK(p) do { uint32_t n_ = LOAD_CLOCK(); \
                        dg_acc[p] += (int32_t)(n_ - dg_t) > 0 ? n_ - dg_t : 0u; dg_t = n_; } while (0)
#else
#define DG_START() ((void)0)
#define DG_MARK(p) ((void)0)
#endif

static inline int32_t sat16(int32_t v){ return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static inline int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* 1.0 (Phase 10d): clamp32 for lo <= hi, the top first: two conditional moves, where clamp32's order costs a third */
static inline int32_t clamp_hl(int32_t v, int32_t lo, int32_t hi) { v = v > hi ? hi : v; return v < lo ? lo : v; }
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
#include "rings_tables.h"                       /* (1.0: the one sine table, RG_SINE) */
static inline int32_t sine_at(uint32_t ph)                      /* 4096-point table, 20-bit fraction */
{
    uint32_t i = ph >> 20, f = (ph >> 5) & 0x7FFFu;
    int32_t a, b;
    i = (i + 3072u) & 4095u;                         /* (1.0: RG_SINE, sin, a 3/4 cycle on: the LXR's -cos) */
    a = RG_SINE[i];
    b = RG_SINE[i + 1u];
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
       F_RESO, F_ACID, F_MS20L, F_MS20H, F_LADDER, F_LAST = F_LADDER };   /* (1.0: the character filters, ACID ..) */
static inline int flt_char(uint32_t t) { return t >= F_ACID && t <= F_LADDER; }
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
    uint8_t cdirty;              /* 1.0: the character types' coefficients are old (ch_block) */
    /* 1.0: the character types (ACID ..). eg: the voice's amp EG this block (voices.c), Q16; xamt: FILTER X (ENV AMT,
     * LADDER's BASS COMP), Q16; rk: RESONANCE as turned (0..1, Q16); fe: the cutoff ck is for (-1: none yet) */
    int32_t eg, xamt, rk, fe;
    int32_t ck[18];
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
/* (1.0 Phase 10d: fast_tanh and tanh20 clamp the table's index where they branched past 2: the same to the bit, the
 * table 1 from 1.95 (FASTTANH[500 ..]), and a loop with one in it stays one block, where the compiler keeps its
 * registers: ACID's loop -15%) */
static inline int32_t fast_tanh(int32_t x)           /* ResonantFilter.c fastTanh, Q16 */
{
    int32_t a = x < 0 ? -x : x, r;
    uint32_t i, f;
    a = a > (2 << 16) - 1 ? (2 << 16) - 1 : a;       /* (FASTTANH[511 ..]: 1) */
    i = (uint32_t)a >> 8;
    f = (uint32_t)a & 255u;
    r = FASTTANH[i] + (((FASTTANH[i + 1u] - FASTTANH[i]) * (int32_t)f) >> 8);
    return x < 0 ? -r : r;
}
static inline int32_t q16_to_s16(int32_t v)          /* x 0x7FFF, saturated (FILTER_GAIN, __SSAT) */
{
    v = clamp_hl(v, -65537, 65537);                  /* (1.0 Phase 10d: no branches, the same to the bit: +-65537
                                                      * give 32767 and -32768) */
    return (v * 32767) >> 16;
}
/* 1.0 (Phase 10d): q16_to_s16(fast_tanh(x)), the same to the bit: the tanh is within +-1, no saturation to test */
static inline int32_t tanh_s16(int32_t x) { return (fast_tanh(x) * 32767) >> 16; }
/* .. and the DJ filter's input (perform.c): softclip((v * 2 * drive) >> 12) at svf_init's drive, which the DJ filter
 * keeps. Its |x| < 0.32 there, where softClipTwo (2 tanh(x / 2)) is x - x^3 / 12 within 2 LSB (the table's own
 * error: 2.4): no table, no branches */
#define DJ_DRIVE2 2580                               /* (2 x (int)(0.315 x 4096)) */
static inline int32_t dj_in(int32_t v)
{
    int32_t x = (v * DJ_DRIVE2) >> 12, t = (x * x) >> 16;
    t = (t * 5461) >> 16;                            /* (x^2 / 12) */
    return x - ((t * x) >> 16);
}
static inline int32_t dj_mix(int32_t x, int32_t o, int32_t w) { return sat16(x + (((o - x) * w) >> 15)); }   /* (the
                                                      * DJ filter's fade in over the knob's first eighth) */

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
    uint32_t i, f;
    a = a > (2 << 20) - 1 ? (2 << 20) - 1 : a;
    i = (uint32_t)a >> 12;
    f = (uint32_t)a & 4095u;
    r = (FASTTANH[i] << 4) + (((FASTTANH[i + 1u] - FASTTANH[i]) * (int32_t)f) >> 8);
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
    case F_ACID:
    case F_MS20L:
    case F_MS20H:
    case F_LADDER:
        s->cdirty = 1;                               /* (ch_block: at the cutoff the envelope gives) */
        break;
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
/* ------------------------------------------ 1.0: the character filters --- */
/* After CHORD, on the same knobs (CUTOFF, RESONANCE, DRIVE: the input's soft clip) and FILTER X:
 *   ACID     the 303's diode ladder: four stages each coupled to the next (y' = wc (y[j-1] - 2 y[j] + y[j+1]), the
 *            last y' = wc (y3 - y4)), the loop solved at once (the stages by Thomas' algorithm, zero-delay: stable at
 *            any cutoff), the 4th fed back through a ~60 Hz high-pass (the resonance thins at low cutoffs, as the
 *            303's does) into a tanh. ~17 dB an octave; it whistles near k = 18.4 (never past: it rings out). Its g
 *            is the cutoff's x 4: down 3 dB at half the cutoff at RESONANCE 0, the peak ~2 octaves over it.
 *            FILTER X: ENV AMT
 *   MS20 LP  two one-poles, the band between them fed back (k to 2) through an asymmetric hard-knee clip (knees +0.6,
 *   MS20 HP  -0.45, a quarter's slope past them): rougher, not louder, as RESONANCE rises. HP: the loop's input through
 *            two high-passes, (1 - a)^2 = u - 2 y1 + y2 (12 dB an octave). FILTER X: ENV AMT
 *   LADDER   LP4's loop with the 1st stage saturated too (y - y^3 / 3) and BASS COMP (FILTER X): u = tanh(x - k (y4 - c x))
 * RESONANCE on a curve (1 - (1 - r)^2: more of the knob up where it sings). ENV AMT opens the cutoff by the voice's amp
 * EG (cutoff + amount x EG, up to the whole range); while it moves, the coefficients are new every 16 samples, the
 * cutoff ramped across the block (no steps on a fast decay). States Q20, as LP4's (ACID's Q24) */
#define CH_SUB 16u
#define A_LIM (16 << 24)                             /* (ACID's states, Q24: at low cutoffs Q20's rounding held a DC) */
static inline int32_t ch_reso(const svf_t *s) { int32_t a = 65536 - s->rk; return 65536 - mulq16(a, a); }
static inline int32_t mulr20(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 19)) >> 20); }
#define ACID_P(m, j) ((m) * 4 - (m) * ((m) - 1) / 2 + (j) - (m))   /* (P's upper triangle, m <= j: 0..9) */
enum { ACID_A = 10, ACID_LINV = 14, ACID_KL, ACID_MK };
static void ch_coef(svf_t *s, uint32_t type, uint32_t fe)
{
    int32_t g = lut1024(FILT_G, fe), r = ch_reso(s), *c = s->ck;
    switch (type) {
    case F_ACID: {                                   /* ck: P (0..9), A (10..13), linv, kk linv, mk (ACID_*) */
        int32_t k = r * 18 + (r >> 1), j, m, den, inv[4], ee[3], a[4], kk;   /* (k to 18.5: just under the whistle;
                                                      * the high-pass keeps it further under at low cutoffs) */
        int64_t y[4];
        g *= 4;                                      /* (two octaves up: at RESONANCE 0 the ladder is down 3 dB at an
                                                      * eighth of its g's; this way at half the cutoff, open at the top,
                                                      * the peak ~2 octaves above) */
        for (j = 0; j < 4; j++) {                    /* (I - g M): 1 + 2g (1 + g the last), -g beside: Thomas' */
            den = (j < 3 ? 65536 + 2 * g : 65536 + g) - (j ? mulq16(g, ee[j - 1]) : 0);
            inv[j] = (int32_t)(((uint64_t)1 << 32) / (uint32_t)den);
            if (j < 3)
                ee[j] = mulq16(g, inv[j]);
        }
        a[0] = mulq16(g, inv[0]);                    /* (A: the stages' share of u, its forward pass then back, Q16:
                                                      * the loop's gain, linv, as 0.23 had it) */
        for (j = 1; j < 4; j++)
            a[j] = mulq16(mulq16(g, a[j - 1]), inv[j]);
        for (j = 2; j >= 0; j--)
            a[j] += mulq16(ee[j], a[j + 1]);
        /* 1.0 (Phase 10d): the stages solved as sums, y = P e + A u, Q28: P's columns are the forward and back
         * passes on each state alone (the same coefficients), P symmetric as I - g M is.
         * The loop then is one multiply-add a term, one rounding a stage, where the passes were 14 rounded
         * products in a chain (-40% a sample on the device) */
        for (m = 0; m < 4; m++) {
            for (j = 0; j < 4; j++) {
                int64_t t = j == m ? (int64_t)1 << 28 : 0;
                if (j)
                    t += (y[j - 1] * g + 32768) >> 16;
                y[j] = (t * inv[j] + 32768) >> 16;
            }
            for (j = 2; j >= 0; j--)
                y[j] += (y[j + 1] * ee[j] + 32768) >> 16;
            for (j = m; j < 4; j++)
                c[ACID_P(m, j)] = (int32_t)y[j];
        }
        for (j = 0; j < 4; j++)                      /* (A as 0.23's Q16, its rounding kept: the same sound) */
            c[ACID_A + j] = a[j] << 12;
        kk = k - mulq16(k, 280);                     /* (k (1 - Gh), Gh = 280: ~60 Hz) */
        c[ACID_LINV] = (int32_t)(((uint64_t)1 << 32) / (uint32_t)(65536 + mulq16(kk, a[3])));
        c[ACID_KL] = mulr16(kk, c[ACID_LINV]);
        c[ACID_MK] = 65536 + k / 2;                  /* (the makeup, as LP4's: the peak drives the tanh) */
        break;
    }
    case F_MS20L:
    case F_MS20H: {                                  /* ck: G, 1 - G, k, linv */
        int32_t G = (int32_t)(((uint64_t)(uint32_t)g << 16) / (uint32_t)(65536 + g)), k = mulq16(r, 128450);   /* (k to 1.96) */
        c[0] = G;
        c[1] = 65536 - G;
        c[2] = k;
        c[3] = (int32_t)(((uint64_t)1 << 32) / (uint32_t)(65536 - mulq16(k, mulq16(G, c[1]))));
        break;
    }
    default: {                                       /* LADDER. ck: G, 1 - G, k, G^4, inv, 1 + k c, mk */
        int32_t G = (int32_t)(((uint64_t)(uint32_t)g << 16) / (uint32_t)(65536 + g)), k = r * 4 - (r >> 4), g4;
        g4 = mulq16(mulq16(G, G), mulq16(G, G));
        c[0] = G;
        c[1] = 65536 - G;
        c[2] = k;
        c[3] = g4;
        c[4] = (int32_t)(((uint64_t)1 << 32) / (uint32_t)(65536 + mulq16(k, g4)));
        c[5] = 65536 + mulq16(k, s->xamt);
        c[6] = 65536 + mulq16(k >> 1, 65536 - (s->xamt >> 1));   /* (the makeup: LP4's, half of it gone at full comp) */
        break;
    }
    }
}
static inline int32_t ms20_clip(int32_t x)           /* Q20: the knees +0.6, -0.45, a quarter's slope past them */
{
    if (x > 629146)
        return 629146 + ((x - 629146) >> 2);
    if (x < -471859)
        return -471859 + ((x + 471859) >> 2);
    return x;
}
/* (Phase 10d: the loops below are inlined for the voices, dj 0: the input through DRIVE's softclip, the output
 * stored; and for the DJ filter, dj 1: dj_in, the output faded in by w) */
#define FLT_IN(s, v, dj) ((dj) ? dj_in(v) : softclip(((v) * 2 * (s)->drive) >> 12))
#define FLT_OUT(x, o, dj, w) ((int16_t)((dj) ? dj_mix(x, o, w) : (o)))
static inline __attribute__((always_inline)) void ch_acid(svf_t *s, int16_t *buf, uint32_t n, const int dj, int32_t w)
{
    const int32_t *c = s->ck, linv = c[ACID_LINV], kl = c[ACID_KL], mk = c[ACID_MK];
    int32_t *e = s->e, e0 = e[0], e1 = e[1], e2 = e[2], e3 = e[3], e4 = e[4], e5 = e[5];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = FLT_IN(s, buf[i], dj) << 8, d3, u, y0, y1, y2, y3, v;
        int64_t a3 = (int64_t)c[ACID_P(0, 3)] * e0 + (int64_t)c[ACID_P(1, 3)] * e1 + (int64_t)c[ACID_P(2, 3)] * e2 +
                     (int64_t)c[ACID_P(3, 3)] * e3 + (1 << 27);   /* (the 4th stage from the states alone, Q52) */
        d3 = (int32_t)(a3 >> 28);
        u = tanh20((int32_t)(((int64_t)x * linv + (int64_t)(e4 - d3) * kl + (1 << 19)) >> 20)) << 4;
        y0 = (int32_t)(((int64_t)c[ACID_P(0, 0)] * e0 + (int64_t)c[ACID_P(0, 1)] * e1 + (int64_t)c[ACID_P(0, 2)] * e2 +
                        (int64_t)c[ACID_P(0, 3)] * e3 + (int64_t)c[ACID_A] * u + (1 << 27)) >> 28);
        y1 = (int32_t)(((int64_t)c[ACID_P(0, 1)] * e0 + (int64_t)c[ACID_P(1, 1)] * e1 + (int64_t)c[ACID_P(1, 2)] * e2 +
                        (int64_t)c[ACID_P(1, 3)] * e3 + (int64_t)c[ACID_A + 1] * u + (1 << 27)) >> 28);
        y2 = (int32_t)(((int64_t)c[ACID_P(0, 2)] * e0 + (int64_t)c[ACID_P(1, 2)] * e1 + (int64_t)c[ACID_P(2, 2)] * e2 +
                        (int64_t)c[ACID_P(2, 3)] * e3 + (int64_t)c[ACID_A + 2] * u + (1 << 27)) >> 28);
        y3 = (int32_t)((a3 + (int64_t)c[ACID_A + 3] * u) >> 28);
        e0 = clamp_hl(2 * y0 - e0, -A_LIM, A_LIM);
        e1 = clamp_hl(2 * y1 - e1, -A_LIM, A_LIM);
        e2 = clamp_hl(2 * y2 - e2, -A_LIM, A_LIM);
        e3 = clamp_hl(2 * y3 - e3, -A_LIM, A_LIM);
        {                                            /* (the loop's high-pass: its low-pass state, the rounding's
                                                      * remainder carried in e5: no dead zone for k to ring on) */
            int64_t a = (int64_t)(y3 - e4) * 280 + e5;
            v = (int32_t)(a >> 16);
            e5 = (int32_t)(a - ((int64_t)v << 16));
        }
        e4 += 2 * v;
        buf[i] = FLT_OUT(buf[i], tanh_s16(mulr16((y3 + 128) >> 8, mk)), dj, w);
    }
    e[0] = e0;
    e[1] = e1;
    e[2] = e2;
    e[3] = e3;
    e[4] = e4;
    e[5] = e5;
}
static inline __attribute__((always_inline)) void ch_ms20(svf_t *s, int16_t *buf, uint32_t n, const int hp,
                                                          const int dj, int32_t w)
{
    const int32_t *c = s->ck, G = c[0], H = c[1], k = c[2], linv = c[3];
    int32_t e0 = s->e[0], e1 = s->e[1];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = FLT_IN(s, buf[i], dj) << 4, be, u, v, y1, y2;
        be = mulr16(H, mulr16(H, e0) - e1);          /* (the band fed back: alpha u + be, alpha = G (1 - G)) */
        u = mulr16(x + mulr16(k, be), linv);
        u = x + ms20_clip(u - x);
        v = mulr16(u - e0, G);
        y1 = v + e0;
        e0 = clamp_hl(y1 + v, -S_LIM, S_LIM);
        v = mulr16(y1 - e1, G);
        y2 = v + e1;
        e1 = clamp_hl(y2 + v, -S_LIM, S_LIM);
        buf[i] = FLT_OUT(buf[i], tanh_s16(((hp ? u - 2 * y1 + y2 : y2) + 8) >> 4), dj, w);
    }
    s->e[0] = e0;
    s->e[1] = e1;
}
static void ch_run(svf_t *s, uint32_t type, int16_t *buf, uint32_t n)
{
    const int32_t *c = s->ck;
    int32_t *e = s->e;
    uint32_t i;
    switch (type) {
    case F_ACID:
        ch_acid(s, buf, n, 0, 0);
        break;
    case F_MS20L:
    case F_MS20H:
        ch_ms20(s, buf, n, type == F_MS20H, 0, 0);
        break;
    default: {                                       /* LADDER */
        const int32_t G = c[0], B = c[1], k = c[2], g4 = c[3], inv = c[4], cb = c[5], mk = c[6];
        for (i = 0; i < n; i++) {
            int32_t x = mulr16(softclip((buf[i] * 2 * s->drive) >> 12) << 4, cb), u, j, v, y;
            int32_t S = mulr16(B, mulr16(G, mulr16(G, mulr16(G, e[0]) + e[1]) + e[2]) + e[3]);
            u = tanh20(x - mulr16(k, mulr16(mulr16(g4, x) + S, inv)));
            v = mulr16(u - e[0], G);                 /* (the 1st stage, saturated: y - y^3 / 3) */
            y = v + e[0];
            e[0] = y + v;
            y = clamp32(y, -(1 << 20), 1 << 20);
            u = y - mulr16(mulr20(mulr20(y, y), y), 21845);
            for (j = 1; j < 4; j++) {
                v = mulr16(u - e[j], G);
                u = v + e[j];
                e[j] = u + v;
            }
            buf[i] = (int16_t)tanh_s16(mulr16((u + 8) >> 4, mk));
        }
        break;
    }
    }
}
static void ch_block(svf_t *s, uint32_t type, int16_t *buf)
{
    int32_t fe = (int32_t)s->fval, f0 = s->fe;
    uint32_t n;
    if (type != F_LADDER && s->xamt)
        fe = clamp32(fe + mulq16(s->xamt, s->eg), 0, 65536);
    if (f0 < 0 || fe == f0) {
        if (f0 < 0 || s->cdirty)
            ch_coef(s, type, (uint32_t)fe);
        ch_run(s, type, buf, BLOCK);
    } else {
        for (n = 0; n < BLOCK / CH_SUB; n++) {
            ch_coef(s, type, (uint32_t)(f0 + (fe - f0) * (int32_t)(n + 1u) / (int32_t)(BLOCK / CH_SUB)));
            ch_run(s, type, buf + n * CH_SUB, CH_SUB);
        }
    }
    s->fe = fe;
    s->cdirty = 0;
}

static void svf_block28(svf_t *s, uint32_t type, int16_t *buf)
{
    uint32_t i;
    if (type != s->was || s->clr) {                  /* (a switch or a hit: from rest, the line silent) */
        memset(s->e, 0, sizeof s->e);
        s->fb = 0;
        s->fe = -1;
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
    if (flt_char(type)) {
        ch_block(s, type, buf);
        return;
    }
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
            buf[i] = (int16_t)tanh_s16(mulr16((u + 8) >> 4, mk));
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
            buf[i] = (int16_t)tanh_s16(mulr16(w, og));
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
            buf[i] = (int16_t)tanh_s16(mulr16(y, og));
        }
        break;
    }
    case F_RESO: {
        const int32_t c1 = s->k1, c2 = s->k2, c3 = s->k3, d = s->k4, og = d + 8192;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = softclip((buf[i] * 2 * s->drive) >> 12);
            int32_t y = cham_bp(&s->e[0], x, c1, d) + mulr16(cham_bp(&s->e[2], x, c2, d), 45875) +
                        mulr16(cham_bp(&s->e[4], x, c3, d), 39322);   /* (the root, the fifth x 0.7, the octave x 0.6) */
            buf[i] = (int16_t)tanh_s16(mulr16(y, og));
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
    s->rk = 6554;                                    /* (1.0: as q) */
    s->fe = -1;
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
            s1 = clamp_hl(s1, -S_LIM, S_LIM);
            s2 = clamp_hl(s2, -S_LIM, S_LIM);
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

/* -------------------------------------------- 1.0: the DJ filter (10d) --- */
/* perform.c's DJ filter: the master, both sides, through the LXR's LP or HP, ACID or MS20 (svf_block's and ch_run's
 * sound), faded in by w (Q15). Cheaper than svf_block on each side then a pass to mix: the input's softclip without
 * its table (dj_in), the loops made per type (no switch a sample), the mix in the loop, the tanh's output as is
 * (tanh_s16), the coefficients worked out once for both sides */
static inline __attribute__((always_inline)) void dj_svf(svf_t *s, int16_t *buf, int32_t w, const int hp)
{
    int32_t s1 = s->s1, s2 = s->s2;
    const int32_t cx = s->cx, cs1 = s->cs1, cs2 = s->cs2, g0 = s->g0, gg0 = s->gg0, g2 = s->g2, r2 = s->r2;
    uint32_t i;
    for (i = 0; i < BLOCK; i++) {                    /* (svf_block's F_LP, F_HP) */
        int32_t x = dj_in(buf[i]);
        int32_t y1 = (int32_t)(((int64_t)cx * x + (int64_t)cs2 * s2 + (int64_t)cs1 * s1) >> 16);
        int32_t xx = x - y1, sc = softclip(s1);
        int32_t y0 = (int32_t)(((int64_t)g0 * sc + (int64_t)gg0 * xx) >> 16);
        int32_t ugb = mulr16(r2, y0);
        s1 = sc + (int32_t)(((int64_t)g2 * (xx - ugb)) >> 16);
        s2 = s2 + (int32_t)(((int64_t)g2 * y0) >> 16);
        s1 = clamp_hl(s1, -S_LIM, S_LIM);
        s2 = clamp_hl(s2, -S_LIM, S_LIM);
        buf[i] = (int16_t)dj_mix(buf[i], hp ? q16_to_s16(x - ugb - y1) : tanh_s16(y1), w);
    }
    s->s1 = s1;
    s->s2 = s2;
}
static void dj_side(svf_t *s, uint32_t type, int16_t *buf, uint32_t n, int32_t w)
{
    switch (type) {
    case F_LP:
        dj_svf(s, buf, w, 0);
        break;
    case F_HP:
        dj_svf(s, buf, w, 1);
        break;
    case F_ACID:
        ch_acid(s, buf, n, 1, w);
        break;
    default:                                         /* F_MS20L, F_MS20H */
        ch_ms20(s, buf, n, type == F_MS20H, 1, w);
        break;
    }
}
static void dj_block(svf_t *s, uint32_t type, int16_t *l, int16_t *r, int32_t w)   /* s: the two sides' */
{
    svf_t *t = s + 1;
    uint32_t c, n;
    for (c = 0; c < 2u; c++) {                       /* (from rest, as svf_block's and svf_block28's) */
        svf_t *u = s + c;
        if (type == F_LP || type == F_HP) {
            u->was = 0;
            u->clr = 0;
        } else if (type != u->was || u->clr) {
            memset(u->e, 0, sizeof u->e);
            u->fe = -1;
            u->clr = 0;
            u->was = (uint8_t)type;
            u->dirty = 1;
        }
    }
    if (s->dirty || t->dirty) {                      /* (the two sides have the same settings: perform.c) */
        svf_coef(s);
        memcpy(&t->f, &s->f, (uint32_t)((char *)&s->e[0] - (char *)&s->f));   /* (f .. lp2q) */
        t->cdirty = s->cdirty;
        t->dirty = 0;
    }
    if (type == F_LP || type == F_HP) {
        dj_side(s, type, l, BLOCK, w);
        dj_side(t, type, r, BLOCK, w);
        return;
    }
    {                                                /* (ch_block's, both sides: its cutoff ramped while it moves) */
        int32_t fe = (int32_t)s->fval, f0 = s->fe;
        if (f0 < 0 || fe == f0) {
            if (f0 < 0 || s->cdirty) {
                ch_coef(s, type, (uint32_t)fe);
                memcpy(t->ck, s->ck, sizeof t->ck);
            }
            dj_side(s, type, l, BLOCK, w);
            dj_side(t, type, r, BLOCK, w);
        } else {
            for (n = 0; n < BLOCK / CH_SUB; n++) {
                ch_coef(s, type, (uint32_t)(f0 + (fe - f0) * (int32_t)(n + 1u) / (int32_t)(BLOCK / CH_SUB)));
                memcpy(t->ck, s->ck, sizeof t->ck);
                dj_side(s, type, l + n * CH_SUB, CH_SUB, w);
                dj_side(t, type, r + n * CH_SUB, CH_SUB, w);
            }
        }
        s->fe = t->fe = fe;
        s->cdirty = t->cdirty = 0;
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
