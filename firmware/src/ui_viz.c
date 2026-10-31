/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 30: the pages' pictures (ui_draw.c includes this after its helpers). In place of a table of what the knobs
 * already show, a page draws what its settings do: the oscillator's wave, the envelope's shape and time, the filter's
 * curve, the LFO as it will move, the reverb's tail, the delay's echoes. Each is drawn from the parameters as the
 * engine takes them (its tables and curves: EG_ATK, eg_shape, dist_sample ..), in fixed point, and only when they
 * change (the body's signature): nothing moves on its own, so a picture costs no more than the table did.
 * The look: a panel with a sigil in its corner (8-bit, as the splash's potion), the curve in the theme colour over a
 * faint fill, a sparkle at the point that matters. Body coordinates: 240 x 128, drawn in two strips (cv_oy). */

#define VZ_X 4                                      /* the panel */
#define VZ_Y 2
#define VZ_W 232
#define VZ_H vz_h                                   /* (104 on a sound page, over the steps' strip; 122 elsewhere) */
#define VX0 12                                      /* the plot: x VX0 .. VX1 - 1, y VY0 .. VY1 */
#define VX1 228
#define VY0 24
#define VY1 (VZ_H - 6)
#define VMID ((VY0 + VY1) / 2)
#define VN (VX1 - VX0)
static int32_t vz_h = 104;
static int16_t vz_y[VN] __attribute__((section(".pool")));   /* a curve's y per column, 1/16 px */
static void card_value(char *b, uint32_t n, uint32_t p, uint32_t v);

static uint16_t vz_bg(void) { return T_SURF; }
static uint16_t vz_fill(void) { return ux_mix(T_SURF, T_THEME, 16); }
static uint16_t vz_faint(void) { return ux_mix(T_SURF, T_MID, 40); }
static uint16_t vz_star(void) { return ux_mix(T_THEME, RGB(255, 255, 255), 55); }
static uint32_t sv(uint32_t s, uint32_t h, uint32_t k)    /* the track's section s, half h, knob k: its value */
{
    uint32_t p = sect_par(ui.track, s, h, k);
    return p && p != PAR_NONE ? vp_get(p) : 0u;
}
static int32_t vz_sin(uint32_t ph) { return sine_at(ph + 0x40000000u); }   /* sin, Q15 (the LXR's table: -cos) */

/* a sparkle: a four-point star, bright at its heart */
static void vz_sparkle(int32_t x, int32_t y, int32_t r)
{
    uint16_t c = vz_star();
    cv_rect(x - r, y, 2 * r + 1, 1, ux_mix(T_SURF, c, 70));
    cv_rect(x, y - r, 1, 2 * r + 1, ux_mix(T_SURF, c, 70));
    cv_rect(x - 1, y - 1, 3, 3, c);
    cv_pset(x, y, RGB(255, 255, 255));
}

/* the panel: its caption, a faint field of stars behind */
static void vz_panel(const char *cap)
{
    static const uint8_t STARS[14][2] = {{30, 30}, {70, 52}, {118, 28}, {160, 66}, {204, 38}, {48, 84}, {96, 74},
                                         {142, 90}, {186, 86}, {222, 60}, {16, 58}, {132, 48}, {84, 40}, {176, 30}};
    uint32_t i;
    cv_rrect(VZ_X, VZ_Y, VZ_W, VZ_H, 6, vz_bg(), T_BG);
    if (ux.style == ST_LINE)
        cv_frame(VZ_X, VZ_Y, VZ_W, VZ_H, T_RULE);
    for (i = 0; i < 14u; i++)
        cv_pset(STARS[i][0], STARS[i][1], ux_mix(T_SURF, T_TEXT, 14));
    if (cap && cap[0])
        cv_text_fit(VX0, VZ_Y + 4, &AF_S, cap, T_MID, vz_bg(), VX1 - VX0);
}
/* a dashed guide line across the plot */
static void vz_hguide(int32_t y, uint16_t c)
{
    int32_t x;
    for (x = VX0; x < VX1; x += 4)
        cv_rect(x, y, 2, 1, c);
}

/* the curve in vz_y (n columns from x0): its fill down (or up) to base, then a 2 px trace, softened above and below */
static void vz_trace(int32_t x0, int32_t n, int32_t base, uint16_t c, uint16_t fill)
{
    int32_t i;
    uint16_t soft = ux_mix(vz_bg(), c, 35);
    if (fill != vz_bg())
        for (i = 0; i < n; i++) {
            int32_t y = (vz_y[i] + 8) >> 4;
            if (y < base)
                cv_rect(x0 + i, y, 1, base - y, fill);
            else if (y > base)
                cv_rect(x0 + i, base, 1, y - base, fill);
        }
    for (i = 0; i < n; i++) {
        int32_t a = vz_y[i], b = i + 1 < n ? vz_y[i + 1] : vz_y[i];
        int32_t lo = (a < b ? a : b) >> 4, hi = ((a > b ? a : b) + 15) >> 4;
        if (hi < lo + 1)
            hi = lo + 1;
        cv_rect(x0 + i, lo - 1, 1, 1, soft);
        cv_rect(x0 + i, lo, 1, hi - lo + 1, c);
        cv_rect(x0 + i, hi + 1, 1, 1, soft);
    }
}
/* y of a Q15 sample on a band centred at mid, half-height h, 1/16 px */
static int16_t vz_yq15(int32_t v, int32_t mid, int32_t h)
{
    v = clamp32(v, -32768, 32767);
    return (int16_t)(mid * 16 - ((v * h) >> 11));
}

/* an oscillator's wave at phase ph: SINE, TRI, SAW, SQUARE, NOISE (a fixed draw), PWM (width pw 0..127); Q15 */
static int32_t vz_wave(uint32_t w, uint32_t ph, uint32_t pw)
{
    switch (w) {
    case 0: return vz_sin(ph);
    case 1: { int32_t t = (int32_t)(ph >> 15); return t < 65536 ? t - 32768 : 98303 - t; }
    case 2: return (int32_t)(ph >> 16) - 32768;
    case 3: return ph < 0x80000000u ? 30000 : -30000;
    case 4: {
        uint32_t h = (ph >> 25) * 0x9E3779B1u;     /* (128 values a cycle) */
        h ^= h >> 15;
        return (int32_t)((h >> 8) & 0xFFFFu) - 32768;
    }
    default: return ph < 0x80000000u + (pw - 64u) * 0x00FC0000u ? 30000 : -30000;
    }
}

/* ---------------------------------------------------- the MODEL page --- */
/* the picture's box, right of the text: what the model is, drawn simply, moved by its knobs */
#define IX 156
#define IW 66
#define IY0 30
#define IY1 94
#define ICY 62

static int32_t ic_noise(uint32_t i)                   /* a fixed draw, Q15 */
{
    uint32_t h = (i + 1u) * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return (int32_t)((h >> 8) & 0xFFFFu) - 32768;
}
/* an ellipse's outline (32 chords); a dashed one (every other chord) when dash */
static void ic_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c, int dash)
{
    uint32_t k;
    int32_t px = cx + rx, py = cy;
    for (k = 1; k <= 32u; k++) {
        uint32_t ph = k * 0x08000000u;
        int32_t x = cx + ((vz_sin(ph + 0x40000000u) * rx) >> 15), y = cy + ((vz_sin(ph) * ry) >> 15);
        if (!dash || (k & 1u))
            cv_line(px, py, x, y, c);
        px = x;
        py = y;
    }
}
static void ic_disc(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)   /* a filled ellipse */
{
    int32_t y;
    for (y = -ry; y <= ry; y++) {
        int32_t w = 0;
        while (w < rx && (w + 1) * (w + 1) * ry * ry + y * y * rx * rx <= rx * rx * ry * ry)
            w++;
        cv_rect(cx - w, cy + y, 2 * w + 1, 1, c);
    }
}
/* the wave pictures: column i (0 .. IW - 1) of model m, its knobs a b c (0..127), Q15 */
static int32_t ic_wave(uint32_t m, uint32_t i, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t ph = i * (0xFFFFFFFFu / IW);             /* (one pass of the box) */
    int32_t env = vz_sin(i * (0x7FFFFFFFu / IW));       /* (a hump over the box) */
    switch (m) {
    case 4: {                                          /* P VA: a saw into a square, by MORPH */
        uint32_t p = ph * 2u;
        int32_t saw = (int32_t)(p >> 16) - 32768, sq = p < 0x80000000u ? 26000 : -26000;
        return (saw * 3 / 4 * (127 - (int32_t)c) + sq * (int32_t)c) / 127;
    }
    case 5: {                                          /* P WSHAPE: a sine folded, more by TIMBRE */
        int32_t x = vz_sin(ph + ph / 2u) * (32 + (int32_t)b * 3 / 4) / 32;   /* (a cycle and a half) */
        while (x > 32767 || x < -32768)
            x = x > 32767 ? 65534 - x : -65536 - x;
        return x;
    }
    case 3: case 6: {                                  /* FM: a sine bent by another, deeper with TIMBRE / BRIGHT */
        int32_t mod = vz_sin(ph * 2u * (1u + a / 43u)) * (int32_t)b / 160;
        return vz_sin(ph * 2u + (uint32_t)(mod << 15));
    }
    case 7: {                                          /* P GRAIN: formant bursts */
        uint32_t seg = i * 3u / IW, j = i - seg * IW / 3u;
        int32_t w = vz_sin(j * (0x7FFFFFFFu / (IW / 3u)));
        return (int32_t)(((int64_t)vz_sin(j * (0xFFFFFFFFu / (IW / 3u)) * (2u + b / 24u)) * w) >> 15);
    }
    case 13: {                                         /* P NOISE: smoother the lower TIMBRE */
        int32_t s = 0, k, n = 1 + (127 - (int32_t)b) / 24;
        for (k = 0; k < n; k++)
            s += ic_noise((i + (uint32_t)k) / (uint32_t)n * 7u + (uint32_t)k);
        return s / n * 3 / 4;
    }
    case 20: {                                         /* MICRO CLICK: one pulse, wider with LENGTH */
        int32_t w = 2 + (int32_t)a / 10;
        return (int32_t)i >= 20 && (int32_t)i < 20 + w ? 28000 : 0;
    }
    case 21: {                                         /* TRAIN: a run of clicks, more with LENGTH */
        uint32_t n = 3u + a / 24u;
        return i % (IW / n) < 2u ? 28000 - (int32_t)i * 200 : 0;
    }
    case 22: {                                         /* CRUNCH: a held, stepped noise */
        uint32_t st = 2u + (127u - b) / 16u;
        return ic_noise(i / st) / 4 * 3 * env >> 15;
    }
    case 23:                                           /* BITS: an LFSR's ones and zeros */
        return (ic_noise(i / 5u) & 0x100) ? 22000 : -22000;
    case 24: {                                         /* BURST: noise, rising and falling */
        int32_t e = (int32_t)i < 8 ? (int32_t)i * 4096 : 32767 - ((int32_t)i - 8) * 32767 / (IW - 8) * (160 - (int32_t)a) / 160;
        return (int32_t)(((int64_t)ic_noise(i) * (e > 0 ? e : 0)) >> 15);
    }
    case 25: {                                         /* SNAP: a click into a short noise */
        int32_t e = 32767 - (int32_t)i * 32767 / (8 + (int32_t)a / 4);
        return i < 3u ? 30000 : (int32_t)(((int64_t)ic_noise(i) * (e > 0 ? e : 0)) >> 15);
    }
    case 26:                                           /* GRAIN: a sine in a window */
        return (int32_t)(((int64_t)vz_sin(ph * (3u + b / 20u)) * env) >> 15);
    case 27:                                           /* DUST: a crackle, now and then */
        return (ic_noise(i * 3u) & 0x7000) == 0x7000 ? ic_noise(i) : 0;
    case 28: {                                         /* POP: down in pitch, then back */
        uint32_t p = i * i * (0xFFFFFFFFu / (IW * IW)) * 3u + ph * 2u;
        int32_t e = 32767 - (int32_t)i * 400;
        return (int32_t)(((int64_t)vz_sin(p) * (e > 4000 ? e : 4000)) >> 15);
    }
    case 30: {                                         /* PULSAR: pulsarets closer and closer, narrower with COLOUR */
        uint32_t p = i * i * (0xFFFFFFFFu / (IW * IW)) * 7u, k = 1u + b / 16u;
        return p < 0xFFFFFFFFu / k ? vz_sin(p * k) * 7 / 8 : 0;
    }
    case 31: {                                         /* WAVELET: a few cycles in a window, more with LENGTH */
        int32_t w = (32767 - vz_sin(ph + 0x40000000u)) / 2;
        return (int32_t)(((int64_t)vz_sin(ph * (1u + a / 8u)) * w) >> 15);
    }
    case 32: {                                         /* SPLINTER: a spike, then smaller ones closer and closer */
        int32_t x = 4, g = IW / 3, h = 30000, k, n = 3 + (int32_t)b / 13;
        for (k = 0; k < n && x < IW; k++) {
            if ((int32_t)i >= x && (int32_t)i < x + (k ? 1 : 3))
                return (k & 1) ? -h : h;
            x += g;
            g = g * 2 / 3 > 2 ? g * 2 / 3 : 2;
            h = h * 3 / 4;
        }
        return 0;
    }
    default: {                                         /* (29) CHIRP: a sine swept up */
        uint32_t p = i * i * (0xFFFFFFFFu / (IW * IW)) * (4u + b / 16u);
        return vz_sin(p) * 7 / 8;
    }
    }
}
/* a drum: from the side (SN, P SNARE), or its head (the kicks); a cymbal; the hats */
static void ic_drum(int side, uint32_t a)
{
    int32_t k;
    uint16_t c = T_THEME, f = ux_mix(vz_bg(), T_THEME, 22);
    if (!side) {                                       /* (the kick's front head, its lugs round it) */
        int32_t r = 22 + (int32_t)a / 32;
        ic_disc(IX + IW / 2, ICY, r, r, f);
        ic_ellipse(IX + IW / 2, ICY, r, r, c, 0);
        ic_ellipse(IX + IW / 2, ICY, r - 4, r - 4, ux_mix(vz_bg(), T_THEME, 60), 0);
        ic_ellipse(IX + IW / 2, ICY, 7, 7, ux_mix(vz_bg(), T_THEME, 45), 0);
        for (k = 0; k < 8; k++) {
            uint32_t ph = (uint32_t)k * 0x20000000u;
            int32_t x = IX + IW / 2 + ((vz_sin(ph + 0x40000000u) * (r + 2)) >> 15);
            int32_t y = ICY + ((vz_sin(ph) * (r + 2)) >> 15);
            cv_rect(x - 1, y - 1, 3, 3, c);
        }
        return;
    }
    cv_rect(IX + 4, ICY - 10, IW - 8, 24, f);          /* (the shell, side on: top, sides, bottom, the lugs) */
    ic_disc(IX + IW / 2, ICY - 10, IW / 2 - 4, 7, ux_mix(vz_bg(), T_THEME, 35));
    ic_ellipse(IX + IW / 2, ICY - 10, IW / 2 - 4, 7, c, 0);
    cv_rect(IX + 4, ICY - 10, 1, 24, c);
    cv_rect(IX + IW - 4, ICY - 10, 1, 24, c);
    for (k = 0; k <= 16; k++) {                        /* (the bottom: the near half of the ellipse) */
        int32_t x = IX + 4 + k * (IW - 8) / 16;
        int32_t dx = x - (IX + IW / 2), rx = IW / 2 - 4, y = ICY + 14 + (int32_t)(7 * (rx * rx - dx * dx) / (rx * rx));
        cv_rect(x, y, (IW - 8) / 16 + 1, 1, c);
    }
    for (k = 0; k < 5; k++)
        cv_rect(IX + 12 + k * (IW - 24) / 4, ICY - 2, 2, 10, ux_mix(vz_bg(), T_THEME, 60));
    for (k = 0; k < IW - 16; k += 3)                   /* (the snare wires under it) */
        cv_pset(IX + 8 + k, ICY + 26 + (k & 1), T_MID);
}
static void ic_cymbal(int hats, uint32_t a)
{
    uint16_t c = T_THEME, f = ux_mix(vz_bg(), T_THEME, 30);
    int32_t top = hats ? ICY - 12 : ICY - 16, gap = hats ? 3 + (int32_t)a / 32 : 0;
    cv_rect(IX + IW / 2, top, 1, IY1 - top, T_MID);    /* (the stand and its feet) */
    cv_line(IX + IW / 2, IY1, IX + IW / 2 - 14, IY1 + 6, T_MID);
    cv_line(IX + IW / 2, IY1, IX + IW / 2 + 14, IY1 + 6, T_MID);
    ic_disc(IX + IW / 2, top, IW / 2 - 2, 5, f);
    ic_ellipse(IX + IW / 2, top, IW / 2 - 2, 5, c, 0);
    ic_disc(IX + IW / 2, top - 3, 7, 3, c);
    if (hats) {
        ic_disc(IX + IW / 2, top + 6 + gap, IW / 2 - 2, 5, f);
        ic_ellipse(IX + IW / 2, top + 6 + gap, IW / 2 - 2, 5, c, 0);
    } else {
        vz_sparkle(IX + IW - 10, top - 2, 3);
    }
}
static void ic_bell(uint32_t a)
{
    static const uint8_t W[12] = {6, 9, 11, 12, 13, 14, 15, 17, 19, 22, 26, 29};   /* (half-width down the bell) */
    int32_t k, cx = IX + IW / 2, y0 = IY0 + 2;
    uint16_t c = T_THEME;
    for (k = 0; k < 12; k++)
        cv_rect(cx - W[k], y0 + k * 4, 2 * W[k] + 1, 4, ux_mix(vz_bg(), T_THEME, 18 + k * 2));
    for (k = 0; k < 11; k++) {
        cv_line(cx - W[k], y0 + k * 4, cx - W[k + 1], y0 + k * 4 + 4, c);
        cv_line(cx + W[k], y0 + k * 4, cx + W[k + 1], y0 + k * 4 + 4, c);
    }
    cv_rect(cx - W[0], y0, 2 * W[0] + 1, 1, c);
    ic_ellipse(cx, y0 + 46, W[11], 4, c, 0);
    cv_rect(cx - 1, y0 - 5, 3, 5, c);
    ic_disc(cx, y0 + 52, 4, 4, T_MID);                 /* (the clapper) */
    for (k = 1; k <= 1 + (int32_t)a / 48; k++)         /* (it rings: arcs, longer the more DECAY) */
        ic_ellipse(cx, y0 + 26, W[11] + 3 + k * 4, 20 + k * 4, ux_mix(vz_bg(), T_THEME, 40 - k * 10), 1);
}
static void ic_string(uint32_t a, uint32_t b)
{
    int32_t i, amp = 4 + (int32_t)a / 9;
    cv_rect(IX, ICY - 4, 3, 9, T_MID);
    cv_rect(IX + IW - 3, ICY - 4, 3, 9, T_MID);
    for (i = 0; i < IW - 6; i++) {
        int32_t s = vz_sin((uint32_t)i * (0x7FFFFFFFu / (IW - 6)) * (1u + b / 64u));
        cv_pset(IX + 3 + i, ICY + ((s * amp) >> 15), ux_mix(vz_bg(), T_THEME, 40));
        vz_y[i] = (int16_t)(ICY * 16 - ((s * amp * 16) >> 15));
    }
    vz_trace(IX + 3, IW - 6, ICY, T_THEME, vz_bg());
}
static void ic_chord(uint32_t a)
{
    static const int8_t CH[4][4] = {{0, 2, 4, 6}, {0, 2, 4, 7}, {0, 3, 5, 7}, {0, 2, 5, 8}};   /* (staff steps) */
    int32_t k, y0 = ICY - 12;
    for (k = 0; k < 5; k++)
        cv_rect(IX, y0 + k * 6, IW, 1, vz_faint());
    for (k = 0; k < 4; k++) {
        int32_t y = y0 + 27 - CH[a >> 5 & 3u][k] * 3, x = IX + 22 + (k & 1) * 9;
        ic_disc(x, y, 4, 3, T_THEME);
    }
    cv_rect(IX + 35, y0 + 27 - CH[a >> 5 & 3u][3] * 3 - 22, 1, 24, T_THEME);
}
/* 37: P SPEECH: a wizard's head (the hat, its stars, the brows, the beard); the lips move as any knob turns (a new
 * shape at each step; it says nothing about the values), words coming out when they're open */
static void ic_wizard(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t mo = (a * 3u + b * 5u + c * 7u) % 5u;     /* the mouth: 0 shut .. 4 wide */
    int32_t cx = IX + 33, y, k;
    uint16_t hat = ux_mix(vz_bg(), T_THEME, 85), face = ux_mix(vz_bg(), T_TEXT, 62);
    uint16_t beard = ux_mix(vz_bg(), T_TEXT, 92), dark = vz_bg();
    for (y = 0; y < 26; y++) {                        /* the hat: a cone, its tip bent over to the right */
        int32_t hw = 2 + y * 19 / 26, sx = (25 - y) * (25 - y) / 50;
        cv_rect(cx - hw + sx, 22 + y, 2 * hw + 1, 1, hat);
    }
    cv_rrect(cx - 27, 46, 55, 6, 3, hat, vz_bg());    /* .. its brim */
    vz_sparkle(cx - 4, 36, 2);
    vz_sparkle(cx + 7, 28, 1);
    ic_disc(cx, 61, 15, 10, face);                    /* the face */
    for (y = 0; y < 34; y++) {                        /* the beard: wide at the cheeks, to a point */
        int32_t hw = 17 - y * y / 70 + (y & 3) / 2;
        if (hw < 1)
            hw = 1;
        cv_rect(cx - hw, 63 + y, 2 * hw + 1, 1, beard);
    }
    cv_rect(cx - 10, 55, 8, 2, beard);                /* the brows, the eyes */
    cv_rect(cx + 3, 55, 8, 2, beard);
    cv_rect(cx - 7, 58, 3, 3, dark);
    cv_rect(cx + 5, 58, 3, 3, dark);
    cv_rect(cx - 1, 60, 3, 4, ux_mix(face, T_THEME, 25));   /* the nose */
    for (k = 0; k < 9; k++) {                         /* the moustache, over the lips */
        cv_rect(cx - 2 - k, 65 + k / 3, 2, 2, ux_mix(beard, T_TEXT, 30));
        cv_rect(cx + 1 + k, 65 + k / 3, 2, 2, ux_mix(beard, T_TEXT, 30));
    }
    if (mo) {                                         /* the lips: open by mo, words out to the right */
        ic_disc(cx, 70, 3 + (int32_t)mo, (int32_t)mo, dark);
        for (k = 1; k <= (int32_t)mo / 2 + 1; k++)
            ic_ellipse(cx + 18 + k * 6, 70, 2, 4 + k * 2, ux_mix(vz_bg(), T_THEME, 80 - k * 18), 1);
    } else {
        cv_rect(cx - 4, 70, 9, 1, dark);
    }
}
static void ic_particles(uint32_t c, int ring)
{
    uint32_t k, n = 8u + c / 3u;
    if (ring)
        ic_ellipse(IX + IW / 2, ICY, IW / 2 - 4, 26, ux_mix(vz_bg(), T_THEME, 30), 1);
    for (k = 0; k < n; k++) {
        int32_t x = IX + 4 + (int32_t)(((uint32_t)ic_noise(k * 2u) & 0xFFFFu) * (IW - 8) >> 16);
        int32_t y = IY0 + 4 + (int32_t)(((uint32_t)ic_noise(k * 2u + 1u) & 0xFFFFu) * (IY1 - IY0 - 8) >> 16);
        uint16_t col = ux_mix(vz_bg(), T_THEME, 40 + (int32_t)(k * 37u % 60u));
        cv_rect(x, y, 2, 2, col);
        if (k % 7u == 0u)
            vz_sparkle(x, y, 2);
    }
}
static void ic_bars(uint32_t b)                        /* P ADDTV: its harmonics, brighter with TIMBRE */
{
    int32_t k;
    for (k = 0; k < 12; k++) {
        int32_t h = 56 - k * (int32_t)(140u - b) * 56 / (140 * 12);
        h = h * (k & 1 ? 3 : 4) / 4;
        h = h < 2 ? 2 : h;
        cv_rect(IX + 2 + k * 5, IY1 - h, 3, h, k & 1 ? ux_mix(vz_bg(), T_THEME, 60) : T_THEME);
    }
}
static void ic_table(uint32_t b, uint32_t c)           /* P WTABLE: its waves stacked, the row TIMBRE picks lit */
{
    int32_t r, i, row = (int32_t)b * 5 / 128;
    for (r = 4; r >= 0; r--) {
        int32_t x0 = IX + r * 4, y0 = ICY + 14 - r * 7, px = 0, py = 0;
        uint16_t col = r == row ? T_THEME : ux_mix(vz_bg(), T_THEME, 35);
        for (i = 0; i < IW - 18; i++) {
            uint32_t ph = (uint32_t)i * (0xFFFFFFFFu / (IW - 18));
            int32_t s = vz_sin(ph), sq = ph < 0x80000000u ? 32767 : -32767, w;
            w = (s * (4 - r) + ((int32_t)(ph >> 16) - 32768) * r) / 4;   /* (sine to saw down the table) */
            w = (w * (127 - (int32_t)c / 2) + sq / 2 * ((int32_t)c / 2)) / 127;   /* (MORPH: squarer) */
            if (i)
                cv_line(px, py, x0 + i, y0 - ((w * 10) >> 15), col);
            px = x0 + i;
            py = y0 - ((w * 10) >> 15);
        }
    }
}
static void ic_swarm(uint32_t b)                       /* P SWARM: saws drifting apart, further with TIMBRE */
{
    int32_t k, i;
    for (k = 3; k >= 0; k--) {
        int32_t px = 0, py = 0;
        uint16_t c = k ? ux_mix(vz_bg(), T_THEME, 70 - k * 15) : T_THEME;
        for (i = 0; i < IW; i++) {
            uint32_t ph = (uint32_t)i * (0xFFFFFFFFu / IW) * 2u + (uint32_t)k * (b + 10u) * 0x00600000u;
            int32_t y = ICY - (((int32_t)(ph >> 16) - 32768) * 20 >> 15) + (k - 2) * 4;
            if (i)
                cv_line(px, py, IX + i, y, c);
            px = IX + i;
            py = y;
        }
    }
}
/* the model's picture: m the model, v the voice, a b c its first three knobs */
static void ic_model(uint32_t m, uint32_t v, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t i;
    switch (m) {
    case 0:
        if (v <= 2u)
            ic_drum(0, 64);
        else if (v == 3u)
            ic_drum(1, 64);
        else
            ic_cymbal(v == 5u, 0);
        return;
    case 1: case 16: ic_bell(a); return;
    case 2: case 15: ic_string(a, b); return;
    case 8: ic_bars(b); return;
    case 9: ic_table(b, c); return;
    case 10: ic_chord(a); return;
    case 11: ic_wizard(a, b, c); return;
    case 12: ic_swarm(b); return;
    case 14: ic_particles(c, 1); return;
    case 17: ic_drum(0, a); return;
    case 18: ic_drum(1, a); return;
    case 19: ic_cymbal(1, a); return;
    default: break;
    }
    for (i = 0; i < IW; i++)                           /* (the rest: a wave) */
        vz_y[i] = vz_yq15(ic_wave(m, i, a, b, c), ICY, 26);
    if (m >= 20u)
        cv_rect(IX, ICY, IW, 1, vz_faint());
    vz_trace(IX, IW, ICY, T_THEME, m >= 20u ? vz_bg() : vz_fill());
}

/* text in w px from (x, y), broken at spaces, at most n lines h px apart (the last cut short); the lines it took */
static int32_t vz_wrap(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t n, const char *s, uint16_t fg)
{
    char ln[64];
    uint32_t used = 0;
    while (*s && used < n) {
        uint32_t k = 0, cut = 0;
        while (s[k] && k < sizeof ln - 1u) {         /* (the longest run that fits, cut at its last space) */
            ln[k] = s[k];
            ln[k + 1u] = 0;
            if (text_w(&AF_S, ln) > w)
                break;
            k++;
            if (!s[k] || s[k] == ' ')
                cut = k;
        }
        if (!cut)
            cut = k;
        ln[cut] = 0;
        if (used + 1u == n && s[cut])
            cv_text_fit(x, y, &AF_S, s, fg, vz_bg(), w);
        else
            cv_text(x, y, &AF_S, ln, fg);
        s += cut;
        while (*s == ' ')
            s++;
        y += h;
        used++;
    }
    return (int32_t)used;
}

/* what the voice is: a line for each model and for the LXR's own voices */
static const char *model_line(uint32_t m, uint32_t v)
{
    static const char *const D[NMODEL] = {
        "", "A struck resonator: bells, bars and plates", "A plucked or struck string", "Glassy, metallic FM",
        "Two analog oscillators", "A wave folded and shaped", "Two-operator FM", "Granular formants",
        "Additive: a sum of harmonics", "Wavetables, row by column", "A four-note chord", "A speaking voice",
        "A swarm of eight saws", "Filtered noise", "Dust ringing through resonators", "An inharmonic string",
        "A modal resonator: a bell or a bar", "An analog kick", "An analog snare", "An analog hat",
        "A square click, or a doublet", "A train of clicks", "Crunchy, held noise", "An LFSR's bits",
        "A burst of noise", "A snap: noise and a click", "A resonant grain", "A crackle of dust",
        "A pop that slides back", "A sine swept four octaves", "Pulses speeding up into a pitch",
        "A few cycles of a sine in a window", "An impulse splintering apart"};
    static const char *const LXR[4] = {"The LXR's drum: OSC, FM, a transient",
                                       "The LXR's snare: OSC, noise, a transient", "The LXR's cymbal: 3 OSCs in FM",
                                       "The LXR's hats: 3 OSCs in FM"};
    if (m && m < NMODEL)
        return D[m];
    return LXR[v <= 2u ? 0 : v == 3u ? 1 : v == 4u ? 2 : 3];
}
static void viz_model(void)
{
    uint32_t v = track_voice(ui.track), m = sv(S_MODEL, 0, 0), a = 0, b = 0, c = 0;
    int over = v <= 2u && mdl[v].want && !mdl[v].type;   /* 0.15: over the models' limit (rings.c model_limit) */
    m = m < NMODEL ? m : 0u;
    vz_panel(over ? "" : m ? (m <= 3u ? "RINGS RESONATOR" : m <= 19u ? "PLAITS ENGINE" : "MICROSOUND")
                           : "THE LXR-02'S VOICE");
    if (over)
        cv_text_fit(VX0, VZ_Y + 4, &AF_S, model_cap == 1u ? "1 MODEL ON ONE CORE: PLAYS AS LXR"
                                                          : "OVER 2 MODELS: PLAYS AS LXR", T_REC, vz_bg(), VX1 - VX0);
    if (m) {
        a = sv(S_MODEL, 0, 1);
        b = sv(S_MODEL, 0, 2);
        c = sv(S_MODEL, 0, 3);
    }
    cv_text(VX0, 22, &AF_M, m ? L_MODEL[m] : "LXR", T_THEME);
    vz_wrap(VX0, 44, IX - VX0 - 12, 14, 3u, model_line(m, v), T_TEXT);
    ic_model(m, v, a, b, c);
}

/* OSCILLATOR: the wave, more cycles the higher the pitch; the snare's noise mixed in */
static void viz_osc(void)
{
    uint32_t v = track_voice(ui.track), sn = v == 3u, i, w, pw, coarse, mix = 0, nf = 0, step, nph = 0;
    coarse = sv(S_OSC, 0, 0);
    if (sn) {
        nf = sv(S_OSC, 0, 2);
        mix = sv(S_OSC, 0, 3);
        w = sv(S_OSC, 1, 0);
        pw = sv(S_OSC, 1, 1);
    } else {
        w = sv(S_OSC, 0, 2);
        pw = sv(S_OSC, 0, 3);
    }
    step = (uint32_t)(((uint64_t)(uint32_t)exp2_q16((int32_t)(coarse * 65536u / 36u)) << 16) / VN);   /* (1 .. 11 cycles) */
    vz_panel(sn ? (mix ? (mix > 100u ? "MOSTLY NOISE" : "THE WAVE AND NOISE") : "THE WAVE, NO NOISE")
                : w < 6u ? L_WAVE[w] : "");
    if (sn)
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, w < 6u ? L_WAVE[w] : "", T_MID, vz_bg());
    vz_hguide(VMID, vz_faint());
    for (i = 0; i < VN; i++) {
        int32_t y = vz_wave(w, i * step, pw);
        if (sn) {                                    /* (MIX: the noise's share; its rate the NOISE knob) */
            int32_t n;
            uint32_t h;
            nph += 0x1000000u + nf * 0x200000u;
            h = (nph >> 24) * 0x9E3779B1u + 0x51u;
            h ^= h >> 13;
            n = (int32_t)((h >> 8) & 0xFFFFu) - 32768;
            y = ((y * (127 - (int32_t)mix) + n * (int32_t)mix) * 258) >> 15;   /* (/ 127) */
        }
        vz_y[i] = vz_yq15(y, VMID, 32);
    }
    vz_trace(VX0, VN, VMID, T_THEME, vz_fill());
    vz_sparkle(VX0 + 3, vz_y[0] >> 4, 3);
}

