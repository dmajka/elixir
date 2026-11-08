/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the screen (240 x 240), drawn with Felucca's renderer (gfx.c: anti-aliased Inter Tight, the palettes).
 *   0..23    header: the pattern (and the one queued), the kit, the tempo, play / record
 *   24..43   the title: the track, the page, a message
 *   44..171  the body: the step grid of the shown bar (7 tracks x 16 steps, the bar keys below), or the page's own
 *   172..239 the four knob cards
 * The panel takes ~77 ms for the whole screen over its SPI, so the screen is regions, each drawn only when what
 * it shows changed (a signature of it): the grid's 16 columns each, so the playhead redraws two of them. */

#define HDR_H 24
#define TITLE_Y 24
#define TITLE_H 20
#define BODY_Y 44
#define BODY_H 128
#define CARD_Y 172
#define CARD_H 68
#define GRID_X 32
#define CELL_W 13
#define ROW_H 16
#define BARS_Y (BODY_Y + NTRACK * ROW_H)
#define BARS_H (BODY_H - NTRACK * ROW_H)

enum { R_HDR, R_TITLE, R_LABELS, R_BARS, R_BODY, R_STRIP, R_CARD0, R_COL0 = R_CARD0 + 4, NREG = R_COL0 + 16 };
static uint32_t rsig[NREG];
static uint8_t body_full;                            /* the body takes the cards' place too (CONFIG > LOAD) */
static uint8_t body_is_grid;                        /* what the body showed last (a change redraws it all) */

static const uint16_t TRACK_COL[NTRACK] = {RGB(255, 112, 56), RGB(255, 166, 44), RGB(244, 208, 74), RGB(124, 210, 92),
                                           RGB(62, 198, 200), RGB(80, 156, 240), RGB(168, 124, 242)};

static uint32_t sig_add(uint32_t s, uint32_t v) { return (s ^ v) * 16777619u; }
static int region(uint32_t id, uint32_t sig)
{
    sig = sig_add(sig, ux.gen);
    if (!ui.force && rsig[id] == sig)
        return 0;
    rsig[id] = sig;
    return 1;
}

static void text_c(int32_t x, int32_t y, int32_t w, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    char b[40];
    text_fit(b, sizeof b, s, f, w);
    cv_text_in(x, y, w, f, b, fg, bg);
}
static void two_digits(char *b, uint32_t v)
{
    b[0] = (char)('0' + (v / 10u) % 10u);
    b[1] = (char)('0' + v % 10u);
    b[2] = 0;
}

/* ------------------------------------------------------------ header --- */
static void draw_header(void)
{
    char b[24], t[8];
    uint32_t bpm = sq.sync_ext ? (sq.bpm_ext_x10 + 5u) / 10u : seq_bpm();
    uint32_t s = sig_add(sq.active, sq.pending);
    s = sig_add(s, sq.running | sq.rec << 1 | sq.song_mode << 2 | sq.sync_ext << 3 | (sq.beat && sq.running) << 4);
    s = sig_add(s, bpm);
    s = sig_add(s, kit_loaded);
    s = sig_add(s, sq.song_pos | sq.song << 8);
    s = sig_add(s, (uint32_t)kit_names[kit_loaded][0] | (uint32_t)kit_names[kit_loaded][3] << 8);
    s = sig_add(s, morph_amt | (uint32_t)keys_live() << 8 | (uint32_t)(ui.mode == M_KEYS) << 9);
    if (!region(R_HDR, s))
        return;
    cv_begin(240, HDR_H, T_BG);
    put_str(b, "P", sizeof b);
    two_digits(b + 1, sq.active + 1u);
    cv_text(6, 4, &AF_M, b, T_THEME);
    if (sq.pending != sq.active) {
        put_str(b, ">", sizeof b);
        two_digits(b + 1, sq.pending + 1u);
        cv_text(36, 5, &AF_S, b, T_ACCENT);
    }
    if (sq.song_mode) {
        put_str(b, "SONG ", sizeof b);
        two_digits(b + 5, sq.song + 1u);
        cv_text(64, 5, &AF_S, b, T_MID);
    } else {
        memcpy(t, kit_names[kit_loaded], 8);
        t[7] = 0;
        {
            uint32_t n = 7;
            while (n && (t[n - 1] == ' ' || t[n - 1] == 0))
                t[--n] = 0;
        }
        put_str(b, t, sizeof b);
        cv_text_fit(64, 5, &AF_S, b, T_TEXT, T_BG, keys_live() ? 54 : 84);
        if (morph_amt)
            cv_text(keys_live() ? 120 : 150, 5, &AF_S, "M", T_ACCENT);
    }
    if (keys_live() && ui.mode != M_KEYS) {          /* 31: KEYS on (SHIFT + EDIT): the keys play notes here */
        cv_rrect(130, 3, 32, 15, 4, T_ACCENT, T_BG);
        text_c(130, 3 + CAP_IN(S, 15), 32, &AF_S, "KEYS", T_INK, T_ACCENT);
    }
    if (sq.sync_ext)
        cv_text(keys_live() && ui.mode != M_KEYS ? 166 : 160, 5, &AF_S, "MIDI", T_MID);
    put_int(b, (int32_t)bpm);
    cv_text_r(214, 4, &AF_M, b, T_TEXT, T_BG);
    if (sq.running) {                                /* play: a triangle (bright on the beat) */
        int32_t y;
        uint16_t c = sq.beat ? T_ACCENT : T_THEME;
        for (y = 0; y < 12; y++)
            cv_rect(222, 6 + y, y < 6 ? y + 1 : 12 - y, 1, c);
    } else {
        cv_rect(222, 7, 9, 9, T_DIM);
    }
    if (sq.rec)
        cv_rrect(232, 8, 7, 7, 3, T_REC, T_BG);
    cv_rect(0, HDR_H - 1, 240, 1, T_LINE);
    cv_blit(0, 0);
}

/* ------------------------------------------------------------- title --- */
static void title_text(char *b, uint32_t n, uint32_t *pg, uint32_t *np)
{
    *pg = ui.page;
    *np = mode_pages();
    switch (ui.mode) {
    case M_HOME: put_str(b, "PATTERN", n); break;
    case M_PATTERN: put_str(b, "PATTERNS", n); break;
    case M_SONG:                                     /* (37: ROWS: what OCT+ does, as ACTION's) */
        put_str(b, ui.page % NSONGPG == 2u ? (song_rd[sq.song % NUM_SONGS] & 0x80u ? "ROADS: BY CHANCE" : "ROADS: AS WRITTEN")
                   : !(ui.page % NSONGPG) ? "SONG" : ui.song_edit == SE_DELETE ? "PRESS OCT+ TO DELETE THE ROW"
                                                                              : "PRESS OCT+ TO INSERT A ROW", n);
        break;
    case M_SOUND: {
        uint8_t l[NSPG][2];
        uint32_t i, k = sound_pages(l);
        uint32_t a = ui.half, z = sect_half_used(ui.track, ui.sect, 0) + sect_half_used(ui.track, ui.sect, 1);
        if (ui.copy || ca_rm) {                      /* 0.11: OCT+ held: the knobs move every voice (Phase 17: or MIDI) */
            put_str(b, "CONTROL ALL: ", n);
            put_str(b + str_len(b), SECT_NAME[ui.sect], n - str_len(b));
        } else {
            put_str(b, SECT_NAME[ui.sect], n);
        }
        if (z > 1u) {                                /* 29: the section's page of its pages */
            char d[8];
            put_str(d, " (", sizeof d);
            put_int(d + 2, (int32_t)a + 1);
            put_str(d + str_len(d), "/", sizeof d - str_len(d));
            put_int(d + str_len(d), (int32_t)z);
            put_str(d + str_len(d), ")", sizeof d - str_len(d));
            put_str(b + str_len(b), d, n - str_len(b));
        }
        for (i = 0; i < k && !(l[i][0] == ui.sect && l[i][1] == ui.half); i++)
            ;
        *pg = i;
        *np = k;
        break;
    }
    case M_STEP: {
        uint32_t l;
        put_str(b, STEP_TITLE[ui.page % 4u], n);
        l = (uint32_t)str_len(b);
        put_str(b + l, "  STEP ", n - l);
        put_int(b + str_len(b), ui.step + 1);
        break;
    }
    case M_PERF:                                     /* (29: the pages of the set shown) */
        put_str(b, PERF_TITLE[ui.page % NPERF], n);
        *pg = ui.page % NPERF - perf_first();
        *np = perf_count();
        break;
    case M_EUCLID:
        if (ui.page % NEUCLID == PG_ACTION && ui.action) {   /* 0.10: what OCT+ does, in place of the title */
            static const char *const A[NACT] = {"", "DICE", "COMMIT", "UNDO", "RESET"};
            put_str(b, "PRESS OCT+ TO ", n);
            put_str(b + str_len(b), A[ui.action < NACT ? ui.action : 0], n - str_len(b));
        } else if (ui.page % NEUCLID == PG_ACTION && shown_pat()->gen[ui.track][0] == GEN_GRIDS) {
            put_str(b, "ACTION / GRIDS MAP", n);
        } else if (ui.page % NEUCLID == PG_ACTION && shown_pat()->gen[ui.track][0] == GEN_EUCLID) {
            put_str(b, "ACTION / EUCLID 2", n);
        } else {
            put_str(b, EUCLID_TITLE[ui.page % NEUCLID], n);
        }
        break;
    case M_FX: {
        static const char *const T[NFXPG] = {"REVERB", "FX BUS", "", "DELAY (1/2)", "DELAY (2/2)", "DELAY: TAPE",
                                             "ROUTING", "DIRT", "DJ FILTER", "RING (2/2)", "MASTER COMP", "COMP: SIDECHAIN",
                                             "FX SEND (1/2)", "FX SEND (2/2)", "DELAY SEND (1/2)", "DELAY SEND (2/2)",
                                             "REVERB SEND (1/2)", "REVERB SEND (2/2)"};
        uint32_t g = fx_page();
        if (g == FXPG_PARS) {                        /* (the type's knobs: named after it) */
            put_str(b, "FX BUS: ", n);
            put_str(b + 8, L_FX[par[PAR_FX_TYPE] < NFX ? par[PAR_FX_TYPE] : 0], n - 8u);
        } else {
            int gr = par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN;   /* (1.0 Phase 13: GRAIN's page second) */
            put_str(b, gr && g == FXPG_DLY ? "DELAY (1/3)" : gr && g == FXPG_DUB ? "GRAIN (2/3)"
                     : gr && g == FXPG_DLY2 ? "DELAY (3/3)"
                     : g == FXPG_DJ && pfs.dj_ftype == DJT_RING ? "RING (1/2)" : T[g], n);   /* (1.0 Phase 18) */
        }
        break;
    }
    case M_MIXER:
        put_str(b, MIXER_TITLE[(ui.page % NMIXER) / 2u], n);
        put_str(b + str_len(b), ui.page % 2u ? " (2/2)" : " (1/2)", n - str_len(b));
        break;
    case M_CONFIG: put_str(b, CONFIG_TITLE[ui.page % NCONFIG], n); break;
    case M_SAVE: put_str(b, "SAVE ", n); put_str(b + 5, IO_NAME[ui.io_type], n - 5); break;
    case M_LOAD: put_str(b, "LOAD ", n); put_str(b + 5, IO_NAME[ui.io_type], n - 5); break;
    case M_RECOPT: put_str(b, "RECORD", n); break;
    case M_KEYS: put_str(b, "KEYS", n); break;
    default: b[0] = 0; break;
    }
}
static int msg_on(void) { return ui.msg[0] && fm1_ms - ui.msg_ms < (ui.msg_long ? 6000u : 1500u); }
static void draw_title(void)
{
    char b[40];
    uint32_t pg, np, s, t = ui.track, mo = msg_on(), i;
    title_text(b, sizeof b, &pg, &np);
    s = sig_add(ui.mode, t);
    for (i = 0; b[i]; i++)                           /* (the text itself: a title that changes on its page) */
        s = sig_add(s, (uint8_t)b[i]);
    s = sig_add(s, pg | np << 8 | mo << 16 | ui.step << 20);
    s = sig_add(s, mo ? (uint32_t)ui.msg[0] | (uint32_t)ui.msg[2] << 8 | ui.msg_ms : 0u);
    if (!region(R_TITLE, s))
        return;
    cv_begin(240, TITLE_H, T_BG);
    cv_rrect(4, 2, 24, 16, 4, TRACK_COL[t], T_BG);
    cv_text_in(4, CAP_IN(S, 16) + 2, 24, &AF_S, VOICE_SHORT[t], RGB(16, 16, 16), TRACK_COL[t]);
    if (mo)
        cv_text_fit(34, CAP_IN(S, 20), &AF_S, ui.msg, T_ACCENT, T_BG, 200);
    else
        cv_text_fit(34, CAP_IN(S, 20), &AF_S, b, T_TEXT, T_BG, np > 8u ? 158 : 236 - 34 - (int32_t)np * 7 - 4);
    if (np > 8u && !mo) {                            /* the page: n / pages (many: the sound pages) */
        char d[8];
        put_int(d, (int32_t)pg + 1);
        put_str(d + str_len(d), "/", sizeof d - str_len(d));
        put_int(d + str_len(d), (int32_t)np);
        cv_text_r(236, CAP_IN(S, 20), &AF_S, d, T_MID, T_BG);
    } else if (np > 1u && !mo) {                     /* .. or dots */
        uint32_t i;
        int32_t x = 236 - (int32_t)np * 7;
        for (i = 0; i < np; i++)
            cv_rrect(x + (int32_t)i * 7, 8, 5, 5, 2, i == pg ? T_THEME : T_DIM, T_BG);
    }
    cv_blit(0, TITLE_Y);
}

/* -------------------------------------------------------------- grid --- */
static void draw_labels(void)
{
    uint32_t t, s = sig_add(ui.track, seq_mutes() | (uint32_t)sq.solo << 8), lk = 0, l;
    const pattern_t *pt = shown_pat();
    for (l = 0; l < NLINK; l++)                      /* 0.6: the tracks a link joins (a mark by the name) */
        if ((pt->link[l][1] & 15u) && (pt->link[l][1] & 15u) < NLE)
            lk |= 1u << (pt->link[l][0] & 7u) | 1u << (pt->link[l][0] >> 3 & 7u);
    s = sig_add(s, ui.perf | lk << 8);
    for (t = 0; t < NTRACK; t++)
        s = sig_add(s, fm1_ms - ui.flash_ms[t] < 90u);
    if (!region(R_LABELS, s))
        return;
    cv_begin(GRID_X, NTRACK * ROW_H, T_BG);
    for (t = 0; t < NTRACK; t++) {
        int32_t y = (int32_t)t * ROW_H;
        int muted = (seq_mutes() >> t) & 1u, flash = fm1_ms - ui.flash_ms[t] < 90u;
        uint16_t c = muted ? T_DIM : TRACK_COL[t];
        if (t == ui.track) {
            cv_rrect(2, y + 1, 27, ROW_H - 2, 3, c, T_BG);
            cv_text_in(2, y + CAP_IN(S, ROW_H), 27, &AF_S, VOICE_SHORT[t], RGB(12, 12, 12), c);
        } else {
            cv_text_in(2, y + CAP_IN(S, ROW_H), 27, &AF_S, VOICE_SHORT[t], c, T_BG);
        }
        if (flash && !muted)
            cv_rect(0, y + 4, 2, ROW_H - 8, T_ACCENT);
        if (muted)
            cv_rect(6, y + ROW_H / 2, 19, 1, t == ui.track ? RGB(12, 12, 12) : T_DIM);
        if ((sq.solo >> t) & 1u)                     /* 0.10: soloed: a bar on the right */
            cv_rect(29, y + 3, 2, ROW_H - 6, T_ACCENT);
        if (lk >> t & 1u)
            cv_rect(25, y + 4, 3, 3, t == ui.track ? RGB(12, 12, 12) : T_ACCENT);
    }
    cv_blit(0, BODY_Y);
}

static uint32_t col_sig(uint32_t c)
{
    uint32_t t, s = sig_add(c, ui.bar), st = ui.bar * 16u + c;
    s = sig_add(s, ui.track | ui.mode << 4 | (ui.mode == M_STEP && ui.step == st) << 8 | seq_mutes() << 9);
    for (t = 0; t < NTRACK; t++) {
        const pattern_t *pt = view_pat(t);
        const step_t *x = &pt->step[t][st];
        s = sig_add(s, (uint32_t)pt->cond[t][st] | (uint32_t)pt->rat[t][st] << 8 | (pt == &gpat) << 16 |
                           (uint32_t)(x->prob >> 7) << 17);
        s = sig_add(s, main_on(pt, t, st) | (st < track_len(pt, t)) << 1 |
                           (sq.running && sq.idx[t] == (int8_t)st) << 2);
        if (main_on(pt, t, st))
            s = sig_add(s, (uint32_t)x->volume | (uint32_t)x->prob << 8 | (uint32_t)pt->timing[t][st] << 16 |
                               (uint32_t)(x->p1n != NO_AUTOMATION || x->p2n != NO_AUTOMATION) << 24);
    }
    return s;
}
static void draw_col(uint32_t c)
{
    uint32_t t, st = ui.bar * 16u + c;
    uint16_t bg = (c & 3u) == 0u ? T_SURF : T_BG;
    if (!region(R_COL0 + c, col_sig(c)))
        return;
    cv_begin(CELL_W, NTRACK * ROW_H, bg);
    for (t = 0; t < NTRACK; t++) {
        const pattern_t *pt = view_pat(t);
        int32_t y = (int32_t)t * ROW_H;
        const step_t *x = &pt->step[t][st];
        int on = main_on(pt, t, st), in = st < track_len(pt, t), muted = (seq_mutes() >> t) & 1u;
        int play = sq.running && sq.idx[t] == (int8_t)st;
        uint16_t col = muted ? T_DIM : TRACK_COL[t];
        if (!in) {
            cv_rect(1, y + 7, CELL_W - 2, 1, T_LINE);
            continue;
        }
        if (play)
            cv_rect(0, y, CELL_W, ROW_H, T_SEL);
        if (on) {
            uint32_t vel = x->volume & STEP_VOLUME_MASK, h = 4u + vel * 8u / 127u;
            uint16_t fill = ux_mix(play ? T_SEL : bg, col, 45 + (int32_t)vel * 55 / 127);
            int32_t top = y + (int32_t)(ROW_H - h) / 2;
            if (step_prob(x) < 100u && step_prob(x)) {   /* a chance: an outline */
                cv_rrect(1, top, CELL_W - 2, (int32_t)h, 2, fill, play ? T_SEL : bg);
                cv_rect(3, top + 2, CELL_W - 6, (int32_t)h - 4, play ? T_SEL : bg);
            } else {
                cv_rrect(1, top, CELL_W - 2, (int32_t)h, 2, fill, play ? T_SEL : bg);
            }
            if ((pt->timing[t][st] & 15u) != 0u)     /* flam: a notch */
                cv_rect(CELL_W / 2, top, 1, (int32_t)h, play ? T_SEL : bg);
            if (x->p1n != NO_AUTOMATION || x->p2n != NO_AUTOMATION)   /* a lock: a dot */
                cv_rect(CELL_W - 4, y + 1, 3, 3, T_ACCENT);
            if ((pt->timing[t][st] >> 4) != 7u)      /* shifted: a tick under it */
                cv_rect((pt->timing[t][st] >> 4) < 7u ? 1 : CELL_W - 3, y + ROW_H - 2, 2, 1, T_TEXT);
            if (step_cond(pt, t, st))                /* a trig condition: a dot top left */
                cv_rect(1, y + 1, 3, 3, T_THEME);
            if (step_len(pt, t, st) == LEN_TIE)      /* 19b: tied: a line on into the next step */
                cv_rect(CELL_W - 3, y + ROW_H / 2, 3, 2, T_ACCENT);
            else if (step_len(pt, t, st))            /* .. a length: a line at its right edge */
                cv_rect(CELL_W - 2, top + 1, 1, (int32_t)h - 2, T_TEXT);
            if (pt->rat[t][st] & 7u)                 /* a ratchet: a bar under it, longer with more hits */
                cv_rect(2, y + ROW_H - 1, 1 + (int32_t)(pt->rat[t][st] & 7u), 1, T_ACCENT);
        } else {
            cv_rect(CELL_W / 2 - 1, y + ROW_H / 2 - 1, 2, 2, t == ui.track ? T_MID : T_LINE);
        }
        if (t == ui.track && ui.mode == M_STEP && st == ui.step)
            cv_frame(0, y, CELL_W, ROW_H, T_ACCENT);
    }
    cv_blit(GRID_X + c * CELL_W, BODY_Y);
}

static void draw_bars(void)
{
    const pattern_t *pt = shown_pat();
    uint32_t k, len = track_len(pt, ui.track), play = sq.idx[ui.track] < 0 ? 99u : (uint32_t)sq.idx[ui.track] / 16u;
    uint32_t s = sig_add(ui.bar, len);
    s = sig_add(s, sq.running ? play : 99u);
    s = sig_add(s, ui.track);
    if (!region(R_BARS, s))
        return;
    cv_begin(240, BARS_H, T_BG);
    cv_text(4, CAP_IN(S, BARS_H), &AF_S, "BAR", T_MID);
    for (k = 0; k < 4u; k++) {
        int32_t x = GRID_X + (int32_t)k * 52;
        char b[4] = {(char)('1' + k), 0};
        int used = k * 16u < len;
        uint16_t c = k == ui.bar ? T_THEME : used ? T_RAISE : T_BG;
        cv_rrect(x + 1, 2, 50, BARS_H - 4, 3, c, T_BG);
        if (!used)
            cv_frame(x + 1, 2, 50, BARS_H - 4, T_LINE);
        cv_text_in(x + 1, CAP_IN(S, BARS_H), 50, &AF_S, b, k == ui.bar ? T_INK : used ? T_TEXT : T_DIM, c);
        if (sq.running && play == k)
            cv_rect(x + 4, BARS_H - 4, 44, 2, T_ACCENT);
        if (used && len < (k + 1u) * 16u) {          /* the track ends inside this bar */
            int32_t e = x + 1 + (int32_t)(len - k * 16u) * 50 / 16;
            cv_rect(e, 2, 2, BARS_H - 4, T_ACCENT);
        }
    }
    cv_blit(0, BARS_Y);
}

