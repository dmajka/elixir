/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: kits. 64 kit slots in RAM (the LXR's mainParameters: a kit is its sound parameters, the .SND layout, then
 * Kettle's: KIT_LEN, kit_par), each with an 8-character name; the edit buffer is par[] (sound.c). LOAD copies a slot
 * into it, SAVE copies it (or, SAVE MORPH, the morph's mixture) into a slot; a project (project.c) keeps all of them in
 * flash. Ported from the LXR firmware, Core/Src/Preset (presetManager.c, initKit.c), Copyright (C) 2012-2026 Erica
 * Synths, GPL-3.0-only. */

#define NUM_KITS 64
#define KIT_NAME_LEN 8
static uint8_t kits[NUM_KITS][KIT_LEN] POOL;       /* (21 KB: the pool, the fast RAM kept for the ISR) */
static char kit_names[NUM_KITS][KIT_NAME_LEN];
static uint8_t kit_loaded;                           /* the slot the edit buffer came from (menu_loadedPresetNr) */
static uint8_t kit_morph_loaded;                     /* the slot loaded as the morph target */

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

/* Kettle's parameters in the init kit: 0 (no sends, no shaping), but the reverb (a medium ROOM, LEVEL 100) and the
 * tilt EQs (64: flat) */
static uint32_t init_kit_k(uint32_t p)
{
    if (p >= PAR_TILT1 && p < PAR_TILT1 + 6u)
        return 64;
    if (p >= PAR_MDECAY1 && p < NPAR) {              /* the models' knobs: a medium drum, mid-way struck */
        static const uint8_t D[6] = {64, 40, 64, 40, 32, 0};
        return D[(p - PAR_MDECAY1) / 6u];
    }
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
}

/* preset_loadDrumsetRam: a slot into the edit buffer (the morph target stays) */
static void kit_load_ram(uint32_t n)
{
    uint32_t i;
    if (n >= NUM_KITS)
        return;
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = kits[n][i];
    kit_loaded = (uint8_t)n;
    snd_post_all();
}
/* preset_saveDrumsetRam: the edit buffer, or with morph the mixture that sounds, into a slot */
static void kit_save_ram(uint32_t n, int morph)
{
    uint32_t i;
    if (n >= NUM_KITS)
        return;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t p = kit_par(i);
        kits[n][i] = morph && morph_amt && !morph_excluded(p) ? (uint8_t)morph_lerp(par[p], morph_par[i], morph_amt)
                                                               : par[p];
    }
    kit_loaded = (uint8_t)n;
}
static void kit_load_morph(uint32_t n)               /* LOAD MORPH: a slot as the morph target */
{
    uint32_t i;
    if (n >= NUM_KITS)
        return;
    for (i = 0; i < KIT_LEN; i++)
        morph_par[i] = kits[n][i];
    kit_morph_loaded = (uint8_t)n;
    snd_post_all();
}
static void kit_set_morph(uint32_t amt)              /* PAR_MORPH 0..255 */
{
    if (morph_amt == amt)
        return;
    morph_amt = (uint8_t)amt;
    par[PAR_MORPH] = (uint8_t)amt;
    snd_post_all();
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
/* a kit (kit layout) as Kettle reads it: the modulation targets indexes of MOD_TARGETS (preset_readDrumsetData's
 * check), no Out = FX BUS, Kettle's values in range */
static void kit_sanitize(uint8_t *p)
{
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        if (p[PAR_VEL_DEST_1 + i] >= NUM_MOD_TARGETS)
            p[PAR_VEL_DEST_1 + i] = 0;
        if (p[PAR_TARGET_LFO1 + i] >= NUM_MOD_TARGETS)
            p[PAR_TARGET_LFO1 + i] = 0;
    }
    out_migrate(&p[PAR_AUDIO_OUT1], &p[kit_idx(PAR_FX_SEND1)]);
    for (i = END_OF_SOUND_PARAMETERS; i < KIT_LEN; i++)
        if (p[i] > par_max_k(kit_par(i)))
            p[i] = (uint8_t)init_kit_k(kit_par(i));
}

#if KETTLE_FACTORY_KITS
/* 0.7: a bank of LXR-02 kits built in from a local folder (tools/gen_factory_kits.py; never in a public build: the
 * kits aren't Kettle's to give away). FACTORY_KITS[f]: the .SND file (8 name bytes, the LXR's 247). A fresh unit
 * starts with the first 64 in its slots; LOAD > FACTORY loads one into the edit buffer, or all into the slots. */
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
        kit_factory_values(i, kits[i]);
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
    snd_post_all();
}
#endif
