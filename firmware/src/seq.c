/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the LXR-02's sequencer. 32 patterns (the LXR's 64, halved for RAM: 5,162 B each) of 7 tracks x 64 steps (4
 * bars of 16), each step with velocity, note, probability, two automation locks, flam (x2 .. x8) and a shift of -7..+7
 * eighths of a step; per track a length of 1..64; per pattern a shuffle and a kit. 64 songs of 64 entries (a pattern,
 * END or LOOP). Rolls, live recording of notes and automation, mutes, the Euclidean generator, MIDI clock in and out.
 * Ported from the LXR firmware, Core/Src/Soundengine/Sequencer (sequencer.c, EuklidGenerator.c, clockSync.c), Copyright
 * (C) 2012-2026 Erica Synths, GPL-3.0-only.
 *
 * The clock is the LXR's: 96 ticks a beat, 24 a step; a step starts on tick 0, and its flams and shifts land on the 24
 * ticks. It runs in the audio ISR, once a BLOCK (64 samples, as the LXR's engine), from the sample count (internal) or
 * the MIDI clock (MIDI sync: 4 ticks a pulse, spread over the pulse's length). Shuffle delays every other step by up to
 * 16 ticks; the ticks it held back then fire at once (as the LXR's negative deltaT does). Differences from the LXR, on
 * purpose: the flam / shift choke is per track (the LXR's is shared by all tracks, so one track's negative shift could
 * swallow another's notes), a step sends one MIDI note (the LXR sent the note twice when a note override was set), MIDI
 * clock out stays on the unshuffled grid, and the roll rates finer than 1/16 and the triplet ones (listed by the LXR,
 * all 1/16 there) play at their rate. */

#define NUM_PATTERN 32
#define NUM_STEPS 64
#define NUM_SONGS 64
#define MAX_SONG_LENGTH 64
#define SONG_END 64u                                 /* (the LXR's numbers, past its 64 patterns: kept) */
#define SONG_LOOP 65u
#define STEP_ACTIVE_MASK 0x80u
#define STEP_VOLUME_MASK 0x7Fu

typedef struct { uint8_t volume, prob, note, p1n, p1v, p2n, p2v; } step_t;   /* the LXR's Step, as its .PAT */
typedef struct {
    step_t step[NTRACK][NUM_STEPS];
    uint8_t timing[NTRACK][NUM_STEPS];           /* flam (low nibble: off x2 x3 x4 x6 x8) | shift << 4 (0..14, 7 = 0) */
    uint16_t main[NTRACK][4];                    /* step on bits, a word a bar */
    uint8_t lhi[NTRACK][NUM_STEPS];              /* the locks' codes' high bits: lock 1 low nibble, 2 high (lock_get) */
    uint8_t cond[NTRACK][NUM_STEPS];             /* the trig condition (0.5: COND_*, cond_ok) */
    uint8_t rat[NTRACK][NUM_STEPS];              /* the ratchet: hits - 1 (0 none) | rate << 3 | fade << 5 */
    uint8_t len[NTRACK];                         /* 1..64 (0: 16) */
    uint8_t clk[NTRACK];                         /* the track's clock: CLK_TICKS's index (CLK_1X: as the pattern) */
    uint8_t gen[NTRACK][4];                      /* its generator: GEN_* and its three settings (seq_gen_*) */
    uint8_t eu2[NTRACK][3];                      /* 0.13: EUCLID's second pulse: hits (0 off), rotation, EU_* */
    uint8_t dir[NTRACK];                         /* 0.10: its direction (DIR_*) */
    uint8_t bnc[NTRACK];                         /* 0.6: its bounces: start (BNC_TICKS's index) | pitch + 12 << 2 */
    uint8_t shuffle, kit;
    uint8_t gx, gy, gchaos;                      /* GRIDS: the map's X, Y and its chaos, 0..127 (the pattern's) */
    uint8_t link[8][4];                          /* 0.6: track coupling (seq_links): src | tgt << 3 | event << 6,
                                                  * effect | window << 4, amount (int8), 0; effect 0: no link */
    uint8_t cdepth;                              /* the links' depth, 0..127 (127: as set) */
    uint8_t sroot, scale, sfollow;               /* 19a: the scale (root C..B, SCALE_*) and the tracks that follow
                                                  * it (bit t) */
    uint8_t mel[NTRACK][8];                      /* 20: the track's melody (ML_*: MEL_* and its settings) */
    uint8_t groove, gamt, ghum, gskip;           /* 26: the GROOVE (0 off, 1.. GROOVE_T's + 1), its amount (100: as
                                                  * written), HUMANIZE 0..127, the tracks that don't follow (bit t) */
    int8_t tbase[NTRACK];                        /* 0.23-t7: the track's BASE note, semitones from C4 (-24..24): a step's
                                                  * note (but a NOTE_LOCK one) plays this much off */
} pattern_t;
#define NOTE_LOCK 0x80u                              /* 0.23-t7: a step's note bit 7: locked (as set), BASE leaves it */
#define TBASE_MAX 24
#define NLINK 8u
/* 26: the grooves. A step's offset in 24ths of a step (its ticks at 1x; + late) and its velocity's, by its place in
 * the bar (s & 15), both scaled by the pattern's amount / 100. SWING nn: the second 16th of each 8th at nn% of it */
#define NGROOVE 12u
#define GAMT_DEFAULT 100u
static const int8_t GROOVE_T[NGROOVE][16] = {
    {0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2},                       /* SWING 54 */
    {0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4},                       /* SWING 58 */
    {0, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6, 0, 6},                       /* SWING 62 */
    {0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8},                       /* SWING 66 */
    {0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10},               /* SWING 71 */
    {0, 1, 2, 1, 3, 1, 2, 1, 0, 1, 2, 1, 3, 1, 2, 1},                       /* LAZY: 2 and 4 late, the 8ths behind */
    {0, 5, -2, 7, 4, 6, -3, 8, 0, 5, -1, 7, 3, 6, -2, 9},                   /* DILLA: loose, uneven */
    {0, 6, 0, 5, 0, 7, 0, 6, 0, 6, 0, 5, 0, 7, 0, 6},                       /* 2-STEP */
    {0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0},                       /* DUB: the off-beats late */
    {-3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3},       /* PUSH: ahead */
    {0},                                                                    /* TECHNO: accents only */
    {0, 2, 4, 2, 0, -2, 0, 2, 0, 2, 4, 2, 0, -2, 0, 2}};                    /* BROKEN: 5 + 3 */
static const int8_t GROOVE_V[NGROOVE][16] = {
    {0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4},
    {0, -6, 0, -6, 0, -6, 0, -6, 0, -6, 0, -6, 0, -6, 0, -6},
    {0, -8, 0, -8, 0, -8, 0, -8, 0, -8, 0, -8, 0, -8, 0, -8},
    {0, -10, 0, -10, 0, -10, 0, -10, 0, -10, 0, -10, 0, -10, 0, -10},
    {0, -12, 0, -12, 0, -12, 0, -12, 0, -12, 0, -12, 0, -12, 0, -12},
    {0, -10, -4, -10, 4, -10, -4, -10, 0, -10, -4, -10, 4, -10, -4, -10},
    {0, -14, -6, -20, 4, -12, -8, -18, 0, -16, -4, -22, 6, -10, -8, -16},
    {0, -12, -4, 8, 0, -12, -4, 8, 0, -12, -4, 8, 0, -12, -4, 8},
    {0, 0, -16, 0, 0, 0, -16, 0, 0, 0, -16, 0, 0, 0, -16, 0},
    {0},
    {0, 12, -12, 12, 0, 12, -12, 12, 0, 12, -12, 12, 0, 12, -12, 12},
    {16, -8, -8, -8, -8, 16, -8, -8, 16, -8, -8, -8, -8, 16, -8, -8}};

/* the tracks' clocks (0.5): ticks a step, slowest to fastest; 1x the LXR's 24 */
#define NCLK 11u
#define CLK_1X 5u
static const uint8_t CLK_TICKS[NCLK] = {96, 72, 48, 36, 32, 24, 18, 16, 12, 8, 6};
/* the ratchets: their rate (ticks between hits) */
static const uint8_t RAT_TICKS[4] = {12, 8, 6, 16};    /* 1/32 1/48 1/64 1/24 */
enum { RF_FLAT, RF_DOWN, RF_UP };
/* 0.6: bouncing ratchets. A step's ratchet with RAT_BOUNCE: its hits from BNC_HITS, its rate field the curve (each
 * gap this many 65536ths of the one before; DRUNK: BALL's, +-15% at random), the first gap the track's start
 * (BNC_TICKS), the pitch drifting by the track's semitones over the bounce. Timed in samples (seq_bounces), not
 * ticks, so the last bounces can come closer than a tick */
#define RAT_BOUNCE 0x80u
enum { BC_BALL, BC_TIGHT, BC_SLOW, BC_DRUNK };
static const uint8_t BNC_HITS[8] = {1, 3, 4, 5, 6, 8, 12, 16};
static const uint32_t BNC_RATIO[4] = {45875, 36045, 85197, 45875};   /* 0.7 0.55 1.3 0.7 */
static const uint8_t BNC_TICKS[4] = {96, 48, 24, 12};                /* 1/4 1/8 1/16 1/32 */
#define BNC_DEFAULT (12u << 2 | 1u)                  /* from 1/8, no pitch drift */
#define BNC_GAP_MIN (64u << 8)                       /* samples, Q8: a block */
/* 0.6: track coupling. A pattern's links (seq_links): when the source does its event, the target's hits in the
 * window (its steps) get the effect, by the amount scaled by the pattern's depth */
enum { LV_HIT, LV_ACCENT, LV_SILENT, NLV };
enum { LE_NONE, LE_DUCK, LE_ONLY, LE_TRIG, LE_RAT, LE_VEL, LE_PITCH, LE_SHIFT, LE_DECAY, NLE };
enum { LW_SAME, LW_NEXT, LW_NEXT2, LW_BAR, NLW };
#define LINK_ACCENT 100u                             /* ACCENT: a hit's velocity above this */
/* each effect's amount: lowest, highest, where a new one starts (DUCK, ONLY: %; TRIG: velocity; RAT: +1..+4
 * hits, 5 a bounce; VEL, DECAY: added; PITCH: semitones; SHIFT: % of half a step) */
static const int8_t LK_LO[NLE] = {0, 0, 0, 1, 1, -127, -24, 0, -127};
static const int8_t LK_HI[NLE] = {0, 100, 100, 127, 5, 127, 24, 100, 127};
static const int8_t LK_DEF[NLE] = {0, 60, 100, 30, 2, -40, 7, 50, -40};
/* the starting points (the LINKS page's Start): DUCK HATS, CALL / RESPONSE, GHOST FILL, CHAIN */
#define NSTART 4u
static const uint8_t LK_START[NSTART][4] = {
    {0 | 5 << 3 | LV_HIT << 6, LE_DUCK | LW_NEXT << 4, 60, 0},       /* D1 hit: CH chance -60%, the next step */
    {5 | 6 << 3 | LV_SILENT << 6, LE_ONLY | LW_NEXT2 << 4, 100, 0},  /* CH rests: OH only then, the next 2 */
    {0 | 1 << 3 | LV_SILENT << 6, LE_TRIG | LW_SAME << 4, 30, 0},    /* D1 rests: D2 at 30, at once */
    {3 | 5 << 3 | LV_HIT << 6, LE_RAT | LW_NEXT << 4, 2, 0}};        /* SN hit: CH +2 hits, the next step */
/* a hit's changes from the links (the occurrence's first hit decides them, a flam's later hits keep them) */
typedef struct {
    int16_t vel, pitch, dec;
    uint8_t rat, shift, drop, on;
} lmod_t;
#define NDQ 4u                                       /* the hits a track can hold back (SHIFT, JITTER) */
/* trig conditions: none, FILL, not FILL, PRE, not PRE, NEI, not NEI, 1ST, not 1ST, then A:B for B 2..8, A 1..B */
enum { COND_NONE, COND_FILL, COND_NFILL, COND_PRE, COND_NPRE, COND_NEI, COND_NNEI, COND_1ST, COND_N1ST, COND_AB };
#define NCOND (COND_AB + 35u)
/* the generators. 0.10: EUCLID is one too, the one that writes the steps and plays none live (its settings
 * length, hits, rotation); last, so saved projects keep their numbers */
enum { GEN_OFF, GEN_MUTATE, GEN_TURING, GEN_CA, GEN_GRIDS, GEN_EUCLID, NGEN };
static int gen_live(const pattern_t *p, uint32_t t) { return p->gen[t][0] != GEN_OFF && p->gen[t][0] != GEN_EUCLID; }
/* 0.10: the tracks' directions (as the Polyend Play's): forward, reverse, ping-pong (the ends once, twice), any
 * step at random, a step either way at random. A time round is the track's length in steps whichever way */
enum { DIR_FWD, DIR_REV, DIR_PING, DIR_PING2, DIR_RAND, DIR_WALK, NDIR };

/* 19a: the scales: the semitones above the root in each (bit n), in the order the SCALE knob has them. A track that
 * follows the pattern's scale plays its notes there: the voice's sounding semitone (its tune + the note, absolute)
 * to the nearest degree (scale_fit) */
enum { SC_CHROM, SC_MAJOR, SC_MINOR, SC_DORIAN, SC_PHRYG, SC_MIXO, SC_LYD, SC_HMINOR, SC_MINPENT, SC_MAJPENT, SC_BLUES,
       SC_MIN7, SC_FIFTHS, SC_PHRDOM, SC_HIRA, SC_PELOG, SC_WHOLE,
       /* 1.0: twelve more, after the others (saved numbers stay) */
       SC_LOCRIAN, SC_MELMIN, SC_HUNMIN, SC_DBLHARM, SC_DIMHW, SC_DIMWH, SC_LYDDOM, SC_ALTERED, SC_INSEN, SC_IWATO,
       SC_KUMOI, SC_EGYPT, NSCALE };
static const uint16_t SCALE_MASK[NSCALE] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0x6B5, 0xAD5, 0x9AD, 0x4A9, 0x295, 0x4E9,
                                            0x489, 0x081, 0x5B3, 0x18D, 0x18B, 0x555,
                                            0x56B, 0xAAD, 0x9CD, 0x9B3, 0x6DB, 0xB6D, 0x6D5, 0x55B, 0x4A3, 0x463,
                                            0x28D, 0x4A5};
#define SFOLLOW_DEFAULT 0x07u                        /* (D1-D3 follow a new pattern's scale) */
/* 19a: the scale a blank pattern starts with: the last one chosen (the project's) */
static struct { uint8_t root, scale; } scl_last;
/* 19b: a step's length: AUTO (the voice's envelope), 1/32 .. 1/2, TIE (held into the next note, legato). 3 bits in
 * spare bits: cond's top two (44 conditions use 6) and prob's top one (0..100 uses 7) */
enum { LEN_AUTO, LEN_32, LEN_16, LEN_8, LEN_8D, LEN_4, LEN_2, LEN_TIE, NLEN };
static const uint8_t LEN_TICKS[NLEN] = {0, 12, 24, 48, 72, 96, 192, 0};
/* 0.23-t11: a step's longer lengths, 3/4 .. 64 bars and INF (held till the track's next note: a drone), codes 8 ..
 * (3 more bits in lhi's spare ones, 2, 3 and 6: the codes' bits 10-11, which no code uses). In steps of 1/16; the melody keeps
 * NLEN's. The knob: AUTO 1/32 .. 1/2, 3/4 .. 64 BARS, INF, TIE (len_knob) */
#define LEN_LONG 8u
static const uint16_t LEN_STEPS[] = {12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024};
#define NLEN_LONG (sizeof LEN_STEPS / sizeof LEN_STEPS[0])
#define LEN_INF (LEN_LONG + NLEN_LONG)
#define NLEN_ALL (LEN_INF + 1u)
#define NLEN_KNOB NLEN_ALL                           /* (the knob's positions: as many as the codes) */
static inline uint32_t len_knob(uint32_t i)          /* the knob's position i: its code (TIE last) */
{
    return i < LEN_TIE ? i : i + 1u >= NLEN_KNOB ? LEN_TIE : i + 1u;
}
static inline uint32_t len_pos(uint32_t l)           /* .. and back */
{
    return l < LEN_TIE ? l : l == LEN_TIE ? NLEN_KNOB - 1u : l < NLEN_ALL ? l - 1u : 0u;
}
#define COND_MASK 0x3Fu
#define PROB_MASK 0x7Fu
static inline uint32_t step_cond(const pattern_t *p, uint32_t t, uint32_t s) { return p->cond[t][s] & COND_MASK; }
static inline uint32_t step_prob(const step_t *x) { return x->prob & PROB_MASK; }
static inline uint32_t step_len(const pattern_t *p, uint32_t t, uint32_t s)
{
    return (uint32_t)(p->cond[t][s] >> 6) | (uint32_t)(p->step[t][s].prob >> 7) << 2 |
           (uint32_t)(p->lhi[t][s] >> 2 & 3u) << 3 | (uint32_t)(p->lhi[t][s] >> 6 & 1u) << 5;
}
static void step_len_set(pattern_t *p, uint32_t t, uint32_t s, uint32_t l)
{
    p->cond[t][s] = (uint8_t)((p->cond[t][s] & COND_MASK) | (l & 3u) << 6);
    p->step[t][s].prob = (uint8_t)((p->step[t][s].prob & PROB_MASK) | ((l >> 2) & 1u) << 7);
    p->lhi[t][s] = (uint8_t)((p->lhi[t][s] & ~0x4Cu) | ((l >> 3) & 3u) << 2 | ((l >> 5) & 1u) << 6);
}
/* 19d: DENSITY's BUILD: what its positive side adds */
enum { DB_CHANCE, DB_GHOSTS, DB_RATCHETS, NDB };

/* 0.8: the velocity the keys play at: the rolls, KEYS, and KEYS' accent (SHIFT). The device's (project.c kset) */
static uint8_t key_vel = 100, key_acc = 127;

static pattern_t pat[NUM_PATTERN] POOL;
static pattern_t pat_backup POOL;   /* the playing pattern as loaded (or saved): RELOAD's, and DROP's */
static uint8_t pat_keep = 1;        /* 0.9: a pattern's edits stay when another plays (as in the project); 0: dropped,
                                     * the LXR's way (the pattern page's Edits; project.c kset.pdrop) */
static uint8_t pat_revert;          /* RELOAD: the playing pattern back as saved, whatever Edits says */
static pattern_t gpat POOL;         /* the generators' live tracks: track t of it plays when t's generator is on */
static pattern_t upat POOL;         /* UNDO: a track as it was before DICE, COMMIT, EUCLID (undo_t, undo_p) */
/* 20: the melody's notes as they played, step by step (bit 7: played; COMMIT writes them in), their lengths */
static uint8_t mel_n[NTRACK][NUM_STEPS] POOL, mel_l[NTRACK][NUM_STEPS] POOL;
static uint8_t songs[NUM_SONGS][MAX_SONG_LENGTH] RAMBUF;   /* (1.0: in the old debug gap) */
static uint8_t song_rd[NUM_SONGS] POOL;   /* 42: a song's ROADS: bit 7 on; bits 0-3 SURPRISE ^ 4 (0..15); bits 4-6 HOME
                                           * (SONG_HOME's) */

static struct {
    uint8_t running, active, pending, load_pending;
    uint8_t song_mode, song, song_pos;
    uint8_t song_n;                              /* (0.19: the times the row has played, less 1) */
    uint8_t song_nx, song_prv, song_run;         /* 42: ROADS: the next row (0xFF: not picked yet), the row before,
                                                  * the rows in a row on one pattern */
    int8_t song_dn;                              /* .. this time the row's times one more or less */
    uint16_t song_hb;                            /* .. bars since the song's first row */
    int8_t idx[NTRACK];                          /* the playing step (-1: before the first) */
    uint8_t prescale, master_cnt;
    uint8_t muted, roll, roll_rate, last_on;
    uint8_t solo;                                /* 0.10: the soloed tracks (any: only they play; seq_mutes) */
    uint8_t rec, erase, lane;                    /* recording, erasing (SHIFT CLEAR while recording), automation lane */
    int8_t armed_step, armed_track;              /* a step held while a knob turns: its lock */
    int8_t skip[NTRACK];                         /* a note just recorded into the next step: not triggered again */
    uint8_t rnd[NTRACK][3];                      /* probability draws: the previous, this and the next step */
    uint8_t sync_ext;                            /* MIDI sync (BPM at MIDI) */
    uint8_t mclk_n;                              /* ticks since the last MIDI clock out */
    uint32_t tick_q16;                           /* samples a tick, Q16 */
    uint32_t acc_q16;                            /* samples into the tick */
    uint32_t due;                                /* ticks the clock has given that are not played yet */
    int32_t hold;                                /* samples a shuffled step still waits */
    uint8_t holding;
    uint32_t ticks;                              /* ticks since start (the rolls' grid) */
    /* MIDI sync */
    uint32_t ext_left;                           /* ticks of the last pulse still to come */
    uint32_t ext_iv;                             /* samples a pulse (smoothed), Q8 */
    uint32_t ext_since;                          /* samples since the last pulse */
    int32_t ext_next;                            /* samples until its next tick, Q8 */
    uint8_t ext_clk;                             /* pulses since the last step (0..5) */
    uint8_t beat;                                /* the master step's beat (the UI's flash): step & 3 == 0 */
    uint16_t bpm_ext_x10;                        /* the tempo the pulses show */
    /* PERF (perform.c) */
    uint8_t fill;                                /* FILL: every step plays, whatever its probability */
    uint8_t stut_req, stut_on;                   /* STUTTER held; looping */
    uint16_t stut_len, stut_n;                   /* the loop's length and the ticks into it */
    uint32_t stut_ticks;                         /* ticks since the loop began */
    struct {
        int8_t idx[NTRACK];
        uint8_t prescale, master_cnt, beat;
        uint8_t rnd[NTRACK][3];
        uint8_t tph[NTRACK];
        uint16_t ptick, loops[NTRACK];
        uint8_t dpos[NTRACK], dback[NTRACK];
        int8_t dnext[NTRACK], dprev[NTRACK];
        uint16_t tacc[NTRACK];
        int32_t toff[NTRACK];
    } stut;                                      /* the position the loop goes back to */
    /* 0.5: the tracks' clocks, conditions, ratchets, generators */
    uint8_t tph[NTRACK];                         /* ticks into the track's step (its clock: CLK_TICKS) */
    uint16_t ptick;                              /* ticks into the pattern (its end: pat_ticks) */
    uint16_t loops[NTRACK];                      /* the track's times round since the start (A:B, 1ST) */
    /* 0.10: the directions: steps into the time round, going back (PING), the next step drawn (RAND, WALK), the
     * step before (-1: none) */
    uint8_t dpos[NTRACK], dback[NTRACK];
    int8_t dnext[NTRACK], dprev[NTRACK];
    uint8_t pre;                                 /* bit t: track t's last condition's result (PRE, NEI) */
    uint16_t cocc[NTRACK][NUM_STEPS];            /* the occurrence (loop + 1) a step's condition was worked out for */
    uint64_t cres[NTRACK];                       /* .. its result, bit k */
    uint8_t rleft[NTRACK], rcnt[NTRACK], rstep[NTRACK], rvel[NTRACK], rn[NTRACK];   /* ratchets running */
    uint32_t rocc[NTRACK];                       /* the occurrence the ratchet started for */
    /* 0.6: bounces running (seq_bounces), the roll sweep (PERF: KNOB 3 while a roll is held) */
    uint8_t bleft[NTRACK], bn[NTRACK], bstep[NTRACK], bvel[NTRACK], bnote[NTRACK], brat[NTRACK];
    int8_t bdrift[NTRACK];
    int32_t bcnt[NTRACK];                        /* samples to its next hit, Q8 */
    uint32_t bgap[NTRACK];                       /* the gap before it, Q8 */
    uint32_t bocc[NTRACK];
    uint32_t brng;
    uint8_t roll_free, roll_k;                   /* the swept rate in use; its position 0..127 (1/4 .. 1/192) */
    int32_t rl_cnt;                              /* samples to the swept rolls' next hit, Q8 */
    /* 39: RUSH (seq_rush, after Elixir Desktop's snare rush, GPL-3.0, github.com/johnnyhawk02/elixir-desktop): the
     * knob 0..100 (0 off) and its track; the project's settings: where the roll starts and goes free (%), the top
     * speed (RT_*), FLAM (%), PITCH (-12..12), the filter at 100 (% of the DJ filter's high-pass side, 0 off), the
     * glide (5 ms units); in use: the amount, its track; this step: an accent taken, a pattern hit, the master step
     * the rush track last played on (below the roll); the free roll: running, its rate (log2 hits a step, Q16), its
     * phase (Q16), an accent waiting; a flam's hit held back (samples Q8); the filter's push; the step recorded */
    uint8_t ru_live, ru_t, ru_start, ru_free_at, ru_top, ru_flam, ru_filt, ru_glide;
    int8_t ru_pitch;
    uint8_t ru_R, ru_tr, ru_acc, ru_orig, ru_free, ru_head, ru_pvel, ru_pnote;
    uint32_t ru_lastk, ru_ph;
    int32_t ru_log, ru_pcnt, ru_hp;
    int8_t ru_rk[NTRACK];
    /* 0.6: the links (seq_links): each track's steps since the start; a link's window (target steps), armed;
     * a TRIGGER waiting for its step; each track's occurrence its events and changes were worked out for */
    uint32_t nst[NTRACK];
    uint32_t lk_from[NLINK], lk_to[NLINK];
    uint8_t lk_on, lk_trig;
    uint32_t locc[NTRACK];
    lmod_t lmod[NTRACK];
    struct {                                     /* hits held back (SHIFT, JITTER): ticks to go (0: free), step,
                                                  * loop, the links' changes */
        uint8_t n, s;
        uint32_t loop;
        lmod_t m;
    } dq[NTRACK][NDQ];
    /* 0.6: PERFORM 2 (perform.c perf_block sets them): DRILL's tracks; JITTER's most (ticks), the occurrence its
     * delay was drawn for, that delay; HALF, its other tick */
    uint8_t drill, drill_span, rdrill, jit, half, hph;
    uint32_t jocc[NTRACK];
    uint8_t jd[NTRACK];
    uint8_t rper[NTRACK], rfade[NTRACK], rnote[NTRACK];   /* a ratchet's rate, fade, note (as it started) */
    uint8_t ldec;                                /* bit t: track t's lane 2 holds a link's DECAY */
    volatile uint8_t gsync;                      /* bit t: the live track from the pattern (the ISR does it) */
    uint16_t greg[NTRACK];                       /* TURING's registers */
    uint8_t gpert[NTRACK];                       /* GRIDS: this time round's perturbation (its chaos) */
    /* 19: TRANSPOSE (scale degrees: the global LFOs); the notes held: ticks to go (0: none), a step's length set
     * (its MIDI note-off then), tied into the next note; each track's last note (DENSITY's ghosts play it) */
    int8_t transp;
    /* 0.23-t7: TRANSPOSE (PERF): each track's degrees (-7..8), those waiting for WHEN (trdue), the keys' tracks (bit t),
     * WHEN (0 now, 1 the beat, 2 the bar), the keys HOLD (1: back to 0 when let go) */
    int8_t trp[NTRACK], trpend[NTRACK];
    uint8_t trdue, trmask, trwhen, trhold;
    uint16_t glen[NTRACK];
    uint8_t gmidi, tie;
    uint8_t lnote[NTRACK];
    /* 19d: DENSITY in use (the knob and the global LFOs: perform.c perf_block), -64..63; the ghosts' velocity,
     * BUILD (DB_*), the tracks it leaves alone (bit t) */
    int8_t dens;
    uint8_t dghost, dbuild, dex;
    /* 40: WANDER's Energy (wander.c): each track's own DENSITY on top, -64..63; the tracks it mutes (seq_mutes);
     * wonly: 1 its part alone (its KEEP writes it into the pattern), 2 the knob's alone (DENSITY's COMMIT) */
    int8_t wdens[NTRACK];
    uint8_t wmute, wonly;
    /* 1.0: the remote's DENSITY a track (TRACKS' CC 96..103, remote.c), -64..63 on top of the knob's (not committed) */
    int8_t tdens[NTRACK];
    /* 1.0: the remote's roll a track (GLOBAL's CC 112..119, remote.c): its CC 0..127 (0 off; seq_croll_*), the
     * free ones' samples to the next hit (Q8) */
    uint8_t croll[NTRACK];
    int32_t cr_cnt[NTRACK];
    uint8_t dvary;                               /* 31: VARY, 0..127: the order shuffled each time round */
    /* 20: the melody: DEJA VU's loop (semitones above the root, lengths), where it is, the notes played (MOTIF's
     * phrase), the last note (WALK), MOTIF's four, the occurrence a note was chosen for; CHANGE and SPREAD from
     * the global LFOs (perf_block), -64..64 */
    int8_t mloop[NTRACK][16];
    uint8_t mlen[NTRACK][16], mfull[NTRACK][2], mmok;   /* (filled: the loop's places, bit t: MOTIF's four) */
    uint8_t mpos[NTRACK];
    uint16_t mcnt[NTRACK];                       /* (0.23-t11: 16 bits: ROTATE's cycle is n * n notes) */
    uint8_t mre;                                 /* 0.23-t11: the tracks whose melody starts again at their loop's
                                                  * start (WANDER home: the arps from their top, as from PLAY) */
    int8_t mlast[NTRACK], mmot[NTRACK][4];
    uint32_t mocc[NTRACK];
    int8_t mchg, mspr;
    /* 1.0: TIME STRETCH, a track's speed against the clock (as the Hapax's): 50 .. 200% in 0.01% (5000 .. 20000;
     * the project's, KTS1), its ticks a tick of the clock (Q16) and the part of one it has (seq_advance); bit t: off
     * the clock's grid since it last realigned (at STOP; with CONFIG's REALIGN, at each pattern change too) */
    uint16_t tstr[NTRACK];
    uint32_t trate[NTRACK];
    uint16_t tacc[NTRACK];
    int32_t toff[NTRACK];                            /* (each track's ticks ahead of the clock, x65536) */
    uint8_t tfree, realign_pat, thome;               /* (thome: tracks gliding back into time, then 100%) */
} sq;
#define TSTR_MIN 5000u
#define TSTR_MAX 20000u
#define TSTR_ONE 10000u
static void seq_stretch_set(uint32_t t, uint32_t v)  /* (the panel, a quest: track t's speed, 0.01%) */
{
    if (t >= NTRACK)
        return;
    v = v < TSTR_MIN ? TSTR_MIN : v > TSTR_MAX ? TSTR_MAX : v;
    sq.tstr[t] = (uint16_t)v;
    sq.trate[t] = (v * 65536u + TSTR_ONE / 2u) / TSTR_ONE;   /* (one word: the ISR reads it as it is) */
    if (v != TSTR_ONE)
        sq.tfree |= (uint8_t)(1u << t);
}
static uint32_t trk_ticks(uint32_t t);
static uint32_t track_len(const pattern_t *p, uint32_t t);
static pattern_t *tpat(uint32_t t);
/* track t back into time (a quest's drift ending): its offset from the clock taken round its loop to the nearest,
 * then run up to 2% slow or fast till it is 0 (seq_advance), then 100% */
static void seq_stretch_home(uint32_t t)
{
    int32_t loop, w, c;                              /* (32-bit: the whole ticks round the loop, the part kept) */
    if (t >= NTRACK)
        return;
    loop = (int32_t)(track_len(tpat(t), t) * trk_ticks(t));
    w = sq.toff[t] >> 16;
    if (loop > 0 && loop < 32768) {
        w %= loop;
        if (w >= loop / 2)
            w -= loop;
        else if (w < -loop / 2)
            w += loop;
    }
    c = (int32_t)((uint32_t)w << 16 | ((uint32_t)sq.toff[t] & 0xFFFFu));
    sq.toff[t] = c;
    if (!c) {
        seq_stretch_set(t, TSTR_ONE);
        return;
    }
    sq.thome |= (uint8_t)(1u << t);
}
static uint32_t trk_scale(uint32_t t, uint32_t ticks)   /* ticks of track t's, in the clock's (its speed) */
{
    uint32_t r = sq.trate[t];
    if (r == 65536u || !ticks)
        return ticks;
    ticks = (uint32_t)(((uint64_t)ticks * 65536u + r / 2u) / r);
    return ticks ? ticks : 1u;
}
/* 37: a pattern's voices, in its kit byte's bits 6-7 (bits 0-5: the kit slot, KIT's). FREE (0, the default): the
 * shared voices, which an edit on any FREE pattern changes; LOCKED: its own, kept in kit slot PV_SLOT(n) (named
 * "P01 LOCK") while another pattern plays; KIT: the saved kit slot it points at, loaded as it starts (the LXR's KIT
 * CHANGE AUTO, per pattern: edits there aren't kept). While a LOCKED or KIT pattern plays, the shared voices wait in
 * kits[KIT_SHARED] (pv_from: the slot they came from) */
enum { PV_FREE, PV_LOCKED, PV_KIT, NPV };
#define PV_SLOT(n) (NUM_KITS / 2u + (n))
static uint8_t pv_from;
static uint8_t pv_skip;             /* (a pattern saved into another slot: its KIT not reloaded when it switches) */
static uint32_t pat_voices(const pattern_t *p) { return (uint32_t)(p->kit >> 6) < NPV ? (uint32_t)(p->kit >> 6) : PV_FREE; }
static uint32_t pat_kit(const pattern_t *p) { return p->kit & 63u; }
static void pat_set_voices(pattern_t *p, uint32_t v, uint32_t k) { p->kit = (uint8_t)((k & 63u) | (v % NPV) << 6); }
static void pv_name(uint32_t n)                      /* a LOCKED pattern's slot: "P01 LOCK" */
{
    char *d = kit_names[PV_SLOT(n)];
    d[0] = 'P';
    d[1] = (char)('0' + (n + 1u) / 10u);
    d[2] = (char)('0' + (n + 1u) % 10u);
    memcpy(d + 3, " LOCK", 5);
}
/* the playing pattern's voices change from `from` (whose voices were a) to `to`'s */
static void pv_switch(uint32_t a, uint32_t from, uint32_t to)
{
    uint32_t b = pat_voices(&pat[to]);
    if (a == PV_LOCKED) {
        kit_stash(PV_SLOT(from));
    } else if (a == PV_FREE && b != PV_FREE) {
        kit_stash(KIT_SHARED);
        pv_from = kit_loaded;
    }
    if (b == PV_LOCKED) {
        kit_unstash(PV_SLOT(to));
    } else if (b == PV_KIT) {
        if (!pv_skip)
            kit_load_ram(pat_kit(&pat[to]));
    } else if (a != PV_FREE) {
        kit_unstash(KIT_SHARED);
        kit_loaded = pv_from;
    }
    pv_skip = 0;
}
/* the playing pattern's voices set to v (the pattern page): LOCKED takes the voices heard; KIT points at the slot the
 * shared voices came from */
static void pv_set(uint32_t v)
{
    pattern_t *p = &pat[sq.active];
    uint32_t n = sq.active, a = pat_voices(p), k = a == PV_FREE ? kit_loaded : pv_from;
    if (v >= NPV || v == a)
        return;
    if (a == PV_FREE) {
        kit_stash(KIT_SHARED);
        pv_from = kit_loaded;
    }
    pat_set_voices(p, v, v == PV_KIT ? k : pat_kit(p));
    if (v == PV_LOCKED) {
        kit_stash(PV_SLOT(n));
        pv_name(n);
    } else if (v == PV_KIT) {
        kit_load_ram(k);
    } else {
        kit_unstash(KIT_SHARED);
        kit_loaded = pv_from;
    }
}
/* a pattern copied over another (COPY, SAVE): a LOCKED one's voices go with it; the playing pattern's voices follow */
static void pv_copy(uint32_t src, uint32_t dst)
{
    uint32_t a = pat_voices(&pat[sq.active]);
    if (src == dst || src >= NUM_PATTERN || dst >= NUM_PATTERN)
        return;
    if (src == sq.active && a == PV_LOCKED)
        kit_stash(PV_SLOT(src));                     /* (as it sounds now) */
    if (pat_voices(&pat[src]) == PV_LOCKED) {
        uint8_t k[KIT_LEN];
        kit_get(PV_SLOT(src), k);
        kit_put(PV_SLOT(dst), k);
        pv_name(dst);
    }
    pat[dst] = pat[src];
    if (dst == sq.active)                            /* (its old voices go unkept: KIT's way out) */
        pv_switch(a == PV_LOCKED ? PV_KIT : a, dst, dst);
}
/* 0.13: what triggers the global LFOs (perform.c glfo_block): PLAY, a pattern change, a bar; OCT+ on LFO n's page:
 * GLE_KEY << n */
enum { GLE_PLAY = 1, GLE_PATTERN = 2, GLE_BAR = 4, GLE_KEY = 8 };
static volatile uint8_t glfo_ev;
/* 0.10: the tracks silent: muted, or not soloed while any is */
static uint32_t seq_mutes(void)
{
    return sq.muted | sq.wmute | (sq.solo ? ~(uint32_t)sq.solo & ((1u << NTRACK) - 1u) : 0u);
}

/* MIDI out and the voices (midi.c, voices.c) */
static void midi_out_note(uint32_t track, uint32_t note, uint32_t vel);
static void midi_out_note_off(uint32_t track);
static void midi_out_rt(uint32_t status);
static void midi_out_pc(uint32_t pattern);
static void kit_load_ram(uint32_t n);
static uint32_t ui_track(void);                      /* the track the panel shows (ui.c) */
static uint32_t ui_pattern(void);                    /* the pattern it shows */

/* ------------------------------------------------------------ steps --- */
/* a step's lock (lane 0, 1): its 12-bit destination code (sound.c autodest_to_par): the step's byte and its 4 high
 * bits in lhi (Elixir's parameters, 302 and on) */
static uint32_t lock_get(const pattern_t *p, uint32_t t, uint32_t k, uint32_t lane)
{
    const step_t *s = &p->step[t][k];
    return (lane ? s->p2n : s->p1n) | ((p->lhi[t][k] >> (lane * 4u)) & 3u) << 8;   /* (bits 2, 3, 6: the length) */
}
static void lock_set(pattern_t *p, uint32_t t, uint32_t k, uint32_t lane, uint32_t code)
{
    step_t *s = &p->step[t][k];
    if (lane)
        s->p2n = (uint8_t)code;
    else
        s->p1n = (uint8_t)code;
    p->lhi[t][k] = (uint8_t)((p->lhi[t][k] & (lane ? 0xCFu : 0xFCu)) | ((code >> 8) & 3u) << (lane * 4u));
}

static void step_reset(step_t *s)                    /* seq_resetNote */
{
    s->note = SEQ_DEFAULT_NOTE;
    s->p1n = s->p2n = NO_AUTOMATION;
    s->p1v = s->p2v = 0;
    s->prob = 100;
    s->volume = 127;
}
static void seq_clear_track(uint32_t t, pattern_t *p)   /* seq_clearTrack */
{
    uint32_t k;
    for (k = 0; k < NUM_STEPS; k++) {
        step_reset(&p->step[t][k]);
        p->timing[t][k] = 7u << 4;                   /* no flam, shift 0 */
        p->lhi[t][k] = 0;
        p->cond[t][k] = 0;
        p->rat[t][k] = 0;
        if ((k & 7u) == 0u)
            p->step[t][k].volume |= STEP_ACTIVE_MASK;
    }
    for (k = 0; k < 4u; k++)
        p->main[t][k] = 0;
    p->len[t] = 16;
    p->clk[t] = CLK_1X;
    memset(p->gen[t], 0, sizeof p->gen[t]);
    memset(p->eu2[t], 0, sizeof p->eu2[t]);
    memset(p->mel[t], 0, sizeof p->mel[t]);
    p->bnc[t] = BNC_DEFAULT;
    p->dir[t] = DIR_FWD;
    p->tbase[t] = 0;
}
static void seq_clear_pattern(pattern_t *p)
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++)
        seq_clear_track(t, p);
    memset(p->link, 0, sizeof p->link);
    p->cdepth = 127;
    p->sroot = scl_last.root;                        /* (19a: a blank pattern, the scale last chosen) */
    p->scale = scl_last.scale;
    p->sfollow = SFOLLOW_DEFAULT;
    p->groove = p->ghum = p->gskip = 0;
    p->gamt = GAMT_DEFAULT;
}
/* 1.0: a pattern deleted (the pattern page: CLEAR, a pattern, COPY): blank, as a new project's; the playing one's voices
 * go unkept as pv_copy's do (a LOCKED one's slot is left as it was) */
static void pat_delete(uint32_t n)
{
    uint32_t a = pat_voices(&pat[sq.active]);
    if (n >= NUM_PATTERN)
        return;
    seq_clear_pattern(&pat[n]);
    pat[n].shuffle = pat[n].kit = 0;
    pat[n].gx = pat[n].gy = pat[n].gchaos = 0;
    if (n == sq.active) {
        pv_switch(a == PV_LOCKED ? PV_KIT : a, n, n);
        pat_backup = pat[n];
    }
}
static int pat_blank(const pattern_t *p)             /* nothing on and no generator: a blank pattern */
{
    uint32_t t, k;
    for (t = 0; t < NTRACK; t++) {
        if (p->gen[t][0])
            return 0;
        for (k = 0; k < 4u; k++)
            if (p->main[t][k])
                return 0;
    }
    return 1;
}
static void seq_clear_automation(uint32_t t, pattern_t *p, uint32_t lane)
{
    uint32_t k;
    for (k = 0; k < NUM_STEPS; k++) {
        lock_set(p, t, k, lane, NO_AUTOMATION);
        if (lane == 0u)
            p->step[t][k].p1v = 0;
        else
            p->step[t][k].p2v = 0;
    }
}
static uint32_t track_len(const pattern_t *p, uint32_t t)
{
    uint32_t l = p->len[t];
    return l == 0u ? 16u : l > NUM_STEPS ? NUM_STEPS : l;
}
static int main_on(const pattern_t *p, uint32_t t, uint32_t s) { return (p->main[t][s >> 4] >> (s & 15u)) & 1u; }
static void main_set(pattern_t *p, uint32_t t, uint32_t s, int on)
{
    if (on)
        p->main[t][s >> 4] |= (uint16_t)(1u << (s & 15u));
    else
        p->main[t][s >> 4] &= (uint16_t)~(1u << (s & 15u));
}

static void seq_set_shuffle_from(uint32_t pt) { par[PAR_SHUFFLE] = pat[pt].shuffle; }

/* ------------------------------------------------------------- tempo --- */
static uint32_t seq_bpm(void) { return par[PAR_BPM] < 20u ? 120u : par[PAR_BPM]; }
static void seq_tempo_changed(void)                  /* BPM (or MIDI's) -> the tick and the LFO sync */
{
    uint32_t x10 = sq.sync_ext ? (sq.bpm_ext_x10 ? sq.bpm_ext_x10 : 1200u) : seq_bpm() * 10u;
    sq.tick_q16 = (uint32_t)(((uint64_t)FS * 60u * 10u << 16) / (uint32_t)(x10 * 96u));
    if (seq_bpm_x10 != x10) {
        seq_bpm_x10 = x10;
        fx_tempo(x10);                               /* (21b: a synced delay) */
        lfo_recalc_sync();
    }
}

/* -------------------------------------------------------- triggering --- */
/* 19a: semitone n (absolute) in scale sc from root r: the nearest degree (a tie: the lower), then deg degrees on */
static int32_t scale_q(uint32_t sc, uint32_t r, int32_t n, int32_t deg)
{
    uint32_t m = SCALE_MASK[sc < NSCALE ? sc : SC_CHROM];
    int32_t d;
#define IN_SC(x) ((m >> (uint32_t)((((x) - (int32_t)r) % 12 + 12) % 12)) & 1u)
    for (d = 0; d < 12 && !IN_SC(n - d) && !IN_SC(n + d); d++)
        ;
    n = IN_SC(n - d) ? n - d : n + d;
    for (; deg > 0; deg--)
        do
            n++;
        while (!IN_SC(n));
    for (; deg < 0; deg++)
        do
            n--;
        while (!IN_SC(n));
#undef IN_SC
    return n;
}
static int scale_on(uint32_t t)                      /* track t follows a scale (not CHROMATIC) */
{
    const pattern_t *p = &pat[sq.active];
    return t < NTRACK && ((p->sfollow >> t) & 1u) && p->scale && p->scale < NSCALE;
}
/* 19a: track t's note in the playing pattern's scale, deg degrees on (and TRANSPOSE's): the voice's sounding
 * semitone (its tune + the note) to the nearest degree, so a voice tuned anywhere plays in key; a track that does
 * not follow, as it is */
static uint32_t scale_fit(uint32_t t, uint32_t note, int32_t deg)
{
    const pattern_t *p = &pat[sq.active];
    int32_t c, n;
    if (t >= NTRACK || !((p->sfollow >> t) & 1u))
        return note;
    deg += sq.transp;
    if (!scale_on(t) && !deg)
        return note;
    c = (int32_t)(vc[t < V_HAT ? t : V_HAT].osc.midi_freq >> 8);
    n = c + (int32_t)note - SEQ_DEFAULT_NOTE;
    return (uint32_t)clamp32((int32_t)note + scale_q(scale_on(t) ? p->scale : SC_CHROM, p->sroot % 12u, n, deg) - n,
                             0, 127);
}
/* 0.23-t7: track t's BASE (the playing pattern's), a step's note as it plays, a played note as a step keeps it */
static int32_t tbase(uint32_t t) { return t < NTRACK ? pat[sq.active].tbase[t] : 0; }
static uint32_t tb_note(uint32_t t) { return (uint32_t)(SEQ_DEFAULT_NOTE + tbase(t)); }   /* (a plain hit's) */
static uint32_t snote(const pattern_t *p, uint32_t t, uint32_t s)
{
    uint32_t n = p->step[t][s].note;
    return n & NOTE_LOCK ? n & 127u : (uint32_t)clamp32((int32_t)n + tbase(t), 0, 127);
}
static uint8_t note_keep(uint32_t t, uint32_t note) { return (uint8_t)clamp32((int32_t)note - tbase(t), 0, 127); }
/* 0.23-t7: TRANSPOSE (PERF): track t's notes x degrees of the pattern's scale on (CHROMATIC: semitones), the note kept
 * as far off its degree as it was (out of the scale: still out) */
static uint32_t trp_fit(uint32_t t, uint32_t note)
{
    const pattern_t *p = &pat[sq.active];
    int32_t x = t < NTRACK ? sq.trp[t] : 0, n;
    uint32_t sc = p->scale < NSCALE ? p->scale : SC_CHROM, r = p->sroot % 12u;
    if (!x)
        return note;
    n = (int32_t)(vc[t < V_HAT ? t : V_HAT].osc.midi_freq >> 8) + (int32_t)note - SEQ_DEFAULT_NOTE;
    return (uint32_t)clamp32((int32_t)note + scale_q(sc, r, n, x) - scale_q(sc, r, n, 0), 0, 127);
}
static uint32_t seq_rand(void);
/* seq_triggerVoice: track t's voice, with step k's locks of pattern p (p 0: none, a roll). 19a: the note in the
 * scale; random pitch per hit there in whole degrees (up to 7 either way, the amount squared) */
static void seq_trigger(uint32_t t, const pattern_t *p, uint32_t k, uint32_t vel, uint32_t note)
{
    const step_t *s = p ? &p->step[t][k] : 0;
    uint32_t r = vc[t < V_HAT ? t : V_HAT].rnd[0];
    int32_t deg = 0;
    auto_set(t, 0, s ? lock_get(p, t, k, 0) : NO_AUTOMATION, s ? s->p1v : 0u);
    auto_set(t, 1, s ? lock_get(p, t, k, 1) : NO_AUTOMATION, s ? s->p2v : 0u);
    if (r && scale_on(t)) {
        int32_t span = (int32_t)(r * r * 7u / 16129u);
        deg = span ? (int32_t)(seq_rand() % (uint32_t)(2 * span + 1)) - span : 0;
        rp_scaled = 1;
    }
    note = trp_fit(t, scale_fit(t, note, deg));
    midi_out_note_off(t);
    voice_trigger(t, vel, note);
    midi_out_note(t, note, vel);
}
/* 19b: a tied note's next: its locks, its pitch, nothing struck (MIDI: the new note on, then the old one off) */
static void midi_out_legato(uint32_t track, uint32_t note, uint32_t vel);
static void seq_legato(uint32_t t, const pattern_t *p, uint32_t k, uint32_t vel, uint32_t note)
{
    const step_t *s = &p->step[t][k];
    auto_set(t, 0, lock_get(p, t, k, 0), s->p1v);
    auto_set(t, 1, lock_get(p, t, k, 1), s->p2v);
    note = trp_fit(t, scale_fit(t, note, 0));
    voice_legato(t, note);
    midi_out_legato(t, note, vel);
}
/* 19b: track t's note as its step's length has it: the voice held (its GATE on) for the length, the MIDI note-off at
 * its end; AUTO: the voice's own envelope (GATE: a step of the track), MIDI's note-off as the LXR's; TIE: held
 * into the next note, which slides there (seq_legato) */
static uint32_t trk_ticks(uint32_t t);
static void gate_start(uint32_t t, uint32_t l)
{
    uint8_t b = (uint8_t)(1u << t);
    sq.tie &= (uint8_t)~b;
    sq.gmidi = (uint8_t)(l != LEN_AUTO ? sq.gmidi | b : sq.gmidi & ~b);
    voice_hold(t, 1);
    if (l == LEN_TIE || l >= LEN_INF) {              /* (INF: held, no countdown, till the next note) */
        sq.tie |= l == LEN_TIE ? b : 0u;
        sq.glen[t] = 0;
        return;
    }
    l = l >= LEN_LONG ? LEN_STEPS[l - LEN_LONG] * 24u : l ? LEN_TICKS[l] : trk_ticks(t);
    l = trk_scale(t, l);                             /* (1.0: as long as the track's stretched steps) */
    sq.glen[t] = (uint16_t)(l > 0xFFFFu ? 0xFFFFu : l);
}
static void gate_end(uint32_t t)
{
    uint8_t b = (uint8_t)(1u << t);
    voice_hold(t, 0);
    if (sq.gmidi & b)
        midi_out_note_off(t);
    sq.gmidi &= (uint8_t)~b;
    sq.tie &= (uint8_t)~b;
    sq.glen[t] = 0;
}
static void seq_gates(void)                          /* a tick: the notes whose length ran out */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++)
        if (sq.glen[t] && !--sq.glen[t])
            gate_end(t);
}

