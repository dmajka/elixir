/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the panel. The LXR-02's front (Core/Src/buttonHandler.c, menu/menu.c; Copyright (C) 2012-2026 Erica
 * Synths, GPL-3.0-only) on the FM-1's controls:
 *
 *   white keys 1..16   the 16 step buttons (steps of the shown bar; patterns; in PERF 1..7 rolls, 8 FILL, 9..16
 *                      the effects of perform.c; SHIFT + key latches it)
 *   black keys 1..7    the 7 voice buttons (D1 D2 D3 SN CY CH OH): select; with SHIFT mute (PERF: mute)
 *   black keys 8..11   the 4 bar buttons: show bar 1..4 (patterns: the bank, 2 of 16; kits and songs: 4);
 *                      SHIFT: the track's length; COPY: copy
 *   OCT-               SHIFT (held; the screen shows what SHIFT + a key does: the LXR's SHIFT + step functions)
 *   OCT+               COPY (held, + a track, a bar, a pattern); SHIFT + COPY = CLEAR; OK in the dialogs
 *   EDIT ENV SCL LFO GLO   the sound sections: OSC > FM > CLICK, AMP EG > MOD, FILTER > SHAPE, LFO > RANDOM >
 *                      CROSS MOD (Kettle's: their pages are K_PAGES), MIX > MIXER (volume,
 *                      pan, FX send, reverb send) > CONFIG (the CPU load at the bottom)
 *   FX  SEQ  ARP  HOME  SAVE   FX bus > reverb; step edit; PERF > EUCLID; GRID > PATTERNS > SONG; SAVE > LOAD
 *   PLAY REC           transport (SHIFT PLAY: the song from its start); record (SHIFT REC: its options)
 *   ALGORITHM SELECT PRESETS   the track; the page; the kit browser (OCT+ loads)
 *   KNOB 1..4          the four parameters on the screen; with a step key held, that step's lock
 * The main loop calls ui_input (every ms), ui_leds and ui_draw (a frame). */

enum { M_HOME, M_PATTERN, M_SONG, M_SOUND, M_STEP, M_PERF, M_EUCLID, M_FX, M_MIXER, M_CONFIG, M_SAVE, M_LOAD,
       M_LAST, M_RECOPT, M_KEYS, NMODE };
enum { S_OSC, S_AEG, S_MOD, S_FM, S_CLICK, S_FILTER, S_LFO, S_MIX, S_SHAPE, S_RANDOM, S_XMOD, S_MODEL, NSECT };
#define NSPG 24                                      /* the sound pages a track can have (sections x halves) */
static const char *const SECT_NAME[NSECT] = {"OSCILLATOR", "AMP EG", "MOD EG / VELO", "FM", "TRANSIENT", "FILTER",
                                             "LFO", "MIX", "SHAPE", "RANDOM PER HIT", "CROSS MOD / CHOKE", "MODEL"};
enum { IO_KIT, IO_PATTERN, IO_SONG, IO_MORPH, IO_SOUND, IO_PROJECT, IO_FACTORY, NIO };   /* (FACTORY: LOAD, a kit bank) */
static const char *const IO_NAME[NIO] = {"KIT", "PATTERN", "SONG", "MORPH", "SOUND", "PROJECT", "FACTORY"};
#if KETTLE_FACTORY_KITS
#define IO_LOAD_LAST IO_FACTORY
#else
#define IO_LOAD_LAST IO_PROJECT
#define FACTORY_NKITS 0u
#endif

/* the keys: k = semitones above F3 (fm1_in.notes bit k) */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* virtual parameters: what the cards edit that is not in par[] */
enum { VP_BASE = 0x200, VP_NONE = VP_BASE, VP_IO_TYPE, VP_IO_SLOT, VP_NAME_POS, VP_NAME_CHAR, VP_NAME_CASE,
       VP_SONG_PAT, VP_SONG_NUM, VP_LEDS, VP_AUTOSAVE, VP_ACCEL, VP_PALETTE, VP_TRACK_VOL, VP_FX_SUB, VP_MASTER,
       VP_DJ, VP_REPEAT, VP_TAPE, VP_M1, VP_M4 = VP_M1 + NMACRO - 1, VP_MSEL, VP_MTARGET, VP_MMIN, VP_MMAX,
       VP_COND, VP_RAT_N, VP_RAT_RATE, VP_RAT_FADE, VP_ACTION, VP_GMODE, VP_GA, VP_GB, VP_GC, VP_CLOCK,
       VP_GX, VP_GY, VP_GCHAOS, VP_RAT_SHAPE, VP_BSTART, VP_BPITCH, VP_ROLL,
       VP_LINK, VP_LSTART, VP_LAMT, VP_DEPTH, VP_LWHEN, VP_LTGT, VP_LEFF, VP_LWIN, VP_KOCT, VP_KVEL, VP_KACC, VP_PKEEP,
       VP_IO_SRC, VP_IO_TRACK,
       VP_P2, VP_P2_LAST = VP_P2 + 7 };
/* the EUCLID pages' ACTION (OCT+ does it) */
enum { ACT_NONE, ACT_DICE, ACT_COMMIT, ACT_UNDO, ACT_RESET, NACT };

static struct {
    uint8_t mode, back;              /* back: where SEQ returns to */
    uint8_t track, bar, sect, half, page;
    uint8_t step;                    /* STEP: the selected step (0..63) */
    uint8_t perf;                    /* the white keys roll (PERF, and the sound pages opened from it) */
    uint8_t kplay;                   /* 0.8: the keys play the track's voice (KEYS, and the sound pages opened from it) */
    uint8_t koct, knote;             /* .. KEYS' octave (3: the keys from F3), the last note played */
    uint32_t pf_held, pf_latch;      /* PERF's keys 8..16 held, latched (bit PF_*: pf.on is both) */
    uint8_t roll_latch;              /* the rolls latched (SHIFT + key 1..7) */
    uint8_t drill_held, drill_latch; /* 0.6: PERFORM 2's keys 1..7 (DRILL) */
    uint16_t kbank;                  /* bit w: white key w went down on a PERFORM 2 page (it lets go there) */
    uint8_t msel;                    /* MACRO SETUP: the macro */
    uint8_t action;                  /* EUCLID: the ACTION OCT+ does (ACT_*) */
    uint8_t eu_undo;                 /* the Euclid knobs' UNDO saved (track + 1): turned again, it keeps that one */
    uint8_t link;                    /* LINKS: the link shown (0..7) */
    uint8_t shift, copy, shift_used, copy_used;
    int8_t held_step;                /* a step key held (its locks), -1 none */
    uint8_t lock_turned;
    uint8_t copy_src;                /* COPY: the source track + 1 / pattern + 1 */
    uint8_t clear_open, clear_sel;
    uint8_t last_all;
    uint8_t kb_open, kb_sel;         /* the kit browser */
    uint8_t sb_open, sb_voice, sb_on; /* 0.8: the sound browser (its voice; the voice as it was: sb_orig, sb_on values) */
    uint16_t sb_sel;
    uint8_t sb_orig[SOUND_MAX];
    uint32_t kb_ms;
    uint8_t io_type, io_slot, io_naming, io_pos, io_bank;
    uint8_t io_src;                  /* 0.8: KIT and MORPH from / to the slots (0) or the library (1) */
    uint8_t lib_kit, lib_sound;      /* .. the library's kit and sound last loaded or saved */
    char io_name[8];
    uint8_t song_row;
    uint8_t bank;                    /* PATTERN / SONG: the bank of 16 the keys show */
    char msg[32];
    uint32_t msg_ms;
    uint8_t msg_long;                /* a warning: 6 s on screen, not 1.5 */
    uint8_t force;
    uint32_t keys;                   /* the keys as last seen (releases) */
    uint8_t hot;                     /* the card last turned (1..4), 0 none */
    uint32_t hot_ms;
    uint32_t knob_ms[4];
    uint32_t trig_seen[NTRACK];
    uint32_t flash_ms[NTRACK];
} ui = {.held_step = -1, .koct = 3, .knote = 0xFF};


static uint32_t ui_track(void) { return ui.track; }
static uint32_t ui_pattern(void) { return sq.active; }

static void ui_draw(void);
static void seq_copy_track(uint32_t src, uint32_t dst, pattern_t *p);
static void bar_copy(pattern_t *p, uint32_t t, uint32_t from, uint32_t to, int all);
/* a step key: the step on / off (a generator's track: its live copy too, the step as the pattern has it) */
static void step_toggle(pattern_t *p, uint32_t t, uint32_t s)
{
    int on = !main_on(p, t, s);
    main_set(p, t, s, on);
    if (p == &pat[sq.active] && p->gen[t][0] != GEN_OFF) {
        gpat.step[t][s] = p->step[t][s];
        gpat.timing[t][s] = p->timing[t][s];
        gpat.lhi[t][s] = p->lhi[t][s];
        gpat.cond[t][s] = p->cond[t][s];
        gpat.rat[t][s] = p->rat[t][s];
        main_set(&gpat, t, s, on);
    }
}

static void ui_message(const char *s)
{
    put_str(ui.msg, s, sizeof ui.msg);
    ui.msg_ms = fm1_ms;
    ui.msg_long = 0;
    ui.force = 1;
}

/* the voice (0..5) of a track: the open hat is the hat voice */
static uint32_t track_voice(uint32_t t) { return t < 6u ? t : 5u; }
/* the LXR's edit page of a track (VOICE1_PAGE .. VOICE7_PAGE) */
static uint32_t track_page(uint32_t t) { return t; }
/* Kettle's sections (S_SHAPE ..): the first of each six per-voice parameters, by half (0: none). MODEL: the DRUM
 * voices' only */
static const uint16_t K_PAGES[4][8] = {{PAR_SHAPE1, PAR_DRIVE1, PAR_BITS1, PAR_TILT1},
                                       {PAR_RND_PITCH1, PAR_RND_DECAY1, PAR_RND_FILTER1, PAR_RND_PAN1},
                                       {PAR_XSRC1, PAR_XMODE1, PAR_XAMT1, PAR_CHOKE1},
                                       {PAR_MODEL1, PAR_MDECAY1, PAR_MDAMP1, PAR_MMAT1,
                                        PAR_MPOS1, PAR_MEXC1, PAR_MTENS1, 0}};
/* the parameter on knob k of section s's half h for track t (0: none) */
static uint32_t sect_par(uint32_t t, uint32_t s, uint32_t h, uint32_t k)
{
    uint32_t q;
    if (s < S_SHAPE)
        return LXR_PAGES[track_page(t)][s][8u + h * 4u + k];
    if (s == S_MODEL && track_voice(t) > 2u)
        return 0;
    q = K_PAGES[s - S_SHAPE][h * 4u + k];
    return q ? q + track_voice(t) : 0u;
}
/* the button that opens a section: the first section of its group */
static uint32_t sect_group(uint32_t s)
{
    static const uint8_t G[NSECT] = {S_OSC, S_AEG, S_AEG, S_OSC, S_OSC, S_FILTER, S_LFO, S_MIX, S_FILTER, S_LFO, S_LFO,
                                     S_OSC};
    return G[s];
}

/* ------------------------------------------------------------ pages --- */
/* the four parameters a page puts on the knobs */
static void page_params(uint16_t out[4]);
static uint32_t mode_pages(void);

static int sect_half_used(uint32_t t, uint32_t s, uint32_t h)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        uint32_t p = sect_par(t, s, h, k);
        if (p && p != PAR_NONE)
            return 1;
    }
    return 0;
}
/* the sound pages in order: (section, half) that hold something for this track */
static uint32_t sound_pages(uint8_t list[NSPG][2])
{
    uint32_t s, h, n = 0;
    for (s = 0; s < NSECT; s++)
        for (h = 0; h < 2u; h++)
            if (sect_half_used(ui.track, s, h)) {
                list[n][0] = (uint8_t)s;
                list[n][1] = (uint8_t)h;
                n++;
            }
    return n;
}
static void sound_fix(void)                          /* a section this track has (the snare has no FM page) */
{
    uint8_t l[NSPG][2];
    uint32_t n = sound_pages(l), i;
    for (i = 0; i < n; i++)
        if (l[i][0] == ui.sect && l[i][1] == ui.half)
            return;
    for (i = 0; i < n; i++)
        if (l[i][0] >= ui.sect) {
            ui.sect = l[i][0];
            ui.half = l[i][1];
            return;
        }
    ui.sect = l[0][0];
    ui.half = l[0][1];
}