/* FM: the carrier with the modulator's phase pushed in (D1-D3: one modulator, or MIX: the two added; CY, HH: two) */
static void viz_fm(void)
{
    uint32_t v = track_voice(ui.track), i, cw, mw, mw2 = 0, mix = 0;
    int32_t amt, amt2 = 0, r1, r2 = 0, car;
    char b[40];
    car = (int32_t)sv(S_OSC, 0, 0);
    if (v <= 2u) {
        amt = (int32_t)sv(S_FM, 0, 0);
        r1 = (int32_t)sv(S_FM, 0, 1) - car;
        mw = sv(S_FM, 0, 2);
        mix = sv(S_FM, 0, 3);
        cw = sv(S_OSC, 0, 2);
    } else {
        r1 = (int32_t)sv(S_FM, 0, 0) - car;
        r2 = (int32_t)sv(S_FM, 0, 1) - car;
        amt = (int32_t)sv(S_FM, 0, 2);
        amt2 = (int32_t)sv(S_FM, 0, 3);
        mw = sv(S_FM, 1, 0);
        mw2 = sv(S_FM, 1, 1);
        cw = sv(S_OSC, 0, 2);
    }
    put_str(b, mix ? "MIX: THE TWO ADDED" : v <= 2u ? "FM: THE WAVE BENT" : "METAL: 3 OSCILLATORS",
            sizeof b);
    vz_panel(b);
    vz_hguide(VMID, vz_faint());
    {                                                /* (the ratio, drawn at x8 at most) */
        int32_t q = exp2_q16(clamp32(r1, -48, 60) * 65536 / 12);
        put_str(b, "RATIO ", sizeof b);
        if (q >= 65536) {
            put_int(b + 6, (q + 32768) >> 16);
            put_str(b + str_len(b), ":1", sizeof b - str_len(b));
        } else {
            put_str(b + 6, "1:", sizeof b - 6u);
            put_int(b + str_len(b), (int32_t)((65536 * 2 / (q ? q : 1) + 1) / 2));
        }
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
    }
    r1 = exp2_q16(clamp32(r1, -36, 36) * 65536 / 12);    /* (the ratios, Q16) */
    r2 = exp2_q16(clamp32(r2, -36, 36) * 65536 / 12);
    for (i = 0; i < VN; i++) {                       /* (two carrier cycles) */
        uint32_t ph = i * (0x80000000u / VN) * 4u;
        int32_t m = vz_wave(mw, (uint32_t)(((uint64_t)ph * (uint32_t)r1) >> 16), 64), y;
        if (mix) {
            y = (vz_wave(cw, ph, 64) + (m * amt) / 127) / 2;
        } else {
            int32_t d = (int32_t)(((int64_t)m * amt) >> 8);   /* (up to a half turn either way) */
            if (v > 2u)
                d += (int32_t)(((int64_t)vz_wave(mw2, (uint32_t)(((uint64_t)ph * (uint32_t)r2) >> 16), 64) * amt2) >> 8);
            y = vz_wave(cw, ph + (uint32_t)(d << 16), 64);
        }
        vz_y[i] = vz_yq15(m, VMID, 10);
        cv_pset(VX0 + (int32_t)i, vz_y[i] >> 4, vz_faint());   /* (the modulator, faint behind) */
        vz_y[i] = vz_yq15(y, VMID, 32);
    }
    vz_trace(VX0, VN, VMID, T_THEME, vz_bg());
}

