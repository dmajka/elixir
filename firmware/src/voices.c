/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the LXR-02's six voices, their LFOs and the mixer, in fixed point.
 * Ported from the LXR firmware, Core/Src/Soundengine/DSPAudio (DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c,
 * lfo.c, mixer.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only.
 *
 * Voices 0..2 DRUM (main + FM/mix oscillator), 3 SNARE (oscillator + filtered noise), 4 CYMBAL and 5 HIHAT (three
 * operator FM); track 7 (index 6) plays the hi-hat voice with its open decay. Each voice: a transient generator,
 * an amp EG (repeat for claps), a filter, a waveshaper, a sample rate reducer, pan and an output (left/right/both).
 * The FM-1 has one stereo output: the LXR's two stereo pairs and four mono outs all go to it (a mono out to its
 * side), so a kit keeps its routing. Elixir's sends: FX SEND, how much of the voice goes through the FX bus instead
 * of its output (127: all, the LXR's Out = FX BUS, which a kit from before the sends still means), and REVERB
 * SEND, the voice into the reverb (reverb.c) on top of that.
 *
 * Elixir's sound shaping (0.3), each voice: a choice of waveshaper for its DRIVE (the LXR's curve, or FOLD, CLIP,
 * ASYM, RECTIFY: shape_block); after the sample rate reducer a bit reducer and a tilt EQ (post_block); random per
 * hit (pitch, decay, filter, pan: voice_trigger); a choke group (a hit fades the other voices of its group out in a
 * block); cross modulation from another voice (DUCK: its envelope turns this one down, FILTER: opens this one's
 * filter, RING: multiplies the two). The voices render first, then the cross modulation, then the mixer. */

#define NVOICE 6
#define NTRACK 7
enum { V_DRUM1, V_DRUM2, V_DRUM3, V_SNARE, V_CYM, V_HAT };
enum { SH_LXR, SH_FOLD, SH_CLIP, SH_ASYM, SH_RECT, SH_FUZZ, SH_TAPE, SH_SFOLD, SH_BITE, NSHAPE };   /* the waveshapers
                                                                  * (shape_block; 23a: FUZZ .. BITE) */
enum { XM_DUCK, XM_FILTER, XM_RING, NXMODE };                  /* the cross modulation's modes (xmod_block) */

typedef struct {
    uint32_t phase, inc;         /* the free running increment (rate or sync) */
    uint32_t offset;             /* retrigger phase */
    int32_t rnd;                 /* S&H, Q16 */
    uint32_t rate;               /* the rate setting 0..1 (Q16), for a tempo change */
    uint8_t wave, retrig, sync;
    uint8_t mode, run;           /* 0.13: LM_* (Elixir's LFO MODE); ONE, HALF: still running since the hit */
    uint32_t gone;               /* .. ONE, HALF: the phase run since the hit */
    int32_t hold;                /* .. HOLD: the value at the hit, Q16 */
} lfo_t;

typedef struct {
    uint16_t dest;               /* the parameter, 0 = none */
    int32_t amount;              /* Q16 */
    int32_t last;                /* the last source value, Q16 (0..65536) */
    int32_t nv;                  /* 25b: the value it set this block (0..65536), -1 none (sound.c modnodes_block) */
} modnode_t;

typedef struct {
    osc_t osc, mod, mod2;        /* DRUM: osc + mod; SNARE: osc + mod (the noise); CYMBAL / HIHAT: all three */
    trans_t tr;
    int32_t snap;                /* snapEg value, Q16 */
    decay_eg_t peg;              /* the pitch / mod EG (DRUM, SNARE) */
    slope_eg_t aeg;
    svf_t flt;
    uint8_t ftype, mix_oscs, vol_mod, is_open;
    uint8_t pan, route, out_fx;  /* out_fx: the LXR's Out = FX BUS (a kit from before the sends) */
    int32_t fx_send, rev_send;   /* Q15 */
    int32_t fm1, fm2;            /* DRUM: FM amount; CYMBAL / HIHAT: the two modulators' gains. Q15 */
    int32_t peg_amt;             /* pitch EG depth, Q16 */
    int32_t mix;                 /* SNARE: noise / osc mix, Q15 */
    int32_t vol;                 /* Q15 */
    int32_t velo;                /* Q15 */
    uint32_t pwm;                /* Q16 */
    uint32_t pmod;               /* the oscillator's pitchMod this block, Q16 */
    int32_t dk;                  /* waveshaper k, Q8; dks: DRUM's smoothed copy (Q16) */
    int32_t dks;
    int32_t vol_s;               /* DRUM: the volume, smoothed per sample (Q24) */
    int32_t gain, last_gain;     /* DRUM: amp EG this block / the last (Q24); others: gain (Q24) */
    uint32_t dec_closed, dec_open;
    int16_t pan_l, pan_r;        /* latched at the trigger (USE_TRIGGERED_PAN) */
    uint32_t dec_cnt;            /* the sample rate reducer, Q16 */
    int16_t dec_hold;
    lfo_t lfo;
    lfo_t lfo2;                  /* 25b: the track's LFO 2 (sound.c PAR_L2RATE1 ..: its target is its own voice's) */
    /* Elixir's shaping (sound.c sets them) */
    uint8_t bits, shape, tilt, choke_grp, xsrc, xmode;
    uint8_t rnd[4];              /* random per hit: pitch, decay, filter, pan amounts (0..127) */
    uint8_t choking;             /* choked: fading out (1), silent (2) until its next hit */
    uint8_t throw_rev, throw_fx; /* PERF's THROW: the send at 127 while held (perform.c) */
    uint8_t gate;                /* 19b: GATE: the amp EG holds for the note's length (seq.c gate_start) */
    int32_t drive;               /* DRIVE 0..1, Q16 (the shapers but the LXR's) */
    int32_t xamt;                /* Q15 */
    uint32_t dec_base;           /* the amp EG's decay as set (random per hit scales aeg.dec from it) */
    uint32_t rp;                 /* this hit's random pitch factor, Q16 */
    int32_t rnd_f;               /* .. its random cutoff offset, Q16 */
    int32_t tilt_lp, sh_dc;      /* the tilt EQ's low-pass, RECTIFY's and BITE's DC follower */
    int32_t sh_z;                /* 23a: TAPE's high cut, BITE's last sample */
} voice_t;

static voice_t vc[NVOICE];
static int16_t flt_line[NVOICE][FLT_LINE] RAMBUF;   /* 28: the COMB filter's line per voice (6 KB) */
/* 0.9: the DRUM voices' MODEL section: Rings' resonators (rings.c; 0.6's physical models: physmodels.c, not built);
 * 0.15: Plaits' engines (plaits.c) */
#include "rings.c"
#include "plaits.c"
#include "micro.c"                                  /* (Phase 24: MICRO, on every voice) */

static uint32_t decim_rate[NVOICE + 1];          /* [6]: ALL (the PERF page's global reducer). Q16 */
static modnode_t velo_mod[NVOICE], lfo_mod[NVOICE], lfo2_mod[NVOICE];
static uint32_t seq_bpm_x10 = 1200;               /* the tempo the LFO sync follows */
static volatile uint32_t trig_count[NTRACK];         /* triggers of each track (the UI flashes its key) */
static uint32_t rev_g = 65536u;                      /* 19c: the reverb sends moved by the global LFOs, Q16 (0 .. 2) */
static uint8_t rp_scaled;                            /* 19a: the next hit's random pitch is in the scale's degrees
                                                      * (seq.c seq_trigger): no bend */
