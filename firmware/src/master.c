/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Elixir: Felucca's MASTER pot: read before the audio starts (the first block at its level), then smoothed per UI
 * frame; the curve x^2 (Felucca's master_of_pot) into the engine's master_q12. */
static int32_t master_knob = 512 * 16;
static inline uint32_t master_of_pot(uint32_t k10) { return (k10 * k10) >> 8; }
static void master_poll(void)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a >= 0) {
        master_knob += (a * 16 - master_knob) / 8;
        master_q12 = (int32_t)master_of_pot((uint32_t)(master_knob / 16));
    }
}
/* Elixir 0.5.1: the battery, as Felucca reads it (its main loop, ui_draw.c batt_level): ADC channel 3 (the battery's
 * divider) through a slow IIR, the stock firmware's thresholds 531 / 561 / 591 for 0..3 bars. On USB power it charges
 * (Felucca shows a bolt): 4 */
static int32_t batt_raw;
static void batt_poll(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        batt_raw = batt_raw ? batt_raw + (b - batt_raw) / 32 : b;
}
static int32_t batt_level(void)
{
    if (usb.config && !usb.suspended)
        return 4;
    return batt_raw >= 591 ? 3 : batt_raw >= 561 ? 2 : batt_raw >= 531 ? 1 : 0;
}
static void master_boot(void)
{
    uint32_t i;
    (void)fm1_adc_read(FM1_ADC_MASTER);                 /* (the first conversion after the pin setup: dropped) */
    for (i = 0; i < 3u; i++) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) {
            master_knob = a * 16;
            master_q12 = (int32_t)master_of_pot((uint32_t)a);
            return;
        }
    }
}