/* one of the 24 ticks of a step: a 24-bit pattern, the MSB tick 0 (flamIdxToPattern) */
static const uint32_t FLAM_PAT[6] = {0x800000u, 0x800800u, 0x808080u, 0x820820u, 0x888888u, 0x924924u};
enum { POS_PREV, POS_CUR, POS_NEXT };

/* the pattern track t plays from: its generator's live copy (gpat) while the generator is on */
static pattern_t *tpat(uint32_t t)
{
    pattern_t *p = &pat[sq.active];
    return gen_live(p, t) ? &gpat : p;
}
static uint32_t trk_ticks(uint32_t t)                /* ticks a step of track t */
{
    uint32_t c = pat[sq.active].clk[t];
    return CLK_TICKS[c < NCLK ? c : CLK_1X];
}
static uint32_t pat_ticks(void)                     /* the pattern's length in ticks: its longest track's */
{
    const pattern_t *p = &pat[sq.active];
    uint32_t t, best = 0;
    for (t = 0; t < NTRACK; t++) {
        uint32_t n = track_len(p, t) * trk_ticks(t);
        if (n > best)
            best = n;
    }
    return best;
}

/* the trig conditions: step s of track t at its loop (the times round since the start) plays? Worked out once
 * an occurrence (a flam asks again); PRE: this track's last condition (other than PRE / NEI), NEI: the track
 * before's */
