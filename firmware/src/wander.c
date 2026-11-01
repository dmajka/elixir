/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: WANDER (40), PERF's third set (ARP: PERFORM > SPELLS > WANDER > GLOBAL MOD): slow change over a long
 * journey (docs/WANDER.md). COPY (OCT+) sets out: home is the pattern playing and a copy of the voices (the kit and
 * the morph target). A QUEST is a chain of chapters, each a mood (Energy, Light, Space, Grit) the road walks towards,
 * planned at the start by a small Markov table (each quest its own), so a quest has a shape but never the same path:
 *   THREADS   1-3 at once (STRIDE's): one track's voice moved one way by a gesture (a few knobs that agree: DARKER,
 *             WETTER ..), a notch every SOUND bars glided over the bar, for a leg of 3-8 notches; the next gesture
 *             by what fits the mood, what follows the last (DARKER: WETTER, LONGER) and the land (the spell picked on
 *             GRIMOIRE: its move tables; NONE every gesture). A knob a thread moves, turned by hand: it lets go.
 *   ENERGY    every RHYTHM bars, at a phrase's start, a step towards the chapter's; each track's DENSITY follows it
 *             (seq.c sq.wdens), the colour first (the hats), the backbone (the kick, the snare) only a little; at the
 *             bottom with RANGE up, the colour mutes (sq.wmute). Nothing jumps: each track's moves a little a bar.
 *   RHYTHM    (41) with it, on an open road (GOAL NONE), the steps change: six candidates (a hit grown as a ghost
 *             that rises, one shed fading, an answer: one grown where another track sheds, a hit moved a step, a
 *             cell of 2-4 steps borrowed from elsewhere, an accent, a chance, a ratchet) scored by the track's own
 *             idiom (35d ECHO's matching: the steps at the same place in the bar after the same 1-3 states), the
 *             hits held near home's, the kick and the snare kept off each other, no version heard just now; STRIDE
 *             the temperature. Every few steps a WAYMARK; MEMORY: now and then the road turns back to one.
 * FOOTPRINTS (41): the road writes into the pattern playing, each step it changes journalled first (its bytes as
 * they were); when another pattern plays, the one left gets them back. So a slot is never changed by travelling.
 * GOAL Pnn: a destination. The steps that differ from its, the colour first and the backbone last, change at each
 * phrase's start, paced to arrive by the JOURNEY's end; then it plays, GOAL reads --, nothing more moves (the voices
 * where the road left them) till another GOAL and COPY (OCT+). A LOCKED or KIT goal's voices: the voices drawn to
 * them a bar at a time. A pattern changed by hand: an arrival there.
 * SHIFT (OCT-) comes home over RETURN bars (twice: at once): the voices glide back to the copy, Energy to none, the
 * rhythm retraced (in another pattern: a journey to home's, then home's); the FATE says what comes back (TREASURE:
 * the leg that moved most kept; WOUND: a track thinner or darker). The JOURNEY's end (not ENDLESS): its FATE; LOST:
 * here is home (KEEP), the road ends. With OSC/MODELS in, at a chapter's edge, a voice's model may change: its
 * level down over a bar, the model, the level back. The pages:
 *   QUEST  QUEST, JOURNEY (bars; ENDLESS), FATE, GOAL (NONE, P01-P32, SONG; -- arrived)
 *   PACE   SOUND (bars a notch; OFF), RHYTHM (bars an Energy step; OFF), RANGE (how far Energy swings), STRIDE
 *   HOME   RETURN (bars; INSTANT), MEMORY (NEVER .. OFTEN), ORDER, SWAY (GOAL SONG's)
 * GOAL SONG (42): the song's rows, each a destination in turn (seq.c song_road: ORDER IN ORDER, ROADS by the song's
 * chances, ANY), each leg the JOURNEY (SWAY: a quarter .. four times it); it never arrives. Play goes to PATTERN.
 * The black keys: the tracks that travel; the white keys: SPELLS' sixteen parts (its own set; HITS lets Energy in);
 * bar keys: 1 ALL / NONE, 2 KEEP (here is home: the voices copied again, Energy and the road written into the
 * pattern), 3 REST (the road waits; again: on; SHIFT (OCT-): POSTCARD, where you are into the next empty pattern),
 * 4 the defaults. A spell cast while travelling is a side journey: the road waits till COPY (OCT+) here, or the
 * spell reversed; coming home, the voices go home from the copy anyway (a cast still to reverse: its pattern back
 * too). Included by ui.c after spells.c. */

#define NQUEST 11u
#define NWJ 7u                                      /* JOURNEY: 16 .. 512 bars, ENDLESS */
#define WJ_ENDLESS 6u
#define NWP 7u                                       /* SOUND, RHYTHM: OFF, 1 .. 16 bars, PHRASE */
#define WP_PHRASE 6u
#define NWR 10u                                      /* RETURN: INSTANT, 1 .. 256 bars */
#define NTHREAD 3u
#define NCHAP 6u                                     /* the chapters a path plans */
#define NGLIDE 12u
#define NFOOT 80u                                    /* (41) the footprints */
#define NWAY 4u                                      /* .. the waymarks */
#define NMOVER 6u                                    /* .. hits rising and fading */
#define WBAR 384u                                    /* ticks a bar (16 steps at x1) */
enum { WM_E, WM_L, WM_S, WM_G, NWM };                /* the moods: Energy, Light, Space, Grit */
enum { WF_HOME, WF_TREASURE, WF_WOUND, WF_LOST, WF_DICE, NWF };
enum { WS_IDLE, WS_ROAD, WS_RETURN };
enum { WV_QUEST, WV_JOURNEY, WV_FATE, WV_GOAL, WV_SOUND, WV_RHYTHM, WV_RANGE, WV_STRIDE, WV_RETURN, WV_MEMORY,
       WV_ORDER, WV_SWAY, NWV };                     /* the cards (VP_WD + n) */
enum { Q_LONG, Q_HOMECOMING, Q_SPIRE, Q_HOLLOW, Q_TIDE, Q_SIDE, Q_LOST, Q_MAZE, Q_ROSARY, Q_POND, Q_WHEEL };
#define QF_SHORT 1u                                  /* (a quest's ways: legs of 2-4 notches) */
#define QF_ANY 2u                                    /* (any gesture, whatever the land) */
#define QF_ONE 4u                                    /* (one thread, long legs, the smallest notches) */
#define QF_MAZE 8u                                   /* (by rule: the tracks in turn, two gestures in turn, a track
                                                      * turned a step; the same seed each time) */
#define QF_ROSARY 16u                                /* (the hits leave one by one, then come back) */
#define QF_FREE 32u                                  /* (the kick's and the snare's rhythm travels too) */
#define QF_WHEEL 64u                                 /* (rhythm only, MEMORY up; the kick's and snare's rhythm too,
                                                      * their anchors kept) */
static const uint16_t WD_BARS[NWJ] = {16, 32, 64, 128, 256, 512, 0};
static const uint8_t WD_PACE[NWP] = {0, 1, 2, 4, 8, 16, 0};
static const uint16_t WD_RET[NWR] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256};
#define WG_SONG (NUM_PATTERN + 1u)                   /* (42) GOAL SONG */
enum { WO_ORDER, WO_ROADS, WO_ANY, NWO };            /* .. ORDER: the rows in order, by the song's chances, any */
static const uint8_t WD_SWAY[17] = {16, 19, 23, 27, 32, 38, 45, 54, 64, 76, 91, 108, 128, 152, 181, 215, 255};   /* (a
                                                      * leg's time, x64: a quarter .. four times the JOURNEY) */
#define WD_MASK0 (SC_ENV | SC_FILTER | SC_FX | SC_MOD | SC_HITS | SC_ACC | SC_CHANCE)   /* (the white keys at first: not
                                                      * PITCH, OSC, the drastic ones) */

/* the gestures: up to three roles (spells.c's) moved together, by d a notch (STRIDE's middle), and how each moves
 * the moods; nx: the gestures that often follow it */
typedef struct { const char *name; uint8_t r[3]; int8_t d[3]; int8_t m[NWM]; uint8_t nx[2]; } gest_t;
enum { G_DARK, G_BRIGHT, G_LONG, G_SHORT, G_WET, G_DRY, G_DIRT, G_CLEAN, G_METAL, G_ROUND, G_SNAP, G_SOFT, G_RING,
       G_WIDE, G_RESTLESS, G_STILL, G_HIGH, G_LOW, G_CRUSH, G_WARM, NGEST };
#define RN NROLE                                     /* (none) */
static const gest_t GEST[NGEST] = {
    {"DARKER", {R_FFREQ, R_TILT, R_RESO}, {-6, -4, 1}, {0, -3, 0, 0}, {G_WET, G_LONG}},
    {"BRIGHTER", {R_FFREQ, R_TILT, RN}, {6, 4, 0}, {0, 3, 0, 0}, {G_SNAP, G_DRY}},
    {"LONGER", {R_DEC, R_RVS, RN}, {5, 1, 0}, {-1, 0, 2, 0}, {G_WET, G_DARK}},
    {"SHORTER", {R_DEC, RN, RN}, {-5, 0, 0}, {2, 0, -2, 0}, {G_SNAP, G_DRY}},
    {"WETTER", {R_RVS, R_FXS, RN}, {6, 3, 0}, {0, 0, 3, 0}, {G_LONG, G_DARK}},
    {"DRIER", {R_RVS, R_FXS, RN}, {-6, -3, 0}, {1, 0, -3, 0}, {G_SHORT, G_BRIGHT}},
    {"DIRTIER", {R_DRIVE, R_FDRIVE, RN}, {5, 4, 0}, {1, 0, 0, 3}, {G_CRUSH, G_METAL}},
    {"CLEANER", {R_DRIVE, R_FDRIVE, R_BITS}, {-5, -4, -1}, {0, 0, 0, -3}, {G_ROUND, G_STILL}},
    {"METALLIC", {R_FMAMT, R_FMFREQ, RN}, {6, 3, 0}, {0, 1, 0, 2}, {G_RING, G_DIRT}},
    {"ROUNDER", {R_FMAMT, RN, RN}, {-6, 0, 0}, {0, -1, 0, -2}, {G_WARM, G_CLEAN}},
    {"SNAPPIER", {R_TVOL, R_MODAMT, RN}, {6, 3, 0}, {2, 1, 0, 0}, {G_SHORT, G_BRIGHT}},
    {"SOFTER", {R_TVOL, R_ATK, RN}, {-6, 2, 0}, {-2, -1, 0, 0}, {G_LONG, G_ROUND}},
    {"RINGING", {R_RESO, RN, RN}, {5, 0, 0}, {0, 1, 0, 1}, {G_METAL, G_WET}},
    {"WIDER", {R_RNDPAN, RN, RN}, {6, 0, 0}, {0, 0, 2, 0}, {G_WET, G_RESTLESS}},
    {"RESTLESS", {R_RNDF, R_RNDD, RN}, {5, 4, 0}, {1, 0, 0, 1}, {G_WIDE, G_DIRT}},
    {"STILLER", {R_RNDF, R_RNDD, R_RNDPAN}, {-5, -4, -4}, {-1, 0, 0, -1}, {G_CLEAN, G_DARK}},
    {"HIGHER", {R_COARSE, RN, RN}, {1, 0, 0}, {0, 2, 0, 0}, {G_BRIGHT, G_SNAP}},
    {"LOWER", {R_COARSE, RN, RN}, {-1, 0, 0}, {0, -2, 0, 0}, {G_DARK, G_LONG}},
    {"CRUSHED", {R_BITS, R_DECIM, RN}, {1, -6, 0}, {0, 0, 0, 3}, {G_DIRT, G_RESTLESS}},
    {"WARMER", {R_TILT, R_DRIVE, RN}, {-5, 2, 0}, {0, -1, 0, 1}, {G_DARK, G_LONG}}};

/* the quests: chapters (a mood, a weight: its share of the JOURNEY, ENDLESS 8 bars a unit; the chapters that may
 * follow, with their chances; CH_END: the road ends) */
#define CH_END 0xFFu
typedef struct { const char *name; int8_t aim[NWM]; uint8_t w, nx[2], pr; } chap_t;   /* (pr: nx[0]'s chance /128) */
typedef struct {
    const char *name, *line;                         /* its name, what it does */
    uint8_t fate, journey, fl, nch;                  /* its FATE and JOURNEY at first; QF_*; its chapters */
    const chap_t *ch;
} quest_t;
static const chap_t CH_LONG[1] = {{"THE ROAD", {0, 0, 0, 0}, 4, {CH_END, CH_END}, 128}};
static const chap_t CH_HOMECOMING[4] = {{"SETTING OUT", {20, 20, 0, 0}, 3, {1, 2}, 64},
                                        {"FAR FIELDS", {-10, -20, 50, 0}, 4, {2, 3}, 52},
                                        {"STRANGE TOWN", {40, 10, -20, 45}, 4, {1, 3}, 52},
                                        {"THE WAY BACK", {0, 0, 0, 0}, 3, {CH_END, CH_END}, 128}};
