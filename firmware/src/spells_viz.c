/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: SPELLS' screen (32; spells.c). The body: an 8-bit wizard top left (his orb lit when a spell can be cast);
 * beside him the spell's name, what it does (or what the knob last turned does) and the targets (the black keys);
 * under them the white keys: what the spell may change, sound over pattern. OCT+ rolls a d12 beside the wizard,
 * spinning for SP_ROLL_MS, slowing to its face; the die stays while the cast is on audition, with what it changed.
 * Nothing else moves. Included by ui_draw.c. */

/* the knobs' cards */
static void sp_card_name(char *b, uint32_t n, uint32_t i)
{
    static const char *const N[8] = {"Spell", "Variation", "Scope", "Roll", "Sound", "Pattern", "Density", "Chaos"};
    static const char *const L[NSPL] = {"Rev Send", "FX Send", "Rev Level", "FX Mix"};
    put_str(b, i < 8u ? N[i] : i == SPV_MOT ? "Motion" : i == SPV_MLEN ? "Lengths" : i >= SPV_LIM ? L[(i - SPV_LIM) % NSPL]
                                                                 : SPELL[sp_spell() % NSPELL].cn[(i - 8u) & 3u], n);
}
static void sp_card_value(char *b, uint32_t n, uint32_t i, uint32_t v)
{
    static const char *const MODE[NSPM] = {"NEW", "SAME"};
    static const char *const MOT[NSPMO] = {"SAME", "ONE", "FEW", "MANY", "FREE"};
    static const char *const MLEN[NSPML] = {"BARS", "EVEN", "ODD"};
    switch (i) {
    case SPV_SPELL:
        if (v >= NSPELL && v < NSPELL + NRECIPE && rcp[v - NSPELL].spell) {
            put_str(b, "U", n);
            put_int(b + 1, (int32_t)(v - NSPELL + 1u));
            put_str(b + str_len(b), " ", n - str_len(b));
            put_str(b + str_len(b), SPELL[(rcp[v - NSPELL].spell - 1u) % NSPELL].card, n - str_len(b));
        } else {
            put_str(b, SPELL[v % NSPELL].card, n);
        }
        return;
    case SPV_VAR:
        if (v)
            put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2);
        else
            put_str(b, "EXACT", n);
        return;
    case SPV_SCOPE: put_str(b, sp_scope_of(sp.mask) < NSCOPE ? SCOPE_NAME[v % NSCOPE] : "CUSTOM", n); return;
    case SPV_MODE: put_str(b, MODE[v % NSPM], n); return;
    case SPV_MOT: put_str(b, MOT[v % NSPMO], n); return;
    case SPV_MLEN: put_str(b, MLEN[v % NSPML], n); return;
    case SPV_MODE + 1u + SPK_DENS: {                 /* (64 as now; -100 .. +100%) */
        int32_t d = (int32_t)v - 64, pc = d < 0 ? d * 100 / 64 : d * 100 / 63;
        if (!d) {
            put_str(b, "AS IS", n);
            return;
        }
        if (pc > 0)
            b[0] = '+';
        put_str(put_int(b + (pc > 0), pc), "%", 2);
        return;
    }
    case SPV_MODE + 1u + SPK_SOUND:
    case SPV_MODE + 1u + SPK_PAT:
        if (!v) {
            put_str(b, "OFF", n);
            return;
        }
        /* fall through */
    default:
        if (i < SPV_LIM)
            break;
        if (v)                                       /* (LIMITS: the value itself, as the mixer shows it) */
            put_int(b, (int32_t)v);
        else
            put_str(b, "NONE", n);
        return;
    }
    put_str(put_int(b, (int32_t)(v * 100u / 127u)), "%", 2);
}
/* what a knob does (the body, while it is turned) */
static const char *sp_knob_line(uint32_t i)
{
    static const char *const L[8] = {"", "Knobs moved each cast", "White keys: what it changes", "",
                                     "How much voices change", "How much steps change",
                                     "Fewer or more hits", "Rules to anything goes"};
    static const char *const M[NSPM] = {"A new roll each cast", "The last roll again"};
    static const char *const T[NSPL] = {"Most a voice's reverb send", "Most a voice's FX send",
                                        "Most the reverb's level", "Most the FX bus's mix"};
    static const char *const MO[NSPMO] = {"Every track as one", "One track its own speed", "Two tracks their own way",
                                          "Half the tracks their own", "Any speed, any direction"};
    static const char *const ML[NSPML] = {"16, 32 or 64 steps", "Bars and 12, 24 steps", "Odd lengths too: 7, 15"};
    if (i == SPV_MOT)
        return MO[sp.mot % NSPMO];
    if (i == SPV_MLEN)
        return ML[sp.mlen % NSPML];
    return i == SPV_MODE ? M[sp.mode % NSPM] : i < 8u ? L[i] : i >= SPV_LIM ? T[(i - SPV_LIM) % NSPL]
                                                                            : SPELL[sp_spell() % NSPELL].cl[(i - 8u) & 3u];
}

