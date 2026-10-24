/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the LXR-02's sequencer. 32 patterns (the LXR's 64, halved for RAM: 3.6 KB each) of 7 tracks x 64 steps (4
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
#define ROLL_VOLUME 100u
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
    uint8_t bnc[NTRACK];                         /* 0.6: its bounces: start (BNC_TICKS's index) | pitch + 12 << 2 */
    uint8_t shuffle, kit;
    uint8_t gx, gy, gchaos;                      /* GRIDS: the map's X, Y and its chaos, 0..127 (the pattern's) */
    uint8_t link[8][4];                          /* 0.6: track coupling (seq_links): src | tgt << 3 | event << 6,
                                                  * effect | window << 4, amount (int8), 0; effect 0: no link */
    uint8_t cdepth;                              /* the links' depth, 0..127 (127: as set) */
} pattern_t;
#define NLINK 8u

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
    uint8_t rat, shift, drop, on, held;
} lmod_t;
/* trig conditions: none, FILL, not FILL, PRE, not PRE, NEI, not NEI, 1ST, not 1ST, then A:B for B 2..8, A 1..B */
enum { COND_NONE, COND_FILL, COND_NFILL, COND_PRE, COND_NPRE, COND_NEI, COND_NNEI, COND_1ST, COND_N1ST, COND_AB };
#define NCOND (COND_AB + 35u)
/* the generators */
enum { GEN_OFF, GEN_MUTATE, GEN_TURING, GEN_CA, GEN_GRIDS, NGEN };

static pattern_t pat[NUM_PATTERN] POOL;
static pattern_t pat_backup POOL;   /* the playing pattern as loaded (or saved) */
static pattern_t gpat POOL;         /* the generators' live tracks: track t of it plays when t's generator is on */
static pattern_t upat POOL;         /* UNDO: a track as it was before DICE, COMMIT, EUCLID (undo_t, undo_p) */
static uint8_t songs[NUM_SONGS][MAX_SONG_LENGTH] POOL;

static struct {
    uint8_t running, active, pending, load_pending;
    uint8_t song_mode, song, song_pos;
    int8_t idx[NTRACK];                          /* the playing step (-1: before the first) */
    uint8_t prescale, master_cnt;
    uint8_t muted, roll, roll_rate, last_on;
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
    } stut;                                      /* the position the loop goes back to */
    /* 0.5: the tracks' clocks, conditions, ratchets, generators */
    uint8_t tph[NTRACK];                         /* ticks into the track's step (its clock: CLK_TICKS) */
    uint16_t ptick;                              /* ticks into the pattern (its end: pat_ticks) */
    uint16_t loops[NTRACK];                      /* the track's times round since the start (A:B, 1ST) */
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
    /* 0.6: the links (seq_links): each track's steps since the start; a link's window (target steps), armed;
     * a TRIGGER waiting for its step; each track's occurrence its events and changes were worked out for */
    uint32_t nst[NTRACK];
    uint32_t lk_from[NLINK], lk_to[NLINK];
    uint8_t lk_on, lk_trig;
    uint32_t locc[NTRACK];
    lmod_t lmod[NTRACK];
    uint8_t dly[NTRACK], dly_s[NTRACK];          /* SHIFT: a hit held back (ticks to go), its step, loop, changes */
    uint32_t dly_loop[NTRACK];
    lmod_t dly_m[NTRACK];
    uint8_t ldec;                                /* bit t: track t's lane 2 holds a link's DECAY */
    volatile uint8_t gsync;                      /* bit t: the live track from the pattern (the ISR does it) */
    uint16_t greg[NTRACK];                       /* TURING's registers */
    uint8_t gpert[NTRACK];                       /* GRIDS: this time round's perturbation (its chaos) */
} sq;

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
 * bits in lhi (Kettle's parameters, 302 and on) */
static uint32_t lock_get(const pattern_t *p, uint32_t t, uint32_t k, uint32_t lane)
{
    const step_t *s = &p->step[t][k];
    return (lane ? s->p2n : s->p1n) | ((p->lhi[t][k] >> (lane * 4u)) & 15u) << 8;
}
static void lock_set(pattern_t *p, uint32_t t, uint32_t k, uint32_t lane, uint32_t code)
{
    step_t *s = &p->step[t][k];
    if (lane)
        s->p2n = (uint8_t)code;
    else
        s->p1n = (uint8_t)code;
    p->lhi[t][k] = (uint8_t)((p->lhi[t][k] & (lane ? 0x0Fu : 0xF0u)) | ((code >> 8) & 15u) << (lane * 4u));
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
    p->bnc[t] = BNC_DEFAULT;
}
static void seq_clear_pattern(pattern_t *p)
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++)
        seq_clear_track(t, p);
    memset(p->link, 0, sizeof p->link);
    p->cdepth = 127;
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
        lfo_recalc_sync();
    }
}