static const chap_t CH_SPIRE[4] = {{"FOOTHILLS", {25, 20, 0, 5}, 4, {1, 1}, 128},
                                   {"THE CLIMB", {55, 45, -10, 20}, 4, {2, 2}, 128},
                                   {"THE SUMMIT", {90, 75, -25, 40}, 2, {3, 3}, 128},
                                   {"THE WAY DOWN", {5, 10, 40, 0}, 4, {CH_END, CH_END}, 128}};
static const chap_t CH_HOLLOW[4] = {{"CAVE MOUTH", {-35, -35, 30, 0}, 3, {1, 1}, 128},
                                    {"THE DEEP", {-95, -70, 80, 10}, 4, {2, 2}, 128},
                                    {"ECHOES", {-60, -45, 95, 15}, 3, {3, 3}, 128},
                                    {"INTO THE LIGHT", {15, 40, 10, 0}, 3, {CH_END, CH_END}, 128}};
static const chap_t CH_TIDE[3] = {{"EBB", {-50, -10, 60, 0}, 4, {1, 1}, 128},
                                  {"FLOOD", {40, 25, -25, 10}, 4, {0, 2}, 100},
                                  {"SLACK WATER", {0, 0, 20, 0}, 2, {0, 0}, 128}};
static const chap_t CH_SIDE[4] = {{"THE VILLAGE", {0, 0, 0, 0}, 2, {1, 2}, 64},
                                  {"THE MARSHES", {-30, -35, 60, 10}, 3, {0, 3}, 100},
                                  {"THE FAIR", {55, 30, -15, 25}, 3, {0, 3}, 100},
                                  {"THE OLD TOWER", {-10, -40, 35, 60}, 3, {0, 0}, 128}};
static const chap_t CH_LOST[4] = {{"THE FOG", {-20, -30, 55, 20}, 2, {1, 2}, 64},
                                  {"THE BRAMBLES", {45, -10, -20, 60}, 2, {2, 3}, 64},
                                  {"GLASS PLAINS", {15, 60, 30, -25}, 2, {3, 0}, 64},
                                  {"THE DEEP WOOD", {-55, -50, 35, 5}, 2, {0, 1}, 64}};
static const chap_t CH_MAZE[2] = {{"THE CORRIDORS", {10, 0, 0, 10}, 4, {1, 1}, 128},
                                  {"THE TURNING", {-10, 10, 10, 0}, 4, {0, 0}, 128}};
static const chap_t CH_ROSARY[2] = {{"TELLING DOWN", {0, -15, 25, 0}, 4, {1, 1}, 128},
                                    {"TELLING UP", {0, 10, 0, 0}, 4, {CH_END, CH_END}, 128}};
static const chap_t CH_POND[2] = {{"STILL WATER", {0, -15, 25, 0}, 4, {1, 1}, 128},
                                  {"A RIPPLE", {5, 5, 35, 5}, 2, {0, 0}, 128}};
static const chap_t CH_WHEEL[2] = {{"THE WHEEL", {10, 0, 0, 0}, 4, {1, 1}, 128},
                                   {"THE WHEEL TURNS", {-10, 0, 10, 0}, 4, {0, 0}, 128}};
#define QCH(a) sizeof a / sizeof a[0], a
static const quest_t QUEST[NQUEST] = {
    {"THE LONG ROAD", "A steady walk, one way", WF_LOST, 3, 0, QCH(CH_LONG)},
    {"HOMECOMING", "Out, around, and home", WF_HOME, 3, 0, QCH(CH_HOMECOMING)},
    {"THE SPIRE", "Up to the summit and down", WF_TREASURE, 4, 0, QCH(CH_SPIRE)},
    {"THE HOLLOW HILL", "Down into the dark, out", WF_HOME, 3, 0, QCH(CH_HOLLOW)},
    {"THE TIDE", "In and out, never arriving", WF_LOST, WJ_ENDLESS, 0, QCH(CH_TIDE)},
    {"SIDE QUESTS", "Detours, each partway home", WF_DICE, 4, 0, QCH(CH_SIDE)},
    {"THE LOST WIZARD", "Anywhere at all, never home", WF_LOST, WJ_ENDLESS, QF_SHORT | QF_ANY | QF_FREE, QCH(CH_LOST)},
    {"CLOCKWORK MAZE", "By rule, not chance", WF_HOME, 3, QF_MAZE, QCH(CH_MAZE)},
    {"THE ROSARY", "Hit by hit away, and back", WF_HOME, 3, QF_ROSARY | QF_FREE, QCH(CH_ROSARY)},
    {"THE STILL POND", "Barely moving, for a long time", WF_LOST, 5, QF_ONE, QCH(CH_POND)},
    {"TURNING WHEEL", "An endless beat, turning", WF_LOST, WJ_ENDLESS, QF_WHEEL, QCH(CH_WHEEL)}};
/* THE LONG ROAD's ways (one is picked as it sets out) */
#define WAY(n, e, l, s, g) {n, {e, l, s, g}, 4, {CH_END, CH_END}, 128}
static const chap_t WD_WAY[6] = {WAY("TO DUSK", -30, -60, 40, 0), WAY("TO DAWN", 30, 60, -20, 0),
                                 WAY("TO THE FORGE", 40, 0, -20, 70), WAY("INTO MIST", -20, -20, 80, 0),
                                 WAY("TO MARKET", 70, 30, 0, 20), WAY("OVER THE MOOR", -40, -30, 30, 30)};
static const char *const WF_NAME[NWF] = {"HOME", "TREASURE", "WOUND", "LOST", "DICE"};
/* the tracks Energy moves first (the colour: the hats) to last (the backbone: the snare, the kick) */
static const uint8_t WD_ORDER[NTRACK] = {5, 6, 4, 2, 1, 3, 0};
static int wd_backbone(uint32_t t) { return t == 0u || t == 3u; }

typedef struct { uint8_t on, t, g, left; uint16_t moved; } thread_t;
typedef struct { uint8_t t, s; step_t x; uint8_t tim, lhi, cond, rat; } foot_t;   /* (41) a step's bytes (t: its
                                                                                  * track, bit 7 on) */
typedef struct { uint16_t p; uint8_t tg, last, th; uint32_t due, every; } glide_t;   /* (p 0: free; th: its thread
                                                                                      * + 1, 0 none) */
static struct {
    uint8_t k[NWV];                                  /* the knobs (WV_*) */
    uint8_t tracks;                                  /* the tracks that travel (bit t) */
    uint16_t mask;                                   /* the parts (SC_*: the white keys) */
    uint16_t wdown;                                  /* (the white keys pressed on WANDER) */
    uint8_t state, rest, side;                       /* WS_*; REST; a spell's side journey */
    uint8_t home_p, home_loads;                      /* the home pattern; kit_loads when the voices were copied */
    uint8_t quest, way;                              /* the quest set out on; THE LONG ROAD's way */
    uint8_t path[NCHAP], npath, chap;                /* the chapters planned; which one */
    uint32_t bars, pbase;                            /* bars travelled; ENDLESS: the path's first */
    uint32_t lastbar;                                /* the bar last seen (sq.ticks / WBAR + 1; 0 none) */
    uint8_t sbars, rbars;                            /* bars since the last notch, the last Energy step */
    uint8_t rdue;                                    /* an Energy step due at the next phrase's start */
    uint16_t lastpt;                                 /* sq.ptick last seen (a phrase begins: it wraps) */
    int8_t aim[NWM];                                 /* the chapter's mood */
    int16_t cur[NWM];                                /* where the road has got to (home 0), -100..100 */
    int8_t lvl;                                      /* Energy's step, -100..100 */
    int8_t dt[NTRACK];                               /* each track's DENSITY, as Energy wants it */
    uint8_t wantmute;
    thread_t th[NTHREAD];
    uint8_t lastg, lastt;                            /* the gesture and track last ended (the chain's) */
    uint8_t best_t, best_g;                          /* TREASURE: the leg that moved most */
    uint16_t best;
    uint8_t fate;                                    /* the FATE a return brings (WF_*) */
    uint16_t ret_left, ret_i;                        /* RETURN: steps to go; the sweep's next kit index (0 none) */
    uint32_t ret_due;
    uint8_t ret_dens;                                /* .. bars to bring Energy home in */
    uint16_t ret_n, ret_f;                           /* .. its steps; how far along the road it began (the screen's) */
    char said[32];                                   /* what the last end said (the screen's) */
    /* 41: the destination (its pattern + 1; 0 an open road), there (nothing moves), GOAL reading --, its change
     * queued, the footprints full; the steps still to change (the screen's) */
    uint8_t goal, arrived, gdash, going, full;
    uint16_t gl;
    uint8_t slot, nfoot;                             /* the pattern the footprints are in; how many */
    uint8_t nwm, wmi, rsteps;                        /* the waymarks kept, the next; rhythm steps taken */
    uint8_t nh[NTRACK];                              /* each track's hits at home (the band the rhythm holds) */
    struct { uint8_t t, s, tg; } mv[NMOVER];         /* hits rising (to velocity tg) or fading (tg 0); t: track + 1 */
    uint32_t recent[4];                              /* the last versions (wd_hash) */
    uint8_t mz[2], mzn;                              /* MAZE: its two gestures, the legs taken */
    uint8_t rosk;                                    /* ROSARY: the hits a step */
    uint8_t ms_t, ms_ph, ms_md, ms_vol;              /* a model swap: its track + 1, the bars into it, the model,
                                                      * the level to go back to */
    char rmove[16];                                  /* the rhythm's last move (the screen's) */
    /* 42: GOAL SONG: the row the road heads for, the one before it, its pattern; the leg's bars, its first bar */
    uint8_t srow, sprv, sdest;
    uint16_t sleg;
    uint32_t lbar0;
} wd POOL;
static glide_t wgl[NGLIDE] POOL;
static uint8_t wd_home[KIT_LEN] POOL, wd_mhome[KIT_LEN] POOL;   /* the voices at home (the kit, the morph target) */
static foot_t wd_fp[NFOOT] POOL;                     /* (41) the footprints: the slot's steps as they were */
static uint16_t wd_wm[NWAY][NTRACK][4] POOL;         /* .. the waymarks: the hits at a place on the road */
static uint32_t wd_x = 0x9E3779B9u;

static void wd_pname(char *b, uint32_t p)            /* "P07" */
{
    b[0] = 'P';
    b[1] = (char)('0' + (p + 1u) / 10u);
    b[2] = (char)('0' + (p + 1u) % 10u);
    b[3] = 0;
}
static void wd_pmsg(const char *a, uint32_t p)       /* a message ending in a pattern ("ARRIVED: P04") */
{
    char b[32];
    put_str(b, a, sizeof b);
    wd_pname(b + str_len(b), p);
    ui_message(b);
}
static uint32_t wd_rnd(uint32_t n)                   /* 0 .. n - 1 */
{
    wd_x ^= wd_x << 13;
    wd_x ^= wd_x >> 17;
    wd_x ^= wd_x << 5;
    return n ? (wd_x >> 4) % n : 0u;
}
static const uint8_t WD_K0[NWV] = {Q_HOMECOMING, 3, WF_HOME, 0, 2, 4, 64, 48, 4, 24, 0, 0};
static void wd_knobs0(void)
{
    memcpy(wd.k, WD_K0, NWV);
    wd.tracks = (uint8_t)((1u << NTRACK) - 1u);
    wd.mask = WD_MASK0;
}
static void wd_defaults(void)
{
    memset(&wd, 0, sizeof wd);
    memset(wgl, 0, sizeof wgl);
    wd_knobs0();
    memset(sq.wdens, 0, sizeof sq.wdens);
    sq.wmute = 0;
}
static uint32_t wd_journey(void) { return WD_BARS[wd.k[WV_JOURNEY] % NWJ]; }   /* (0: ENDLESS) */
static int wd_song(void) { return wd.goal == WG_SONG; }
static uint32_t wd_trip(void) { return wd_song() ? wd.sleg : wd_journey() ? wd_journey() : 128u; }   /* a
                                                      * destination's (ENDLESS: 128; a song's: this leg's) */