static int32_t venv[NVOICE];                         /* each voice's output envelope, Q15 (cross modulation) */
static uint32_t tape_q16 = 65536u;                   /* PERF's TAPE STOP: every voice's pitch (and the tempo), Q16 */
static int32_t exp2_q16(int32_t y);                  /* (fxbus.c) */
/* random per hit: its own generator (the noise oscillators' sequence stays as the LXR's), -32768 .. 32767 */
static uint32_t hit_rng = 0x2545F491u;
static int32_t hit_rand(void)
{
    hit_rng ^= hit_rng << 13;
    hit_rng ^= hit_rng >> 17;
    hit_rng ^= hit_rng << 5;
    return (int32_t)(hit_rng >> 16) - 32768;
}

/* -------------------------------------------------------------- LFO --- */
enum { LFO_SINE, LFO_TRI, LFO_SAW_UP, LFO_SAW_DOWN, LFO_REC, LFO_NOISE, LFO_EXP_UP, LFO_EXP_DOWN, NLFO };
/* 0.13: the LFO's mode, as Elektron's (Elixir's LFO MODE). What restarts it is the LXR's RETRIGGER voice; in HOLD,
 * ONE and HALF with RETRIGGER off, its own voice's hits.
 *   TRIG  the LXR's: a hit starts the cycle again from the offset (RETRIGGER off: it runs free)
 *   HOLD  runs free; a hit takes its value then and holds it until the next
 *   ONE   a hit starts it from the offset, it runs one cycle and stops at its end (still until the first hit)
 *   HALF  the same, half a cycle */
enum { LM_TRIG, LM_HOLD, LM_ONE, LM_HALF, NLFOMODE };
static const uint8_t SYNC_SCALE_X4[12] = {0, 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 128};   /* lfo_calcPhaseInc's scaler x 4 */

static void lfo_update_inc(lfo_t *l)                 /* lfo_setFreq / lfo_setSync */
{
    if (!l->sync || l->sync > 11u) {
        l->inc = lut1024u(LFO_INC, l->rate);
    } else {
        /* bpm / 60 / 4 x scaler cycles a second, / (FS / BLOCK) a block */
        uint64_t num = ((uint64_t)seq_bpm_x10 * SYNC_SCALE_X4[l->sync] * BLOCK) << 32;
        l->inc = (uint32_t)(num / (uint32_t)(10u * 60u * 4u * 4u * FS));
    }
}
static void lfo_recalc_sync(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        lfo_update_inc(&vc[i].lfo);
        lfo_update_inc(&vc[i].lfo2);
    }
}
static int32_t lfo_wave(lfo_t *l, uint32_t old)      /* the wave at the phase (from old: RANDOM's draw), 0..1, Q16 */
{
    switch (l->wave) {
    case LFO_SINE:
        return (int32_t)((uint32_t)(LXR_SINE[l->phase >> 20] + 32767) * 65536u / 65534u);
    case LFO_TRI: {
        uint32_t p = l->phase >> 15;                  /* 17 bits */
        return 65536 - iabs((int32_t)p - 65536);
    }
    case LFO_SAW_UP:
        return (int32_t)(l->phase >> 16);
    case LFO_REC:
        return l->phase > 0x7FFFFFFFu ? 65536 : 0;
    case LFO_NOISE:
        if (old > l->phase)
            l->rnd = (int32_t)(noise_next() >> 16);
        return l->rnd;
    case LFO_SAW_DOWN:
        return (int32_t)((0xFFFFFFFFu - l->phase) >> 16);
    case LFO_EXP_UP: {
        int64_t x = l->phase >> 16;
        return (int32_t)((x * x * x) >> 32);
    }
    case LFO_EXP_DOWN: {
        int64_t x = (0xFFFFFFFFu - l->phase) >> 16;
        return (int32_t)((x * x * x) >> 32);
    }
    default:
        return 0;
    }
}
static int32_t lfo_calc(lfo_t *l)                    /* lfo_calc: 0..1, Q16 */
{
    uint32_t old = l->phase;
    int32_t v;
    if (l->mode >= LM_ONE) {                         /* 0.13: ONE, HALF: to the cycle's (half's) end, then still */
        uint32_t end = l->mode == LM_ONE ? 0xFFFFFFFFu : 0x80000000u;
        if (l->run) {
            uint32_t d = end - l->gone < l->inc ? end - l->gone : l->inc;
            l->phase += d;
            l->gone += d;
            l->run = l->gone < end;
        }
    } else {
        l->phase += l->inc;
    }
    v = lfo_wave(l, old);
    return l->mode == LM_HOLD ? l->hold : v;
}
static void lfo_mode_set(lfo_t *l, uint32_t m)       /* 0.13: ONE, HALF wait at the offset; HOLD from its value now */
{
    if (l->mode == m)
        return;
    l->mode = (uint8_t)m;
    l->run = 0;
    l->gone = 0;
    if (m >= LM_ONE)
        l->phase = l->offset;
    l->hold = lfo_wave(l, l->phase);
}
static void lfo_hit(lfo_t *l)                        /* a trigger, as its mode takes it */
{
    if (l->mode == LM_HOLD) {
        l->hold = lfo_wave(l, l->phase);
        return;
    }
    l->phase = l->offset;
    if (l->mode >= LM_ONE) {
        l->gone = 0;
        l->run = 1;
        if (l->wave == LFO_NOISE)                    /* (a new value each one-shot) */
            l->rnd = (int32_t)(noise_next() >> 16);
    }
}
static void lfo_retrigger(uint32_t voice)            /* a voice was triggered: the LFOs that follow it restart */
{
    uint32_t i;
    for (i = 0; i < 2u * NVOICE; i++) {              /* (25b: LFO 2 as LFO 1) */
        lfo_t *l = i < NVOICE ? &vc[i].lfo : &vc[i - NVOICE].lfo2;
        uint32_t src = l->retrig ? l->retrig : l->mode != LM_TRIG ? i % NVOICE + 1u : 0u;
        if (src == voice + 1u)
            lfo_hit(l);
    }
}

/* --------------------------------------------------------- triggers --- */
static void modnode_update(modnode_t *m, int32_t val);   /* (sound.c) */

static void voice_pan_latch(voice_t *v)
{
    int32_t p = v->pan & 127u;
    if (v->rnd[3])                                   /* random pan: up to +-64 */
        p = clamp32(p + hit_rand() * v->rnd[3] / (127 * 512), 0, 127);
    v->pan_l = (int16_t)(PAN_L[p] >> 1);             /* (Q14: 32768 would not fit) */
    v->pan_r = (int16_t)(PAN_R[p] >> 1);
}

static uint32_t trans_offset(const voice_t *v)       /* OFFSET mode: 1 - the transient's volume, Q16 */
{
    return v->tr.wave == TR_OFFSET ? (uint32_t)(65536 - (v->tr.vol << 1)) : 65536u;
}

/* 28: COMB and RESONATOR follow the played note (60: as set) */
static void flt_key(svf_t *f, uint32_t note)
{
    int8_t k = (int8_t)((int32_t)note - SEQ_DEFAULT_NOTE);
    if (f->key != k) {
        f->key = k;
        f->dirty = 1;
    }
}