static void draw_grid(void)
{
    uint32_t c;
    if (!body_is_grid) {                            /* (the body showed something else: the grid all over) */
        body_is_grid = 1;
        rsig[R_LABELS] = rsig[R_BARS] = 0;
        for (c = 0; c < 16u; c++)
            rsig[R_COL0 + c] = 0;
    }
    draw_labels();
    for (c = 0; c < 16u; c++)
        draw_col(c);
    draw_bars();
}

/* 19a: the scales' names (seq.c SC_*) */
static const char *const SCALE_NAME[NSCALE] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "MIXOLYD",
                                               "LYDIAN", "H MINOR", "MIN PNT", "MAJ PNT", "BLUES", "MINOR 7",
                                               "FIFTHS", "PHR DOM", "HIRAJO", "PELOG", "WHOLE T"};   /* (52 px at most) */
#include "ui_viz.c"                                  /* (30: the pages' pictures) */
#include "spells_viz.c"                              /* (32: SPELLS' wizard and die) */
#include "wander_viz.c"                              /* (40: WANDER's road, compass, cottage) */

/* ---------------------------------------------------- the other bodies --- */
/* the body's top h rows in strips of at most 64 rows (the canvas): draw(y0) draws with the strip's origin at -y0.
 * h is split evenly (128: two of 64; 196, the body and the cards' place: four of 49) */
static void body_strips_h(void (*draw)(int32_t), int32_t h)
{
    int32_t y, n = (h + 63) / 64, step = (h + n - 1) / n;
    for (y = 0; y < h; y += step) {
        cv_begin(240, (uint32_t)(h - y < step ? h - y : step), T_BG);
        cv_oy = -y;
        draw(y);
        cv_oy = 0;
        cv_blit(0, BODY_Y + (uint32_t)y);
    }
}
static void body_strips(void (*draw)(int32_t)) { body_strips_h(draw, BODY_H); }

static void list_row(int32_t y, const char *left, const char *right, int sel, int dim)
{
    uint16_t bg = sel ? T_SEL : T_BG;
    if (sel)
        cv_rrect(4, y, 232, 16, 3, T_SEL, T_BG);
    cv_text_fit(10, y + CAP_IN(S, 16), &AF_S, left, sel ? T_INK : dim ? T_DIM : T_MID, bg, 60);
    cv_text_fit(64, y + CAP_IN(S, 16), &AF_S, right, sel ? T_INK : dim ? T_DIM : T_TEXT, bg, 168);
}

static void draw_kit_browser(int32_t y0)             /* 0.9: the slots, the library's kits (L1 ..), 0.19 the factory's (F1 ..) */
{
    static const char *const TITLE[3] = {"KIT SLOTS", "LIBRARY KITS", "FACTORY KITS"};
    uint32_t i, k = ui.kb_sel, first = kb_step(ui.kb_sel, -3);
    (void)y0;
    for (i = 0; i < 3u && kb_step(first, 5) == kb_step(first, 6) && kb_step(first, -1) != first; i++)
        first = kb_step(first, -1);                   /* (the list's end: the last 7 shown) */
    cv_text(10, 2, &AF_S, TITLE[kb_part(ui.kb_sel)], T_THEME);
    cv_text_r(232, 2, &AF_S, "OCT+ LOAD", T_MID, T_BG);
    for (i = 0, k = first; i < 7u; i++) {
        char l[8], r[12];
        uint32_t q = kb_part(k);
        put_str(l, "", sizeof l);
        if (q == KB_SLOTS) {
            two_digits(l, k + 1u);
            memcpy(r, kit_names[k], 8);
            if (k == kit_loaded)
                put_str(l + 2, " *", sizeof l - 2);
        } else {
            l[0] = q == KB_LIB ? 'L' : 'F';
            put_int(l + 1, (int32_t)(k - KB_FIRST[q] + 1u));
            if (q == KB_FACTORY)
                kit_factory_name(k - KB_FAC, r);
            else if (kl_name(KLK_KIT, k - NUM_KITS, r))
                memset(r, ' ', 8);
        }
        r[8] = 0;
        list_row(18 + (int32_t)i * 16, l, r, k == ui.kb_sel, 0);
        if (kb_step(k, 1) == k)
            break;
        k = kb_step(k, 1);
    }
}

static void draw_sound_browser(int32_t y0)           /* 0.8 */
{
    int32_t i, first = (int32_t)ui.sb_sel - 3, rows = sb_n < 7u ? (int32_t)sb_n : 7;
    char t[24];
    (void)y0;
    if (first > (int32_t)sb_n - rows)
        first = (int32_t)sb_n - rows;
    if (first < 0)
        first = 0;
    put_str(t, "SOUND ", sizeof t);
    put_str(t + 6, VOICE_NAME[ui.track], sizeof t - 6u);
    cv_text(10, 2, &AF_S, t, T_THEME);
    cv_text_r(232, 2, &AF_S, "OCT+ KEEP  OCT- BACK", T_MID, T_BG);
    for (i = 0; i < rows; i++) {
        uint32_t e = (uint32_t)(first + i);
        char l[16], r[12];
        sb_label(e, ui.sb_voice, r, l, sizeof l);
        r[8] = 0;
        list_row(18 + i * 16, l, r, e == ui.sb_sel, 0);
    }
}

static void draw_clear(int32_t y0)
{
    (void)y0;
    cv_rrect(10, 20, 220, 88, 6, T_SURF, T_BG);
    cv_text_in(10, 30, 220, &AF_S, "SHIFT + COPY", T_MID, T_SURF);
    cv_text_in(10, 50, 220, &AF_M, CLEAR_NAME[ui.clear_sel], T_ACCENT, T_SURF);
    cv_text_in(10, 76, 220, &AF_S, "SELECT: what   COPY: confirm", T_TEXT, T_SURF);
    cv_text_in(10, 90, 220, &AF_S, "let go of SHIFT to cancel", T_MID, T_SURF);
}

static void cat(char *b, const char *s, uint32_t n);
/* 0.24.1: another app's data found at power-on (project.c fx_found): what, where to back it up, the answer */
static void draw_dx_ask(int32_t y0)
{
    char b[40], t[32];
    (void)y0;
    b[0] = 0;
    if (fx_found & FX_PROJ)
        cat(b, "PROJECTS", sizeof b);
    if (fx_found & FX_KITS)
        cat(b, b[0] ? ", KITS" : "KITS", sizeof b);
    if (fx_found & FX_SET)
        cat(b, b[0] ? ", SETTINGS" : "SETTINGS", sizeof b);
    put_str(t, fx_name(), sizeof t);
    cat(t, " DATA IS HERE", sizeof t);
    cv_rrect(6, 12, 228, 106, 6, T_SURF, T_BG);
    cv_text_in(6, 17, 228, &AF_S, t, T_MID, T_SURF);
    cv_text_in(6, 31, 228, &AF_S, b, T_TEXT, T_SURF);
    if (ui.dx_ask >= 3u) {
        cv_text_in(6, 58, 228, &AF_M, "ERASING", T_ACCENT, T_SURF);
        return;
    }
    cv_text_in(6, 45, 228, &AF_S, fx_first() < FX_NAPP ? FX_APPS[fx_first()].backup : "", T_MID, T_SURF);
    cv_text_in(6, 61, 228, &AF_M, ui.dx_ask == 2u ? "SURE? ERASE IT" : "ERASE IT FOR ELIXIR?", T_ACCENT, T_SURF);
    cv_text_in(6, 85, 228, &AF_S, ui.dx_ask == 2u ? "COPY (OCT+) again: erase" : "COPY (OCT+) twice: erase", T_TEXT,
               T_SURF);
    cv_text_in(6, 99, 228, &AF_S, "SHIFT (OCT-): keep, save nothing", T_MID, T_SURF);
}
/* 0.11: Control All's dialog, after OCT+ is let go */
static void draw_ca_ask(int32_t y0)
{
    (void)y0;
    cv_rrect(10, 20, 220, 88, 6, T_SURF, T_BG);
    cv_text_in(10, 30, 220, &AF_S, "CONTROL ALL", T_MID, T_SURF);
    cv_text_in(10, 50, 220, &AF_M, ui.ca_len ? "REVERT THE LENGTHS?" : "REVERT THE KIT?", T_ACCENT, T_SURF);
    cv_text_in(10, 76, 220, &AF_S, "OCT+: revert   OCT-: keep", T_TEXT, T_SURF);
    cv_text_in(10, 90, 220, &AF_S, "the changes sound until then", T_MID, T_SURF);
}

/* PERF: the 16 keys (lit: on, outlined: latched), then the STUTTER length, the THROW voice, the DJ filter */
static const char *const PERF_KEY[NPF2] = {"FILL", "RPT", "FRZ", "T.RV", "T.FX", "FB+", "STP", "LP~", "HP~",
                                           "HLF", "JIT", "CUT", "SHT", "PMP", "RES", "SPC", "CRS", "REV"};
static const char *const PERF_NAME[NPF2] = {"FILL", "REPEAT", "FREEZE", "THROW REV", "THROW FX", "FB PUSH",
                                            "TAPE STOP", "LP SWEEP", "HP SWEEP", "HALF", "JITTER", "CUTS", "SHORT",
                                            "PUMP", "COMB", "SPACE", "CRUSH", "REVERSE"};
/* 39: RUSH's roll at amount r: hits a step x 16, so a note value 1/16 .. 1/256 (0: no roll yet, below START) */
static uint32_t rush_den(uint32_t r)
{
    uint32_t top = (sq.ru_top < NRT ? sq.ru_top : RT_256) + 2u;
    if (r <= sq.ru_start)
        return 0;
    if (r <= sq.ru_free_at)
        return r > (sq.ru_start + sq.ru_free_at) / 2u ? 32u : 16u;
    return (16u * (uint32_t)exp2_q16((int32_t)(65536u + (r - sq.ru_free_at) * (top - 1u) * 65536u /
                                                (RUSH_MAX - sq.ru_free_at))) + 32768u) >> 16;
}
/* 39: PERF's RUSH pages: one picture of the whole rush over the knob's 0..100%: the roll's speed (from START, log),
 * the other drums left (the hats dashed), the filter (from 90), START and FREE marked, the knob where it is; the
 * stage and the roll large; under it the shown bar's RUSH locks */
static void draw_rush(int32_t y0)
{
    uint32_t t = ui.track < NTRACK ? ui.track : 0u, R = sq.ru_live ? sq.ru_live : sq.ru_R, i, den = rush_den(R);
    const pattern_t *vp = view_pat(t);
    uint16_t col = TRACK_COL[sq.ru_live || sq.ru_R ? sq.ru_tr : t];
    char b[24];
    (void)y0;
    cv_text(6, 4, &AF_M, VOICE_NAME[sq.ru_live || sq.ru_R ? sq.ru_tr : t], col);
    if (!R)
        put_str(b, "OFF", sizeof b);
    else if (!den)
        put_str(put_int(b, (int32_t)R), "%", 4);
    else {
        b[0] = '1';
        b[1] = '/';
        put_int(b + 2, (int32_t)den);
    }
    cv_text_r(234, 0, &AF_L, b, R ? T_ACCENT : T_DIM, T_BG);
    cv_text(6, 22, &AF_S, !R ? "TURN RUSH" : R <= sq.ru_start ? "THE KIT GOES OVER" : R <= sq.ru_free_at ? "ON THE GRID"
                                                                                      : "FREE ROLL", R ? T_TEXT : T_MID);
    cv_rect(6, 92, 228, 1, T_LINE);                  /* (the graph: 0..100% across, 52 px up) */
    for (i = 0; i < 228u; i++) {
        uint32_t r = i * RUSH_MAX / 227u, d = rush_den(r), y;
        int32_t x = 6 + (int32_t)i;
        y = 65536u - rush_share(r, RU_ONLY);         /* the other drums left */
        cv_rect(x, 91 - (int32_t)(y * 50u >> 16), 1, 1, T_MID);
        y = 65536u - rush_share(r, RU_HATS);
        if (i & 2u)
            cv_rect(x, 91 - (int32_t)(y * 50u >> 16), 1, 1, T_DIM);
        if (d)                                       /* the roll: log2 of 1/16 .. 1/256 */
            cv_rect(x, 90 - (int32_t)((uint32_t)(log2_q16(d) - (4 << 16)) * 50u / (4u << 16)), 1, 3, col);
        if (sq.ru_filt && r > RU_HPFROM)
            cv_rect(x, 92 - (int32_t)((r - RU_HPFROM) * sq.ru_filt / 100u * 12u / (RUSH_MAX - RU_HPFROM)), 1, 1,
                    T_REC);
    }
    cv_rect(6 + (int32_t)(sq.ru_start * 227u / RUSH_MAX), 88, 1, 8, T_MID);
    cv_rect(6 + (int32_t)(sq.ru_free_at * 227u / RUSH_MAX), 88, 1, 8, T_MID);
    if (R)
        cv_rect(6 + (int32_t)(R * 227u / RUSH_MAX) - 1, 38, 3, 58, T_ACCENT);
    for (i = 0; i < 16u; i++) {                      /* the shown bar's RUSH locks */
        uint32_t s = ui.bar * 16u + i, v = rush_lock(vp, t, s), h = v * 8u / RUSH_MAX;
        int32_t bx = 6 + (int32_t)i * 14;
        int at = sq.running && sq.idx[t] == (int32_t)s, held = ui.held_step == (int8_t)s;
        cv_rect(bx, 107, 12, 1, main_on(vp, t, s) ? T_MID : T_SURF);
        if (h)
            cv_rect(bx, 107 - (int32_t)h, 12, (int32_t)h, held ? T_ACCENT : TRACK_COL[t]);
        if (at || held)
            cv_rect(bx, 108, 12, 1, held ? T_ACCENT : T_THEME);
    }
    if (ui.held_step >= 0)
        cv_text(6, 112, &AF_S, "TURN RUSH: THIS STEP'S LOCK", T_ACCENT);
    else if (sq.rec && sq.running)
        cv_text(6, 112, &AF_S, "REC: RUSH WRITES INTO THE STEPS", T_REC);
    else
        cv_text(6, 112, &AF_S, "BLACK: TRACK  HOLD A STEP: LOCK", T_MID);
}
/* 0.23-t7: TRANSPOSE: the white keys, the middle one 0; each track where it is (lit: the keys move it) */
static void draw_trp(int32_t y0)
{
    const pattern_t *p = &pat[sq.active];
    uint32_t t = ui.track < NTRACK ? ui.track : 0u, i;
    int32_t x = sq.trp[t];
    char b[24];
    (void)y0;
    cv_text(6, 4, &AF_M, VOICE_NAME[t], TRACK_COL[t]);
    if (x > 0) {
        b[0] = '+';
        put_int(b + 1, x);
    } else {
        put_int(b, x);
    }
    cv_text_r(234, 0, &AF_L, b, x ? T_ACCENT : T_DIM, T_BG);
    cv_text(6, 22, &AF_S, p->scale && p->scale < NSCALE ? SCALE_NAME[p->scale] : "SEMITONES", T_MID);
    for (i = 0; i < 16u; i++) {                      /* (the keys) */
        int32_t kx = 6 + (int32_t)i * 14;
        cv_rrect(kx, 40, 12, 30, 2, i == 7u ? T_THEME : T_SURF, T_BG);
        if ((int32_t)i == x + 7)
            cv_rect(kx + 2, 58, 8, 10, TRACK_COL[t]);
        if ((int32_t)i == sq.trpend[t] + 7 && sq.trdue)
            cv_rect(kx + 2, 42, 8, 2, T_ACCENT);
    }
    for (i = 0; i < NTRACK; i++) {                   /* (each track, a row) */
        int on = (sq.trmask >> i) & 1u;
        if (on || sq.trp[i])
            cv_rect(6 + (sq.trp[i] + 7) * 14 + 1, 76 + (int32_t)i * 4, 10, 3, on ? TRACK_COL[i] : T_DIM);
    }
    cv_text(6, 112, &AF_S, sq.trdue ? (sq.trwhen == 2u ? "ON THE NEXT BAR" : "ON THE NEXT BEAT")
                                    : "BLACK: TRACKS  WHITE: DEGREES", sq.trdue ? T_ACCENT : T_MID);
}
/* 0.25.2: a global LFO's DEST turned: its full name over the page a while, as MODEL shows a model */
#define DEST_POP_MS 3000u
static int dest_pop(void)
{
    int32_t g = glfo_page();
    return g >= 0 && ui.dest_ms && fm1_ms - ui.dest_ms < DEST_POP_MS && ui.dest_p == VP_GL + (uint32_t)g * NGLSET;
}
static void draw_dest_pop(void)
{
    uint32_t n = (uint32_t)glfo_page(), d = gls[n % NGLFO].dest < NGD ? gls[n % NGLFO].dest : GD_OFF, i;
    char b[40];
    cv_rrect(4, 4, 232, 76, 6, T_THEME, T_BG);
    cv_rrect(6, 6, 228, 72, 5, T_SURF, T_THEME);
    put_str(b, "GLOBAL LFO ", sizeof b);
    put_int(b + str_len(b), (int32_t)n + 1);
    put_str(b + str_len(b), d == GD_OFF ? " DEST" : " DEST: ", sizeof b - str_len(b));
    put_str(b + str_len(b), GD_FULL[d][0], sizeof b - str_len(b));
    cv_text_fit(14, 10, &AF_S, b, T_MID, T_SURF, 212);
    cv_text_fit(14, 28, &AF_M, GD_FULL[d][1], d == GD_OFF ? T_DIM : T_THEME, T_SURF, 212);
    if (d >= GD_MACRO1 && d < GD_MACRO1 + NMACRO) {  /* (a macro: what it moves) */
        uint32_t q = pfs.m[d - GD_MACRO1].par;
        if (q)
            mod_target_name(b, lock_idx_of(q), 1, sizeof b);
        else
            put_str(b, "No target (MACRO SETUP)", sizeof b);
        cv_text_fit(14, 52, &AF_S, b, T_TEXT, T_SURF, 212);
    }
    for (i = 0; i < NGD && GD_ORDER[i] != d; i++)    /* (where it is in the list) */
        ;
    put_int(b, (int32_t)i + 1);
    put_str(b + str_len(b), "/", sizeof b - str_len(b));
    put_int(b + str_len(b), (int32_t)NGD);
    cv_text_r(226, 10, &AF_S, b, T_DIM, T_SURF);
}
/* 1.0: the GENERATOR knob turned: the generator's name and what it does over the page a while (as DEST's), in
 * GEN_ORDER's order (the knob's) */
static int32_t help_text_m(int32_t x, int32_t y, const char *s, uint16_t c, int32_t left);
static const char *const GEN_NAME[NGEN] = {"OFF", "EUCLID", "MUTATE", "TURING", "CELLULAR", "GRIDS"};
static const char *const GEN_DESC[NGEN] = {
    "No generator: the track plays its steps as they are.",
    "Writes the steps: Hits spread as evenly as they go over Length, turned by Rotate.",
    "Each time round, a step flipped or moved or a lock drifted, at Amount's chance. Freeze stops it.",
    "A shift register loop: Lock 127 repeats it, lower flips its bits. Density: how many steps play.",
    "Each time round, the steps become the next row of a cellular automaton (Rule 0-255).",
    "Mutable Instruments' Grids: Part picks the drum, Density how busy, Accent the loud hits."};
