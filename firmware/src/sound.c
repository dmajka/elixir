/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the sound parameters. par[] is the LXR's activeParameters (the edit buffer: a kit's sound parameters,
 * then the step and global ones, same numbers as the LXR's ParamEnums, so a kit is the LXR's .SND data); snd_apply
 * sets the voices from a value as midiParser_ccHandler does (Core/Src/Soundengine/MIDI/MidiParser.c); the
 * modulation nodes (DSPAudio/modulationNode.c: velocity and LFO, one each per voice) and the sequencer's
 * automation (automationNode.c) move a parameter away from its value for a while; MORPH crossfades every sound
 * parameter towards a second kit (Preset/presetManager.c preset_morph). Ported from the LXR firmware,
 * Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only.
 *
 * Threads: the audio ISR owns the voices. The main loop writes par[] and posts the number (snd_post); the ISR
 * applies what was posted at the start of its next block (snd_service). The sequencer and MIDI input run in the
 * ISR and apply at once. */

/* Kettle's own sound parameters, after the LXR's numbering (NRPN n - 128 reaches them): each voice's sends to the
 * FX bus and to the reverb, and the reverb (0.2); each voice's sound shaping (0.3: voices.c). They belong to the kit
 * as the LXR's sound parameters do: a kit stores them after its END_OF_SOUND_PARAMETERS (KIT_LEN, kit_par), morph
 * moves them, a step locks them (all but the reverb's model). Six of each per-voice one, in voice order. */
enum {
    PAR_FX_SEND1 = NUM_PARAMS,                   /* how much of the voice goes through the FX bus (127: all of it,
                                                  * as the LXR's Out = FX BUS) */
    PAR_REV_SEND1 = PAR_FX_SEND1 + 6,            /* the voice into the reverb */
    PAR_REV_SIZE = PAR_REV_SEND1 + 6,
    PAR_REV_DAMP,
    PAR_REV_LEVEL,                               /* the reverb's return */
    PAR_REV_TYPE,                                /* ROOM SPRING HALL (not lockable) */
    PAR_BITS1,                                   /* bit reduction 0 (16 bits) .. 15 (1 bit) */
    PAR_SHAPE1 = PAR_BITS1 + 6,                  /* the waveshaper: the LXR's, FOLD, CLIP, ASYM, RECTIFY */
    PAR_TILT1 = PAR_SHAPE1 + 6,                  /* tilt EQ, 64 flat */
    PAR_CHOKE1 = PAR_TILT1 + 6,                  /* choke group 0 (none) .. 4 */
    PAR_RND_PITCH1 = PAR_CHOKE1 + 6,             /* random per hit: pitch, decay, filter, pan */
    PAR_RND_DECAY1 = PAR_RND_PITCH1 + 6,
    PAR_RND_FILTER1 = PAR_RND_DECAY1 + 6,
    PAR_RND_PAN1 = PAR_RND_FILTER1 + 6,
    PAR_XSRC1 = PAR_RND_PAN1 + 6,                /* cross modulation: the source voice (0 none, 1..6) */
    PAR_XMODE1 = PAR_XSRC1 + 6,                  /* .. DUCK, FILTER, RING */
    PAR_XAMT1 = PAR_XMODE1 + 6,                  /* .. its amount */
    PAR_MODEL1 = PAR_XAMT1 + 6,                  /* 0.6: the DRUM voices' physical model (voices.c mdl): 0 the LXR's
                                                  * engine, SKIN (a membrane) BAR BELL PLATE TUBE (six, as the others:
                                                  * 4..6 unused) */
    PAR_MDECAY1 = PAR_MODEL1 + 6,
    PAR_MDAMP1 = PAR_MDECAY1 + 6,
    PAR_MMAT1 = PAR_MDAMP1 + 6,
    PAR_MPOS1 = PAR_MMAT1 + 6,
    PAR_MEXC1 = PAR_MPOS1 + 6,
    PAR_MTENS1 = PAR_MEXC1 + 6,
    NPAR = PAR_MTENS1 + 6
};
#define KIT_LEN (END_OF_SOUND_PARAMETERS + NPAR - NUM_PARAMS)
#define KIT_GAP (NUM_PARAMS - END_OF_SOUND_PARAMETERS)
static inline int par_sound(uint32_t p) { return p < END_OF_SOUND_PARAMETERS || (p >= NUM_PARAMS && p < NPAR); }
static inline uint32_t kit_par(uint32_t i) { return i < END_OF_SOUND_PARAMETERS ? i : i + KIT_GAP; }   /* index -> p */
static inline uint32_t kit_idx(uint32_t p) { return p < END_OF_SOUND_PARAMETERS ? p : p - KIT_GAP; }   /* p -> index */

