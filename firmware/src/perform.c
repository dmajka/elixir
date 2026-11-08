/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: PERF's effects (0.4). The panel (ui.c) holds or latches them in pf.on, the audio ISR applies them at
 * each block's start (perf_block) and on the master (perf_master):
 *   FILL       the steps play whatever their probability (seq.c sq.fill)
 *   STUTTER    the sequencer's play position loops at the REPEAT length (1/4 .. 1/32) from the next point of that
 *              grid; let go, it goes on where it would have been (seq.c sq.stut_*)
 *   FREEZE     the FX bus's DELAY loops what it holds: its input off, its feedback 100% (fxbus.c fx.freeze)
 *   THROW REV  the throw voice's (the selected track's when the key went down) reverb send at 127 ..
 *   THROW FX   .. its FX send at 127; the tail rings out after (voices.c mix_voice)
 *   FB PUSH    the FX bus's feedback (DELAY ~0.95, PHASER and FLANGER their most) (fxbus.c fx.push)
 *   TAPE STOP  the tempo and every voice's pitch slow down together to a stop over TAPE time, then the sound fades;
 *              let go, they spin back up in 0.25 s (voices.c tape_q16)
 *   LP / HP SWEEP  the DJ filter pushed towards its low-pass (high-pass) end over a bar, back in a quarter of that
 * The DJ FILTER (PERF's knob, 64 off): the master through the voices' filter (dsp.c svf), low-pass left of the
 * middle, high-pass right, faded in from the middle (no click where it turns from one to the other).
 * MACROS: four knobs, each a parameter between its MIN and MAX (ui.c); they and REPEAT and TAPE are the project's
 * (project.c).
 * 0.6: PERFORM 2, the second bank (the PERFORM 2 pages: ui.c), its settings the project's too (KPF2):
 *   DRILL      keys 1..7: the track's hits become ratchets of 2 .. 8 at random rates, velocities, pitches (seq.c)
 *   HALF       the sequencer at half speed, the voices as they were (seq.c sq.half)
 *   JITTER     each hit late by a random part of up to a 32nd (seq.c sq.jit, the held-back hits)
 *   CUTS       the master gated by a random 1/32 pattern, 0.7 ms fades
 *   SHORT      every voice's amp decay cut to a click
 *   PUMP       every voice but D1 ducked by D1's hits, back over an 8th (voices.c pump_g*)
 *   COMB       a tuned comb (its note, its feedback) on the master: drums ring at a pitch
 *   SPACE      the reverb held (its tail kept going), the dry sound dipped (voices.c space_on, reverb.c)
 *   CRUSH      the master's bits and sample rate down
 *   REVERSE    the last 1/16 .. 1/4 played backwards, again while held (a recorder: 0.25 s, mono, 22 kHz)
 * 0.13: two GLOBAL LFOs (the PERF pages GLOBAL LFO 1, 2 and their MODE pages), the project's (gls): each moves one
 * of the whole kit's performance controls (GD_*). DEPTH -127 .. 127. MORPH and FILTER (the DJ filter, both ways) are
 * moved from where their knobs are, by the depth either way; LP and HP push the DJ filter towards that end; the
 * effects (FB PUSH, COMB, CRUSH, JITTER, CUTS, PUMP) come in by the LFO (its upside down for a negative depth), at
 * most as the key would play them, their settings as they are. SPEED is a length in 16ths to 256 bars, at the
 * tempo (the clock: MIDI's too, TAPE STOP, HALF). MODE as Elektron's: FREE, TRIG (restarts), HOLD (runs, its value
 * held at each trigger), ONE (a cycle from the trigger, then still), HALF (half of one); TRIG the triggers: PLAY,
 * PATTERN (PLAY and each pattern change), BAR (PLAY and each bar); OCT+ on its page triggers it too. PHASE: where a
 * trigger starts it. The audio ISR runs them (glfo_block); morph is the main loop's (ui.c glfo_morph).
 * 19c: a third global LFO, and more for them to move, each from where it is set either way by the depth: DENSITY
 * (19d), TRANSPOSE (the scale's degrees, up to 7 either way: seq.c scale_fit), the melody's CHANGE and SPREAD
 * (Phase 20), the delay's feedback and time (+-1/4 of its range, moved every 16 blocks), the reverb sends (x0 .. x2),
 * and the four macros (MACRO 1..4: through them any lockable parameter; while playing, the main loop's: ui.c
 * glfo_macros).
 * 19d: DENSITY (the DENSITY page, the project's): -64..63, thins the steps the weakest first or builds them up
 * (seq.c dens_*): the chances, then ghost notes at GHOST's velocity, then (BUILD RATCHETS) ratchets; the tracks it
 * leaves alone (the black keys on its page).
 * 0.16: STUTTER (its own PERF page, the project's: KSC1): the master's slices repeated, from REVERSE's recorder (so
 * mono, 22 kHz: no more memory). At each LENGTH of the clock a burst comes at CHANCE's odds (or a global LFO's, the
 * larger; OCT+ held on the page: at the next SIZE, as long as it is held): the slice of SIZE from there plays as it
 * is, recorded, then repeats until LENGTH is over. VARY changes each SIZE of the burst: SHRINK (the slice halved,
 * down to an eighth: a roll that speeds up), PITCH DN / UP (a semitone more each time), REVERSE (every other one
 * backwards), RANDOM (one of those a burst). In time: the repeats count samples from the burst's start, so they stay
 * on the grid. */

enum { PF_FILL, PF_STUT, PF_FREEZE, PF_THROW_R, PF_THROW_F, PF_PUSH, PF_TAPE, PF_SWEEP_LP, PF_SWEEP_HP, NPF };
enum { PF_HALF = NPF, PF_JITTER, PF_CUTS, PF_SHORT, PF_PUMP, PF_COMB, PF_SPACE, PF_CRUSH, PF_REVERSE, NPF2 };
#define COMB_LEN 4096u                               /* the comb's line: down to ~11 Hz */
#define REC_LEN 5512u                                /* REVERSE's recorder: 0.25 s at 22.05 kHz */
#define SHORT_DEC 2097152u                           /* SHORT: the amp EG's decay rate (an eighth a block) */
static int16_t comb_buf[COMB_LEN] POOL;
static int16_t rec_buf[REC_LEN] NOINITBUF;          /* (1.0: in NOINIT, zeroed at start) */
#define NMACRO 4
enum { SV_OFF, SV_SHRINK, SV_PDOWN, SV_PUP, SV_REV, SV_RAND, NSV };   /* (0.16: STUTTER's VARY) */
#define NSTSIZE 7u                                   /* SIZE: 1/8 1/16 1/16T 1/32 1/32T 1/64, RANDOM (1/16 .. 1/64) */
#define NSTLEN 5u                                    /* LENGTH: 1/16 1/8 1/4 1/2 1 bar */
static const uint8_t ST_SIZE[NSTSIZE - 1u] = {48, 24, 16, 12, 8, 6};   /* (ticks) */
static const uint16_t ST_LEN[NSTLEN] = {24, 48, 96, 192, 384};
static struct {
    volatile uint32_t on;                            /* the effects held or latched (bit PF_*) */
    volatile uint8_t throw_v;                        /* the voice THROW sends */
    uint8_t dj;                                      /* the DJ filter's knob, 64 off */
    int32_t sweep;                                   /* the sweeps: -65536 (low-pass end) .. 65536 (high-pass) */
    int32_t dj_d;                                    /* the filter's position in use, -65536 .. 65536 */
    uint8_t dj_type;                                 /* the type the filter ran last (a change: its state reset) */
    int32_t dj_rs;                                   /* 1.0: Reso as the filter has it, Q8 (glides to the knob's) */
    svf_t dj_f[2];
    int32_t rg_s[8][2];                              /* 1.0: RING's bands (lp, bp; Q4 samples) */
    int32_t rg_xh[9], rg_yh[2][5];                   /* 1.0 Phase 10e: RING's half-band filters' histories (the mono
                                                      * sum in, the bands' left and right out) */
    /* 0.6: PERFORM 2's states: CUTS (samples to the next 32nd, the gate, its gain Q15); PUMP (D1's hits seen, the
     * envelope Q15, rising); COMB (its write index, the loop's low-pass); CRUSH (the held sample, samples to the
     * next); REVERSE (the recorder's write index, half a sample; the part playing: its end, length, position in
     * 44.1 kHz samples, the mix Q15) */
    int32_t cut_n, cut_g;
    uint8_t cut_open;
    uint32_t pump_seen;
    int32_t pump_env;
    uint8_t pump_up;
    uint32_t comb_w;
    int32_t comb_lp;
    int32_t cr_l, cr_r, cr_n;
    uint32_t rec_w;
    int32_t rec_half;
    uint32_t rv_end, rv_len, rv_pos;
    int32_t rv_mix;
    /* 0.16: STUTTER: OCT+ held on its page (the panel's); the burst (0 none, 1 the first SIZE: live, recorded, 2
     * repeating), its VARY, the clock it last saw (ticks; stopped: its own), the slice (start in the recorder, its
     * length at 22 kHz), the SIZEs left, the samples left of this one (Q16), the read position (Q16, 22 kHz), its
     * speed (Q16), the SIZEs played, the mix (Q15) */
    volatile uint8_t st_hold;
    uint8_t st_on, st_mode;
    uint32_t st_tk, st_last, st_acc;
    uint32_t st_start, st_n, st_units, st_rep;
    int64_t st_left, st_uq;                          /* (a SIZE, Q16 samples) */
    uint32_t st_pos, st_rate;
    int32_t st_mix;
} pf;
/* the project's: the macros and the effects' settings */
static struct {
    struct { uint16_t par; uint8_t lo, hi, val; } m[NMACRO];   /* par 0: none */
    uint8_t repeat;                                  /* the STUTTER length: 1/4 1/8 1/16 1/32 */
    uint8_t tape;                                    /* the TAPE STOP time, 0..127: 0.2 .. 4 s */
    /* 0.6: PERFORM 2: DRILL's density, JITTER's most, CUTS' density, PUMP's depth, COMB's note (MIDI) and feedback,
     * CRUSH's amount (0..127 each, but the note), REVERSE's length (1/16 1/8 1/4) */
    uint8_t drill, jitter, cuts, pump, comb_note, comb_fb, crush, rev_len;
    /* 19d: DENSITY, the ghosts' velocity, BUILD (DB_*), the tracks left out (bit t) (KSC1) */
    int8_t dens;
    uint8_t ghost, build, dex;
    /* 0.16: STUTTER: CHANCE 0..127, SIZE, LENGTH, VARY (ST_SIZE, ST_LEN, SV_*) (KSC1) */
    uint8_t st_chance, st_size, st_len, st_vary;
    uint8_t dvary;                                   /* 31: DENSITY's VARY, 0..127 (KSC1) */
    uint8_t dj_reso;                                 /* 0.24.3: the DJ filter's resonance, 0..127 (0: 0.6's) (KSC1) */
    uint8_t dj_ftype;                                /* 1.0: the DJ filter's type, DJT_* (KSC1) */
    uint8_t dj_tune;                                 /* 1.0: RING's chord, RC_* (KSC1) */
} pfs;
/* 1.0: the DJ filter's type, the global character filter: the LXR's SVF (LP, HP), ACID (its LP; the LXR's HP) or MS20
 * (its LP, HP) */
enum { DJT_LXR, DJT_ACID, DJT_MS20, DJT_RING, NDJT };
/* 1.0: RING, a tuned resonator (after the Torso S-4's RING, as Rings' strings are): NRB band-passes at a chord over a
 * root the DJ knob sweeps in semitones (C1 at the left end, C3 in the middle, C6 at the right: darker one way,
 * brighter the other), Reso their ring time (T60 0.08 .. 2.6 s), TUNE the chord. On the mix summed to mono, the bands
 * panned alternately left and right (the root in the middle); half the dry under it, so the drums keep their hit */
#define NRB 8u
enum { RC_NOTE, RC_OCT, RC_FIFTH, RC_MAJ, RC_MIN, RC_SUS4, RC_MIN7, RC_PENTA, NRTUNE };
static const int8_t RING_SH[NRTUNE][NRB] = {
    {0, 12, 19, 24, 28, 31, 34, 36},                 /* NOTE: the harmonics 1..8 (a string, tempered) */
    {0, 12, 24, 36, -1, -1, -1, -1},                 /* OCTAVES */
    {0, 7, 12, 19, 24, 31, 36, -1},                  /* FIFTHS */
    {0, 4, 7, 12, 16, 19, 24, 28},                   /* MAJOR */
    {0, 3, 7, 12, 15, 19, 24, 27},                   /* MINOR */
    {0, 5, 7, 12, 17, 19, 24, -1},                   /* SUS4 */
    {0, 3, 7, 10, 12, 15, 19, 22},                   /* MIN7 */
    {0, 3, 5, 7, 10, 12, 15, 17}};                   /* PENTA (minor) */
static uint32_t ring_root(int32_t d)                 /* the root at the DJ filter's position d, a MIDI note */
{
    int32_t q = 48 * 65536 + d * (d < 0 ? 24 : 36);
    return (uint32_t)(q + 32768) >> 16;
}
static uint32_t dj_kind(int32_t d)                   /* the filter type at the DJ filter's position d (0: none) */
{
    static const uint8_t T[DJT_RING][2] = {{F_LP, F_HP}, {F_ACID, F_HP}, {F_MS20L, F_MS20H}};
    return d ? T[pfs.dj_ftype < DJT_RING ? pfs.dj_ftype : 0u][d > 0] : 0u;
}
static uint32_t dj_fval(int32_t d)                   /* .. its cutoff, Q16 (from the top, down to the left; up to the right) */
{
    uint32_t a = (uint32_t)iabs(d);
    return d < 0 ? 65536u - ((a * 58982u) >> 16) : (a * 45875u) >> 16;   /* (unsigned: a * 58982 passes 2^31) */
}
#define NPF2SET 8u                                   /* (KPF2: the bytes from drill) */
static const uint8_t REPEAT_TICKS[4] = {96, 48, 24, 12};

/* 0.13: the global LFOs */
enum { GD_OFF, GD_MORPH, GD_FILTER, GD_LP, GD_HP, GD_PUSH, GD_COMB, GD_CRUSH, GD_JITTER, GD_CUTS, GD_PUMP,
       GD_DENSITY, GD_TRANSPOSE, GD_MCHANGE, GD_MSPREAD, GD_DFB, GD_DTIME, GD_RSEND, GD_MACRO1,
       GD_STUT = GD_MACRO1 + NMACRO, GD_DIRT, GD_DBAND, NGD };   /* (0.16: STUTTER's chance, from 0 as the effects;
                                                      * 1.0: the master DIRT's amount, the delay's BAND, either way) */
#define NGBI (GD_STUT - GD_DENSITY + 2)              /* (19c: the ones moved either way, gl.bi; GD_DIRT's, GD_DBAND's last) */
static uint32_t gd_bi(uint32_t g)                    /* (its place in gl.bi, NGBI none) */
{
    return g >= GD_DENSITY && g < GD_STUT ? g - GD_DENSITY : g == GD_DIRT ? NGBI - 2u : g == GD_DBAND ? NGBI - 1u : NGBI;
}
enum { GM_FREE, GM_TRIG, GM_HOLD, GM_ONE, GM_HALF, NGM };
enum { GT_PLAY, GT_PATTERN, GT_BAR, NGT };
#define NGLFO 3u
/* SYNC: a cycle, in sixths of a 16th (4 ticks): 0.13's 1/16 .. 256 bars, then (1.0) 1/32 and the triplets and dotted
 * ones, 3/8 and 5/16 (GL_ORDER: the knob's, OFF then slow .. fast as the track LFOs'). GL_FREE: SYNC OFF, RATE in Hz */
#define NGLEN 28u
#define GL_FREE 0x7Fu
static const uint16_t GL_LEN[NGLEN] = {6, 12, 24, 48, 96, 192, 288, 384, 576, 768, 1152, 1536, 2304, 3072, 4608, 6144,
                                       9216, 12288, 24576,
                                       3, 4, 8, 18, 16, 30, 36, 32, 72};   /* (1/32 1/16T 1/8T 3/16 1/4T 5/16 3/8
                                                                            * 1/2T 3/4) */
static const uint8_t GL_ORDER[NGLEN + 1u] = {GL_FREE, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 27, 3, 25,
                                             26, 24, 2, 22, 23, 1, 21, 0, 20, 19};
static struct {                                      /* the project's (KGL1) */
    uint8_t dest, len, wave, mode, trig, phase;      /* GD_*, GL_LEN's index (GL_FREE), LFO_*, GM_*, GT_*, 0..127 */
    int8_t depth;                                    /* -127 .. 127 */
    uint8_t rate;                                    /* 1.0: SYNC OFF's rate, 0..127: 0.01 .. 20 Hz (KGL1's 8th) */
} gls[NGLFO];
#define NGLKGL 7u                                    /* (KGL1: 0.13's bytes of each, then 1.0's RATE) */
#define NGLSET 8u                                    /* (the settings of each: the pages' knobs) */
#define GL_RATE_INC 62333u                           /* (0.01 Hz: 2^32 x BLOCK / FS / 100) */
static uint32_t gl_rate_inc(uint32_t r)              /* RATE's step a block: 0.01 Hz x 2^(11 r / 127) */
{
    return (uint32_t)(((uint64_t)GL_RATE_INC * (uint32_t)exp2_q16((int32_t)((r & 127u) * 11u * 65536u / 127u))) >> 16);
}
static struct {
    lfo_t l[NGLFO];
    uint32_t tick[NGLFO];                            /* the tick length and SPEED l[n].inc was worked out for */
    uint8_t len[NGLFO];
    int32_t out[NGLFO];                              /* their value, 0 .. 65536 (the screen) */
    int32_t dj;                                      /* the DJ filter moved by them, -65536 .. 65536 */
    int32_t amt[NGD];                                /* each effect's amount from them, 0 .. 65536 */
    volatile int32_t morph;                          /* morph moved by them, -255 .. 255 (the main loop's) */
    volatile int32_t bi[NGBI];                       /* 19c: GD_DENSITY .. moved either way, -65536 .. 65536 */
} gl;

static void perf_defaults(void)
{
    uint32_t c;
    memset(&pfs, 0, sizeof pfs);
    pfs.repeat = 2;                                  /* 1/16 */
    pfs.tape = 64;                                   /* ~0.9 s */
    pfs.dj_tune = RC_MAJ;                            /* (1.0: RING: a major chord) */
    pfs.drill = 80;
    pfs.jitter = 64;
    pfs.cuts = 50;
    pfs.pump = 100;
    pfs.comb_note = 45;                              /* A2 */
    pfs.comb_fb = 100;
    pfs.crush = 64;
    pfs.rev_len = 1;                                 /* 1/8 */
    pfs.ghost = 40;                                  /* 19d */
    pfs.build = DB_GHOSTS;
    pfs.st_size = 1;                                 /* (STUTTER: 1/16 over a 1/4) */
    pfs.st_len = 2;
    for (c = 0; c < NGLFO; c++) {                    /* 0.13: the global LFOs: off, a triangle over 4 bars */
        memset(&gls[c], 0, sizeof gls[c]);
        gls[c].len = 7;
        gls[c].wave = LFO_TRI;
        gls[c].rate = 64;                            /* (1.0: ~0.33 Hz, when SYNC is OFF) */
        gl.tick[c] = 0;
    }
}
static void perf_init(void)
{
    uint32_t c;
    perf_defaults();
    pf.dj = 64;
    pf.cut_g = 32767;
    pf.cut_open = 1;
    for (c = 0; c < 2u; c++) {
        svf_init(&pf.dj_f[c]);
        pf.dj_f[c].q = 39322;                        /* (0.6: a little resonance) */
    }
}

/* 0.13: the global LFOs, a block (the audio ISR, perf_block): their triggers, a step, what they move */
static void glfo_block(void)
{
    static const uint8_t LM_OF[NGM] = {LM_TRIG, LM_TRIG, LM_HOLD, LM_ONE, LM_HALF};
    static const uint8_t TRIG_EV[NGT] = {GLE_PLAY, GLE_PLAY | GLE_PATTERN, GLE_PLAY | GLE_BAR};
    uint32_t ev = glfo_ev, n;
    int32_t morph = 0, bi[NGBI] = {0};
    glfo_ev = 0;
    gl.dj = 0;
    memset(gl.amt, 0, sizeof gl.amt);
    for (n = 0; n < NGLFO; n++) {
        lfo_t *l = &gl.l[n];
        const uint32_t g = gls[n].dest < NGD ? gls[n].dest : GD_OFF, m = gls[n].mode < NGM ? gls[n].mode : GM_FREE;
        uint32_t t = sq.tick_q16 ? sq.tick_q16 : 1u;
        int32_t d = gls[n].depth, x;
        if (gls[n].len >= NGLEN) {                   /* 1.0: SYNC OFF: RATE */
            gl.len[n] = GL_FREE;
            l->inc = gl_rate_inc(gls[n].rate);
        } else if (gl.tick[n] != t || gl.len[n] != gls[n].len) {   /* a cycle: GL_LEN units of 4 ticks; 2^32 x BLOCK
                                                      * / its samples */
            gl.tick[n] = t;
            gl.len[n] = gls[n].len;
            l->inc = (uint32_t)((((uint64_t)BLOCK << 48) / 4u / t) / GL_LEN[gls[n].len]);
        }
        l->wave = (uint8_t)(gls[n].wave % NLFO);
        l->offset = gls[n].phase >= 127u ? 0xFFFFFFFFu : (uint32_t)gls[n].phase << 25;
        lfo_mode_set(l, LM_OF[m]);
        if (m != GM_FREE && (ev & (TRIG_EV[gls[n].trig % NGT] | GLE_KEY << n)))
            lfo_hit(l);
        {
            uint32_t inc = l->inc;                   /* (the clock's speed: TAPE STOP, HALF; free: TAPE STOP's) */
            l->inc = (uint32_t)(((uint64_t)inc * tape_q16) >> (16u + (sq.half && sq.running && gls[n].len < NGLEN)));
            gl.out[n] = lfo_calc(l);
            l->inc = inc;
        }
        if (g == GD_OFF || !d)
            continue;
        x = gl.out[n];
        if (g == GD_MORPH) {
            morph += d * x / 32768;                  /* (127 x 65536 / 32768: about 255) */
        } else if (g == GD_FILTER) {
            gl.dj += (int32_t)(((int64_t)d * (2 * x - 65536)) / 127);
        } else if (gd_bi(g) < NGBI) {                /* (19c: either way from where it is) */
            bi[gd_bi(g)] = clamp32(bi[gd_bi(g)] + (int32_t)(((int64_t)d * (2 * x - 65536)) / 127), -65536, 65536);
        } else {                                     /* the rest from 0 by |depth|; a negative one upside down */
            int32_t a = (d > 0 ? x : 65536 - x) * (d > 0 ? d : -d) / 127;
            if (g == GD_LP)
                gl.dj -= a;
            else if (g == GD_HP)
                gl.dj += a;
            else
                gl.amt[g] = gl.amt[g] + a < 65536 ? gl.amt[g] + a : 65536;
        }
    }
    gl.morph = clamp32(morph, -255, 255);
    for (n = 0; n < NGBI; n++)
        gl.bi[n] = bi[n];
}
/* an effect's amount, 0 .. 65536: its key (held, latched, the remote's) the most, else the LFOs' */
static uint32_t pf_amount(uint32_t on, uint32_t bit, uint32_t g)
{
    return (on >> bit) & 1u ? 65536u : (uint32_t)gl.amt[g];
}

/* the audio ISR, a block's start: the held effects into the engine */
static void perf_block(void)
{
    uint32_t on = pf.on, v, a;
    int32_t target, step;
    glfo_block();
    {                                                /* 19c, 19d: DENSITY, TRANSPOSE, the delay, the reverb sends */
        int32_t t = gl.bi[GD_TRANSPOSE - GD_DENSITY] * 7;
        sq.dens = (int8_t)clamp32(pfs.dens + gl.bi[0] / 1024, -64, 63);
        sq.dghost = pfs.ghost;
        sq.dbuild = (uint8_t)(pfs.build < NDB ? pfs.build : DB_GHOSTS);
        sq.dex = pfs.dex;
        sq.dvary = pfs.dvary;
        sq.transp = (int8_t)((t + (t < 0 ? -32768 : 32768)) / 65536);
        sq.mchg = (int8_t)(gl.bi[GD_MCHANGE - GD_DENSITY] / 1024);   /* (20: the melodies' DEJA VU, SPREAD) */
        sq.mspr = (int8_t)(gl.bi[GD_MSPREAD - GD_DENSITY] / 1024);
        fx.lfo_fb = gl.bi[GD_DFB - GD_DENSITY];
        fx.lfo_time = gl.bi[GD_DTIME - GD_DENSITY];
        rev_g = (uint32_t)(65536 + gl.bi[GD_RSEND - GD_DENSITY]);
        dirt.lfo = gl.bi[NGBI - 2];                  /* (1.0: the master DIRT's amount) */
        fx.lfo_band = gl.bi[NGBI - 1];               /* (1.0: the delay's BAND, +-2 octaves; OFF stays off) */
    }
    sq.fill = (on >> PF_FILL) & 1u;
    sq.half = (on >> PF_HALF) & 1u;
    a = pf_amount(on, PF_JITTER, GD_JITTER);         /* (0.13: the global LFOs' amounts, a key's the most) */
    sq.jit = a >= 1024u ? (uint8_t)(1u + pfs.jitter * 11u * (a >> 4) / (127u << 12)) : 0u;
    sq.drill_span = (uint8_t)(1u + pfs.drill * 6u / 127u);
    space_on = (on >> PF_SPACE) & 1u;
    if ((on >> PF_SHORT) & 1u)                       /* SHORT: every voice's decay a click, as long as it's on */
        for (v = 0; v < NVOICE; v++)
            if (vc[v].aeg.dec < SHORT_DEC)
                vc[v].aeg.dec = SHORT_DEC;
    pump_g0 = pump_g1;                               /* PUMP: D1's hit: down in 2 blocks, back over an 8th */
    a = pf_amount(on, PF_PUMP, GD_PUMP);
    if (a) {
        if (trig_count[0] != pf.pump_seen)
            pf.pump_up = 1;
        if (pf.pump_up) {
            pf.pump_env += 16384;
            if (pf.pump_env >= 32767) {
                pf.pump_env = 32767;
                pf.pump_up = 0;
            }
        } else {
            pf.pump_env -= (int32_t)(32767u * BLOCK / (48u * (sq.tick_q16 >> 16) + 1u)) + 1;
            if (pf.pump_env < 0)
                pf.pump_env = 0;
        }
        pump_g1 = 32768 - ((pf.pump_env * pf.pump_env) >> 15) * (int32_t)(pfs.pump * (a >> 4) >> 12) / 127;
    } else {
        pf.pump_env = 0;
        pump_g1 = pump_g1 + 2048 < 32768 ? pump_g1 + 2048 : 32768;
    }
    pf.pump_seen = trig_count[0];
    sq.stut_req = (on >> PF_STUT) & 1u;
    sq.stut_len = REPEAT_TICKS[pfs.repeat & 3u];
    fx.freeze = (on >> PF_FREEZE) & 1u;
    fx.push = (int32_t)pf_amount(on, PF_PUSH, GD_PUSH);
    fx.throw_echo = (uint8_t)((on >> PF_THROW_R) & 1u);   /* (21c: THROW REV throws the echoes in too) */
    for (v = 0; v < NVOICE; v++) {
        vc[v].throw_rev = (uint8_t)(v == pf.throw_v && ((on >> PF_THROW_R) & 1u));
        vc[v].throw_fx = (uint8_t)(v == pf.throw_v && ((on >> PF_THROW_F) & 1u));
    }
    if ((on >> PF_TAPE) & 1u) {                      /* TAPE STOP: linear in speed over its time (T ms) */
        int32_t t_ms = (int32_t)(((int64_t)200 * exp2_q16(pfs.tape * 2232)) >> 16);   /* 200 x 20^(t / 127) */
        step = (int32_t)(95109 / (uint32_t)(t_ms > 1 ? t_ms : 1));   /* 65536 x BLOCK / FS per ms */
        tape_q16 = tape_q16 > (uint32_t)step ? tape_q16 - (uint32_t)step : 0u;
    } else if (tape_q16 < 65536u) {
        tape_q16 = tape_q16 + 380u < 65536u ? tape_q16 + 380u : 65536u;   /* (back in 0.25 s) */
    }
    /* the sweeps: a bar to their end, back in a quarter of it. A bar is 384 ticks of tick_q16 / 65536 samples:
     * 65536 x BLOCK / (384 x tick_q16 / 65536) = 2^38 / 384 / tick_q16 = (2^32 / 6) / tick_q16 (32-bit divide) */
    step = (int32_t)(715827883u / (sq.tick_q16 ? sq.tick_q16 : 1u));
    target = ((on >> PF_SWEEP_HP) & 1u ? 65536 : 0) - ((on >> PF_SWEEP_LP) & 1u ? 65536 : 0);
    if (pf.sweep < target)
        pf.sweep = pf.sweep + step * (pf.sweep < 0 ? 4 : 1) < target ? pf.sweep + step * (pf.sweep < 0 ? 4 : 1)
                                                                        : target;
    else if (pf.sweep > target)
        pf.sweep = pf.sweep - step * (pf.sweep > 0 ? 4 : 1) > target ? pf.sweep - step * (pf.sweep > 0 ? 4 : 1)
                                                                        : target;
}

static void stutter(int16_t *l, int16_t *r);
/* 0.6: PERFORM 2 on the master, in this order: COMB, CUTS, CRUSH, (0.16) STUTTER, then the recorder and REVERSE */
static void perf2_master(int16_t *l, int16_t *r)
{
    uint32_t on = pf.on, i, a, st_was;
    a = pf_amount(on, PF_COMB, GD_COMB);
    if (a) {                                         /* COMB: y = x (1 - fb) 4 + fb lp(y[n - D]); out x / 2 + y (0.13:
                                                      * by the amount, a: x + (y - x / 2) a) */
        int32_t g = (int32_t)(a >> 1);
        int32_t f8 = (int32_t)(((uint64_t)112640u * (uint32_t)exp2_q16(((int32_t)pfs.comb_note - 69) * 65536 / 12)) >>
                               16);                  /* the note's Hz, Q8 */
        uint32_t d8 = ((uint32_t)FS << 16) / (uint32_t)(f8 > 0 ? f8 : 1), di = d8 >> 8, df = d8 & 255u;
        int32_t fb = 16384 + pfs.comb_fb * 125, gi = (32768 - fb) * 4;
        if (di < 2u)
            di = 2u;
        if (di > COMB_LEN - 2u)
            di = COMB_LEN - 2u;
        for (i = 0; i < BLOCK; i++) {
            uint32_t a = (pf.comb_w - di) & (COMB_LEN - 1u), b = (a - 1u) & (COMB_LEN - 1u);
            int32_t x = (l[i] + r[i]) >> 1, y = comb_buf[a] + (((comb_buf[b] - comb_buf[a]) * (int32_t)df) >> 8);
            pf.comb_lp += ((y - pf.comb_lp) * 24576) >> 15;   /* (a gentle low-pass in the loop) */
            y = sat16(((x * gi) >> 15) + ((pf.comb_lp * fb) >> 15));
            comb_buf[pf.comb_w++ & (COMB_LEN - 1u)] = (int16_t)y;
            l[i] = (int16_t)sat16(l[i] + ((((y - (l[i] >> 1))) * g) >> 15));
            r[i] = (int16_t)sat16(r[i] + ((((y - (r[i] >> 1))) * g) >> 15));
        }
    }
    a = pf_amount(on, PF_CUTS, GD_CUTS);
    if (a || pf.cut_g < 32767) {                     /* CUTS: each 32nd open or shut at random, 32-sample fades */
        int32_t per = (int32_t)((12u * (sq.tick_q16 >> 8)) >> 8), dens = (int32_t)(pfs.cuts * (a >> 4) >> 12);
        for (i = 0; i < BLOCK; i++) {
            if (!a)
                pf.cut_open = 1;
            else if (--pf.cut_n <= 0) {
                pf.cut_n = per > 64 ? per : 64;
                pf.cut_open = (int32_t)(seq_rand() % 100u) >= dens * 100 / 127;
            }
            pf.cut_g = pf.cut_open ? (pf.cut_g + 1024 < 32767 ? pf.cut_g + 1024 : 32767)
                                   : (pf.cut_g > 1024 ? pf.cut_g - 1024 : 0);
            l[i] = (int16_t)((l[i] * pf.cut_g) >> 15);
            r[i] = (int16_t)((r[i] * pf.cut_g) >> 15);
        }
    }
    a = pf_amount(on, PF_CRUSH, GD_CRUSH);
    if (a) {                                         /* CRUSH: held 1 .. 16 samples, 16 .. 4 bits */
        uint32_t c = pfs.crush * (a >> 4) >> 12, hold = 1u + c * 15u / 127u, bits = 16u - c * 12u / 127u;
        int32_t mask = (int32_t)(0xFFFFFFFFu << (16u - bits));
        for (i = 0; i < BLOCK; i++) {
            if (--pf.cr_n <= 0) {
                pf.cr_n = (int32_t)hold;
                pf.cr_l = l[i] & mask;
                pf.cr_r = r[i] & mask;
            }
            l[i] = (int16_t)pf.cr_l;
            r[i] = (int16_t)pf.cr_r;
        }
    }
    st_was = pf.st_on;                               /* 0.16: STUTTER, then the recorder (its first SIZE recorded) */
    stutter(l, r);
    /* REVERSE: the recorder (mono, every other sample) stops while it plays; the part from its end backwards, again
     * and again, 64-sample fades at its ends and in and out */
    if ((on >> PF_REVERSE) & 1u && !pf.rv_len) {
        uint32_t n = (uint32_t)(((uint64_t)(24u << (pfs.rev_len % 3u)) * sq.tick_q16) >> 17);   /* 22 kHz */
        pf.rv_len = n < 64u ? 64u : n > REC_LEN ? REC_LEN : n;
        pf.rv_end = pf.rec_w;
        pf.rv_pos = 0;
    }
    if (pf.rv_len) {
        int32_t tgt = (on >> PF_REVERSE) & 1u ? 32767 : 0;
        for (i = 0; i < BLOCK; i++) {
            uint32_t k = pf.rv_pos >> 1, e = pf.rv_len - k, f = k < e ? k : e;
            int32_t v = rec_buf[(pf.rv_end + REC_LEN - 1u - k) % REC_LEN];
            v = f < 32u ? v * (int32_t)f / 32 : v;
            pf.rv_mix += pf.rv_mix < tgt ? 512 : pf.rv_mix > tgt ? -512 : 0;
            pf.rv_mix = clamp32(pf.rv_mix, 0, 32767);
            l[i] = (int16_t)(l[i] + (((v - l[i]) * pf.rv_mix) >> 15));
            r[i] = (int16_t)(r[i] + (((v - r[i]) * pf.rv_mix) >> 15));
            if (++pf.rv_pos >= pf.rv_len * 2u)
                pf.rv_pos = 0;
        }
        if (!tgt && !pf.rv_mix)
            pf.rv_len = 0;
    } else if (st_was != 2u || pf.st_on != 2u) {    /* (0.16: STUTTER's repeats hold it too) */
        for (i = 0; i < BLOCK; i += 2) {
            rec_buf[pf.rec_w] = (int16_t)((l[i] + r[i] + l[i + 1u] + r[i + 1u]) >> 2);
            pf.rec_w = pf.rec_w + 1u < REC_LEN ? pf.rec_w + 1u : 0u;
        }
    }
}

/* 0.16: STUTTER, on the master before the recorder (perf2_master) */
static uint32_t st_ticks(void)                       /* the clock: the sequencer's ticks, stopped its own */
{
    if (sq.running)
        return sq.ticks;
    pf.st_acc += BLOCK << 16;
    while (sq.tick_q16 && pf.st_acc >= sq.tick_q16) {
        pf.st_acc -= sq.tick_q16;
        pf.st_tk++;
    }
    return pf.st_tk;
}
static void st_unit(void)                            /* a SIZE of the burst starts: how this one reads the slice */
{
    static const uint16_t SEMI[13] = {65535, 61857, 58385, 55108, 52015, 49096, 46340, 43740, 41285, 38967, 36780,
                                      34716, 32768};  /* (2^(-k/12), Q16) */
    uint32_t k = pf.st_rep - 1u < 12u ? pf.st_rep - 1u : 12u;   /* (the first repeat as it was) */
    pf.st_pos = 0;
    pf.st_rate = pf.st_mode == SV_PDOWN ? SEMI[k] : pf.st_mode == SV_PUP ? (65536u << 16) / SEMI[k] : 65536u;
}
static void st_start(uint32_t i)                     /* a burst from sample i of this block (the recorder's next) */
{
    uint32_t sz = pfs.st_size < NSTSIZE - 1u ? ST_SIZE[pfs.st_size] : ST_SIZE[1u + seq_rand() % 5u];
    uint32_t lt = ST_LEN[pfs.st_len < NSTLEN ? pfs.st_len : 2u], h = 0;
    uint64_t uq = (uint64_t)sz * sq.tick_q16;        /* (a SIZE: Q16 samples) */
    while ((uq >> 17) > REC_LEN - BLOCK) {           /* (too long for the recorder: a half, a quarter ..) */
        uq >>= 1;
        h++;
    }
    pf.st_n = (uint32_t)(uq >> 17) < 32u ? 32u : (uint32_t)(uq >> 17);   /* (22 kHz samples) */
    pf.st_uq = (int64_t)uq;
    pf.st_units = (lt > sz ? lt / sz : 2u) << h;
    pf.st_left += (int64_t)uq;
    pf.st_start = (pf.rec_w + i / 2u) % REC_LEN;
    pf.st_rep = 0;
    pf.st_mode = (uint8_t)(pfs.st_vary == SV_RAND ? SV_SHRINK + seq_rand() % 4u : pfs.st_vary % NSV);
    pf.st_on = 1;
}
static void stutter(int16_t *l, int16_t *r)
{
    uint32_t tk = st_ticks(), last = pf.st_last, i;
    uint32_t a = (uint32_t)pfs.st_chance * 65536u / 127u, sz, lt;
    if ((uint32_t)gl.amt[GD_STUT] > a)
        a = (uint32_t)gl.amt[GD_STUT];
    pf.st_last = tk;
    if (tk < last)                                   /* (PLAY: the clock from 0) */
        last = tk;
    sz = pfs.st_size < NSTSIZE - 1u ? ST_SIZE[pfs.st_size] : 6u;
    lt = ST_LEN[pfs.st_len < NSTLEN ? pfs.st_len : 2u];
    if (!pf.st_on) {
        if (pf.st_hold ? tk / sz != last / sz : a && tk / lt != last / lt && (seq_rand() & 0xFFFFu) < a) {
            pf.st_left = 0;
            st_start(0);
        } else if (!pf.st_mix) {
            return;
        }
    }
    for (i = 0; i < BLOCK; i++) {
        int32_t v = 0;
        if (pf.st_on) {
            pf.st_left -= 65536;
            if (pf.st_left <= 0) {                   /* (the next SIZE, counted from the burst's start: in time) */
                pf.st_on = 2;                        /* (recorded: the recorder holds it now) */
                pf.st_rep++;
                if (pf.st_rep >= pf.st_units && !pf.st_hold) {
                    pf.st_on = 0;
                    if (a && (seq_rand() & 0xFFFFu) < a)   /* (again at once: the next LENGTH's chance) */
                        st_start(i);
                } else {
                    pf.st_left += pf.st_uq;
                    st_unit();
                }
            }
        }
        if (pf.st_on == 2u) {
            uint32_t n = pf.st_mode == SV_SHRINK ? pf.st_n >> (pf.st_rep < 4u ? pf.st_rep - 1u : 3u) : pf.st_n;
            uint32_t p = pf.st_pos >> 16, f = pf.st_pos & 0xFFFFu, e = n - p < p ? n - p : p, k;
            int32_t x0, x1;
            if (pf.st_mode == SV_REV && (pf.st_rep & 1u) == 0u)
                p = n - 1u - p;
            k = (pf.st_start + p) % REC_LEN;
            x0 = rec_buf[k];
            x1 = rec_buf[k + 1u < REC_LEN ? k + 1u : 0u];
            v = x0 + (((x1 - x0) * (int32_t)(f >> 1)) >> 15);
            if (e < 16u)                             /* (16-sample fades at the slice's ends) */
                v = v * (int32_t)e / 16;
            pf.st_pos += pf.st_rate >> 1;            /* (the recorder at half rate) */
            if ((pf.st_pos >> 16) >= n)
                pf.st_pos -= n << 16;
        }
        pf.st_mix += pf.st_on == 2u ? (pf.st_mix < 32767 ? 1024 : 0) : (pf.st_mix > 0 ? -1024 : 0);
        pf.st_mix = clamp32(pf.st_mix, 0, 32767);
        if (pf.st_mix) {
            l[i] = (int16_t)(l[i] + (((v - l[i]) * pf.st_mix) >> 15));
            r[i] = (int16_t)(r[i] + (((v - r[i]) * pf.st_mix) >> 15));
        }
    }
}

#define RG_LIM (1 << 26)
#define RG_GAIN 45875                                /* (the bands' input, x their damping^0.4: 0.7) */
#define RG_SUB 16u                                   /* (half-rate samples a pass: 32 at 44.1 kHz) */
/* 1.0 Phase 10e: the bands run at half rate (22.05 kHz; all sit under ~5.9 kHz): the mono sum down through a
 * half-band low-pass (11 taps, 3 coefficients: -45 dB from 16.2 kHz, the top band's alias, -0.05 dB at 5.9 kHz), the
 * bands' sums back up through the same (x 2); ~10 samples' delay on the ring, the dry untouched */
#define RG_H0 19781                                  /* (Q16; the centre 1/2) */
#define RG_H1 (-4211)
#define RG_H2 861
/* (1.0 Phase 10d: a band at a time over 16 samples, its coefficients and states held, its pan its own loop: ~40%
 * fewer instructions than a sample at a time through every band) */
static inline __attribute__((always_inline)) void rg_band(int32_t *st, const int32_t *xm, int32_t *yl, int32_t *yr,
                                                          int32_t c, int32_t q, int32_t g, const uint32_t pan)
{
    int32_t s0 = st[0], s1 = st[1];
    uint32_t i;
    for (i = 0; i < RG_SUB; i++) {
        int32_t xin = mulr16(xm[i], g), lp, bp;
        lp = s0 + mulr16(c, s1);
        bp = s1 + mulr16(c, xin - lp - mulr16(q, s1));
        s0 = clamp_hl(lp, -RG_LIM, RG_LIM);
        s1 = bp = clamp_hl(bp, -RG_LIM, RG_LIM);
        if (pan == 0u) {                             /* (the middle) */
            int32_t m = bp - (bp >> 2);
            yl[i] += m;
            yr[i] += m;
        } else if (pan == 1u) {                      /* (left) */
            yl[i] += bp;
            yr[i] += bp >> 2;
        } else {
            yl[i] += bp >> 2;
            yr[i] += bp;
        }
    }
    st[0] = s0;
    st[1] = s1;
}
static int32_t rg_coef(int32_t w)                    /* 2 sin(w / 2) ~ w - w^3 / 24 + w^5 / 1920, w Q16 (to ~1.7) */
{
    int32_t w2 = mulq16(w, w), w3 = mulq16(w2, w);
    return w - w3 / 24 + mulq16(w3, w2) / 1920;
}
static inline __attribute__((always_inline)) int32_t rg_up(const int32_t *y)   /* between y[2] and y[3], Q16 x 2 */
{
    return (int32_t)(((int64_t)(y[2] + y[3]) * (2 * RG_H0) + (int64_t)(y[1] + y[4]) * (2 * RG_H1) +
                      (int64_t)(y[0] + y[5]) * (2 * RG_H2) + 32768) >> 16);
}
static void ring_block(int16_t *l, int16_t *r, int32_t d, int32_t w)
{
    int32_t c[NRB], q[NRB], g[NRB], ts;
    uint32_t root = ring_root(d), nb = 0, k, i, b;
    const int8_t *sh = RING_SH[pfs.dj_tune < NRTUNE ? pfs.dj_tune : 0u];
    ts = (int32_t)(((int64_t)1764 * exp2_q16((int32_t)((int64_t)pf.dj_rs * 5 * 65536 / 32512))) >> 16);   /* T60,
                                                      * half-rate samples */
    for (k = 0; k < NRB && sh[k] >= 0; k++) {
        int32_t n = (int32_t)root + sh[k], wq, cq, cf, cr;
        uint32_t qq;
        wq = (int32_t)(((int64_t)440 * exp2_q16((n - 69) * 65536 / 12) * 411775 / FS) >> 16);   /* 2 pi f / FS */
        if (wq > 55000)                              /* (over ~5.9 kHz: left out) */
            break;
        cq = rg_coef(wq * 2);                        /* (at half rate) */
        qq = 1852156736u / ((uint32_t)ts * ((uint32_t)cq >> 4));   /* 6.9 / (T60 c), Q16: its damping (both / 16:
                                                      * 32 bits) */
        q[nb] = qq > 65536u ? 65536 : qq < 33u ? 33 : (int32_t)qq;
        c[nb] = cq;
        cf = rg_coef(wq);                            /* (a band peaks at 1 / (q cos(w / 2)), w at its own rate: the */
        cr = (int32_t)(log2_q16(65536u - (uint32_t)(mulq16(cq, cq) >> 2)) -   /* input x cos(w) / cos(w / 2) */
                       log2_q16(65536u - (uint32_t)(mulq16(cf, cf) >> 2)));   /* rings as at the full rate; cos^2 =
                                                      * 1 - c^2 / 4: cr is log2 of the ratio squared) */
        g[nb] = mulq16(exp2_q16((log2_q16((uint32_t)q[nb]) - (16 << 16)) * 2 / 5 + cr / 2), RG_GAIN);   /* (q^0.4:
                                                      * a long ring about as loud as a short one) */
        nb++;                                        /* (the bands' pan: 0 the middle, odd right, even left) */
    }
    for (b = 0; b < BLOCK; b += 2u * RG_SUB) {
        int32_t xb[9u + 2u * RG_SUB], xm[RG_SUB], yl[5u + RG_SUB], yr[5u + RG_SUB];
        int16_t *lb = l + b, *rb = r + b;
        memcpy(xb, pf.rg_xh, sizeof pf.rg_xh);
        memcpy(yl, pf.rg_yh[0], sizeof pf.rg_yh[0]);
        memcpy(yr, pf.rg_yh[1], sizeof pf.rg_yh[1]);
        for (i = 0; i < 2u * RG_SUB; i++)
            xb[9u + i] = (lb[i] + rb[i]) << 3;       /* (the mono sum, Q4) */
        for (i = 0; i < RG_SUB; i++) {               /* (down: centred on xb[2i + 5]) */
            const int32_t *x = xb + 2u * i;
            xm[i] = (int32_t)(((int64_t)x[5] * 32768 + (int64_t)(x[4] + x[6]) * RG_H0 +
                               (int64_t)(x[2] + x[8]) * RG_H1 + (int64_t)(x[0] + x[10]) * RG_H2 + 32768) >> 16);
            yl[5u + i] = yr[5u + i] = 0;
        }
        for (k = 0; k < nb; k++) {
            if (k == 0u)
                rg_band(pf.rg_s[k], xm, yl + 5, yr + 5, c[k], q[k], g[k], 0u);
            else if (k & 1u)
                rg_band(pf.rg_s[k], xm, yl + 5, yr + 5, c[k], q[k], g[k], 2u);
            else
                rg_band(pf.rg_s[k], xm, yl + 5, yr + 5, c[k], q[k], g[k], 1u);
        }
        for (i = 0; i < RG_SUB; i++) {               /* (up: even samples between, odd on the half-rate ones) */
            int32_t j, o, v[2][2];
            v[0][0] = rg_up(yl + i);
            v[0][1] = yl[i + 3u];
            v[1][0] = rg_up(yr + i);
            v[1][1] = yr[i + 3u];
            for (j = 0; j < 2; j++) {
                int16_t *pl = lb + 2u * i + (uint32_t)j, *pr = rb + 2u * i + (uint32_t)j;
                o = (*pl >> 1) + (int32_t)(((int64_t)fast_tanh(v[0][j] >> 3) * 24575) >> 16);   /* (0.75: room
                                                      * for the dry) */
                *pl = (int16_t)sat16(*pl + (((o - *pl) * w) >> 15));
                o = (*pr >> 1) + (int32_t)(((int64_t)fast_tanh(v[1][j] >> 3) * 24575) >> 16);
                *pr = (int16_t)sat16(*pr + (((o - *pr) * w) >> 15));
            }
        }
        memcpy(pf.rg_xh, xb + 2u * RG_SUB, sizeof pf.rg_xh);
        memcpy(pf.rg_yh[0], yl + RG_SUB, sizeof pf.rg_yh[0]);
        memcpy(pf.rg_yh[1], yr + RG_SUB, sizeof pf.rg_yh[1]);
    }
}

/* the master (engine_block): the DJ filter, the tape's fade; l, r: the block, saturated */
static void perf_master(int16_t *l, int16_t *r)
{
    int32_t d = clamp32(((int32_t)pf.dj - 64) * 1040 + pf.sweep + gl.dj + sq.ru_hp, -65536, 65536), a = iabs(d), w;
    uint32_t i, c, type;
    pf.dj_d = d;
    {                                                /* 0.24.3: Reso: the damping R from 0.6 (as before) to 0.03, */
        int32_t t = (int32_t)(pfs.dj_reso & 127u) << 8, e = t - pf.dj_rs;   /* on a square law (most of the knob
                                                      * where it bites). 1.0: it glides there (~10 ms): the knob's
                                                      * steps a block at a time clicked, ACID's (its makeup) most */
        uint32_t k, rk;
        int32_t q;
        pf.dj_rs = !a || (e > -8 && e < 8) ? t : pf.dj_rs + e / 8;   /* (no filter: at once) */
        k = (127u << 8) - (uint32_t)pf.dj_rs;        /* (Q8) */
        q = 1966 + (int32_t)((uint64_t)37356u * k * k / (16129u << 16));
        rk = (uint32_t)pf.dj_rs * 516u >> 8;         /* (1.0: ACID's, MS20's: their own curve, 0 .. 1) */
        for (c = 0; c < 2u; c++)
            if (pf.dj_f[c].q != q || pf.dj_f[c].rk != (int32_t)rk) {
                pf.dj_f[c].q = q;
                pf.dj_f[c].rk = (int32_t)rk;
                pf.dj_f[c].dirty = 1;
            }
    }
    if (a > 0 && pfs.dj_ftype == DJT_RING) {         /* 1.0: RING */
        w = a >= 8192 ? 32768 : a * 4;
        if (pf.dj_type != 0xFFu) {                   /* (from rest) */
            memset(pf.rg_s, 0, sizeof pf.rg_s);
            memset(pf.rg_xh, 0, sizeof pf.rg_xh);
            memset(pf.rg_yh, 0, sizeof pf.rg_yh);
            pf.dj_type = 0xFFu;
        }
        ring_block(l, r, d, w);
    } else if (a > 0) {
        type = dj_kind(d);
        w = a >= 8192 ? 32768 : a * 4;               /* (faded in over the first eighth: no click at the middle) */
        if (type != pf.dj_type) {
            svf_reset(&pf.dj_f[0]);
            svf_reset(&pf.dj_f[1]);
            pf.dj_type = (uint8_t)type;
        }
        for (c = 0; c < 2u; c++) {
            svf_t *s = &pf.dj_f[c];
            uint32_t fv = dj_fval(d);
            if (s->fval != fv) {
                s->fval = fv;
                s->dirty = 1;
            }
        }
        dj_block(pf.dj_f, type, l, r, w);            /* (1.0 Phase 10d: both sides, mixed in: dsp.c) */
    }
    perf2_master(l, r);
    if (tape_q16 < 16384u) {                         /* the tape's last quarter of speed: fades to silence */
        int32_t g = (int32_t)tape_q16 * 2;
        for (i = 0; i < BLOCK; i++) {
            l[i] = (int16_t)((l[i] * g) >> 15);
            r[i] = (int16_t)((r[i] * g) >> 15);
        }
    }
}