static int gen_pop(void)
{
    return ui.mode == M_EUCLID && ui.page % NEUCLID == PG_GEN && ui.dest_ms && fm1_ms - ui.dest_ms < DEST_POP_MS &&
           ui.dest_p == VP_GMODE && !ui.kb_open && !ui.clear_open;
}
static void draw_gen_pop(int32_t y0)
{
    uint32_t g = vp_get(VP_GMODE) % NGEN;
    char b[16];
    (void)y0;
    cv_rrect(4, 4, 232, 120, 6, T_THEME, T_BG);
    cv_rrect(6, 6, 228, 116, 5, T_SURF, T_THEME);
    cv_text(14, 10, &AF_S, "GENERATOR", T_MID);
    cv_text_fit(14, 28, &AF_M, GEN_NAME[g], g ? T_THEME : T_DIM, T_SURF, 212);
    help_text_m(14, 58, GEN_DESC[g], T_TEXT, 14);
    put_int(b, (int32_t)g + 1);
    put_str(b + str_len(b), "/", sizeof b - str_len(b));
    put_int(b + str_len(b), (int32_t)NGEN);
    cv_text_r(226, 10, &AF_S, b, T_DIM, T_SURF);
}
static void draw_perf_page(int32_t y0);
static uint32_t glfo_sig(uint32_t i) { return gls[i].dest != GD_OFF ? (uint32_t)gl.out[i] >> 11 : 0u; }
static void draw_perf(int32_t y0)
{
    draw_perf_page(y0);
    if (dest_pop())
        draw_dest_pop();
}
static void draw_perf_page(int32_t y0)
{
    static const char *const REP[4] = {"1/4", "1/8", "1/16", "1/32"};
    uint32_t i;
    char b[40];
    int32_t d = pf.dj_d;
    (void)y0;
    int b2 = perf_bank2();
    for (i = 0; i < 16u; i++) {                      /* (PERFORM 2: its bank, 1..7 DRILL) */
        int32_t x = 3 + (int32_t)(i % 8u) * 29, y = 4 + (int32_t)(i / 8u) * 26;
        uint32_t e = (b2 ? NPF : 0u) + i - NTRACK;
        int on = i < NTRACK ? ((b2 ? sq.drill : sq.roll) >> i) & 1u : (pf.on >> e) & 1u;
        int lat = i < NTRACK ? ((b2 ? ui.drill_latch : ui.roll_latch) >> i) & 1u : (ui.pf_latch >> e) & 1u;
        uint16_t c = on ? (i < NTRACK ? TRACK_COL[i] : T_THEME) : T_SURF;
        cv_rrect(x, y, 27, 22, 3, lat ? T_ACCENT : c, T_BG);
        if (lat)
            cv_rrect(x + 2, y + 2, 23, 18, 2, c, T_ACCENT);
        text_c(x + 1, y + CAP_IN(S, 22), 25, &AF_S, i < NTRACK ? VOICE_SHORT[i] : PERF_KEY[e],
               on ? RGB(12, 12, 12) : (i < NTRACK && ((seq_mutes() >> i) & 1u)) ? T_DIM : T_TEXT, c);
    }
    put_str(b, b2 ? "1-7: DRILL  REPEAT " : "REPEAT ", sizeof b);
    put_str(b + str_len(b), REP[pfs.repeat & 3u], sizeof b - str_len(b));
    put_str(b + str_len(b), "   THROW ", sizeof b - str_len(b));
    put_str(b + str_len(b), VOICE_SHORT[track_voice(ui.track)], sizeof b - str_len(b));
    if (perf_dens() && (pfs.dens || dundo == 1u))    /* 31: DENSITY's COMMIT, its UNDO */
        cv_text(6, 60, &AF_S, pfs.dens ? "OCT+: COMMIT INTO THE PATTERN" : "OCT+: UNDO THE COMMIT", T_ACCENT);
    else
        cv_text(6, 60, &AF_S, b, T_TEXT);
    cv_text(6, 78, &AF_S, "LP", d < 0 ? T_THEME : T_DIM);
    cv_text_r(234, 78, &AF_S, "HP", d > 0 ? T_THEME : T_DIM, T_BG);
    cv_rect(26, 84, 186, 4, T_SURF);                  /* the DJ filter: where it is */
    cv_rect(118, 80, 2, 12, T_LINE);
    cv_rect(118 + d * 92 / 65536 - 2, 80, 5, 12, d ? T_ACCENT : T_MID);
    if (tape_q16 < 65536u) {
        cv_text(6, 98, &AF_S, "TAPE", T_REC);
        cv_rect(44, 102, (int32_t)(tape_q16 * 186u >> 16), 4, T_REC);
    } else if (glfo_page() >= 0) {                   /* 0.13: the global LFOs as they move */
        for (i = 0; i < NGLFO; i++) {                /* (1.0 Phase 11: four; FOLLOW's level as its bar) */
            static const char *const L[NGLFO] = {"L1", "L2", "L3", "L4"};
            int32_t x = 6 + (int32_t)i * 58;
            int on = gls[i].dest != GD_OFF && gls[i].depth;
            cv_text(x, 98, &AF_S, L[i], (int32_t)i == glfo_page() ? T_THEME : T_MID);
            cv_rect(x + 20, 102, 34, 4, T_SURF);
            if (gls[i].dest != GD_OFF)               /* (Dest OFF: no motion, so a live LFO stands out) */
                cv_rect(x + 20, 102, (int32_t)((uint32_t)gl.out[i] * 34u >> 16), 4, on ? T_ACCENT : T_DIM);
        }
    } else if (perf_stut()) {                        /* 0.16: STUTTER: the burst as it plays, a box a SIZE */
        uint32_t u = pf.st_on ? pf.st_units : 0u, k;
        cv_text(6, 98, &AF_S, pf.st_on == 2u ? "STUTTER" : pf.st_on ? "LIVE" : "", pf.st_on ? T_ACCENT : T_DIM);
        for (k = 0; k < u && k < 16u; k++)
            cv_rect(80 + (int32_t)k * 9, 100, 7, 7, k < pf.st_rep ? T_ACCENT : k == pf.st_rep ? T_THEME : T_SURF);
    } else if (perf_dens()) {                        /* 19d: the tracks DENSITY moves (left out: struck through) */
        for (i = 0; i < NTRACK; i++) {
            int32_t x = 6 + (int32_t)i * 33;
            int out = (pfs.dex >> i) & 1u;
            cv_text(x + 4, 98, &AF_S, VOICE_SHORT[i], out ? T_DIM : TRACK_COL[i]);
            if (out)
                cv_rect(x + 2, 98 + 7, 22, 1, T_TEXT);
        }
        cv_rect(26, 118, 186, 2, T_SURF);            /* (where DENSITY is, the LFOs' push in it) */
        cv_rect(118, 115, 2, 8, T_LINE);
        cv_rect(118 + sq.dens * 92 / 64 - 2, 115, 5, 8, sq.dens ? T_ACCENT : T_MID);
    }
    b[0] = 0;
    for (i = 0; i < NPF2; i++)                       /* what is on, by name */
        if ((pf.on >> i) & 1u) {
            if (b[0])
                put_str(b + str_len(b), " ", sizeof b - str_len(b));
            put_str(b + str_len(b), PERF_NAME[i], sizeof b - str_len(b));
        }
    if (ui.shift)
        cv_text(6, 112, &AF_S, "SHIFT + KEY: LATCH (AGAIN: OFF)", T_MID);
    else if (perf_dens())
        return;
    else if (perf_stut())
        cv_text(6, 112, &AF_S, pf.st_hold ? "HELD: STUTTERING" : "OCT+ HELD: STUTTER NOW", pf.st_hold ? T_ACCENT : T_MID);
    else if (b[0])
        cv_text_fit(6, 112, &AF_S, b, T_ACCENT, T_BG, 228);
    else if (glfo_page() >= 0 && gls[glfo_page()].wave == GW_FOLLOW)   /* (1.0 Phase 11) */
        cv_text(6, 112, &AF_S, "FOLLOW: + DEPTH PUSHES, - DUCKS", T_MID);
    else if (glfo_page() >= 0)
        cv_text(6, 112, &AF_S, "OCT+: TRIGGER THIS LFO", T_MID);
    else
        cv_text(6, 112, &AF_S, "BLACK: MUTE  BAR KEYS: REPEAT", T_MID);
}

/* 37: instructions. A term in the theme's colour, a gap, then its text, wrapped to the left margin; every page spaces
 * them alike (HELP_LH a line, HELP_GAP between items). Returns the next item's y */
#define HELP_X 10
#define HELP_LH 14
#define HELP_GAP 8
#define HELP_R 232
static int32_t help_text_m(int32_t x, int32_t y, const char *s, uint16_t c, int32_t left);
static int32_t help_text(int32_t x, int32_t y, const char *s, uint16_t c) { return help_text_m(x, y, s, c, HELP_X); }
/* .. its lines after the first from `left` (1.0: a block of text indented as a whole, the generator's words) */
static int32_t help_text_m(int32_t x, int32_t y, const char *s, uint16_t c, int32_t left)
{
    char line[48];
    while (*s == ' ')
        s++;
    while (*s) {
        uint32_t cut = 0, e = 0;
        while (s[e]) {                                /* the most words that fit */
            while (s[e] == ' ')
                e++;
            while (s[e] && s[e] != ' ')
                e++;
            if (e >= sizeof line)
                break;
            memcpy(line, s, e);
            line[e] = 0;
            if (cut && x + text_w(&AF_S, line) > HELP_R)
                break;
            cut = e;
        }
        if (!cut)
            break;
        memcpy(line, s, cut);
        line[cut] = 0;
        cv_text(x, y, &AF_S, line, c);
        x = left;
        s += cut;
        while (*s == ' ')
            s++;
        if (*s)
            y += HELP_LH;
    }
    return y + HELP_LH;
}
static int32_t help_item(int32_t y, const char *term, const char *text)
{
    int32_t x = HELP_X;
    if (term && term[0])
        x = cv_text(HELP_X, y, &AF_S, term, T_THEME) + 6;
    return help_text(x, y, text, T_TEXT) + HELP_GAP;
}

/* .. a line of key hints: each key (or term) in the theme's colour, then what it does; n pairs */
static void help_pairs(int32_t x, int32_t y, const char *const *kv, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        x = cv_text(x, y, &AF_S, kv[2u * i], T_THEME) + 4;
        x = cv_text(x, y, &AF_S, kv[2u * i + 1u], T_TEXT) + 9;
    }
}

static void draw_shift_legend(int32_t y0)
{
    static const char *const F[6] = {"KIT", "MORPH", "RESET SOUND", "RESET PERF", "SOLO", "RELOAD"};
    uint32_t i;
    (void)y0;
    cv_text(8, 0, &AF_S, "SHIFT + WHITE KEY", T_THEME);
    for (i = 0; i < NTRACK; i++) {                   /* keys 1..7: play the voice */
        int32_t x = 4 + (int32_t)i * 33;
        cv_rrect(x, 14, 31, 16, 3, T_RAISE, T_BG);
        text_c(x, 14 + CAP_IN(S, 16), 31, &AF_S, VOICE_SHORT[i], T_TEXT, T_RAISE);
    }
    for (i = 0; i < 6u; i++) {                       /* 1.0: keys 8..13, the menu (two a row, in key order) */
        int32_t x = 4 + (int32_t)(i % 2u) * 117, y = 33 + (int32_t)(i / 2u) * 18;
        char b[16];
        put_int(b, (int32_t)i + 8);
        cv_rrect(x, y, 115, 16, 3, T_SEL, T_BG);
        cv_text(x + 4, y + CAP_IN(S, 16), &AF_S, b, T_INK);
        cv_text_fit(x + 22, y + CAP_IN(S, 16), &AF_S, F[i], T_INK, T_SEL, 90);
    }
    {
        static const char *const L1[6] = {"VOICE", "mute", "GLO", "solo", "BAR", "length"};
        static const char *const L2[6] = {"SEQ", "links", "EDIT", "keys", "PLAY", "song"};
        static const char *const L3[6] = {"REC", "options", "HOME", "perf reset", "KNOB", "fine"};
        help_pairs(8, 90, L1, 3);
        help_pairs(8, 103, L2, 3);
        help_pairs(8, 116, L3, 3);
    }
}

/* 0.8: KEYS: the FM-1's keyboard (from F; held keys in the track's colour), the last note, how to play */
static int32_t key_x(uint32_t k)                     /* the left of key k (semitones above the first F) */
{
    static const uint8_t WI[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};   /* F .. E: the white key at or below */
    return 8 + (int32_t)(k / 12u * 7u + WI[k % 12u]) * 14;
}
static void draw_keys(int32_t y0)
{
    uint32_t k;
    char b[32];
    uint16_t c = TRACK_COL[ui.track];
    (void)y0;
    for (k = 0; k < 27u; k++) {                      /* the white keys, then the black ones over them */
        uint32_t s = k % 12u;
        int held = (ui.keys >> k) & 1u;
        if (s == 1u || s == 3u || s == 5u || s == 8u || s == 10u)
            continue;
        cv_rrect(key_x(k), 6, 13, 64, 2, held ? c : T_RAISE, T_BG);
        if ((53u + k + 12u * ui.koct - 36u) % 12u == 0u)
            cv_rect(key_x(k) + 4, 60, 5, 2, held ? RGB(12, 12, 12) : T_THEME);
    }
    for (k = 0; k < 27u; k++) {
        uint32_t s = k % 12u;
        int held = (ui.keys >> k) & 1u;
        if (!(s == 1u || s == 3u || s == 5u || s == 8u || s == 10u))
            continue;
        cv_rect(key_x(k) + 8, 6, 11, 40, T_BG);       /* (a gap round it) */
        cv_rrect(key_x(k) + 9, 6, 9, 39, 2, held ? c : T_MID, T_BG);
        if (!held)
            cv_rect(key_x(k) + 10, 7, 7, 37, T_BG);
    }
    put_str(b, "F", sizeof b);
    put_int(b + 1, (int32_t)ui.koct);
    cv_text(8, 76, &AF_S, b, T_MID);
    if (ui.knote < 128u) {
        note_name(b, ui.knote);
        cv_text_r(232, 74, &AF_M, b, T_THEME, T_BG);
    }
    {
        static const char *const L1[2] = {"SHIFT + KEY", "accent"}, *const LR[2] = {"REC", "the notes go into the steps"};
        static const char *const L2[4] = {"ALGORITHM", "track", "SHIFT + EDIT", "grid"};
        help_pairs(8, 96, sq.rec && sq.running ? LR : L1, 1);
        help_pairs(8, 112, L2, 2);
    }
}

static void draw_patterns(int32_t y0)
{
    uint32_t i;
    (void)y0;
    for (i = 0; i < NUM_PATTERN; i++) {
        int32_t x = 4 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u / 4u) * 2, y = 4 + (int32_t)(i / 16u) * 26;
        const pattern_t *p = &pat[i];
        int used = p->main[0][0] | p->main[1][0] | p->main[2][0] | p->main[3][0] | p->main[4][0] | p->main[5][0] |
                   p->main[6][0] | p->main[0][1] | p->main[3][1] | p->main[5][1];
        uint16_t c = i == sq.active ? T_THEME : i == sq.pending ? T_ACCENT : used ? T_RAISE : T_SURF;
        cv_rrect(x, y, 13, 21, 2, c, T_BG);
        if (i / 16u == ui.bank)
            cv_rect(x, y + 22, 13, 2, T_TEXT);
        if (used && i != sq.active && i != sq.pending)
            cv_rect(x + 5, y + 10, 3, 3, T_TEXT);
    }
    {   /* (1.0, the user: the keys, larger, in place of 37's lock; the Voices card says FREE / LOCKED / KIT) */
        static const char *const K[3][2] = {{"COPY", "hold OCT+, tap pattern"}, {"PASTE", "hold OCT+, tap target"},
                                            {"DELETE", "hold OCT-, tap pattern"}};
        for (i = 0; i < 3u; i++) {
            cv_text(10, 60 + (int32_t)i * 21, &AF_M, K[i][0], T_THEME);
            cv_text(84, 60 + (int32_t)i * 21, &AF_M, K[i][1], T_TEXT);
        }
    }
}

static void draw_song(int32_t y0)
{
    int32_t i, first = (int32_t)ui.song_row - 3;
    (void)y0;
    if (first < 0)
        first = 0;
    if (first > MAX_SONG_LENGTH - 8)
        first = MAX_SONG_LENGTH - 8;
    for (i = 0; i < 8; i++) {
        uint32_t r = (uint32_t)(first + i), v = song_row(songs[sq.song][r]);
        char l[8], b[16];
        two_digits(l, r + 1u);
        if (sq.song_mode && sq.running && sq.song_pos == r)
            put_str(l + 2, " >", 6);
        if (v == SONG_END)
            put_str(b, "END", sizeof b);
        else if (v == SONG_LOOP)
            put_str(b, "LOOP", sizeof b);
        else {
            uint32_t n = song_reps(sq.song, r);
            put_str(b, "PATTERN ", sizeof b);
            two_digits(b + 8, v + 1u);
            if (sq.song_mode && sq.running && sq.song_pos == r)
                n = song_times();                    /* (42: ROADS: this time's) */
            if (sq.song_mode && sq.running && sq.song_pos == r && n > 1u) {   /* (0.19: playing: the time it is) */
                put_str(b + 10, "  ", sizeof b - 10u);
                put_str(put_int(b + 12, (int32_t)sq.song_n + 1), "/", 2);
                put_int(b + str_len(b), (int32_t)n);
            } else if (n > 1u) {
                put_str(b + 10, "  x", sizeof b - 10u);
                put_int(b + 13, (int32_t)n);
            }
        }
        list_row(i * 16, l, b, r == ui.song_row, v == SONG_END);
    }
}

static void io_slot_name(char *b, uint32_t n, uint32_t type, uint32_t s)
{
    if (type == IO_FACTORY || type == IO_FMORPH) {
        if (s >= FACTORY_NKITS) {
            put_str(b, "ALL > SLOTS", n);
        } else {
            kit_factory_name(s, b);
            b[8] = 0;
        }
        return;
    }
    if (io_lib()) {                                  /* 0.8: the library's */
        static const char *const KIND[4] = {"  DRUM", "  SNARE", "  CYMBAL", "  HIHAT"};
        uint8_t h[3];
        if (kl_name(type == IO_SOUND ? KLK_SOUND : KLK_KIT, s, b)) {
            put_str(b, "(empty)", n);
        } else {
            b[8] = 0;
            if (type == IO_SOUND && !kl_peek(KLK_SOUND, s, h, 3) && h[0] < 4u)
                put_str(b + 8, KIND[h[0]], n - 8u);
        }
    } else if (type == IO_KIT || type == IO_MORPH) {
        memcpy(b, kit_names[s], 8);
        b[8] = 0;
    } else if (type == IO_PROJECT) {
        if (proj_exists(s)) {
            memcpy(b, pj.name[s], 8);
            b[8] = 0;
        } else {
            put_str(b, "(empty)", n);
        }
    } else if (type == IO_PATTERN) {
        const pattern_t *p = &pat[s];
        uint32_t t, k, on = 0;
        for (t = 0; t < NTRACK; t++)
            for (k = 0; k < 4u; k++)
                on += (uint32_t)__builtin_popcount(p->main[t][k]);
        put_int(b, (int32_t)on);
        put_str(b + (on >= 100 ? 3 : on >= 10 ? 2 : 1), on == 1u ? " step" : " steps", n - 4u);
        if (s == sq.active)
            put_str(b + str_len(b), "  (playing)", n - str_len(b));
    } else {
        uint32_t k = 0;
        while (k < MAX_SONG_LENGTH && song_row(songs[s][k]) != SONG_END)
            k++;
        put_int(b, (int32_t)k);
        put_str(b + str_len(b), " rows", n - str_len(b));
    }
}
static void draw_io(int32_t y0)
{
    (void)y0;
    if (ui.io_naming) {                              /* the name: big, the cursor under its character */
        uint32_t i;
        cv_text(10, 4, &AF_S, "NAME", T_MID);
        for (i = 0; i < 8u; i++) {
            char c[2] = {ui.io_name[i] ? ui.io_name[i] : ' ', 0};
            int32_t x = 12 + (int32_t)i * 27;
            cv_rrect(x, 26, 24, 34, 4, i == ui.io_pos ? T_SEL : T_SURF, T_BG);
            text_c(x, 26 + CAP_IN(M, 34), 24, &AF_M, c, i == ui.io_pos ? T_INK : T_TEXT, i == ui.io_pos ? T_SEL : T_SURF);
        }
        {
            static const char *const L1[6] = {"K1", "position", "K2", "letter", "K3", "case"};
            static const char *const L2[4] = {"OCT+", "save", "OCT-", "back"};
            help_pairs(10, 76, L1, 3);
            help_pairs(10, 92, L2, 2);
        }
        return;
    }
    {
        int32_t i, max = (int32_t)vp_max(VP_IO_SLOT), first = (int32_t)ui.io_slot - 3, rows = max + 1 < 7 ? max + 1 : 7;
        if (first < 0)
            first = 0;
        if (first > max + 1 - rows)
            first = max + 1 - rows;
        for (i = 0; i < rows; i++) {
            uint32_t s = (uint32_t)(first + i);
            char l[8], r[32];
            if (s >= 99u)
                put_int(l, (int32_t)s + 1);
            else
                two_digits(l, s + 1u);
            io_slot_name(r, sizeof r, ui.io_type, s);
            list_row(i * 16, l, r, s == ui.io_slot, 0);
        }
        {
            static const char *const LS[4] = {"OCT+", "save", "SAVE", "the load page"};
            static const char *const LL[4] = {"OCT+", "load", "SAVE", "the save page"};
            help_pairs(10, 116, ui.mode == M_SAVE ? LS : LL, 2);
        }
    }
}

/* 1.0 Phase 12 (the user: six bars over four knobs confused): the mixer's pages (and FX's send pages) draw a bar a
 * knob, each over its card: a voice's volume, pan or send in its colour (a volume with the voice's level as it
 * plays), anything else (FX mix, Master, the reverb's) in the theme's; no knob, no bar */