static uint32_t wd_tb(void) { return wd.bars - wd.lbar0; }   /* bars into it */
static uint32_t wd_dest(void) { return (wd_song() ? wd.sdest : wd.goal - 1u) % NUM_PATTERN; }
static uint32_t wd_stride(void) { return wd.k[WV_STRIDE]; }
static uint32_t wd_qfl(void) { return QUEST[wd.quest % NQUEST].fl; }
static int wd_gliding(void) { return wd.state == WS_ROAD && !wd.rest && !wd.side; }
static int wd_moving(void) { return wd_gliding() && !wd.arrived; }
static uint32_t wd_bar_ms(void) { return 240000u / seq_bpm(); }
static const chap_t *wd_chap(void)
{
    const quest_t *Q = &QUEST[wd.quest % NQUEST];
    return wd.quest == Q_LONG ? &WD_WAY[wd.way % 6u] : &Q->ch[wd.path[wd.chap % NCHAP] % Q->nch];
}

/* ------------------------------------------------------------ voices --- */
static void wd_snap(void)                            /* the voices as they are: home */
{
    uint32_t i;
    for (i = 0; i < KIT_LEN; i++) {
        wd_home[i] = par[kit_par(i)];
        wd_mhome[i] = morph_par[i];
    }
    wd.home_loads = kit_loads;
    wd.best = 0;
}
static int wd_home_loads_ok(void) { return wd.home_loads == kit_loads; }
static int wd_par_glides(uint32_t p)                 /* a value that glides home (not a switch: a type, a model) */
{
    uint32_t d = par_dtype(p);
    if ((p >= PAR_MODEL1 && p < PAR_MODEL1 + 6u) || (p >= PAR_INS1 && p < PAR_INS1 + 6u))
        return 0;
    return d == DTYPE_0B127 || d == DTYPE_0B255 || d == DTYPE_PM63 || d == DTYPE_PM100 || d == DTYPE_0B100 ||
           d == DTYPE_0B15 || d == DTYPE_1B16 || d == DTYPE_1B64 || d == DTYPE_NOTE_NAME;
}
static void wd_glides_off(void)
{
    memset(wgl, 0, sizeof wgl);
    memset(wd.th, 0, sizeof wd.th);
    wd.ms_t = 0;
}
/* a role's bounds on voice v: its range, D1 a kick's (spells.c KK_LIM), D2 D3 the toms' registers, the sends
 * within SPELLS' LIMITS, a resonance short of whistling */
static void wd_bounds(uint32_t r, uint32_t v, uint32_t p, int32_t *lo, int32_t *hi)
{
    uint32_t k;
    *lo = (int32_t)vp_min(p);
    *hi = (int32_t)vp_max(p);
    if (v == 0u)
        for (k = 0; k < NKK; k++)
            if (KK_PAR[k] == p) {
                *lo = KK_LIM[k][0];
                *hi = KK_LIM[k][1];
            }
    if (r == R_COARSE && (v == 1u || v == 2u)) {
        *lo = TOM[v - 1u][0];
        *hi = TOM[v - 1u][1];
    }
    if (r == R_RVS || r == R_FXS)
        *hi = sp.lim[r == R_RVS ? SPL_RVS : SPL_FXS];
    if (r == R_RESO)
        *hi = v == 0u ? 72 : 100;
    if (r == R_DRIVE || r == R_FDRIVE)
        *hi = 110;
    if (r == R_BITS)
        *hi = 10;
    if (r == R_FFREQ && v == 0u)
        *lo = 40;
    if (*hi < *lo)
        *hi = *lo;
}
/* the parameter role r moves on track t's voice, if the white keys let it and it is heard (0: none) */
static uint32_t wd_par(uint32_t r, uint32_t t)
{
    uint32_t v = track_voice(t), p = r < NROLE ? sp_role_par(r, v) : 0u;
    if (!p || !(wd.mask & sp_role_cat(r)) || par_unheard(t, p) || (v == 0u && r == R_COARSE))
        return 0;
    return p;
}
static uint32_t wd_notch(uint32_t d)                 /* a notch, STRIDE's: half .. twice d */
{
    uint32_t n = d * (4u + wd_stride() * 12u / 127u) / ((wd_qfl() & QF_ONE) ? 16u : 8u);   /* (THE STILL POND:
                                                      * half) */
    return n ? n : 1u;
}
static int wd_can(uint32_t t, uint32_t g)            /* gesture g can still move track t */
{
    uint32_t i, v = track_voice(t);
    for (i = 0; i < 3u; i++) {
        uint32_t p = wd_par(GEST[g].r[i], t);
        int32_t lo, hi;
        if (!p || !GEST[g].d[i])
            continue;
        wd_bounds(GEST[g].r[i], v, p, &lo, &hi);
        if (GEST[g].d[i] > 0 ? (int32_t)par[p] < hi : (int32_t)par[p] > lo)
            return 1;
    }
    return 0;
}
static int wd_land(uint32_t g)                       /* the land (the spell on GRIMOIRE) has gesture g's moves */
{
    const spell_t *S = &SPELL[sp_spell() % NSPELL];
    uint32_t i, k;
    if (!S->nmv || (wd_qfl() & QF_ANY))              /* (NONE, SKELETON: every gesture; THE LOST WIZARD's too) */
        return 1;
    for (i = 0; i < S->nmv; i++)
        for (k = 0; k < 3u; k++)
            if (S->mv[i].r == GEST[g].r[k])
                return 1;
    return 0;
}
static void wd_glide(uint32_t p, int32_t tg, uint32_t th)   /* p to tg over a bar */
{
    uint32_t i, f = NGLIDE, n = (uint32_t)(tg > par[p] ? tg - par[p] : par[p] - tg);
    if (!n)
        return;
    for (i = 0; i < NGLIDE; i++) {
        if (wgl[i].p == p) {
            f = i;
            break;
        }
        if (!wgl[i].p && f == NGLIDE)
            f = i;
    }
    if (f == NGLIDE) {                               /* (no room: at once) */
        morph_follow(p, tg - (int32_t)par[p]);
        par_set(p, (uint32_t)tg);
        return;
    }
    wgl[f].p = (uint16_t)p;
    wgl[f].tg = (uint8_t)tg;
    wgl[f].last = par[p];
    wgl[f].th = (uint8_t)th;
    wgl[f].every = wd_bar_ms() / n;
    wgl[f].due = fm1_ms + wgl[f].every;
}
static void wd_leg_end(uint32_t i)
{
    thread_t *h = &wd.th[i];
    uint32_t k;
    if (!h->on)
        return;
    if (h->moved > wd.best) {                        /* (TREASURE's) */
        wd.best = h->moved;
        wd.best_t = h->t;
        wd.best_g = h->g;
    }
    wd.lastg = h->g;
    wd.lastt = h->t;
    h->on = 0;
    for (k = 0; k < NGLIDE; k++)
        if (wgl[k].th == i + 1u)
            wgl[k].th = 0;
}
/* a new thread: the track and gesture that fit best, by the mood still to go, the chain from the last gesture, the
 * land, a hand-on (the same gesture on the next track), with STRIDE's chance */
static void wd_pick(uint32_t i)
{
    uint32_t t, g, k, bt = NTRACK, bg = 0, heat = 24u + wd_stride(), fl = wd_qfl();
    int32_t best = -99999;
    if (fl & QF_MAZE) {                              /* (THE CLOCKWORK MAZE: the next track, its two gestures in turn) */
        for (k = 1; k <= NTRACK && bt >= NTRACK; k++) {
            t = (wd.lastt + k) % NTRACK;
            g = wd.mz[wd.mzn & 1u];
            if (((wd.tracks >> t) & 1u) && wd_can(t, g)) {
                bt = t;
                bg = g;
                wd.lastt = (uint8_t)t;
            }
        }
        wd.mzn++;
        heat = 0;
    }
    for (t = 0; t < NTRACK && heat; t++) {
        uint32_t busy = 0;
        if (!((wd.tracks >> t) & 1u))
            continue;
        for (k = 0; k < NTHREAD; k++)
            busy |= k != i && wd.th[k].on && wd.th[k].t == t;
        for (g = 0; g < NGEST; g++) {
            int32_t s = 0;
            if (!wd_can(t, g))
                continue;
            for (k = 0; k < NWM; k++)
                s += GEST[g].m[k] * ((int32_t)wd.aim[k] - wd.cur[k]) / 4;
            s += g == GEST[wd.lastg].nx[0] || g == GEST[wd.lastg].nx[1] ? 40 : 0;
            s += wd_land(g) ? 24 : -60;
            s += g == wd.lastg && t != wd.lastt && (t + 1u == wd.lastt || t == wd.lastt + 1u) ? 30 : 0;   /* (hands on) */
            s -= g == wd.lastg && t == wd.lastt ? 50 : 0;
            s -= busy ? 80 : 0;
            s += (int32_t)wd_rnd(heat);
            if (s > best) {
                best = s;
                bt = t;
                bg = g;
            }
        }
    }
    if (bt >= NTRACK)
        return;
    wd.th[i].on = 1;
    wd.th[i].t = (uint8_t)bt;
    wd.th[i].g = (uint8_t)bg;
    wd.th[i].left = (uint8_t)(fl & QF_SHORT ? 2u + wd_rnd(3) : fl & QF_ONE ? 8u + wd_rnd(8) : 3u + wd_rnd(6));
    wd.th[i].moved = 0;
}
static void wd_step(uint32_t i)                      /* thread i's notch */
{
    thread_t *h = &wd.th[i];
    const gest_t *G = &GEST[h->g];
    uint32_t k, v = track_voice(h->t), moved = 0;
    for (k = 0; k < 3u; k++) {
        uint32_t p = wd_par(G->r[k], h->t), n;
        int32_t lo, hi, tg;
        if (!p || !G->d[k])
            continue;
        wd_bounds(G->r[k], v, p, &lo, &hi);
        n = wd_notch((uint32_t)(G->d[k] < 0 ? -G->d[k] : G->d[k]));
        if (G->r[k] == R_COARSE || G->r[k] == R_BITS)
            n = 1;                                   /* (a semitone, a bit at a time) */
        tg = clamp32((int32_t)par[p] + (G->d[k] < 0 ? -(int32_t)n : (int32_t)n), lo, hi);
        moved += (uint32_t)(tg > par[p] ? tg - par[p] : par[p] - tg);
        wd_glide(p, tg, i + 1u);
    }
    for (k = 0; k < NWM; k++)
        wd.cur[k] = (int16_t)clamp32(wd.cur[k] + G->m[k] * (int32_t)(4u + wd_stride() * 12u / 127u) / 4, -100, 100);
    h->moved = (uint16_t)(h->moved + moved);
    if (!moved || !--h->left)
        wd_leg_end(i);
}
static uint32_t wd_nthreads(void)
{
    return (wd_qfl() & QF_ONE) ? 1u : 1u + (wd_stride() >= 43u) + (wd_stride() >= 86u);
}
static void wd_sound(void)                           /* SOUND's bar: each thread a notch (new ones as they end) */
{
    uint32_t i;
    for (i = 0; i < NTHREAD; i++) {
        if (i >= wd_nthreads()) {
            wd_leg_end(i);
            continue;
        }
        if (!wd.th[i].on)
            wd_pick(i);
        if (wd.th[i].on)
            wd_step(i);
    }
}
static void wd_glides(void)                          /* each ms: the notches gliding; a knob turned by hand lets go */
{
    uint32_t i;
    for (i = 0; i < NGLIDE; i++) {
        glide_t *g = &wgl[i];
        if (!g->p)
            continue;
        if (par[g->p] != g->last) {                  /* (turned: you win) */
            if (g->th)
                wd_leg_end(g->th - 1u);
            g->p = 0;
            continue;
        }
        if (!wd_gliding() || (int32_t)(fm1_ms - g->due) < 0)
            continue;
        {
            int32_t d = g->tg > par[g->p] ? 1 : -1;
            morph_follow(g->p, d);
            par_set(g->p, (uint32_t)((int32_t)par[g->p] + d));
            g->last = par[g->p];
            g->due += g->every ? g->every : 1u;
            if (par[g->p] == g->tg)
                g->p = 0;
        }
    }
}

