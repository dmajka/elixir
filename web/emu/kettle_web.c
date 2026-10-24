/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (the design: Felucca's web/emu/felucca_web.c,
 * after X0X's browser build, charlesvestal/fm1-x0x, GPL-3.0)
 * Kettle in the browser: the firmware's sources compiled to WebAssembly and run in an AudioWorklet against a
 * simulated FM-1:
 *   - the device clock: TIMER4 ticks (24 MHz) and fm1_ms advance with the audio the worklet asks for; the audio
 *     ISR (audio.c fm1_alnk0_irq, the real one) renders each I2S half as the DMA would ask for it, and the main loop
 *     (main.c's, web_frame) runs every 15 ms, with ui_input every millisecond in between;
 *   - the panel: buttons and encoders by their printed labels (panel.c), the keys as fm1_in.notes, the LEDs read
 *     back from fm1_led / fm1_led_dim / fm1_led_breath / fm1_led_mid through the key matrix;
 *   - the screen: lcd_fill / lcd_blit into a 240 x 240 RGB565 frame;
 *   - the flash: a 1 MiB NOR image behind storage.c's hooks (settings, projects, the session); the page keeps its
 *     sectors in the browser and hands them back at boot.
 * Not here: USB (audio, MIDI out is collected for the page: web_midi_out), the TRS jack, updates.
 * Also built natively (tests/ui_test.c) for the screen and input tests. */
#include <stdint.h>
#include <string.h>
#undef memset                            /* the firmware's own (libc.c), apart from the host's */
#undef memcpy
#undef memcmp
#define memset kettle_memset
#define memcpy kettle_memcpy
#define memcmp kettle_memcmp

#define __attribute__(x)
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 1
#define FELUCCA_CPU2 0
#ifndef KETTLE_VERSION
#define KETTLE_VERSION "WEB"
#endif
#define RING_PUBLISH() ((void)0)
#define POOL
static volatile uint32_t fm1_ms;
struct { uint32_t magic, failed, pending; } bootguard;
#define BOOTGUARD_MAGIC 0x42475244u

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u
static uint32_t web_ticks;
static uint32_t fm1_ticks(void) { return web_ticks; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }

/* --------------------------------------------------------- the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h: key id at (column, row bit) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static struct { volatile uint32_t notes, buttons; } fm1_in;
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t web_dim_lo;
static void fm1_led_dim_level(uint32_t lo) { web_dim_lo = lo; }
static uint32_t web_anim_run, web_anim_t0;
static void fm1_led_anim_start(uint32_t glow) { (void)glow; web_anim_run = 1; web_anim_t0 = web_ticks; }
static int fm1_led_anim_on(void)
{
    if (web_anim_run && (web_ticks - web_anim_t0 > 700u * 1000u * FM1_TICKS_PER_US || fm1_in.notes || fm1_in.buttons))
        web_anim_run = 0;
    return (int)web_anim_run;
}
static uint32_t web_pressed, web_released;
static int32_t web_enc_steps[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released)
        *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = web_enc_steps[e % 7u]; web_enc_steps[e % 7u] = 0; return s; }

/* ----------------------------------------------------------- the screen --- */
static uint16_t web_fb[240 * 240];       /* RGB565 as the panel gets it (high byte first) */
static uint32_t web_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = sw;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            web_fb[(y + j) * 240u + x + i] = p[j * w + i];
    web_draws++;
}
static void lcd_sync(void) {}

/* ----------------------------------------------------------- the sources --- */
#include "kettle_tables.h"
#include "lxr_tables.h"
#include "lxr_params.h"
#include "grids_map.h"
#include "../../firmware/src/libc.c"
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/usb.c"
#include "../../firmware/src/midi_uart.c"
#include "../../firmware/src/dsp.c"
#include "../../firmware/src/voices.c"
#include "../../firmware/src/fxbus.c"
#include "../../firmware/src/reverb.c"
#include "../../firmware/src/sound.c"
#include "../../firmware/src/kit.c"
static uint32_t ui_track(void);
static uint32_t ui_pattern(void);
#include "../../firmware/src/seq.c"
#include "../../firmware/src/perform.c"
#include "../../firmware/src/params.c"
#include "../../firmware/src/midi.c"
#include "../../firmware/src/engine.c"

/* the audio ISR (audio.c) against the simulated ALNK0 */
static uint32_t web_half;
#define FM1_AUDIO_HALF 1u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return web_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
void isr_alnk0(void) {}
#include "../../firmware/src/audio.c"

/* ------------------------------------------------------------ the flash --- */
static uint8_t nor[0x100000];            /* the SPI NOR (erased: 0xFF) */
static uint32_t web_flash_writes;
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    web_flash_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    web_flash_writes++;
    return 0;
}
static void flash_hush(void) { out_hush = 1; }  /* (the page's flash is instant: the output keeps its ramp) */
static void flash_unhush(void) { out_hush = 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
#define FL_FAR(fn) (fn)
#include "../../firmware/src/storage.c"
#include "../../firmware/src/project.c"

/* ------------------------------------------------- the main loop's HAL --- */
static uint32_t web_master_adc = 700;   /* MASTER, 0..1023 as the ADC reads it */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620; }
#include "../../firmware/src/master.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
#include "../../firmware/src/ui_draw.c"

