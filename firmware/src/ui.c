/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the panel. The LXR-02's front (Core/Src/buttonHandler.c, menu/menu.c; Copyright (C) 2012-2026 Erica
 * Synths, GPL-3.0-only) on the FM-1's controls:
 *
 *   white keys 1..16   the 16 step buttons (steps of the shown bar; patterns; in PERF 1..7 rolls, 8 FILL, 9..16
 *                      the effects of perform.c; SHIFT + key latches it)
 *   black keys 1..7    the 7 voice buttons (D1 D2 D3 SN CY CH OH): select; with SHIFT mute (PERF: mute)
 *   black keys 8..11   the 4 bar buttons: show bar 1..4 (patterns: the bank, 2 of 16; kits and songs: 4);
 *                      SHIFT: the track's length; COPY: copy
 *   OCT-               SHIFT (held; the screen shows the menu, what SHIFT + a key does, on every page but PERF's;
 *                      1.0: held while a knob turns, fine: a step a detent)
 *   OCT+               COPY (held, + a track, a bar, a pattern); SHIFT + COPY = CLEAR; OK in the dialogs;
 *                      held on a sound page, a knob: CONTROL ALL (let go: OCT+ reverts, OCT- keeps; 1.0: on the
 *                      length, every track's)
 *   EDIT ENV SCL LFO GLO   the sound sections (SHIFT GLO: solo the track): OSC > FM > CLICK, AMP EG > MOD, FILTER > SHAPE, LFO > RANDOM >
 *                      CROSS MOD (Elixir's: their pages are K_PAGES), MIX > MIXER (volume,
 *                      pan, FX send, reverb send) > CONFIG (the CPU load at the bottom)
 *   FX  SEQ  ARP  HOME  SAVE   FX bus > reverb; the track's pages > step edit; PERF's sets; GRID > PATTERNS >
 *                      LOAD / SAVE (1.0: FILES) > SONG; SAVE > LOAD (a sound button again: its next section only)
 *   PLAY REC           transport (SHIFT PLAY: the song from its start); record (SHIFT REC: its options)
 *   ALGORITHM SELECT PRESETS   the track; the page; the kit browser (OCT+ loads)
 *   KNOB 1..4          the four parameters on the screen; with a step key held, that step's lock
 * The main loop calls ui_input (every ms), ui_leds and ui_draw (a frame). */

enum { M_HOME, M_PATTERN, M_SONG, M_SOUND, M_STEP, M_PERF, M_EUCLID, M_FX, M_MIXER, M_CONFIG, M_SAVE, M_LOAD,
       M_RECOPT, M_KEYS, NMODE };
enum { S_OSC, S_AEG, S_MOD, S_FM, S_CLICK, S_FILTER, S_LFO, S_MIX, S_SHAPE, S_RANDOM, S_XMOD, S_MODEL, S_LFOM,
       S_GATE, S_LFO2, S_INSERT, S_SENDS, NSECT };
#define NSPG 34                                      /* the sound pages a track can have (sections x halves) */
/* (29: LFO and LFO MODE are LFO 1's three pages; MIX is no longer shown, its parameters are elsewhere) */
static const char *const SECT_NAME[NSECT] = {"OSCILLATOR", "AMP EG", "MOD EG / VELO", "FM", "TRANSIENT", "FILTER",
                                             "LFO 1", "MIX", "SHAPE", "RANDOM PER HIT", "CROSS MOD / CHOKE", "MODEL",
                                             "LFO 1", "GATE", "LFO 2", "INSERT FX", "FX TRACK SENDS"};
enum { IO_KIT, IO_PATTERN, IO_SONG, IO_MORPH, IO_SOUND, IO_PROJECT, IO_FACTORY, IO_FMORPH, NIO };   /* (FACTORY: LOAD,
                                                                                      * a kit bank; FMORPH: as the morph target) */
static const char *const IO_NAME[NIO] = {"KIT", "PATTERN", "SONG", "MORPH", "SOUND", "PROJECT", "KIT", "MORPH"};
/* 1.0: the Type knob's order, most used first. LOAD: no PATTERN or SONG (their own pages pick them); FACTORY is a
 * KIT From FACTORY (the From knob's third). SAVE: the same, then PATTERN, SONG */
static const uint8_t IO_LOAD_ORD[] = {IO_KIT, IO_MORPH, IO_SOUND, IO_PROJECT};
static const uint8_t IO_SAVE_ORD[] = {IO_KIT, IO_MORPH, IO_SOUND, IO_PROJECT, IO_PATTERN, IO_SONG};
/* the keys: k = semitones above F3 (fm1_in.notes bit k) */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* virtual parameters: what the cards edit that is not in par[] */
enum { VP_BASE = 0x400, VP_NONE = VP_BASE, VP_IO_TYPE, VP_IO_SLOT, VP_NAME_POS, VP_NAME_CHAR, VP_NAME_CASE,
       VP_SONG_PAT, VP_SONG_NUM, VP_LEDS, VP_AUTOSAVE, VP_ACCEL, VP_PALETTE, VP_TRACK_VOL, VP_FX_SUB, VP_MASTER,
       VP_DJ, VP_REPEAT, VP_TAPE, VP_M1, VP_M4 = VP_M1 + NMACRO - 1, VP_MSEL, VP_MTARGET, VP_MMIN, VP_MMAX,
       VP_COND, VP_RAT_N, VP_RAT_RATE, VP_RAT_FADE, VP_ACTION, VP_GMODE, VP_GA, VP_GB, VP_GC, VP_CLOCK,
       VP_GX, VP_GY, VP_GCHAOS, VP_RAT_SHAPE, VP_BSTART, VP_BPITCH, VP_ROLL,
       VP_LINK, VP_LSTART, VP_LAMT, VP_DEPTH, VP_LWHEN, VP_LTGT, VP_LEFF, VP_LWIN, VP_KOCT, VP_KVEL, VP_KACC, VP_PKEEP, VP_KB_SRC,
       VP_IO_SRC, VP_IO_TRACK, VP_DIR, VP_RCH, VP_RCH2, VP_RKNOBS, VP_CAKIT, VP_EH2, VP_ER2, VP_EOP, VP_PMEM, VP_KBSLOTS,
       VP_DENS, VP_GHOST, VP_BUILD, VP_SROOT, VP_SCALE, VP_SFOLLOW, VP_LEN,   /* (19) */
       VP_GROOVE, VP_GAMT, VP_GHUM, VP_GFOLLOW,      /* (26: GROOVE) */
       VP_STCH, VP_STSIZE, VP_STLEN, VP_STVARY,      /* (0.16: STUTTER) */
       VP_MEDIT, VP_DVARY,                           /* (31: edits under morph; DENSITY's VARY) */
       VP_RUSH, VP_RTOP, VP_RFILT, VP_RGLIDE, VP_RSTART, VP_RFREE, VP_RFLAM, VP_RPITCH,   /* (39: RUSH) */
       VP_SP, VP_SP_LAST = VP_SP + 17,               /* (32: SPELLS' knobs, spells.c SPV_*) */
       VP_WD, VP_WD_LAST = VP_WD + 11,               /* (40: WANDER's knobs, wander.c WV_*) */
       VP_MEL, VP_MEL_LAST = VP_MEL + NML - 1,       /* 20: the track's melody (pattern_t mel: ML_*) */
       VP_GL, VP_GL_LAST = VP_GL + NGLFO * NGLSET - 1,   /* 0.13: the global LFOs' settings (gls: NGLSET each) */
       VP_P2, VP_P2_LAST = VP_P2 + 7, VP_SONG_REP,
       VP_SONG_ROW, VP_SONG_EDIT, VP_PLAY, VP_VOICES,   /* (37) */
       VP_ROADS, VP_RSUR, VP_RHOME,                  /* (42: the song's ROADS) */
       VP_TBASE, VP_TRP, VP_TRWHEN, VP_TRHOLD,       /* (0.23-t7: a track's BASE, TRANSPOSE) */
       VP_GTAKES,                                    /* (0.24: what the global channel takes) */
       VP_DJRESO, VP_RMUTE,                          /* (0.24.3: the DJ filter's resonance; the remote's mute, solo) */
       VP_MVOL, VP_MOUT, VP_MOUT_LAST = VP_MOUT + NTRACK - 1,   /* (1.0: the master volume; a track's MIDI out) */
       VP_TD, VP_TD_LAST = VP_TD + NTRACK - 1,       /* (1.0: the remote's DENSITY a track: seq.c sq.tdens) */
       VP_STRETCH, VP_REALIGN,                       /* (1.0: TIME STRETCH, a track's; when the tracks realign) */
       VP_DJTYPE,                                    /* (1.0: the DJ filter's type, perform.c DJT_*) */
       VP_DJTUNE,                                    /* (1.0: RING's chord, perform.c RC_*) */
       VP_UMIX, VP_UMIX_LAST = VP_UMIX + NVOICE - 1,   /* (1.0: a voice IN / OUT of the stereo mix while USB records) */
       VP_DJSPREAD, VP_DJSLOPE, VP_DJTONE, VP_DJWET };   /* (1.0 Phase 18: RING (2/2), perform.c pfs.dj_spread ..) */
_Static_assert(NPAR <= VP_BASE, "the virtual parameters after the parameters (25b + 27a: NPAR 525, past 0x200)");
/* the ACTION page's action (OCT+ does it); 0.10: those of the track's generator (gen_acts) */
enum { ACT_NONE, ACT_DICE, ACT_COMMIT, ACT_UNDO, ACT_RESET, NACT };

static struct {
    uint8_t mode;
    uint8_t track, bar, sect, half, page;
    uint8_t step;                    /* STEP: the selected step (0..63) */
    uint8_t perf;                    /* the white keys roll (PERF, and the sound pages opened from it) */
    uint8_t kplay;                   /* 0.8: the keys play the track's voice (KEYS, and the sound pages opened from it) */
    uint8_t koct, knote;             /* .. KEYS' octave (3: the keys from F3), the last note played */
    uint8_t kkey;                    /* .. 19b: the key that played it (its release lets a GATE voice go), 0xFF none */
    uint32_t pf_held, pf_latch;      /* PERF's keys 8..16 held, latched (bit PF_*: pf.on is both) */
    uint8_t roll_latch;              /* the rolls latched (SHIFT + key 1..7) */
    uint8_t drill_held, drill_latch; /* 0.6: PERFORM 2's keys 1..7 (DRILL) */
    uint32_t rm_held, rm_latch;      /* 0.12: the effects the remote's buttons hold, latch (bit PF_*) */
    uint8_t rm_drill;                /* .. the DRILLs it holds */
    uint16_t kbank;                  /* bit w: white key w went down on a PERFORM 2 page (it lets go there) */
    uint8_t msel;                    /* MACRO SETUP: the macro */
    uint8_t action;                  /* the GENERATOR's ACTION OCT+ does (ACT_*) */
    uint8_t eu_undo;                 /* the Euclid knobs' UNDO saved (track + 1): turned again, it keeps that one */
    uint8_t link;                    /* LINKS: the link shown (0..7) */
    uint8_t shift, copy, shift_used, copy_used;
    int8_t held_step;                /* a step key held (its locks), -1 none */
    uint8_t trkey;                   /* 0.23-t7: TRANSPOSE: the white key held + 1 (0: none) */
    uint8_t lock_turned;
    uint8_t copy_src;                /* COPY: the source track + 1 / pattern + 1 */
    uint8_t ca, ca_loads;            /* 0.11: Control All under way (the kit as it was: ca_snap), kit_loads then */
    uint8_t ca_ask;                  /* .. OCT+ let go: its dialog (OCT+ reverts, OCT- keeps) */
    uint8_t ca_kept;                 /* 1.0 Phase 17: ca_snap holds the kit before the last Control All (until a kit
                                      * load: ca_loads): GLOBAL's button 71 reverts to it */
    uint8_t dx_ask, dx_seen;         /* 0.24.1: another app's data found (project.c fx_found): 1 asked, 2 COPY (OCT+)
                                      * once, 3 erasing (dx_ms: when); dx_seen: asked this power-on */
    uint32_t dx_ms;
    uint8_t clear_open, clear_sel;
    uint8_t pdel, pdel_n;            /* 1.0: the pattern page's delete: 1 CLEAR pressed (pick one), 2 asked; the pattern */
    uint8_t ca_len;                  /* 1.0: Control All on the tracks' lengths (OCT+ held, the length knob): ca_lens */
    uint8_t ca_lens[NTRACK];         /* .. the lengths as they were */
    uint8_t fine;                    /* 1.0: a knob turned with OCT- held (fine: no acceleration; the page stays shown) */
    uint8_t kb_open;                 /* the kit browser (0.9; 0.19: one list, kb_has) */
    uint16_t kb_sel, kb_at;          /* .. its entry; 1 + where it was when it closed (0: not opened yet) */
    uint8_t sb_open, sb_voice, sb_on; /* 0.8: the sound browser (its voice; the voice as it was: sb_orig, sb_on values) */
    uint16_t sb_sel;
    uint8_t sb_orig[SOUND_MAX];
    uint32_t kb_ms;
    uint8_t io_type, io_slot, io_naming, io_pos, io_bank;
    uint8_t io_src;                  /* 0.8: KIT and MORPH from / to the slots (0) or the library (1) */
    uint8_t lib_kit, lib_sound;      /* .. the library's kit and sound last loaded or saved */
    char io_name[8];
    uint8_t song_row;
    uint8_t song_edit;               /* 37: SONG: ROWS: INSERT or DELETE (SE_*) */
    uint8_t bank;                    /* PATTERN / SONG: the bank of 16 the keys show */
    char msg[32];
    uint32_t msg_ms;
    uint8_t msg_long;                /* a warning: 6 s on screen, not 1.5 */
    uint8_t force;
    uint32_t keys;                   /* the keys as last seen (releases) */
    uint8_t hot;                     /* the card last turned (1..4), 0 none */
    uint32_t hot_ms;
    uint32_t knob_ms[4];
    uint32_t dest_ms;                /* 0.25.2: a global LFO's DEST last turned (its name shown a while: dest_p) */
    uint16_t dest_p;
    uint32_t trig_seen[NTRACK];
    uint32_t flash_ms[NTRACK];
    uint8_t mpage[NMODE];            /* 0.13: CONFIG BUTTONS (kset.pmem): each mode's page when it was left, */
    uint8_t gsect[NSECT], ghalf[NSECT];   /* .. each sound button's section (+ 1, 0 none) and half, */
    uint8_t seq_m, glo_m;            /* .. the mode SEQ and GLO showed last (pmem_save), */
    uint8_t pset[4];                 /* .. 29: PERF's page in each set (PERFORM, 32: SPELLS, 40: WANDER, GLOBAL MOD) */
} ui = {.held_step = -1, .koct = 3, .knote = 0xFF, .kkey = 0xFF};


static uint32_t ui_track(void) { return ui.track; }
static uint32_t io_nord(void) { return ui.mode == M_SAVE ? sizeof IO_SAVE_ORD : sizeof IO_LOAD_ORD; }
static uint32_t io_ord(uint32_t i) { return ui.mode == M_SAVE ? IO_SAVE_ORD[i % sizeof IO_SAVE_ORD] : IO_LOAD_ORD[i % sizeof IO_LOAD_ORD]; }

static uint32_t sp_vget(uint32_t i);                 /* (32: spells.c) */
static uint32_t sp_vmax(uint32_t i);
static void sp_vset(uint32_t i, uint32_t v);
static uint32_t wd_vget(uint32_t i);                 /* (40: wander.c) */
static uint32_t wd_vmax(uint32_t i);
static void wd_vset(uint32_t i, uint32_t v);
static int wd_vdead(uint32_t i);
static int vp_dead(uint32_t p) { return p >= VP_WD && p <= VP_WD_LAST && wd_vdead(p - VP_WD); }   /* (40: a knob not
                                                      * yet in use: shown --, locked) */
static uint32_t ui_pattern(void) { return sq.active; }

static void ui_draw(void);
static void seq_copy_track(uint32_t src, uint32_t dst, pattern_t *p);
static void bar_copy(pattern_t *p, uint32_t t, uint32_t from, uint32_t to, int all);
/* a step key: the step on / off (a generator's track: its live copy too, the step as the pattern has it) */
static void step_toggle(pattern_t *p, uint32_t t, uint32_t s)
{
    int on = !main_on(p, t, s);
    main_set(p, t, s, on);
    if (p == &pat[sq.active] && gen_live(p, t)) {
        gpat.step[t][s] = p->step[t][s];
        gpat.timing[t][s] = p->timing[t][s];
        gpat.lhi[t][s] = p->lhi[t][s];
        gpat.cond[t][s] = p->cond[t][s];
        gpat.rat[t][s] = p->rat[t][s];
        main_set(&gpat, t, s, on);
    }
}

/* the effects and DRILLs on: the panel's held and latched, the remote's (0.12) */
static void perf_apply(void)
{
    pf.on = ui.pf_held | ui.pf_latch | ui.rm_held | ui.rm_latch;
    sq.drill = (uint8_t)(ui.drill_held | ui.drill_latch | ui.rm_drill);
}
/* 31: PERF RESET (SHIFT + HOME): every performance effect off at once, wherever it was turned on: the keys held or
 * latched (rolls, DRILLs, the effects; the remote's too), STUTTER's chance, DENSITY to 0, the DJ filter to the middle.
 * The global LFOs, morph and the macros stay: they are the project's sound, not a key left on */
static void ui_message(const char *s);
static void perf_reset(void)
{
    uint32_t t;
    ui.pf_held = ui.pf_latch = ui.rm_held = ui.rm_latch = 0;
    ui.drill_held = ui.drill_latch = ui.rm_drill = 0;
    for (t = 0; t < NTRACK; t++)
        seq_set_roll(t, 0);
    ui.roll_latch = 0;
    perf_apply();
    pf.st_hold = 0;
    pfs.st_chance = 0;
    pfs.dens = 0;
    memset(sq.tdens, 0, sizeof sq.tdens);            /* (1.0: the remote's DENSITY a track, its rolls) */
    memset(sq.croll, 0, sizeof sq.croll);
    pf.dj = 64;
    sq.ru_live = 0;                                  /* (39: RUSH's knob down) */
    ui_message("PERF RESET: EFFECTS OFF");
}
/* 0.13: the main loop, each ms: morph moved by the global LFOs (perform.c gl.morph; a new mixture at most every
 * 8 ms: snd_post_morph) */
static void glfo_morph(void)
{
    static uint32_t ms;
    int32_t m = gl.morph;
    if (m == morph_mod || fm1_ms - ms < 8u)
        return;
    ms = fm1_ms;
    morph_mod = m;
    morph_update();
}
/* 0.12: the remote (remote.c): its knobs' modes, what it has seen forgotten, what came in */
enum { RK_PICKUP, RK_ABS, RK_REL, NRK };
#define RK_KNOBS 0x7Fu                               /* settings' rknobs: RK_*, and (0.24.3) */
#define RK_TOGGLE 0x80u                              /* .. the remote's mute and solo: as the button's state */
static void remote_reset(void);
static void remote_service(void);

static void ui_message(const char *s)
{
    put_str(ui.msg, s, sizeof ui.msg);
    ui.msg_ms = fm1_ms;
    ui.msg_long = 0;
    ui.force = 1;
}
static int ui_asking(void) { return ui.ca_ask || ui.dx_ask || ui.pdel == 2u; }   /* a dialog: the keys and knobs wait */
static const char *fx_msg(const char *what)          /* 0.24.1: "WYRD'S DATA: NOT SAVED" */
{
    static char b[32];
    put_str(b, fx_name(), sizeof b);
    put_str(b + str_len(b), what, sizeof b - str_len(b));
    return b;
}

/* the voice (0..5) of a track: the open hat is the hat voice */
static uint32_t track_voice(uint32_t t) { return t < 6u ? t : 5u; }
/* the LXR's edit page of a track (VOICE1_PAGE .. VOICE7_PAGE) */
static uint32_t track_page(uint32_t t) { return t; }
/* Elixir's sections (S_SHAPE ..): the first of each six per-voice parameters, by half (0: none). MODEL: the DRUM
 * voices' only */
static const uint16_t K_PAGES[NSECT - S_SHAPE][8] = {{PAR_SHAPE1, PAR_DRIVE1, PAR_VOICE_DECIMATION1, PAR_BITS1,   /* (29: the */
                                        PAR_TILT1},       /* .. sample rate from MIX, before bits as it plays) */
                                       {PAR_RND_PITCH1, PAR_RND_DECAY1, PAR_RND_FILTER1, PAR_RND_PAN1},
                                       {PAR_XSRC1, PAR_XMODE1, PAR_XAMT1, PAR_CHOKE1},
                                       {PAR_MODEL1, PAR_MDECAY1, PAR_MDAMP1, PAR_MMAT1,
                                        PAR_MPOS1, PAR_MEXC1, PAR_MTENS1, 0},
                                       {0},               /* (0.13: LFO 1's MODE; 1.0: on LFO 1's page 2) */
                                       {PAR_GATE1},       /* (19b) */
                                       {0},               /* (25b: LFO 2: lfo_par) */
                                       {PAR_INS1, PAR_INSA1, PAR_INSB1, PAR_INSC1},          /* (27a) */
                                       {PAR_FX_SEND1, PAR_DSEND1, PAR_REV_SEND1}};   /* (1.0 Phase 16; one page:
                                                      * the returns are global, on the FX and mixer pages) */
/* 1.0: every LFO's knobs in one order (LP_*: page 1 Dest, Depth, Sync, Rate; page 2 Wave, Phase, Mode, Retrigger;
 * the global LFOs' too, ui.c P_PERF). LFO 1's Voice is no knob: its Dest turns through every voice's (lfo1_dest_turn) */
enum { LP_DEST, LP_DEPTH, LP_SYNC, LP_RATE, LP_WAVE, LP_PHASE, LP_MODE, LP_RETRIG, NLP };
static uint32_t lfo_par(uint32_t t, int two, uint32_t i)   /* track t's LFO 1 (two: LFO 2), its LP_* i */
{
    static const uint16_t P[2][NLP] = {{PAR_TARGET_LFO1, PAR_AMOUNT_LFO1, PAR_SYNC_LFO1, PAR_FREQ_LFO1, PAR_WAVE_LFO1,
                                        PAR_OFFSET_LFO1, PAR_LFOMODE1, PAR_RETRIGGER_LFO1},
                                       {PAR_L2TGT1, PAR_L2AMT1, PAR_L2SYNC1, PAR_L2RATE1, PAR_L2WAVE1, PAR_L2OFS1,
                                        PAR_L2MODE1, PAR_L2RTRG1}};
    return i < NLP ? P[two != 0][i] + track_voice(t) : 0u;
}
/* 1.0: where knob k of section s's half h is in the LXR's page (LXR_PAGES' 0..7): FILTER's first page Type, Freq,
 * Reso, Drive (the LXR's: Freq, Reso, Type, Drive) */
static uint32_t lxr_knob(uint32_t s, uint32_t h, uint32_t k)
{
    static const uint8_t FK[4] = {2, 0, 1, 3};
    return s == S_FILTER && h == 0u && k < 4u ? FK[k] : h * 4u + k;
}
/* the parameter on knob k of section s's half h for track t as the pages hold it (sect_par: but those a model hides) */
static uint32_t sect_par_raw(uint32_t t, uint32_t s, uint32_t h, uint32_t k)
{
    uint32_t q;
    if (s == S_LFO || s == S_LFO2)
        return lfo_par(t, s == S_LFO2, h * 4u + k);
    if (s == S_FILTER && h == 1u)                    /* 1.0: FILTER X, the second page's first knob */
        return k == 0u ? PAR_FLTX1 + track_voice(t) : 0u;
    if (s < S_SHAPE)
        return LXR_PAGES[track_page(t)][s][8u + lxr_knob(s, h, k)];
    q = K_PAGES[s - S_SHAPE][h * 4u + k];
    return q ? q + track_voice(t) : 0u;
}
/* the parameter on knob k of section s's half h for track t (0: none). 24: MODEL on SN-HH: the model alone but on
 * MICRO; MICRO's second page REVERSE, TENSION on knobs 1, 2 (it has no POSITION). 29: on LXR, D1-D3's too */
static uint32_t sect_par(uint32_t t, uint32_t s, uint32_t h, uint32_t k)
{
    uint32_t v = track_voice(t);
    if (s == S_FILTER && h == 1u && !flt_char(par[PAR_FILTER_TYPE_1 + v] + 1u))
        return 0;                                    /* (1.0: FILTER X: the character types' alone) */
    if (s == S_MODEL && mi[v].type && h == 1u)
        return k == 0u ? PAR_MEXC1 + v : k == 1u ? PAR_MTENS1 + v : 0u;
    if (s == S_MODEL && nzo[v].on && h == 1u)       /* (1.0: NOISE: COLOUR, RATE, ENV on the first page alone) */
        return 0;
    if (s == S_MODEL && (h || k) && !mi[v].type && !eno[v].on && !nzo[v].on && (v > 2u || !par[PAR_MODEL1 + v]))
        return 0;
    return sect_par_raw(t, s, h, k);
}
/* 0.25: the amp EG shapes a model's own sound: ENSEMBLE's, and Plaits' engines that don't envelope themselves (all but
 * STRING, MODAL and the drums, plaits.c PE_ENV; SPEECH's word mode does, but its other modes don't) */
static int aeg_heard(uint32_t t)
{
    uint32_t v = track_voice(t), m = par[PAR_MODEL1 + v];
    return m == MD_EN || m == MD_NOISE || (v <= 2u && m >= MD_PL0 && m < MD_MC0 && !PE_ENV[m - MD_PL0]);
}
/* 0.19: a model making its own sound on track t's voice (Plaits, MICRO, Rings not struck by the voice): its OSC page
 * (but COARSE, FINE: the model's pitch), FM, TRANSIENT and AMP EG unheard (0.25: but aeg_heard's). 0: heard; 1: not now, but morphing would
 * strike Rings (the morph kit's EXCITER up): shown faint, locked; 2: never: those pages hidden */
static uint32_t lxr_unheard(uint32_t t)
{
    uint32_t v = track_voice(t), m = par[PAR_MODEL1 + v], e = PAR_MEXC1 + v;
    if (!m || m >= NMODEL || (v <= 2u && mdl[v].want && !mdl[v].type))   /* (over the models' limit: the LXR's) */
        return 0;
    if (m <= 3u && v <= 2u && par[e])
        return 0;
    if (m <= 3u && v <= 2u && morph_par[kit_idx(e)])
        return morph_amt ? 0u : 1u;
    return 2;
}
static uint32_t par_unheard(uint32_t t, uint32_t p)  /* (one of those knobs: lxr_unheard's, else 0) */
{
    static const uint8_t S[4] = {S_OSC, S_FM, S_CLICK, S_AEG};
    uint32_t u = lxr_unheard(t), i, k;
    if (!u || !p)
        return 0;
    for (i = 0; i < 4u; i++)
        for (k = 8; k < 16u; k++)
            if (LXR_PAGES[track_page(t)][S[i]][k] == p)
                return (S[i] == S_OSC && k <= 9u) || (S[i] == S_AEG && aeg_heard(t)) ? 0u : u;
    return 0;
}
/* the button that opens a section: the first section of its group */
static uint32_t sect_group(uint32_t s)
{
    static const uint8_t G[NSECT] = {S_MODEL, S_AEG, S_AEG, S_MODEL, S_MODEL, S_FILTER, S_LFO, S_MIX, S_FILTER, S_LFO,
                                     S_LFO, S_MODEL, S_LFO, S_AEG, S_LFO, S_FILTER, S_LFO};   /* (29: EDIT opens
                                                      * MODEL; 1.0 Phase 16: SENDS in LFO's group, last) */
    return G[s];
}

/* ------------------------------------------------------------ pages --- */
/* the four parameters a page puts on the knobs */
static void page_params(uint16_t out[4]);
static int wd_goal_song(void);
static uint32_t mode_pages(void);
static pattern_t *shown_pat(void);

static int sect_half_used(uint32_t t, uint32_t s, uint32_t h)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        uint32_t p = sect_par(t, s, h, k);
        if (p && p != PAR_NONE && par_unheard(t, p) < 2u)   /* (0.19: a model's own sound: not its unheard pages) */
            return 1;
    }
    return 0;
}
/* the sections in page order (the numbers stay, they are saved). 29: a synth's order, the voice's model first, the
 * modulation last; MIX not shown (volume, pan: the mixer; sample rate: SHAPE; MIDI: CONFIG; length: SEQ's TRACK) */