/* TRANSIENT: the sample as it plays (its pitch shortens it), SNAP's pitch blip, OFFSET's start */
static void viz_trans(void)
{
    uint32_t w = sv(S_CLICK, 0, 0), vol = sv(S_CLICK, 0, 1), fr = sv(S_CLICK, 0, 2), i;
    uint32_t inc = 262144u + (((fr & 127u) << 20) * 10u) / 339u;   /* (sound.c: x0.25 .. x4) */
    char b[40];
    put_str(b, w < sizeof L_TRANS / sizeof L_TRANS[0] ? L_TRANS[w] : "", sizeof b);
    vz_panel(b);
    if (w == 1u) {                                   /* OFFSET: the wave starts on, not from 0 */
        for (i = 0; i < VN; i++)
            vz_y[i] = vz_yq15(vz_sin((uint32_t)(i * (0x80000000u / VN) * 3u) + (uint32_t)(vol << 24)), VMID, 32);
        vz_hguide(VMID, vz_faint());
        vz_trace(VX0, VN, VMID, T_THEME, vz_fill());
        vz_sparkle(VX0 + 2, vz_y[0] >> 4, 3);
        cv_text(VX0, VZ_Y + 4 + 14, &AF_S, "THE WAVE STARTS FURTHER ON", T_MID);
        return;
    }
    if (w == 0u) {                                   /* SNAP: a pitch blip (snap_calc: down 0.2 x pitch a block, ^2) */
        for (i = 0; i < VN; i++) {                   /* (over 30 ms) */
            int64_t blk = (int64_t)i * 30 * FS / (1000 * BLOCK);   /* (x VN) */
            int32_t v = 65536 - (int32_t)(blk * 13107 * (int64_t)(inc >> 4) / ((int64_t)VN << 16));
            v = v < 0 ? 0 : v;
            v = (int32_t)(((int64_t)v * v) >> 16) * (int32_t)vol / 127;
            vz_y[i] = (int16_t)((VY1 - 2) * 16 - (int32_t)(((int64_t)v * (VY1 - VY0 - 8) * 16) >> 16));
        }
        vz_hguide(VY1 - 2, vz_faint());
        vz_trace(VX0, VN, VY1 - 2, T_THEME, vz_fill());
        vz_sparkle(VX0 + 2, vz_y[0] >> 4, 3);
        cv_text(VX0, VZ_Y + 4 + 14, &AF_S, "A PITCH BLIP AT THE HIT", T_MID);
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, "30 ms", T_MID, vz_bg());
        return;
    }
    {
        const int8_t *d = LXR_TRANS[(w - 2u) % 12u];
        uint32_t len = 2204u;
        uint64_t span;
        while (len > 64u && iabs(d[len - 1u]) < 3)   /* (the sample's sound: its silent end off) */
            len--;
        span = ((uint64_t)len * 9u / 8u) << 20;      /* (the plot: the sound at x1, a little after) */
        vz_hguide(VMID, vz_faint());
        for (i = 0; i < VN; i++) {                   /* (each column: the samples' peak, as a DAW draws them) */
            uint64_t a = span * i / VN, z = span * (i + 1u) / VN;
            uint32_t s0 = (uint32_t)(a >> 20), s1 = (uint32_t)(z >> 20), s, mx = 0;
            for (s = s0; s <= s1 && s < 2204u; s++)
                mx = (uint32_t)iabs(d[s]) > mx ? (uint32_t)iabs(d[s]) : mx;
            mx = mx * vol / 127u;
            if (mx)
                cv_rect(VX0 + (int32_t)i, VMID - (int32_t)(mx * 32u / 128u), 1, 1 + (int32_t)(mx * 64u / 128u),
                        mx > 40u ? T_THEME : ux_mix(vz_bg(), T_THEME, 60));
        }
        put_int(b, (int32_t)((len << 20) / inc * 10u / 441u));   /* (2204 << 20: in 32 bits) */
        put_str(b + str_len(b), " ms AT THIS PITCH", sizeof b - str_len(b));
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
    }
}

/* the envelope: attack (eg_shape with ik), decay (k), the clap's repeats; its time in ms. gate: a held part */
/* n / d as Q24, 0 <= n <= d, in 32 bits (the chip's library has no 64-bit divide) */
static int32_t vz_frac24(int32_t n, int32_t d)
{
    uint32_t un = n > 0 ? (uint32_t)n : 0u, ud = d > 1 ? (uint32_t)d : 1u;
    while (ud >= 1u << 15) {
        un >>= 1;
        ud >>= 1;
    }
    un = un > ud ? ud : un;
    return (int32_t)(((un << 16) / ud) << 8);
}
static int32_t eg_time_ms(uint32_t atk_or_dec)        /* the blocks from 0 to 1 at this step, as ms */
{
    uint32_t n = atk_or_dec ? (uint32_t)(1u << 24) / atk_or_dec : 100000u;
    return (int32_t)((uint64_t)n * BLOCK * 1000u / FS);
}
static void put_ms_short(char *b, int32_t ms)
{
    if (ms < 1000) {
        put_str(put_int(b, ms), " ms", 4);
    } else {
        char *e = put_int(b, ms / 1000);
        *e++ = '.';
        put_str(put_int(e, ms / 100 % 10), " s", 3);
    }
}
static void viz_env(int gate)
{
    uint32_t v = track_voice(ui.track), a = sv(S_AEG, 0, 0), d = sv(S_AEG, 0, 1), sl, rep = 0, i;
    int32_t ta, td = eg_time_ms(EG_DEC[d & 127u]), th = 0, tot, pk = 0, rise;
    int held = par[PAR_GATE1 + v] != 0u;
    char b[48], t[16];
    if (v == 3u || v == 4u) {                        /* (SN, CY: the clap's REPEAT third, the slope last) */
        rep = sv(S_AEG, 0, 2);
        sl = sv(S_AEG, 0, 3);
    } else {
        sl = sv(S_AEG, 0, 2);
    }
    ta = eg_time_ms(EG_ATK[a & 127u]);
    rise = rep ? ta * (int32_t)(rep > 16u ? 16u : rep) : ta;   /* (slope_calc: REPEAT's teeth, each an attack long) */
    if (gate && held)
        th = (rise + td) / 2 + 60;                   /* (a held step: drawn half as long again) */
    tot = rise + th + td;
    tot = tot < 20 ? 20 : tot;
    put_str(b, rep ? "REPEATS " : "ATTACK ", sizeof b);
    if (rep)
        put_int(b + str_len(b), (int32_t)rep);
    else                                             /* (the times as the cards show them) */
        card_value(b + str_len(b), sizeof b - str_len(b), sect_par(ui.track, S_AEG, 0, 0), a);
    put_str(b + str_len(b), "   DECAY ", sizeof b - str_len(b));
    card_value(b + str_len(b), sizeof b - str_len(b), sect_par(ui.track, S_AEG, 0, 1), d);
    vz_panel(gate ? (held ? "GATE ON: HELD FOR THE STEP'S LENGTH" : "GATE OFF: ONE SHOT, ITS DECAY") : b);
    vz_hguide(VY1, vz_faint());
    for (i = 0; i < VN; i++) {
        int32_t tm = (int32_t)((uint32_t)i * (uint32_t)tot * 25u / 54u), y;   /* (hundredths of a ms) */
        if (tm < rise * 100) {
            if (rep) {                               /* (a tooth: from 1 down, the decay's curve) */
                int32_t x = (1 << 24) - vz_frac24(tm % (ta * 100 + 1), ta * 100 + 1);
                y = eg_shape(x, EG_SLOPE[sl & 127u]);
            } else {
                int32_t x = vz_frac24(tm, ta * 100 + 1);
                y = eg_shape(x, EG_ISLOPE[sl & 127u]);
            }
            pk = rep ? 0 : (int32_t)i;
        } else if (tm < (rise + th) * 100) {
            y = 1 << 24;
        } else {
            int32_t x = (1 << 24) - vz_frac24(tm - (rise + th) * 100, td * 100 + 1);
            y = x > 0 ? eg_shape(x, EG_SLOPE[sl & 127u]) : 0;
        }
        vz_y[i] = (int16_t)(VY1 * 16 - (int32_t)(((int64_t)y * (VY1 - VY0 - 6) * 16) >> 24));
    }
    vz_trace(VX0, VN, VY1, T_THEME, vz_fill());
    if (gate && held)                                /* (the gate: a bar under the held part) */
        cv_rect(VX0 + rise * VN / tot, VY1 + 2, th * VN / tot, 3, T_ACCENT);
    vz_sparkle(VX0 + pk, VY0 + 6, 3);
    put_str(b, "TOTAL ", sizeof b);
    put_ms_short(t, tot);
    put_str(b + str_len(b), t, sizeof b - str_len(b));
    cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
}