static void voice_trigger(uint32_t track, uint32_t vel, uint32_t note)
{
    uint32_t vi = track < V_HAT ? track : V_HAT;
    voice_t *v = &vc[vi];
    if (track < NTRACK)
        trig_count[track]++;
    lfo_retrigger(vi);
    modnode_update(&velo_mod[vi], (int32_t)(vel * 65536u / 127u));
    v->velo = (int32_t)(vel * 32768u / 127u);
    if (vi <= V_DRUM3) {                                 /* Drum_trigger */
        osc_reset_phase(&v->osc, trans_offset(v), 1);
        osc_reset_phase(&v->mod, 65536u, 1);
        osc_base_note(&v->osc, note);
        osc_base_note(&v->mod, note);
        v->peg.value = EG_ONE;
        svf_reset(&v->flt);
    } else if (vi == V_SNARE) {                          /* Snare_trigger */
        osc_reset_phase(&v->osc, trans_offset(v), 0);
        v->peg.value = EG_ONE;
        osc_base_note(&v->osc, note);
    } else {                                             /* Cymbal_trigger, HiHat_trigger */
        osc_reset_phase(&v->osc, trans_offset(v), 0);
        v->mod.phase = v->mod2.phase = 0;
        osc_base_note(&v->osc, note);
        osc_base_note(&v->mod, note);
        osc_base_note(&v->mod2, note);
        if (vi == V_HAT) {
            v->is_open = track == 6u;
            v->aeg.dec = v->is_open ? v->dec_open : v->dec_closed;
        }
    }
    /* random per hit: pitch (up to an octave either way, the amount squared: fine at small amounts), decay (x0.5 ..
     * x2 of its time), cutoff (up to half its range either way) */
    v->rp = v->rnd[0] && !rp_scaled ? (uint32_t)exp2_q16(hit_rand() * v->rnd[0] * v->rnd[0] * 2 / 16129) : 65536u;
    rp_scaled = 0;                                   /* (Q16 octaves) */
    if (mi[vi].type)                                 /* (24: a MICRO sound's, at the voice's next block) */
        mi_trigger(vi, vel);
    else if (vi <= V_DRUM3 && mdl[vi].type >= MD_PL0)    /* (0.15: a Plaits engine's) */
        pl_trigger(vi, vel);
    else if (vi <= V_DRUM3 && mdl[vi].type)              /* (0.6: a model's strike, at the hit's pitch) */
        model_trigger(vi, vel, (uint32_t)(((uint64_t)v->osc.note_inc * v->rp) >> 16));
    if (v->rnd[1]) {
        uint32_t base = vi == V_HAT ? v->aeg.dec : v->dec_base;
        int32_t y = hit_rand() * v->rnd[1] / 127 * 2;   /* (Q16 octaves of time) */
        v->aeg.dec = (uint32_t)(((uint64_t)base * (uint32_t)exp2_q16(-y)) >> 16);
    } else if (vi != V_HAT) {
        v->aeg.dec = v->dec_base;
    }
    v->rnd_f = v->rnd[2] ? hit_rand() * v->rnd[2] / 127 : 0;
    flt_key(&v->flt, v->osc.base_note);
    if (v->choke_grp) {                              /* the choke group: the others fade out */
        uint32_t w;
        for (w = 0; w < NVOICE; w++)
            if (w != vi && vc[w].choke_grp == v->choke_grp && !vc[w].choking)
                vc[w].choking = 1;
    }
    v->choking = 0;
    slope_trigger(&v->aeg);
    v->tr.phase = 0;
    v->snap = 65536;
    voice_pan_latch(v);
}

/* 19b: GATE. A note held (on: the voice's GATE on) or let go: the amp EG's top held until then, then its decay */
static void voice_hold(uint32_t track, int on)
{
    slope_eg_t *e = &vc[track < V_HAT ? track : V_HAT].aeg;
    if (on) {
        e->hold = vc[track < V_HAT ? track : V_HAT].gate;
        return;
    }
    e->hold = 0;
    if (e->state == EG_H)
        e->state = EG_D;
}
/* .. a tied note's next: the voice's pitch there, nothing struck again (legato) */
static void voice_legato(uint32_t track, uint32_t note)
{
    uint32_t vi = track < V_HAT ? track : V_HAT;
    voice_t *v = &vc[vi];
    if (track < NTRACK)
        trig_count[track]++;
    osc_base_note(&v->osc, note);
    if (vi != V_SNARE)
        osc_base_note(&v->mod, note);
    if (vi >= V_CYM)
        osc_base_note(&v->mod2, note);
    flt_key(&v->flt, v->osc.base_note);
}

/* ---------------------------------------------------- the block, async --- */
/* calcDrumVoiceAsync / Snare_calcAsync / Cymbal_calcAsync / HiHat_calcAsync: envelopes and pitches, once a block */
static void voice_async(uint32_t vi)
{
    voice_t *v = &vc[vi];
    uint32_t rp = tape_q16 == 65536u ? v->rp : (uint32_t)(((uint64_t)v->rp * tape_q16) >> 16);
    if (vi <= V_DRUM3 || vi == V_SNARE) {
        int32_t pe = decay_calc(&v->peg);                /* Q24 */
        uint32_t pmod = 65536u + (uint32_t)(((int64_t)pe * v->peg_amt) >> 24);
        if (v->tr.wave == TR_SNAP && !mi[vi].type)   /* (MICRO has no transient) */
            pmod += (uint32_t)(((int64_t)snap_calc(&v->snap, v->tr.inc) * v->tr.vol) >> 15);
        pmod = rp == 65536u ? pmod : (uint32_t)(((uint64_t)pmod * rp) >> 16);   /* (random pitch, tape) */
        v->pmod = pmod;
        if (vi <= V_DRUM3) {
            v->fm2 = (int32_t)(((int64_t)v->fm1 * pe) >> 24);   /* osc.fmMod = fmModAmount x egPitchVal */
            if (v->aeg.atk == EG_ONE && (v->aeg.state == EG_A || v->aeg.state == EG_REPEAT)) {
                v->gain = slope_calc(&v->aeg);           /* attack 0: no ramp */
                v->last_gain = v->gain;
            } else {
                v->last_gain = v->gain;
                v->gain = slope_calc(&v->aeg);
            }
            osc_set_freq(&v->osc, pmod);
            osc_set_freq(&v->mod, rp);
        } else {
            v->gain = slope_calc(&v->aeg);
            osc_set_freq(&v->osc, pmod);
            osc_set_freq(&v->mod, rp);
        }
    } else {
        v->gain = slope_calc(&v->aeg);
        if (v->tr.wave == TR_SNAP && !mi[vi].type)
            v->pmod = 65536u + (uint32_t)(((int64_t)snap_calc(&v->snap, v->tr.inc) * v->tr.vol) >> 15);
        osc_set_freq(&v->osc, rp == 65536u ? v->pmod : (uint32_t)(((uint64_t)v->pmod * rp) >> 16));
        osc_set_freq(&v->mod, rp);
        osc_set_freq(&v->mod2, rp);
    }
}

/* ------------------------------------------------------- the block, sync --- */
static void add_sat(int16_t *a, const int16_t *b)
{
    uint32_t i;
    for (i = 0; i < BLOCK; i++)
        a[i] = (int16_t)sat16(a[i] + b[i]);
}

/* the voice's volume x gain (Q15 x Q24) as Q15, and the velocity when VOL MOD is on */
static int32_t combined_q15(const voice_t *v)
{
    int64_t c = ((int64_t)v->vol * v->gain) >> 24;
    if (v->vol_mod)
        c = (c * v->velo) >> 15;
    return (int32_t)c;
}