#define NSORD (NSECT - 1u)
static const uint8_t SECT_ORDER[NSORD] = {S_MODEL, S_OSC, S_FM, S_CLICK, S_FILTER, S_SHAPE, S_INSERT, S_AEG, S_GATE,
                                          S_MOD, S_LFO, S_LFOM, S_LFO2, S_RANDOM, S_XMOD, S_SENDS};
static uint32_t sect_rank(uint32_t s)
{
    uint32_t i;
    for (i = 0; i < NSORD && SECT_ORDER[i] != s; i++)
        ;
    return i;
}
/* the sound pages in order: (section, half) that hold something for this track */
static uint32_t sound_pages(uint8_t list[NSPG][2])
{
    uint32_t i, s, h, n = 0;
    for (i = 0; i < NSORD; i++)
        for (s = SECT_ORDER[i], h = 0; h < 2u; h++)
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
        if (sect_rank(l[i][0]) >= sect_rank(ui.sect)) {
            ui.sect = l[i][0];
            ui.half = l[i][1];
            return;
        }
    ui.sect = l[0][0];
    ui.half = l[0][1];
}

static const uint16_t P_HOME[4] = {PAR_BPM, PAR_SHUFFLE, PAR_TRACK_LENGTH, VP_TRACK_VOL};
static const uint16_t P_STEP[4][4] = {{PAR_STEP_NOTE, VP_LEN, PAR_STEP_VOLUME, VP_NONE},   /* (0.23-t11: as the */
                                      {PAR_STEP_PROB, VP_COND, PAR_STEP_SHIFT, PAR_STEP_FLAM},   /* Elektrons') */
                                      {PAR_P1_DEST, PAR_P1_VAL, PAR_P2_DEST, PAR_P2_VAL},
                                      {VP_RAT_N, VP_RAT_SHAPE, VP_RAT_RATE, VP_RAT_FADE}};   /* (29: SHAPE names
                                                                                               * RATE: before it) */
static const char *const STEP_TITLE[4] = {"STEP", "TRIG", "LOCKS", "RATCHET"};
/* 0.8: the performance pages: PERFORM is the LXR's PERFORMANCE page (BPM, roll, morph, sample rate). 0.13: in order,
 * the performance pages (PERFORM 2's last: its keys the second bank), the global LFOs (each: DEST DEPTH SPEED WAVE,
 * then MODE TRIG PHASE), the macros. 19: DENSITY after MORE (its black keys leave tracks out), a third LFO. 0.16:
 * STUTTER after DENSITY (OCT+ held: on). 29: two sets, ARP between them: PERFORM (the MIX pages gone: the mixer has
 * them), GLOBAL MOD (the global LFOs, the macros, the links); SELECT stays in the set. 32: SPELLS between them
 * (0.19: a fifth, MOTION). 39: RUSH after STUTTER. 40: WANDER after SPELLS, a set of its own (QUEST, PACE, HOME) */
#define NPERF 30u
#define PG_DENS 2u
#define PG_STUT 3u
#define PG_RUSH 4u                                   /* (2 pages: RUSH, RUSH SETUP) */
#define PG_TRP 6u                                    /* 0.23-t7: TRANSPOSE (white keys: the degrees, black: the tracks) */
#define PG_PERF2 7u                                  /* (3 pages: PERFORM 2 and its setups, its keys the second bank) */
#define PG_SPELL 10u                                  /* 32: SPELLS (5 pages: GRIMOIRE, CAST, CHARACTER, LIMITS, MOTION;
                                                      * spells.c) */
#define PG_WANDER 15u                                /* 40: WANDER (3 pages: QUEST, PACE, HOME; wander.c) */
#define PG_GMOD 18u                                  /* GLOBAL MOD's first page */
#define PG_GLFO 18u                                  /* (8 pages: LFO 1, its mode, LFO 2 .., LFO 3 .., 1.0: LFO 4 ..) */
#define PG_MACRO (PG_GLFO + 2u * NGLFO)              /* (2 pages: MACROS, MACRO SETUP) */
#define PG_LINKS (PG_MACRO + 2u)                     /* (2 pages: LINKS, LINK SETUP) */
_Static_assert(PG_LINKS + 2u == NPERF, "PERF's pages: GLOBAL MOD's last");
#define GLV(n, k) (VP_GL + (n) * NGLSET + (k))
static const uint16_t P_PERF[NPERF][4] = {{PAR_BPM, PAR_ROLL, PAR_MORPH, PAR_VOICE_DECIMATION_ALL},
                                          {VP_DJ, VP_REPEAT, PAR_SHUFFLE, VP_TAPE},
                                          {VP_DENS, VP_GHOST, VP_BUILD, VP_DVARY},
                                          {VP_STCH, VP_STSIZE, VP_STLEN, VP_STVARY},
                                          {VP_RUSH, VP_RTOP, VP_RFILT, VP_RGLIDE},
                                          {VP_RSTART, VP_RFREE, VP_RFLAM, VP_RPITCH},
                                          {VP_TRP, VP_TRWHEN, VP_TRHOLD, VP_NONE},
                                          {PAR_BPM, PAR_ROLL, PAR_MORPH, PAR_VOICE_DECIMATION_ALL},
                                          {VP_P2, VP_P2 + 1, VP_P2 + 2, VP_P2 + 3},
                                          {VP_P2 + 4, VP_P2 + 5, VP_P2 + 6, VP_P2 + 7},
                                          {VP_SP, VP_SP + 1, VP_SP + 2, VP_SP + 3},
                                          {VP_SP + 4, VP_SP + 5, VP_SP + 6, VP_SP + 7},
                                          {VP_SP + 8, VP_SP + 9, VP_SP + 10, VP_SP + 11},
                                          {VP_SP + 12, VP_SP + 13, VP_SP + 14, VP_SP + 15},
                                          {VP_SP + 16, VP_SP + 17, VP_NONE, VP_NONE},
                                          {VP_WD, VP_WD + 1, VP_WD + 2, VP_WD + 3},
                                          {VP_WD + 4, VP_WD + 5, VP_WD + 6, VP_WD + 7},
                                          {VP_WD + 8, VP_WD + 9, VP_WD + 10, VP_WD + 11},
                                          {GLV(0, 0), GLV(0, 1), GLV(0, 2), GLV(0, 7)},   /* (1.0: as every LFO: */
                                          {GLV(0, 3), GLV(0, 6), GLV(0, 4), GLV(0, 5)},   /* .. ui.c LP_*) */
                                          {GLV(1, 0), GLV(1, 1), GLV(1, 2), GLV(1, 7)},
                                          {GLV(1, 3), GLV(1, 6), GLV(1, 4), GLV(1, 5)},
                                          {GLV(2, 0), GLV(2, 1), GLV(2, 2), GLV(2, 7)},
                                          {GLV(2, 3), GLV(2, 6), GLV(2, 4), GLV(2, 5)},
                                          {GLV(3, 0), GLV(3, 1), GLV(3, 2), GLV(3, 7)},
                                          {GLV(3, 3), GLV(3, 6), GLV(3, 4), GLV(3, 5)},
                                          {VP_M1, VP_M1 + 1, VP_M1 + 2, VP_M1 + 3},
                                          {VP_MSEL, VP_MTARGET, VP_MMIN, VP_MMAX},
                                          {VP_LINK, VP_LSTART, VP_LAMT, VP_DEPTH},
                                          {VP_LWHEN, VP_LTGT, VP_LEFF, VP_LWIN}};
static const char *const PERF_TITLE[NPERF] = {"PERFORM", "PERFORM: MORE", "PERFORM: DENSITY", "PERFORM: STUTTER",
                                              "PERFORM: RUSH", "PERFORM: RUSH SETUP", "PERFORM: TRANSPOSE",
                                              "PERFORM 2", "PERFORM 2: SETUP (1/2)", "PERFORM 2: SETUP (2/2)",
                                              "SPELLS: GRIMOIRE", "SPELLS: CAST", "SPELLS: CHARACTER",
                                              "SPELLS: LIMITS", "SPELLS: MOTION", "WANDER: QUEST", "WANDER: PACE",
                                              "WANDER: HOME", "GLOBAL LFO 1 (1/2)", "GLOBAL LFO 1 (2/2)", "GLOBAL LFO 2 (1/2)",
                                              "GLOBAL LFO 2 (2/2)", "GLOBAL LFO 3 (1/2)", "GLOBAL LFO 3 (2/2)",
                                              "GLOBAL LFO 4 (1/2)", "GLOBAL LFO 4 (2/2)",
                                              "MACROS", "MACRO SETUP", "LINKS", "LINK SETUP"};
static int perf_dens(void) { return ui.mode == M_PERF && ui.page % NPERF == PG_DENS; }
static int perf_stut(void) { return ui.mode == M_PERF && ui.page % NPERF == PG_STUT; }
static int perf_rush(void) { return ui.mode == M_PERF && (ui.page % NPERF == PG_RUSH || ui.page % NPERF == PG_RUSH + 1u); }
static int perf_trp(void) { return ui.mode == M_PERF && ui.page % NPERF == PG_TRP; }
static int perf_links(void) { return ui.mode == M_PERF && ui.page % NPERF >= PG_LINKS; }
static int perf_spells(void) { return ui.mode == M_PERF && ui.page % NPERF >= PG_SPELL && ui.page % NPERF < PG_WANDER; }
static int perf_wander(void) { return ui.mode == M_PERF && ui.page % NPERF >= PG_WANDER && ui.page % NPERF < PG_GMOD; }
/* 29: the set of PERF pages shown (32: 0 PERFORM, 1 SPELLS, 40: 2 WANDER, 3 GLOBAL MOD): its first page, how many */
#define NPSET 4u
static const uint8_t PERF_SET0[NPSET + 1u] = {0, PG_SPELL, PG_WANDER, PG_GMOD, NPERF};
static uint32_t perf_set(void)
{
    uint32_t g = ui.page % NPERF;
    return g >= PG_GMOD ? 3u : g >= PG_WANDER ? 2u : g >= PG_SPELL ? 1u : 0u;
}
static uint32_t perf_first(void) { return PERF_SET0[perf_set()]; }
static uint32_t perf_count(void) { return (uint32_t)PERF_SET0[perf_set() + 1u] - PERF_SET0[perf_set()]; }
/* 0.13: the global LFO a PERF page sets (0, 1), -1 none */
static int32_t glfo_page(void)
{
    uint32_t g = ui.page % NPERF;
    return ui.mode == M_PERF && g >= PG_GLFO && g < PG_GLFO + 2u * NGLFO ? (int32_t)((g - PG_GLFO) / 2u) : -1;
}
static int perf_bank2(void) { return ui.mode == M_PERF && ui.page % NPERF >= PG_PERF2 && ui.page % NPERF < PG_SPELL; }
/* 0.10: the track's sequencing pages (SEQ; 0.10-0.18: ARP's second set): GENERATOR (the type, then its settings:
 * page_params), ACTION (GRIDS: and the map), TRACK, BOUNCE. 19a: SCALE (the pattern's root and scale, the track's
 * follow; TRACK has the follow too). 20: MELODY's two pages. 26: GROOVE (the pattern's groove, amount, HUMANIZE; the
 * track's follow). 29: TRACK first; the LINKS to PERF's GLOBAL MOD */
#define NEUCLID 9u
#define PG_TRACK 0u
#define PG_GEN 1u
#define PG_ACTION 2u
#define PG_MELODY 3u                                 /* (1.0: MELODY and SCALE before GROOVE and BOUNCE: the */
#define PG_SCALE 5u                                  /* .. more used, and easy to miss after them) */
#define PG_GROOVE 6u
#define PG_BOUNCE 7u
#define PG_TIME 8u                                   /* (1.0: TIME STRETCH) */
static const uint16_t P_EUCLID[NEUCLID][4] = {{PAR_TRACK_LENGTH, VP_CLOCK, VP_DIR, VP_SFOLLOW},
                                              {VP_GMODE, VP_GA, VP_GB, VP_GC},
                                              {VP_ACTION, VP_NONE, VP_NONE, VP_NONE},
                                              {VP_MEL + ML_SHAPE, VP_MEL + ML_SPREAD, VP_MEL + ML_BIAS, VP_MEL + ML_STEPS},
                                              {VP_MEL + ML_DEJA, VP_MEL + ML_LOOP, VP_MEL + ML_LEN, VP_MEL + ML_LVAR},
                                              {VP_SROOT, VP_SCALE, VP_SFOLLOW, VP_TBASE},
                                              {VP_GROOVE, VP_GAMT, VP_GHUM, VP_GFOLLOW},
                                              {VP_BSTART, VP_BPITCH, VP_NONE, VP_NONE},
                                              {VP_STRETCH, VP_REALIGN, VP_NONE, VP_NONE}};
static const char *const EUCLID_TITLE[NEUCLID] = {"TRACK", "GENERATOR", "ACTION", "MELODY (1/2)", "MELODY (2/2)",
                                                  "SCALE", "GROOVE", "BOUNCE", "TIME STRETCH"};
/* the GENERATOR knob's order (KNOB 1: the value's index here) */
static const uint8_t GEN_ORDER[NGEN] = {GEN_OFF, GEN_EUCLID, GEN_MUTATE, GEN_TURING, GEN_CA, GEN_GRIDS};
/* the actions a track's generator has (the ACTION knob's 1..; 0: none): EUCLID's steps diced, a live generator's
 * track committed or reset; UNDO for every one. 20: a melody's notes committed, it reset, as a live generator's */
#define NGACT 4u
static uint32_t gen_acts(const pattern_t *p, uint32_t t, uint8_t out[NGACT])
{
    uint32_t n = 0, g = p->gen[t][0];
    int live = gen_live(p, t) || mel_on(p, t);
    if (g == GEN_EUCLID)
        out[n++] = ACT_DICE;
    if (live)
        out[n++] = ACT_COMMIT;
    out[n++] = ACT_UNDO;
    if (live)
        out[n++] = ACT_RESET;
    return n;
}
/* the FX pages (0.16: in this order): the reverb, the bus, its type's knobs (not with the bus OFF); 1.0: the
 * always-on DELAY after the bus (TIME, FEEDBACK, TONE, LEVEL; its MODE, SYNC, ECHO > REVERB; with TAPE, its WOW,
 * SATURATE, GLITCH, PITCH); 23: DIRT, the bus's and the master's; the master's COMPRESSOR and its SIDECHAIN; 31: the
 * voices' FX and reverb sends, the mixer's pages. ui.page counts the pages shown (fx_page: which one it is) */
#define NFXPG 18u
enum { FXPG_REV, FXPG_BUS, FXPG_PARS, FXPG_DLY, FXPG_DLY2, FXPG_DUB, FXPG_ROUTE, FXPG_DIRT, FXPG_DJ, FXPG_DJ2, FXPG_COMP,
       FXPG_COMP2, FXPG_FSEND, FXPG_FSEND2, FXPG_DSEND, FXPG_DSEND2, FXPG_RSEND, FXPG_RSEND2 };   /* (1.0 Phase 16:
                                                      * ROUTING, the delay sends; in the order shown) */
static const uint16_t P_FX[NFXPG][4] = {{PAR_REV_TYPE, PAR_REV_SIZE, PAR_REV_DAMP, PAR_REV_LEVEL},
                                        {PAR_FX_TYPE, PAR_FX_DRY_WET, VP_FX_SUB, VP_NONE},
                                        {PAR_FX_P1, PAR_FX_P2, PAR_FX_P3, PAR_FX_P4},
                                        {PAR_FX_DELAY_TYPE, PAR_DSYNC, PAR_DLY_TIME, PAR_DLY_LEVEL},   /* (1.0: the user's */
                                        {PAR_DLY_TONE, PAR_ECHOREV, PAR_DLY_FB, PAR_DLY_BAND},   /* .. order: FEEDBACK
                                                      * and BAND side by side, as they work together) */
                                        {PAR_DLY_WOW, PAR_DSAT, PAR_GLITCH, PAR_DPITCH},
                                        {PAR_DLY_ROUTE, PAR_REV_FX, PAR_ECHOREV, VP_NONE},
                                        {PAR_FXDIRT, VP_NONE, PAR_MDIRT, PAR_MDIRT_AMT},
                                        {VP_DJTYPE, VP_DJ, VP_DJRESO, VP_DJTUNE},   /* (1.0: the global filter: Type, Filt, Reso; RING: Tune) */
                                        {VP_DJSPREAD, VP_DJSLOPE, VP_DJTONE, VP_DJWET},   /* (1.0 Phase 18: RING (2/2)) */
                                        {PAR_MC_THR, PAR_MC_RATIO, PAR_MC_ATK, PAR_MC_REL},
                                        {PAR_MC_SRC, PAR_MC_GAIN, PAR_MC_MIX, VP_NONE},
                                        {PAR_FX_SEND1, PAR_FX_SEND1 + 1, PAR_FX_SEND1 + 2, PAR_FX_SEND1 + 3},
                                        {PAR_FX_SEND1 + 4, PAR_FX_SEND1 + 5, PAR_FX_DRY_WET, VP_NONE},
                                        {PAR_DSEND1, PAR_DSEND1 + 1, PAR_DSEND1 + 2, PAR_DSEND1 + 3},
                                        {PAR_DSEND1 + 4, PAR_DSEND1 + 5, PAR_DLY_LEVEL, VP_NONE},
                                        {PAR_REV_SEND1, PAR_REV_SEND1 + 1, PAR_REV_SEND1 + 2, PAR_REV_SEND1 + 3},
                                        {PAR_REV_SEND1 + 4, PAR_REV_SEND1 + 5, PAR_REV_LEVEL, VP_MVOL}};