/* MOD EG: the pitch envelope (D1-D3, SN); VELOCITY: soft to hard hits, what they move */
static void viz_mod(void)
{
    uint32_t v = track_voice(ui.track), i;
    int vel_page = ui.half == 1u || v >= 4u;
    char b[48], t[24];
    if (!vel_page) {
        uint32_t dec = sv(S_MOD, 0, 0), sl = sv(S_MOD, 0, 1), amt = sv(S_MOD, 0, 2);
        int32_t td = eg_time_ms(EG_ATK[dec & 127u]);
        int32_t semis = (int32_t)(PITCH_AMT[amt & 127u] * 12u >> 16);
        (void)semis;
        vz_panel("PITCH ENVELOPE: FROM HIGH, DOWN");
        vz_hguide(VY1, vz_faint());
        for (i = 0; i < VN; i++) {                   /* (over 400 ms, or the decay if longer) */
            int32_t span = td > 400 ? td : 400, tm = (int32_t)((uint32_t)i * (uint32_t)span * 25u / 54u);
            int32_t x = (1 << 24) - vz_frac24(tm, td * 100 + 1), y;
            y = x > 0 ? eg_shape(x, DEC_SLOPE[sl & 127u]) : 0;
            y = (int32_t)(((int64_t)y * (int32_t)amt * 258) >> 15);
            vz_y[i] = (int16_t)(VY1 * 16 - (int32_t)(((int64_t)y * (VY1 - VY0 - 6) * 16) >> 24));
        }
        vz_trace(VX0, VN, VY1, T_THEME, vz_fill());
        if (amt)
            vz_sparkle(VX0 + 2, vz_y[0] >> 4, 3);
        card_value(t, sizeof t, sect_par(ui.track, S_MOD, 0, 0), dec);
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, t, T_MID, vz_bg());
        return;
    }
    {
        uint32_t hh = v >= 4u ? 0u : 1u;
        uint32_t pd = sect_par(ui.track, S_MOD, hh, 0), amt = sv(S_MOD, hh, 1), vol = sv(S_MOD, hh, 2);
        put_str(b, "VELOCITY > ", sizeof b);
        if (pd && pd != PAR_NONE)
            par_format(b + str_len(b), pd, vp_get(pd), sizeof b - str_len(b));
        vz_panel(b);
        for (i = 0; i < 8u; i++) {                   /* (eight hits, soft to hard: their level, the target's move) */
            int32_t x = VX0 + 8 + (int32_t)i * 26, vel = 16 + (int32_t)i * 111 / 7;
            int32_t h = vol ? vel * 40 / 127 : 40, m = pd && vp_get(pd) ? (int32_t)amt * vel * 22 / (127 * 127) : 0;
            cv_rrect(x, VY1 - h, 12, h, 2, ux_mix(vz_bg(), TRACK_COL[ui.track], 35 + vel * 50 / 127), vz_bg());
            if (pd && vp_get(pd))
                cv_rect(x - 2, VY1 - h - 4 - m, 16, 2, T_THEME);
            if (i == 7u && m)
                vz_sparkle(x + 6, VY1 - h - 4 - m, 3);
        }
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, vol ? "SOFT HITS QUIETER" : "EVERY HIT AT FULL LEVEL", T_MID, vz_bg());
    }
}

/* FILTER: the response over 20 Hz .. 20 kHz (10 octaves), the cutoff marked. log2 units: 1 = 6 dB */
static int32_t log2_64(uint64_t v)                    /* log2(v), Q16; v >= 1 */
{
    uint32_t sh = 0;
    while (v >> 32) {
        v >>= 1;
        sh++;
    }
    return log2_q16((uint32_t)(v ? v : 1u)) + (int32_t)(sh << 16);
}
/* the magnitude of a 2-pole section at r = f / fc (Q16) with damping d (1 / Q, Q16), as -log2 of its denominator's
 * root: num^2 / ((1 - r^2)^2 + (d r)^2), the numerator by type (Q16 log2 units, 0 = unity) */
static int32_t svf_mag(uint32_t type, int32_t lr, int32_t d)
{
    int64_t r = exp2_q16(clamp32(lr, -7 * 65536, 7 * 65536)), r2 = (r * r) >> 16, a = 65536 - r2;
    int64_t dr = (d * r) >> 16;
    uint64_t den = (uint64_t)((a * a) >> 16) + (uint64_t)((dr * dr) >> 16) + 1u, num;
    switch (type) {
    case F_HP: num = (uint64_t)((r2 * r2) >> 16); break;
    case F_BP: case F_DUB: case F_RESO: num = (uint64_t)((dr * dr) >> 16); break;
    case F_UBP: num = (uint64_t)((r * r) >> 16); break;
    case F_NOTCH: num = (uint64_t)((a * a) >> 16); break;
    case F_PEAK: num = (uint64_t)((a * a) >> 16) + (uint64_t)((dr * dr) >> 14); break;
    default: num = 65536u; break;
    }
    return (log2_64(num + 1u) - log2_64(den)) / 2;
}
static void viz_filter(void)
{
    uint32_t fv = sv(S_FILTER, 0, 0), rs = sv(S_FILTER, 0, 1), ty = sv(S_FILTER, 0, 2) + 1u, i;
    int32_t fc_hz, lfc, d, peak = 0, px = 0;
    char b[40];
    fc_hz = (int32_t)(((int64_t)lut1024(FILT_F, fv * 65536u / 127u) * FS) >> 16);   /* (f = fc / FS) */
    fc_hz = fc_hz < 20 ? 20 : fc_hz > 20000 ? 20000 : fc_hz;
    lfc = log2_q16((uint32_t)fc_hz * 64u) - log2_q16(20u * 64u);   /* (octaves over 20 Hz, Q16) */
    d = 65536 - (int32_t)(rs * 65536u / 127u);       /* (sound.c's R, 1 .. 0.02; 1 / Q = 2R) */
    d = (d < 6554 ? 1311 : d) * 2;
    put_str(b, ty - 1u < F_LAST ? L_FILTER[ty - 1u] : "", sizeof b);
    if (ty != F_OFF) {
        put_str(b + str_len(b), "   ", sizeof b - str_len(b));
        if (fc_hz >= 1000) {
            char *e = put_int(b + str_len(b), fc_hz / 1000);
            *e++ = '.';
            put_str(put_int(e, fc_hz / 100 % 10), " kHz", 5);
        } else {
            put_str(put_int(b + str_len(b), fc_hz), " Hz", 4);
        }
    }
    vz_panel(b);
    {                                                /* (the octaves: faint lines; 0 dB dashed) */
        int32_t o;
        for (o = 1; o < 10; o++)
            cv_rect(VX0 + o * VN / 10, VY0, 1, VY1 - VY0, ux_mix(vz_bg(), T_TEXT, 6));
        vz_hguide(VY0 + 24, vz_faint());
    }
    for (i = 0; i < VN; i++) {
        int32_t lr = (int32_t)(i * 10u * 65536u / VN) - lfc, m;   /* (log2 of f / fc) */
        switch (ty) {
        case F_OFF: m = 0; break;
        case F_LP: case F_LP2: m = svf_mag(F_LP, lr, d); break;
        case F_LP4: m = 2 * svf_mag(F_LP, lr, d < 32768 ? d * 2 : 65536); break;
        case F_COMBP: case F_COMBN: {                /* (peaks at the multiples of fc, dips between; -: the odd ones) */
            uint32_t ph = (uint32_t)((uint64_t)(uint32_t)exp2_q16(clamp32(lr, -14 * 65536, 6 * 65536)) << 16);
            int32_t c = vz_sin(ph + 0x40000000u + (ty == F_COMBN ? 0x80000000u : 0u));
            m = c * ((int32_t)rs + 20) / 32;
            break;
        }
        case F_APF: {                                /* (heard with the dry: two notches) */
            int32_t a = svf_mag(F_NOTCH, lr - 65536, 65536), b2 = svf_mag(F_NOTCH, lr + 65536, 65536);
            m = (a + b2) / 2;
            break;
        }
        case F_VOWEL:                                /* (two formants, the second higher) */
            m = (svf_mag(F_BP, lr, 16000) > svf_mag(F_BP, lr - 3 * 65536 / 2, 16000) ? svf_mag(F_BP, lr, 16000)
                                                                                     : svf_mag(F_BP, lr - 3 * 65536 / 2, 16000));
            break;
        case F_RESO: {                               /* CHORD: the root, its fifth, the octave */
            int32_t a = svf_mag(F_BP, lr, d), b2 = svf_mag(F_BP, lr - 38336, d), c = svf_mag(F_BP, lr - 65536, d);
            m = a > b2 ? a : b2;
            m = m > c ? m : c;
            break;
        }
        default: m = svf_mag(ty, lr, d); break;
        }
        m = clamp32(m, -8 * 65536, 4 * 65536);
        vz_y[i] = (int16_t)((VY0 + 24) * 16 - ((m * 6 * 16) >> 16));
        if (m > peak) {
            peak = m;
            px = (int32_t)i;
        }
    }
    vz_trace(VX0, VN, VY1, T_THEME, vz_fill());
    if (ty != F_OFF) {
        int32_t x = VX0 + (int32_t)(((int64_t)lfc * VN) / (10 * 65536));
        x = clamp32(x, VX0, VX1 - 1);
        cv_rect(x, VY1 - 6, 1, 6, T_ACCENT);
        if (peak > 65536 / 2)
            vz_sparkle(VX0 + px, (vz_y[px] >> 4) - 4, 3);
    }
}

/* SHAPE: a sine through the waveshaper at its drive, the sample rate's steps and the bits' (the clean sine faint);
 * its second page: the tilt's slope */
static int32_t shape_one(uint32_t sh, int32_t x, int32_t drive)   /* voices.c shape_block's curve, without state */
{
    int32_t g = 65536 + 7 * drive, a = (int32_t)(((int64_t)x * g) >> 16), lo = -32767 + (int32_t)(((int64_t)drive * 22000) >> 16);
    switch (sh) {
    case SH_LXR: return dist_sample(x, dist_k((uint32_t)drive));
    case SH_FOLD:
        if (a > 32767 || a < -32767) {
            int32_t t = (a + 32767) % (4 * 32767);
            t = t < 0 ? t + 4 * 32767 : t;
            a = t < 2 * 32767 ? t - 32767 : 3 * 32767 - t;
        }
        return a;
    case SH_CLIP: return sat16(a);
    case SH_ASYM: return a >= 0 ? dist_sample(sat16(a), 1024) : (a < lo ? lo : a);
    case SH_RECT: return sat16(iabs(a) * 2 - 20860);  /* (its DC: 2 / pi of it, taken out) */
    case SH_FUZZ: return sat16((int32_t)(((int64_t)a * (4 * 65536 + 3 * drive)) >> 16));
    case SH_TAPE: return sat16(soft3(a));
    case SH_SFOLD: return sine_at(((uint32_t)a << 15) + 0x40000000u);
    default: return sat16(dist_sample(sat16(a + 4096), 256 + ((drive * 1536) >> 16)) - 3000);
    }
}
static void viz_shape(void)
{
    uint32_t sh = sv(S_SHAPE, 0, 0), dr = sv(S_SHAPE, 0, 1), dec = sv(S_SHAPE, 0, 2), bits = sv(S_SHAPE, 0, 3), i;
    uint32_t tilt = sv(S_SHAPE, 1, 0);
    int32_t drive = (int32_t)(dr * 65536u / 127u), held = 0, hold, q;
    uint32_t rate = lut1024u(DECIM, dec * 65536u / 127u);
    hold = rate >= 65536u ? 1 : rate < 1640u ? 40 : (int32_t)(65536u / rate);   /* (the columns a sample is held) */
    char b[40];
    if (ui.half == 1u) {                             /* TILT: the slope round 1 kHz */
        int32_t s = (int32_t)tilt - 64;
        vz_panel(s ? (s > 0 ? "TILT: BRIGHTER" : "TILT: DARKER") : "TILT: FLAT");
        vz_hguide(VMID, vz_faint());
        for (i = 0; i < VN; i++)
            vz_y[i] = (int16_t)(VMID * 16 - ((int32_t)i - (int32_t)VN / 2) * s * 16 / 160);
        vz_trace(VX0, VN, VMID, T_THEME, vz_fill());
        cv_text(VX0, VY1 - 12, &AF_S, "LOWS", T_MID);
        cv_text_r(VX1, VY1 - 12, &AF_S, "HIGHS", T_MID, vz_bg());
        return;
    }
    put_str(b, sh < NSHAPE ? L_SHAPE[sh] : "", sizeof b);
    if (bits) {
        put_str(b + str_len(b), "   ", sizeof b - str_len(b));
        put_int(b + str_len(b), 16 - (int32_t)bits);
        put_str(b + str_len(b), " BITS", sizeof b - str_len(b));
    }
    vz_panel(b);
    vz_hguide(VMID, vz_faint());
    for (i = 0; i < VN; i++)                         /* (the clean sine, faint) */
        cv_pset(VX0 + (int32_t)i, vz_yq15(vz_sin(i * (0xFFFFFFFFu / VN)) * 3 / 4, VMID, 32) >> 4, vz_faint());
    q = bits ? 1 << bits : 1;                        /* (bits: 15 .. 1 left, a step of 2^bits) */
    for (i = 0; i < VN; i++) {
        int32_t y;
        if (hold > 1 && (int32_t)i % hold)
            y = held;
        else {
            y = shape_one(sh, vz_sin(i * (0xFFFFFFFFu / VN)) * 3 / 4, drive);
            if (bits)
                y = (y / q) * q;
            held = y;
        }
        vz_y[i] = vz_yq15(y, VMID, 32);
    }
    vz_trace(VX0, VN, VMID, T_THEME, vz_fill());
}