static const uint16_t P_HOME[4] = {PAR_BPM, PAR_SHUFFLE, PAR_TRACK_LENGTH, VP_TRACK_VOL};
static const uint16_t P_STEP[4][4] = {{PAR_STEP_VOLUME, PAR_STEP_NOTE, PAR_STEP_PROB, VP_COND},
                                      {PAR_STEP_FLAM, PAR_STEP_SHIFT, VP_NONE, VP_NONE},
                                      {PAR_P1_DEST, PAR_P1_VAL, PAR_P2_DEST, PAR_P2_VAL},
                                      {VP_RAT_N, VP_RAT_RATE, VP_RAT_FADE, VP_RAT_SHAPE}};
static const char *const STEP_TITLE[4] = {"STEP", "TIMING", "LOCKS", "RATCHET"};
/* 0.8: the performance pages first, then the macros, then PERFORM 2 (its keys the second bank). PERFORM is the LXR's
 * PERFORMANCE page (BPM, roll, morph, sample rate); MIX rides the voices' volumes as the LXR's faders */
#define NPERF 9u
#define PG_MIX 2u
#define PG_PERF2 6u                                  /* (the pages from here: PERFORM 2, its keys the second bank) */
static const uint16_t P_PERF[NPERF][4] = {{PAR_BPM, PAR_ROLL, PAR_MORPH, PAR_VOICE_DECIMATION_ALL},
                                          {VP_DJ, VP_REPEAT, PAR_SHUFFLE, VP_TAPE},
                                          {PAR_VOL1, PAR_VOL2, PAR_VOL3, PAR_VOL4},
                                          {PAR_VOL5, PAR_VOL6, PAR_FX_DRY_WET, PAR_REV_LEVEL},
                                          {VP_M1, VP_M1 + 1, VP_M1 + 2, VP_M1 + 3},
                                          {VP_MSEL, VP_MTARGET, VP_MMIN, VP_MMAX},
                                          {PAR_BPM, PAR_ROLL, PAR_MORPH, PAR_VOICE_DECIMATION_ALL},
                                          {VP_P2, VP_P2 + 1, VP_P2 + 2, VP_P2 + 3},
                                          {VP_P2 + 4, VP_P2 + 5, VP_P2 + 6, VP_P2 + 7}};
static const char *const PERF_TITLE[NPERF] = {"PERFORM", "PERFORM: MORE", "PERFORM: MIX", "PERFORM: MIX 2", "MACROS",
                                              "MACRO SETUP", "PERFORM 2", "PERFORM 2: SETUP", "PERFORM 2: SETUP 2"};
static int perf_mix(void) { return ui.mode == M_PERF && (ui.page % NPERF == PG_MIX || ui.page % NPERF == PG_MIX + 1u); }
static int perf_bank2(void) { return ui.mode == M_PERF && ui.page % NPERF >= PG_PERF2; }
#define NEUCLID 6u
#define PG_LINKS 4u
static const uint16_t P_EUCLID[NEUCLID][4] = {{PAR_EUKLID_LENGTH, PAR_EUKLID_STEPS, PAR_EUKLID_ROTATION, VP_ACTION},
                                              {VP_GMODE, VP_GA, VP_GB, VP_GC},
                                              {PAR_TRACK_LENGTH, VP_CLOCK, VP_BSTART, VP_BPITCH},
                                              {VP_GX, VP_GY, VP_GCHAOS, VP_NONE},
                                              {VP_LINK, VP_LSTART, VP_LAMT, VP_DEPTH},
                                              {VP_LWHEN, VP_LTGT, VP_LEFF, VP_LWIN}};
static const char *const EUCLID_TITLE[NEUCLID] = {"EUCLID", "GENERATOR", "TRACK CLOCK", "GRIDS MAP", "LINKS",
                                                  "LINK SETUP"};
static const uint16_t P_FX[3][4] = {{PAR_FX_TYPE, PAR_FX_DRY_WET, PAR_FX_ROUTING, VP_FX_SUB},
                                    {PAR_FX_P1, PAR_FX_P2, PAR_FX_P3, PAR_FX_P4},
                                    {PAR_REV_TYPE, PAR_REV_SIZE, PAR_REV_DAMP, PAR_REV_LEVEL}};
#define NMIXER 8u
static const uint16_t P_MIXER[NMIXER][4] = {{PAR_VOL1, PAR_VOL2, PAR_VOL3, PAR_VOL4},
                                            {PAR_VOL5, PAR_VOL6, PAR_FX_DRY_WET, VP_MASTER},
                                            {PAR_PAN1, PAR_PAN2, PAR_PAN3, PAR_PAN4},
                                            {PAR_PAN5, PAR_PAN6, PAR_REV_LEVEL, PAR_REV_SIZE},
                                            {PAR_FX_SEND1, PAR_FX_SEND1 + 1, PAR_FX_SEND1 + 2, PAR_FX_SEND1 + 3},
                                            {PAR_FX_SEND1 + 4, PAR_FX_SEND1 + 5, PAR_FX_DRY_WET, VP_NONE},
                                            {PAR_REV_SEND1, PAR_REV_SEND1 + 1, PAR_REV_SEND1 + 2, PAR_REV_SEND1 + 3},
                                            {PAR_REV_SEND1 + 4, PAR_REV_SEND1 + 5, PAR_REV_LEVEL, VP_NONE}};
static const char *const MIXER_TITLE[NMIXER / 2u] = {"MIXER: VOLUME", "MIXER: PAN", "MIXER: FX SEND",
                                                     "MIXER: REVERB SEND"};
static const uint16_t P_CONFIG[7][4] = {
    {PAR_BPM, PAR_SHUFFLE, PAR_FOLLOW, PAR_PATTERN_MODE},
    {PAR_MIDI_CHAN_GLOBAL, PAR_MIDI_FILT_TX, PAR_MIDI_FILT_RX, PAR_MIDI_ROUTING},
    {PAR_MIDI_CHAN_1, PAR_MIDI_CHAN_2, PAR_MIDI_CHAN_3, PAR_MIDI_CHAN_4},
    {PAR_MIDI_CHAN_5, PAR_MIDI_CHAN_6, PAR_MIDI_CHAN_7, VP_NONE},
    {PAR_MIDI_NOTE1, PAR_MIDI_NOTE2, PAR_MIDI_NOTE3, PAR_MIDI_NOTE4},
    {PAR_MIDI_NOTE5, PAR_MIDI_NOTE6, PAR_MIDI_NOTE7, VP_NONE},
    {VP_LEDS, VP_AUTOSAVE, VP_ACCEL, VP_PALETTE}};
static const char *const CONFIG_TITLE[7] = {"SEQUENCER", "MIDI", "MIDI CHANNELS", "MIDI CHANNELS", "MIDI NOTES",
                                            "MIDI NOTES", "DEVICE"};
static const uint16_t P_NAME[4] = {VP_NAME_POS, VP_NAME_CHAR, VP_NAME_CASE, VP_NONE};
static const uint16_t P_SONG[4] = {VP_SONG_PAT, VP_SONG_NUM, VP_NONE, VP_NONE};
static const uint16_t P_PATTERN[4] = {PAR_PATTERN_MODE, PAR_SHUFFLE, VP_PKEEP, VP_NONE};
static const uint16_t P_LAST[4] = {PAR_TRACK_LENGTH, VP_NONE, VP_NONE, VP_NONE};
static const uint16_t P_RECOPT[4] = {PAR_AUTOM_TRACK, VP_NONE, VP_NONE, VP_NONE};
static const uint16_t P_KEYS[4] = {VP_KOCT, VP_KVEL, VP_KACC, VP_NONE};

static uint32_t mode_pages(void)
{
    uint8_t l[NSPG][2];
    switch (ui.mode) {
    case M_SOUND: return sound_pages(l);
    case M_STEP: return 4;
    case M_EUCLID: return NEUCLID;
    case M_PERF: return NPERF;
    case M_FX: return 3;
    case M_MIXER: return NMIXER;
    case M_CONFIG: return 7;
    default: return 1;
    }
}
static void page_params(uint16_t out[4])
{
    const uint16_t *p = P_HOME;
    uint32_t k;
    switch (ui.mode) {
    case M_SOUND:
        for (k = 0; k < 4u; k++) {
            uint32_t q = sect_par(ui.track, ui.sect, ui.half, k);
            out[k] = (uint16_t)(q ? q : VP_NONE);
        }
        return;
    case M_STEP: p = P_STEP[ui.page % 4u]; break;
    case M_PERF: p = P_PERF[ui.page % NPERF]; break;
    case M_EUCLID: p = P_EUCLID[ui.page % NEUCLID]; break;
    case M_FX: p = P_FX[ui.page % 3u]; break;
    case M_MIXER: p = P_MIXER[ui.page % NMIXER]; break;
    case M_CONFIG: p = P_CONFIG[ui.page % 7u]; break;
    case M_SAVE:
    case M_LOAD:
        if (ui.io_naming) {
            p = P_NAME;
            break;
        }
        out[0] = VP_IO_TYPE;                          /* (the third: the slots or the library, the sound's track) */
        out[1] = VP_IO_SLOT;
        out[2] = ui.io_type == IO_KIT || ui.io_type == IO_MORPH ? VP_IO_SRC
               : ui.io_type == IO_SOUND ? VP_IO_TRACK : VP_NONE;
        out[3] = VP_NONE;
        return;
    case M_SONG: p = P_SONG; break;
    case M_PATTERN: p = P_PATTERN; break;
    case M_LAST: p = P_LAST; break;
    case M_RECOPT: p = P_RECOPT; break;
    case M_KEYS: p = P_KEYS; break;
    default: break;
    }
    for (k = 0; k < 4u; k++)
        out[k] = p[k];
    if (ui.mode == M_PERF && ui.page % NPERF == 0u && (sq.roll & ~ui.roll_latch))
        out[1] = ui.shift ? PAR_ROLL : VP_ROLL;      /* 0.6: a roll held: the roll knob sweeps its rate (SHIFT: the grid's) */
}

/* ------------------------------------------- the values the cards edit --- */
static pattern_t *shown_pat(void) { return &pat[ui_pattern()]; }
/* a link as the LINKS page's Start shows it: 0 none, 1.. a starting point, NSTART + 1 changed from one */
static uint32_t link_start(const uint8_t *L)
{
    uint32_t i;
    if (!(L[1] & 15u))
        return 0;
    for (i = 0; i < NSTART; i++)
        if (L[0] == LK_START[i][0] && L[1] == LK_START[i][1] && L[2] == LK_START[i][2])
            return i + 1u;
    return NSTART + 1u;
}
/* the pattern a track shows: a generator's live copy while it is on (what plays) */
static const pattern_t *view_pat(uint32_t t)
{
    const pattern_t *p = shown_pat();
    return p == &pat[sq.active] && p->gen[t][0] != GEN_OFF ? &gpat : p;
}
static uint32_t vp_max(uint32_t p);
/* 0.8: SAVE / LOAD on the library (a SOUND always) */
static int io_lib(void)
{
    return ui.io_type == IO_SOUND || ((ui.io_type == IO_KIT || ui.io_type == IO_MORPH) && ui.io_src);
}
static uint32_t io_slot_default(uint32_t type);
static void track_select(uint32_t t);
/* the banks of 16 the white keys show (the bar keys pick one): patterns 2, kits and songs 4; 0: no banks here */
static uint32_t ui_banks(void)
{
    if (ui.mode == M_PATTERN || ui.mode == M_SONG)
        return NUM_PATTERN / 16u;
    if ((ui.mode == M_SAVE || ui.mode == M_LOAD) && ui.io_type != IO_PROJECT)
        return (vp_max(VP_IO_SLOT) + 16u) / 16u;
    return 0;
}
static void bank_fix(void)
{
    if (ui_banks() && ui.bank >= ui_banks())
        ui.bank = 0;
}
static step_t *sel_step(void) { return &shown_pat()->step[ui.track][ui.step & 63u]; }
static uint32_t vp_min(uint32_t p);
static uint32_t vp_max(uint32_t p);
static const char *const FX_P_NAME[NFX][4] = {
    {"-", "-", "-", "-"}, {"Drive", "Color", "Tone", "Volume"}, {"Freq", "-", "-", "-"},
    {"Attack", "Release", "Threshld", "Gain"}, {"Time", "Range", "Tone", "Feedback"},
    {"Rate", "Depth", "Feedback", "Centre"}, {"Rate", "Depth", "Feedback", "Delay"}};