static uint32_t fx_shown(uint8_t out[NFXPG])        /* the pages shown, in order */
{
    uint32_t n = 0, t = par[PAR_FX_TYPE];
    out[n++] = FXPG_REV;
    out[n++] = FXPG_BUS;
    if (t != FX_OFF && t != FX_DELAY)
        out[n++] = FXPG_PARS;
    out[n++] = FXPG_DLY;
    if (par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN) {     /* (1.0 Phase 13: GRAIN's SIZE, SCATTER, DENSITY, PITCH second, */
        out[n++] = FXPG_DUB;                         /* .. the user's order) */
        out[n++] = FXPG_DLY2;
    } else {
        out[n++] = FXPG_DLY2;
        if (par[PAR_FX_DELAY_TYPE] == DELAY_TAPE || par[PAR_FX_DELAY_TYPE] == DELAY_TAPEPP)
            out[n++] = FXPG_DUB;
    }
    out[n++] = FXPG_ROUTE;                           /* (1.0 Phase 16: the bus, the delay and the reverb's order) */
    out[n++] = FXPG_DIRT;
    out[n++] = FXPG_DJ;
    if (pfs.dj_ftype == DJT_RING)                    /* (1.0 Phase 18: RING's second page) */
        out[n++] = FXPG_DJ2;
    out[n++] = FXPG_COMP;
    out[n++] = FXPG_COMP2;
    out[n++] = FXPG_FSEND;
    out[n++] = FXPG_FSEND2;
    out[n++] = FXPG_DSEND;
    out[n++] = FXPG_DSEND2;
    out[n++] = FXPG_RSEND;
    out[n++] = FXPG_RSEND2;
    return n;
}
static uint32_t fx_page(void)                        /* the page on screen (FXPG_*) */
{
    uint8_t l[NFXPG];
    uint32_t n = fx_shown(l);
    return l[ui.page < n ? ui.page : n - 1u];
}
#define NMIXER 10u
static const uint16_t P_MIXER[NMIXER][4] = {{PAR_VOL1, PAR_VOL2, PAR_VOL3, PAR_VOL4},
                                            {PAR_VOL5, PAR_VOL6, PAR_FX_DRY_WET, VP_MASTER},
                                            {PAR_PAN1, PAR_PAN2, PAR_PAN3, PAR_PAN4},
                                            {PAR_PAN5, PAR_PAN6, PAR_REV_LEVEL, PAR_REV_SIZE},
                                            {PAR_FX_SEND1, PAR_FX_SEND1 + 1, PAR_FX_SEND1 + 2, PAR_FX_SEND1 + 3},
                                            {PAR_FX_SEND1 + 4, PAR_FX_SEND1 + 5, PAR_FX_DRY_WET, VP_NONE},
                                            {PAR_DSEND1, PAR_DSEND1 + 1, PAR_DSEND1 + 2, PAR_DSEND1 + 3},   /* (1.0 */
                                            {PAR_DSEND1 + 4, PAR_DSEND1 + 5, PAR_DLY_LEVEL, VP_NONE},   /* .. Phase 16) */
                                            {PAR_REV_SEND1, PAR_REV_SEND1 + 1, PAR_REV_SEND1 + 2, PAR_REV_SEND1 + 3},
                                            {PAR_REV_SEND1 + 4, PAR_REV_SEND1 + 5, PAR_REV_LEVEL, VP_MVOL}};
static const char *const MIXER_TITLE[NMIXER / 2u] = {"MIXER: VOLUME", "MIXER: PAN", "MIXER: FX SEND",
                                                     "MIXER: DLY SEND", "MIXER: REV SEND"};   /* (Phase 12: REV, so (1/2) fits) */
#define NCONFIG 16u
static const uint16_t P_CONFIG[NCONFIG][4] = {
    {PAR_BPM, PAR_SHUFFLE, PAR_FOLLOW, VP_PLAY},     /* (37: Play in place of Kit chg) */
    {PAR_MIDI_CHAN_GLOBAL, PAR_MIDI_FILT_TX, PAR_MIDI_FILT_RX, PAR_MIDI_ROUTING},
    {PAR_MIDI_CHAN_1, PAR_MIDI_CHAN_2, PAR_MIDI_CHAN_3, PAR_MIDI_CHAN_4},
    {PAR_MIDI_CHAN_5, PAR_MIDI_CHAN_6, PAR_MIDI_CHAN_7, VP_GTAKES},
    {PAR_MIDI_NOTE1, PAR_MIDI_NOTE2, PAR_MIDI_NOTE3, PAR_MIDI_NOTE4},
    {PAR_MIDI_NOTE5, PAR_MIDI_NOTE6, PAR_MIDI_NOTE7, VP_NONE},
    {VP_MOUT, VP_MOUT + 1, VP_MOUT + 2, VP_MOUT + 3},   /* (1.0: MIDI OUT; the master volume) */
    {VP_MOUT + 4, VP_MOUT + 5, VP_MOUT + 6, VP_MVOL},
    {VP_UMIX, VP_UMIX + 1, VP_UMIX + 2, VP_UMIX + 3},   /* (1.0, Phase 8: USB MIX, the voices in the stereo mix) */
    {VP_UMIX + 4, VP_UMIX + 5, VP_NONE, VP_NONE},
    {VP_LEDS, VP_AUTOSAVE, VP_ACCEL, VP_PALETTE},
    {VP_RCH, VP_RCH2, VP_RKNOBS, VP_RMUTE},
    {VP_MEDIT, VP_CAKIT, VP_NONE, VP_NONE},
    {VP_PMEM, VP_KBSLOTS, VP_NONE, VP_NONE},
    {VP_NONE, VP_NONE, VP_NONE, VP_NONE},
    {VP_NONE, VP_NONE, VP_NONE, VP_NONE}};
static const char *const CONFIG_TITLE[NCONFIG] = {"SEQUENCER", "MIDI", "MIDI CHANNELS", "MIDI CHANNELS", "MIDI NOTES",
                                                  "MIDI NOTES", "MIDI OUT", "MIDI OUT, VOLUME", "USB MIX",
                                                  "USB MIX", "DEVICE",
                                                  "REMOTE", "MORPH",
                                                  "BUTTONS & KITS", "LOAD", "ELIXIR"};
#define CF_MOUT 6u                                    /* 1.0: MIDI OUT (2 pages) */
#define CF_UMIX 8u                                    /* 1.0: USB MIX (2 pages) */
#define CF_REMOTE 11u
#define CF_LOAD 14u                                   /* 17a, 17b: the load by part, the cores (ui_draw.c draw_load) */
#define CF_ABOUT 15u                                  /* 29: the potion, the version, the load of each core */
static const uint16_t P_NAME[4] = {VP_NAME_POS, VP_NAME_CHAR, VP_NAME_CASE, VP_NONE};
/* 37: the song: KNOB 2 the row (SELECT pages); ROWS: insert or delete one (OCT+), the play mode; 42: ROADS: the
 * song played as written or by its chances, how surprising, back to its first row every so many bars */
#define NSONGPG 3u
static const uint16_t P_SONG[NSONGPG][4] = {{VP_SONG_NUM, VP_SONG_ROW, VP_SONG_PAT, VP_SONG_REP},
                                            {VP_SONG_ROW, VP_SONG_EDIT, VP_SONG_PAT, VP_PLAY},
                                            {VP_SONG_NUM, VP_ROADS, VP_RSUR, VP_RHOME}};
enum { SE_INSERT, SE_DELETE, NSE };
static const uint16_t P_PATTERN[4] = {VP_VOICES, PAR_SHUFFLE, VP_PKEEP, VP_PLAY};   /* (37: Voices, Play) */
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
    case M_FX: {
        uint8_t f[NFXPG];
        return fx_shown(f);
    }
    case M_MIXER: return NMIXER;
    case M_CONFIG: return NCONFIG;
    case M_SONG: return NSONGPG;
    default: return 1;
    }
}
static void page_params(uint16_t out[4])
{
    const uint16_t *p = P_HOME;
    uint32_t k;
    if (ui.kb_open) {                                 /* 0.9.2: the kit browser: KNOB 1 its part (0.19: FACTORY too) */
        out[0] = VP_KB_SRC;
        out[1] = out[2] = out[3] = VP_NONE;
        return;
    }
    switch (ui.mode) {
    case M_SOUND:
        for (k = 0; k < 4u; k++) {
            uint32_t q = sect_par(ui.track, ui.sect, ui.half, k);
            out[k] = (uint16_t)(q ? q : VP_NONE);
        }
        return;
    case M_STEP: p = P_STEP[ui.page % 4u]; break;
    case M_PERF: p = P_PERF[ui.page % NPERF]; break;
    case M_EUCLID: {
        uint32_t g = shown_pat()->gen[ui.track][0];
        p = P_EUCLID[ui.page % NEUCLID];
        for (k = 0; k < 4u; k++)
            out[k] = p[k];
        if (ui.page % NEUCLID == PG_GEN && g == GEN_EUCLID) {
            out[1] = PAR_EUKLID_LENGTH;
            out[2] = PAR_EUKLID_STEPS;
            out[3] = PAR_EUKLID_ROTATION;
        } else if (ui.page % NEUCLID == PG_GEN && g == GEN_OFF) {
            out[1] = out[2] = out[3] = VP_NONE;
        } else if (ui.page % NEUCLID == PG_ACTION && g == GEN_GRIDS) {   /* (the map: only with GRIDS) */
            out[1] = VP_GX;
            out[2] = VP_GY;
            out[3] = VP_GCHAOS;
        } else if (ui.page % NEUCLID == PG_ACTION && g == GEN_EUCLID) {  /* 0.13: EUCLID's second pulse */
            out[1] = VP_EH2;
            out[2] = VP_ER2;
            out[3] = VP_EOP;
        }
        return;
    }
    case M_FX: p = P_FX[fx_page()]; break;
    case M_MIXER: p = P_MIXER[ui.page % NMIXER]; break;
    case M_CONFIG: p = P_CONFIG[ui.page % NCONFIG]; break;
    case M_SAVE:
    case M_LOAD:
        if (ui.io_naming) {
            p = P_NAME;
            break;
        }
        out[0] = VP_IO_TYPE;                          /* (KIT, MORPH: From / To before the Slot it lists, 0.25.3;
                                                      * SOUND: the Slot, then its track) */
        if (ui.io_type == IO_KIT || ui.io_type == IO_MORPH) {
            out[1] = VP_IO_SRC;
            out[2] = VP_IO_SLOT;
        } else if (ui.io_type == IO_FACTORY || ui.io_type == IO_FMORPH) {   /* (1.0: a KIT or MORPH From FACTORY) */
            out[1] = VP_IO_SRC;
            out[2] = VP_IO_SLOT;
        } else if (ui.io_type == IO_SOUND) {         /* (1.0: the track, then the Slot) */
            out[1] = VP_IO_TRACK;
            out[2] = VP_IO_SLOT;
        } else {
            out[1] = VP_IO_SLOT;
            out[2] = VP_NONE;
        }
        out[3] = VP_NONE;
        return;
    case M_SONG: p = P_SONG[ui.page % NSONGPG]; break;
    case M_PATTERN: p = P_PATTERN; break;
    case M_RECOPT: p = P_RECOPT; break;
    case M_KEYS: p = P_KEYS; break;
    default: break;
    }
    for (k = 0; k < 4u; k++) {
        uint32_t q = p[k];
        out[k] = (uint16_t)(q >= VP_GL && q <= VP_GL_LAST && gls[(q - VP_GL) / NGLSET].wave == GW_FOLLOW &&
                                    ((q - VP_GL) % NGLSET == 2u || (q - VP_GL) % NGLSET == 4u) ? VP_NONE : q);
    }                                                /* (1.0 Phase 11: FOLLOW: no SYNC, no MODE) */
    if (out[3] == VP_DJTUNE && pfs.dj_ftype != DJT_RING)
        out[3] = VP_NONE;                            /* (1.0: Tune: RING's alone) */
    if (out[2] == VP_WD + 10u && !wd_goal_song())
        out[2] = out[3] = VP_NONE;                   /* (1.0: WANDER's Order, Sway: Goal SONG's alone) */
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
    return p == &pat[sq.active] && gen_live(p, t) ? &gpat : p;
}
static uint32_t vp_max(uint32_t p);
/* 0.9: the kit browser's entries: 0..63 the slots, 64.. the library's kits that exist, KB_FAC.. the factory kits.
 * 0.19: one list, in parts: FACTORY, LIBRARY, SLOTS, or (CONFIG: Kits first SLOTS, kset.kbslots) SLOTS, LIBRARY,
 * FACTORY; KNOB 1 jumps between the parts */
#define KB_FAC (NUM_KITS + KL_KITS)
#define KB_N (KB_FAC + FACTORY_NKITS)
enum { KB_SLOTS, KB_LIB, KB_FACTORY };
static uint32_t kb_part(uint32_t i) { return i < NUM_KITS ? KB_SLOTS : i < KB_FAC ? KB_LIB : KB_FACTORY; }
static uint32_t kb_part_at(uint32_t o) { return kset.kbslots ? o : 2u - o; }    /* the part shown o-th (0..2) */
static const uint16_t KB_FIRST[3] = {0, NUM_KITS, KB_FAC}, KB_LEN[3] = {NUM_KITS, KL_KITS, FACTORY_NKITS};
static uint32_t kb_pos(uint32_t i)                  /* entry i's place in the list */
{
    uint32_t q = kb_part(i), o, pos = 0;
    for (o = 0; kb_part_at(o) != q; o++)
        pos += KB_LEN[kb_part_at(o)];
    return pos + i - KB_FIRST[q];
}
static int32_t kb_entry(int32_t pos)                /* the entry at a place, -1 past the ends */
{
    uint32_t o;
    if (pos < 0)
        return -1;
    for (o = 0; o < 3u; o++) {
        uint32_t q = kb_part_at(o);
        if (pos < (int32_t)KB_LEN[q])
            return (int32_t)KB_FIRST[q] + pos;
        pos -= (int32_t)KB_LEN[q];
    }
    return -1;
}
static int kb_has(int32_t i)
{
    if (i < 0 || i >= (int32_t)KB_N)
        return 0;
    return kb_part((uint32_t)i) != KB_LIB || kl_exists(KLK_KIT, (uint32_t)i - NUM_KITS);
}
static int32_t kb_part_start(uint32_t q)            /* where a part opens: the slots on the kit loaded; -1 empty */
{
    uint32_t i;
    if (q == KB_SLOTS)
        return (int32_t)kit_loaded;
    if (q == KB_LIB && kb_has((int32_t)(NUM_KITS + ui.lib_kit)))
        return (int32_t)(NUM_KITS + ui.lib_kit);
    for (i = KB_FIRST[q]; i < KB_FIRST[q] + KB_LEN[q]; i++)
        if (kb_has((int32_t)i))
            return (int32_t)i;
    return -1;
}
static uint32_t kb_start(void)                       /* where the list opens: where it was, else its first part */
{
    uint32_t o;
    if (ui.kb_at && kb_has(ui.kb_at - 1))
        return ui.kb_at - 1u;
    for (o = 0; o < 3u; o++)
        if (kb_part_start(kb_part_at(o)) >= 0)
            return (uint32_t)kb_part_start(kb_part_at(o));
    return kit_loaded;
}
static uint32_t kb_step(uint32_t s, int32_t n)       /* n entries on from s (as far as there are) */
{
    while (n) {
        int32_t d = n > 0 ? 1 : -1, pos = (int32_t)kb_pos(s) + d, j;
        while ((j = kb_entry(pos)) >= 0 && !kb_has(j))
            pos += d;
        if (j < 0)
            break;
        s = (uint32_t)j;
        n -= d;
    }
    return s;
}
static uint32_t kb_order(void)                       /* KNOB 1: the place (0..2) of the part kb_sel is in */
{
    uint32_t o;
    for (o = 0; kb_part_at(o) != kb_part(ui.kb_sel); o++)
        ;
    return o;
}
static void kb_jump(uint32_t o)                      /* KNOB 1 to the part shown o-th (an empty one passed over) */
{
    int32_t d = o < kb_order() ? -1 : 1, k = (int32_t)o, i;
    for (; k >= 0 && k < 3; k += d)
        if ((i = kb_part_start(kb_part_at((uint32_t)k))) >= 0) {
            ui.kb_sel = (uint16_t)i;
            return;
        }
}
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
/* 0.23-t7: a step's note as it plays in pattern p (its BASE); set from the STEP page: a NOTE_LOCK, but at BASE: plain,
 * following BASE again */
static uint32_t step_note_shown(const pattern_t *p, uint32_t t, uint32_t k)
{
    uint32_t n = p->step[t][k].note;
    return n & NOTE_LOCK ? n & 127u : (uint32_t)clamp32((int32_t)n + p->tbase[t], 0, 127);
}
static void step_note_lock(pattern_t *p, uint32_t t, uint32_t k, uint32_t v)
{
    p->step[t][k].note = (uint8_t)((int32_t)v == SEQ_DEFAULT_NOTE + p->tbase[t] ? SEQ_DEFAULT_NOTE : (v & 127u) | NOTE_LOCK);
}
/* 0.23-t7: TRANSPOSE: the keys' tracks (none: the shown one) to x degrees, now or at WHEN */
static void trp_set(int32_t x)
{
    uint32_t t;
    if (!sq.trmask)
        sq.trmask = (uint8_t)(1u << ui.track);
    for (t = 0; t < NTRACK; t++)
        if ((sq.trmask >> t) & 1u)
            sq.trpend[t] = (int8_t)x;
    if (!sq.running || !sq.trwhen)
        memcpy(sq.trp, sq.trpend, sizeof sq.trp);
    else
        sq.trdue = 1;
    ui.force = 1;
}
static uint32_t vp_min(uint32_t p);
static uint32_t vp_max(uint32_t p);
static const char *const FX_P_NAME[NFX][4] = {
    {"-", "-", "-", "-"}, {"Drive", "Color", "Tone", "Volume"}, {"Freq", "-", "-", "-"},
    {"Attack", "Release", "Threshld", "Gain"}, {"Time", "Range", "Tone", "Feedback"},
    {"Rate", "Depth", "Feedback", "Centre"}, {"Rate", "Depth", "Feedback", "Delay"},
    {"Rate", "Depth", "Delay", "Lo cut"}, {"Shift", "Fdbk", "Delay", "Spread"}};

/* a mod target's voice range (VEL DEST: its own; LFO TARGET: the voice PAR_VOICE_LFO picks) */
static void target_range(uint32_t p, uint32_t *lo, uint32_t *hi)
{
    uint32_t v = 0;
    if (p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6)
        v = p - PAR_VEL_DEST_1;
    else if (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) {
        v = par[PAR_VOICE_LFO1 + (p - PAR_TARGET_LFO1)];
        v = v >= 1u && v <= 6u ? v - 1u : 0u;
    } else if (p >= PAR_L2TGT1 && p < PAR_L2TGT1 + 6u)   /* (25b: LFO 2's: its own) */
        v = p - PAR_L2TGT1;
    *lo = MOD_TARGET_VOICE[v][0];
    *hi = MOD_TARGET_VOICE[v][1];
}

/* 25a: the target knob's order: OFF, the voice's LXR targets, then Elixir's (NUM_MOD_TARGETS ..) */
static int is_target(uint32_t p)
{
    return (p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) || (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) ||
           (p >= PAR_L2TGT1 && p < PAR_L2TGT1 + 6u);
}
static uint32_t target_voice(uint32_t p)            /* (the voice whose targets these are) */
{
    uint32_t lo, hi, v;
    target_range(p, &lo, &hi);
    for (v = 0; v < 5u && MOD_TARGET_VOICE[v][0] != lo; v++)
        ;
    return v;
}

/* 0.25.2: the targets by group, as EDIT shows the pages (the signal path: SECT_ORDER), each group by name; then the
 * ones on no sound page: the mix and sends, the FX bus, the reverb, the master. Their full names (the knob's are the
 * LXR's 8 letters) for the LFO page and the knob's order */
enum { TG_MIX = NSORD, TG_FXBUS, TG_REVERB, TG_MASTER, TG_NONE };
static const char *const TG_NAME[TG_NONE - NSORD] = {"MIX & SENDS", "FX BUS", "REVERB", "MASTER"};
static void card_name(char *b, uint32_t n, uint32_t p);
static uint32_t tgt_group(uint32_t v, uint32_t p)    /* (SECT_ORDER's rank, or TG_*) */
{
    uint32_t i, t, h, k;
    for (i = 0; i < NSORD; i++) {
        uint32_t s = SECT_ORDER[i];
        if (s == S_SENDS)                            /* (1.0 Phase 16: the sends stay MIX & SENDS', the returns theirs) */
            continue;
        for (t = v; t <= (v == 5u ? 6u : v); t++)   /* (the hat: the open hat's page too) */
            for (h = 0; h < 2u; h++)
                for (k = 0; k < 4u; k++)
                    if (sect_par_raw(t, s, h, k) == p)
                        return i;
    }
    if ((p >= PAR_VOL1 && p <= PAR_VOL6) || (p >= PAR_PAN1 && p <= PAR_PAN3) || (p >= PAR_PAN4 && p <= PAR_PAN6) ||
        (p >= PAR_FX_SEND1 && p < PAR_REV_SEND1 + 6u))
        return TG_MIX;
    if (p == PAR_REV_SIZE || p == PAR_REV_DAMP || p == PAR_REV_LEVEL)
        return TG_REVERB;
    return p == PAR_MDIRT_AMT ? TG_MASTER : TG_FXBUS;
}
static const char *tgt_group_name(uint32_t g)
{
    return g < NSORD ? SECT_NAME[SECT_ORDER[g]] : g < TG_NONE ? TG_NAME[g - NSORD] : "";
}
/* target idx of voice v: its group (TG_NONE: OFF) and its full name in b */
static uint32_t tgt_full(char *b, uint32_t n, uint32_t v, uint32_t idx)
{
    static const char *const X[][2] = {{"Frequncy", "Frequency"}, {"Resnance", "Resonance"}, {"Overdriv", "Overdrive"},
                                       {"PulsWdth", "Pulse width"}, {"ClockSnc", "Clock sync"}, {"Retriggr", "Retrigger"},
                                       {"SampleRt", "Sample rate"}, {"RepeatCt", "Repeat count"},
                                       {"Dcy Clsd", "Decay closed"}, {"Dcy Open", "Decay open"},
                                       {"Freqcy 1", "Frequency 1"}, {"Freqcy 2", "Frequency 2"},
                                       {"Vol mod", "To volume"}, {"Panning", "Pan"}, {"Struct", "Structure"},
                                       {"DstParam", "Dest"}, {"Threshld", "Threshold"}};
    uint32_t p = idx && idx < NUM_TARGETS_X && idx != 1u ? mod_dest(idx, v) : 0u, g, i;
    char t[24];
    if (!p) {
        put_str(b, "Off", n);
        return TG_NONE;
    }
    g = tgt_group(v, p);
    if (p >= PAR_FX_SEND1 && p < PAR_REV_SEND1 + 6u) {
        put_str(b, p < PAR_REV_SEND1 ? "FX send" : "Reverb send", n);
        return g;
    }
    if (g == TG_REVERB) {
        put_str(b, p == PAR_REV_SIZE ? "Size" : p == PAR_REV_DAMP ? "Damping" : "Level", n);
        return g;
    }
    if (p == PAR_MDIRT_AMT || p == PAR_ECHOREV) {
        put_str(b, p == PAR_ECHOREV ? "Echo to reverb" : "Dirt amount", n);
        return g;
    }
    if (par_dly(p)) {                                /* (1.0: the always-on delay's) */
        static const char *const D[6] = {"Delay time", "Delay feedback", "Delay tone", "Delay level", "Delay wow",
                                         "Delay band"};
        put_str(b, D[p - PAR_DLY_TIME < 6u ? p - PAR_DLY_TIME : 0u], n);
        return g;
    }
    if ((p >= PAR_FILTER_FREQ_1 && p <= PAR_RESO_6) || (p >= PAR_FILTER_DRIVE_1 && p <= PAR_FILTER_DRIVE_6))
        put_str(t, par_name(p), sizeof t);           /* (1.0: the cards' Freq, Reso, Drive: here in full, as X has) */
    else
        card_name(t, sizeof t, p);
    if (p >= PAR_FX_P1 && p <= PAR_FX_P4 && t[0] == '-') {   /* (no FX bus effect: its knob's number) */
        put_str(t, "Param ", sizeof t);
        put_int(t + 6, (int32_t)(p - PAR_FX_P1) + 1);
    }
    for (i = 0; i < sizeof X / sizeof X[0]; i++)
        if (str_eq(t, X[i][0])) {
            put_str(t, X[i][1], sizeof t);
            break;
        }
    b[0] = 0;
    if (p < NUM_PARAMS) {                            /* (one page's two of a name: the LXR's category says which) */
        const char *c = par_cat(p);
        put_str(b, str_eq(c, "Velocity") ? "Velocity " : str_eq(c, "PitchMod") ? "Pitch mod "
                   : str_eq(c, "Noise") ? "Noise " : str_eq(c, "Nois/Osc") ? "Noise/osc " : "", n);
    }
    if (b[0]) {
        uint32_t k = str_len(b);
        put_str(b + k, t, n - k);
        if (b[k] >= 'A' && b[k] <= 'Z')
            b[k] = (char)(b[k] + 32);
    } else {
        put_str(b, t, n);
    }
    return g;
}
static int tgt_before(uint32_t ga, const char *a, uint32_t gb, const char *b)   /* (a's place before b's) */
{
    if (ga != gb)
        return ga < gb;
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y)
            return x < y;
    }
    return !*a && *b;
}
/* the knob's list: OFF, then the voice's targets (the LXR's, Elixir's) in that order; n of them */
#define NTGT_LIST 72u
_Static_assert(1u + 38u + 4u + NXT <= NTGT_LIST, "a voice's targets");
static uint32_t tgt_list_v(uint32_t v, uint8_t out[NTGT_LIST])   /* (voice v's; 1.0: the delay's four too, sound.c
                                                      * TGT_DLY, for every voice; D2 the bus's as D1's) */
{
    char nm[NTGT_LIST][16];
    uint8_t gr[NTGT_LIST];
    uint32_t lo = MOD_TARGET_VOICE[v][0], hi = MOD_TARGET_VOICE[v][1], u, n = 1, i;
    out[0] = 0;
    for (u = lo; u <= hi + 4u + NXT && n < NTGT_LIST; u++) {
        uint32_t idx = u > hi + 4u ? NUM_MOD_TARGETS + u - hi - 5u : u > hi ? TGT_DLY + u - hi - 1u : u, g;
        if (tgt_dly(u) && u <= hi)                   /* (D2's FX P1..P4: D1's) */
            idx = MOD_TARGET_VOICE[0][1] - 3u + u - TGT_DLY;
        char b[16];
        g = tgt_full(b, sizeof b, v, idx);
        for (i = n; i > 1u && tgt_before(g, b, gr[i - 1u], nm[i - 1u]); i--) {   /* (in its place) */
            out[i] = out[i - 1u];
            gr[i] = gr[i - 1u];
            put_str(nm[i], nm[i - 1u], sizeof nm[i]);
        }
        out[i] = (uint8_t)idx;
        gr[i] = (uint8_t)g;
        put_str(nm[i], b, sizeof nm[i]);
        n++;
    }
    return n;
}
static uint32_t tgt_list(uint32_t p, uint8_t out[NTGT_LIST]) { return tgt_list_v(target_voice(p), out); }
static uint32_t target_turn(uint32_t p, uint32_t raw, int32_t n)
{
    uint8_t l[NTGT_LIST];
    uint32_t c = tgt_list(p, l), i;
    for (i = 0; i < c && l[i] != raw; i++)
        ;
    if (i == c)                                      /* (another voice's: from OFF) */
        i = 0;
    return l[clamp32((int32_t)i + n, 0, (int32_t)c - 1)];
}