/* INSERT FX: what the type does: the notches (PHASER, FLANGER), the voices (CHORUS), the level (TREMOLO), the sides
 * (AUTOPAN), the partials moved (SHIFTER), a wave times a sine (RING) */
static void viz_insert(void)
{
    uint32_t ty = sv(S_INSERT, 0, 0), a = sv(S_INSERT, 0, 1), pb = sv(S_INSERT, 0, 2), pc = sv(S_INSERT, 0, 3), i;
    char b[40];
    put_str(b, ty < NINS ? L_INS[ty] : "", sizeof b);
    vz_panel(ty ? b : "NO INSERT: THE VOICE AS IT IS");
    if (!ty) {                                       /* (the voice untouched: a plain wave) */
        for (i = 0; i < VN; i++)
            vz_y[i] = vz_yq15(vz_sin(i * (0xFFFFFFFFu / VN) * 2u), VMID, 24);
        vz_trace(VX0, VN, VMID, vz_faint(), vz_bg());
        return;
    }
    switch (ty) {
    case INS_PHASER: case INS_FLANGER: {             /* (notches: 4 spread, or a comb; feedback sharpens them) */
        uint32_t k;
        for (k = 0; k < 3u; k++) {                   /* (three moments of the sweep: the rate's spread) */
            uint32_t off = k * (8u + pb / 4u);
            for (i = 0; i < VN; i++) {
                uint32_t ph = (i + off) * (0x80000000u / VN) * (ty == INS_PHASER ? 2u : 9u);
                int32_t c = vz_sin(ph), dip = (32767 - iabs(c)) >> 7;   /* (0 .. 255 at the notch) */
                dip = (int32_t)(((int64_t)dip * dip * dip) >> 16) * (int32_t)a / 127;
                dip = dip * (40 + (ty == INS_PHASER ? iabs((int32_t)pc - 64) * 2 : (int32_t)pc)) / 167;
                vz_y[i] = (int16_t)((VY0 + 8) * 16 + dip * 8);
            }
            if (k == 0u)
                vz_trace(VX0, VN, VY0 + 8, T_THEME, vz_bg());
            else
                for (i = 0; i < VN; i++)
                    cv_pset(VX0 + (int32_t)i, vz_y[i] >> 4, ux_mix(vz_bg(), T_THEME, 45 - (int32_t)k * 15));
        }
        cv_text(VX0, VY1 - 12, &AF_S, "LOWS", T_MID);
        cv_text_r(VX1, VY1 - 12, &AF_S, "HIGHS", T_MID, vz_bg());
        return;
    }
    case INS_CHORUS: {                               /* (two copies, detuned by the depth) */
        uint32_t k;
        vz_hguide(VMID, vz_faint());
        for (k = 0; k < 3u; k++) {
            int32_t det = ((int32_t)k - 1) * (int32_t)pc * (int32_t)a / 900;
            for (i = 0; i < VN; i++)
                vz_y[i] = vz_yq15(vz_sin(i * (0x80000000u / VN) * 5u + (uint32_t)(det * (int32_t)i * 600000)), VMID, 26);
            if (k == 1u)
                vz_trace(VX0, VN, VMID, T_THEME, vz_bg());
            else
                for (i = 0; i < VN; i++)
                    cv_pset(VX0 + (int32_t)i, vz_y[i] >> 4, ux_mix(vz_bg(), T_THEME, 45));
        }
        return;
    }
    case INS_TREMOLO: case INS_AUTOPAN: {            /* (the level by its LFO: rate, depth a, sine to square) */
        uint32_t cyc = 1u + pb / 24u, side;
        for (side = 0; side < (ty == INS_AUTOPAN ? 2u : 1u); side++) {
            int32_t mid = ty == INS_AUTOPAN ? (side ? 82 : 42) : VMID, hh = ty == INS_AUTOPAN ? 14 : 30;
            for (i = 0; i < VN; i++) {
                int32_t l = ins_shape(i * (0xFFFFFFFFu / VN) * cyc + (side ? 0x80000000u : 0u), pc);   /* Q15 */
                int32_t gn = 32768 - (int32_t)(((int64_t)(32768 - l) / 2) * (int32_t)a / 127);
                int32_t w = vz_sin(i * (0xFFFFFFFFu / VN) * 9u);
                vz_y[i] = vz_yq15((int32_t)(((int64_t)w * gn) >> 15), mid, hh);
            }
            vz_trace(VX0, VN, mid, T_THEME, vz_fill());
            if (ty == INS_AUTOPAN)
                cv_text(VX0, mid - 6, &AF_S, side ? "R" : "L", T_MID);
        }
        return;
    }
    case INS_SHIFT: {                                /* (the partials, and where they move: +- 500 Hz) */
        int32_t sh = ((int32_t)pb - 64) * 3;
        for (i = 1; i <= 6u; i++) {
            int32_t x = VX0 + (int32_t)i * 28, h = 60 - (int32_t)i * 7;
            cv_rect(x, VY1 - h, 2, h, vz_faint());
            cv_rect(x + sh * (int32_t)a / 127, VY1 - h, 2, h, T_THEME);
        }
        cv_text(VX0, VY0, &AF_S, sh >= 0 ? "EVERY PARTIAL UP BY THE SAME HZ" : "EVERY PARTIAL DOWN BY THE SAME HZ", T_MID);
        return;
    }
    default: {                                       /* RING: the wave times a sine (AM: the sine lifted) */
        vz_hguide(VMID, vz_faint());
        for (i = 0; i < VN; i++) {
            int32_t w = vz_sin(i * (0xFFFFFFFFu / VN) * 2u), m = vz_sin(i * (0xFFFFFFFFu / VN) * (2u + pb / 10u));
            int32_t mm = m + (int32_t)(((int64_t)(32767 - m) * (int32_t)pc) / 254);
            int32_t y = (int32_t)(((int64_t)w * mm) >> 15);
            y = ((y * (int32_t)a + w * (127 - (int32_t)a)) * 258) >> 15;   /* (/ 127) */
            cv_pset(VX0 + (int32_t)i, vz_yq15(m, VMID, 12) >> 4, vz_faint());
            vz_y[i] = vz_yq15(y, VMID, 32);
        }
        vz_trace(VX0, VN, VMID, T_THEME, vz_bg());
        return;
    }
    }
}

/* LFO 1, LFO 2: one bar of it at the tempo: its wave, amount and offset; the hits that restart it (TRIG), hold it
 * (HOLD) or start one cycle (ONE) or half (HALF): the retrigger track's steps in the shown bar */
static void viz_lfo(int two)
{
    uint32_t rate, amt, wave, sync, rtrg, ofs, mode, i, s, hits = 0, tgt;
    uint64_t cyc_q16;                                /* cycles in the bar, Q16 */
    int32_t lastv = 0;
    uint32_t ph = 0, gone = 0, run;
    const pattern_t *pt = shown_pat();
    char b[48];
    if (two) {
        rate = sv(S_LFO2, 0, 0); amt = sv(S_LFO2, 0, 1); wave = sv(S_LFO2, 0, 2);
        tgt = sect_par(ui.track, S_LFO2, 0, 3);
        mode = sv(S_LFO2, 1, 0); sync = sv(S_LFO2, 1, 1); rtrg = sv(S_LFO2, 1, 2); ofs = sv(S_LFO2, 1, 3);
    } else {
        rate = sv(S_LFO, 0, 0); amt = sv(S_LFO, 0, 1); wave = sv(S_LFO, 0, 2);
        tgt = sect_par(ui.track, S_LFO, 0, 3);
        sync = sv(S_LFO, 1, 1); rtrg = sv(S_LFO, 1, 2); ofs = sv(S_LFO, 1, 3);
        mode = sv(S_LFOM, 0, 0);
    }
    if (sync && sync <= 11u)
        cyc_q16 = (uint64_t)SYNC_SCALE_X4[sync] << 14;
    else {                                           /* (the free rate a bar at the tempo) */
        uint64_t inc = lut1024u(LFO_INC, rate * 65536u / 127u), bpm = seq_bpm() ? seq_bpm() : 120u;
        cyc_q16 = (inc * (FS / BLOCK) * 240u) >> 16;
        cyc_q16 = (uint32_t)(cyc_q16 > 0xffffffffu ? 0xffffffffu : cyc_q16) / (uint32_t)bpm;   /* (no 64-bit divide here) */
    }
    cyc_q16 = cyc_q16 < 1u ? 1u : cyc_q16 > (24u << 16) ? 24u << 16 : cyc_q16;
    put_str(b, "TO ", sizeof b);
    if (tgt && tgt != PAR_NONE && vp_get(tgt))
        par_format(b + 3, tgt, vp_get(tgt), sizeof b - 3u);
    else
        put_str(b, "NO TARGET", sizeof b);
    if (mode < NLFOMODE && mode) {
        put_str(b + str_len(b), "  ", sizeof b - str_len(b));
        put_str(b + str_len(b), L_LFOMODE[mode], sizeof b - str_len(b));
    }
    vz_panel(b);
    {                                                /* (who restarts it: the RETRIGGER voice's track, or its own) */
        uint32_t rt = rtrg ? rtrg - 1u : (mode != LM_TRIG ? track_voice(ui.track) : 99u);
        if (rt < NVOICE) {
            for (s = 0; s < 16u; s++)
                if (main_on(pt, rt == 5u ? 5u : rt, ui.bar * 16u + s)) {
                    hits |= 1u << s;
                    cv_rect(VX0 + (int32_t)(s * VN / 16u), VY1 + 1, 3, 3, TRACK_COL[rt]);
                }
            if (!hits) {                             /* (none in this bar: shown on the beats, dimmed) */
                hits = 0x1111u;
                for (s = 0; s < 16u; s += 4u)
                    cv_rect(VX0 + (int32_t)(s * VN / 16u), VY1 + 1, 3, 3, T_DIM);
            }
        }
    }
    for (s = 0; s < 4u; s++)
        cv_rect(VX0 + (int32_t)(s * VN / 4u), VY0, 1, VY1 - VY0, ux_mix(vz_bg(), T_TEXT, 7));
    vz_hguide(VY1, vz_faint());
    {
        uint32_t step = (uint32_t)((cyc_q16 << 16) / VN), prev = 0xFFFFu, rnd = 0x1234567u;
        int32_t held = 0, rk = 0;
        run = mode < LM_ONE;
        ph = ofs << 25;
        for (i = 0; i < VN; i++) {
            uint32_t col = i * 16u / VN, old;
            int32_t val;
            if (col != prev) {                       /* (a step's first column: its hit) */
                prev = col;
                if ((hits >> col) & 1u) {
                    if (mode == LM_TRIG || mode >= LM_ONE) {
                        ph = ofs << 25;
                        gone = 0;
                        run = 1;
                    }
                    held = lastv;
                }
            }
            old = ph;
            if (mode >= LM_ONE) {
                uint32_t end = mode == LM_ONE ? 0xFFFFFFFFu : 0x80000000u;
                if (run) {
                    uint32_t d = end - gone < step ? end - gone : step;
                    ph += d;
                    gone += d;
                    run = gone < end;
                }
            } else {
                ph += step;
            }
            if (wave == LFO_NOISE) {                 /* (a fixed draw a cycle: lfo_wave's would take the noise) */
                if (old > ph || i == 0u) {
                    rnd = rnd * 1664525u + 1013904223u;
                    rk = (int32_t)((rnd >> 16) & 0xFFFFu);
                }
                val = rk;
            } else {
                lfo_t l;
                l.wave = (uint8_t)wave;
                l.phase = ph;
                val = lfo_wave(&l, old);
            }
            lastv = val;
            if (mode == LM_HOLD)
                val = held;
            if (amt < 127u)                          /* (the wave at full depth, faint) */
                cv_pset(VX0 + (int32_t)i, VY1 - (int32_t)(((int64_t)val * (VY1 - VY0 - 4)) >> 16), vz_faint());
            val = (int32_t)(((int64_t)val * (int32_t)amt) / 127);
            vz_y[i] = (int16_t)(VY1 * 16 - (int32_t)(((int64_t)val * (VY1 - VY0 - 4) * 16) >> 16));
        }
    }
    vz_trace(VX0, VN, VY1, T_THEME, vz_fill());
    if (sync && sync <= 11u) {
        put_str(b, "SYNC ", sizeof b);
        put_str(b + 5, L_SYNC[sync], sizeof b - 5u);
    } else {                                         /* (its rate, as its card shows it) */
        card_value(b, sizeof b, sect_par(ui.track, ui.sect == S_LFO2 ? S_LFO2 : S_LFO, 0, 0), rate);
    }
    cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
}