/* -------------------------------------------------------- triggering --- */
/* seq_triggerVoice: track t's voice, with step k's locks of pattern p (p 0: none, a roll) */
static void seq_trigger(uint32_t t, const pattern_t *p, uint32_t k, uint32_t vel, uint32_t note)
{
    const step_t *s = p ? &p->step[t][k] : 0;
    auto_set(t, 0, s ? lock_get(p, t, k, 0) : NO_AUTOMATION, s ? s->p1v : 0u);
    auto_set(t, 1, s ? lock_get(p, t, k, 1) : NO_AUTOMATION, s ? s->p2v : 0u);
    midi_out_note_off(t);
    voice_trigger(t, vel, note);
    midi_out_note(t, note, vel);
}

/* one of the 24 ticks of a step: a 24-bit pattern, the MSB tick 0 (flamIdxToPattern) */
static const uint32_t FLAM_PAT[6] = {0x800000u, 0x800800u, 0x808080u, 0x820820u, 0x888888u, 0x924924u};
enum { POS_PREV, POS_CUR, POS_NEXT };

/* the pattern track t plays from: its generator's live copy (gpat) while the generator is on */
static pattern_t *tpat(uint32_t t)
{
    pattern_t *p = &pat[sq.active];
    return p->gen[t][0] != GEN_OFF ? &gpat : p;
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
    uint32_t c = p->cond[t][s], r, n = t ? t - 1u : NTRACK - 1u;
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

static uint32_t shift_flam(const pattern_t *p, uint32_t t, uint32_t s, uint32_t pos, uint32_t loop)
{                                                    /* seq_getShiftFlamPattern */
    uint32_t tm, pt;
    int32_t sh;
    if (!main_on(p, t, s))
        return 0;
    if (sq.rnd[t][pos == POS_PREV ? 0 : pos == POS_CUR ? 1 : 2] > p->step[t][s].prob && !sq.fill)
        return 0;
    if (!cond_ok(p, t, s, loop))
        return 0;
    tm = p->timing[t][s];
    pt = FLAM_PAT[(tm & 15u) < 6u ? tm & 15u : 0u];
    sh = ((int32_t)(tm >> 4) - 7) * 3;              /* 8 shifts a step, 24 ticks: x3 */
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
static void step_hit(uint32_t t, const pattern_t *p, uint32_t s, uint32_t loop, const lmod_t *m)
{
    uint32_t r = p->rat[t][s], n, vel = p->step[t][s].volume & STEP_VOLUME_MASK, note = p->step[t][s].note;
    uint32_t occ = (loop << 6 | s) + 1u;
    if (m && m->on) {
        if (m->rat && !(r & 7u))                     /* (RATCHET: the step's own ratchet first) */
            r = m->rat >= 5u ? RAT_BOUNCE | 5u | BC_BALL << 3 : m->rat;
        vel = (uint32_t)clamp32((int32_t)vel + m->vel, 1, 127);
        note = (uint32_t)clamp32((int32_t)note + m->pitch, 0, 127);
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
        sq.rcnt[t] = RAT_TICKS[(r >> 3) & 3u];
        sq.rstep[t] = (uint8_t)s;
        sq.rvel[t] = (uint8_t)vel;
        if (((r >> 5) & 3u) == RF_UP)
            vel = vel / n;
    }
    sq.last_on |= 1u << t;
    seq_trigger(t, p, s, vel ? vel : 1u, note);
}
static void seq_ratchets(void)                       /* the ratchets' later hits, on their own rate */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        const pattern_t *p;
        uint32_t r, j, v;
        if (!sq.rleft[t] || --sq.rcnt[t])
            continue;
        p = tpat(t);
        r = p->rat[t][sq.rstep[t]];
        sq.rcnt[t] = RAT_TICKS[(r >> 3) & 3u];
        j = sq.rn[t] - sq.rleft[t];                  /* this hit's number (1 ..) */
        sq.rleft[t]--;
        v = sq.rvel[t];
        if (((r >> 5) & 3u) == RF_DOWN)
            v = v * (sq.rn[t] - j) / sq.rn[t];
        else if (((r >> 5) & 3u) == RF_UP)
            v = v * (j + 1u) / sq.rn[t];
        if (!(sq.muted & (1u << t)))
            seq_trigger(t, p, sq.rstep[t], v ? v : 1u, p->step[t][sq.rstep[t]].note);
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
            if (!(sq.muted & (1u << t)))
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
            uint32_t note = tpat(t)->step[t][i].note;
            seq_trigger(t, 0, 0, ROLL_VOLUME, note);
            seq_add_note(t, ROLL_VOLUME, note);
        }
}

