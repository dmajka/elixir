/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host test: the fixed-point voices against the LXR's own float voices (tests/lxrref). For each voice and
 * a set of sounds (the init kit, and variations of oscillator, FM, filter, envelope and drive settings): trigger
 * both, render 0.4 s, compare the envelopes (RMS in 5.8 ms windows, dB) and, where no noise is in the sound, the
 * waveforms (correlation). Built by tests/run.sh with the LXR tree at LXR_SRC. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static int16_t cap[6][64];
#define VOICE_CAPTURE(v, b) memcpy(cap[v], b, sizeof cap[v])
#include "engine_host.h"

void ref_init(uint32_t seed);
void ref_set(uint32_t p, uint32_t itg);
void ref_trigger(uint32_t track, uint32_t vel, uint32_t note);
void ref_block(int16_t out[6][64]);

#define NBLK 276                                     /* 0.4 s */
static int16_t a[6][NBLK * 64], b[6][NBLK * 64];

static void set_both(uint32_t p, uint32_t v)
{
    par[p] = (uint8_t)v;
    snd_refresh(p);
    ref_set(p, v);
}

/* one case: the kit as the init kit, the overrides, then a trigger of track t */
static int run_case(const char *name, uint32_t t, const uint16_t (*ov)[2], uint32_t nov, int noisy)
{
    uint32_t i, k, vi = t < 6 ? t : 5;
    int16_t rb[6][64];
    double num = 0, da = 0, db = 0, worst = 0;
    int fail = 0;
    engine_init();
    ref_init(0x9E3779B9u);
    noise_rng = 0x9E3779B9u;
    for (i = 1; i < END_OF_SOUND_PARAMETERS; i++)
        if (!(i >= PAR_FREQ_LFO1 && i <= PAR_AMOUNT_LFO6))
            set_both(i, par[i]);
    for (i = 0; i < nov; i++)
        set_both(ov[i][0], ov[i][1]);
    for (i = 0; i < 6; i++) {                        /* no velocity / LFO modulation, no decimation: the voices alone */
        velo_mod[i].dest = lfo_mod[i].dest = 0;
    }
    for (k = 0; k < 40; k++) {                       /* settle the smoothers (the LXR's start at 0 after boot) */
        voices_block();
        ref_block(rb);
    }
    voice_trigger(t, 127, SEQ_DEFAULT_NOTE);
    ref_trigger(t, 127, SEQ_DEFAULT_NOTE);
    for (k = 0; k < NBLK; k++) {
        voices_block();
        ref_block(rb);
        memcpy(&a[vi][k * 64], cap[vi], sizeof cap[vi]);
        memcpy(&b[vi][k * 64], rb[vi], sizeof rb[vi]);
        if (getenv("EGDUMP") && k < 12)
            printf("    blk %u  gain %d state %d val %d rcnt %d\n", k, vc[vi].gain, vc[vi].aeg.state, vc[vi].aeg.value,
                   vc[vi].aeg.rcnt);
    }
    if (getenv("SDUMP") && !strcmp(getenv("SDUMP"), name))
        for (i = 0; i < (getenv("SSTEP") ? 2000u : 24u); i += getenv("SSTEP") ? 100u : 1u)
            printf("    %3u  port %6d  lxr %6d\n", i, a[vi][i], b[vi][i]);
    for (i = 0; i < NBLK * 64u; i++) {
        num += (double)a[vi][i] * b[vi][i];
        da += (double)a[vi][i] * a[vi][i];
        db += (double)b[vi][i] * b[vi][i];
    }
    for (k = 0; k + 256 <= NBLK * 64u; k += 256) {   /* the envelopes */
        double ea = 0, eb = 0, d;
        for (i = k; i < k + 256; i++) {
            ea += (double)a[vi][i] * a[vi][i];
            eb += (double)b[vi][i] * b[vi][i];
        }
        ea = 10 * log10(ea / 256 + 1e-9);
        eb = 10 * log10(eb / 256 + 1e-9);
        if (eb < 10 * log10(32768.0 * 32768.0) - 50)   /* below -50 dBFS: not compared */
            continue;
        d = fabs(ea - eb);
        if (getenv("ENVDUMP") && d > 1.0)
            printf("    win %4u  port %6.1f  lxr %6.1f\n", k / 256, ea, eb);
        if (d > worst)
            worst = d;
    }
    {
        double corr = num / sqrt(da * db + 1e-9);
        double ra = 10 * log10(da / (NBLK * 64.0) + 1e-9), rbb = 10 * log10(db / (NBLK * 64.0) + 1e-9);
        /* noise: the RNG draws drift apart between the two (the float and the fixed noise clocks wrap a sample apart
         * now and then), so only the level and the envelope compare, with room for two noise realisations */
        fail = noisy ? worst > 4.0 || fabs(ra - rbb) > 2.5 : worst > 1.5 || fabs(ra - rbb) > 1.0 || corr < 0.98;
        printf("  %-22s %-7s level %6.1f / %6.1f dB  env worst %4.2f dB  corr %6.3f%s\n", name, VOICE_NAME[t], ra,
               rbb, worst, corr, fail ? "  FAIL" : "");
    }
    return fail;
}

