/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Elixir: Felucca's I2S output (ALNK0 -> the codec) and its audio ISR: per half buffer (HALF_FRAMES), the
 * engine's blocks (engine.c, BLOCK each), then Q15 -> 24 bit. The load meter as Felucca's. */
#define HALF_FRAMES 128u          /* two of the engine's blocks: 2.9 ms at 44.1 kHz */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define CPU_AVG 32u
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling (Felucca's) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint32_t audio_halves, audio_max_us, audio_late;
static volatile uint32_t t5_nested_ticks;          /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static volatile uint8_t in_audio;
static uint32_t cpu_q8, audio_cpu_rem;              /* the audio ISR's load, 1/256 */

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    in_audio = 1;
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, i, us, all;
        int32_t *o = &abuf[half * HALF_WORDS];
        for (b = 0; b < HALF_FRAMES; b += BLOCK)
            engine_block(o + 2u * b);
        for (i = 0; i < HALF_WORDS; i++)
            o[i] *= 1 << OUT_SHIFT;
        fm1_audio_ack_half();
        audio_halves++;
        all = fm1_ticks() - t0;
        us = (all - t5_nested_ticks) / FM1_TICKS_PER_US;
        if (us > audio_max_us)
            audio_max_us = us;
        {
            uint32_t load = cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half && !out_hush)
            audio_late++;                          /* the DMA moved on while we rendered (a save's erases: not
                                                    * counted, they are not the engine's load) */
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