/* ------------------------------------------------------------ energy --- */
static uint32_t wd_depth(int32_t e)                  /* RANGE: DENSITY at Energy's ends (+-6 .. +-64) */
{
    return 6u + wd.k[WV_RANGE] * (e < 0 ? 58u : 57u) / 127u;
}
static void wd_targets(void)                         /* each track's DENSITY for Energy now (and the mutes) */
{
    int32_t e = wd.lvl, a = e < 0 ? -e : e, dp = (int32_t)wd_depth(e);
    uint32_t i, n = 0, r = 0;
    wd.wantmute = 0;
    for (i = 0; i < NTRACK; i++)
        n += ((wd.tracks >> WD_ORDER[i]) & 1u) && !wd_backbone(WD_ORDER[i]);
    for (i = 0; i < NTRACK; i++) {
        uint32_t t = WD_ORDER[i];
        int32_t x;
        if (!((wd.tracks >> t) & 1u) || !(wd.mask & SC_HITS)) {
            wd.dt[t] = 0;
            continue;
        }
        if (wd_backbone(t)) {                        /* (the backbone kept: a ghost more, a ghost fewer) */
            wd.dt[t] = (int8_t)clamp32(e * dp / 200, -20, 24);
            continue;
        }
        x = clamp32((a - 50 * (int32_t)r / (int32_t)n) * 2, 0, 100);   /* (one after another, overlapping) */
        wd.dt[t] = (int8_t)clamp32((e < 0 ? -x : x) * dp / 100, -64, 63);
        if (e <= -75 && x >= 100 && wd.k[WV_RANGE] >= 96u)
            wd.wantmute |= (uint8_t)(1u << t);
        r++;
    }
}
static void wd_dens_bar(uint32_t most)               /* a bar: each track's DENSITY a little towards its own */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        int32_t d = sq.wdens[t], g = wd.dt[t];
        d += clamp32(g - d, -(int32_t)most, (int32_t)most);
        sq.wdens[t] = (int8_t)d;
        if (((wd.wantmute >> t) & 1u) && d <= -60)
            sq.wmute |= (uint8_t)(1u << t);
        else if (!((wd.wantmute >> t) & 1u))
            sq.wmute &= (uint8_t)~(1u << t);
    }
}
static void wd_energy(void)                          /* RHYTHM's step: Energy towards the chapter's */
{
    int32_t st = 8 + (int32_t)(wd_stride() * 24u / 127u), g = (wd_qfl() & QF_ROSARY) ? 0 : wd.aim[WM_E];
    wd.lvl = (int8_t)(wd.lvl + clamp32(g - wd.lvl, -st, st));
    wd_targets();
}

/* ------------------------------------------------------- footprints --- */
static void wd_get(const pattern_t *p, uint32_t t, uint32_t s, foot_t *f)
{
    f->t = (uint8_t)(t | (uint32_t)main_on(p, t, s) << 7);
    f->s = (uint8_t)s;
    f->x = p->step[t][s];
    f->tim = p->timing[t][s];
    f->lhi = p->lhi[t][s];
    f->cond = p->cond[t][s];
    f->rat = p->rat[t][s];
}
static void wd_put(pattern_t *p, uint32_t t, uint32_t s, const foot_t *f)
{
    p->step[t][s] = f->x;
    p->timing[t][s] = f->tim;
    p->lhi[t][s] = f->lhi;
    p->cond[t][s] = f->cond;
    p->rat[t][s] = f->rat;
    main_set(p, t, s, f->t >> 7);
}
static int wd_fpi(uint32_t t, uint32_t s)            /* step (t, s)'s footprint (-1: none) */
{
    uint32_t i;
    for (i = 0; i < wd.nfoot; i++)
        if ((wd_fp[i].t & 7u) == t && wd_fp[i].s == s)
            return (int)i;
    return -1;
}
static int wd_mark(uint32_t t, uint32_t s)           /* step (t, s) journalled before the road changes it (0: no room) */
{
    if (wd_fpi(t, s) >= 0)
        return 1;
    if (wd.nfoot >= NFOOT) {
        wd.full = 1;
        return 0;
    }
    wd_get(&pat[wd.slot % NUM_PATTERN], t, s, &wd_fp[wd.nfoot++]);
    return 1;
}
static void wd_retrace(uint32_t n)                   /* the last n footprints retraced: those steps as they were */
{
    while (n-- && wd.nfoot) {
        const foot_t *f = &wd_fp[--wd.nfoot];
        wd_put(&pat[wd.slot % NUM_PATTERN], f->t & 7u, f->s, f);
    }
    wd.full = 0;
    memset(wd.mv, 0, sizeof wd.mv);
}
static uint32_t wd_hits(const pattern_t *p, uint32_t t)
{
    uint32_t s, n = 0, len = track_len(p, t);
    for (s = 0; s < len; s++)
        n += (uint32_t)main_on(p, t, s);
    return n;
}
static void wd_count_home(void)                      /* the hits the rhythm holds near */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++)
        wd.nh[t] = (uint8_t)wd_hits(&pat[sq.active], t);
}

/* ------------------------------------------------------------ rhythm --- */
/* the tracks whose rhythm wanders: the black keys' but a live generator's; the kick and the snare only on THE LOST
 * WIZARD, THE ROSARY, THE TURNING WHEEL */
static uint32_t wd_rtracks(void)
{
    uint32_t m = wd.tracks, t;
    if (!(wd_qfl() & (QF_FREE | QF_WHEEL)))
        m &= ~0x09u;
    for (t = 0; t < NTRACK; t++)
        if (gen_live(&pat[sq.active], t))
            m &= ~(1u << t);
    return m;
}
static int wd_keep_at(uint32_t t, uint32_t s)        /* a hit the road leaves be: the kick's beats, the backbeat */
{
    return sp_anchor(t, s) && !(wd_qfl() & QF_FREE);
}
static uint32_t wd_cls(const pattern_t *p, uint32_t t, uint32_t s)   /* REST, GHOST, HIT, ACCENT */
{
    uint32_t v = p->step[t][s].volume & STEP_VOLUME_MASK;
    return !main_on(p, t, s) ? 0u : v < 50u ? 1u : v < 100u ? 2u : 3u;
}
static uint32_t wd_mc(uint32_t s)                    /* its place: the bar's first, a beat, an 8th, a 16th */
{
    s &= 15u;
    return !s ? 3u : !(s & 3u) ? 2u : !(s & 1u) ? 1u : 0u;
}
/* the track's own idiom (35d ECHO's matching): of its other steps at s's place in the bar after the same 1-3 states
 * as s's (order 3, backing off to 2, 1), the share that are on (or rests), 0..64 */
static uint32_t wd_style(const pattern_t *p, uint32_t t, uint32_t s, int on)
{
    uint32_t len = track_len(p, t), o, j, k;
    for (o = 3; o; o--) {
        uint32_t n = 0, y = 0;
        for (j = 0; j < len; j++) {
            if (j == s || wd_mc(j) != wd_mc(s))
                continue;
            for (k = 1; k <= o && wd_cls(p, t, (j + len - k) % len) == wd_cls(p, t, (s + len - k) % len); k++)
                ;
            if (k <= o)
                continue;
            n++;
            y += (wd_cls(p, t, j) != 0u) == (on != 0);
        }
        if (n)
            return y * 64u / n;
    }
    return 16;
}
static uint32_t wd_key(uint32_t t, uint32_t s)
{
    uint32_t x = (t * 64u + s + 1u) * 2654435761u;
    return x ^ x >> 15;
}
static uint32_t wd_hash(void)                        /* the version playing: the wandering tracks' hits */
{
    const pattern_t *p = &pat[sq.active];
    uint32_t t, s, h = 0, rt = wd_rtracks();
    for (t = 0; t < NTRACK; t++)
        for (s = 0; s < track_len(p, t) && ((rt >> t) & 1u); s++)
            h ^= main_on(p, t, s) ? wd_key(t, s) : 0u;
    return h;
}
static uint32_t wd_note(const pattern_t *p, uint32_t t)   /* the note the track's hits play */
{
    uint32_t s;
    for (s = 0; s < track_len(p, t); s++)
        if (main_on(p, t, s))
            return p->step[t][s].note;
    return note_keep(t, sq.lnote[t]);
}
static void wd_vol(pattern_t *p, uint32_t t, uint32_t s, uint32_t v)
{
    p->step[t][s].volume = (uint8_t)((p->step[t][s].volume & STEP_ACTIVE_MASK) | (v > 127u ? 127u : v));
}
static void wd_say(uint32_t t, const char *w)        /* the move, for the screen ("HH +HIT") */
{
    put_str(wd.rmove, VOICE_SHORT[t % NTRACK], sizeof wd.rmove);
    put_str(wd.rmove + str_len(wd.rmove), w, sizeof wd.rmove - str_len(wd.rmove));
}
static void wd_mover(uint32_t t, uint32_t s, uint32_t tg)   /* a hit rising to tg (0: fading); no room: at once */
{
    uint32_t i;
    for (i = 0; i < NMOVER; i++)
        if (!wd.mv[i].t || (wd.mv[i].t == t + 1u && wd.mv[i].s == s)) {
            wd.mv[i].t = (uint8_t)(t + 1u);
            wd.mv[i].s = (uint8_t)s;
            wd.mv[i].tg = (uint8_t)tg;
            return;
        }
    if (tg)
        wd_vol(&pat[wd.slot], t, s, tg);
    else
        main_set(&pat[wd.slot], t, s, 0);
}
static void wd_movers(void)                          /* each rhythm step: the risers up, the fading down (then off,
                                                      * their bytes as they were) */
{
    pattern_t *p = &pat[wd.slot];
    uint32_t i;
    for (i = 0; i < NMOVER; i++) {
        uint32_t t = wd.mv[i].t - 1u, s = wd.mv[i].s, v;
        int f;
        if (!wd.mv[i].t)
            continue;
        v = p->step[t][s].volume & STEP_VOLUME_MASK;
        if (!main_on(p, t, s)) {
            wd.mv[i].t = 0;
        } else if (wd.mv[i].tg) {
            v += (wd.mv[i].tg > v ? wd.mv[i].tg - v : 0u) / 2u + 4u;
            if (v >= wd.mv[i].tg) {
                v = wd.mv[i].tg;
                wd.mv[i].t = 0;
            }
            wd_vol(p, t, s, v);
        } else if (v > 30u) {
            wd_vol(p, t, s, v - v / 3u - 6u);
        } else {
            f = wd_fpi(t, s);
            if (f >= 0)
                wd_put(p, t, s, &wd_fp[f]);
            main_set(p, t, s, 0);
            wd.mv[i].t = 0;
        }
    }
}
static uint32_t wd_tg_vel(const pattern_t *p, uint32_t t, uint32_t s)   /* a grown hit's velocity: the track's at
                                                                          * that place, as near as it has */
{
    uint32_t j, n = 0, v = 0;
    for (j = 0; j < track_len(p, t); j++)
        if (main_on(p, t, j) && wd_mc(j) == wd_mc(s)) {
            v += p->step[t][j].volume & STEP_VOLUME_MASK;
            n++;
        }
    return n ? v / n : 80u;
}