/* 1.0: LFO 1's Dest (LFO l, PAR_TARGET_LFO1 + l) through every voice's, its own voice's first, then the others in
 * order (each OFF but the first's left out); the voice (PAR_VOICE_LFO1 + l) set as it goes. Its Voice is no knob */
static void lfo1_dest_turn(uint32_t l, int32_t n)
{
    uint8_t ord[NVOICE], li[NTGT_LIST];
    uint32_t oi, i, c, k, raw = par[PAR_TARGET_LFO1 + l], cv = par[PAR_VOICE_LFO1 + l];
    cv = cv >= 1u && cv <= 6u ? cv - 1u : 0u;
    ord[0] = (uint8_t)l;
    for (i = 0, k = 1; i < NVOICE; i++)
        if (i != l)
            ord[k++] = (uint8_t)i;
    for (oi = 0; oi < NVOICE && ord[oi] != cv; oi++)
        ;
    c = tgt_list_v(ord[oi %= NVOICE], li);
    for (i = 0; i < c && li[i] != raw; i++)
        ;
    if (i == c || !raw) {                            /* (OFF, or not in the list: from OFF) */
        oi = 0;
        c = tgt_list_v(ord[0], li);
        i = 0;
    }
    for (; n > 0; n--)
        if (i + 1u < c)
            i++;
        else if (oi + 1u < NVOICE) {
            c = tgt_list_v(ord[++oi], li);
            i = 1;
        }
    for (; n < 0; n++)
        if (i > 1u || (i == 1u && !oi))
            i--;
        else if (i == 1u) {
            c = tgt_list_v(ord[--oi], li);
            i = c - 1u;
        }
    par_set(PAR_VOICE_LFO1 + l, (li[i] ? ord[oi] : l) + 1u);
    par_set(PAR_TARGET_LFO1 + l, li[i]);
}
/* 0.25.2: the global LFOs' destinations the same way: by group in the signal's order, each group by name */
static const uint8_t GD_ORDER[NGD] = {GD_OFF, GD_DENSITY, GD_JITTER, GD_MCHANGE, GD_MSPREAD, GD_STUT, GD_TRANSPOSE,
                                      GD_MORPH, GD_DBAND, GD_DFB, GD_DTIME, GD_PUSH, GD_RSEND, GD_COMB, GD_CRUSH, GD_CUTS,
                                      GD_DIRT, GD_PUMP, GD_FILTER, GD_HP, GD_LP, GD_RTONE, GD_RWET, GD_MACRO1, GD_MACRO1 + 1,
                                      GD_MACRO1 + 2, GD_MACRO1 + 3};
static const char *const GD_FULL[NGD][2] = {
    {"", "Off"}, {"KIT", "Morph"}, {"DJ FILTER", "DJ filter"}, {"DJ FILTER", "Low-pass sweep"},
    {"DJ FILTER", "High-pass sweep"}, {"DELAY", "Feedback push"}, {"MASTER FX", "Comb"}, {"MASTER FX", "Crush"},
    {"SEQUENCER", "Jitter"}, {"MASTER FX", "Cuts"}, {"MASTER FX", "Pump"}, {"SEQUENCER", "Density"},
    {"SEQUENCER", "Transpose"}, {"SEQUENCER", "Melody change"}, {"SEQUENCER", "Melody spread"},
    {"DELAY", "Delay feedback"}, {"DELAY", "Delay time"}, {"REVERB", "Reverb sends"}, {"MACROS", "Macro 1"},
    {"MACROS", "Macro 2"}, {"MACROS", "Macro 3"}, {"MACROS", "Macro 4"}, {"SEQUENCER", "Stutter chance"},
    {"MASTER FX", "Dirt amount"}, {"DELAY", "Delay band"}, {"DJ FILTER", "Ring wet"}, {"DJ FILTER", "Ring tone"}};
_Static_assert(NMACRO == 4u && NGD == 27u, "GD_ORDER, GD_FULL: every destination");
static int is_gl_dest(uint32_t p) { return p >= VP_GL && p <= VP_GL_LAST && (p - VP_GL) % NGLSET == 0u; }
static uint32_t order_turn(const uint8_t *o, uint32_t c, uint32_t v, int32_t n)   /* (v n places on in order o) */
{
    uint32_t i;
    for (i = 0; i < c && o[i] != v; i++)
        ;
    return o[clamp32((int32_t)(i < c ? i : 0u) + n, 0, (int32_t)c - 1)];
}
static uint32_t gd_turn(uint32_t d, int32_t n) { return order_turn(GD_ORDER, NGD, d, n); }
#define NDLY_ORDER (NDELAY + 2u)
static const uint8_t DLY_ORDER[NDLY_ORDER] = {DELAY_OFF, DELAY_NORMAL, DELAY_PINGPONG, DELAY_TAPE, DELAY_TAPEPP, DELAY_GRAIN};   /* (1.0:
                                                      * the delay's MODE knob, OFF first) */

