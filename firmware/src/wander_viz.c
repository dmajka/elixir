/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: WANDER's screen (40; wander.c). One big picture a page, little text: on top the quest's name and how far
 * (bars), under it the chapter's signboard and what the road is doing; then
 *   QUEST  the road: the cottage (home) on the left, the road in the quest's shape (straight; a loop home; a
 *          mountain; a dip into a cave under a hill; a wavy shore; detours; a wandering path fading; right-angled
 *          corridors; beads; round a pond; a wheel), the wizard (SPELLS') walking it as the bars pass, his
 *          footprints behind him, the FATE at its end (a flag, a chest, a bandage, fog, the d12), or a GOAL's
 *          castle, its flag the pattern; the tracks that travel under it (lit; WANDER's mutes dim)
 *   PACE   a compass of the four moods (Energy up, Light right, Space down, Grit left): the road's shape filled,
 *          the chapter's aim a ghost, home the ring; beside it the threads moving now ("D3 DARKER"), the rhythm's
 *          last move ("HH +HIT") and the hits
 *   HOME   the cottage, the home pattern, the wizard at its door (at home), far down the road, or walking back
 * A knob turned: its name and what it does in place of the top lines. Included by ui_draw.c. */

/* the knobs' cards */
static void wd_card_name(char *b, uint32_t n, uint32_t i)
{
    static const char *const N[NWV] = {"Quest", "Journey", "Fate", "Goal", "Sound", "Rhythm", "Range", "Stride",
                                       "Return", "Memory", "Order", "Sway"};
    put_str(b, N[i % NWV], n);
}
static void wd_bars_text(char *b, uint32_t n, uint32_t bars)
{
    if (bars >= 1024u && !(bars % 1024u)) {         /* (1.0: "1024 BARS" is too wide for a card) */
        put_str(put_int(b, (int32_t)(bars / 1024u)), "K BARS", n - 4u);
        return;
    }
    put_str(put_int(b, (int32_t)bars), bars == 1u ? " BAR" : " BARS", n - 4u);
}
static void wd_card_value(char *b, uint32_t n, uint32_t i, uint32_t v)
{
    static const char *const QC[NQUEST] = {"ROAD", "ROUND", "SPIRE", "HOLLOW", "TIDE", "DETOUR", "ASTRAY", "MAZE",
                                           "ROSARY", "POND", "WHEEL", "FROG", "CANYON"};
    switch (i) {
    case WV_QUEST: put_str(b, QC[v % NQUEST], n); return;
    case WV_JOURNEY:
        if (WD_BARS[v % NWJ])
            wd_bars_text(b, n, WD_BARS[v % NWJ]);
        else
            put_str(b, "ENDLESS", n);
        return;
    case WV_FATE: put_str(b, v % NWF == WF_TREASURE ? "CHEST" : WF_NAME[v % NWF], n); return;   /* (TREASURE: too
                                                      * wide for a card) */
    case WV_SOUND:
    case WV_RHYTHM:
        if (!v)
            put_str(b, "OFF", n);
        else if (v % NWP == WP_PHRASE)
            put_str(b, "PHRASE", n);
        else
            wd_bars_text(b, n, WD_PACE[v % NWP]);
        return;
    case WV_RANGE: put_str(put_int(b, (int32_t)(10u + v * 90u / 127u)), "%", 2); return;
    case WV_STRIDE: put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2); return;
    case WV_RETURN:
        if (v % NWR)
            wd_bars_text(b, n, WD_RET[v % NWR]);
        else
            put_str(b, "INSTANT", n);
        return;
    case WV_GOAL:
        if (wd.gdash)
            put_str(b, "--", n);
        else if (v == WG_SONG)
            put_str(b, "SONG", n);
        else if (v)
            wd_pname(b, v - 1u);
        else
            put_str(b, "NONE", n);
        return;
    case WV_MEMORY:
        if (v)
            put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2);
        else
            put_str(b, "NEVER", n);
        return;
    case WV_ORDER: put_str(b, v % NWO == WO_ORDER ? "IN ORDER" : v % NWO == WO_ROADS ? "ROADS" : "ANY", n); return;
    case WV_SWAY:
        if (v)
            put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2);
        else
            put_str(b, "EVEN", n);
        return;
    default: put_str(b, "--", n); return;
    }
}
/* what a knob does (the top lines, while it is turned) */
static void wd_knob_line(char *b, uint32_t n, uint32_t i)
{
    static const char *const F[NWF] = {"Home as it was", "Treasure: the leg that moved most kept",
                                       "Wound: a track darker or thinner", "Never back: where it ends is home",
                                       "The d12 picks one of the four"};
    uint32_t v = wd.k[i % NWV];
    b[0] = 0;
    switch (i) {
    case WV_QUEST: put_str(b, QUEST[v % NQUEST].line, n); return;
    case WV_JOURNEY: {                               /* (in minutes at the tempo) */
        uint32_t s = WD_BARS[v % NWJ] * 240u / seq_bpm();
        if (!WD_BARS[v % NWJ]) {
            put_str(b, "Never ends: OCT- brings it home", n);
            return;
        }
        put_str(b, "About ", n);
        if (s >= 60u) {
            put_int(b + str_len(b), (int32_t)(s / 60u));
            put_str(b + str_len(b), " min ", n - str_len(b));
        }
        put_int(b + str_len(b), (int32_t)(s % 60u));
        put_str(b + str_len(b), " s at ", n - str_len(b));
        put_int(b + str_len(b), (int32_t)seq_bpm());
        put_str(b + str_len(b), " BPM", n - str_len(b));
        return;
    }
    case WV_FATE: put_str(b, F[v % NWF], n); return;
    case WV_SOUND: put_str(b, v ? "A voice moves a notch each time" : "The voices stay as they are", n); return;
    case WV_RHYTHM: put_str(b, v ? "Energy steps, at a phrase's start" : "The hits stay as they are", n); return;
    case WV_RANGE: put_str(b, "How far Energy thins or fills", n); return;
    case WV_STRIDE:
        put_str(b, v < 43u ? "Small steps, one thread" : v < 86u ? "Steps, two threads at once" : "Bold steps, three threads",
                n);
        return;
    case WV_RETURN: put_str(b, v % NWR ? "OCT-: home over this many bars" : "OCT-: home at once", n); return;
    case WV_GOAL:
        put_str(b, v == WG_SONG ? "The road follows the song's rows" : v ? "The road leads to that pattern"
                                                                         : "An open road, no destination", n);
        return;
    case WV_MEMORY: put_str(b, v ? "Now and then a place you know" : "The road never turns back", n); return;
    case WV_ORDER:
        put_str(b, v % NWO == WO_ORDER ? "Goal SONG: its rows in order" : v % NWO == WO_ROADS
                   ? "Goal SONG: by its chances (ROADS)" : "Goal SONG: any row next", n);
        return;
    case WV_SWAY:
        put_str(b, v ? "Goal SONG: rows from 1/4 to 4x the Journey" : "Goal SONG: a row every Journey", n);
        return;
    default: return;
    }
}