/* a mod target's voice range (VEL DEST: its own; LFO TARGET: the voice PAR_VOICE_LFO picks) */
static void target_range(uint32_t p, uint32_t *lo, uint32_t *hi)
{
    uint32_t v = 0;
    if (p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6)
        v = p - PAR_VEL_DEST_1;
    else if (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) {
        v = par[PAR_VOICE_LFO1 + (p - PAR_TARGET_LFO1)];
        v = v >= 1u && v <= 6u ? v - 1u : 0u;
    }
    *lo = MOD_TARGET_VOICE[v][0];
    *hi = MOD_TARGET_VOICE[v][1];
}

/* a parameter's index in the lock targets (lock_target_par; 0: none) */
static uint32_t lock_idx_of(uint32_t d)
{
    uint32_t i;
    if (!d)
        return 0;
    if (d >= NUM_PARAMS)                              /* Kettle's: after the LXR's targets */
        return lock_target_idx(d);
    for (i = 1; i < NUM_MOD_TARGETS; i++)              /* the first target of the parameter (in its voice) */
        if (MOD_TARGETS[i][1] == d && (par_voice(d) == 0xFFu || (i >= MOD_TARGET_VOICE[par_voice(d)][0] &&
                                                                i <= MOD_TARGET_VOICE[par_voice(d)][1])))
            return i;
    return 0;
}
/* the macros (PERF): macro m's parameter between its MIN and MAX (MAX below MIN: the other way) */
static void vp_set(uint32_t p, uint32_t v);
static uint32_t macro_max(uint32_t m)
{
    uint32_t p = pfs.m[m & 3u].par, x = p ? vp_max(p) : 127u;
    return x > 127u ? 127u : x;
}
static void macro_set(uint32_t m, uint32_t v)
{
    uint32_t p = pfs.m[m].par;
    int32_t lo = pfs.m[m].lo, hi = pfs.m[m].hi, x;
    pfs.m[m].val = (uint8_t)v;
    if (!p)
        return;
    x = lo + (hi - lo) * (int32_t)v / 127;
    vp_set(p, (uint32_t)x);
    if (sq.rec && par_lockable(p))                   /* REC: a lock on the playing step, as a knob's */
        seq_record_automation(ui.track, p, (uint32_t)x);
}

static uint32_t vp_get(uint32_t p)
{
    pattern_t *pt = shown_pat();
    switch (p) {
    case PAR_TRACK_LENGTH: return track_len(pt, ui.track);
    case PAR_STEP_VOLUME: return sel_step()->volume & STEP_VOLUME_MASK;
    case PAR_STEP_NOTE: return sel_step()->note & 127u;
    case PAR_STEP_PROB: return sel_step()->prob > 100u ? 100u : sel_step()->prob;
    case PAR_STEP_FLAM: return pt->timing[ui.track][ui.step & 63u] & 15u;
    case PAR_STEP_SHIFT: return pt->timing[ui.track][ui.step & 63u] >> 4;
    case PAR_P1_DEST:
    case PAR_P2_DEST: {
        return lock_idx_of(autodest_to_par(lock_get(pt, ui.track, ui.step & 63u, p == PAR_P2_DEST)));
    }
    case PAR_P1_VAL: return sel_step()->p1v;
    case PAR_P2_VAL: return sel_step()->p2v;
    case PAR_EUKLID_LENGTH: return eu_len[ui.track];
    case PAR_EUKLID_STEPS: return eu_steps[ui.track];
    case PAR_EUKLID_ROTATION: return eu_rot[ui.track];
    case PAR_ROLL: return sq.roll_rate;
    case PAR_MORPH: return morph_amt;
    case PAR_AUTOM_TRACK: return sq.lane;
    case VP_TRACK_VOL: return par[PAR_VOL1 + track_voice(ui.track)];
    case VP_FX_SUB:
        return fx.type == FX_DRIVE ? par[PAR_FX_DIST_TYPE] : fx.type == FX_RING ? par[PAR_FX_WAVE]
             : fx.type == FX_COMP ? par[PAR_FX_RATIO] : par[PAR_FX_DELAY_TYPE];
    case VP_IO_TYPE: return ui.io_type;
    case VP_IO_SLOT: return ui.io_slot;
    case VP_IO_SRC: return ui.io_src;
    case VP_IO_TRACK: return ui.track;
    case VP_NAME_POS: return ui.io_pos;
    case VP_NAME_CHAR: return (uint8_t)ui.io_name[ui.io_pos & 7u];
    case VP_NAME_CASE: return ui.io_name[ui.io_pos & 7u] >= 'a' && ui.io_name[ui.io_pos & 7u] <= 'z';
    case VP_SONG_PAT: {                           /* the patterns, then END, LOOP */
        uint32_t v = songs[sq.song][ui.song_row & 63u];
        return v < NUM_PATTERN ? v : v == SONG_LOOP ? NUM_PATTERN + 1u : NUM_PATTERN;
    }
    case VP_SONG_NUM: return sq.song;
    case VP_LEDS: return kset.leds;
    case VP_AUTOSAVE: return autosave_on;
    case VP_PKEEP: return pat_keep;
    case VP_ACCEL: return kset.accel;
    case VP_PALETTE: return kset.palette;
    case VP_MASTER: return (uint32_t)(master_q12 >> 5);
    case VP_DJ: return pf.dj;
    case VP_REPEAT: return pfs.repeat;
    case VP_TAPE: return pfs.tape;
    case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3: return pfs.m[p - VP_M1].val;
    case VP_MSEL: return ui.msel;
    case VP_MTARGET: return lock_idx_of(pfs.m[ui.msel].par);
    case VP_MMIN: return pfs.m[ui.msel].lo;
    case VP_MMAX: return pfs.m[ui.msel].hi;
    case VP_COND: return pt->cond[ui.track][ui.step & 63u];
    case VP_RAT_N: return pt->rat[ui.track][ui.step & 63u] & 7u;
    case VP_RAT_RATE: return (pt->rat[ui.track][ui.step & 63u] >> 3) & 3u;
    case VP_RAT_FADE: return (pt->rat[ui.track][ui.step & 63u] >> 5) & 3u;
    case VP_ACTION: return ui.action;
    case VP_GMODE: return pt->gen[ui.track][0];
    case VP_GA: case VP_GB: case VP_GC: return pt->gen[ui.track][p - VP_GA + 1u];
    case VP_CLOCK: return pt->clk[ui.track] < NCLK ? pt->clk[ui.track] : CLK_1X;
    case VP_RAT_SHAPE: return (pt->rat[ui.track][ui.step & 63u] & RAT_BOUNCE) != 0u;
    case VP_BSTART: return pt->bnc[ui.track] & 3u;
    case VP_BPITCH: return (pt->bnc[ui.track] >> 2) > 24u ? 12u : pt->bnc[ui.track] >> 2;
    case VP_ROLL: return sq.roll_k;
    case VP_GX: return pt->gx;
    case VP_GY: return pt->gy;
    case VP_GCHAOS: return pt->gchaos;
    case VP_LINK: return ui.link & 7u;
    case VP_P2: case VP_P2 + 1: case VP_P2 + 2: case VP_P2 + 3: case VP_P2 + 4: case VP_P2 + 5: case VP_P2 + 6:
    case VP_P2_LAST: return (&pfs.drill)[p - VP_P2];
    case VP_LSTART: return link_start(pt->link[ui.link & 7u]);
    case VP_LAMT: {
        const uint8_t *L = pt->link[ui.link & 7u];
        uint32_t e = L[1] & 15u;
        return e && e < NLE ? (uint32_t)clamp32((int8_t)L[2], LK_LO[e], LK_HI[e]) - (uint32_t)(int32_t)LK_LO[e] : 0u;
    }
    case VP_DEPTH: return pt->cdepth;
    case VP_KOCT: return ui.koct;
    case VP_KVEL: return key_vel;
    case VP_KACC: return key_acc;
    case VP_LWHEN: {
        const uint8_t *L = pt->link[ui.link & 7u];
        return (L[0] & 7u) % NTRACK * NLV + (L[0] >> 6) % NLV;
    }
    case VP_LTGT: return (pt->link[ui.link & 7u][0] >> 3 & 7u) % NTRACK;
    case VP_LEFF: return (pt->link[ui.link & 7u][1] & 15u) % NLE;
    case VP_LWIN: return pt->link[ui.link & 7u][1] >> 4 & 3u;
    default:
        return p < NPAR ? par[p] : 0u;
    }
}
static uint32_t vp_min(uint32_t p)
{
    uint32_t lo, hi;
    if ((p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) || (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6))
        return 0;
    (void)lo;
    (void)hi;
    switch (p) {
    case PAR_TRACK_LENGTH: case PAR_EUKLID_LENGTH: case PAR_EUKLID_STEPS: return 1;
    case VP_GA: return shown_pat()->gen[ui.track][0] == GEN_TURING ? 2u : 0u;
    case VP_P2 + 4: return 24;
    case VP_KVEL: case VP_KACC: return 1;
    case PAR_BPM: return 1;
    case PAR_MIDI_CHAN_1: case PAR_MIDI_CHAN_2: case PAR_MIDI_CHAN_3: case PAR_MIDI_CHAN_4: case PAR_MIDI_CHAN_5:
    case PAR_MIDI_CHAN_6: case PAR_MIDI_CHAN_7: case PAR_MIDI_CHAN_GLOBAL: return 1;
    default: return p < NPAR ? par_min(p) : 0u;
    }
}
static uint32_t vp_max(uint32_t p)
{
    uint32_t lo, hi;
    if ((p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) || (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6)) {
        target_range(p, &lo, &hi);
        return hi;
    }
    switch (p) {
    case PAR_TRACK_LENGTH: case PAR_EUKLID_LENGTH: return 64;
    case PAR_EUKLID_STEPS: return eu_len[ui.track] ? eu_len[ui.track] : 16u;
    case PAR_EUKLID_ROTATION: return (eu_len[ui.track] ? eu_len[ui.track] : 16u) - 1u;
    case PAR_STEP_VOLUME: case PAR_STEP_NOTE: case PAR_P1_VAL: case PAR_P2_VAL: return 127;
    case PAR_STEP_PROB: return 100;
    case PAR_STEP_FLAM: return 5;
    case PAR_STEP_SHIFT: return 14;
    case PAR_P1_DEST: case PAR_P2_DEST: return NUM_MOD_TARGETS - 1u + NLOCK_K;
    case PAR_ROLL: return 13;
    case PAR_MORPH: return 255;
    case PAR_AUTOM_TRACK: return 1;
    case PAR_BPM: return 255;
    case PAR_SHUFFLE: return 127;
    case PAR_FOLLOW: return 1;
    case PAR_PATTERN_MODE: return 1;
    case VP_TRACK_VOL: return 127;
    case VP_FX_SUB:
        return fx.type == FX_DRIVE ? 2u : fx.type == FX_RING || fx.type == FX_COMP ? 5u
             : fx.type == FX_DELAY ? 1u : 0u;
    case VP_IO_TYPE: return ui.mode == M_SAVE ? IO_PROJECT : IO_LOAD_LAST;
    case VP_IO_SLOT:
        return io_lib() ? (ui.io_type == IO_SOUND ? KL_SOUNDS : KL_KITS) - 1u
             : ui.io_type == IO_PROJECT ? PROJ_USER - 1u : ui.io_type == IO_PATTERN ? NUM_PATTERN - 1u
             : ui.io_type == IO_FACTORY ? FACTORY_NKITS          /* (the last: all of them into the slots) */
             : ui.io_type == IO_SONG ? NUM_SONGS - 1u : NUM_KITS - 1u;
    case VP_NAME_POS: return 7;
    case VP_NAME_CHAR: return 126;
    case VP_NAME_CASE: return 1;
    case VP_SONG_PAT: return NUM_PATTERN + 1u;
    case VP_SONG_NUM: return NUM_SONGS - 1u;
    case VP_LEDS: return 2;
    case VP_AUTOSAVE: case VP_ACCEL: case VP_IO_SRC: case VP_PKEEP: return 1;
    case VP_IO_TRACK: return NTRACK - 1u;
    case VP_PALETTE: return NPALETTES - 1u;
    case VP_MASTER: case VP_DJ: case VP_TAPE: case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3: return 127;
    case VP_REPEAT: case VP_MSEL: return 3;
    case VP_MTARGET: return NUM_MOD_TARGETS - 1u + NLOCK_K;
    case VP_MMIN: case VP_MMAX: return macro_max(ui.msel);
    case VP_COND: return NCOND - 1u;
    case VP_RAT_N: return 7;
    case VP_RAT_RATE: return 3;
    case VP_RAT_FADE: return 2;
    case VP_ACTION: return NACT - 1u;
    case VP_GMODE: return NGEN - 1u;
    case VP_CLOCK: return NCLK - 1u;
    case VP_RAT_SHAPE: return 1;
    case VP_BSTART: return 3;
    case VP_BPITCH: return 24;
    case VP_ROLL: return 127;
    case VP_GX: case VP_GY: case VP_GCHAOS: return 127;
    case VP_LINK: return NLINK - 1u;
    case VP_P2 + 4: return 96;                       /* (COMB's note: C1 .. C7) */
    case VP_P2_LAST: return 2;
    case VP_P2: case VP_P2 + 1: case VP_P2 + 2: case VP_P2 + 3: case VP_P2 + 5: case VP_P2 + 6: return 127;
    case VP_LSTART: return link_start(shown_pat()->link[ui.link & 7u]) > NSTART ? NSTART + 1u : NSTART;
    case VP_LAMT: {
        uint32_t e = shown_pat()->link[ui.link & 7u][1] & 15u;
        return e && e < NLE ? (uint32_t)(LK_HI[e] - LK_LO[e]) : 0u;
    }
    case VP_DEPTH: return 127;
    case VP_KOCT: return 6;
    case VP_KVEL: case VP_KACC: return 127;
    case VP_LWHEN: return NTRACK * NLV - 1u;
    case VP_LTGT: return NTRACK - 1u;
    case VP_LEFF: return NLE - 1u;
    case VP_LWIN: return NLW - 1u;
    case VP_GA: case VP_GB: case VP_GC: {           /* by the generator: see seq.c */
        static const uint8_t MX[NGEN][3] = {{0, 0, 0}, {100, 0, 1}, {16, 127, 127}, {255, 0, 1}, {2, 127, 1}};
        uint32_t g = shown_pat()->gen[ui.track][0];
        return MX[g < NGEN ? g : 0][p - VP_GA];
    }
    default: return p < NPAR ? par_max(p) : 0u;
    }
}

