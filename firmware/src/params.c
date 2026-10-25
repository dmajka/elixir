/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: what the UI knows of a parameter: its name, its voice, its range and how its value reads. From the
 * LXR's parameter model (lxr_params.h: the names and the value types of menu.c, the pages of menuPages.h, the
 * modulation targets of Cc2Text.c; Copyright (C) 2012-2026 Erica Synths, GPL-3.0-only), with the LXR's 3-letter
 * value lists spelled out for a larger screen; and Kettle's own parameters (sound.c: the sends, the reverb). */

static uint8_t par_text_of[NPAR];                /* TEXT_* (its name), from the pages and the target list */
static uint8_t par_voice_of[NPAR];               /* the voice 0..5 a sound parameter belongs to, 0xFF none */
static uint32_t par_k_voice(uint32_t p);

static uint32_t par_voice(uint32_t p) { return p < NPAR ? par_voice_of[p] : 0xFFu; }

static void params_init(void)
{
    uint32_t pg, s, k, v, i;
    for (i = 0; i < NPAR; i++)
        par_voice_of[i] = par_k_voice(i) < 6u ? (uint8_t)par_k_voice(i) : 0xFFu;
    for (pg = 0; pg < LXR_NPAGES; pg++)
        for (s = 0; s < 8u; s++)
            for (k = 0; k < 8u; k++) {
                uint32_t t = LXR_PAGES[pg][s][k], p = LXR_PAGES[pg][s][8u + k];
                if (p && p < NUM_PARAMS && t && !par_text_of[p])
                    par_text_of[p] = (uint8_t)t;
                if (p && p < END_OF_SOUND_PARAMETERS && pg < 6u && par_voice_of[p] == 0xFFu &&
                    !(p >= PAR_FX_TYPE && p <= PAR_FX_DELAY_TYPE) && p != PAR_TRACK_LENGTH)
                    par_voice_of[p] = (uint8_t)pg;
            }
    for (i = 0; i < NUM_MOD_TARGETS; i++) {
        uint32_t p = MOD_TARGETS[i][1];
        if (p && !par_text_of[p])
            par_text_of[p] = (uint8_t)MOD_TARGETS[i][0];
        for (v = 0; v < 6u; v++)
            if (i >= MOD_TARGET_VOICE[v][0] && i <= MOD_TARGET_VOICE[v][1] && par_voice_of[p] == 0xFFu &&
                !(p >= PAR_FX_TYPE && p <= PAR_FX_DELAY_TYPE))
                par_voice_of[p] = (uint8_t)v;
    }
}

/* -------------------------------------------------------- the lists --- */
static const char *const L_WAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "NOISE", "PWM"};
static const char *const L_FILTER[] = {"LP", "HP", "BP", "UNITY BP", "NOTCH", "PEAK", "LP2", "OFF"};
static const char *const L_OUT[] = {"STEREO", "STEREO 2", "LEFT", "RIGHT", "LEFT 2", "RIGHT 2"};   /* (the LXR's 7th,
                                                  * FX BUS, is FX SEND 127: kit.c out_migrate) */
static const char *const L_ROLL[] = {"ONE", "1/1", "1/2", "1/4", "1/8", "1/16", "1/24", "1/32", "1/48", "1/64",
                                     "1/128", "1/3", "1/6", "1/12"};
static const char *const L_SYNC[] = {"OFF", "4/1", "2/1", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12",
                                     "1/16", "1/32"};