/* ------------------------------------------------------------ pieces --- */
static void wd_cottage(int32_t x, int32_t y, int32_t s)   /* its floor's left corner at x, y; s: its width */
{
    const uint16_t wall = RGB(220, 196, 150), roof = RGB(172, 62, 52), dark = RGB(60, 40, 30);
    int32_t h = s * 2 / 3, i;
    cv_rect(x, y - h, s, h, wall);
    for (i = 0; i <= s / 2 + 3; i++)                 /* (the roof: a triangle, a little over the walls) */
        cv_rect(x + s / 2 - i, y - h - s / 2 + i, 2 * i + 1, 1, roof);
    cv_rect(x + s / 2 - s / 8, y - h * 5 / 8, s / 4, h * 5 / 8, dark);   /* (the door) */
    cv_rect(x + s / 8, y - h * 3 / 4, s / 5, s / 5, RGB(250, 220, 120));   /* (a lit window) */
    cv_rect(x + s - s / 4, y - h - s / 2 + s / 6, s / 8 + 1, s / 4, dark);   /* (the chimney) */
}
static void wd_wizard(int32_t x, int32_t y, int lit)   /* SPELLS' wizard, his feet at x, y */
{
    draw_wizard(x - 11, y - 28, 1, lit);
}
static void wd_flag(int32_t x, int32_t y)
{
    int32_t i;
    cv_rect(x, y - 24, 2, 24, T_MID);
    for (i = 0; i < 7; i++)
        cv_rect(x + 2, y - 24 + i, 12 - i * 12 / 7, 1, T_THEME);
    for (i = 0; i < 7; i++)
        cv_rect(x + 2, y - 17 + i, i * 12 / 7, 1, T_THEME);
}
static void wd_fog(int32_t x, int32_t y)
{
    uint16_t f = ux_mix(T_BG, T_TEXT, 22);
    cv_rrect(x - 6, y - 30, 34, 14, 7, f, T_BG);
    cv_rrect(x - 14, y - 20, 40, 14, 7, ux_mix(T_BG, T_TEXT, 30), T_BG);
    cv_rrect(x - 2, y - 10, 30, 12, 6, f, T_BG);
}
static void wd_fate_at(int32_t x, int32_t y)         /* the FATE's sign at the road's end */
{
    uint32_t f = wd.k[WV_FATE] % NWF;
    if (!wd_journey() || f == WF_LOST) {
        wd_fog(x, y);
        return;
    }
    switch (f) {
    case WF_TREASURE:                                /* (a chest) */
        cv_rect(x, y - 14, 22, 14, RGB(150, 96, 52));
        cv_rect(x, y - 18, 22, 5, RGB(120, 72, 40));
        cv_rect(x, y - 10, 22, 2, RGB(236, 196, 84));
        cv_rect(x + 9, y - 12, 4, 5, RGB(236, 196, 84));
        return;
    case WF_WOUND:                                   /* (a bandage) */
        cv_rrect(x, y - 20, 20, 20, 4, RGB(240, 240, 240), T_BG);
        cv_rect(x + 8, y - 17, 4, 14, RGB(210, 50, 50));
        cv_rect(x + 3, y - 12, 14, 4, RGB(210, 50, 50));
        return;
    case WF_DICE: draw_d12(x + 10, y - 11, 11, 0, 127, 0); return;
    default: wd_flag(x + 4, y); return;
    }
}