/* the candidates: a change of one or two steps */
enum { RO_GROW, RO_SHED, RO_SHIFT, RO_BORROW, RO_ACC, RO_CHANCE, RO_RAT, NRO };
typedef struct { uint8_t op, t, s, u, s2, n; } rmove_t;   /* (u: ANSWER's track, BORROW's source; NTRACK none) */
static int wd_find(const pattern_t *p, uint32_t t, uint32_t from, int on, int keep)   /* the next step on (or a
                                                      * rest) from from, round the track (-1: none) */
{
    uint32_t len = track_len(p, t), k;
    for (k = 0; k < len; k++) {
        uint32_t s = (from + k) % len;
        if (main_on(p, t, s) == on && !(keep && on && wd_keep_at(t, s)))
            return (int)s;
    }
    return -1;
}
static int wd_rcand(rmove_t *m)                      /* a candidate (0: none found) */
{
    const pattern_t *p = &pat[wd.slot];
    uint8_t l[NTRACK], w[NRO];
    uint32_t rt = wd_rtracks(), n = 0, t, i, sum = 0, r, len, hits = (wd.mask & SC_HITS) != 0u;
    int s, s2;
    int32_t e = wd.lvl;
    for (t = 0; t < NTRACK; t++)
        if ((rt >> t) & 1u)
            l[n++] = (uint8_t)t;
    if (!n)
        return 0;
    t = l[wd_rnd(n)];
    len = track_len(p, t);
    w[RO_GROW] = (uint8_t)(hits ? 4 + (e > 0 ? e / 20 : 0) : 0);
    w[RO_SHED] = (uint8_t)(hits ? 4 + (e < 0 ? -e / 20 : 0) : 0);
    w[RO_SHIFT] = w[RO_BORROW] = (uint8_t)(hits * 2u);
    w[RO_ACC] = (uint8_t)((wd.mask & SC_ACC) ? 3u : 0u);
    w[RO_CHANCE] = (uint8_t)((wd.mask & SC_CHANCE) ? 2u : 0u);
    w[RO_RAT] = (uint8_t)((wd.mask & SC_RAT) ? 1u : 0u);
    for (i = 0; i < NRO; i++)
        sum += w[i];
    if (!sum)
        return 0;
    for (r = wd_rnd(sum), i = 0; r >= w[i]; r -= w[i++])
        ;
    m->op = (uint8_t)i;
    m->t = (uint8_t)t;
    m->u = NTRACK;
    m->n = 0;
    s = wd_find(p, t, wd_rnd(len), i != RO_GROW, 1);
    if (i == RO_BORROW) {                            /* (a cell of 2-4 from here or another track, a beat or more away) */
        uint32_t u = wd_rnd(2) ? t : l[wd_rnd(n)], lu = track_len(p, u), k, on = 0, df = 0;
        m->u = (uint8_t)u;
        m->n = (uint8_t)(2u + wd_rnd(3));
        m->s2 = (uint8_t)(wd_rnd(lu) & ~1u);
        m->s = (uint8_t)((m->s2 + 4u * (1u + wd_rnd(3))) % len);
        for (k = 0; k < m->n; k++) {
            uint32_t a = (m->s2 + k) % lu, b = (m->s + k) % len;
            on += (uint32_t)main_on(p, u, a);
            df += main_on(p, u, a) != main_on(p, t, b) && !(main_on(p, t, b) && wd_keep_at(t, b));
        }
        return on && df;
    }
    if (s < 0)
        return 0;
    m->s = (uint8_t)s;
    switch (i) {
    case RO_GROW:                                    /* (ANSWER: now and then another track sheds elsewhere) */
        if (n > 1u && wd_rnd(5) < 2u) {
            uint32_t u = l[wd_rnd(n)];
            s2 = u == t ? -1 : wd_find(p, u, wd_rnd(track_len(p, u)), 1, 1);
            if (s2 >= 0 && s2 != s) {
                m->u = (uint8_t)u;
                m->s2 = (uint8_t)s2;
            }
        }
        return 1;
    case RO_SHIFT: {
        uint32_t d = wd_rnd(2) ? 1u : len - 1u, b = (uint32_t)(s + (int)d) % len;
        m->s2 = (uint8_t)b;
        return !main_on(p, t, b);
    }
    case RO_ACC: {
        uint32_t v = p->step[t][s].volume & STEP_VOLUME_MASK;
        m->n = (uint8_t)(v >= 100u ? 66u + wd_rnd(24) : 104u + wd_rnd(24));
        return 1;
    }
    case RO_CHANCE:
        if (wd_mc((uint32_t)s) >= 2u)
            return 0;
        m->n = (uint8_t)(step_prob(&p->step[t][s]) >= 100u ? 45u + wd_rnd(45) : 100u);
        return 1;
    case RO_RAT:
        if (p->rat[t][s] & RAT_BOUNCE)
            return 0;
        m->n = (uint8_t)((p->rat[t][s] & 7u) ? 0u : 1u | wd_rnd(4) << 3);
        return 1;
    default:
        return 1;
    }
}
static int32_t wd_rscore(const rmove_t *m, uint32_t h)   /* how well it fits (the higher the better; h: the
                                                      * version now) */
{
    const pattern_t *p = &pat[wd.slot];
    uint32_t t = m->t, s = m->s, i, n;
    int32_t sc = (int32_t)wd_rnd(16u + wd_stride()), dn = 0, tg, nn;
    switch (m->op) {
    case RO_GROW:
        sc += (int32_t)wd_style(p, t, s, 1);
        dn = 1;
        h ^= wd_key(t, s);
        if ((t == 0u || t == 3u) && main_on(p, 3u - t, s))   /* (the kick and the snare: not together) */
            sc -= 40;
        if ((t == 5u || t == 6u) && main_on(p, 3, s))   /* (a hat: not on the snare's) */
            sc -= 20;
        if (m->u < NTRACK) {
            sc += (int32_t)wd_style(p, m->u, m->s2, 0) / 2 + 6;
            h ^= wd_key(m->u, m->s2);
        }
        break;
    case RO_SHED:
        sc += (int32_t)wd_style(p, t, s, 0);
        dn = -1;
        h ^= wd_key(t, s);
        break;
    case RO_SHIFT:
        sc += (int32_t)(wd_style(p, t, m->s2, 1) + wd_style(p, t, s, 0)) / 2;
        h ^= wd_key(t, s) ^ wd_key(t, m->s2);
        break;
    case RO_BORROW:                                  /* (composed: a figure the track already knows) */
        sc += 24;
        for (i = 0; i < m->n; i++) {
            uint32_t a = (m->s2 + i) % track_len(p, m->u), b = (s + i) % track_len(p, t);
            if (main_on(p, m->u, a) != main_on(p, t, b)) {
                h ^= wd_key(t, b);
                dn += main_on(p, m->u, a) ? 1 : -1;
            }
        }
        break;
    default:
        sc += 14;
        break;
    }
    n = wd_hits(p, t);                               /* (the hits held near home's, Energy leaning them) */
    tg = (int32_t)wd.nh[t] + (int32_t)wd.nh[t] * wd.lvl * (int32_t)wd.k[WV_RANGE] / (127 * 400);
    nn = (int32_t)n + dn;
    sc += ((int32_t)n > tg ? (int32_t)n - tg : tg - (int32_t)n) * 12 - (nn > tg ? nn - tg : tg - nn) * 12;
    for (i = 0; i < 4u; i++)                         /* (not a version heard just now) */
        sc -= h == wd.recent[i] ? 60 : 0;
    if (wd.nfoot >= 16u + wd.k[WV_RANGE] / 4u)       /* (far enough: a step back as it was rather than a new one) */
        sc += wd_fpi(t, s) >= 0 ? 30 : -30;
    return sc;
}
static void wd_rapply(const rmove_t *m)
{
    pattern_t *p = &pat[wd.slot];
    uint32_t t = m->t, s = m->s, i;
    step_t *x = &p->step[t][s];
    foot_t f;
    if (m->op != RO_BORROW && !wd_mark(t, s))
        return;
    switch (m->op) {
    case RO_GROW: {
        int k = wd_fpi(t, s);
        uint32_t tg = wd_tg_vel(p, t, s);
        if (wd_fp[k].t >> 7) {                       /* (a hit the road shed: back, as it was, rising) */
            wd_put(p, t, s, &wd_fp[k]);
            tg = wd_fp[k].x.volume & STEP_VOLUME_MASK;
        } else {                                     /* (a new one: the track's note, plain) */
            x->note = (uint8_t)wd_note(p, t);
            x->prob = (uint8_t)((x->prob & ~PROB_MASK) | 100u);
            lock_set(p, t, s, 0, NO_AUTOMATION);
            lock_set(p, t, s, 1, NO_AUTOMATION);
            x->p1v = x->p2v = 0;
            p->cond[t][s] = 0;
            p->rat[t][s] = 0;
            p->timing[t][s] = 7u << 4;
            x->volume = STEP_ACTIVE_MASK;
        }
        wd_vol(p, t, s, 26u + wd_rnd(12));
        main_set(p, t, s, 1);
        wd_mover(t, s, tg > 30u ? tg : 30u);
        if (m->u < NTRACK && wd_mark(m->u, m->s2)) {
            wd_mover(m->u, m->s2, 0);
            wd_say(t, " ANSWERS");
            return;
        }
        wd_say(t, " +HIT");
        return;
    }
    case RO_SHED:
        wd_mover(t, s, 0);
        wd_say(t, " -HIT");
        return;
    case RO_SHIFT:
        if (!wd_mark(t, m->s2))
            return;
        wd_get(p, t, s, &f);
        wd_put(p, t, m->s2, &f);
        main_set(p, t, s, 0);
        wd_say(t, m->s2 == (s + 1u) % track_len(p, t) ? " LATER" : " EARLIER");
        return;
    case RO_BORROW:
        for (i = 0; i < m->n; i++) {
            uint32_t a = (m->s2 + i) % track_len(p, m->u), b = (s + i) % track_len(p, t), on = main_on(p, m->u, a);
            if (on == (uint32_t)main_on(p, t, b) || (!on && wd_keep_at(t, b)) || !wd_mark(t, b))
                continue;
            if (on) {
                wd_get(p, m->u, a, &f);
                if (m->u != t)
                    f.x.note = (uint8_t)wd_note(p, t);
                wd_put(p, t, b, &f);
            } else {
                main_set(p, t, b, 0);
            }
        }
        wd_say(t, " BORROWS");
        return;
    case RO_ACC:
        wd_vol(p, t, s, m->n);
        wd_say(t, m->n >= 100u ? " ACCENT" : " SOFTER");
        return;
    case RO_CHANCE:
        x->prob = (uint8_t)((x->prob & ~PROB_MASK) | m->n);
        wd_say(t, m->n < 100u ? " MAYBE" : " SURE");
        return;
    default:
        p->rat[t][s] = m->n;
        wd_say(t, m->n ? " RATCHET" : " PLAIN");
        return;
    }
}
static void wd_waymark(void)                         /* a waymark: the hits here */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++)
        memcpy(wd_wm[wd.wmi][t], pat[sq.active].main[t], sizeof wd_wm[0][0]);
    wd.wmi = (uint8_t)((wd.wmi + 1u) % NWAY);
    wd.nwm += wd.nwm < NWAY;
}
static void wd_way_back(void)                        /* MEMORY: back to a waymark (a version heard before) */
{
    pattern_t *p = &pat[wd.slot];
    const uint16_t (*w)[4] = wd_wm[(wd.wmi + NWAY - 1u - wd_rnd(wd.nwm)) % NWAY];
    uint32_t t, s, rt = wd_rtracks();
    for (t = 0; t < NTRACK; t++)
        for (s = 0; s < track_len(p, t) && ((rt >> t) & 1u); s++) {
            uint32_t on = (w[t][s >> 4] >> (s & 15u)) & 1u;
            int k;
            if (on == (uint32_t)main_on(p, t, s) || !wd_mark(t, s))
                continue;
            k = wd_fpi(t, s);
            if (on && (wd_fp[k].t >> 7))
                wd_put(p, t, s, &wd_fp[k]);
            main_set(p, t, s, (int)on);
        }
    memset(wd.mv, 0, sizeof wd.mv);
    put_str(wd.rmove, "A PLACE I KNOW", sizeof wd.rmove);
}
static void wd_rotate(uint32_t t)                    /* THE CLOCKWORK MAZE: track t a step later, its last first */
{
    pattern_t *p = &pat[wd.slot];
    uint32_t len = track_len(p, t), s, n = 0;
    foot_t a, b;
    for (s = 0; s < len; s++)
        n += wd_fpi(t, s) < 0;
    if (n > NFOOT - wd.nfoot)
        return;
    for (s = 0; s < len; s++)
        wd_mark(t, s);
    wd_get(p, t, len - 1u, &a);
    for (s = 0; s < len; s++) {
        wd_get(p, t, s, &b);
        wd_put(p, t, s, &a);
        a = b;
    }
    wd_say(t, " TURNS");
}
static void wd_rosary(void)                          /* THE ROSARY: hits leave, the colour's and the weakest first;
                                                      * then come back the way they went */
{
    pattern_t *p = &pat[wd.slot];
    uint32_t k, i, s, rt = wd_rtracks();
    if (wd.path[wd.chap % NCHAP] == 1u) {
        wd_retrace(wd.rosk);
        put_str(wd.rmove, wd.nfoot ? "A BEAD BACK" : "ALL HOME", sizeof wd.rmove);
        return;
    }
    for (k = 0; k < wd.rosk; k++) {
        uint32_t bt = NTRACK, bs = 0, best = 0, tot = 0;
        for (i = 0; i < NTRACK; i++) {
            uint32_t t = WD_ORDER[i];
            for (s = 0; s < track_len(p, t) && ((rt >> t) & 1u); s++)
                if (main_on(p, t, s)) {
                    uint32_t sc = (NTRACK - i) * 16u + (3u - wd_mc(s)) * 4u + 1u;
                    tot++;
                    if (sc > best) {
                        best = sc;
                        bt = t;
                        bs = s;
                    }
                }
        }
        if (tot <= 1u || !wd_mark(bt, bs))
            break;
        main_set(p, bt, bs, 0);
        wd_say(bt, " LEAVES");
    }
}
static void wd_rstep(void)                           /* RHYTHM's step on an open road (at a phrase's start) */
{
    uint32_t i, k, fl = wd_qfl(), rt = wd_rtracks();
    if (!(wd.mask & (SC_HITS | SC_ACC | SC_CHANCE | SC_RAT)) || !rt)
        return;
    if (fl & QF_ROSARY) {
        wd_rosary();
        return;
    }
    if (fl & QF_MAZE) {                              /* (the tracks in turn, a step each) */
        for (k = 1; k <= NTRACK; k++)
            if ((rt >> ((wd.rsteps + k) % NTRACK)) & 1u)
                break;
        wd.rsteps = (uint8_t)(wd.rsteps + k);
        wd_rotate(wd.rsteps % NTRACK);
        return;
    }
    wd_movers();
    if (wd.nwm && wd_rnd(400) < wd.k[WV_MEMORY]) {
        wd_way_back();
    } else {
        for (k = 0; k < 1u + (wd_stride() >= 86u); k++) {
            rmove_t c, b;
            int32_t best = -99999;
            uint32_t h = wd_hash();
            for (i = 0; i < 6u; i++)
                if (wd_rcand(&c)) {
                    int32_t sc = wd_rscore(&c, h);
                    if (sc > best) {
                        best = sc;
                        b = c;
                    }
                }
            if (best > -99999)
                wd_rapply(&b);
        }
    }
    wd.recent[wd.rsteps & 3u] = wd_hash();
    if (!(++wd.rsteps & 3u))
        wd_waymark();
}

