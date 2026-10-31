/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: kits. 64 kit slots in RAM (the LXR's mainParameters: a kit is its sound parameters, the .SND layout, then
 * Elixir's: KIT_LEN, kit_par), each with an 8-character name; the edit buffer is par[] (sound.c). LOAD copies a slot
 * into it, SAVE copies it (or, SAVE MORPH, the morph's mixture) into a slot; a project (project.c) keeps all of them in
 * flash. Ported from the LXR firmware, Core/Src/Preset (presetManager.c, initKit.c), Copyright (C) 2012-2026 Erica
 * Synths, GPL-3.0-only. */

#define NUM_KITS 64
#define KIT_NAME_LEN 8
/* 25-0: the slots packed, each value in its parameter's own bits (par_max: 7 for most, 1..4 for the switches, 8 for
 * the targets): 288 of KIT_LEN's 398 bytes, 7 KB of pool back. Only here: the edit buffer, the morph target, projects,
 * the library and LXR files keep a byte a value. kit_get / kit_put a whole kit; kit_load_ram unpacks straight into
 * par[] (it runs in the audio ISR too: a pattern's kit, MIDI's bank select), with no buffer shared with the main loop */
#define KIT_PK 344                                   /* (a slot's bytes: kit_pk_init checks the widths fit) */
#define KIT_SHARED NUM_KITS                          /* 37: a row past the slots: the shared voices (seq.c PV_*) */
static uint8_t kits[NUM_KITS + 1][KIT_PK] POOL;      /* (the pool, the fast RAM kept for the ISR) */
static uint8_t kit_w[KIT_LEN] POOL;                  /* each value's bits (kit_pk_init) */
static uint16_t kit_pk_bits;
static char kit_names[NUM_KITS][KIT_NAME_LEN];
static uint8_t kit_loaded;                           /* the slot the edit buffer came from (menu_loadedPresetNr) */
static uint8_t kit_morph_loaded;                     /* the slot loaded as the morph target */
static uint8_t kit_loads;                            /* 0.11: one more at each kit into the edit buffer or the morph
                                                      * target (Control All) */

/* initKit.c: "Initkit" from hrtlkits_batch3 (name, then the parameters 0..245) */
static const uint8_t INIT_KIT[254] = {
    0x49, 0x6e, 0x69, 0x74, 0x6b, 0x69, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x1f, 0x3f,
    0x29, 0x3f, 0x3a, 0x3f, 0x2d, 0x40, 0x7f, 0x3f, 0x32, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f,
    0x3d, 0x00, 0x00, 0x00, 0x00, 0x38, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x44, 0x68, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x0d, 0x00, 0x03, 0x00, 0x0a, 0x00, 0x6b, 0x00, 0x05, 0x11, 0x2e, 0x0b,
    0x04, 0x0a, 0x00, 0x06, 0x00, 0x00, 0x1a, 0x5d, 0x07, 0x0e, 0x2c, 0x0e, 0x7f, 0x51, 0x16, 0x0a, 0x0e, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x3f, 0x3f, 0x3f, 0x00, 0x00, 0x3f,
    0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00};

/* Elixir's parameters in the init kit: 0 (no sends, no shaping), but the reverb (a medium ROOM, LEVEL 100) and the
 * tilt EQs (64: flat) */
static uint32_t init_kit_k(uint32_t p)
{
    if (p >= PAR_TILT1 && p < PAR_TILT1 + 6u)
        return 64;
    if (p >= PAR_MDECAY1 && p < PAR_LFOMODE1) {      /* the models' knobs (0.9: Rings'): a medium decay, harmonic */
        static const uint8_t D[6] = {64, 64, 32, 40, 0, 0};
        return D[(p - PAR_MDECAY1) / 6u];
    }
    if (p >= PAR_MC_THR && p <= PAR_MC_MIX) {         /* (0.16: the compressor: -12 dB, ~1 ms, ~0.2 s, all wet) */
        static const uint8_t C[6] = {96, 40, 64, 0, 0, 127};
        return C[p - PAR_MC_THR];
    }
    if ((p >= PAR_INSA1 && p < NPAR) || (p >= PAR_L2RATE1 && p < PAR_L2RATE1 + 6u))
        return 64;                                   /* (27a: the insert's knobs, its type OFF; 25b: LFO 2's rate) */
    return p == PAR_REV_SIZE ? 64u : p == PAR_REV_DAMP ? 48u : p == PAR_REV_LEVEL ? 100u : 0u;
}

static void kit_init_values(uint8_t *p)              /* the init kit (CLEAR KIT), kit layout */
{
    uint32_t i;
    for (i = 0; i < KIT_LEN; i++)
        p[i] = (uint8_t)(i >= END_OF_SOUND_PARAMETERS ? init_kit_k(kit_par(i))
                         : i + 8u < sizeof INIT_KIT ? INIT_KIT[i + 8u] : 0u);
}
static void kit_init_par(void)                       /* the init kit into the edit buffer */
{
    uint8_t k[KIT_LEN];
    uint32_t i;
    kit_init_values(k);
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k[i];
    kit_loads++;
}

/* 25-0: the packing. Its widths from the parameters' largest values (params.c par_max, sound.c par_max_k), at boot */
static uint32_t par_max(uint32_t p);
static int kit_pk_init(void)                         /* 0: the packed kit fits KIT_PK */
{
    uint32_t i, t = 0;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t p = kit_par(i), m = p < NUM_PARAMS ? par_max(p) : par_max_k(p), w = 0;
        while (w < 8u && (1u << w) <= m)
            w++;
        kit_w[i] = (uint8_t)w;
        t += w;
    }
    kit_pk_bits = (uint16_t)t;
    return t <= KIT_PK * 8u ? 0 : -1;
}
static void kit_pack(uint32_t n, const uint8_t *k)   /* a kit (kit layout) into row n; a value past its bits: its most */
{
    uint8_t *d = kits[n];
    uint32_t i, acc = 0, b = 0, o = 0;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t w = kit_w[i], v = k[i], top = (1u << w) - 1u;
        acc |= (v > top ? top : v) << b;
        b += w;
        while (b >= 8u) {
            d[o++] = (uint8_t)acc;
            acc >>= 8;
            b -= 8u;
        }
    }
    if (b)
        d[o++] = (uint8_t)acc;
    while (o < KIT_PK)
        d[o++] = 0;
}
static void kit_put(uint32_t n, const uint8_t *k)    /* .. into slot n */
{
    if (n < NUM_KITS)
        kit_pack(n, k);
}
#define KIT_UNPACK(n, put) do { const uint8_t *s_ = kits[n]; uint32_t i_, acc_ = 0, b_ = 0, o_ = 0; \
        for (i_ = 0; i_ < KIT_LEN; i_++) { uint32_t w_ = kit_w[i_]; \
            while (b_ < w_) { acc_ |= (uint32_t)s_[o_++] << b_; b_ += 8u; } \
            put(i_, acc_ & ((1u << w_) - 1u)); acc_ >>= w_; b_ -= w_; } } while (0)