static uint8_t par[NPAR];                        /* the edit buffer (activeParameters, then Kettle's) */
static uint8_t morph_par[KIT_LEN];               /* the morph target kit (kit layout: kit_par) */
static uint8_t morph_amt;                        /* 0..255 (PAR_MORPH): 0 = the kit as it is */

/* automation (step parameter locks): the parameter each track's two lanes hold, and the value */
#define NO_AUTOMATION 0xFFu
static uint16_t auto_par[NTRACK][3];             /* 0 = none (lanes 0, 1: the step's locks; 2: a link's DECAY) */
static uint8_t auto_val[NTRACK][3];

/* a lock's destination code (12 bits: the step's byte, p1n / p2n, and 4 bits in the pattern's lhi, seq.c). Below
 * 256, the LXR's numbers: the parameter + 1 below 128 (its "stupid offset"), the parameter above, 0xFF none. From
 * 256, Kettle's parameters as they are. (0.2 stored Kettle's in the numbers the LXR leaves free, 247..254 and
 * 221..227; project.c lock_v2 moves a version-2 project's locks over.) */
static uint32_t autodest_to_par(uint32_t d)
{
    if (d == NO_AUTOMATION || d == 0u)
        return 0;
    if (d >= 256u)
        return d < NPAR ? d : 0u;
    return d <= 127u ? d - 1u : d;
}
static uint32_t par_to_autodest(uint32_t p)
{
    if (p >= NUM_PARAMS)
        return p < NPAR && p != PAR_REV_TYPE ? p : NO_AUTOMATION;
    return p == 0u ? NO_AUTOMATION : p < 127u ? p + 1u : p;
}
_Static_assert(NPAR < 4096 && NUM_PARAMS >= 256, "lock codes: 12 bits, Kettle's past the LXR's byte");
#define NLOCK_K (NPAR - NUM_PARAMS - 1u)             /* Kettle's lockable parameters: all but PAR_REV_TYPE */
static inline int par_lockable(uint32_t p)
{
    return par_sound(p) && !(p >= PAR_UNUSED_1 && p <= PAR_UNUSED_7) && par_to_autodest(p) != NO_AUTOMATION;
}
/* the lock targets the STEP page picks from: the LXR's (MOD_TARGETS), then Kettle's (PAR_REV_TYPE left out) */
static uint32_t lock_target_par(uint32_t idx)
{
    uint32_t p;
    if (idx < NUM_MOD_TARGETS)
        return MOD_TARGETS[idx][1];
    if (idx >= NUM_MOD_TARGETS + NLOCK_K)
        return 0;
    p = NUM_PARAMS + idx - NUM_MOD_TARGETS;
    return p >= PAR_REV_TYPE ? p + 1u : p;
}
static uint32_t lock_target_idx(uint32_t p)          /* (Kettle's parameter p: its index there) */
{
    return NUM_MOD_TARGETS + p - NUM_PARAMS - (p > PAR_REV_TYPE ? 1u : 0u);
}
/* the largest value of Kettle's parameters */
static uint32_t par_max_k(uint32_t p)
{
    if (p == PAR_REV_TYPE)
        return 2;
    if (p >= PAR_BITS1 && p < PAR_BITS1 + 6u)
        return 15;
    if (p >= PAR_SHAPE1 && p < PAR_SHAPE1 + 6u)
        return NSHAPE - 1u;
    if (p >= PAR_CHOKE1 && p < PAR_CHOKE1 + 6u)
        return 4;
    if (p >= PAR_XSRC1 && p < PAR_XSRC1 + 6u)
        return 6;
    if (p >= PAR_XMODE1 && p < PAR_XMODE1 + 6u)
        return NXMODE - 1u;
    if (p >= PAR_MODEL1 && p < PAR_MODEL1 + 6u)
        return NMODEL - 1u;
    return 127;
}