/* ------------------------------------------------------- destinations --- */
static int wd_gdiff(const pattern_t *p, const pattern_t *g, uint32_t t, uint32_t s)   /* step s differs from the
                                                      * goal's (its step there, round its length) */
{
    uint32_t sg = s % track_len(g, t), on = main_on(p, t, s);
    const step_t *a = &p->step[t][s], *b = &g->step[t][sg];
    int32_t dv = (int32_t)(a->volume & STEP_VOLUME_MASK) - (int32_t)(b->volume & STEP_VOLUME_MASK);
    if (on != (uint32_t)main_on(g, t, sg))
        return 1;
    return on && (dv > 8 || dv < -8 || a->prob != b->prob || a->note != b->note || p->rat[t][s] != g->rat[t][sg]);
}
static int wd_gtrack(const pattern_t *p, const pattern_t *g, uint32_t t)   /* track t goes there step by step */
{
    return !gen_live(p, t) && !gen_live(g, t);
}
static uint32_t wd_gleft(uint32_t gp)                /* the steps still to change on the way to pattern gp */
{
    const pattern_t *p = &pat[wd.slot], *g = &pat[gp % NUM_PATTERN];
    uint32_t t, s, n = 0;
    if (gp == wd.slot)                               /* (here: the footprints back) */
        return wd.nfoot;
    for (t = 0; t < NTRACK; t++)
        for (s = 0; s < track_len(p, t) && wd_gtrack(p, g, t); s++)
            n += (uint32_t)wd_gdiff(p, g, t, s);
    return n;
}
static void wd_toward(uint32_t gp, uint32_t n)       /* n steps towards pattern gp's: the colour first, the backbone
                                                      * last (here: the footprints retraced) */
{
    pattern_t *p = &pat[wd.slot];
    const pattern_t *g = &pat[gp % NUM_PATTERN];
    foot_t f;
    if (gp == wd.slot) {
        wd_retrace(n);
        return;
    }
    while (n--) {
        uint32_t i, t = NTRACK, s = 0, len, k;
        for (i = 0; i < NTRACK; i++) {               /* (the first track with a step to change; now and then the next) */
            uint32_t u = WD_ORDER[i];
            for (k = 0; k < track_len(p, u) && wd_gtrack(p, g, u) && !wd_gdiff(p, g, u, k); k++)
                ;
            if (k < track_len(p, u) && wd_gtrack(p, g, u)) {
                t = u;
                if (wd_rnd(3))
                    break;
            }
        }
        if (t >= NTRACK)
            return;
        len = track_len(p, t);
        for (k = 0, s = wd_rnd(len); k < len && !wd_gdiff(p, g, t, s); k++)
            s = (s + 1u) % len;
        if (!wd_mark(t, s))
            return;
        wd_get(g, t, s % track_len(g, t), &f);
        wd_put(p, t, s, &f);
    }
}
static void wd_vg_one(uint32_t i, uint32_t x, uint32_t bl)   /* a voice's value a share closer to x */
{
    uint32_t p = kit_par(i);
    int32_t d = (int32_t)x - (int32_t)par[p];
    if (d && wd_par_glides(p))
        par_set(p, (uint32_t)((int32_t)par[p] + (d + (d > 0 ? (int32_t)bl - 1 : 1 - (int32_t)bl)) / (int32_t)bl));
}
static void wd_voice_goal(void)                      /* a bar towards a LOCKED or KIT goal: the voices drawn to its */
{
    uint32_t g = wd_dest(), v = pat_voices(&pat[g]), bl = wd_trip() > wd_tb() ? wd_trip() - wd_tb() : 1u;
    if (v == PV_FREE || g == sq.active)
        return;
#define WD_VG(i, x) wd_vg_one(i, x, bl)
    KIT_UNPACK(v == PV_LOCKED ? PV_SLOT(g) : pat_kit(&pat[g]), WD_VG);
#undef WD_VG
    snd_post_all();
}
static uint32_t wd_phrase_bars(void) { return (pat_ticks() + WBAR - 1u) / WBAR; }
static void wd_leg(void)                             /* 42: GOAL SONG: the song's next row the destination (another
                                                      * pattern than here if it has one), its time by SWAY */
{
    uint32_t s = sq.song % NUM_SONGS, n = song_len(s), o = wd.k[WV_ORDER] % NWO, r = wd.srow, k, i;
    int32_t j = ((int32_t)wd_rnd(17) - 8) * wd.k[WV_SWAY] / 127;
    for (i = 0; i < 8u; i++) {
        k = o == WO_ORDER ? (r + 1u < n ? r + 1u : 0u)
                          : song_road(s, r, wd.sprv, o == WO_ANY ? 128u : song_sur(s), sq.active);
        wd.sprv = (uint8_t)r;
        r = k;
        if (song_pat(s, r) != sq.active)
            break;
    }
    wd.srow = (uint8_t)r;
    wd.sdest = (uint8_t)song_pat(s, r);
    k = (wd_journey() ? wd_journey() : 128u) * WD_SWAY[j + 8] / 64u;
    wd.sleg = (uint16_t)(k ? k : 1u);
    wd.lbar0 = wd.bars;
    wd.gl = (uint16_t)wd_gleft(wd.sdest);
}
static void wd_arrived(void)                         /* there: nothing more moves; Energy home over a few bars (a
                                                      * song: on to its next row) */
{
    wd.going = 0;
    wd_pmsg("ARRIVED: ", sq.active);
    if (wd_song()) {
        wd_leg();
        return;
    }
    wd.arrived = wd.gdash = 1;
    wd.lvl = 0;
    wd_targets();
}
static void wd_arrive(void)                          /* the goal's turn: it plays (here: when the footprints are back) */
{
    uint32_t g = wd_dest();
    if (g == sq.active) {
        wd_retrace(NFOOT);
        wd_arrived();
    } else if (!wd.going) {
        seq_set_next_pattern(g);
        wd.going = 1;
    }
}
static void wd_goal_phrase(void)                     /* a phrase begins on the way: the steps paced to arrive in time */
{
    uint32_t g = wd_dest(), left = wd_gleft(g), pb = wd_phrase_bars();
    uint32_t bl = wd_trip() > wd_tb() ? wd_trip() - wd_tb() : 0u;
    if (left)
        wd_toward(g, bl > pb ? (left * pb + bl - 1u) / bl : left);
    wd.gl = (uint16_t)wd_gleft(g);
    if ((!wd.gl && !wd_song()) || bl <= pb || wd.full)   /* (a song's row: its time, SWAY's) */
        wd_arrive();
}
static void wd_moved(void)                           /* another pattern plays: the one left gets its steps back; a
                                                      * destination reached (by hand: there too) */
{
    wd_retrace(NFOOT);
    wd.nwm = 0;
    wd.slot = sq.active;
    wd.home_loads = kit_loads;                       /* (its voices: the road's now, home kept) */
    wd_count_home();
    if (wd.state == WS_ROAD && wd.goal && !wd.arrived)
        wd_arrived();
    wd.going = 0;
}

/* a model swap (OSC / MODELS in, at a chapter's edge): a voice's level down over a bar, the model, the level back */
static void wd_swap_start(void)
{
    uint32_t t = 1u + wd_rnd(NTRACK - 1u), v = track_voice(t), ok = sp_eng_ok(v), md = par[PAR_MODEL1 + v], n = 0;
    uint8_t m[4];
    if (!(wd.mask & SC_OSC) || wd.ms_t || !((wd.tracks >> t) & 1u) || wd_rnd(2) || v >= 6u)
        return;
    if ((ok & SE_LXR) && md)
        m[n++] = MD_LXR;
    if ((ok & SE_RINGS) && v <= 2u && sp_models(v) < model_cap)
        m[n++] = (uint8_t)(MD_MODAL + wd_rnd(3));
    if ((ok & SE_PLAITS) && v <= 2u && sp_models(v) < model_cap)
        m[n++] = (uint8_t)(MD_PL0 + wd_rnd(16));
    if (ok & SE_MICRO)
        m[n++] = (uint8_t)(MD_MC0 + wd_rnd(NMICRO));
    if (!n || (md = m[wd_rnd(n)]) == par[PAR_MODEL1 + v] || !model_ok(v, md))
        return;
    wd.ms_t = (uint8_t)(t + 1u);
    wd.ms_md = (uint8_t)md;
    wd.ms_ph = 0;
}
static void wd_swap_bar(void)
{
    uint32_t v = track_voice(wd.ms_t - 1u), p = PAR_VOL1 + v;
    switch (wd.ms_ph++) {
    case 0:
        wd.ms_vol = par[p];
        wd_glide(p, 0, 0);
        return;
    case 1:
        return;
    case 2:
        if (par[p] > 3u) {                           /* (turned by hand meanwhile: left alone) */
            wd.ms_t = 0;
            return;
        }
        par_set(p, 0);
        sp_model(v, wd.ms_md);
        wd_glide(p, wd.ms_vol, 0);
        wd_say(wd.ms_t - 1u, " CHANGES");
        return;
    default:
        wd.ms_t = 0;
        return;
    }
}

/* ----------------------------------------------------------- the road --- */
static void wd_chapter(void)                         /* a chapter begins: its mood, never quite the same */
{
    const chap_t *C = wd_chap();
    uint32_t k;
    for (k = 0; k < NWM; k++)
        wd.aim[k] = (int8_t)clamp32(C->aim[k] + (int32_t)wd_rnd(21) - 10, -100, 100);
    wd_swap_start();
}
static void wd_plan(uint32_t from)                   /* the path's chapters, from chapter from */
{
    const quest_t *Q = &QUEST[wd.quest % NQUEST];
    uint32_t c = from % Q->nch;
    wd.npath = 0;
    while (wd.npath < NCHAP) {
        const chap_t *C = &Q->ch[c];
        wd.path[wd.npath++] = (uint8_t)c;
        c = wd_rnd(128) < C->pr ? C->nx[0] : C->nx[1];
        if (c == CH_END)
            break;
        if (wd.npath == NCHAP - 1u && Q->ch[Q->nch - 1u].nx[0] == CH_END)   /* (room for the last: it ends there) */
            c = Q->nch - 1u;
    }
    wd.chap = 0;
}
static uint32_t wd_weight(uint32_t n)                /* the path's first n chapters' weight */
{
    const quest_t *Q = &QUEST[wd.quest % NQUEST];
    uint32_t i, w = 0;
    for (i = 0; i < n && i < wd.npath; i++)
        w += Q->ch[wd.path[i] % Q->nch].w;
    return w;
}
static uint32_t wd_chap_end(uint32_t c)              /* the bar chapter c ends at */
{
    uint32_t J = wd_song() ? 0u : wd_journey();     /* (a song: chapters on and on) */
    return J ? J * wd_weight(c + 1u) / (wd_weight(NCHAP) ? wd_weight(NCHAP) : 1u) : wd.pbase + 8u * wd_weight(c + 1u);
}
static uint32_t wd_progress(void)                    /* how far along the road, 0..256 (ENDLESS: the path's) */
{
    uint32_t J = wd.goal ? wd_trip() : wd_journey(), b = wd_tb(), a, e;
    if (wd.arrived)
        return 256u;
    if (J)
        return b >= J ? 256u : b * 256u / J;
    a = wd.pbase;
    e = wd.pbase + 8u * wd_weight(NCHAP);
    return e > a ? (b - a) * 256u / (e - a) : 0u;
}