/* the road's shapes: points in a 100 x 100 box (x on, y down), mapped onto the picture */
#define WD_NPT 10u
static const uint8_t WD_ROAD[NQUEST][WD_NPT][2] = {
    {{0, 90}, {12, 89}, {24, 87}, {36, 85}, {48, 82}, {60, 79}, {72, 76}, {84, 72}, {94, 69}, {100, 67}},   /* straight */
    {{0, 92}, {30, 96}, {62, 88}, {90, 60}, {84, 18}, {56, 6}, {28, 16}, {12, 40}, {4, 70}, {0, 88}},       /* a loop */
    {{0, 92}, {16, 86}, {32, 62}, {44, 30}, {52, 6}, {60, 30}, {72, 62}, {86, 84}, {94, 90}, {100, 92}},    /* a peak */
    {{0, 86}, {16, 86}, {32, 92}, {44, 100}, {56, 102}, {68, 100}, {80, 92}, {90, 86}, {96, 84}, {100, 84}},   /* a dip */
    {{0, 86}, {11, 74}, {22, 86}, {33, 98}, {44, 86}, {55, 74}, {66, 86}, {77, 98}, {88, 86}, {100, 76}},   /* waves */
    {{0, 90}, {14, 88}, {20, 58}, {28, 88}, {46, 86}, {54, 56}, {62, 86}, {78, 84}, {86, 54}, {100, 82}},   /* detours */
    {{0, 90}, {14, 80}, {10, 62}, {26, 52}, {40, 66}, {54, 46}, {46, 28}, {64, 18}, {82, 32}, {100, 22}},   /* astray */
    {{0, 92}, {20, 92}, {20, 64}, {44, 64}, {44, 88}, {66, 88}, {66, 42}, {86, 42}, {86, 68}, {100, 68}},   /* corridors */
    {{0, 72}, {8, 90}, {22, 102}, {40, 104}, {58, 100}, {72, 90}, {82, 76}, {88, 62}, {92, 48}, {100, 40}},   /* beads */
    {{0, 92}, {18, 100}, {40, 104}, {62, 102}, {82, 94}, {96, 78}, {100, 60}, {94, 44}, {80, 34}, {62, 30}},   /* the pond */
    {{0, 60}, {8, 28}, {30, 6}, {58, 4}, {82, 20}, {94, 50}, {84, 82}, {58, 100}, {28, 98}, {4, 78}},      /* a wheel */
    {{0, 92}, {12, 91}, {24, 89}, {36, 86}, {48, 82}, {60, 77}, {70, 71}, {80, 64}, {90, 56}, {100, 47}},   /* a slow rise */
    {{0, 92}, {16, 70}, {30, 86}, {44, 58}, {56, 78}, {66, 50}, {76, 68}, {84, 40}, {92, 52}, {100, 26}}};   /* a gorge */
