/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the LXR-02's six voices, their LFOs and the mixer, in fixed point.
 * Ported from the LXR firmware, Core/Src/Soundengine/DSPAudio (DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c,
 * lfo.c, mixer.c), Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only.
 *
 * Voices 0..2 DRUM (main + FM/mix oscillator), 3 SNARE (oscillator + filtered noise), 4 CYMBAL and 5 HIHAT (three
 * operator FM); track 7 (index 6) plays the hi-hat voice with its open decay. Each voice: a transient generator,
 * an amp EG (repeat for claps), a filter, a waveshaper, a sample rate reducer, pan and an output (left/right/both).
 * The FM-1 has one stereo output: the LXR's two stereo pairs and four mono outs all go to it (a mono out to its
 * side), so a kit keeps its routing. Kettle's sends: FX SEND, how much of the voice goes through the FX bus instead
 * of its output (127: all, the LXR's Out = FX BUS, which a kit from before the sends still means), and REVERB
 * SEND, the voice into the reverb (reverb.c) on top of that.
 *
 * Kettle's sound shaping (0.3), each voice: a choice of waveshaper for its DRIVE (the LXR's curve, or FOLD, CLIP,
 * ASYM, RECTIFY: shape_block); after the sample rate reducer a bit reducer and a tilt EQ (post_block); random per
 * hit (pitch, decay, filter, pan: voice_trigger); a choke group (a hit fades the other voices of its group out in a
 * block); cross modulation from another voice (DUCK: its envelope turns this one down, FILTER: opens this one's
 * filter, RING: multiplies the two). The voices render first, then the cross modulation, then the mixer. */

#define NVOICE 6
#define NTRACK 7
enum { V_DRUM1, V_DRUM2, V_DRUM3, V_SNARE, V_CYM, V_HAT };
enum { SH_LXR, SH_FOLD, SH_CLIP, SH_ASYM, SH_RECT, NSHAPE };   /* the waveshapers (shape_block) */
enum { XM_DUCK, XM_FILTER, XM_RING, NXMODE };                  /* the cross modulation's modes (xmod_block) */

typedef struct {
    uint32_t phase, inc;         /* the free running increment (rate or sync) */
    uint32_t offset;             /* retrigger phase */
    int32_t rnd;                 /* S&H, Q16 */
    uint32_t rate;               /* the rate setting 0..1 (Q16), for a tempo change */
    uint8_t wave, retrig, sync;
} lfo_t;

typedef struct {
    uint16_t dest;               /* the parameter, 0 = none */
    int32_t amount;              /* Q16 */
    int32_t last;                /* the last source value, Q16 (0..65536) */
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
    /* Kettle's shaping (sound.c sets them) */
    uint8_t bits, shape, tilt, choke_grp, xsrc, xmode;
    uint8_t rnd[4];              /* random per hit: pitch, decay, filter, pan amounts (0..127) */
    uint8_t choking;             /* choked: fading out (1), silent (2) until its next hit */
    uint8_t throw_rev, throw_fx; /* PERF's THROW: the send at 127 while held (perform.c) */
    int32_t drive;               /* DRIVE 0..1, Q16 (the shapers but the LXR's) */
    int32_t xamt;                /* Q15 */
    uint32_t dec_base;           /* the amp EG's decay as set (random per hit scales aeg.dec from it) */
    uint32_t rp;                 /* this hit's random pitch factor, Q16 */
    int32_t rnd_f;               /* .. its random cutoff offset, Q16 */
    int32_t tilt_lp, sh_dc;      /* the tilt EQ's low-pass, RECTIFY's DC follower */
} voice_t;