/* the waveshaper of the voice's DRIVE, but the LXR's (its curve: dist_sample): the drive a gain of 1 .. 8, then
 * FOLD (folds back from full scale, a triangle), CLIP (hard), ASYM (the positive half soft, the negative clipped
 * lower: even harmonics), RECTIFY (full wave, the octave up; its DC followed and taken out). 23a: FUZZ (a gain of 4
 * .. 56 into a hard clip, the quietest of the tail gated down), TAPE (a cubic soft clip, then a high cut from ~11 kHz
 * down to ~5 kHz with the drive: 0.8 .. 0.5), SINE FOLD (a sine of the input: West Coast folds, two turns at the
 * most), BITE (the highs pushed, then a biased soft clip harder with the drive: an overdrive that bites; its DC taken
 * out) */
static inline int32_t soft3(int32_t x)               /* x - 4/27 x^3 over -1.5 .. 1.5 (Q15): full scale at 1.5 */
{
    int32_t x3;
    x = clamp32(x, -49152, 49152);
    x3 = (int32_t)((((int64_t)x * x >> 15) * x) >> 15);
    return x - ((x3 * 4855) >> 15);
}
static void shape_block(voice_t *v, int16_t *buf)
{
    uint32_t i;
    int32_t g = 65536 + 7 * v->drive, lo = -32767 + (int32_t)(((int64_t)v->drive * 22000) >> 16);
    for (i = 0; i < BLOCK; i++) {
        int32_t a = (int32_t)(((int64_t)buf[i] * g) >> 16), y;
        switch (v->shape) {
        case SH_FOLD:
            if (a > 32767 || a < -32767) {
                int32_t t = (a + 32767) % (4 * 32767);
                if (t < 0)
                    t += 4 * 32767;
                a = t < 2 * 32767 ? t - 32767 : 3 * 32767 - t;
            }
            y = a;
            break;
        case SH_CLIP:
            y = sat16(a);
            break;
        case SH_ASYM:
            y = a >= 0 ? dist_sample(sat16(a), 1024) : (a < lo ? lo : a);
            break;
        case SH_RECT:
            y = iabs(a) * 2;
            v->sh_dc += (y - v->sh_dc) >> 8;
            y = sat16(y - v->sh_dc);
            break;
        case SH_FUZZ:
            y = sat16((int32_t)(((int64_t)a * (4 * 65536 + 3 * v->drive)) >> 16));   /* (a x 4 .. 7) */
            if (iabs(buf[i]) < 256)                  /* (the gate: below -42 dB, down with the input) */
                y = (y * iabs(buf[i])) >> 8;
            break;
        case SH_TAPE:
            v->sh_z += (int32_t)(((int64_t)(soft3(a) - v->sh_z) * (26214 - ((v->drive * 9830) >> 16))) >> 15);
            y = sat16(v->sh_z);
            break;
        case SH_SFOLD:
            y = sine_at(((uint32_t)a << 15) + 0x40000000u);   /* (sin: the LXR's table is -cos; full scale a
                                                              * quarter turn) */
            break;
        default: {                                   /* SH_BITE */
            int32_t e = a + ((a - v->sh_z) >> 1);
            v->sh_z = a;
            y = dist_sample(sat16(e + 4096), 256 + ((v->drive * 1536) >> 16));   /* (the bias: odd and even) */
            v->sh_dc += (y - v->sh_dc) >> 8;
            y = sat16(y - v->sh_dc);
            break;
        }
        }
        buf[i] = (int16_t)y;
    }
}

static void drum_block(voice_t *v, int16_t *buf)     /* calcDrumVoiceSyncBlock */
{
    int16_t mb[BLOCK];
    uint32_t i, vi = (uint32_t)(v - vc);
    int32_t g, step;
    if (vi <= V_DRUM3 && mdl[vi].type >= MD_PL0) {   /* 0.15: a Plaits engine (plaits.c): its own sound */
        pl_block(vi, v, buf);
        svf_block(&v->flt, v->ftype, buf);
        goto shape;
    }
    if (vi <= V_DRUM3 && mdl[vi].type) {             /* 0.9: a model (rings.c), excited by the voice's own sound */
        if (mdl[vi].exc) {                           /* (EXCITER: the oscillators, transient and amp EG) */
            osc_block(&v->mod, mb, v->fm1, 0, 0, v->pwm);
            if (v->mix_oscs) {
                osc_block(&v->osc, buf, 32768 - v->fm1, 0, 0, v->pwm);
                add_sat(buf, mb);
            } else {
                osc_block(&v->osc, buf, 32768, mb, v->fm2, v->pwm);
            }
            trans_block(&v->tr, mb);
            add_sat(buf, mb);
            g = v->last_gain;
            step = (v->gain - v->last_gain) / (BLOCK - 1);
            for (i = 0; i < BLOCK; i++, g += step)
                buf[i] = (int16_t)(((int64_t)buf[i] * g) >> 24);
            if (v->vol_mod)
                for (i = 0; i < BLOCK; i++)
                    buf[i] = (int16_t)((buf[i] * v->velo) >> 15);
        } else {
            memset(buf, 0, BLOCK * sizeof buf[0]);
        }
        model_block(vi, v, buf);
        svf_block(&v->flt, v->ftype, buf);
        goto shape;
    }
    if (mi[vi].type) {                               /* 24: MICRO (micro.c): its sound for the oscillators and transient */
        mi_block(vi, v, buf);
    } else {
        osc_block(&v->mod, mb, v->fm1, 0, 0, v->pwm);
        if (v->mix_oscs) {
            osc_block(&v->osc, buf, 32768 - v->fm1, 0, 0, v->pwm);
            add_sat(buf, mb);
        } else {
            osc_block(&v->osc, buf, 32768, mb, v->fm2, v->pwm);
        }
        trans_block(&v->tr, mb);
        add_sat(buf, mb);
    }
    svf_block(&v->flt, v->ftype, buf);
    /* the amp EG, from last block's gain to this one's (bufferTool_addGainInterpolated; 24: not on MICRO, its LENGTH
     * shapes it) */
    g = v->last_gain;
    step = (v->gain - v->last_gain) / (BLOCK - 1);
    if (!mi[vi].type)
        for (i = 0; i < BLOCK; i++, g += step)
            buf[i] = (int16_t)(((int64_t)buf[i] * g) >> 24);
    if (v->vol_mod)
        for (i = 0; i < BLOCK; i++)
            buf[i] = (int16_t)((buf[i] * v->velo) >> 15);
shape:
    if (v->shape == SH_LXR) {
        for (i = 0; i < BLOCK; i++) {                /* calcDistBlockFiltered: the shape smoothed (0.01 a sample) */
            v->dks += ((v->dk << 8) - v->dks) / 100;
            buf[i] = (int16_t)dist_sample(buf[i], v->dks >> 8);
        }
    } else {
        shape_block(v, buf);
    }
    for (i = 0; i < BLOCK; i++) {                    /* the mixer volume, smoothed (MixerPageFilter, 0.01) */
        v->vol_s += ((v->vol << 9) - v->vol_s) / 100;
        buf[i] = (int16_t)((buf[i] * (v->vol_s >> 9)) >> 15);
    }
}

/* 24: SN, CY, HH on MICRO: its sound for the voice's own, then the filter, the volume (and velocity: VOL MOD; not the
 * amp EG: MICRO's LENGTH shapes it) and the shape */
static void micro_block(voice_t *v, int16_t *buf)
{
    uint32_t i;
    int32_t c = v->vol_mod ? (v->vol * v->velo) >> 15 : v->vol;
    mi_block((uint32_t)(v - vc), v, buf);
    svf_block(&v->flt, v->ftype, buf);
    for (i = 0; i < BLOCK; i++)
        buf[i] = (int16_t)sat16((int32_t)(((int64_t)buf[i] * c) >> 15));
    if (v->shape == SH_LXR)
        dist_block(buf, v->dk);
    else
        shape_block(v, buf);
}