/* a parameter's index in the lock targets (lock_target_par; 0: none) */
static uint32_t lock_idx_of(uint32_t d)
{
    uint32_t i;
    if (!d)
        return 0;
    if (d >= NUM_PARAMS)                              /* Elixir's: after the LXR's targets */
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
static uint32_t macro_put(uint32_t m, uint32_t v)   /* (its parameter at macro value v: the value, 0 none) */
{
    uint32_t p = pfs.m[m].par;
    int32_t lo = pfs.m[m].lo, hi = pfs.m[m].hi, x;
    if (!p)
        return 0;
    x = lo + (hi - lo) * (int32_t)v / 127;
    vp_set(p, (uint32_t)x);
    return (uint32_t)x;
}
static int8_t macro_lfo[NMACRO];                     /* (19c: the global LFOs' push on each, as last applied) */
static void macro_set(uint32_t m, uint32_t v)
{
    uint32_t p = pfs.m[m].par, x;
    pfs.m[m].val = (uint8_t)v;
    x = macro_put(m, (uint32_t)clamp32((int32_t)v + macro_lfo[m], 0, 127));
    if (p && sq.rec && par_lockable(p))              /* REC: a lock on the playing step, as a knob's */
        seq_record_automation(ui.track, p, x);
}
/* 19c: the global LFOs on the macros (MACRO 1..4): each macro's parameter where its knob is, moved either way by
 * them while playing (stopped: back where the knob is, so the session rests), at most every 8 ms */
static void glfo_macros(void)
{
    static uint32_t ms;
    uint32_t m;
    if (fm1_ms - ms < 8u)
        return;
    ms = fm1_ms;
    for (m = 0; m < NMACRO; m++) {
        int32_t o = sq.running ? gl.bi[GD_MACRO1 - GD_DENSITY + m] * 127 / 65536 : 0;
        if (o == macro_lfo[m])
            continue;
        macro_lfo[m] = (int8_t)o;
        macro_put(m, (uint32_t)clamp32((int32_t)pfs.m[m].val + o, 0, 127));
    }
}

/* 1.0: CONFIG > MORPH, Edits SPLIT (the default): with the morph knob at 100% a sound knob edits the morph kit
 * (what is heard; SAVE > MORPH keeps it), below it the kit alone. BOTH KITS: both move (morph_follow); KIT ONLY:
 * the kit. (medit 0 SPLIT, 1 KIT ONLY, 2 BOTH KITS: settings from before hold 0, BOTH KITS's then) */
static int morph_side(uint32_t p)
{
    return kset.medit == 0u && par[PAR_MORPH] >= 255u && p < NPAR && par_sound(p) && !morph_excluded(p);
}
static uint32_t vp_get(uint32_t p)
{
    pattern_t *pt = shown_pat();
    switch (p) {
    case PAR_TRACK_LENGTH: return track_len(pt, ui.track);
    case PAR_STEP_VOLUME: return sel_step()->volume & STEP_VOLUME_MASK;
    case PAR_STEP_NOTE: return step_note_shown(shown_pat(), ui.track, ui.step & 63u);
    case PAR_STEP_PROB: return step_prob(sel_step()) > 100u ? 100u : step_prob(sel_step());
    case PAR_STEP_FLAM: return pt->timing[ui.track][ui.step & 63u] & 15u;
    case PAR_STEP_SHIFT: return pt->timing[ui.track][ui.step & 63u] >> 4;
    case PAR_P1_DEST:
    case PAR_P2_DEST: {
        uint32_t c = lock_get(pt, ui.track, ui.step & 63u, p == PAR_P2_DEST);
        return c == RUSH_CODE ? LOCK_RUSH_IDX : lock_idx_of(autodest_to_par(c));   /* (39: RUSH) */
    }
    case PAR_P1_VAL: return sel_step()->p1v;
    case PAR_P2_VAL: return sel_step()->p2v;
    case PAR_EUKLID_LENGTH: case PAR_EUKLID_STEPS: case PAR_EUKLID_ROTATION:
        return pt->gen[ui.track][p - PAR_EUKLID_LENGTH + 1u];
    case PAR_ROLL: return sq.roll_rate;
    case PAR_MORPH: return par[PAR_MORPH];           /* (0.13: the knob; morph_amt has the LFOs' push) */
    case PAR_AUTOM_TRACK: return sq.lane;
    case VP_TRACK_VOL: return par[PAR_VOL1 + track_voice(ui.track)];
    case VP_FX_SUB:
        return fx.type == FX_DRIVE ? par[PAR_FX_DIST_TYPE] : fx.type == FX_RING ? par[PAR_FX_WAVE]
             : fx.type == FX_COMP ? par[PAR_FX_RATIO] : 0u;
    case VP_IO_TYPE: {                               /* (1.0: its place in IO_*_ORD; FACTORY is KIT's) */
        uint32_t i, t = ui.io_type == IO_FACTORY ? IO_KIT : ui.io_type == IO_FMORPH ? IO_MORPH : ui.io_type;
        for (i = 0; i < io_nord() && io_ord(i) != t; i++)
            ;
        return i < io_nord() ? i : 0u;
    }
    case VP_IO_SLOT: return ui.io_slot;
    case VP_IO_SRC: return ui.io_type == IO_FACTORY || ui.io_type == IO_FMORPH ? 2u : ui.io_src;   /* (1.0: SLOTS,
                                                                                                * LIBRARY, FACTORY) */
    case VP_IO_TRACK: return ui.track;
    case VP_NAME_POS: return ui.io_pos;
    case VP_NAME_CHAR: return (uint8_t)ui.io_name[ui.io_pos & 7u];
    case VP_NAME_CASE: return ui.io_name[ui.io_pos & 7u] >= 'a' && ui.io_name[ui.io_pos & 7u] <= 'z';
    case VP_SONG_PAT: {                           /* the patterns, then END, LOOP */
        uint32_t v = song_row(songs[sq.song][ui.song_row & 63u]);
        return v < NUM_PATTERN ? v : v == SONG_LOOP ? NUM_PATTERN + 1u : NUM_PATTERN;
    }
    case VP_SONG_NUM: return sq.song;
    case VP_SONG_REP: return song_rep_i(sq.song, ui.song_row);
    case VP_SONG_ROW: return ui.song_row;
    case VP_SONG_EDIT: return ui.song_edit;
    case VP_PLAY: return sq.song_mode;
    case VP_ROADS: return song_rd[sq.song % NUM_SONGS] >> 7;
    case VP_RSUR: return (song_rd[sq.song % NUM_SONGS] & 15u) ^ 4u;
    case VP_RHOME: return (song_rd[sq.song % NUM_SONGS] >> 4) & 7u;
    case VP_VOICES: return pat_voices(&pat[sq.active]);
    case VP_LEDS: return kset.leds;
    case VP_AUTOSAVE: return autosave_on;
    case VP_PKEEP: return pat_keep;
    case VP_ACCEL: return kset.accel;
    case VP_PALETTE: return kset.palette;
    case VP_RCH: return kset.remote > 16u ? 0u : kset.remote ? kset.remote : 15u;   /* (0: OFF) */
    case VP_RCH2: return kset.rglobal > 16u ? 0u : kset.rglobal ? kset.rglobal : 16u;
    case VP_RKNOBS: return kset.rknobs & RK_KNOBS;
    case VP_RMUTE: return (kset.rknobs & RK_TOGGLE) != 0;
    case VP_CAKIT: return kset.cakit;
    case VP_MEDIT: return kset.medit < 3u ? kset.medit : 0u;
    case VP_PMEM: return kset.pmem;
    case VP_KBSLOTS: return kset.kbslots;
    case VP_GTAKES: return kset.gtakes;
    case VP_MVOL: return out_vol;
    case VP_MOUT ... VP_MOUT_LAST: return (kset.mout >> (p - VP_MOUT)) & 1u;
    case VP_UMIX ... VP_UMIX_LAST: return (kset.mixout >> (p - VP_UMIX)) & 1u;
    case VP_TD ... VP_TD_LAST: return (uint32_t)(sq.tdens[p - VP_TD] + 64);
    case VP_STRETCH: return sq.tstr[ui.track];
    case VP_REALIGN: return kset.realign != 0;
    case VP_GL ... VP_GL_LAST: {
        const uint32_t n = (p - VP_GL) / NGLSET, k = (p - VP_GL) % NGLSET;
        const uint8_t f[NGLSET] = {gls[n].dest, (uint8_t)(gls[n].depth + 127), gls[n].len, gls[n].wave, gls[n].mode,
                                   gls[n].trig, gls[n].phase, gls[n].rate};
        return f[k];
    }
    case VP_EH2: case VP_ER2: case VP_EOP: return pt->eu2[ui.track][p - VP_EH2];
    case VP_MASTER: return (uint32_t)(master_q12 >> 5);
    case VP_DJ: return pf.dj;
    case VP_REPEAT: return pfs.repeat;
    case VP_TAPE: return pfs.tape;
    case VP_DJRESO: return pfs.dj_reso;
    case VP_DJTYPE: return djt_place(pfs.dj_ftype);   /* (1.0: the knob's order) */
    case VP_DJTUNE: return pfs.dj_tune;
    case VP_DJSPREAD: return pfs.dj_spread;          /* (1.0 Phase 18) */
    case VP_DJSLOPE: return pfs.dj_slope;
    case VP_DJTONE: return pfs.dj_tone;
    case VP_DJWET: return pfs.dj_wet;
    case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3: return pfs.m[p - VP_M1].val;
    case VP_MSEL: return ui.msel;
    case VP_MTARGET: return lock_idx_of(pfs.m[ui.msel].par);
    case VP_MMIN: return pfs.m[ui.msel].lo;
    case VP_MMAX: return pfs.m[ui.msel].hi;
    case VP_COND: return step_cond(pt, ui.track, ui.step & 63u);
    case VP_LEN: return len_pos(step_len(pt, ui.track, ui.step & 63u));   /* (0.23-t11: the knob's position) */
    case VP_DENS: return (uint32_t)(pfs.dens + 64);
    case VP_GHOST: return pfs.ghost;
    case VP_BUILD: return pfs.build < NDB ? pfs.build : DB_GHOSTS;
    case VP_DVARY: return pfs.dvary;
    case VP_SP ... VP_SP_LAST: return sp_vget(p - VP_SP);
    case VP_WD ... VP_WD_LAST: return wd_vget(p - VP_WD);
    case VP_STCH: return pfs.st_chance;
    case VP_RUSH:                                    /* (39: a step held: its RUSH lock) */
        return ui.held_step >= 0 ? rush_lock(shown_pat(), ui.track, (uint32_t)ui.held_step) : sq.ru_live;
    case VP_RTOP: return sq.ru_top < NRT ? sq.ru_top : RT_256;
    case VP_RFILT: return sq.ru_filt;
    case VP_RGLIDE: return sq.ru_glide;
    case VP_RSTART: return sq.ru_start;
    case VP_RFREE: return sq.ru_free_at;
    case VP_RFLAM: return sq.ru_flam;
    case VP_RPITCH: return (uint32_t)(sq.ru_pitch + 12);
    case VP_STSIZE: return pfs.st_size < NSTSIZE ? pfs.st_size : 1u;
    case VP_STLEN: return pfs.st_len < NSTLEN ? pfs.st_len : 2u;
    case VP_STVARY: return pfs.st_vary < NSV ? pfs.st_vary : SV_OFF;
    case VP_SROOT: return pt->sroot % 12u;
    case VP_SCALE: return pt->scale < NSCALE ? pt->scale : SC_CHROM;
    case VP_SFOLLOW: return (pt->sfollow >> ui.track) & 1u;
    case VP_TBASE: return (uint32_t)(pt->tbase[ui.track] + TBASE_MAX);
    case VP_TRP: return (uint32_t)(sq.trpend[ui.track] + 7);
    case VP_TRWHEN: return sq.trwhen;
    case VP_TRHOLD: return sq.trhold;
    case VP_GROOVE: return pt->groove <= NGROOVE ? pt->groove : 0u;
    case VP_GAMT: return pt->gamt > 127u ? 127u : pt->gamt;
    case VP_GHUM: return pt->ghum > 127u ? 127u : pt->ghum;
    case VP_GFOLLOW: return !((pt->gskip >> ui.track) & 1u);
    case VP_MEL ... VP_MEL_LAST: {
        uint32_t k = p - VP_MEL, v = pt->mel[ui.track][k];
        return k == ML_SHAPE ? (v < NMEL ? v : MEL_OFF) : k == ML_LOOP ? (v < 1u ? 1u : v > 16u ? 16u : v)
             : k == ML_LEN ? (v < NLEN ? v : LEN_AUTO) : v > 127u ? 127u : v;
    }
    case VP_RAT_N: return pt->rat[ui.track][ui.step & 63u] & 7u;
    case VP_RAT_RATE: return (pt->rat[ui.track][ui.step & 63u] >> 3) & 3u;
    case VP_RAT_FADE: return (pt->rat[ui.track][ui.step & 63u] >> 5) & 3u;
    case VP_ACTION: {
        uint8_t a[NGACT];
        uint32_t n = gen_acts(pt, ui.track, a), i;
        for (i = 0; i < n; i++)
            if (a[i] == ui.action)
                return i + 1u;
        return 0;
    }
    case VP_GMODE: {
        uint32_t i;
        for (i = 0; i < NGEN && GEN_ORDER[i] != pt->gen[ui.track][0]; i++)
            ;
        return i < NGEN ? i : 0u;
    }
    case VP_DIR: return pt->dir[ui.track] < NDIR ? pt->dir[ui.track] : DIR_FWD;
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
    case VP_KB_SRC: return kb_order();
    case VP_LWHEN: {
        const uint8_t *L = pt->link[ui.link & 7u];
        return (L[0] & 7u) % NTRACK * NLV + (L[0] >> 6) % NLV;
    }
    case VP_LTGT: return (pt->link[ui.link & 7u][0] >> 3 & 7u) % NTRACK;
    case VP_LEFF: return (pt->link[ui.link & 7u][1] & 15u) % NLE;
    case VP_LWIN: return pt->link[ui.link & 7u][1] >> 4 & 3u;
    default:
        return p >= NPAR ? 0u : morph_side(p) ? morph_par[kit_idx(p)] : par[p];
    }
}
static uint32_t vp_min(uint32_t p)
{
    uint32_t lo, hi;
    if (is_target(p))
        return 0;
    (void)lo;
    (void)hi;
    switch (p) {
    case PAR_TRACK_LENGTH: case PAR_EUKLID_LENGTH: return 1;
    case PAR_EUKLID_STEPS: case PAR_EUKLID_ROTATION: return 0;   /* (0.10: 0 hits, none) */
    case VP_GA: return shown_pat()->gen[ui.track][0] == GEN_TURING ? 2u : 0u;
    case VP_P2 + 4: return 24;
    case VP_STRETCH: return TSTR_MIN;
    case VP_KVEL: case VP_KACC: case VP_GHOST: case VP_MEL + ML_LOOP: return 1;
    case PAR_BPM: return 1;
    case PAR_MIDI_CHAN_1: case PAR_MIDI_CHAN_2: case PAR_MIDI_CHAN_3: case PAR_MIDI_CHAN_4: case PAR_MIDI_CHAN_5:
    case PAR_MIDI_CHAN_6: case PAR_MIDI_CHAN_7: case PAR_MIDI_CHAN_GLOBAL: return 1;
    default: return p < NPAR ? par_min(p) : 0u;
    }
}
static uint32_t vp_max(uint32_t p)
{
    uint32_t lo, hi;
    if (is_target(p)) {
        target_range(p, &lo, &hi);
        return hi;
    }
    switch (p) {
    case PAR_TRACK_LENGTH: case PAR_EUKLID_LENGTH: return 64;
    case PAR_EUKLID_STEPS: return shown_pat()->gen[ui.track][1] ? shown_pat()->gen[ui.track][1] : 16u;
    case PAR_EUKLID_ROTATION: return (shown_pat()->gen[ui.track][1] ? shown_pat()->gen[ui.track][1] : 16u) - 1u;
    case PAR_STEP_VOLUME: case PAR_STEP_NOTE: case PAR_P1_VAL: case PAR_P2_VAL: return 127;
    case PAR_STEP_PROB: return 100;
    case PAR_STEP_FLAM: return 5;
    case PAR_STEP_SHIFT: return 14;
    case PAR_P1_DEST: case PAR_P2_DEST: return LOCK_RUSH_IDX;   /* (39: RUSH last) */
    case PAR_ROLL: return 13;
    case PAR_MORPH: return 255;
    case PAR_AUTOM_TRACK: return 1;
    case PAR_BPM: return 255;
    case PAR_SHUFFLE: return 127;
    case PAR_FOLLOW: return 1;
    case PAR_PATTERN_MODE: return 1;
    case VP_TRACK_VOL: return 127;
    case VP_FX_SUB:
        return fx.type == FX_DRIVE ? 2u : fx.type == FX_RING || fx.type == FX_COMP ? 5u : 0u;
    case VP_IO_TYPE: return io_nord() - 1u;
    case VP_IO_SLOT:
        return io_lib() ? (ui.io_type == IO_SOUND ? KL_SOUNDS : KL_KITS) - 1u
             : ui.io_type == IO_PROJECT ? PROJ_USER - 1u : ui.io_type == IO_PATTERN ? NUM_PATTERN - 1u
             : ui.io_type == IO_FACTORY ? FACTORY_NKITS          /* (the last: all of them into the slots) */
             : ui.io_type == IO_FMORPH ? FACTORY_NKITS - 1u      /* (a morph target: one kit, no ALL) */
             : ui.io_type == IO_SONG ? NUM_SONGS - 1u : NUM_KITS - 1u;
    case VP_NAME_POS: return 7;
    case VP_NAME_CHAR: return 126;
    case VP_NAME_CASE: return 1;
    case VP_SONG_PAT: return NUM_PATTERN + 1u;
    case VP_SONG_NUM: return NUM_SONGS - 1u;
    case VP_SONG_REP: return NSONGREP - 1u;
    case VP_SONG_ROW: return MAX_SONG_LENGTH - 1u;
    case VP_SONG_EDIT: return NSE - 1u;
    case VP_PLAY: case VP_ROADS: return 1;
    case VP_RSUR: return 15;
    case VP_RHOME: return NSONGHOME - 1u;
    case VP_VOICES: return NPV - 1u;
    case VP_LEDS: return 2;
    case VP_IO_SRC: return ui.mode == M_LOAD && (ui.io_type == IO_KIT || ui.io_type == IO_FACTORY ||
                                                 ui.io_type == IO_MORPH || ui.io_type == IO_FMORPH) ? 2u : 1u;
    case VP_AUTOSAVE: case VP_ACCEL: case VP_PKEEP: case VP_KBSLOTS: return 1;
    case VP_KB_SRC: return 2;
    case VP_GTAKES: return NGTAKES - 1u;
    case VP_IO_TRACK: return NTRACK - 1u;
    case VP_PALETTE: return NPALETTES - 1u;
    case VP_RCH: case VP_RCH2: return 16;
    case VP_RKNOBS: return NRK - 1u;
    case VP_MEDIT: return 2;
    case VP_CAKIT: case VP_PMEM: case VP_RMUTE: case VP_MOUT ... VP_MOUT_LAST: case VP_UMIX ... VP_UMIX_LAST: return 1;
    case VP_MVOL: case VP_TD ... VP_TD_LAST: return 127;
    case VP_STRETCH: return TSTR_MAX;
    case VP_REALIGN: return 1;
    case VP_DJTYPE: return NDJT - 1u;
    case VP_DJTUNE: return NRTUNE - 1u;
    case VP_GL ... VP_GL_LAST: {
        static const uint8_t MX[NGLSET] = {NGD - 1, 254, GL_FREE, GW_FOLLOW, NGM - 1, NGT - 1, 127, 127};
        if ((p - VP_GL) % NGLSET == 5u && gls[(p - VP_GL) / NGLSET].wave == GW_FOLLOW)
            return GF_ALL;                           /* (1.0 Phase 11: FOLLOW's SOURCE: a voice, ALL) */
        return MX[(p - VP_GL) % NGLSET];
    }
    case VP_EH2: return shown_pat()->gen[ui.track][1] ? shown_pat()->gen[ui.track][1] : 16u;
    case VP_ER2: return (shown_pat()->gen[ui.track][1] ? shown_pat()->gen[ui.track][1] : 16u) - 1u;
    case VP_EOP: return NEUOP - 1u;
    case VP_DJSPREAD ... VP_DJWET:
    case VP_MASTER: case VP_DJ: case VP_TAPE: case VP_DJRESO: case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3: return 127;
    case VP_REPEAT: case VP_MSEL: return 3;
    case VP_MTARGET: return NUM_MOD_TARGETS - 1u + NLOCK_K;
    case VP_MMIN: case VP_MMAX: return macro_max(ui.msel);
    case VP_COND: return NCOND - 1u;
    case VP_LEN: return NLEN_KNOB - 1u;
    case VP_DENS: case VP_GHOST: case VP_DVARY: return 127;
    case VP_SP ... VP_SP_LAST: return sp_vmax(p - VP_SP);
    case VP_WD ... VP_WD_LAST: return wd_vmax(p - VP_WD);
    case VP_BUILD: return NDB - 1u;
    case VP_STCH: return 127;
    case VP_RUSH: case VP_RFILT: case VP_RGLIDE: case VP_RFLAM: return RUSH_MAX;   /* (39: GLIDE in 5 ms) */
    case VP_RSTART: return RUSH_MAX - 2u;
    case VP_RFREE: return RUSH_MAX - 1u;
    case VP_RTOP: return NRT - 1u;
    case VP_RPITCH: return 24;
    case VP_STSIZE: return NSTSIZE - 1u;
    case VP_STLEN: return NSTLEN - 1u;
    case VP_STVARY: return NSV - 1u;
    case VP_SROOT: return 11;
    case VP_SCALE: return NSCALE - 1u;
    case VP_SFOLLOW: return 1;
    case VP_TBASE: return 2u * TBASE_MAX;
    case VP_TRP: return 15;
    case VP_TRWHEN: return 2;
    case VP_TRHOLD: return 1;
    case VP_GROOVE: return NGROOVE;
    case VP_GAMT: case VP_GHUM: return 127;
    case VP_GFOLLOW: return 1;
    case VP_MEL ... VP_MEL_LAST: {
        static const uint8_t MX[NML] = {NMEL - 1, 127, 127, 127, 127, 16, NLEN - 1, 127};
        return MX[p - VP_MEL];
    }
    case VP_RAT_N: return 7;
    case VP_RAT_RATE: return 3;
    case VP_RAT_FADE: return 2;
    case VP_ACTION: {
        uint8_t a[NGACT];
        return gen_acts(shown_pat(), ui.track, a);
    }
    case VP_GMODE: return NGEN - 1u;
    case VP_DIR: return NDIR - 1u;
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
        static const uint8_t MX[NGEN][3] = {{0, 0, 0}, {100, 0, 1}, {16, 127, 127}, {255, 0, 1}, {2, 127, 1},
                                            {0, 0, 0}};
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
        if (ui.ca_len) {                             /* (1.0: Control All: every track's) */
            uint32_t t;
            for (t = 0; t < NTRACK; t++)
                pt->len[t] = (uint8_t)v;
        }
        return;
    case PAR_STEP_VOLUME:
        sel_step()->volume = (uint8_t)((sel_step()->volume & STEP_ACTIVE_MASK) | v);
        return;
    case PAR_STEP_NOTE: step_note_lock(shown_pat(), ui.track, ui.step & 63u, v); return;
    case PAR_STEP_PROB: sel_step()->prob = (uint8_t)((sel_step()->prob & ~PROB_MASK) | v); return;
    case PAR_STEP_FLAM:
        pt->timing[ui.track][ui.step & 63u] = (uint8_t)((pt->timing[ui.track][ui.step & 63u] & 0xF0u) | v);
        return;
    case PAR_STEP_SHIFT:
        pt->timing[ui.track][ui.step & 63u] = (uint8_t)((pt->timing[ui.track][ui.step & 63u] & 0x0Fu) | v << 4);
        return;
    case PAR_P1_DEST:
    case PAR_P2_DEST: {
        uint32_t d = v == LOCK_RUSH_IDX ? RUSH_CODE : v ? par_to_autodest(lock_target_par(v)) : NO_AUTOMATION;
        lock_set(pt, ui.track, ui.step & 63u, p == PAR_P2_DEST, d);
        return;
    }
    case PAR_P1_VAL: sel_step()->p1v = (uint8_t)v; return;
    case PAR_P2_VAL: sel_step()->p2v = (uint8_t)v; return;
    case PAR_EUKLID_LENGTH:
    case PAR_EUKLID_STEPS:
    case PAR_EUKLID_ROTATION:
    case VP_EH2: case VP_ER2: case VP_EOP: {
        uint8_t *g = pt->gen[ui.track], *e = pt->eu2[ui.track];
        if (p == PAR_EUKLID_LENGTH) {
            g[1] = (uint8_t)v;
            if (g[2] > v)
                g[2] = (uint8_t)v;
            if (g[3] >= v)
                g[3] = 0;
            if (e[0] > v)                            /* (0.13: the second pulse's too) */
                e[0] = (uint8_t)v;
            if (e[1] >= v)
                e[1] = 0;
        } else if (p == PAR_EUKLID_STEPS) {
            g[2] = (uint8_t)v;
        } else if (p == PAR_EUKLID_ROTATION) {
            g[3] = (uint8_t)v;
        } else {
            e[p - VP_EH2] = (uint8_t)v;
        }
        if (!g[2] && !e[0] && p != PAR_EUKLID_STEPS && p != VP_EH2)   /* 0.10: nothing written until hits are turned */
            return;
        if (ui.eu_undo != ui.track + 1u) {           /* (UNDO: the track before the first turn) */
            seq_undo_save(ui.track);
            ui.eu_undo = (uint8_t)(ui.track + 1u);
        }
        euclid_generate(ui.track, pt);
        sq.gsync |= (uint8_t)(1u << ui.track);
        return;
    }
    case PAR_ROLL: sq.roll_rate = (uint8_t)v; par[PAR_ROLL] = (uint8_t)v; sq.roll_free = 0; return;
    case PAR_MORPH: kit_set_morph(v); return;
    case PAR_AUTOM_TRACK: sq.lane = (uint8_t)v; return;
    case PAR_SHUFFLE: par[PAR_SHUFFLE] = (uint8_t)v; pat[sq.active].shuffle = (uint8_t)v; return;
    case VP_TRACK_VOL: par_set(PAR_VOL1 + track_voice(ui.track), v); return;
    case VP_FX_SUB:
        if (fx.type >= FX_DRIVE && fx.type <= FX_COMP)
            par_set(fx.type == FX_DRIVE ? PAR_FX_DIST_TYPE : fx.type == FX_RING ? PAR_FX_WAVE : PAR_FX_RATIO, v);
        return;
    case VP_IO_TYPE:
        ui.io_type = (uint8_t)io_ord(v);
        ui.io_slot = (uint8_t)(io_lib() ? io_slot_default(ui.io_type) : 0u);
        ui.bank = (uint8_t)(ui.io_slot / 16u);
        bank_fix();
        return;
    case VP_IO_SLOT: ui.io_slot = (uint8_t)v; return;
    case VP_IO_SRC:
        if (v == 2u) {                               /* (1.0: KIT, MORPH From FACTORY: the factory kits' list) */
            ui.io_type = ui.io_type == IO_MORPH || ui.io_type == IO_FMORPH ? IO_FMORPH : IO_FACTORY;
        } else {
            if (ui.io_type == IO_FACTORY)
                ui.io_type = IO_KIT;
            else if (ui.io_type == IO_FMORPH)
                ui.io_type = IO_MORPH;
            ui.io_src = (uint8_t)v;
        }
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
        song_put(sq.song, ui.song_row, v < NUM_PATTERN ? v : v == NUM_PATTERN ? SONG_END : SONG_LOOP,
                 song_rep_i(sq.song, ui.song_row));   /* (0.19: its times kept) */
        return;
    case VP_SONG_NUM:
        sq.song = (uint8_t)v;
        sq.song_nx = 0xFF;
        return;
    case VP_SONG_REP:
        if (song_row(songs[sq.song][ui.song_row & 63u]) < NUM_PATTERN)
            song_put(sq.song, ui.song_row, song_row(songs[sq.song][ui.song_row & 63u]), v);
        return;
    case VP_SONG_ROW: ui.song_row = (uint8_t)v; return;
    case VP_SONG_EDIT: ui.song_edit = (uint8_t)v; return;
    case VP_PLAY: seq_play_song(v); return;
    case VP_ROADS: case VP_RSUR: case VP_RHOME: {   /* (42: this song's) */
        uint8_t *r = &song_rd[sq.song % NUM_SONGS];
        *r = (uint8_t)(p == VP_ROADS ? (*r & 0x7Fu) | v << 7
                       : p == VP_RSUR ? (*r & 0xF0u) | (v ^ 4u) : (*r & 0x8Fu) | v << 4);
        sq.song_nx = 0xFF;
        return;
    }
    case VP_VOICES:
        pv_set(v);
        ui_message(v == PV_LOCKED ? "VOICES KEPT IN ITS KIT SLOT" : v == PV_KIT ? "PLAYS ITS SAVED KIT"
                                                                             : "SHARED VOICES");
        return;
    case VP_LEDS: kset.leds = (uint8_t)v; settings_later(); return;
    case VP_AUTOSAVE: autosave_on = (uint8_t)v; settings_later(); return;
    case VP_PKEEP: pat_keep = (uint8_t)v; kset.pdrop = (uint8_t)!v; settings_later(); return;
    case VP_ACCEL: kset.accel = (uint8_t)v; settings_later(); return;
    case VP_PALETTE: kset.palette = (uint8_t)v; palette_apply(); settings_later(); ui.force = 1; return;
    case VP_RCH:
    case VP_RCH2: {                                  /* (0.16: TRACKS', GLOBAL's; never the same: the other skipped) */
        uint32_t old = vp_get(p), other = vp_get(p == VP_RCH ? VP_RCH2 : VP_RCH);
        if (v && v == other)
            v = v > old ? (v < 16u ? v + 1u : old) : v - 1u;
        if (p == VP_RCH) {
            kset.remote = (uint8_t)(v ? v : 17u);
            remote_ch = (uint8_t)(v ? v - 1u : 0xFFu);
        } else {
            kset.rglobal = (uint8_t)(v ? v : 17u);
            remote_gch = (uint8_t)(v ? v - 1u : 0xFFu);
        }
        remote_reset();
        settings_later();
        return;
    }
    case VP_RKNOBS: kset.rknobs = (uint8_t)((kset.rknobs & RK_TOGGLE) | v); remote_reset(); settings_later(); return;
    case VP_RMUTE: kset.rknobs = (uint8_t)((kset.rknobs & RK_KNOBS) | (v ? RK_TOGGLE : 0u)); settings_later(); return;
    case VP_CAKIT: kset.cakit = (uint8_t)v; settings_later(); return;
    case VP_MEDIT: kset.medit = (uint8_t)v; settings_later(); return;
    case VP_PMEM: kset.pmem = (uint8_t)v; settings_later(); return;
    case VP_KBSLOTS: kset.kbslots = (uint8_t)v; ui.kb_at = 0; settings_later(); return;
    case VP_GTAKES: kset.gtakes = midi_gtakes = (uint8_t)v; settings_later(); return;
    case VP_MVOL: kset.mvol = out_vol = (uint8_t)v; settings_later(); return;
    case VP_MOUT ... VP_MOUT_LAST: {
        uint32_t b = 1u << (p - VP_MOUT);
        kset.mout = midi_mout = (uint8_t)(v ? kset.mout | b : kset.mout & ~b);
        settings_later();
        return;
    }
    case VP_UMIX ... VP_UMIX_LAST: {
        uint32_t b = 1u << (p - VP_UMIX);
        kset.mixout = mix_out = (uint8_t)(v ? kset.mixout | b : kset.mixout & ~b);
        settings_later();
        return;
    }
    case VP_TD ... VP_TD_LAST: sq.tdens[p - VP_TD] = (int8_t)((int32_t)v - 64); return;
    case VP_STRETCH:                                 /* (by hand: a quest's way back into time lets go) */
        sq.thome &= (uint8_t)~(1u << ui.track);
        seq_stretch_set(ui.track, v);
        return;
    case VP_REALIGN: kset.realign = sq.realign_pat = (uint8_t)(v != 0u); settings_later(); return;
    case VP_GL ... VP_GL_LAST: {
        const uint32_t n = (p - VP_GL) / NGLSET;
        switch ((p - VP_GL) % NGLSET) {
        case 0: gls[n].dest = (uint8_t)v; break;
        case 1: gls[n].depth = (int8_t)((int32_t)v - 127); break;
        case 2: gls[n].len = (uint8_t)v; break;
        case 3:
            gls[n].wave = (uint8_t)v;
            if (v != GW_FOLLOW && gls[n].trig >= NGT)   /* (1.0 Phase 11: out of FOLLOW: its SOURCE no trigger) */
                gls[n].trig = GT_PLAY;
            break;
        case 4: gls[n].mode = (uint8_t)v; break;
        case 5: gls[n].trig = (uint8_t)v; break;
        case 6: gls[n].phase = (uint8_t)v; break;
        default: gls[n].rate = (uint8_t)v; break;
        }
        return;
    }
    case VP_NONE: case VP_MASTER: return;
    case VP_DJ: pf.dj = (uint8_t)v; return;
    case VP_REPEAT: pfs.repeat = (uint8_t)v; return;
    case VP_TAPE: pfs.tape = (uint8_t)v; return;
    case VP_DJRESO: pfs.dj_reso = (uint8_t)v; return;
    case VP_DJTYPE: pfs.dj_ftype = DJT_AT[v < NDJT ? v : 0u]; return;
    case VP_DJTUNE: pfs.dj_tune = (uint8_t)(v < NRTUNE ? v : 0u); return;
    case VP_DJSPREAD: pfs.dj_spread = (uint8_t)(v & 127u); return;   /* (1.0 Phase 18) */
    case VP_DJSLOPE: pfs.dj_slope = (uint8_t)(v & 127u); return;
    case VP_DJTONE: pfs.dj_tone = (uint8_t)(v & 127u); return;
    case VP_DJWET: pfs.dj_wet = (uint8_t)(v & 127u); return;
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
    case VP_COND:
        pt->cond[ui.track][ui.step & 63u] = (uint8_t)((pt->cond[ui.track][ui.step & 63u] & ~COND_MASK) | v);
        return;
    case VP_LEN: step_len_set(pt, ui.track, ui.step & 63u, len_knob(v)); return;
    case VP_DENS: pfs.dens = (int8_t)((int32_t)v - 64); return;
    case VP_GHOST: pfs.ghost = (uint8_t)v; return;
    case VP_BUILD: pfs.build = (uint8_t)v; return;
    case VP_DVARY: pfs.dvary = (uint8_t)v; return;
    case VP_SP ... VP_SP_LAST: sp_vset(p - VP_SP, v); return;
    case VP_WD ... VP_WD_LAST: wd_vset(p - VP_WD, v); return;
    case VP_STCH: pfs.st_chance = (uint8_t)v; return;
    case VP_RUSH:                                    /* (from OFF: the shown track's) */
        if (!sq.ru_live)
            sq.ru_t = (uint8_t)(ui.track < NTRACK ? ui.track : 0u);
        sq.ru_live = (uint8_t)v;
        return;
    case VP_RTOP: sq.ru_top = (uint8_t)v; return;
    case VP_RFILT: sq.ru_filt = (uint8_t)v; return;
    case VP_RGLIDE: sq.ru_glide = (uint8_t)v; return;
    case VP_RSTART:                                  /* (START under FREE: FREE moves up) */
        sq.ru_start = (uint8_t)v;
        if (sq.ru_free_at <= v)
            sq.ru_free_at = (uint8_t)(v + 1u);
        return;
    case VP_RFREE:
        sq.ru_free_at = (uint8_t)(v ? v : 1u);
        if (sq.ru_start >= sq.ru_free_at)
            sq.ru_start = (uint8_t)(sq.ru_free_at - 1u);
        return;
    case VP_RFLAM: sq.ru_flam = (uint8_t)v; return;
    case VP_RPITCH: sq.ru_pitch = (int8_t)((int32_t)v - 12); return;
    case VP_STSIZE: pfs.st_size = (uint8_t)v; return;
    case VP_STLEN: pfs.st_len = (uint8_t)v; return;
    case VP_STVARY: pfs.st_vary = (uint8_t)v; return;
    case VP_SROOT: case VP_SCALE:                    /* (19a: the scale a blank pattern starts with, from now) */
        if (p == VP_SROOT)
            pt->sroot = (uint8_t)v;
        else
            pt->scale = (uint8_t)v;
        scl_last.root = pt->sroot;
        scl_last.scale = pt->scale;
        return;
    case VP_SFOLLOW: pt->sfollow = (uint8_t)(v ? pt->sfollow | 1u << ui.track : pt->sfollow & ~(1u << ui.track)); return;
    case VP_TBASE: pt->tbase[ui.track] = (int8_t)((int32_t)v - TBASE_MAX); return;
    case VP_TRP:                                     /* (0.23-t7: the shown track and the keys' tracks) */
        sq.trmask |= (uint8_t)(1u << ui.track);
        trp_set((int32_t)v - 7);
        return;
    case VP_TRWHEN: sq.trwhen = (uint8_t)v; return;
    case VP_TRHOLD: sq.trhold = (uint8_t)v; return;
    case VP_GROOVE:                                  /* (26: from OFF with the amount at 0: as written) */
        if (v && !pt->groove && !pt->gamt)
            pt->gamt = GAMT_DEFAULT;
        pt->groove = (uint8_t)v;
        return;
    case VP_GAMT: pt->gamt = (uint8_t)v; return;
    case VP_GHUM: pt->ghum = (uint8_t)v; return;
    case VP_GFOLLOW: pt->gskip = (uint8_t)(v ? pt->gskip & ~(1u << ui.track) : pt->gskip | 1u << ui.track); return;
    case VP_MEL ... VP_MEL_LAST: {                  /* (a melody turned on: its settings from their defaults) */
        uint8_t *m = pt->mel[ui.track];
        if (p == VP_MEL + ML_SHAPE && v && !m[ML_SHAPE] && !m[ML_SPREAD] && !m[ML_LOOP])
            memcpy(m, MEL_DEFAULT, NML);
        m[p - VP_MEL] = (uint8_t)v;
        return;
    }
    case VP_RAT_N: case VP_RAT_RATE: case VP_RAT_FADE: {
        uint8_t *r = &pt->rat[ui.track][ui.step & 63u];
        uint32_t sh = p == VP_RAT_N ? 0u : p == VP_RAT_RATE ? 3u : 5u, m = p == VP_RAT_N ? 7u : 3u;
        *r = (uint8_t)((*r & ~(m << sh)) | v << sh);
        return;
    }
    case VP_ACTION: {
        uint8_t a[NGACT];
        uint32_t n = gen_acts(pt, ui.track, a);
        ui.action = (uint8_t)(v && v <= n ? a[v - 1u] : ACT_NONE);
        return;
    }
    case VP_GMODE: {                                /* a generator: its settings from their defaults, its live track */
        static const uint8_t DEF[NGEN][3] = {{0, 0, 0}, {25, 0, 0}, {8, 64, 64}, {30, 0, 0}, {0, 64, 1}, {16, 0, 0}};
        uint8_t *g = pt->gen[ui.track];
        v = GEN_ORDER[v < NGEN ? v : 0];
        if (g[0] == v)
            return;
        g[0] = (uint8_t)v;
        memcpy(g + 1, DEF[v], 3);
        ui.action = ACT_NONE;
        if (v == GEN_GRIDS)                          /* (the part from the track: drums BD, snare SD, metal HH) */
            g[1] = (uint8_t)(ui.track < 3u ? 0u : ui.track == 3u ? 1u : 2u);
        if (v == GEN_EUCLID)                         /* (0.10: over the track's length, no hits yet: nothing written) */
            g[1] = (uint8_t)track_len(pt, ui.track);
        sq.gsync |= (uint8_t)(1u << ui.track);
        return;
    }
    case VP_GA: case VP_GB: case VP_GC:
        pt->gen[ui.track][p - VP_GA + 1u] = (uint8_t)v;
        if (pt->gen[ui.track][0] == GEN_GRIDS)       /* (GRIDS: the live track again from the map) */
            sq.gsync |= (uint8_t)(1u << ui.track);
        return;
    case VP_CLOCK: pt->clk[ui.track] = (uint8_t)v; return;
    case VP_DIR: pt->dir[ui.track] = (uint8_t)v; return;
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
    case VP_KB_SRC:
        kb_jump(v);
        ui.kb_ms = fm1_ms;
        return;
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
        if (morph_side(p)) {                         /* (1.0: SPLIT at 100%: the morph kit's) */
            morph_par[kit_idx(p)] = (uint8_t)v;
            snd_post(p);
            return;
        }
        if (p >= PAR_VOICE_LFO1 && p <= PAR_VOICE_LFO6) {     /* a new voice: its target moves along */
            uint32_t l = p - PAR_VOICE_LFO1, old = par[p], tp = PAR_TARGET_LFO1 + l, idx = par[tp];
            uint32_t ov = old >= 1u && old <= 6u ? old - 1u : 0u, nv = v >= 1u && v <= 6u ? v - 1u : 0u;
            par[p] = (uint8_t)v;
            if (idx >= NUM_MOD_TARGETS)              /* (25a: Elixir's go with the voice as they are) */
                par_set(tp, idx);
            else if (idx >= MOD_TARGET_VOICE[ov][0] && idx <= MOD_TARGET_VOICE[ov][1]) {
                idx = idx - MOD_TARGET_VOICE[ov][0] + MOD_TARGET_VOICE[nv][0];
                if (idx > MOD_TARGET_VOICE[nv][1])
                    idx = MOD_TARGET_VOICE[nv][1];
                par_set(tp, idx);
            }
            return;
        }
        if (is_target(p)) {
            uint32_t lo, hi;
            target_range(p, &lo, &hi);
            if (v && v < lo)                         /* 0 (off), or inside the voice's targets */
                v = lo;
        }
        par_set(p, v);
        return;
    }
}

/* ------------------------------------------------------ control all --- */
/* 0.11: Control All (Elektron's): OCT+ held on a sound page, a knob moves its parameter on every voice that has it,
 * by the knob's steps from each voice's own value (clamped), so the voices keep their differences. The first turn
 * keeps the kit as it was. Letting go of OCT+ asks (the FM-1's thumb can't easily reach OCT- while holding OCT+, and
 * a slipped thumb mustn't keep it all): OCT+ again reverts, OCT- keeps; the changes sound meanwhile. OCT- while OCT+
 * is still held reverts at once. No locks are recorded: it is a kit move. A kit loaded meanwhile ends it, kept.
 * 0.12: with morph on, the morph target's value moves by the same steps (CONFIG: CONTROL ALL, Morph BOTH KITS, the
 * default), so the change sounds at any morph amount (a + d morphed to b + d is the mixture + d) and stays as morph
 * turns; KIT ONLY moves the kit alone, the LXR's way (at full morph, nothing heard). */
static uint8_t ca_snap[KIT_LEN] POOL, ca_msnap[KIT_LEN] POOL;
static uint32_t ca_name(uint32_t t) { return t == 31u || t == 32u ? 4u : t; }   /* (the hats' closed, open decay) */
/* the parameter on track u that is knob k's of section s's half h on track tr's page (the same name in the same
 * section), 0 none */
static uint32_t ca_par_at(uint32_t tr, uint32_t s, uint32_t h, uint32_t k, uint32_t u)
{
    const uint16_t *a = LXR_PAGES[track_page(tr)][s], *b;
    uint32_t j = lxr_knob(s, h, k), i, t, n = 0;
    if (s >= S_SHAPE || s == S_LFO || (s == S_FILTER && h))   /* (Elixir's: blocks of six, the voice's own; the LFOs) */
        return sect_par(u, s, h, k);
    if (!(t = ca_name(a[j])))
        return 0;
    for (i = 0; i < j; i++)                          /* (its n-th of that name: the metal voices' two frequencies) */
        if (ca_name(a[i]) == t)
            n++;
    b = LXR_PAGES[track_page(u)][s];
    for (i = 0; i < 8u; i++)
        if (ca_name(b[i]) == t && !n--)
            return b[8u + i] != PAR_NONE ? b[8u + i] : 0u;
    return 0;
}
/* 1.0 Phase 17: a track left out of Control All (kset.caout; the hats' voice only when CH and OH both are) */
static int ca_isout(uint32_t u) { return u < 5u ? (kset.caout >> u) & 1u : (kset.caout & 0x60u) == 0x60u; }
static int ca_keys(void) { return ca_rm || (ui.copy && ui.ca && !ui.ca_len); }   /* (a track key: out / in) */
/* 1.0 (10c): the MODEL knob's order: the LXR's, Rings', Plaits', MICRO's, then Elixir's own (CLOUD, in P GRAIN's
 * number; RAIN, in P PARTCL's; NOISE, in P NOISE's; ENSEMBLE). The ones a voice hasn't (model_ok: Rings', Plaits' and CLOUD on SN-HH, the
 * dropped ones) passed */
static const uint8_t MODEL_ORDER[NMODEL] = {
    MD_LXR, MD_MODAL, MD_STRING, MD_FM, MD_PL0, MD_PL0 + 1, MD_PL0 + 2, MD_PL0 + 4, MD_PL0 + 5, MD_PL0 + 6,
    MD_PL0 + 7, MD_PL0 + 8, MD_PL0 + 11, MD_PMODAL, MD_PL0 + 13, MD_PL0 + 14, MD_PL0 + 15, MD_MC0,
    MD_MC0 + 1, MD_MC0 + 2, MD_MC0 + 3, MD_MC0 + 4, MD_MC0 + 5, MD_MC0 + 6, MD_MC0 + 7, MD_MC0 + 8, MD_MC0 + 9,
    MD_CLOUD, MD_RAIN, MD_NOISE, MD_EN};   /* (Phase 21: RAIN, in P PARTCL's number, after CLOUD) */
/* 1.0: the drum voices other than vi with a Rings or Plaits model (the ones the limit counts): bit k for Dk+1 */
static uint32_t models_elsewhere(uint32_t vi)
{
    uint32_t k, m = 0;
    for (k = 0; k < 3u; k++)
        if (k != vi && model_counted(model_pick(k, par[PAR_MODEL1 + k])))
            m |= 1u << k;
    return m;
}
/* voice vi's model n of its models on from md, in MODEL_ORDER (at an end: there). 1.0: while the other drum voices
 * hold the models' limit (rings.c model_limit), past Rings' and Plaits' (and said why), so a model never moves
 * another voice to the LXR's */
static uint32_t model_turn(uint32_t vi, uint32_t md, int32_t n)
{
    int32_t i, j;
    uint32_t el = vi <= 2u ? models_elsewhere(vi) : 0u, full = (uint32_t)__builtin_popcount(el) >= model_cap, past = 0;
    for (i = 0; i < (int32_t)NMODEL && MODEL_ORDER[i] != md; i++)
        ;
    if (i >= (int32_t)NMODEL)
        i = 0;
    while (n) {
        int32_t d = n > 0 ? 1 : -1;
        for (j = i + d; j >= 0 && j < (int32_t)NMODEL &&
                        (!model_ok(vi, MODEL_ORDER[j]) || (full && model_counted(MODEL_ORDER[j]) && (past = 1)));
             j += d)
            ;
        if (j < 0 || j >= (int32_t)NMODEL)
            break;
        i = j;
        n -= d;
    }
    if (past) {                                      /* (which voices have them: "D2 + D3 HAVE THE MODELS") */
        char b[32];
        uint32_t k, o = 0;
        for (k = 0; k < 3u; k++)
            if ((el >> k) & 1u) {
                if (o)
                    b[o++] = '+';
                b[o++] = 'D';
                b[o++] = (char)('1' + k);
            }
        b[o] = 0;
        put_str(b + o, model_cap == 1u ? " HAS THE 1 MODEL" : " HAVE THE 2 MODELS", sizeof b - o);
        ui_message(b);
    }
    return MODEL_ORDER[i];
}
/* a knob from from to to: past the values it hasn't (24: a MODEL knob in MODEL_ORDER, the voice's models alone) */
static uint32_t model_step(uint32_t p, uint32_t from, uint32_t to)
{
    if (p == PAR_FX_TYPE && to == FX_DELAY)          /* (1.0: the delay is always there, not a type: past it) */
        return to > from ? FX_DELAY + 1u : FX_DELAY - 1u;
    if (p == PAR_FX_TYPE && to == FX_DRIVE && !fx_live(to))   /* (Phase 10f: DRIVE set aside) */
        return to > from ? FX_RING : FX_OFF;
    if (p == PAR_FX_TYPE && to == FX_CHORUS && !fx_live(to))   /* (CHORUS set aside: FLANGER, then SHIFT) */
        return to > from ? FX_SHIFT : FX_FLANGER;
    if (p >= PAR_INS1 && p < PAR_INS1 + 6u && to != ins_live(to))   /* (1.0: the dropped inserts: past them) */
        return to > from ? INS_SHIFT : INS_OFF;
    if (p < PAR_MODEL1 || p >= PAR_MODEL1 + 6u)
        return to;
    return model_turn(p - PAR_MODEL1, from, (int32_t)to - (int32_t)from);
}
/* 1.0: a knob's value step places on from v, as the knob turns it (the syncs by length, the targets by group) */
static uint32_t knob_next(uint32_t q, uint32_t v, int32_t step)
{
    if (is_lfo_sync(q))
        return order_turn(SYNC_ORDER, NSYNC, v, step);
    if (is_target(q))
        return target_turn(q, v, step);
    if (q == PAR_FX_DELAY_TYPE)
        return order_turn(DLY_ORDER, NDLY_ORDER, v, step);
    return model_step(q, v, (uint32_t)clamp32((int32_t)v + step, (int32_t)vp_min(q), (int32_t)vp_max(q)));
}
/* knob k of section s's half h, as on track tr's page, on every track in Control All by step from each one's value */
static void ca_move(uint32_t tr, uint32_t s, uint32_t h, uint32_t k, int32_t step)
{
    uint16_t done[NTRACK];
    uint32_t u, i, nd = 0;
    if (!ui.ca) {
        for (i = 0; i < KIT_LEN; i++) {
            ca_snap[i] = par[kit_par(i)];
            ca_msnap[i] = morph_par[i];
        }
        ui.ca = 1;
        ui.ca_kept = 1;
        ui.ca_loads = kit_loads;
        ui.copy_used = 1;                            /* (nothing copied on release) */
        ui.copy_src = 0;
    }
    for (u = 0; u < NTRACK; u++) {
        uint32_t q = ca_par_at(tr, s, h, k, u);
        for (i = 0; i < nd && done[i] != q; i++)     /* (the hats' voice once) */
            ;
        if (!q || i < nd || (u != tr && ca_isout(u)))   /* (Phase 17: the tracks left out, but the one turned) */
            continue;
        done[nd++] = (uint16_t)q;
        if (q >= PAR_TARGET_LFO1 && q <= PAR_TARGET_LFO6) {   /* (1.0: each LFO 1 through its own list) */
            lfo1_dest_turn(q - PAR_TARGET_LFO1, step);
            continue;
        }
        if (morph_side(q)) {                         /* (1.0: SPLIT at 100%: the morph kit, as a knob; Control All's
                                                      * KIT ONLY: the kit) */
            if (kset.cakit)
                par_set(q, knob_next(q, par[q], step));
            else
                vp_set(q, knob_next(q, vp_get(q), step));
            continue;
        }
        if (morph_amt && !kset.cakit && par_sound(q) && !morph_excluded(q)) {   /* (the target first: vp_set sounds it) */
            uint8_t *m = &morph_par[kit_idx(q)];
            *m = (uint8_t)knob_next(q, *m, step);
        }
        vp_set(q, knob_next(q, vp_get(q), step));
    }
}
static void control_all(uint32_t k, int32_t step) { ca_move(ui.track, ui.sect, ui.half, k, step); }
/* 1.0 Phase 17: a voice's parameter p to v from MIDI (the remote's TRACKS rows, a CC, an NRPN; the main loop) while
 * Control All is on by MIDI: p's knob on every track in it, by the step it made on its own track. 0: not Control
 * All's (off, its own track left out, a list a step means nothing in, no knob has it): the caller sets it alone */
static int ca_take(uint32_t p, uint32_t v)
{
    uint32_t t, s, h, k, vo = par_voice(p);
    int32_t d;
    if (!ca_rm || vo >= 6u || is_lfo_sync(p) || is_target(p) || (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) ||
        (p >= PAR_MODEL1 && p < PAR_MODEL1 + 6u))
        return 0;
    for (t = vo; t <= (vo == 5u ? 6u : vo); t++)     /* (the hat: the open hat's page too) */
        for (s = 0; s < NSECT; s++)
            for (h = 0; h < 2u; h++)
                for (k = 0; k < 4u; k++)
                    if (sect_par_raw(t, s, h, k) == p)
                        goto found;
    return 0;
found:
    if (ca_isout(t) || ca_par_at(t, s, h, k, t) != p)   /* (left out; a knob its model hides now) */
        return 0;
    d = (int32_t)v - (int32_t)vp_get(p);
    if (d)
        ca_move(t, s, h, k, d);
    ui.force = 1;
    return 1;
}
/* 1.0 Phase 17: a track key while Control All is on: the track left out (its voice back as Control All found it) or
 * back in; kept in the settings */
static void ca_out_key(uint32_t t)
{
    uint32_t v = track_voice(t), i;
    int was = ca_isout(t);
    char b[24];
    kset.caout ^= (uint8_t)(1u << t);
    settings_later();
    if (ui.ca && !ui.ca_len && !was && ca_isout(t)) {
        for (i = 0; i < KIT_LEN; i++)
            if (par_voice(kit_par(i)) == v) {
                par[kit_par(i)] = ca_snap[i];
                morph_par[i] = ca_msnap[i];
            }
        snd_post_all();
    }
    put_str(b, VOICE_SHORT[t], sizeof b);
    put_str(b + 2, (kset.caout >> t) & 1u ? " LEFT OUT" : " BACK IN", sizeof b - 2u);
    ui_message(b);
    ui.force = 1;
}
static void control_all_undo(void)
{
    uint32_t i;
    if (ui.ca_len) {                                 /* 1.0: the lengths as they were */
        pattern_t *pt = shown_pat();
        for (i = 0; i < NTRACK; i++)
            pt->len[i] = ui.ca_lens[i];
        ui.ca = ui.ca_ask = ui.ca_len = 0;
        ui_message("LENGTHS REVERTED");
        return;
    }
    for (i = 0; i < KIT_LEN; i++) {
        par[kit_par(i)] = ca_snap[i];
        morph_par[i] = ca_msnap[i];
    }
    snd_post_all();
    ui.ca = ui.ca_ask = ui.ca_kept = 0;
    ui_message("CONTROL ALL REVERTED");
}

/* 31: with morph on, a knob's edit moves the morph target by as much (CONFIG: MORPH, Edits BOTH KITS, the default), as
 * Control All does: heard at any morph amount, and it stays as morph turns. KIT ONLY: the kit alone (the LXR's way:
 * at full morph, nothing heard) */
/* 1.0: the MODE knob into GRAIN with Density OFF (no grains: a new kit's, or TAPE's Glitch at 0) starts a cloud you
 * hear: Size 129 ms, Scatter 40%, Density 16/s (about two grains at once), Level 100 if it was 0; Position, Pitch,
 * Feedback as they were. With no delay send up, a message says where the sound goes in */
/* 1.0: GRAIN's Size, Scatter, Density and Pitch are the tape modes' Wow, Saturate, Glitch and Pitch (the same four
 * parameters). Crossing into GRAIN or out of it, the side being left is kept aside and the other's put back (until a
 * kit is loaded: then GRAIN's starting cloud, or the tape's zeros), so neither shows the other's numbers */
static void morph_follow(uint32_t p, int32_t d);
static void dly_cross(uint32_t from, uint32_t to)
{
    static const uint16_t P[4] = {PAR_DLY_WOW, PAR_DSAT, PAR_GLITCH, PAR_DPITCH};
    static const uint8_t G0[4] = {88, 50, 84, DP_OFF};   /* (GRAIN's starting cloud; the tape's: all 0) */
    static uint8_t aside[2][4];
    static uint32_t kept[2] = {~0u, ~0u};            /* (the kit load each side was kept in) */
    uint32_t i, any = 0, f = from == DELAY_GRAIN, g = to == DELAY_GRAIN;
    if (f == g)
        return;
    for (i = 0; i < 4u; i++) {
        uint32_t v = kept[g] == kit_loads ? aside[g][i] : g ? G0[i] : 0u;
        aside[f][i] = (uint8_t)vp_get(P[i]);
        morph_follow(P[i], (int32_t)v - (int32_t)vp_get(P[i]));
        vp_set(P[i], v);
    }
    kept[f] = kit_loads;
    if (!g)
        return;
    if (!vp_get(PAR_DLY_LEVEL))
        vp_set(PAR_DLY_LEVEL, 100);
    for (i = 0; i < NVOICE; i++)
        any |= par[PAR_DSEND1 + i] | (par[PAR_DLY_ROUTE] > 64u ? par[PAR_FX_SEND1 + i] : 0u);
    if (!any)
        ui_message("GRAIN: TURN UP A DELAY SEND");
}
static void morph_follow(uint32_t p, int32_t d)
{
    if (d && morph_amt && kset.medit == 2u && p < NPAR && par_sound(p) && !morph_excluded(p)) {
        uint8_t *m = &morph_par[kit_idx(p)];
        *m = (uint8_t)clamp32((int32_t)*m + d, (int32_t)vp_min(p), (int32_t)vp_max(p));
    }
}
/* a knob: n detents on parameter p (accelerated for wide ranges) */
/* 0.16: with SYNC on, the delay's TIME a note value a detent (the value's middle, as card_value reads it) */
static int sync_knob(uint32_t p) { return p == PAR_DLY_TIME && fx.d_sync; }
static int32_t sync_detent(uint32_t v, int32_t n)
{
    int32_t i = (int32_t)(((v * 65536u / 127u) * NSYNCV) >> 16) + (n > 0 ? 1 : -1);
    i = i < 0 ? 0 : i >= (int32_t)NSYNCV ? (int32_t)NSYNCV - 1 : i;
    return (2 * i + 1) * 127 / 24;
}
static void knob_turn(uint32_t k, int32_t n)
{
    uint16_t pp[4];
    uint32_t p, mn, mx;
    int32_t v, step = n;
    page_params(pp);
    p = pp[k];
    if (p == VP_NONE || !n || (ui.mode == M_SOUND && par_unheard(ui.track, p)) || vp_dead(p))   /* (0.19: a
                                                      * model's: locked; 40: not yet in use) */
        return;
    mn = vp_min(p);
    mx = vp_max(p);
    if (ui.shift) {                                  /* 1.0: OCT- held: fine, a step a detent (no acceleration, RUSH's
                                                      * speed too); the page stays shown, OCT-'s release does nothing */
        ui.fine = 1;
        ui.shift_used = 1;
        ui.force = 1;
    } else if (kset.accel && mx - mn > 32u) {
        uint32_t dt = fm1_ms - ui.knob_ms[k];
        if (dt < 18u)
            step = n * 4;
        else if (dt < 40u)
            step = n * 2;
    }
    if (p == VP_STRETCH && !ui.fine) {               /* 1.0: TIME STRETCH a whole 1% a detent (OCT- held: 0.01%) */
        uint32_t c = vp_get(p);
        v = n > 0 ? (int32_t)(c / 100u) + step : (int32_t)((c + 99u) / 100u) + step;
        vp_set(p, (uint32_t)clamp32(v * 100, (int32_t)mn, (int32_t)mx));
        ui.knob_ms[k] = fm1_ms;
        ui.hot = (uint8_t)(k + 1u);
        ui.hot_ms = fm1_ms;
        return;
    }
    if (p == VP_RUSH && !ui.fine) {                  /* 0.23-t7: RUSH by the turn's speed: 1% a detent slow, up to 10% fast */
        uint32_t dt = fm1_ms - ui.knob_ms[k], sp = (uint32_t)(n < 0 ? -n : n) * 1000u / (dt + 1u);
        step = n * (sp >= 80u ? 10 : sp >= 40u ? 5 : sp >= 20u ? 2 : 1);
    }
    ui.knob_ms[k] = fm1_ms;
    ui.hot = (uint8_t)(k + 1u);
    ui.hot_ms = fm1_ms;
    if ((ui.copy || ca_rm) && ui.mode == M_SOUND && ui.held_step < 0 && !ui.sb_open && !ui.kb_open &&
        !ca_isout(ui.track)) {
        control_all(k, step);                        /* 0.11: OCT+ held: every voice (Phase 17: or by MIDI) */
        return;
    }
    if (ui.copy && p == PAR_TRACK_LENGTH && ui.held_step < 0 && !ui.kb_open && !ui.ca) {
        uint32_t t;                                  /* 1.0: OCT+ held, the length (TRACK, the grid): every track */
        const pattern_t *pt = shown_pat();           /* .. to the shown one's (let go: OCT+ reverts, OCT- keeps) */
        for (t = 0; t < NTRACK; t++)
            ui.ca_lens[t] = pt->len[t];
        ui.ca = ui.ca_len = 1;
        ui.copy_used = 1;
        ui.copy_src = 0;
        ui_message("CONTROL ALL: EVERY TRACK'S LENGTH");
    }
    if (ui.held_step >= 0 && p == VP_RUSH) {         /* 39: a step held on RUSH's page: its RUSH lock (0: gone) */
        pattern_t *pt = shown_pat();
        uint32_t hs = (uint32_t)ui.held_step, c;
        v = clamp32((int32_t)rush_lock(pt, ui.track, hs) + step, 0, (int32_t)RUSH_MAX);
        if (v)
            rush_lock_set(pt, ui.track, hs, (uint32_t)v);
        else
            for (c = 0; c < 2u; c++)
                if (lock_get(pt, ui.track, hs, c) == RUSH_CODE)
                    lock_set(pt, ui.track, hs, c, NO_AUTOMATION);
        live_step_sync(pt, ui.track, hs);
        ui.lock_turned = 1;
        return;
    }
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
        v = sync_knob(p) ? sync_detent(*dv, n) : is_lfo_sync(p) ? (int32_t)order_turn(SYNC_ORDER, NSYNC, *dv, n)
                         : clamp32((int32_t)*dv + step, (int32_t)mn, (int32_t)(mx > 127u ? 127u : mx));
        *dv = (uint8_t)model_step(p, *dv, (uint32_t)v);
        main_set(pt, ui.track, (uint32_t)ui.held_step, 1);
        live_step_sync(pt, ui.track, (uint32_t)ui.held_step);
        ui.lock_turned = 1;
        return;
    }
    v = sync_knob(p) ? sync_detent(vp_get(p), n) : is_lfo_sync(p) ? (int32_t)order_turn(SYNC_ORDER, NSYNC, vp_get(p), n)
                     : clamp32((int32_t)vp_get(p) + step, (int32_t)mn, (int32_t)mx);
    if (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) {   /* (1.0: LFO 1's, through every voice's) */
        lfo1_dest_turn(p - PAR_TARGET_LFO1, step);
        return;
    }
    if (is_target(p))                                /* (25a: past the voice's LXR targets, Elixir's; 0.25.2: by */
        v = (int32_t)target_turn(p, vp_get(p), step);   /* .. group in EDIT's order) */
    if (p >= VP_GL && p <= VP_GL_LAST && (p - VP_GL) % NGLSET == 2u)   /* (1.0: SYNC: OFF, then slow .. fast) */
        v = (int32_t)order_turn(GL_ORDER, NGLEN + 1u, vp_get(p), n);
    if (is_gl_dest(p)) {                             /* (0.25.2: the same; its name shown over PERF's page) */
        v = (int32_t)gd_turn(vp_get(p), n);
        ui.dest_ms = fm1_ms;
        ui.dest_p = (uint16_t)p;
    }
    if (p == PAR_FX_DELAY_TYPE)                      /* (1.0: OFF, then the modes) */
        v = (int32_t)order_turn(DLY_ORDER, NDLY_ORDER, vp_get(p), n);
    if (p == VP_GMODE) {                             /* (1.0: the generator's name and what it does, a while) */
        ui.dest_ms = fm1_ms;
        ui.dest_p = (uint16_t)p;
    }
    v = (int32_t)model_step(p, vp_get(p), (uint32_t)v);
    if (p == PAR_FX_DELAY_TYPE)
        dly_cross(vp_get(p), (uint32_t)v);
    morph_follow(p, (int32_t)v - (int32_t)vp_get(p));
    vp_set(p, (uint32_t)v);
    if (ui.mode == M_STEP)                           /* (a generator's track: what plays takes the step's edit) */
        live_step_sync(shown_pat(), ui.track, ui.step & 63u);
    if (sq.rec && par_lockable(p))                   /* knob movements record to the playing step (REC) */
        seq_record_automation(ui.track, p, (uint32_t)v);
}