/* RANDOM PER HIT: four lanes, each hit's draw scattered as far as the amount lets it (the same draws each time) */
static void viz_random(void)
{
    static const char *const N[4] = {"PITCH", "DECAY", "FILTER", "PAN"};
    uint32_t k, i, h = 0x2545F491u;
    vz_panel("EACH HIT DRAWS ITS OWN");
    for (k = 0; k < 4u; k++) {
        int32_t y = VY0 + 4 + (int32_t)k * 18, a = (int32_t)sv(S_RANDOM, 0, k), w = a * 70 / 127;
        cv_text(VX0, y - 5, &AF_S, N[k], a ? T_TEXT : T_DIM);
        cv_rect(76, y, 140, 1, vz_faint());
        cv_rect(146, y - 4, 1, 9, ux_mix(vz_bg(), T_TEXT, 40));
        if (a)
            cv_rect(146 - w, y - 1, 2 * w + 1, 3, ux_mix(vz_bg(), T_THEME, 30));
        for (i = 0; i < 9u; i++) {                   /* (nine hits' draws) */
            int32_t r;
            h ^= h << 13;
            h ^= h >> 17;
            h ^= h << 5;
            r = (int32_t)((h >> 8) & 255u) - 128;
            cv_rrect(146 + r * w / 128 - 2, y - 2, 5, 5, 2, a ? T_THEME : T_DIM, vz_bg());
        }
        if (a == 127)
            vz_sparkle(146 + w, y, 3);
    }
}

/* CROSS MOD / CHOKE: the source's voice into this one (its mode, by the amount); the voices that choke each other */
static void viz_xmod(void)
{
    static const char *const VS[NVOICE] = {"D1", "D2", "D3", "SN", "CY", "HH"};
    uint32_t v = track_voice(ui.track), src = sv(S_XMOD, 0, 0), md = sv(S_XMOD, 0, 1), amt = sv(S_XMOD, 0, 2);
    uint32_t ch = sv(S_XMOD, 0, 3), i;
    char b[32];
    vz_panel(src ? "CROSS MOD" : "CROSS MOD OFF");
    if (src && src <= NVOICE) {                      /* (the source chip, the arrow, this voice) */
        uint32_t s = src - 1u;
        int32_t w = 1 + (int32_t)amt * 5 / 127;
        cv_rrect(VX0 + 4, 30, 40, 20, 4, TRACK_COL[s], vz_bg());
        text_c(VX0 + 4, 30 + CAP_IN(S, 20), 40, &AF_S, VS[s], RGB(12, 12, 12), TRACK_COL[s]);
        cv_rect(VX0 + 48, 40 - w / 2, 108, w, ux_mix(vz_bg(), T_THEME, 40 + (int32_t)amt * 60 / 127));
        for (i = 0; i < 6u; i++)
            cv_rect(VX0 + 150 + (int32_t)i, 40 - 6 + (int32_t)i, 1, 13 - 2 * (int32_t)i, T_THEME);
        cv_rrect(VX0 + 160, 30, 40, 20, 4, TRACK_COL[ui.track], vz_bg());
        text_c(VX0 + 160, 30 + CAP_IN(S, 20), 40, &AF_S, VS[v], RGB(12, 12, 12), TRACK_COL[ui.track]);
        put_str(b, md < 3u ? L_XMODE[md] : "", sizeof b);
        text_c(VX0 + 48, 24, 108, &AF_S, b, T_TEXT, vz_bg());
        if (amt)
            vz_sparkle(VX0 + 102, 40, 3);
    } else {
        cv_text(VX0, 34, &AF_S, "SOURCE: ANOTHER VOICE MOVES THIS ONE", T_MID);
    }
    cv_text(VX0, 64, &AF_S, ch ? "CHOKE" : "CHOKE OFF", ch ? T_TEXT : T_MID);
    for (i = 0; i < NVOICE; i++) {                   /* (the voices in this one's choke group lit) */
        int32_t x = VX0 + 52 + (int32_t)i * 27;
        int same = ch && par[PAR_CHOKE1 + i] == ch;
        cv_rrect(x, 76, 23, 16, 3, same ? TRACK_COL[i < 5u ? i : 5u] : ux_mix(vz_bg(), T_TEXT, 10), vz_bg());
        text_c(x, 76 + CAP_IN(S, 16), 23, &AF_S, VS[i], same ? RGB(12, 12, 12) : T_DIM,
               same ? TRACK_COL[i < 5u ? i : 5u] : ux_mix(vz_bg(), T_TEXT, 10));
    }
}

/* the sound page's picture, by section */
/* 0.19: OSC on a voice a model plays (its own sound): the model, COARSE and FINE its pitch (lxr_unheard) */
static void viz_pitch(void)
{
    uint32_t v = track_voice(ui.track), m = sv(S_MODEL, 0, 0);
    vz_panel("PITCH");
    cv_text(VX0, 22, &AF_M, L_MODEL[m], T_THEME);
    vz_wrap(VX0, 44, IX - VX0 - 12, 14, 3u, "COARSE and FINE tune it. Its sound is on the MODEL page", T_TEXT);
    ic_model(m, v, sv(S_MODEL, 0, 1), sv(S_MODEL, 0, 2), sv(S_MODEL, 0, 3));
}
static void draw_sound_viz(void)
{
    vz_h = 104;
    if (ui.sect == S_OSC && lxr_unheard(ui.track) == 2u) {
        viz_pitch();
        return;
    }
    switch (ui.sect) {
    case S_MODEL: viz_model(); break;
    case S_OSC: viz_osc(); break;
    case S_FM: viz_fm(); break;
    case S_CLICK: viz_trans(); break;
    case S_FILTER: viz_filter(); break;
    case S_SHAPE: viz_shape(); break;
    case S_INSERT: viz_insert(); break;
    case S_AEG: viz_env(0); break;
    case S_GATE: viz_env(1); break;
    case S_MOD: viz_mod(); break;
    case S_LFO: case S_LFOM: viz_lfo(0); break;
    case S_LFO2: viz_lfo(1); break;
    case S_RANDOM: viz_random(); break;
    case S_XMOD: viz_xmod(); break;
    default: vz_panel(""); break;
    }
}

/* ------------------------------------------------- the track's pages --- */
/* GROOVE: one bar of 16ths, each hit where the groove puts it (its 24ths of a step, by the amount) and as loud;
 * HUMANIZE's reach round each; the selected track's steps lit, the rest faint */
static void viz_groove(void)
{
    const pattern_t *pt = shown_pat();
    uint32_t g = pt->groove, s, fol = !((pt->gskip >> ui.track) & 1u);
    int32_t cw = VN / 16, hum = (int32_t)pt->ghum * 3 * cw / (127 * 24);
    char b[40];
    static const char *const G[NGROOVE + 1u] = {"NO GROOVE", "SWING 54%", "SWING 58%", "SWING 62%", "SWING 66%",
                                                "SWING 71%", "LAZY", "DILLA", "2-STEP", "DUB", "PUSH", "TECHNO",
                                                "BROKEN"};
    put_str(b, G[g <= NGROOVE ? g : 0], sizeof b);
    if (!fol)
        put_str(b + str_len(b), ": NOT THIS TRACK", sizeof b - str_len(b));
    vz_panel(b);
    for (s = 0; s < 16u; s++) {
        int32_t x = VX0 + (int32_t)s * cw + cw / 2, d = 0, v = 0, h, on = main_on(pt, ui.track, ui.bar * 16u + s);
        uint16_t c = on ? TRACK_COL[ui.track] : ux_mix(vz_bg(), T_TEXT, 22);
        cv_rect(x, VY1 - 2, 1, 4, (s & 3u) ? vz_faint() : ux_mix(vz_bg(), T_TEXT, 50));   /* (the grid) */
        if (g && g <= NGROOVE && fol) {
            d = GROOVE_T[g - 1u][s] * (int32_t)pt->gamt / 100;
            v = GROOVE_V[g - 1u][s] * (int32_t)pt->gamt / 100;
        }
        x += d * cw / 24;
        h = clamp32(100 + v, 8, 127) * (VY1 - VY0 - 14) / 127;
        if (hum && fol)
            cv_rect(x - hum, VY1 - h - 6, 2 * hum + 3, 2, ux_mix(vz_bg(), T_THEME, 35));
        cv_rrect(x - 1, VY1 - 4 - h, 4, h, 2, c, vz_bg());
        if (d && on)
            vz_sparkle(x + 1, VY1 - 8 - h, 2);
    }
    put_str(put_int(b, (int32_t)pt->gamt), "%", 2);
    cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
}

/* BOUNCE: a ball's hits from the first gap (BALL's curve: each gap 0.7 of the last), the pitch drifting */
static void viz_bounce(void)
{
    uint32_t b = shown_pat()->bnc[ui.track], i;
    int32_t gap = (int32_t)BNC_TICKS[b & 3u] * 256, x = 0, drift = (int32_t)(b >> 2) - 12, px = VX0, py = VY1;
    char t[40];
    put_str(t, drift ? (drift > 0 ? "THE PITCH RISES " : "THE PITCH FALLS ") : "EACH GAP 0.7 OF THE LAST",
            sizeof t);
    if (drift)
        put_str(put_int(t + str_len(t), drift < 0 ? -drift : drift), " SEMIS", 7);
    vz_panel(t);
    vz_hguide(VY1, vz_faint());
    for (i = 0; i < 8u; i++) {                       /* (the window: a bar, 384 ticks) */
        int32_t nx = x + gap, hx = VX0 + nx * VN / (BNC_TICKS[b & 3u] * 256 * 7 / 2), j, hgt;
        hgt = gap * (VY1 - VY0 - 4) / (BNC_TICKS[b & 3u] * 256);
        hgt = hgt > VY1 - VY0 - 4 ? VY1 - VY0 - 4 : hgt;
        for (j = px; j <= hx && j < VX1; j++) {     /* (the arc: a parabola over the gap) */
            int32_t u = (j - px) * 256 / (hx - px + 1);
            int32_t y = VY1 - (int32_t)((int64_t)hgt * 4 * u * (256 - u) >> 16);
            cv_rect(j, y, 1, 2, ux_mix(vz_bg(), T_THEME, 60));
            (void)py;
        }
        if (hx >= VX1)
            break;
        cv_rrect(hx - 2, VY1 - 3 - (drift * (int32_t)i) / 3, 5, 5, 2, TRACK_COL[ui.track], vz_bg());
        px = hx;
        x = nx;
        gap = (int32_t)(((int64_t)gap * 45875) >> 16);
    }
    cv_rrect(VX0 - 2, VY1 - 3, 5, 5, 2, T_ACCENT, vz_bg());
    vz_sparkle(VX0, VY1 - 8, 2);
}

/* MELODY: the notes the track has played in the shown bar, on the scale's lanes (STEPS' degrees brighter), the
 * band SPREAD and BIAS draw from */
static int32_t mel_row_y(int32_t semi) { return VY1 - 2 - (clamp32(semi, -24, 24) + 24) * (VY1 - VY0 - 6) / 48; }
static void viz_melody(void)
{
    const pattern_t *pt = shown_pat();
    const uint8_t *g = pt->mel[ui.track];
    uint32_t m = SCALE_MASK[pt->scale < NSCALE ? pt->scale : SC_CHROM], th = g[ML_STEPS] * 4u / 128u, s, any = 0;
    static const uint8_t W[12] = {4, 0, 0, 2, 2, 0, 0, 3, 0, 0, 1, 1};
    int32_t semi, n = 0, c, r, lo, hi, a;
    uint8_t set[12];
    char b[40];
    static const char *const S[NMEL] = {"MELODY OFF", "RANDOM", "WALK", "ARP UP", "ARP DOWN", "ARP UP/DOWN", "MOTIF"};
    for (s = 0; s < 12u; s++)
        if (((m >> s) & 1u) && W[s] >= th)
            set[n++] = (uint8_t)s;
    n = n ? n : 1;
    put_str(b, S[g[ML_SHAPE] < NMEL ? g[ML_SHAPE] : 0], sizeof b);
    put_str(b + str_len(b), "  IN ", sizeof b - str_len(b));
    put_str(b + str_len(b), SCALE_NAME[pt->scale < NSCALE ? pt->scale : 0], sizeof b - str_len(b));
    vz_panel(b);
    c = ((int32_t)g[ML_BIAS] - 64) * n / 64;          /* (mel_fresh's centre and reach, in degrees) */
    r = (int32_t)g[ML_SPREAD] * 2 * n / 127;
    lo = mel_semi(set, n, c - r);
    hi = mel_semi(set, n, c + r);
    cv_rect(VX0, mel_row_y(hi) - 1, VN, mel_row_y(lo) - mel_row_y(hi) + 3, ux_mix(vz_bg(), T_THEME, 12));
    for (semi = -24; semi <= 24; semi++) {           /* (the lanes: the scale's notes; STEPS' allowed ones brighter) */
        uint32_t d = (uint32_t)((semi % 12 + 12) % 12);
        if ((m >> d) & 1u)
            cv_rect(VX0, mel_row_y(semi), VN, 1, d == 0u ? ux_mix(vz_bg(), T_TEXT, 30)
                                                 : W[d] >= th ? ux_mix(vz_bg(), T_TEXT, 16) : ux_mix(vz_bg(), T_TEXT, 7));
    }
    {                                                /* (the root at the voice's tune, as mel_note) */
        int32_t vt = (int32_t)(vc[ui.track < V_HAT ? ui.track : V_HAT].osc.midi_freq >> 8);
        a = ((int32_t)(pt->sroot % 12u) - vt) % 12;
        a = a < 0 ? a + 12 : a;
        a = a > 6 ? a - 12 : a;
    }
    for (s = 0; s < 16u; s++) {
        uint32_t st = ui.bar * 16u + s;
        int32_t x = VX0 + (int32_t)s * (VN / 16), y;
        if (!(mel_n[ui.track][st] & 0x80u) || !main_on(pt, ui.track, st))
            continue;
        y = mel_row_y((int32_t)(mel_n[ui.track][st] & 127u) - SEQ_DEFAULT_NOTE - a);
        cv_rrect(x + 1, y - 2, VN / 16 - 2, 5, 2, TRACK_COL[ui.track], vz_bg());
        any = 1;
    }
    if (!any)
        text_c(VX0, VMID - 6, VN, &AF_S, !mel_on(pt, ui.track) ? "SHAPE: TURN IT ON" : sq.running ? "NO HITS IN THIS BAR"
                                                        : mel_on(pt, ui.track) ? "PLAY: THE NOTES SHOW AS THEY ARE PICKED"
                                                        : "SHAPE: TURN IT ON", T_MID, ux_mix(vz_bg(), T_THEME, 12));
    vz_sparkle(VX1 - 4, mel_row_y(hi), 2);
}
/* MELODY (2/2): DEJA VU's loop: its LOOP places, the notes they hold, where it is; the dice to the lock */
static void viz_melody_loop(void)
{
    const pattern_t *pt = shown_pat();
    const uint8_t *g = pt->mel[ui.track];
    uint32_t loop = g[ML_LOOP] < 1u ? 1u : g[ML_LOOP] > 16u ? 16u : g[ML_LOOP], p, dv = g[ML_DEJA];
    int loops = g[ML_SHAPE] == MEL_RANDOM || g[ML_SHAPE] == MEL_WALK;
    int32_t gx;
    vz_panel(!g[ML_SHAPE] ? "MELODY OFF" : loops ? (dv == 127u ? "DEJA VU: THE LOOP LOCKED" : dv ? "DEJA VU: A LOOP THAT CHANGES"
                                                                                                  : "DEJA VU 0: NEW NOTES")
                                   : g[ML_SHAPE] == MEL_MOTIF ? "MOTIF: DEJA VU SETS HOW OFTEN IT CHANGES"
                                                              : "ARP: THE LOOP IS ITS OWN");
    for (p = 0; p < 16u; p++) {
        int32_t x = VX0 + (int32_t)p * (VN / 16), in = p < loop && loops;
        int full = (sq.mfull[ui.track][p >> 3] >> (p & 7u)) & 1u;
        cv_rrect(x + 1, 30, VN / 16 - 2, 36, 2, in ? ux_mix(vz_bg(), T_TEXT, 12) : ux_mix(vz_bg(), T_TEXT, 4), vz_bg());
        if (in && full) {
            int32_t h = clamp32(sq.mloop[ui.track][p] + 24, 1, 48) * 32 / 48;
            cv_rect(x + 3, 64 - h, VN / 16 - 6, 3, TRACK_COL[ui.track]);
        }
        if (in && p == sq.mpos[ui.track] % loop && sq.running)
            cv_rect(x + 1, 68, VN / 16 - 2, 2, T_ACCENT);
    }
    cv_text(VX0, 79, &AF_S, "NEW", T_MID);            /* (DEJA VU: new notes at 0 .. the loop locked at 127) */
    cv_text_r(VX1, 79, &AF_S, "LOCK", T_MID, vz_bg());
    cv_rect(VX0 + 32, 85, VN - 72, 2, vz_faint());
    gx = VX0 + 32 + (int32_t)dv * (VN - 72) / 127;
    cv_rrect(gx - 3, 81, 7, 10, 2, T_THEME, vz_bg());
    if (dv == 127u || !dv)
        vz_sparkle(gx, 78, 2);
}
/* SCALE: an octave from the root, its notes lit (the root brighter), the chord tones STEPS keeps marked; the tracks
 * that follow it */