/* the wizard, 22 x 28, 4 px a character: h hat, H its shade, s a star, b gold, f face, e eyes and boots, w beard,
 * W its shade, r robe, R its shade, k staff, n hands, o orb, O its light, g its glow, * sparkles */
static const char WIZARD[28][23] = {
    "........h.........*...", ".......hh.......*gog..", ".......hHh.......oOo*.", "......hhsh.......gog..",
    "......hhhHh.......k...", ".....hshhHh.......k...", ".....hhhhhHh......k...", "....hhhhshhHh.....k...",
    "....bbbbbbbbb.....k...", ".hhhhhhhhhhhhhhh..k...", ".....fefffef......k...", ".....fffffff......k...",
    "....wwfffffww.....k...", "..rrwwwwwwwwwrr...k...", "..rrwwwwWwwwwrrrrnnn..", "..Rrrwwwwwwwrrrrrnnn..",
    "..RrrwwwWwwwrr....k...", "..RRrrwwwwwrrr....k...", "..RRrrrwWwrrrr....k...", ".RRRrrrrwrrrrrr...k...",
    ".RRRrrrrrrrsrrr...k...", ".RRrrrrsrrrrrrrr..k...", ".RRrrrrrrrrrrrrr..k...", "RRRrrrrrrrrrsrrrr.k...",
    "RRRrrsrrrrrrrrrrr.k...", "RRRrrrrrrrrrrrrrr.k...", "bbbbbbbbbbbbbbbbb.k...", "..ee.......ee.....k..."};
static uint16_t wiz_col(char m, int lit)
{
    const uint16_t white = RGB(255, 255, 255), black = RGB(0, 0, 0), gold = RGB(236, 196, 84);
    switch (m) {
    case 'h': return T_THEME;
    case 'H': return ux_mix(T_THEME, black, 35);
    case 's': return gold;
    case 'b': return gold;
    case 'f': case 'n': return RGB(232, 186, 150);
    case 'e': return RGB(34, 30, 44);
    case 'w': return RGB(236, 236, 240);
    case 'W': return RGB(184, 184, 196);
    case 'r': return ux_mix(T_THEME, black, 20);
    case 'R': return ux_mix(T_THEME, black, 50);
    case 'k': return RGB(150, 96, 52);
    case 'o': return lit ? T_ACCENT : ux_mix(T_ACCENT, T_BG, 65);
    case 'O': return lit ? ux_mix(T_ACCENT, white, 60) : ux_mix(T_ACCENT, T_BG, 45);
    case 'g': return lit ? ux_mix(T_ACCENT, T_BG, 55) : 0;
    case '*': return lit ? ux_mix(T_ACCENT, white, 50) : 0;
    default: return 0;
    }
}
static void draw_wizard(int32_t x0, int32_t y0, int32_t px, int lit)
{
    uint32_t r, k;
    for (r = 0; r < 28u; r++)
        for (k = 0; k < 22u; k++) {
            uint16_t c = wiz_col(WIZARD[r][k], lit);
            if (c)
                cv_rect(x0 + (int32_t)k * px, y0 + (int32_t)r * px, px, px, c);
        }
}

/* the d12: a dodecahedron face on, a pentagon in a decagon, turned by a (64ths of a turn) and tilted (ys: 0..127
 * its height); the five faces round it lit from the top left */
static const int8_t SP_SIN[64] = {0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125,
                                  122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60,
                                  -71, -81, -90, -98, -106, -112, -117, -122, -125, -126, -127, -126, -125, -122,
                                  -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12};
static const int8_t SP_DEC[10][2] = {{0, -127}, {75, -103}, {121, -39}, {121, 39}, {75, 103}, {0, 127}, {-75, 103},
                                     {-121, 39}, {-121, -39}, {-75, -103}};