/* ------------------------------------------------------------ modes --- */
static void pmem_save(void)                          /* 0.13: where the buttons were, as this page is left */
{
    if (ui.mode == M_SOUND) {
        uint32_t g = sect_group(ui.sect);
        ui.gsect[g] = (uint8_t)(ui.sect + 1u);
        ui.ghalf[g] = ui.half;
    } else {
        ui.mpage[ui.mode] = ui.page;
    }
    if (ui.mode == M_STEP || ui.mode == M_EUCLID)
        ui.seq_m = ui.mode;
    if (ui.mode == M_PERF)
        ui.pset[perf_set()] = ui.page;
    if (ui.mode == M_MIXER || ui.mode == M_CONFIG)
        ui.glo_m = ui.mode;
}
static void mode_set(uint32_t m)
{
    if (ui.mode != m) {
        pmem_save();
        ui.page = kset.pmem ? ui.mpage[m] : 0u;      /* (0.13: the page it was on, with CONFIG BUTTONS) */
        ui.io_naming = 0;
        ui.sb_open = 0;                              /* (the sound browser: what it loaded stays) */
        ui.pdel = 0;                                 /* (1.0: the pattern page's delete let go) */
    }
    if ((m == M_HOME || m == M_STEP || m == M_EUCLID || m == M_PATTERN || m == M_SONG || m == M_KEYS) && ui.perf) {
        uint32_t t;
        ui.perf = 0;                                 /* (PERF left: what it latched lets go) */
        ui.pf_latch = 0;
        for (t = 0; t < NTRACK; t++)
            if (ui.roll_latch & (1u << t))
                seq_set_roll(t, 0);
        ui.roll_latch = 0;
        ui.drill_latch = 0;
        perf_apply();
    }
    if (m == M_PERF)
        ui.perf = 1;
    if (m == M_KEYS) {                               /* 0.8: KEYS: the keys play notes (PERF's let go: above) */
        ui.kplay = 1;                                /* (31: on every page after, until SHIFT + EDIT: keys_live) */
        ui.held_step = -1;
        sq.armed_step = sq.armed_track = -1;
    }
    if (m == M_CONFIG && ui.mode != M_CONFIG) {
        uint32_t i;
        audio_max_us = 0;                            /* (the CPU meter's peaks: from now) */
        for (i = 0; i < NLD; i++)
            ld_pk[i] = 0;
#if defined(LOAD_CLOCK) && ELIXIR_LDDIAG
        for (i = 0; i < NDG; i++)
            dg_pk[i] = 0;
#endif
    }
    if (m != ui.mode) {
        ui.action = 0;
        ui.eu_undo = 0;
    }
    ui.mode = (uint8_t)m;
    if (ui.page >= mode_pages())
        ui.page = 0;
    ui.clear_open = 0;
    ui.force = 1;
    bank_fix();
}
/* 29: the next section of s's button in page order (after its last: its first) */
static uint32_t sect_next(uint32_t s)
{
    uint32_t r = sect_rank(s), i;
    for (i = 1; i <= NSORD; i++) {
        uint32_t n = SECT_ORDER[(r + i) % NSORD];
        if (sect_group(n) == sect_group(s))
            return n;
    }
    return s;
}
static void sound_open(uint32_t s, int again)        /* a section button; again: its next section */
{
    if (ui.mode == M_SOUND && again) {
        if (sect_group(ui.sect) == s) {
            uint32_t i;
            ui.half = 0;                             /* (1.0: the next section only, never its second page: SELECT
                                                      * goes through the pages; LFO 1's MODE page is LFO 1's) */
            for (i = 0; i < NSORD; i++) {            /* (none on this track: its next one) */
                ui.sect = (uint8_t)sect_next(ui.sect);
                if (ui.sect == S_LFOM)
                    continue;
                if (sect_half_used(ui.track, ui.sect, 0) || sect_half_used(ui.track, ui.sect, 1))
                    break;
            }
            sound_fix();
            ui.force = 1;
            return;
        }
    }
    if (ui.mode == M_SOUND)                          /* (another button's sections: where this one was) */
        pmem_save();
    mode_set(M_SOUND);
    ui.sect = (uint8_t)s;
    ui.half = 0;
    if (kset.pmem && ui.gsect[s]) {                  /* 0.13: the section and half it showed last */
        ui.sect = (uint8_t)(ui.gsect[s] - 1u);
        ui.half = ui.ghalf[s];
    }
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
         : type == IO_SONG ? sq.song : type == IO_FACTORY || type == IO_FMORPH ? 0u : (pj.loaded < PROJ_USER ? pj.loaded : 0u);
}
static void io_open(uint32_t m, uint32_t type)
{
    if (m == M_SAVE && type > IO_PROJECT)
        type = IO_KIT;                                /* (FACTORY: nothing to save to) */
    if (m == M_LOAD && (type == IO_PATTERN || type == IO_SONG))
        type = IO_KIT;                                /* (1.0: loaded on their own pages) */
    mode_set(m);
    ui.io_type = (uint8_t)type;
    ui.io_slot = (uint8_t)io_slot_default(type);
    ui.io_naming = 0;
    if (type != IO_PROJECT)
        ui.bank = (uint8_t)(ui.io_slot / 16u);
}
static void solo_key(void)                           /* 0.10: the track soloed (again: not): SHIFT GLO, the menu's 12 */
{
    sq.solo ^= (uint8_t)(1u << ui.track);
    ui_message(sq.solo & (1u << ui.track) ? "SOLO ON" : sq.solo ? "SOLO OFF (OTHERS STILL SOLO)" : "SOLO OFF");
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
                           : rc == -20 ? "LIBRARY FULL" : rc == ST_LOCKED ? fx_msg(" DATA: NOT SAVED") : "SAVE FAILED");
                break;
            }
            kit_save_ram(s, ui.io_type == IO_MORPH);
            memcpy(kit_names[s], ui.io_name, 8);
            ui_message("KIT SAVED");
            break;
        case IO_PATTERN:                              /* preset_savePatternRAM */
            if (pat_voices(&pat[sq.active]) == PV_FREE)   /* (37: KIT keeps its slot; bits 6-7 its voices) */
                pat_set_voices(&pat[sq.active], PV_FREE, kit_loaded);
            pat[sq.active].shuffle = par[PAR_SHUFFLE];
            if (s == sq.active) {
                pat_backup = pat[s];
            } else {
                pv_skip = pat_voices(&pat[sq.active]) == PV_KIT;   /* (no kit reload when it switches) */
                pv_copy(sq.active, s);
                seq_set_next_pattern(s);
            }
            ui_message("PATTERN SAVED");
            break;
        case IO_SONG:
            if (s != sq.song) {
                memcpy(songs[s], songs[sq.song], MAX_SONG_LENGTH);
                song_rd[s % NUM_SONGS] = song_rd[sq.song % NUM_SONGS];   /* (42: its ROADS too) */
            }
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
                put_str(b, rc == -20 ? "PROJECT TOO BIG" : rc == ST_LOCKED ? fx_msg(" DATA: NOT SAVED") : "SAVE FAILED",
                        sizeof b);
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
        pv_loaded(s);
        ui_message("KIT LOADED");
        break;
    case IO_SOUND: {
        int rc = libsound_load(s, track_voice(ui.track));
        if (rc == 0)
            ui.lib_sound = (uint8_t)s;
        ui_message(rc == 0 ? "SOUND LOADED" : rc == -2 ? "NOT THIS TRACK'S KIND" : "EMPTY SLOT");
        break;
    }
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
    case IO_MORPH:
        if (ui.io_src) {
            ui.lib_kit = (uint8_t)s;
            ui_message(libkit_load(s, 1) ? "EMPTY SLOT" : "MORPH TARGET LOADED");
            break;
        }
        kit_load_morph(s);
        ui_message("MORPH TARGET LOADED");
        break;
    case IO_FMORPH:                                   /* (a factory kit as the morph target, as KIT From FACTORY) */
        kit_factory_morph(s);
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
enum { CL_TRACK, CL_PATTERN, CL_AUTO1, CL_AUTO2, CL_SOUND, CL_KIT, CL_SONG_ROW, NCLEAR };
static const char *const CLEAR_NAME[NCLEAR] = {"CLEAR TRACK", "CLEAR PATTERN", "CLEAR AUTOMATION 1",
                                               "CLEAR AUTOMATION 2", "CLEAR SOUND (INIT)", "CLEAR KIT (INIT)",
                                               "DELETE SONG ROW"};