static void snare_block(voice_t *v, int16_t *buf)    /* Snare_calcSyncBlock */
{
    int16_t ob[BLOCK], tb[BLOCK];
    uint32_t i;
    int32_t c;
    osc_block(&v->mod, buf, 29491, 0, 0, 0);         /* the noise, x 0.9 */
    svf_block(&v->flt, v->ftype, buf);
    osc_block(&v->osc, ob, 32768 - v->mix, 0, 0, v->pwm);
    trans_block(&v->tr, tb);
    c = combined_q15(v);
    for (i = 0; i < BLOCK; i++) {
        int32_t t = ((buf[i] * v->mix) >> 15) + ob[i] + tb[i];
        buf[i] = (int16_t)sat16((int32_t)(((int64_t)t * c) >> 15));
    }
    if (v->shape == SH_LXR)
        dist_block(buf, v->dk);
    else
        shape_block(v, buf);
}

static void metal_block(voice_t *v, int16_t *buf, int32_t gain)   /* Cymbal / HiHat_calcSyncBlock */
{
    int16_t m1[BLOCK], m2[BLOCK];
    uint32_t i;
    int32_t c;
    osc_block(&v->mod, m1, v->fm1, 0, 0, v->pwm);
    osc_block(&v->mod2, m2, v->fm2, 0, 0, v->pwm);
    add_sat(m1, m2);
    osc_block(&v->osc, buf, gain, m1, -1, v->pwm);   /* fmMod 1: the modulator's sample as it is */
    svf_block(&v->flt, v->ftype, buf);
    trans_block(&v->tr, m1);
    c = combined_q15(v);
    for (i = 0; i < BLOCK; i++)
        buf[i] = (int16_t)sat16((int32_t)(((int64_t)sat16(buf[i] + m1[i]) * c) >> 15));
    if (v->shape == SH_LXR)
        dist_block(buf, v->dk);
    else
        shape_block(v, buf);
}

static void decimate(voice_t *v, int16_t *buf, uint32_t rate)     /* mixer_decimateBlock */
{
    uint32_t i;
    if (rate >= 65536u && v->dec_cnt == 0) {
        v->dec_hold = buf[BLOCK - 1];
        return;
    }
    for (i = 0; i < BLOCK; i++) {
        v->dec_cnt += rate;
        if (v->dec_cnt >= 65536u) {
            v->dec_cnt -= 65536u;
            v->dec_hold = buf[i];
        }
        buf[i] = v->dec_hold;
    }
}

/* after the sample rate reducer: the bit reducer (rounded to the kept bits) and the tilt EQ (a one-pole split at
 * ~700 Hz; TILT 64 flat, 127: the lows x0.25 and the highs x1.75, 0 the other way) */
static void post_block(voice_t *v, int16_t *buf)
{
    uint32_t i;
    if (v->bits) {
        int32_t half = 1 << (v->bits - 1u), mask = ~((1 << v->bits) - 1);
        for (i = 0; i < BLOCK; i++)
            buf[i] = (int16_t)sat16((buf[i] + half) & mask);
    }
    if (v->tilt != 64u) {
        int32_t a = ((int32_t)v->tilt - 64) * 24576 / 63, gl = 32768 - a, gh = 32768 + a;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = buf[i], h;
            v->tilt_lp += ((x - v->tilt_lp) * 3112) >> 15;
            h = x - v->tilt_lp;
            buf[i] = (int16_t)sat16((v->tilt_lp * gl + h * gh) >> 15);
        }
    }
}

/* ----------------------------------------------------------- insert --- */
/* 27a: one INSERT per track, after the shaper, the bits and the tilt, before the pan and the sends (mono). AMOUNT (a)
 * is how much of it is heard, 0 none, whatever the type; b and c are the type's (params.c names them). Its LFO steps
 * a block at a time, and the delays and gains are ramped across the block. The delay types have the voice's own line
 * (ins_line: Phase 28's COMB keeps its own). A new type starts from rest */
enum { INS_OFF, INS_PHASER, INS_FLANGER, INS_CHORUS, INS_TREMOLO, INS_AUTOPAN, INS_SHIFT, INS_RING, NINS };
#define INS_LINE 512u
typedef struct {
    uint8_t type, a, b, c;       /* (sound.c: PAR_INS1 ..) */
    uint8_t was;                 /* the type its state is for */
    uint16_t wp;                 /* the line's write position */
    uint32_t ph, osc;            /* the LFO's phase (a block); RING's and SHIFTER's oscillator (a sample) */
    int32_t st[33];              /* PHASER: 4 allpasses' x1, y1; SHIFTER: 8 sections' x1 x2 y1 y2, then the delay */
    int32_t fb;                  /* the last output, fed back */
    int32_t d[2];                /* FLANGER, CHORUS: the taps' delays at the block's end, Q16 samples */
    int32_t g;                   /* TREMOLO: the gain at the block's end, Q15 */
    int32_t pg[2][2];            /* AUTOPAN: the left and right gains at the block's start [0] and end [1], Q15 */
} ins_t;
static ins_t ins[NVOICE] RAMBUF;                    /* (RAMTEXT's: the pool is short) */
static int16_t ins_line[NVOICE][INS_LINE] POOL;     /* (6 KB) */
/* SHIFTER: two chains of four allpasses 90 degrees apart (Olli Niemitalo's, a^2 in Q30); the first's output a sample
 * late */
static const int32_t HILB[8] = {514752760, 940832443, 1048613677, 1071056671,
                                173686865, 787083823, 1015061512, 1063647745};