/* LXR presetManager.c interpolate: a + (b - a) x / 256, rounded */
static uint32_t morph_lerp(uint32_t a, uint32_t b, uint32_t x)
{
    uint32_t f = (uint32_t)((int32_t)(a * 256u) + ((int32_t)b - (int32_t)a) * (int32_t)x) & 0xFFFFu;
    return (f & 0xFFu) < 0x7Fu ? f >> 8 : (f >> 8) + 1u;
}
static int morph_excluded(uint32_t p)            /* the outputs and the modulation targets stay the kit's */
{
    return (p >= PAR_AUDIO_OUT1 && p <= PAR_AUDIO_OUT6) || (p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) ||
           (p >= PAR_VOICE_LFO1 && p <= PAR_TARGET_LFO6) || p == PAR_FX_TYPE || p == PAR_MORPH_KIT_DEST ||
           p == PAR_REV_TYPE || (p >= PAR_MODEL1 && p < PAR_MODEL1 + 6u);
}

/* the value a parameter sounds with: an automation lock, else the kit's (morphed) */
static uint32_t par_base(uint32_t p)
{
    uint32_t t, k;
    for (t = 0; t < NTRACK; t++)
        for (k = 0; k < 3u; k++)
            if (auto_par[t][k] == p && p)
                return auto_val[t][k];
    if (par_sound(p) && morph_amt && !morph_excluded(p))
        return morph_lerp(par[p], morph_par[kit_idx(p)], morph_amt);
    return par[p];
}

/* ------------------------------------------------------------------ apply --- */
static const uint8_t COARSE_P[NVOICE] = {PAR_COARSE1, PAR_COARSE2, PAR_COARSE3, PAR_COARSE4, PAR_COARSE5, PAR_COARSE6};

static void set_coarse(osc_t *o, uint32_t itg)
{
    o->midi_freq = (uint16_t)((o->midi_freq & 0x00FFu) | (itg & 0xFFu) << 8);
    osc_recalc(o);
}
static void set_fine(osc_t *o, uint32_t itg)
{
    o->midi_freq = (uint16_t)((o->midi_freq & 0xFF00u) | (itg & 0xFFu));
    osc_recalc(o);
}
/* a waveform, a filter type ..: the setting, or (modulated) n x the 0..1 value */
static uint32_t pick(uint32_t itg, uint32_t q16, int mod, uint32_t n)
{
    if (mod) {
        uint32_t w = (uint32_t)(((uint64_t)n * q16) >> 16);
        return w >= n ? n - 1u : w;
    }
    return itg;
}

/* midiParser_ccHandler: the parameter p at the value itg (0..127, its menu index, ..) / q16 (0..1, Q16);
 * mod: a modulator's value (the LXR's isFloatModulator) */