/* 0.10: CLEAR SOUND: the track's voice as the init kit has it (the rest of the kit as it is) */
static void sound_init(uint32_t v, int saved)        /* (1.0: saved: as the kit loaded has it, RELOAD's one voice) */
{
    uint8_t k[KIT_LEN], s[SOUND_MAX];
    if (!saved)
        kit_init_values(k);
    else if (pat_voices(&pat[sq.active]) == PV_LOCKED)
        kit_get_row(PV_SLOT(sq.active), k);          /* (37: a LOCKED pattern's: as last kept) */
    else
        kit_get(kit_loaded, k);
    voice_put_par(v, s, voice_get(k, v, s), v);
}
static void clear_do(void)
{
    pattern_t *pt = shown_pat();
    switch (ui.clear_sel) {
    case CL_TRACK: seq_clear_track(ui.track, pt); ui_message("TRACK CLEARED"); break;
    case CL_PATTERN: seq_clear_pattern(pt); ui_message("PATTERN CLEARED"); break;
    case CL_AUTO1: seq_clear_automation(ui.track, pt, 0); ui_message("AUTOMATION 1 CLEARED"); break;
    case CL_AUTO2: seq_clear_automation(ui.track, pt, 1); ui_message("AUTOMATION 2 CLEARED"); break;
    case CL_SOUND: sound_init(track_voice(ui.track), 0); ui_message("SOUND CLEARED (INIT)"); break;
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
    ui.action = ACT_NONE;                            /* (0.10: another track's generator, its own actions) */
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
    key_note_post(ui.track, tb_note(ui.track), key_vel, 0);
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

/* 31: DENSITY's COMMIT: what it does written into the pattern, DENSITY back to 0 (seq.c seq_dens_commit); with
 * DENSITY at 0 just after one, OCT+ undoes it (the pattern and DENSITY as they were) */
static int8_t dc_dens;                               /* (DENSITY before the COMMIT, swapped by UNDO) */
static void dens_commit_key(void)
{
    if (pfs.dens) {
        uint32_t n = seq_dens_commit();
        dc_dens = pfs.dens;
        pfs.dens = 0;
        sq.dens = 0;                                 /* (now: not the pattern and DENSITY both till the next block) */
        ui_message(n ? "DENSITY COMMITTED (OCT+: UNDO)" : "DENSITY: NOTHING TO COMMIT");
    } else if (seq_dens_undo()) {
        int8_t d = pfs.dens;
        pfs.dens = dc_dens;
        dc_dens = d;
        ui_message("DENSITY COMMIT UNDONE");
    } else {
        ui_message("DENSITY AT 0: NOTHING TO COMMIT");
    }
}
static void wd_side_cast(void);                      /* (40: wander.c: a spell cast while wandering, reversed) */
static void wd_side_end(void);
#include "spells.c"                                  /* (32: SPELLS) */
#include "wander.c"                                  /* (40: WANDER) */
/* ------------------------------------------------------------- keys --- */
/* 31: KEYS (SHIFT + EDIT) stays on as the pages change, so the knobs can be turned while playing; the keys play notes
 * on every page but those whose keys are the page's own (patterns, song, save and load, step edit, PERF) */
static int keys_live(void)
{
    return ui.kplay && ui.mode != M_PATTERN && ui.mode != M_SONG && ui.mode != M_SAVE && ui.mode != M_LOAD &&
           ui.mode != M_STEP && ui.mode != M_PERF;
}
/* 1.0: SHIFT (OCT-) held: the menu (SHIFT + a white key), on every page but PERF's (its own: latches, SPELLS',
 * WANDER's) and KEYS (an accent); a knob turned meanwhile hides it (fine adjust: the page's values) */
static int shift_menu(void) { return ui.shift && ui.mode != M_PERF && ui.mode != M_PATTERN && !keys_live(); }   /* (1.0:
                                                      * the pattern page: SHIFT + a pattern deletes it) */
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
        perf_apply();
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
        if (down && e == PF_FREEZE && (!par[PAR_DLY_LEVEL] || par[PAR_FX_DELAY_TYPE] == DELAY_OFF))
            ui_message(par[PAR_FX_DELAY_TYPE] == DELAY_OFF ? "FREEZE: THE DELAY IS OFF" : "FREEZE: THE DELAY'S LEVEL IS 0");
        perf_apply();
    }
    ui.force = 1;
}

/* 19a: KEYS in the scale (the track follows one): white key w plays the w-th degree up from the root at or below the
 * keys' first note, as the voice sounds it (two octaves and more, nothing out of key) */
static int32_t key_scale_note(uint32_t w)
{
    const pattern_t *p = &pat[sq.active];
    uint32_t m = SCALE_MASK[p->scale < NSCALE ? p->scale : SC_CHROM], r = p->sroot % 12u;
    int32_t c = (int32_t)(vc[track_voice(ui.track)].osc.midi_freq >> 8);
    int32_t n = c + 53 + 12 * ((int32_t)ui.koct - 3) - SEQ_DEFAULT_NOTE;   /* (the first key's sounding semitone) */
    n -= ((n - (int32_t)r) % 12 + 12) % 12;
    while (w) {
        n++;
        if ((m >> (uint32_t)(((n - (int32_t)r) % 12 + 12) % 12)) & 1u)
            w--;
    }
    return n - c + SEQ_DEFAULT_NOTE;
}
/* 0.8: KEYS: key k (semitones above F3) plays the track's voice at its note, the octave KEYS' knob sets; SHIFT: the
 * accent. As a MIDI note: recorded into the steps under REC (midi.c key_service). 19: a white key (w, -1 a black
 * one) in the track's scale; a GATE voice held until the key is let go */
static void key_play(uint32_t k, int32_t w)
{
    int32_t n = w >= 0 && scale_on(ui.track) ? key_scale_note((uint32_t)w) : 53 + (int32_t)k + 12 * ((int32_t)ui.koct - 3);
    if (ui.shift)
        ui.shift_used = 1;
    ui.knote = (uint8_t)clamp32(n, 0, 127);
    ui.kkey = (uint8_t)k;
    key_note_post(ui.track, ui.knote, ui.shift ? key_acc : key_vel, KF_REC | KF_HOLD);
    ui.force = 1;
}