static int cond_ok(const pattern_t *p, uint32_t t, uint32_t s, uint32_t loop)
{
    uint32_t c = step_cond(p, t, s), r, n = t ? t - 1u : NTRACK - 1u;
    if (c == COND_NONE)
        return 1;
    if (sq.cocc[t][s] == (uint16_t)(loop + 1u))
        return (int)((sq.cres[t] >> s) & 1u);
    switch (c) {
    case COND_FILL: r = sq.fill; break;
    case COND_NFILL: r = !sq.fill; break;
    case COND_PRE: r = (sq.pre >> t) & 1u; break;
    case COND_NPRE: r = !((sq.pre >> t) & 1u); break;
    case COND_NEI: r = (sq.pre >> n) & 1u; break;
    case COND_NNEI: r = !((sq.pre >> n) & 1u); break;
    case COND_1ST: r = loop == 0u; break;
    case COND_N1ST: r = loop != 0u; break;
    default: {                                       /* A:B: the A-th time of every B */
        uint32_t k = c - COND_AB, b = 2;
        while (k >= b && b < 8u) {
            k -= b;
            b++;
        }
        r = loop % b == k;
        break;
    }
    }
    if (c < COND_PRE || c > COND_NNEI)
        sq.pre = (uint8_t)(r ? sq.pre | 1u << t : sq.pre & ~(1u << t));
    sq.cocc[t][s] = (uint16_t)(loop + 1u);
    sq.cres[t] = r ? sq.cres[t] | 1ull << s : sq.cres[t] & ~(1ull << s);
    return (int)r;
}

/* 19d: DENSITY. A step's strength, 1..255: its place in the bar (the bar's first step, the beats, the 8ths, the
 * 16ths), then its velocity (on: an empty step has none), then a per-pattern random order among equals. Thinning
 * drops the weakest first (the bar's first steps last), building fills the strongest empty places first; turned
 * slowly, the groove thins or fills steadily, it doesn't flicker.
 * 31: VARY moves each strength by a random amount drawn afresh each time round (loop), up to +-128 at 127: a little
 * swaps neighbours (which 16th goes, which ghost comes), more lets an offbeat outlast a beat; the bar's first step
 * isn't moved (at -100% it alone stays). The same draw for the step's checks before, at and after it (as HUMANIZE's) */
static uint32_t groove_hash(uint32_t t, uint32_t s, uint32_t loop);
static uint32_t dens_rank(const pattern_t *p, uint32_t t, uint32_t s, int on, uint32_t loop)
{
    uint32_t i = s & 15u, c = i == 0u ? 3u : (i & 3u) == 0u ? 2u : (i & 1u) == 0u ? 1u : 0u;
    uint32_t h = ((s * 40503u + t * 9973u + sq.active * 21011u) >> 5) & 15u;
    uint32_t r = c * 64u + (on ? (p->step[t][s].volume & STEP_VOLUME_MASK) * 47u / 127u : 0u) + h + 1u;
    if (sq.dvary && c < 3u) {                        /* (the bar's first step as it is: the last to go) */
        int32_t n = (int32_t)(groove_hash(t + 8u, s, loop) & 255u) - 128;
        r = (uint32_t)clamp32((int32_t)r + n * (int32_t)sq.dvary / 127, 1, 254);
    }
    return r;
}
static int32_t dens_at(uint32_t t)                   /* track t's DENSITY: the knob's (but a track left out), 40: and
                                                      * WANDER's */
{
    int32_t d = sq.wonly == 1u || ((sq.dex >> t) & 1u) ? 0 : sq.dens;
    return clamp32(d + (sq.wonly == 2u ? 0 : sq.wdens[t]) + (sq.wonly ? 0 : sq.tdens[t]), -64, 63);
}
static int dens_on(uint32_t t)                       /* DENSITY moves track t's steps (GRIDS, TURING: their density) */
{
    uint32_t g = pat[sq.active].gen[t][0];
    return dens_at(t) && g != GEN_GRIDS && g != GEN_TURING;
}
static int dens_drop(const pattern_t *p, uint32_t t, uint32_t s, uint32_t loop)
{
    int32_t d = dens_at(t);
    return d < 0 && dens_on(t) && dens_rank(p, t, s, 1, loop) <= (uint32_t)(-d) * 3u;   /* (at -64: up to the beats) */
}
/* the positive side in parts: the chances up to 100% (all of it with BUILD CHANCE), then the ghosts, then (RATCHETS)
 * ratchets of 2 on the strongest hits; a part's amount 0..256 (dens_part) */
static uint32_t dens_part(uint32_t t, uint32_t part)
{
    int32_t dd = dens_at(t);
    uint32_t n = sq.dbuild == DB_CHANCE ? 1u : sq.dbuild == DB_GHOSTS ? 2u : 3u, lo, hi, d = (uint32_t)dd;
    if (dd <= 0 || part >= n)
        return 0;
    lo = part * 64u / n;
    hi = (part + 1u) * 64u / n;
    return d < lo ? 0u : d >= hi - 1u ? 256u : (d - lo + 1u) * 256u / (hi - lo);
}
static uint32_t dens_prob(const pattern_t *p, uint32_t t, uint32_t s)   /* a step's chance, DENSITY's push in it */
{
    uint32_t pr = step_prob(&p->step[t][s]);
    if (dens_at(t) <= 0 || !dens_on(t))
        return pr;
    return pr + (100u - (pr > 100u ? 100u : pr)) * dens_part(t, 0) / 256u;
}
static int dens_ghost(const pattern_t *p, uint32_t t, uint32_t s, uint32_t loop)   /* an empty step plays a ghost */
{
    uint32_t a = dens_on(t) ? dens_part(t, 1) : 0u;
    return a && dens_rank(p, t, s, 0, loop) > 256u - a;
}
static int dens_rat(const pattern_t *p, uint32_t t, uint32_t s, uint32_t loop)     /* a hit gets a ratchet of 2 */
{
    uint32_t a = dens_on(t) ? dens_part(t, 2) : 0u;
    return a && dens_rank(p, t, s, 1, loop) > 256u - a;
}
static uint32_t dens_ghost_vel(uint32_t t, uint32_t s, uint32_t loop)   /* 31: a ghost's velocity (VARY: +-a third) */
{
    int32_t v = sq.dghost ? sq.dghost : 1, n = (int32_t)((groove_hash(t + 16u, s, loop) >> 8) & 255u) - 128;
    return (uint32_t)clamp32(v + v * n * (int32_t)sq.dvary / (3 * 128 * 127), 1, 127);
}
static uint32_t gen_dens(uint32_t t, uint32_t d)    /* a GRIDS or TURING track's density, DENSITY's push in it */
{
    d = d > 127u ? 127u : d;
    return dens_at(t) ? (uint32_t)clamp32((int32_t)d + dens_at(t) * 2, 0, 127) : d;
}

/* 20: MELODY: which note a track's hit plays (its rhythm the steps', or its generator's). The random part is
 * Mutable Instruments Marbles' X section's idea (Emilie Gillet, MIT): SPREAD (the range), BIAS (the register),
 * STEPS (from every degree of the pattern's scale to fewer, stronger ones: the chord tones, then the root and fifth)
 * and DEJA VU (0: a new note every time; up: a loop of LOOP notes that changes now and then; 127: locked). The
 * shapes: RANDOM, WALK (a degree or two from the last), ARP up, down, up-down (the chord tones over SPREAD's range),
 * MOTIF (four notes, then transposed, inverted, backwards ending on the root; new with DEJA VU's chance every
 * sixteen). LENGTH a note's length (19b's), LEN VAR the chance of another. Live, as TURING: the steps stay as they
 * are; what played is kept (mel_n, mel_l) and COMMIT writes it in */
enum { MEL_OFF, MEL_RANDOM, MEL_WALK, MEL_ARP_UP, MEL_ARP_DN, MEL_ARP_UD, MEL_MOTIF,
       MEL_CONV, MEL_DIV, MEL_CONDIV, MEL_LEAP, MEL_SPIRAL, MEL_ALT, MEL_PAIR, MEL_ROTATE, MEL_INVERT, MEL_PEDAL, NMEL };
/* 0.23-t11: the arps' orders over the n chord tones (0 the lowest), each a cycle of arp_per's notes: CONVERGE
 * 1-4-2-3 (outside in), DIVERGE 2-3-1-4 (the middle out), CON/DIV the one then the other, LEAPFROG 1-3-2-4-3-5 (up
 * two, back one), SPIRAL 1-2-4-3, ALTERNATE the low half and the high half interleaved, PAIRS 2-1-4-3, ROTATE
 * 1234 2341 3412 .., INVERT 1234 4321, PEDAL 1-2-1-3-1-4 (the lowest between each) */
static int mel_arp(uint32_t sh) { return (sh >= MEL_ARP_UP && sh <= MEL_ARP_UD) || (sh >= MEL_CONV && sh < NMEL); }
static int32_t arp_per(uint32_t sh, int32_t n)
{
    switch (sh) {
    case MEL_ARP_UD: return 2 * n - 2;
    case MEL_CONDIV: case MEL_INVERT: return 2 * n;
    case MEL_LEAP: return n >= 3 ? 2 * (n - 2) : n;
    case MEL_ROTATE: return n * n;
    case MEL_PEDAL: return 2 * (n - 1);
    default: return n;
    }
}
static int32_t arp_div(int32_t k, int32_t n)        /* DIVERGE's k-th */
{
    int32_t c = (n & 1) ? n / 2 : n / 2 - 1, j;
    if (n & 1) {
        j = (k + 1) / 2;
        return (k & 1) ? c - j : c + j;
    }
    j = k / 2;
    return (k & 1) ? c + 1 + j : c - j;
}
static int32_t arp_idx(uint32_t sh, int32_t k, int32_t n, int32_t per)   /* the k-th (0 .. per - 1) note's tone */
{
    int32_t i;
    switch (sh) {
    case MEL_ARP_DN: return n - 1 - k;
    case MEL_ARP_UD: return k < n ? k : per - k;
    case MEL_CONV: return (k & 1) ? n - 1 - k / 2 : k / 2;
    case MEL_DIV: return arp_div(k, n);
    case MEL_CONDIV: return k < n ? ((k & 1) ? n - 1 - k / 2 : k / 2) : arp_div(k - n, n);
    case MEL_LEAP: return n >= 3 ? k / 2 + (k & 1) * 2 : k;
    case MEL_SPIRAL: i = (k & ~3) + (int32_t)((0x2310u >> ((k & 3) * 4)) & 3u); return i < n ? i : k;
    case MEL_ALT: return (k & 1) ? (n + 1) / 2 + k / 2 : k / 2;
    case MEL_PAIR: i = k ^ 1; return i < n ? i : k;
    case MEL_ROTATE: return (k % n + k / n) % n;
    case MEL_INVERT: return k < n ? k : 2 * n - 1 - k;
    case MEL_PEDAL: return (k & 1) ? k / 2 + 1 : 0;
    default: return k;
    }
}
enum { ML_SHAPE, ML_SPREAD, ML_BIAS, ML_STEPS, ML_DEJA, ML_LOOP, ML_LEN, ML_LVAR, NML };
static const uint8_t MEL_DEFAULT[NML] = {MEL_RANDOM, 64, 64, 40, 0, 8, LEN_AUTO, 0};
static int mel_on(const pattern_t *p, uint32_t t) { return t < NTRACK && p->mel[t][ML_SHAPE] && p->mel[t][ML_SHAPE] < NMEL; }
static uint32_t gen_rand(uint32_t n);
/* the semitones above the root STEPS (at least th) allows, low to high: the scale's, weighed root 4, fifth 3,
 * third 2, seventh 1, the rest 0 */