/* 0.6: the links. Step s of track t is due to play (on, its chance, its condition), the SILENT event's test */
static int step_live(const pattern_t *p, uint32_t t, uint32_t s, uint32_t loop)
{
    return main_on(p, t, s) && (sq.rnd[t][1] <= p->step[t][s].prob || sq.fill) && cond_ok(p, t, s, loop);
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
        if ((trig >> t & 1u) && !(sq.muted >> t & 1u)) {
            uint32_t i = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
            sq.last_on |= 1u << t;
            seq_trigger(t, 0, 0, tvel[t], tpat(t)->step[t][i].note);
        }
}

/* what plays on this tick: each track at its clock's position in its step (tph: the 24 positions of a step that
 * this tick covers, slower clocks covering one now and then, faster several). 0.6: the hits found first, then
 * the links (with any set and a depth), then they play */
static void seq_flam(void)
{
    uint32_t t, hm = 0, sil = 0, live = pat[sq.active].cdepth && links_set(&pat[sq.active]);
    uint8_t hs[NTRACK];
    uint32_t hl[NTRACK];
    int8_t hp[NTRACK];
    for (t = 0; t < NTRACK; t++) {
        const pattern_t *p = tpat(t);
        uint32_t len, cur, nxt, prv, L = trk_ticks(t), lo, hi, bits, lp = sq.loops[t];
        lo = sq.tph[t] * 24u / L;
        hi = (sq.tph[t] + 1u) * 24u / L;
        bits = hi > lo ? ((1u << (hi - lo)) - 1u) << (24u - hi) : 0u;
        len = track_len(p, t);
        cur = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
        if (live && sq.tph[t] == 0u && ((sq.muted & (1u << t)) || !step_live(p, t, cur, lp)))
            sil |= 1u << t;
        if ((sq.muted & (1u << t)) || !bits)
            continue;
        nxt = (cur + 1u) % len;
        prv = cur ? cur - 1u : len - 1u;
        if (shift_flam(p, t, nxt, POS_NEXT, nxt ? lp : lp + 1u) & bits) {
            hs[t] = (uint8_t)nxt;
            hl[t] = nxt ? lp : lp + 1u;
            hp[t] = 1;
        } else if (shift_flam(p, t, cur, POS_CUR, lp) & bits) {
            if (sq.skip[t] == (int8_t)cur) {         /* recorded into this step a moment ago: it sounded already */
                sq.skip[t] = -1;
                continue;
            }
            hs[t] = (uint8_t)cur;
            hl[t] = lp;
            hp[t] = 0;
        } else if (shift_flam(p, t, prv, POS_PREV, cur || !lp ? lp : lp - 1u) & bits) {
            hs[t] = (uint8_t)prv;
            hl[t] = cur || !lp ? lp : lp - 1u;
            hp[t] = -1;
        } else {
            if (sq.last_on & (1u << t)) {
                sq.last_on &= (uint8_t)~(1u << t);
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
        if (!(hm >> t & 1u) || (m && m->drop))
            continue;
        if (m && m->shift) {                         /* SHIFT: the occurrence's first hit held back, later ones not */
            if (!m->held) {
                if (sq.dly[t]) {                     /* (one held already: it plays now) */
                    sq.dly[t] = 0;
                    step_hit(t, tpat(t), sq.dly_s[t], sq.dly_loop[t], &sq.dly_m[t]);
                }
                sq.dly[t] = (uint8_t)(m->shift * trk_ticks(t) / 200u + 1u);
                sq.dly_s[t] = hs[t];
                sq.dly_loop[t] = hl[t];
                sq.dly_m[t] = *m;
                sq.dly_m[t].shift = 0;
                sq.lmod[t].held = 1;
            }
            continue;
        }
        step_hit(t, tpat(t), hs[t], hl[t], m);
    }
}
static void seq_delayed(void)                        /* SHIFT's held hits, when due */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++)
        if (sq.dly[t] && !--sq.dly[t])
            step_hit(t, tpat(t), sq.dly_s[t], sq.dly_loop[t], &sq.dly_m[t]);
}