static void palette_apply(void) { palette_set(kset.palette % NPALETTES); }

static void vp_set(uint32_t p, uint32_t v)
{
    pattern_t *pt = shown_pat();
    switch (p) {
    case PAR_TRACK_LENGTH:
        pt->len[ui.track] = (uint8_t)v;
        if (ui.mode == M_LAST && ui.last_all) {
            uint32_t t;
            for (t = 0; t < NTRACK; t++)
                pt->len[t] = (uint8_t)v;
        }
        return;
    case PAR_STEP_VOLUME:
        sel_step()->volume = (uint8_t)((sel_step()->volume & STEP_ACTIVE_MASK) | v);
        return;
    case PAR_STEP_NOTE: sel_step()->note = (uint8_t)v; return;
    case PAR_STEP_PROB: sel_step()->prob = (uint8_t)v; return;
    case PAR_STEP_FLAM:
        pt->timing[ui.track][ui.step & 63u] = (uint8_t)((pt->timing[ui.track][ui.step & 63u] & 0xF0u) | v);
        return;
    case PAR_STEP_SHIFT:
        pt->timing[ui.track][ui.step & 63u] = (uint8_t)((pt->timing[ui.track][ui.step & 63u] & 0x0Fu) | v << 4);
        return;
    case PAR_P1_DEST:
    case PAR_P2_DEST: {
        uint32_t d = v ? par_to_autodest(lock_target_par(v)) : NO_AUTOMATION;
        lock_set(pt, ui.track, ui.step & 63u, p == PAR_P2_DEST, d);
        return;
    }
    case PAR_P1_VAL: sel_step()->p1v = (uint8_t)v; return;
    case PAR_P2_VAL: sel_step()->p2v = (uint8_t)v; return;
    case PAR_EUKLID_LENGTH:
    case PAR_EUKLID_STEPS:
    case PAR_EUKLID_ROTATION:
        if (p == PAR_EUKLID_LENGTH) {
            eu_len[ui.track] = (uint8_t)v;
            if (eu_steps[ui.track] > v)
                eu_steps[ui.track] = (uint8_t)v;
            if (eu_rot[ui.track] >= v)
                eu_rot[ui.track] = 0;
        } else if (p == PAR_EUKLID_STEPS) {
            eu_steps[ui.track] = (uint8_t)v;
        } else {
            eu_rot[ui.track] = (uint8_t)v;
        }
        if (ui.eu_undo != ui.track + 1u) {           /* (UNDO: the track before the first turn) */
            seq_undo_save(ui.track);
            ui.eu_undo = (uint8_t)(ui.track + 1u);
        }
        euclid_generate(ui.track, pt);
        sq.gsync |= (uint8_t)(1u << ui.track);
        return;
    case PAR_ROLL: sq.roll_rate = (uint8_t)v; par[PAR_ROLL] = (uint8_t)v; sq.roll_free = 0; return;
    case PAR_MORPH: kit_set_morph(v); return;
    case PAR_AUTOM_TRACK: sq.lane = (uint8_t)v; return;
    case PAR_SHUFFLE: par[PAR_SHUFFLE] = (uint8_t)v; pat[sq.active].shuffle = (uint8_t)v; return;
    case VP_TRACK_VOL: par_set(PAR_VOL1 + track_voice(ui.track), v); return;
    case VP_FX_SUB:
        if (fx.type >= FX_DRIVE && fx.type <= FX_DELAY)
            par_set(fx.type == FX_DRIVE ? PAR_FX_DIST_TYPE : fx.type == FX_RING ? PAR_FX_WAVE
                    : fx.type == FX_COMP ? PAR_FX_RATIO : PAR_FX_DELAY_TYPE, v);
        return;
    case VP_IO_TYPE:
        ui.io_type = (uint8_t)v;
        ui.io_slot = (uint8_t)(io_lib() ? io_slot_default(v) : 0u);
        ui.bank = (uint8_t)(ui.io_slot / 16u);
        bank_fix();
        return;
    case VP_IO_SLOT: ui.io_slot = (uint8_t)v; return;
    case VP_IO_SRC:
        ui.io_src = (uint8_t)v;
        ui.io_slot = io_slot_default(ui.io_type);
        ui.bank = (uint8_t)(ui.io_slot / 16u);
        return;
    case VP_IO_TRACK: track_select(v); return;
    case VP_NAME_POS: ui.io_pos = (uint8_t)v; return;
    case VP_NAME_CHAR: ui.io_name[ui.io_pos & 7u] = (char)(v < 32u ? 32u : v); return;
    case VP_NAME_CASE: {
        char c = ui.io_name[ui.io_pos & 7u];
        if (v && c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        else if (!v && c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        ui.io_name[ui.io_pos & 7u] = c;
        return;
    }
    case VP_SONG_PAT:
        songs[sq.song][ui.song_row & 63u] = (uint8_t)(v < NUM_PATTERN ? v : v == NUM_PATTERN ? SONG_END : SONG_LOOP);
        return;
    case VP_SONG_NUM: sq.song = (uint8_t)v; return;
    case VP_LEDS: kset.leds = (uint8_t)v; settings_later(); return;
    case VP_AUTOSAVE: autosave_on = (uint8_t)v; settings_later(); return;
    case VP_PKEEP: pat_keep = (uint8_t)v; kset.pdrop = (uint8_t)!v; settings_later(); return;
    case VP_ACCEL: kset.accel = (uint8_t)v; settings_later(); return;
    case VP_PALETTE: kset.palette = (uint8_t)v; palette_apply(); settings_later(); ui.force = 1; return;
    case VP_NONE: case VP_MASTER: return;
    case VP_DJ: pf.dj = (uint8_t)v; return;
    case VP_REPEAT: pfs.repeat = (uint8_t)v; return;
    case VP_TAPE: pfs.tape = (uint8_t)v; return;
    case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3: macro_set(p - VP_M1, v); return;
    case VP_MSEL: ui.msel = (uint8_t)v; return;
    case VP_MTARGET: {
        uint32_t q = lock_target_par(v);
        pfs.m[ui.msel].par = (uint16_t)q;
        pfs.m[ui.msel].lo = 0;
        pfs.m[ui.msel].hi = (uint8_t)macro_max(ui.msel);
        if (q)
            pfs.m[ui.msel].val = (uint8_t)(par[q] * 127u / (macro_max(ui.msel) ? macro_max(ui.msel) : 1u));
        return;
    }
    case VP_MMIN: pfs.m[ui.msel].lo = (uint8_t)v; return;
    case VP_MMAX: pfs.m[ui.msel].hi = (uint8_t)v; return;
    case VP_COND: pt->cond[ui.track][ui.step & 63u] = (uint8_t)v; return;
    case VP_RAT_N: case VP_RAT_RATE: case VP_RAT_FADE: {
        uint8_t *r = &pt->rat[ui.track][ui.step & 63u];
        uint32_t sh = p == VP_RAT_N ? 0u : p == VP_RAT_RATE ? 3u : 5u, m = p == VP_RAT_N ? 7u : 3u;
        *r = (uint8_t)((*r & ~(m << sh)) | v << sh);
        return;
    }
    case VP_ACTION: ui.action = (uint8_t)v; return;
    case VP_GMODE: {                                /* a generator: its settings from their defaults, its live track */
        static const uint8_t DEF[NGEN][3] = {{0, 0, 0}, {25, 0, 0}, {8, 64, 64}, {30, 0, 0}, {0, 64, 1}};
        uint8_t *g = pt->gen[ui.track];
        if (g[0] == v)
            return;
        g[0] = (uint8_t)v;
        memcpy(g + 1, DEF[v < NGEN ? v : 0], 3);
        if (v == GEN_GRIDS)                          /* (the part from the track: drums BD, snare SD, metal HH) */
            g[1] = (uint8_t)(ui.track < 3u ? 0u : ui.track == 3u ? 1u : 2u);
        sq.gsync |= (uint8_t)(1u << ui.track);
        return;
    }
    case VP_GA: case VP_GB: case VP_GC:
        pt->gen[ui.track][p - VP_GA + 1u] = (uint8_t)v;
        if (pt->gen[ui.track][0] == GEN_GRIDS)       /* (GRIDS: the live track again from the map) */
            sq.gsync |= (uint8_t)(1u << ui.track);
        return;
    case VP_CLOCK: pt->clk[ui.track] = (uint8_t)v; return;
    case VP_RAT_SHAPE: {
        uint8_t *r = &pt->rat[ui.track][ui.step & 63u];
        *r = (uint8_t)(v ? *r | RAT_BOUNCE : *r & ~RAT_BOUNCE);
        return;
    }
    case VP_BSTART: pt->bnc[ui.track] = (uint8_t)((pt->bnc[ui.track] & ~3u) | v); return;
    case VP_BPITCH: pt->bnc[ui.track] = (uint8_t)((pt->bnc[ui.track] & 3u) | v << 2); return;
    case VP_ROLL: sq.roll_k = (uint8_t)v; sq.roll_free = 1; return;
    case VP_LINK: ui.link = (uint8_t)v; return;
    case VP_P2: case VP_P2 + 1: case VP_P2 + 2: case VP_P2 + 3: case VP_P2 + 4: case VP_P2 + 5: case VP_P2 + 6:
    case VP_P2_LAST: (&pfs.drill)[p - VP_P2] = (uint8_t)v; return;
    case VP_LSTART: {
        uint8_t *L = pt->link[ui.link & 7u];
        if (v == 0u)
            memset(L, 0, 4);
        else if (v <= NSTART)
            memcpy(L, LK_START[v - 1u], 4);
        return;
    }
    case VP_LAMT: {
        uint8_t *L = pt->link[ui.link & 7u];
        uint32_t e = L[1] & 15u;
        if (e && e < NLE)
            L[2] = (uint8_t)(int8_t)((int32_t)v + LK_LO[e]);
        return;
    }
    case VP_DEPTH: pt->cdepth = (uint8_t)v; return;
    case VP_KOCT: ui.koct = (uint8_t)v; return;
    case VP_KVEL: key_vel = (uint8_t)v; kset.kvel = (uint8_t)v; settings_later(); return;
    case VP_KACC: key_acc = (uint8_t)v; kset.kacc = (uint8_t)v; settings_later(); return;
    case VP_LWHEN: {
        uint8_t *L = pt->link[ui.link & 7u];
        L[0] = (uint8_t)((L[0] & 0x38u) | v / NLV | (v % NLV) << 6);
        return;
    }
    case VP_LTGT: {
        uint8_t *L = pt->link[ui.link & 7u];
        L[0] = (uint8_t)((L[0] & ~0x38u) | v << 3);
        return;
    }
    case VP_LEFF: {                                  /* a new effect: its amount from where it starts */
        uint8_t *L = pt->link[ui.link & 7u];
        if (!L[0] && !(L[1] & 15u))                  /* (a new link: from the track shown to the next) */
            L[0] = (uint8_t)(ui.track | (ui.track + 1u) % NTRACK << 3);
        L[1] = (uint8_t)((L[1] & 0xF0u) | v);
        L[2] = (uint8_t)LK_DEF[v < NLE ? v : 0u];
        return;
    }
    case VP_LWIN: {
        uint8_t *L = pt->link[ui.link & 7u];
        L[1] = (uint8_t)((L[1] & 15u) | v << 4);
        return;
    }
    case VP_GX: case VP_GY: case VP_GCHAOS: {       /* the pattern's map: every GRIDS track again from it */
        uint32_t t;
        if (p == VP_GX)
            pt->gx = (uint8_t)v;
        else if (p == VP_GY)
            pt->gy = (uint8_t)v;
        else
            pt->gchaos = (uint8_t)v;
        for (t = 0; t < NTRACK; t++)
            if (pt->gen[t][0] == GEN_GRIDS && !sq.running)
                sq.gsync |= (uint8_t)(1u << t);      /* (playing, each step reads the map as it comes) */
        return;
    }
    default:
        if (p >= PAR_VOICE_LFO1 && p <= PAR_VOICE_LFO6) {     /* a new voice: its target moves along */
            uint32_t l = p - PAR_VOICE_LFO1, old = par[p], tp = PAR_TARGET_LFO1 + l, idx = par[tp];
            uint32_t ov = old >= 1u && old <= 6u ? old - 1u : 0u, nv = v >= 1u && v <= 6u ? v - 1u : 0u;
            par[p] = (uint8_t)v;
            if (idx >= MOD_TARGET_VOICE[ov][0] && idx <= MOD_TARGET_VOICE[ov][1]) {
                idx = idx - MOD_TARGET_VOICE[ov][0] + MOD_TARGET_VOICE[nv][0];
                if (idx > MOD_TARGET_VOICE[nv][1])
                    idx = MOD_TARGET_VOICE[nv][1];
                par_set(tp, idx);
            }
            return;
        }
        if ((p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) || (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6)) {
            uint32_t lo, hi;
            target_range(p, &lo, &hi);
            if (v && v < lo)                         /* 0 (off), or inside the voice's targets */
                v = lo;
        }
        par_set(p, v);
        return;
    }
}

/* a knob: n detents on parameter p (accelerated for wide ranges) */
static void knob_turn(uint32_t k, int32_t n)
{
    uint16_t pp[4];
    uint32_t p, mn, mx;
    int32_t v, step = n;
    page_params(pp);
    p = pp[k];
    if (p == VP_NONE || !n)
        return;
    mn = vp_min(p);
    mx = vp_max(p);
    if (kset.accel && mx - mn > 32u) {
        uint32_t dt = fm1_ms - ui.knob_ms[k];
        if (dt < 18u)
            step = n * 4;
        else if (dt < 40u)
            step = n * 2;
    }
    ui.knob_ms[k] = fm1_ms;
    ui.hot = (uint8_t)(k + 1u);
    ui.hot_ms = fm1_ms;
    /* a step key held: that step's lock (sequencer automation), in the lane that has the parameter or is free */
    if (ui.held_step >= 0 && par_lockable(p) && ui.mode != M_STEP) {
        pattern_t *pt = shown_pat();
        step_t *s = &pt->step[ui.track][ui.held_step];
        uint32_t dest = par_to_autodest(p), lane = sq.lane, hs = (uint32_t)ui.held_step;
        uint32_t d1 = lock_get(pt, ui.track, hs, 0), d2 = lock_get(pt, ui.track, hs, 1);
        uint8_t *dv;
        if (lane == 0u && d1 != dest && d1 != NO_AUTOMATION && (d2 == dest || d2 == NO_AUTOMATION))
            lane = 1;
        else if (lane == 1u && d2 != dest && d2 != NO_AUTOMATION && (d1 == dest || d1 == NO_AUTOMATION))
            lane = 0;
        dv = lane ? &s->p2v : &s->p1v;
        if ((lane ? d2 : d1) != dest) {
            lock_set(pt, ui.track, hs, lane, dest);
            *dv = par[p];
        }
        v = clamp32((int32_t)*dv + step, (int32_t)mn, (int32_t)(mx > 127u ? 127u : mx));
        *dv = (uint8_t)v;
        main_set(pt, ui.track, (uint32_t)ui.held_step, 1);
        ui.lock_turned = 1;
        return;
    }
    v = clamp32((int32_t)vp_get(p) + step, (int32_t)mn, (int32_t)mx);
    vp_set(p, (uint32_t)v);
    if (sq.rec && par_lockable(p))                   /* knob movements record to the playing step (REC) */
        seq_record_automation(ui.track, p, (uint32_t)v);
}

/* ------------------------------------------------------------ modes --- */
static void mode_set(uint32_t m)
{
    if (ui.mode != m) {
        ui.page = 0;
        ui.io_naming = 0;
        ui.sb_open = 0;                              /* (the sound browser: what it loaded stays) */
    }
    if ((m == M_HOME || m == M_STEP || m == M_PATTERN || m == M_SONG || m == M_KEYS) && ui.perf) {
        uint32_t t;
        ui.perf = 0;                                 /* (PERF left: what it latched lets go) */
        ui.pf_latch = 0;
        pf.on = ui.pf_held;
        for (t = 0; t < NTRACK; t++)
            if (ui.roll_latch & (1u << t))
                seq_set_roll(t, 0);
        ui.roll_latch = 0;
        ui.drill_latch = 0;
        sq.drill = ui.drill_held;
    }
    if (m == M_PERF)
        ui.perf = 1;
    if (m == M_KEYS) {                               /* 0.8: KEYS: the keys play notes (PERF's let go: above) */
        ui.kplay = 1;
        ui.held_step = -1;
        sq.armed_step = sq.armed_track = -1;
    } else if (m != M_SOUND) {
        ui.kplay = 0;                                /* (the sound pages keep playing them) */
    }
    if (m != M_STEP)
        ui.back = (uint8_t)m;
    if (m == M_CONFIG && ui.mode != M_CONFIG)
        audio_max_us = 0;                            /* (the CPU meter's peak: from now) */
    if (m != ui.mode) {
        ui.action = 0;
        ui.eu_undo = 0;
    }
    ui.mode = (uint8_t)m;
    ui.clear_open = 0;
    ui.force = 1;
    bank_fix();
}
static void sound_open(uint32_t s, int again)        /* a section button; again: its next section */
{
    static const uint8_t NEXT[NSECT] = {S_FM, S_MOD, S_AEG, S_CLICK, S_MODEL, S_SHAPE, S_RANDOM, S_MIX,
                                        S_FILTER, S_XMOD, S_LFO, S_OSC};
    if (ui.mode == M_SOUND && again) {
        if (sect_group(ui.sect) == s) {
            uint8_t l[NSPG][2];
            uint32_t n = sound_pages(l), i;
            if (sect_half_used(ui.track, ui.sect, 1) && ui.half == 0) {   /* the section's second half first */
                ui.half = 1;
                ui.force = 1;
                return;
            }
            ui.sect = NEXT[ui.sect];
            ui.half = 0;
            for (i = 0; i < n && !(l[i][0] == ui.sect); i++)
                ;
            if (i == n)                              /* (none on this track: its next one) */
                ui.sect = NEXT[ui.sect];
            sound_fix();
            ui.force = 1;
            return;
        }
    }
    mode_set(M_SOUND);
    ui.sect = (uint8_t)s;
    ui.half = 0;
    sound_fix();
}

static void put_pad8(char *d, const char *s)          /* an 8-character name: s, spaces after */
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        d[i] = *s ? *s++ : ' ';
}
static uint32_t io_slot_default(uint32_t type)
{
    if (type == IO_SOUND)
        return ui.lib_sound;
    if ((type == IO_KIT || type == IO_MORPH) && ui.io_src)
        return ui.lib_kit;
    return type == IO_KIT ? kit_loaded : type == IO_MORPH ? kit_morph_loaded : type == IO_PATTERN ? sq.active
         : type == IO_SONG ? sq.song : type == IO_FACTORY ? 0u : (pj.loaded < PROJ_USER ? pj.loaded : 0u);
}
static void io_open(uint32_t m, uint32_t type)
{
    if (m == M_SAVE && type > IO_PROJECT)
        type = IO_KIT;                                /* (FACTORY: nothing to save to) */
    mode_set(m);
    ui.io_type = (uint8_t)type;
    ui.io_slot = (uint8_t)io_slot_default(type);
    ui.io_naming = 0;
    if (type != IO_PROJECT)
        ui.bank = (uint8_t)(ui.io_slot / 16u);
}

/* OK in SAVE / LOAD */
static void io_ok(void)
{
    uint32_t s = ui.io_slot;
    char b[32];
    if (ui.mode == M_SAVE) {
        if ((ui.io_type == IO_KIT || ui.io_type == IO_MORPH || ui.io_type == IO_SOUND || ui.io_type == IO_PROJECT) &&
            !ui.io_naming) {
            ui.io_naming = 1;                         /* the name first */
            ui.io_pos = 0;
            if (ui.io_type == IO_PROJECT)
                memcpy(ui.io_name, proj_exists(s) ? pj.name[s] : "PROJECT ", 8);
            else if (ui.io_type == IO_SOUND && kl_name(KLK_SOUND, s, ui.io_name))
                put_pad8(ui.io_name, VOICE_NAME[track_voice(ui.track)]);
            else if (ui.io_type != IO_SOUND && (!ui.io_src || kl_name(KLK_KIT, s, ui.io_name)))
                memcpy(ui.io_name, kit_names[ui.io_src ? kit_loaded : s], 8);
            ui.force = 1;
            return;
        }
        switch (ui.io_type) {
        case IO_KIT:
        case IO_MORPH:
        case IO_SOUND:
            if (io_lib()) {
                int rc = ui.io_type == IO_SOUND ? libsound_save(s, ui.io_name, track_voice(ui.track))
                                                : libkit_save(s, ui.io_name, ui.io_type == IO_MORPH);
                if (ui.io_type == IO_SOUND)
                    ui.lib_sound = (uint8_t)s;
                else
                    ui.lib_kit = (uint8_t)s;
                ui_message(rc == 0 ? (ui.io_type == IO_SOUND ? "SOUND SAVED" : "KIT SAVED TO LIBRARY")
                           : rc == -20 ? "LIBRARY FULL" : "SAVE FAILED");
                break;
            }
            kit_save_ram(s, ui.io_type == IO_MORPH);
            memcpy(kit_names[s], ui.io_name, 8);
            ui_message("KIT SAVED");
            break;
        case IO_PATTERN:                              /* preset_savePatternRAM */
            pat[sq.active].kit = kit_loaded;
            pat[sq.active].shuffle = par[PAR_SHUFFLE];
            if (s == sq.active) {
                pat_backup = pat[s];
            } else {
                if (par[PAR_PATTERN_MODE] == 1u)
                    par[PAR_PATTERN_MODE] = 2;       /* (no kit reload when it switches) */
                pat[s] = pat[sq.active];
                seq_set_next_pattern(s);
            }
            ui_message("PATTERN SAVED");
            break;
        case IO_SONG:
            if (s != sq.song)
                memcpy(songs[s], songs[sq.song], MAX_SONG_LENGTH);
            sq.song = (uint8_t)s;
            ui_message("SONG SAVED");
            break;
        case IO_PROJECT: {
            int rc;
            ui_message("SAVING PROJECT...");
            ui_draw();
            rc = proj_save(s, ui.io_name);
            if (rc == 0) {
                pj.loaded = (uint8_t)s;
                kset.project = (uint8_t)s;
                settings_later();
                ui_message("PROJECT SAVED");
            } else {
                put_str(b, rc == -20 ? "PROJECT TOO BIG" : "SAVE FAILED", sizeof b);
                ui_message(b);
            }
            break;
        }
        default:
            break;
        }
        ui.io_naming = 0;
        return;
    }
    switch (ui.io_type) {                             /* LOAD */
    case IO_KIT:
        if (ui.io_src) {                              /* a library kit: the edit buffer, the slot it came from stays */
            ui.lib_kit = (uint8_t)s;
            ui_message(libkit_load(s, 0) ? "EMPTY SLOT" : "KIT LOADED");
            break;
        }
        kit_load_ram(s);
        ui_message("KIT LOADED");
        break;
    case IO_SOUND: {
        int rc = libsound_load(s, track_voice(ui.track));
        if (rc == 0)
            ui.lib_sound = (uint8_t)s;
        ui_message(rc == 0 ? "SOUND LOADED" : rc == -2 ? "NOT THIS TRACK'S KIND" : "EMPTY SLOT");
        break;
    }
#if KETTLE_FACTORY_KITS
    case IO_FACTORY:
        if (s >= FACTORY_NKITS) {                     /* ALL: the bank's first 64 into the slots */
            kit_factory_fill();
            kit_load_ram(kit_loaded);
            ui_message("FACTORY KITS IN SLOTS 1-64");
        } else {
            kit_factory_load(s);
            ui_message("FACTORY KIT LOADED");
        }
        break;
#endif
    case IO_MORPH:
        if (ui.io_src) {
            ui.lib_kit = (uint8_t)s;
            ui_message(libkit_load(s, 1) ? "EMPTY SLOT" : "MORPH TARGET LOADED");
            break;
        }
        kit_load_morph(s);
        ui_message("MORPH TARGET LOADED");
        break;
    case IO_PATTERN:
        seq_set_next_pattern(s);
        ui_message(sq.running ? "PATTERN QUEUED" : "PATTERN LOADED");
        break;
    case IO_SONG:
        sq.song = (uint8_t)s;
        ui_message("SONG LOADED");
        break;
    case IO_PROJECT:
        if (!proj_exists(s)) {
            ui_message("EMPTY SLOT");
            break;
        }
        if (sq.running)
            seq_stop_now();
        ui_message("LOADING PROJECT...");
        ui_draw();
        if (proj_load(s) == 0) {
            pj.loaded = (uint8_t)s;
            kset.project = (uint8_t)s;
            settings_later();
            ui_message("PROJECT LOADED");
        } else {
            ui_message("LOAD FAILED");
        }
        break;
    default:
        break;
    }
}

/* CLEAR (SHIFT + COPY): what it clears */
enum { CL_TRACK, CL_PATTERN, CL_AUTO1, CL_AUTO2, CL_KIT, CL_SONG_ROW, NCLEAR };
static const char *const CLEAR_NAME[NCLEAR] = {"CLEAR TRACK", "CLEAR PATTERN", "CLEAR AUTOMATION 1",
                                               "CLEAR AUTOMATION 2", "CLEAR KIT (INIT)", "DELETE SONG ROW"};
static void clear_do(void)
{
    pattern_t *pt = shown_pat();
    switch (ui.clear_sel) {
    case CL_TRACK: seq_clear_track(ui.track, pt); ui_message("TRACK CLEARED"); break;
    case CL_PATTERN: seq_clear_pattern(pt); ui_message("PATTERN CLEARED"); break;
    case CL_AUTO1: seq_clear_automation(ui.track, pt, 0); ui_message("AUTOMATION 1 CLEARED"); break;
    case CL_AUTO2: seq_clear_automation(ui.track, pt, 1); ui_message("AUTOMATION 2 CLEARED"); break;
    case CL_KIT:
        kit_init_par();
        snd_post_all();
        ui_message("KIT CLEARED");
        break;
    case CL_SONG_ROW: song_delete_row(sq.song, ui.song_row); ui_message("ROW DELETED"); break;
    default: break;
    }
    ui.clear_open = 0;
    ui.force = 1;
}

static void track_select(uint32_t t)
{
    ui.track = (uint8_t)(t < NTRACK ? t : NTRACK - 1u);
    ui.sb_open = 0;
    if (ui.mode == M_SOUND)
        sound_fix();
    ui.force = 1;
}

/* 0.8: the sound browser: entry e into the track's voice, and a hit of it */
static void sb_audition(uint32_t e)
{
    uint8_t v[SOUND_MAX];
    uint32_t from, n = sb_values(e, ui.sb_voice, v, &from);
    if (n)
        voice_put_par(ui.sb_voice, v, n, from);
    key_note_post(ui.track, SEQ_DEFAULT_NOTE, key_vel, 0);
}
static void sb_turn(int32_t n)                       /* PRESETS on a sound page: opens it, moves through it */
{
    if (!ui.sb_open) {
        ui.sb_voice = (uint8_t)track_voice(ui.track);
        sb_build(ui.sb_voice);
        if (!sb_n) {
            ui_message("NO SOUNDS FOR THIS TRACK");
            return;
        }
        ui.sb_on = (uint8_t)voice_get_par(ui.sb_voice, ui.sb_orig);
        ui.sb_open = 1;
        ui.sb_sel = 0;
    } else {
        ui.sb_sel = (uint16_t)clamp32((int32_t)ui.sb_sel + n, 0, (int32_t)sb_n - 1);
    }
    sb_audition(ui.sb_sel);
}
static void sb_close(int keep)                       /* OCT+: keeps what it loaded, OCT-: the sound as it was */
{
    if (!keep)
        voice_put_par(ui.sb_voice, ui.sb_orig, ui.sb_on, ui.sb_voice);
    ui.sb_open = 0;
    ui_message(keep ? "SOUND LOADED" : "SOUND AS IT WAS");
}

/* ------------------------------------------------------------- keys --- */
/* PERF's keys: 1..7 roll their track, 8..16 the effects (perform.c PF_*); SHIFT + a key latches it (again: lets
 * go), a key on what is latched lets go of it. 0.6: on the PERFORM 2 pages, the second bank (1..7: DRILL); a key
 * lets go in the bank it went down in */
static void perf_key(uint32_t w, int down)
{
    int b2;
    if (down)
        ui.kbank = (uint16_t)(perf_bank2() ? ui.kbank | 1u << w : ui.kbank & ~(1u << w));
    b2 = (ui.kbank >> w) & 1u;
    if (w < NTRACK && b2) {                          /* DRILL */
        uint8_t b = (uint8_t)(1u << w);
        if (down && ui.shift) {
            ui.shift_used = 1;
            ui.drill_latch ^= b;
        } else if (down && (ui.drill_latch & b)) {
            ui.drill_latch &= (uint8_t)~b;
        } else if (down) {
            ui.drill_held |= b;
        } else {
            ui.drill_held &= (uint8_t)~b;
        }
        sq.drill = (uint8_t)(ui.drill_held | ui.drill_latch);
    } else if (w < NTRACK) {
        uint8_t b = (uint8_t)(1u << w);
        if (down && ui.shift) {
            ui.shift_used = 1;
            ui.roll_latch ^= b;
            seq_set_roll(w, (ui.roll_latch & b) != 0u);
        } else if (down) {
            ui.roll_latch &= (uint8_t)~b;
            seq_set_roll(w, 1);
        } else if (!(ui.roll_latch & b)) {
            seq_set_roll(w, 0);
        }
    } else {
        uint32_t e = (b2 ? NPF : 0u) + w - NTRACK, b = 1u << e;
        if (down && ui.shift) {
            ui.shift_used = 1;
            ui.pf_latch ^= b;
        } else if (down && (ui.pf_latch & b)) {
            ui.pf_latch &= ~b;
        } else if (down) {
            ui.pf_held |= b;
        } else {
            ui.pf_held &= ~b;
        }
        if (down && (e == PF_THROW_R || e == PF_THROW_F))
            pf.throw_v = (uint8_t)track_voice(ui.track);
        if (down && e == PF_FREEZE && fx.type != FX_DELAY)
            ui_message("FREEZE: THE FX BUS IS NOT A DELAY");
        pf.on = ui.pf_held | ui.pf_latch;
    }
    ui.force = 1;
}

/* 0.8: KEYS: key k (semitones above F3) plays the track's voice at its note, the octave KEYS' knob sets; SHIFT: the
 * accent. As a MIDI note: recorded into the steps under REC (midi.c key_service) */
static void key_play(uint32_t k)
{
    int32_t n = 53 + (int32_t)k + 12 * ((int32_t)ui.koct - 3);
    if (ui.shift)
        ui.shift_used = 1;
    ui.knote = (uint8_t)clamp32(n, 0, 127);
    key_note_post(ui.track, ui.knote, ui.shift ? key_acc : key_vel, 1);
    ui.force = 1;
}

static void white_down(uint32_t w)
{
    uint32_t s = ui.bar * 16u + w;
    pattern_t *pt = shown_pat();
    if (ui.kplay) {
        key_play(WHITE_KEY[w]);
        return;
    }
    if (ui.perf && ui.shift) {                        /* PERF: SHIFT latches */
        perf_key(w, 1);
        return;
    }
    if (ui.shift) {                                   /* the LXR's SHIFT + step functions */
        ui.shift_used = 1;
        switch (w) {
        case 7: io_open(M_LOAD, IO_KIT); return;
        case 8: io_open(M_LOAD, IO_MORPH); return;
        case 9: mode_set(M_SONG); return;
        case 10: io_open(M_LOAD, IO_PROJECT); return;
        case 11: mode_set(M_CONFIG); return;
        case 12:                                      /* RELOAD: the kit and the pattern as saved */
            kit_load_ram(kit_loaded);
            sq.pending = sq.active;
            sq.load_pending = 1;
            pat_revert = 1;
            if (!sq.running)
                seq_pattern_activate();
            ui_message("RELOADED");
            return;
        case 13:
            if (ui.mode == M_LAST)
                ui.last_all ^= 1u;
            else {
                mode_set(M_LAST);
                ui.last_all = 0;
            }
            ui.force = 1;
            return;
        case 14: mode_set(M_FX); return;
        case 15: mode_set(M_PATTERN); return;
        default:                                      /* 1..7: play the voice (at the keys' velocity) */
            key_note_post(w, SEQ_DEFAULT_NOTE, key_vel, 0);
            return;
        }
    }
    switch (ui.mode) {
    case M_PATTERN: {
        uint32_t n = ui.bank * 16u + w;
        if (n >= NUM_PATTERN)
            return;
        if (ui.copy) {
            ui.copy_used = 1;
            if (!ui.copy_src) {
                ui.copy_src = (uint8_t)(n + 1u);
                ui_message("COPY: PICK THE TARGET");
            } else {
                pat[n] = pat[ui.copy_src - 1u];
                if (n == sq.active)
                    pat_backup = pat[n];
                ui.copy_src = 0;
                ui_message("PATTERN COPIED");
            }
            return;
        }
        seq_set_next_pattern(n);
        return;
    }
    case M_SONG:
        if (ui.bank * 16u + w >= NUM_PATTERN)
            return;
        songs[sq.song][ui.song_row & 63u] = (uint8_t)(ui.bank * 16u + w);
        if (ui.song_row < MAX_SONG_LENGTH - 1u)
            ui.song_row++;
        ui.force = 1;
        return;
    case M_SAVE:
    case M_LOAD:
        if (ui.io_type != IO_PROJECT && ui.bank * 16u + w > vp_max(VP_IO_SLOT))
            return;
        if (ui.io_type == IO_PATTERN) {
            ui.io_slot = (uint8_t)(ui.bank * 16u + w);
            io_ok();
        } else if (ui.io_type != IO_PROJECT) {
            ui.io_slot = (uint8_t)(ui.bank * 16u + w);
            ui.force = 1;
        }
        return;
    case M_LAST:
        vp_set(PAR_TRACK_LENGTH, s + 1u);
        return;
    case M_STEP:
        if (ui.step == s) {                           /* the selected step again: on / off */
            step_toggle(pt, ui.track, s);
        } else {
            ui.step = (uint8_t)s;
        }
        ui.force = 1;
        return;
    default:
        break;
    }
    if (ui.perf) {                                   /* PERF: rolls, FILL, the effects */
        perf_key(w, 1);
        return;
    }
    /* the voice pages: the step on / off; held, the knobs set its locks */
    step_toggle(pt, ui.track, s);
    ui.held_step = (int8_t)s;
    ui.lock_turned = 0;
    sq.armed_step = (int8_t)s;
    sq.armed_track = (int8_t)ui.track;
}
static void white_up(uint32_t w)
{
    uint32_t b2 = (ui.kbank >> w) & 1u;
    if (ui.kplay)
        return;
    if (ui.perf || (w >= NTRACK && (ui.pf_held & (1u << ((b2 ? NPF : 0u) + w - NTRACK)))) ||
        (w < NTRACK && b2 && (ui.drill_held & (1u << w))))
        perf_key(w, 0);                               /* (an effect held when PERF was left lets go too) */
    if (ui.held_step == (int8_t)(ui.bar * 16u + w)) {
        ui.held_step = -1;
        sq.armed_step = sq.armed_track = -1;
    }
}
static void black_down(uint32_t k)
{
    pattern_t *pt = shown_pat();
    if (ui.kplay) {
        key_play(BLACK_KEY[k]);
        return;
    }
    if (k < NTRACK) {
        uint32_t t = k;
        if (ui.copy && ui.mode != M_PATTERN) {        /* COPY + a track, then the target */
            ui.copy_used = 1;
            if (!ui.copy_src) {
                ui.copy_src = (uint8_t)(t + 1u);
                ui_message("COPY: PICK THE TARGET");
            } else {
                seq_copy_track(ui.copy_src - 1u, t, pt);
                ui.copy_src = 0;
                ui_message("TRACK COPIED");
            }
            return;
        }
        if (ui.shift) {
            ui.shift_used = 1;
            if (ui.perf) {                            /* PERF + SHIFT: unmute the tracks up to this one */
                sq.muted &= (uint8_t)~((2u << t) - 1u);
            } else {
                sq.muted ^= (uint8_t)(1u << t);
            }
            ui.force = 1;
            return;
        }
        if (ui.perf && ui.mode == M_PERF) {
            sq.muted ^= (uint8_t)(1u << t);
            ui.force = 1;
            return;
        }
        track_select(t);
        return;
    }
    k -= NTRACK;                                      /* the bars */
    if (ui.mode == M_PERF) {                          /* PERF: the bar keys pick the STUTTER length */
        pfs.repeat = (uint8_t)k;
        ui.force = 1;
        return;
    }
    if (ui_banks()) {
        if (k < ui_banks())
            ui.bank = (uint8_t)k;
        ui.force = 1;
        return;
    }
    if (ui.shift) {                                   /* SHIFT + bar: the track's length, bar by bar */
        ui.shift_used = 1;
        vp_set(PAR_TRACK_LENGTH, (k + 1u) * 16u);
        ui_message("LENGTH SET");
        return;
    }
    if (ui.copy) {                                    /* COPY + bar: the shown bar into it (the grid: every track) */
        uint32_t t;
        ui.copy_used = 1;
        if (k == ui.bar)
            return;
        if (ui.mode == M_HOME) {
            for (t = 0; t < NTRACK; t++)
                bar_copy(pt, t, ui.bar, k, 1);
            ui_message("BAR COPIED: ALL TRACKS");
        } else {
            bar_copy(pt, ui.track, ui.bar, k, 0);
            ui_message("BAR COPIED");
        }
        return;
    }
    ui.bar = (uint8_t)k;
    ui.force = 1;
}

static void seq_copy_track(uint32_t src, uint32_t dst, pattern_t *p)   /* seq_copyTrack */
{
    uint32_t k;
    for (k = 0; k < NUM_STEPS; k++) {
        p->step[dst][k] = p->step[src][k];
        p->timing[dst][k] = p->timing[src][k];
        p->lhi[dst][k] = p->lhi[src][k];
        p->cond[dst][k] = p->cond[src][k];
        p->rat[dst][k] = p->rat[src][k];
    }
    for (k = 0; k < 4u; k++)
        p->main[dst][k] = p->main[src][k];
    p->len[dst] = p->len[src];
    p->clk[dst] = p->clk[src];
    p->bnc[dst] = p->bnc[src];
    sq.gsync |= (uint8_t)(1u << dst);
}
/* bar `from` of track t into bar `to`, all of each step. The track grows to hold it when it ends before; all:
 * (the grid's copy of every track) only a track whose length is whole bars (a 12-step track keeps its 12) */
static void bar_copy(pattern_t *p, uint32_t t, uint32_t from, uint32_t to, int all)
{
    uint32_t i, a = from * 16u, b = to * 16u, len = track_len(p, t);
    for (i = 0; i < 16u; i++) {
        p->step[t][b + i] = p->step[t][a + i];
        p->timing[t][b + i] = p->timing[t][a + i];
        p->lhi[t][b + i] = p->lhi[t][a + i];
        p->cond[t][b + i] = p->cond[t][a + i];
        p->rat[t][b + i] = p->rat[t][a + i];
    }
    p->main[t][to] = p->main[t][from];
    if (len < b + 16u && (!all || len % 16u == 0u))
        p->len[t] = (uint8_t)(b + 16u);
    sq.gsync |= (uint8_t)(1u << t);
}

/* ---------------------------------------------------------- buttons --- */
static void button_down(uint32_t b)
{
    switch (b) {
    case B_OCTDN:
        ui.shift = 1;
        ui.shift_used = 0;
        ui.force = 1;
        return;
    case B_OCTUP:
        if (ui.shift) {
            ui.shift_used = 1;
            if (sq.rec && sq.running) {               /* recording: SHIFT CLEAR erases as the steps play */
                sq.erase = 1;
                return;
            }
            if (!ui.clear_open) {
                ui.clear_open = 1;
                ui.clear_sel = ui.mode == M_SONG ? CL_SONG_ROW : CL_TRACK;
            } else {
                clear_do();
            }
            ui.force = 1;
            return;
        }
        if (ui.sb_open) {
            sb_close(1);
            return;
        }
        if (ui.kb_open) {                             /* the kit browser: load */
            kit_load_ram(ui.kb_sel);
            ui.kb_open = 0;
            ui_message("KIT LOADED");
            return;
        }
        if (ui.mode == M_SAVE || ui.mode == M_LOAD) {
            io_ok();
            ui.force = 1;
            return;
        }
        if (ui.mode == M_EUCLID && ui.page % NEUCLID == 0u && ui.action) {   /* EUCLID: OCT+ does the ACTION */
            static const char *const M[NACT] = {"", "DICED", "COMMITTED", "UNDONE", "RESET"};
            static const char *const NO[NACT] = {"", "", "NO GENERATOR ON", "NOTHING TO UNDO", ""};
            int ok = 1;
            switch (ui.action) {
            case ACT_DICE: seq_dice(ui.track); break;
            case ACT_COMMIT: ok = shown_pat()->gen[ui.track][0] != GEN_OFF; seq_commit(ui.track); break;
            case ACT_UNDO: ok = seq_undo(ui.track); break;
            default: sq.gsync |= (uint8_t)(1u << ui.track); break;
            }
            ui_message(ok ? M[ui.action] : NO[ui.action]);
            ui.eu_undo = 0;
            ui.action = 0;
            return;
        }
        if (ui.mode == M_SONG) {                      /* COPY in the song: insert a row (a copy of the one above) */
            song_insert_row(sq.song, ui.song_row);
            ui_message("ROW INSERTED");
            return;
        }
        ui.copy = 1;
        ui.copy_used = 0;
        ui.copy_src = 0;
        ui.force = 1;
        return;
    case B_PLAY:
        if (ui.shift) {                               /* SHIFT PLAY: the song from its start */
            ui.shift_used = 1;
            if (sq.running)
                seq_stop_now();
            sq.song_mode = 1;
            seq_start(1);
            return;
        }
        if (sq.running) {
            seq_stop_now();
        } else {
            sq.song_mode = 0;
            seq_start(1);
        }
        return;
    case B_REC:
        if (ui.shift) {
            ui.shift_used = 1;
            mode_set(M_RECOPT);
            return;
        }
        sq.rec ^= 1u;
        ui.force = 1;
        return;
    case B_HOME:
        mode_set(ui.mode == M_HOME ? M_PATTERN : ui.mode == M_PATTERN ? M_SONG : M_HOME);
        return;
    case B_SEQ:
        if (ui.shift) {                               /* 0.6: SHIFT SEQ: the LINKS page */
            ui.shift_used = 1;
            mode_set(M_EUCLID);
            ui.perf = 0;
            ui.page = PG_LINKS;
            return;
        }
        if (ui.mode == M_STEP) {
            mode_set(ui.back == M_STEP ? M_HOME : ui.back);
        } else {
            uint8_t back = ui.mode;
            mode_set(M_STEP);
            ui.back = back;
            if (ui.step / 16u != ui.bar)
                ui.step = (uint8_t)(ui.bar * 16u);
        }
        return;
    case B_ARP:
        if (ui.shift) {
            ui.shift_used = 1;
            mode_set(M_EUCLID);
            return;
        }
        mode_set(ui.mode == M_PERF ? M_EUCLID : M_PERF);
        if (ui.mode == M_EUCLID)
            ui.perf = 0;
        return;
    case B_FX:
        mode_set(M_FX);
        return;
    case B_EDIT:
        if (ui.shift) {                               /* 0.8: SHIFT EDIT: KEYS (again: the grid) */
            ui.shift_used = 1;
            mode_set(ui.mode == M_KEYS ? M_HOME : M_KEYS);
            return;
        }
        sound_open(S_OSC, 1);
        return;
    case B_ENV:
        sound_open(S_AEG, 1);
        return;
    case B_SCL:
        sound_open(S_FILTER, 1);
        return;
    case B_LFO:
        sound_open(S_LFO, 1);
        return;
    case B_GLO:
        if (ui.mode == M_SOUND && ui.sect == S_MIX) {
            if (ui.half == 0 && sect_half_used(ui.track, S_MIX, 1)) {
                ui.half = 1;
                ui.force = 1;
            } else {
                mode_set(M_MIXER);
            }
        } else if (ui.mode == M_MIXER) {
            mode_set(M_CONFIG);
        } else {
            sound_open(S_MIX, 0);
        }
        return;
    case B_SAVE:
        if (ui.shift) {
            ui.shift_used = 1;
            io_open(M_LOAD, ui.mode == M_LOAD || ui.mode == M_SAVE ? ui.io_type : IO_KIT);
            return;
        }
        if (ui.mode == M_SAVE)
            io_open(M_LOAD, ui.io_type);
        else if (ui.mode == M_LOAD)
            io_open(M_SAVE, ui.io_type);
        else
            io_open(M_SAVE, IO_KIT);
        return;
    default:
        return;
    }
}
static void button_up(uint32_t b)
{
    if (b == B_OCTDN) {
        ui.shift = 0;
        if (ui.clear_open)                            /* SHIFT let go: CLEAR aborted (the LXR's way) */
            ui.clear_open = 0;
        sq.erase = 0;
        ui.force = 1;
        if (!ui.shift_used && (ui.mode == M_SAVE || ui.mode == M_LOAD) && ui.io_naming) {
            ui.io_naming = 0;                         /* OCT- alone: back (the dialogs) */
        } else if (!ui.shift_used && ui.kb_open) {
            ui.kb_open = 0;
        } else if (!ui.shift_used && ui.sb_open) {
            sb_close(0);
        }
    } else if (b == B_OCTUP) {
        ui.copy = 0;
        sq.erase = 0;
        if (ui.copy_src && !ui.copy_used)
            ui.copy_src = 0;
        ui.force = 1;
    }
}

/* ------------------------------------------------------------ input --- */
static void ui_input(void)
{
    uint32_t rel, pressed = fm1_input_edges(&rel), b, k, keys = fm1_in.notes;
    uint32_t down = keys & ~ui.keys, up = ui.keys & ~keys;
    int32_t n;
    for (b = 0; b < NB; b++) {
        if (pressed & (1u << panel.btn[b]))
            button_down(b);
        if (rel & (1u << panel.btn[b]))
            button_up(b);
    }
    (void)fm1_input_note_edges();
    ui.keys = keys;
    for (k = 0; k < 16u; k++) {
        if (down & (1u << WHITE_KEY[k]))
            white_down(k);
        if (up & (1u << WHITE_KEY[k]))
            white_up(k);
    }
    for (k = 0; k < 11u; k++)
        if (down & (1u << BLACK_KEY[k]))
            black_down(k);
    if ((n = panel_enc(EN_ALGO)) != 0) {
        int32_t t = clamp32((int32_t)ui.track + n, 0, NTRACK - 1);
        track_select((uint32_t)t);
    }
    if ((n = panel_enc(EN_SELECT)) != 0) {
        if (ui.clear_open) {
            ui.clear_sel = (uint8_t)clamp32((int32_t)ui.clear_sel + n, 0, NCLEAR - 1);
        } else if (ui.kb_open) {
            ui.kb_sel = (uint8_t)clamp32((int32_t)ui.kb_sel + n, 0, NUM_KITS - 1);
            ui.kb_ms = fm1_ms;
        } else if (ui.sb_open) {
            sb_turn(n);
        } else if (ui.mode == M_SONG) {
            ui.song_row = (uint8_t)clamp32((int32_t)ui.song_row + n, 0, MAX_SONG_LENGTH - 1);
        } else if (ui.mode == M_SAVE || ui.mode == M_LOAD) {
            if (ui.io_naming)
                ui.io_pos = (uint8_t)clamp32((int32_t)ui.io_pos + n, 0, 7);
            else
                ui.io_slot = (uint8_t)clamp32((int32_t)ui.io_slot + n, 0, (int32_t)vp_max(VP_IO_SLOT));
        } else if (ui.mode == M_SOUND) {
            uint8_t l[NSPG][2];
            uint32_t np = sound_pages(l), i;
            for (i = 0; i < np && !(l[i][0] == ui.sect && l[i][1] == ui.half); i++)
                ;
            i = (uint32_t)clamp32((int32_t)i + n, 0, (int32_t)np - 1);
            ui.sect = l[i][0];
            ui.half = l[i][1];
        } else if (ui.mode == M_HOME || ui.mode == M_LAST || ui.mode == M_RECOPT) {
            ui.bar = (uint8_t)clamp32((int32_t)ui.bar + n, 0, 3);
        } else if (ui.mode == M_PATTERN) {
            ui.bank = (uint8_t)clamp32((int32_t)ui.bank + n, 0, (int32_t)ui_banks() - 1);
        } else {
            ui.page = (uint8_t)clamp32((int32_t)ui.page + n, 0, (int32_t)mode_pages() - 1);
        }
        ui.force = 1;
    }
    if (pj.auto_err) {                               /* the session did not autosave (project.c) */
        ui_message(pj.auto_err == -20 ? "AUTOSAVE: PROJECT TOO BIG" : "AUTOSAVE FAILED");
        ui.msg_long = 1;
        pj.auto_err = 0;
    }
    if ((n = panel_enc(EN_PRESET)) != 0) {
        if (ui.mode == M_SAVE || ui.mode == M_LOAD) {
            ui.io_slot = (uint8_t)clamp32((int32_t)ui.io_slot + n, 0, (int32_t)vp_max(VP_IO_SLOT));
        } else if ((ui.mode == M_SOUND || ui.mode == M_KEYS) && !ui.kb_open) {
            sb_turn(n);
        } else {
            if (!ui.kb_open) {
                ui.kb_open = 1;
                ui.kb_sel = kit_loaded;
            } else {
                ui.kb_sel = (uint8_t)clamp32((int32_t)ui.kb_sel + n, 0, NUM_KITS - 1);
            }
            ui.kb_ms = fm1_ms;
        }
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((n = panel_enc(EN_K1 + k)) != 0)
            knob_turn(k, n);
    if (ui.kb_open && fm1_ms - ui.kb_ms > 6000u) {   /* the kit browser closes by itself, nothing loaded */
        ui.kb_open = 0;
        ui.force = 1;
    }
    if (ui.hot && fm1_ms - ui.hot_ms > 1200u)
        ui.hot = 0;
    {
        uint32_t t;
        for (t = 0; t < NTRACK; t++)
            if (trig_count[t] != ui.trig_seen[t]) {
                ui.trig_seen[t] = trig_count[t];
                ui.flash_ms[t] = fm1_ms;
            }
    }
}

/* ------------------------------------------------------------- LEDs --- */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_put(uint8_t *l, uint32_t id, int on)
{
    uint32_t c, r;
    if (!on)
        return;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                l[c] |= (uint8_t)(1u << r);
}
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0}, nm[FM1_NCOL] = {0};
    const pattern_t *pt = shown_pat();
    uint32_t k, c, play_step = sq.idx[ui.track] < 0 ? 0xFFu : (uint32_t)sq.idx[ui.track];
    uint32_t mode_btn = ui.mode == M_HOME || ui.mode == M_PATTERN || ui.mode == M_SONG ? B_HOME
                      : ui.mode == M_STEP ? B_SEQ : ui.mode == M_PERF || ui.mode == M_EUCLID ? B_ARP
                      : ui.mode == M_KEYS ? B_EDIT
                      : ui.mode == M_FX ? B_FX : ui.mode == M_SAVE || ui.mode == M_LOAD ? B_SAVE
                      : ui.mode == M_MIXER || ui.mode == M_CONFIG ? B_GLO
                      : ui.mode == M_SOUND ? (ui.sect == S_OSC || ui.sect == S_FM || ui.sect == S_CLICK ? B_EDIT
                                              : ui.sect == S_AEG || ui.sect == S_MOD ? B_ENV
                                              : sect_group(ui.sect) == S_FILTER ? B_SCL
                                              : sect_group(ui.sect) == S_LFO ? B_LFO : B_GLO)
                      : NB;
    if (fm1_led_anim_on())
        return;
    if (ui.kplay) {                                  /* 0.8: KEYS: a keyboard (lit: held; the Cs a mid level) */
        for (k = 0; k < 27u; k++) {
            uint32_t n = 53u + k + 12u * ui.koct - 36u;
            led_put(nl, 14u + k, (ui.keys >> k) & 1u);
            led_put(n % 12u == 0u ? nm : nd, 14u + k, 1);
        }
    }
    for (k = 0; k < 16u && !ui.kplay; k++) {          /* the white keys */
        uint32_t id = 14u + WHITE_KEY[k], s = ui.bar * 16u + k;
        if (ui.shift) {
            led_put(k >= 7u ? nl : nd, id, 1);         /* (the SHIFT functions lit) */
            continue;
        }
        switch (ui.mode) {
        case M_PATTERN:
        case M_SONG: {
            uint32_t n = ui.bank * 16u + k;
            led_put(nl, id, n == sq.active);
            led_put(nb, id, sq.pending == n && sq.pending != sq.active);
            led_put(nd, id, 1);
            break;
        }
        case M_SAVE:
        case M_LOAD:
            led_put(nl, id, ui.io_type != IO_PROJECT && ui.bank * 16u + k == ui.io_slot);
            led_put(nd, id, ui.io_type != IO_PROJECT);
            break;
        case M_PERF:                                 /* (lit: on; breathing: latched; PERFORM 2: its bank) */
            if (k < NTRACK && perf_bank2()) {
                led_put(nb, id, (ui.drill_latch >> k) & 1u);
                led_put(nl, id, (sq.drill >> k) & 1u);
            } else if (k < NTRACK) {
                led_put(nb, id, (ui.roll_latch >> k) & 1u);
                led_put(nl, id, (sq.roll >> k) & 1u);
            } else {
                uint32_t e = (perf_bank2() ? NPF : 0u) + k - NTRACK;
                led_put(nb, id, (ui.pf_latch >> e) & 1u);
                led_put(nl, id, (pf.on >> e) & 1u);
            }
            led_put(nd, id, 1);
            break;
        case M_LAST: {
            uint32_t len = track_len(pt, ui.track);
            led_put(nl, id, s + 1u == len);
            led_put(nd, id, s < len);
            break;
        }
        default: {
            int on = main_on(view_pat(ui.track), ui.track, s), at = sq.running && s == play_step;
            if (ui.mode == M_STEP && s == ui.step) {
                led_put(nb, id, 1);
                break;
            }
            led_put(nl, id, at ? !on : on);
            led_put(nm, id, !on && (k & 3u) == 0u);   /* the beats: a steady mid level */
            led_put(nd, id, 1);
            break;
        }
        }
    }
    for (k = 0; k < NTRACK && !ui.kplay; k++) {      /* the voice keys */
        uint32_t id = 14u + BLACK_KEY[k];
        int muted = (sq.muted >> k) & 1u, flash = fm1_ms - ui.flash_ms[k] < 70u;
        led_put(nl, id, (k == ui.track && !ui.perf) || (flash && !muted));
        led_put(nb, id, ui.copy_src == k + 1u && ui.mode != M_PATTERN);
        led_put(nd, id, !muted);
    }
    for (k = 0; k < 4u && !ui.kplay; k++) {           /* the bar keys */
        uint32_t id = 14u + BLACK_KEY[NTRACK + k];
        if (ui_banks()) {
            led_put(nl, id, ui.bank == k);
            led_put(nd, id, k < ui_banks());
        } else if (ui.mode == M_PERF) {              /* (the STUTTER length) */
            led_put(nl, id, pfs.repeat == k);
            led_put(nd, id, 1);
        } else {
            led_put(nl, id, ui.bar == k);
            led_put(nb, id, sq.running && play_step != 0xFFu && play_step / 16u == k && ui.bar != k);
            led_put(nd, id, k * 16u < track_len(pt, ui.track));
        }
    }
    if (mode_btn < NB)
        led_put(nl, panel.btn[mode_btn], 1);
    led_put(nl, panel.btn[B_REC], sq.rec);
    led_put(nb, panel.btn[B_REC], sq.erase);
    led_put(nl, panel.btn[B_OCTDN], ui.shift);
    led_put(nl, panel.btn[B_OCTUP], ui.copy);
    led_put(nb, panel.btn[B_OCTUP], ui.clear_open || ui.kb_open || ui.sb_open || ui.copy_src || ((ui.mode == M_SAVE ||
                                                                                     ui.mode == M_LOAD) && !ui.copy));
    for (k = 0; k < NB; k++)
        if (k != B_PLAY)
            led_put(nd, panel.btn[k], 1);
    if (!sq.running)
        led_put(nd, panel.btn[B_PLAY], 1);
    for (c = 0; c < FM1_NCOL; c++) {
        nd[c] &= (uint8_t)~(nl[c] | nb[c]);
        nm[c] &= (uint8_t)~(nl[c] | nb[c]);
        nl[c] &= (uint8_t)~nb[c];
        if (kset.leds == 0u) {                       /* LEDS OFF: no glow */
            nd[c] = 0;
            nm[c] = 0;
        }
    }
    if (sq.running && (sq.prescale < 12u || !sq.beat))   /* PLAY: green, dark for half a step on each beat */
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    fm1_led_dim_level(kset.leds == 1u);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = nm[c];
        fm1_led[c] = nl[c];
    }
}

/* MIDI notes in STEP edit set the selected step's note (midiParser_noteOn, SEQ_PAGE) */
static int ui_step_note_edit(uint32_t note)
{
    if (ui.mode != M_STEP)
        return 0;
    sel_step()->note = (uint8_t)note;
    return 1;
}