static uint32_t mel_set(uint32_t th, uint8_t out[12])
{
    static const uint8_t W[12] = {4, 0, 0, 2, 2, 0, 0, 3, 0, 0, 1, 1};
    const pattern_t *p = &pat[sq.active];
    uint32_t m = SCALE_MASK[p->scale < NSCALE ? p->scale : SC_CHROM], i, n = 0;
    for (i = 0; i < 12u; i++)
        if (((m >> i) & 1u) && W[i] >= th)
            out[n++] = (uint8_t)i;
    return n;                                        /* (the root always: n >= 1) */
}
static int32_t mel_semi(const uint8_t *set, int32_t n, int32_t d)   /* degree d of the set (0: the root) */
{
    int32_t o = d >= 0 ? d / n : -((n - 1 - d) / n);
    return 12 * o + set[d - o * n];
}
static int32_t mel_deg(const uint8_t *set, int32_t n, int32_t s)    /* the set's nearest degree to semitone s */
{
    int32_t d = (s >= 0 ? s / 12 : -((11 - s) / 12)) * n, best = d, e = 99, k;
    for (k = d - n; k <= d + 2 * n; k++) {
        int32_t x = mel_semi(set, n, k) - s;
        x = x < 0 ? -x : x;
        if (x < e) {
            e = x;
            best = k;
        }
    }
    return best;
}
static int32_t mel_fresh(uint32_t t, const uint8_t *g, const uint8_t *set, int32_t n, int32_t sp)
{
    int32_t c = ((int32_t)g[ML_BIAS] - 64) * n / 64, r = sp * 2 * n / 127, d;
    if (g[ML_SHAPE] == MEL_WALK) {
        static const int8_t ST[10] = {-2, -1, -1, -1, 1, 1, 1, 2, -1, 1};
        int32_t st = ST[gen_rand(10)], l = mel_deg(set, n, sq.mlast[t]);
        d = l + st;
        if (d > c + r || d < c - r)                  /* (at the range's edge: back the other way) */
            d = l - st;
        d = d > c + r ? c + r : d < c - r ? c - r : d;
    } else {                                         /* RANDOM, MOTIF: a bell round BIAS, SPREAD wide */
        int32_t x = (int32_t)gen_rand(256) + (int32_t)gen_rand(256) - 255;
        d = c + (x * r + (x < 0 ? -127 : 127)) / 255;
    }
    return mel_semi(set, n, d);
}
static uint32_t mel_length(const uint8_t *g)
{
    static const uint8_t ALT[12] = {LEN_16, LEN_16, LEN_16, LEN_8, LEN_8, LEN_8, LEN_8D, LEN_8D, LEN_4, LEN_4, LEN_TIE,
                                    LEN_32};
    uint32_t l = g[ML_LEN] < NLEN ? g[ML_LEN] : LEN_AUTO;
    return g[ML_LVAR] && gen_rand(254) < g[ML_LVAR] ? ALT[gen_rand(12)] : l;
}
/* track t's next note (its step s; occ: the hit, a flam's later hits keep the note): the note for the voice (its
 * tune taken off, as scale_fit), its length into *len */
static uint32_t mel_note(uint32_t t, uint32_t s, uint32_t occ, uint32_t *len)
{
    const uint8_t *g = pat[sq.active].mel[t];
    uint8_t set[12];
    uint32_t th = g[ML_STEPS] * 4u / 128u, loop = g[ML_LOOP] < 1u ? 1u : g[ML_LOOP] > 16u ? 16u : g[ML_LOOP], i;
    int32_t n, semi, c, sp = clamp32((int32_t)g[ML_SPREAD] + sq.mspr * 2, 0, 127);
    int32_t dv = clamp32((int32_t)g[ML_DEJA] - sq.mchg * 2, 0, 127);
    int fresh;
    if (sq.mocc[t] == occ && (mel_n[t][s] & 0x80u)) {
        *len = mel_l[t][s];
        return mel_n[t][s] & 127u;
    }
    sq.mocc[t] = occ;
    fresh = dv < 127 && gen_rand(2000) < ((uint32_t)((127 - dv) * (127 - dv) * (127 - dv)) >> 10);
    n = (int32_t)mel_set(th, set);
    if (mel_arp(g[ML_SHAPE])) {                     /* the chord tones in the arp's order, SPREAD's range */
        uint8_t as[12];
        int32_t an = (int32_t)mel_set(th < 2u ? 2u : th, as), span = 2 + sp * 3 * an / 127, k = sq.mcnt[t];
        int32_t b = ((int32_t)g[ML_BIAS] - 64) * an / 64 - span / 2;
        int32_t per = arp_per(g[ML_SHAPE], span);
        k %= per;                                    /* (0.23-t11: the count kept in the cycle: a byte's wrap at 256 */
        sq.mcnt[t] = (uint16_t)k;                    /* jumped an arp of 6, 7, 10 .. notes to elsewhere in it) */
        k = arp_idx(g[ML_SHAPE], k, span, per);
        semi = mel_semi(as, an, b + k);
        *len = mel_length(g);
    } else if (g[ML_SHAPE] == MEL_MOTIF) {
        uint32_t j = sq.mcnt[t] & 3u, q = (sq.mcnt[t] >> 2) & 3u;
        int32_t m0, mj;
        if (!j && !q && (fresh || !((sq.mmok >> t) & 1u))) {   /* (a new motif: DEJA VU's chance, every 16) */
            for (i = 0; i < 4u; i++)
                sq.mmot[t][i] = (int8_t)mel_fresh(t, g, set, n, sp);
            sq.mmok |= (uint8_t)(1u << t);
            for (i = 0; i < 4u; i++)
                sq.mlen[t][i] = (uint8_t)mel_length(g);
        }
        m0 = mel_deg(set, n, sq.mmot[t][0]);
        mj = mel_deg(set, n, sq.mmot[t][q == 3u ? 3u - j : j]);
        c = ((int32_t)g[ML_BIAS] - 64) * n / 64;
        semi = mel_semi(set, n, q == 1u ? mj + 2 : q == 2u ? 2 * m0 - mj : q == 3u && j == 3u ? (c / n) * n : mj);
        *len = sq.mlen[t][j];
    } else {                                         /* RANDOM, WALK: DEJA VU's loop */
        uint32_t p = sq.mpos[t] % loop;
        if (fresh || !((sq.mfull[t][p >> 3] >> (p & 7u)) & 1u)) {
            sq.mloop[t][p] = (int8_t)mel_fresh(t, g, set, n, sp);
            sq.mlen[t][p] = (uint8_t)mel_length(g);
            sq.mfull[t][p >> 3] |= (uint8_t)(1u << (p & 7u));
        }
        semi = mel_semi(set, n, mel_deg(set, n, sq.mloop[t][p]));   /* (as STEPS has it now) */
        *len = sq.mlen[t][p];
        sq.mpos[t] = (uint8_t)((p + 1u) % loop);
    }
    sq.mcnt[t]++;
    sq.mlast[t] = (int8_t)clamp32(semi, -60, 60);
    {                                                /* (the root at the voice's tune: its sounding semitone) */
        int32_t vt = (int32_t)(vc[t < V_HAT ? t : V_HAT].osc.midi_freq >> 8);
        int32_t a = ((int32_t)(pat[sq.active].sroot % 12u) - vt) % 12;
        a = a < 0 ? a + 12 : a;
        a = a > 6 ? a - 12 : a;
        semi = clamp32(SEQ_DEFAULT_NOTE + a + semi, 0, 127);
    }
    mel_n[t][s] = (uint8_t)(0x80u | (uint32_t)semi);
    mel_l[t][s] = (uint8_t)*len;
    return (uint32_t)semi;
}
static void mel_reset(uint32_t t)                    /* the melody from its start (PLAY, a pattern change, RESET) */
{
    sq.mpos[t] = sq.mcnt[t] = 0;
    sq.mocc[t] = 0;
    sq.mlast[t] = 0;
}
static void mel_realign(uint32_t t)                  /* (a loop's start: a melody due to start again) */
{
    if ((sq.mre >> t) & 1u) {
        sq.mre &= (uint8_t)~(1u << t);
        mel_reset(t);
    }
}

/* 26: GROOVE and HUMANIZE for track t's step s in its time round loop (the playing pattern's: a generator's live
 * copy plays with it too). HUMANIZE draws from the step and the round, so the step's checks before, at and after it
 * agree: up to +-3 24ths and +-16 velocity at 127. *dv: the velocity's change (may be 0) */
static uint32_t groove_hash(uint32_t t, uint32_t s, uint32_t loop)
{
    uint32_t h = (t + 1u) * 0x9E3779B1u ^ (s + 1u) * 0x85EBCA77u ^ (loop + 1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    return h ^ (h >> 15);
}
static int32_t groove_at(uint32_t t, uint32_t s, uint32_t loop, int32_t *dv)
{
    const pattern_t *p = &pat[sq.active];
    int32_t d = 0, v = 0;
    if ((p->gskip >> t) & 1u) {
        if (dv)
            *dv = 0;
        return 0;
    }
    if (p->groove && p->groove <= NGROOVE) {
        d = GROOVE_T[p->groove - 1u][s & 15u] * (int32_t)p->gamt;
        d = (d + (d < 0 ? -50 : 50)) / 100;
        v = GROOVE_V[p->groove - 1u][s & 15u] * (int32_t)p->gamt / 100;
    }
    if (p->ghum) {
        uint32_t h = groove_hash(t, s, loop);
        int32_t a = (int32_t)(h & 0xFFu) - 128, b = (int32_t)((h >> 8) & 0xFFu) - 128;   /* (-128..127) */
        d += (a * (int32_t)p->ghum * 3 + (a < 0 ? -127 * 64 : 127 * 64)) / (127 * 128);
        v += b * (int32_t)p->ghum * 16 / (127 * 128);
    }
    if (dv)
        *dv = v;
    return d;
}

static uint32_t shift_flam(const pattern_t *p, uint32_t t, uint32_t s, uint32_t pos, uint32_t loop)
{                                                    /* seq_getShiftFlamPattern */
    uint32_t tm, pt;
    int32_t sh;
    if (!main_on(p, t, s))                           /* (19d: a ghost, on the step) */
        return dens_ghost(p, t, s, loop) && pos == POS_CUR ? FLAM_PAT[0] : 0u;
    if (dens_drop(p, t, s, loop))
        return 0;
    if (sq.rnd[t][pos == POS_PREV ? 0 : pos == POS_CUR ? 1 : 2] > dens_prob(p, t, s) && !sq.fill)
        return 0;
    if (!cond_ok(p, t, s, loop))
        return 0;
    tm = p->timing[t][s];
    pt = FLAM_PAT[(tm & 15u) < 6u ? tm & 15u : 0u];
    sh = ((int32_t)(tm >> 4) - 7) * 3;              /* 8 shifts a step, 24 ticks: x3 */
    sh = clamp32(sh + groove_at(t, s, loop, 0), -23, 23);   /* (26: the groove's, within the steps either side) */
    if (sh == 0)
        return pos == POS_CUR ? pt : 0u;
    if (sh > 0)
        return pos == POS_PREV ? (pt << (24 - sh)) & 0xFFFFFFu : pos == POS_CUR ? (pt >> sh) & 0xFFFFFFu : 0u;
    sh = -sh;
    return pos == POS_CUR ? (pt << sh) & 0xFFFFFFu : pos == POS_NEXT ? (pt >> (24 - sh)) & 0xFFFFFFu : 0u;
}

/* a step's hit: its ratchet (hits - 1 more, rate ticks apart, the velocity faded) starts with an occurrence's
 * first hit (a flam's later hits do not start it again); m: the links' changes (0: none) */
static const uint16_t TRACK_DECAY[NTRACK] = {PAR_VELOD1, PAR_VELOD2, PAR_VELOD3, PAR_VELOD4, PAR_VELOD5,
                                             PAR_VELOD6_CLOSED, PAR_VELOD6_OPEN};
static void bounce_start(uint32_t t, uint32_t r, uint32_t s, uint32_t vel, uint32_t note, uint32_t occ);
static uint32_t seq_rand(void);
static void step_hit(uint32_t t, const pattern_t *p, uint32_t s, uint32_t loop, const lmod_t *m)
{
    uint32_t r = p->rat[t][s], n, vel = p->step[t][s].volume & STEP_VOLUME_MASK, note = snote(p, t, s);
    uint32_t occ = (loop << 6 | s) + 1u, ln = step_len(p, t, s);
    if (!main_on(p, t, s)) {                         /* 19d: DENSITY's ghost: the track's last note, no locks */
        sq.last_on |= 1u << t;
        seq_trigger(t, 0, 0, dens_ghost_vel(t, s, loop), sq.lnote[t]);
        gate_start(t, LEN_AUTO);
        return;
    }
    if (mel_on(&pat[sq.active], t))                  /* (20: the melody's note and length) */
        note = (uint32_t)clamp32((int32_t)mel_note(t, s, occ, &ln) + tbase(t), 0, 127);   /* (0.23-t7: BASE) */
    {                                                /* (26: the groove's and HUMANIZE's velocity) */
        int32_t gv;
        groove_at(t, s, loop, &gv);
        if (gv && vel)
            vel = (uint32_t)clamp32((int32_t)vel + gv, 1, 127);
    }
    if (!(r & 7u) && !(r & RAT_BOUNCE) && dens_rat(p, t, s, loop))
        r = 1u;                                      /* (19d: BUILD's ratchets: 2 hits, 1/32) */
    if (m && m->on) {
        if (m->rat && !(r & 7u))                     /* (RATCHET: the step's own ratchet first) */
            r = m->rat >= 5u ? RAT_BOUNCE | 5u | BC_BALL << 3 : m->rat;
        vel = (uint32_t)clamp32((int32_t)vel + m->vel, 1, 127);
        note = (uint32_t)clamp32((int32_t)note + m->pitch, 0, 127);
    }
    if (sq.drill & (1u << t)) {                      /* DRILL: 2 .. 8 hits at 1/32, 1/48 or 1/64, at random */
        r = (seq_rand() % (sq.drill_span + 1u) + 1u) | (seq_rand() % 3u) << 3;
    }
    if (m && m->dec) {                               /* DECAY: lane 2, this hit and its ratchet */
        uint32_t d = TRACK_DECAY[t];
        auto_set(t, 2, par_to_autodest(d), (uint32_t)clamp32((int32_t)par[d] + m->dec, 0, 127));
        sq.ldec |= (uint8_t)(1u << t);
    } else if (sq.ldec & (1u << t)) {
        auto_set(t, 2, NO_AUTOMATION, 0);
        sq.ldec &= (uint8_t)~(1u << t);
    }
    n = (r & 7u) + 1u;
    if (sq.bocc[t] != occ)                           /* a new hit cuts a bounce (a flam's later hits do not) */
        sq.bleft[t] = 0;
    if (r & RAT_BOUNCE) {
        if (BNC_HITS[r & 7u] > 1u && sq.bocc[t] != occ) {
            bounce_start(t, r, s, vel, note, occ);
            if (((r >> 5) & 3u) == RF_UP)
                vel = vel / BNC_HITS[r & 7u];
        }
    } else if (n > 1u && sq.rocc[t] != occ) {
        sq.rocc[t] = occ;
        sq.rleft[t] = (uint8_t)(n - 1u);
        sq.rn[t] = (uint8_t)n;
        {
            uint32_t rp = trk_scale(t, RAT_TICKS[(r >> 3) & 3u]);   /* (1.0: at the track's speed) */
            sq.rcnt[t] = sq.rper[t] = (uint8_t)(rp > 255u ? 255u : rp);
        }
        sq.rfade[t] = (uint8_t)((r >> 5) & 3u);
        sq.rnote[t] = (uint8_t)note;
        sq.rdrill = (uint8_t)(sq.drill & (1u << t) ? sq.rdrill | 1u << t : sq.rdrill & ~(1u << t));
        sq.rstep[t] = (uint8_t)s;
        sq.rvel[t] = (uint8_t)vel;
        if (((r >> 5) & 3u) == RF_UP)
            vel = vel / n;
    }
    sq.last_on |= 1u << t;
    sq.lnote[t] = (uint8_t)note;
    if ((sq.tie >> t) & 1u)                          /* (19b: tied into this one: it slides here) */
        seq_legato(t, p, s, vel ? vel : 1u, note);
    else
        seq_trigger(t, p, s, vel ? vel : 1u, note);
    gate_start(t, ln);
}
static void seq_ratchets(void)                       /* the ratchets' later hits, on their own rate */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        const pattern_t *p;
        uint32_t f, j, v, note;
        if (!sq.rleft[t] || --sq.rcnt[t])
            continue;
        p = tpat(t);
        f = sq.rfade[t];
        sq.rcnt[t] = sq.rper[t];
        j = sq.rn[t] - sq.rleft[t];                  /* this hit's number (1 ..) */
        sq.rleft[t]--;
        v = sq.rvel[t];
        note = sq.rnote[t];
        if (f == RF_DOWN)
            v = v * (sq.rn[t] - j) / sq.rn[t];
        else if (f == RF_UP)
            v = v * (j + 1u) / sq.rn[t];
        if (sq.rdrill & (1u << t)) {                 /* DRILL: each hit's velocity and pitch (+-2) at random */
            v = 30u + seq_rand() % 98u;
            note = (uint32_t)clamp32((int32_t)note + (int32_t)(seq_rand() % 5u) - 2, 0, 127);
        }
        if (!(seq_mutes() & (1u << t)))
            seq_trigger(t, p, sq.rstep[t], v ? v : 1u, note);
    }
}

/* 0.6: a bounce (ratchet r) from step s's hit (that hit is its first): the rest in samples, seq_bounces */
static void bounce_start(uint32_t t, uint32_t r, uint32_t s, uint32_t vel, uint32_t note, uint32_t occ)
{
    uint32_t b = pat[sq.active].bnc[t];
    sq.bocc[t] = occ;
    sq.brat[t] = (uint8_t)r;
    sq.bn[t] = BNC_HITS[sq.brat[t] & 7u];
    sq.bleft[t] = (uint8_t)(sq.bn[t] - 1u);
    sq.bstep[t] = (uint8_t)s;
    sq.bvel[t] = (uint8_t)vel;
    sq.bnote[t] = (uint8_t)note;
    sq.bdrift[t] = (int8_t)((int32_t)(b >> 2) - 12);
    sq.bgap[t] = (uint32_t)(((uint64_t)BNC_TICKS[b & 3u] * sq.tick_q16) >> 8);
    sq.bcnt[t] = (int32_t)sq.bgap[t];
}
static uint32_t seq_rand(void)                       /* xorshift32 (the bounces' DRUNK, the links' chances) */
{
    uint32_t x = sq.brng ? sq.brng : 0x6C8E9CF5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return sq.brng = x;
}
/* the block's bounces: a hit when its count runs out, the next gap the curve's ratio of this one (TAPE STOP slows
 * them with the clock) */
