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
#define HALF_TICKS (HALF_FRAMES * 1000000u / FS * FM1_TICKS_PER_US)
static uint16_t ld_q8[NLD], ld_rem[NLD], ld_pk[NLD]; /* 17a: each part's load, 1/256 of a half: the average (as cpu_q8),
                                                     * the peak since CONFIG opened (ui.c clears it with audio_max_us) */

/* 17b, 17c: the second core. Started at power-on (main.c cores_boot); while it answers, it renders D1-D3 each block
 * (voices.c c1_drums). cores_why: why there is one core */
enum { CW_TWO, CW_BUILD, CW_HELD, CW_BAD, CW_NOANS, CW_FAULT, CW_CRASH, CW_NOSTART };
static uint8_t cores_why = CW_BUILD;
static uint32_t c1_stack(void)
{
#if FELUCCA_CPU2
    return fm1_cpu1_stack_used();
#else
    return 0;
#endif
}
#ifdef LOAD_CLOCK
/* TIMER4 less the ticks TIMER5 spent nested in this ISR. The two read again if TIMER5 came between them: else the
 * clock stepped back by a scan (TIMER5's ticks counted, the time not), a short part's time wrapped below 0 and it
 * showed thousands of % (0.15-test2: REVERB while morphing) */
static uint32_t load_clock(void)
{
    uint32_t n, t;
    do {
        n = t5_nested_ticks;
        t = fm1_ticks();
    } while (n != t5_nested_ticks);
    return t - n;
}
#endif

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
#ifdef LOAD_CLOCK
        ld_t = t0;
        for (i = 0; i < NLD; i++)
            ld_acc[i] = 0;
#endif
        for (b = 0; b < HALF_FRAMES; b += BLOCK)
            engine_block(o + 2u * b);
        for (i = 0; i < HALF_WORDS; i++)
            o[i] *= 1 << OUT_SHIFT;
        LOAD_MARK(LD_MIX);
#ifdef LOAD_CLOCK
        for (i = 0; i < NLD; i++) {
            uint32_t q = (ld_acc[i] < 4u * HALF_TICKS ? ld_acc[i] : 4u * HALF_TICKS) * 256u / HALF_TICKS, l = ld_q8[i] * (CPU_AVG - 1u) + q + ld_rem[i];
            ld_q8[i] = (uint16_t)(l / CPU_AVG);
            ld_rem[i] = (uint16_t)(l % CPU_AVG);
            if (q > ld_pk[i])
                ld_pk[i] = (uint16_t)(q > 0xFFFFu ? 0xFFFFu : q);
        }
#endif
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
