/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: The Firmware Hub's card picture (docs/card.png, fmhub.json's "image"): the firmware's own screen, drawn by the
 * browser build (web/emu/elixir_web.c) natively, as tests/ui_test.c does. A beat with the tracks on their own clocks
 * and lengths, playing, so each track's playhead sits on a different step; the selected track's TRACK page below.
 *   cc -O1 -I build/gen -I firmware/src tools/card_shot.c -o build/card_shot -lz && build/card_shot docs/card.png */
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "../web/emu/elixir_web.c"

static uint32_t btns, keys;

static void run_ms(uint32_t ms)
{
    uint32_t n = ms * FS / 1000u;
    while (n) {
        uint32_t k = n > 512u ? 512u : n;
        web_render(k);
        n -= k;
    }
}
static void press(uint32_t b) { btns |= 1u << b; web_buttons(btns); run_ms(40); btns &= ~(1u << b); web_buttons(btns); run_ms(40); }
static void tap_key(uint32_t k) { keys |= 1u << k; web_keys(keys); run_ms(40); keys &= ~(1u << k); web_keys(keys); run_ms(40); }
static void turn(uint32_t role, int32_t n) { int32_t i; for (i = 0; i < (n < 0 ? -n : n); i++) { web_enc(role, n < 0 ? -1 : 1); run_ms(30); } }

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); }
static void chunk(FILE *f, const char *kind, const uint8_t *body, uint32_t n)
{
    uint32_t c = (uint32_t)crc32(crc32(0, (const Bytef *)kind, 4), body, n);
    put32(f, n);
    fwrite(kind, 1, 4, f);
    fwrite(body, 1, n, f);
    put32(f, c);
}
static void save_png(const char *path)               /* the screen, 240 x 240 RGB */
{
    static uint8_t raw[240u * (1u + 240u * 3u)], z[sizeof raw + 1024u];
    static const uint8_t ihdr[13] = {0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
    uLongf zn = sizeof z;
    uint32_t x, y;
    FILE *f;
    for (y = 0; y < 240u; y++) {
        uint8_t *r = raw + y * (1u + 240u * 3u);
        r[0] = 0;
        for (x = 0; x < 240u; x++) {
            uint16_t px = (uint16_t)((web_fb[y * 240u + x] >> 8) | (web_fb[y * 240u + x] << 8));
            r[1 + x * 3] = (uint8_t)((px >> 11) * 255 / 31);
            r[2 + x * 3] = (uint8_t)(((px >> 5) & 63) * 255 / 63);
            r[3 + x * 3] = (uint8_t)((px & 31) * 255 / 31);
        }
    }
    compress2(z, &zn, raw, sizeof raw, 9);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", ihdr, sizeof ihdr);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", z, 0);
    fclose(f);
}

static void hits(pattern_t *p, uint32_t t, uint32_t len, uint32_t clk, const char *steps)
{                                                    /* steps: one char a step, '.' off, '1'-'9' a velocity */
    uint32_t s;
    p->len[t] = (uint8_t)len;
    p->clk[t] = (uint8_t)clk;
    for (s = 0; steps[s] && s < len; s++)
        if (steps[s] != '.') {
            main_set(p, t, s, 1);
            p->step[t][s].volume = (uint8_t)((p->step[t][s].volume & ~STEP_VOLUME_MASK) | ((steps[s] - '0') * 127u / 9u));
        }
}

int main(int argc, char **argv)
{
    pattern_t *p = &pat[0];
    uint32_t i;
    web_nor_erase();
    web_boot();
    run_ms(1600);
    for (i = 0; i < NUM_PATTERN; i++)
        seq_clear_pattern(&pat[i]);
    sq.active = sq.pending = 0;
    par_set(PAR_BPM, 120);
    par[PAR_SHUFFLE] = 0;
    /* the clocks (seq.c CLK_TICKS): /4 /3 /2 2/3 3/4 1x 4/3 3/2 x2 x3 x4 */
    hits(p, 0, 16, 5, "9.....7.9.....5.");            /* D1  1x */
    hits(p, 1, 12, 4, "..6..4...7..");                /* D2  3/4: 12 steps in a bar */
    hits(p, 2, 10, 3, "5...3..6..");                  /* D3  2/3 */
    hits(p, 3, 16, 5, "....9.......9..3");            /* SN  1x */
    hits(p, 4, 8, 2, "6...3...");                     /* CY  /2 */
    hits(p, 5, 16, 8, "7393739373937393");            /* CH  x2: 32nds */
    hits(p, 6, 14, 7, "..5....6..5...");              /* OH  3/2 */
    pat_backup = *p;
    run_ms(20);
    tap_key(BLACK_KEY[5]);                           /* the closed hat selected: its clock on the TRACK page */
    press(B_PLAY);
    run_ms(3170);
    for (i = 0; i < 6u && ui.mode != M_EUCLID; i++)
        press(B_SEQ);
    turn(EN_SELECT, (int32_t)PG_TRACK - (int32_t)ui.page);
    run_ms(60);
    save_png(argc > 1 ? argv[1] : "docs/card.png");
    return 0;
}