static void snd_apply(uint32_t p, uint32_t itg, uint32_t q16, int mod)
{
    int32_t f = (int32_t)(q16 > 65536u ? 65536u : q16);
    uint32_t i;
    if (p >= PAR_COARSE1 && p <= PAR_FINE6) {                        /* coarse / fine of the main oscillators */
        osc_t *o = &vc[(p - PAR_COARSE1) >> 1].osc;
        if ((p - PAR_COARSE1) & 1u)
            set_fine(o, itg);
        else
            set_coarse(o, itg);
        return;
    }
    if (p >= PAR_FILTER_FREQ_1 && p <= PAR_FILTER_FREQ_6) {
        vc[p - PAR_FILTER_FREQ_1].flt.fval = (uint32_t)f;
        vc[p - PAR_FILTER_FREQ_1].flt.dirty = 1;
        return;
    }
    if (p >= PAR_RESO_1 && p <= PAR_RESO_6) {                        /* SVF_setReso: q = 1 - r (0.02 below 0.1) */
        int32_t q = 65536 - f;
        vc[p - PAR_RESO_1].flt.q = q < 6554 ? 1311 : q;
        vc[p - PAR_RESO_1].flt.dirty = 1;
        return;
    }
    if (p >= PAR_VELOA1 && p <= PAR_VELOD6_OPEN) {
        static const uint8_t VI[13] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5};
        uint32_t k = p - PAR_VELOA1;
        voice_t *v = &vc[VI[k]];
        itg &= 127u;
        if (k == 11u)
            v->dec_closed = EG_DEC[itg];
        else if (k == 12u)
            v->dec_open = EG_DEC[itg];
        else if (k & 1u)
            v->aeg.dec = v->dec_base = EG_DEC[itg];
        else
            v->aeg.atk = EG_ATK[itg];
        return;
    }
    if (p >= PAR_VOL_SLOPE1 && p <= PAR_VOL_SLOPE6) {
        slope_eg_t *e = &vc[p - PAR_VOL_SLOPE1].aeg;
        e->k = EG_SLOPE[itg & 127u];
        e->ik = EG_ISLOPE[itg & 127u];
        return;
    }
    if (p >= PAR_MOD_EG1 && p <= PAR_MOD_EG4) {                      /* the pitch EG's decay (DecayEg_setDecay) */
        vc[p - PAR_MOD_EG1].peg.dec = EG_ATK[itg & 127u];
        return;
    }
    if (p >= PAR_MODAMNT1 && p <= PAR_MODAMNT4) {
        vc[p - PAR_MODAMNT1].peg_amt = (int32_t)PITCH_AMT[itg & 127u];
        return;
    }
    if (p >= PAR_PITCH_SLOPE1 && p <= PAR_PITCH_SLOPE4) {
        vc[p - PAR_PITCH_SLOPE1].peg.k = DEC_SLOPE[itg & 127u];
        return;
    }
    if (p >= PAR_FMAMNT1 && p <= PAR_FM_FREQ3) {
        voice_t *v = &vc[(p - PAR_FMAMNT1) >> 1];
        if ((p - PAR_FMAMNT1) & 1u)
            set_coarse(&v->mod, itg);
        else
            v->fm1 = f >> 1;
        return;
    }
    if (p >= PAR_VOL1 && p <= PAR_VOL6) {
        vc[p - PAR_VOL1].vol = f >> 1;
        return;
    }
    if (p >= PAR_DRIVE1 && p <= PAR_HAT_DISTORTION) {
        vc[p - PAR_DRIVE1].dk = dist_k((uint32_t)f);
        vc[p - PAR_DRIVE1].drive = f;
        return;
    }
    if (p >= PAR_VOICE_DECIMATION1 && p <= PAR_VOICE_DECIMATION_ALL) {
        decim_rate[p - PAR_VOICE_DECIMATION1] = lut1024u(DECIM, (uint32_t)f);
        return;
    }
    if (p >= PAR_FREQ_LFO1 && p <= PAR_FREQ_LFO6) {
        lfo_t *l = &vc[p - PAR_FREQ_LFO1].lfo;
        l->rate = (uint32_t)f;
        lfo_update_inc(l);
        return;
    }
    if (p >= PAR_AMOUNT_LFO1 && p <= PAR_AMOUNT_LFO6) {
        lfo_mod[p - PAR_AMOUNT_LFO1].amount = f;
        return;
    }
    if (p >= PAR_FILTER_DRIVE_1 && p <= PAR_FILTER_DRIVE_6) {        /* SVF_setDrive: 0.4 + d^2 x 6, Q12 */
        vc[p - PAR_FILTER_DRIVE_1].flt.drive = 1638 + (int32_t)(((int64_t)f * f * 6) >> 20);
        return;
    }
    if (p >= PAR_MIX_MOD_1 && p <= PAR_MIX_MOD_3) {
        vc[p - PAR_MIX_MOD_1].mix_oscs = itg != 0u;
        return;
    }
    if (p >= PAR_VOLUME_MOD_ON_OFF1 && p <= PAR_VOLUME_MOD_ON_OFF6) {
        vc[p - PAR_VOLUME_MOD_ON_OFF1].vol_mod = itg != 0u;
        return;
    }
    if (p >= PAR_VELO_MOD_AMT_1 && p <= PAR_VELO_MOD_AMT_6) {
        velo_mod[p - PAR_VELO_MOD_AMT_1].amount = f;
        return;
    }
    if (p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) {                /* a modTargets index -> the parameter */
        velo_mod[p - PAR_VEL_DEST_1].dest = itg < NUM_MOD_TARGETS ? MOD_TARGETS[itg][1] : 0u;
        return;
    }
    if (p >= PAR_WAVE_LFO1 && p <= PAR_WAVE_LFO6) {
        vc[p - PAR_WAVE_LFO1].lfo.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NLFO);
        return;
    }
    if (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) {
        lfo_mod[p - PAR_TARGET_LFO1].dest = itg < NUM_MOD_TARGETS ? MOD_TARGETS[itg][1] : 0u;
        return;
    }
    if (p >= PAR_RETRIGGER_LFO1 && p <= PAR_RETRIGGER_LFO6) {
        vc[p - PAR_RETRIGGER_LFO1].lfo.retrig = (uint8_t)(mod ? (7u * (uint32_t)f) >> 16 : itg);
        return;
    }
    if (p >= PAR_SYNC_LFO1 && p <= PAR_SYNC_LFO6) {
        lfo_t *l = &vc[p - PAR_SYNC_LFO1].lfo;
        l->sync = (uint8_t)itg;
        lfo_update_inc(l);
        return;
    }
    if (p >= PAR_OFFSET_LFO1 && p <= PAR_OFFSET_LFO6) {
        vc[p - PAR_OFFSET_LFO1].lfo.offset = f >= 65536 ? 0xFFFFFFFFu : (uint32_t)f << 16;
        return;
    }
    if (p >= PAR_FILTER_TYPE_1 && p <= PAR_FILTER_TYPE_6) {
        uint32_t t = mod ? ((uint32_t)(((uint64_t)F_LP2 * (uint32_t)f) >> 16) + 1u) : itg + 1u;
        vc[p - PAR_FILTER_TYPE_1].ftype = (uint8_t)(mod && t >= F_OFF ? F_OFF - 1u : t);
        return;
    }
    if (p >= PAR_TRANS1_VOL && p <= PAR_TRANS6_VOL) {
        vc[p - PAR_TRANS1_VOL].tr.vol = f >> 1;
        return;
    }
    if (p >= PAR_TRANS1_WAVE && p <= PAR_TRANS6_WAVE) {              /* transient_setWaveform */
        uint32_t w = pick(itg, (uint32_t)f, mod, LXR_TRANS_N);
        vc[p - PAR_TRANS1_WAVE].tr.wave = (uint8_t)(w < LXR_TRANS_N + 2u ? w : 0u);
        return;
    }
    if (p >= PAR_TRANS1_FREQ && p <= PAR_TRANS6_FREQ) {              /* 1 + (v / 33.9 - 0.75): x0.25 .. x4 */
        vc[p - PAR_TRANS1_FREQ].tr.inc = 262144u + (((itg & 127u) << 20) * 10u) / 339u;
        return;
    }
    if (p >= PAR_AUDIO_OUT1 && p <= PAR_AUDIO_OUT6) {          /* (FX BUS: a kit from before the sends) */
        vc[p - PAR_AUDIO_OUT1].route = (uint8_t)(itg < R_FX ? itg : R_ST1);
        vc[p - PAR_AUDIO_OUT1].out_fx = itg == R_FX;
        return;
    }
    if (p >= PAR_RND_PITCH1 && p < PAR_XSRC1) {                     /* random per hit: the amounts, 0..127 */
        uint32_t k = p - PAR_RND_PITCH1;
        vc[k % 6u].rnd[k / 6u] = (uint8_t)(itg & 127u);
        return;
    }
    if (p >= PAR_FX_SEND1 && p < PAR_FX_SEND1 + NVOICE) {
        vc[p - PAR_FX_SEND1].fx_send = f >> 1;
        return;
    }
    if (p >= PAR_REV_SEND1 && p < PAR_REV_SEND1 + NVOICE) {
        vc[p - PAR_REV_SEND1].rev_send = f >> 1;
        return;
    }
    if (p >= PAR_PWM_1 && p <= PAR_PWM_6) {
        vc[p - PAR_PWM_1].pwm = (uint32_t)f;
        return;
    }
    if (p >= PAR_FX_P1 && p <= PAR_FX_P4) {                          /* FxBus_setParameter */
        fx.p[p - PAR_FX_P1] = f;
        fxbus_update();
        return;
    }
    switch (p) {
    case PAR_OSC_WAVE_DRUM1:
    case PAR_OSC_WAVE_DRUM2:
    case PAR_OSC_WAVE_DRUM3:
        vc[p - PAR_OSC_WAVE_DRUM1].osc.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_OSC_WAVE_SNARE:
        vc[V_SNARE].osc.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_WAVE1_CYM:
        vc[V_CYM].osc.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_WAVE1_HH:
        vc[V_HAT].osc.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_MOD_WAVE_DRUM1:
    case PAR_MOD_WAVE_DRUM2:
    case PAR_MOD_WAVE_DRUM3:
        vc[p - PAR_MOD_WAVE_DRUM1].mod.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_WAVE2_CYM:
        vc[V_CYM].mod.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_WAVE3_CYM:
        vc[V_CYM].mod2.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_WAVE2_HH:
        vc[V_HAT].mod.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_WAVE3_HH:
        vc[V_HAT].mod2.wave = (uint8_t)pick(itg, (uint32_t)f, mod, NWAVE);
        break;
    case PAR_NOISE_FREQ1:                                            /* the snare's noise: v x 22 kHz */
        vc[V_SNARE].mod.note_inc = (uint32_t)(((uint64_t)f * 22000u * 97391u) >> 16);
        break;
    case PAR_MIX1:
        vc[V_SNARE].mix = f >> 1;
        break;
    case PAR_MOD_OSC_F1_CYM:
        set_coarse(&vc[V_CYM].mod, itg);
        break;
    case PAR_MOD_OSC_F2_CYM:
        set_coarse(&vc[V_CYM].mod2, itg);
        break;
    case PAR_MOD_OSC_GAIN1_CYM:
        vc[V_CYM].fm1 = f >> 1;
        break;
    case PAR_MOD_OSC_GAIN2_CYM:
        vc[V_CYM].fm2 = f >> 1;
        break;
    case PAR_MOD_OSC_F1:
        set_coarse(&vc[V_HAT].mod, itg);
        break;
    case PAR_MOD_OSC_F2:
        set_coarse(&vc[V_HAT].mod2, itg);
        break;
    case PAR_MOD_OSC_GAIN1:
        vc[V_HAT].fm1 = f >> 1;
        break;
    case PAR_MOD_OSC_GAIN2:
        vc[V_HAT].fm2 = f >> 1;
        break;
    case PAR_REPEAT4:
        vc[V_SNARE].aeg.repeat = (uint8_t)itg;
        break;
    case PAR_REPEAT5:
        vc[V_CYM].aeg.repeat = (uint8_t)itg;
        break;
    case PAR_PAN1:
    case PAR_PAN2:
    case PAR_PAN3:
        vc[p - PAR_PAN1].pan = (uint8_t)itg;
        break;
    case PAR_PAN4:
    case PAR_PAN5:
    case PAR_PAN6:
        vc[p - PAR_PAN4 + 3u].pan = (uint8_t)itg;
        break;
    case PAR_FX_TYPE:
        fx.type = (uint8_t)(itg < NFX ? itg : FX_OFF);
        fxbus_update();
        break;
    case PAR_FX_ROUTING:
        fx.route = (uint8_t)(itg <= R_R2 ? itg : R_ST1);
        break;
    case PAR_FX_DRY_WET:
        i = itg > 100u ? 100u : itg;
        fx.dw = (int32_t)(i * 32768u / 100u);
        break;
    case PAR_FX_DIST_TYPE:
        fx.dist_type = (uint8_t)itg;
        break;
    case PAR_FX_RATIO:
        fx.ratio = (uint8_t)itg;
        break;
    case PAR_FX_WAVE:
        fx.ring_wave = (uint8_t)itg;
        break;
    case PAR_FX_DELAY_TYPE:
        if (fx.delay_type != itg) {
            fx.delay_type = (uint8_t)(itg ? DELAY_PINGPONG : DELAY_NORMAL);
            fxbus_update();
        }
        break;
    case PAR_REV_TYPE:
        rv.type = (uint8_t)(itg < 3u ? itg : 0u);
        break;
    case PAR_REV_SIZE:
        rv.size = (uint8_t)(itg & 127u);
        break;
    case PAR_REV_DAMP:
        rv.damp = (uint8_t)(itg & 127u);
        break;
    case PAR_REV_LEVEL:
        rv.level = f >> 1;
        break;
    case PAR_BITS1: case PAR_BITS1 + 1: case PAR_BITS1 + 2: case PAR_BITS1 + 3: case PAR_BITS1 + 4: case PAR_BITS1 + 5:
        vc[p - PAR_BITS1].bits = (uint8_t)(mod ? (15u * (uint32_t)f) >> 16 : itg > 15u ? 15u : itg);
        break;
    case PAR_SHAPE1: case PAR_SHAPE1 + 1: case PAR_SHAPE1 + 2: case PAR_SHAPE1 + 3: case PAR_SHAPE1 + 4:
    case PAR_SHAPE1 + 5:
        vc[p - PAR_SHAPE1].shape = (uint8_t)pick(itg < NSHAPE ? itg : 0u, (uint32_t)f, mod, NSHAPE);
        break;
    case PAR_TILT1: case PAR_TILT1 + 1: case PAR_TILT1 + 2: case PAR_TILT1 + 3: case PAR_TILT1 + 4: case PAR_TILT1 + 5:
        vc[p - PAR_TILT1].tilt = (uint8_t)(itg & 127u);
        break;
    case PAR_CHOKE1: case PAR_CHOKE1 + 1: case PAR_CHOKE1 + 2: case PAR_CHOKE1 + 3: case PAR_CHOKE1 + 4:
    case PAR_CHOKE1 + 5:
        vc[p - PAR_CHOKE1].choke_grp = (uint8_t)(itg > 4u ? 0u : itg);
        break;
    case PAR_XSRC1: case PAR_XSRC1 + 1: case PAR_XSRC1 + 2: case PAR_XSRC1 + 3: case PAR_XSRC1 + 4: case PAR_XSRC1 + 5:
        vc[p - PAR_XSRC1].xsrc = (uint8_t)(itg > 6u ? 0u : itg);
        break;
    case PAR_XMODE1: case PAR_XMODE1 + 1: case PAR_XMODE1 + 2: case PAR_XMODE1 + 3: case PAR_XMODE1 + 4:
    case PAR_XMODE1 + 5:
        vc[p - PAR_XMODE1].xmode = (uint8_t)pick(itg < NXMODE ? itg : 0u, (uint32_t)f, mod, NXMODE);
        break;
    case PAR_XAMT1: case PAR_XAMT1 + 1: case PAR_XAMT1 + 2: case PAR_XAMT1 + 3: case PAR_XAMT1 + 4: case PAR_XAMT1 + 5:
        vc[p - PAR_XAMT1].xamt = f >> 1;
        break;
    case PAR_MODEL1: case PAR_MODEL1 + 1: case PAR_MODEL1 + 2:   /* (the DRUM voices only) */
        mdl[p - PAR_MODEL1].type = (uint8_t)pick(itg < NMODEL ? itg : 0u, (uint32_t)f, mod, NMODEL);
        break;
    default:
        if (p >= PAR_MDECAY1 && p < NPAR && (p - PAR_MDECAY1) % 6u < 3u) {   /* the model's knobs, 0..127 */
            model_t *m = &mdl[(p - PAR_MDECAY1) % 6u];
            uint8_t v = (uint8_t)(mod ? (127u * (uint32_t)f) >> 16 : itg & 127u);
            switch ((p - PAR_MDECAY1) / 6u) {
            case 0: m->dec = v; break;
            case 1: m->damp = v; break;
            case 2: m->mat = v; break;
            case 3: m->pos = v; break;
            case 4: m->exc = v; break;
            default: m->tens = v; break;
            }
        }
        break;
    }
}