static int32_t mixer_voice(uint32_t p, int *pan)     /* the voice a knob's parameter belongs to, -1 none */
{
    uint32_t v;
    *pan = 0;
    for (v = 0; v < NVOICE; v++) {
        if (p == (v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u)) {
            *pan = 1;
            return (int32_t)v;
        }
        if (p == PAR_VOL1 + v || p == PAR_FX_SEND1 + v || p == PAR_REV_SEND1 + v || p == PAR_DSEND1 + v)
            return (int32_t)v;
    }
    return -1;
}
static uint32_t mixer_sig(void)
{
    uint16_t k[4];
    uint32_t i, s = 7;
    int pan;
    page_params(k);
    for (i = 0; i < 4u; i++) {
        int32_t v = mixer_voice(k[i], &pan);
        s = sig_add(s, k[i] | (k[i] != VP_NONE ? vp_get(k[i]) : 0u) << 16);
        if (v >= 0 && k[i] == PAR_VOL1 + (uint32_t)v)
            s = sig_add(s, (uint32_t)(voice_peak[v] >> 10));
    }
    return s;
}
static void draw_mixer(int32_t y0)
{
    uint16_t k[4];
    uint32_t i;
    (void)y0;
    page_params(k);
    for (i = 0; i < 4u; i++) {
        uint32_t p = k[i], val, mn, mx;
        int32_t x = (int32_t)i * 60 + 17, h, v;
        int pan;
        uint16_t c;
        char b[24];
        if (p == VP_NONE)
            continue;
        v = mixer_voice(p, &pan);
        val = vp_get(p);
        mn = vp_min(p);
        mx = vp_max(p);
        c = v >= 0 ? TRACK_COL[v] : T_THEME;
        cv_rrect(x, 8, 26, 96, 3, T_SURF, T_BG);
        if (pan) {                                   /* (pan: from the middle to where it is) */
            int32_t y = 104 - 3 - (int32_t)(val * 90u / 127u), m = 104 - 3 - 45;
            cv_rect(x + 2, 104 - 3 - 45, 22, 1, T_LINE);
            cv_rect(x + 3, y < m ? y : m, 20, (y < m ? m - y : y - m) + 2, ux_mix(T_SURF, c, 55));
        } else {
            h = mx > mn ? (int32_t)((val - mn) * 90u / (mx - mn)) : 0;
            cv_rect(x + 3, 104 - h - 3, 20, h, ux_mix(T_SURF, c, 55));
            if (v >= 0 && p == PAR_VOL1 + (uint32_t)v) {   /* (a volume: the voice's level as it plays) */
                int32_t pk = voice_peak[v] * 90 / 32768;
                if (pk > 0)
                    cv_rect(x + 3, 104 - pk - 3, 20, 2, T_ACCENT);
            }
        }
        if (v >= 0)
            put_str(b, v == 5 ? "HH" : VOICE_SHORT[v], sizeof b);
        else
            card_name(b, sizeof b, p);
        text_c((int32_t)i * 60, 110, 60, &AF_S, b, v >= 0 ? c : T_MID, T_BG);
    }
}

static void draw_fx(int32_t y0)
{
    (void)y0;
    vz_h = 122;
    switch (fx_page()) {                             /* 30: each page's picture (ui_viz.c) */
    case FXPG_REV: viz_reverb(); break;
    case FXPG_BUS: case FXPG_PARS: viz_fxbus(); break;
    case FXPG_DLY: viz_echoes(0, 0); break;
    case FXPG_DLY2: viz_echoes(0, 1); break;
    case FXPG_DUB: viz_echoes(1, 0); break;
    case FXPG_ROUTE: viz_route(); break;                 /* (1.0 Phase 16) */
    case FXPG_DIRT: viz_dirt(); break;
    case FXPG_DJ: case FXPG_DJ2: viz_djf(); break;
    case FXPG_COMP: viz_mcomp(0); break;
    case FXPG_COMP2: viz_mcomp(1); break;
    default: draw_mixer(y0); break;                  /* (31: the sends, as the mixer shows them) */
    }
}

/* the audio ISR's load (audio.c): the average, the peak since CONFIG opened, the halves it was late for. A bar
 * at the bottom of the CONFIG pages */
static uint32_t cpu_pct(void) { return (cpu_q8 * 100u + 128u) >> 8; }
static uint32_t cpu_peak_pct(void) { return audio_max_us * 100u / (HALF_FRAMES * 1000000u / FS); }
/* the battery (master.c batt_level), at the CPU line's right end: three bars (one left: red, none: a red outline);
 * on USB power, USB */
static void draw_batt(int32_t y)
{
    int32_t lvl = batt_level(), x = 210, i;
    uint16_t c = lvl == 0 ? T_REC : T_MID;
    if (lvl == 4) {
        cv_text_r(234, y, &AF_S, "USB", T_MID, T_BG);
        return;
    }
    cv_rect(x, y + 1, 20, 1, c);
    cv_rect(x, y + 10, 20, 1, c);
    cv_rect(x, y + 1, 1, 10, c);
    cv_rect(x + 19, y + 1, 1, 10, c);
    cv_rect(x + 20, y + 4, 2, 4, c);
    for (i = 0; i < lvl; i++)
        cv_rect(x + 2 + i * 6, y + 3, 5, 6, lvl == 1 ? T_REC : T_THEME);
}
static void draw_cpu(int32_t y, const char *name)   /* (name: "CPU "; LOAD's "BLOCK ") */
{
    char b[40];
    uint32_t c = cpu_pct(), pk = cpu_peak_pct();
    int32_t w = (int32_t)(c > 100u ? 100u : c), wp = (int32_t)(pk > 100u ? 100u : pk);
    put_str(b, name, sizeof b);
    put_int(b + str_len(b), (int32_t)c);
    put_str(b + str_len(b), "%  PEAK ", sizeof b - str_len(b));
    put_int(b + str_len(b), (int32_t)pk);
    put_str(b + str_len(b), "%", sizeof b - str_len(b));
    if (audio_late) {
        put_str(b + str_len(b), "  LATE ", sizeof b - str_len(b));
        put_int(b + str_len(b), (int32_t)audio_late);
    }
    cv_text(10, y, &AF_S, b, c >= 85u || audio_late ? T_REC : T_MID);
    draw_batt(y);
    cv_rect(10, y + 14, 220, 4, T_SURF);
    cv_rect(10, y + 14, w * 220 / 100, 4, c >= 85u ? T_REC : T_THEME);
    if (wp)
        cv_rect(10 + wp * 218 / 100, y + 13, 2, 6, T_ACCENT);
}

/* 0.6: the links. An amount as the effect reads it (b: 12 at least) */
static const char *const LINK_EFF[NLE] = {"OFF", "DUCK", "ONLY", "TRIG", "RATCH", "VEL", "PITCH", "SHIFT", "DECAY"};
static void link_amount(char *b, uint32_t e, int32_t a)
{
    if (e == LE_RAT && a >= 5) {
        put_str(b, "BOUNCE", 8);
        return;
    }
    if (e == LE_DUCK)
        *b++ = '-';
    else if (a > 0 && (e == LE_VEL || e == LE_PITCH || e == LE_DECAY || e == LE_RAT))
        *b++ = '+';
    b = put_int(b, a);
    if (e == LE_DUCK || e == LE_ONLY || e == LE_SHIFT)
        put_str(b, "%", 2);
}
static void cat(char *b, const char *s, uint32_t n) { put_str(b + str_len(b), s, n - (uint32_t)str_len(b)); }
/* LINKS, LINK SETUP: the 8 links (set: raised; the one shown: lit), the one shown as a sentence */
static void draw_links(int32_t y0)
{
    static const char *const EV[NLV] = {" hits", " hits hard", " rests"};
    static const char *const WIN[NLW] = {"on the same step", "on the next step", "on the next 2 steps",
                                         "for the rest of the bar"};
    static const char *const TWIN[NLW] = {"at once", "on the next step", "2 steps on", "at the next bar"};
    static const char *const START[NSTART] = {"DUCK HATS", "CALL / RESPONSE", "GHOST FILL", "CHAIN"};
    const pattern_t *pt = shown_pat();
    const uint8_t *L = pt->link[ui.link & 7u];
    uint32_t i, e = L[1] & 15u, src = (L[0] & 7u) % NTRACK, tgt = (L[0] >> 3 & 7u) % NTRACK, ev = (L[0] >> 6) % NLV;
    uint32_t w = L[1] >> 4 & 3u, st = link_start(L);
    int32_t a = (int8_t)L[2];
    char b[48], v[16];
    (void)y0;
    for (i = 0; i < NLINK; i++) {
        int32_t x = 4 + (int32_t)i * 29;
        int set = (pt->link[i][1] & 15u) && (pt->link[i][1] & 15u) < NLE, sel = i == (ui.link & 7u);
        uint16_t bg = sel ? T_THEME : set ? T_RAISE : T_SURF;
        put_int(b, (int32_t)i + 1);
        cv_rrect(x, 2, 27, 17, 3, bg, T_BG);
        text_c(x, 2 + CAP_IN(S, 17), 27, &AF_S, b, sel ? RGB(12, 12, 12) : set ? T_TEXT : T_DIM, bg);
    }
    if (!e || e >= NLE) {
        put_str(b, "Link ", sizeof b);
        put_int(b + 5, (int32_t)(ui.link & 7u) + 1);
        cat(b, " is empty.", sizeof b);
        cv_text(8, 30, &AF_M, b, T_TEXT);
        help_item(help_item(56, "START", "KNOB 2 picks a starting point, or set one up on LINK SETUP"),
                  "SHIFT + SEQ", "opens this page");
        return;
    }
    if (e > LE_DUCK && e != LE_TRIG)
        a = clamp32(a, LK_LO[e], LK_HI[e]);
    put_str(b, "When ", sizeof b);
    cat(b, VOICE_SHORT[src], sizeof b);
    cat(b, EV[ev], sizeof b);
    cv_text(8, 26, &AF_M, b, T_TEXT);
    put_str(b, VOICE_SHORT[tgt], sizeof b);
    link_amount(v, e, a);
    switch (e) {
    case LE_DUCK: cat(b, " chance ", sizeof b); cat(b, v, sizeof b); break;
    case LE_ONLY:
        cat(b, a >= 100 ? " plays only then" : " only then, ", sizeof b);
        if (a < 100)
            cat(b, v, sizeof b);
        break;
    case LE_TRIG: cat(b, " plays at ", sizeof b); cat(b, v, sizeof b); break;
    case LE_RAT: cat(b, a >= 5 ? " bounces" : " ratchets ", sizeof b); if (a < 5) cat(b, v, sizeof b); break;
    case LE_VEL: cat(b, " velocity ", sizeof b); cat(b, v, sizeof b); break;
    case LE_PITCH: cat(b, " pitch ", sizeof b); cat(b, v, sizeof b); break;
    case LE_SHIFT: cat(b, " plays ", sizeof b); cat(b, v, sizeof b); cat(b, " late", sizeof b); break;
    default: cat(b, " decay ", sizeof b); cat(b, v, sizeof b); break;
    }
    cv_text(8, 48, &AF_M, b, TRACK_COL[tgt]);
    cv_text(8, 70, &AF_M, e == LE_TRIG ? TWIN[w] : WIN[w], T_TEXT);
    put_str(b, st && st <= NSTART ? START[st - 1u] : "", sizeof b);
    cv_text(8, 96, &AF_S, b, T_MID);
    put_str(b, "DEPTH ", sizeof b);
    put_str(put_int(b + 6, (int32_t)((pt->cdepth * 100u + 63u) / 127u)), "%", 2);
    cv_text(232 - text_w(&AF_S, b), 96, &AF_S, b, pt->cdepth ? T_MID : T_ACCENT);
}

/* 17a-17c: CONFIG > LOAD. Each part's share of a half buffer (2.9 ms; audio.c): the average, the peak since CONFIG
 * opened, a bar with the peak's tick (with two cores D1-D3 is the second core's). Below, the cores: CORES 2 with core
 * 0's wait for the second and the second's stack, or CORES 1 and why */
#define USTACK_B 12032u                              /* app.ld: _ustack_lo .. _guard1 */
#define SSTACK_B 7936u                               /* app.ld: _sstack_lo .. _sstack_top */
static void load_line(int32_t y, const char *name, const char *v, uint16_t c)
{
    cv_text(10, y, &AF_S, name, T_MID);
    cv_text_r(232, y, &AF_S, v, c, T_BG);
}
static void stack_line(char *b, uint32_t used, uint32_t size)   /* "1234 OF 24320 B" (b: 40) */
{
    put_int(b, (int32_t)used);
    cat(b, " OF ", 40);
    put_int(b + str_len(b), (int32_t)size);
    cat(b, " B", 40);
}
static void draw_load(void)
{
    static const uint8_t ORDER[NLD - 1u] = {LD_D13, LD_SNHH, LD_MIX, LD_FX, LD_REV, LD_SEQ};
    static const char *const PART[NLD] = {"SEQ, MOD", "D1-D3", "SN-HH", "MIXER", "FX BUS", "REVERB", "WAIT"};
#if FELUCCA_CPU2
    /* (1.0: core 1 runs the reverb too while core 0 is the heavier: voices.c rev_c1) */
    const char *d13 = cores_why == CW_TWO && rev_c1 ? "D1-D3, REV" : "D1-D3";
#else
    const char *d13 = "D1-D3";
#endif
    static const char *const WHY[] = {"", "ONE-CORE BUILD", "REC HELD AT POWER-ON", "BAD READ", "NO ANSWER",
                                      "FAULT", "SINCE A CRASH", "NO START"};
    char b[40];
    uint32_t k;
    for (k = 0; k < NLD - 1u; k++) {
        uint32_t i = ORDER[k], a = (ld_q8[i] * 100u + 128u) >> 8, pk = (ld_pk[i] * 100u + 128u) >> 8;
        int32_t y = 2 + (int32_t)k * 12, w = (int32_t)(a > 100u ? 100u : a), wp = (int32_t)(pk > 100u ? 100u : pk);
        cv_text(10, y, &AF_S, i == LD_D13 ? d13 : PART[i], T_TEXT);
        put_str(put_int(b, (int32_t)a), "%", 2);
        cv_text_r(104, y, &AF_S, b, T_TEXT, T_BG);
        put_str(put_int(b, (int32_t)pk), "%", 2);
        cv_text_r(144, y, &AF_S, b, T_MID, T_BG);
        cv_rect(152, y + 4, 80, 4, T_SURF);
        cv_rect(152, y + 4, w * 80 / 100, 4, T_THEME);
        if (wp)
            cv_rect(152 + wp * 78 / 100, y + 3, 2, 6, T_ACCENT);
    }
    put_str(b, "CORE 0 WORK ", sizeof b);           /* (37: from the ELIXIR page) core 0: the ISR less its wait */
    {
        uint32_t c = cpu_pct(), w = (ld_q8[LD_WAIT] * 100u + 128u) >> 8;
        put_int(b + str_len(b), (int32_t)(cores_why == CW_TWO && c > w ? c - w : c));
    }
    cat(b, "%", sizeof b);
    cv_text(10, 82, &AF_S, b, T_TEXT);
    put_str(b, "CORE 1  ", sizeof b);
    if (cores_why == CW_TWO) {
        put_int(b + str_len(b), (int32_t)((ld_q8[LD_D13] * 100u + 128u) >> 8));
        cat(b, "%", sizeof b);
    } else {
        cat(b, "OFF", sizeof b);
    }
    cv_text(122, 82, &AF_S, b, T_TEXT);
    /* one reading a line, the name left and the value right (0.25.3: they overlapped on one line) */
    cv_rect(10, 99, 222, 1, T_LINE);
    if (cores_why == CW_TWO) {
        put_int(b, (int32_t)((ld_q8[LD_WAIT] * 100u + 128u) >> 8));
        cat(b, "%", sizeof b);
        load_line(104, "CORE 0 WAITS", b, T_TEXT);
#if !(defined(LOAD_CLOCK) && ELIXIR_LDDIAG)
        stack_line(b, c1_stack(), 4096u);
        load_line(118, "CORE 1 STACK", b, c1_stack() > 3584u ? T_REC : T_TEXT);
#endif
    } else {
        load_line(104, "1 CORE", WHY[cores_why < sizeof WHY / sizeof WHY[0] ? cores_why : 0],
                  cores_why >= CW_BAD ? T_REC : T_TEXT);
    }
#if defined(LOAD_CLOCK) && ELIXIR_LDDIAG
    {   /* (the diagnostic: SEQ, MOD's parts, average and peak) */
        static const char *const DGN[NDG] = {"PARAMS", "MIDI, KEYS, PERF", "SEQUENCER", "VOICE MOD"};
        uint32_t k;
        for (k = 0; k < NDG; k++) {
            put_int(b, (int32_t)((dg_q8[k] * 100u + 128u) >> 8));
            cat(b, "%  ", sizeof b);
            put_int(b + str_len(b), (int32_t)((dg_pk[k] * 100u + 128u) >> 8));
            cat(b, "%", sizeof b);
            load_line(118 + 14 * (int32_t)k, DGN[k], b, T_TEXT);
        }
    }
    if (0)
#endif
    {   /* 18a: the main stacks' deepest use since power-on, of their sizes (app.ld); red past three quarters */
        uint32_t u = ustack_used(), sv = sstack_used();
        stack_line(b, u, USTACK_B);
        load_line(132, "USER STACK", b, u > USTACK_B / 4u * 3u ? T_REC : T_TEXT);
        stack_line(b, sv, SSTACK_B);
        load_line(146, "SYS STACK", b, sv > SSTACK_B / 4u * 3u ? T_REC : T_TEXT);
    }
#if UAC_STEMS && !(defined(LOAD_CLOCK) && ELIXIR_LDDIAG)
    {   /* 1.0, Phase 8: the USB audio's service (TIMER5, outside the parts above) and its glitches since power-on */
        uint32_t g, pm = uac_load_pm(&g);
        if (stems_live()) {
            put_int(b, (int32_t)(pm / 10u));
            cat(b, ".", sizeof b);
            put_int(b + str_len(b), (int32_t)(pm % 10u));
            cat(b, "%  ", sizeof b);
        } else {
            put_str(b, "OFF  ", sizeof b);
        }
        put_int(b + str_len(b), (int32_t)g);
        cat(b, g == 1u ? " GLITCH" : " GLITCHES", sizeof b);
        load_line(160, "USB AUDIO", b, g ? T_REC : T_TEXT);
    }
#endif
}

static void draw_about(void);
static void draw_config_help(int32_t y0)
{
    int32_t y = 6;
    (void)y0;
    if (ui.page % NCONFIG == CF_LOAD) {
        draw_load();
        draw_cpu(178, "BLOCK ");                     /* (the cards' place: LOAD has no knobs. 1.0: BLOCK, the
                                                      * whole half's time, CORE 0 + its WAIT: what LATE counts) */
        return;
    }
    if (ui.page % NCONFIG == CF_ABOUT) {
        draw_about();
        return;
    }
    switch (ui.page % NCONFIG) {
    case 0:
        y = help_item(y, "TEMPO", "at its lowest, Elixir follows MIDI clock");
        y = help_item(y, "FOLLOW", "the bar shown follows the playhead");
        help_item(y, "PLAY", "PATTERN loops the pattern; SONG plays the song's rows");
        break;
    case 1:
        y = help_item(y, "GLOBAL", "program change picks the pattern, CC 102 / 103 the kit (Global in: more)");
        y = help_item(y, "TX N", "the notes of the tracks MIDI OUT has on");
        y = help_item(y, "FILTERS", "N notes, R clock and transport, C CC and NRPN, P program change");
        help_item(y, "ROUTE", "TRS>USB passes the jack's MIDI on to USB");
        break;
    case 2:
    case 3:
        y = help_item(y, "CHANNEL", "each track's MIDI channel, in and out");
        if (ui.page % NCONFIG == 3u)
            help_item(y, "GLOBAL IN", "KEYS: patterns, kits (CC 102, 103), notes, wheel; PAT+KIT: no notes; ALL: the LXR's");
        break;
    case 4:
    case 5:
        y = help_item(y, "ANY", "the track plays any note, pitched");
        help_item(y, "A NOTE", "only that note triggers the track");
        break;
    case CF_MOUT:                                    /* 1.0: a track's notes out; the master volume */
    case CF_MOUT + 1:
        y = help_item(y, "OUT", "the track's notes go out on its channel (TX N on too)");
        if (ui.page % NCONFIG == CF_MOUT + 1u)
            help_item(y, "VOLUME", "the master volume, after MASTER; CC 124 on the remote's channels");
        break;
    case CF_UMIX:                                    /* 1.0, Phase 8: the voices in the USB audio's stereo mix */
    case CF_UMIX + 1:
        y = help_item(y, "OUT", "leaves the stereo mix (USB 1-2, the outputs) while a computer records");
        y = help_item(y, "STEMS", "USB 3-8: D1 D2 D3 SN CY HH, always");
        help_text(HELP_X, y, stems_live() ? "RECORDING" : "NOT RECORDING: ALL IN THE MIX", stems_live() ? T_REC : T_MID);
        break;
    case CF_REMOTE: {                                /* 0.12: the remote; a channel it shares with a track warned of */
        uint32_t t, clash = 0;
        for (t = 0; t <= NTRACK; t++) {
            uint32_t c = t < NTRACK ? midi_track_ch(t) : midi_global_ch();
            clash |= (remote_ch < 16u && c == remote_ch) || (remote_gch < 16u && c == remote_gch);
        }
        y = help_item(y, "TRACKS", "the channel for the tracks' knobs, mutes and solo");
        y = help_item(y, "GLOBAL", "the channel for PERF, FX and density");
        y = help_item(y, "KNOBS", "PICKUP: a pot acts once it passes the value. REL: encoders");
        if (clash)                                   /* (in Mute/solo's line: no room for both) */
            help_text(HELP_X, y, "SHARES A TRACK'S OR THE GLOBAL CHANNEL", T_REC);
        else
            help_item(y, "MUTE/SOLO", "TOGGLE: for toggle pads");
        break;
    }
    case CF_REMOTE + 1:                              /* 0.12: Control All with morph; 31: and the edits */
        y = help_item(y, "EDITS, CTRL ALL", "where an edit goes while morph is on");
        y = help_item(y, "BOTH KITS", "the morph target moves too: heard at any morph amount");
        help_item(y, "KIT ONLY", "the kit alone: at full morph nothing is heard");
        break;
    case CF_REMOTE + 2:                              /* 0.13: page memory; 0.19: the kit list's order */
        y = help_item(y, "LAST PAGE", "a button opens the page it showed when you left it; pressed again, it moves on");
        help_item(y, "KITS FIRST", "where PRESETS' kit list starts: FACTORY or your SLOTS");
        break;
    default:
        y = help_item(y, "LEDS", "off, low or high glow");
        y = help_item(y, "AUTOSAVE", "keeps the session when stopped");
        y = help_item(y, "ACCEL", "fast turns move further");
        help_item(y, "PALETTE", "the screen's colours");
        break;
    }
}