static void white_down(uint32_t w)
{
    uint32_t s = ui.bar * 16u + w;
    pattern_t *pt = shown_pat();
    if (keys_live()) {
        key_play(WHITE_KEY[w], (int32_t)w);
        return;
    }
    if (ui.mode == M_PATTERN && ui.shift && ui.pdel != 2u)   /* 1.0: the pattern page: SHIFT (OCT-) + a pattern */
        ui.pdel = 1;
    if (ui.pdel) {                                   /* 1.0: CLEAR or SHIFT on the pattern page: this one, asked */
        uint32_t n = ui.bank * 16u + w;
        if (ui.shift)
            ui.shift_used = 1;
        if (ui.mode != M_PATTERN) {
            ui.pdel = 0;
        } else {
            if (ui.pdel == 1u && n < NUM_PATTERN) {
                ui.pdel = 2;
                ui.pdel_n = (uint8_t)n;
                ui.msg[0] = 0;                       /* (the dialog says it now) */
                ui.force = 1;
            }
            return;
        }
    }
    if (perf_spells()) {                             /* 32: SPELLS: what the spell may change (SHIFT: only that) */
        if (ui.shift)
            ui.shift_used = 1;
        sp_white(w, ui.shift);
        return;
    }
    if (perf_wander()) {                             /* 40: WANDER: the parts that travel (SHIFT: only that) */
        if (ui.shift)
            ui.shift_used = 1;
        wd_white(w, ui.shift);
        return;
    }
    if (perf_trp()) {                                /* 0.23-t7: TRANSPOSE: the middle key 0, -7 .. 8 degrees about it */
        trp_set((int32_t)w - 7);
        ui.trkey = (uint8_t)(w + 1u);
        return;
    }
    if (ui.mode == M_PERF && ui.shift) {              /* PERF: SHIFT latches (1.0: on PERF's pages only) */
        perf_key(w, 1);
        return;
    }
    if (ui.shift) {                                   /* the menu (the LXR's SHIFT + step functions; 1.0: 8 .. 13) */
        ui.shift_used = 1;
        switch (w) {
        case 7: io_open(M_LOAD, IO_KIT); return;
        case 8: io_open(M_LOAD, IO_MORPH); return;
        case 9:                                       /* 1.0: RESET SOUND: the track's voice as the kit has it */
            sound_init(track_voice(ui.track), 1);
            ui_message("SOUND RESET (AS SAVED)");
            return;
        case 10: perf_reset(); return;                /* (SHIFT HOME's) */
        case 11: solo_key(); return;                  /* (SHIFT GLO's) */
        case 12:                                      /* RELOAD: the kit and the pattern as saved */
            if (pat_voices(&pat[sq.active]) == PV_LOCKED)
                kit_unstash(PV_SLOT(sq.active));     /* (37: a LOCKED pattern's: as last kept) */
            else
                kit_load_ram(kit_loaded);
            sq.pending = sq.active;
            sq.load_pending = 1;
            pat_revert = 1;
            if (!sq.running)
                seq_pattern_activate();
            ui_message("RELOADED");
            return;
        default:                                      /* 1..7: play the voice (at the keys' velocity); 14..16 free */
            if (w < NTRACK)
                key_note_post(w, tb_note(w), key_vel, 0);
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
                pv_copy(ui.copy_src - 1u, n);       /* (37: a LOCKED one's voices too) */
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
        song_put(sq.song, ui.song_row, ui.bank * 16u + w, song_rep_i(sq.song, ui.song_row));
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
    if (perf_rush()) {                               /* 39: RUSH's page: a step held, the knob its RUSH lock */
        ui.held_step = (int8_t)s;
        ui.lock_turned = 0;
        ui.force = 1;
        return;
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
    if (keys_live() || sp_white_up(w) || wd_white_up(w))
        return;
    if (ui.trkey == w + 1u) {                        /* (0.23-t7: TRANSPOSE's key let go: HOLD back to 0) */
        ui.trkey = 0;
        if (sq.trhold && perf_trp())
            trp_set(0);
    }
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
    if (keys_live()) {
        key_play(BLACK_KEY[k], -1);
        return;
    }
    if (k < NTRACK) {
        uint32_t t = k;
        if (ca_keys()) {                             /* 1.0 Phase 17: Control All on: the track out / in */
            ca_out_key(t);
            return;
        }
        if (ui.copy && ui.mode != M_PATTERN && !ui.ca) {   /* COPY + a track, then the target */
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
            if (ui.mode == M_PERF) {                  /* PERF + SHIFT: unmute the tracks up to this one */
                sq.muted &= (uint8_t)~((2u << t) - 1u);
            } else {
                sq.muted ^= (uint8_t)(1u << t);
            }
            ui.force = 1;
            return;
        }
        if (perf_dens()) {                           /* 19d: DENSITY's page: the track left out (again: in) */
            pfs.dex ^= (uint8_t)(1u << t);
            ui.force = 1;
            return;
        }
        if (perf_spells()) {                         /* 32: SPELLS: the track a target (again: not) */
            sp_black(t);
            return;
        }
        if (perf_wander()) {                         /* 40: WANDER: the track travels (again: stays home) */
            wd_black(t);
            return;
        }
        if (perf_trp()) {                            /* 0.23-t7: TRANSPOSE: the track one the keys move (again: not) */
            sq.trmask ^= (uint8_t)(1u << t);
            if ((sq.trmask >> t) & 1u)
                track_select(t);
            ui.force = 1;
            return;
        }
        if (perf_rush()) {                           /* 39: RUSH's page: the track it rushes (the knob up: at once) */
            sq.ru_t = (uint8_t)t;
            track_select(t);
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
    if (perf_spells()) {                             /* 32: SPELLS: ALL / NONE, KEEP, SAVE, DEFAULTS */
        sp_bar(k);
        return;
    }
    if (perf_wander()) {                             /* 40: WANDER: ALL / NONE, KEEP, REST, DEFAULTS */
        wd_bar_key(k);
        return;
    }
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
    if (ui.copy && !ui.ca) {                          /* COPY + bar: the shown bar into it (the grid: every track) */
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
    p->sfollow = (uint8_t)((p->sfollow & ~(1u << dst)) | ((p->sfollow >> src) & 1u) << dst);
    memcpy(p->mel[dst], p->mel[src], NML);
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
    if (ui.dx_ask) {                                 /* 0.24.1: another app's data: SHIFT (OCT-) keeps, COPY (OCT+) twice
                                                      * erases it */
        if (b == B_OCTDN && ui.dx_ask < 3u) {
            ui.dx_ask = 0;
            ui.shift_used = 1;
            ui_message("KEPT: ELIXIR SAVES NOTHING");
            ui.msg_long = 1;
        } else if (b == B_OCTUP && ui.dx_ask < 3u) {
            ui.dx_ask++;
            ui.dx_ms = fm1_ms;
            ui.copy_used = 1;
        }
        ui.force = 1;
        if (b != B_PLAY && b != B_REC)               /* (the transport still works; the rest waits) */
            return;
    }
    if (ui.pdel == 2u) {                             /* 1.0: DELETE PATTERN?: COPY (OCT+) deletes, SHIFT (OCT-) keeps */
        if (b == B_OCTUP || b == B_OCTDN) {
            if (b == B_OCTUP) {
                char m[24];
                pat_delete(ui.pdel_n);
                m[0] = 'P';
                m[1] = (char)('0' + (ui.pdel_n + 1u) / 10u);
                m[2] = (char)('0' + (ui.pdel_n + 1u) % 10u);
                put_str(m + 3, " DELETED", sizeof m - 3u);
                ui_message(m);
                ui.copy_used = 1;
            } else {
                ui_message("PATTERN KEPT");
                ui.shift_used = 1;
            }
            ui.pdel = 0;
        }
        ui.force = 1;
        if (b != B_PLAY && b != B_REC)               /* (the transport still works; the rest waits) */
            return;
    } else if (ui.pdel && b != B_OCTDN) {            /* (armed: any other button lets it go) */
        ui.pdel = 0;
        ui.force = 1;
        if (b == B_OCTUP && !ui.shift) {
            ui_message("DELETE: CANCELLED");
            ui.copy_used = 1;
            return;
        }
    }
    if (ui.ca_ask) {                                 /* 0.11: Control All's dialog: OCT+ reverts, OCT- keeps */
        if (b == B_OCTUP) {
            control_all_undo();
        } else if (b == B_OCTDN) {
            ui_message(ui.ca_len ? "LENGTHS KEPT" : "CONTROL ALL KEPT");
            ui.ca_ask = ui.ca_len = 0;
            ui.shift_used = 1;                       /* (its release: nothing) */
        }
        ui.force = 1;
        if (b != B_PLAY && b != B_REC)               /* (the transport still works; the rest waits) */
            return;
    }
    switch (b) {
    case B_OCTDN:
        if ((ui.copy || ca_rm) && ui.ca) {           /* 0.11: Control All's undo, not SHIFT (Phase 17: by MIDI too) */
            control_all_undo();
            ui.shift_used = 1;
            return;
        }
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
            if (ui.mode == M_PATTERN) {               /* 1.0: the pattern page: CLEAR, then the pattern to delete */
                ui.pdel = 1;
                ui_message("DELETE: PRESS A PATTERN");
                ui.msg_long = 1;
                ui.force = 1;
                return;
            }
            if (!ui.clear_open) {
                ui.clear_open = 1;
                ui.clear_sel = ui.mode == M_SONG ? CL_SONG_ROW   /* (0.10: on a sound page, its sound) */
                             : ui.mode == M_SOUND || ui.mode == M_KEYS ? CL_SOUND : CL_TRACK;
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
            ui.kb_open = 0;
            ui.kb_at = (uint16_t)(ui.kb_sel + 1u);
            if (ui.kb_sel >= KB_FAC) {                /* 0.19: a factory kit: the edit buffer, as LOAD > FACTORY */
                kit_factory_load(ui.kb_sel - KB_FAC);
                ui_message("FACTORY KIT LOADED");
                return;
            }
            if (ui.kb_sel >= NUM_KITS) {              /* a library kit: the edit buffer, as LOAD > KIT From LIBRARY */
                ui.lib_kit = (uint8_t)(ui.kb_sel - NUM_KITS);
                ui_message(libkit_load(ui.lib_kit, 0) ? "EMPTY SLOT" : "LIBRARY KIT LOADED");
                return;
            }
            kit_load_ram(ui.kb_sel);
            pv_loaded(ui.kb_sel);
            ui_message("KIT LOADED");
            return;
        }
        if (ui.mode == M_SAVE || ui.mode == M_LOAD) {
            io_ok();
            ui.force = 1;
            return;
        }

        if (ui.mode == M_EUCLID && ui.page % NEUCLID == PG_ACTION && ui.action) {   /* OCT+ does the ACTION */
            static const char *const M[NACT] = {"", "DICED", "COMMITTED", "UNDONE", "RESET"};
            static const char *const NO[NACT] = {"", "", "NO GENERATOR ON", "NOTHING TO UNDO", ""};
            int ok = 1;
            switch (ui.action) {
            case ACT_DICE: seq_dice(ui.track); break;
            case ACT_COMMIT:
                ok = gen_live(shown_pat(), ui.track) || mel_on(shown_pat(), ui.track);
                seq_commit(ui.track);
                break;
            case ACT_UNDO: ok = seq_undo(ui.track); break;
            default:                                 /* (RESET; 20: a melody new from its start) */
                memset(sq.mfull[ui.track], 0, sizeof sq.mfull[ui.track]);
                sq.mmok &= (uint8_t)~(1u << ui.track);
                sq.gsync |= (uint8_t)(1u << ui.track);
                break;
            }
            ui_message(ok ? M[ui.action] : NO[ui.action]);
            ui.eu_undo = 0;
            ui.action = 0;
            return;
        }
        if (perf_dens()) {                           /* 31: OCT+ on DENSITY's page: COMMIT (DENSITY at 0: UNDO) */
            dens_commit_key();
            return;
        }
        if (perf_stut()) {                           /* 0.16: OCT+ held on STUTTER's page: it stutters */
            pf.st_hold = 1;
            ui.force = 1;
            return;
        }
        if (perf_spells()) {                         /* 32: OCT+ on SPELLS: cast (cast again: from before it) */
            sp_cast_key();
            return;
        }
        if (perf_wander()) {                         /* 40: OCT+ on WANDER: set out (a new course; resting: on) */
            wd_copy_key();
            return;
        }
        if (glfo_page() >= 0) {                      /* 0.13: OCT+ on a global LFO's page: it starts again */
            uint32_t n = (uint32_t)glfo_page();
            static const char *const TR[NGLFO] = {"LFO 1 TRIGGERED", "LFO 2 TRIGGERED", "LFO 3 TRIGGERED",
                                                  "LFO 4 TRIGGERED"};
            glfo_ev |= (uint8_t)(GLE_KEY << n);
            ui_message(gls[n].wave == GW_FOLLOW ? "FOLLOW: NO TRIGGER" : gls[n].mode == GM_FREE ?
                       "FREE: NO TRIGGER (SEE MODE)" : TR[n]);
            return;
        }
        if (ui.mode == M_SONG) {                      /* COPY in the song: insert a row (a copy of the one above); */
            if (ui.page % NSONGPG == 1u && ui.song_edit == SE_DELETE) {   /* (37: ROWS: its action) */
                song_delete_row(sq.song, ui.song_row);
                ui_message("ROW DELETED");
                return;
            }
            song_insert_row(sq.song, ui.song_row);
            ui_message("ROW INSERTED: PICK ITS PATTERN");
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
        if (sq.running)                               /* (37: the pattern or the song, as Play says) */
            seq_stop_now();
        else
            seq_start(1);
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
        if (ui.shift) {                               /* 31: SHIFT HOME: PERF RESET */
            ui.shift_used = 1;
            perf_reset();
            return;
        }
        if (ui.mode == M_PATTERN)                    /* (1.0: LOAD third, before SONG) */
            io_open(M_LOAD, ui.io_type);
        else
            mode_set(ui.mode == M_HOME ? M_PATTERN : ui.mode == M_LOAD ? M_SONG : M_HOME);
        return;
    case B_SEQ:
        if (ui.shift) {                               /* 0.6: SHIFT SEQ: the LINKS page (29: in PERF's GLOBAL MOD) */
            ui.shift_used = 1;
            mode_set(M_PERF);
            ui.page = PG_LINKS;
            return;
        }
        /* 29: the track's pages (TRACK, GENERATOR ..), then its steps; SELECT pages in each */
        if (ui.mode == M_EUCLID || (ui.mode != M_STEP && kset.pmem && ui.seq_m == M_STEP)) {
            mode_set(M_STEP);
            if (ui.step / 16u != ui.bar)
                ui.step = (uint8_t)(ui.bar * 16u);
        } else {
            mode_set(M_EUCLID);
        }
        return;
    case B_ARP:
        if (ui.shift) {                               /* (the track's pages, as before 0.19: SEQ has them) */
            ui.shift_used = 1;
            mode_set(M_EUCLID);
            return;
        }
        if (ui.mode == M_PERF) {                      /* 29: PERFORM, 32: SPELLS, 40: WANDER, GLOBAL MOD (and back) */
            uint32_t o = (perf_set() + 1u) % NPSET;
            ui.pset[perf_set()] = ui.page;
            ui.page = PERF_SET0[o];
            if (kset.pmem && ui.pset[o] >= PERF_SET0[o] && ui.pset[o] < PERF_SET0[o + 1u])
                ui.page = ui.pset[o];
            ui.force = 1;
        } else {
            mode_set(M_PERF);
        }
        return;
    case B_FX:
        mode_set(M_FX);
        return;
    case B_EDIT:
        if (ui.shift) {                               /* 0.8: SHIFT EDIT: KEYS (again: the grid); 31: KEYS on */
            ui.shift_used = 1;                        /* .. another page: off, the page stays */
            if (ui.kplay && ui.mode != M_KEYS) {
                ui.kplay = 0;
                ui_message("KEYS OFF");
                ui.force = 1;
            } else if (ui.mode == M_KEYS) {
                ui.kplay = 0;
                mode_set(M_HOME);
            } else {
                mode_set(M_KEYS);
            }
            return;
        }
        sound_open(S_MODEL, 1);
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
        if (ui.shift) {                               /* 0.10: SHIFT GLO: the track soloed (again: not) */
            ui.shift_used = 1;
            solo_key();
            return;
        }
        if (ui.mode == M_MIXER)                      /* (29: the mixer, then CONFIG; the track's MIX pages gone) */
            mode_set(M_CONFIG);
        else if (kset.pmem && ui.mode != M_CONFIG && ui.glo_m == M_CONFIG)
            mode_set(M_CONFIG);                      /* (0.13: the pages GLO showed last) */
        else
            mode_set(M_MIXER);
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
        ui.fine = 0;
        if (ui.clear_open)                            /* SHIFT let go: CLEAR aborted (the LXR's way) */
            ui.clear_open = 0;
        sq.erase = 0;
        ui.force = 1;
        if (!ui.shift_used && (ui.mode == M_SAVE || ui.mode == M_LOAD) && ui.io_naming) {
            ui.io_naming = 0;                         /* OCT- alone: back (the dialogs) */
        } else if (!ui.shift_used && ui.kb_open) {
            ui.kb_open = 0;
            ui.kb_at = (uint16_t)(ui.kb_sel + 1u);
        } else if (!ui.shift_used && ui.sb_open) {
            sb_close(0);
        } else if (!ui.shift_used && perf_spells()) {   /* 32: OCT- alone on SPELLS: REVERSE */
            sp_reverse_key();
        } else if (!ui.shift_used && perf_wander()) {   /* 40: OCT- alone on WANDER: home (again: at once) */
            wd_shift_key();
        }
    } else if (b == B_OCTUP) {
        pf.st_hold = 0;                              /* (0.16: STUTTER lets go) */
        if (ui.ca && !ca_rm) {                        /* 0.11: Control All: ask (Phase 17: on by MIDI, it goes on) */
            ui.ca = 0;
            ui.ca_ask = 1;
        }
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
    glfo_morph();                                    /* (0.13: the global LFOs' morph) */
    glfo_macros();                                   /* (19c: .. and the macros) */
    if (fx_found && !ui.dx_seen) {                   /* 0.24.1: another app's data: asked once a power-on */
        ui.dx_seen = 1;
        ui.dx_ask = 1;
        ui.force = 1;
    }
    if (ui.dx_ask == 3u && fm1_ms - ui.dx_ms >= 100u) {   /* (ERASING drawn first) */
        int rc;
        char b[32];
        put_str(b, fx_name(), sizeof b);
        rc = fx_erase();
        ui.dx_ask = 0;
        ui.force = 1;
        if (rc == 0) {
            settings_later();
            put_str(b + str_len(b), " DATA ERASED", sizeof b - str_len(b));
        }
        ui_message(rc ? "ERASE FAILED: SAVES NOTHING" : b);
        ui.msg_long = 1;
    }
    if ((ui.ca || ui.ca_ask) && !ui.ca_len && kit_loads != ui.ca_loads) {   /* 0.11: a kit loaded: Control All ends
                                                      * (kept; 1.0: not the lengths') */
        ui.ca = ui.ca_ask = 0;
        ui.force = 1;
    }
    for (b = 0; b < NB; b++) {
        if (pressed & (1u << panel.btn[b]))
            button_down(b);
        if (rel & (1u << panel.btn[b]))
            button_up(b);
    }
    (void)fm1_input_note_edges();
    ui.keys = keys;
    for (k = 0; k < 16u; k++) {
        if ((down & (1u << WHITE_KEY[k])) && !ui_asking())
            white_down(k);
        if (up & (1u << WHITE_KEY[k]))
            white_up(k);
    }
    for (k = 0; k < 11u; k++)
        if ((down & (1u << BLACK_KEY[k])) && !ui_asking())
            black_down(k);
    if (ui.kkey < 27u && ((up >> ui.kkey) & 1u)) {   /* 19b: the key that played let go: a GATE voice too */
        if (ui.kplay)
            key_note_post(ui.track, ui.knote, 0, KF_REL);
        ui.kkey = 0xFF;
    }
    if ((n = panel_enc(EN_ALGO)) != 0 && !ui_asking()) {
        int32_t t = clamp32((int32_t)ui.track + n, 0, NTRACK - 1);
        track_select((uint32_t)t);
    }
    if ((n = panel_enc(EN_SELECT)) != 0 && !ui_asking()) {
        if (ui.clear_open) {
            ui.clear_sel = (uint8_t)clamp32((int32_t)ui.clear_sel + n, 0, NCLEAR - 1);
        } else if (ui.kb_open) {
            ui.kb_sel = (uint16_t)kb_step(ui.kb_sel, n);
            ui.kb_ms = fm1_ms;
        } else if (ui.sb_open) {
            sb_turn(n);
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
        } else if (ui.mode == M_HOME || ui.mode == M_RECOPT) {
            ui.bar = (uint8_t)clamp32((int32_t)ui.bar + n, 0, 3);
        } else if (ui.mode == M_PATTERN) {
            ui.bank = (uint8_t)clamp32((int32_t)ui.bank + n, 0, (int32_t)ui_banks() - 1);
        } else if (ui.mode == M_PERF) {              /* (29: within the set ARP shows) */
            uint32_t f = perf_first();
            ui.page = (uint8_t)clamp32((int32_t)ui.page + n, (int32_t)f, (int32_t)(f + perf_count()) - 1);
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
    if ((n = panel_enc(EN_PRESET)) != 0 && !ui_asking()) {
        if (ui.mode == M_SAVE || ui.mode == M_LOAD) {
            ui.io_slot = (uint8_t)clamp32((int32_t)ui.io_slot + n, 0, (int32_t)vp_max(VP_IO_SLOT));
        } else if ((ui.mode == M_SOUND || ui.mode == M_KEYS) && !ui.kb_open) {
            sb_turn(n);
        } else {
            if (!ui.kb_open) {
                ui.kb_open = 1;
                ui.kb_sel = (uint16_t)kb_start();
            } else {
                ui.kb_sel = (uint16_t)kb_step(ui.kb_sel, n);
            }
            ui.kb_ms = fm1_ms;
        }
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((n = panel_enc(EN_K1 + k)) != 0 && !ui_asking())
            knob_turn(k, n);
    sp_service();                                    /* (32: the die's roll) */
    wd_service();                                    /* (40: the road) */
    remote_service();
    if (ui.kb_open && fm1_ms - ui.kb_ms > 6000u) {   /* the kit browser closes by itself, nothing loaded */
        ui.kb_open = 0;
        ui.kb_at = (uint16_t)(ui.kb_sel + 1u);
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
                      : ui.mode == M_STEP || ui.mode == M_EUCLID ? B_SEQ : ui.mode == M_PERF ? B_ARP
                      : ui.mode == M_KEYS ? B_EDIT
                      : ui.mode == M_FX ? B_FX : ui.mode == M_SAVE || ui.mode == M_LOAD ? B_SAVE
                      : ui.mode == M_MIXER || ui.mode == M_CONFIG ? B_GLO
                      : ui.mode == M_SOUND ? (sect_group(ui.sect) == S_MODEL ? B_EDIT
                                              : sect_group(ui.sect) == S_AEG ? B_ENV
                                              : sect_group(ui.sect) == S_FILTER ? B_SCL
                                              : sect_group(ui.sect) == S_LFO ? B_LFO : B_GLO)
                      : NB;
    if (fm1_led_anim_on())
        return;
    if (keys_live()) {                               /* 0.8: KEYS: a keyboard (lit: held; the Cs a mid level) */
        for (k = 0; k < 27u; k++) {
            uint32_t n = 53u + k + 12u * ui.koct - 36u;
            led_put(nl, 14u + k, (ui.keys >> k) & 1u);
            led_put(n % 12u == 0u ? nm : nd, 14u + k, 1);
        }
    }
    for (k = 0; k < 16u && !keys_live(); k++) {       /* the white keys */
        uint32_t id = 14u + WHITE_KEY[k], s = ui.bar * 16u + k;
        if (shift_menu()) {
            led_put(k >= 7u && k < 13u ? nl : k < NTRACK ? nd : nm, id, k < 13u);   /* (the menu's keys lit) */
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
            if (perf_spells()) {                     /* (32: SPELLS: lit, what the spell may change) */
                led_put(nl, id, (sp.mask >> k) & 1u);
            } else if (perf_wander()) {              /* (40: WANDER: lit, the parts that travel) */
                led_put(nl, id, (wd.mask >> k) & 1u);
            } else if (perf_trp()) {                 /* (0.23-t7: TRANSPOSE: breathing, the middle; lit, the shown track's;
                                                      * mid, one waiting for WHEN) */
                led_put(nb, id, k == 7u);
                led_put(nl, id, (int32_t)k == sq.trp[ui.track] + 7);
                led_put(nm, id, (int32_t)k == sq.trpend[ui.track] + 7);
            } else if (perf_rush()) {                /* (39: RUSH: lit, a step's RUSH; dim, the steps on) */
                const pattern_t *vp = view_pat(ui.track);
                led_put(nl, id, rush_lock(vp, ui.track, s) != 0u || (sq.running && s == play_step));
                led_put(nm, id, main_on(vp, ui.track, s) && !rush_lock(vp, ui.track, s));
            } else if (k < NTRACK && perf_bank2()) {
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
    for (k = 0; k < NTRACK && !keys_live(); k++) {   /* the voice keys */
        uint32_t id = 14u + BLACK_KEY[k];
        int muted = ((perf_dens() ? pfs.dex : seq_mutes()) >> k) & 1u, flash = fm1_ms - ui.flash_ms[k] < 70u;
        if (perf_spells()) {                         /* (32: SPELLS: lit, the targets) */
            led_put(nl, id, (sp.tracks >> k) & 1u);
            led_put(nd, id, 1);
            continue;
        }
        if (perf_wander()) {                         /* (40: WANDER: lit, the tracks that travel; breathing, a thread
                                                      * moving it) */
            uint32_t i, th = 0;
            for (i = 0; i < NTHREAD; i++)
                th |= wd.th[i].on && wd.th[i].t == k && wd.state == WS_ROAD;
            led_put(nl, id, ((wd.tracks >> k) & 1u) && !th);
            led_put(nb, id, th);
            led_put(nd, id, 1);
            continue;
        }
        if (perf_trp()) {                            /* (0.23-t7: TRANSPOSE: lit, the keys' tracks; breathing, one moved) */
            led_put(nl, id, (sq.trmask >> k) & 1u);
            led_put(nb, id, sq.trp[k] != 0);
            led_put(nd, id, !muted);
            continue;
        }
        if (perf_rush()) {                           /* (39: RUSH: lit, its track) */
            led_put(nl, id, k == ui.track);
            led_put(nb, id, sq.ru_R && k == sq.ru_tr);
            led_put(nd, id, !muted);
            continue;
        }
        if (ca_keys()) {                             /* (Phase 17: Control All: lit, the tracks in it; dim, left out) */
            led_put(nl, id, !((kset.caout >> k) & 1u));
            led_put(nd, id, 1);
            continue;
        }
        led_put(nl, id, (k == ui.track && !ui.perf) || (flash && !muted));
        led_put(nb, id, ui.copy_src == k + 1u && ui.mode != M_PATTERN);
        led_put(nd, id, !muted);
    }
    for (k = 0; k < 4u && !keys_live(); k++) {        /* the bar keys */
        uint32_t id = 14u + BLACK_KEY[NTRACK + k];
        if (ui_banks()) {
            led_put(nl, id, ui.bank == k);
            led_put(nd, id, k < ui_banks());
        } else if (perf_spells()) {                  /* (32: ALL lit with every track; KEEP breathing to keep; SAVE,
                                                      * DEFAULTS) */
            led_put(nl, id, k == 0u && sp.tracks == (1u << NTRACK) - 1u);
            led_put(nb, id, k == 1u && sp_pending());
            led_put(nd, id, 1);
        } else if (perf_wander()) {                  /* (40: ALL lit with every track; KEEP breathing on the road; REST
                                                      * lit resting) */
            led_put(nl, id, (k == 0u && wd.tracks == (1u << NTRACK) - 1u) || (k == 2u && wd.rest));
            led_put(nb, id, (k == 1u && wd.state != WS_IDLE && !ui.shift) || (k == 2u && ui.shift));
            led_put(nd, id, 1);
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
    if (perf_spells() && !ui.shift) {                /* (32: OCT+ breathing: a spell to cast; OCT-: one to reverse) */
        led_put(nb, panel.btn[B_OCTUP], sp_castable());
        led_put(nb, panel.btn[B_OCTDN], sp_pending());
    }
    if (perf_wander() && !ui.shift) {                /* (40: OCT+ breathing: a road to set out on, or to go on; OCT-:
                                                      * a way home) */
        led_put(nb, panel.btn[B_OCTUP], wd_course());
        led_put(nb, panel.btn[B_OCTDN], wd.state != WS_IDLE);
    }
    if ((ui.mode == M_EUCLID && ui.page % NEUCLID == PG_ACTION && ui.action) ||   /* (1.0: ACTION's "PRESS OCT+ TO",
                                                      * the song's ROWS: OCT+ breathing, as the dialogs') */
        (ui.mode == M_SONG && ui.page % NSONGPG == 1u))
        led_put(nb, panel.btn[B_OCTUP], 1);
    led_put(nb, panel.btn[B_OCTUP], ui.ca || ui.ca_ask || ui.dx_ask || ui.pdel || ui.clear_open || ui.kb_open || ui.sb_open || ui.copy_src || ((ui.mode == M_SAVE ||
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
    step_note_lock(shown_pat(), ui.track, ui.step & 63u, note);
    live_step_sync(shown_pat(), ui.track, ui.step & 63u);
    return 1;
}