#define WD_X0 40
#define WD_W 160
#define WD_Y0 44
#define WD_H 58
static int32_t wd_rx(uint32_t q, uint32_t i) { return WD_X0 + WD_ROAD[q][i][0] * WD_W / 100; }
static int32_t wd_ry(uint32_t q, uint32_t i) { return WD_Y0 + WD_ROAD[q][i][1] * WD_H / 100; }
static int32_t wd_seg(int32_t dx, int32_t dy)        /* a segment's length, near enough */
{
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    return dx > dy ? dx + dy * 3 / 8 : dy + dx * 3 / 8;
}
static void wd_along(uint32_t q, uint32_t f, int32_t *x, int32_t *y)   /* the point f (0..256) along road q */
{
    int32_t tot = 0, at, i;
    for (i = 1; i < (int32_t)WD_NPT; i++)
        tot += wd_seg(wd_rx(q, i) - wd_rx(q, i - 1), wd_ry(q, i) - wd_ry(q, i - 1));
    at = tot * (int32_t)(f > 256u ? 256u : f) / 256;
    for (i = 1; i < (int32_t)WD_NPT; i++) {
        int32_t l = wd_seg(wd_rx(q, i) - wd_rx(q, i - 1), wd_ry(q, i) - wd_ry(q, i - 1));
        if (at <= l || i == (int32_t)WD_NPT - 1) {
            l = l ? l : 1;
            at = at > l ? l : at;
            *x = wd_rx(q, i - 1) + (wd_rx(q, i) - wd_rx(q, i - 1)) * at / l;
            *y = wd_ry(q, i - 1) + (wd_ry(q, i) - wd_ry(q, i - 1)) * at / l;
            return;
        }
        at -= l;
    }
}
static uint32_t wd_where(void)                       /* the wizard's place along the road, 0..256 */
{
    if (wd.state == WS_ROAD)
        return wd_progress();
    if (wd.state == WS_RETURN)                       /* (walking back) */
        return wd.ret_n ? (uint32_t)wd.ret_f * wd.ret_left / wd.ret_n : 0u;
    return 0;
}
static void wd_scenery(uint32_t q)                   /* behind the road: the mountain, the hill, the water */
{
    int32_t y;
    uint16_t hill = ux_mix(T_BG, T_SURF, 70);
    if (q == Q_SPIRE)
        for (y = 0; y < 66; y++) {
            int32_t w = y * 8 / 7;
            cv_rect(WD_X0 + WD_W * 52 / 100 - w, WD_Y0 - 2 + y, 2 * w + 1, 1, hill);
        }
    else if (q == Q_HOLLOW) {
        for (y = 0; y < 48; y++) {                   /* (the hill: a dome) */
            int32_t w = 18 + y * 2 - y * y / 40;
            cv_rect(WD_X0 + WD_W / 2 - w, WD_Y0 + 6 + y, 2 * w, 1, hill);
        }
        cv_rrect(WD_X0 + WD_W * 36 / 100, WD_Y0 + 38, WD_W * 28 / 100, 26, 12, RGB(24, 20, 26), hill);   /* (the cave) */
    } else if (q == Q_TIDE) {
        cv_rect(WD_X0 - 4, WD_Y0 + WD_H + 2, WD_W + 8, 6, ux_mix(T_BG, RGB(60, 120, 200), 35));
    } else if (q == Q_POND) {                        /* (the pond: an ellipse) */
        for (y = -11; y <= 11; y++) {
            int32_t w = 40;
            while (w * w * 121 + y * y * 1600 > 1600 * 121)
                w--;
            cv_rect(WD_X0 + WD_W / 2 - w, WD_Y0 + 42 + y, 2 * w, 1, ux_mix(T_BG, RGB(60, 120, 200), 45));
        }
    } else if (q == Q_FROG) {                        /* (1.0: the thermometer, its heat; the pot's water) */
        int32_t h = 22 + wd.fh * 18 / 100;
        cv_rect(WD_X0 - 4, WD_Y0 + WD_H + 2, WD_W + 8, 6, ux_mix(T_BG, RGB(60, 120, 200), 30));
        cv_rrect(WD_X0 + 2, WD_Y0 - 6, 8, 44, 4, hill, T_BG);
        cv_rect(WD_X0 + 4, WD_Y0 + 36 - h, 4, h, wd.fh < 0 ? RGB(90, 150, 230) : RGB(220, 90, 60));
    } else if (q == Q_CANYON) {                      /* (1.0: the walls) */
        for (y = 0; y < 60; y++) {
            int32_t w = 10 + y / 3;
            cv_rect(WD_X0 - 4, WD_Y0 - 4 + y, w, 1, hill);
            cv_rect(WD_X0 + WD_W + 4 - w, WD_Y0 - 4 + y, w, 1, hill);
        }
    } else if (q == Q_WHEEL) {                       /* (the spokes) */
        for (y = 1; y < (int32_t)WD_NPT; y += 2)
            cv_line(WD_X0 + WD_W * 49 / 100, WD_Y0 + WD_H * 52 / 100, wd_rx(q, (uint32_t)y), wd_ry(q, (uint32_t)y), hill);
    }
}
static void wd_castle(int32_t x, int32_t y, uint32_t g)   /* a GOAL: a castle, its flag the pattern */
{
    const uint16_t st = RGB(160, 160, 172);
    char b[4];
    int32_t i;
    wd_pname(b, g % NUM_PATTERN);
    if (g >= NUM_PATTERN) {                          /* (42: GOAL SONG, not on the road: the song, S01) */
        wd_pname(b, sq.song);
        b[0] = 'S';
    }
    cv_rect(x, y - 16, 22, 16, st);
    for (i = 0; i < 3; i++)
        cv_rect(x + i * 9, y - 20, 4, 4, st);
    cv_rect(x + 8, y - 9, 6, 9, RGB(70, 60, 60));
    cv_rect(x + 10, y - 36, 2, 16, T_MID);
    cv_rect(x + 12, y - 36, 26, 14, T_THEME);
    cv_text(x + 14, y - 36 + CAP_IN(S, 14), &AF_S, b, T_BG);
}
static void wd_head(uint32_t pg)                     /* the top lines: the quest and how far; the chapter and the
                                                      * state (a knob turned: its name, what it does); PACE: one */
{
    char b[40];
    uint32_t i = pg * 4u + (ui.hot ? ui.hot - 1u : 0u);
    if (ui.hot && !wd_vdead(i)) {
        wd_card_name(b, sizeof b, i);
        if (pg == 1u) {                              /* (PACE: on one line) */
            put_str(b + str_len(b), ": ", sizeof b - str_len(b));
            wd_knob_line(b + str_len(b), sizeof b - str_len(b), i);
            cv_text_fit(2, 4, &AF_S, b, T_ACCENT, T_BG, 236);
            return;
        }
        cv_text_fit(2, 2, &AF_M, b, T_ACCENT, T_BG, 236);
        wd_knob_line(b, sizeof b, i);
        cv_text_fit(2, 24, &AF_S, b, T_TEXT, T_BG, 236);
        return;
    }
    put_str(b, QUEST[(wd.state == WS_IDLE ? wd.k[WV_QUEST] : wd.quest) % NQUEST].name, sizeof b);
    cv_text_fit(2, 2, &AF_M, b, T_THEME, T_BG, 160);
    if (wd.state != WS_IDLE) {                       /* (the bars: of the JOURNEY's) */
        put_int(b, (int32_t)(wd_song() ? wd_tb() : wd.bars));
        if (wd_song()) {                             /* (42: the song's leg) */
            put_str(b + str_len(b), "/", sizeof b - str_len(b));
            put_int(b + str_len(b), (int32_t)wd.sleg);
        } else if (wd_journey()) {
            put_str(b + str_len(b), "/", sizeof b - str_len(b));
            put_int(b + str_len(b), (int32_t)wd_journey());
        }
        cv_text_r(236, 6, &AF_S, b, T_MID, T_BG);
    }
    if (pg == 1u)
        return;
    if (ui.shift) {                                  /* (0.23-t11: what SHIFT does here, POSTCARD's key breathing) */
        cv_text_fit(2, 22 + CAP_IN(S, 17), &AF_S, "+ BLACK KEY 10 (BAR 3): POSTCARD", T_ACCENT, T_BG, 236);
        return;
    }
    {
        const char *st = wd.state == WS_IDLE ? "AT HOME" : wd.state == WS_RETURN ? "COMING HOME"
                       : wd.side ? "ASIDE: A SPELL" : wd.rest ? "RESTING" : 0;
        int32_t x = 2;
        if (!st && wd.goal) {                        /* (41: how far off the goal is) */
            if (wd.arrived) {
                put_str(b, "ARRIVED", sizeof b);
            } else {
                put_str(put_int(b, wd.gl), " TO ", 6);
                wd_pname(b + str_len(b), wd_dest());
            }
            st = b;
        }
        if (wd.state == WS_ROAD) {                   /* (the signboard) */
            const char *c = wd_chap()->name;
            int32_t w = text_w(&AF_S, c) + 12;
            cv_rrect(2, 22, w, 17, 3, RGB(150, 96, 52), T_BG);
            cv_text(8, 22 + CAP_IN(S, 17), &AF_S, c, RGB(250, 236, 210));
            x = w + 10;
        }
        if (st)
            cv_text_fit(x, 22 + CAP_IN(S, 17), &AF_S, st, wd.state == WS_ROAD ? T_ACCENT : T_MID, T_BG, 236 - x);
    }
}
static void wd_chips(int32_t y)                      /* the tracks that travel (lit), WANDER's mutes dim */
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        int32_t x = 2 + (int32_t)t * 34;
        int on = (wd.tracks >> t) & 1u, mute = (sq.wmute >> t) & 1u;
        uint16_t c = on ? (mute ? ux_mix(TRACK_COL[t], T_BG, 60) : TRACK_COL[t]) : T_SURF;
        cv_rrect(x, y, 32, 15, 3, c, T_BG);
        text_c(x, y + CAP_IN(S, 15), 32, &AF_S, VOICE_SHORT[t], on ? RGB(12, 12, 12) : T_DIM, c);
    }
}