static void viz_scale(void)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const uint8_t BLK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0}, WX[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    const pattern_t *pt = shown_pat();
    uint32_t m = SCALE_MASK[pt->scale < NSCALE ? pt->scale : SC_CHROM], root = pt->sroot % 12u, k, t;
    int32_t ww = 28, x0 = VX0 + 10;
    char b[32];
    put_str(b, NN[root], sizeof b);
    put_str(b + str_len(b), " ", sizeof b - str_len(b));
    put_str(b + str_len(b), SCALE_NAME[pt->scale < NSCALE ? pt->scale : 0], sizeof b - str_len(b));
    vz_panel(b);
    for (k = 0; k < 12u; k++) {                      /* (C .. B: white keys, then black over them) */
        uint32_t d = (k + 12u - root) % 12u;
        int in = (m >> d) & 1u;
        uint16_t c = d == 0u ? T_ACCENT : in ? ux_mix(vz_bg(), T_THEME, 75) : ux_mix(vz_bg(), T_TEXT, 18);
        if (BLK[k])
            continue;
        cv_rrect(x0 + (int32_t)WX[k] * ww, 24, ww - 2, 52, 3, c, vz_bg());
        text_c(x0 + (int32_t)WX[k] * ww, 62, ww - 2, &AF_S, NN[k], in || d == 0u ? RGB(12, 12, 12) : T_MID, c);
    }
    for (k = 0; k < 12u; k++) {
        uint32_t d = (k + 12u - root) % 12u;
        int in = (m >> d) & 1u;
        uint16_t c = d == 0u ? T_ACCENT : in ? T_THEME : ux_mix(vz_bg(), T_TEXT, 30);
        int32_t x = x0 + (int32_t)WX[k] * ww + ww - 9;
        if (!BLK[k])
            continue;
        cv_rect(x - 1, 24, 18, 32, vz_bg());
        cv_rrect(x, 24, 16, 31, 3, c, vz_bg());
    }
    vz_sparkle(x0 + (int32_t)WX[root] * ww + (BLK[root] ? ww : ww / 2 - 1), 22, 3);
    cv_text(VX0, 84, &AF_S, "FOLLOW", T_MID);
    for (t = 0; t < NTRACK; t++) {
        int on = (pt->sfollow >> t) & 1u;
        int32_t x = VX0 + 56 + (int32_t)t * 23;
        cv_rrect(x, 82, 21, 14, 3, on ? TRACK_COL[t] : ux_mix(vz_bg(), T_TEXT, 10), vz_bg());
        text_c(x, 82 + CAP_IN(S, 14), 21, &AF_S, VOICE_SHORT[t], on ? RGB(12, 12, 12) : T_DIM,
               on ? TRACK_COL[t] : ux_mix(vz_bg(), T_TEXT, 10));
    }
}
static int track_viz_page(void)                       /* the SEQ track pages with a picture (the rest: the grid) */
{
    uint32_t g = ui.page % NEUCLID;
    return ui.mode == M_EUCLID && (g == PG_GROOVE || g == PG_BOUNCE || g == PG_MELODY || g == PG_MELODY + 1u ||
                                   g == PG_SCALE);
}
static void draw_track_viz(int32_t y0)
{
    (void)y0;
    vz_h = 122;
    switch (ui.page % NEUCLID) {
    case PG_GROOVE: viz_groove(); break;
    case PG_BOUNCE: viz_bounce(); break;
    case PG_MELODY: viz_melody(); break;
    case PG_MELODY + 1u: viz_melody_loop(); break;
    default: viz_scale(); break;
    }
}
static uint32_t track_viz_sig(void)
{
    const pattern_t *pt = shown_pat();
    uint32_t s = sig_add(ui.page, ui.track | ui.bar << 4), k;
    s = sig_add(s, pt->groove | pt->gamt << 8 | pt->ghum << 16 | pt->gskip << 24);
    s = sig_add(s, pt->bnc[ui.track] | pt->sroot << 8 | pt->scale << 16 | pt->sfollow << 24);
    for (k = 0; k < 8u; k++)
        s = sig_add(s, pt->mel[ui.track][k]);
    s = sig_add(s, pt->main[ui.track][ui.bar & 3u]);
    if (ui.page % NEUCLID == PG_MELODY)              /* (the notes as they are picked) */
        for (k = 0; k < 16u; k++)
            s = sig_add(s, mel_n[ui.track][ui.bar * 16u + k]);
    if (ui.page % NEUCLID == PG_MELODY + 1u) {
        s = sig_add(s, sq.mfull[ui.track][0] | sq.mfull[ui.track][1] << 8 | (sq.running ? sq.mpos[ui.track] : 99u) << 16);
        for (k = 0; k < 16u; k++)
            s = sig_add(s, (uint8_t)sq.mloop[ui.track][k]);
    }
    return s;
}

/* -------------------------------------------------------- the FX pages --- */
/* REVERB (37): what makes the sound, on the right: ROOM a hut, HALL a castle, PLATE a metal shield, SPRING a bare
 * coil; SIZE grows it (the spring longer), its echoes arcs going out to the right, LEVEL their strength, DAMPING them
 * fading sooner. The type's name and what it is on the left */