static void seq_bounces(void)
{
    uint32_t t, dt = (uint32_t)(((uint64_t)(BLOCK << 8) * tape_q16) >> 16);
    for (t = 0; t < NTRACK; t++) {
        sq.bcnt[t] -= sq.bleft[t] ? (int32_t)dt : 0;
        while (sq.bleft[t] && sq.bcnt[t] <= 0) {
            const pattern_t *p = tpat(t);
            uint32_t r = sq.brat[t], c = (r >> 3) & 3u, n = sq.bn[t], j = n - sq.bleft[t], v = sq.bvel[t], rt;
            int32_t note = (int32_t)sq.bnote[t] + sq.bdrift[t] * (int32_t)j / (int32_t)(n - 1u);
            sq.bleft[t]--;
            if (((r >> 5) & 3u) == RF_DOWN)
                v = v * (n - j) / n;
            else if (((r >> 5) & 3u) == RF_UP)
                v = v * (j + 1u) / n;
            if (!(seq_mutes() & (1u << t)))
                seq_trigger(t, p, sq.bstep[t], v ? v : 1u, (uint32_t)clamp32(note, 0, 127));
            rt = BNC_RATIO[c];
            if (c == BC_DRUNK)                       /* +-15% */
                rt = (uint32_t)((int32_t)rt + (int32_t)(((seq_rand() >> 16) * 13763u) >> 16) - 6881);
            sq.bgap[t] = (uint32_t)(((uint64_t)sq.bgap[t] * rt) >> 16);
            if (sq.bgap[t] < BNC_GAP_MIN)
                sq.bgap[t] = BNC_GAP_MIN;
            if (sq.bgap[t] > (uint32_t)(((uint64_t)192u * sq.tick_q16) >> 8))   /* (SLOW: half a bar at most) */
                sq.bgap[t] = (uint32_t)(((uint64_t)192u * sq.tick_q16) >> 8);
            sq.bcnt[t] += (int32_t)sq.bgap[t];
        }
    }
}
/* the roll sweep's rate: position k 0..127 from 1/4 (96 ticks) to 1/192 (2 ticks), on a log scale; samples, Q8 */
static uint32_t roll_period(uint32_t k)
{
    int32_t e = -(int32_t)((k > 127u ? 127u : k) * 2882u);   /* log2(48) / 127 a step, Q16 */
    return (uint32_t)(((uint64_t)96u * sq.tick_q16 >> 8) * (uint32_t)exp2_q16(e) >> 16);
}
static void seq_add_note(uint32_t t, uint32_t vel, uint32_t note);
static void seq_roll_sweep(void)                     /* the block's swept rolls (seq_set_roll starts them) */
{
    uint32_t t, per;
    if (!sq.roll || !sq.roll_free)
        return;
    per = roll_period(sq.roll_k);
    if (sq.rl_cnt > (int32_t)per)                    /* faster: at once */
        sq.rl_cnt = (int32_t)per;
    sq.rl_cnt -= (int32_t)(((uint64_t)(BLOCK << 8) * tape_q16) >> 16);
    if (sq.rl_cnt > 0)
        return;
    sq.rl_cnt += (int32_t)per;
    if (sq.rl_cnt <= 0)
        sq.rl_cnt = (int32_t)per;
    for (t = 0; t < NTRACK; t++)
        if (sq.roll & (1u << t)) {
            uint32_t i = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
            uint32_t note = snote(tpat(t), t, i);
            seq_trigger(t, 0, 0, key_vel, note);
            seq_add_note(t, key_vel, note);
        }
}

/* 1.0: the remote's roll a track (remote.c: GLOBAL's CC 112..119), each track rolling its own hits at its own rate,
 * a steep curve: a small turn the grid's 1/4 (CC 1..4), 1/8 (5..8), 1/16 (9..16), 1/32 (17..32); the rest free, off the
 * grid, 1/32 at 33 to 1/256 at 127 on a log scale (as RUSH's end). Only while playing; recorded under REC */
#define CROLL_FREE 33u
static uint32_t croll_grid(uint32_t v)                /* a grid roll's ticks (0: off or free) */
{
    return !v || v >= CROLL_FREE ? 0u : v <= 4u ? 96u : v <= 8u ? 48u : v <= 16u ? 24u : 12u;
}
static void croll_hit(uint32_t t)
{
    uint32_t i = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t], note = snote(tpat(t), t, i);
    seq_trigger(t, 0, 0, key_vel, note);
    seq_add_note(t, key_vel, note);
}
static void seq_croll_grid(void)                     /* (a tick) */
{
    uint32_t t, e;
    for (t = 0; t < NTRACK; t++)
        if ((e = croll_grid(sq.croll[t])) != 0u && sq.ticks % e == 0u)
            croll_hit(t);
}
static void seq_croll_free(void)                     /* (a block) */
{
    uint32_t t, adv = (uint32_t)(((uint64_t)(BLOCK << 8) * tape_q16) >> 16);
    for (t = 0; t < NTRACK; t++) {
        uint32_t v = sq.croll[t], per;
        if (v < CROLL_FREE) {
            sq.cr_cnt[t] = 0;                        /* (free again: the first hit at once) */
            continue;
        }
        per = (uint32_t)(((uint64_t)12u * sq.tick_q16 >> 8) *
                         (uint32_t)exp2_q16(-(int32_t)((v - CROLL_FREE) * 2092u)) >> 16);   /* (3 octaves / 94) */
        if (sq.cr_cnt[t] > (int32_t)per)             /* (faster: at once) */
            sq.cr_cnt[t] = (int32_t)per;
        sq.cr_cnt[t] -= (int32_t)adv;
        if (sq.cr_cnt[t] > 0)
            continue;
        sq.cr_cnt[t] += (int32_t)per;
        if (sq.cr_cnt[t] <= 0)
            sq.cr_cnt[t] = (int32_t)per;
        croll_hit(t);
    }
}

/* 39: RUSH, after Elixir Desktop's snare rush (GPL-3.0, github.com/johnnyhawk02/elixir-desktop, app/player.js): one
 * knob 0..100% that turns the kit into a roll on one track. Rising, the other tracks' hits go over to it (a fixed
 * hash of track and step under (R / 98)^2.5, so the same hits go first and more join; the hats under (R / 80)^2.5);
 * the kick keeps its beats until 90 and the bar's first until 95. From START the track plays only the roll: 16ths
 * on the straight grid, 32nds halfway to FREE, at one level that swells from 60% to full, an accent it took on the
 * step's first; from FREE a free roll off the grid, 32nds up to TOP (1/64, 1/128, 1/256) at 100, its rate gliding
 * (GLIDE) so the knob never steps it; from 90 the DJ filter's high-pass opens to FILTER at 100. Elixir's own: FLAM
 * (while the free roll's rate moves, a grace note before a hit) and PITCH (+-12 semitones at 100).
 * A step's RUSH lock (the lock code of the LXR's roll parameter, 247: no sound takes it) sets the amount for its
 * step, gliding to the next step's; the first track with one leads. The knob, while up, overrides them */
#define RUSH_CODE 247u
static void rush_defaults(void)                      /* (Elixir Desktop's: FREE 80, TOP 1/256, 40 ms; 1.0: START 10, */
{                                                    /* .. the roll from early on: the user, 2026-11-08; was 75) */
    sq.ru_start = 10;
    sq.ru_free_at = 80;
    sq.ru_top = 2;
    sq.ru_flam = 0;
    sq.ru_pitch = 0;
    sq.ru_filt = 60;
    sq.ru_glide = 8;
}
#define RUSH_MAX 100u
#define RU_HATS 80u                                  /* (the hats all taken by) */
#define RU_ONLY 98u                                  /* (every other hit by) */
#define RU_KICKS 90u                                 /* (the kick's four beats until) */
#define RU_FIRST 95u                                 /* (the bar's first kick until) */
#define RU_HPFROM 90u                                /* (the filter from) */
enum { RT_64, RT_128, RT_256, NRT };
static uint32_t trk_dir(uint32_t t);
static void live_step_sync(const pattern_t *p, uint32_t t, uint32_t s);
static uint32_t rush_lock(const pattern_t *p, uint32_t t, uint32_t k)   /* step k's RUSH (0: none) */
{
    uint32_t v = 0;
    if (!main_on(p, t, k))
        return 0;
    if (lock_get(p, t, k, 0) == RUSH_CODE)
        v = p->step[t][k].p1v;
    else if (lock_get(p, t, k, 1) == RUSH_CODE)
        v = p->step[t][k].p2v;
    return v > RUSH_MAX ? RUSH_MAX : v;
}
/* step k of track t gets a RUSH of v (the lane that has one, else a free one, else the recording lane); turned on
 * at the keys' velocity if it was off */
static void rush_lock_set(pattern_t *p, uint32_t t, uint32_t k, uint32_t v)
{
    uint32_t d0 = lock_get(p, t, k, 0), d1 = lock_get(p, t, k, 1), lane = sq.lane;
    if (d0 == RUSH_CODE || (d1 != RUSH_CODE && d0 == NO_AUTOMATION))
        lane = 0;
    else if (d1 == RUSH_CODE || d1 == NO_AUTOMATION)
        lane = 1;
    lock_set(p, t, k, lane, RUSH_CODE);
    if (lane)
        p->step[t][k].p2v = (uint8_t)v;
    else
        p->step[t][k].p1v = (uint8_t)v;
    if (!main_on(p, t, k)) {
        p->step[t][k].volume = (uint8_t)((p->step[t][k].volume & ~STEP_VOLUME_MASK) | key_vel);
        main_set(p, t, k, 1);
    }
}
static uint32_t rush_at(uint32_t t)                  /* track t's RUSH lock now, gliding to the next (0: none) */
{
    const pattern_t *p = tpat(t);
    uint32_t k, r, n;
    if (sq.idx[t] < 0)
        return 0;
    k = (uint32_t)sq.idx[t];
    r = rush_lock(p, t, k);
    if (!r || trk_dir(t) != DIR_FWD)
        return r;
    n = rush_lock(p, t, (k + 1u) % track_len(p, t));
    if (!n)
        return r;
    return (uint32_t)((int32_t)r + ((int32_t)n - (int32_t)r) * (int32_t)sq.tph[t] / (int32_t)trk_ticks(t));
}
static void rush_now(void)                           /* the amount in use and its track: the knob, else a lock */
{
    uint32_t t, r;
    if (sq.ru_live) {
        sq.ru_R = sq.ru_live;
        sq.ru_tr = sq.ru_t;
        return;
    }
    for (t = 0; t < NTRACK; t++)
        if ((r = rush_at(t)) != 0u) {
            sq.ru_R = (uint8_t)r;
            sq.ru_tr = (uint8_t)t;
            return;
        }
    sq.ru_R = 0;
}
static uint32_t rush_hash(uint32_t t, uint32_t s)    /* 0..65535, fixed for a track's step */
{
    uint32_t h = (t * 131u + s + 1u) * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    return h >> 16;
}
static uint32_t rush_share(uint32_t r, uint32_t all)   /* (min(1, r / all))^2.5, 0..65536 */
{
    uint32_t x = r >= all ? 65536u : r * 65536u / all, x2 = (uint32_t)(((uint64_t)x * x) >> 16), s = 0, b;
    for (b = 1u << 16; b; b >>= 1)                   /* (sqrt(x) in Q16: sqrt(x << 16)) */
        if ((uint64_t)(s + b) * (s + b) <= (uint64_t)x << 16)
            s += b;
    return (uint32_t)(((uint64_t)x2 * s) >> 16);
}
static void rush_hit(uint32_t vel, uint32_t note)    /* the rush track, at vel (its PITCH added) */
{
    uint32_t t = sq.ru_tr;
    if ((seq_mutes() >> t) & 1u)
        return;
    if (sq.ru_pitch)
        note = (uint32_t)clamp32((int32_t)note + (sq.ru_pitch * (int32_t)sq.ru_R + (sq.ru_pitch > 0 ? 50 : -50)) /
                                                     (int32_t)RUSH_MAX, 0, 127);
    seq_trigger(t, 0, 0, vel ? vel : 1u, note);
}
static uint32_t rush_note(void)
{
    uint32_t t = sq.ru_tr, k = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
    return snote(tpat(t), t, k);
}
static uint32_t rush_vel(int acc)                    /* the roll's level: 60% at START to full at 100 */
{
    uint32_t R = sq.ru_R, st = sq.ru_start < RUSH_MAX ? sq.ru_start : RUSH_MAX - 1u;
    if (acc)
        return 127;
    if (R <= st)
        return key_vel;
    return key_vel * (154u + 102u * (R - st) / (RUSH_MAX - st)) >> 8;
}
/* seq_flam: a hit of track t's step s while RUSH is up: 1 if the rush takes it (or covers it) */
static int rush_take(uint32_t t, uint32_t s)
{
    const pattern_t *p = tpat(t);
    uint32_t R = sq.ru_R, vel = p->step[t][s].volume & STEP_VOLUME_MASK, k = sq.ticks / 24u;
    int down = t == 0u && !(s & 3u);
    sq.ru_orig = 1;
    if (t == sq.ru_tr) {                             /* its own: the roll's from START; below, one a step */
        if (vel > LINK_ACCENT)
            sq.ru_acc = 1;
        if (R > sq.ru_start || sq.ru_lastk == k + 1u)
            return 1;
        sq.ru_lastk = k + 1u;
        return 0;
    }
    if (down) {                                      /* the kick's beats: kept, then the bar's first, then none */
        return R >= RU_FIRST || (R >= RU_KICKS && (s & 15u));
    }
    if (rush_hash(t, s) >= rush_share(R, t >= 5u ? RU_HATS : RU_ONLY))
        return 0;
    if (vel > LINK_ACCENT)                           /* taken: its accent goes over */
        sq.ru_acc = 1;
    if (R <= sq.ru_start && sq.ru_lastk != k + 1u) {   /* (below the roll: the rush track plays it, once a step) */
        sq.ru_lastk = k + 1u;
        rush_hit(vel > key_vel ? vel : key_vel, rush_note());
    }
    return 1;
}
/* seq_tick_one: the grid roll, START .. FREE: 16ths, 32nds from halfway; above FREE the step's first hands its
 * accent to the free roll */
static void rush_grid(void)
{
    uint32_t ph = sq.ticks % 24u, R = sq.ru_R, half = (sq.ru_start + sq.ru_free_at) / 2u, acc, orig;
    if (ph && ph != 12u)
        return;
    acc = sq.ru_acc;
    orig = sq.ru_orig;
    if (!ph)
        sq.ru_acc = sq.ru_orig = 0;
    if (!R || R <= sq.ru_start)
        return;
    if (R > sq.ru_free_at) {
        if (!ph && orig && acc)
            sq.ru_head = 1;
        return;
    }
    if (ph == 12u && R <= half)
        return;
    rush_hit(rush_vel(!ph && acc), rush_note());
}
/* the block: the amount, recording, the free roll, a held flam, the filter */
static void seq_rush(void)
{
    uint32_t t = sq.ru_tr, adv = (uint32_t)(((uint64_t)(BLOCK << 8) * tape_q16) >> 16), R;
    uint32_t steplen = (uint32_t)(((uint64_t)24u * sq.tick_q16) >> 8);   /* (a 16th, samples Q8) */
    int32_t k = (int32_t)((uint32_t)BLOCK * 65536u / ((uint32_t)sq.ru_glide * 5u * (FS / 1000u) + BLOCK)), hpt;
    rush_now();
    R = sq.ru_R;
    if (sq.ru_pcnt) {                                /* a flam's hit, held back */
        sq.ru_pcnt -= (int32_t)adv;
        if (sq.ru_pcnt <= 0) {
            sq.ru_pcnt = 0;
            rush_hit(sq.ru_pvel, sq.ru_pnote);
        }
    }
    for (t = 0; t < NTRACK; t++)                     /* recording: the knob into each step its track passes */
        if (!(sq.rec && sq.ru_live && t == sq.ru_t)) {
            sq.ru_rk[t] = -1;
        } else if (sq.idx[t] >= 0 && sq.ru_rk[t] != sq.idx[t]) {
            sq.ru_rk[t] = (int8_t)sq.idx[t];
            rush_lock_set(&pat[sq.active], t, (uint32_t)sq.idx[t], sq.ru_live);
            live_step_sync(&pat[sq.active], t, (uint32_t)sq.idx[t]);
        }
    hpt = R > RU_HPFROM && sq.ru_filt ? (int32_t)((uint32_t)sq.ru_filt * 655u * (R - RU_HPFROM) / (RUSH_MAX - RU_HPFROM))
                                      : 0;
    sq.ru_hp += (int32_t)(((int64_t)(hpt - sq.ru_hp) * (k > 4096 ? k : 4096)) >> 16);
    if (!hpt && sq.ru_hp < 64)
        sq.ru_hp = 0;
    if (R <= sq.ru_free_at || !steplen) {
        sq.ru_free = 0;
        sq.ru_head = 0;
        return;
    }
    {
        int32_t top = (int32_t)(sq.ru_top < NRT ? sq.ru_top : RT_256) + 2;   /* (log2 hits a step at 100: 2 .. 4) */
        int32_t tgt = 65536 + (int32_t)((R - sq.ru_free_at) * (uint32_t)(top - 1) * 65536u /
                                         (RUSH_MAX - sq.ru_free_at));
        uint32_t rate, cap = steplen * 2u, d;        /* (at most a hit every 128 samples) */
        int32_t diff;
        if (!sq.ru_free) {                           /* in step with the 32nds it takes over from */
            sq.ru_free = 1;
            sq.ru_log = tgt;
            sq.ru_ph = (sq.ticks % 12u) * 65536u / 12u;
        } else {
            sq.ru_log += (int32_t)(((int64_t)(tgt - sq.ru_log) * k) >> 16);
        }
        diff = tgt - sq.ru_log;
        rate = (uint32_t)exp2_q16(sq.ru_log);
        if (rate > cap)
            rate = cap;
        d = (rate >> 4) * (uint32_t)(BLOCK << 8) / ((steplen >> 4) ? (steplen >> 4) : 1u);
        d = (uint32_t)(((uint64_t)d * tape_q16) >> 16);
        sq.ru_ph += d;
        while (sq.ru_ph >= 65536u) {
            uint32_t vel = rush_vel(sq.ru_head), note = rush_note(), c = (uint32_t)(diff < 0 ? -diff : diff) >> 9;
            sq.ru_ph -= 65536u;
            sq.ru_head = 0;
            if (c > 127u)
                c = 127u;
            if (c && !sq.ru_pcnt && sq.ru_flam && seq_rand() % 12700u < c * sq.ru_flam) {
                uint32_t gap = (steplen / ((rate >> 8) ? (rate >> 8) : 1u)) << 6;   /* (a quarter of the gap) */
                rush_hit(vel * 5u / 9u, note);       /* a flam: the grace note, the hit a moment after */
                sq.ru_pvel = (uint8_t)vel;
                sq.ru_pnote = (uint8_t)note;
                sq.ru_pcnt = (int32_t)(gap < (1100u << 8) ? gap : 1100u << 8);
                continue;
            }
            rush_hit(vel, note);
        }
    }
}

/* 0.6: the links. Step s of track t is due to play (on, its chance, its condition), the SILENT event's test */
static int step_live(const pattern_t *p, uint32_t t, uint32_t s, uint32_t loop)
{
    if (!main_on(p, t, s))
        return dens_ghost(p, t, s, loop);
    return !dens_drop(p, t, s, loop) && (sq.rnd[t][1] <= dens_prob(p, t, s) || sq.fill) && cond_ok(p, t, s, loop);
}
static uint32_t links_set(const pattern_t *p)        /* bit l: link l is set */
{
    uint32_t l, m = 0;
    for (l = 0; l < NLINK; l++)
        if ((p->link[l][1] & 15u) && (p->link[l][1] & 15u) < NLE)
            m |= 1u << l;
    return m;
}
/* the links on this tick's hits (hm: the tracks with one; hs, hl: its step, loop; hp: its step from the track's
 * own, -1 .. 1), each link in turn seeing what those before it did: its event arms its window on the target's
 * steps (TRIGGER: a hit at its start), then the target's hit in the window takes the effect (ONLY IF: one
 * outside it is dropped). A dropped hit is SILENT to the links after it */