static void wd_message(const char *a, const char *b)
{
    put_str(wd.said, a, sizeof wd.said);
    put_str(wd.said + str_len(wd.said), b, sizeof wd.said - str_len(wd.said));
    ui_message(wd.said);
}
static void wd_set_out(void)                         /* COPY (OCT+): from here (home taken when at home; on the road
                                                      * or coming home: the same home, a new course) */
{
    if (wd.state == WS_IDLE) {
        wd_snap();
        wd.home_p = sq.active;
        memset(wd.cur, 0, sizeof wd.cur);
        wd.lvl = 0;
        memset(sq.wdens, 0, sizeof sq.wdens);
        sq.wmute = 0;
        wd.slot = sq.active;                         /* (41: no footprints yet) */
        wd.nfoot = wd.nwm = 0;
        memset(wd.recent, 0, sizeof wd.recent);
        memset(wd.mv, 0, sizeof wd.mv);
    }
    wd_x ^= fm1_ms * 2654435761u ^ sq.ticks;
    wd_rnd(2);
    wd.quest = (uint8_t)(wd.k[WV_QUEST] % NQUEST);
    if (wd_qfl() & QF_MAZE) {                        /* (by rule: the same seed each time; two opposite gestures) */
        wd_x = 0x2545F491u;
        wd.mz[0] = (uint8_t)wd_rnd(NGEST);
        wd.mz[1] = (uint8_t)(wd.mz[0] ^ 1u);
        wd.mzn = wd.rsteps = 0;
        wd.lastt = NTRACK - 1u;
    }
    wd.way = (uint8_t)wd_rnd(6);
    wd.goal = wd.k[WV_GOAL];                         /* (41: a destination, or an open road) */
    wd.arrived = wd.gdash = wd.going = wd.full = 0;
    wd.rmove[0] = 0;
    wd.ms_t = 0;
    wd_count_home();
    wd.state = WS_ROAD;
    wd.rest = wd.side = 0;
    wd.bars = wd.pbase = wd.lbar0 = 0;
    wd.sbars = wd.rbars = wd.rdue = 0;
    wd.lastbar = 0;
    wd.lastpt = sq.ptick;
    wd.ret_left = wd.ret_i = 0;
    wd_glides_off();
    wd_plan(0);
    wd_chapter();
    wd_targets();
    wd.said[0] = 0;
    if (wd_song()) {                                 /* (42: from the song's row playing this; WANDER plays the rows) */
        uint32_t r = 0, n = song_len(sq.song);
        while (r < n && song_pat(sq.song, r) != sq.active)
            r++;
        wd.srow = (uint8_t)(r < n ? r : 0xFFu);
        wd.sprv = 0xFF;
        if (sq.song_mode)
            seq_play_song(0);
        wd_leg();
    }
    wd.gl = (uint16_t)(wd.goal ? wd_gleft(wd_dest()) : 0u);
    if (wd_qfl() & QF_ROSARY) {                      /* (the hits a step: all gone by half the road, back by its end) */
        uint32_t t, h = 0, rp = WD_PACE[wd.k[WV_RHYTHM] % NWP], n;
        for (t = 0; t < NTRACK; t++)
            h += ((wd_rtracks() >> t) & 1u) ? wd.nh[t] : 0u;
        n = (wd_journey() ? wd_journey() : 64u) / 2u / (rp ? rp : wd_phrase_bars());
        wd.rosk = (uint8_t)clamp32((int32_t)((h + n - 1u) / (n ? n : 1u)), 1, 16);
    }
}
/* home over RETURN bars (instant: at once): the FATE's (from the JOURNEY's end; SHIFT (OCT-): LOST is HOME) */
static void wd_home_go(uint32_t fate, int instant)
{
    uint32_t i, v, bars = instant || !sq.running ? 0u : WD_RET[wd.k[WV_RETURN] % NWR];
    char b[24];
    wd.ret_f = (uint16_t)(wd.state == WS_ROAD ? wd_progress() : 0u);
    if (fate == WF_DICE)
        fate = wd_rnd(4);
    if (fate == WF_LOST)
        fate = WF_HOME;
    wd_glides_off();
    b[0] = 0;
    if (fate == WF_TREASURE && wd.best && wd_home_loads_ok()) {   /* (the leg that moved most stays as it is) */
        v = track_voice(wd.best_t);
        for (i = 0; i < 3u; i++) {
            uint32_t r = GEST[wd.best_g].r[i], p = r < NROLE ? sp_role_par(r, v) : 0u;
            if (p) {
                wd_home[kit_idx(p)] = par[p];
                wd_mhome[kit_idx(p)] = morph_par[kit_idx(p)];
            }
        }
        put_str(b, VOICE_SHORT[wd.best_t], sizeof b);
        put_str(b + str_len(b), " ", sizeof b - str_len(b));
        put_str(b + str_len(b), GEST[wd.best_g].name, sizeof b - str_len(b));
    } else if (fate == WF_WOUND && wd.tracks) {      /* (one track thinner or darker; the kick last) */
        uint32_t t, n = 0, d = wd_rnd(2);
        uint8_t l[NTRACK];
        for (t = 1; t < NTRACK; t++)
            if ((wd.tracks >> t) & 1u)
                l[n++] = (uint8_t)t;
        t = n ? l[wd_rnd(n)] : 0u;
        v = track_voice(t);
        if (d) {
            i = kit_idx(PAR_FILTER_FREQ_1 + v);
            wd_home[i] = (uint8_t)clamp32((int32_t)wd_home[i] - 18, v == 0u ? 40 : 10, 127);
        } else {
            i = kit_idx(sp_role_par(R_DEC, v));
            wd_home[i] = (uint8_t)clamp32((int32_t)wd_home[i] - 14, 4, 127);
            i = kit_idx(PAR_VOL1 + v);
            wd_home[i] = (uint8_t)clamp32((int32_t)wd_home[i] - 10, 20, 127);
        }
        put_str(b, VOICE_SHORT[t], sizeof b);
        put_str(b + str_len(b), d ? " DARKER" : " THINNER", sizeof b - str_len(b));
    } else {
        fate = WF_HOME;
    }
    wd.fate = (uint8_t)fate;
    if (sp_pending() && sp.pend_p == sq.active) {    /* (a spell's side journey still to reverse: its pattern back) */
        pat[sq.active] = upat;
        sp.pend = 0;
        sq.gsync = (uint8_t)((1u << NTRACK) - 1u);
    }
    for (i = 0; i < NTRACK; i++) {                   /* (a muted track: back at the bottom, growing) */
        if ((sq.wmute >> i) & 1u)
            sq.wdens[i] = -64;
        wd.dt[i] = 0;
    }
    sq.wmute = 0;
    wd.wantmute = 0;
    wd.lvl = 0;
    wd.state = WS_RETURN;
    wd.rest = wd.side = 0;
    wd.ret_left = wd.ret_n = (uint16_t)(bars ? bars * 16u : 1u);
    wd.ret_dens = (uint8_t)(bars < 255u ? bars : 255u);
    wd.ret_i = 0;
    wd.ret_due = fm1_ms;
    if (!bars)
        memset(sq.wdens, 0, sizeof sq.wdens);
    put_str(wd.said, fate == WF_TREASURE ? "HOME WITH " : fate == WF_WOUND ? "HOME HURT: " : "COMING HOME", sizeof wd.said);
    put_str(wd.said + str_len(wd.said), b, sizeof wd.said - str_len(wd.said));
    ui_message(wd.said);
}
static void wd_keep(void)                            /* here is home: the voices copied, Energy written in, the road's
                                                      * steps kept (no footprints) */
{
    uint32_t n = seq_wander_commit();
    (void)n;
    wd_snap();
    wd.home_p = sq.active;
    wd.nfoot = wd.nwm = wd.full = 0;
    memset(wd.mv, 0, sizeof wd.mv);
    wd_count_home();
    memset(wd.cur, 0, sizeof wd.cur);
    wd.lvl = 0;
    memset(wd.dt, 0, sizeof wd.dt);
    wd.wantmute = 0;
}
static void wd_end(void)                             /* the JOURNEY's end: its FATE */
{
    uint32_t f = wd.k[WV_FATE] % NWF;
    if (f == WF_DICE)
        f = wd_rnd(4);
    if (f == WF_LOST) {
        wd_keep();
        wd_glides_off();
        wd.state = WS_IDLE;
        wd_message("LOST: ", "HERE IS HOME NOW");
        return;
    }
    wd_home_go(f, 0);
}
static void wd_phrase_start(void)                    /* a phrase begins: PHRASE's notch; Energy's step and the
                                                      * rhythm's (an open road) when due; a destination's steps */
{
    if (wd.k[WV_SOUND] % NWP == WP_PHRASE)
        wd_sound();
    if (wd.rdue || wd.k[WV_RHYTHM] % NWP == WP_PHRASE) {
        wd.rdue = 0;
        wd.rbars = 0;
        wd_energy();
        if (!wd.goal)
            wd_rstep();
    }
    if (wd.goal && !wd.going)
        wd_goal_phrase();
}
static void wd_bar(void)                             /* a bar of the road */
{
    uint32_t J = wd_song() ? 0u : wd_journey(), sp_ = WD_PACE[wd.k[WV_SOUND] % NWP], rp = WD_PACE[wd.k[WV_RHYTHM] % NWP];
    wd.bars++;
    if (wd.goal) {                                   /* (a destination: there by the trip's end; its voices) */
        if (wd_tb() >= wd_trip())
            wd_arrive();
        wd_voice_goal();
    } else if (J && wd.bars >= J) {
        wd_end();
        return;
    }
    if (wd.ms_t)
        wd_swap_bar();
    while (wd.bars >= wd_chap_end(wd.chap)) {        /* (the next chapter; the path's end: ENDLESS plans on) */
        if (wd.chap + 1u < wd.npath) {
            wd.chap++;
        } else if (!J) {
            const quest_t *Q = &QUEST[wd.quest % NQUEST];
            const chap_t *C = &Q->ch[wd.path[wd.chap] % Q->nch];
            wd.pbase = wd.bars;
            wd_plan(C->nx[0] == CH_END ? 0u : wd_rnd(128) < C->pr ? C->nx[0] : C->nx[1]);
        } else {
            break;
        }
        wd_chapter();
    }
    if (sp_ && ++wd.sbars >= sp_) {
        wd.sbars = 0;
        wd_sound();
    }
    if (rp && ++wd.rbars >= rp)                      /* (Energy: at the next phrase's start) */
        wd.rdue = 1;
    wd_dens_bar(2u + wd_stride() / 40u);
}
static void wd_return_step(void)                     /* each ms while coming home: a slice of the voices' sweep */
{
    uint32_t n;
    int morphed = 0;
    if (!wd.ret_i) {
        if ((int32_t)(fm1_ms - wd.ret_due) < 0)
            return;
        wd.ret_i = 1;
    }
    for (n = 0; n < 64u && wd.ret_i <= KIT_LEN; n++, wd.ret_i++) {
        uint32_t k = wd.ret_i - 1u, p = kit_par(k), d = wd_par_glides(p) ? wd.ret_left : 1u;
        int32_t v = par[p], h = wd_home[k], m = morph_par[k], mh = wd_mhome[k];
        if (v != h) {
            v += (h - v + (h > v ? (int32_t)d - 1 : 1 - (int32_t)d)) / (int32_t)d;
            par_set(p, (uint32_t)v);
        }
        if (m != mh) {
            morph_par[k] = (uint8_t)(m + (mh - m + (mh > m ? (int32_t)d - 1 : 1 - (int32_t)d)) / (int32_t)d);
            morphed = 1;
        }
    }
    if (morphed && morph_amt)
        snd_post_morph();
    if (wd.ret_i <= KIT_LEN)
        return;
    wd.ret_i = 0;
    wd.ret_due += wd_bar_ms() / 16u;
    wd.ret_left--;
}
static uint32_t wd_hg(void) { return sq.song_mode ? sq.active : wd.home_p; }   /* the pattern home is (a song: this) */
static void wd_home_done(void)                       /* the voices home: the rhythm the rest of the way, home's pattern */
{
    uint32_t g = wd_hg();
    if (wd.going)
        return;
    wd_toward(g, 255u);
    if (sq.active != g) {
        seq_set_next_pattern(g);
        wd.going = 1;
        return;
    }
    memset(sq.wdens, 0, sizeof sq.wdens);
    wd.state = WS_IDLE;
    sq.mre = (1u << NTRACK) - 1u;                    /* (0.23-t11: the melodies from their top at each loop's start) */
    ui_message(wd.fate == WF_HOME ? "HOME AGAIN" : wd.said);
}
static void wd_service(void)                         /* each ms: the bars, the glides, the way home */
{
    if (wd.state == WS_IDLE)
        return;
    if (sq.active != wd.slot)                        /* (41: another pattern: the footprints back in the one left) */
        wd_moved();
    if (!wd_home_loads_ok() && wd.state == WS_ROAD) {   /* (a kit loaded: its voices are home now) */
        wd_snap();
        wd_glides_off();
    }
    wd_glides();
    if (wd.state == WS_RETURN) {
        uint32_t b = sq.running ? sq.ticks / WBAR + 1u : 0u;
        if (b != wd.lastbar) {                       /* (Energy home with the voices) */
            if (wd.lastbar && b) {
                uint32_t t, most = 64u;
                if (wd.ret_dens) {
                    uint32_t m = 0;
                    for (t = 0; t < NTRACK; t++)
                        m = (uint32_t)(sq.wdens[t] < 0 ? -sq.wdens[t] : sq.wdens[t]) > m
                                ? (uint32_t)(sq.wdens[t] < 0 ? -sq.wdens[t] : sq.wdens[t]) : m;
                    most = (m + wd.ret_dens - 1u) / wd.ret_dens;
                    wd.ret_dens--;
                }
                wd_dens_bar(most ? most : 1u);
                if (wd.ret_left) {                   /* (the rhythm home, a share a bar) */
                    uint32_t g = wd_hg(), bl = (wd.ret_left + 15u) / 16u, left = wd_gleft(g);
                    if (left && !wd.going)
                        wd_toward(g, (left + bl - 1u) / bl);
                }
            }
            wd.lastbar = b;
        }
        if (wd.ret_left)
            wd_return_step();
        else
            wd_home_done();
        return;
    }
    if (sq.running) {
        uint32_t b = sq.ticks / WBAR + 1u;
        if (b != wd.lastbar) {
            if (wd.lastbar && wd_moving())
                wd_bar();
            else if (wd.lastbar && wd.arrived)       /* (there: Energy home) */
                wd_dens_bar(4);
            wd.lastbar = b;
        }
        if (sq.ptick < wd.lastpt && wd.state == WS_ROAD && wd_moving())
            wd_phrase_start();
        wd.lastpt = sq.ptick;
    } else {
        wd.lastbar = 0;
        wd.lastpt = 0;
    }
}