static void sp_fill(const int32_t *x, const int32_t *y, uint32_t n, uint16_t c)   /* a convex polygon */
{
    int32_t y0 = y[0], y1 = y[0], yy;
    uint32_t i;
    for (i = 1; i < n; i++) {
        y0 = y[i] < y0 ? y[i] : y0;
        y1 = y[i] > y1 ? y[i] : y1;
    }
    for (yy = y0; yy <= y1; yy++) {
        int32_t l = 9999, r = -9999;
        for (i = 0; i < n; i++) {
            uint32_t j = (i + 1u) % n;
            int32_t ya = y[i], yb = y[j], xa = x[i], xb = x[j], xx;
            if ((yy < ya && yy < yb) || (yy > ya && yy > yb))
                continue;
            xx = ya == yb ? (xa < xb ? xa : xb) : xa + (yy - ya) * (xb - xa) / (yb - ya);
            l = xx < l ? xx : l;
            r = xx > r ? xx : r;
            if (ya == yb)
                r = xa > xb ? xa : xb;
        }
        if (l <= r)
            cv_rect(l, yy, r - l + 1, 1, c);
    }
}
static void draw_d12(int32_t cx, int32_t cy, int32_t rad, uint32_t a, int32_t ys, uint32_t face)
{
    int32_t dx[10], dy[10], px[5], py[5], s = SP_SIN[a & 63u], c = SP_SIN[(a + 16u) & 63u];
    uint32_t k;
    char b[4];
    const uint16_t white = RGB(255, 255, 255), black = RGB(0, 0, 0);
    for (k = 0; k < 10u; k++) {                      /* (the outline, turned, then tilted) */
        int32_t x = SP_DEC[k][0], y = SP_DEC[k][1];
        dx[k] = cx + ((x * c - y * s) >> 7) * rad / 127;
        dy[k] = cy + (((x * s + y * c) >> 7) * rad / 127) * ys / 127;
    }
    for (k = 0; k < 5u; k++) {                       /* (the face: on the outline's even corners, 0.53 of it) */
        px[k] = cx + (dx[2u * k] - cx) * 17 / 32;
        py[k] = cy + (dy[2u * k] - cy) * 17 / 32;
    }
    for (k = 0; k < 5u; k++) {                       /* (the faces round it: P k, D 2k .. 2k+2, P k+1) */
        int32_t fx[5] = {px[k], dx[2u * k], dx[2u * k + 1u], dx[(2u * k + 2u) % 10u], px[(k + 1u) % 5u]};
        int32_t fy[5] = {py[k], dy[2u * k], dy[2u * k + 1u], dy[(2u * k + 2u) % 10u], py[(k + 1u) % 5u]};
        int32_t lx = dx[2u * k + 1u] - cx, ly = dy[2u * k + 1u] - cy;   /* (towards the top left: lighter) */
        int32_t l = clamp32(50 - (lx + ly) * 40 / (rad > 0 ? rad : 1), 10, 90);
        sp_fill(fx, fy, 5, ux_mix(ux_mix(T_THEME, black, 45), ux_mix(T_THEME, white, 25), l));
    }
    sp_fill(px, py, 5, ux_mix(T_THEME, white, 35));
    for (k = 0; k < 10u; k++)                        /* (the edges) */
        cv_line(dx[k], dy[k], dx[(k + 1u) % 10u], dy[(k + 1u) % 10u], T_TEXT);
    for (k = 0; k < 5u; k++) {
        cv_line(px[k], py[k], px[(k + 1u) % 5u], py[(k + 1u) % 5u], ux_mix(T_THEME, black, 55));
        cv_line(px[k], py[k], dx[2u * k], dy[2u * k], ux_mix(T_THEME, black, 55));
    }
    put_int(b, (int32_t)(face ? face : 12u));
    text_c(cx - 14, cy - 8, 28, &AF_M, b, RGB(20, 18, 28), ux_mix(T_THEME, white, 35));
}
/* the roll: frame f of SP_ROLL_MS / 40, slowing to its face (the angle 0, flat) */
static uint32_t sp_frame(void) { return sp.rolling ? (fm1_ms - sp.roll_ms) / 40u : 0u; }
static void sp_die(int32_t cx, int32_t cy, int32_t rad)
{
    uint32_t n = SP_ROLL_MS / 40u, f = sp_frame(), left = f < n ? n - f : 0u;
    uint32_t a = (left * left * 160u / (n * n)) & 63u;   /* (two and a half turns, slowing) */
    int32_t ys = sp.rolling ? 70 + SP_SIN[(f * 9u) & 63u] * 57 / 127 * (int32_t)left / (int32_t)n : 127;
    uint32_t face = sp.rolling && left > 2u ? (sp.face + f * 7u) % 12u + 1u : sp.face;
    draw_d12(cx, cy, rad, a, ys < 0 ? -ys : ys, face);
}