/* --------------------------------------------------------- the device --- */
static uint32_t web_booted, web_boot_ms, web_last_frame;

static void web_power_on(void)                  /* main.c fm1_main's boot */
{
    usb.config = 1;                             /* the page is the USB host: MIDI out reaches it (web_midi_out) */
    persist_boot();
    palette_apply();
    draw_splash();
    panel_init();
    engine_init();
    session_boot();
    master_boot();
    audio_init();
    fm1_led_dim_level(kset.leds == 1u);
    fm1_led_anim_start(kset.leds != 0u);
    web_boot_ms = fm1_ms + 600u;
    web_booted = 1;
}
static void web_frame(void)                      /* one pass of main.c's loop */
{
    master_poll();
    batt_poll();
    ui_input();
    settings_poll();
    autosave_poll();
    ui_leds();
    ui_draw();
}

#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;

static void web_ms(void)                         /* a millisecond of the device */
{
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (!web_booted || (int32_t)(fm1_ms - web_boot_ms) < 0)
        return;
    if (web_boot_ms) {
        web_boot_ms = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        ui.force = 1;
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {
        web_last_frame = fm1_ms;
        web_frame();
    } else {
        ui_input();
    }
}

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT void web_render(uint32_t n)               /* n frames (<= 1024): the device runs until they exist */
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }
EXPORT void web_boot(void)
{
    if (!web_booted)
        web_power_on();
}

/* input by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023 */
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask) { fm1_in.notes = mask & ((1u << 27) - 1u); }
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < NE)
        web_enc_steps[panel.enc[role] % 7u] += n * panel.dir[role];
}
EXPORT void web_master(uint32_t v) { web_master_adc = v > 1023u ? 1023u : v; }
EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)   /* a message from Web MIDI */
{
    if (status < 0x80u)
        return 1;
    if (status >= 0xF8u)
        return (uint32_t)midi_enqueue(0xFu | status << 8, 1);
    if (status >= 0xF0u)
        return 1;
    return (uint32_t)midi_enqueue((status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, 1);
}
/* MIDI out (the FM-1 sends it over USB): the packets since the last call, for the page's Web MIDI out */
static uint32_t web_mo[256];
EXPORT uint32_t *web_midi_out(void) { return web_mo; }
EXPORT uint32_t web_midi_out_take(void)
{
    uint32_t n = 0;
    while (mo_r != mo_w && n < 256u)
        web_mo[n++] = midi_out_q[mo_r++ % MQ];
    return n;
}

/* the LEDs: bit b = the button labelled b lit (bit 14: PLAY's green), keys bit k */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void)
{
    return led_buttons(fm1_led) | ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) << 14;
}
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void) { return led_buttons(fm1_led_dim); }
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim); }
EXPORT uint32_t web_dim_level(void) { return web_dim_lo; }
EXPORT uint32_t web_breath_buttons(void) { return led_buttons(fm1_led_breath); }
EXPORT uint32_t web_breath_keys(void) { return led_keys(fm1_led_breath); }
EXPORT uint32_t web_mid_keys(void) { return led_keys(fm1_led_mid); }
EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return sq.running; }
EXPORT uint32_t web_sample_rate(void) { return FS; }
EXPORT uint32_t web_cpu_q8(void) { return cpu_q8; }