static voice_t vc[NVOICE];
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
static void model_trigger(uint32_t vi, uint32_t vel)
{
    model_t *m = &mdl[vi];
    int32_t v15 = (int32_t)(vel * 32767u / 127u), pp = 1311 + m->pos * 245;   /* the strike point 0.02 .. 0.5, Q16 */
    uint32_t k;
    if (m->exc < 64u) {                              /* a pulse: 190 samples (a thump) .. 1 (a click) */
        m->ex_len = 1u + (63u - m->exc) * 3u;
        m->ex_noise = 0;
        m->ex_amp = m->ex_len > 1u ? v15 * 3 / (int32_t)(m->ex_len + 1u) : v15;   /* (its area ~ the click's x 2) */
    } else {                                         /* a noise burst: 1.5 .. 7 ms */
        m->ex_len = 64u + (m->exc - 64u) * 4u;
        m->ex_noise = 1;
        m->ex_amp = v15 * 3 / (int32_t)(1u + m->ex_len / 16u);
    }
    m->ex_n = m->ex_len;
    for (k = 0; k < MD_NMODE; k++) {                 /* POSITION: sin(pi p (k + 1)); harder: the higher modes louder */
        int32_t a = sine_at(((uint32_t)pp * (k + 1u) << 15) + 0x40000000u);   /* (the table: -cos) */
        a = a < 0 ? -a : a;
        a = a < 3277 ? 3277 : a;
        m->amp[k] = (a * (32768 - (32768 - v15) * (int32_t)k / 8)) >> 15;
    }
    m->ten = (int32_t)(m->tens * (uint32_t)v15 >> 7);   /* up to + an octave */
    if (m->type == MD_TUBE) {
        memset(tube_line[vi], 0, sizeof tube_line[vi]);
        m->tlp = 0;
    }
}
/* the block's coefficients from the voice's pitch (inc: its oscillator's, with the pitch EG and random) */
static void model_coefs(model_t *m, uint32_t inc)
{
    uint32_t k, om0 = OM_MS / (uint32_t)((20 * exp2_q16(m->dec * 3922)) >> 16), lim = 1932735283u;  /* 0.45 */
    int32_t mat = 65536 + ((int32_t)m->mat - 64) * 310;   /* MATERIAL: the ratios to the power 0.7 .. 1.3, Q16 */
    if (m->type == MD_BELL || m->type == MD_PLATE)
        om0 >>= 1;                                   /* (they ring twice as long) */
    inc = (uint32_t)(((uint64_t)inc * (uint32_t)(65536 + m->ten)) >> 16);
    for (k = 0; k < MD_NMODE; k++) {
        int32_t l2 = MD_LOG2[(m->type - 1u) & 3u][k], ratio = exp2_q16((int32_t)(((int64_t)l2 * mat) >> 16));
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
    if (m->ex_noise)
        x = (int32_t)(int16_t)(noise_next() >> 16) * m->ex_amp >> 15;
    else
        x = m->ex_len == 1u ? m->ex_amp   /* (the table: -cos; a quarter on, sin) */
                            : (sine_at((m->ex_len - m->ex_n) * (0x80000000u / m->ex_len) + 0x40000000u) * m->ex_amp)
                                  >> 15;
    return x;
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
            y = sat16(((m->tlp * g) >> 15) + (model_exc(m) >> 1));
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
        buf[i] = (int16_t)sat16(out >> 10);
    }
}

static uint32_t decim_rate[NVOICE + 1];          /* [6]: ALL (the PERF page's global reducer). Q16 */
static modnode_t velo_mod[NVOICE], lfo_mod[NVOICE];
static uint32_t seq_bpm_x10 = 1200;               /* the tempo the LFO sync follows */
static volatile uint32_t trig_count[NTRACK];         /* triggers of each track (the UI flashes its key) */
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
    for (i = 0; i < NVOICE; i++)
        lfo_update_inc(&vc[i].lfo);
}
static int32_t lfo_calc(lfo_t *l)                    /* lfo_calc: 0..1, Q16 */
{
    uint32_t old = l->phase;
    l->phase += l->inc;
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
static void lfo_retrigger(uint32_t voice)            /* a voice was triggered: the LFOs that follow it restart */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].lfo.retrig == voice + 1u)
            vc[i].lfo.phase = vc[i].lfo.offset;
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

static void voice_trigger(uint32_t track, uint32_t vel, uint32_t note)
{
    uint32_t vi = track < V_HAT ? track : V_HAT;
    voice_t *v = &vc[vi];
    if (track < NTRACK)
        trig_count[track]++;
    lfo_retrigger(vi);
    modnode_update(&velo_mod[vi], (int32_t)(vel * 65536u / 127u));
    v->velo = (int32_t)(vel * 32768u / 127u);
    if (vi <= V_DRUM3 && mdl[vi].type)                   /* (0.6: a model's strike) */
        model_trigger(vi, vel);
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
    v->rp = v->rnd[0] ? (uint32_t)exp2_q16(hit_rand() * v->rnd[0] * v->rnd[0] * 2 / 16129) : 65536u;   /* (Q16 oct) */
    if (v->rnd[1]) {
        uint32_t base = vi == V_HAT ? v->aeg.dec : v->dec_base;
        int32_t y = hit_rand() * v->rnd[1] / 127 * 2;   /* (Q16 octaves of time) */
        v->aeg.dec = (uint32_t)(((uint64_t)base * (uint32_t)exp2_q16(-y)) >> 16);
    } else if (vi != V_HAT) {
        v->aeg.dec = v->dec_base;
    }
    v->rnd_f = v->rnd[2] ? hit_rand() * v->rnd[2] / 127 : 0;
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

/* ---------------------------------------------------- the block, async --- */
/* calcDrumVoiceAsync / Snare_calcAsync / Cymbal_calcAsync / HiHat_calcAsync: envelopes and pitches, once a block */
static void voice_async(uint32_t vi)
{
    voice_t *v = &vc[vi];
    uint32_t rp = tape_q16 == 65536u ? v->rp : (uint32_t)(((uint64_t)v->rp * tape_q16) >> 16);
    if (vi <= V_DRUM3 || vi == V_SNARE) {
        int32_t pe = decay_calc(&v->peg);                /* Q24 */
        uint32_t pmod = 65536u + (uint32_t)(((int64_t)pe * v->peg_amt) >> 24);
        if (v->tr.wave == TR_SNAP)
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
        if (v->tr.wave == TR_SNAP)
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
 * lower: even harmonics), RECTIFY (full wave, the octave up; its DC followed and taken out) */
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
        default:                                     /* SH_RECT */
            y = iabs(a) * 2;
            v->sh_dc += (y - v->sh_dc) >> 8;
            y = sat16(y - v->sh_dc);
            break;
        }
        buf[i] = (int16_t)y;
    }
}