static void draw_spells(int32_t y0)
{
    const spell_t *S = &SPELL[sp_spell() % NSPELL];
    uint32_t pg = ui.page % NPERF - PG_SPELL, t, k, hk = ui.hot ? ui.hot - 1u : 4u;
    int pend = sp_pending(), roll = sp.rolling;
    const int32_t X = 74, W = 164;                   /* (the column beside the wizard) */
    char b[40];
    (void)y0;
    draw_wizard(2, 2, 3, sp_castable());
    if (sp_is_rcp()) {                               /* (a recipe: U3 and its spell) */
        put_str(b, "U", sizeof b);
        put_int(b + 1, (int32_t)(sp.sel - NSPELL + 1u));
        put_str(b + str_len(b), " ", sizeof b - str_len(b));
        put_str(b + str_len(b), S->name, sizeof b - str_len(b));
    } else {
        put_str(b, S->name, sizeof b);
    }
    cv_text_fit(X, 2, &AF_M, b, T_THEME, T_BG, W);
    if (roll || pend) {                              /* (the die; beside it what the cast changed) */
        sp_die(X + 22, 38, 21);
        if (roll) {
            cv_text(X + 50, 32, &AF_S, "ROLLING", T_ACCENT);
        } else {
            put_int(b, sp.nsnd);
            cv_text(X + 50, 24, &AF_S, b, T_TEXT);
            cv_text(X + 50 + text_w(&AF_S, b) + 4, 24, &AF_S, sp.nsnd == 1u ? "SOUND" : "SOUNDS", T_MID);
            put_int(b, sp.nstep);
            cv_text(X + 50, 40, &AF_S, b, T_TEXT);
            cv_text(X + 50 + text_w(&AF_S, b) + 4, 40, &AF_S, sp.nstep == 1u ? "STEP" : "STEPS", T_MID);
        }
    } else {
        uint32_t i = pg * 4u + hk;
        int knob = hk < 4u && i != SPV_SPELL;
        if (knob) {                                  /* (a knob turned: its name, what it does) */
            sp_card_name(b, sizeof b, i);
            cv_text_fit(X, 24, &AF_S, b, T_ACCENT, T_BG, W);
        } else {
            cv_text_fit(X, 24, &AF_S, S->line[0], T_TEXT, T_BG, W);
        }
        cv_text_fit(X, 40, &AF_S, knob ? sp_knob_line(i) : S->line[1], T_TEXT, T_BG, W);
    }
    for (t = 0; t < NTRACK; t++) {                   /* (the targets: lit, the black keys pick them) */
        int32_t x = X + (int32_t)t * 23;
        int on = (sp.tracks >> t) & 1u;
        uint16_t c = on ? TRACK_COL[t] : T_SURF;
        cv_rrect(x, 64, 22, 15, 3, c, T_BG);
        text_c(x, 64 + CAP_IN(S, 15), 22, &AF_S, VOICE_SHORT[t], on ? RGB(12, 12, 12) : T_DIM, c);
    }
    for (k = 0; k < 16u; k++) {                      /* (the white keys, sound over pattern: lit in, grey in but not this spell's, outlined out) */
        int32_t x = (int32_t)(k & 7u) * 30, y = k < 8u ? 92 : 110;
        int on = (sp.mask >> k) & 1u, used = ((S->cats | SC_LEN) >> k) & 1u;   /* (LEN: MOTION's, any spell) */
        uint16_t c = !on ? T_BG : used ? T_THEME : T_SURF;   /* (out: an outline) */
        cv_rrect(x + 1, y, 28, 15, 3, on ? c : T_SURF, T_BG);
        if (!on)
            cv_rrect(x + 2, y + 1, 26, 13, 2, T_BG, T_SURF);
        text_c(x + 1, y + CAP_IN(S, 15), 28, &AF_S, SC_SHORT[k], !on ? T_DIM : used ? RGB(12, 12, 12) : T_MID, c);
    }
}
static uint32_t spells_sig(void)
{
    uint32_t s = sig_add(16, sp.sel | (uint32_t)sp.tracks << 8 | (uint32_t)sp.mask << 16);
    s = sig_add(s, (uint32_t)sp.scope | (uint32_t)sp.mode << 8);
    s = sig_add(s, (uint32_t)sp_pending() | (uint32_t)sp.rolling << 1 | sp_frame() << 2 | (uint32_t)sp.face << 12 |
                       ui.page << 16 | (uint32_t)ui.hot << 24);
    s = sig_add(s, sp.nsnd | (uint32_t)sp.nstep << 16);
    s = sig_add(s, sp_is_rcp() ? rcp[sp.sel - NSPELL].spell : 0u);
    return s;
}