/* ------------------------------------------------------- spells, keys --- */
static void wd_side_cast(void)                       /* SPELLS cast: a side journey (the road waits) */
{
    if (wd.state == WS_ROAD)
        wd.side = 1;
}
static void wd_side_end(void)                        /* .. the spell reversed: back on the road */
{
    if (wd.state == WS_ROAD && wd.side) {
        wd.side = 0;
        ui_message("SPELL REVERSED: ON THE ROAD AGAIN");
    }
}
static void wd_copy_key(void)                        /* COPY (OCT+): set out; resting or aside: on; a new course */
{
    if (!wd.tracks) {
        ui_message("PICK A TRACK FIRST (BLACK KEYS)");
        return;
    }
    if (wd.state == WS_ROAD && (wd.rest || wd.side)) {
        wd.rest = wd.side = 0;
        ui_message("ON THE ROAD AGAIN");
        return;
    }
    if (wd.state == WS_ROAD && wd.arrived && wd.gdash) {
        ui_message("ARRIVED: TURN GOAL FOR ANOTHER");
        return;
    }
    if (wd.k[WV_GOAL] == WG_SONG && !song_len(sq.song)) {
        ui_message("THE SONG HAS NO ROWS YET");
        return;
    }
    if (wd.k[WV_GOAL] && wd.k[WV_GOAL] - 1u == sq.active && (wd.state == WS_IDLE || !wd.nfoot)) {
        ui_message("ALREADY THERE: PICK ANOTHER GOAL");
        return;
    }
    wd_set_out();
    if (wd.goal) {
        wd_pmsg(wd_song() ? "THE SONG'S ROADS: " : "SETTING OUT FOR ", wd_dest());
    } else {
        ui_message(wd.k[WV_SOUND] || wd.k[WV_RHYTHM] ? "SETTING OUT (OCT-: HOME)" : "SOUND, RHYTHM OFF: NOTHING MOVES");
    }
}
static int wd_course(void)                           /* COPY (OCT+) has a course to set (its LED breathes) */
{
    uint32_t g = wd.k[WV_GOAL];
    if (!wd.tracks)
        return 0;
    if (wd.state != WS_ROAD || wd.rest || wd.side)
        return wd.state != WS_IDLE || !g || g - 1u != sq.active;
    return wd.arrived ? !wd.gdash && (!g || g - 1u != sq.active) : g != wd.goal;
}
static void wd_postcard(void)                        /* SHIFT (OCT-) + REST: where you are into the next empty pattern */
{
    uint32_t n;
    for (n = 0; n < NUM_PATTERN && (n == sq.active || !pat_blank(&pat[n])); n++)
        ;
    if (n >= NUM_PATTERN) {
        ui_message("NO EMPTY PATTERN FOR A POSTCARD");
        return;
    }
    pat[n] = pat[sq.active];                         /* (0.23-t11: the voices as they sound now, LOCKED in its own */
    kit_stash(PV_SLOT(n));                           /* kit slot: not pv_copy, which stashes the playing one's) */
    pat_set_voices(&pat[n], PV_LOCKED, pat_kit(&pat[n]));
    pv_name(n);
    sq.wonly = 1;                                    /* (Energy as it plays, written in) */
    dens_write(&pat[n]);
    sq.wonly = 0;
    wd_pmsg("POSTCARD SAVED TO ", n);
}
static void wd_shift_key(void)                       /* SHIFT (OCT-): home over RETURN bars; again: at once */
{
    if (wd.state == WS_RETURN) {
        wd.ret_left = 1;
        wd.ret_i = 0;
        wd.ret_due = fm1_ms;
        wd.ret_dens = 0;
        memset(sq.wdens, 0, sizeof sq.wdens);
        ui_message("HOME AT ONCE");
        return;
    }
    if (wd.state != WS_ROAD) {
        ui_message("HOME ALREADY: COPY (OCT+) SETS OUT");
        return;
    }
    wd_home_go(wd.k[WV_FATE] % NWF, 0);
}
static void wd_black(uint32_t t)
{
    char b[24];
    wd.tracks ^= (uint8_t)(1u << t);
    put_str(b, VOICE_SHORT[t], sizeof b);
    put_str(b + str_len(b), wd.tracks >> t & 1u ? ": TRAVELS" : ": STAYS HOME", sizeof b - str_len(b));
    ui_message(b);
    if (wd.state == WS_ROAD)
        wd_targets();
    ui.force = 1;
}
static void wd_white(uint32_t w, int only)
{
    char b[32];
    wd.wdown |= (uint16_t)(1u << w);
    if (only)
        wd.mask = (uint16_t)(1u << w);
    else
        wd.mask ^= (uint16_t)(1u << w);
    put_str(b, SC_LONG[w], sizeof b);
    put_str(b + str_len(b), only ? " ONLY" : (wd.mask >> w) & 1u ? ": IN" : ": OUT", sizeof b - str_len(b));
    ui_message(b);
    if (wd.state == WS_ROAD)
        wd_targets();
    ui.force = 1;
}
static int wd_white_up(uint32_t w)
{
    if (!((wd.wdown >> w) & 1u))
        return 0;
    wd.wdown &= (uint16_t)~(1u << w);
    return 1;
}
static void wd_bar_key(uint32_t k)                   /* 1 ALL / NONE, 2 KEEP, 3 REST (SHIFT: POSTCARD), 4 DEFAULTS */
{
    if (k == 2u && ui.shift) {
        ui.shift_used = 1;
        wd_postcard();
        return;
    }
    switch (k) {
    case 0:
        wd.tracks = (uint8_t)(wd.tracks == (1u << NTRACK) - 1u ? 0u : (1u << NTRACK) - 1u);
        ui_message(wd.tracks ? "EVERY TRACK TRAVELS" : "NO TRACK TRAVELS");
        if (wd.state == WS_ROAD)
            wd_targets();
        return;
    case 1:
        if (wd.state == WS_IDLE) {
            ui_message("NOT TRAVELLING: NOTHING TO KEEP");
            return;
        }
        if (wd.state == WS_RETURN) {
            wd.state = WS_IDLE;
            wd.ret_left = wd.ret_i = 0;
        }
        wd_keep();
        ui_message("KEPT: HERE IS HOME NOW");
        return;
    case 2:
        if (wd.state != WS_ROAD) {
            ui_message("NOT TRAVELLING: NO REST");
            return;
        }
        wd.rest = (uint8_t)!wd.rest;
        ui_message(wd.rest ? "RESTING BY THE FIRE" : "ON THE ROAD AGAIN");
        return;
    case 3:
        wd_knobs0();
        ui_message("WANDER: AS AT FIRST");
        ui.force = 1;
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------ knobs --- */
static int wd_vdead(uint32_t i) { (void)i; return 0; }   /* (a knob not in use yet: none since 42) */
static uint32_t wd_vget(uint32_t i) { return wd.k[i % NWV]; }
static uint32_t wd_vmax(uint32_t i)
{
    switch (i) {
    case WV_QUEST: return NQUEST - 1u;
    case WV_JOURNEY: return NWJ - 1u;
    case WV_FATE: return NWF - 1u;
    case WV_SOUND: case WV_RHYTHM: return NWP - 1u;
    case WV_RETURN: return NWR - 1u;
    case WV_RANGE: case WV_STRIDE: case WV_MEMORY: case WV_SWAY: return 127u;
    case WV_GOAL: return WG_SONG;
    case WV_ORDER: return NWO - 1u;
    default: return 0u;
    }
}
static void wd_vset(uint32_t i, uint32_t v)
{
    if (wd_vdead(i) || i >= NWV)
        return;
    if (i == WV_QUEST && v != wd.k[i] && (QUEST[wd.k[i] % NQUEST].fl & QF_WHEEL)) {   /* (from THE TURNING WHEEL:
                                                      * SOUND, RHYTHM, MEMORY as at first) */
        wd.k[WV_SOUND] = WD_K0[WV_SOUND];
        wd.k[WV_RHYTHM] = WD_K0[WV_RHYTHM];
        wd.k[WV_MEMORY] = WD_K0[WV_MEMORY];
    }
    wd.k[i] = (uint8_t)v;
    if (i == WV_GOAL)
        wd.gdash = 0;
    if (i == WV_QUEST) {                             /* (a quest: its own FATE, JOURNEY at first; THE TURNING WHEEL:
                                                      * rhythm only, every 4 bars, MEMORY up, no destination) */
        wd.k[WV_FATE] = QUEST[v % NQUEST].fate;
        wd.k[WV_JOURNEY] = QUEST[v % NQUEST].journey;
        if (QUEST[v % NQUEST].fl & QF_WHEEL) {
            wd.k[WV_SOUND] = 0;
            wd.k[WV_RHYTHM] = 3;
            wd.k[WV_MEMORY] = 96;
            wd.k[WV_GOAL] = 0;
        }
    }
    if (i == WV_RANGE && wd.state == WS_ROAD)
        wd_targets();
}

/* ------------------------------------------------------------ project --- */
static void wd_write(void)                           /* KWND (project.c) */
{
    pw_byte(NWV + 3u);
    pw_bytes(wd.k, NWV);
    pw_byte(wd.tracks);
    pw_byte(wd.mask & 0xFFu);
    pw_byte(wd.mask >> 8);
}
static void wd_read(void)
{
    uint8_t b[NWV + 3u];
    uint32_t n = pr_byte(), i;
    wd_defaults();
    memcpy(b, wd.k, NWV);
    b[NWV] = wd.tracks;
    b[NWV + 1u] = wd.mask & 0xFFu;
    b[NWV + 2u] = wd.mask >> 8;
    pr_bytes(b, n < sizeof b ? n : sizeof b);
    pr_skip(n > sizeof b ? n - sizeof b : 0u);
    for (i = 0; i < NWV; i++)
        wd.k[i] = (uint8_t)(wd_vdead(i) ? 0u : b[i] <= wd_vmax(i) ? b[i] : WD_K0[i]);
    wd.tracks = (uint8_t)(b[NWV] & ((1u << NTRACK) - 1u));
    wd.mask = (uint16_t)(b[NWV + 1u] | b[NWV + 2u] << 8);
}