static inline uint32_t q16_of(uint32_t v) { return v >= 127u ? 65536u : v * 65536u / 127u; }
static void snd_refresh(uint32_t p)                 /* apply a parameter's value as it stands */
{
    uint32_t v = par_base(p);
    snd_apply(p, v, q16_of(v), 0);
}

/* ------------------------------------------------- modulation nodes --- */
/* modulationNode.c modNode_updateValue: the target at its value / 127 + amount x (source x 2 - 1), 0..1 */
static uint32_t mod_touched[(NPAR + 31) / 32];
static void modnode_update(modnode_t *m, int32_t val)
{
    int32_t nv;
    m->last = val;
    if (!m->dest || !m->amount)
        return;
    nv = (int32_t)q16_of(par_base(m->dest)) + (int32_t)(((int64_t)m->amount * (2 * val - 65536)) >> 16);
    nv = clamp32(nv, 0, 65536);
    snd_apply(m->dest, (uint32_t)(nv * 127) >> 16, (uint32_t)nv, 1);
    mod_touched[m->dest >> 5] |= 1u << (m->dest & 31u);
}
/* once a block, before the voices: what was modulated goes back to its value, then the velocity and the LFOs move
 * their targets again (modNode_resetTargets, modNode_reassignVeloMod, lfo_dispatchNextValue) */