static void seq_links(uint32_t hm, const uint8_t *hs, const uint32_t *hl, const int8_t *hp, uint32_t sil)
{
    const pattern_t *pa = &pat[sq.active];
    uint32_t l, t, fresh = 0, trig = 0;
    uint8_t tvel[NTRACK];
    for (t = 0; t < NTRACK; t++)
        if (hm >> t & 1u) {
            uint32_t occ = (hl[t] << 6 | hs[t]) + 1u;
            if (sq.locc[t] != occ) {                 /* the occurrence's first hit: its events, its changes */
                fresh |= 1u << t;
                sq.locc[t] = occ;
                memset(&sq.lmod[t], 0, sizeof sq.lmod[t]);
            }
        }
    for (l = 0; l < NLINK; l++) {
        const uint8_t *L = pa->link[l];
        uint32_t src = L[0] & 7u, tgt = L[0] >> 3 & 7u, ev = L[0] >> 6, e = L[1] & 15u, w = L[1] >> 4 & 3u;
        uint32_t b = 1u << l, fired;
        int32_t a = (int32_t)(int8_t)L[2] * (int32_t)pa->cdepth / 127;
        lmod_t *m = &sq.lmod[tgt];
        if (!e || e >= NLE || src >= NTRACK || tgt >= NTRACK || ev >= NLV) {
            sq.lk_on &= (uint8_t)~b;
            sq.lk_trig &= (uint8_t)~b;
            continue;
        }
        if ((sq.lk_on & b) && sq.nst[tgt] > sq.lk_to[l])     /* its window is over */
            sq.lk_on &= (uint8_t)~b;
        if ((sq.lk_trig & b) && sq.tph[tgt] == 0u && sq.nst[tgt] >= sq.lk_from[l]) {   /* a TRIGGER's step */
            sq.lk_trig &= (uint8_t)~b;
            if (a > 0) {
                trig |= 1u << tgt;
                tvel[tgt] = (uint8_t)(a > 127 ? 127 : a);
            }
        }
        if (ev == LV_SILENT)
            fired = sil >> src & 1u;
        else
            fired = (fresh >> src & 1u) && !sq.lmod[src].drop &&
                    (ev == LV_HIT || (int32_t)(tpat(src)->step[src][hs[src]].volume & STEP_VOLUME_MASK) +
                                             sq.lmod[src].vel > (int32_t)LINK_ACCENT);
        if (fired) {
            uint32_t ts = sq.nst[tgt], i = sq.idx[tgt] < 0 ? 0u : (uint32_t)sq.idx[tgt] & 15u;
            if (e == LE_TRIG) {                      /* (TRIGGER: the window its delay) */
                if (w == LW_SAME) {
                    if (a > 0) {
                        trig |= 1u << tgt;
                        tvel[tgt] = (uint8_t)(a > 127 ? 127 : a);
                    }
                } else {
                    sq.lk_from[l] = w == LW_NEXT ? ts + 1u : w == LW_NEXT2 ? ts + 2u : ts + 16u - i;
                    sq.lk_trig |= (uint8_t)b;
                }
            } else {
                sq.lk_from[l] = w == LW_NEXT || w == LW_NEXT2 ? ts + 1u : ts;
                sq.lk_to[l] = w == LW_SAME ? ts : w == LW_NEXT ? ts + 1u : w == LW_NEXT2 ? ts + 2u : ts + 15u - i;
                sq.lk_on |= (uint8_t)b;
            }
        }
        if (e == LE_TRIG || !(fresh >> tgt & 1u) || m->drop)
            continue;
        {
            uint32_t h = sq.nst[tgt] + (uint32_t)(int32_t)hp[tgt];
            int in = (sq.lk_on & b) && h >= sq.lk_from[l] && h <= sq.lk_to[l];
            if (e == LE_ONLY ? !in && (int32_t)(seq_rand() % 100u) < a : in && e == LE_DUCK &&
                                                                          (int32_t)(seq_rand() % 100u) < a) {
                m->drop = m->on = 1;
                sil |= 1u << tgt;
                continue;
            }
            if (!in || e == LE_ONLY || e == LE_DUCK)
                continue;
            m->on = 1;
            switch (e) {
            case LE_RAT: if (a > (int32_t)m->rat) m->rat = (uint8_t)(a > 5 ? 5 : a); break;
            case LE_VEL: m->vel = (int16_t)(m->vel + a); break;
            case LE_PITCH: m->pitch = (int16_t)(m->pitch + a); break;
            case LE_SHIFT: if (a > (int32_t)m->shift) m->shift = (uint8_t)(a > 100 ? 100 : a); break;
            case LE_DECAY: m->dec = (int16_t)(m->dec + a); break;
            default: break;
            }
        }
    }
    for (t = 0; t < NTRACK; t++)                     /* the TRIGGERs: the track's step's note, no locks */
        if ((trig >> t & 1u) && !(seq_mutes() >> t & 1u)) {
            uint32_t i = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
            sq.last_on |= 1u << t;
            seq_trigger(t, 0, 0, tvel[t], snote(tpat(t), t, i));
        }
}

/* a hit held back d ticks (the queue full: the one due first plays now, its place taken) */
static void dq_push(uint32_t t, uint32_t d, uint32_t s, uint32_t loop, const lmod_t *m)
{
    uint32_t k, f = 0;
    for (k = 0; k < NDQ; k++) {
        if (!sq.dq[t][k].n) {
            f = k;
            break;
        }
        if (sq.dq[t][k].n < sq.dq[t][f].n)
            f = k;
    }
    if (k == NDQ) {
        sq.dq[t][f].n = 0;
        step_hit(t, tpat(t), sq.dq[t][f].s, sq.dq[t][f].loop, &sq.dq[t][f].m);
    }
    sq.dq[t][f].n = (uint8_t)(d > 255u ? 255u : d);
    sq.dq[t][f].s = (uint8_t)s;
    sq.dq[t][f].loop = loop;
    if (m)
        sq.dq[t][f].m = *m;
    else
        memset(&sq.dq[t][f].m, 0, sizeof sq.dq[t][f].m);
    sq.dq[t][f].m.shift = 0;
}

/* 0.10: the step after cur on track t going its direction (back: PING's way back, updated); RAND, WALK: the one
 * drawn (sq.dnext) */
static uint32_t trk_dir(uint32_t t)                  /* (the pattern's, as the clock: not a live track's copy) */
{
    uint32_t d = pat[sq.active].dir[t];
    return d < NDIR ? d : DIR_FWD;
}
static uint32_t dir_after(uint32_t t, uint32_t cur, uint32_t len, uint8_t *back)
{
    uint32_t d = trk_dir(t);
    switch (d) {
    case DIR_REV: return cur && cur < len ? cur - 1u : len - 1u;
    case DIR_PING:
    case DIR_PING2:
        if (len < 2u)
            return 0;
        if (cur >= len)
            cur = len - 1u;
        if (!*back) {
            if (cur + 1u < len)
                return cur + 1u;
            *back = 1;                               /* (the end: PING turns, PING2 plays it again first) */
            return d == DIR_PING ? cur - 1u : cur;
        }
        if (cur)
            return cur - 1u;
        *back = 0;
        return d == DIR_PING ? 1u : 0u;
    case DIR_RAND:
    case DIR_WALK: return sq.dnext[t] >= 0 && (uint32_t)sq.dnext[t] < len ? (uint32_t)sq.dnext[t] : 0u;
    default: return cur + 1u >= len ? 0u : cur + 1u;
    }
}
/* what plays on this tick: each track at its clock's position in its step (tph: the 24 positions of a step that
 * this tick covers, slower clocks covering one now and then, faster several). 0.6: the hits found first, then
 * the links (with any set and a depth), then they play */
static void seq_flam(uint32_t mk)                   /* (1.0: mk, the tracks this pass moves: seq_advance) */
{
    uint32_t t, hm = 0, sil = 0, live = pat[sq.active].cdepth && links_set(&pat[sq.active]);
    uint8_t hs[NTRACK];
    uint32_t hl[NTRACK];
    int8_t hp[NTRACK];
    for (t = 0; t < NTRACK; t++) {
        const pattern_t *p = tpat(t);
        uint32_t len, cur, nxt, prv, nlp, plp, L = trk_ticks(t), lo, hi, bits, lp = sq.loops[t];
        int has_prv = 1;
        if (!(mk >> t & 1u))
            continue;
        lo = sq.tph[t] * 24u / L;
        hi = (sq.tph[t] + 1u) * 24u / L;
        bits = hi > lo ? ((1u << (hi - lo)) - 1u) << (24u - hi) : 0u;
        len = track_len(p, t);
        cur = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
        if (live && sq.tph[t] == 0u && ((seq_mutes() & (1u << t)) || !step_live(p, t, cur, lp)))
            sil |= 1u << t;
        if ((seq_mutes() & (1u << t)) || !bits)
            continue;
        if (trk_dir(t) == DIR_FWD) {
            nxt = (cur + 1u) % len;
            prv = cur ? cur - 1u : len - 1u;
            nlp = nxt ? lp : lp + 1u;
            plp = cur || !lp ? lp : lp - 1u;
        } else {                                     /* (0.10: the steps either side as the direction has them) */
            uint8_t bk = sq.dback[t];
            nxt = dir_after(t, cur, len, &bk);
            nlp = sq.dpos[t] + 1u >= len ? lp + 1u : lp;
            has_prv = sq.dprev[t] >= 0 && (uint32_t)sq.dprev[t] < len;
            prv = has_prv ? (uint32_t)sq.dprev[t] : 0u;
            plp = sq.dpos[t] == 0u && lp ? lp - 1u : lp;
        }
        if (shift_flam(p, t, nxt, POS_NEXT, nlp) & bits) {
            hs[t] = (uint8_t)nxt;
            hl[t] = nlp;
            hp[t] = 1;
        } else if (shift_flam(p, t, cur, POS_CUR, lp) & bits) {
            if (sq.skip[t] == (int8_t)cur) {         /* recorded into this step a moment ago: it sounded already */
                sq.skip[t] = -1;
                continue;
            }
            hs[t] = (uint8_t)cur;
            hl[t] = lp;
            hp[t] = 0;
        } else if (has_prv && shift_flam(p, t, prv, POS_PREV, plp) & bits) {
            hs[t] = (uint8_t)prv;
            hl[t] = plp;
            hp[t] = -1;
        } else {
            if (sq.last_on & (1u << t)) {
                sq.last_on &= (uint8_t)~(1u << t);
                if (!(((sq.gmidi | sq.tie) >> t) & 1u))   /* (19b: a length set: its own note-off) */
                    midi_out_note_off(t);
            }
            continue;
        }
        hm |= 1u << t;
    }
    if (live)
        seq_links(hm, hs, hl, hp, sil);
    for (t = 0; t < NTRACK; t++) {
        const lmod_t *m = live ? &sq.lmod[t] : 0;
        uint32_t d = 0;
        if (!(hm >> t & 1u) || (m && m->drop))
            continue;
        if (sq.ru_R && rush_take(t, hs[t]))         /* 39: RUSH takes it */
            continue;
        if (m && m->shift)                           /* SHIFT: up to half a step late */
            d = m->shift * trk_ticks(t) / 200u + 1u;
        if (sq.jit) {                                /* JITTER: late by a random part of its most, an occurrence's
                                                      * hits alike */
            uint32_t occ = (hl[t] << 6 | hs[t]) + 1u;
            if (sq.jocc[t] != occ) {
                sq.jocc[t] = occ;
                sq.jd[t] = (uint8_t)(seq_rand() % (sq.jit + 1u));
            }
            d += sq.jd[t];
        }
        d = trk_scale(t, d);                         /* (1.0: in the track's stretched ticks) */
        if (d)
            dq_push(t, d, hs[t], hl[t], m);
        else
            step_hit(t, tpat(t), hs[t], hl[t], m);
    }
}
static void seq_delayed(void)                        /* the hits held back, when due */
{
    uint32_t t, k;
    for (t = 0; t < NTRACK; t++)
        for (k = 0; k < NDQ; k++)
            if (sq.dq[t][k].n && !--sq.dq[t][k].n)
                step_hit(t, tpat(t), sq.dq[t][k].s, sq.dq[t][k].loop, &sq.dq[t][k].m);
}

/* the rolls (PERF: the keys roll their track at ROLL's rate): ticks between two hits */
static const uint16_t ROLL_TICKS[14] = {0, 384, 192, 96, 48, 24, 16, 12, 8, 6, 3, 128, 64, 32};
static void seq_add_note(uint32_t t, uint32_t vel, uint32_t note);

/* ------------------------------------------------------- patterns --- */
/* track t of s into d: its steps and all of its settings */
/* a step's settings edited on a generator's track (chance, velocity, note, condition, timing, locks, ratchet,
 * length): its live copy, which is what plays, takes them too; whether it plays stays the generator's */
static void live_step_sync(const pattern_t *p, uint32_t t, uint32_t s)
{
    if (p != &pat[sq.active] || !gen_live(p, t) || t >= NTRACK || s >= NUM_STEPS)
        return;
    gpat.step[t][s] = p->step[t][s];
    gpat.timing[t][s] = p->timing[t][s];
    gpat.lhi[t][s] = p->lhi[t][s];
    gpat.cond[t][s] = p->cond[t][s];
    gpat.rat[t][s] = p->rat[t][s];
}
static void trk_copy(pattern_t *d, const pattern_t *s, uint32_t t)
{
    memcpy(d->step[t], s->step[t], sizeof d->step[t]);
    memcpy(d->timing[t], s->timing[t], sizeof d->timing[t]);
    memcpy(d->lhi[t], s->lhi[t], sizeof d->lhi[t]);
    memcpy(d->cond[t], s->cond[t], sizeof d->cond[t]);
    memcpy(d->rat[t], s->rat[t], sizeof d->rat[t]);
    memcpy(d->main[t], s->main[t], sizeof d->main[t]);
    memcpy(d->gen[t], s->gen[t], sizeof d->gen[t]);
    memcpy(d->eu2[t], s->eu2[t], sizeof d->eu2[t]);
    d->len[t] = s->len[t];
    d->clk[t] = s->clk[t];
    d->bnc[t] = s->bnc[t];
    d->dir[t] = s->dir[t];
    d->sfollow = (uint8_t)((d->sfollow & ~(1u << t)) | (s->sfollow & (1u << t)));
    memcpy(d->mel[t], s->mel[t], sizeof d->mel[t]);
    d->tbase[t] = s->tbase[t];
}
static void seq_gen_sync(uint32_t t);
/* every track before its first step, its counts from 0; 1.0: but the tracks in keep (TIME STRETCH's, off the clock's
 * grid: a pattern change) where they are. The rest realigned: on the clock's grid again */
static void seq_reset_tracks_keep(uint32_t keep)
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        sq.rleft[t] = 0;
        sq.rocc[t] = 0;
        sq.bleft[t] = 0;
        sq.bocc[t] = 0;
        sq.locc[t] = 0;
        sq.jocc[t] = 0;
        if (keep >> t & 1u)
            continue;
        sq.idx[t] = -1;
        sq.tph[t] = 0;
        sq.tacc[t] = 0;
        sq.toff[t] = 0;
        sq.thome &= (uint8_t)~(1u << t);
        sq.loops[t] = 0;
        sq.nst[t] = 0;
        sq.dpos[t] = sq.dback[t] = 0;
        sq.dnext[t] = sq.dprev[t] = -1;
        mel_reset(t);                                /* (20: the melody from its start) */
        if (sq.tstr[t] == TSTR_ONE || !sq.tstr[t])
            sq.tfree &= (uint8_t)~(1u << t);
    }
    memset(sq.dq, 0, sizeof sq.dq);
    sq.lk_on = sq.lk_trig = 0;
    memset(sq.cocc, 0, sizeof sq.cocc);
    sq.pre = 0;
    sq.ptick = 0;
}
static void seq_reset_tracks(void) { seq_reset_tracks_keep(0); }
static void seq_pattern_activate(void)               /* seq_setPendingPatternActive */
{
    uint32_t t;
    if (sq.active != sq.pending || sq.load_pending) {
        uint32_t was = pat_voices(&pat[sq.active]), from = sq.active;
        seq_reset_tracks_keep(sq.running && !sq.realign_pat ? sq.tfree : 0u);   /* (1.0: TIME STRETCH's tracks
                                                      * realign at STOP, or with REALIGN at a pattern change) */
        sq.gsync = (1u << NTRACK) - 1u;              /* (the generators' live tracks from the new pattern) */
        sq.load_pending = 0;
        if (!pat_keep || pat_revert)
            pat[sq.active] = pat_backup;             /* unsaved changes of the old pattern go (the LXR's way) */
        pat_revert = 0;
        if (pat_blank(&pat[sq.pending])) {           /* (19a: a blank pattern starts with the scale last chosen) */
            pat[sq.pending].sroot = scl_last.root;
            pat[sq.pending].scale = scl_last.scale;
        }
        pat_backup = pat[sq.pending];
        sq.active = sq.pending;
        glfo_ev |= GLE_PATTERN;
        seq_set_shuffle_from(sq.active);
        pv_switch(was, from, sq.active);             /* (37: its voices; the LXR's KIT CHANGE AUTO is KIT's) */
        midi_out_pc(sq.active);
        for (t = 0; t < NTRACK; t++)
            midi_out_note_off(t);
    } else {
        pv_skip = 0;
    }
}
static void seq_set_next_pattern(uint32_t n)         /* seq_setNextPattern */
{
    if (n >= NUM_PATTERN)
        return;
    sq.pending = (uint8_t)n;
    sq.load_pending = 1;
    if (!sq.running)
        seq_pattern_activate();
}
/* 0.19: a song row also says how many times its pattern plays: the pattern in bits 0-4, the times in bits 5-7 (k:
 * SONG_REPS[k < 2 ? k : k - 1]; k = 2 never, so END and LOOP stay 64 and 65, the LXR's) */
#define NSONGREP 7u
static const uint8_t SONG_REPS[NSONGREP] = {1, 2, 3, 4, 6, 8, 16};
static uint32_t song_row(uint32_t v)                 /* a row's pattern, or SONG_END / SONG_LOOP */
{
    return v == SONG_END || v == SONG_LOOP ? v : (v >> 5) == 2u ? SONG_END : v & 31u;
}
static uint32_t song_rep_i(uint32_t s, uint32_t r)   /* its times: SONG_REPS's index */
{
    uint32_t k = songs[s % NUM_SONGS][r & 63u] >> 5;
    return song_row(songs[s % NUM_SONGS][r & 63u]) >= NUM_PATTERN ? 0u : k < 2u ? k : k - 1u;
}
static uint32_t song_reps(uint32_t s, uint32_t r) { return SONG_REPS[song_rep_i(s, r) % NSONGREP]; }
static void song_put(uint32_t s, uint32_t r, uint32_t v, uint32_t i)   /* row r: pattern (or END, LOOP) v, times i */
{
    i %= NSONGREP;
    songs[s % NUM_SONGS][r & 63u] = (uint8_t)(v >= NUM_PATTERN ? v : v | (i < 2u ? i : i + 1u) << 5);
}
/* 42: ROADS: a song played by its own chances. At a row's end, the rows that play this pattern (order 2: after the
 * pattern before it too, when any do), one of the rows after them: SURPRISE 0 the commonest, up the chances
 * flatter (now and then any row), its times one more or less; HOME: back to the first row every so many bars; a
 * pattern's fourth row in a row: another if the song has one. A row after the last (END, LOOP): the first */
#define NSONGHOME 8u
static const uint16_t SONG_HOME[NSONGHOME] = {0, 8, 16, 32, 64, 128, 256, 512};
static uint32_t song_len(uint32_t s)                 /* the rows before the first END or LOOP */
{
    uint32_t n = 0;
    while (n < MAX_SONG_LENGTH && song_row(songs[s % NUM_SONGS][n]) < NUM_PATTERN)
        n++;
    return n;
}
static uint32_t song_pat(uint32_t s, uint32_t r) { return song_row(songs[s % NUM_SONGS][r & 63u]); }
static uint32_t song_sur(uint32_t s) { return ((song_rd[s % NUM_SONGS] & 15u) ^ 4u) * 127u / 15u; }   /* 0..127 */
/* the row after row r (prev: the one before it, 0xFF none) by song s's chances; sur 0..127 (128: any row); avoid:
 * a pattern not to go to if another can be */
static uint32_t song_road(uint32_t s, uint32_t r, uint32_t prev, uint32_t sur, uint32_t avoid)
{
    uint8_t c[MAX_SONG_LENGTH];
    uint32_t n = song_len(s), nc = 0, i, j, o, best = 0, bn = 0;
    if (!n)
        return 0;
    if (r >= n || (seq_rand() & 127u) < sur)         /* (any row: SURPRISE's top, or from outside the song) */
        nc = 0;
    else
        for (o = 2; o && !nc; o--)                    /* (order 2, then 1) */
            for (i = 0; i < n; i++)
                if (song_pat(s, i) == song_pat(s, r) &&
                    (o == 1u || (prev < n && song_pat(s, i ? i - 1u : n - 1u) == song_pat(s, prev))) &&
                    song_pat(s, i + 1u < n ? i + 1u : 0u) != avoid)
                    c[nc++] = (uint8_t)(i + 1u < n ? i + 1u : 0u);
    if (!nc) {
        for (i = 0; i < n; i++)
            if (song_pat(s, i) != avoid || i + 1u == n)
                c[nc++] = (uint8_t)i;
        return c[seq_rand() % nc];
    }
    if (sur)                                         /* (by the chances: a row after one of them) */
        return c[seq_rand() % nc];
    for (i = 0, o = seq_rand() % nc; i < nc; i++) {  /* (0: the commonest next pattern, a tie by chance) */
        uint32_t k = (i + o) % nc, m = 0;
        for (j = 0; j < nc; j++)
            m += song_pat(s, c[j]) == song_pat(s, c[k]);
        if (m > bn) {
            bn = m;
            best = c[k];
        }
    }
    return best;
}
static uint32_t song_times(void)                     /* the playing row's times (ROADS: one more or less) */
{
    int32_t i = (int32_t)song_rep_i(sq.song, sq.song_pos) + sq.song_dn;
    return SONG_REPS[i < 0 ? 0 : i >= (int32_t)NSONGREP ? NSONGREP - 1u : (uint32_t)i];
}
static int song_roads(void) { return (song_rd[sq.song % NUM_SONGS] & 0x80u) && song_len(sq.song); }
static uint32_t seq_next_in_song(int advance)        /* seq_determineNextPattern (0.19: a row its times first) */
{
    uint32_t pos, s = sq.song % NUM_SONGS;
    if (!sq.song_mode)
        return sq.active;
    if (sq.song_pos < MAX_SONG_LENGTH && song_row(songs[s][sq.song_pos]) < NUM_PATTERN &&
        sq.song_n + 1u < song_times()) {
        if (advance)
            sq.song_n++;
        return song_row(songs[s][sq.song_pos]);
    }
    if (song_roads()) {                              /* (42: picked once, so a look ahead finds the same) */
        if (sq.song_nx >= MAX_SONG_LENGTH) {
            uint32_t h = SONG_HOME[(song_rd[s] >> 4) % NSONGHOME];
            sq.song_nx = (uint8_t)(sq.song_pos >= MAX_SONG_LENGTH || (h && sq.song_hb >= h) ? 0u
                                   : song_road(s, sq.song_pos, sq.song_prv, song_sur(s),
                                               sq.song_run >= 4u ? song_pat(s, sq.song_pos) : 0xFFu));
        }
        pos = sq.song_nx;
    } else {
        pos = (uint32_t)(uint8_t)(sq.song_pos + 1u);
        if (pos >= MAX_SONG_LENGTH)
            pos = 0;
    }
    if (advance) {
        sq.song_hb = (uint16_t)(pos ? sq.song_hb + (pat_ticks() + 383u) / 384u * (sq.song_n + 1u) : 0u);
        sq.song_run = (uint8_t)(sq.song_pos < MAX_SONG_LENGTH && song_pat(s, pos) == song_pat(s, sq.song_pos)
                                ? sq.song_run + 1u : 1u);
        sq.song_prv = sq.song_pos;
        sq.song_pos = (uint8_t)pos;
        sq.song_n = 0;
        sq.song_nx = 0xFF;
        sq.song_dn = (int8_t)(song_roads() && (seq_rand() & 255u) < song_sur(s) ? (seq_rand() & 2u) - 1 : 0);
    }
    return song_row(songs[s][pos]);
}