static uint32_t ins_inc(uint32_t b)                  /* the LFO's step a block: 0.05 Hz .. 12.8 Hz (b 0..127) */
{
    return (uint32_t)(((uint64_t)311665u * (uint32_t)exp2_q16((int32_t)(b * 8u * 65536u / 127u))) >> 16);
}
static int32_t ins_shape(uint32_t ph, uint32_t c)    /* TREMOLO, AUTOPAN: a sine (c 0) to a square (127), Q15 */
{
    int32_t v = sine_at(ph), q = v >= 0 ? 32767 : -32767;
    return v + (((q - v) * (int32_t)c) / 127);
}
static inline int32_t ins_read(const int16_t *l, uint32_t wp, int32_t d)   /* the line d (Q16) samples back */
{
    uint32_t pos = (wp << 16) - (uint32_t)d, i = (pos >> 16) & (INS_LINE - 1u), f = pos & 0xFFFFu;
    int32_t x0 = l[i], x1 = l[(i + 1u) & (INS_LINE - 1u)];
    return x0 + (int32_t)(((int64_t)(x1 - x0) * (int32_t)f) >> 16);
}
static void ins_block(uint32_t vi, int16_t *buf)
{
    ins_t *s = &ins[vi];
    int16_t *line = ins_line[vi];
    uint32_t i, t = s->type < NINS ? s->type : INS_OFF;
    int32_t amt = s->a * 258;                        /* Q15 */
    if (t != s->was) {                               /* (a new type: from rest) */
        memset(s->st, 0, sizeof s->st);
        s->fb = 0;
        s->d[0] = s->d[1] = 0;
        s->g = 32768;
        s->pg[0][0] = s->pg[0][1] = s->pg[1][0] = s->pg[1][1] = 32768;
        if (t == INS_FLANGER || t == INS_CHORUS)
            memset(line, 0, sizeof ins_line[0]);
        s->was = (uint8_t)t;
    }
    if (t == INS_OFF)
        return;
    switch (t) {
    case INS_PHASER: {                               /* 4 first-order allpasses swept 150 Hz .. 4.8 kHz, fed back + or - */
        int32_t l, w, c, fbk = ((int32_t)s->c - 64) * 400, m = amt >> 1;   /* (c 64: no feedback) */
        s->ph += ins_inc(s->b);
        l = 65536 - iabs((int32_t)(s->ph >> 15) - 65536);                     /* (a triangle, Q16) */
        w = (int32_t)(((int64_t)150 * exp2_q16(l * 5) * 76490) >> 30);       /* (pi f / FS, Q16) */
        c = (int32_t)(((int64_t)(65536 - w) << 15) / (65536 + w));
        for (i = 0; i < BLOCK; i++) {                /* (8 bits below the sample's, rounded: no limit cycle) */
            int32_t x = buf[i], in = clamp32((x << 8) + (int32_t)(((int64_t)s->fb * fbk) >> 15), -(1 << 23), 1 << 23), k, o;
            for (k = 0; k < 8; k += 2) {
                int32_t y = (int32_t)(((int64_t)c * (s->st[k + 1] - in) + 16384) >> 15) + s->st[k];
                s->st[k] = in;
                s->st[k + 1] = y = clamp32(y, -(1 << 24), 1 << 24);
                in = y;
            }
            s->fb = in;
            o = (in + 128) >> 8;
            buf[i] = (int16_t)sat16(x + (((o - x) * m) >> 15));
        }
        break;
    }
    case INS_FLANGER:                                /* 0.23 .. 4.7 ms, fed back + or - (c 64: none) */
    case INS_CHORUS: {                               /* two taps either side of 6 ms, c the depth */
        int32_t d0[2] = {0, 0}, dd[2] = {0, 0}, fbk = t == INS_FLANGER ? clamp32(((int32_t)s->c - 64) * 480, -30000, 30000) : 0;
        int32_t m = amt >> 1;
        uint32_t k, n = t == INS_CHORUS ? 2u : 1u;
        s->ph += ins_inc(s->b);
        for (k = 0; k < n; k++) {
            int32_t e;
            if (t == INS_FLANGER) {
                int32_t l = 65536 - iabs((int32_t)(s->ph >> 15) - 65536);
                e = (10 << 16) + l * 200;
            } else {
                e = (265 << 16) + (int32_t)(((int64_t)sine_at(s->ph + k * 0x80000000u) * s->c * 3467) >> 10);   /* (+-215) */
            }
            d0[k] = s->d[k] ? s->d[k] : e;
            dd[k] = (e - d0[k]) / (int32_t)BLOCK;
            s->d[k] = e;
        }
        for (i = 0; i < BLOCK; i++) {
            int32_t x = buf[i], r = ins_read(line, s->wp, d0[0]);
            if (n == 2u)
                r = (r + ins_read(line, s->wp, d0[1])) >> 1;
            line[s->wp] = (int16_t)sat16(x + ((r * fbk) >> 15));
            s->wp = (uint16_t)((s->wp + 1u) & (INS_LINE - 1u));
            d0[0] += dd[0];
            d0[1] += dd[1];
            buf[i] = (int16_t)sat16(x + (((r - x) * m) >> 15));
        }
        break;
    }
    case INS_TREMOLO: {
        int32_t g0 = s->g, g1, dg;
        s->ph += ins_inc(s->b);
        g1 = 32768 - (int32_t)((((int64_t)ins_shape(s->ph, s->c) + 32767) * amt) >> 16);
        dg = (g1 - g0) / (int32_t)BLOCK;
        for (i = 0; i < BLOCK; i++, g0 += dg)
            buf[i] = (int16_t)((buf[i] * g0) >> 15);
        s->g = g1;
        break;
    }
    case INS_AUTOPAN: {                              /* (mix_voice pans with pg: one side turned down at a time) */
        int32_t p;
        s->ph += ins_inc(s->b);
        p = (int32_t)(((int64_t)ins_shape(s->ph, s->c) * amt) >> 15);
        s->pg[0][0] = s->pg[1][0];
        s->pg[0][1] = s->pg[1][1];
        s->pg[1][0] = 32768 - (p > 0 ? p : 0);
        s->pg[1][1] = 32768 + (p < 0 ? p : 0);
        break;
    }
    case INS_SHIFT: {                                /* +-500 Hz (b 64: none), fed back + or - (c 64: none; at most
                                                      * 0.64: near DC the pair is less than 90 degrees apart) */
        int32_t d = (int32_t)s->b - 64, hz = d * d * 500 / (63 * 63), fbk = clamp32(((int32_t)s->c - 64) * 335, -21000, 21000);
        int32_t inc = (int32_t)(((int64_t)(d < 0 ? -hz : hz) << 32) / FS);
        for (i = 0; i < BLOCK; i++) {                /* (8 bits below the sample's, rounded: no limit cycle) */
            int32_t x = buf[i], a = (x << 8) + (int32_t)(((int64_t)s->fb * fbk) >> 15), b, k, y;
            a = b = clamp32(a, -(1 << 23), 1 << 23);
            for (k = 0; k < 4; k++) {                /* (y = a^2 (x + y2) - x2) */
                int32_t *z = &s->st[k * 4];
                y = (int32_t)(((int64_t)HILB[k] * (a + z[3]) + (1 << 29)) >> 30) - z[1];
                z[1] = z[0]; z[0] = a; z[3] = z[2]; z[2] = y;
                a = y;
            }
            for (k = 4; k < 8; k++) {
                int32_t *z = &s->st[k * 4];
                y = (int32_t)(((int64_t)HILB[k] * (b + z[3]) + (1 << 29)) >> 30) - z[1];
                z[1] = z[0]; z[0] = b; z[3] = z[2]; z[2] = y;
                b = y;
            }
            y = (int32_t)(((int64_t)s->st[32] * sine_at(s->osc + 0x40000000u) + (int64_t)b * sine_at(s->osc) +
                           16384) >> 15);
            s->st[32] = a;
            s->osc += (uint32_t)inc;
            s->fb = y = clamp32(y, -(1 << 23), 1 << 23);
            y = (y + 128) >> 8;
            buf[i] = (int16_t)sat16(x + (((y - x) * amt) >> 15));
        }
        break;
    }
    default: {                                       /* RING: against a sine of 20 Hz .. 2.6 kHz; c 127: AM */
        uint32_t inc = (uint32_t)(((uint64_t)20u * (uint32_t)exp2_q16((int32_t)(s->b * 7u * 65536u / 127u)) << 16) / FS);
        int32_t c = s->c * 258;
        for (i = 0; i < BLOCK; i++) {
            int32_t x = buf[i], w = sine_at(s->osc), mm = w + ((((32767 + w) >> 1) - w) * c >> 15);
            int32_t y = (x * mm) >> 15;
            s->osc += inc;
            buf[i] = (int16_t)sat16(x + (((y - x) * amt) >> 15));
        }
        break;
    }
    }
}

/* ------------------------------------------------------------ mixer --- */
enum { R_ST1, R_ST2, R_L1, R_R1, R_L2, R_R2, R_FX };
static int32_t mix_l[BLOCK], mix_r[BLOCK];          /* the outputs (both stereo pairs and the mono outs) */
/* 0.6: PERFORM 2 (perform.c). PUMP: every voice but D1 through a gain from pump_g0 to pump_g1 over the block (Q15);
 * SPACE: the dry mix dipped before the FX bus and reverb join it (and the reverb held: reverb.c) */