/* the sound page's body: 30: the section's picture (ui_viz.c); below it, in its own region (SOUND_STRIP), the track's
 * steps in the shown bar, so the playhead redraws only them */
#define SOUND_VIZ_H 108
static void draw_sound(int32_t y0)
{
    (void)y0;
    draw_sound_viz();
}
static void draw_sound_strip(void)
{
    uint32_t k, s = sig_add(ui.track, ui.bar | ui.copy << 2 | ui.ca << 3 | ui.mode << 4 | (uint32_t)ca_rm << 8 |
                                          (uint32_t)kset.caout << 10);
    const pattern_t *pt = shown_pat();
    for (k = 0; k < 4u; k++)
        s = sig_add(s, pt->main[ui.track][k]);
    s = sig_add(s, sq.running ? (uint32_t)(uint8_t)sq.idx[ui.track] : 0xFFFFu);
    if (!region(R_STRIP, s))
        return;
    cv_begin(240, BODY_H - SOUND_VIZ_H, T_BG);
    if (ui.copy || ca_rm) {                          /* 0.11: Control All: what OCT- and letting go do (Phase 17: the
                                                      * tracks in it, the left-out ones dim; by MIDI: no dialog) */
        if (ui.copy && !ui.ca) {
            cv_text(6, 4, &AF_S, "KNOB: ALL TRACKS   TRACK KEY: COPY", T_ACCENT);
        } else {
            for (k = 0; k < NTRACK; k++)
                cv_text(6 + (int32_t)k * 18, 4, &AF_S, VOICE_SHORT[k], (kset.caout >> k) & 1u ? T_DIM : TRACK_COL[k]);
            cv_text(136, 4, &AF_S, ca_rm ? "OCT-: REVERT" : "LET GO: ASKS", T_ACCENT);
        }
    } else {
        for (k = 0; k < 16u; k++) {                  /* the track's steps in the shown bar */
            uint32_t st = ui.bar * 16u + k;
            int32_t x = 6 + (int32_t)k * 14;
            int on = main_on(pt, ui.track, st), play = sq.running && sq.idx[ui.track] == (int8_t)st;
            uint16_t c = on ? TRACK_COL[ui.track] : (k & 3u) == 0u ? T_RAISE : T_SURF;
            cv_rrect(x, 2, 12, 12, 2, c, T_BG);
            if (play)
                cv_rect(x, 16, 12, 2, T_ACCENT);
        }
    }
    cv_blit(0, BODY_Y + SOUND_VIZ_H);
}
static uint32_t sound_sig(void)
{
    uint32_t k, h, s = sig_add(ui.track, ui.sect | ui.half << 4 | ui.bar << 6 |
                                         (uint32_t)mdl[0].type << 10 | (uint32_t)mdl[1].type << 16 |
                                         (uint32_t)mdl[2].type << 22);
    uint32_t v = track_voice(ui.track);
    s = sig_add(s, (uint32_t)mi[v].type);            /* (24: MICRO's knobs) */
    for (h = 0; h < 2u; h++)
        for (k = 0; k < 4u; k++) {
            uint32_t p = sect_par(ui.track, ui.sect, h, k);
            if (p)
                s = sig_add(s, vp_get(p));
        }
    if (ui.sect == S_LFO || ui.sect == S_LFO2) {     /* (30: the LFO's other page, its hits) */
        const pattern_t *pt = shown_pat();
        for (k = 0; k < NTRACK; k++)
            s = sig_add(s, pt->main[k][ui.bar]);
        s = sig_add(s, par[PAR_VOICE_LFO1 + track_voice(ui.track)]);
        s = sig_add(s, seq_bpm());
    }
    if (ui.sect == S_SHAPE || ui.sect == S_AEG || ui.sect == S_GATE || ui.sect == S_MOD || ui.sect == S_FM)
        for (h = 0; h < 2u; h++)                     /* (30: the pages drawn from another section's knobs) */
            for (k = 0; k < 4u; k++)
                s = sig_add(s, sv(S_SHAPE, h, k) | sv(S_AEG, h, k) << 8 | sv(S_MOD, h, k) << 16 | sv(S_OSC, h, k) << 24);
    if (ui.sect == S_SENDS)                          /* (1.0 Phase 16: the picture's caption) */
        s = sig_add(s, par[PAR_DLY_ROUTE]);
    if (ui.sect == S_XMOD)
        for (k = 0; k < NVOICE; k++)
            s = sig_add(s, par[PAR_CHOKE1 + k]);
    s = sig_add(s, par[PAR_GATE1 + v] | (uint32_t)mdl[v < 3u ? v : 0u].want << 8 | model_cap << 9);
    s = sig_add(s, sv(S_MODEL, 0, 0) | (v < 3u ? vp_get(PAR_MEXC1 + v) : 0u) << 8);   /* (30: a model's pages) */
    return s;
}

/* 1.0: the pattern page's DELETE (CLEAR, then a pattern) */
static void draw_pdel_ask(int32_t y0)
{
    char t[24];
    (void)y0;
    put_str(t, "DELETE PATTERN P", sizeof t);
    t[16] = (char)('0' + (ui.pdel_n + 1u) / 10u);
    t[17] = (char)('0' + (ui.pdel_n + 1u) % 10u);
    put_str(t + 18, "?", 2);
    cv_rrect(10, 20, 220, 88, 6, T_SURF, T_BG);
    cv_text_in(10, 30, 220, &AF_S, ui.pdel_n == sq.active ? "THE PLAYING PATTERN" : "PATTERNS", T_MID, T_SURF);
    cv_text_in(10, 50, 220, &AF_M, t, T_ACCENT, T_SURF);
    cv_text_in(10, 76, 220, &AF_S, "COPY (OCT+): delete", T_TEXT, T_SURF);
    cv_text_in(10, 90, 220, &AF_S, "SHIFT (OCT-): keep", T_MID, T_SURF);
}

static void draw_body(void)
{
    uint32_t s;
    void (*fn)(int32_t) = 0;
    body_full = 0;
    if (ui.dx_ask) {                                 /* 0.24.1: another app's data */
        fn = draw_dx_ask;
        s = sig_add(97, ui.dx_ask | (uint32_t)fx_found << 4 | (uint32_t)fx_apps << 8);
    } else if (ui.pdel == 2u) {                      /* 1.0: DELETE PATTERN? */
        fn = draw_pdel_ask;
        s = sig_add(211, ui.pdel_n | (uint32_t)(ui.pdel_n == sq.active) << 8);
    } else if (ui.ca_ask) {                          /* 0.11: Control All's dialog */
        fn = draw_ca_ask;
        s = 41u + ui.ca_len;
    } else if (perf_spells() && !ui.clear_open && !ui.kb_open && !(ui.shift && ui.shift_used && !ui.fine)) {   /* (32) */
        fn = draw_spells;
        s = spells_sig();
    } else if (perf_wander() && !ui.clear_open && !ui.kb_open && !(ui.shift && ui.shift_used && !ui.fine)) {   /* (40) */
        fn = draw_wander;
        s = wander_sig();
    } else if (perf_rush() && !ui.clear_open && !ui.kb_open) {   /* (39: RUSH; 1.0: SHIFT keeps it) */
        uint32_t t = ui.track < NTRACK ? ui.track : 0u, k;
        const pattern_t *vp = view_pat(t);
        fn = draw_rush;
        s = sig_add(39, sq.ru_live | (uint32_t)sq.ru_R << 8 | t << 16 | (uint32_t)(uint8_t)ui.held_step << 20);
        s = sig_add(s, sq.ru_start | (uint32_t)sq.ru_free_at << 8 | (uint32_t)sq.ru_top << 16 | (uint32_t)sq.ru_tr << 20 |
                           (uint32_t)sq.rec << 24 | ui.bar << 28);
        s = sig_add(s, sq.ru_filt | (uint32_t)(uint8_t)sq.idx[t] << 8);
        for (k = 0; k < 16u; k++)
            s = sig_add(s, rush_lock(vp, t, ui.bar * 16u + k) | (uint32_t)main_on(vp, t, ui.bar * 16u + k) << 8);
    } else if (perf_trp() && !ui.clear_open && !ui.kb_open) {   /* (0.23-t7: TRANSPOSE) */
        uint32_t k;
        fn = draw_trp;
        s = sig_add(43, sq.trmask | (uint32_t)sq.trdue << 8 | (uint32_t)sq.trwhen << 9 | ui.track << 12 |
                            (uint32_t)pat[sq.active].scale << 16);
        for (k = 0; k < NTRACK; k++)
            s = sig_add(s, (uint32_t)(uint8_t)sq.trp[k] | (uint32_t)(uint8_t)sq.trpend[k] << 8);
    } else if (perf_links() && !ui.clear_open && !ui.kb_open) {   /* (29: PERF's GLOBAL MOD) */
        uint32_t l;
        const pattern_t *pt = shown_pat();
        fn = draw_links;
        s = sig_add(12, ui.link | (uint32_t)pt->cdepth << 8);
        for (l = 0; l < NLINK; l++)
            s = sig_add(s, (uint32_t)pt->link[l][0] | (uint32_t)pt->link[l][1] << 8 | (uint32_t)pt->link[l][2] << 16);
    } else if (ui.mode == M_PERF && !ui.clear_open && !ui.kb_open) {   /* (SHIFT: latches; 1.0: PERF's pages only) */
        fn = draw_perf;
        s = sig_add(sig_add(11, pf.on), ui.pf_latch | (uint32_t)perf_bank2() << 31);
        s = sig_add(s, sq.drill | ui.drill_latch << 8);
        s = sig_add(s, sq.roll | ui.roll_latch << 8 | seq_mutes() << 16 | ui.shift << 24);
        s = sig_add(s, pfs.repeat | ui.track << 4 | (uint32_t)(pf.dj_d + 65536) >> 10 << 8 | (tape_q16 >> 11) << 20);
        if (glfo_page() >= 0)                        /* (0.13: the LFOs' bars, which page) */
            s = sig_add(sig_add(sig_add(s, glfo_sig(0) | glfo_sig(1) << 8 | glfo_sig(2) << 16 | glfo_sig(3) << 24),
                                (uint32_t)gls[0].dest | (uint32_t)gls[1].dest << 8 | (uint32_t)gls[2].dest << 16 |
                                    (uint32_t)gls[3].dest << 24),
                        ui.page | (uint32_t)dest_pop() << 8);   /* (0.25.2: DEST's name over it) */
        if (perf_dens())                             /* (19d: the tracks left out, DENSITY in use; 31: COMMIT) */
            s = sig_add(s, pfs.dex | (uint32_t)(uint8_t)sq.dens << 8 | 1u << 16 | (uint32_t)(pfs.dens != 0) << 17 |
                               (uint32_t)dundo << 18);
        if (perf_stut())                             /* (0.16: STUTTER: a burst, its repeat, held) */
            s = sig_add(s, pf.st_on | pf.st_rep << 2 | (uint32_t)pf.st_hold << 12 | 2u << 16);
    } else if (shift_menu() && !ui.fine && !ui.clear_open) {   /* (1.0: a knob turned: the page, fine adjust) */
        fn = draw_shift_legend;
        s = 1;
    } else if (ui.clear_open) {
        fn = draw_clear;
        s = sig_add(2, ui.clear_sel);
    } else if (ui.sb_open) {
        fn = draw_sound_browser;
        s = sig_add(40, ui.sb_sel | ui.track << 16);
    } else if (ui.kb_open) {
        fn = draw_kit_browser;
        s = sig_add(3, ui.kb_sel | kit_loaded << 9 | kset.kbslots << 15);
    } else {
        switch (ui.mode) {
        case M_PATTERN:
            fn = draw_patterns;
            s = sig_add(4, sq.active | sq.pending << 8 | ui.bank << 16);
            s = sig_add(s, pat[sq.active].kit | (uint32_t)kit_names[pat_kit(&pat[sq.active])][0] << 8);
            break;
        case M_SONG:
            fn = draw_song;
            s = sig_add(5, ui.song_row | sq.song << 8 | (sq.song_mode && sq.running ? sq.song_pos : 0xFFu) << 16 |
                                 (uint32_t)sq.song_n << 24);
            {
                uint32_t i;
                for (i = 0; i < MAX_SONG_LENGTH; i++)
                    s = sig_add(s, songs[sq.song][i]);
            }
            break;
        case M_SAVE:
        case M_LOAD:
            fn = draw_io;
            s = sig_add(6, ui.io_type | ui.io_slot << 8 | ui.io_naming << 16 | ui.io_pos << 20 | ui.mode << 24 |
                               (uint32_t)ui.io_src << 29);
            s = sig_add(s, ui.track | kl.seq << 8);
            s = sig_add(s, (uint32_t)ui.io_name[0] | (uint32_t)ui.io_name[1] << 8 | (uint32_t)ui.io_name[2] << 16 |
                               (uint32_t)ui.io_name[3] << 24);
            s = sig_add(s, (uint32_t)ui.io_name[4] | (uint32_t)ui.io_name[5] << 8 | (uint32_t)ui.io_name[6] << 16 |
                               (uint32_t)ui.io_name[7] << 24);
            break;
        case M_MIXER: {
            uint32_t v;
            fn = draw_mixer;
            s = sig_add(mixer_sig(), ui.page);
            (void)v;
            break;
        }
        case M_FX: {
            uint32_t v;
            fn = draw_fx;
            s = sig_add(8, fx.type | fx.dist_type << 4 | fx.delay_type << 8 | fx.ratio << 12 | par[PAR_FX_DRY_WET] << 16);
            s = sig_add(s, fx_page() | rv.type << 4 | (uint32_t)par[PAR_REV_LEVEL] << 8 | fx.d_sync << 16);
            s = sig_add(s, (uint32_t)par[PAR_FX_P1] | (uint32_t)par[PAR_FX_P2] << 8 | (uint32_t)par[PAR_FX_P3] << 16 |
                               (uint32_t)par[PAR_FX_P4] << 24);   /* (30: the pictures' settings) */
            s = sig_add(s, (uint32_t)par[PAR_REV_SIZE] | (uint32_t)par[PAR_REV_DAMP] << 8 | (uint32_t)par[PAR_REV_TYPE] << 16 |
                               (uint32_t)par[PAR_ECHOREV] << 24);
            s = sig_add(s, (uint32_t)par[PAR_GLITCH] | (uint32_t)par[PAR_DPITCH] << 8 | (uint32_t)par[PAR_FXDIRT] << 16 |
                               (uint32_t)par[PAR_MDIRT_AMT] << 24);
            s = sig_add(s, (uint32_t)par[PAR_MDIRT] | (uint32_t)par[PAR_MC_THR] << 8 | (uint32_t)par[PAR_MC_GAIN] << 16 |
                               (uint32_t)par[PAR_FX_TYPE] << 24);
            s = sig_add(s, (uint32_t)par[PAR_DLY_TIME] | (uint32_t)par[PAR_DLY_FB] << 8 | (uint32_t)par[PAR_DLY_TONE] << 16 |
                               (uint32_t)par[PAR_DLY_LEVEL] << 24);   /* (1.0: the delay's) */
            s = sig_add(s, (uint32_t)par[PAR_DLY_WOW] | (uint32_t)par[PAR_DSAT] << 8 | fx_bpm_x10 << 16 |
                               (uint32_t)par[PAR_DLY_BAND] << 24);
            if (fx_page() >= FXPG_FSEND)                 /* (the send pages: the mixer's bars) */
                s = sig_add(s, mixer_sig());
            if (fx_page() == FXPG_DJ || fx_page() == FXPG_DJ2)   /* (1.0: the DJ filter as it moves, its type and Reso) */
                s = sig_add(sig_add(s, (uint32_t)(pf.dj_d + 65536) >> 9 | (uint32_t)pfs.dj_ftype << 20 |
                                           (uint32_t)pfs.dj_reso << 24),
                            pfs.dj_tune | (uint32_t)pfs.dj_spread << 8 | (uint32_t)pfs.dj_slope << 16 |
                                (uint32_t)pfs.dj_tone << 24 | (uint32_t)pfs.dj_wet << 3);   /* (RING's chord, its
                                                      * page 2: Phase 18) */
            if (fx_page() == FXPG_COMP2 && mc.src && mc.src <= NVOICE)
                s = sig_add(s, shown_pat()->main[mc.src - 1u][ui.bar & 3u] | ui.bar << 16);
            if (fx_page() == FXPG_COMP || fx_page() == FXPG_COMP2)   /* (0.16: the compressor's gain reduction) */
                s = sig_add(s, (uint32_t)(mc.gr >> 12) | mc.ratio << 16 | mc.src << 20);
            for (v = 0; v < NVOICE; v++)
                s = sig_add(s, par[PAR_FX_SEND1 + v] | (uint32_t)par[PAR_REV_SEND1 + v] << 8 |
                                   (uint32_t)par[PAR_DSEND1 + v] << 16);
            s = sig_add(s, (uint32_t)par[PAR_DLY_ROUTE] | (uint32_t)par[PAR_REV_FX] << 8);   /* (1.0 Phase 16) */
            break;
        }
        case M_KEYS:
            fn = draw_keys;
            s = sig_add(14, ui.keys | (uint32_t)ui.knote << 27);
            s = sig_add(s, ui.track | ui.koct << 4 | (sq.rec && sq.running) << 8);
            break;
        case M_CONFIG:
            fn = draw_config_help;
            s = sig_add(9, ui.page | stems_live() << 7 | (uint32_t)remote_ch << 8 | (uint32_t)remote_gch << 16);
            s = sig_add(s, cpu_pct() | cpu_peak_pct() << 8 | audio_late << 16 | (uint32_t)batt_level() << 28);
            if (ui.page % NCONFIG == CF_LOAD || ui.page % NCONFIG == CF_ABOUT) {
                uint32_t i;
                for (i = 0; i < NLD; i++)
                    s = sig_add(s, ld_q8[i] | (uint32_t)ld_pk[i] << 16);
                s = sig_add(s, cores_why | c1_stack() << 8);
                s = sig_add(s, ustack_used() | sstack_used() << 16);
#if UAC_STEMS
                {
                    uint32_t g, pm = uac_load_pm(&g);
                    s = sig_add(s, pm | g << 10 | stems_live() << 31);
                }
#endif
            }
            break;
        case M_SOUND:
            fn = draw_sound;
            s = sig_add(10, sound_sig());
            break;
        case M_EUCLID:
            if (gen_pop()) {                         /* (1.0: the generator's name, what it does) */
                fn = draw_gen_pop;
                s = sig_add(17, vp_get(VP_GMODE));
                break;
            }
            if (track_viz_page()) {                  /* (30: GROOVE, BOUNCE, MELODY, SCALE: their pictures) */
                fn = draw_track_viz;
                s = sig_add(15, track_viz_sig());
                break;
            }
            draw_grid();
            return;
        default:
            draw_grid();
            return;
        }
    }
    if (body_is_grid) {                              /* (the grid was there: this body all over) */
        body_is_grid = 0;
        rsig[R_BODY] = 0;
    }
    if (fn == draw_sound) {                          /* (30: the picture, then the steps' strip under it) */
        if (region(R_BODY, s)) {
            body_strips_h(fn, SOUND_VIZ_H);
            rsig[R_STRIP] = 0;
        }
        draw_sound_strip();
        return;
    }
    body_full = fn == draw_config_help && ui.page % NCONFIG == CF_LOAD;
    if (!region(R_BODY, s))
        return;
    if (body_full)                                   /* (CONFIG > LOAD: no knobs, so the cards' place too) */
        body_strips_h(fn, 240 - BODY_Y);
    else
        body_strips(fn);
}

