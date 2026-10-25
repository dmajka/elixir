/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: the screen (240 x 240), drawn with Felucca's renderer (gfx.c: anti-aliased Inter Tight, the palettes).
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

enum { R_HDR, R_TITLE, R_LABELS, R_BARS, R_BODY, R_CARD0, R_COL0 = R_CARD0 + 4, NREG = R_COL0 + 16 };
static uint32_t rsig[NREG];
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
    s = sig_add(s, morph_amt);
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
        cv_text_fit(64, 5, &AF_S, b, T_TEXT, T_BG, 84);
        if (morph_amt)
            cv_text(150, 5, &AF_S, "M", T_ACCENT);
    }
    if (sq.sync_ext)
        cv_text(160, 5, &AF_S, "MIDI", T_MID);
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
    case M_SONG: put_str(b, "SONG", n); break;
    case M_SOUND: {
        uint8_t l[NSPG][2];
        uint32_t i, k = sound_pages(l);
        if (ui.copy) {                               /* 0.11: OCT+ held: the knobs move every voice */
            put_str(b, "CONTROL ALL: ", n);
            put_str(b + str_len(b), SECT_NAME[ui.sect], n - str_len(b));
        } else {
            put_str(b, SECT_NAME[ui.sect], n);
        }
        if (ui.kplay)                                /* (0.8: the keys still play notes) */
            put_str(b + str_len(b), "  KEYS", n - str_len(b));
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
    case M_PERF: put_str(b, PERF_TITLE[ui.page % NPERF], n); break;
    case M_EUCLID:
        if (ui.page % NEUCLID == PG_ACTION && ui.action) {   /* 0.10: what OCT+ does, in place of the title */
            static const char *const A[NACT] = {"", "DICE", "COMMIT", "UNDO", "RESET"};
            put_str(b, "PRESS OCT+ TO ", n);
            put_str(b + str_len(b), A[ui.action < NACT ? ui.action : 0], n - str_len(b));
        } else if (ui.page % NEUCLID == PG_ACTION && shown_pat()->gen[ui.track][0] == GEN_GRIDS) {
            put_str(b, "ACTION / GRIDS MAP", n);
        } else {
            put_str(b, EUCLID_TITLE[ui.page % NEUCLID], n);
        }
        break;
    case M_FX: put_str(b, ui.page % 3u == 2u ? "REVERB" : "FX BUS", n); break;
    case M_MIXER: put_str(b, MIXER_TITLE[(ui.page % NMIXER) / 2u], n); break;
    case M_CONFIG: put_str(b, CONFIG_TITLE[ui.page % 7u], n); break;
    case M_SAVE: put_str(b, "SAVE ", n); put_str(b + 5, IO_NAME[ui.io_type], n - 5); break;
    case M_LOAD: put_str(b, "LOAD ", n); put_str(b + 5, IO_NAME[ui.io_type], n - 5); break;
    case M_LAST: put_str(b, ui.last_all ? "LAST STEP (ALL)" : "LAST STEP", n); break;
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
        s = sig_add(s, (uint32_t)pt->cond[t][st] | (uint32_t)pt->rat[t][st] << 8 | (pt == &gpat) << 16);
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
            if (x->prob < 100u && x->prob) {        /* a chance: an outline */
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
            if (pt->cond[t][st])                     /* a trig condition: a dot top left */
                cv_rect(1, y + 1, 3, 3, T_THEME);
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

/* ---------------------------------------------------- the other bodies --- */
/* the body in two strips (the canvas holds half of it): draw(y0) draws with the strip's origin at -y0 */
static void body_strips(void (*draw)(int32_t))
{
    int32_t y;
    for (y = 0; y < BODY_H; y += BODY_H / 2) {
        cv_begin(240, BODY_H / 2, T_BG);
        cv_oy = -y;
        draw(y);
        cv_oy = 0;
        cv_blit(0, BODY_Y + (uint32_t)y);
    }
}

static void list_row(int32_t y, const char *left, const char *right, int sel, int dim)
{
    uint16_t bg = sel ? T_SEL : T_BG;
    if (sel)
        cv_rrect(4, y, 232, 16, 3, T_SEL, T_BG);
    cv_text_fit(10, y + CAP_IN(S, 16), &AF_S, left, sel ? T_INK : dim ? T_DIM : T_MID, bg, 60);
    cv_text_fit(64, y + CAP_IN(S, 16), &AF_S, right, sel ? T_INK : dim ? T_DIM : T_TEXT, bg, 168);
}

static void draw_kit_browser(int32_t y0)             /* 0.9: the slots or (0.9.2) the library's kits (L1 ..) */
{
    uint32_t i, k = ui.kb_sel, first = kb_step(ui.kb_sel, -3);
    (void)y0;
    for (i = 0; i < 3u && kb_step(first, 5) == kb_step(first, 6) && kb_step(first, -1) != first; i++)
        first = kb_step(first, -1);                   /* (the list's end: the last 7 shown) */
    cv_text(10, 2, &AF_S, ui.kb_lib ? "LIBRARY KITS" : "KIT SLOTS", T_THEME);
    cv_text_r(232, 2, &AF_S, "OCT+ LOAD", T_MID, T_BG);
    if (!kb_has((int32_t)ui.kb_sel)) {                /* (the library empty) */
        cv_text(10, 34, &AF_S, "THE LIBRARY IS EMPTY", T_MID);
        return;
    }
    for (i = 0, k = first; i < 7u; i++) {
        char l[8], r[12];
        put_str(l, "", sizeof l);
        if (k < NUM_KITS) {
            two_digits(l, k + 1u);
            memcpy(r, kit_names[k], 8);
            if (k == kit_loaded)
                put_str(l + 2, " *", sizeof l - 2);
        } else {
            uint32_t n = k - NUM_KITS + 1u, p = 1;
            l[0] = 'L';
            if (n >= 100u)
                l[p++] = (char)('0' + n / 100u);
            if (n >= 10u)
                l[p++] = (char)('0' + n / 10u % 10u);
            l[p++] = (char)('0' + n % 10u);
            l[p] = 0;
            if (kl_name(KLK_KIT, k - NUM_KITS, r))
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

/* 0.11: Control All's dialog, after OCT+ is let go */
static void draw_ca_ask(int32_t y0)
{
    (void)y0;
    cv_rrect(10, 20, 220, 88, 6, T_SURF, T_BG);
    cv_text_in(10, 30, 220, &AF_S, "CONTROL ALL", T_MID, T_SURF);
    cv_text_in(10, 50, 220, &AF_M, "REVERT THE KIT?", T_ACCENT, T_SURF);
    cv_text_in(10, 76, 220, &AF_S, "OCT+: revert   OCT-: keep", T_TEXT, T_SURF);
    cv_text_in(10, 90, 220, &AF_S, "the changes sound until then", T_MID, T_SURF);
}

/* PERF: the 16 keys (lit: on, outlined: latched), then the STUTTER length, the THROW voice, the DJ filter */
static const char *const PERF_KEY[NPF2] = {"FILL", "RPT", "FRZ", "T.RV", "T.FX", "FB+", "STP", "LP~", "HP~",
                                           "HLF", "JIT", "CUT", "SHT", "PMP", "RES", "SPC", "CRS", "REV"};
static const char *const PERF_NAME[NPF2] = {"FILL", "REPEAT", "FREEZE", "THROW REV", "THROW FX", "FB PUSH",
                                            "TAPE STOP", "LP SWEEP", "HP SWEEP", "HALF", "JITTER", "CUTS", "SHORT",
                                            "PUMP", "COMB", "SPACE", "CRUSH", "REVERSE"};
static void draw_perf(int32_t y0)
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
    cv_text(6, 60, &AF_S, b, T_TEXT);
    cv_text(6, 78, &AF_S, "LP", d < 0 ? T_THEME : T_DIM);
    cv_text_r(234, 78, &AF_S, "HP", d > 0 ? T_THEME : T_DIM, T_BG);
    cv_rect(26, 84, 186, 4, T_SURF);                  /* the DJ filter: where it is */
    cv_rect(118, 80, 2, 12, T_LINE);
    cv_rect(118 + d * 92 / 65536 - 2, 80, 5, 12, d ? T_ACCENT : T_MID);
    if (tape_q16 < 65536u) {
        cv_text(6, 98, &AF_S, "TAPE", T_REC);
        cv_rect(44, 102, (int32_t)(tape_q16 * 186u >> 16), 4, T_REC);
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
    else if (b[0])
        cv_text_fit(6, 112, &AF_S, b, T_ACCENT, T_BG, 228);
    else
        cv_text(6, 112, &AF_S, "BLACK: MUTE  BAR KEYS: REPEAT", T_MID);
}

static void draw_shift_legend(int32_t y0)
{
    static const char *const F[9] = {"KIT", "MORPH", "SONG", "PROJECT", "CONFIG", "RELOAD", "LAST", "FX",
                                     "PATTERN"};
    uint32_t i;
    (void)y0;
    cv_text(8, 0, &AF_S, "SHIFT + WHITE KEY", T_THEME);
    for (i = 0; i < NTRACK; i++) {                   /* keys 1..7: play the voice */
        int32_t x = 4 + (int32_t)i * 33;
        cv_rrect(x, 15, 31, 17, 3, T_RAISE, T_BG);
        text_c(x, 15 + CAP_IN(S, 17), 31, &AF_S, VOICE_SHORT[i], T_TEXT, T_RAISE);
    }
    for (i = 0; i < 9u; i++) {                       /* keys 8..16: the LXR's SHIFT functions */
        int32_t x = 4 + (int32_t)(i % 3u) * 78, y = 35 + (int32_t)(i / 3u) * 19;
        char b[16];
        put_int(b, (int32_t)i + 8);
        cv_rrect(x, y, 76, 17, 3, T_SEL, T_BG);
        cv_text(x + 4, y + CAP_IN(S, 17), &AF_S, b, T_INK);
        cv_text_fit(x + 22, y + CAP_IN(S, 17), &AF_S, F[i], T_INK, T_SEL, 52);
    }
    cv_text(8, 94, &AF_S, "VOICE: mute  GLO: solo  BAR: length", T_TEXT);
    cv_text(8, 108, &AF_S, "SEQ: links EDIT: keys PLAY: song REC: opts", T_TEXT);
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
    cv_text(8, 96, &AF_S, sq.rec && sq.running ? "REC: the notes go into the steps" : "SHIFT + key: accent", T_TEXT);
    cv_text(8, 112, &AF_S, "ALGORITHM: track   SHIFT + EDIT: grid", T_MID);
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
    {
        char b[24];
        put_str(b, "BANK ", sizeof b);
        b[5] = (char)('1' + ui.bank);
        b[6] = 0;
        cv_text(6, 110, &AF_S, b, T_MID);
        cv_text(60, 110, &AF_S, "BAR KEYS: bank", T_DIM);
        cv_text_r(234, 110, &AF_S, sq.pending != sq.active ? "QUEUED" : "", T_ACCENT, T_BG);
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
        uint32_t r = (uint32_t)(first + i), v = songs[sq.song][r];
        char l[8], b[16];
        two_digits(l, r + 1u);
        if (sq.song_mode && sq.running && sq.song_pos == r)
            put_str(l + 2, " >", 6);
        if (v == SONG_END)
            put_str(b, "END", sizeof b);
        else if (v == SONG_LOOP)
            put_str(b, "LOOP", sizeof b);
        else {
            put_str(b, "PATTERN ", sizeof b);
            two_digits(b + 8, v + 1u);
        }
        list_row(i * 16, l, b, r == ui.song_row, v == SONG_END);
    }
}

static void io_slot_name(char *b, uint32_t n, uint32_t type, uint32_t s)
{
#if KETTLE_FACTORY_KITS
    if (type == IO_FACTORY) {
        if (s >= FACTORY_NKITS) {
            put_str(b, "ALL > SLOTS", n);
        } else {
            kit_factory_name(s, b);
            b[8] = 0;
        }
        return;
    }
#endif
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
        while (k < MAX_SONG_LENGTH && songs[s][k] != SONG_END)
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
        cv_text(10, 76, &AF_S, "K1: position  K2: letter  K3: case", T_TEXT);
        cv_text(10, 92, &AF_S, "OCT+: save    OCT-: back", T_MID);
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
        cv_text(10, 116, &AF_S, ui.mode == M_SAVE ? "OCT+ SAVE    SAVE: LOAD PAGE" : "OCT+ LOAD    SAVE: SAVE PAGE",
                T_MID);
    }
}

/* the mixer's bars: the volumes (and the pans' ticks), or a send (its pages: the FX send, the reverb send) */
static uint32_t mixer_bar_par(uint32_t v)
{
    uint32_t g = (ui.page % NMIXER) / 2u;
    if (ui.mode == M_PERF)                           /* (PERF's MIX pages: the volumes) */
        return PAR_VOL1 + v;
    return g == 2u ? PAR_FX_SEND1 + v : g == 3u ? PAR_REV_SEND1 + v : PAR_VOL1 + v;
}
static void draw_mixer(int32_t y0)
{
    uint32_t v;
    (void)y0;
    for (v = 0; v < NVOICE; v++) {
        int32_t x = 10 + (int32_t)v * 38, h = par[mixer_bar_par(v)] * 90 / 127;
        int32_t pk = voice_peak[v] * 90 / 32768;
        cv_rrect(x, 8, 26, 96, 3, T_SURF, T_BG);
        cv_rect(x + 3, 104 - h - 3, 20, h, ux_mix(T_SURF, TRACK_COL[v], 55));
        if (pk > 0 && mixer_bar_par(v) == PAR_VOL1 + v)
            cv_rect(x + 3, 104 - pk - 3, 20, 2, T_ACCENT);
        {   /* pan: a tick */
            int32_t p = par[v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u];
            cv_rect(x + 1 + p * 22 / 127, 108, 3, 4, T_THEME);
        }
        text_c(x - 4, 114, 34, &AF_S, v == 5u ? "HH" : VOICE_SHORT[v], TRACK_COL[v], T_BG);
    }
}

static void draw_fx(int32_t y0)
{
    char b[24];
    uint32_t i;
    int rev = ui.page % 3u == 2u;
    (void)y0;
    cv_text(10, 6, &AF_S, rev ? "MODEL" : "TYPE", T_MID);
    cv_text(10, 20, &AF_M, rev ? L_REVMODEL[rv.type % 3u] : L_FX[fx.type < NFX ? fx.type : 0], T_THEME);
    if (!rev && fx.type == FX_DRIVE)
        cv_text(120, 22, &AF_S, L_DIST[fx.dist_type % 3u], T_TEXT);
    else if (!rev && fx.type == FX_DELAY)
        cv_text(120, 22, &AF_S, L_DELAY[fx.delay_type & 1u], T_TEXT);
    else if (!rev && fx.type == FX_COMP)
        cv_text(120, 22, &AF_S, L_RATIO[fx.ratio % 6u], T_TEXT);
    cv_text(10, 50, &AF_S, rev ? "REVERB SENDS" : "FX SENDS", T_MID);
    for (i = 0; i < NVOICE; i++) {                  /* each voice's send: lit when it sends, the amount */
        uint32_t a = par[(rev ? PAR_REV_SEND1 : PAR_FX_SEND1) + i];
        int on = a != 0u;
        int32_t x = 10 + (int32_t)i * 36;
        cv_rrect(x, 66, 32, 18, 3, on ? TRACK_COL[i] : T_SURF, T_BG);
        text_c(x, 66 + CAP_IN(S, 18), 32, &AF_S, i == 5u ? "HH" : VOICE_SHORT[i], on ? RGB(12, 12, 12) : T_DIM,
               on ? TRACK_COL[i] : T_SURF);
        put_int(b, (int32_t)a);
        text_c(x, 86, 32, &AF_S, b, on ? T_TEXT : T_DIM, T_BG);
    }
    if (rev) {
        put_str(b, "LEVEL ", sizeof b);
        put_int(b + 6, (int32_t)par[PAR_REV_LEVEL]);
    } else {
        put_str(b, "DRY/WET ", sizeof b);
        put_int(b + 8, (int32_t)par[PAR_FX_DRY_WET]);
        put_str(b + str_len(b), "%", sizeof b - str_len(b));
    }
    cv_text(10, 104, &AF_S, b, T_TEXT);
    cv_text_r(234, 104, &AF_S, "SENDS: GLO > MIXER", T_MID, T_BG);
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
static void draw_cpu(int32_t y)
{
    char b[40];
    uint32_t c = cpu_pct(), pk = cpu_peak_pct();
    int32_t w = (int32_t)(c > 100u ? 100u : c), wp = (int32_t)(pk > 100u ? 100u : pk);
    put_str(b, "CPU ", sizeof b);
    put_int(b + 4, (int32_t)c);
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
        cv_text(8, 56, &AF_S, "KNOB 2 (Start): a starting point,", T_TEXT);
        cv_text(8, 71, &AF_S, "or set one up on LINK SETUP.", T_TEXT);
        cv_text(8, 94, &AF_S, "SHIFT + SEQ opens this page.", T_MID);
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

static void draw_config_help(int32_t y0)
{
    (void)y0;
    switch (ui.page % 7u) {
    case 0:
        cv_text(10, 8, &AF_S, "TEMPO: turn it to the bottom for MIDI", T_TEXT);
        cv_text(10, 22, &AF_S, "clock sync (the LXR's MID).", T_TEXT);
        cv_text(10, 40, &AF_S, "FOLLOW: the bar follows the playhead", T_TEXT);
        cv_text(10, 56, &AF_S, "KIT CHANGE AUTO: a pattern loads the", T_TEXT);
        cv_text(10, 70, &AF_S, "kit it was saved with", T_TEXT);
        cv_text(10, 86, &AF_S, "MIDI clock goes out on USB always.", T_MID);
        break;
    case 1:
        cv_text(10, 8, &AF_S, "GLOBAL: notes play the selected track;", T_TEXT);
        cv_text(10, 22, &AF_S, "program change picks the pattern", T_TEXT);
        cv_text(10, 44, &AF_S, "FILTERS: N notes  R clock/start/stop", T_TEXT);
        cv_text(10, 58, &AF_S, "C CC / NRPN  P program change", T_TEXT);
        cv_text(10, 80, &AF_S, "ROUTE: TRS>USB forwards the jack", T_TEXT);
        cv_text(10, 94, &AF_S, "MIDI input to USB (soft thru)", T_TEXT);
        break;
    case 2:
    case 3:
        cv_text(10, 8, &AF_S, "Each track's MIDI channel: in and out", T_TEXT);
        break;
    case 4:
    case 5:
        cv_text(10, 8, &AF_S, "ANY: the track plays any note, pitched", T_TEXT);
        cv_text(10, 22, &AF_S, "A NOTE: only that note triggers it", T_TEXT);
        break;
    default:
        cv_text(10, 8, &AF_S, "LEDS: OFF / LOW / HIGH glow", T_TEXT);
        cv_text(10, 22, &AF_S, "AUTOSAVE: the session, when stopped", T_TEXT);
        cv_text(10, 36, &AF_S, "ACCEL: fast turns move faster", T_TEXT);
        break;
    }
    draw_cpu(104);
}

/* the sound page's body: the section's parameters (the knobs' half lit), then the track's steps */
static void draw_sound(int32_t y0)
{
    uint32_t k, h;
    const pattern_t *pt = shown_pat();
    (void)y0;
    for (h = 0; h < 2u; h++)
        for (k = 0; k < 4u; k++) {
            uint32_t p = sect_par(ui.track, ui.sect, h, k);
            int32_t x = 6 + (int32_t)(k % 2u) * 117, y = 6 + (int32_t)h * 46 + (int32_t)(k / 2u) * 20;
            char v[24];
            int act = h == ui.half;
            if (!p)
                continue;
            if (act)
                cv_rrect(x - 2, y - 1, 114, 19, 3, T_SURF, T_BG);
            cv_text_fit(x + 2, y + CAP_IN(S, 17), &AF_S, par_name(p), act ? T_MID : T_DIM, act ? T_SURF : T_BG, 60);
            par_format(v, p, vp_get(p), sizeof v);
            cv_text_fit(x + 64, y + CAP_IN(S, 17), &AF_S, v, act ? T_TEXT : T_DIM, act ? T_SURF : T_BG, 46);
        }
    if (ui.copy) {                                   /* 0.11: Control All: what OCT- and letting go do */
        cv_text(6, 106, &AF_S, ui.ca ? "LET GO: KEEP OR REVERT" : "KNOB: ALL TRACKS   TRACK KEY: COPY", T_ACCENT);
        return;
    }
    for (k = 0; k < 16u; k++) {                      /* the track's steps in the shown bar */
        uint32_t s = ui.bar * 16u + k;
        int32_t x = 6 + (int32_t)k * 14;
        int on = main_on(pt, ui.track, s), play = sq.running && sq.idx[ui.track] == (int8_t)s;
        uint16_t c = on ? TRACK_COL[ui.track] : (k & 3u) == 0u ? T_RAISE : T_SURF;
        cv_rrect(x, 104, 12, 16, 2, c, T_BG);
        if (play)
            cv_rect(x, 122, 12, 3, T_ACCENT);
    }
}
static uint32_t sound_sig(void)
{
    uint32_t k, h, s = sig_add(ui.track, ui.sect | ui.half << 4 | ui.bar << 6 | ui.copy << 8 | ui.ca << 9);
    const pattern_t *pt = shown_pat();
    for (h = 0; h < 2u; h++)
        for (k = 0; k < 4u; k++) {
            uint32_t p = sect_par(ui.track, ui.sect, h, k);
            if (p)
                s = sig_add(s, vp_get(p));
        }
    for (k = 0; k < 4u; k++)
        s = sig_add(s, pt->main[ui.track][k]);
    return sig_add(s, sq.running ? (uint32_t)(uint8_t)sq.idx[ui.track] : 0xFFFFu);
}

static void draw_body(void)
{
    uint32_t s;
    void (*fn)(int32_t) = 0;
    if (ui.ca_ask) {                                 /* 0.11: Control All's dialog */
        fn = draw_ca_ask;
        s = 41;
    } else if (perf_mix() && !ui.shift && !ui.clear_open && !ui.kb_open) {   /* 0.8: PERF's MIX: the faders */
        uint32_t v;
        fn = draw_mixer;
        s = 13;
        for (v = 0; v < NVOICE; v++)
            s = sig_add(s, par[PAR_VOL1 + v] | (uint32_t)(voice_peak[v] >> 10) << 8 |
                               (uint32_t)par[v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u] << 16);
    } else if ((ui.mode == M_PERF || (ui.perf && ui.shift)) && !ui.clear_open && !ui.kb_open) {   /* (SHIFT: latches) */
        fn = draw_perf;
        s = sig_add(sig_add(11, pf.on), ui.pf_latch | (uint32_t)perf_bank2() << 31);
        s = sig_add(s, sq.drill | ui.drill_latch << 8);
        s = sig_add(s, sq.roll | ui.roll_latch << 8 | seq_mutes() << 16 | ui.shift << 24);
        s = sig_add(s, pfs.repeat | ui.track << 4 | (uint32_t)(pf.dj_d + 65536) >> 10 << 8 | (tape_q16 >> 11) << 20);
    } else if (ui.shift && !ui.clear_open) {
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
        s = sig_add(3, ui.kb_sel | kit_loaded << 8 | (uint32_t)ui.kb_lib << 16);
    } else {
        switch (ui.mode) {
        case M_PATTERN:
            fn = draw_patterns;
            s = sig_add(4, sq.active | sq.pending << 8 | ui.bank << 16);
            break;
        case M_SONG:
            fn = draw_song;
            s = sig_add(5, ui.song_row | sq.song << 8 | (sq.song_mode && sq.running ? sq.song_pos : 0xFFu) << 16);
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
            s = 7;
            for (v = 0; v < NVOICE; v++)
                s = sig_add(s, par[mixer_bar_par(v)] | (uint32_t)(voice_peak[v] >> 10) << 8 |
                                   (uint32_t)par[v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u] << 16);
            s = sig_add(s, ui.page);
            break;
        }
        case M_FX: {
            uint32_t v;
            fn = draw_fx;
            s = sig_add(8, fx.type | fx.dist_type << 4 | fx.delay_type << 8 | fx.ratio << 12 | par[PAR_FX_DRY_WET] << 16);
            s = sig_add(s, ui.page % 3u | rv.type << 4 | (uint32_t)par[PAR_REV_LEVEL] << 8);
            for (v = 0; v < NVOICE; v++)
                s = sig_add(s, par[PAR_FX_SEND1 + v] | (uint32_t)par[PAR_REV_SEND1 + v] << 8);
            break;
        }
        case M_KEYS:
            fn = draw_keys;
            s = sig_add(14, ui.keys | (uint32_t)ui.knote << 27);
            s = sig_add(s, ui.track | ui.koct << 4 | (sq.rec && sq.running) << 8);
            break;
        case M_CONFIG:
            fn = draw_config_help;
            s = sig_add(9, ui.page);
            s = sig_add(s, cpu_pct() | cpu_peak_pct() << 8 | audio_late << 16 | (uint32_t)batt_level() << 28);
            break;
        case M_SOUND:
            fn = draw_sound;
            s = sig_add(10, sound_sig());
            break;
        case M_EUCLID:
            if (ui.page % NEUCLID >= PG_LINKS) {
                uint32_t l;
                const pattern_t *pt = shown_pat();
                fn = draw_links;
                s = sig_add(12, ui.link | (uint32_t)pt->cdepth << 8);
                for (l = 0; l < NLINK; l++)
                    s = sig_add(s, (uint32_t)pt->link[l][0] | (uint32_t)pt->link[l][1] << 8 |
                                       (uint32_t)pt->link[l][2] << 16);
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
    if (!region(R_BODY, s))
        return;
    body_strips(fn);
}

/* ------------------------------------------------------------- cards --- */
static void card_name(char *b, uint32_t n, uint32_t p)
{
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
    case VP_NAME_POS: put_str(b, "Position", n); return;
    case VP_NAME_CHAR: put_str(b, "Letter", n); return;
    case VP_NAME_CASE: put_str(b, "Case", n); return;
    case VP_SONG_PAT: put_str(b, "Pattern", n); return;
    case VP_SONG_NUM: put_str(b, "Song", n); return;
    case VP_LEDS: put_str(b, "Leds", n); return;
    case VP_AUTOSAVE: put_str(b, "Autosave", n); return;
    case VP_PKEEP: put_str(b, "Edits", n); return;
    case VP_ACCEL: put_str(b, "Accel", n); return;
    case VP_PALETTE: put_str(b, "Colours", n); return;
    case VP_TRACK_VOL: put_str(b, "Volume", n); return;
    case VP_MASTER: put_str(b, "Master", n); return;
    case VP_DJ: put_str(b, "DJ filter", n); return;
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
                                                            : fx.type == FX_DELAY ? "Mode" : "-", n); return;
    case PAR_STEP_VOLUME: put_str(b, "Velocity", n); return;
    case PAR_STEP_NOTE: put_str(b, "Note", n); return;
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
    if ((ui.mode == M_MIXER || perf_mix()) && ((p >= PAR_VOL1 && p <= PAR_VOL6) || (p >= PAR_PAN1 && p <= PAR_PAN3) ||
                               (p >= PAR_PAN4 && p <= PAR_PAN6))) {
        uint32_t v = p >= PAR_VOL1 && p <= PAR_VOL6 ? p - PAR_VOL1 : p <= PAR_PAN3 ? p - PAR_PAN1 : p - PAR_PAN4 + 3u;
        put_str(b, v == 5u ? "HH" : VOICE_SHORT[v], n);
        put_str(b + 2, p >= PAR_VOL1 && p <= PAR_VOL6 ? " vol" : " pan", n - 2);
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
static void card_value(char *b, uint32_t n, uint32_t p, uint32_t v)
{
    switch (p) {
    case VP_NONE: b[0] = 0; return;
    case VP_IO_TYPE: put_str(b, IO_NAME[v % NIO], n); return;
    case VP_IO_SLOT:
        if (v >= 99u)
            put_int(b, (int32_t)v + 1);
        else
            two_digits(b, v + 1u);
        return;
    case VP_IO_SRC: put_str(b, v ? "LIBRARY" : "SLOTS", n); return;
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
    case VP_LEDS: put_str(b, v == 0u ? "OFF" : v == 1u ? "LOW" : "HIGH", n); return;
    case VP_AUTOSAVE: case VP_ACCEL: put_str(b, v ? "ON" : "OFF", n); return;
    case VP_PKEEP: put_str(b, v ? "KEEP" : "DROP", n); return;
    case VP_PALETTE: put_str(b, UI_PALETTES[v % NPALETTES].name, n); return;
    case VP_FX_SUB:
        put_str(b, fx.type == FX_DRIVE ? L_DIST[v % 3u] : fx.type == FX_RING ? L_WAVE[v % NWAVE]
                   : fx.type == FX_COMP ? L_RATIO[v % 6u] : fx.type == FX_DELAY ? L_DELAY[v & 1u] : "-", n);
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
    case VP_KB_SRC: put_str(b, v ? "LIBRARY" : "SLOTS", n); return;
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
        uint8_t a[3];
        uint32_t na = gen_acts(shown_pat()->gen[ui.track][0], a);
        put_str(b, A[v && v <= na ? a[v - 1u] : 0], n);
        return;
    }
    case VP_GMODE: {                                  /* (v: GEN_ORDER's index) */
        static const char *const G[NGEN] = {"OFF", "EUCLID", "MUTATE", "TURING", "CELLULAR", "GRIDS"};
        put_str(b, G[v < NGEN ? v : 0], n);
        return;
    }
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
    int lock = 0, hot = ui.hot == k + 1u;
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
    if (!region(R_CARD0 + k, s))
        return;
    card_name(nm, sizeof nm, p);
    card_value(v, sizeof v, p, val);
    cv_begin(60, CARD_H, T_BG);
    if (p != VP_NONE) {
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
    page_params(pp);
    for (k = 0; k < 4u; k++)
        draw_card(k, pp[k]);
}

/* -------------------------------------------------------------- frame --- */
static void ui_draw(void)
{
    draw_header();
    draw_title();
    draw_body();
    draw_cards();
    ui.force = 0;
}

static void draw_splash(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 84, 240, &AF_L, "KETTLE", T_THEME, 1);
    draw_text_box(0, 124, 240, &AF_S, "LXR-02 drum engine for the FM-1", T_TEXT, 1);
    draw_text_box(0, 142, 240, &AF_S, "unofficial port, " KETTLE_VERSION, T_MID, 1);
}