static void modnodes_block(void)
{
    uint32_t w, i;
    for (w = 0; w < (NPAR + 31) / 32; w++) {
        uint32_t m = mod_touched[w];
        mod_touched[w] = 0;
        while (m) {
            uint32_t b = (uint32_t)__builtin_ctz(m);
            m &= m - 1u;
            snd_refresh(w * 32u + b);
        }
    }
    for (i = 0; i < NVOICE; i++)
        modnode_update(&velo_mod[i], velo_mod[i].last);
    for (i = 0; i < NVOICE; i++)
        modnode_update(&lfo_mod[i], lfo_calc(&vc[i].lfo));
}
/* after modnodes_block: each voice's cutoff moved by its hit's random offset and by its cross-mod source's envelope
 * (XM_FILTER: last block's, voices.c venv), from where the kit, the locks and the LFO put it; back next block */
static void voice_filter_mods(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &vc[i];
        int32_t off = v->rnd_f;
        uint32_t p = PAR_FILTER_FREQ_1 + i;
        if (v->xsrc && v->xsrc != i + 1u && v->xmode == XM_FILTER)
            off += (int32_t)(((int64_t)v->xamt * venv[v->xsrc - 1u]) >> 14);   /* (Q15 x Q15 -> Q16) */
        if (!off)
            continue;
        v->flt.fval = (uint32_t)clamp32((int32_t)v->flt.fval + off, 0, 65536);
        v->flt.dirty = 1;
        mod_touched[p >> 5] |= 1u << (p & 31u);
    }
}