static int32_t pump_g0 = 32768, pump_g1 = 32768;
static uint8_t space_on;
static int16_t fx_l[BLOCK], fx_r[BLOCK];            /* the FX bus (the LXR's saturating int16 sums) */
static int32_t rev_in[BLOCK];                       /* the reverb's send (mono) */
static uint32_t rev_fed;                            /* a send fed it this block */
static uint32_t voice_active;                       /* bit v: the voice sounded this block (the UI's meters) */
/* 0.16: the master compressor's SIDECHAIN voice (1..6, 0 none: fxbus.c mcomp): its dry part goes here, not into the
 * mix, so the compressor's gain leaves it alone (comp_master adds it back) */
static uint8_t mc_key;
static int32_t mc_kl[BLOCK], mc_kr[BLOCK];
static int32_t voice_peak[NVOICE];

static void mix_voice(const voice_t *v, const int16_t *buf, int32_t *dl, int32_t *dr)   /* mixer_addDataToOutput, with
                                                      * the sends; the dry part into dl, dr */
{
    uint32_t i;
    int32_t s = v->out_fx || v->throw_fx ? 32768 : v->fx_send >= 32767 ? 32768 : v->fx_send;
    const ins_t *in = &ins[v - vc];
    int16_t pl[BLOCK], pr[BLOCK];                    /* the pan's gains a sample (27a: AUTOPAN moves them), Q14 */
    int32_t rs = v->throw_rev ? 32767 : rev_g == 65536u ? v->rev_send : clamp32((int32_t)(((int64_t)v->rev_send * rev_g) >> 16), 0, 32767);
    int16_t dry[BLOCK];
    if (in->type == INS_AUTOPAN && in->was == INS_AUTOPAN) {
        int32_t gl = in->pg[0][0], gr = in->pg[0][1], dgl = (in->pg[1][0] - gl) / (int32_t)BLOCK,
                dgr = (in->pg[1][1] - gr) / (int32_t)BLOCK;
        for (i = 0; i < BLOCK; i++, gl += dgl, gr += dgr) {
            pl[i] = (int16_t)((v->pan_l * gl) >> 15);
            pr[i] = (int16_t)((v->pan_r * gr) >> 15);
        }
    } else {
        for (i = 0; i < BLOCK; i++) {
            pl[i] = v->pan_l;
            pr[i] = v->pan_r;
        }
    }
    if (rs) {
        int32_t any = 0;
        for (i = 0; i < BLOCK; i++) {
            rev_in[i] += (buf[i] * rs) >> 15;
            any |= buf[i];
        }
        rev_fed |= any != 0;                         /* (a silent voice lets the reverb sleep) */
    }
    if (s) {                                         /* the FX send: its share through the bus (panned, as the LXR's) */
        for (i = 0; i < BLOCK; i++) {
            int32_t x = s == 32768 ? buf[i] : (buf[i] * s) >> 15;
            fx_l[i] = (int16_t)sat16(fx_l[i] + ((x * pl[i]) >> 14));
            fx_r[i] = (int16_t)sat16(fx_r[i] + ((x * pr[i]) >> 14));
        }
        if (s == 32768)
            return;
        for (i = 0; i < BLOCK; i++)                  /* the rest to the output */
            dry[i] = (int16_t)(buf[i] - ((buf[i] * s) >> 15));
        buf = dry;
    }
    switch (v->route) {
    case R_L1:
    case R_L2:
        for (i = 0; i < BLOCK; i++)
            dl[i] += buf[i];
        break;
    case R_R1:
    case R_R2:
        for (i = 0; i < BLOCK; i++)
            dr[i] += buf[i];
        break;
    default:
        for (i = 0; i < BLOCK; i++) {
            dl[i] += (buf[i] * pl[i]) >> 14;
            dr[i] += (buf[i] * pr[i]) >> 14;
        }
        break;
    }
}

static void fxbus_process(void);                    /* (fxbus.c) */
static void fxbus_early(void);
static void reverb_process(void);                   /* (reverb.c) */
static void reverb_add(void);
static void modnodes_block(void);                   /* (sound.c) */

/* one block of the six voices into mix_l / mix_r (Q15, unsaturated) */
static void voice_filter_mods(void);                /* (sound.c) */
static int16_t vbuf[NVOICE][BLOCK];                 /* the voices' blocks, rendered (cross modulation reads them) */

/* cross modulation: voice vi's block from its source's (rendered this block, as it was before its own cross
 * modulation): DUCK turns it down by the source's envelope, RING multiplies the two (x2) and blends that in */
static void xmod_block(uint32_t vi, int16_t *out)
{
    const voice_t *v = &vc[vi];
    uint32_t i, s = v->xsrc - 1u;
    if (v->xmode == XM_DUCK) {
        int32_t g = 32768 - (int32_t)(((int64_t)v->xamt * venv[s]) >> 15);
        for (i = 0; i < BLOCK; i++)
            out[i] = (int16_t)((out[i] * g) >> 15);
    } else if (v->xmode == XM_RING) {
        for (i = 0; i < BLOCK; i++) {
            int32_t r = sat16((out[i] * vbuf[s][i]) >> 14);
            out[i] = (int16_t)sat16(out[i] + (((r - out[i]) * v->xamt) >> 15));
        }
    }
}

/* voices lo .. hi - 1 into vbuf, with their decimation, bits, tilt and output envelope. 0.15 (17c): D1-D3 are the
 * second core's job (c1_drums) while core 0 renders SN, CY and HH: a voice's render touches only its own state (each
 * oscillator and model has its own noise, dsp.c), so the sound is the same on either core, in either order */
static void voices_render(uint32_t lo, uint32_t hi)
{
    uint32_t i, vi;
    for (vi = lo; vi < hi; vi++) {
        voice_t *v = &vc[vi];
        int16_t *b = vbuf[vi];
        int32_t pk = 0;
        if (vi <= V_DRUM3)
            drum_block(v, b);
        else if (mi[vi].type)
            micro_block(v, b);
        else if (vi == V_SNARE)
            snare_block(v, b);
        else
            metal_block(v, b, vi == V_CYM ? 32768 : 16384);
#ifdef VOICE_CAPTURE
        VOICE_CAPTURE(vi, b);                        /* (host tests: the voice before the mixer) */
#endif
        decimate(v, b, (uint32_t)(((uint64_t)decim_rate[vi] * decim_rate[NVOICE]) >> 16));
        post_block(v, b);
        ins_block(vi, b);                            /* (27a) */
        for (i = 0; i < BLOCK; i += 2) {
            int32_t a = iabs(b[i]);
            if (a > pk)
                pk = a;
        }
        if (v->choking == 2u)
            pk = 0;
        venv[vi] = pk > venv[vi] ? pk : venv[vi] - (venv[vi] >> 5);   /* (release ~45 ms) */
    }
}
#if FELUCCA_CPU2
static uint32_t c1_ticks;                            /* the second core's last job, TIMER4 (written there, read after
                                                      * the wait) */
static void c1_drums(uint32_t arg)                   /* (runs on the second core) */
{
    uint32_t t0 = fm1_ticks();
    (void)arg;
    voices_render(V_DRUM1, V_SNARE);
    c1_ticks = fm1_ticks() - t0;
}
#endif
#ifdef VOICES_SWAP
static uint8_t voices_swap;                          /* (host tests: SN-HH first, as two cores may finish them) */
#endif
#ifdef FX_NOW
static uint8_t fx_now;                               /* (host tests: the reverb on this block's send, as before 18b) */
#endif