#define KIT_TO_BUF(i, v) (out[i] = (uint8_t)(v))
#define KIT_TO_PAR(i, v) (par[kit_par(i)] = (uint8_t)(v))
static void kit_get(uint32_t n, uint8_t *out)        /* slot n as a kit (kit layout) */
{
    if (n >= NUM_KITS) {
        kit_init_values(out);
        return;
    }
    KIT_UNPACK(n, KIT_TO_BUF);
}

static void kit_get_row(uint32_t n, uint8_t *out)    /* 37: row n (KIT_SHARED too) as a kit */
{
    if (n > KIT_SHARED) {
        kit_init_values(out);
        return;
    }
    KIT_UNPACK(n, KIT_TO_BUF);
}

/* preset_loadDrumsetRam: a slot into the edit buffer (the morph target stays) */
static void kit_load_ram(uint32_t n)
{
    if (n >= NUM_KITS)
        return;
    KIT_UNPACK(n, KIT_TO_PAR);
    kit_loaded = (uint8_t)n;
    kit_loads++;
    snd_post_all();
}
/* 37: the edit buffer as it is (no morph) into row n (a slot, or KIT_SHARED), and back; kit_loaded stays */
static void kit_stash(uint32_t n)                    /* (straight from par[]: it runs in the audio ISR, no buffer) */
{
    uint8_t *d = kits[n <= KIT_SHARED ? n : KIT_SHARED];
    uint32_t i, acc = 0, b = 0, o = 0;
    if (n > KIT_SHARED)
        return;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t w = kit_w[i], v = par[kit_par(i)], top = (1u << w) - 1u;
        acc |= (v > top ? top : v) << b;
        b += w;
        while (b >= 8u) {
            d[o++] = (uint8_t)acc;
            acc >>= 8;
            b -= 8u;
        }
    }
    if (b)
        d[o++] = (uint8_t)acc;
    while (o < KIT_PK)
        d[o++] = 0;
}
static void kit_unstash(uint32_t n)
{
    if (n > KIT_SHARED)
        return;
    KIT_UNPACK(n, KIT_TO_PAR);
    kit_loads++;
    snd_post_all();
}
/* preset_saveDrumsetRam: the edit buffer, or with morph the mixture that sounds, into a slot */
static void kit_save_ram(uint32_t n, int morph)
{
    uint8_t k[KIT_LEN];
    uint32_t i;
    if (n >= NUM_KITS)
        return;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t p = kit_par(i);
        k[i] = morph && morph_amt && !morph_excluded(p) ? (uint8_t)morph_mix(p, par[p], morph_par[i], morph_amt)
                                                        : par[p];
    }
    kit_put(n, k);
    kit_loaded = (uint8_t)n;
}
static void kit_load_morph(uint32_t n)               /* LOAD MORPH: a slot as the morph target */
{
    if (n >= NUM_KITS)
        return;
    kit_get(n, morph_par);
    kit_morph_loaded = (uint8_t)n;
    kit_loads++;
    snd_post_all();
}
/* 0.13: morph as it sounds (morph_amt): the knob (par[PAR_MORPH]) and the global LFOs' push (morph_mod, -255 .. 255:
 * perform.c glfo_morph) */