/* ------------------------------------------------------------- pages --- */
static void draw_wd_quest(void)
{
    uint32_t q = (wd.state == WS_IDLE ? wd.k[WV_QUEST] : wd.quest) % NQUEST, f = wd_where(), i, n;
    int32_t x, y, px = 0, py = 0;
    uint16_t road = ux_mix(RGB(196, 160, 110), T_BG, wd.state == WS_IDLE ? 55 : 15);
    wd_scenery(q);
    for (i = 1; i < WD_NPT; i++) {                   /* (the road; astray: fading; beads) */
        cv_line_t(wd_rx(q, i - 1), wd_ry(q, i - 1), wd_rx(q, i), wd_ry(q, i),
                  q == Q_LOST ? ux_mix(road, T_BG, (int)(i * 9u)) : road, 3);
        if (q == Q_ROSARY)
            cv_rrect(wd_rx(q, i) - 4, wd_ry(q, i) - 4, 9, 9, 4, RGB(200, 170, 220), T_BG);
    }
    n = q == Q_HOMECOMING ? 3u : q == Q_WHEEL ? 5u : WD_NPT - 1u;   /* (a GOAL's castle; a loop's: its far side) */
    if (wd.state == WS_IDLE ? wd.k[WV_GOAL] : wd.goal)
        wd_castle(clamp32(wd_rx(q, n) + 6, 0, 200), wd_ry(q, n) + 2,
                  wd.state == WS_IDLE ? wd.k[WV_GOAL] - 1u : wd_dest());
    else if (q != Q_HOMECOMING && q != Q_WHEEL)
        wd_fate_at(wd_rx(q, WD_NPT - 1u) + 8, wd_ry(q, WD_NPT - 1u) + 2);
    if (wd.state == WS_ROAD && wd.goal && (q == Q_HOMECOMING || q == Q_WHEEL))   /* (a loop: the castle on its far
                                                      * side is the road's end) */
        f = f * (q == Q_HOMECOMING ? 3u : 5u) / (WD_NPT - 1u);
    n = f / 6u;                                      /* (his footprints behind him) */
    for (i = 1; i < n; i++) {
        wd_along(q, i * 6u, &x, &y);
        cv_rect(x - 1 + (int32_t)(i & 1u) * 2, y - 1, 2, 2, ux_mix(road, RGB(40, 30, 20), 55));
    }
    wd_cottage(6, WD_Y0 + WD_H - 4, 28);
    wd_along(q, f, &px, &py);
    if (wd.rest && wd.state == WS_ROAD) {            /* (a fire beside him) */
        cv_rect(px + 12, py - 3, 10, 3, RGB(110, 70, 40));
        cv_rrect(px + 13, py - 11, 8, 9, 4, RGB(240, 140, 40), T_BG);
        cv_rect(px + 16, py - 9, 2, 5, RGB(255, 230, 120));
    }
    wd_wizard(px, py + 2, wd.state == WS_ROAD && wd_moving());
    wd_chips(112);
}
/* the compass: a mood -100..100 at its axis' radius 0..R (home: R / 2) */
#define WD_CX 70
#define WD_CY 74
#define WD_R 34
static void wd_quad(const int32_t *m, int32_t *x, int32_t *y)
{
    static const int8_t AX[NWM][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};   /* (Energy up, Light right, Space down,
                                                                            * Grit left) */
    uint32_t k;
    for (k = 0; k < NWM; k++) {
        int32_t r = WD_R * (clamp32(m[k], -100, 100) + 100) / 200;
        x[k] = WD_CX + AX[k][0] * r;
        y[k] = WD_CY + AX[k][1] * r;
    }
}
static void draw_wd_pace(void)
{
    static const char *const AXN[NWM] = {"ENERGY", "LIGHT", "SPACE", "GRIT"};
    int32_t m[NWM], x[NWM], y[NWM], k, i, row = 0;
    char b[24];
    for (k = 0; k < 64; k++) {                       /* (home: the ring) */
        int32_t s = SP_SIN[k], c = SP_SIN[(k + 16) & 63];
        cv_pset(WD_CX + s * WD_R / 2 / 127, WD_CY + c * WD_R / 2 / 127, T_MID);
    }
    cv_line(WD_CX - WD_R, WD_CY, WD_CX + WD_R, WD_CY, T_LINE);
    cv_line(WD_CX, WD_CY - WD_R, WD_CX, WD_CY + WD_R, T_LINE);
    text_c(WD_CX - 30, WD_CY - WD_R - 17, 60, &AF_S, AXN[0], T_MID, T_BG);
    text_c(WD_CX - 30, WD_CY + WD_R + 2, 60, &AF_S, AXN[2], T_MID, T_BG);
    cv_text(WD_CX + WD_R + 3, WD_CY - 6, &AF_S, AXN[1], T_MID);
    cv_text_r(WD_CX - WD_R - 3, WD_CY - 6, &AF_S, AXN[3], T_MID, T_BG);
    if (wd.state == WS_ROAD) {                       /* (the chapter's aim: a ghost) */
        for (k = 0; k < (int32_t)NWM; k++)
            m[k] = wd.aim[k];
        wd_quad(m, x, y);
        for (k = 0; k < (int32_t)NWM; k++)
            cv_line(x[k], y[k], x[(k + 1) % NWM], y[(k + 1) % NWM], T_DIM);
    }
    m[WM_E] = clamp32(wd.lvl + wd.cur[WM_E] / 2, -100, 100);
    for (k = 1; k < (int32_t)NWM; k++)
        m[k] = wd.cur[k];
    wd_quad(m, x, y);
    sp_fill(x, y, NWM, ux_mix(T_THEME, T_BG, 62));
    for (k = 0; k < (int32_t)NWM; k++)
        cv_line_t(x[k], y[k], x[(k + 1) % NWM], y[(k + 1) % NWM], T_THEME, 2);
    for (i = 0; i < (int32_t)NTHREAD; i++) {         /* (the threads: "D3 DARKER") */
        const thread_t *h = &wd.th[i];
        int32_t yy = 30 + row * 24;
        if (!h->on || wd.state != WS_ROAD)
            continue;
        cv_rrect(146, yy, 26, 17, 3, TRACK_COL[h->t], T_BG);
        text_c(146, yy + CAP_IN(S, 17), 26, &AF_S, VOICE_SHORT[h->t], RGB(12, 12, 12), TRACK_COL[h->t]);
        cv_text_fit(176, yy + CAP_IN(S, 17), &AF_S, GEST[h->g].name, T_TEXT, T_BG, 62);
        row++;
    }
    if (!row)
        cv_text_fit(146, 30 + CAP_IN(S, 17), &AF_S, wd.state == WS_ROAD && wd.k[WV_SOUND] ? "WAITING" : "STILL",
                    T_MID, T_BG, 92);
    if (wd.state == WS_ROAD && wd.rmove[0] && row < 3)   /* (41: the rhythm's last move) */
        cv_text_fit(146, 86, &AF_S, wd.rmove, T_ACCENT, T_BG, 92);
    if (wd.state != WS_IDLE && (wd.mask & SC_HITS)) {   /* (Energy's hits: the colour's DENSITY, as a share) */
        int32_t d = wd_depth(wd.lvl) * (wd.lvl < 0 ? -wd.lvl : wd.lvl) / 100;
        put_str(b, "HITS ", sizeof b);
        if (wd.lvl > 0)
            put_str(b + str_len(b), "+", sizeof b - str_len(b));
        else if (wd.lvl < 0)
            put_str(b + str_len(b), "-", sizeof b - str_len(b));
        put_str(put_int(b + str_len(b), d * 100 / 64), "%", 2);
        cv_text_fit(146, 106, &AF_S, b, T_MID, T_BG, 92);
    }
}
static void draw_wd_home(void)
{
    uint32_t f = wd_where();
    int32_t x = 74 + (int32_t)(f * 150u / 256u);
    char b[24];
    cv_line_t(70, 104, 236, 104, ux_mix(RGB(196, 160, 110), T_BG, 15), 3);
    wd_cottage(14, 106, 56);
    if (wd.state == WS_IDLE)
        x = 74;
    else if (wd.state == WS_ROAD)
        x = 74 + (int32_t)(wd_progress() * 150u / 256u);
    wd_wizard(x + 11, 104, wd.state != WS_IDLE);
    wd_pname(b, wd.state == WS_IDLE ? sq.active : wd.home_p);
    cv_text(150, 46, &AF_M, b, T_THEME);
    if (wd.state == WS_ROAD) {                       /* (how far away) */
        put_int(b, (int32_t)wd.bars);
        put_str(b + str_len(b), wd.bars == 1u ? " BAR AWAY" : " BARS AWAY", sizeof b - str_len(b));
    }
    cv_text_fit(150, 68, &AF_S, wd.state == WS_ROAD ? b : wd.state == WS_RETURN ? "ON THE WAY" : "AT HOME", T_MID,
                T_BG, 88);
}
static void draw_wander(int32_t y0)
{
    uint32_t pg = ui.page % NPERF - PG_WANDER;
    (void)y0;
    wd_head(pg);
    if (pg == 0u)
        draw_wd_quest();
    else if (pg == 1u)
        draw_wd_pace();
    else
        draw_wd_home();
}
static uint32_t wander_sig(void)
{
    uint32_t s = sig_add(40, wd.state | (uint32_t)wd.rest << 2 | (uint32_t)wd.side << 3 | (uint32_t)wd.quest << 4 |
                                 (uint32_t)wd.chap << 8 | (uint32_t)wd.tracks << 12 | (uint32_t)wd.mask << 16);
    uint32_t i;
    s = sig_add(s, wd.bars | wd_where() << 16 | (uint32_t)sq.wmute << 25);
    s = sig_add(s, ui.page | (uint32_t)ui.hot << 8 | (uint32_t)(uint8_t)wd.lvl << 16 | (uint32_t)sq.active << 24);
    for (i = 0; i < NWV; i++)
        s = sig_add(s, wd.k[i]);
    for (i = 0; i < NWM; i++)
        s = sig_add(s, (uint32_t)(uint16_t)wd.cur[i] | (uint32_t)(uint8_t)wd.aim[i] << 16);
    for (i = 0; i < NTHREAD; i++)
        s = sig_add(s, wd.th[i].on | (uint32_t)wd.th[i].t << 1 | (uint32_t)wd.th[i].g << 4);
    s = sig_add(s, wd.goal | (uint32_t)wd.arrived << 6 | (uint32_t)wd.gdash << 7 | (uint32_t)wd.gl << 8 |
                       (uint32_t)wd.rsteps << 20 | (uint32_t)(uint8_t)wd.rmove[0] << 24);
    s = sig_add(s, wd.sdest | (uint32_t)wd.sleg << 8 | (uint32_t)sq.song << 24);
    s = sig_add(s, (uint32_t)(uint16_t)wd.fh);         /* (1.0: BOILING FROG's thermometer) */
    return sig_add(s, seq_bpm() | (uint32_t)wd.home_p << 9 | (uint32_t)wd_home_loads_ok() << 15);
}