/* 37: what PLAY plays: the pattern (0) or the song (1). Switched to the song while playing, it goes on from the first
 * row with the pattern playing (none: from the song's first row, at the bar) */
static void seq_play_song(uint32_t on)
{
    uint32_t r;
    if (on && !sq.song_mode && sq.running) {
        sq.song_pos = 0xFF;
        sq.song_n = 0;
        sq.song_nx = 0xFF;
        sq.song_dn = 0;
        for (r = 0; r < MAX_SONG_LENGTH; r++)
            if (song_row(songs[sq.song][r]) == sq.active) {
                sq.song_pos = (uint8_t)r;
                break;
            }
    }
    sq.song_mode = (uint8_t)(on != 0u);
}
/* .. a kit slot loaded (LOAD, PRESETS) while a KIT pattern plays: the pattern points at it */
static void pv_loaded(uint32_t s)
{
    if (pat_voices(&pat[sq.active]) == PV_KIT && s < NUM_KITS)
        pat_set_voices(&pat[sq.active], PV_KIT, s);
}

static void seq_stop_now(void);
/* the pattern's end (the start, and each time round its longest track, in ticks): a pattern change? 0: the song
 * ended (stopped) */
static int seq_pattern_end(void)                     /* (seq_nextStep's master step 0) */
{
    if (sq.active == sq.pending) {
        uint32_t n = seq_next_in_song(1);
        if (n == SONG_END) {
            seq_stop_now();
            sq.pending = (uint8_t)(song_row(songs[sq.song][0]) < NUM_PATTERN ? song_row(songs[sq.song][0]) : 0u);
            sq.song_pos = 0;
            return 0;
        }
        if (n == SONG_LOOP || n >= NUM_PATTERN) {
            n = song_row(songs[sq.song][0]) < NUM_PATTERN ? song_row(songs[sq.song][0]) : 0u;
            sq.song_pos = 0;
        }
        sq.pending = (uint8_t)n;
    }
    seq_pattern_activate();
    return 1;
}

static void seq_gen_step(uint32_t t, uint32_t i);
static void seq_gen_loop(uint32_t t);
/* track t's next step (seq_nextStep, a track at a time: each at its clock) */
static void seq_track_step(uint32_t t)
{
    const pattern_t *p;
    uint32_t len, i, d;
    p = tpat(t);
    len = track_len(p, t);
    d = trk_dir(t);
    if (d == DIR_FWD) {
        i = (uint32_t)(sq.idx[t] + 1);
        if (i >= len || (i & 63u) == 0u) {
            i = 0;
            if (sq.idx[t] >= 0) {                    /* round again */
                sq.loops[t]++;
                seq_gen_loop(t);
                mel_realign(t);
            }
        }
        sq.dpos[t] = (uint8_t)i;
    } else if (sq.idx[t] < 0) {                      /* 0.10: the first step, by the direction */
        i = d == DIR_REV ? len - 1u : d == DIR_RAND ? seq_rand() % len : 0u;
        sq.dpos[t] = 0;
        sq.dback[t] = 0;
    } else {
        i = dir_after(t, (uint32_t)sq.idx[t], len, &sq.dback[t]);
        if (++sq.dpos[t] >= len) {                   /* round again: as many steps as the track has */
            sq.dpos[t] = 0;
            sq.loops[t]++;
            seq_gen_loop(t);
            mel_realign(t);
        }
    }
    if (d == DIR_RAND)                               /* (the next one drawn now: a negative shift sees it) */
        sq.dnext[t] = (int8_t)(seq_rand() % len);
    else if (d == DIR_WALK)
        sq.dnext[t] = (int8_t)(len < 2u ? 0u : (i + ((seq_rand() & 1u) ? 1u : len - 1u)) % len);
    sq.dprev[t] = sq.idx[t];
    sq.idx[t] = (int8_t)i;
    sq.nst[t]++;
    seq_gen_step(t, i);
    if (!(seq_mutes() & (1u << t)) && main_on(p, t, i)) {
        if (sq.erase && t == ui_track()) {
            main_set(&pat[sq.active], t, i, 0);
            if (p == &gpat)
                main_set(&gpat, t, i, 0);
        } else {
            sq.rnd[t][0] = sq.rnd[t][1];
            sq.rnd[t][1] = sq.rnd[t][2];
            sq.rnd[t][2] = (uint8_t)(noise_next() % 100u + 1u);
        }
    }
}
/* the position a tick on: the pattern's end, the steps that start; play: what sounds on it */
/* 1.0: TIME STRETCH: a tick of the clock moves each track by its speed (sq.trate): 0, 1 or 2 of its own ticks, a
 * pass each (at 100%, the one as before) */
static void seq_advance(int play)
{
    uint32_t t, k, pt = pat_ticks(), mk[2] = {0, 0};
    if (sq.ptick >= pt)
        sq.ptick = 0;
    if (sq.ptick == 0u && !sq.stut_on) {
        sq.prescale = 0;                             /* (the global step grid: the pattern's) */
        if (!seq_pattern_end())
            return;
    }
    if (sq.prescale == 0u) {
        sq.master_cnt++;
        sq.beat = (sq.ptick / 24u & 3u) == 0u;
    }
    for (t = 0; t < NTRACK; t++) {
        uint32_t r = sq.trate[t], a;
        if (sq.thome >> t & 1u) {                    /* (back into time: up to 2% slower or faster, then 100%) */
            int32_t c = sq.toff[t] > 1310 ? 1310 : sq.toff[t] < -1310 ? -1310 : sq.toff[t];
            r = (uint32_t)(65536 - c);
            if (!c) {
                sq.thome &= (uint8_t)~(1u << t);
                seq_stretch_set(t, TSTR_ONE);
            }
        }
        sq.toff[t] += (int32_t)r - 65536;
        a = sq.tacc[t] + r;
        sq.tacc[t] = (uint16_t)a;
        mk[0] |= (a >> 16 ? 1u : 0u) << t;
        mk[1] |= (a >> 17 ? 1u : 0u) << t;
    }
    for (k = 0; k < 2u && (!k || mk[k]); k++) {
        for (t = 0; t < NTRACK; t++)
            if ((mk[k] >> t & 1u) && sq.tph[t] == 0u)
                seq_track_step(t);
        if (play) {
            seq_flam(mk[k]);
            if (!k) {
                seq_delayed();
                seq_ratchets();
            }
        }
        if (!k)
            sq.ptick++;
        for (t = 0; t < NTRACK; t++)
            if ((mk[k] >> t & 1u) && ++sq.tph[t] >= trk_ticks(t))
                sq.tph[t] = 0;
    }
}

/* PERF's STUTTER: the position saved (stut_save) and back to it (stut_load) */
static void stut_save(void)
{
    memcpy(sq.stut.idx, sq.idx, sizeof sq.idx);
    memcpy(sq.stut.rnd, sq.rnd, sizeof sq.rnd);
    memcpy(sq.stut.tph, sq.tph, sizeof sq.tph);
    memcpy(sq.stut.loops, sq.loops, sizeof sq.loops);
    memcpy(sq.stut.dpos, sq.dpos, sizeof sq.dpos);
    memcpy(sq.stut.dback, sq.dback, sizeof sq.dback);
    memcpy(sq.stut.dnext, sq.dnext, sizeof sq.dnext);
    memcpy(sq.stut.dprev, sq.dprev, sizeof sq.dprev);
    memcpy(sq.stut.tacc, sq.tacc, sizeof sq.tacc);
    memcpy(sq.stut.toff, sq.toff, sizeof sq.toff);
    sq.stut.prescale = sq.prescale;
    sq.stut.master_cnt = sq.master_cnt;
    sq.stut.beat = sq.beat;
    sq.stut.ptick = sq.ptick;
}
static void stut_load(void)
{
    memcpy(sq.idx, sq.stut.idx, sizeof sq.idx);
    memcpy(sq.rnd, sq.stut.rnd, sizeof sq.rnd);
    memcpy(sq.tph, sq.stut.tph, sizeof sq.tph);
    memcpy(sq.loops, sq.stut.loops, sizeof sq.loops);
    memcpy(sq.dpos, sq.stut.dpos, sizeof sq.dpos);
    memcpy(sq.dback, sq.stut.dback, sizeof sq.dback);
    memcpy(sq.dnext, sq.stut.dnext, sizeof sq.dnext);
    memcpy(sq.dprev, sq.stut.dprev, sizeof sq.dprev);
    memcpy(sq.tacc, sq.stut.tacc, sizeof sq.tacc);
    memcpy(sq.toff, sq.stut.toff, sizeof sq.toff);
    sq.prescale = sq.stut.prescale;
    sq.master_cnt = sq.stut.master_cnt;
    sq.beat = sq.stut.beat;
    sq.ptick = sq.stut.ptick;
}
/* the position a tick on, nothing played (the ticks a stutter held back, when it ends) */
static void seq_tick_quiet(void)
{
    seq_advance(0);
    if (++sq.prescale >= 24u)
        sq.prescale = 0;
}
/* at a tick's start: STUTTER starts on its grid, loops, ends (the position goes on where it would have been) */
static void seq_stutter(void)
{
    if (sq.stut_req && !sq.stut_on && sq.stut_len && sq.ticks % sq.stut_len == 0u) {
        stut_save();
        sq.stut_on = 1;
        sq.stut_n = 0;
        sq.stut_ticks = 0;
    } else if (sq.stut_on && !sq.stut_req) {     /* from the saved position, the ticks since, unplayed */
        uint32_t k = sq.stut_ticks;
        sq.stut_on = 0;
        stut_load();
        while (k--)
            seq_tick_quiet();
    } else if (sq.stut_on && sq.stut_n >= sq.stut_len) {
        stut_load();
        sq.stut_n = 0;
    }
}

/* one tick of the 96-a-beat clock */
static void seq_tick_one(void)
{
    uint32_t t;
    seq_stutter();
    if (sq.running && sq.ticks % 384u == 0u)         /* (0.13: a bar, for the global LFOs) */
        glfo_ev |= GLE_BAR;
    if (sq.running)
        rush_now();                                  /* (39: RUSH's amount for this tick's hits) */
    seq_advance(1);
    if (!sq.running)
        return;
    seq_gates();
    rush_grid();
    if (sq.trdue && (!sq.trwhen || !(sq.ticks % (sq.trwhen == 1u ? 96u : 384u)))) {   /* (0.23-t7: TRANSPOSE on time) */
        memcpy(sq.trp, sq.trpend, sizeof sq.trp);
        sq.trdue = 0;
    }
    if (sq.roll && !sq.roll_free && sq.roll_rate && sq.roll_rate < 14u) {   /* rolls: on their own grid */
        uint32_t every = ROLL_TICKS[sq.roll_rate];
        if (sq.ticks % every == 0u)
            for (t = 0; t < NTRACK; t++)
                if (sq.roll & (1u << t)) {
                    uint32_t i = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
                    uint32_t note = snote(tpat(t), t, i);
                    seq_trigger(t, 0, 0, key_vel, note);
                    seq_add_note(t, key_vel, note);
                }
    }
    seq_croll_grid();                                /* (1.0: the remote's rolls a track) */
    sq.ticks++;
    if (sq.stut_on) {
        sq.stut_n++;
        sq.stut_ticks++;
    }
    if (++sq.prescale >= 24u)
        sq.prescale = 0;
}

/* -------------------------------------------------------- transport --- */
static void seq_start(int reset)                     /* seq_setRunning(1) (+ seq_resetPosition) */
{
    if (reset || !sq.running) {
        seq_reset_tracks();
        sq.prescale = 0;
        sq.master_cnt = 0;
        sq.ticks = 0;
        sq.due = 0;
        sq.hold = 0;
        sq.holding = 0;
        sq.acc_q16 = sq.tick_q16;                    /* the first tick at once (seq_resetPosition: deltaT 0) */
        sq.mclk_n = 0;
        sq.ext_left = 0;
        sq.ext_clk = 0;
        sq.stut_on = 0;
        if (sq.song_mode)
            sq.song_pos = sq.song_nx = 0xFF;         /* (seq_nextStep advances to 0) */
        sq.song_dn = 0;
        glfo_ev |= GLE_PLAY;
    }
    sq.running = 1;
    if (!sq.sync_ext)
        midi_out_rt(reset ? 0xFAu : 0xFBu);
}
static void seq_stop_now(void)
{
    uint32_t t;
    sq.running = 0;
    if (!sq.sync_ext)
        midi_out_rt(0xFCu);
    for (t = 0; t < NTRACK; t++) {
        midi_out_note_off(t);
        voice_hold(t, 0);                            /* (19b: what was held lets go) */
        sq.glen[t] = 0;
    }
    sq.gmidi = sq.tie = 0;
    seq_reset_tracks();
    sq.last_on = 0;
    sq.prescale = 0;
    sq.stut_on = 0;
    auto_clear_all();
}

/* ------------------------------------------------- the clock (ISR) --- */
static uint32_t seq_shuffle_delay(void)              /* seq_getDeltaTShuffleDelay: shuffle x 16 ticks, Q16 samples */
{
    return (uint32_t)(((uint64_t)sq.tick_q16 * 16u * par[PAR_SHUFFLE]) / 127u);
}
static void seq_play_due(int32_t elapsed)            /* the ticks given so far, held back by a shuffle */
{
    while (sq.due) {
        if (sq.prescale == 0u && (sq.master_cnt & 1u) && par[PAR_SHUFFLE] && sq.running) {
            if (!sq.holding) {
                sq.holding = 1;
                sq.hold = (int32_t)(seq_shuffle_delay() >> 16);
                elapsed = 0;
            }
            sq.hold -= elapsed;
            elapsed = 0;
            if (sq.hold > 0)
                return;
            sq.holding = 0;
        }
        sq.due--;
        seq_tick_one();
    }
}
/* MIDI clock out: on the internal clock's grid, every 4 ticks */
static void seq_clock_out_tick(void)
{
    if (sq.mclk_n++ % 4u == 0u)
        midi_out_rt(0xF8u);
}

/* the audio ISR, once a BLOCK, before the voices render it */
static void seq_gen_syncs(void);
static void seq_block(void)
{
    if (sq.gsync)                                    /* (the generators' live tracks the panel reset) */
        seq_gen_syncs();
    if (!sq.sync_ext) {
        sq.acc_q16 += BLOCK * tape_q16;             /* (TAPE STOP slows the clock) */
        while (sq.acc_q16 >= sq.tick_q16) {          /* (the clock goes out stopped too, as the LXR's) */
            sq.acc_q16 -= sq.tick_q16;
            if (sq.running && (!sq.half || (sq.hph ^= 1u)))   /* (HALF: every other tick) */
                sq.due++;
            seq_clock_out_tick();
        }
    } else {
        sq.ext_since += BLOCK;
        while (sq.ext_left && sq.ext_next <= 0) {    /* the pulse's later ticks, spread over its length */
            sq.ext_left--;
            if (sq.running && (!sq.half || (sq.hph ^= 1u)))
                sq.due++;
            sq.ext_next += (int32_t)(sq.ext_iv / 4u);
        }
        sq.ext_next -= BLOCK << 8;
    }
    if (sq.running) {
        seq_play_due(BLOCK);
        seq_bounces();
        seq_roll_sweep();
        seq_croll_free();
        seq_rush();
    } else {
        sq.ru_R = sq.ru_free = 0;
        sq.ru_hp = 0;
        sq.due = 0;
    }
}

/* MIDI sync: a clock pulse (24 a beat), start, continue, stop (midi.c, in the ISR) */
static void seq_ext_pulse(void)
{
    if (!sq.sync_ext)
        return;
    if (sq.ext_left && sq.running)                   /* the pulse came early: the last one's ticks now */
        sq.due += sq.ext_left;
    if (sq.ext_since > 0u && sq.ext_since < (uint32_t)FS) {
        uint32_t iv = sq.ext_since << 8;
        sq.ext_iv = sq.ext_iv ? sq.ext_iv + ((int32_t)(iv - sq.ext_iv) >> 3) : iv;
        sq.bpm_ext_x10 = (uint16_t)clamp32((int32_t)((uint64_t)FS * 600u * 256u / (uint32_t)(sq.ext_iv * 24u)), 100,
                                           3000);
        seq_tempo_changed();
    }
    sq.ext_since = 0;
    if (sq.running)
        sq.due++;
    sq.ext_left = 3;
    sq.ext_next = (int32_t)(sq.ext_iv / 4u);
    if (sq.running)
        seq_play_due(0);
}
static void seq_ext_transport(uint32_t st)
{
    if (!sq.sync_ext)
        return;
    if (st == 0xFAu) {
        seq_start(1);
    } else if (st == 0xFBu) {
        seq_start(0);
    } else if (st == 0xFCu) {
        seq_stop_now();
    }
}

/* ------------------------------------------------------- recording --- */
/* seq_addNote: a played note into the playing step, or the next one past its half (the LXR's quantisation) */
static void seq_add_note(uint32_t t, uint32_t vel, uint32_t note)
{
    pattern_t *p;
    uint32_t step, len, q;
    if (!sq.running || !sq.rec || t >= NTRACK)
        return;
    p = &pat[sq.active];
    len = track_len(p, t);
    step = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
    q = sq.tph[t] * 2u >= trk_ticks(t) ? step + 1u : step;
    if (q >= len)
        q = 0;
    if (q != step) {
        if (q == 0u && sq.active == sq.pending && sq.song_mode) {
            uint32_t n = seq_next_in_song(0);
            if (n < NUM_PATTERN)
                p = &pat[n];
        }
        sq.skip[t] = (int8_t)q;
    }
    p->step[t][q].note = note_keep(t, note);       /* (0.23-t7: as BASE plays it back) */
    p->step[t][q].volume = (uint8_t)(vel | STEP_ACTIVE_MASK);
    p->step[t][q].prob = (uint8_t)((p->step[t][q].prob & ~PROB_MASK) | 100u);   /* (19b: its length kept) */
    main_set(p, t, q, 1);
    if (p == &pat[sq.active] && gen_live(p, t)) {   /* (a generator's track: its live copy too) */
        gpat.step[t][q] = p->step[t][q];
        main_set(&gpat, t, q, 1);
    }
}
/* seq_recordAutomation: a knob turned while recording (the playing step of the track) or with a step held */
static void seq_record_automation(uint32_t t, uint32_t param, uint32_t val)
{
    uint32_t dest = par_to_autodest(param);
    if (sq.rec && sq.running && t < NTRACK && sq.idx[t] >= 0) {
        pattern_t *p = &pat[sq.active];
        uint32_t k = (uint32_t)sq.idx[t];
        if (main_on(p, t, k)) {
            lock_set(p, t, k, sq.lane, dest);
            if (sq.lane == 0u)
                p->step[t][k].p1v = (uint8_t)val;
            else
                p->step[t][k].p2v = (uint8_t)val;
            live_step_sync(p, t, k);
        }
    }
    if (sq.armed_step >= 0 && sq.armed_track >= 0) {
        pattern_t *p = &pat[ui_pattern()];
        uint32_t at = (uint32_t)sq.armed_track, k = (uint32_t)sq.armed_step;
        lock_set(p, at, k, sq.lane, dest);
        if (sq.lane == 0u)
            p->step[at][k].p1v = (uint8_t)val;
        else
            p->step[at][k].p2v = (uint8_t)val;
        live_step_sync(p, at, k);
    }
}

/* ------------------------------------------------------------- rolls --- */
static void seq_set_roll(uint32_t t, int on)         /* seq_setRoll */
{
    if (t >= NTRACK)
        return;
    if (on) {
        if (!sq.roll)
            sq.rl_cnt = 0;                           /* (swept: the first hit at once) */
        sq.roll |= (uint8_t)(1u << t);
        if ((sq.roll_rate == 0u && !sq.roll_free) || !sq.running) {   /* ONE, or stopped: a single hit */
            seq_trigger(t, 0, 0, key_vel, tb_note(t));
            seq_add_note(t, key_vel, tb_note(t));
        }
    } else {
        sq.roll &= (uint8_t)~(1u << t);
    }
}