/* ------------------------------------------------- automation nodes --- */
/* automationNode.c: a triggered step's two locks. The lane's last parameter goes back to its value first */
static void auto_set(uint32_t track, uint32_t lane, uint32_t dest, uint32_t val)
{
    uint32_t old = auto_par[track][lane], p = autodest_to_par(dest);
    auto_par[track][lane] = 0;
    if (old)
        snd_refresh(old);
    if (p && par_sound(p)) {
        auto_par[track][lane] = (uint16_t)p;
        auto_val[track][lane] = (uint8_t)val;
        snd_apply(p, val, q16_of(val), 0);
    }
}
static void auto_clear_all(void)
{
    uint32_t t, k;
    for (t = 0; t < NTRACK; t++)
        for (k = 0; k < 3u; k++)
            auto_set(t, k, NO_AUTOMATION, 0);
}

/* ------------------------------------------------- main loop -> ISR --- */
#define SNDQ 256u
static uint16_t sndq[SNDQ];
static volatile uint32_t sndq_w, sndq_r;
static volatile uint8_t snd_all;                    /* refresh every sound parameter (a kit load, morph) */
static void snd_post(uint32_t p)
{
    if (sndq_w - sndq_r >= SNDQ) {
        snd_all = 1;
        return;
    }
    sndq[sndq_w % SNDQ] = (uint16_t)p;
    RING_PUBLISH();
    sndq_w++;
}
static void snd_post_all(void) { snd_all = 1; }

static void seq_tempo_changed(void);               /* (seq.c) */
static void snd_service(void)                       /* the audio ISR, at a block start */
{
    if (snd_all) {
        uint32_t p;
        snd_all = 0;
        sndq_r = sndq_w;
        for (p = 1; p < KIT_LEN; p++)
            snd_refresh(kit_par(p));
        return;
    }
    while (sndq_r != sndq_w) {
        uint32_t p;
        RING_PUBLISH();
        p = sndq[sndq_r % SNDQ];
        sndq_r++;
        if (par_sound(p))
            snd_refresh(p);
    }
}

/* the main loop's write: the edit buffer, then the sound (a sound parameter) */
static void par_set(uint32_t p, uint32_t v)
{
    if (p >= NPAR)
        return;
    par[p] = (uint8_t)v;
    if (par_sound(p))
        snd_post(p);
}