/* ------------------------------------------------------------- cards --- */
static void card_name(char *b, uint32_t n, uint32_t p)
{
    if (ui.mode == M_SOUND && ui.sect == S_SENDS) {  /* 1.0 Phase 16: FX TRACK SENDS: short, to fit */
        const char *t = p == PAR_FX_DRY_WET ? "FX D/W" : p == PAR_DLY_LEVEL ? "Dly lvl" : p == PAR_REV_LEVEL ? "Rev lvl"
                      : p >= PAR_DSEND1 && p < PAR_DSEND1 + 6u ? "Delay" : p >= PAR_REV_SEND1 && p < PAR_REV_SEND1 + 6u
                      ? "Reverb" : p >= PAR_FX_SEND1 && p < PAR_FX_SEND1 + 6u ? "FX" : 0;
        if (t) {
            put_str(b, t, n);
            return;
        }
    }
    if (p >= PAR_DSEND1 && p < PAR_DSEND1 + 6u) {    /* (1.0 Phase 16: as the other sends, its voice) */
        put_str(b, p - PAR_DSEND1 == 5u ? "HH" : VOICE_SHORT[p - PAR_DSEND1], n);
        put_str(b + 2, " dly", n - 2);
        return;
    }
    if (p == PAR_DLY_LEVEL && ui.mode != M_FX) {
        put_str(b, "Dly level", n);
        return;
    }
    if (par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN && (p == PAR_DLY_TIME || p == PAR_DLY_WOW || p == PAR_GLITCH || p == PAR_DSAT)) {
        put_str(b, p == PAR_DLY_TIME ? "Position" : p == PAR_DLY_WOW ? "Size" : p == PAR_GLITCH ? "Density" : "Scatter", n);
        return;                                      /* (1.0 Phase 13: GRAIN's knobs) */
    }
    if (p >= PAR_FX_P1 && p <= PAR_FX_P4) {
        put_str(b, FX_P_NAME[fx.type < NFX ? fx.type : 0][p - PAR_FX_P1], n);
        return;
    }
    switch (p) {
    case VP_NONE: b[0] = 0; return;
    case VP_IO_TYPE: put_str(b, "Type", n); return;
    case VP_IO_SLOT: put_str(b, "Slot", n); return;
    case VP_IO_SRC: put_str(b, ui.mode == M_SAVE ? "To" : "From", n); return;
    case VP_IO_TRACK: put_str(b, "Track", n); return;
    case PAR_FILTER_FREQ_1 ... PAR_FILTER_FREQ_6: put_str(b, "Freq", n); return;   /* (1.0: the cards; the LXR's */
    case PAR_RESO_1 ... PAR_RESO_6: put_str(b, "Reso", n); return;               /* "Frequncy", "Resnance", */
    case PAR_FILTER_DRIVE_1 ... PAR_FILTER_DRIVE_6: put_str(b, "Drive", n); return;   /* "Overdriv") */
    case VP_NAME_POS: put_str(b, "Position", n); return;
    case VP_NAME_CHAR: put_str(b, "Letter", n); return;
    case VP_NAME_CASE: put_str(b, "Case", n); return;
    case VP_SONG_PAT: put_str(b, "Pattern", n); return;
    case VP_SONG_NUM: put_str(b, "Song", n); return;
    case VP_SONG_REP: put_str(b, "Repeats", n); return;
    case VP_SONG_ROW: put_str(b, "Row", n); return;
    case VP_SONG_EDIT: put_str(b, "Action", n); return;
    case VP_PLAY: put_str(b, "Play", n); return;
    case VP_ROADS: put_str(b, "Way", n); return;
    case VP_RSUR: put_str(b, "Surprise", n); return;
    case VP_RHOME: put_str(b, "Home", n); return;
    case VP_VOICES: put_str(b, "Voices", n); return;
    case VP_LEDS: put_str(b, "Leds", n); return;
    case VP_AUTOSAVE: put_str(b, "Autosave", n); return;
    case VP_PKEEP: put_str(b, "Edits", n); return;
    case VP_ACCEL: put_str(b, "Accel", n); return;
    case VP_PALETTE: put_str(b, "Colours", n); return;
    case VP_RCH: put_str(b, "Tracks", n); return;
    case VP_RCH2: put_str(b, "Global", n); return;
    case VP_RKNOBS: put_str(b, "Knobs", n); return;
    case VP_RMUTE: put_str(b, "Mute/solo", n); return;
    case VP_CAKIT: put_str(b, "Ctrl all", n); return;
    case VP_MEDIT: put_str(b, "Edits", n); return;
    case VP_PMEM: put_str(b, "Last page", n); return;
    case VP_KBSLOTS: put_str(b, "Kits first", n); return;
    case VP_GTAKES: put_str(b, "Global in", n); return;
    case VP_EH2: put_str(b, "Hits 2", n); return;
    case VP_ER2: put_str(b, "Rotate 2", n); return;
    case VP_EOP: put_str(b, "Join", n); return;
    case VP_DENS: put_str(b, "Density", n); return;
    case VP_SP ... VP_SP_LAST: sp_card_name(b, n, p - VP_SP); return;
    case VP_WD ... VP_WD_LAST: wd_card_name(b, n, p - VP_WD); return;
    case VP_GHOST: put_str(b, "Ghost vel", n); return;
    case VP_BUILD: put_str(b, "Build", n); return;
    case VP_DVARY: put_str(b, "Vary", n); return;
    case VP_STCH: put_str(b, "Chance", n); return;
    case VP_RUSH: put_str(b, "Rush", n); return;
    case VP_RTOP: put_str(b, "Top", n); return;
    case VP_RFILT: put_str(b, "Filter", n); return;
    case VP_RGLIDE: put_str(b, "Glide", n); return;
    case VP_RSTART: put_str(b, "Roll at", n); return;
    case VP_RFREE: put_str(b, "Free at", n); return;
    case VP_RFLAM: put_str(b, "Flam", n); return;
    case VP_RPITCH: put_str(b, "Pitch", n); return;
    case VP_STSIZE: put_str(b, "Size", n); return;
    case VP_STLEN: put_str(b, "Length", n); return;
    case VP_STVARY: put_str(b, "Vary", n); return;
    case VP_SROOT: put_str(b, "Root", n); return;
    case VP_SCALE: put_str(b, "Scale", n); return;
    case VP_SFOLLOW: put_str(b, "Follow", n); return;
    case VP_TBASE: put_str(b, "Base", n); return;
    case VP_TRP: put_str(b, "Degrees", n); return;
    case VP_TRWHEN: put_str(b, "When", n); return;
    case VP_TRHOLD: put_str(b, "Keys", n); return;
    case VP_GROOVE: put_str(b, "Groove", n); return;
    case VP_GAMT: put_str(b, "Amount", n); return;
    case VP_GHUM: put_str(b, "Humanize", n); return;
    case VP_GFOLLOW: put_str(b, "Follow", n); return;
    case VP_LEN: put_str(b, "Length", n); return;
    case VP_MEL ... VP_MEL_LAST: {
        static const char *const M[NML] = {"Shape", "Spread", "Bias", "Steps", "Deja vu", "Loop", "Length", "Len var"};
        put_str(b, M[p - VP_MEL], n);
        return;
    }
    case VP_GL ... VP_GL_LAST: {
        static const char *const N[NGLSET] = {"Dest", "Depth", "Sync", "Wave", "Mode", "Trig", "Phase", "Rate"};
        static const char *const F[NGLSET] = {"Dest", "Depth", "", "Wave", "", "Source", "Thresh", "Release"};
        put_str(b, (gls[(p - VP_GL) / NGLSET].wave == GW_FOLLOW ? F : N)[(p - VP_GL) % NGLSET], n);   /* (1.0 Phase
                                                      * 11: FOLLOW's) */
        return;
    }
    case VP_TRACK_VOL: put_str(b, "Volume", n); return;
    case VP_MASTER: put_str(b, "Master", n); return;
    case VP_DJ: put_str(b, ui.mode == M_FX ? "Filt" : "DJ filter", n); return;   /* (FX: its page names it) */
    case VP_COND: put_str(b, "Condition", n); return;
    case VP_RAT_N: put_str(b, "Hits", n); return;
    case VP_RAT_RATE: put_str(b, shown_pat()->rat[ui.track][ui.step & 63u] & RAT_BOUNCE ? "Curve" : "Rate", n); return;
    case VP_RAT_SHAPE: put_str(b, "Shape", n); return;
    case VP_BSTART: put_str(b, "Bounce", n); return;
    case VP_BPITCH: put_str(b, "Bnc pitch", n); return;
    case VP_ROLL: put_str(b, "Roll", n); return;
    case VP_LINK: put_str(b, "Link", n); return;
    case VP_P2: case VP_P2 + 1: case VP_P2 + 2: case VP_P2 + 3: case VP_P2 + 4: case VP_P2 + 5: case VP_P2 + 6:
    case VP_P2_LAST: {
        static const char *const N[8] = {"Drill", "Jitter", "Cuts", "Pump", "Comb", "Comb fb", "Crush", "Reverse"};
        put_str(b, N[p - VP_P2], n);
        return;
    }
    case VP_LSTART: put_str(b, "Start", n); return;
    case VP_LAMT: put_str(b, "Amount", n); return;
    case VP_DEPTH: put_str(b, "Depth", n); return;
    case VP_LWHEN: put_str(b, "When", n); return;
    case VP_LTGT: put_str(b, "Target", n); return;
    case VP_LEFF: put_str(b, "Effect", n); return;
    case VP_LWIN: put_str(b, "Window", n); return;
    case VP_KOCT: put_str(b, "Octave", n); return;
    case VP_KVEL: put_str(b, "Velocity", n); return;
    case VP_KACC: put_str(b, "Accent", n); return;
    case VP_KB_SRC: put_str(b, "From", n); return;
    case VP_RAT_FADE: put_str(b, "Fade", n); return;
    case VP_ACTION: put_str(b, "Action", n); return;
    case VP_GMODE: put_str(b, "Generator", n); return;
    case VP_CLOCK: put_str(b, "Clock", n); return;
    case VP_DIR: put_str(b, "Direction", n); return;
    case VP_GX: put_str(b, "Map X", n); return;
    case VP_GY: put_str(b, "Map Y", n); return;
    case VP_GCHAOS: put_str(b, "Chaos", n); return;
    case VP_GA: case VP_GB: case VP_GC: {
        static const char *const N[NGEN][3] = {{"-", "-", "-"}, {"Amount", "-", "Freeze"},
                                               {"Length", "Lock", "Density"}, {"Rule", "-", "Freeze"},
                                               {"Part", "Density", "Accent"}, {"-", "-", "-"}};
        uint32_t g = shown_pat()->gen[ui.track][0];
        put_str(b, N[g < NGEN ? g : 0][p - VP_GA], n);
        return;
    }
    case VP_REPEAT: put_str(b, "Repeat", n); return;
    case VP_TAPE: put_str(b, "Tape time", n); return;
    case VP_DJRESO: put_str(b, ui.mode == M_FX && pfs.dj_ftype == DJT_RING ? "Decay" : "Reso", n); return;
    case VP_DJTUNE: put_str(b, "Tune", n); return;
    case VP_DJSPREAD: put_str(b, "Spread", n); return;   /* (1.0 Phase 18: RING (2/2)) */
    case VP_DJSLOPE: put_str(b, "Slope", n); return;
    case VP_DJTONE: put_str(b, "Tone", n); return;
    case VP_DJWET: put_str(b, "Wet", n); return;
    case VP_DJTYPE: put_str(b, "Type", n); return;
    case VP_MVOL: put_str(b, "Volume", n); return;   /* (1.0) */
    case VP_STRETCH: put_str(b, "Stretch", n); return;
    case VP_REALIGN: put_str(b, "Realign", n); return;
    case VP_MOUT ... VP_MOUT_LAST: put_str(b, VOICE_SHORT[p - VP_MOUT], n); cat(b, " out", n); return;
    case VP_UMIX ... VP_UMIX_LAST: put_str(b, p - VP_UMIX == 5u ? "HH" : VOICE_SHORT[p - VP_UMIX], n); cat(b, " mix", n); return;
    case VP_TD ... VP_TD_LAST: put_str(b, VOICE_SHORT[p - VP_TD], n); cat(b, " dens", n); return;
    case VP_MSEL: put_str(b, "Macro", n); return;
    case VP_MTARGET: put_str(b, "Target", n); return;
    case VP_MMIN: put_str(b, "Min", n); return;
    case VP_MMAX: put_str(b, "Max", n); return;
    case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3:
        if (pfs.m[p - VP_M1].par)
            mod_target_name(b, lock_idx_of(pfs.m[p - VP_M1].par), 1, n);
        else {
            put_str(b, "Macro ", n);
            put_int(b + 6, (int32_t)(p - VP_M1) + 1);
        }
        return;
    case VP_FX_SUB: put_str(b, fx.type == FX_DRIVE ? "Shape" : fx.type == FX_RING ? "Wave" : fx.type == FX_COMP ? "Ratio"
                                                            : "-", n); return;
    case PAR_FX_DELAY_TYPE: put_str(b, "Mode", n); return;   /* (1.0: on the delay's page) */
    case PAR_STEP_VOLUME: put_str(b, "Velocity", n); return;
    case PAR_STEP_NOTE: put_str(b, sel_step()->note & NOTE_LOCK ? "Locked" : "Note", n); return;   /* (0.23-t7) */
    case PAR_STEP_PROB: put_str(b, "Chance", n); return;
    case PAR_STEP_FLAM: put_str(b, "Flam", n); return;
    case PAR_STEP_SHIFT: put_str(b, "Shift", n); return;
    case PAR_P1_DEST: put_str(b, "Lock 1", n); return;
    case PAR_P2_DEST: put_str(b, "Lock 2", n); return;
    case PAR_P1_VAL: case PAR_P2_VAL: put_str(b, "Value", n); return;
    case PAR_EUKLID_LENGTH: put_str(b, "Length", n); return;
    case PAR_EUKLID_STEPS: put_str(b, "Hits", n); return;
    case PAR_EUKLID_ROTATION: put_str(b, "Rotate", n); return;
    case PAR_AUTOM_TRACK: put_str(b, "Lock lane", n); return;
    case PAR_FOLLOW: put_str(b, "Follow", n); return;
    case PAR_PATTERN_MODE: put_str(b, "Kit chg", n); return;
    case PAR_MIDI_CHAN_GLOBAL: put_str(b, "Global ch", n); return;
    case PAR_MIDI_FILT_TX: put_str(b, "TX filter", n); return;
    case PAR_MIDI_FILT_RX: put_str(b, "RX filter", n); return;
    case PAR_MIDI_ROUTING: put_str(b, "Route", n); return;
    case PAR_VOICE_DECIMATION_ALL: put_str(b, "SRate", n); return;
    default: break;
    }
    if (p >= PAR_MIDI_CHAN_1 && p <= PAR_MIDI_CHAN_6) {
        put_str(b, VOICE_SHORT[p - PAR_MIDI_CHAN_1], n);
        put_str(b + 2, " ch", n - 2);
        return;
    }
    if (p == PAR_MIDI_CHAN_7) {
        put_str(b, "OH ch", n);
        return;
    }
    if (p >= PAR_MIDI_NOTE1 && p <= PAR_MIDI_NOTE7) {
        put_str(b, VOICE_SHORT[p - PAR_MIDI_NOTE1], n);
        put_str(b + 2, " note", n - 2);
        return;
    }
    if (p >= PAR_FX_SEND1 && p < PAR_REV_SIZE) {    /* a send: its voice */
        put_str(b, (p - PAR_FX_SEND1) % 6u == 5u ? "HH" : VOICE_SHORT[(p - PAR_FX_SEND1) % 6u], n);
        put_str(b + 2, p < PAR_REV_SEND1 ? " fx" : " rev", n - 2);
        return;
    }
    if (ui.mode != M_FX && p >= PAR_REV_SIZE && p <= PAR_REV_TYPE) {   /* (the reverb's page names its own) */
        put_str(b, "Rev ", n);
        put_str(b + 4, p == PAR_REV_LEVEL ? "level" : p == PAR_REV_SIZE ? "size" : p == PAR_REV_DAMP ? "damp" : "type",
                n - 4);
        return;
    }
    if (ui.mode == M_MIXER && ((p >= PAR_VOL1 && p <= PAR_VOL6) || (p >= PAR_PAN1 && p <= PAR_PAN3) ||
                               (p >= PAR_PAN4 && p <= PAR_PAN6))) {
        uint32_t v = p >= PAR_VOL1 && p <= PAR_VOL6 ? p - PAR_VOL1 : p <= PAR_PAN3 ? p - PAR_PAN1 : p - PAR_PAN4 + 3u;
        put_str(b, v == 5u ? "HH" : VOICE_SHORT[v], n);
        put_str(b + 2, p >= PAR_VOL1 && p <= PAR_VOL6 ? " vol" : " pan", n - 2);
        return;
    }
    if (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6) {   /* (0.25.2: as LFO 2's and the global LFOs': the LXR's */
        put_str(b, "Dest", n);                        /* .. DstParam) */
        return;
    }
    if (p == PAR_FX_DRY_WET) {
        put_str(b, "Dry/Wet", n);
        return;
    }
    if (p == PAR_FX_ROUTING) {
        put_str(b, "Output", n);
        return;
    }
    put_str(b, par_name(p), n);
}
/* a trig condition's name */
static void cond_name(char *b, uint32_t c, uint32_t n)
{
    static const char *const C[COND_AB] = {"-", "FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"};
    uint32_t k, bb = 2;
    if (c < COND_AB) {
        put_str(b, C[c], n);
        return;
    }
    k = c - COND_AB;
    while (k >= bb && bb < 8u) {
        k -= bb;
        bb++;
    }
    put_int(b, (int32_t)k + 1);
    put_str(b + str_len(b), ":", n - str_len(b));
    put_int(b + str_len(b), (int32_t)bb);
}
static void put_ms(char *b, uint32_t ms10)           /* a time: 0.9 ms, 12 ms, 1.25 s */
{
    char *e;
    if (ms10 < 100u) {
        e = put_int(b, (int32_t)(ms10 / 10u));
        *e++ = '.';
        put_str(put_int(e, (int32_t)(ms10 % 10u)), " ms", 4);
    } else if (ms10 < 10000u) {
        put_str(put_int(b, (int32_t)(ms10 / 10u)), " ms", 4);
    } else {
        e = put_int(b, (int32_t)(ms10 / 10000u));
        *e++ = '.';
        two_digits(e, ms10 % 10000u / 100u);
        put_str(e + 2, " s", 3);
    }
}
/* 30: a frequency, hundredths of a Hz: 0.25 Hz, 12.5 Hz, 850 Hz, 1.2 kHz, 12 kHz */
static void put_hz(char *b, uint32_t n, uint32_t hz100)
{
    char *e;
    if (hz100 < 1000u) {
        e = put_int(b, (int32_t)(hz100 / 100u));
        *e++ = '.';
        two_digits(e, hz100 % 100u);
        e += 2;
    } else if (hz100 < 10000u) {
        e = put_int(b, (int32_t)(hz100 / 100u));
        *e++ = '.';
        e = put_int(e, (int32_t)(hz100 / 10u % 10u));
    } else if (hz100 < 100000u) {
        e = put_int(b, (int32_t)(hz100 / 100u));
    } else {
        e = put_int(b, (int32_t)(hz100 / 100000u));
        if (hz100 < 1000000u) {
            *e++ = '.';
            e = put_int(e, (int32_t)(hz100 / 10000u % 10u));
        }
        put_str(e, " kHz", n - (uint32_t)(e - b));
        return;
    }
    put_str(e, " Hz", n - (uint32_t)(e - b));
}
/* an envelope's time (the blocks from 0 to 1 at its step, Q24) as ms */
static void put_eg_time(char *b, uint32_t n, uint32_t step)
{
    uint32_t blocks = step ? (1u << 24) / step : 0u;
    if (!step || blocks > 600000u)
        put_str(b, "HOLD", n);
    else
        put_ms(b, blocks * (BLOCK * 100u) / 441u);   /* (ms x 10: blocks x BLOCK x 10000 / FS) */
}
/* a ratio, hundredths: x0.25 */
static void put_x(char *b, uint32_t r100)
{
    char *e;
    b[0] = 'x';
    e = put_int(b + 1, (int32_t)(r100 / 100u));
    *e++ = '.';
    two_digits(e, r100 % 100u);
}
/* 30: a knob in its own units where they say more than 0..127 (the filter in Hz, times in ms ..); 0: not one */
static int card_unit(char *b, uint32_t n, uint32_t p, uint32_t v)
{
    uint32_t f = (v & 127u) * 65536u / 127u;
    if (par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN) {     /* (1.0 Phase 13: GRAIN's POSITION, SIZE, DENSITY, SCATTER) */
        if (p == PAR_DLY_TIME && !fx.d_sync) {
            put_ms(b, (uint32_t)((((uint64_t)f * f) >> 16) * GR_PMAX >> 16) * 100u / 441u);
            return 1;
        }
        if (p == PAR_DLY_WOW) {
            put_ms(b, gr_len((int32_t)f) * 100u / 441u);
            return 1;
        }
        if (p == PAR_GLITCH) {
            uint32_t r = gr_rate((int32_t)((v & 127u) * 516u)) * 10u >> 16;   /* (tenths a second) */
            char *e;
            if (!r) {
                put_str(b, "OFF", n);
                return 1;
            }
            e = put_int(b, (int32_t)(r / 10u));
            if (r < 100u) {
                *e++ = '.';
                e = put_int(e, (int32_t)(r % 10u));
            }
            put_str(e, "/s", 3);
            return 1;
        }
        if (p == PAR_DSAT) {
            put_str(put_int(b, (int32_t)((v & 127u) * 100u / 127u)), "%", 2);
            return 1;
        }
    }
    if (p >= PAR_FILTER_FREQ_1 && p <= PAR_FILTER_FREQ_6) {
        put_hz(b, n, (uint32_t)(((uint64_t)(uint32_t)lut1024(FILT_F, f) * (FS * 100u)) >> 16));
        return 1;
    }
    if (p >= PAR_RESO_1 && p <= PAR_RESO_6) {        /* (the SVF's damping R, sound.c: Q = 1 / 2R) */
        uint32_t r = 65536u - f, q10;
        char *e;
        r = r < 6554u ? 1311u : r;
        q10 = 327680u / r;
        b[0] = 'Q';
        b[1] = ' ';
        e = put_int(b + 2, (int32_t)(q10 / 10u));
        if (q10 < 100u) {
            *e++ = '.';
            put_int(e, (int32_t)(q10 % 10u));
        }
        return 1;
    }
    if (p >= PAR_VELOA1 && p <= PAR_VELOD6_OPEN) {   /* (the attacks EG_ATK, the decays EG_DEC) */
        uint32_t k = p - PAR_VELOA1;
        put_eg_time(b, n, (k & 1u) || k >= 11u ? EG_DEC[v & 127u] : EG_ATK[v & 127u]);
        return 1;
    }
    if (p >= PAR_MOD_EG1 && p <= PAR_MOD_EG4) {
        put_eg_time(b, n, EG_ATK[v & 127u]);
        return 1;
    }
    if ((p >= PAR_FREQ_LFO1 && p <= PAR_FREQ_LFO6) || (p >= PAR_L2RATE1 && p < PAR_L2RATE1 + 6u)) {
        uint32_t sy = p >= PAR_L2RATE1 ? par[PAR_L2SYNC1 + p - PAR_L2RATE1] : par[PAR_SYNC_LFO1 + p - PAR_FREQ_LFO1];
        uint64_t x = 512u + (((uint64_t)f * 65024u) >> 16);   /* (lfo.c: (1/128 + v (1 - 1/128))^3 x 200 Hz) */
        if (sy && sy < NSYNC)
            put_str(b, "SYNC", n);
        else
            put_hz(b, n, (uint32_t)((((x * x) >> 16) * x * 20000u) >> 32));
        return 1;
    }
    if (p >= PAR_VOICE_DECIMATION1 && p <= PAR_VOICE_DECIMATION_ALL) {
        put_hz(b, n, (uint32_t)(((uint64_t)lut1024u(DECIM, f) * (FS * 100u)) >> 16));
        return 1;
    }
    if (p == PAR_NOISE_FREQ1) {                      /* (the snare's noise: v x 22 kHz) */
        put_hz(b, n, (v & 127u) * 2200000u / 127u);
        return 1;
    }
    if (p >= PAR_TRANS1_FREQ && p <= PAR_TRANS6_FREQ) {   /* (the sample's speed: x0.25 .. x4) */
        put_x(b, ((262144u + (((v & 127u) << 20) * 10u) / 339u) * 25u) >> 18);
        return 1;
    }
    if (p == PAR_DLY_BAND) {                         /* (1.0: OFF, or its centre; TAPE's half-rate loop stops at ~3.3 kHz) */
        uint32_t h;
        if (!v) {
            put_str(b, "OFF", n);
            return 1;
        }
        h = (uint32_t)((15000u * (uint64_t)(uint32_t)exp2_q16((int32_t)((v - 1u) * 331612u / 126u))) >> 16);
        put_hz(b, n, is_tape() && h > 332000u ? 332000u : h);
        return 1;
    }
    if (p == PAR_DLY_TIME && !fx.d_sync) {           /* (1.0: the delay's; TAPE's: tape_target, at half rate) */
        if (is_tape()) {
            uint32_t mx = tape_max() << 16;
            uint32_t d = TP_MIN + (uint32_t)(((((uint64_t)f * f) >> 16) * (mx - TP_MIN)) >> 16);
            put_ms(b, (d >> 15) * 100u / 441u);
        } else {
            put_ms(b, (delay_samples(f) >> 16) * 100u / 441u);
        }
        return 1;
    }
    if (p >= PAR_FX_P1 && p <= PAR_FX_P4) {          /* (fxbus_update: by the bus's type) */
        uint32_t k = p - PAR_FX_P1;
        switch (fx.type) {
        case FX_RING:
            if (k)
                return 0;
            put_hz(b, n, f * 88000u >> 16);
            return 1;
        case FX_COMP:
            if (k == 0u)
                put_ms(b, 3u + f * 1000u / 65536u);
            else if (k == 1u)
                put_ms(b, 250u + f * 1750u / 65536u);
            else if (k == 2u)
                put_str(put_int(b, -96 + (int32_t)(f * 96u / 65536u)), " dB", 4);
            else
                put_x(b, 100u + f * 200u / 65536u);
            return 1;
#if FX_CHORUS_ON
        case FX_CHORUS:                              /* (1.0: Rate, Depth, Delay, Lo cut) */
            if (k == 1u)
                put_ms(b, f * 40u / 65536u);
            else if (k == 2u)
                put_ms(b, 50u + f * 130u / 65536u);
            else if (k == 3u)
                put_hz(b, n, (20u + f * 780u / 65536u) * 100u);
            else
                put_hz(b, n, (uint32_t)(((uint64_t)3277u * (uint32_t)exp2_q16((int32_t)((f * 7643u) >> 10)) * 100u) >> 32));
            return 1;
#endif
        case FX_SHIFT:                               /* (Phase 12: Shift, Fdbk, Delay, Spread) */
            if (k == 0u) {
                uint32_t inc = sh_inc((int32_t)f), a = inc > 0x80000000u ? 0u - inc : inc;
                uint32_t c = (uint32_t)(((uint64_t)a * 100u * FS) >> 32);   /* (hundredths of a Hz) */
                char *q = b;
                if (!a) {
                    put_str(b, "0Hz", n);
                    return 1;
                }
                *q++ = inc > 0x80000000u ? '-' : '+';  /* (no space: "+85Hz" as the insert's, big on the card) */
                if (c >= 99950u) {
                    q = put_int(q, 1);
                    put_str(q, "kHz", n - (uint32_t)(q - b));
                    return 1;
                }
                if (c >= 1000u) {
                    q = put_int(q, (int32_t)((c + 50u) / 100u));
                } else {
                    q = put_int(q, (int32_t)(c / 100u));
                    *q++ = '.';
                    q = put_int(q, (int32_t)(c / 10u % 10u));
                    if (c < 100u)
                        q = put_int(q, (int32_t)(c % 10u));
                }
                put_str(q, "Hz", n - (uint32_t)(q - b));
            } else if (k == 2u) {
                put_ms(b, (uint32_t)((BLOCK + (((uint64_t)f * (882u - BLOCK)) >> 16)) * 100u / 441u));
            } else {
                put_str(put_int(b, (int32_t)((k == 1u ? f * 95u : f * 100u) >> 16)), "%", 2);
            }
            return 1;
        case FX_PHASER: case FX_FLANGER:
            if (k)
                return 0;
            put_hz(b, n, (uint32_t)(((uint64_t)3277u * (uint32_t)exp2_q16((int32_t)((f * 7643u) >> 10)) * 100u) >> 32));
            return 1;
        default: return 0;
        }
    }
    return 0;
}