/* --------------------------------------------------- song editing --- */
static void song_insert_row(uint32_t song, uint32_t pos)   /* seq_insertRow: the row repeated below (its times too) */
{
    uint32_t i;
    for (i = MAX_SONG_LENGTH - 1u; i > pos; i--)
        songs[song][i] = songs[song][i - 1u];
}
static void song_delete_row(uint32_t song, uint32_t pos)   /* seq_deleteRow */
{
    uint32_t i;
    for (i = pos; i < MAX_SONG_LENGTH - 1u; i++)
        songs[song][i] = songs[song][i + 1u];
    songs[song][MAX_SONG_LENGTH - 1u] = SONG_END;
}

/* ------------------------------------------------------- Euclid --- */
/* the Euclidean generator: n hits over len steps, evenly (Bjorklund), rotated; it rewrites the track's steps and
 * length (EuklidGenerator.c: the same rhythms, built directly). 0.10: its settings the track's (gen[t] at
 * GEN_EUCLID: length, hits, rotation), and 0 hits none. 0.13: a second pulse over the same length (eu2[t]: hits, 0
 * off, and rotation) joined to the first by EU_OR, EU_XOR, EU_AND or EU_SUB (the first's hits less the second's) */
enum { EU_OR, EU_XOR, EU_AND, EU_SUB, NEUOP };
static uint64_t euclid_bits(uint32_t len, uint32_t n, uint32_t rot)
{
    uint64_t bits = 0;
    uint32_t k;
    if (n > len)
        n = len;
    for (k = 0; k < len; k++)                        /* the hits where k x n / len steps over a whole number */
        if (((k * n) % len) < n)
            bits |= 1ull << k;
    rot %= len;
    if (rot)
        bits = ((bits << rot) | (bits >> (len - rot))) & (len == 64u ? ~0ull : ((1ull << len) - 1u));
    return bits;
}
static void euclid_generate(uint32_t t, pattern_t *p)
{
    uint32_t len = p->gen[t][1] ? p->gen[t][1] : 16u, k;
    const uint8_t *e = p->eu2[t];
    uint64_t bits, b2;
    if (len > NUM_STEPS)
        len = NUM_STEPS;
    bits = euclid_bits(len, p->gen[t][2], p->gen[t][3]);
    if (e[0]) {
        b2 = euclid_bits(len, e[0], e[1]);
        bits = e[2] == EU_XOR ? bits ^ b2 : e[2] == EU_AND ? bits & b2 : e[2] == EU_SUB ? bits & ~b2 : bits | b2;
    }
    for (k = 0; k < 4u; k++)
        p->main[t][k] = (uint16_t)(bits >> (16u * k));
    p->len[t] = (uint8_t)len;
}

/* ------------------------------------------------------ generators --- */
/* 0.5: a track's generator plays a live copy of the track (gpat's track t), which it changes as it plays; the
 * pattern stays as it was until COMMIT copies the live track into it (UNDO takes that back); RESET (or turning the
 * generator on, a pattern change) copies the pattern's track into the live one again. gen[t]: the mode, then
 *   MUTATE    amount (0..100: the chance of each of two changes a time round: a step flipped, a step moved to a
 *             free neighbour, a lock's (else the velocity's) value drifted +-8), -, FREEZE
 *   TURING    length (2..16: the register), lock (0: random .. 127: the loop repeats), density (0..127: the share
 *             of steps that play); each step shifts the register, its last bit back in, flipped at random by
 *             (127 - lock) / 254; the step plays if the register's value is below the density, its velocity from
 *             the value
 *   CELLULAR  rule (0..255, Wolfram's numbering: 30, 90, 110 ..), -, FREEZE; each time round the steps are the
 *             rule's next row of the last (the track's length, wrapping round); a row gone empty starts again
 *             from the pattern's
 *   GRIDS     part (BD SD HH), density (0..127), accent (the velocity from the map); the map's X, Y and chaos are
 *             the pattern's (gx gy gchaos). Emilie Gillet's Grids (grids/pattern_generator.cc EvaluateDrums,
 *             ReadDrumMap; GPL-3.0-or-later, here under GPL-3.0; the map: grids_map.h): the level of the part's step
 *             between the 4 nodes round X, Y, plus this time round's perturbation (up to chaos / 2, drawn at the
 *             track's first step), plays above 255 - density; accented above 192. Grids' bar is 32 steps (32nds):
 *             a step here reads its two, the second alone playing half a step late (the step's shift) */
static uint32_t gen_rand(uint32_t n) { return n ? (uint32_t)(hit_rand() + 32768) % n : 0u; }
static inline uint32_t u8mix(uint32_t a, uint32_t b, uint32_t bal) { return (a * (255u - bal) + b * bal) >> 8; }
/* Grids' ReadDrumMap: part's step (0..31) at x, y (0..255) */
static uint32_t grids_level(uint32_t part, uint32_t step, uint32_t x, uint32_t y)
{
    uint32_t i = x >> 6, j = y >> 6, o = part * 32u + step;
    uint32_t a = GRIDS_NODE[GRIDS_AT[i][j]][o], b = GRIDS_NODE[GRIDS_AT[i + 1u][j]][o];
    uint32_t c = GRIDS_NODE[GRIDS_AT[i][j + 1u]][o], d = GRIDS_NODE[GRIDS_AT[i + 1u][j + 1u]][o];
    return u8mix(u8mix(a, b, (x << 2) & 255u), u8mix(c, d, (x << 2) & 255u), (y << 2) & 255u);
}
static inline uint32_t u7to8(uint32_t v) { return (v << 1) | (v >> 6); }   /* 0..127 -> 0..255 */
/* GRIDS: step i of track t from the map (its first: this time round's perturbation) */
static void grids_step(uint32_t t, uint32_t i)
{
    const pattern_t *p = &pat[sq.active];
    const uint8_t *g = p->gen[t];
    uint32_t part = g[1] < 3u ? g[1] : 0u, x = u7to8(p->gx), y = u7to8(p->gy), s = (i & 15u) * 2u, k, hit[2];
    uint32_t thr = 255u - u7to8(gen_dens(t, g[2])), lv[2];
    if ((i & 15u) == 0u)
        sq.gpert[t] = (uint8_t)((gen_rand(256) * (u7to8(p->gchaos) >> 2)) >> 8);
    for (k = 0; k < 2u; k++) {
        lv[k] = grids_level(part, s + k, x, y);
        lv[k] = lv[k] < 255u - sq.gpert[t] ? lv[k] + sq.gpert[t] : 255u;
        hit[k] = lv[k] > thr && lv[k] > 0u;
    }
    main_set(&gpat, t, i, (int)(hit[0] || hit[1]));
    gpat.timing[t][i] = (uint8_t)((p->timing[t][i] & 0x0Fu) | (hit[0] || !hit[1] ? 7u : 11u) << 4);
    if (hit[0] || hit[1]) {
        uint32_t l = hit[0] ? lv[0] : lv[1];
        gpat.step[t][i].volume = (uint8_t)(g[3] ? (STEP_ACTIVE_MASK | (l > 192u ? 127u : 72u))
                                                : p->step[t][i].volume);
    }
}
static void seq_gen_sync(uint32_t t)                 /* the live track from the pattern's (the ISR) */
{
    const pattern_t *p = &pat[sq.active];
    uint32_t k;
    trk_copy(&gpat, p, t);
    mel_reset(t);
    memset(mel_n[t], 0, sizeof mel_n[t]);            /* (what played: COMMIT's, until then) */
    sq.greg[t] = 0;
    for (k = 0; k < 16u; k++)                        /* TURING: the register from the pattern's first steps */
        sq.greg[t] |= (uint16_t)(main_on(p, t, k) << k);
    if (!sq.greg[t])
        sq.greg[t] = (uint16_t)(hit_rand() | 1);
    if (p->gen[t][0] == GEN_GRIDS)                   /* (the whole track from the map at once: the grid shows it) */
        for (k = 0; k < track_len(p, t); k++)
            grids_step(t, k);
}
static void seq_gen_syncs(void)                     /* what the panel asked for (sq.gsync) */
{
    uint32_t t, m = sq.gsync;
    sq.gsync = 0;
    for (t = 0; t < NTRACK; t++)
        if (m & (1u << t))
            seq_gen_sync(t);
}
static void seq_gen_step(uint32_t t, uint32_t i)     /* step i of track t starts: TURING decides it */
{
    const uint8_t *g = pat[sq.active].gen[t];
    uint32_t n, mask, bit, v, d;
    if (g[0] == GEN_GRIDS) {
        grids_step(t, i);
        return;
    }
    if (g[0] != GEN_TURING)
        return;
    n = g[1] < 2u ? 2u : g[1] > 16u ? 16u : g[1];
    mask = (1u << n) - 1u;
    bit = (sq.greg[t] >> (n - 1u)) & 1u;
    if (gen_rand(254) < 127u - (g[2] > 127u ? 127u : g[2]))
        bit ^= 1u;
    sq.greg[t] = (uint16_t)(((sq.greg[t] << 1) | bit) & mask);
    v = (uint32_t)(sq.greg[t] & mask) * 127u / mask;   /* 0..127 */
    d = gen_dens(t, g[3]);
    main_set(&gpat, t, i, v < d);
    if (v < d)
        gpat.step[t][i].volume = (uint8_t)(STEP_ACTIVE_MASK | (50u + (d ? v * 77u / d : 0u)));
}
static void seq_gen_loop(uint32_t t)                 /* track t round again: MUTATE, CELLULAR */
{
    const uint8_t *g = pat[sq.active].gen[t];
    pattern_t *p = &gpat;
    uint32_t len = track_len(p, t), k, i;
    if (g[0] == GEN_MUTATE && !g[3]) {
        for (k = 0; k < 2u; k++) {
            uint32_t s = gen_rand(len), op = gen_rand(3);
            if (gen_rand(100) >= g[1])
                continue;
            if (op == 0u) {                          /* flip */
                main_set(p, t, s, !main_on(p, t, s));
            } else {
                for (i = 0; i < len && !main_on(p, t, s); i++)   /* (an on step: from s on) */
                    s = (s + 1u) % len;
                if (!main_on(p, t, s))
                    continue;
                if (op == 1u) {                      /* move to a free neighbour */
                    uint32_t d = gen_rand(2) ? (s + 1u) % len : (s + len - 1u) % len;
                    if (main_on(p, t, d))
                        continue;
                    p->step[t][d] = p->step[t][s];
                    p->timing[t][d] = p->timing[t][s];
                    p->lhi[t][d] = p->lhi[t][s];
                    p->cond[t][d] = p->cond[t][s];
                    p->rat[t][d] = p->rat[t][s];
                    main_set(p, t, d, 1);
                    main_set(p, t, s, 0);
                } else {                             /* drift a lock's value, else the velocity */
                    step_t *st = &p->step[t][s];
                    int32_t dv = (int32_t)gen_rand(17) - 8;
                    if (st->p1n != NO_AUTOMATION || p->lhi[t][s] & 3u)
                        st->p1v = (uint8_t)clamp32((int32_t)st->p1v + dv, 0, 127);
                    else if (st->p2n != NO_AUTOMATION || p->lhi[t][s] >> 4 & 3u)
                        st->p2v = (uint8_t)clamp32((int32_t)st->p2v + dv, 0, 127);
                    else
                        st->volume = (uint8_t)((st->volume & STEP_ACTIVE_MASK) |
                                               clamp32((int32_t)(st->volume & STEP_VOLUME_MASK) + dv, 1, 127));
                }
            }
        }
    } else if (g[0] == GEN_CA && !g[3]) {
        uint64_t row = 0, nr = 0;
        for (k = 0; k < len; k++)
            row |= (uint64_t)main_on(p, t, k) << k;
        for (k = 0; k < len; k++) {
            uint32_t l = (uint32_t)(row >> ((k + len - 1u) % len)) & 1u, c = (uint32_t)(row >> k) & 1u;
            uint32_t r = (uint32_t)(row >> ((k + 1u) % len)) & 1u;
            nr |= (uint64_t)((g[1] >> (l << 2 | c << 1 | r)) & 1u) << k;
        }
        if (!nr)                                     /* (died out: from the pattern's row again, else a cell) */
            for (k = 0; k < len; k++)
                nr |= (uint64_t)main_on(&pat[sq.active], t, k) << k;
        if (!nr)
            nr = 1;
        for (k = 0; k < len; k++)
            main_set(p, t, k, (int)((nr >> k) & 1u));
    }
}

/* the panel's actions on track t of the shown pattern (the main loop): UNDO keeps the track as it was */
static int8_t undo_t = -1;
static uint8_t undo_p;
static uint8_t dundo;
static void seq_undo_save(uint32_t t)
{
    dundo = 0;                                       /* (31: upat a track's now, not DENSITY's pattern) */
    trk_copy(&upat, &pat[sq.active], t);
    undo_t = (int8_t)t;
    undo_p = sq.active;
}
static int seq_undo(uint32_t t)                      /* the track and its saved copy swapped (UNDO again: redo) */
{
    pattern_t *p = &pat[sq.active];
    uint8_t g[4], m[NML];
    if (undo_t != (int8_t)t || undo_p != sq.active)
        return 0;
    memcpy(g, p->gen[t], 4);                         /* (the generator's settings stay as they are, the melody's) */
    memcpy(m, p->mel[t], NML);
    trk_copy(&gpat, p, t);                           /* (gpat: a scratch track for the swap, synced below) */
    trk_copy(p, &upat, t);
    trk_copy(&upat, &gpat, t);
    memcpy(p->gen[t], g, 4);
    memcpy(p->mel[t], m, NML);
    sq.gsync |= (uint8_t)(1u << t);
    return 1;
}
static void seq_commit(uint32_t t)                   /* COMMIT: the live track into the pattern; 20: the melody's
                                                      * notes and lengths as they played into its steps */
{
    pattern_t *p = &pat[sq.active];
    uint8_t g[4], m[NML];
    uint32_t k;
    if (!gen_live(p, t) && !mel_on(p, t))
        return;
    seq_undo_save(t);
    memcpy(g, p->gen[t], 4);
    memcpy(m, p->mel[t], NML);
    if (gen_live(p, t))
        trk_copy(p, &gpat, t);
    for (k = 0; k < NUM_STEPS; k++)
        if (mel_n[t][k] & 0x80u) {
            p->step[t][k].note = (uint8_t)(mel_n[t][k] & 127u);
            step_len_set(p, t, k, mel_l[t][k]);
            if (gen_live(p, t))
                gpat.step[t][k].note = p->step[t][k].note;
        }
    memcpy(p->gen[t], g, 4);
    memcpy(p->mel[t], m, NML);
}
/* 31: DENSITY's COMMIT (OCT+ on its page): what it does now written into the playing pattern, as this time round
 * plays it (VARY's draw): its dropped steps off, its chances up, its ghosts as steps (the track's last note, its
 * velocity), its ratchets; DENSITY then goes back to 0, as it sounded. The whole pattern is kept for its UNDO (OCT+
 * again, DENSITY still at 0: the pattern and DENSITY as they were; again: redo). Returns the steps it changed */
static uint8_t dundo_p;                              /* (dundo: 1 a COMMIT to undo, 2 to redo; its pattern) */
static uint32_t dens_write(pattern_t *p)             /* DENSITY now, into p (the playing pattern) */
{
    uint32_t t, s, n = 0;
    for (t = 0; t < NTRACK; t++) {
        uint32_t len = track_len(p, t), lp = sq.loops[t];
        if (!dens_on(t))
            continue;
        for (s = 0; s < len; s++) {
            step_t *x = &p->step[t][s];
            if (!main_on(p, t, s)) {
                if (!dens_ghost(p, t, s, lp))
                    continue;
                main_set(p, t, s, 1);                /* (a ghost: a plain step, as a recorded one) */
                x->note = note_keep(t, sq.lnote[t]);
                x->volume = (uint8_t)(dens_ghost_vel(t, s, lp) | STEP_ACTIVE_MASK);
                x->prob = 100u;                      /* (its length AUTO, no condition) */
                lock_set(p, t, s, 0, NO_AUTOMATION);
                lock_set(p, t, s, 1, NO_AUTOMATION);
                p->cond[t][s] = 0;
                p->rat[t][s] = 0;
                p->timing[t][s] = 7u << 4;
                n++;
            } else if (dens_drop(p, t, s, lp)) {
                main_set(p, t, s, 0);
                n++;
            } else {
                uint32_t pr = dens_prob(p, t, s);
                if (pr > 100u)
                    pr = 100u;
                if (pr != step_prob(x)) {
                    x->prob = (uint8_t)((x->prob & ~PROB_MASK) | pr);
                    n++;
                }
                if (!(p->rat[t][s] & 7u) && !(p->rat[t][s] & RAT_BOUNCE) && dens_rat(p, t, s, lp)) {
                    p->rat[t][s] = (uint8_t)((p->rat[t][s] & ~7u) | 1u);   /* (2 hits, 1/32: as it played) */
                    n++;
                }
            }
        }
        if (gen_live(p, t))                          /* (a live generator: from the pattern again) */
            sq.gsync |= (uint8_t)(1u << t);
    }
    return n;
}
static uint32_t seq_dens_commit(void)
{
    uint32_t n;
    upat = pat[sq.active];
    undo_t = -1;                                     /* (upat holds the pattern now: no track's UNDO) */
    sq.wonly = 2;
    n = dens_write(&pat[sq.active]);
    sq.wonly = 0;
    dundo = 1;
    dundo_p = sq.active;
    return n;
}
/* 40: WANDER's KEEP: its Energy (each track's own DENSITY) written into the playing pattern as DENSITY's COMMIT
 * writes the knob's, and the tracks it muted muted (no UNDO: here is home now). Returns the steps it changed */
static uint32_t seq_wander_commit(void)
{
    uint32_t n;
    sq.wonly = 1;
    n = dens_write(&pat[sq.active]);
    sq.wonly = 0;
    memset(sq.wdens, 0, sizeof sq.wdens);
    sq.muted |= sq.wmute;
    sq.wmute = 0;
    return n;
}
static int seq_dens_undo(void)                       /* the pattern and the kept copy swapped */
{
    if (!dundo || dundo_p != sq.active)
        return 0;
    gpat = pat[sq.active];                           /* (gpat: scratch for the swap, then synced) */
    pat[sq.active] = upat;
    upat = gpat;
    dundo = (uint8_t)(3u - dundo);
    sq.gsync = (uint8_t)((1u << NTRACK) - 1u);
    return 1;
}
/* DICE: the track again, at random: Euclid's length, its number of hits anywhere */
static void seq_dice(uint32_t t)
{
    pattern_t *p = &pat[sq.active];
    uint32_t len = p->gen[t][1] ? p->gen[t][1] : 16u, n = p->gen[t][2], k;
    seq_undo_save(t);
    if (len > NUM_STEPS)
        len = NUM_STEPS;
    if (p->eu2[t][0])                                /* (0.13: two pulses joined: as many hits as they made) */
        for (k = n = 0; k < len; k++)
            n += (uint32_t)main_on(p, t, k);
    if (n > len)
        n = len;
    for (k = 0; k < 4u; k++)
        p->main[t][k] = 0;
    p->len[t] = (uint8_t)len;
    while (n) {
        uint32_t s = gen_rand(len);
        if (!main_on(p, t, s)) {
            main_set(p, t, s, 1);
            n--;
        }
    }
    sq.gsync |= (uint8_t)(1u << t);
}

/* ------------------------------------------------------------ init --- */
static void seq_init(void)                           /* seq_init */
{
    uint32_t i, t;
    for (i = 0; i < NUM_SONGS; i++)
        for (t = 0; t < MAX_SONG_LENGTH; t++)
            songs[i][t] = SONG_END;
    songs[0][0] = 0;
    memset(song_rd, 0, sizeof song_rd);              /* (42: PLAY, as written) */
    sq.song_nx = 0xFF;
    for (i = 0; i < NUM_PATTERN; i++) {
        seq_clear_pattern(&pat[i]);
        pat[i].shuffle = 0;
        pat[i].kit = 0;
        pat[i].gx = pat[i].gy = pat[i].gchaos = 0;
    }
    pat_backup = pat[0];
    for (t = 0; t < NTRACK; t++) {
        sq.idx[t] = -1;
        sq.skip[t] = -1;
    }
    sq.armed_step = sq.armed_track = -1;
    sq.roll_rate = 5;                                /* 1/16 */
    rush_defaults();                                 /* 39 */
    memset(sq.ru_rk, -1, sizeof sq.ru_rk);
    memset(sq.lnote, SEQ_DEFAULT_NOTE, sizeof sq.lnote);
    sq.dghost = 40;
    for (t = 0; t < NTRACK; t++)                     /* (1.0: TIME STRETCH, as the clock) */
        seq_stretch_set(t, TSTR_ONE);
    seq_reset_tracks();
    sq.gsync = (1u << NTRACK) - 1u;
    seq_tempo_changed();
}