static const char *const L_LFOW[] = {"SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "RANDOM", "EXP UP", "EXP DN"};
static const char *const L_RETRIG[] = {"OFF", "DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "HIHAT"};
static const char *const L_QUANT[] = {"OFF", "1/8", "1/16", "1/32", "1/64"};
static const char *const L_TRANS[] = {"SNAP", "OFFSET", "CLICK", "CLICK 2", "TICK", "KICK", "RIM", "DRIP",
                                      "HAT", "CLAP", "KICK 2", "SNARE", "TOM", "SNAP 2"};
static const char *const L_ROUTE[] = {"OFF", "USB>DIN", "DIN>DIN", "ALL>DIN", "TRS>USB", "TRS>ALL"};
static const char *const L_MFILT[] = {"OFF", "N", "R", "RN", "C", "CN", "CR", "CRN",
                                      "P", "PN", "PR", "PRN", "PC", "PCN", "PCR", "ALL"};
static const char *const L_MIXFM[] = {"FM", "MIX"};
static const char *const L_FX[] = {"OFF", "DRIVE", "RING", "COMP", "DELAY", "PHASER", "FLANGER"};
static const char *const L_DIST[] = {"TUBE", "FOLD", "CLIP"};
static const char *const L_RATIO[] = {"1:1", "2:1", "3:1", "4:1", "6:1", "8:1"};
static const char *const L_DELAY[] = {"MONO", "PING PONG"};
static const char *const L_PATMODE[] = {"OFF", "AUTO"};
static const char *const L_FLAM[] = {"OFF", "x2", "x3", "x4", "x6", "x8"};
static const char *const L_OFFON[] = {"OFF", "ON"};
static const char *const L_REVMODEL[] = {"ROOM", "SPRING", "HALL"};
static const char *const L_SHAPE[] = {"LXR", "FOLD", "CLIP", "ASYM", "RECTIFY"};
static const char *const L_XMODE[] = {"DUCK", "FILTER", "RING"};
static const char *const L_CHOKE[] = {"OFF", "1", "2", "3", "4"};
static const char *const L_MODEL[] = {"LXR", "MODAL", "STRING", "FM"};   /* (0.9: Rings' models, rings.c) */
static const char *const L_BITS[] = {"OFF", "15", "14", "13", "12", "11", "10", "9", "8", "7", "6", "5", "4", "3",
                                     "2", "1"};
enum { MENU_REV_MODEL = 27, MENU_SHAPE, MENU_XMODE, MENU_CHOKE, MENU_BITS, MENU_MODEL };   /* (Kettle's, after LXR's) */
#define LIST(a) {a, sizeof a / sizeof a[0]}
static const struct { const char *const *s; uint8_t n; } MENUS[] = {
    [MENU_FILTER] = LIST(L_FILTER), [MENU_WAVEFORM] = LIST(L_WAVE), [MENU_AUDIO_OUT] = LIST(L_OUT),
    [MENU_ROLL_RATES] = LIST(L_ROLL), [MENU_SYNC_RATES] = LIST(L_SYNC), [MENU_LFO_WAVES] = LIST(L_LFOW),
    [MENU_RETRIGGER] = LIST(L_RETRIG), [MENU_SEQ_QUANT] = LIST(L_QUANT), [MENU_TRANS] = LIST(L_TRANS),
    [MENU_MIDI_ROUTING] = LIST(L_ROUTE), [MENU_MIDI_FILTERING] = LIST(L_MFILT), [MENU_MIX_FM] = LIST(L_MIXFM),
    [MENU_FX] = LIST(L_FX), [MENU_DIST] = LIST(L_DIST), [MENU_RATIO] = LIST(L_RATIO), [MENU_DELAY] = LIST(L_DELAY),
    [MENU_PAT_MODE] = LIST(L_PATMODE), [MENU_STEP_FLAM] = LIST(L_FLAM), [MENU_AUDIO_OUT_FX] = LIST(L_OUT),
    [MENU_REV_MODEL] = LIST(L_REVMODEL), [MENU_SHAPE] = LIST(L_SHAPE), [MENU_XMODE] = LIST(L_XMODE),
    [MENU_CHOKE] = LIST(L_CHOKE), [MENU_BITS] = LIST(L_BITS), [MENU_MODEL] = LIST(L_MODEL),
};
static const char *const VOICE_NAME[NTRACK] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};
static const char *const VOICE_SHORT[NTRACK] = {"D1", "D2", "D3", "SN", "CY", "CH", "OH"};

/* Kettle's parameters (sound.c): the groups of six per-voice ones and the single ones, in their order; each
 * {first, size, its value type DTYPE_* | MENU_* << 8, long name, category, short name, the name a lock shows} */
static const struct { uint16_t first; uint8_t n; uint16_t dtype; const char *nm[4]; } K_PARS[] = {
    {PAR_FX_SEND1, 6, DTYPE_0B127, {"FX send", "Send", "fxs", "FX"}},
    {PAR_REV_SEND1, 6, DTYPE_0B127, {"Rev send", "Send", "rvs", "Rev"}},
    {PAR_REV_SIZE, 1, DTYPE_0B127, {"Size", "Reverb", "siz", "Rev size"}},
    {PAR_REV_DAMP, 1, DTYPE_0B127, {"Damping", "Reverb", "dmp", "Rev damp"}},
    {PAR_REV_LEVEL, 1, DTYPE_0B127, {"Level", "Reverb", "lvl", "Rev level"}},
    {PAR_REV_TYPE, 1, DTYPE_MENU | MENU_REV_MODEL << 8, {"Model", "Reverb", "mdl", "Rev model"}},
    {PAR_BITS1, 6, DTYPE_MENU | MENU_BITS << 8, {"Bits", "Shape", "bit", "Bits"}},
    {PAR_SHAPE1, 6, DTYPE_MENU | MENU_SHAPE << 8, {"Shape", "Shape", "shp", "Shape"}},
    {PAR_TILT1, 6, DTYPE_0B127, {"Tilt", "Shape", "tlt", "Tilt"}},
    {PAR_CHOKE1, 6, DTYPE_MENU | MENU_CHOKE << 8, {"Choke", "Choke", "chk", "Choke"}},
    {PAR_RND_PITCH1, 6, DTYPE_0B127, {"Pitch", "Random", "rpi", "RndPitch"}},
    {PAR_RND_DECAY1, 6, DTYPE_0B127, {"Decay", "Random", "rdc", "RndDecay"}},
    {PAR_RND_FILTER1, 6, DTYPE_0B127, {"Filter", "Random", "rft", "RndFiltr"}},
    {PAR_RND_PAN1, 6, DTYPE_0B127, {"Pan", "Random", "rpn", "RndPan"}},
    {PAR_XSRC1, 6, DTYPE_VOICE_LFO, {"Source", "CrossMod", "xsr", "XSource"}},
    {PAR_XMODE1, 6, DTYPE_MENU | MENU_XMODE << 8, {"Mode", "CrossMod", "xmd", "XMode"}},
    {PAR_XAMT1, 6, DTYPE_0B127, {"Amount", "CrossMod", "xam", "XAmount"}},
    {PAR_MODEL1, 6, DTYPE_MENU | MENU_MODEL << 8, {"Model", "Model", "mdl", "Model"}},
    {PAR_MDECAY1, 6, DTYPE_0B127, {"Decay", "Model", "mdc", "MDecay"}},
    {PAR_MDAMP1, 6, DTYPE_0B127, {"Bright", "Model", "mbr", "MBright"}},
    {PAR_MMAT1, 6, DTYPE_0B127, {"Struct", "Model", "mst", "Structure"}},
    {PAR_MPOS1, 6, DTYPE_0B127, {"Position", "Model", "mps", "Position"}},
    {PAR_MEXC1, 6, DTYPE_0B127, {"Exciter", "Model", "mex", "Exciter"}},
    {PAR_MTENS1, 6, DTYPE_0B127, {"Tension", "Model", "mtn", "Tension"}},
};
static uint32_t par_k(uint32_t p)                    /* its row in K_PARS (p >= NUM_PARAMS) */
{
    uint32_t i;
    for (i = sizeof K_PARS / sizeof K_PARS[0] - 1u; i > 0u && p < K_PARS[i].first; i--)
        ;
    return i;
}
static uint32_t par_k_voice(uint32_t p)              /* a per-voice one's voice, 0xFF a single one / the LXR's */
{
    uint32_t r;
    if (p < NUM_PARAMS || p >= NPAR)
        return 0xFF;
    r = par_k(p);
    return K_PARS[r].n == 6u ? p - K_PARS[r].first : 0xFFu;
}
static const char *par_name_k(uint32_t p, uint32_t which) { return K_PARS[par_k(p)].nm[which]; }

static uint32_t par_dtype(uint32_t p)
{
    return p < NUM_PARAMS ? PAR_DTYPE[p] & 0xFFu : p < NPAR ? K_PARS[par_k(p)].dtype & 0xFFu : DTYPE_0B127;
}
static uint32_t par_menu(uint32_t p)
{
    return p < NUM_PARAMS ? PAR_DTYPE[p] >> 8 : p < NPAR ? K_PARS[par_k(p)].dtype >> 8 : 0u;
}

static uint32_t par_min(uint32_t p)
{
    switch (par_dtype(p)) {
    case DTYPE_1B16:
    case DTYPE_1B64:
        return 1;
    default:
        return p == PAR_BPM ? 1u : 0u;
    }
}
static uint32_t par_max(uint32_t p)
{
    switch (par_dtype(p)) {
    case DTYPE_0B255:
        return 255;
    case DTYPE_MENU: {
        uint32_t m = par_menu(p);
        return m < sizeof MENUS / sizeof MENUS[0] && MENUS[m].n ? MENUS[m].n - 1u : 127u;
    }
    case DTYPE_1B16:
        return 16;
    case DTYPE_1B64:
        return 64;
    case DTYPE_0B15:
        return 15;
    case DTYPE_ON_OFF:
    case DTYPE_0b1:
        return 1;
    case DTYPE_0B100:
        return 100;
    case DTYPE_TARGET_SELECTION_LFO:
    case DTYPE_TARGET_SELECTION_VELO:
    case DTYPE_AUTOM_TARGET:
        return NUM_MOD_TARGETS - 1u;
    case DTYPE_VOICE_LFO:
        return 6;
    default:
        return 127;
    }
}

/* the names: long (the cards), short (3 letters, the LXR's) */
static const char *par_name(uint32_t p)
{
    uint32_t t = p < NUM_PARAMS ? par_text_of[p] : 0u;
    if (p >= NUM_PARAMS && p < NPAR)
        return par_name_k(p, 0);
    switch (p) {                                     /* (the step and global ones the pages name otherwise) */
    case PAR_BPM: return "Tempo";
    case PAR_SHUFFLE: return "Shuffle";
    case PAR_ROLL: return "Roll";
    case PAR_MORPH: return "Morph";
    case PAR_TRACK_LENGTH: return "Length";
    default: break;
    }
    return t ? LONG_NAMES[TEXT_NAMES[t][2]] : "";
}
static const char *par_cat(uint32_t p)
{
    uint32_t t = p < NUM_PARAMS ? par_text_of[p] : 0u;
    if (p >= NUM_PARAMS && p < NPAR)
        return par_name_k(p, 1);
    return t ? CAT_NAMES[TEXT_NAMES[t][1]] : "";
}
static const char *par_short(uint32_t p)
{
    uint32_t t = p < NUM_PARAMS ? par_text_of[p] : 0u;
    if (p >= NUM_PARAMS && p < NPAR)
        return par_name_k(p, 2);
    return t ? SHORT_NAMES[TEXT_NAMES[t][0]] : "";
}

static char *put_int(char *b, int32_t v)
{
    char t[12];
    uint32_t n = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static void put_str(char *b, const char *s, uint32_t max)
{
    uint32_t i = 0;
    while (s[i] && i + 1u < max) {
        b[i] = s[i];
        i++;
    }
    b[i] = 0;
}
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void note_name(char *b, uint32_t n)
{
    put_str(b, NOTE_NAMES[n % 12u], 4);
    put_int(b + (NOTE_NAMES[n % 12u][1] ? 2 : 1), (int32_t)(n / 12u) - 1);
}

/* a modulation target: the voice and the parameter's name (lock targets past the LXR's: Kettle's, lock_target_par) */
static void mod_target_name(char *b, uint32_t idx, int with_voice, uint32_t max)
{
    uint32_t p, v;
    if (idx >= NUM_MOD_TARGETS && (p = lock_target_par(idx)) != 0u) {
        b[0] = 0;
        if (par_k_voice(p) < 6u) {                   /* a per-voice one: its voice */
            put_str(b, VOICE_SHORT[par_k_voice(p)], max);
            put_str(b + 2, " ", max - 2u);
            put_str(b + 3, par_name_k(p, 3), max - 3u);
        } else {
            put_str(b, par_name_k(p, 3), max);
        }
        return;
    }
    if (idx == 0u || idx >= NUM_MOD_TARGETS) {
        put_str(b, "OFF", max);
        return;
    }
    p = MOD_TARGETS[idx][1];
    if (p == PAR_VOICE_DECIMATION_ALL) {
        put_str(b, "ALL SRate", max);
        return;
    }
    b[0] = 0;
    if (with_voice) {
        for (v = 0; v < 6u; v++)
            if (idx >= MOD_TARGET_VOICE[v][0] && idx <= MOD_TARGET_VOICE[v][1]) {
                put_str(b, VOICE_SHORT[v], max);
                break;
            }
        if (b[0] && max > 4u) {
            uint32_t n = 0;
            while (b[n])
                n++;
            b[n++] = ' ';
            put_str(b + n, LONG_NAMES[TEXT_NAMES[MOD_TARGETS[idx][0]][2]], max - n);
            return;
        }
    }
    put_str(b, LONG_NAMES[TEXT_NAMES[MOD_TARGETS[idx][0]][2]], max);
}

/* the value as the screen shows it */
static void par_format(char *b, uint32_t p, uint32_t v, uint32_t max)
{
    uint32_t m;
    if (p >= PAR_TILT1 && p < PAR_TILT1 + 6u) {    /* tilt: from flat (64), + brighter */
        if (v > 64u) {
            b[0] = '+';
            put_int(b + 1, (int32_t)v - 64);
        } else {
            put_int(b, (int32_t)v - 64);
        }
        return;
    }
    switch (par_dtype(p)) {
    case DTYPE_MENU:
        m = par_menu(p);
        if (p >= PAR_STEP_FLAM && p <= PAR_STEP_SHIFT && m == MENU_STEP_SHIFT) {
            put_int(b, (int32_t)v - 7);
            if (v > 7u) {                            /* + sign */
                char t[8];
                put_str(t, b, 8);
                b[0] = '+';
                put_str(b + 1, t, max - 1u);
            }
            return;
        }
        if (m < sizeof MENUS / sizeof MENUS[0] && MENUS[m].n)
            put_str(b, MENUS[m].s[v < MENUS[m].n ? v : MENUS[m].n - 1u], max);
        else
            put_int(b, (int32_t)v);
        return;
    case DTYPE_PM63:
        if (p == PAR_PAN1 || p == PAR_PAN2 || p == PAR_PAN3 || p == PAR_PAN4 || p == PAR_PAN5 || p == PAR_PAN6) {
            if (v == 63u || v == 64u)
                put_str(b, "C", max);
            else if (v < 63u) {
                b[0] = 'L';
                put_int(b + 1, 63 - (int32_t)v);
            } else {
                b[0] = 'R';
                put_int(b + 1, (int32_t)v - 64);
            }
            return;
        }
        {   /* coarse tunings: semitones from 60; fine and the rest: from 63 */
            int coarse = (p >= PAR_COARSE1 && p <= PAR_FINE6 && !((p - PAR_COARSE1) & 1u)) ||
                         p == PAR_FM_FREQ1 || p == PAR_FM_FREQ2 || p == PAR_FM_FREQ3 || p == PAR_MOD_OSC_F1_CYM ||
                         p == PAR_MOD_OSC_F2_CYM || p == PAR_MOD_OSC_F1 || p == PAR_MOD_OSC_F2;
            int32_t d = (int32_t)v - (coarse ? 60 : 63);
            if (d > 0) {
                b[0] = '+';
                put_int(b + 1, d);
            } else {
                put_int(b, d);
            }
        }
        return;
    case DTYPE_ON_OFF:
        put_str(b, v ? "ON" : "OFF", max);
        return;
    case DTYPE_0b1:
        put_int(b, (int32_t)v + 1);
        return;
    case DTYPE_NOTE_NAME:
        if (v == 0u && p >= PAR_MIDI_NOTE1 && p <= PAR_MIDI_NOTE7)
            put_str(b, "ANY", max);
        else
            note_name(b, v);
        return;
    case DTYPE_TARGET_SELECTION_LFO:
    case DTYPE_TARGET_SELECTION_VELO:
        mod_target_name(b, v, 0, max);
        return;
    case DTYPE_AUTOM_TARGET:
        mod_target_name(b, v, 1, max);
        return;
    case DTYPE_VOICE_LFO:
        put_str(b, v >= 1u && v <= 6u ? VOICE_NAME[v - 1u] : "OFF", max);
        return;
    case DTYPE_0B100:
        put_int(b, (int32_t)v);
        if (p == PAR_STEP_PROB || p == PAR_FX_DRY_WET) {
            uint32_t n = 0;
            while (b[n])
                n++;
            b[n] = '%';
            b[n + 1] = 0;
        }
        return;
    default:
        if (p == PAR_BPM && v <= 1u) {
            put_str(b, "MIDI", max);
            return;
        }
        put_int(b, (int32_t)v);
        return;
    }
}