static void card_value(char *b, uint32_t n, uint32_t p, uint32_t v)
{
    if (pl_card(b, n, p, v))                         /* (0.23-t7: P WTABLE's, P CHORD's) */
        return;
    if ((p == PAR_P1_VAL || p == PAR_P2_VAL) && ui.mode == M_STEP &&
        lock_get(shown_pat(), ui.track, ui.step & 63u, p == PAR_P2_VAL) == RUSH_CODE) {
        put_str(put_int(b, (int32_t)(v > RUSH_MAX ? RUSH_MAX : v)), "%", 2);   /* (39: a RUSH lock: its amount) */
        return;
    }
    switch (p) {                                     /* (0.16: the master compressor) */
    case PAR_DLY_ROUTE:                              /* (1.0 Phase 16: DLY > FX, PARALLEL, FX > DLY) */
        if (v == 64u) {
            put_str(b, "PARALLEL", n);
            return;
        }
        put_str(b, v > 64u ? "F>D " : "D>F ", n);   /* (short, for the card; the ROUTING page spells it out) */
        put_str(put_int(b + 4, (int32_t)(v > 64u ? (v - 64u) * 100u / 63u : (64u - v) * 100u / 64u)), "%", 2);
        return;
    case PAR_ECHOREV:
        if (dly_loop()) {                            /* (1.0 Phase 16: held at 0: REV > FX and FX > DLY make a loop) */
            put_str(b, "LOOP", n);
            return;
        }
        break;
    case PAR_MC_THR: put_str(put_int(b, -(int32_t)((127u - v) * 48u + 63u) / 127), " dB", 4); return;
    case PAR_MC_ATK: {
        uint32_t tq = (uint32_t)(((uint64_t)289014u * (uint32_t)exp2_q16((int32_t)(v & 127u) * 5143)) >> 16);
        put_ms(b, ((((tq >> 8) * 100u) / 441u) + 128u) >> 8);
        return;
    }
    case PAR_MC_REL: put_ms(b, (uint32_t)(((uint64_t)882u * (uint32_t)exp2_q16((int32_t)(v & 127u) * 3428)) >> 16) * 100u / 441u);
        return;
    case PAR_MC_GAIN: b[0] = '+'; put_str(put_int(b + 1, (int32_t)((v * 24u + 63u) / 127u)), " dB", 4); return;
    case PAR_MC_MIX: put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2); return;
    default: break;
    }
    if (p == PAR_DLY_TIME && fx.d_sync) {             /* (21b: SYNC: a note value; /2 when it plays at half) */
        static const char *const NV[NSYNCV] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4",
                                               "1/2T", "1/4.", "1/2"};
        uint32_t t = v * 65536u / 127u, i = (t * NSYNCV) >> 16, k = 1;
        uint64_t mx = is_tape() ? (uint64_t)tape_max() << 17 : is_grain() ? (uint64_t)GR_PMAX << 16
                    : fx.delay_type == DELAY_NORMAL ? D_HI_MAX : (uint64_t)D_HI_MAX * 48 / 100, sm;
        i = i < NSYNCV ? i : NSYNCV - 1u;
        sm = sync_len(i);
        for (; sm > mx; sm >>= 1)
            k <<= 1;
        put_str(b, NV[i], n);
        if (k > 1u) {                                /* (too long for the line: played at a half, a quarter ..) */
            put_str(b + str_len(b), " /", n - str_len(b));
            put_int(b + str_len(b), (int32_t)k);
        }
        return;
    }
    if (card_unit(b, n, p, v))
        return;
    switch (p) {
    case VP_NONE: b[0] = 0; return;
    case VP_IO_TYPE: put_str(b, IO_NAME[io_ord(v)], n); return;
    case VP_IO_SLOT:
        if (v >= 99u)
            put_int(b, (int32_t)v + 1);
        else
            two_digits(b, v + 1u);
        return;
    case VP_IO_SRC: put_str(b, v == 2u ? "FACTORY" : v ? "LIBRARY" : "SLOTS", n); return;
    case VP_IO_TRACK: put_str(b, VOICE_NAME[v % NTRACK], n); return;
    case VP_NAME_POS: put_int(b, (int32_t)v + 1); return;
    case VP_NAME_CHAR: b[0] = (char)(v >= 32u ? v : ' '); b[1] = 0; return;
    case VP_NAME_CASE: put_str(b, v ? "lower" : "UPPER", n); return;
    case VP_SONG_PAT:
        if (v == NUM_PATTERN) put_str(b, "END", n);
        else if (v > NUM_PATTERN) put_str(b, "LOOP", n);
        else { b[0] = 'P'; two_digits(b + 1, v + 1u); }
        return;
    case VP_SONG_NUM: two_digits(b, v + 1u); return;
    case VP_SONG_ROW: two_digits(b, v + 1u); return;
    case VP_SONG_EDIT: put_str(b, v == SE_DELETE ? "DELETE" : "INSERT", n); return;
    case VP_PLAY: put_str(b, v ? "SONG" : "PATTERN", n); return;
    case VP_ROADS: put_str(b, v ? "ROADS" : "PLAY", n); return;
    case VP_RSUR: put_str(put_int(b, (int32_t)(v * 100u / 15u)), "%", 2); return;
    case VP_RHOME:
        if (SONG_HOME[v % NSONGHOME])
            put_str(put_int(b, SONG_HOME[v % NSONGHOME]), " BARS", 6);
        else
            put_str(b, "NEVER", n);
        return;
    case VP_VOICES:
        if (v == PV_KIT) {
            put_str(b, "KIT ", n);
            two_digits(b + 4, pat_kit(&pat[sq.active]) + 1u);
        } else {
            put_str(b, v == PV_LOCKED ? "LOCKED" : "FREE", n);
        }
        return;
    case VP_SONG_REP:                                /* (0.19: END and LOOP play no times) */
        if (song_row(songs[sq.song][ui.song_row & 63u]) >= NUM_PATTERN) {
            put_str(b, "-", n);
        } else {
            b[0] = 'x';
            put_int(b + 1, SONG_REPS[v % NSONGREP]);
        }
        return;
    case VP_LEDS: put_str(b, v == 0u ? "OFF" : v == 1u ? "LOW" : "HIGH", n); return;
    case VP_AUTOSAVE: case VP_ACCEL: put_str(b, v ? "ON" : "OFF", n); return;
    case VP_PKEEP: put_str(b, v ? "KEEP" : "DROP", n); return;
    case VP_PALETTE: put_str(b, UI_PALETTES[v % NPALETTES].name, n); return;
    case VP_RCH: case VP_RCH2: if (v) put_int(b, (int32_t)v); else put_str(b, "OFF", n); return;
    case VP_RKNOBS: put_str(b, v == RK_ABS ? "ABS" : v == RK_REL ? "REL" : "PICKUP", n); return;
    case VP_RMUTE: put_str(b, v ? "TOGGLE" : "PRESS", n); return;
    case VP_CAKIT: put_str(b, v ? "KIT ONLY" : "BOTH KITS", n); return;
    case VP_MEDIT: put_str(b, v == 1u ? "KIT ONLY" : v ? "BOTH KITS" : "SPLIT", n); return;   /* (1.0: SPLIT) */
    case VP_PMEM: put_str(b, v ? "YES" : "NO", n); return;
    case VP_GL ... VP_GL_LAST:
        if (gls[(p - VP_GL) / NGLSET].wave == GW_FOLLOW) {   /* (1.0 Phase 11: FOLLOW's SOURCE, THRESHOLD, RELEASE) */
            uint32_t k = (p - VP_GL) % NGLSET;
            if (k == 5u) {
                put_str(b, v >= GF_ALL ? "ALL" : v == V_HAT ? "HH" : VOICE_SHORT[v], n);
                return;
            }
            if (k == 6u) {
                if (v) {
                    put_int(b, -60 + (int32_t)(((v & 127u) - 1u) * 48u + 63u) / 126);
                    put_str(b + str_len(b), " dB", n - str_len(b));
                } else {
                    put_str(b, "OFF", n);
                }
                return;
            }
            if (k == 7u) {                           /* (10 ms x 200^(v / 127)) */
                uint32_t ms = (uint32_t)((10ull * (uint32_t)exp2_q16((int32_t)((v & 127u) * 500948u / 127u)) + 32768u) >> 16);
                char *q;
                if (ms < 1000u) {
                    q = put_int(b, (int32_t)ms);
                    put_str(q, " ms", n - (uint32_t)(q - b));
                } else {
                    q = put_int(b, (int32_t)(ms / 1000u));
                    *q++ = '.';
                    q = put_int(q, (int32_t)((ms % 1000u + 50u) / 100u > 9u ? 9u : (ms % 1000u + 50u) / 100u));
                    put_str(q, " s", n - (uint32_t)(q - b));
                }
                return;
            }
        }
        switch ((p - VP_GL) % NGLSET) {
        case 0: {
            static const char *const D[NGD] = {"OFF", "MORPH", "FILTER", "LP", "HP", "FB PUSH", "COMB", "CRUSH",
                                               "JITTER", "CUTS", "PUMP", "DENSITY", "TRANSPOSE", "MEL CHANGE",
                                               "MEL SPREAD", "DELAY FB", "DELAY TIME", "REV SEND", "MACRO 1",
                                               "MACRO 2", "MACRO 3", "MACRO 4", "STUTTER", "DIRT", "DELAY BAND"};
            put_str(b, D[v < NGD ? v : 0], n);
            return;
        }
        case 1:
            if (v > 127u)
                b[0] = '+';
            put_int(b + (v > 127u), (int32_t)v - 127);
            return;
        case 2: {                                    /* (1.0: OFF, then the lengths in sixths of a 16th) */
            static const uint8_t L[13] = {3, 4, 6, 8, 12, 16, 18, 24, 30, 32, 36, 48, 72};
            static const char *const N[13] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "3/16", "1/4", "5/16",
                                              "1/2T", "3/8", "1/2", "3/4"};
            uint32_t l = v < NGLEN ? GL_LEN[v] : 0u, i;
            if (!l) {
                put_str(b, "OFF", n);
            } else if (l < 96u) {
                for (i = 0; i < 12u && L[i] != l; i++)
                    ;
                put_str(b, N[i], n);
            } else {
                put_int(b, (int32_t)(l / 96u));
                put_str(b + str_len(b), l == 96u ? " BAR" : " BARS", n - str_len(b));
            }
            return;
        }
        case 7: {                                    /* (1.0: RATE, SYNC OFF's: 0.01 .. 20 Hz; below 0.1 Hz its time) */
            uint32_t e = (uint32_t)exp2_q16((int32_t)((v & 127u) * 11u * 65536u / 127u));
            if (gls[(p - VP_GL) / NGLSET].len != GL_FREE) {
                put_str(b, "SYNC", n);
            } else if (e >= 10u << 16) {
                put_hz(b, n, (e + 32768u) >> 16);
            } else {
                uint32_t t10 = (uint32_t)((1000ull << 16) / e);   /* (tenths of a second a cycle) */
                char *q = put_int(b, (int32_t)(t10 >= 1000u ? (t10 + 5u) / 10u : t10 / 10u));
                if (t10 < 1000u) {
                    *q++ = '.';
                    q = put_int(q, (int32_t)(t10 % 10u));
                }
                put_str(q, " s", n - (uint32_t)(q - b));
            }
            return;
        }
        case 3: put_str(b, v == GW_FOLLOW ? "FOLLOW" : L_LFOW[v % NLFO], n); return;
        case 4: {
            static const char *const M[NGM] = {"FREE", "TRIG", "HOLD", "ONE", "HALF"};
            put_str(b, M[v < NGM ? v : 0], n);
            return;
        }
        case 5: {
            static const char *const T[NGT] = {"PLAY", "PATTERN", "BAR"};
            put_str(b, T[v < NGT ? v : 0], n);
            return;
        }
        default: put_int(b, (int32_t)v); return;
        }
    case VP_EH2: if (v) put_int(b, (int32_t)v); else put_str(b, "OFF", n); return;
    case VP_MVOL: put_int(b, (int32_t)v); return;
    case VP_STRETCH: {                               /* (1.0: 100%, 99.37%) */
        char *e = put_int(b, (int32_t)(v / 100u));
        if (v % 100u) {
            *e++ = '.';
            *e++ = (char)('0' + v % 100u / 10u);
            *e++ = (char)('0' + v % 10u);
        }
        put_str(e, "%", 2);
        return;
    }
    case VP_REALIGN: put_str(b, v ? "PATTERN" : "STOP", n); return;
    case VP_DJTYPE: {
        static const char *const T[NDJT] = {"LXR", "ACID", "MS20", "RING"};
        put_str(b, T[v < NDJT ? v : 0u], n);
        return;
    }
    case VP_DJTUNE: {
        static const char *const T[NRTUNE] = {"NOTE", "OCTAVES", "FIFTHS", "MAJOR", "MINOR", "SUS4", "MIN7", "PENTA"};
        put_str(b, T[v < NRTUNE ? v : 0u], n);
        return;
    }
    case VP_MOUT ... VP_MOUT_LAST: put_str(b, v ? "ON" : "OFF", n); return;
    case VP_UMIX ... VP_UMIX_LAST: put_str(b, v ? "OUT" : "IN", n); return;
    case VP_DJSPREAD: case VP_DJWET:                 /* (1.0 Phase 18: RING (2/2): 0 .. 100%; Spread 0 MONO) */
        if (p == VP_DJSPREAD && !v)
            put_str(b, "MONO", n);
        else
            put_str(put_int(b, (int32_t)v * 100 / 127), "%", 2);
        return;
    case VP_DJSLOPE: case VP_DJTONE:                 /* (-64 .. +63 about the middle, as before) */
        if (v > 64u)
            b[0] = '+';
        put_int(b + (v > 64u), (int32_t)v - 64);
        return;
    case VP_DENS: case VP_TD ... VP_TD_LAST: {       /* (-64..63 as -100 .. +100%) */
        int32_t d = (int32_t)v - 64, pc = d < 0 ? d * 100 / 64 : d * 100 / 63;
        if (pc > 0)
            b[0] = '+';
        put_str(put_int(b + (pc > 0), pc), "%", 2);
        return;
    }
    case VP_GHOST: put_int(b, (int32_t)v); return;
    case VP_SP ... VP_SP_LAST: sp_card_value(b, n, p - VP_SP, v); return;
    case VP_WD ... VP_WD_LAST: wd_card_value(b, n, p - VP_WD, v); return;
    case VP_DVARY: if (v) put_int(b, (int32_t)v); else put_str(b, "OFF", n); return;
    case VP_STCH: put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2); return;
    case VP_RUSH: case VP_RFILT:
        if (v)
            put_str(put_int(b, (int32_t)v), "%", 2);
        else
            put_str(b, "OFF", n);
        return;
    case VP_RSTART: case VP_RFREE: case VP_RFLAM: put_str(put_int(b, (int32_t)v), "%", 2); return;
    case VP_RTOP: put_str(b, v == RT_64 ? "1/64" : v == RT_128 ? "1/128" : "1/256", n); return;
    case VP_RGLIDE: put_str(put_int(b, (int32_t)v * 5), " ms", 4); return;
    case VP_RPITCH:
        if (v > 12u)
            b[0] = '+';
        put_str(put_int(b + (v > 12u), (int32_t)v - 12), " st", 4);
        return;
    case VP_STSIZE: {
        static const char *const Z[NSTSIZE] = {"1/8", "1/16", "1/16T", "1/32", "1/32T", "1/64", "RANDOM"};
        put_str(b, Z[v < NSTSIZE ? v : 1u], n);
        return;
    }
    case VP_STLEN: {
        static const char *const L[NSTLEN] = {"1/16", "1/8", "1/4", "1/2", "1 BAR"};
        put_str(b, L[v < NSTLEN ? v : 2u], n);
        return;
    }
    case VP_STVARY: {
        static const char *const V[NSV] = {"OFF", "SHRINK", "PITCH DN", "PITCH UP", "REVERSE", "RANDOM"};
        put_str(b, V[v < NSV ? v : 0], n);
        return;
    }
    case VP_BUILD: {
        static const char *const B[NDB] = {"CHANCE", "GHOSTS", "RATCHETS"};
        put_str(b, B[v < NDB ? v : 0], n);
        return;
    }
    case VP_SROOT: {
        static const char *const R[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        put_str(b, R[v % 12u], n);
        return;
    }
    case VP_SCALE: put_str(b, SCALE_NAME[v < NSCALE ? v : 0], n); return;
    case VP_TBASE: note_name(b, v + SEQ_DEFAULT_NOTE - TBASE_MAX); return;
    case VP_TRP:
        if (v > 7u)
            *b++ = '+';
        put_int(b, (int32_t)v - 7);
        return;
    case VP_TRWHEN: put_str(b, v == 2u ? "BAR" : v ? "BEAT" : "NOW", n); return;
    case VP_TRHOLD: put_str(b, v ? "HOLD" : "LATCH", n); return;
    case VP_SFOLLOW: case VP_GFOLLOW: put_str(b, v ? "ON" : "OFF", n); return;
    case VP_GROOVE: {                                /* (26: seq.c GROOVE_T's order) */
        static const char *const G[NGROOVE + 1u] = {"OFF", "SW 54%", "SW 58%", "SW 62%", "SW 66%", "SW 71%", "LAZY",
                                                    "DILLA", "2-STEP", "DUB", "PUSH", "TECHNO", "BROKEN"};
        put_str(b, G[v <= NGROOVE ? v : 0], n);
        return;
    }
    case VP_GAMT: put_str(put_int(b, (int32_t)v), "%", 2); return;
    case VP_LEN: case VP_MEL + ML_LEN: {
        static const char *const L[NLEN_ALL] = {"AUTO", "1/32", "1/16", "1/8", "1/8.", "1/4", "1/2", "TIE", "3/4",
                                                "1 BAR", "1.5 BARS", "2 BARS", "3 BARS", "4 BARS", "6 BARS", "8 BARS",
                                                "12 BARS", "16 BARS", "24 BARS", "32 BARS", "48 BARS", "64 BARS",
                                                "INF"};
        if (p == VP_LEN)                             /* (0.23-t11: the step's knob, TIE last; the melody's: NLEN) */
            v = len_knob(v);
        put_str(b, L[v < NLEN_ALL ? v : 0], n);
        return;
    }
    case VP_MEL + ML_SHAPE: {
        static const char *const S[NMEL] = {"OFF", "RANDOM", "WALK", "ARP UP", "ARP DN", "ARP U/D", "MOTIF",
                                            "CONVERGE", "DIVERGE", "CON/DIV", "LEAPFROG", "SPIRAL", "ALTERNATE",
                                            "PAIRS", "ROTATE", "INVERT", "PEDAL"};
        put_str(b, S[v < NMEL ? v : 0], n);
        return;
    }
    case VP_MEL + ML_BIAS: {                          /* (the register: -100 .. +100%, 0 the root's octave) */
        int32_t d = (int32_t)v - 64, pc = d < 0 ? d * 100 / 64 : d * 100 / 63;
        if (pc > 0)
            b[0] = '+';
        put_str(put_int(b + (pc > 0), pc), "%", 2);
        return;
    }
    case VP_MEL + ML_SPREAD: case VP_MEL + ML_STEPS: case VP_MEL + ML_DEJA: case VP_MEL + ML_LOOP:
    case VP_MEL + ML_LVAR: put_int(b, (int32_t)v); return;
    case VP_ER2: put_int(b, (int32_t)v); return;
    case VP_EOP: {
        static const char *const O[NEUOP] = {"OR", "XOR", "AND", "SUB"};
        put_str(b, O[v < NEUOP ? v : 0], n);
        return;
    }
    case VP_FX_SUB:
        put_str(b, fx.type == FX_DRIVE ? L_DIST[v % 3u] : fx.type == FX_RING ? L_WAVE[v % NWAVE]
                   : fx.type == FX_COMP ? L_RATIO[v % 6u] : "-", n);
        return;
    case VP_TRACK_VOL: case VP_MASTER: put_int(b, (int32_t)v); return;
    case VP_COND: cond_name(b, v, n); return;
    case VP_RAT_N:
        if (!v)
            put_str(b, "OFF", n);
        else
            put_int(b, shown_pat()->rat[ui.track][ui.step & 63u] & RAT_BOUNCE ? BNC_HITS[v & 7u] : (int32_t)v + 1);
        return;
    case VP_RAT_RATE: {
        static const char *const R[4] = {"1/32", "1/48", "1/64", "1/24"};
        static const char *const C[4] = {"BALL", "TIGHT", "SLOW", "DRUNK"};
        put_str(b, shown_pat()->rat[ui.track][ui.step & 63u] & RAT_BOUNCE ? C[v & 3u] : R[v & 3u], n);
        return;
    }
    case VP_RAT_SHAPE: put_str(b, v ? "BOUNCE" : "EVEN", n); return;
    case VP_BSTART: {
        static const char *const S[4] = {"1/4", "1/8", "1/16", "1/32"};
        put_str(b, S[v & 3u], n);
        return;
    }
    case VP_BPITCH:
        if (v > 12u)
            b[0] = '+';
        put_int(b + (v > 12u), (int32_t)v - 12);
        return;
    case VP_ROLL:                                    /* the swept rate as a note value: 1/4 .. 1/192 */
        b[0] = '1';
        b[1] = '/';
        put_int(b + 2, (int32_t)((4u * (uint32_t)exp2_q16((int32_t)(v * 2882u)) + 32768u) >> 16));
        return;
    case VP_RAT_FADE: put_str(b, v == RF_DOWN ? "DOWN" : v == RF_UP ? "UP" : "FLAT", n); return;
    case VP_LINK: put_int(b, (int32_t)v + 1); return;
    case VP_KOCT:
        if (v > 3u)
            b[0] = '+';
        put_int(b + (v > 3u), (int32_t)v - 3);
        return;
    case VP_KVEL: case VP_KACC: put_int(b, (int32_t)v); return;
    case VP_KB_SRC: {
        static const char *const P[3] = {"SLOTS", "LIBRARY", "FACTORY"};
        put_str(b, P[kb_part_at(v < 3u ? v : 0u)], n);
        return;
    }
    case VP_KBSLOTS: put_str(b, v ? "SLOTS" : "FACTORY", n); return;
    case VP_GTAKES: {
        static const char *const G[NGTAKES] = {"KEYS", "PAT+KIT", "ALL", "OFF"};
        put_str(b, G[v < NGTAKES ? v : 0u], n);
        return;
    }
    case VP_P2 + 4: note_name(b, v); return;
    case VP_P2_LAST: {
        static const char *const L[3] = {"1/16", "1/8", "1/4"};
        put_str(b, L[v < 3u ? v : 1u], n);
        return;
    }
    case VP_P2: case VP_P2 + 1: case VP_P2 + 2: case VP_P2 + 3: case VP_P2 + 5: case VP_P2 + 6:
        put_str(put_int(b, (int32_t)((v * 100u + 63u) / 127u)), "%", 2);
        return;
    case VP_LSTART: {
        static const char *const S[NSTART + 2u] = {"-", "DUCK", "CALL", "GHOST", "CHAIN", "EDITED"};
        put_str(b, S[v <= NSTART + 1u ? v : 0], n);
        return;
    }
    case VP_LAMT: {
        const uint8_t *L = shown_pat()->link[ui.link & 7u];
        uint32_t e = L[1] & 15u;
        if (!e || e >= NLE)
            put_str(b, "-", n);
        else
            link_amount(b, e, (int32_t)v + LK_LO[e]);
        return;
    }
    case VP_DEPTH: put_str(put_int(b, (int32_t)((v * 100u + 63u) / 127u)), "%", 2); return;
    case VP_LWHEN: {
        static const char *const E[NLV] = {" hit", " acc", " rest"};
        put_str(b, VOICE_SHORT[v / NLV % NTRACK], n);
        put_str(b + 2, E[v % NLV], n - 2u);
        return;
    }
    case VP_LTGT: put_str(b, VOICE_SHORT[v % NTRACK], n); return;
    case VP_LEFF: put_str(b, LINK_EFF[v < NLE ? v : 0], n); return;
    case VP_LWIN: {
        static const char *const W[NLW] = {"SAME", "NEXT", "NEXT 2", "BAR"};
        put_str(b, W[v & 3u], n);
        return;
    }
    case VP_ACTION: {                                 /* (v: the track's generator's actions' 1.., gen_acts) */
        static const char *const A[NACT] = {"-", "DICE", "COMMIT", "UNDO", "RESET"};
        uint8_t a[NGACT];
        uint32_t na = gen_acts(shown_pat(), ui.track, a);
        put_str(b, A[v && v <= na ? a[v - 1u] : 0], n);
        return;
    }
    case VP_GMODE: put_str(b, v == 4u ? "CELL" : GEN_NAME[v < NGEN ? v : 0], n); return;   /* (GEN_ORDER's
                                                      * index; 1.0: CELLULAR fits as CELL) */
    case VP_DIR: {
        static const char *const D[NDIR] = {"FWD", "REV", "PING", "PING2", "RAND", "WALK"};
        put_str(b, D[v < NDIR ? v : 0], n);
        return;
    }
    case VP_CLOCK: {
        static const char *const C[NCLK] = {"/4", "/3", "/2", "2/3", "3/4", "1x", "4/3", "3/2", "x2", "x3", "x4"};
        put_str(b, C[v < NCLK ? v : CLK_1X], n);
        return;
    }
    case VP_GA: case VP_GB: case VP_GC: {
        uint32_t g = shown_pat()->gen[ui.track][0];
        if (g == GEN_GRIDS && p == VP_GA) {
            static const char *const PART[3] = {"BD", "SD", "HH"};
            put_str(b, PART[v < 3u ? v : 0u], n);
        } else if (vp_max(p) == 0u)
            put_str(b, "-", n);
        else if (vp_max(p) == 1u)
            put_str(b, v ? "ON" : "OFF", n);
        else {
            put_int(b, (int32_t)v);
            if (g == GEN_MUTATE)
                put_str(b + str_len(b), "%", n - str_len(b));
        }
        return;
    }
    case VP_DJ:
        if (v == 64u)
            put_str(b, "OFF", n);
        else if (pfs.dj_ftype == DJT_RING)          /* (1.0: RING: its root) */
            note_name(b, ring_root(((int32_t)v - 64) * 1040));
        else {
            put_str(b, v < 64u ? "LP " : "HP ", n);
            put_int(b + 3, v < 64u ? 64 - (int32_t)v : (int32_t)v - 64);
        }
        return;
    case VP_REPEAT: {
        static const char *const R[4] = {"1/4", "1/8", "1/16", "1/32"};
        put_str(b, R[v & 3u], n);
        return;
    }
    case VP_TAPE: {                                  /* 200 ms x 20^(v / 127), as tenths of a second */
        int32_t ms = (int32_t)(((int64_t)200 * exp2_q16((int32_t)v * 2232)) >> 16);
        put_int(b, ms / 1000);
        put_str(b + str_len(b), ".", n - str_len(b));
        put_int(b + str_len(b), ms / 100 % 10);
        put_str(b + str_len(b), " s", n - str_len(b));
        return;
    }
    case VP_MSEL: put_str(b, "M", n); put_int(b + 1, (int32_t)v + 1); return;
    case VP_MTARGET: mod_target_name(b, v, 1, n); return;
    case VP_MMIN: case VP_MMAX: {
        uint32_t q = pfs.m[ui.msel].par;
        if (q)
            par_format(b, q, v, n);
        else
            put_int(b, (int32_t)v);
        return;
    }
    case VP_M1: case VP_M1 + 1: case VP_M1 + 2: case VP_M1 + 3: {
        uint32_t m = p - VP_M1, q = pfs.m[m].par;
        if (q)                                       /* (the parameter's value as it reads) */
            par_format(b, q, (uint32_t)((int32_t)pfs.m[m].lo + ((int32_t)pfs.m[m].hi - (int32_t)pfs.m[m].lo) *
                                                                   (int32_t)v / 127), n);
        else
            put_str(b, "-", n);
        return;
    }
    case PAR_TRACK_LENGTH: case PAR_EUKLID_LENGTH: case PAR_EUKLID_STEPS: case PAR_EUKLID_ROTATION:
    case PAR_STEP_VOLUME: case PAR_P1_VAL: case PAR_P2_VAL:
        put_int(b, (int32_t)v);
        return;
    case PAR_STEP_NOTE: note_name(b, v); return;
    case PAR_STEP_PROB: put_int(b, (int32_t)v); put_str(b + str_len(b), "%", n - str_len(b)); return;
    case PAR_STEP_FLAM: put_str(b, L_FLAM[v % 6u], n); return;
    case PAR_AUTOM_TRACK: put_int(b, (int32_t)v + 1); return;
    case PAR_PATTERN_MODE: put_str(b, v ? "AUTO" : "OFF", n); return;
    case PAR_FOLLOW: put_str(b, v ? "ON" : "OFF", n); return;
    case PAR_MORPH: put_int(b, (int32_t)(v * 100u / 255u)); put_str(b + str_len(b), "%", n - str_len(b)); return;
    default: break;
    }
    if (p < NPAR)
        par_format(b, p, v, n);
    else
        put_int(b, (int32_t)v);
}