static int32_t morph_mod;
static void morph_update(void)
{
    uint32_t a = (uint32_t)clamp32((int32_t)par[PAR_MORPH] + morph_mod, 0, 255);
    if (morph_amt == a)
        return;
    morph_amt = (uint8_t)a;
    snd_post_morph();                                /* (0.15: paced, sound.c snd_morph_sweep) */
}
static void kit_set_morph(uint32_t amt)              /* PAR_MORPH 0..255 */
{
    par[PAR_MORPH] = (uint8_t)amt;
    morph_update();
}

/* a voice's Out = FX BUS (the LXR's, a kit from before the sends) becomes its output STEREO and FX SEND 127: the
 * same sound. out: the six PAR_AUDIO_OUTs, send: the six PAR_FX_SENDs */
static void out_migrate(uint8_t *out, uint8_t *send)
{
    uint32_t i;
    for (i = 0; i < 6u; i++)
        if (out[i] >= 6u) {
            out[i] = 0;
            send[i] = 127;
        }
}
/* a kit (kit layout) as Elixir reads it: the modulation targets indexes of MOD_TARGETS (preset_readDrumsetData's
 * check), no Out = FX BUS, Elixir's values in range */
static void kit_sanitize(uint8_t *p)
{
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        if (!target_ok(p[PAR_VEL_DEST_1 + i]))       /* (25a: and Elixir's after them) */
            p[PAR_VEL_DEST_1 + i] = 0;
        if (!target_ok(p[PAR_TARGET_LFO1 + i]))
            p[PAR_TARGET_LFO1 + i] = 0;
    }
    for (i = 0; i < 6u; i++)                         /* (25-0: a transient wave past the table plays as 0: 0 in the */
        if (p[PAR_TRANS1_WAVE + i] >= LXR_TRANS_N + 2u)  /* packed slot. Some LXR kits have 24) */
            p[PAR_TRANS1_WAVE + i] = 0;
    out_migrate(&p[PAR_AUDIO_OUT1], &p[kit_idx(PAR_FX_SEND1)]);
    for (i = 3; i < 6u; i++)                         /* (24: SN-HH have the LXR's engine and MICRO's types) */
        if (!model_ok(i, p[kit_idx(PAR_MODEL1 + i)]))
            p[kit_idx(PAR_MODEL1 + i)] = 0;
    for (i = END_OF_SOUND_PARAMETERS; i < KIT_LEN; i++)
        if (p[i] > par_max_k(kit_par(i)))
            p[i] = (uint8_t)init_kit_k(kit_par(i));
}

/* 0.7: a bank of LXR-02 kits built in (tools/gen_factory_kits.py); from 0.19 the LXR-02's factory kits, always (Erica
 * Synths' permission). FACTORY_KITS[f]: the .SND file (8 name bytes, the LXR's 247). The kit browser and LOAD >
 * FACTORY load one into the edit buffer; LOAD > FACTORY's ALL > SLOTS puts the first 64 into the slots (a fresh unit
 * no longer starts with them there: 0.19). */
#include "factory_kits.h"
static void kit_factory_values(uint32_t f, uint8_t *p)
{
    uint32_t i;
    kit_init_values(p);
    for (i = 0; i < END_OF_SOUND_PARAMETERS; i++)
        p[i] = FACTORY_KITS[f][8u + i];
    kit_sanitize(p);
}
static void kit_factory_name(uint32_t f, char *b)
{
    uint32_t i;
    for (i = 0; i < KIT_NAME_LEN; i++) {
        uint8_t c = FACTORY_KITS[f][i];
        b[i] = (char)(c >= 32u && c < 127u ? c : ' ');
    }
}
static void kit_factory_fill(void)                   /* the bank's first 64 into the slots */
{
    uint32_t i;
    for (i = 0; i < NUM_KITS && i < FACTORY_NKITS; i++) {
        uint8_t k[KIT_LEN];
        kit_factory_values(i, k);
        kit_put(i, k);
        kit_factory_name(i, kit_names[i]);
    }
}
static void kit_factory_load(uint32_t f)             /* one into the edit buffer (the slot stays as it was) */
{
    uint8_t k[KIT_LEN];
    uint32_t i;
    if (f >= FACTORY_NKITS)
        return;
    kit_factory_values(f, k);
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k[i];
    kit_loads++;
    snd_post_all();
}