/* the rolls (PERF: the keys roll their track at ROLL's rate): ticks between two hits */
static const uint16_t ROLL_TICKS[14] = {0, 384, 192, 96, 48, 24, 16, 12, 8, 6, 3, 128, 64, 32};
static void seq_add_note(uint32_t t, uint32_t vel, uint32_t note);

/* ------------------------------------------------------- patterns --- */
/* track t of s into d: its steps and all of its settings */
static void trk_copy(pattern_t *d, const pattern_t *s, uint32_t t)
{
    memcpy(d->step[t], s->step[t], sizeof d->step[t]);
    memcpy(d->timing[t], s->timing[t], sizeof d->timing[t]);
    memcpy(d->lhi[t], s->lhi[t], sizeof d->lhi[t]);
    memcpy(d->cond[t], s->cond[t], sizeof d->cond[t]);
    memcpy(d->rat[t], s->rat[t], sizeof d->rat[t]);
    memcpy(d->main[t], s->main[t], sizeof d->main[t]);
    memcpy(d->gen[t], s->gen[t], sizeof d->gen[t]);
    d->len[t] = s->len[t];
    d->clk[t] = s->clk[t];
    d->bnc[t] = s->bnc[t];
}
static void seq_gen_sync(uint32_t t);
static void seq_reset_tracks(void)                   /* every track before its first step, its counts from 0 */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        sq.idx[t] = -1;
        sq.tph[t] = 0;
        sq.loops[t] = 0;
        sq.rleft[t] = 0;
        sq.rocc[t] = 0;
        sq.bleft[t] = 0;
        sq.bocc[t] = 0;
        sq.nst[t] = 0;
        sq.locc[t] = 0;
        sq.dly[t] = 0;
    }
    sq.lk_on = sq.lk_trig = 0;
    memset(sq.cocc, 0, sizeof sq.cocc);
    sq.pre = 0;
    sq.ptick = 0;
}
static void seq_pattern_activate(void)               /* seq_setPendingPatternActive */
{
    uint32_t t;
    if (sq.active != sq.pending || sq.load_pending) {
        seq_reset_tracks();
        sq.gsync = (1u << NTRACK) - 1u;              /* (the generators' live tracks from the new pattern) */
        sq.load_pending = 0;
        pat[sq.active] = pat_backup;                 /* unsaved changes of the old pattern go (the LXR's way) */
        pat_backup = pat[sq.pending];
        sq.active = sq.pending;
        seq_set_shuffle_from(sq.active);
        if (par[PAR_PATTERN_MODE] == 2u)             /* SKIP_NEXT_KIT_AUTOLOAD (saving to a new slot) */
            par[PAR_PATTERN_MODE] = 1;
        else if (par[PAR_PATTERN_MODE] == 1u)        /* KIT CHANGE AUTO: the pattern's kit */
            kit_load_ram(pat[sq.active].kit);
        midi_out_pc(sq.active);
        for (t = 0; t < NTRACK; t++)
            midi_out_note_off(t);
    } else if (par[PAR_PATTERN_MODE] == 2u) {
        par[PAR_PATTERN_MODE] = 1;
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
static uint32_t seq_next_in_song(int advance)        /* seq_determineNextPattern */
{
    uint32_t pos;
    if (!sq.song_mode)
        return sq.active;
    pos = (uint32_t)(uint8_t)(sq.song_pos + 1u);
    if (pos >= MAX_SONG_LENGTH)
        pos = 0;
    if (advance)
        sq.song_pos = (uint8_t)pos;
    return songs[sq.song][pos];
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
            sq.pending = songs[sq.song][0] < NUM_PATTERN ? songs[sq.song][0] : 0u;
            sq.song_pos = 0;
            return 0;
        }
        if (n == SONG_LOOP || n >= NUM_PATTERN) {
            n = songs[sq.song][0] < NUM_PATTERN ? songs[sq.song][0] : 0u;
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
    uint32_t len, i;
    p = tpat(t);
    len = track_len(p, t);
    i = (uint32_t)(sq.idx[t] + 1);
    if (i >= len || (i & 63u) == 0u) {
        i = 0;
        if (sq.idx[t] >= 0) {                        /* round again */
            sq.loops[t]++;
            seq_gen_loop(t);
        }
    }
    sq.idx[t] = (int8_t)i;
    sq.nst[t]++;
    seq_gen_step(t, i);
    if (!(sq.muted & (1u << t)) && main_on(p, t, i)) {
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
static void seq_advance(int play)
{
    uint32_t t, pt = pat_ticks();
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
    for (t = 0; t < NTRACK; t++)
        if (sq.tph[t] == 0u)
            seq_track_step(t);
    if (play) {
        seq_flam();
        seq_delayed();
        seq_ratchets();
    }
    sq.ptick++;
    for (t = 0; t < NTRACK; t++)
        if (++sq.tph[t] >= trk_ticks(t))
            sq.tph[t] = 0;
}

/* PERF's STUTTER: the position saved (stut_save) and back to it (stut_load) */
static void stut_save(void)
{
    memcpy(sq.stut.idx, sq.idx, sizeof sq.idx);
    memcpy(sq.stut.rnd, sq.rnd, sizeof sq.rnd);
    memcpy(sq.stut.tph, sq.tph, sizeof sq.tph);
    memcpy(sq.stut.loops, sq.loops, sizeof sq.loops);
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
    seq_advance(1);
    if (!sq.running)
        return;
    if (sq.roll && !sq.roll_free && sq.roll_rate && sq.roll_rate < 14u) {   /* rolls: on their own grid */
        uint32_t every = ROLL_TICKS[sq.roll_rate];
        if (sq.ticks % every == 0u)
            for (t = 0; t < NTRACK; t++)
                if (sq.roll & (1u << t)) {
                    uint32_t i = sq.idx[t] < 0 ? 0u : (uint32_t)sq.idx[t];
                    uint32_t note = tpat(t)->step[t][i].note;
                    seq_trigger(t, 0, 0, ROLL_VOLUME, note);
                    seq_add_note(t, ROLL_VOLUME, note);
                }
    }
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
    uint32_t t;
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
            sq.song_pos = 0xFF;                      /* (seq_nextStep advances to 0) */
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
    for (t = 0; t < NTRACK; t++)
        midi_out_note_off(t);
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
            if (sq.running)
                sq.due++;
            seq_clock_out_tick();
        }
    } else {
        sq.ext_since += BLOCK;
        while (sq.ext_left && sq.ext_next <= 0) {    /* the pulse's later ticks, spread over its length */
            sq.ext_left--;
            if (sq.running)
                sq.due++;
            sq.ext_next += (int32_t)(sq.ext_iv / 4u);
        }
        sq.ext_next -= BLOCK << 8;
    }
    if (sq.running) {
        seq_play_due(BLOCK);
        seq_bounces();
        seq_roll_sweep();
    } else {
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
    p->step[t][q].note = (uint8_t)note;
    p->step[t][q].volume = (uint8_t)(vel | STEP_ACTIVE_MASK);
    p->step[t][q].prob = 100;
    main_set(p, t, q, 1);
    if (p == &pat[sq.active] && p->gen[t][0] != GEN_OFF) {   /* (a generator's track: its live copy too) */
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
            seq_trigger(t, 0, 0, ROLL_VOLUME, SEQ_DEFAULT_NOTE);
            seq_add_note(t, ROLL_VOLUME, SEQ_DEFAULT_NOTE);
        }
    } else {
        sq.roll &= (uint8_t)~(1u << t);
    }
}

/* --------------------------------------------------- song editing --- */
static void song_insert_row(uint32_t song, uint32_t pos)   /* seq_insertRow: the row repeated below */
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
 * length (EuklidGenerator.c: the same rhythms, built directly) */
static uint8_t eu_len[NTRACK], eu_steps[NTRACK], eu_rot[NTRACK];
static void euclid_generate(uint32_t t, pattern_t *p)
{
    uint32_t len = eu_len[t] ? eu_len[t] : 16u, n = eu_steps[t], k, rot;
    uint64_t bits = 0;
    if (len > NUM_STEPS)
        len = NUM_STEPS;
    if (n == 0u)
        n = 1;
    if (n > len)
        n = len;
    for (k = 0; k < len; k++)                        /* the hits where k x n / len steps over a whole number */
        if (((k * n) % len) < n)
            bits |= 1ull << k;
    rot = eu_rot[t] % len;
    if (rot)
        bits = ((bits << rot) | (bits >> (len - rot))) & (len == 64u ? ~0ull : ((1ull << len) - 1u));
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
    uint32_t thr = 255u - u7to8(g[2] > 127u ? 127u : g[2]), lv[2];
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
    uint32_t n, mask, bit, v;
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
    main_set(&gpat, t, i, v < g[3]);
    if (v < g[3])
        gpat.step[t][i].volume = (uint8_t)(STEP_ACTIVE_MASK | (50u + (g[3] ? v * 77u / g[3] : 0u)));
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
                    if (st->p1n != NO_AUTOMATION || p->lhi[t][s] & 15u)
                        st->p1v = (uint8_t)clamp32((int32_t)st->p1v + dv, 0, 127);
                    else if (st->p2n != NO_AUTOMATION || p->lhi[t][s] >> 4)
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
static void seq_undo_save(uint32_t t)
{
    trk_copy(&upat, &pat[sq.active], t);
    undo_t = (int8_t)t;
    undo_p = sq.active;
}
static int seq_undo(uint32_t t)                      /* the track and its saved copy swapped (UNDO again: redo) */
{
    pattern_t *p = &pat[sq.active];
    uint8_t g[4];
    if (undo_t != (int8_t)t || undo_p != sq.active)
        return 0;
    memcpy(g, p->gen[t], 4);                         /* (the generator's settings stay as they are) */
    trk_copy(&gpat, p, t);                           /* (gpat: a scratch track for the swap, synced below) */
    trk_copy(p, &upat, t);
    trk_copy(&upat, &gpat, t);
    memcpy(p->gen[t], g, 4);
    sq.gsync |= (uint8_t)(1u << t);
    return 1;
}
static void seq_commit(uint32_t t)                   /* COMMIT: the live track into the pattern */
{
    pattern_t *p = &pat[sq.active];
    uint8_t g[4];
    if (p->gen[t][0] == GEN_OFF)
        return;
    seq_undo_save(t);
    memcpy(g, p->gen[t], 4);
    trk_copy(p, &gpat, t);
    memcpy(p->gen[t], g, 4);
}
/* DICE: the track again, at random: Euclid's length, its number of hits anywhere */
static void seq_dice(uint32_t t)
{
    pattern_t *p = &pat[sq.active];
    uint32_t len = eu_len[t] ? eu_len[t] : 16u, n = eu_steps[t], k;
    seq_undo_save(t);
    if (len > NUM_STEPS)
        len = NUM_STEPS;
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
        eu_len[t] = 16;
        eu_steps[t] = 4;
    }
    sq.armed_step = sq.armed_track = -1;
    sq.roll_rate = 5;                                /* 1/16 */
    seq_reset_tracks();
    sq.gsync = (1u << NTRACK) - 1u;
    seq_tempo_changed();
}