static void draw_card(uint32_t k, uint32_t p)
{
    char nm[24], v[24];
    uint32_t val, mn, mx, s;
    int lock = 0, hot = ui.hot == k + 1u, idle = (ui.mode == M_SOUND && p != VP_NONE && par_unheard(ui.track, p)) ||
                                                 vp_dead(p);
    uint16_t bg = T_SURF, fg = T_TEXT;
    int32_t x0 = (int32_t)k * 60;
    if (p != VP_NONE && ui.held_step >= 0 && par_lockable(p) && ui.mode != M_STEP) {
        const pattern_t *sp = shown_pat();
        const step_t *st = &sp->step[ui.track][ui.held_step];
        uint32_t d = par_to_autodest(p), hs = (uint32_t)ui.held_step;
        lock = 1;
        val = lock_get(sp, ui.track, hs, 0) == d ? st->p1v : lock_get(sp, ui.track, hs, 1) == d ? st->p2v : vp_get(p);
    } else {
        val = p == VP_NONE ? 0u : vp_get(p);
    }
    mn = p == VP_NONE ? 0u : vp_min(p);
    mx = p == VP_NONE ? 1u : vp_max(p);
    s = sig_add(p, val);
    s = sig_add(s, hot | lock << 1 | fx.type << 2 | ui.mode << 8);
    s = sig_add(s, ui.msel | (uint32_t)pfs.m[p >= VP_M1 && p <= VP_M4 ? p - VP_M1 : ui.msel & 3u].par << 4 |
                       (uint32_t)shown_pat()->gen[ui.track][0] << 20 |
                       (uint32_t)(shown_pat()->rat[ui.track][ui.step & 63u] & RAT_BOUNCE) << 21);
    s = sig_add(s, ins[track_voice(ui.track)].type | (uint32_t)idle << 8);   /* (27a: the insert's knobs are named
                                                                              * by its type) */
    card_name(nm, sizeof nm, p);
    card_value(v, sizeof v, p, val);
    {                                                /* (31: and the text itself: a model's knobs have its names, */
        uint32_t i;                                  /* .. P CHORD's the chord: the old model's stayed on a change) */
        for (i = 0; nm[i]; i++)
            s = sig_add(s, (uint8_t)nm[i]);
        for (i = 0; v[i]; i++)
            s = sig_add(s, (uint8_t)v[i]);
    }
    if (!region(R_CARD0 + k, s))
        return;
    cv_begin(60, CARD_H, T_BG);
    if (idle) {                                      /* (0.19: a model's own sound: this knob unheard, locked; 40: a
                                                      * knob not yet in use) */
        uint16_t f = ux_mix(T_BG, T_SURF, 50);
        cv_rrect(2, 2, 56, CARD_H - 6, 5, f, T_BG);
        cv_text_in(2, 8, 56, &AF_S, nm, ux_mix(f, T_MID, 55), f);
        cv_text_in(2, 26, 56, &AF_M, "--", ux_mix(f, T_MID, 55), f);
    } else if (p != VP_NONE) {
        if (hot) {
            bg = T_SEL;
            fg = T_INK;
        }
        cv_rrect(2, 2, 56, CARD_H - 6, 5, bg, T_BG);
        cv_text_in(2, 8, 56, &AF_S, nm, hot ? T_INK : T_MID, bg);
        {
            int big = text_w(&AF_M, v) <= 52;
            if (big)
                cv_text_in(2, 26, 56, &AF_M, v, lock ? T_ACCENT : fg, bg);
            else
                text_c(2, 30, 56, &AF_S, v, lock ? T_ACCENT : fg, bg);
        }
        if (mx > mn) {                               /* the gauge */
            int32_t w = (int32_t)((val - mn) * 44u / (mx - mn));
            cv_rect(8, CARD_H - 15, 44, 3, hot ? T_INK : T_LINE);
            cv_rect(8, CARD_H - 15, w, 3, lock ? T_ACCENT : hot ? T_INK : T_THEME);
        }
    } else {
        cv_rect(28, CARD_H / 2 - 2, 4, 1, T_LINE);
    }
    cv_rect(26, 0, 8, 2, hot ? T_ACCENT : T_LINE);  /* (the knob above it) */
    cv_blit((uint32_t)x0, CARD_Y);
}
static void draw_cards(void)
{
    uint16_t pp[4];
    uint32_t k;
    if (body_full) {                                 /* (the body drew over the cards' place: draw them when it's back) */
        for (k = 0; k < 4u; k++)
            rsig[R_CARD0 + k] = 0;
        return;
    }
    page_params(pp);
    for (k = 0; k < 4u; k++)
        draw_card(k, pp[k]);
}

/* -------------------------------------------------------------- frame --- */
static void ui_draw(void)
{
    if (ui.mode == M_SOUND)                          /* (0.19: a page a model's sound hides (a kit loaded): the next) */
        sound_fix();
    draw_header();
    draw_title();
    draw_body();
    draw_cards();
    ui.force = 0;
}

/* 0.14: the splash, an 8-bit potion and ELIXIR in pixel letters. Each map character is a 5 px square:
 * c cork, o glass outline, g glass, h shine, l liquid (the theme), L its light, d its dark, s a sparkle */
static const char POTION[18][23] = {
    ".....s...cccc.........",
    ".........cccc......s..",
    "........oooooo....sss.",
    ".........ohgo......s..",
    ".........ohgo.........",
    ".s.......oggo.........",
    "sss.....oggggo........",
    ".s.....oggggggo.......",
    "......oggggggggo......",
    ".....oLLLLLLLLLLo.....",
    "....ollhlllllllllo....",
    "....olhlllllllLllo...s",
    "....olhllllllllllo....",
    "....olhllllLlllllo....",
    "....ollllllllllLlo....",
    ".....oldddddddddo.....",
    "......oddddddddo......",
    ".......oooooooo.......",
};
/* E L I X I R, 7 rows each, a bit per column (MSB the left one), and their widths */
static const uint8_t WORD[6][7] = {
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
};
static const uint8_t WORD_W[6] = {5, 5, 3, 5, 3, 5};

static void draw_word(uint32_t x, uint32_t y, uint32_t px, uint16_t c)
{
    uint32_t i, r, k;
    for (i = 0; i < 6u; i++) {
        for (r = 0; r < 7u; r++)
            for (k = 0; k < WORD_W[i]; k++)
                if (WORD[i][r] >> (WORD_W[i] - 1u - k) & 1u)
                    lcd_fill(x + k * px, y + r * px, px, px, c);
        x += (WORD_W[i] + 2u) * px;
    }
}

/* a potion map character's colour (0: none) */
static uint16_t potion_col(char m)
{
    const uint16_t white = RGB(255, 255, 255), black = RGB(0, 0, 0);
    switch (m) {
    case 'c': return RGB(150, 96, 52);
    case 'o': return T_TEXT;
    case 'g': return ux_mix(T_BG, T_TEXT, 14);
    case 'h': return white;
    case 'l': return T_THEME;
    case 'L': return ux_mix(T_THEME, white, 45);
    case 'd': return ux_mix(T_THEME, black, 35);
    case 's': return ux_mix(T_THEME, white, 60);
    default: return 0;
    }
}
static void draw_splash(void)
{
    uint32_t r, k;
    lcd_fill(0, 0, 240, 240, T_BG);
    for (r = 0; r < 18u; r++)
        for (k = 0; k < 22u; k++) {
            uint16_t c = potion_col(POTION[r][k]);
            if (c)
                lcd_fill(65u + k * 5u, 10u + r * 5u, 5, 5, c);
        }
    draw_word(33, 117, 5, ux_mix(T_THEME, T_BG, 45));           /* (36 columns: 180 px; a shadow, then the word) */
    draw_word(30, 114, 5, T_TEXT);
    draw_text_box(0, 160, 240, &AF_M, "drum machine", T_TEXT, 1);
    draw_text_box(0, 192, 240, &AF_S, "the LXR-02's engine on the FM-1", T_MID, 1);
    draw_text_box(0, 209, 240, &AF_S, "unofficial port, " ELIXIR_VERSION, T_MID, 1);
}
/* 29: CONFIG's last page: the potion and the word small, the version, the CPU line with the battery (37: the load of
 * each core is on LOAD) */
static void draw_about(void)
{
    uint32_t r, k, i;
    int32_t x = 128;
    for (r = 0; r < 18u; r++)
        for (k = 0; k < 22u; k++) {
            uint16_t c = potion_col(POTION[r][k]);
            if (c)
                cv_rect(14 + (int32_t)k * 3, 4 + (int32_t)r * 3, 3, 3, c);
        }
    for (i = 0; i < 6u; i++) {
        for (r = 0; r < 7u; r++)
            for (k = 0; k < WORD_W[i]; k++)
                if (WORD[i][r] >> (WORD_W[i] - 1u - k) & 1u)
                    cv_rect(x + (int32_t)k * 3 - 46, 12 + (int32_t)r * 3, 3, 3, T_TEXT);
        x += (int32_t)(WORD_W[i] + 2u) * 3;
    }
    cv_text(82, 40, &AF_S, ELIXIR_VERSION, T_THEME);
    cv_text(82, 54, &AF_S, "the LXR-02 on the FM-1", T_MID);
    draw_cpu(110, "CPU ");
}