static void vz_arc(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* an arc, -50 .. +50 degrees, to the right */
{
    int32_t k, px = 0, py = 0;
    for (k = -5; k <= 5; k++) {
        uint32_t ph = (uint32_t)(k * 0x02400000);
        int32_t x = cx + ((vz_sin(ph + 0x40000000u) * r) >> 15), y = cy + ((vz_sin(ph) * r) >> 15);
        if (k > -5)
            cv_line(px, py, x, y, c);
        px = x;
        py = y;
    }
}
static void vz_cone(int32_t cx, int32_t top, int32_t hw, int32_t h, uint16_t c)   /* a roof: apex up, base hw */
{
    int32_t y;
    for (y = 0; y < h; y++) {
        int32_t w = hw * (y + 1) / h;
        cv_rect(cx - w, top + y, 2 * w + 1, 1, c);
    }
}
static void viz_reverb(void)
{
    static const char *const D[NREV] = {"A little hut: close walls, quick echoes", "A bare spring: twangy, bright",
                                        "A castle hall: a long, slow swell", "A steel shield: dense and smooth"};
    uint32_t ty = par[PAR_REV_TYPE] % NREV, sz = par[PAR_REV_SIZE], dp = par[PAR_REV_DAMP], lv = par[PAR_REV_LEVEL];
    int32_t f = 55 + (int32_t)sz * 45 / 127, gy = VY1 - 4, cx = 166, cy, rx, i, n = 2 + (int32_t)sz * 4 / 127;
    uint16_t line = ux_mix(vz_bg(), T_THEME, 90), fill = ux_mix(vz_bg(), T_THEME, 30), dark = ux_mix(vz_bg(), T_TEXT, 8);
    uint16_t roof = ux_mix(vz_bg(), T_THEME, 70);
    vz_panel("REVERB");
    cv_text(VX0, 26, &AF_M, L_REVMODEL[ty], T_THEME);
    vz_wrap(VX0, 50, 92, 14, 3u, D[ty], T_TEXT);
    if (ty == 0u) {                                  /* ROOM: a hut, its thatched roof, a door, a window */
        int32_t w = 58 * f / 100, h = 32 * f / 100, x0 = cx - w / 2, rh = h + 4;
        cv_rect(112, gy, VX1 - 112, 1, vz_faint());
        cv_rect(x0, gy - h, w, h, fill);
        cv_frame(x0, gy - h, w, h, line);
        vz_cone(cx, gy - h - rh, w / 2 + 7, rh, roof);
        for (i = 1; i < 4; i++)                      /* (the thatch) */
            cv_line(cx - (w / 2 + 7) * i / 4, gy - h - rh + rh * i / 4, cx + (w / 2 + 7) * i / 4, gy - h - rh + rh * i / 4,
                    ux_mix(roof, vz_bg(), 35));
        cv_rrect(cx - w / 8 - 1, gy - h * 6 / 10, w / 4 + 2, h * 6 / 10, 3, dark, fill);
        cv_rect(x0 + w - w / 4 - 3, gy - h + h / 4, w / 6 + 2, w / 6 + 2, ux_mix(vz_bg(), T_ACCENT, 70));
        cy = gy - h;
        rx = w / 2 + 8;
    } else if (ty == 2u) {                           /* HALL: a castle, its wall, two towers, a gate */
        int32_t w = 88 * f / 100, h = 30 * f / 100, x0 = cx - w / 2, tw = w / 5, th = h * 3 / 2, t;
        cv_rect(112, gy, VX1 - 112, 1, vz_faint());
        cv_rect(x0, gy - h, w, h, fill);
        for (i = x0 + tw; i + 4 < x0 + w - tw; i += 8)   /* (the battlements) */
            cv_rect(i + 2, gy - h - 4, 4, 4, fill);
        for (t = 0; t < 2; t++) {
            int32_t tx = t ? x0 + w - tw : x0;
            cv_rect(tx, gy - th, tw, th, ux_mix(vz_bg(), T_THEME, 42));
            cv_frame(tx, gy - th, tw, th, line);
            vz_cone(tx + tw / 2, gy - th - tw, tw / 2 + 2, tw, roof);
            cv_rect(tx + tw / 2, gy - th - tw - 7, 1, 7, line);   /* (the flag) */
            cv_rect(tx + tw / 2 + 1, gy - th - tw - 7, 5, 3, T_ACCENT);
            cv_rect(tx + tw / 2 - 1, gy - th + th / 3, 2, 5, dark);
        }
        cv_frame(x0, gy - h, w, h, line);
        cv_rrect(cx - w / 10, gy - h * 6 / 10, w / 5, h * 6 / 10, w / 10, dark, fill);
        cy = gy - h;
        rx = w / 2 + 6;
    } else if (ty == 3u) {                           /* PLATE: a metal shield, a boss and rivets, its edge */
        int32_t w = 52 * f / 100, h = 66 * f / 100, x0 = cx - w / 2, top = (VY0 + VY1) / 2 - h / 2, y;
        uint16_t steel = ux_mix(vz_bg(), T_TEXT, 48), shine = ux_mix(vz_bg(), T_TEXT, 78);
        for (y = 0; y < h; y++) {
            int32_t hw = w / 2, t = y - h * 2 / 5;
            if (t > 0) {
                int32_t r = h - h * 2 / 5;
                hw = w / 2 * (r * r - t * t) / (r * r);
            }
            if (hw < 1)
                hw = 1;
            cv_rect(cx - hw, top + y, 2 * hw + 1, 1, line);
            if (hw > 2) {
                cv_rect(cx - hw + 2, top + y, hw - 2, 1, shine);
                cv_rect(cx, top + y, hw - 1, 1, steel);
            }
        }
        cv_rect(x0 + 2, top, w - 3, 2, line);
        ic_disc(cx, top + h * 2 / 5, w / 7 + 1, w / 7 + 1, line);
        ic_disc(cx - 1, top + h * 2 / 5 - 1, w / 7 - 2, w / 7 - 2, shine);
        for (i = 0; i < 4; i++)
            cv_rect(x0 + 5 + i * (w - 12) / 3, top + 4, 2, 2, line);
        cy = top + h * 2 / 5;
        rx = w / 2 + 6;
    } else {                                         /* SPRING: a bare coil, longer with SIZE */
        int32_t len = 36 + (int32_t)sz * 64 / 127, x0 = 118, turns = 4 + (int32_t)sz * 6 / 127, ry = 15, pass;
        cy = (VY0 + VY1) / 2 + 4;
        for (pass = 0; pass < 2; pass++) {           /* (its far side first, dim; its near side over it) */
            int32_t k, px = 0, py = 0, N = turns * 24;
            for (k = 0; k <= N; k++) {
                uint32_t ph = (uint32_t)k * 0x0AAAAAABu;   /* (24 steps a turn) */
                int32_t x = x0 + len * k / N + ((vz_sin(ph) * (len / turns * 3 / 5)) >> 15);   /* (leaning) */
                int32_t y = cy - ((vz_sin(ph + 0x40000000u) * ry) >> 15), near = vz_sin(ph) > 0;
                if (k && near == pass)
                    cv_line_t(px, py, x, y, pass ? T_THEME : ux_mix(vz_bg(), T_THEME, 55), pass ? 2 : 1);
                px = x;
                py = y;
            }
        }
        cv_line_t(x0 - 4, cy + ry, x0, cy + ry, T_THEME, 2);   /* (its hooks) */
        cv_line_t(x0 + len, cy - ry, x0 + len + 4, cy - ry, T_THEME, 2);
        cx = x0 + len / 2;
        rx = len / 2 + 6;
    }
    for (i = 1; i <= n; i++) {                       /* the echoes: LEVEL their strength, DAMPING fading them sooner */
        int32_t al = ((int32_t)lv + 30) * 70 / 157 - i * (20 + (int32_t)dp * 40 / 127) / n;
        if (al > 4 && cx + rx + i * 5 < VX1)
            vz_arc(cx, cy, rx + i * 5, ux_mix(vz_bg(), T_THEME, al));
    }
}
/* the delay's echoes: each a bar, FEEDBACK's power of the first; PING PONG left above, right below; TAPE wobbles;
 * TONE darkens the later ones; (DUB) the pitch climbing or falling, GLITCH breaking some; (SEND) the echoes into a wash */
static void viz_echoes(int dub, int send)
{
    uint32_t ty = fx.delay_type < NDELAY ? fx.delay_type : 0u, n;
    int32_t gap = 12 + (int32_t)par[PAR_FX_P1] * 44 / 127, lvl = 30 * 65536 / 34, fb = (int32_t)par[PAR_FX_P4];
    int32_t tone = (int32_t)par[PAR_FX_P3], x = VX0 + 4, pp = ty == DELAY_PINGPONG || ty == DELAY_TAPEPP, tape = ty >= DELAY_TAPE;
    uint32_t h = 0x9E3779B9u;
    static const char *const NM[NDELAY] = {"MONO DELAY", "PING PONG", "TAPE DELAY", "TAPE PING PONG"};
    char b[24];
    vz_panel(dub ? "DELAY: DUB" : send ? "DELAY: ECHOES INTO THE REVERB" : "");
    if (!dub && !send) {                             /* (its type spelled out, big; SYNC's division beside it) */
        b[0] = 0;
        if (fx.d_sync) {
            put_str(b, "SYNC ", sizeof b);
            card_value(b + str_len(b), sizeof b - str_len(b), PAR_FX_P1, par[PAR_FX_P1]);
        }
        cv_text(VX0, VZ_Y + 3, &AF_M, NM[ty], T_THEME);
        if (b[0])
            cv_text_r(VX1, VZ_Y + 6, &AF_S, b, T_MID, vz_bg());
    }
    vz_hguide(VMID, vz_faint());
    if (send && par[PAR_ECHOREV]) {                  /* (the reverb's wash under them) */
        int32_t i, a = (int32_t)par[PAR_ECHOREV] * 16 / 127;
        for (i = 0; i < VN; i++)
            cv_rect(VX0 + i, VMID - a, 1, 2 * a, ux_mix(vz_bg(), T_THEME, 10 + i * 15 / VN));
    }
    cv_rect(x, VMID - 30, 3, VMID, T_ACCENT);            /* (the hit, then its echoes) */
    for (n = 1; n < 24u; n++) {
        int32_t a, ex, side = pp ? ((n & 1u) ? -1 : 1) : 0, w = 3 + (127 - tone) * (int32_t)n / 400;
        x += gap;
        lvl = (int32_t)(((int64_t)lvl * (fb * 3 + 100)) >> 9);   /* (FEEDBACK: the next's share) */
        a = (lvl * 34) >> 16;
        if (x >= VX1 || a < 1)
            break;
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        ex = tape ? x + (int32_t)((h >> 28) & 3u) - 1 : x;
        if (dub && par[PAR_GLITCH] && ((h >> 8) & 127u) < par[PAR_GLITCH] / 2u)
            a /= 3;                                  /* (GLITCH: some broken) */
        if (side < 0)
            cv_rect(ex, VMID - a, w, a, ux_mix(vz_bg(), T_THEME, 90 - (int32_t)n * 3));
        else if (side > 0)
            cv_rect(ex, 62, w, a, ux_mix(vz_bg(), T_THEME, 90 - (int32_t)n * 3));
        else
            cv_rect(ex, VMID - a, w, 2 * a + 1, ux_mix(vz_bg(), T_THEME, 90 - (int32_t)n * 3));
        if (dub && par[PAR_DPITCH])                  /* (PITCH: a tick, higher or lower each time) */
            cv_rect(ex - 1, VMID - 32 + (par[PAR_DPITCH] == 3u ? (int32_t)n * 2 : -(int32_t)n * 2) + 24, w + 2, 2, T_ACCENT);
    }
    if (pp) {
        cv_text(VX0 - 7, VMID - 30, &AF_S, "L", T_MID);
        cv_text(VX0 - 7, VMID + 18, &AF_S, "R", T_MID);
    }
    vz_sparkle(VX0 + 5, VMID - 34, 3);
}
/* a sine through a drive's curve (the FX bus's DRIVE, DIRT): the clean one faint */
static void vz_drive_curve(uint32_t kind, int32_t drive, int32_t mid, int32_t hh, uint16_t c, int fill)
{
    uint32_t i;
    for (i = 0; i < VN; i++) {
        int32_t x = vz_sin(i * (0xFFFFFFFFu / VN)) * 3 / 4, y;
        switch (kind) {                              /* 0 TUBE (the LXR's curve), 1 FOLD, 2 CLIP, 3 FUZZ, 4 DIODE, 5 TAPE */
        case 1: y = shape_one(SH_FOLD, x, drive); break;
        case 2: y = shape_one(SH_CLIP, x, drive); break;
        case 3: y = shape_one(SH_FUZZ, x, drive); break;
        case 4: y = shape_one(SH_ASYM, x, drive); break;
        case 5: y = shape_one(SH_TAPE, x, drive); break;
        default: y = dist_sample(x, dist_k((uint32_t)drive)); break;
        }
        vz_y[i] = vz_yq15(y, mid, hh);
    }
    vz_trace(VX0, VN, mid, c, fill ? vz_fill() : vz_bg());
}
/* a compressor's curve: in -48 .. 0 dB across, out up; THRESHOLD, RATIO, the GAIN after */
static void vz_comp_curve(int32_t thr_db, int32_t ratio_x10, int32_t gain_db, int32_t gr_db10)
{
    uint32_t i;
    int32_t x0 = VX0, w = 120, y0 = VY1, hgt = VY1 - VY0;
    for (i = 0; i < (uint32_t)w; i++)                /* (1:1, faint) */
        cv_pset(x0 + (int32_t)i, y0 - (int32_t)i * hgt / w, vz_faint());
    for (i = 0; i < (uint32_t)w; i++) {
        int32_t in = -48 * 10 + (int32_t)i * 480 / w, out = in;   /* (tenths of a dB) */
        if (ratio_x10 && in > thr_db * 10)
            out = thr_db * 10 + (in - thr_db * 10) * 10 / ratio_x10;
        out += gain_db * 10;
        vz_y[i] = (int16_t)((y0 * 16) - clamp32(out + 480, 0, 480 + 240) * hgt * 16 / 480);
    }
    vz_trace(x0, w, y0, T_THEME, vz_fill());
    cv_rect(x0 + (thr_db + 48) * w / 48, VY0, 1, VY1 - VY0, ux_mix(vz_bg(), T_ACCENT, 60));
    {                                                /* (the gain it takes now: a meter at the right) */
        int32_t gx = VX0 + 150, gw = 18, gh = clamp32(gr_db10 * (VY1 - VY0) / 240, 0, VY1 - VY0);
        cv_rect(gx, VY0, gw, VY1 - VY0, ux_mix(vz_bg(), T_TEXT, 10));
        if (gh)
            cv_rect(gx, VY0, gw, gh, T_ACCENT);
        cv_text(gx + gw + 6, VY0, &AF_S, "GR", T_MID);
    }
}
static void viz_fxbus(void)
{
    uint32_t ty = par[PAR_FX_TYPE] < NFX ? par[PAR_FX_TYPE] : 0u, i;
    char b[40];
    switch (ty) {
    case FX_DRIVE:
        put_str(b, "DRIVE: ", sizeof b);
        put_str(b + 7, L_DIST[fx.dist_type % 3u], sizeof b - 7u);
        vz_panel(b);
        vz_hguide(VMID, vz_faint());
        vz_drive_curve(fx.dist_type % 3u, (int32_t)par[PAR_FX_P1] * 65536 / 127, VMID, 32, T_THEME, 1);
        break;
    case FX_RING:
        vz_panel("RING MOD: TIMES A SINE");
        vz_hguide(VMID, vz_faint());
        for (i = 0; i < VN; i++) {
            int32_t w = vz_sin(i * (0xFFFFFFFFu / VN) * 2u), m = vz_sin(i * (0xFFFFFFFFu / VN) * (3u + par[PAR_FX_P1] / 8u));
            cv_pset(VX0 + (int32_t)i, vz_yq15(m, VMID, 12) >> 4, vz_faint());
            vz_y[i] = vz_yq15((int32_t)(((int64_t)w * m) >> 15), VMID, 32);
        }
        vz_trace(VX0, VN, VMID, T_THEME, vz_bg());
        break;
    case FX_COMP: {
        static const uint8_t R10[6] = {10, 20, 30, 40, 60, 80};
        put_str(b, "COMPRESSOR ", sizeof b);
        put_str(b + str_len(b), L_RATIO[fx.ratio % 6u], sizeof b - str_len(b));
        vz_panel(b);
        vz_comp_curve(-(int32_t)(127u - par[PAR_FX_P3]) * 48 / 127, R10[fx.ratio % 6u], 0, 0);
        break;
    }
    case FX_DELAY: viz_echoes(0, 0); break;
    case FX_PHASER: case FX_FLANGER: {
        uint32_t k, nn = ty == FX_PHASER ? 2u : 9u;
        vz_panel(ty == FX_PHASER ? "PHASER: NOTCHES SWEEPING" : "FLANGER: A COMB SWEEPING");
        for (k = 0; k < 3u; k++) {
            for (i = 0; i < VN; i++) {
                uint32_t ph = (i + k * (6u + par[PAR_FX_P1] / 8u)) * (0x80000000u / VN) * nn;
                int32_t c = vz_sin(ph), dip = (32767 - iabs(c)) >> 7;
                dip = (int32_t)(((int64_t)dip * dip * dip) >> 16) * (int32_t)par[PAR_FX_P2] / 127;
                dip = dip * (40 + (int32_t)par[PAR_FX_P3]) / 167;
                vz_y[i] = (int16_t)((VY0 + 8) * 16 + dip * 8);
            }
            if (k == 0u)
                vz_trace(VX0, VN, VY0 + 8, T_THEME, vz_bg());
            else
                for (i = 0; i < VN; i++)
                    cv_pset(VX0 + (int32_t)i, vz_y[i] >> 4, ux_mix(vz_bg(), T_THEME, 45 - (int32_t)k * 15));
        }
        break;
    }
    default:
        vz_panel("THE FX BUS IS OFF");
        for (i = 0; i < VN; i++)
            vz_y[i] = vz_yq15(vz_sin(i * (0xFFFFFFFFu / VN) * 2u), VMID, 24);
        vz_trace(VX0, VN, VMID, vz_faint(), vz_bg());
        break;
    }
}
/* DIRT: a sine through the master's (its type, by AMOUNT); the bus's drive named */
static void viz_dirt(void)
{
    static const uint8_t K[5] = {5, 0, 3, 4, 1};     /* (L_DIRT: TAPE TUBE FUZZ DIODE FOLD as vz_drive_curve's) */
    uint32_t t = par[PAR_MDIRT] % 5u, a = par[PAR_MDIRT_AMT], bus = par[PAR_FXDIRT];
    char b[40];
    put_str(b, "MASTER: ", sizeof b);
    put_str(b + 8, a ? L_DIRT[t] : "CLEAN", sizeof b - 8u);
    vz_panel(b);
    vz_hguide(VMID, vz_faint());
    if (bus) {                                       /* (the bus's drive: its own, before its type) */
        put_str(b, "BUS DRIVE ", sizeof b);
        put_int(b + 10, (int32_t)bus);
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
    }
    vz_drive_curve(K[t], (int32_t)a * 65536 / 127, VMID, 32, T_THEME, 1);
}
/* the master COMPRESSOR: its curve, the gain it takes; SIDECHAIN: the key's hits ducking the mix */
static void viz_mcomp(int side)
{
    static const uint8_t R10[NCRATIO] = {0, 15, 20, 30, 40, 60, 100, 250};
    int32_t gr10 = (int32_t)(((int64_t)mc.gr * 602) >> 16);
    char b[40];
    if (!side) {
        put_str(b, mc.ratio ? L_CRATIO[mc.ratio < NCRATIO ? mc.ratio : 0] : "OFF: RATIO TURNS IT ON", sizeof b);
        vz_panel(b);
        vz_comp_curve(-(int32_t)((127u - par[PAR_MC_THR]) * 48u + 63u) / 127, R10[mc.ratio < NCRATIO ? mc.ratio : 0],
                      (int32_t)((par[PAR_MC_GAIN] * 24u + 63u) / 127u), gr10);
    } else {
        uint32_t s, src = mc.src <= NVOICE ? mc.src : 0u;
        const pattern_t *pt = shown_pat();
        vz_panel(src ? "THE KEY'S HITS DUCK THE MIX" : "KEYED BY THE MIX ITSELF");
        vz_hguide(VY0 + 8, vz_faint());
        for (s = 0; s < 16u; s++) {                  /* (the key's hits; the gain dipping after each) */
            int32_t x = VX0 + (int32_t)s * (VN / 16), k;
            int on = src ? main_on(pt, src - 1u, ui.bar * 16u + s) : !(s & 3u);
            if (!on)
                continue;
            cv_rect(x, VY1 - 12, 3, 12, src ? TRACK_COL[src - 1u] : T_MID);
            for (k = 0; k < VN / 16 && x + k < VX1; k++)
                cv_rect(x + k, VY0 + 8, 1, (VN / 16 - k) * (mc.ratio ? 24 : 0) / (VN / 16), ux_mix(vz_bg(), T_THEME, 50));
        }
        put_str(b, "-", sizeof b);
        put_int(b + 1, gr10 / 10);
        put_str(b + str_len(b), ".", sizeof b - str_len(b));
        put_int(b + str_len(b), gr10 % 10);
        put_str(b + str_len(b), " dB", sizeof b - str_len(b));
        cv_text_r(VX1, VZ_Y + 4 + 14, &AF_S, b, T_MID, vz_bg());
    }
}