/* 18b: the effects' work that needs no voice of this block, on core 0 while the second core renders D1-D3: the
 * reverb on the last block's send (reverb.c), the delay's reads (fxbus.c fxbus_early). Then the sends start again
 * for the mixer. On one core, the same work in the same order, so the sound doesn't depend on the cores */
static void fx_early(void)
{
    uint32_t i;
#ifdef FX_NOW
    if (!fx_now)
#endif
        reverb_process();
    fxbus_early();
    for (i = 0; i < BLOCK; i++) {
        fx_l[i] = fx_r[i] = 0;
        rev_in[i] = 0;
    }
    rev_fed = 0;
}

/* one block of the six voices into mix_l / mix_r (Q15, unsaturated) */
static void voices_block(void)
{
    int16_t buf[BLOCK];
    uint32_t i, vi;
    modnodes_block();                                /* reset the targets, velocity and LFO modulation */
    voice_filter_mods();                             /* random per hit, cross modulation's FILTER */
    for (vi = 0; vi < NVOICE; vi++) {
        voice_async(vi);
    }
    model_limit();                                   /* (model_cap may have changed: main.c) */
    for (i = 0; i < BLOCK; i++)
        mix_l[i] = mix_r[i] = mc_kl[i] = mc_kr[i] = 0;
    voice_active = 0;
    LOAD_MARK(LD_SEQ);
#if FELUCCA_CPU2
    if (fm1_cpu1_run(c1_drums, 0)) {                 /* D1-D3 on the second core, SN-HH and the early effects here */
        voices_render(V_SNARE, NVOICE);
        LOAD_MARK(LD_SNHH);
        fx_early();
        LOAD_MARK(LD_REV);
        if (fm1_cpu1_wait() == 0) {
            LOAD_MARK(LD_WAIT);
            ld_acc[LD_D13] += c1_ticks;
        } else {                                     /* it misread or didn't answer: held from now on, D1-D3 here */
            LOAD_MARK(LD_WAIT);
            voices_render(V_DRUM1, V_SNARE);
            LOAD_MARK(LD_D13);
        }
    } else
#endif
#ifdef VOICES_SWAP
    if (voices_swap) {
        voices_render(V_SNARE, NVOICE);
        fx_early();
        voices_render(V_DRUM1, V_SNARE);
    } else
#endif
    {
        voices_render(V_DRUM1, V_SNARE);
        LOAD_MARK(LD_D13);
        voices_render(V_SNARE, NVOICE);
        LOAD_MARK(LD_SNHH);
        fx_early();
        LOAD_MARK(LD_REV);
    }
    for (vi = 0; vi < NVOICE; vi++) {                /* cross modulation, choke, the mixer */
        voice_t *v = &vc[vi];
        const int16_t *b = vbuf[vi];
        int32_t pk = 0;
        if ((v->xsrc && v->xsrc != vi + 1u && v->xamt && v->xmode != XM_FILTER) || v->choking) {
            memcpy(buf, b, sizeof buf);
            if (v->xsrc && v->xsrc != vi + 1u && v->xamt && v->xmode != XM_FILTER)
                xmod_block(vi, buf);
            if (v->choking == 1u) {                  /* choked: out over this block, then silent */
                for (i = 0; i < BLOCK; i++)
                    buf[i] = (int16_t)((buf[i] * (int32_t)(BLOCK - 1u - i)) / (int32_t)BLOCK);
                v->choking = 2;
            } else if (v->choking) {
                memset(buf, 0, sizeof buf);
            }
            b = buf;
        }
        if (vi != V_DRUM1 && (pump_g0 < 32768 || pump_g1 < 32768)) {
            int32_t g = pump_g0 << 6, dg = pump_g1 - pump_g0;   /* (Q21 ramp: 64 samples) */
            if (b != buf)
                memcpy(buf, b, sizeof buf);
            for (i = 0; i < BLOCK; i++, g += dg)
                buf[i] = (int16_t)((buf[i] * (g >> 6)) >> 15);
            b = buf;
        }
        mix_voice(v, b, mc_key == vi + 1u ? mc_kl : mix_l, mc_key == vi + 1u ? mc_kr : mix_r);
        for (i = 0; i < BLOCK; i += 4) {
            int32_t a = iabs(b[i]);
            if (a > pk)
                pk = a;
        }
        voice_peak[vi] = pk;
        if (pk > 64)
            voice_active |= 1u << vi;
    }
    if (space_on)                                    /* SPACE: the dry sound dipped to 0.4 */
        for (i = 0; i < BLOCK; i++) {
            mix_l[i] = (mix_l[i] * 13107) >> 15;
            mix_r[i] = (mix_r[i] * 13107) >> 15;
            mc_kl[i] = (mc_kl[i] * 13107) >> 15;
            mc_kr[i] = (mc_kr[i] * 13107) >> 15;
        }
    LOAD_MARK(LD_MIX);
    fxbus_process();
    LOAD_MARK(LD_FX);
#ifdef FX_NOW
    if (fx_now)
        reverb_process();
#endif
    reverb_add();
    LOAD_MARK(LD_REV);
}

static void voices_init(void)                       /* initDrumVoice, Snare_init, Cymbal_init, HiHat_init */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &vc[i];
        v->vol = 26214;                              /* 0.8 */
        v->osc.midi_freq = v->mod.midi_freq = v->mod2.midi_freq = 70u << 8 | 64u;
        v->osc.base_note = v->mod.base_note = v->mod2.base_note = SEQ_DEFAULT_NOTE;
        v->osc.wave = (uint8_t)(i <= V_DRUM3 ? W_TRI + i : W_TRI);
        v->mod.wave = (uint8_t)(i >= V_CYM ? W_SINE : W_TRI);
        v->mod2.wave = W_NOISE;
        v->osc.rng = 0x9E3779B9u + 0x01000193u * (3u * i);   /* (each its own, never 0) */
        v->mod.rng = 0x9E3779B9u + 0x01000193u * (3u * i + 1u);
        v->mod2.rng = 0x9E3779B9u + 0x01000193u * (3u * i + 2u);
        mi[i].rng = 0x2545F491u + 0x01000193u * (i + 1u);   /* (24: MICRO's noise) */
        if (i == V_SNARE)
            v->mod.wave = W_NOISE;                   /* the snare's noise (Snare_calcSyncBlock: calcNoiseBlock) */
        v->fm1 = v->fm2 = 16384;
        v->peg_amt = 32768;
        v->tr.inc = 1u << 20;
        v->tr.vol = 32767;
        v->pmod = 65536u;
        v->vol_mod = 1;
        v->mix_oscs = 1;
        v->ftype = F_LP;
        v->aeg.atk = v->aeg.dec = 167772;            /* 0.01 */
        v->aeg.k = v->aeg.ik = 0;
        v->peg.dec = 167772;
        v->dk = dist_k(0);
        v->dks = v->dk << 8;
        v->vol_s = v->vol << 9;
        v->route = R_ST1;
        v->pan = 63;
        v->tilt = 64;
        v->rp = 65536u;
        v->dec_base = v->aeg.dec;
        svf_init(&v->flt);
        v->flt.line = flt_line[i];
        v->lfo.rate = 65536u;
        lfo_update_inc(&v->lfo);
        v->lfo2.rate = 65536u;
        lfo_update_inc(&v->lfo2);
        memset(&ins[i], 0, sizeof ins[i]);
        memset(ins_line[i], 0, sizeof ins_line[i]);
        osc_recalc(&v->osc);
        osc_recalc(&v->mod);
        osc_recalc(&v->mod2);
        voice_pan_latch(v);
        decim_rate[i] = 65536u;
    }
    decim_rate[NVOICE] = 65536u;
}