static void drum_block(voice_t *v, int16_t *buf)     /* calcDrumVoiceSyncBlock */
{
    int16_t mb[BLOCK];
    uint32_t i, vi = (uint32_t)(v - vc);
    int32_t g, step;
    if (vi <= V_DRUM3 && mdl[vi].type) {             /* 0.6: a model, the filter, no amp EG */
        model_block(vi, v, buf);
        svf_block(&v->flt, v->ftype, buf);
        goto shape;
    }
    osc_block(&v->mod, mb, v->fm1, 0, 0, v->pwm);
    if (v->mix_oscs) {
        osc_block(&v->osc, buf, 32768 - v->fm1, 0, 0, v->pwm);
        add_sat(buf, mb);
    } else {
        osc_block(&v->osc, buf, 32768, mb, v->fm2, v->pwm);
    }
    trans_block(&v->tr, mb);
    add_sat(buf, mb);
    svf_block(&v->flt, v->ftype, buf);
    /* the amp EG, from last block's gain to this one's (bufferTool_addGainInterpolated) */
    g = v->last_gain;
    step = (v->gain - v->last_gain) / (BLOCK - 1);
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
static int32_t voice_peak[NVOICE];

static void mix_voice(const voice_t *v, const int16_t *buf)   /* mixer_addDataToOutput, with the sends */
{
    uint32_t i;
    int32_t s = v->out_fx || v->throw_fx ? 32768 : v->fx_send >= 32767 ? 32768 : v->fx_send;
    int32_t rs = v->throw_rev ? 32767 : v->rev_send;
    int16_t dry[BLOCK];
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
            fx_l[i] = (int16_t)sat16(fx_l[i] + ((x * v->pan_l) >> 14));
            fx_r[i] = (int16_t)sat16(fx_r[i] + ((x * v->pan_r) >> 14));
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
            mix_l[i] += buf[i];
        break;
    case R_R1:
    case R_R2:
        for (i = 0; i < BLOCK; i++)
            mix_r[i] += buf[i];
        break;
    default:
        for (i = 0; i < BLOCK; i++) {
            mix_l[i] += (buf[i] * v->pan_l) >> 14;
            mix_r[i] += (buf[i] * v->pan_r) >> 14;
        }
        break;
    }
}

static void fxbus_process(void);                    /* (fxbus.c) */
static void reverb_process(void);                   /* (reverb.c) */
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
    for (i = 0; i < BLOCK; i++) {
        mix_l[i] = mix_r[i] = 0;
        fx_l[i] = fx_r[i] = 0;
        rev_in[i] = 0;
    }
    rev_fed = 0;
    voice_active = 0;
    for (vi = 0; vi < NVOICE; vi++) {                /* render */
        voice_t *v = &vc[vi];
        int16_t *b = vbuf[vi];
        int32_t pk = 0;
        if (vi <= V_DRUM3)
            drum_block(v, b);
        else if (vi == V_SNARE)
            snare_block(v, b);
        else
            metal_block(v, b, vi == V_CYM ? 32768 : 16384);
#ifdef VOICE_CAPTURE
        VOICE_CAPTURE(vi, b);                        /* (host tests: the voice before the mixer) */
#endif
        decimate(v, b, (uint32_t)(((uint64_t)decim_rate[vi] * decim_rate[NVOICE]) >> 16));
        post_block(v, b);
        for (i = 0; i < BLOCK; i += 2) {
            int32_t a = iabs(b[i]);
            if (a > pk)
                pk = a;
        }
        if (v->choking == 2u)
            pk = 0;
        venv[vi] = pk > venv[vi] ? pk : venv[vi] - (venv[vi] >> 5);   /* (release ~45 ms) */
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
        mix_voice(v, b);
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
        }
    fxbus_process();
    reverb_process();
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
        v->lfo.rate = 65536u;
        lfo_update_inc(&v->lfo);
        osc_recalc(&v->osc);
        osc_recalc(&v->mod);
        osc_recalc(&v->mod2);
        voice_pan_latch(v);
        decim_rate[i] = 65536u;
    }
    decim_rate[NVOICE] = 65536u;
}