int main(void)
{
    static const uint16_t NONE[1][2] = {{0, 0}};
    static const uint16_t KICK_FM[][2] = {{PAR_MIX_MOD_1, 0}, {PAR_FMAMNT1, 90}, {PAR_FM_FREQ1, 72}, {PAR_MOD_WAVE_DRUM1, 0}};
    static const uint16_t KICK_SQR[][2] = {{PAR_OSC_WAVE_DRUM1, 3}, {PAR_FILTER_TYPE_1, 0}, {PAR_FILTER_FREQ_1, 60}, {PAR_RESO_1, 100}};
    static const uint16_t KICK_HP[][2] = {{PAR_OSC_WAVE_DRUM1, 2}, {PAR_FILTER_TYPE_1, 1}, {PAR_FILTER_FREQ_1, 40}, {PAR_RESO_1, 60}, {PAR_FILTER_DRIVE_1, 90}};
    static const uint16_t KICK_BP[][2] = {{PAR_OSC_WAVE_DRUM1, 5}, {PAR_PWM_1, 40}, {PAR_FILTER_TYPE_1, 2}, {PAR_FILTER_FREQ_1, 80}, {PAR_RESO_1, 90}};
    static const uint16_t KICK_LP2[][2] = {{PAR_OSC_WAVE_DRUM1, 2}, {PAR_FILTER_TYPE_1, 6}, {PAR_FILTER_FREQ_1, 70}, {PAR_RESO_1, 110}};
    static const uint16_t KICK_DRV[][2] = {{PAR_DRIVE1, 120}, {PAR_VELOA1, 20}, {PAR_VOL_SLOPE1, 100}, {PAR_MODAMNT1, 90}, {PAR_PITCH_SLOPE1, 10}};
    static const uint16_t KICK_TR[][2] = {{PAR_TRANS1_WAVE, 5}, {PAR_TRANS1_VOL, 100}, {PAR_TRANS1_FREQ, 50}};
    static const uint16_t KICK_SNAP[][2] = {{PAR_TRANS1_WAVE, 0}, {PAR_TRANS1_VOL, 90}, {PAR_TRANS1_FREQ, 30}};
    static const uint16_t SN_TONE[][2] = {{PAR_MIX1, 0}, {PAR_OSC_WAVE_SNARE, 0}, {PAR_MODAMNT4, 60}};
    static const uint16_t CLAP[][2] = {{PAR_REPEAT4, 4}, {PAR_VELOA4, 8}, {PAR_VELOD4, 40}, {PAR_VOL_SLOPE4, 8},
                                       {PAR_FILTER_TYPE_4, 2}, {PAR_FILTER_FREQ_4, 80}, {PAR_RESO_4, 80}, {PAR_MIX1, 127}};
    static const uint16_t CYM_SIN_NOF[][2] = {{PAR_WAVE1_CYM, 0}, {PAR_WAVE3_CYM, 0}, {PAR_MOD_OSC_GAIN1_CYM, 100}, {PAR_MOD_OSC_GAIN2_CYM, 60}, {PAR_FILTER_TYPE_5, 7}};
    static const uint16_t CYM_SIN_NOFM[][2] = {{PAR_WAVE1_CYM, 0}, {PAR_WAVE3_CYM, 0}, {PAR_FILTER_TYPE_5, 7}};
    static const uint16_t CYM_NOI_NOF[][2] = {{PAR_FILTER_TYPE_5, 7}};
    static const uint16_t CYM_NOI_HP[][2] = {{PAR_FILTER_TYPE_5, 1}};
    static const uint16_t CYM_SQ[][2] = {{PAR_WAVE1_CYM, 3}, {PAR_WAVE2_CYM, 2}, {PAR_WAVE3_CYM, 1}, {PAR_FILTER_TYPE_5, 5}, {PAR_RESO_5, 90}, {PAR_REPEAT5, 2}};
    static const uint16_t HAT_SIN[][2] = {{PAR_WAVE3_HH, 2}, {PAR_MOD_OSC_GAIN1, 120}, {PAR_FILTER_TYPE_6, 3}, {PAR_RESO_6, 70}};
    static const uint16_t SN_MIX[][2] = {{PAR_MIX1, 127}};
    static const uint16_t SN_BP[][2] = {{PAR_FILTER_TYPE_4, 2}, {PAR_FILTER_FREQ_4, 80}, {PAR_RESO_4, 80}};
    static const uint16_t SN_REP[][2] = {{PAR_REPEAT4, 4}, {PAR_VELOA4, 8}, {PAR_VELOD4, 40}, {PAR_VOL_SLOPE4, 8}};
    int fails = 0;
    uint32_t t;
    printf("compare: the port against the LXR's float voices\n");
    for (t = 0; t < 3; t++)
        fails += run_case("init kit", t, NONE, 0, 0);
    fails += run_case("init kit", 3, NONE, 0, 1);
    fails += run_case("init kit", 4, NONE, 0, 1);
    fails += run_case("init kit", 5, NONE, 0, 1);
    fails += run_case("init kit (open)", 6, NONE, 0, 1);
    fails += run_case("FM", 0, KICK_FM, 4, 0);
    fails += run_case("square LP reso", 0, KICK_SQR, 4, 0);
    fails += run_case("saw HP drive", 0, KICK_HP, 5, 0);
    fails += run_case("PWM BP", 0, KICK_BP, 5, 0);
    fails += run_case("saw LP2", 0, KICK_LP2, 4, 0);
    fails += run_case("drive attack slope", 0, KICK_DRV, 5, 0);
    fails += run_case("transient kick", 0, KICK_TR, 3, 0);
    fails += run_case("snap", 0, KICK_SNAP, 3, 0);
    fails += run_case("tonal snare", 3, SN_TONE, 3, 0);
    fails += run_case("snare noise only", 3, SN_MIX, 1, 1);
    fails += run_case("snare BP", 3, SN_BP, 3, 1);
    fails += run_case("snare repeat", 3, SN_REP, 4, 1);
    fails += run_case("clap (repeat)", 3, CLAP, 8, 1);
    fails += run_case("cym sine FM no filter", 4, CYM_SIN_NOF, 5, 0);
    fails += run_case("cym sine no FM no flt", 4, CYM_SIN_NOFM, 3, 0);
    fails += run_case("cym noise no filter", 4, CYM_NOI_NOF, 1, 1);
    fails += run_case("cym noise HP", 4, CYM_NOI_HP, 1, 1);
    fails += run_case("cymbal sq/saw/tri peak", 4, CYM_SQ, 6, 0);
    fails += run_case("hat saw UBP", 5, HAT_SIN, 4, 0);
    printf(fails ? "compare_test: %d FAILED\n" : "compare_test: ok\n", fails);
    return fails != 0;
}
