/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir host test: the browser build (web/emu/elixir_web.c) natively: boot, play with the panel as a user would,
 * check what the sequencer and the sound did, and write the screen of each step to build/shots/NN-name.ppm.
 *   cc -O1 -I build/gen -I firmware/src tests/ui_test.c -o build/ui_test && build/ui_test build/shots */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../web/emu/elixir_web.c"

static const char *shots = "build/shots";
static int shot_n, fails;
static uint32_t btns, keys;

static void run_ms(uint32_t ms)                      /* the device for ms milliseconds (the worklet's pace) */
{
    uint32_t n = ms * FS / 1000u;
    while (n) {
        uint32_t k = n > 512u ? 512u : n;
        web_render(k);
        n -= k;
    }
}
static void shot(const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%02d-%s.ppm", shots, shot_n++, name);
    f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t px = (uint16_t)((web_fb[i] >> 8) | (web_fb[i] << 8));
        uint8_t rgb[3] = {(uint8_t)((px >> 11) * 255 / 31), (uint8_t)(((px >> 5) & 63) * 255 / 63),
                          (uint8_t)((px & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void press(uint32_t b) { btns |= 1u << b; web_buttons(btns); run_ms(40); btns &= ~(1u << b); web_buttons(btns); run_ms(40); }
static void hold(uint32_t b) { btns |= 1u << b; web_buttons(btns); run_ms(40); }
static void release(uint32_t b) { btns &= ~(1u << b); web_buttons(btns); run_ms(40); }
static void tap_key(uint32_t k) { keys |= 1u << k; web_keys(keys); run_ms(40); keys &= ~(1u << k); web_keys(keys); run_ms(40); }
static void white(uint32_t w) { tap_key(WHITE_KEY[w]); }
static void black(uint32_t b) { tap_key(BLACK_KEY[b]); }
static void turn(uint32_t role, int32_t n) { int32_t i; for (i = 0; i < (n < 0 ? -n : n); i++) { web_enc(role, n < 0 ? -1 : 1); run_ms(30); } }
static uint32_t pj_len(uint32_t l)                   /* a project's payload in flash, bytes */
{
    proj_hdr_t h;
    return proj_exists(l) && !hdr_read((uint32_t)pj.phys[l], &h) ? h.len : 0u;
}
static void check(int ok, const char *what);
/* 25-0: the kit slots are packed (kit.c kit_get / kit_put): one value of slot n */
static uint32_t kit_at(uint32_t n, uint32_t i) { uint8_t k[KIT_LEN]; kit_get(n, k); return k[i]; }
static void kit_set1(uint32_t n, uint32_t i, uint32_t v) { uint8_t k[KIT_LEN]; kit_get(n, k); k[i] = (uint8_t)v; kit_put(n, k); }
static void fx_goto(uint32_t g)                      /* 0.16: FX's page g (FXPG_*), as shown */
{
    uint32_t i;
    for (i = 0; i < NFXPG && fx_page() != g; i++)
        turn(EN_SELECT, fx_page() < g ? 1 : -1);
}
static void goto_mode(uint32_t b, uint32_t m)        /* press b until the mode is m */
{
    uint32_t i;
    for (i = 0; i < 6u && ui.mode != m; i++)
        press(b);
    check(ui.mode == m, "the mode a button cycles to");
}
static void perf_goto(uint32_t g)                    /* 29: PERF's page g (ARP: its set) */
{
    uint32_t i;
    goto_mode(B_ARP, M_PERF);
    for (i = 0; i < NPSET && (g >= PG_GMOD ? 3u : g >= PG_WANDER ? 2u : g >= PG_SPELL) != perf_set(); i++)   /* (32,
                                                      * 40: four sets) */
        press(B_ARP);
    for (i = 0; i < NPERF && ui.page != g; i++)
        turn(EN_SELECT, ui.page < g ? 1 : -1);
}
static void seq_goto(uint32_t g)                     /* 29: SEQ's track page g (TRACK, GENERATOR ..) */
{
    goto_mode(B_SEQ, M_EUCLID);
    turn(EN_SELECT, (int32_t)g - (int32_t)ui.page);
}
static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        fails++;
    }
}

/* ------------------------------------------------------- Phase 2 (0.3) --- */
/* the engine from a clean state: the init kit, then f (the test's settings) */
static void eng_reset(void (*f)(void))
{
    memset(&fx, 0, sizeof fx);
    memset(vc, 0, sizeof vc);
    memset(venv, 0, sizeof venv);
    noise_rng = 0x9E3779B9u;
    hit_rng = 0x2545F491u;
    voices_init();
    fxbus_init();
    reverb_init();
    kit_init_par();
    par[PAR_VELOD1] = 40;
    par[PAR_VELOD2] = 40;
    if (f)
        f();
    snd_post_all();
    snd_service();
}
static int32_t outl[64 * 400], outr[64 * 400];
static void eng_run(uint32_t blocks, uint32_t at)    /* blocks of the engine into outl / outr from block at */
{
    uint32_t i;
    for (i = 0; i < blocks; i++) {
        voices_block();
        memcpy(outl + (at + i) * 64u, mix_l, sizeof mix_l);
        memcpy(outr + (at + i) * 64u, mix_r, sizeof mix_r);
    }
}
static double rms(const int32_t *x, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return sqrt(e / n);
}
static double hf(const int32_t *x, uint32_t n)       /* the first difference's RMS: the highs */
{
    double e = 0;
    uint32_t i;
    for (i = 1; i < n; i++)
        e += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    return sqrt(e / n);
}
static uint32_t t_shape, t_bits, t_tilt;
static void set_shape(void) { par[PAR_SHAPE1] = (uint8_t)t_shape; par[PAR_DRIVE1] = 100; }
static void set_bits(void) { par[PAR_BITS1] = (uint8_t)t_bits; }
static void set_tilt(void) { par[PAR_TILT1] = (uint8_t)t_tilt; par[PAR_OSC_WAVE_DRUM1] = 2; }
static void set_choke(void) { par[PAR_CHOKE1] = 1; par[PAR_CHOKE1 + 1] = 1; par[PAR_VELOD1] = 90; }
static void set_rnd(void) { par[PAR_RND_PITCH1] = 60; par[PAR_RND_PAN1] = 127; }
static void set_duck(void) { par[PAR_XSRC1 + 3] = 1; par[PAR_XMODE1 + 3] = XM_DUCK; par[PAR_XAMT1 + 3] = 127;
                             par[PAR_VELOD1] = 90; par[PAR_VELOD4] = 100; }
static uint32_t t_fx, t_fb;
static void set_fx(void)
{
    par[PAR_FX_TYPE] = (uint8_t)t_fx;
    par[PAR_FX_DRY_WET] = 50;
    par[PAR_FX_P1] = 80;
    par[PAR_FX_P2] = 127;
    par[PAR_FX_P3] = (uint8_t)t_fb;
    par[PAR_FX_P4] = 60;
    par[PAR_FX_SEND1 + 3] = 127;
    par[PAR_VELOD4] = 110;
}
static void phase2(void)
{
    uint32_t i;
    /* the waveshapers: each its own sound at a high drive; RECTIFY without DC */
    {
        double r[NSHAPE], dc = 0;
        for (t_shape = 0; t_shape < NSHAPE; t_shape++) {
            eng_reset(set_shape);
            voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
            eng_run(200, 0);
            r[t_shape] = rms(outl, 64 * 200);
            if (t_shape == SH_RECT)
                for (i = 0; i < 64u * 200u; i++)
                    dc += outl[i];
            check(r[t_shape] > 100 && r[t_shape] < 40000, "a waveshaper sounds");
        }
        printf("  shapers (rms): LXR %.0f FOLD %.0f CLIP %.0f ASYM %.0f RECTIFY %.0f; RECTIFY mean %.1f\n", r[0],
               r[1], r[2], r[3], r[4], dc / (64 * 200));
        check(r[SH_FOLD] != r[SH_LXR] && r[SH_CLIP] != r[SH_LXR] && r[SH_RECT] != r[SH_ASYM], "the shapers differ");
        check(fabs(dc / (64 * 200)) < r[SH_RECT] * 0.2, "RECTIFY: its DC followed (what is left: the hit's envelope)");
    }
    /* bits: 1 bit leaves two levels (before the pan); 0 leaves the voice as it was */
    {
        int16_t seen[8];
        uint32_t ns = 0, k;
        t_bits = 15;
        eng_reset(set_bits);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        for (i = 0; i < 50u; i++) {
            voices_block();
            for (k = 0; k < 64u; k++) {
                uint32_t j;
                for (j = 0; j < ns && seen[j] != vbuf[0][k]; j++)
                    ;
                if (j == ns && ns < 8u)
                    seen[ns++] = vbuf[0][k];
            }
        }
        check(ns <= 3, "1 bit: two levels (and silence)");
    }
    /* tilt: 127 brighter than flat, 0 darker */
    {
        double h[3];
        uint32_t tt[3] = {0, 64, 127};
        for (i = 0; i < 3u; i++) {
            t_tilt = tt[i];
            eng_reset(set_tilt);
            voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
            eng_run(100, 0);
            h[i] = hf(outl, 64 * 100) / rms(outl, 64 * 100);
        }
        printf("  tilt: highs / level at 0 %.3f, 64 %.3f, 127 %.3f\n", h[0], h[1], h[2]);
        check(h[0] < h[1] && h[1] < h[2], "tilt: 0 darker, 127 brighter");
    }
    /* choke: D1 and D2 in group 1; D2's hit silences D1 */
    {
        double before, after;
        eng_reset(set_choke);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        eng_run(30, 0);
        voice_trigger(1, 1, SEQ_DEFAULT_NOTE);
        eng_run(30, 30);
        before = rms(outl + 20 * 64, 64 * 10);
        after = voice_peak[0];
        printf("  choke: D1 %.0f (rms) before D2's hit, its peak %.0f after\n", before, after);
        check(before > 1000 && after == 0 && vc[0].choking == 2, "choke: D2 cuts D1");
    }
    /* random per hit: two hits differ; at 0 they are the same */
    {
        static int32_t a[64 * 40];
        eng_reset(set_rnd);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        eng_run(40, 0);
        memcpy(a, outl, sizeof a);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        eng_run(40, 0);
        check(memcmp(a, outl, sizeof a) != 0 && vc[0].rp != 65536u, "random per hit: each hit its own");
        eng_reset(0);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        eng_run(40, 0);
        memcpy(a, outl, sizeof a);
        eng_reset(0);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        eng_run(40, 0);
        check(memcmp(a, outl, sizeof a) == 0, "random per hit at 0: the same each time");
    }
    /* cross modulation: the snare ducked by the kick */
    {
        double d, n;
        eng_reset(0);
        par[PAR_VELOD1] = 90;
        par[PAR_VELOD4] = 100;
        snd_post_all();
        snd_service();
        voice_trigger(3, 127, SEQ_DEFAULT_NOTE);
        voices_block();
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        for (i = 0; i < 20u; i++)
            voices_block();
        n = voice_peak[3];
        eng_reset(set_duck);
        voice_trigger(3, 127, SEQ_DEFAULT_NOTE);
        voices_block();
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        for (i = 0; i < 20u; i++)
            voices_block();
        d = voice_peak[3];
        printf("  cross mod DUCK: the snare's peak under the kick %.0f, %.0f unducked\n", d, n);
        check(d < n * 0.5, "cross mod: DUCK turns the snare down under the kick");
    }
    /* PHASER, FLANGER: a sound, not the dry one, bounded at the most feedback */
    {
        double r[2], dry;
        eng_reset(0);
        par[PAR_FX_SEND1 + 3] = 127;
        par[PAR_VELOD4] = 110;
        snd_post_all();
        snd_service();
        voice_trigger(3, 127, SEQ_DEFAULT_NOTE);
        eng_run(200, 0);
        dry = rms(outl, 64 * 200);
        for (t_fx = FX_PHASER; t_fx <= FX_FLANGER; t_fx++) {
            int32_t pk = 0;
            for (t_fb = 0; t_fb <= 127; t_fb += 127) {
                eng_reset(set_fx);
                voice_trigger(3, 127, SEQ_DEFAULT_NOTE);
                eng_run(200, 0);
                for (i = 0; i < 64u * 200u; i++)
                    pk = outl[i] > pk ? outl[i] : -outl[i] > pk ? -outl[i] : pk;
            }
            r[t_fx - FX_PHASER] = rms(outl, 64 * 200);
            check(pk < 70000 && r[t_fx - FX_PHASER] > dry * 0.2 && r[t_fx - FX_PHASER] != dry,
                  "PHASER / FLANGER: a sound of their own, bounded");
        }
        printf("  snare dry %.0f, PHASER %.0f, FLANGER %.0f (rms)\n", dry, r[0], r[1]);
    }
    /* a version-2 lock (Elixir's parameters at the LXR's free numbers) moves to its own number */
    {
        pattern_t *p = &pat[0];
        lock_set(p, 0, 5, 0, 247);
        lock_set(p, 0, 5, 1, 223);
        lock_v2(p, 0, 5, 0);
        lock_v2(p, 0, 5, 1);
        check(autodest_to_par(lock_get(p, 0, 5, 0)) == PAR_FX_SEND1 &&
              autodest_to_par(lock_get(p, 0, 5, 1)) == PAR_FX_SEND1 + 10u, "version-2 locks move to their numbers");
        seq_clear_automation(0, p, 0);
        seq_clear_automation(0, p, 1);
    }
    /* the panel: SCL > SHAPE, LFO > RANDOM > CROSS MOD; a lock on a shaping parameter, saved and loaded */
    for (i = 0; i < NUM_PATTERN; i++)                 /* (the dense project's patterns and kits out) */
        seq_clear_pattern(&pat[i]);
    {
        uint8_t k[KIT_LEN];
        kit_init_values(k);
        for (i = 0; i < NUM_KITS; i++)
            kit_put(i, k);
    }
    pat_backup = pat[sq.active];
    eng_reset(0);
    goto_mode(B_HOME, M_HOME);
    black(0);
    press(B_SCL);
    press(B_SCL);
    check(ui.mode == M_SOUND && ui.sect == S_SHAPE, "SCL twice: SHAPE");
    turn(EN_K1, 1);
    turn(EN_K4, 4);
    {
        uint16_t kp[4];
        page_params(kp);
        check(kp[2] == PAR_VOICE_DECIMATION1, "SHAPE: K3 the sample rate, before the bits (29)");
    }
    check(par[PAR_SHAPE1] == SH_FOLD && par[PAR_BITS1] == 4, "SHAPE: K1 the shaper, K4 the bits");
    shot("shape");
    turn(EN_SELECT, 1);                               /* (29: SHAPE (2/2): the tilt) */
    keys |= 1u << WHITE_KEY[6];                       /* a lock on step 7: D1's tilt */
    web_keys(keys);
    run_ms(40);
    turn(EN_K1, 20);
    keys &= ~(1u << WHITE_KEY[6]);
    web_keys(keys);
    run_ms(40);
    check(autodest_to_par(lock_get(&pat[sq.active], 0, 6, 0)) == PAR_TILT1 ||
          autodest_to_par(lock_get(&pat[sq.active], 0, 6, 1)) == PAR_TILT1, "a step locks the tilt");
    press(B_LFO);                                     /* LFO (1.0: SELECT its pages, its MODE on the second), then LFO 2 */
    check(ui.sect == S_LFO && ui.half == 0, "LFO: LFO 1");
    turn(EN_SELECT, 1);
    check(ui.sect == S_LFO && ui.half == 1, "LFO, SELECT: LFO 1's second page");
    press(B_LFO);
    check(ui.sect == S_LFO2 && ui.half == 0, "LFO again: TRK LFO 2 (25b)");
    shot("lfo2");
    turn(EN_SELECT, 1);
    check(ui.sect == S_LFO2 && ui.half == 1, "SELECT: TRK LFO 2's second page");
    shot("lfo2-b");
    press(B_LFO);
    check(ui.sect == S_RANDOM, "LFO again: RANDOM");
    turn(EN_K1, 30);
    shot("random");
    press(B_LFO);
    check(ui.sect == S_XMOD, "LFO again: CROSS MOD");
    black(3);
    turn(EN_K1, 1);
    turn(EN_K3, 60);
    turn(EN_K4, 1);
    check(par[PAR_XSRC1 + 3] == 1 && par[PAR_XAMT1 + 3] > 0 && par[PAR_CHOKE1 + 3] == 1, "CROSS MOD: source, amount");
    shot("xmod");
    press(B_LFO);
    check(ui.sect == S_SENDS, "LFO again: FX TRACK SENDS (1.0 Phase 16)");
    press(B_LFO);
    check(ui.sect == S_LFO, "LFO again: back to the LFO page");
    press(B_SEQ);
    black(0);
    white(6);
    turn(EN_SELECT, 2);
    shot("step-locks-tilt");
    press(B_SEQ);
    press(B_FX);
    fx_goto(FXPG_BUS);
    turn(EN_K1, -10);
    turn(EN_K1, 3);                                  /* (1.0: past DELAY; Phase 10f: and DRIVE) */
    check(par[PAR_FX_TYPE] == FX_PHASER, "FX: PHASER");
    shot("fx-phaser");
    {
        int rc;
        uint32_t c0 = lock_get(&pat[sq.active], 0, 6, 0), c1 = lock_get(&pat[sq.active], 0, 6, 1);
        rc = proj_save(1, "V3      ");
        par[PAR_BITS1] = 0;
        seq_clear_pattern(&pat[sq.active]);
        rc |= proj_load(1);
        check(rc == 0 && par[PAR_BITS1] == 4 && lock_get(&pat[sq.active], 0, 6, 0) == c0 &&
              lock_get(&pat[sq.active], 0, 6, 1) == c1 && (c0 >= 256u || c1 >= 256u),
              "project (version 3): shaping and its locks");

    }
}

/* ------------------------------------------------------- Phase 3 (0.4) --- */
static void key_hold(uint32_t w) { keys |= 1u << WHITE_KEY[w]; web_keys(keys); run_ms(30); }
static void key_up(uint32_t w) { keys &= ~(1u << WHITE_KEY[w]); web_keys(keys); run_ms(30); }
static double out_rms(uint32_t ms)                   /* the device's output for ms, RMS (web_render's samples) */
{
    double e = 0;
    uint32_t n = ms * FS / 1000u, c = 0;
    while (n) {
        uint32_t k = n > 512u ? 512u : n, i;
        web_render(k);
        for (i = 0; i < k; i++)
            e += (double)out_l[i] * out_l[i] + (double)out_r[i] * out_r[i];
        c += k;
        n -= k;
    }
    return sqrt(e / (2.0 * c)) * 32768.0;
}
static void phase3(void)
{
    uint32_t i, c0;
    /* a beat: D1 on 1 5 9 13 (4 a bar), at 120 BPM; nothing else */
    eng_reset(0);
    for (i = 0; i < NUM_PATTERN; i++)
        seq_clear_pattern(&pat[i]);
    sq.active = sq.pending = 0;
    pat_backup = pat[0];
    par_set(PAR_BPM, 120);
    par[PAR_SHUFFLE] = 0;
    for (i = 0; i < 16u; i += 4)
        main_set(&pat[0], 0, i, 1);
    goto_mode(B_HOME, M_HOME);
    black(0);
    press(B_ARP);
    check(ui.mode == M_PERF && ui.perf, "ARP: PERF");
    press(B_PLAY);
    run_ms(500);
    shot("perf");
    /* STUTTER at 1/8 (2 steps) over a hit: twice the hits; let go, the playhead where it would have been */
    black(NTRACK + 1);
    check(pfs.repeat == 1, "PERF's bar key 2: repeat 1/8");
    while (sq.prescale != 0u || sq.idx[0] % 4 != 3)   /* (just before a hit) */
        run_ms(1);
    key_hold(8);
    run_ms(150);
    check(sq.stut_on, "STUTTER: looping");
    c0 = trig_count[0];
    run_ms(2000);                                     /* a bar at 120: 4 hits plain, 8 looping 1/8 over a hit */
    printf("  stutter 1/8: %u hits in a bar (4 plain)\n", trig_count[0] - c0);
    check(trig_count[0] - c0 >= 7 && trig_count[0] - c0 <= 9, "STUTTER: the loop retriggers");
    key_up(8);
    run_ms(5);
    check(!sq.stut_on && (uint32_t)sq.idx[0] == ((sq.ticks - 1u) / 24u) % 16u, "after STUTTER: back on the grid");
    /* FILL: a step at chance 0 plays only while FILL is held */
    pat[0].step[0][2].prob = 0;
    main_set(&pat[0], 0, 2, 1);
    c0 = trig_count[0];
    run_ms(2000);
    check(trig_count[0] - c0 == 4, "chance 0: the step does not play");
    key_hold(7);
    c0 = trig_count[0];
    run_ms(2000);
    check(trig_count[0] - c0 == 5, "FILL: the step at chance 0 plays");
    key_up(7);
    main_set(&pat[0], 0, 2, 0);
    /* TAPE STOP: tempo and pitch down to a stop and silence; let go, back */
    pfs.tape = 0;                                     /* (0.2 s) */
    key_hold(13);
    run_ms(400);
    c0 = sq.ticks;
    check(tape_q16 == 0 && out_rms(200) < 1.0, "TAPE STOP: stopped and silent");
    check(sq.ticks - c0 <= 1u, "TAPE STOP: the sequencer stopped too");
    shot("perf-tape");
    key_up(13);
    run_ms(400);
    check(tape_q16 == 65536u && sq.ticks > c0 + 10u, "TAPE STOP let go: running again");
    pfs.tape = 64;
    /* LATCH: SHIFT + LP SWEEP stays after the key; the key again lets it go; the sweep reaches its end in a bar */
    hold(B_OCTDN);
    white(14);
    shot("perf-shift");
    release(B_OCTDN);
    check((ui.pf_latch >> PF_SWEEP_LP) & 1u, "SHIFT + key: latched");
    run_ms(2200);
    check(pf.sweep == -65536 && pf.dj_d == -65536, "LP SWEEP: the DJ filter at its low-pass end after a bar");
    shot("perf-sweep");
    white(14);
    check(!((pf.on >> PF_SWEEP_LP) & 1u), "the key again: let go");
    run_ms(700);
    check(pf.sweep == 0, "the sweep back in a quarter of a bar");
    press(B_PLAY);
    /* the DJ filter: at 64 the sound as it was; LP darker, HP thinner */
    {
        double h[3], lo[3];
        uint8_t dj[3] = {64, 10, 118};
        for (i = 0; i < 3u; i++) {
            uint32_t k;
            eng_reset(set_tilt);
            pf.dj = dj[i];
            pf.sweep = 0;
            voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
            for (k = 0; k < 100u; k++) {
                int16_t ml[64], mr[64];
                uint32_t j;
                voices_block();
                for (j = 0; j < 64u; j++) {
                    ml[j] = (int16_t)sat16(mix_l[j]);
                    mr[j] = (int16_t)sat16(mix_r[j]);
                }
                perf_master(ml, mr);
                for (j = 0; j < 64u; j++)
                    outl[k * 64u + j] = ml[j];
            }
            h[i] = hf(outl, 6400);
            lo[i] = rms(outl, 6400);
            if (i == 0) {                             /* (64: no filter at all) */
                int32_t keep[6400];
                memcpy(keep, outl, sizeof keep);
                eng_reset(set_tilt);
                voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
                eng_run(100, 0);
                for (k = 0; k < 6400u; k++)
                    keep[k] -= sat16(outl[k]);
                for (k = 0; k < 6400u && !keep[k]; k++)
                    ;
                check(k == 6400u, "DJ filter at 64: untouched");
            }
        }
        printf("  DJ filter: highs %.0f / %.0f / %.0f, level %.0f / %.0f / %.0f (off, LP, HP)\n", h[0], h[1], h[2],
               lo[0], lo[1], lo[2]);
        check(h[1] < h[0] * 0.5 && lo[2] < lo[0] * 0.7, "DJ filter: LP takes the highs, HP the body");
        pf.dj = 64;
    }
    /* 0.24.3: the DJ filter's Reso: at 0 the damping as before; up, an impulse rings on, and still dies away */
    {
        static const uint8_t DJ[4] = {8, 40, 88, 120};
        uint32_t d, rz;
        int ok = 1;
        for (d = 0; d < 4u; d++) {
            double ring[2], tail[2];
            for (rz = 0; rz < 2u; rz++) {
                uint32_t k, j;
                int16_t ml[64], mr[64];
                eng_reset(0);
                pf.dj = DJ[d];
                pf.sweep = 0;
                pfs.dj_reso = (uint8_t)(rz ? 127u : 0u);
                for (k = 0; k < 400u; k++) {         /* (1.0: the impulse once Reso has glided there) */
                    for (j = 0; j < 64u; j++)
                        ml[j] = mr[j] = (int16_t)(k == 80u && j == 0u ? 20000 : 0);
                    perf_master(ml, mr);
                    for (j = 0; j < 64u; j++)
                        outl[(k % 100u) * 64u + j] = ml[j];
                    if (k == 80u && rz == 0u)
                        ok &= pf.dj_f[0].q == 39322;
                    if (k == 87u)
                        ring[rz] = rms(outl + 81u * 64u, 6u * 64u);
                }
                tail[rz] = rms(outl + 99u * 64u, 64u);
            }
            printf("  DJ %u: ring %.1f / %.1f, tail %.2f / %.2f (Reso 0 / 127)\n", DJ[d], ring[0], ring[1], tail[0],
                   tail[1]);
            ok &= ring[1] > ring[0] * 2.0 && tail[1] < 16.0;   /* (a few LSB of rounding, as at 0) */
        }
        check(ok, "DJ filter Reso: rings at 127 (LP and HP), dies away; 0.6's damping at 0");
        pf.dj = 64;
        pfs.dj_reso = 0;
    }
    /* FREEZE: the delay keeps what it holds; THROW REV: the throw voice's reverb send at 127 while held */
    {
        double fr, nf;
        eng_reset(0);
        kit_init_par();
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);    /* (1.0: the init kit's MODE is OFF) */
        par_set(PAR_DLY_LEVEL, 76);                  /* (1.0: the always-on delay) */
        par_set(PAR_DLY_ROUTE, 127);                 /* (1.0 Phase 16: the bus into it) */
        par_set(PAR_FX_SEND1, 127);
        run_ms(50);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(300);
        key_hold(9);
        check(fx.freeze, "FREEZE held");
        run_ms(3000);
        fr = out_rms(500);
        key_up(9);
        run_ms(3000);
        nf = out_rms(500);
        printf("  FREEZE: %.0f after 3 s frozen, %.0f 3 s after it let go\n", fr, nf);
        check(fr > 50 && nf < fr * 0.2, "FREEZE: the delay rings while held, dies after");
        black(3);                                     /* (PERF: a mute; the throw voice is the selected track) */
        black(3);
        turn(EN_ALGO, 3);
        key_hold(10);
        check(vc[3].throw_rev && !vc[0].throw_rev, "THROW REV: the selected track's voice");
        key_up(10);
        check(!vc[3].throw_rev, "THROW REV let go");
        turn(EN_ALGO, -3);
    }
    /* MACROS: M1 on D1's decay between 10 and 100; turned under REC it records a lock */
    perf_goto(PG_MACRO + 1u);
    check(ui.page == PG_MACRO + 1u, "PERF: MACRO SETUP");
    kset.accel = 0;                                   /* (exact detents) */
    turn(EN_K2, (int32_t)lock_idx_of(PAR_VELOD1));
    check(pfs.m[0].par == PAR_VELOD1, "MACRO SETUP: the target");
    turn(EN_K3, 10);
    turn(EN_K4, -27);
    kset.accel = 1;
    shot("macro-setup");
    turn(EN_SELECT, -1);
    turn(EN_K1, -127);
    check(par[PAR_VELOD1] == pfs.m[0].lo, "M1 at 0: its MIN");
    turn(EN_K1, 127);
    check(par[PAR_VELOD1] == pfs.m[0].hi && pfs.m[0].hi == 100 && pfs.m[0].lo == 10, "M1 at 127: its MAX");
    shot("macros");
    main_set(&pat[0], 0, 0, 1);
    press(B_REC);
    press(B_PLAY);
    run_ms(300);
    turn(EN_K1, -60);
    press(B_PLAY);
    press(B_REC);
    {
        uint32_t k, found = 0;
        for (k = 0; k < 16u; k++)
            found |= autodest_to_par(lock_get(&pat[0], 0, k, 0)) == PAR_VELOD1 ||
                     autodest_to_par(lock_get(&pat[0], 0, k, 1)) == PAR_VELOD1;
        check(found, "a macro turned under REC records a lock");
    }
    {
        int rc = proj_save(1, "PERF    ");
        uint16_t tp = pfs.m[0].par;
        perf_defaults();
        rc |= proj_load(1);
        check(rc == 0 && pfs.m[0].par == tp && pfs.m[0].hi == 100 && pfs.repeat == 1, "project: the macros, REPEAT");
    }
    /* leaving PERF lets go of what is latched */
    hold(B_OCTDN);
    white(13);
    release(B_OCTDN);
    check((pf.on >> PF_TAPE) & 1u, "TAPE latched");
    press(B_HOME);
    check(!ui.perf && !pf.on, "HOME: PERF's latches let go");
    run_ms(400);
    check(tape_q16 == 65536u, "the tape back up");
}

/* ------------------------------------------------------- Phase 4 (0.5) --- */
static uint32_t hits_in(uint32_t t, uint32_t ms)     /* track t's hits over ms */
{
    uint32_t c = trig_count[t];
    run_ms(ms);
    return trig_count[t] - c;
}
static void p4_reset(void)                           /* stopped, pattern 1 empty, 120 BPM, the grid */
{
    uint32_t i;
    if (sq.running)
        press(B_PLAY);
    for (i = 0; i < NUM_PATTERN; i++)
        seq_clear_pattern(&pat[i]);
    sq.active = sq.pending = 0;
    pat_backup = pat[0];
    sq.gsync = 0x7F;
    par_set(PAR_BPM, 120);
    par[PAR_SHUFFLE] = 0;
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}
static void play_from_start(void)
{
    press(B_PLAY);
    while (sq.ticks == 0u)                           /* (to the first tick) */
        run_ms(1);
}
static void phase4(void)
{
    uint32_t i, n, t;
    pattern_t *p = &pat[0];
    /* the bar copy: every track's bar 1 into bar 2 from the grid; a 12-step track keeps its 12 */
    p4_reset();
    for (t = 0; t < 6u; t++)
        for (i = t % 3u; i < 16u; i += 3u + t) {
            main_set(p, t, i, 1);
            p->step[t][i].volume = (uint8_t)(0x80u | (40u + i));
        }
    p->cond[0][0] = COND_AB;
    p->len[5] = 12;
    hold(B_OCTUP);
    black(NTRACK + 1);
    release(B_OCTUP);
    shot("bar-copy");
    {
        int same = 1;
        for (t = 0; t < 6u; t++) {
            same &= p->main[t][1] == p->main[t][0];
            for (i = 0; i < 16u; i++)
                same &= p->step[t][16 + i].volume == p->step[t][i].volume;
        }
        check(same && p->cond[0][16] == COND_AB, "COPY + bar 2 on the grid: every track's bar 1 into bar 2");
        check(p->len[0] == 32 && p->len[4] == 32 && p->len[5] == 12, "the bar copy: tracks of whole bars grow to 2");
        check(p->len[6] == 32, "an empty track of a bar grows too (the pattern 2 bars)");
    }
    /* clocks: 16 steps of D1 at x2, /2, 3/4 against a bar of 1x (2 s at 120) */
    {
        static const uint8_t ck[4] = {CLK_1X, 8 /* x2 */, 2 /* /2 */, 4 /* 3/4 */};
        static const uint8_t want[4] = {16, 32, 8, 12};
        p4_reset();
        for (i = 0; i < 16u; i++)
            main_set(p, 0, i, 1);
        main_set(p, 1, 0, 1);                        /* (D2 a bar long, 1x: the pattern's length) */
        for (n = 0; n < 4u; n++) {
            p->clk[0] = ck[n];
            play_from_start();
            run_ms(100);
            i = hits_in(0, 2000);
            press(B_PLAY);
            printf("  clock %s: %u hits in a bar (want %u)\n", n == 0 ? "1x" : n == 1 ? "x2" : n == 2 ? "/2" : "3/4",
                   i, want[n]);
            check(i + 1u >= want[n] && i <= want[n] + 1u, "a track's clock");
        }
        p->clk[0] = CLK_1X;
    }
    /* trig conditions: 1:2 every other time round, 2:4 the second of four, 1ST, !1ST, FILL */
    {
        static const uint8_t c[5] = {COND_AB, COND_AB + 2 + 3 + 1, COND_1ST, COND_N1ST, COND_FILL};   /* 1:2 2:4 */
        static const uint8_t want[5] = {4, 2, 1, 7, 0};
        p4_reset();
        for (t = 0; t < 5u; t++) {
            main_set(p, t, 0, 1);
            p->cond[t][0] = c[t];
        }
        play_from_start();
        run_ms(100);                                 /* (step 1 of the first time round has played) */
        {
            uint32_t c0[5];
            for (t = 0; t < 5u; t++)
                c0[t] = trig_count[t];
            run_ms(16000 - 100 - 400);               /* to just before the 9th time round (a bar each) */
            for (t = 0; t < 5u; t++) {
                n = trig_count[t] - c0[t] + (t == 0 || t == 2 ? 1u : 0u);   /* (+ the first time's, if it played) */
                printf("  condition %u: %u of 8\n", t, n);
                check(n == want[t], "a trig condition");
            }
        }
        press(B_PLAY);
    }
    /* ratchets: 4 hits at 1/32, fading down */
    p4_reset();
    main_set(p, 0, 0, 1);
    p->rat[0][0] = 3 | 0 << 3 | RF_DOWN << 5;
    n = trig_count[0];
    play_from_start();
    run_ms(1700);                                     /* (a bar: the next time round not yet) */
    n = trig_count[0] - n;
    press(B_PLAY);
    printf("  ratchet: %u hits\n", n);
    check(n == 4, "a ratchet: 4 hits in its step");
    /* generators: TURING at density 0 silent, at 127 playing; locked, it repeats */
    p4_reset();
    for (i = 0; i < 16u; i += 2)
        main_set(p, 0, i, 1);
    p->gen[0][0] = GEN_TURING;
    p->gen[0][1] = 8;
    p->gen[0][2] = 127;
    p->gen[0][3] = 0;
    sq.gsync = 1;
    play_from_start();
    check(hits_in(0, 2000) == 0, "TURING at density 0: nothing");
    p->gen[0][3] = 127;
    n = hits_in(0, 2000);
    check(n >= 12, "TURING at density 127: nearly every step");
    p->gen[0][3] = 64;
    {
        uint64_t a = 0, b = 0;
        run_ms(2000);
        for (i = 0; i < 16u; i++)
            a |= (uint64_t)main_on(&gpat, 0, i) << i;
        run_ms(2000);
        for (i = 0; i < 16u; i++)
            b |= (uint64_t)main_on(&gpat, 0, i) << i;
        check(a == b, "TURING locked, length 8: the bar repeats");
        check(p->main[0][0] == 0x5555u, "the pattern stays as it was");
    }
    press(B_PLAY);
    /* MUTATE at 100%: the live track drifts from the pattern; COMMIT writes it in, UNDO takes that back */
    p4_reset();
    for (i = 0; i < 16u; i += 4)
        main_set(p, 0, i, 1);
    black(0);
    seq_goto(PG_GEN);
    check(ui.mode == M_EUCLID && ui.page == PG_GEN, "SEQ, SELECT: GENERATOR");
    turn(EN_K1, 2);
    check(p->gen[0][0] == GEN_MUTATE, "GENERATOR: MUTATE");
    turn(EN_K2, 75);
    shot("generator");
    play_from_start();
    run_ms(8000);
    press(B_PLAY);
    check(gpat.main[0][0] != p->main[0][0], "MUTATE: the live track changed");
    check(p->main[0][0] == 0x1111u, "MUTATE: the pattern as it was");
    shot("mutated");
    {
        uint16_t live = gpat.main[0][0];
        turn(EN_SELECT, 1);
        check(ui.page == PG_ACTION && vp_max(VP_ACTION) == 3, "ACTION: a live generator's COMMIT, UNDO, RESET");
        turn(EN_K1, 1);
        check(ui.action == ACT_COMMIT, "ACTION: COMMIT");
        check((web_breath_buttons() >> B_OCTUP) & 1u, "ACTION: COMMIT chosen, OCT+ breathes (1.0)");
        shot("action-commit");
        press(B_OCTUP);
        check(p->main[0][0] == live, "COMMIT: the live track into the pattern");
        turn(EN_K1, 2);
        check(ui.action == ACT_UNDO, "ACTION: UNDO");
        press(B_OCTUP);
        check(p->main[0][0] == 0x1111u, "UNDO: the pattern as it was before COMMIT");
        /* DICE (EUCLID's): its hits anywhere over its length; UNDO */
        turn(EN_SELECT, -1);
        turn(EN_K1, -1);
        check(p->gen[0][0] == GEN_EUCLID && p->main[0][0] == 0x1111u, "GENERATOR: EUCLID, the steps as they were");
        p->gen[0][2] = 5;
        turn(EN_SELECT, 1);
        check(vp_max(VP_ACTION) == 2, "ACTION: EUCLID's DICE, UNDO");
        turn(EN_K1, 1);
        check(ui.action == ACT_DICE, "ACTION: DICE");
        press(B_OCTUP);
        n = (uint32_t)__builtin_popcount(p->main[0][0]);
        check(n == 5, "DICE: 5 hits");
        shot("diced");
        turn(EN_K1, 2);
        press(B_OCTUP);
        check(p->main[0][0] == 0x1111u, "UNDO after DICE");
        turn(EN_SELECT, -1);
    }
    /* CELLULAR, rule 90, from a cell: the next rows */
    p->gen[0][0] = GEN_CA;
    p->gen[0][1] = 90;
    p->gen[0][3] = 0;
    p->main[0][0] = 1u << 8;
    sq.gsync = 1;
    run_ms(10);
    seq_gen_loop(0);
    check(gpat.main[0][0] == ((1u << 7) | (1u << 9)), "CELLULAR rule 90: a cell's next row");
    seq_gen_loop(0);
    check(gpat.main[0][0] == ((1u << 6) | (1u << 10)), "CELLULAR rule 90: the row after");
    /* the track page; the step pages: condition, ratchet */
    turn(EN_SELECT, (int32_t)PG_TRACK - (int32_t)ui.page);
    turn(EN_K2, 3);
    check(p->clk[0] == CLK_1X + 3, "TRACK: clock x2");
    shot("track-clock");
    p->clk[0] = CLK_1X;
    p->gen[0][0] = GEN_OFF;
    press(B_SEQ);
    white(0);
    kset.accel = 0;
    turn(EN_SELECT, 1);                               /* (0.23-t11: TRIG, the second page: Chance, Condition ..) */
    turn(EN_K2, 9 + 1);
    kset.accel = 1;
    check(p->cond[0][0] == COND_AB + 1, "STEP: the condition (2:2)");
    shot("step-cond");
    turn(EN_SELECT, 2);
    turn(EN_K1, 2);
    turn(EN_K3, 1);
    turn(EN_K4, 2);
    check((p->rat[0][0] & 7u) == 2 && ((p->rat[0][0] >> 3) & 3u) == 1 && (p->rat[0][0] >> 5) == RF_UP,
          "RATCHET: 3 hits, 1/48, up (29: K2 the shape)");
    shot("step-ratchet");
    turn(EN_SELECT, 1);
    check(ui.mode == M_STEP && ui.page == 3, "SELECT past RATCHET: still RATCHET");
    press(B_SEQ);
    check(ui.mode == M_EUCLID, "SEQ again from the steps: the track's pages (29)");
    /* project version 4: conditions, ratchets, clocks, generators */
    {
        int rc;
        uint8_t c = p->cond[0][0], r = p->rat[0][0];
        p->clk[3] = 9;
        p->gen[3][0] = GEN_TURING;
        p->gen[3][1] = 12;
        rc = proj_save(1, "V4      ");
        seq_clear_pattern(p);
        rc |= proj_load(1);
        check(rc == 0 && p->cond[0][0] == c && p->rat[0][0] == r && p->clk[3] == 9 && p->gen[3][0] == GEN_TURING &&
              p->gen[3][1] == 12, "project (version 4): conditions, ratchets, clocks, generators");
    }
}

/* GRIDS: the map read as Grids reads it; a track from it, step by step */
static uint32_t grids_ref(uint32_t part, uint32_t step, uint32_t x, uint32_t y)   /* (ReadDrumMap, written again) */
{
    uint32_t i = x / 64u, j = y / 64u, o = part * 32u + step, f = (x * 4u) % 256u, g = (y * 4u) % 256u;
    uint32_t a = GRIDS_NODE[GRIDS_AT[i][j]][o], b = GRIDS_NODE[GRIDS_AT[i + 1][j]][o];
    uint32_t c = GRIDS_NODE[GRIDS_AT[i][j + 1]][o], d = GRIDS_NODE[GRIDS_AT[i + 1][j + 1]][o];
    uint32_t ab = (a * (255 - f) + b * f) / 256, cd = (c * (255 - f) + d * f) / 256;
    return (ab * (255 - g) + cd * g) / 256;
}
/* 0.5.1: the output's hush (a save fades the output out and back in) and autosave's rest and quiet */
static void hush_test(void)
{
    static int32_t ob[2 * BLOCK];
    uint32_t i, k, w, n;
    int32_t pk;
    eng_reset(0);
    out_hush = 0;
    out_gain = 4096;
    voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
    for (i = 0, pk = 0; i < 8u; i++) {
        engine_block(ob);
        for (k = 0; k < 2u * BLOCK; k++)
            pk = iabs(ob[k]) > pk ? iabs(ob[k]) : pk;
    }
    check(pk > 1000 && quiet_blocks == 0, "a hit sounds, not quiet");
    out_hush = 1;
    for (i = 0; i < 4u; i++)                         /* 256 samples: the fade */
        engine_block(ob);
    for (i = 0, pk = 0; i < 8u; i++) {
        engine_block(ob);
        for (k = 0; k < 2u * BLOCK; k++)
            pk = iabs(ob[k]) > pk ? iabs(ob[k]) : pk;
    }
    check(pk == 0 && out_gain == 0, "hushed: the output silent after its fade");
    check(quiet_blocks == 0, "hushed: the sound goes on under it (not quiet)");
    out_hush = 0;
    engine_block(ob);
    check(iabs(ob[0]) < 200 && out_gain == 16 * (int32_t)BLOCK, "unhushed: it fades back in");
    for (i = 0; i < 4u; i++)
        engine_block(ob);
    check(out_gain == 4096, "unhushed: at full level after 256 samples");
    for (i = 0; i < FS / BLOCK * 4u && quiet_blocks < FS / BLOCK / 2u; i++)
        engine_block(ob);
    check(quiet_blocks >= FS / BLOCK / 2u, "the hit dies away: quiet");

    /* autosave: 8 s after the stop or the last change, the output quiet; at 30 s anyway */
    seq_init();
    kit_init_par();
    autosave_on = 1;
    pj.failed_sig = 0;
    pj.saved_sig = 0;
    sq.running = 1;
    run_ms(3000);
    sq.running = 0;
    w = web_flash_writes;
    run_ms(6000);
    check(web_flash_writes == w, "autosave: nothing 6 s after a stop");
    for (n = 0; n < 60u && web_flash_writes == w; n++)
        run_ms(100);
    check(web_flash_writes != w && pj.saved_sig != 0, "autosave: the session saved after its rest");
    printf("  autosave: %u.%u s after the stop\n", (unsigned)(6u + n / 10u), (unsigned)(n % 10u));
    pat[0].shuffle = 3;                              /* a change: the rest starts again */
    w = web_flash_writes;
    for (n = 0; n < 150u; n++) {                      /* a voice played every 100 ms: never quiet */
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(100);
        if (web_flash_writes != w)
            break;
    }
    check(n == 150u, "autosave: waits while the output sounds");
    for (; n < 400u && web_flash_writes == w; n++) {
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(100);
    }
    check(web_flash_writes != w && n >= 280u && n < 330u, "autosave: saves anyway after 30 s at rest");
    printf("  autosave while sounding: %u.%u s after the change\n", (unsigned)(n / 10u), (unsigned)(n % 10u));
    check(out_hush == 0 && audio_late == 0, "after the saves: the output back, no LATE counted");
}

/* 0.6 (5a): bouncing ratchets, the roll sweep */
static void phase5a(void)
{
    pattern_t *p = &pat[0];
    uint32_t i, n, c, ms[24], hits;
    p4_reset();
    black(0);
    /* a BALL bounce of 8 hits from 1/8 (250 ms at 120 BPM), the gaps 0.7 of the one before (step 5: its hits all
     * in the count) */
    main_set(p, 0, 4, 1);
    p->rat[0][4] = RAT_BOUNCE | 5u | BC_BALL << 3;
    pat_backup = *p;
    c = trig_count[0];
    play_from_start();
    for (i = 0, n = 0; i < 1900u && n < 24u; i++) {
        run_ms(1);
        while (trig_count[0] != c && n < 24u) {
            ms[n++] = i;
            c++;
        }
    }
    press(B_PLAY);
    printf("  bounce BALL x8 from 1/8 (ms):");
    for (i = 0; i < n; i++)
        printf(" %u", ms[i]);
    printf("\n");
    check(n == 8u, "BOUNCE: 8 hits");
    check(n >= 3u && ms[1] - ms[0] >= 245u && ms[1] - ms[0] <= 255u, "BOUNCE: the first gap 1/8");
    for (i = 2, hits = 1; i < n; i++) {
        double r = (double)(ms[i] - ms[i - 1]) / (double)(ms[i - 1] - ms[i - 2]);
        if (r < 0.62 || r > 0.78)
            hits = 0;
    }
    check(hits, "BOUNCE: each gap 0.7 of the one before");
    /* SLOW with a drift: the gaps grow; the drift reaches the last hit */
    p->rat[0][4] = RAT_BOUNCE | 1u | BC_SLOW << 3;  /* 3 hits */
    p->bnc[0] = (uint8_t)((12u + 7u) << 2 | 3u);   /* from 1/32, +7 semitones */
    pat_backup = *p;
    c = trig_count[0];
    play_from_start();
    for (i = 0, n = 0; i < 1100u && n < 24u; i++) {
        run_ms(1);
        while (trig_count[0] != c && n < 24u) {
            ms[n++] = i;
            c++;
        }
    }
    press(B_PLAY);
    check(n == 3u && ms[2] - ms[1] > ms[1] - ms[0], "BOUNCE SLOW: the gaps grow");
    check(sq.bdrift[0] == 7, "BOUNCE: the track's pitch drift");
    /* a new hit cuts it: step 6 (a 1/16 later) before the first bounce (1/8) */
    p->rat[0][4] = RAT_BOUNCE | 5u | BC_BALL << 3;
    p->bnc[0] = BNC_DEFAULT;
    main_set(p, 0, 5, 1);
    pat_backup = *p;
    c = trig_count[0];
    play_from_start();
    run_ms(1400);
    n = trig_count[0] - c;
    press(B_PLAY);
    check(n == 2u, "BOUNCE: a new hit on the track cuts it");
    /* the step's RATCHET page: Shape, the bounce's hits and curve by name; the track's bounce settings */
    main_set(p, 0, 5, 0);
    main_set(p, 0, 4, 0);
    main_set(p, 0, 0, 1);
    p->rat[0][4] = 0;
    p->rat[0][0] = 0;
    pat_backup = *p;
    goto_mode(B_SEQ, M_STEP);
    white(0);
    turn(EN_SELECT, 3 - (int32_t)ui.page);
    turn(EN_K2, 1);
    turn(EN_K1, 5);
    check((p->rat[0][0] & RAT_BOUNCE) && (p->rat[0][0] & 7u) == 5u, "RATCHET page: BOUNCE, 8 hits");
    shot("step-bounce");
    seq_goto(PG_BOUNCE);
    turn(EN_K1, 1);
    turn(EN_K2, -5);
    check((p->bnc[0] & 3u) == 2u && (p->bnc[0] >> 2) == 7u, "BOUNCE: from 1/16, -5 semitones");
    shot("track-clock-bounce");
    /* the roll sweep: a roll key held in PERF, KNOB 3 the roll's rate, faster as it turns */
    p->bnc[0] = BNC_DEFAULT;
    p->rat[0][0] = 0;
    pat_backup = *p;
    perf_goto(0);
    play_from_start();
    keys |= 1u;
    web_keys(keys);
    run_ms(40);
    {
        uint16_t kp[4];
        uint32_t slow, fast;
        page_params(kp);
        check(kp[1] == VP_ROLL, "PERF: a roll held, KNOB 2 is the roll");
        kset.accel = 0;
        turn(EN_K2, -127);
        c = trig_count[0];
        run_ms(1000);
        slow = trig_count[0] - c;
        turn(EN_K2, 80);
        shot("perf-roll-sweep");
        c = trig_count[0];
        run_ms(1000);
        fast = trig_count[0] - c;
        kset.accel = 1;
        printf("  roll sweep: %u hits a second at its slowest, %u at 80 (1/%u)\n", slow, fast,
               (unsigned)((4u * (uint32_t)exp2_q16((int32_t)(80u * 2882u)) + 32768u) >> 16));
        check(slow >= 2u && slow <= 3u, "roll sweep: slowest 1/4");
        check(fast >= 21u && fast <= 25u, "roll sweep: at 80, about 1/46 (23 a second)");
    }
    keys &= ~1u;
    web_keys(keys);
    run_ms(40);
    {
        uint16_t kp[4];
        page_params(kp);
        check(kp[1] == PAR_ROLL, "PERF: let go, KNOB 2 is the roll rate again");
    }
    press(B_PLAY);
    vp_set(PAR_ROLL, 5);
    check(!sq.roll_free, "the roll rate (PERF: MORE) back to the grid");
    {
        int rc;
        p->rat[0][4] = RAT_BOUNCE | 6u | BC_DRUNK << 3;
        p->bnc[3] = (uint8_t)((12u - 3u) << 2 | 2u);
        rc = proj_save(1, "BOUNCE  ");
        p->rat[0][4] = 0;
        p->bnc[3] = BNC_DEFAULT;
        rc |= proj_load(1);
        check(rc == 0 && p->rat[0][4] == (RAT_BOUNCE | 6u | BC_DRUNK << 3) && p->bnc[3] == ((12u - 3u) << 2 | 2u) &&
              p->bnc[0] == BNC_DEFAULT, "project (version 6): a bounce, a track's bounce settings");
    }
}

/* 0.6 (5b): track coupling */
static uint32_t lk_bar(uint32_t t)                   /* track t's hits over a bar from the start */
{
    uint32_t c = trig_count[t], n;
    play_from_start();
    run_ms(1900);
    n = trig_count[t] - c;
    press(B_PLAY);
    return n;
}
static void start_now(void)                          /* playing from the start, at its first tick (no button) */
{
    seq_start(1);
    while (sq.ticks == 0u)
        run_ms(1);
}
static void to_tick(uint32_t n)
{
    while (sq.ticks < n)
        run_ms(1);
}
static void lk_set(uint32_t l, uint32_t src, uint32_t ev, uint32_t tgt, uint32_t e, uint32_t w, int32_t a)
{
    pattern_t *p = &pat[0];
    p->link[l][0] = (uint8_t)(src | tgt << 3 | ev << 6);
    p->link[l][1] = (uint8_t)(e | w << 4);
    p->link[l][2] = (uint8_t)(int8_t)a;
    p->link[l][3] = 0;
    pat_backup = *p;
}
static void phase5b(void)
{
    pattern_t *p = &pat[0];
    uint32_t k, n, v0, c;
    p4_reset();
    for (k = 0; k < 16u; k++) {                      /* D1 on the beats, D2 every step */
        main_set(p, 0, k, (k & 3u) == 0u);
        main_set(p, 1, k, 1);
    }
    pat_backup = *p;
    check(lk_bar(1) == 16u, "LINKS: none, D2 16 hits");
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_SAME, 100);
    check(lk_bar(1) == 12u, "LINKS: D1 hit ducks D2 (same step)");
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_NEXT, 100);
    check(lk_bar(1) == 12u, "LINKS: .. the next step");
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_NEXT2, 100);
    check(lk_bar(1) == 8u, "LINKS: .. the next 2 steps");
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_BAR, 100);
    check(lk_bar(1) == 0u, "LINKS: .. the rest of the bar");
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_SAME, 50);
    n = lk_bar(1);
    printf("  DUCK 50%%: D2 %u hits of 16 (4 at stake)\n", n);
    check(n >= 12u && n <= 16u, "LINKS: DUCK 50%");
    /* depth 0: as no links */
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_BAR, 100);
    p->cdepth = 0;
    pat_backup = *p;
    memset(sq.locc, 0, sizeof sq.locc);
    check(lk_bar(1) == 16u && lk_bar(0) == 4u && !sq.locc[1], "LINKS: depth 0, as none (not looked at)");
    p->cdepth = 127;
    /* SILENT, ONLY IF: D2 only where D1 rests */
    lk_set(0, 0, LV_SILENT, 1, LE_ONLY, LW_SAME, 100);
    check(lk_bar(1) == 12u, "LINKS: ONLY IF D1 rests");
    /* ACCENT: only D1's loud hit */
    for (k = 0; k < 16u; k += 4u)
        p->step[0][k].volume = (uint8_t)((p->step[0][k].volume & ~STEP_VOLUME_MASK) | (k ? 80u : 127u));
    lk_set(0, 0, LV_ACCENT, 1, LE_DUCK, LW_SAME, 100);
    check(lk_bar(1) == 15u, "LINKS: ACCENT: a loud hit only");
    /* a loop, each link seeing those before it: D1 ducks D2, D2 ducks D1 */
    lk_set(0, 0, LV_HIT, 1, LE_DUCK, LW_SAME, 100);
    lk_set(1, 1, LV_HIT, 0, LE_DUCK, LW_SAME, 100);
    check(lk_bar(0) == 4u && lk_bar(1) == 12u, "LINKS: a loop: the first link wins");
    lk_set(0, 1, LV_HIT, 0, LE_DUCK, LW_SAME, 100);
    lk_set(1, 0, LV_HIT, 1, LE_DUCK, LW_SAME, 100);
    check(lk_bar(0) == 0u && lk_bar(1) == 16u, "LINKS: .. the other way round");
    memset(p->link, 0, sizeof p->link);
    /* TRIGGER: D3 at 30 a step after each D1 hit */
    lk_set(0, 0, LV_HIT, 2, LE_TRIG, LW_NEXT, 30);
    c = trig_count[2];
    start_now();
    to_tick(22);
    check(trig_count[2] == c, "LINKS: TRIGGER waits for the next step");
    to_tick(26);
    check(trig_count[2] == c + 1u && vc[2].velo == (int32_t)(30u * 32768u / 127u), "LINKS: TRIGGER at velocity 30");
    to_tick(380);
    seq_stop_now();
    check(trig_count[2] == c + 4u, "LINKS: TRIGGER after each hit");
    /* RATCHET: +2 on D2's hits with D1's */
    lk_set(0, 0, LV_HIT, 1, LE_RAT, LW_SAME, 2);
    check(lk_bar(1) == 24u, "LINKS: RATCHET +2");
    /* VELOCITY, PITCH, DECAY on that one hit */
    v0 = p->step[1][0].volume & STEP_VOLUME_MASK;
    lk_set(0, 0, LV_HIT, 1, LE_VEL, LW_SAME, -40);
    lk_set(1, 0, LV_HIT, 1, LE_PITCH, LW_SAME, 7);
    lk_set(2, 0, LV_HIT, 1, LE_DECAY, LW_SAME, -40);
    play_from_start();
    run_ms(5);
    check(vc[1].velo == (int32_t)((v0 - 40u) * 32768u / 127u), "LINKS: VELOCITY -40");
    check(vc[1].osc.base_note == p->step[1][0].note + 7u, "LINKS: PITCH +7");
    check(auto_par[1][2] == PAR_VELOD2 && auto_val[1][2] == (uint8_t)clamp32((int32_t)par[PAR_VELOD2] - 40, 0, 127),
          "LINKS: DECAY -40 (lane 2)");
    run_ms(130);
    check(vc[1].velo == (int32_t)(v0 * 32768u / 127u) && vc[1].osc.base_note == p->step[1][1].note &&
          auto_par[1][2] == 0u, "LINKS: the next hit as set");
    press(B_PLAY);
    memset(p->link, 0, sizeof p->link);
    /* SHIFT: D2 half a step late */
    lk_set(0, 0, LV_HIT, 1, LE_SHIFT, LW_SAME, 100);
    c = trig_count[1];
    start_now();
    to_tick(12);
    check(trig_count[1] == c, "LINKS: SHIFT holds the hit back");
    to_tick(15);
    check(trig_count[1] == c + 1u, "LINKS: .. half a step");
    seq_stop_now();
    memset(p->link, 0, sizeof p->link);
    pat_backup = *p;
    /* the page: SHIFT + SEQ; Start; the sentence; LINK SETUP; the grid's mark */
    hold(B_OCTDN);                                   /* (SHIFT) */
    press(B_SEQ);
    release(B_OCTDN);
    check(ui.mode == M_PERF && ui.page == PG_LINKS, "SHIFT + SEQ: LINKS (29: PERF's GLOBAL MOD)");
    shot("links-empty");
    turn(EN_K2, 1);
    check(!memcmp(p->link[0], LK_START[0], 4), "LINKS: Start: DUCK HATS");
    shot("links-duck-hats");
    turn(EN_K1, 1);
    turn(EN_K2, 2);
    check(!memcmp(p->link[1], LK_START[1], 4) && ui.link == 1u, "LINKS: link 2, CALL / RESPONSE");
    kset.accel = 0;
    turn(EN_K3, -30);
    kset.accel = 1;
    check((int8_t)p->link[1][2] == 70, "LINKS: Amount");
    turn(EN_SELECT, 1);
    turn(EN_K3, 2);
    check((p->link[1][1] & 15u) == LE_RAT && (int8_t)p->link[1][2] == LK_DEF[LE_RAT], "LINK SETUP: Effect, its amount");
    turn(EN_K1, -1);
    check((p->link[1][0] & 7u) == 5u && (p->link[1][0] >> 6) == LV_ACCENT, "LINK SETUP: When (CH hits hard)");
    shot("link-setup");
    turn(EN_SELECT, -1);
    shot("links-custom");
    turn(EN_SELECT, -1);
    check(ui.page == PG_MACRO + 1u, "SELECT: GLOBAL MOD's pages before LINKS");
    goto_mode(B_HOME, M_HOME);
    shot("links-grid-mark");
    {
        int rc;
        p->cdepth = 90;
        rc = proj_save(1, "LINKS   ");
        memset(p->link, 0, sizeof p->link);
        p->cdepth = 127;
        rc |= proj_load(1);
        check(rc == 0 && !memcmp(p->link[0], LK_START[0], 4) && (p->link[1][1] & 15u) == LE_RAT && p->cdepth == 90,
              "project (version 6): the links, the depth");
    }
}

/* 0.6 (5c): PERFORM 2 */
static void p2_key(uint32_t w, int down)             /* white key w held or let go */
{
    keys = down ? keys | 1u << WHITE_KEY[w] : keys & ~(1u << WHITE_KEY[w]);
    web_keys(keys);
    run_ms(30);
}
static void phase5c(void)
{
    pattern_t *p = &pat[0];
    uint32_t i, k, n, c, t0, t1;
    int16_t l[BLOCK], r[BLOCK];
    p4_reset();
    for (k = 0; k < 16u; k += 4u)
        main_set(p, 0, k, 1);
    pat_backup = *p;
    perf_goto(PG_PERF2);
    check(ui.page == PG_PERF2 && perf_bank2(), "PERFORM 2: the seventh PERF page");
    /* HALF: the ticks at half the rate */
    start_now();
    t0 = sq.ticks;
    run_ms(500);
    t0 = sq.ticks - t0;
    p2_key(7, 1);
    t1 = sq.ticks;
    run_ms(500);
    t1 = sq.ticks - t1;
    shot("perform2");
    p2_key(7, 0);
    printf("  HALF: %u ticks in 0.5 s (%u without)\n", t1, t0);
    check(t1 * 2u >= t0 - 2u && t1 * 2u <= t0 + 2u, "HALF: half the ticks");
    /* DRILL: D1's 4 hits a bar each 2 .. 8 hits */
    seq_stop_now();
    c = trig_count[0];
    start_now();
    p2_key(0, 1);
    to_tick(380);
    n = trig_count[0] - c;
    p2_key(0, 0);
    seq_stop_now();
    printf("  DRILL: D1 %u hits in a bar (4 plain)\n", n);
    check(n >= 9u && n <= 32u && !sq.drill, "DRILL: 2 .. 8 hits a step, let go");
    /* JITTER: hits late by up to a 32nd (12 ticks), not lost */
    pfs.jitter = 127;
    hold(B_OCTDN);                                   /* (SHIFT: latched) */
    p2_key(8, 1);
    p2_key(8, 0);
    release(B_OCTDN);
    check((pf.on >> PF_JITTER) & 1u && (ui.pf_latch >> PF_JITTER) & 1u, "JITTER latched");
    for (k = 0; k < 16u; k++)
        main_set(p, 1, k, 1);
    pat_backup = *p;
    {
        uint32_t late = 0, most = 0, hits = 0, last = trig_count[1];
        start_now();
        while (sq.ticks < 384u) {
            uint32_t tk = sq.ticks;
            run_ms(1);
            if (trig_count[1] != last) {
                uint32_t d = tk % 24u;
                last = trig_count[1];
                hits++;
                late += d > 0u;
                most = d > most ? d : most;
            }
        }
        seq_stop_now();
        printf("  JITTER: %u of %u hits late, the most %u ticks\n", late, hits, most);
        check(hits >= 15u && late >= 6u && most <= 12u, "JITTER: hits late by up to a 32nd");
    }
    /* latched across banks: JITTER stays on PERFORM; there key 9 is REPEAT's (bank 1) */
    turn(EN_SELECT, -(int32_t)PG_PERF2);
    check(!perf_bank2() && (pf.on >> PF_JITTER) & 1u, "PERFORM: JITTER still latched");
    p2_key(8, 1);
    check((pf.on >> PF_STUT) & 1u, "PERFORM: key 9 is REPEAT there");
    turn(EN_SELECT, (int32_t)PG_PERF2);
    p2_key(8, 0);
    check(!((pf.on >> PF_STUT) & 1u) && (pf.on >> PF_JITTER) & 1u, "a key lets go in the bank it went down in");
    p2_key(8, 1);                                    /* (the latched JITTER let go) */
    p2_key(8, 0);
    check(!((pf.on >> PF_JITTER) & 1u), "JITTER: the key again lets go");
    for (k = 0; k < 16u; k++)
        main_set(p, 1, k, 0);
    pat_backup = *p;
    /* SHORT: the kick's tail */
    {
        float e0 = 0, e1 = 0;
        for (k = 0; k < 2u; k++) {
            if (k)
                p2_key(10, 1);
            start_now();
            run_ms(60);
            for (i = 0; i < 6u; i++) {
                uint32_t j;
                web_render(1024);
                for (j = 0; j < 1024u; j++)
                    *(k ? &e1 : &e0) += out_l[j] * out_l[j];
            }
            seq_stop_now();
            run_ms(400);
        }
        p2_key(10, 0);
        printf("  SHORT: the kick's tail %.1f%% of its energy\n", 100.0 * e1 / (e0 + 1e-9));
        check(e0 > 1.0f && e1 < e0 * 0.1f, "SHORT: the tail cut");
    }
    /* PUMP: the gain at D1's hit, then back */
    pfs.pump = 127;
    p2_key(11, 1);
    start_now();
    run_ms(8);
    check(pump_g1 < 3000, "PUMP: down at D1's hit");
    run_ms(300);
    check(pump_g1 == 32768, "PUMP: back after an 8th");
    p2_key(11, 0);
    seq_stop_now();
    /* the master's: COMB rings at its note, CUTS shuts its share of 32nds, CRUSH holds, REVERSE plays backwards */
    {
        static int16_t y[8192];
        uint32_t on = pf.on, d;
        double a0 = 0, a1 = 0, a2 = 0;
        pf.on = 1u << PF_COMB;
        pfs.comb_note = 45;                          /* A2: 110 Hz, 400.9 samples */
        memset(comb_buf, 0, sizeof comb_buf);
        for (i = 0; i < 8192u / BLOCK; i++) {
            for (k = 0; k < BLOCK; k++)
                l[k] = r[k] = (int16_t)(i == 0u && k == 0u ? 20000 : 0);
            perf2_master(l, r);
            memcpy(y + i * BLOCK, l, sizeof l);
        }
        for (k = 1000; k < 4000u; k++) {
            a0 += (double)y[k] * y[k];
            a1 += (double)y[k] * y[k + 401u];
            a2 += (double)y[k] * y[k + 300u];
        }
        printf("  COMB A2: correlation %.2f at 401 samples, %.2f at 300\n", a1 / a0, a2 / a0);
        check(a1 / a0 > 0.8 && a2 / a0 < 0.3, "COMB: rings at its note");
        pf.on = 1u << PF_CUTS;
        pfs.cuts = 64;                               /* ~50% */
        for (i = 0, n = 0; i < 2000u; i++) {
            for (k = 0; k < BLOCK; k++)
                l[k] = r[k] = 10000;
            perf2_master(l, r);
            for (k = 0; k < BLOCK; k++)
                n += l[k] == 0;
        }
        printf("  CUTS 50%%: %.0f%% of the output shut\n", 100.0 * n / (2000.0 * BLOCK));
        check(n > 2000u * BLOCK * 3u / 10u && n < 2000u * BLOCK * 7u / 10u, "CUTS: about half shut");
        pf.on = 1u << PF_CRUSH;
        pfs.crush = 127;
        for (k = 0; k < BLOCK; k++)
            l[k] = r[k] = (int16_t)(k * 300u + 77u);
        perf2_master(l, r);
        for (k = 1, n = 0; k < BLOCK; k++)
            n += l[k] == l[k - 1u];
        check(n >= 56u && (l[5] & 4095) == 0, "CRUSH: held 16 samples, 4 bits");
        pf.on = 0;
        for (k = 0; k < BLOCK; k++)
            l[k] = r[k] = 0;
        for (d = 0; d < REC_LEN; d++)                /* (the recorder: a ramp, its newest at rec_w - 1) */
            rec_buf[(pf.rec_w + d) % REC_LEN] = (int16_t)(d * 5u);
        pf.on = 1u << PF_REVERSE;
        pfs.rev_len = 1;
        for (i = 0, n = 0; i < 8u; i++) {
            for (k = 0; k < BLOCK; k++)
                l[k] = r[k] = 0;
            perf2_master(l, r);
            for (k = 0; k < BLOCK; k++) {
                uint32_t q = i * BLOCK + k, want = (REC_LEN - 1u - q / 2u) * 5u;
                if (q >= 64u && (uint32_t)(l[k] - (int32_t)want + 2) > 4u)
                    n++;
            }
        }
        check(n == 0u, "REVERSE: the recording backwards");
        pf.on = 0;
        for (i = 0; i < 4u; i++)
            perf2_master(l, r);
        check(!pf.rv_len, "REVERSE: let go, the recorder again");
        pf.on = on;
    }
    turn(EN_SELECT, 1);
    kset.accel = 0;
    turn(EN_K1, -21);
    kset.accel = 1;
    check(pfs.drill == 59u, "PERFORM 2: SETUP: Drill");
    shot("perform2-setup");
    turn(EN_SELECT, 1);
    kset.accel = 0;
    turn(EN_K1, 12);
    kset.accel = 1;
    check(pfs.comb_note == 57u, "PERFORM 2: SETUP 2: Comb A3");
    shot("perform2-setup2");
    {
        int rc = proj_save(1, "PERF2   ");
        pfs.comb_note = 30;
        pfs.drill = 1;
        rc |= proj_load(1);
        check(rc == 0 && pfs.comb_note == 57u && pfs.drill == 59u, "project: PERFORM 2's settings (KPF2)");
    }
    goto_mode(B_HOME, M_HOME);
}

/* 0.6 (5d), 0.9 (10): the models (Rings' resonators; tests/rings_compare.cc holds them to Rings' own code) */
static double goertzel(const float *y, uint32_t n, double f)   /* |the DFT of y at f Hz| / n */
{
    double w = 2.0 * 3.14159265358979 * f / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0, s0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = y[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}
static float mdl_y[16384];
static void mdl_hit(uint32_t vel, uint32_t skip_ms, uint32_t n)   /* D1 struck; n samples from skip_ms on */
{
    uint32_t k = 0;
    run_ms(300);
    voice_trigger(0, vel, vc[0].osc.base_note);
    run_ms(skip_ms);
    while (k < n) {
        uint32_t j, m = n - k > 1024u ? 1024u : n - k;
        web_render(m);
        for (j = 0; j < m; j++)
            mdl_y[k + j] = out_l[j];
        k += m;
    }
}
/* model voice 1 alone (model_block, no voice around it): a hit at hz, n samples of it into mdl_y (x 1/32768). in:
 * the voice's own sound (0: silence), the share EXCITER takes; its peak */
static double rg_hit(uint32_t md, uint32_t dec, uint32_t bright, uint32_t mat, uint32_t pos, uint32_t exc, double hz,
                     uint32_t vel, uint32_t n, int16_t (*in)(uint32_t))
{
    voice_t v;
    double pk = 0;
    uint32_t k, i;
    memset(&v, 0, sizeof v);
    memset(&mdl[1], 0, sizeof mdl[1]);
    mdl[1].want = mdl[1].type = (uint8_t)md;
    mdl[1].dec = (uint8_t)dec;
    mdl[1].damp = (uint8_t)bright;
    mdl[1].mat = (uint8_t)mat;
    mdl[1].pos = (uint8_t)pos;
    mdl[1].exc = (uint8_t)exc;
    v.osc.inc = (uint32_t)(hz / FS * 4294967296.0);
    model_reset(&mdl[1], 1);                         /* (as a block would, the model new: before the hit) */
    rg_rng = 0x9E3779B9u;                            /* (the plucker's noise: the same each run) */
    model_trigger(1, vel, v.osc.inc);
    for (k = 0; k < n; k += BLOCK) {
        int16_t b[BLOCK];
        for (i = 0; i < BLOCK; i++)
            b[i] = in ? in(k + i) : 0;
        model_block(1, &v, b);
        for (i = 0; i < BLOCK && k + i < 16384u; i++)
            mdl_y[k + i] = b[i] / 32768.0f;
        for (i = 0; i < BLOCK; i++)
            pk = fabs(b[i]) > pk ? fabs(b[i]) : pk;
    }
    return pk / 32768.0;
}
static int16_t rg_click(uint32_t i) { return i < 8u ? (int16_t)(20000 - 2500 * (int32_t)i) : 0; }
static uint32_t rg_period(uint32_t lo, uint32_t hi, uint32_t from)   /* the lag of mdl_y's autocorrelation's peak */
{
    uint32_t best = 0, lag, k;
    double bv = -1e30;
    for (lag = lo; lag < hi; lag++) {
        double a = 0;
        for (k = from; k < from + 4096u; k++)
            a += mdl_y[k] * mdl_y[k + lag];
        if (a > bv) {
            bv = a;
            best = lag;
        }
    }
    return best;
}
static double rg_rms(uint32_t a, uint32_t n)
{
    double e = 0;
    uint32_t k;
    for (k = a; k < a + n; k++)
        e += mdl_y[k] * mdl_y[k];
    return sqrt(e / n);
}
static void phase5d(void)
{
    uint32_t k, ok, md;
    /* MODAL, STRUCTURE 34 (0.27: Rings' harmonic stretch, none): its modes at the pitch's multiples */
    rg_hit(MD_MODAL, 90, 90, 34, 5, 0, 220, 127, 16384, NULL);
    printf("  MODAL at 220 Hz, harmonic:");
    for (k = 1, ok = 1; k <= 6u; k++) {
        double on = goertzel(mdl_y, 16384, 220.0 * k);
        double off = goertzel(mdl_y, 16384, 220.0 * k * 0.97) + goertzel(mdl_y, 16384, 220.0 * k * 1.03);
        printf(" %u%s", 220u * k, on > off ? "" : "?");   /* (a peak: above both its neighbours together) */
        ok &= on > off;
    }
    printf("\n");
    check(ok, "MODAL: STRUCTURE 0.27, the modes harmonic");
    /* .. STRUCTURE 100 (0.79): stretched, the 4th mode well above 4 f */
    {
        double st = 0;
        uint32_t best = 0, f;
        rg_hit(MD_MODAL, 90, 90, 100, 20, 0, 220, 127, 16384, NULL);
        for (f = 900; f < 2400u; f += 5) {
            double g = goertzel(mdl_y, 16384, f);
            if (g > st) {
                st = g;
                best = f;
            }
        }
        printf("  MODAL STRUCTURE 100: the strongest partial between 900 and 2400 Hz at %u Hz\n", best);
        check(goertzel(mdl_y, 16384, 880) < st * 0.5 && best > 950u, "MODAL: a stiff STRUCTURE stretches the modes");
    }
    /* STRING: its period */
    rg_hit(MD_STRING, 90, 70, 32, 40, 0, 110, 127, 16384, NULL);
    k = rg_period(200, 1000, 2048);
    printf("  STRING at 110 Hz: period %u samples (%.1f)\n", k, FS / 110.0);
    check(abs((int)k - 401) <= 1, "STRING: at the voice's pitch");
    /* .. below the line's lowest (1020 samples, 43 Hz): at a slower rate, still at pitch */
    rg_hit(MD_STRING, 100, 70, 32, 40, 0, 32.7, 127, 16384, NULL);
    k = rg_period(1000, 1800, 2048);
    printf("  STRING at 32.7 Hz: period %u samples (%.1f)\n", k, FS / 32.7);
    check(abs((int)k - 1349) <= 3, "STRING: below its line's length, at pitch");
    /* FM, a ratio of 1: harmonics of the pitch, its period */
    {
        uint32_t m1 = 0;
        for (k = 0; k < 128u; k++)                   /* (the STRUCTURE whose quantized ratio is 1) */
            if (RG_FM_RATIO[(k * 516u) >> 9] == 0 && RG_FM_RATIO[((k * 516u) >> 9) + 1u] == 0) {
                m1 = k;
                break;
            }
        rg_hit(MD_FM, 80, 70, m1, 64, 0, 196, 127, 16384, NULL);
        k = rg_period(100, 400, 1024);
        printf("  FM at 196 Hz, ratio 1 (STRUCTURE %u): period %u samples (%.1f)\n", m1, k, FS / 196.0);
        check(m1 > 0u && abs((int)k - 225) <= 1, "FM: at the voice's pitch");
    }
    /* DECAY: longer, and more of the hit left 0.3 s on */
    for (md = MD_MODAL; md <= MD_FM; md++) {
        double r1, r2;
        rg_hit(md, 20, 70, 34, 30, 0, 220, 127, 16384, NULL);
        r1 = rg_rms(13230, 2048) / (rg_rms(0, 2048) + 1e-9);
        rg_hit(md, 110, 70, 34, 30, 0, 220, 127, 16384, NULL);
        r2 = rg_rms(13230, 2048) / (rg_rms(0, 2048) + 1e-9);
        printf("  %s DECAY 20 / 110: %.1f / %.1f dB at 0.3 s\n", L_MODEL[md], 20 * log10(r1 + 1e-9), 20 * log10(r2 + 1e-9));
        check(r2 > r1 * 2.0, "MODEL: DECAY lengthens the ring");
    }
    /* the level of a hit, whatever the model, its knobs and the pitch: under full scale (Rings' limiter, then the soft
     * clip), and there (BRIGHT filters Rings' exciter: a dark hit ~20 dB under a bright one, as in Rings) */
    {
        static const uint32_t KN[3] = {10, 64, 120};
        static const double HZ[3] = {55, 220, 1000};
        double lo = 9, hi = 0;
        uint32_t a, b, f;
        for (md = MD_MODAL; md <= MD_FM; md++)
            for (a = 0; a < 3u; a++)
                for (b = 0; b < 3u; b++)
                    for (f = 0; f < 3u; f++) {
                        double pk = rg_hit(md, 64, KN[a], KN[b], 40, 0, HZ[f], 127, FS / 2u, NULL);
                        lo = pk < lo ? pk : lo;
                        hi = pk > hi ? pk : hi;
                    }
        printf("  a hit's peak over every model, BRIGHT, STRUCTURE and pitch: %.2f .. %.2f of full scale\n", lo, hi);
        check(lo > 0.04 && hi < 0.98, "MODEL: a hit's level");
    }
    /* EXCITER: 0, Rings' own exciter; 127, the voice's sound only (silence: nothing; a click: the model rings) */
    for (md = MD_MODAL; md <= MD_FM; md++) {
        double own = rg_hit(md, 64, 64, 34, 40, 0, 220, 127, 8192, NULL);
        double none = rg_hit(md, 64, 64, 34, 40, 127, 220, 127, 8192, NULL);
        double click = rg_hit(md, 64, 64, 34, 40, 127, 220, 127, 8192, rg_click);
        printf("  %s EXCITER 0: %.3f; 127, silent voice %.4f, a click %.3f\n", L_MODEL[md], own, none, click);
        check(own > 0.05 && none < 0.002 && click > 0.01, "MODEL: EXCITER, Rings' own exciter or the voice's sound");
    }
    /* velocity: a softer hit, quieter */
    {
        double hard = rg_hit(MD_MODAL, 64, 64, 34, 40, 0, 220, 127, 8192, NULL);
        double soft = rg_hit(MD_MODAL, 64, 64, 34, 40, 0, 220, 40, 8192, NULL);
        check(soft < hard * 0.6, "MODEL: velocity");
    }
    /* the voice: D1 on MODAL rings at its pitch */
    p4_reset();
    par_set(PAR_FILTER_FREQ_1, 127);                 /* (the voice's filter open) */
    par_set(PAR_FILTER_TYPE_1, F_OFF - 1u);
    par_set(PAR_MODEL1, MD_STRING);
    par_set(PAR_MDECAY1, 90);
    run_ms(20);
    mdl_hit(127, 50, 16384);
    {
        double f0 = (double)vc[0].osc.inc * FS / 4294967296.0, per = FS / f0;
        uint32_t lo = (uint32_t)(per * 0.8), hi = (uint32_t)(per * 1.25);
        k = rg_period(lo, hi, 0);
        printf("  D1 on STRING: period %u samples (%.1f for %.1f Hz)\n", k, per, f0);
        check(fabs(k - per) < 2.0, "MODEL: D1's STRING at D1's pitch");
    }
    /* TENSION: the pitch up at the hit, falling back */
    par_set(PAR_MODEL1, MD_MODAL);
    par_set(PAR_MTENS1, 127);
    run_ms(20);
    voice_trigger(0, 127, vc[0].osc.base_note);
    check(mdl[0].ten > 30000, "MODEL: TENSION up at the hit");
    run_ms(300);
    check(mdl[0].ten < 1000, "MODEL: .. and back");
    par_set(PAR_MTENS1, 0);
    /* the page: EDIT on D1 to MODEL; LXR again: the LXR's engine */
    goto_mode(B_HOME, M_HOME);
    press(B_EDIT);
    while (ui.sect != S_MODEL && ui.mode == M_SOUND) {
        uint32_t s0 = ui.sect, h0 = ui.half;
        press(B_EDIT);
        if (ui.sect == s0 && ui.half == h0)
            break;
    }
    check(ui.sect == S_MODEL, "EDIT: the MODEL section on D1");
    shot("model");
    turn(EN_K1, -1);
    check(par[PAR_MODEL1] == MD_LXR && mdl[0].type == MD_LXR, "MODEL: back to the LXR's engine");
    turn(EN_K1, 2);
    check(par[PAR_MODEL1] == MD_STRING, "MODEL: K1 to STRING");
    par_set(PAR_MPOS1, 30);
    run_ms(20);
    {
        int rc = proj_save(1, "MODEL   ");
        par[PAR_MODEL1] = 0;
        par[PAR_MPOS1] = 0;
        rc |= proj_load(1);
        run_ms(20);
        check(rc == 0 && par[PAR_MODEL1] == MD_STRING && par[PAR_MPOS1] == 30u && mdl[0].type == MD_STRING,
              "project: the model's parameters");
    }
    {   /* 0.15 (17c): two models play at once; D3's, the third, plays as an LXR voice, and its MODEL page says so */
        par_set(PAR_MODEL1 + 1, MD_MODAL);
        par_set(PAR_MODEL1 + 2, MD_PL0);
        run_ms(20);
        check(mdl[0].type == MD_STRING && mdl[1].type == MD_MODAL && mdl[2].type == 0 && mdl[2].want == MD_PL0,
              "the models' limit: D3's, the third, plays as LXR");
        ui.track = 2;
        run_ms(100);
        check(ui.sect == S_MODEL, "D3: its MODEL section");
        shot("model-limit");
        par_set(PAR_MODEL1 + 1, 0);
        run_ms(20);
        check(mdl[2].type == MD_PL0, "the models' limit: D3's plays once D2's goes");
        par_set(PAR_MODEL1 + 2, 0);
        run_ms(20);
        ui.track = 0;
    }
    {   /* 1.0: D2 and D3 on models: D1's MODEL knob goes past Rings' and Plaits' (D3 never moved to the LXR's) */
        par_set(PAR_MODEL1, 0);
        par_set(PAR_MODEL1 + 1, MD_MODAL);
        par_set(PAR_MODEL1 + 2, MD_PL0);
        run_ms(20);
        ui.track = 0;
        run_ms(100);
        turn(EN_K1, 1);
        run_ms(20);
        check(!model_counted(par[PAR_MODEL1]) && par[PAR_MODEL1] != 0u && mdl[2].type == MD_PL0 &&
              !strcmp(ui.msg, "D2+D3 HAVE THE 2 MODELS"), "the models' limit: D1's knob past them, D3's kept, said why");
        shot("model-limit-knob");
        turn(EN_K1, -1);
        check(par[PAR_MODEL1] == 0u, ".. and back to the LXR's");
        par_set(PAR_MODEL1 + 2, 0);
        run_ms(20);
        turn(EN_K1, 1);
        check(par[PAR_MODEL1] == MD_MODAL, ".. one free: D1's knob to MODAL");
        par_set(PAR_MODEL1, MD_STRING);
        par_set(PAR_MODEL1 + 1, 0);
        run_ms(20);
    }
    /* a project before 0.9 (version 6): its models MEMBRANE BAR BELL PLATE to MODAL, TUBE to STRING */
    {
        static uint8_t kb[KIT_LEN];
        uint32_t i;
        memset(kb, 0, sizeof kb);
        for (i = 0; i < 3u; i++)
            kb[kit_idx(PAR_MODEL1 + i)] = (uint8_t)(i == 0 ? 3u : i == 1 ? 5u : 0u);
        proj_models_08(kb);
        check(kb[kit_idx(PAR_MODEL1)] == MD_MODAL && kb[kit_idx(PAR_MODEL1 + 1)] == MD_STRING &&
              kb[kit_idx(PAR_MODEL1 + 2)] == MD_LXR, "project 6: the old models to Rings' (BELL MODAL, TUBE STRING)");
    }
    par_set(PAR_MODEL1, 0);
    goto_mode(B_HOME, M_HOME);
}

static void grids_test(void)
{
    pattern_t *p = &pat[0];
    uint32_t i, part, ok = 1, x, y;
    for (part = 0; part < 3u; part++)                 /* the map at every 17th point, every step */
        for (x = 0; x < 256u; x += 17)
            for (y = 0; y < 256u; y += 17)
                for (i = 0; i < 32u; i++)
                    ok &= grids_level(part, i, x, y) == grids_ref(part, i, x, y);
    check(ok, "GRIDS: the map read as Grids reads it");
    check(grids_level(0, 0, 0, 0) == GRIDS_NODE[10][0] * 255u / 256u * 255u / 256u, "GRIDS: the map's corner is node 10");
    /* D1 BD, SN SD, CH HH from the middle of the map, density 100, accent, no chaos: the live tracks as the map */
    p4_reset();
    p->gx = 64;
    p->gy = 64;
    p->gchaos = 0;
    for (i = 0; i < 3u; i++) {
        uint32_t t = i == 0 ? 0u : i == 1 ? 3u : 5u;
        p->gen[t][0] = GEN_GRIDS;
        p->gen[t][1] = (uint8_t)i;
        p->gen[t][2] = 100;
        p->gen[t][3] = 1;
    }
    sq.gsync = 0x7F;
    run_ms(20);
    {
        uint32_t t, k, n = 0;
        ok = 1;
        for (i = 0; i < 3u; i++) {
            uint32_t thr = 255u - u7to8(100);
            t = i == 0 ? 0u : i == 1 ? 3u : 5u;
            for (k = 0; k < 16u; k++) {
                uint32_t e = grids_ref(i, 2 * k, u7to8(64), u7to8(64)), o = grids_ref(i, 2 * k + 1, u7to8(64), u7to8(64));
                int he = e > thr, ho = o > thr;
                ok &= main_on(&gpat, t, k) == (he || ho);
                if (he || ho) {
                    n++;
                    ok &= (gpat.timing[t][k] >> 4) == (he ? 7u : 11u);
                    ok &= (gpat.step[t][k].volume & 127u) == ((he ? e : o) > 192u ? 127u : 72u);
                }
            }
        }
        printf("  GRIDS at the middle, density 100: %u hits over BD SD HH\n", n);
        check(ok && n > 6, "GRIDS: the tracks as the map has them (hits, the half-step shifts, accents)");
    }
    play_from_start();
    i = hits_in(0, 1900) + hits_in(3, 0);
    check(i > 0, "GRIDS: the tracks play");
    p->gen[0][2] = 0;
    check(hits_in(0, 2000) == 0, "GRIDS at density 0: nothing");
    p->gen[0][2] = 100;
    {
        uint16_t before = gpat.main[5][0];
        p->gx = 127;
        p->gy = 10;
        run_ms(2000);
        check(gpat.main[5][0] != before, "GRIDS: another place on the map, another pattern");
    }
    press(B_PLAY);
    goto_mode(B_HOME, M_HOME);
    black(5);
    seq_goto(PG_GEN);
    shot("grids-generator");
    turn(EN_SELECT, 1);
    {
        uint16_t pp[4];
        page_params(pp);
        check(pp[0] == VP_ACTION && pp[1] == VP_GX && pp[3] == VP_GCHAOS, "GRIDS: the map on the ACTION page");
    }
    kset.accel = 0;
    turn(EN_K2, -20);
    kset.accel = 1;
    check(p->gx == 107, "GRIDS MAP: K2 moves X");
    run_ms(50);
    shot("grids-map");
    {
        int rc = proj_save(1, "GRIDS   ");
        p->gx = p->gy = p->gchaos = 0;
        p->gchaos = 0;
        rc |= proj_load(1);
        check(rc == 0 && p->gx == 107 && p->gy == 10 && p->gen[5][0] == GEN_GRIDS, "project (version 5): the map");
    }
}


/* ------------------------------------------------------ Phase 14 (0.13) --- */
/* the LFO's modes, the global LFOs, a button's last page, Euclid's second pulse */
static void phase14(void)
{
    pattern_t *p = &pat[0];
    uint16_t pp[4];
    uint32_t i, ok, pg, np, ph;
    int32_t lo, hi;
    char b[40];
    lfo_t *l = &vc[0].lfo;
    p4_reset();
    sq.muted = 0;
    kset.accel = 0;                                  /* (exact detents) */
    black(0);
    /* 14d: Euclid's second pulse, on the ACTION page; joined OR, XOR, AND, SUB */
    seq_goto(PG_GEN);
    turn(EN_K1, 1);
    turn(EN_K3, 4);
    check(p->gen[0][0] == GEN_EUCLID && p->main[0][0] == 0x1111u, "EUCLID: 4 hits");
    turn(EN_SELECT, 1);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[1] == VP_EH2 && pp[2] == VP_ER2 && pp[3] == VP_EOP && !strcmp(b, "ACTION / EUCLID 2"),
          "ACTION with EUCLID: Hits 2, Rotate 2, Join");
    turn(EN_K2, 3);
    check(p->main[0][0] == (0x1111u | 0x0841u), "EUCLID 2: 3 hits OR'd in");
    shot("euclid2");
    turn(EN_K4, 1);
    check(p->main[0][0] == (0x1111u ^ 0x0841u), "EUCLID 2: XOR");
    turn(EN_K4, 1);
    check(p->main[0][0] == 0x0001u, "EUCLID 2: AND");
    turn(EN_K4, 1);
    check(p->main[0][0] == 0x1110u, "EUCLID 2: SUB");
    turn(EN_K3, 2);
    check(p->main[0][0] == (0x1111u & ~0x2104u), "EUCLID 2: rotated");
    {
        int rc = proj_save(1, "EU2     ");
        p->eu2[0][0] = p->eu2[0][1] = p->eu2[0][2] = 0;
        rc |= proj_load(1);
        p = &pat[0];
        check(!rc && p->eu2[0][0] == 3 && p->eu2[0][1] == 2 && p->eu2[0][2] == EU_SUB,
              "EUCLID 2: saved with the project (version 9)");
    }
    turn(EN_K2, -3);
    check(p->eu2[0][0] == 0 && p->main[0][0] == 0x1111u, "EUCLID 2: Hits 2 at 0, the first alone");
    seq_clear_track(0, p);
    sq.gsync = 0x7F;

    /* 14a: the LFO's modes (D1's LFO; RETRIGGER off: its own hits in HOLD, ONE, HALF) */
    par_set(PAR_WAVE_LFO1, LFO_SAW_UP);
    par_set(PAR_FREQ_LFO1, 127);
    par_set(PAR_SYNC_LFO1, 0);
    par_set(PAR_RETRIGGER_LFO1, 0);
    par_set(PAR_LFOMODE1, LM_TRIG);
    run_ms(10);
    ph = l->phase;
    voice_trigger(0, 100, SEQ_DEFAULT_NOTE);
    check(l->phase == ph, "TRIG, RETRIGGER off: free, its own hit no restart");
    par_set(PAR_LFOMODE1, LM_ONE);
    run_ms(10);
    ph = l->phase;
    run_ms(50);
    check(l->mode == LM_ONE && !l->run && l->phase == ph && ph == l->offset, "ONE: still at the offset until a hit");
    voice_trigger(0, 100, SEQ_DEFAULT_NOTE);
    check(l->run && !l->gone && l->phase == l->offset, "ONE: a hit starts it from the offset");
    run_ms(3000);
    check(!l->run && l->gone == 0xFFFFFFFFu && l->phase == l->offset - 1u, "ONE: one cycle, then still at its end");
    par_set(PAR_LFOMODE1, LM_HALF);
    run_ms(10);
    voice_trigger(0, 100, SEQ_DEFAULT_NOTE);
    run_ms(3000);
    check(!l->run && l->gone == 0x80000000u, "HALF: half a cycle, then still");
    par_set(PAR_LFOMODE1, LM_HOLD);
    run_ms(10);
    lo = l->hold;
    ph = l->phase;
    run_ms(37);
    check(l->hold == lo && l->phase != ph, "HOLD: runs, its value held");
    voice_trigger(0, 100, SEQ_DEFAULT_NOTE);
    check(l->hold == lfo_wave(l, l->phase) && l->hold != lo, "HOLD: a hit takes its value then");
    goto_mode(B_LFO, M_SOUND);
    check(ui.sect == S_LFO && ui.half == 0, "LFO: LFO 1's first page");
    turn(EN_SELECT, 1);
    page_params(pp);
    check(ui.sect == S_LFO && ui.half == 1 && pp[2] == PAR_LFOMODE1, "1.0: LFO 1's MODE: its second page, K3, by SELECT");
    press(B_LFO);
    check(ui.sect == S_LFO2 && ui.half == 0, "1.0: LFO again, from LFO 1's pages: LFO 2 (the next section, no paging)");
    press(B_LFO);
    check(ui.sect == S_RANDOM && ui.half == 0, "1.0: LFO again: RANDOM PER HIT");
    press(B_ENV);
    press(B_ENV);
    check(ui.sect == S_GATE && ui.half == 0, "1.0: ENV again: GATE, not AMP EG's second page");
    goto_mode(B_LFO, M_SOUND);
    while (ui.sect != S_LFO)
        press(B_LFO);
    turn(EN_SELECT, 1);
    check(ui.sect == S_LFO && ui.half == 1, "LFO MODE: back on its page by SELECT");
    turn(EN_K3, -3);
    check(par[PAR_LFOMODE1] == LM_TRIG, "LFO MODE: back to TRIG by its knob");
    shot("lfo-mode");
    turn(EN_SELECT, -1);                             /* 0.14: SELECT's order too: the LFO's two pages */
    check(ui.sect == S_LFO && ui.half == 0, "SELECT: before LFO 1's second page, its first");
    turn(EN_SELECT, 2);
    check(ui.sect == S_LFO2 && ui.half == 0, "SELECT: after LFO 1's pages, TRK LFO 2 (25b)");
    turn(EN_SELECT, 2);
    check(ui.sect == S_RANDOM && ui.half == 0, "SELECT: after LFO 2, RANDOM PER HIT (29)");

    /* 14b: the global LFOs. MORPH by a one-shot saw over a bar from PLAY: the knob stays, the sound moves */
    p4_reset();
    kit_set_morph(0);
    memset(gls, 0, sizeof gls);
    gls[0].dest = GD_MORPH;
    gls[0].depth = 127;
    gls[0].len = 4;                                  /* (1 bar) */
    gls[0].wave = LFO_SAW_UP;
    gls[0].mode = GM_ONE;
    gls[0].trig = GT_PLAY;
    gls[1].len = 4;
    gls[1].wave = LFO_SAW_UP;
    run_ms(50);
    check(morph_amt == 0 && !gl.l[0].run, "GLOBAL LFO ONE: waiting for PLAY");
    start_now();
    to_tick(192);
    run_ms(20);
    check(morph_amt > 100 && morph_amt < 160 && par[PAR_MORPH] == 0, "GLOBAL LFO: morph halfway at half a bar, knob at 0");
    to_tick(384 + 96);
    run_ms(20);
    check(morph_amt >= 250 && !gl.l[0].run, "GLOBAL LFO ONE: morph to the end, then still");
    kit_set_morph(100);
    run_ms(20);
    check(morph_amt == 255 && vp_get(PAR_MORPH) == 100, "GLOBAL LFO: from where the morph knob is");
    gls[0].depth = -127;
    run_ms(20);
    check(morph_amt == 0, "GLOBAL LFO: a negative depth pulls morph down");
    seq_stop_now();
    kit_set_morph(0);
    /* the speed: FREE, a bar at 120 BPM is 2 s; LFO 2 (no destination) runs the same */
    gls[0].dest = GD_OFF;
    gls[0].mode = GM_FREE;
    run_ms(10);
    ph = gl.l[0].phase;
    run_ms(1000);
    ph = gl.l[0].phase - ph;
    check(ph > 0x7A000000u && ph < 0x86000000u, "GLOBAL LFO: a 1-bar cycle in 2 s at 120 BPM");
    gls[0].len = 18u;                                /* (256 bars: about 8.5 minutes) */
    run_ms(10);
    ph = gl.l[0].phase;
    run_ms(1000);
    ph = gl.l[0].phase - ph;
    check(ph > 7800000u && ph < 8900000u, "GLOBAL LFO: 256 bars, 1/512 of a cycle a second");
    /* FILTER: the DJ filter both ways from its knob */
    gls[0].dest = GD_FILTER;
    gls[0].depth = 127;
    gls[0].len = 0;                                  /* (1/16) */
    lo = hi = 0;
    for (i = 0; i < 200u; i++) {
        run_ms(1);
        lo = pf.dj_d < lo ? pf.dj_d : lo;
        hi = pf.dj_d > hi ? pf.dj_d : hi;
    }
    check(lo < -50000 && hi > 50000 && pf.dj == 64, "GLOBAL LFO FILTER: the DJ filter both ways, its knob unmoved");
    /* the effects: COMB and CRUSH by the LFO, at most as the key; FB PUSH */
    gls[0].dest = GD_COMB;
    gls[1].dest = GD_PUSH;
    gls[1].depth = -127;
    gls[1].wave = LFO_SAW_UP;
    gls[1].len = 0;
    ok = 0;
    for (i = 0; i < 200u; i++) {
        run_ms(1);
        ok |= (gl.amt[GD_COMB] > 0 && gl.amt[GD_COMB] < 65536) << 0 | (fx.push > 0 && fx.push < 65536) << 1;
    }
    check(ok == 3u && !(pf.on & (1u << PF_COMB)), "GLOBAL LFO: COMB and FB PUSH come in by the LFO, no key");
    /* HOLD, triggered each BAR: still within a bar */
    gls[1].dest = GD_OFF;
    gls[0].dest = GD_CRUSH;
    gls[0].wave = LFO_SAW_UP;
    gls[0].mode = GM_HOLD;
    gls[0].trig = GT_BAR;
    start_now();
    to_tick(40);
    lo = gl.out[0];
    to_tick(300);
    hi = gl.out[0];
    to_tick(384 + 40);
    check(lo == hi && gl.out[0] != lo, "GLOBAL LFO HOLD: its value held through a bar, a new one at the next");
    seq_stop_now();
    /* the pages, OCT+ to trigger, saved with the project */
    perf_goto(PG_GLFO);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[0] == GLV(0, 0) && pp[1] == GLV(0, 1) && pp[2] == GLV(0, 2) && pp[3] == GLV(0, 7) &&
          !strcmp(b, "GLOBAL LFO 1 (1/2)") && !perf_bank2(),
          "PERF: GLOBAL LFO 1's page (1.0: DEST DEPTH SYNC RATE), the first bank's keys");
    turn(EN_K1, -20);
    check(gls[0].dest == GD_OFF && dest_pop(), "GLOBAL LFO: DEST down to OFF, its name over the page (0.25.2)");
    turn(EN_K1, 1);
    check(gls[0].dest == GD_DENSITY, "GLOBAL LFO: DEST up from OFF: the SEQUENCER's first by name (0.25.2)");
    {
        uint32_t i, ok = 1;
        for (i = 1; i < NGD; i++)                    /* (by group in the signal's order, each by name) */
            ok &= GD_ORDER[i] != GD_OFF && (str_eq(GD_FULL[GD_ORDER[i]][0], GD_FULL[GD_ORDER[i - 1u]][0]) ?
                  tgt_before(0, GD_FULL[GD_ORDER[i - 1u]][1], 0, GD_FULL[GD_ORDER[i]][1]) : 1);
        for (i = 0; i < NGD; i++)
            ok &= gd_turn(GD_ORDER[i], 0) == GD_ORDER[i];
        check(ok && gd_turn(GD_ORDER[NGD - 1u], 1) == GD_ORDER[NGD - 1u], "GLOBAL LFO DEST: every one once, by name");
    }
    turn(EN_K1, 6);
    shot("global-lfo-dest");
    turn(EN_K2, 10);
    check(gls[0].dest == GD_MORPH && gls[0].depth == 127, "GLOBAL LFO: DEST and DEPTH by the knobs");
    turn(EN_K2, -10);
    check(gls[0].depth == 117, "GLOBAL LFO: DEPTH down");
    turn(EN_SELECT, 1);
    page_params(pp);
    check(pp[0] == GLV(0, 3) && pp[1] == GLV(0, 6) && pp[2] == GLV(0, 4) && pp[3] == GLV(0, 5),
          "1.0: GLOBAL LFO 1 (2/2): WAVE PHASE MODE TRIG, as every LFO");
    turn(EN_K3, -10);
    turn(EN_K3, 3);
    check(gls[0].mode == GM_ONE, "GLOBAL LFO 1: MODE ONE");
    gl.l[0].gone = 0xFFFFFFFFu;
    gl.l[0].run = 0;
    press(B_OCTUP);
    check(gl.l[0].run && !strcmp(ui.msg, "LFO 1 TRIGGERED") && !ui.copy, "OCT+ on its page: the LFO again");
    run_ms(DEST_POP_MS);
    check(!dest_pop(), "GLOBAL LFO: DEST's name gone after a while");
    shot("global-lfo");
    {
        int rc;
        gls[1].dest = GD_JITTER;
        gls[1].depth = -40;
        gls[1].len = 9;
        gls[1].phase = 33;
        gls[2].len = GL_FREE;                        /* (1.0: SYNC OFF, its RATE) */
        gls[2].rate = 99;
        rc = proj_save(1, "GLFO    ");
        memset(gls, 0, sizeof gls);
        rc |= proj_load(1);
        check(!rc && gls[0].dest == GD_MORPH && gls[0].mode == GM_ONE && gls[0].depth == 117 &&
              gls[1].dest == GD_JITTER && gls[1].depth == -40 && gls[1].len == 9 && gls[1].phase == 33 &&
              gls[2].len == GL_FREE && gls[2].rate == 99, "GLOBAL LFOs: saved with the project (1.0: SYNC OFF, RATE)");
    }
    memset(gls, 0, sizeof gls);
    run_ms(20);
    kit_set_morph(0);

    /* 14c: CONFIG BUTTONS, Last page: each button opens where it was left */
    goto_mode(B_HOME, M_HOME);
    press(B_FX);
    turn(EN_SELECT, 2);
    press(B_HOME);
    press(B_FX);
    check(ui.mode == M_FX && ui.page == 0, "Last page NO: FX opens on its first page");
    kset.pmem = 1;
    turn(EN_SELECT, 2);
    press(B_HOME);
    press(B_FX);
    check(ui.mode == M_FX && ui.page == 2, "Last page YES: FX opens where it was left");
    goto_mode(B_GLO, M_CONFIG);
    turn(EN_SELECT, (int32_t)CF_LOAD - 1 - (int32_t)ui.page);
    page_params(pp);
    check(ui.page == CF_LOAD - 1u && pp[0] == VP_PMEM, "CONFIG: BUTTONS");
    shot("config-buttons");
    press(B_FX);
    press(B_GLO);
    check(ui.mode == M_CONFIG && ui.page == CF_LOAD - 1u, "GLO: back to CONFIG's last page");
    press(B_GLO);
    check(ui.mode == M_MIXER, "GLO again: round to the mixer (29)");
    {   /* 0.15 (17a, 17b): CONFIG > LOAD, the last page: each part's load, the cores. The emulator measures none
         * and has one core; the figures here are set as the FM-1 would have them */
        static const uint16_t AVG[NLD] = {10, 105, 77, 8, 13, 15, 20}, PK[NLD] = {20, 133, 90, 10, 18, 28, 40};
        uint32_t i;
        goto_mode(B_GLO, M_CONFIG);
        turn(EN_SELECT, (int32_t)CF_LOAD - (int32_t)ui.page);
        page_params(pp);
        check(ui.page == NCONFIG - 2u && ui.page == CF_LOAD && pp[0] == VP_NONE && pp[3] == VP_NONE,
              "CONFIG: LOAD, before the last page, no knobs");
        check(cores_why == CW_BUILD && c1_stack() == 0, "the emulator: one core (no second core built in)");
        run_ms(100);
        shot("config-load-emu");
        for (i = 0; i < NLD; i++) {
            ld_q8[i] = AVG[i];
            ld_pk[i] = PK[i];
        }
        cores_why = CW_TWO;
        run_ms(100);
        shot("config-load");
        turn(EN_SELECT, 1);                          /* 29: the last page, ELIXIR: the version, each core's load */
        page_params(pp);
        check(ui.page == CF_ABOUT && ui.page == NCONFIG - 1u && pp[0] == VP_NONE, "CONFIG: ELIXIR, the last page");
        run_ms(100);
        shot("config-about");
        turn(EN_SELECT, -1);
        cores_why = CW_BAD;
        run_ms(100);
        shot("config-load-bad");
        press(B_FX);                             /* leaving CONFIG and coming back: the peaks start again */
        goto_mode(B_GLO, M_CONFIG);
        for (i = 0; i < NLD; i++)
            check(ld_pk[i] == 0, "CONFIG opened: the parts' peaks cleared");
        for (i = 0; i < NLD; i++)
            ld_q8[i] = 0;
        cores_why = CW_BUILD;
    }
    goto_mode(B_LFO, M_SOUND);
    for (i = 0; i < NSPG && ui.sect != S_LFO; i++)
        press(B_LFO);
    turn(EN_SELECT, 1);
    press(B_FX);
    press(B_LFO);
    check(ui.mode == M_SOUND && ui.sect == S_LFO && ui.half == 1, "LFO: back to LFO 1's second page (its MODE)");
    press(B_LFO);
    check(ui.sect == S_LFO2, "LFO again: on as before");
    seq_goto(PG_SCALE);
    press(B_HOME);
    press(B_SEQ);
    check(ui.mode == M_EUCLID && ui.page == PG_SCALE, "SEQ: back to the SCALE page");
    press(B_SEQ);
    check(ui.mode == M_STEP, "SEQ again: the steps");
    press(B_HOME);
    press(B_SEQ);
    check(ui.mode == M_STEP, "SEQ: back to the steps, as SEQ showed last");
    perf_goto(PG_MACRO);
    press(B_ARP);
    check(ui.mode == M_PERF && ui.page < PG_SPELL, "ARP in GLOBAL MOD: PERFORM");
    press(B_ARP);
    check(ui.mode == M_PERF && perf_spells(), "ARP again: SPELLS (32)");
    press(B_ARP);
    check(ui.mode == M_PERF && perf_wander(), "ARP again: WANDER (40)");
    press(B_ARP);
    check(ui.mode == M_PERF && ui.page == PG_MACRO, "ARP again: GLOBAL MOD, on MACROS as it was left");
    kset.pmem = 0;
    press(B_HOME);
    press(B_FX);
    check(ui.page == 0, "Last page NO again: the first page");
    goto_mode(B_HOME, M_HOME);
}

/* ------------------------------------------------------- Phase 9 (0.8) --- */
/* 39: RUSH (after Elixir Desktop's): the kit goes over to the rush track as the knob rises, then a roll on the grid,
 * then a free roll up to TOP; REC writes it into the steps as RUSH locks; a step held takes a lock; the STEP page's
 * RUSH target; the project keeps the settings */
static uint32_t rush_bar(uint32_t t, uint32_t *ch, uint32_t *d1)   /* a bar's hits: track t's (CH's, D1's) */
{
    uint32_t a = trig_count[t], c = trig_count[5], d = trig_count[0];
    run_ms(2000);
    *ch = trig_count[5] - c;
    *d1 = trig_count[0] - d;
    return trig_count[t] - a;
}
static void rturn(int32_t n)                         /* (0.23-t7: RUSH turned slowly, 1% a detent: n x 2) */
{
    int32_t i;
    for (i = 0; i < 2 * (n < 0 ? -n : n); i++) {
        web_enc(EN_K1, n < 0 ? -1 : 1);
        run_ms(60);
    }
}
static void phase39_rush(void)
{
    pattern_t *p = &pat[0];
    uint32_t n, ch, d1, k, locked = 0;
    p4_reset();
    kset.accel = 0;
    sq.muted = sq.solo = 0;
    for (k = 0; k < 16u; k++) {                      /* D1 on the beats, SN on 2 and 4, CH every 16th */
        main_set(p, 5, k, 1);
        p->step[5][k].volume = 90;
        if (!(k & 3u)) {
            main_set(p, 0, k, 1);
            p->step[0][k].volume = 100;
        }
    }
    main_set(p, 3, 4, 1);
    main_set(p, 3, 12, 1);
    p->step[3][4].volume = p->step[3][12].volume = 100;
    perf_goto(PG_RUSH);
    check(ui.mode == M_PERF && perf_rush(), "RUSH: its PERF page");
    black(3);
    check(ui.track == 3u && sq.ru_t == 3u, "RUSH: a black key picks its track");
    press(B_PLAY);
    run_ms(100);
    n = rush_bar(3, &ch, &d1);
    printf("  RUSH OFF: SN %u, CH %u, D1 %u in a bar\n", n, ch, d1);
    check(n == 2u && ch == 16u && d1 == 4u, "RUSH: off, the pattern as it is");
    rturn(25);
    check(sq.ru_live == 50u, "RUSH: 1% a slow detent");
    run_ms(50);
    n = rush_bar(3, &ch, &d1);
    printf("  RUSH 50%%: SN %u, CH %u, D1 %u\n", n, ch, d1);
    check(n > 2u && ch < 16u && ch > 4u && d1 == 4u, "RUSH 50%: some hits go over to the snare, the kick stays");
    rturn(14);
    run_ms(50);
    n = rush_bar(3, &ch, &d1);
    printf("  RUSH 78%% (32nds on the grid): SN %u, CH %u, D1 %u\n", n, ch, d1);
    check(n >= 28u && n <= 34u && ch <= 3u && d1 == 4u, "RUSH 78%: a 32nd roll on the grid, the hats gone");
    rturn(11);
    run_ms(300);
    n = rush_bar(3, &ch, &d1);
    printf("  RUSH 100%%: SN %u (1/256 = 256), CH %u, D1 %u\n", n, ch, d1);
    check(n >= 200u && ch == 0u && d1 == 0u, "RUSH 100%: the free roll at the top, only the roll");
    check(sq.ru_hp > 20000, "RUSH 100%: the filter open");
    rturn(-50);
    run_ms(500);
    check(sq.ru_live == 0u && sq.ru_hp == 0 && !sq.ru_R, "RUSH: off again, the filter shut");
    shot("rush");
    /* recording: the knob into every step it passes */
    rturn(40);
    sq.rec = 1;
    run_ms(2000);
    sq.rec = 0;
    rturn(-40);
    for (k = 0; k < 16u; k++)
        locked += rush_lock(p, 3, k) == 80u;
    printf("  RUSH recorded: %u of 16 steps locked\n", locked);
    check(locked >= 15u, "RUSH: REC writes its locks into the steps");
    run_ms(50);
    n = rush_bar(3, &ch, &d1);
    printf("  RUSH from the locks (80%%): SN %u, CH %u\n", n, ch);
    check(n >= 28u && ch <= 3u, "RUSH: the locks rush the bar, the knob down");
    /* a step held on the page: its lock */
    seq_clear_pattern(p);
    p2_key(3, 1);
    rturn(30);
    p2_key(3, 0);
    check(rush_lock(p, 3, 3) == 60u && main_on(p, 3, 3) && sq.ru_live == 0u, "RUSH: a step held takes the lock");
    p2_key(3, 1);
    rturn(-30);
    p2_key(3, 0);
    check(lock_get(p, 3, 3, 0) != RUSH_CODE && lock_get(p, 3, 3, 1) != RUSH_CODE, "RUSH: .. turned to 0, gone");
    press(B_PLAY);
    /* the STEP page: RUSH, the last lock target */
    ui.mode = M_STEP;
    ui.track = 2;
    ui.step = 5;
    vp_set(PAR_P1_DEST, LOCK_RUSH_IDX);
    vp_set(PAR_P1_VAL, 90);
    main_set(p, 2, 5, 1);
    check(lock_get(p, 2, 5, 0) == RUSH_CODE && rush_lock(p, 2, 5) == 90u && vp_get(PAR_P1_DEST) == LOCK_RUSH_IDX,
          "STEP: RUSH as a lock target");
    {
        char b[24] = {0};
        card_value(b, sizeof b, PAR_P1_VAL, 90);
        check(!strcmp(b, "90%"), "STEP: a RUSH lock reads as its amount");
    }
    goto_mode(B_HOME, M_HOME);
    /* the project: the settings and the locks */
    sq.ru_start = 60;
    sq.ru_free_at = 70;
    sq.ru_top = RT_64;
    sq.ru_flam = 33;
    sq.ru_pitch = -5;
    sq.ru_filt = 0;
    sq.ru_glide = 20;
    {
        int rc = proj_save(1, "RUSH    ");
        rush_defaults();
        seq_clear_pattern(p);
        rc |= proj_load(1);
        check(rc == 0 && sq.ru_start == 60u && sq.ru_free_at == 70u && sq.ru_top == RT_64 && sq.ru_flam == 33u &&
              sq.ru_pitch == -5 && sq.ru_filt == 0u && sq.ru_glide == 20u && rush_lock(&pat[0], 2, 5) == 90u,
              "project: RUSH's settings and locks");
    }
    rush_defaults();
    kset.accel = 1;
}
/* 9b, 9g: PERF's pages (the performance ones, then the macros, then PERFORM 2), MIX, the global sample rate */
static void phase9_perf(void)
{
    static const char *const T[NPERF] = {"PERFORM", "PERFORM: MORE", "PERFORM: DENSITY", "PERFORM: STUTTER",
                                         "PERFORM: RUSH", "PERFORM: RUSH SETUP", "PERFORM: TRANSPOSE", "PERFORM 2", "PERFORM 2: SETUP (1/2)", "PERFORM 2: SETUP (2/2)",
                                         "SPELLS: GRIMOIRE", "SPELLS: CAST", "SPELLS: CHARACTER", "SPELLS: LIMITS",
                                         "SPELLS: MOTION", "WANDER: QUEST", "WANDER: PACE", "WANDER: HOME",
                                         "GLOBAL LFO 1 (1/2)", "GLOBAL LFO 1 (2/2)", "GLOBAL LFO 2 (1/2)",
                                         "GLOBAL LFO 2 (2/2)", "GLOBAL LFO 3 (1/2)", "GLOBAL LFO 3 (2/2)",
                                         "GLOBAL LFO 4 (1/2)", "GLOBAL LFO 4 (2/2)", "MACROS",
                                         "MACRO SETUP", "LINKS", "LINK SETUP"};
    pattern_t *p = &pat[0];
    uint16_t kp[4];
    uint32_t i, ok = 1, d0, d1;
    p4_reset();
    goto_mode(B_ARP, M_PERF);
    for (i = 0; i < NPERF; i++) {
        uint32_t pg, np;
        char b[40];
        title_text(b, sizeof b, &pg, &np);
        uint32_t f = i >= PG_GMOD ? PG_GMOD : i >= PG_WANDER ? PG_WANDER : i >= PG_SPELL ? PG_SPELL : 0u;
        ok &= !strcmp(b, T[i]) && pg == i - f &&
              np == (i >= PG_GMOD ? NPERF - PG_GMOD : i >= PG_WANDER ? PG_GMOD - PG_WANDER
                     : i >= PG_SPELL ? PG_WANDER - PG_SPELL : PG_SPELL) &&
              (i >= PG_PERF2 && i < PG_SPELL) == (uint32_t)perf_bank2();
        if (i + 1u == PG_GMOD || i + 1u == PG_WANDER || i + 1u == PG_SPELL) {   /* (29: SELECT stays in the set; ARP:
                                                      * the next) */
            turn(EN_SELECT, 1);
            ok &= ui.page == i;
            press(B_ARP);
        } else {
            turn(EN_SELECT, 1);
        }
    }
    check(ok, "PERF: its pages in order, in four sets (PERFORM, SPELLS, WANDER, GLOBAL MOD), PERFORM 2's bank in the first");
    press(B_ARP);
    turn(EN_SELECT, -(int32_t)NPERF);
    page_params(kp);
    check(kp[0] == PAR_BPM && kp[1] == PAR_ROLL && kp[2] == PAR_MORPH && kp[3] == PAR_VOICE_DECIMATION_ALL,
          "PERFORM: tempo, roll, morph, sample rate (the LXR's page)");
    shot("perf-page");
    /* the sample rate: KNOB 4 lowers it; a macro can target it; under REC it records a lock */
    kset.accel = 0;
    par_set(PAR_VOICE_DECIMATION_ALL, 127);
    run_ms(20);
    d0 = decim_rate[6];
    turn(EN_K4, -60);
    run_ms(20);
    d1 = decim_rate[6];
    check(par[PAR_VOICE_DECIMATION_ALL] == 67 && d1 < d0, "SRATE: KNOB 4 lowers the voices' rate");
    check(lock_idx_of(PAR_VOICE_DECIMATION_ALL) == 1u && lock_target_par(1) == PAR_VOICE_DECIMATION_ALL,
          "SRATE: a macro and lock target");
    main_set(p, 0, 0, 1);
    for (i = 1; i < 16u; i++)
        main_set(p, 0, i, 1);
    pat_backup = *p;
    black(0);
    press(B_REC);
    play_from_start();
    run_ms(300);
    turn(EN_K4, -10);
    run_ms(10);
    press(B_PLAY);
    press(B_REC);
    {
        uint32_t k, n = 0, d = par_to_autodest(PAR_VOICE_DECIMATION_ALL);
        for (k = 0; k < 16u; k++)
            n += lock_get(p, 0, k, 0) == d || lock_get(p, 0, k, 1) == d;
        check(n >= 1u, "SRATE: turned under REC, a lock on the playing step");
    }
    par_set(PAR_VOICE_DECIMATION_ALL, 127);
    par_set(PAR_VOL1, 100);
    kset.accel = 1;
    p4_reset();
}

/* 9c, 9d: KEYS (the keys play the track's voice chromatically, recorded under REC), the keys' velocity and accent */
static void phase9_keys(void)
{
    pattern_t *p = &pat[0];
    uint32_t c, k, n;
    p4_reset();
    black(1);                                        /* D2 */
    hold(B_OCTDN);
    press(B_EDIT);
    release(B_OCTDN);
    check(ui.mode == M_KEYS && ui.kplay, "SHIFT + EDIT: KEYS");
    kset.accel = 0;
    turn(EN_K2, 80 - (int32_t)key_vel);
    check(key_vel == 80 && kset.kvel == 80, "KEYS: the velocity, a setting");
    c = trig_count[1];
    white(4);                                        /* C4: the voice's own pitch */
    run_ms(10);
    check(trig_count[1] == c + 1u && vc[1].osc.base_note == 60u && vc[1].velo == 80 * 32768 / 127,
          "KEYS: white key 5 plays D2 at C4, the velocity set");
    black(0);                                        /* F#3 */
    run_ms(10);
    check(vc[1].osc.base_note == 54u, "KEYS: black key 1 is F#3, not a track");
    check(ui.track == 1u, "KEYS: the black keys don't select");
    turn(EN_K1, 1);
    white(0);
    run_ms(10);
    check(vc[1].osc.base_note == 65u, "KEYS: an octave up, F4");
    turn(EN_K1, -1);
    hold(B_OCTDN);
    white(4);
    release(B_OCTDN);
    run_ms(10);
    check(vc[1].velo == key_acc * 32768 / 127 && ui.mode == M_KEYS, "KEYS: SHIFT plays the accent, no SHIFT function");
    tap_key(BLACK_KEY[0]);                           /* (held for the screen) */
    keys |= 1u << 7 | 1u << 11;
    web_keys(keys);
    run_ms(40);
    shot("keys");
    keys = 0;
    web_keys(keys);
    run_ms(40);
    /* REC: the notes into the steps (note, velocity), as MIDI notes go */
    press(B_REC);
    play_from_start();
    for (k = 0; k < 4u; k++) {
        while (sq.idx[1] != (int8_t)(k * 4u) || sq.tph[1] > 4u)
            run_ms(1);
        white(k);
    }
    press(B_PLAY);
    press(B_REC);
    for (k = 0, n = 0; k < 16u; k++)
        n += main_on(p, 1, k);
    check(n == 4u && main_on(p, 1, 4) && p->step[1][4].note == 55u && (p->step[1][4].volume & 127u) == 80u &&
          p->step[1][12].note == 59u, "KEYS under REC: notes and velocity into D2's steps");
    /* the sound pages keep the keys; HOME lets go */
    press(B_ENV);
    check(ui.mode == M_SOUND && ui.kplay, "KEYS: the sound pages keep playing");
    c = trig_count[1];
    white(2);
    run_ms(10);
    check(trig_count[1] == c + 1u && !main_on(p, 1, 2), "KEYS on a sound page: a note, not a step");
    /* 31: KEYS stays on as the pages change (the header says so), but where the keys are the page's own; SHIFT +
     * EDIT off */
    press(B_FX);
    c = trig_count[1];
    white(2);
    run_ms(10);
    check(ui.mode == M_FX && keys_live() && trig_count[1] == c + 1u, "KEYS (31): on FX's pages, the keys still play");
    shot("keys-on-fx");
    press(B_HOME);
    check(ui.kplay && keys_live(), "KEYS (31): HOME keeps them");
    press(B_HOME);
    check(ui.mode == M_PATTERN && !keys_live(), "KEYS (31): the patterns page has its own keys");
    goto_mode(B_HOME, M_HOME);
    hold(B_OCTDN);
    press(B_EDIT);
    release(B_OCTDN);
    check(!ui.kplay && ui.mode == M_HOME, "KEYS (31): SHIFT + EDIT on another page turns them off, the page stays");
    /* the rolls at the keys' velocity */
    goto_mode(B_ARP, M_PERF);
    white(0);
    run_ms(10);
    check(vc[0].velo == 80 * 32768 / 127, "a roll (stopped: one hit) at the keys' velocity");
    hold(B_OCTDN);
    white(1);                                        /* (SHIFT + 1..7 auditions the voice: not recorded) */
    release(B_OCTDN);
    goto_mode(B_HOME, M_HOME);
    hold(B_OCTDN);
    c = trig_count[0];
    white(0);
    release(B_OCTDN);
    run_ms(10);
    check(trig_count[0] == c + 1u, "SHIFT + 1: the voice plays");
    {   /* 1.2: CONFIG Audition: a track key plays its voice as it selects the track (YES, the default) */
        uint32_t c2 = trig_count[2], c3 = trig_count[3];
        check(vp_get(VP_AUDITION) == 1u, "Audition: YES by default");
        black(2);
        run_ms(10);
        check(ui.track == 2 && trig_count[2] == c2 + 1u && trig_count[0] == c + 1u,
              "Audition YES: a track key selects the track and plays its voice");
        vp_set(VP_AUDITION, 0);
        check((kset.caout & KS_QUIET) && !(kset.caout & 0x7Fu), "Audition NO: the settings' spare bit");
        black(3);
        run_ms(10);
        check(ui.track == 3 && trig_count[3] == c3, "Audition NO: the key only selects");
        vp_set(VP_AUDITION, 1);
        check(!kset.caout, "Audition YES again");
        {   /* 1.3: under REC (playing) a track key records its hit, a pad; CLEAR held erases as the steps play */
            pattern_t *rp = &pat[sq.active], keep = *rp;
            uint32_t i, n0 = 0, n1 = 0;
            for (i = 0; i < 64u; i++)
                main_set(rp, 4, i, 0);
            press(B_PLAY);
            press(B_REC);
            run_ms(100);
            black(4);
            run_ms(10);
            for (i = 0; i < 64u; i++)
                n0 += main_on(rp, 4, i);
            check(sq.rec && sq.running && ui.track == 4 && n0 == 1u, "REC: a track key records its hit (CY)");
            hold(B_OCTDN);
            hold(B_OCTUP);
            check(sq.erase, "REC: SHIFT + OCT+ held: erasing");
            run_ms(4500);
            release(B_OCTUP);
            release(B_OCTDN);
            for (i = 0; i < 64u; i++)
                n1 += main_on(rp, 4, i);
            check(!sq.erase && n1 == 0u, "REC: erased as it played, stopped when let go");
            press(B_REC);
            press(B_PLAY);
            black(4);
            run_ms(10);
            for (i = n0 = 0; i < 64u; i++)
                n0 += main_on(rp, 4, i);
            check(!sq.rec && n0 == 0u, "no REC: a track key isn't recorded");
            *rp = keep;
        }
        {   /* 1.3: REC's Quantize OFF: a hit keeps how late or early it was, as its step's shift; not played twice */
            pattern_t *rp = &pat[sq.active], keep = *rp;
            uint32_t i, k, c, late, early, n;
            for (i = 0; i < 64u; i++) {
                main_set(rp, 4, i, 0);
                rp->timing[4][i] = 7u << 4;
            }
            rp->len[4] = 16;
            rp->groove = 0;
            rp->ghum = 0;
            check(P_RECOPT[1] == VP_RQUANT && vp_get(VP_RQUANT) == 1u, "REC options: KNOB 2 Quantize, ON at first");
            vp_set(VP_RQUANT, 0);
            press(B_PLAY);
            press(B_REC);
            for (k = 0; k < 4000u && !(sq.idx[4] == 2 && sq.tph[4] == 8u); k++)
                run_ms(1);
            c = trig_count[4];
            key_note_post(4, tb_note(4), 100, KF_REC);  /* (a third of step 3 late) */
            run_ms(4);
            late = rp->timing[4][2] >> 4;
            check(main_on(rp, 4, 2) && late >= 9u && late <= 11u, "Quantize OFF: a late hit, step 3 shifted late");
            for (k = 0; k < 4000u && !(sq.idx[4] == 6 && sq.tph[4] == 20u); k++)
                run_ms(1);
            key_note_post(4, tb_note(4), 100, KF_REC);  /* (a sixth of a step before step 8) */
            run_ms(4);
            early = rp->timing[4][7] >> 4;
            check(main_on(rp, 4, 7) && early >= 5u && early <= 6u, "Quantize OFF: an early hit, step 8 shifted early");
            for (k = 0; k < 4000u && sq.idx[4] != 10; k++)
                run_ms(1);
            check(trig_count[4] == c + 2u, "Quantize OFF: each hit sounded once, not again from its step");
            c = trig_count[4];
            for (k = 0; k < 4000u && sq.idx[4] != 9; k++)  /* (round once more) */
                run_ms(1);
            n = trig_count[4] - c;
            printf("  Quantize OFF: late shift %u, early %u, next pass %u hits\n", late, early, n);
            check(n == 2u, "Quantize OFF: the next pass plays both");
            vp_set(VP_RQUANT, 1);
            check(!sq.rfree, "Quantize ON again");
            press(B_REC);
            press(B_PLAY);
            *rp = keep;
        }
        {   /* 1.3: NUDGE: no generator, the GENERATOR page's KNOB 4 turns the track; OCT+ keeps it, leaving: back */
            pattern_t *rp = &pat[sq.active], keep = *rp;
            uint16_t q[4];
            uint32_t i;
            for (i = 0; i < 64u; i++)
                main_set(rp, 4, i, 0);
            rp->len[4] = 16;
            rp->gen[4][0] = GEN_OFF;
            main_set(rp, 4, 0, 1);
            main_set(rp, 4, 5, 1);
            rp->step[4][5].volume = 0x80u | 33u;
            rp->timing[4][5] = 9u << 4;
            black(4);
            seq_goto(PG_GEN);
            page_params(q);
            check(q[3] == VP_NUDGE && vp_get(VP_NUDGE) == 16u, "NUDGE: no generator, KNOB 4 Nudge at 0");
            turn(EN_K4, 2);
            check(ui.nudge == 2 && main_on(rp, 4, 2) && main_on(rp, 4, 7) && !main_on(rp, 4, 0) && !main_on(rp, 4, 5) &&
                  (rp->step[4][7].volume & 0x7Fu) == 33u && rp->timing[4][7] >> 4 == 9u,
                  "NUDGE +2: the steps 2 later, each with what it has");
            turn(EN_K4, -3);
            check(ui.nudge == -1 && main_on(rp, 4, 15) && main_on(rp, 4, 4), "NUDGE -1: round the track's end");
            press(B_OCTUP);
            check(!ui.nudge && main_on(rp, 4, 15) && main_on(rp, 4, 4) && !ui.copy, "NUDGE: OCT+ keeps it");
            turn(EN_K4, 3);
            check(ui.nudge == 3 && main_on(rp, 4, 2) && main_on(rp, 4, 7), "NUDGE +3");
            goto_mode(B_HOME, M_HOME);
            check(!ui.nudge && main_on(rp, 4, 15) && main_on(rp, 4, 4) && !main_on(rp, 4, 2),
                  "NUDGE: leaving without OCT+ puts it back");
            *rp = keep;
        }
        {
            check(P_STEP[0][3] == PAR_STEP_SHIFT, "STEP page: KNOB 4 Shift (1.3)");
        }
        goto_mode(B_GLO, M_CONFIG);
        turn(EN_SELECT, (int32_t)CF_LOAD - 1 - (int32_t)ui.page);
        {
            uint16_t q[4];
            page_params(q);
            check(q[2] == VP_AUDITION, "CONFIG: BUTTONS & KITS, KNOB 3 Audition");
        }
        black(0);
        goto_mode(B_HOME, M_HOME);
    }
    vp_set(VP_KVEL, 100);
    kset.accel = 1;
    p4_reset();
}

/* 9f, 9e: the kit library (kits and sounds in flash, a torn write, the space reclaimed), the sound browser */
static void phase9_library(void)
{
    uint32_t i, ok, c, sv;
    uint8_t a[KIT_LEN], b[KIT_LEN];
    uint16_t kp[4];
    p4_reset();
    check(kl.ok && kl.spare < KL_NSEC, "LIBRARY: its flash found, a sector kept erased");
    /* SAVE KIT > LIBRARY, LOAD KIT < LIBRARY */
    kit_init_par();
    par[PAR_OSC_WAVE_DRUM1] = 3;
    par[PAR_VOL2] = 77;
    snd_post_all();
    io_open(M_SAVE, IO_KIT);
    vp_set(VP_IO_SRC, 1);
    page_params(kp);
    check(kp[1] == VP_IO_SRC && kp[2] == VP_IO_SLOT && vp_max(VP_IO_SLOT) == KL_KITS - 1u,
          "SAVE KIT: To (KNOB 2) LIBRARY before the Slot, 128 kits");
    ui.io_slot = 99;
    press(B_OCTUP);
    check(ui.io_naming, "SAVE KIT > LIBRARY: the name first");
    memcpy(ui.io_name, "LIBKIT1 ", 8);
    press(B_OCTUP);
    shot("io-library");
    check(kl_exists(KLK_KIT, 99) && kl_count(KLK_KIT) == 1u, "SAVE KIT > LIBRARY: in the library");
    par[PAR_OSC_WAVE_DRUM1] = 0;
    par[PAR_VOL2] = 10;
    io_open(M_LOAD, IO_KIT);
    check(ui.io_src == 1u && ui.io_slot == 99u, "LOAD KIT: From LIBRARY stays, the kit last saved");
    press(B_OCTUP);
    check(par[PAR_OSC_WAVE_DRUM1] == 3 && par[PAR_VOL2] == 77, "LOAD KIT < LIBRARY: the edit buffer");
    io_open(M_LOAD, IO_MORPH);
    press(B_OCTUP);
    check(morph_par[kit_idx(PAR_VOL2)] == 77, "LOAD MORPH < LIBRARY: the morph target");
    vp_set(VP_IO_SRC, 0);
    goto_mode(B_HOME, M_HOME);
    /* the kit browser (PRESETS; 0.19: one list, FACTORY, LIBRARY, SLOTS; KNOB 1 jumps between them; the library's
     * empty slots left out) */
    par[PAR_VOL2] = 10;
    ui.kb_at = 0;
    turn(EN_PRESET, 1);
    check(ui.kb_open && ui.kb_sel == KB_FAC, "PRESETS: the kit browser opens on factory kit 1");
    {
        uint16_t kp[4];
        page_params(kp);
        check(kp[0] == VP_KB_SRC && vp_get(VP_KB_SRC) == 0u, "kit browser: KNOB 1 is From, at FACTORY");
    }
    shot("kits-factory");
    turn(EN_PRESET, 100);
    check(ui.kb_sel == 14u, "kit browser: on from the factory's 85 through the library's one kit into the slots");
    turn(EN_PRESET, -15);
    check(ui.kb_sel == NUM_KITS + 99u && vp_get(VP_KB_SRC) == 1u, "kit browser: back to library kit 100 (the only one)");
    shot("kits-library");
    turn(EN_K1, 1);
    check(ui.kb_sel == kit_loaded && vp_get(VP_KB_SRC) == 2u, "kit browser: KNOB 1 to SLOTS, on the loaded kit");
    turn(EN_K1, -2);
    check(ui.kb_sel == KB_FAC, "kit browser: KNOB 1 back to FACTORY");
    turn(EN_PRESET, 2);
    press(B_OCTUP);
    {
        uint8_t f[KIT_LEN];
        kit_factory_values(2, f);
        for (i = 0, ok = 1; i < KIT_LEN; i++)
            ok &= par[kit_par(i)] == f[i];
        check(!ui.kb_open && ok, "kit browser: OCT+ loads factory kit 3 into the edit buffer");
    }
    turn(EN_PRESET, 1);
    check(ui.kb_open && ui.kb_sel == KB_FAC + 2u, "kit browser: it opens where it was");
    turn(EN_K1, 1);
    check(ui.kb_sel == NUM_KITS + 99u, "kit browser: KNOB 1 to LIBRARY, on the kit last loaded");
    press(B_OCTUP);
    check(!ui.kb_open && par[PAR_VOL2] == 77 && ui.lib_kit == 99u, "kit browser: OCT+ loads the library kit");
    vp_set(VP_KBSLOTS, 1);
    turn(EN_PRESET, 1);
    check(ui.kb_open && ui.kb_sel == kit_loaded && vp_get(VP_KB_SRC) == 0u, "CONFIG Kits first SLOTS: it opens on the slots");
    turn(EN_PRESET, -1);
    check(ui.kb_sel == (kit_loaded ? kit_loaded - 1u : 0u), "kit browser: the slots first");
    turn(EN_K1, 2);
    check(ui.kb_sel == KB_FAC, "kit browser: KNOB 1 to FACTORY, last of the three");
    vp_set(VP_KBSLOTS, 0);
    press(B_OCTDN);
    check(!ui.kb_open, "kit browser: OCT- closes it");
    turn(EN_PRESET, 1);
    run_ms(6100);
    check(!ui.kb_open, "kit browser: closes by itself");
    /* a write torn before its record: the slot as it was */
    libkit_get(99, a);
    par[PAR_VOL2] = 12;
    check(!libkit_save(99, "TORN    ", 0), "LIBRARY: a second version");
    memset(nor + kl_off((uint32_t)kl.at[0][99]), 0xFF, KL_HDR);   /* (its record never written) */
    kl_scan();
    libkit_get(99, b);
    check(!memcmp(a, b, KIT_LEN) && b[kit_idx(PAR_VOL2)] == 77, "LIBRARY: a torn write leaves the slot as it was");
    /* the space reclaimed: many more writes than the flash holds, every slot still there */
    for (i = 0; i < 40u; i++) {
        par[PAR_VOL2] = (uint8_t)i;
        libkit_save(i, "MANY    ", 0);
    }
    for (i = 0, ok = 1; i < 600u && ok; i++) {
        par[PAR_VOL3] = (uint8_t)(i & 127u);
        ok = !libkit_save(i % 40u, "MANY    ", 0) && !libsound_save(i % 100u, "SND     ", 0);
    }
    check(ok, "LIBRARY: 1200 writes into 288 pages (the full sectors reclaimed)");
    kl_scan();
    for (i = 0, ok = 1; i < 40u; i++)
        ok &= !libkit_get(i, a) && a[kit_idx(PAR_VOL3)] == ((560u + i) & 127u);
    check(ok && kl_count(KLK_KIT) == 41u && kl_count(KLK_SOUND) == 100u && libkit_get(99, a) == 0,
          "LIBRARY: after them, at power-on, every slot's newest");
    check(!kl_write(KLK_SOUND, 5, 0, 0, 0) && !kl_exists(KLK_SOUND, 5) && kl_count(KLK_SOUND) == 99u,
          "LIBRARY: a slot deleted");
    kl_scan();
    check(!kl_exists(KLK_SOUND, 5) && kl_exists(KLK_SOUND, 6), "LIBRARY: .. still deleted at power-on");
    for (i = 0; i < 128u; i++) {
        kl_write(KLK_KIT, i, 0, 0, 0);
        kl_write(KLK_SOUND, i, 0, 0, 0);
    }
    check(kl_count(KLK_KIT) == 0u && kl_count(KLK_SOUND) == 0u, "LIBRARY: emptied");
    ui.kb_at = 0;
    turn(EN_PRESET, 1);
    turn(EN_K1, 1);
    check(ui.kb_open && ui.kb_sel == kit_loaded, "kit browser: the library empty, KNOB 1 passes it over to SLOTS");
    turn(EN_K1, -1);
    check(ui.kb_sel == KB_FAC, "kit browser: .. and back to FACTORY");
    press(B_OCTDN);
    /* SOUND: D1's into D2 (its velocity target, its LFO on itself, moved along); not into the snare */
    kit_init_par();
    par[PAR_OSC_WAVE_DRUM1] = 2;
    par[PAR_VEL_DEST_1] = MOD_TARGET_VOICE[0][0] + 5u;
    par[PAR_VOICE_LFO1] = 1;
    par[PAR_TARGET_LFO1] = MOD_TARGET_VOICE[0][0] + 7u;
    snd_post_all();
    track_select(0);
    io_open(M_SAVE, IO_SOUND);
    page_params(kp);
    check(kp[1] == VP_IO_TRACK && kp[2] == VP_IO_SLOT && vp_max(VP_IO_SLOT) == KL_SOUNDS - 1u, "SAVE SOUND: the track (1.0: before the slot), 128 sounds");
    ui.io_slot = 3;
    press(B_OCTUP);
    check(!memcmp(ui.io_name, "DRUM 1  ", 8), "SAVE SOUND: named for its voice");
    press(B_OCTUP);
    check(kl_exists(KLK_SOUND, 3), "SAVE SOUND: in the library");
    io_open(M_LOAD, IO_SOUND);
    vp_set(VP_IO_TRACK, 1);
    ui.io_slot = 3;
    press(B_OCTUP);
    check(par[PAR_OSC_WAVE_DRUM2] == 2 && par[PAR_VEL_DEST_2] == MOD_TARGET_VOICE[1][0] + 5u &&
          par[PAR_VOICE_LFO2] == 2 && par[PAR_TARGET_LFO2] == MOD_TARGET_VOICE[1][0] + 7u,
          "LOAD SOUND: D1's into D2, what pointed at D1 at D2");
    sv = par[PAR_OSC_WAVE_DRUM1 + 0];
    vp_set(VP_IO_TRACK, 3);
    press(B_OCTUP);
    check(!strcmp(ui.msg, "NOT THIS TRACK'S KIND"), "LOAD SOUND: a drum's not into the snare");
    (void)sv;
    goto_mode(B_HOME, M_HOME);
    /* the browser: PRESETS on a sound page lists the track's sounds, auditions each; OCT- puts it back */
    kit_init_par();
    kit_set1(2, kit_idx(PAR_OSC_WAVE_DRUM3), 4);   /* (slot 3's D3: something to find) */
    snd_post_all();
    black(1);
    press(B_ENV);
    check(ui.mode == M_SOUND && ui.track == 1u, "D2's sound pages");
    c = trig_count[1];
    turn(EN_PRESET, 1);
    run_ms(20);
    check(ui.sb_open && sb_n >= 2u && trig_count[1] > c && (sb_list[0] >> 14) == SB_SOUND,
          "SOUND BROWSER: open, the library's sound first, auditioned");
    check(par[PAR_OSC_WAVE_DRUM2] == 2, "SOUND BROWSER: the sound in D2 as it plays");
    for (i = 0; i < sb_n && sb_code(i, 1) != (SB_SLOT << 14 | 2u << 11 | 2u); i++)
        ;
    check(i < sb_n, "SOUND BROWSER: a slot kit's D3 for D2");
    {
        char nm[8], tag[16];
        sb_label(sb_nl, 1, nm, tag, sizeof tag);
        check(sb_n > sb_nl + 200u && !memcmp(nm, "Badonka", 7) && !strcmp(tag, "FAC 1 D1"),
              "SOUND BROWSER: the factory kits' drums after the rest (0.19)");
    }
    turn(EN_PRESET, (int32_t)i);
    shot("sound-browser");
    check(par[PAR_OSC_WAVE_DRUM2] == 4, "SOUND BROWSER: moving on loads the next");
    press(B_OCTDN);
    check(!ui.sb_open && par[PAR_OSC_WAVE_DRUM2] == INIT_KIT[8u + PAR_OSC_WAVE_DRUM2],
          "SOUND BROWSER: OCT- puts the sound back");
    turn(EN_PRESET, 1);
    turn(EN_PRESET, 1);
    press(B_OCTUP);
    check(!ui.sb_open && !strcmp(ui.msg, "SOUND LOADED"), "SOUND BROWSER: OCT+ keeps it");
    kit_set1(2, kit_idx(PAR_OSC_WAVE_DRUM3), INIT_KIT[8u + PAR_OSC_WAVE_DRUM3]);
    kl_write(KLK_SOUND, 3, 0, 0, 0);
    goto_mode(B_HOME, M_HOME);
    p4_reset();
}

static void phase9_patkeep(void)                     /* 0.9: a pattern's edits kept when another plays */
{
    p4_reset();
    check(pat_keep == 1 && kset.pdrop == 0, "Edits: KEEP by default");
    black(0);                                        /* D1 */
    white(0);                                        /* D1 on step 1 of pattern 1 */
    check(main_on(&pat[0], 0, 0), "a step on pattern 1");
    goto_mode(B_HOME, M_PATTERN);
    shot("patterns-keep");
    white(1);
    check(sq.active == 1, "PATTERNS: pattern 2 plays");
    white(0);
    check(sq.active == 0 && main_on(&pat[0], 0, 0), "KEEP: pattern 1's step is still there");
    press(B_PLAY);                                   /* playing, the switch at the bar's end */
    white(1);
    run_ms(2100);
    check(sq.active == 1, "PATTERNS: pattern 2, playing");
    white(0);
    run_ms(2100);
    check(sq.active == 0 && main_on(&pat[0], 0, 0), "KEEP: pattern 1's step, playing");
    press(B_PLAY);
    /* RELOAD reverts, whatever Edits says */
    goto_mode(B_HOME, M_HOME);
    white(4);
    hold(B_OCTDN);
    white(12);
    release(B_OCTDN);
    check(!main_on(&pat[0], 0, 4) && main_on(&pat[0], 0, 0), "RELOAD: pattern 1 as entered");
    /* DROP: the LXR's way */
    goto_mode(B_HOME, M_PATTERN);
    turn(EN_K3, -1);
    check(pat_keep == 0 && kset.pdrop == 1, "Edits: DROP, a setting");
    shot("patterns-drop");
    goto_mode(B_HOME, M_HOME);
    white(8);
    goto_mode(B_HOME, M_PATTERN);
    white(1);
    white(0);
    check(!main_on(&pat[0], 0, 8) && main_on(&pat[0], 0, 0), "DROP: pattern 1's unsaved step goes");
    turn(EN_K3, 1);
    check(pat_keep == 1 && kset.pdrop == 0, "Edits: KEEP again");
    goto_mode(B_HOME, M_HOME);
}
/* 0.10: CLEAR SOUND (the track's voice back to the init kit's), SOLO (SHIFT + GLO) */
static void phase11_sound_solo(void)
{
    pattern_t *p = &pat[0];
    uint8_t k[KIT_LEN];
    uint32_t i, ok, c[NTRACK], n[NTRACK];
    p4_reset();
    sq.muted = sq.solo = 0;
    kit_init_par();
    snd_post_all();
    kit_init_values(k);
    /* a sound turned far from the init kit's, SN's too; CLEAR on a sound page offers SOUND, and resets D2 alone */
    black(1);
    press(B_EDIT);
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t q = kit_par(i);
        if (par_voice(q) == 1u || par_voice(q) == 3u)
            par[q] = (uint8_t)(k[i] ^ 5u);
    }
    hold(B_OCTDN);
    press(B_OCTUP);
    check(ui.clear_open && ui.clear_sel == CL_SOUND, "CLEAR on a sound page: CLEAR SOUND first");
    shot("clear-sound");
    press(B_OCTUP);
    release(B_OCTDN);
    for (i = 0, ok = 1; i < KIT_LEN; i++) {
        uint32_t q = kit_par(i);
        if (par_voice(q) == 1u)
            ok &= par[q] == k[i];
        if (par_voice(q) == 3u)
            ok &= par[q] == (uint8_t)(k[i] ^ 5u);
    }
    check(ok, "CLEAR SOUND: D2's parameters as the init kit's, SN's as they were");
    kit_init_par();
    snd_post_all();
    goto_mode(B_HOME, M_HOME);
    /* SOLO: SHIFT + GLO on the selected track; only it plays; another soloed too; off again */
    for (i = 0; i < NTRACK; i++)
        main_set(p, i, 0, 1);
    pat_backup = *p;
    black(3);
    hold(B_OCTDN);
    press(B_GLO);
    release(B_OCTDN);
    check(sq.solo == 1u << 3 && ui.mode == M_HOME, "SHIFT + GLO: SN soloed");
    run_ms(50);
    shot("solo");
    play_from_start();
    for (i = 0; i < NTRACK; i++)
        c[i] = trig_count[i];
    run_ms(2100);                                    /* (the next bar's first step) */
    for (i = 0; i < NTRACK; i++)
        n[i] = trig_count[i] - c[i];
    check(n[3] == 1 && !n[0] && !n[1] && !n[5], "SOLO: only the soloed track plays");
    black(0);
    hold(B_OCTDN);
    press(B_GLO);
    release(B_OCTDN);
    c[0] = trig_count[0];
    c[3] = trig_count[3];
    run_ms(2000);
    check(trig_count[0] - c[0] == 1 && trig_count[3] - c[3] == 1 && sq.muted == 0, "SOLO: two tracks, the mutes untouched");
    hold(B_OCTDN);
    press(B_GLO);
    release(B_OCTDN);
    black(3);
    hold(B_OCTDN);
    press(B_GLO);
    release(B_OCTDN);
    check(sq.solo == 0, "SOLO off again");
    c[1] = trig_count[1];
    run_ms(2000);
    check(trig_count[1] - c[1] == 1, "SOLO off: every track plays");
    press(B_PLAY);
}

/* ------------------------------------------------------ Phase 11 (0.10) --- */
/* track 0's next n steps as it plays them (sq.nst: each step taken, repeats too) */
static void dir_order(uint32_t n, uint8_t *out)
{
    uint32_t k = 0, seen = sq.nst[0];
    play_from_start();
    while (k < n) {
        run_ms(1);
        if (sq.nst[0] != seen) {
            seen = sq.nst[0];
            out[k++] = (uint8_t)sq.idx[0];
        }
    }
    press(B_PLAY);
}
static void phase11(void)
{
    pattern_t *p = &pat[0];
    uint32_t i, ok;
    uint8_t o[16];
    uint16_t pp[4];
    p4_reset();
    sq.muted = 0;
    kset.accel = 0;                                  /* (exact detents) */
    black(0);
    /* 29: SEQ the track's pages, then step edit, and round; SELECT pages in each */
    press(B_LFO);
    press(B_SEQ);
    check(ui.mode == M_EUCLID && ui.page == PG_TRACK, "SEQ: the track's pages, TRACK");
    press(B_SEQ);
    check(ui.mode == M_STEP && ui.page == 0, "SEQ again: step edit, STEP");
    for (i = 1; i < 5u; i++) {
        turn(EN_SELECT, 1);
        check(ui.mode == M_STEP && ui.page == (i < 4u ? i : 3u), "SELECT: the next step page (RATCHET the last)");
    }
    press(B_SEQ);
    check(ui.mode == M_EUCLID, "SEQ again: the track's pages");
    /* 11b, 11c: one GENERATOR page; EUCLID from 0 hits over the track's length, nothing written until the hits */
    for (i = 0; i < 16u; i += 2)
        main_set(p, 0, i, 1);
    p->len[0] = 12;
    goto_mode(B_SEQ, M_EUCLID);
    check(ui.page == PG_TRACK, "SEQ: the TRACK page first");
    turn(EN_SELECT, (int32_t)PG_GEN - (int32_t)ui.page);
    page_params(pp);
    check(pp[0] == VP_GMODE && pp[1] == VP_NONE, "GENERATOR OFF: only the type");
    turn(EN_K1, 1);
    page_params(pp);
    check(p->gen[0][0] == GEN_EUCLID && pp[1] == PAR_EUKLID_LENGTH && pp[2] == PAR_EUKLID_STEPS &&
          pp[3] == PAR_EUKLID_ROTATION, "GENERATOR: EUCLID, its length, hits, rotation");
    check(p->gen[0][1] == 12 && p->gen[0][2] == 0 && p->main[0][0] == 0x5555u,
          "EUCLID: 0 hits over the track's 12, the steps untouched");
    shot("generator-euclid");
    turn(EN_K2, 4);
    check(p->gen[0][1] == 16 && p->main[0][0] == 0x5555u && p->len[0] == 12, "EUCLID: the length alone writes nothing");
    turn(EN_K3, 4);
    check(p->main[0][0] == 0x1111u && p->len[0] == 16, "EUCLID: 4 hits written at once, on 1 5 9 13");
    turn(EN_K4, 1);
    check(p->main[0][0] == 0x2222u, "EUCLID: rotated");
    turn(EN_K3, -4);
    check(p->main[0][0] == 0, "EUCLID: back to 0 hits, none");
    turn(EN_K3, 3);
    check(view_pat(0) == p, "EUCLID: no live copy (the pattern plays)");
    /* a generator over Euclid's steps: MUTATE starts from them */
    {
        uint16_t e = p->main[0][0];
        turn(EN_K1, 1);
        run_ms(5);
        check(p->gen[0][0] == GEN_MUTATE && gpat.main[0][0] == e, "MUTATE after EUCLID: its live track the Euclid steps");
    }
    /* 11d, 11e: the ACTION page, the actions by type; the map only with GRIDS */
    turn(EN_SELECT, 1);
    page_params(pp);
    check(pp[0] == VP_ACTION && pp[1] == VP_NONE, "ACTION: no map without GRIDS");
    turn(EN_K1, 1);
    check(ui.action == ACT_COMMIT, "ACTION: MUTATE's first, COMMIT");
    shot("action-title");
    black(1);
    check(ui.action == ACT_NONE, "another track: no action picked");
    check(vp_max(VP_ACTION) == 1, "a track with no generator: UNDO alone");
    black(0);
    turn(EN_SELECT, -1);
    turn(EN_K1, -5);
    check(p->gen[0][0] == GEN_OFF, "GENERATOR: OFF");
    /* 11f: the directions, over 4 steps */
    goto_mode(B_HOME, M_HOME);
    p4_reset();
    p->len[0] = 4;
    {
        static const uint8_t WANT[5][10] = {{0, 1, 2, 3, 0, 1, 2, 3, 0, 1},   /* FWD */
                                            {3, 2, 1, 0, 3, 2, 1, 0, 3, 2},   /* REV */
                                            {0, 1, 2, 3, 2, 1, 0, 1, 2, 3},   /* PING */
                                            {0, 1, 2, 3, 3, 2, 1, 0, 0, 1}};  /* PING2 */
        for (i = 0; i < 4u; i++) {
            uint32_t k;
            p->dir[0] = (uint8_t)i;
            pat_backup = *p;
            dir_order(10, o);
            for (k = 0, ok = 1; k < 10u; k++)
                ok &= o[k] == WANT[i][k];
            printf("  direction %u:", (unsigned)i);
            for (k = 0; k < 10u; k++)
                printf(" %u", o[k]);
            printf("\n");
            check(ok, "a direction's order of steps");
        }
    }
    p->len[0] = 16;
    p->dir[0] = DIR_RAND;
    dir_order(16, o);
    {
        uint32_t k, mv = 0;
        for (k = 1, ok = 1; k < 16u; k++)
            mv += o[k] != o[k - 1] + 1u;
        check(mv > 4, "RAND: not in order");
    }
    p->dir[0] = DIR_WALK;
    dir_order(16, o);
    for (i = 1, ok = 1; i < 16u; i++)
        ok &= o[i] == (o[i - 1] + 1u) % 16u || o[i] == (o[i - 1] + 15u) % 16u;
    check(ok, "WALK: a step either way each time");
    /* a time round is the length in steps: REV, a step on 1, plays once a bar; 1:2 every other bar */
    p->dir[0] = DIR_PING;
    main_set(p, 0, 0, 1);
    pat_backup = *p;
    play_from_start();
    i = hits_in(0, 8000 - 50);
    press(B_PLAY);
    printf("  PING over 16, a step on 1: %u hits in 4 bars\n", (unsigned)i);
    check(i >= 2 && i <= 3, "PING: step 1 at each turn (every other bar)");
    /* a negative shift on the step coming next, going backwards: heard before it, as going forwards */
    {
        uint32_t c, at[2], k, d;
        for (d = 0; d < 2u; d++) {                   /* FWD: step 2 next; REV: step 3 next (4, 3 ..) */
            uint32_t s2 = d ? 2u : 1u;
            seq_clear_track(0, p);
            p->len[0] = 4;
            p->dir[0] = (uint8_t)(d ? DIR_REV : DIR_FWD);
            main_set(p, 0, s2, 1);
            p->timing[0][s2] = 3u << 4;              /* (shift -4/8 of a step) */
            pat_backup = *p;
            c = trig_count[0];
            at[d] = 0;
            play_from_start();
            for (k = 0; k < 600u && !at[d]; k++) {
                run_ms(1);
                if (trig_count[0] != c)
                    at[d] = sq.ticks;
            }
            press(B_PLAY);
        }
        printf("  a step shifted -1/2, next: heard on tick %u (FWD), %u (REV)\n", (unsigned)at[0], (unsigned)at[1]);
        check(at[0] < 20u && at[1] == at[0], "REV: a negative shift of the next step, early as FWD's");
    }
    seq_clear_track(0, p);
    /* the TRACK page's direction; project version 8 */
    seq_goto(PG_TRACK);
    page_params(pp);
    check(pp[0] == PAR_TRACK_LENGTH && pp[1] == VP_CLOCK && pp[2] == VP_DIR, "TRACK: length, clock, direction");
    turn(EN_K3, -10);
    turn(EN_K3, 2);
    check(p->dir[0] == DIR_PING, "TRACK: direction PING");
    shot("track-direction");
    p->dir[4] = DIR_WALK;
    p->gen[2][0] = GEN_EUCLID;
    p->gen[2][1] = 12;
    p->gen[2][2] = 5;
    {
        int rc = proj_save(1, "V8      ");
        p->dir[0] = p->dir[4] = 0;
        memset(p->gen[2], 0, 4);
        rc |= proj_load(1);
        check(rc == 0 && p->dir[0] == DIR_PING && p->dir[4] == DIR_WALK && p->gen[2][0] == GEN_EUCLID &&
              p->gen[2][2] == 5, "project (version 8): directions, Euclid's settings");
    }
    p4_reset();
    kset.accel = 1;
}

/* 0.11 (Phase 13): Control All: OCT+ held on a sound page, a knob moves its parameter on every voice */
static void phase13_control_all(void)
{
    static const uint16_t DEC[NTRACK] = {50, 52, 54, 56, 58, 60, 61};   /* AMP EG's decay, D1 .. OH */
    static const uint16_t FMP[3] = {82, 84, 86};                         /* FM's first knob, D1 .. D3 */
    pattern_t *p = &pat[0];
    uint8_t k0[KIT_LEN], acc = kset.accel;
    pattern_t before;
    uint32_t i, v, ok;
    p4_reset();
    kit_init_par();
    snd_post_all();
    kset.accel = 0;
    for (v = 0; v < NVOICE; v++)
        par[PAR_FILTER_FREQ_1 + v] = (uint8_t)(v == 4u ? 125u : 20u + 15u * v);
    main_set(p, 1, 0, 1);
    before = *p;
    black(0);
    press(B_SCL);
    check(ui.mode == M_SOUND && ui.sect == S_FILTER, "SCL: the filter page");
    /* the filter on every voice, by the knob's steps from each one's own value, clamped */
    hold(B_OCTUP);
    turn(EN_K2, 5);                            /* (1.0: FILTER's knobs Type, Freq, Reso, Drive) */
    for (v = 0, ok = 1; v < NVOICE; v++)
        ok &= par[PAR_FILTER_FREQ_1 + v] == (v == 4u ? 127u : 25u + 15u * v);
    check(ok, "CONTROL ALL: every voice's filter +5 from its own value, the cymbal's clamped at 127");
    check(ui.ca, "CONTROL ALL under way");
    shot("control-all");
    /* a voice key now leaves the track out (1.0 Phase 17; was: selects), no COPY; again: back in */
    black(2);
    check(ui.track == 0 && !ui.copy_src && (kset.caout & 4u) && par[PAR_FILTER_FREQ_1 + 2] == 50u,
          "CONTROL ALL: a voice key leaves the track out (back as it was), no COPY");
    black(2);
    check(!kset.caout && par[PAR_FILTER_FREQ_1 + 2] == 50u, "again: back in (where it is)");
    par[PAR_FILTER_FREQ_1 + 2] = 55;
    track_select(2);
    release(B_OCTUP);
    check(!ui.ca && ui.ca_ask && par[PAR_FILTER_FREQ_1] == 25u, "letting go of OCT+: the dialog, the changes still sounding");
    run_ms(50);
    shot("control-all-ask");
    turn(EN_K2, 3);
    black(4);
    check(par[PAR_FILTER_FREQ_1] == 25u && ui.track == 2 && ui.ca_ask, "the dialog: knobs and keys do nothing");
    press(B_OCTDN);
    check(!ui.ca_ask && !ui.shift && msg_on() && ui.msg[12] == 'K', "OCT- in the dialog: CONTROL ALL KEPT");
    check(par[PAR_FILTER_FREQ_1] == 25u && !memcmp(p, &before, sizeof before), "kept; nothing copied");
    /* paging during the hold, then OCT- puts back everything since the first turn */
    for (i = 0; i < KIT_LEN; i++)
        k0[i] = par[kit_par(i)];
    black(0);
    hold(B_OCTUP);
    turn(EN_K2, -10);
    press(B_ENV);
    check(ui.mode == M_SOUND && ui.sect == S_AEG, "ENV pages while OCT+ is held");
    turn(EN_K2, 3);
    for (i = 0, ok = 1; i < KIT_LEN; i++) {
        uint32_t q = kit_par(i), d = 0;
        for (v = 0; v < NTRACK; v++)
            d |= q == DEC[v];
        if (d)
            ok &= par[q] == (k0[i] + 3u > 127u ? 127u : k0[i] + 3u);
    }
    check(ok, "CONTROL ALL: decay on every voice, the hats' closed and open decay both");
    check(par[PAR_FILTER_FREQ_1] == 15u && par[PAR_FILTER_FREQ_1 + 1] == 30u, "the filter turn of the same hold");
    press(B_OCTDN);
    for (i = 0, ok = 1; i < KIT_LEN; i++)
        ok &= par[kit_par(i)] == k0[i];
    check(ok && !ui.ca && !ui.shift, "OCT- while OCT+ is held: the kit as before the first turn, not SHIFT");
    check(msg_on() && ui.msg[12] == 'R', "CONTROL ALL REVERTED");
    release(B_OCTUP);
    check(!ui.ca_ask, "letting go after the revert: no dialog");
    /* the dialog's default: OCT+ again reverts */
    v = par[DEC[0]];
    hold(B_OCTUP);
    turn(EN_K2, 6);
    release(B_OCTUP);
    check(ui.ca_ask && par[DEC[0]] == v + 6u, "a decay turn, let go: the dialog");
    press(B_OCTUP);
    for (i = 0, ok = 1; i < KIT_LEN; i++)
        ok &= par[kit_par(i)] == k0[i];
    check(ok && !ui.ca_ask && !ui.copy && ui.msg[12] == 'R', "OCT+ in the dialog: the kit reverted, no COPY held");
    /* a parameter only some voices have: FM's on D1..D3, the others left alone */
    for (i = 0; i < 4u && !(ui.mode == M_SOUND && ui.sect == S_FM); i++)
        press(B_EDIT);
    check(ui.sect == S_FM, "EDIT: MODEL, OSCILLATOR, then FM");
    for (i = 0; i < KIT_LEN; i++)
        k0[i] = par[kit_par(i)];
    hold(B_OCTUP);
    turn(EN_K1, 4);
    release(B_OCTUP);
    press(B_OCTDN);
    for (i = 0, ok = 1; i < KIT_LEN; i++) {
        uint32_t q = kit_par(i);
        ok &= q == FMP[0] || q == FMP[1] || q == FMP[2] ? par[q] == (k0[i] + 4u > 127u ? 127u : k0[i] + 4u)
                                                         : par[q] == k0[i];
    }
    check(ok, "CONTROL ALL on FM: D1..D3 moved, the voices without it untouched");
    /* a Elixir parameter (blocks of six): bits on every voice */
    press(B_SCL);
    press(B_SCL);
    check(ui.sect == S_SHAPE, "SCL again: SHAPE");
    hold(B_OCTUP);
    turn(EN_K4, 2);
    release(B_OCTUP);
    press(B_OCTDN);
    for (v = 0, ok = 1; v < NVOICE; v++)
        ok &= par[PAR_BITS1 + v] == 2u;
    check(ok, "CONTROL ALL on a Elixir parameter: every voice's bits");
    /* COPY still works when no knob is turned; a turn on a page that isn't a sound page edits as usual */
    hold(B_OCTUP);
    black(1);
    black(4);
    release(B_OCTUP);
    check(main_on(p, 4, 0) && !ui.ca && !ui.ca_ask, "COPY + D2, then CY: the track copied (no knob turned), no dialog");
    goto_mode(B_HOME, M_HOME);
    v = par[PAR_BPM];
    hold(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    check(!ui.ca && !ui.ca_ask && par[PAR_BPM] == v + 1u, "OCT+ held on the grid: the knob edits as usual");
    /* 0.12: with morph on, the morph target moves too (BOTH KITS): heard at full morph; reverted with the kit */
    for (i = 0; i < KIT_LEN; i++)
        morph_par[i] = par[kit_par(i)];
    for (v = 0; v < NVOICE; v++)
        morph_par[kit_idx(PAR_FILTER_FREQ_1 + v)] = (uint8_t)(90u + v);
    snd_service();
    kit_set_morph(255);
    {   /* 0.15: morph moved: a sweep of the kit, SND_MSTEP parameters a block (sound.c snd_morph_sweep), not all at once */
        uint32_t b = 0, ok = 1;
        do {
            snd_service();
            b++;
        } while (snd_mi && b < 20u);
        for (v = 0; v < NVOICE; v++)
            ok &= vc[v].flt.fval == q16_of(par_base(PAR_FILTER_FREQ_1 + v)) && par_base(PAR_FILTER_FREQ_1 + v) != par[PAR_FILTER_FREQ_1 + v];
        check(ok && b == (KIT_LEN + SND_MSTEP - 2u) / SND_MSTEP, "MORPH moved: the kit refreshed over 8 blocks, each filter at its mix");
    }
    run_ms(20);
    vp_set(VP_CAKIT, 0);
    black(0);
    press(B_SCL);
    v = par_base(PAR_FILTER_FREQ_1 + 2);
    hold(B_OCTUP);
    turn(EN_K2, 6);
    release(B_OCTUP);
    check(morph_par[kit_idx(PAR_FILTER_FREQ_1 + 2)] == 98u && par_base(PAR_FILTER_FREQ_1 + 2) == v + 6u,
          "full morph, BOTH KITS: the target moves too, D3's filter heard 6 higher");
    press(B_OCTUP);
    check(morph_par[kit_idx(PAR_FILTER_FREQ_1 + 2)] == 92u && par_base(PAR_FILTER_FREQ_1 + 2) == v,
          "OCT+ in the dialog: the morph target reverted too");
    kit_set_morph(128);
    run_ms(20);
    v = par_base(PAR_FILTER_FREQ_1 + 2);
    hold(B_OCTUP);
    turn(EN_K2, 4);
    release(B_OCTUP);
    press(B_OCTDN);
    check(par_base(PAR_FILTER_FREQ_1 + 2) == v + 4u, "half morph, BOTH KITS: heard 4 higher");
    kit_set_morph(255);
    vp_set(VP_CAKIT, 1);
    run_ms(20);
    v = par_base(PAR_FILTER_FREQ_1 + 2);
    hold(B_OCTUP);
    turn(EN_K2, 6);
    release(B_OCTUP);
    press(B_OCTDN);
    check(morph_par[kit_idx(PAR_FILTER_FREQ_1 + 2)] == 96u && par_base(PAR_FILTER_FREQ_1 + 2) == v,
          "full morph, KIT ONLY: the target left alone, nothing heard (the LXR's way)");
    vp_set(VP_CAKIT, 0);
    kit_set_morph(0);
    goto_mode(B_HOME, M_HOME);
    kset.accel = acc;
    kit_init_par();
    snd_post_all();
}

/* 0.12 (Phase 12; 0.16: two channels): the remote map, TRACKS on 15 and GLOBAL on 16, its knob modes, its buttons
 * (as CCs or notes), its feedback; NRPN morph and roll */
static void rcc(uint32_t cc, uint32_t v) { web_midi(0xB0 | remote_ch, cc, v); run_ms(10); }
static void gcc(uint32_t cc, uint32_t v) { web_midi(0xB0 | remote_gch, cc, v); run_ms(10); }
static void gnote(uint32_t n, int on) { web_midi((on ? 0x90 : 0x80) | remote_gch, n, on ? 100 : 0); run_ms(10); }
static void gbtn(uint32_t n) { gcc(n, 127); gcc(n, 0); }   /* a global button pressed and let go, as CCs */
static int fb_seen(uint32_t st, uint32_t d1, uint32_t d2)   /* the remote's feedback since the last call has it */
{
    uint32_t i, n = web_midi_out_take(), *o = web_midi_out(), hit = 0;
    for (i = 0; i < n; i++)
        hit |= ((o[i] >> 8) & 0xFFu) == st && ((o[i] >> 16) & 127u) == d1 && (o[i] >> 24) == d2;
    return hit;
}
static void phase12_remote(void)
{
    pattern_t *p = &pat[0];
    uint32_t ch, i, ok, bpm, t, j, seen[128] = {0};
    p4_reset();
    kit_init_par();
    snd_post_all();
    vp_set(VP_RCH, 15);
    vp_set(VP_RCH2, 16);
    check(remote_ch == 14u && remote_gch == 15u && kset.remote == 15u && kset.rglobal == 16u,
          "REMOTE: TRACKS on 15, GLOBAL on 16");
    vp_set(VP_RCH, 16);
    check(remote_ch == 14u, "REMOTE: TRACKS can't take GLOBAL's channel (turned past it)");
    vp_set(VP_RCH, 15);
    /* no number twice on a channel: TRACKS' knobs 8..103, buttons 104..119; GLOBAL's 8..47 and 3, 48..111 */
    for (i = 0; i < RM_NTK; i++)
        seen[RM_CC0 + i]++;
    for (i = 0; i < RM_NTB; i++)
        seen[RM_TB0 + i]++;
    for (i = ok = 0; i < 128u; i++)
        ok |= seen[i] > 1u || (seen[i] && (i < 8u || i >= 120u));
    memset(seen, 0, sizeof seen);
    for (i = 0; i < RM_NGK; i++)
        seen[RM_GCC(i)]++;
    for (i = 0; i < RM_NGB; i++)
        seen[RM_GB0 + i] += !rm_nobtn(RM_NTB + i);
    for (i = 0; i < 128u; i++)
        ok |= seen[i] > 1u || (seen[i] && ((i < 8u && i != 2u && i != 3u && i != 4u && i != 5u && i != 7u) || i >= 120u));
    check(!ok, "the remote map: each number once a channel, within 8..119 (and GLOBAL's CC 2..5, 7: never 0, 1, 6)");
    vp_set(VP_RKNOBS, RK_ABS);
    goto_mode(B_GLO, M_CONFIG);
    while (ui.page != CF_REMOTE)
        turn(EN_SELECT, 1);
    run_ms(50);
    shot("remote-config");
    goto_mode(B_HOME, M_HOME);
    /* ABS: a track's knobs and GLOBAL's, scaled to their ranges */
    rcc(24, 127);
    gcc(16, 127);
    gcc(8 + 2, 127);
    gcc(8 + 4, 0);
    check(par[PAR_FILTER_FREQ_1] == 127 && par[PAR_FX_DRY_WET] == vp_max(PAR_FX_DRY_WET) && morph_amt == 255 &&
          pf.dj == 0, "ABS: D1's filter, FX dry/wet, morph, DJ filter from the remote");
    pf.dj = 64;
    kit_set_morph(0);
    rcc(40 + 5, 11);
    rcc(40 + 6, 22);
    check(par[PAR_VELOD6_CLOSED] == 11 && par[PAR_VELOD6_OPEN] == 22, "CH's decay, OH's decay: each its own");
    track_select(2);
    rcc(56 + 7, 99);
    check(par[PAR_DSEND1 + 2] == 99u, "column 8: the selected track (D3's delay send, 1.0 Phase 16)");
    i = par[PAR_FILTER_FREQ_1];
    rcc(38, 5);                                      /* (the LXR's CC 38 is D1's filter: here OH's resonance) */
    check(par[PAR_FILTER_FREQ_1] == i && par[PAR_RESO_6] == 5, "the remote channel: the remote map, not the LXR's");
    gcc(8, 0);
    check(par[PAR_BPM] == 60, "the tempo knob: 60 BPM at 0, never MIDI sync");
    gcc(8, 60);
    check(par[PAR_BPM] == 120, "120 BPM at 60");
    gcc(32 + 4, 10);
    gcc(24 + 4, 0);
    check(pfs.drill == 10 && pfs.dens == -64, "GLOBAL: DRILL's setting, DENSITY");
    gcc(24 + 4, 64);
    /* PICKUP: nothing until the pot passes the value; again after the value moves elsewhere */
    vp_set(VP_RKNOBS, RK_PICKUP);
    par[PAR_VOL1] = 64;
    run_ms(30);
    rcc(8, 10);
    rcc(8, 30);
    check(par[PAR_VOL1] == 64, "PICKUP: below the value, nothing");
    rcc(8, 70);
    check(par[PAR_VOL1] == 70, "PICKUP: passed it, it follows");
    rcc(8, 80);
    check(par[PAR_VOL1] == 80, "PICKUP: and on");
    par_set(PAR_VOL1, 20);                           /* (the panel, say) */
    run_ms(60);
    rcc(8, 90);
    check(par[PAR_VOL1] == 20, "PICKUP: the value moved elsewhere: the pot waits again");
    rcc(8, 15);
    check(par[PAR_VOL1] == 15, "PICKUP: passed the new value");
    /* REL: 64 +/- n and two's complement */
    vp_set(VP_RKNOBS, RK_REL);
    par[PAR_RESO_1] = 50;
    rcc(32, 65);
    ok = par[PAR_RESO_1] == 51;
    rcc(32, 61);
    ok &= par[PAR_RESO_1] == 48;
    rcc(32, 2);
    ok &= par[PAR_RESO_1] == 50;
    rcc(32, 127);
    ok &= par[PAR_RESO_1] == 49;
    bpm = par[PAR_BPM];
    gcc(8, 66);
    check(ok && par[PAR_BPM] == bpm + 2u, "REL: 64 +/- n, two's complement, the tempo a BPM a step");
    vp_set(VP_RKNOBS, RK_PICKUP);
    /* buttons, as CCs (a Launch Control's custom template) and as notes: mute, solo on TRACKS */
    sq.muted = sq.solo = 0;
    rcc(104 + 1, 127);
    rcc(104 + 1, 0);
    web_midi(0x90 | remote_ch, 112 + 3, 127);
    run_ms(10);
    web_midi(0x80 | remote_ch, 112 + 3, 0);
    run_ms(10);
    i = par[PAR_VOL1 + 1];
    check(sq.muted == 2u && sq.solo == 8u && par[PAR_VOL1 + 1] == i, "TRACKS: D2 muted (a CC), SN soloed (a note)");
    track_select(4);
    rcc(104 + 7, 127);
    check(sq.muted == (2u | 16u), "TRACKS: column 8's mute, the selected track's (CY)");
    sq.muted = sq.solo = 0;
    /* 0.24.3: Mute/solo TOGGLE: the button's state is the mute's (a controller's toggle buttons) */
    vp_set(VP_RMUTE, 1);
    goto_mode(B_GLO, M_CONFIG);
    while (ui.page != CF_REMOTE)
        turn(EN_SELECT, 1);
    run_ms(50);
    shot("remote-toggle");
    goto_mode(B_HOME, M_HOME);
    check(vp_get(VP_RMUTE) == 1u && vp_get(VP_RKNOBS) == RK_PICKUP, "TOGGLE: kept apart from Knobs");
    rcc(104 + 1, 127);
    ok = sq.muted == 2u;
    rcc(104 + 1, 127);
    ok &= sq.muted == 2u;
    rcc(104 + 1, 0);
    ok &= sq.muted == 0u;
    rcc(104 + 1, 0);
    ok &= sq.muted == 0u;
    web_midi(0x90 | remote_ch, 112 + 3, 127);
    run_ms(10);
    ok &= sq.solo == 8u;
    web_midi(0x80 | remote_ch, 112 + 3, 0);
    run_ms(10);
    check(ok && sq.solo == 0u, "TOGGLE: on mutes, again stays muted, off unmutes; solo the same (notes too)");
    sq.muted = 2u;
    vp_set(VP_RKNOBS, RK_REL);
    check(vp_get(VP_RMUTE) == 1u && vp_get(VP_RKNOBS) == RK_REL, "Knobs changed: TOGGLE kept");
    vp_set(VP_RKNOBS, RK_PICKUP);
    vp_set(VP_RMUTE, 0);
    rcc(104 + 1, 127);
    rcc(104 + 1, 0);
    check(sq.muted == 0u && kset.rknobs == RK_PICKUP, "PRESS again: a press flips it");
    sq.muted = sq.solo = 0;
    /* GLOBAL: select, held FILL, a roll, DRILL, an effect held and latched, DENSITY's tracks, patterns */
    gbtn(64 + 4);
    check(ui.track == 4, "GLOBAL: CY selected");
    gcc(48 + 2, 127);
    ok = (pf.on >> PF_FILL) & 1u;
    gcc(48 + 2, 0);
    check(ok && !((pf.on >> PF_FILL) & 1u), "FILL while held");
    gnote(72, 1);
    ok = sq.roll & 1u;
    gnote(72, 0);
    gcc(80 + 2, 127);
    ok &= (sq.drill >> 2) & 1u;
    gcc(80 + 2, 0);
    check(ok && !(sq.roll & 1u) && !sq.drill, "a roll and a DRILL while held");
    /* 1.0: one row for both: a press held past RM_TAP_MS holds, a tap latches, a press again lets it go */
    gcc(88, 127);
    ok = (pf.on >> PF_HALF) & 1u;
    run_ms(400);
    gcc(88, 0);
    gbtn(88 + 7);
    ok &= !((pf.on >> PF_HALF) & 1u) && ((pf.on >> PF_CRUSH) & 1u);
    goto_mode(B_ARP, M_PERF);
    goto_mode(B_HOME, M_HOME);                       /* (leaving PERF lets the panel's latches go, not the remote's) */
    ok &= (pf.on >> PF_CRUSH) & 1u;
    gcc(88 + 7, 127);
    ok &= !((pf.on >> PF_CRUSH) & 1u);
    run_ms(400);
    gcc(88 + 7, 0);
    check(ok && !((pf.on >> PF_CRUSH) & 1u) && !ui.rm_latch && !ui.rm_held,
          "HALF held (a long press); CRUSH tapped: latched (past PERF), a press again: off, its release nothing");
    gnote(88 + 1, 1);
    gnote(88 + 1, 0);
    ok = (pf.on >> PF_JITTER) & 1u;
    gnote(88 + 1, 1);
    gnote(88 + 1, 0);
    check(ok && !((pf.on >> PF_JITTER) & 1u), "JITTER tapped as notes: latched, then off");
    vp_set(VP_RMUTE, 1);                             /* (TOGGLE: the button's state is the latch) */
    gcc(88 + 2, 127);
    run_ms(400);
    ok = (pf.on >> PF_CUTS) & 1u;
    gcc(88 + 2, 0);
    vp_set(VP_RMUTE, 0);
    check(ok && !((pf.on >> PF_CUTS) & 1u), "Mute/solo TOGGLE: an effect's button state is its latch");
    gcc(96 + 7, 127);
    check(!((pf.on >> PF_CRUSH) & 1u) && pf.on == 0u, "96..103: no longer the effects latched");
    /* 1.0: the knobs off the grid: RING on 3, 4, 5, 7, 63; the delay on 96 .. 103 */
    vp_set(VP_RKNOBS, RK_ABS);
    gcc(3, 127);
    gcc(4, 0);
    gcc(5, 127);
    gcc(7, 127);
    gcc(63, 0);
    check(pfs.dj_wet == 127u && pfs.dj_slope == 0u && pfs.dj_tone == 127u && pfs.dj_tune == NRTUNE - 1u &&
              pfs.dj_spread == 0u, "GLOBAL CC 3, 4, 5, 7, 63: RING's Wet, Slope, Tone, Tune, Spread");
    gcc(96, 0);
    gcc(97, 127);
    gcc(98, 0);
    gcc(99, 0);
    gcc(100, 127);
    gcc(101, 127);
    gcc(102, 0);
    gcc(103, 127);
    check(par[PAR_DLY_TIME] == vp_min(PAR_DLY_TIME) && par[PAR_DLY_FB] == vp_max(PAR_DLY_FB) &&
              par[PAR_DLY_TONE] == vp_min(PAR_DLY_TONE) && par[PAR_DLY_BAND] == 0u &&
              par[PAR_DLY_WOW] == vp_max(PAR_DLY_WOW) && par[PAR_GLITCH] == vp_max(PAR_GLITCH) &&
              par[PAR_DLY_ROUTE] == vp_min(PAR_DLY_ROUTE) && par[PAR_REV_FX] == vp_max(PAR_REV_FX),
          "GLOBAL CC 96..103: the delay's Time, Feedback, Tone, Band, Wow, Glitch, Route, Rev>FX");
    web_midi_out_take();
    pfs.dj_wet = 40;
    par_set(PAR_DLY_FB, 33);
    run_ms(200);
    ok = fb_seen(0xB0u | remote_gch, 3, rm_to_cc(VP_DJWET));
    check(ok && !fb_seen(0xB0u | remote_gch, 97, 0), "feedback: RING Wet on CC 3; 97 a knob's value, no button's");
    pfs.dj_wet = pfs.dj_slope = pfs.dj_tone = pfs.dj_spread = 64;
    pfs.dj_tune = RC_MAJ;
    kit_init_par();
    snd_post_all();
    vp_set(VP_RKNOBS, RK_PICKUP);
    pfs.dex = 0;
    gbtn(104);
    gbtn(104 + 3);
    ok = pfs.dex == 9u;
    gbtn(104 + 3);
    ok &= pfs.dex == 1u;
    gbtn(104 + 7);
    check(ok && !pfs.dex, "GLOBAL: DENSITY's tracks left out (D1, SN), back in, all back in");
    gbtn(56 + 5);
    check(sq.active == 1, "the next pattern");
    gbtn(56 + 4);
    check(sq.active == 0, "the previous pattern");
    /* REC: a track's knob records a lock on its own track */
    for (i = 0; i < 16u; i++)
        main_set(p, 2, i, 1);
    pat_backup = *p;
    gbtn(48);
    check(sq.running, "PLAY from the remote");
    sq.rec = 1;
    vp_set(VP_RKNOBS, RK_ABS);
    for (t = 0; t < 8u; t++)
        rcc(24 + 2, 100 - t);
    sq.rec = 0;
    for (i = ok = 0; i < 16u; i++)
        ok |= lock_get(p, 2, i, 0) == par_to_autodest(PAR_FILTER_FREQ_1 + 2) ||
              lock_get(p, 2, i, 1) == par_to_autodest(PAR_FILTER_FREQ_1 + 2);
    check(ok, "REC: D3's filter knob locks D3's steps");
    gbtn(48);
    check(!sq.running, "STOP from the remote");
    /* feedback: a panel change, a mute; nothing back for what the remote itself sent */
    run_ms(100);
    (void)fb_seen(0, 0, 0);
    par_set(PAR_FILTER_FREQ_1 + 1, 77);
    par_set(PAR_REV_SIZE, 127);
    run_ms(30);
    ch = 0xB0u | remote_ch;
    j = web_midi_out_take();
    {
        uint32_t *o = web_midi_out();
        for (i = ok = 0; i < j; i++)
            ok |= (o[i] >> 8 & 0xFFu) == ch && ((o[i] >> 16) & 127u) == 25u && (o[i] >> 24) == 77u ? 1u
                : (o[i] >> 8 & 0xFFu) == (0xB0u | remote_gch) && ((o[i] >> 16) & 127u) == 25u && (o[i] >> 24) == 127u ? 2u : 0u;
    }
    check(ok == 3u, "feedback: D2's filter (TRACKS CC 25), the reverb's size (GLOBAL CC 25)");
    rcc(16, 33);
    run_ms(30);
    check(!fb_seen(ch, 16, 33), "no echo of what the remote sent");
    kit_load_ram(3);
    run_ms(80);
    {
        uint32_t n = web_midi_out_take(), *o = web_midi_out(), cc = 0;
        for (i = 0; i < n; i++)
            cc += ((o[i] >> 8) & 0xF0u) == 0xB0u && (o[i] >> 8 & 15u) == remote_ch;
        printf("  a kit load: %u remote CCs back\n", cc);
        check(cc > 0, "feedback after a kit load");
    }
    sq.muted = 1u << 2;                              /* (D2's mute came as a CC: its light goes back as one) */
    sq.solo = 1u << 3;                               /* (SN's solo came as a note: as a note) */
    run_ms(40);
    j = web_midi_out_take();
    {
        uint32_t *o = web_midi_out();
        int m_cc = 0, s_note = 0, s_cc = 0;
        for (i = 0; i < j; i++) {
            uint32_t st = o[i] >> 8 & 0xFFu, d1 = (o[i] >> 16) & 127u, d2 = o[i] >> 24;
            m_cc |= st == (0xB0u | remote_ch) && d1 == 104u + 2u && d2 == 127u;
            s_note |= st == (0x90u | remote_ch) && d1 == 112u + 3u && d2 == 127u;
            s_cc |= st == (0xB0u | remote_ch) && d1 == 112u + 3u;
        }
        check(m_cc && s_note && !s_cc, "feedback: D3's mute light (a CC: never heard, both), SN's solo as a note");
    }
    sq.muted = sq.solo = 0;
    /* NRPN morph (120) and roll rate (119) do what the panel does (12a) */
    ch = (par[PAR_MIDI_CHAN_GLOBAL] - 1u) & 15u;
    web_midi(0xB0 | ch, 99, 0);
    web_midi(0xB0 | ch, 98, 120);
    web_midi(0xB0 | ch, 6, 127);
    web_midi(0xB0 | ch, 98, 119);
    web_midi(0xB0 | ch, 6, 5);
    run_ms(20);
    check(morph_amt == 255 && sq.roll_rate == 5, "NRPN morph and roll rate take effect");
    kit_set_morph(0);
    /* OFF: the channels are plain ones again */
    vp_set(VP_RCH, 0);
    vp_set(VP_RCH2, 0);
    par[PAR_RESO_1] = 9;
    web_midi(0xB0 | 14, 32, 100);
    run_ms(20);
    check(remote_ch == 0xFFu && remote_gch == 0xFFu && kset.remote == 17u && par[PAR_RESO_1] == 9,
          "REMOTE OFF: nothing from the map");
    vp_set(VP_RCH, 15);
    vp_set(VP_RCH2, 16);
    vp_set(VP_RKNOBS, RK_PICKUP);
    {   /* settings from 0.15 (16 bytes, one remote channel: 16): both channels at their defaults, the rest kept */
        elixir_settings_t k = kset, old = kset;
        old.remote = 16;
        old.leds = 0;
        st_save(OBJ_SETTINGS, &old, SET_LEN_015);
        kset.leds = 2;
        persist_boot();
        check(remote_ch == 14u && remote_gch == 15u && kset.leds == 0, "0.15's settings: TRACKS 15, GLOBAL 16");
        kset = k;
        st_save(OBJ_SETTINGS, &kset, sizeof kset);
        persist_boot();
    }
    kit_init_par();
    snd_post_all();
}

/* ------------------------------------------------------ Phase 19 (0.16) --- */
static uint32_t midi_offs(uint32_t ch, uint32_t *ons)  /* the MIDI out since the last call: note-offs (and ons) on ch */
{
    uint32_t n = web_midi_out_take(), i, off = 0, on = 0;
    for (i = 0; i < n; i++) {
        uint32_t st = (web_mo[i] >> 8) & 0xFFu, v = web_mo[i] >> 24;
        if ((st & 15u) != ch)
            continue;
        if ((st & 0xF0u) == 0x80u || ((st & 0xF0u) == 0x90u && !v))
            off++;
        else if ((st & 0xF0u) == 0x90u)
            on++;
    }
    if (ons)
        *ons = on;
    return off;
}
static void phase19(void)
{
    pattern_t *p = &pat[0];
    uint16_t pp[4];
    uint32_t i, c, ons, pg, np;
    char b[40];
    p4_reset();
    sq.muted = 0;
    kset.accel = 0;
    /* 19a: the scales */
    check(scale_q(SC_MAJOR, 0, 61, 0) == 60 && scale_q(SC_MAJOR, 0, 60, 1) == 62 && scale_q(SC_MAJOR, 0, 60, -1) == 59 &&
              scale_q(SC_MAJOR, 0, 64, 2) == 67 && scale_q(SC_MINOR, 9, 61, 0) == 60 && scale_q(SC_MINPENT, 9, 59, 0) == 60 && scale_q(SC_MINPENT, 9, 58, 0) == 57,
          "scale_q: the nearest degree (a tie the lower), degrees on");
    check(scale_q(SC_CHROM, 0, 61, 0) == 61 && scale_q(SC_WHOLE, 0, 61, 0) == 60 && scale_q(SC_FIFTHS, 2, 62, 1) == 69,
          "scale_q: CHROMATIC as it is, WHOLE TONE, FIFTHS");
    {   /* 1.0: the twelve scales after WHOLE T: each has its root, no name wider than the widest before them, and a
         * note lands on a degree (Locrian on C: C# stays, D to C#; In Sen: E to F) */
        int ok = 1;
        int32_t wmax = 0;
        uint32_t k;
        for (k = 0; k <= SC_WHOLE; k++)
            if (text_w(&AF_S, SCALE_NAME[k]) > wmax)
                wmax = text_w(&AF_S, SCALE_NAME[k]);
        for (k = SC_LOCRIAN; k < NSCALE; k++)
            ok &= (SCALE_MASK[k] & 1u) && text_w(&AF_S, SCALE_NAME[k]) <= wmax;
        check(ok && NSCALE == 29u && scale_q(SC_LOCRIAN, 0, 61, 0) == 61 && scale_q(SC_LOCRIAN, 0, 62, 0) == 61 &&
                  scale_q(SC_INSEN, 0, 64, 0) == 65 && scale_q(SC_EGYPT, 0, 63, 0) == 62,
              "scales: 28 and CHROM, the new ones rooted, named to fit, quantizing");
    }
    check(p->scale == SC_CHROM && p->sfollow == SFOLLOW_DEFAULT, "a new pattern: CHROMATIC, D1-D3 follow");
    seq_goto(PG_SCALE);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[0] == VP_SROOT && pp[1] == VP_SCALE && pp[2] == VP_SFOLLOW, "EUCLID: the SCALE page, ROOT SCALE FOLLOW");
    turn(EN_K1, 2);                                  /* D */
    turn(EN_K2, SC_DORIAN);
    check(p->sroot == 2 && p->scale == SC_DORIAN && scl_last.root == 2 && scl_last.scale == SC_DORIAN,
          "SCALE: D DORIAN, the project's last");
    shot("scale-page");
    turn(EN_K2, SC_HMINOR - SC_DORIAN);              /* (the widest names) */
    shot("scale-harm-min");
    turn(EN_K2, SC_PHRDOM - SC_HMINOR);
    shot("scale-phr-dom");
    turn(EN_K2, SC_DORIAN - SC_PHRDOM);
    {
        uint32_t cs = (uint32_t)(vc[0].osc.midi_freq >> 8), n = scale_fit(0, SEQ_DEFAULT_NOTE + 1u, 0);
        int32_t s = (int32_t)cs + (int32_t)n - SEQ_DEFAULT_NOTE;
        check(((SCALE_MASK[SC_DORIAN] >> (uint32_t)(((s - 2) % 12 + 12) % 12)) & 1u) && (n == SEQ_DEFAULT_NOTE ||
              n == SEQ_DEFAULT_NOTE + 1u || n == SEQ_DEFAULT_NOTE + 2u),
              "scale_fit: D1's note in D DORIAN from its tune");
        check(scale_fit(4, SEQ_DEFAULT_NOTE + 1u, 0) == SEQ_DEFAULT_NOTE + 1u, "scale_fit: D5 doesn't follow");
    }
    /* KEYS in the scale: white keys the degrees */
    black(0);
    hold(B_OCTDN);
    press(B_EDIT);
    release(B_OCTDN);
    check(ui.mode == M_KEYS, "KEYS");
    {
        int ok = 1;
        int32_t cs = (int32_t)(vc[0].osc.midi_freq >> 8), last = -1;
        for (i = 0; i < 8u; i++) {
            int32_t n = key_scale_note(i), s = cs + n - SEQ_DEFAULT_NOTE;
            ok &= (int)((SCALE_MASK[SC_DORIAN] >> (uint32_t)(((s - 2) % 12 + 12) % 12)) & 1u) && n > last;
            last = n;
        }
        check(ok, "KEYS: the white keys climb D DORIAN's degrees");
        white(1);
        run_ms(10);
        check(ui.knote == (uint8_t)key_scale_note(1), "KEYS: white key 2 plays the second degree");
    }
    shot("keys-scale");
    hold(B_OCTDN);                                   /* (31: KEYS stays on till SHIFT + EDIT) */
    press(B_EDIT);
    release(B_OCTDN);
    /* a blank pattern takes the scale last chosen */
    seq_clear_pattern(&pat[3]);
    check(pat[3].sroot == 2 && pat[3].scale == SC_DORIAN, "a cleared pattern: the scale last chosen");
    {
        int rc = proj_save(1, "SCALE   ");
        p->scale = SC_CHROM;
        p->sroot = 0;
        p->sfollow = 0x55;
        scl_last.root = scl_last.scale = 0;
        rc |= proj_load(1);
        p = &pat[0];
        check(!rc && p->sroot == 2 && p->scale == SC_DORIAN && p->sfollow == SFOLLOW_DEFAULT && scl_last.scale == SC_DORIAN,
              "project v10: the scale, FOLLOW, the last scale");
    }
    p4_reset();
    p = &pat[0];
    p->scale = SC_CHROM;
    scl_last.root = scl_last.scale = 0;

    /* 19b: a step's length; GATE holds the voice for it, MIDI's note-off at its end */
    check(PROJ_VERSION >= 10, "project version 10 or later");
    main_set(p, 0, 0, 1);
    p->cond[0][0] = COND_FILL;
    p->step[0][0].prob = 77;
    step_len_set(p, 0, 0, LEN_8);
    check(step_len(p, 0, 0) == LEN_8 && step_cond(p, 0, 0) == COND_FILL && step_prob(&p->step[0][0]) == 77,
          "a step's length: in cond's and prob's top bits");
    step_len_set(p, 0, 0, LEN_TIE);
    check(step_len(p, 0, 0) == LEN_TIE && step_cond(p, 0, 0) == COND_FILL && step_prob(&p->step[0][0]) == 77,
          "TIE beside the condition and chance");
    p->cond[0][0] = 0;
    p->step[0][0].prob = 100;
    step_len_set(p, 0, 0, LEN_4);
    par_set(PAR_GATE1, 1);
    par_set(PAR_MIDI_FILT_TX, par[PAR_MIDI_FILT_TX] | MF_NOTE);
    vp_set(VP_MOUT, 1);                              /* (1.0: D1's MIDI OUT on; all off by default) */
    run_ms(20);
    midi_offs(0, 0);
    play_from_start();
    run_ms(60);
    check(vc[0].aeg.hold && vc[0].aeg.state == EG_H, "GATE: the 1/4 holds the attack's top");
    check(midi_offs(midi_track_ch(0), 0) == 0u, "a 1/4: no MIDI note-off at the step's end");
    run_ms(500);                                     /* (a 1/4 at 120: 500 ms) */
    check(!vc[0].aeg.hold && vc[0].aeg.state != EG_H, "GATE: let go at the length's end");
    check(midi_offs(midi_track_ch(0), 0) == 1u, "the MIDI note-off at the length's end");
    press(B_PLAY);
    par_set(PAR_GATE1, 0);
    /* TIE: the next note slides there, struck once */
    step_len_set(p, 0, 0, LEN_TIE);
    main_set(p, 0, 1, 1);
    p->step[0][1].note = SEQ_DEFAULT_NOTE + 7u;
    step_len_set(p, 0, 1, LEN_4);                    /* (AUTO's MIDI note-off would come the next tick, as the LXR's) */
    run_ms(20);
    midi_offs(0, 0);
    c = trig_count[0];
    play_from_start();
    while (sq.idx[0] < 1 || sq.tph[0] < 2u)
        run_ms(1);
    run_ms(20);
    check(trig_count[0] == c + 2u && vc[0].osc.base_note == SEQ_DEFAULT_NOTE + 7u, "TIE: the next note's pitch");
    check(vc[0].aeg.state != EG_A || vc[0].aeg.hold, "TIE: not struck again");
    i = midi_offs(midi_track_ch(0), &ons);
    check(ons == 2u && i == 1u, "TIE in MIDI: the new note on, then the old off");
    press(B_PLAY);
    vp_set(VP_MOUT, 0);
    shot("step-tie");
    seq_clear_track(0, p);

    /* 19d: DENSITY: thinning drops the weakest first, the bar's first steps last */
    for (i = 0; i < 16u; i++) {
        main_set(p, 0, i, 1);
        p->step[0][i].volume = 100;
    }
    sq.dens = -40;
    sq.dex = 0;
    {
        uint32_t kept = 0, d1 = 0;
        for (i = 0; i < 16u; i++)
            kept += !dens_drop(p, 0, i, 0);
        check(!dens_drop(p, 0, 0, 0) && !dens_drop(p, 0, 4, 0) && dens_drop(p, 0, 1, 0) && kept == 4u,
              "DENSITY -40: the beats kept, the 16ths gone");
        sq.dex = 1;
        for (i = 0; i < 16u; i++)
            d1 += dens_drop(p, 0, i, 0);
        check(d1 == 0u, "DENSITY: a track left out keeps its steps");
        sq.dex = 0;
        sq.dens = -64;
        check(!dens_drop(p, 0, 0, 0) && dens_drop(p, 0, 4, 0), "DENSITY at its least: the bar's first step alone");
    }
    seq_clear_track(0, p);
    main_set(p, 0, 0, 1);
    sq.dens = 63;
    sq.dbuild = DB_RATCHETS;
    {
        uint32_t g = 0;
        for (i = 1; i < 16u; i++)
            g += dens_ghost(p, 0, i, 0);
        check(g >= 12u && dens_rat(p, 0, 0, 0), "DENSITY up, RATCHETS: ghosts in the empty steps, a ratchet on the hit");
        sq.dens = 20;
        g = 0;
        for (i = 1; i < 16u; i++)
            g += dens_ghost(p, 0, i, 0);
        check(dens_part(0, 0) == 256u && g < 12u && !dens_rat(p, 0, 0, 0), "a third of the way: the chances, a few ghosts");
    }
    sq.dens = 0;
    check(gen_dens(0, 60) == 60, "DENSITY 0: a GRIDS density as it is");
    sq.dens = 20;
    check(gen_dens(0, 60) == 100 && gen_dens(0, 120) == 127, "DENSITY: GRIDS' density pushed");
    sq.dens = 0;
    /* the DENSITY page: KNOB 1 DENSITY, the black keys leave tracks out */
    perf_goto(PG_DENS);
    check(perf_dens(), "PERF: the DENSITY page");
    page_params(pp);
    check(pp[0] == VP_DENS && pp[1] == VP_GHOST && pp[2] == VP_BUILD, "DENSITY: DENSITY GHOSTS BUILD");
    turn(EN_K1, -20);
    black(2);
    run_ms(10);
    check(pfs.dens == -20 && sq.dens == -20 && (pfs.dex & 4u) && sq.dex == pfs.dex, "DENSITY -20, D3 left out");
    shot("perf-density");
    turn(EN_K3, 2);
    check(pfs.build == DB_RATCHETS, "DENSITY: BUILD RATCHETS");
    shot("perf-density-ratchets");
    turn(EN_K3, -1);
    black(2);
    check(!(pfs.dex & 4u), "DENSITY: a black key again, D3 back in");
    /* 19c: LFO 3 on TRANSPOSE and DENSITY */
    {
        int32_t lo = 99, hi = -99, dl = 99, dh = -99;
        gls[2].dest = GD_TRANSPOSE;
        gls[2].depth = 127;
        gls[2].len = 0;
        gls[2].wave = LFO_TRI;
        gls[2].mode = GM_FREE;
        gls[1].dest = GD_DENSITY;
        gls[1].depth = 64;
        gls[1].len = 0;
        gls[1].wave = LFO_TRI;
        gls[1].mode = GM_FREE;
        play_from_start();
        for (i = 0; i < 60u; i++) {
            run_ms(10);
            lo = sq.transp < lo ? sq.transp : lo;
            hi = sq.transp > hi ? sq.transp : hi;
            dl = sq.dens < dl ? sq.dens : dl;
            dh = sq.dens > dh ? sq.dens : dh;
        }
        printf("  LFO 3 TRANSPOSE %d..%d; LFO 2 DENSITY %d..%d\n", lo, hi, dl, dh);
        check(lo <= -6 && hi >= 6, "LFO 3: TRANSPOSE up and down 7 degrees");
        check(dl < -20 && dh > -20, "LFO 2: DENSITY either way from the knob's");
        shot("perf-lfo3");
        press(B_PLAY);
        {
            int rc = proj_save(1, "LFO3    ");
            gls[2].dest = 0;
            pfs.dens = 0;
            pfs.dex = 0x40;
            rc |= proj_load(1);
            check(!rc && gls[2].dest == GD_TRANSPOSE && gls[2].depth == 127 && pfs.dens == -20 && pfs.dex == 0,
                  "project: LFO 3, DENSITY (KSC1)");
            pfs.dj_reso = 99;
            rc = proj_save(1, "LFO3    ");
            pfs.dj_reso = 0;
            rc |= proj_load(1);
            check(!rc && pfs.dj_reso == 99, "project: the DJ filter's Reso (KSC1)");
            pfs.dj_reso = 0;
        }
        memset(gls, 0, sizeof gls);
        pfs.dens = 0;
        run_ms(20);
    }
    press(B_HOME);
    p4_reset();
}

/* ------------------------------------------------------ Phase 20 (0.16) --- */
static uint32_t mel_hits(uint32_t n, uint8_t *out)   /* D1's first n notes (n <= 16) from the start, as they played */
{
    uint32_t i, c = trig_count[0], ms = 0;
    play_from_start();
    while (trig_count[0] - c < n && ms < 20000u) {
        run_ms(2);
        ms += 2;
    }
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)(mel_n[0][i] & 127u);
    return trig_count[0] - c >= n ? n : 0u;
}
static int32_t pc_of(uint32_t note)                  /* D1's note's pitch class, sounding */
{
    int32_t s = (int32_t)(vc[0].osc.midi_freq >> 8) + (int32_t)note - SEQ_DEFAULT_NOTE;
    return (s % 12 + 12) % 12;
}
static void phase20(void)
{
    pattern_t *p = &pat[0];
    uint16_t pp[4];
    uint8_t nt[32];
    uint32_t i, k, pg, np, ok;
    char b[40];
    p4_reset();
    p = &pat[0];
    sq.muted = 0;
    kset.accel = 0;
    for (i = 0; i < 16u; i++)
        main_set(p, 0, i, 1);
    p->scale = SC_MINPENT;
    p->sroot = 9;                                    /* A minor pentatonic: A C D E G */
    black(0);
    seq_goto(PG_MELODY);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[0] == VP_MEL + ML_SHAPE && pp[3] == VP_MEL + ML_STEPS && !strcmp(b, "MELODY (1/2)"),
          "EUCLID: MELODY: SHAPE SPREAD BIAS STEPS");
    turn(EN_K1, 1);
    check(p->mel[0][ML_SHAPE] == MEL_RANDOM && p->mel[0][ML_LOOP] == 8 && p->mel[0][ML_SPREAD] == MEL_DEFAULT[ML_SPREAD],
          "MELODY: RANDOM, its settings from their defaults");
    shot("melody");
    turn(EN_SELECT, 1);
    title_text(b, sizeof b, &pg, &np);
    page_params(pp);
    check(pp[0] == VP_MEL + ML_DEJA && pp[1] == VP_MEL + ML_LOOP && !strcmp(b, "MELODY (2/2)"), "MELODY (2/2): the loop");
    shot("melody-loop");
    /* RANDOM, DEJA VU 0: notes in the scale, changing */
    k = mel_hits(16, nt);
    press(B_PLAY);
    {
        uint32_t dist = 0, j;
        ok = k == 16u;
        for (i = 0; i < k; i++) {
            int32_t c = pc_of(nt[i]);
            ok &= (SCALE_MASK[SC_MINPENT] >> (uint32_t)((c - 9 + 12) % 12)) & 1u;
            for (j = 0; j < i && nt[j] != nt[i]; j++)
                ;
            dist += j == i;
        }
        printf("  MELODY RANDOM: %u notes, %u different\n", k, dist);
        check(ok, "RANDOM: every note in A minor pentatonic");
        check(dist >= 4u, "RANDOM, DEJA VU 0: the notes change");
    }
    /* DEJA VU 127, LOOP 4: a locked loop of 4 */
    p->mel[0][ML_DEJA] = 127;
    p->mel[0][ML_LOOP] = 4;
    k = mel_hits(16, nt);
    press(B_PLAY);
    for (ok = k == 16u, i = 4; i < k; i++)
        ok &= nt[i] == nt[i - 4u];
    check(ok, "DEJA VU 127, LOOP 4: the same four notes round");
    /* STEPS at the top: the root and the fifth */
    p->mel[0][ML_DEJA] = 0;
    p->mel[0][ML_STEPS] = 127;
    p->mel[0][ML_SPREAD] = 127;
    k = mel_hits(16, nt);
    press(B_PLAY);
    for (ok = k == 16u, i = 0; i < k; i++)
        ok &= pc_of(nt[i]) == 9 || pc_of(nt[i]) == 4;
    check(ok, "STEPS 127: only A and E");
    /* WALK: a degree or two at a time */
    p->mel[0][ML_SHAPE] = MEL_WALK;
    p->mel[0][ML_STEPS] = 0;
    p->mel[0][ML_SPREAD] = 64;
    k = mel_hits(16, nt);
    press(B_PLAY);
    {
        uint8_t set[12];
        int32_t n = (int32_t)mel_set(0, set), c = (int32_t)(vc[0].osc.midi_freq >> 8);
        for (ok = k == 16u, i = 1; i < k; i++) {
            int32_t a = mel_deg(set, n, c + nt[i - 1u] - SEQ_DEFAULT_NOTE - 9);
            int32_t d = mel_deg(set, n, c + nt[i] - SEQ_DEFAULT_NOTE - 9) - a;
            ok &= d >= -2 && d <= 2 && d != 0;
        }
        check(ok, "WALK: a degree or two from the last, every note");
    }
    /* ARP UP: rising chord tones, round again */
    p->mel[0][ML_SHAPE] = MEL_ARP_UP;
    p->mel[0][ML_SPREAD] = 30;
    k = mel_hits(16, nt);
    press(B_PLAY);
    {
        uint32_t up = 0, down = 0;
        for (i = 1; i < k; i++) {
            up += nt[i] > nt[i - 1u];
            down += nt[i] < nt[i - 1u];
        }
        printf("  ARP UP: %u up, %u down (%u %u %u %u %u)\n", up, down, nt[0], nt[1], nt[2], nt[3], nt[4]);
        check(up > down * 2u && down >= 1u, "ARP UP: up, then round again");
    }
    /* MOTIF: every fourth phrase backwards, ending on the root */
    p->mel[0][ML_SHAPE] = MEL_MOTIF;
    p->mel[0][ML_DEJA] = 127;
    p->mel[0][ML_SPREAD] = 64;
    k = mel_hits(16, nt);
    press(B_PLAY);
    check(k == 16u && pc_of(nt[15]) == 9 && nt[12] == nt[3] && nt[13] == nt[2] && nt[14] == nt[1],
          "MOTIF: the fourth phrase backwards, on the root");
    /* LENGTH: the melody's length onto the note; COMMIT writes the notes in, UNDO takes them back */
    p->mel[0][ML_SHAPE] = MEL_RANDOM;
    p->mel[0][ML_DEJA] = 0;
    p->mel[0][ML_LEN] = LEN_8;
    k = mel_hits(16, nt);
    check(sq.glen[0] > 0u, "LENGTH 1/8: the note held for its length");
    press(B_PLAY);
    while (ui.page != PG_ACTION)
        turn(EN_SELECT, -1);
    page_params(pp);
    check(pp[0] == VP_ACTION && vp_max(VP_ACTION) == 3, "ACTION with a melody: COMMIT, UNDO, RESET");
    turn(EN_K1, 1);
    press(B_OCTUP);
    for (ok = 1, i = 0; i < 16u; i++)
        ok &= p->step[0][i].note == (mel_n[0][i] & 127u) && step_len(p, 0, i) == mel_l[0][i];
    if (!ok)
        printf("  COMMIT: note %u / %u, len %u / %u, action %u\n", p->step[0][1].note, mel_n[0][1], step_len(p, 0, 1),
               mel_l[0][1], ui.action);
    check(ok && ui.action == ACT_NONE, "COMMIT: the melody's notes and lengths into the steps");
    turn(EN_K1, 2);
    press(B_OCTUP);
    check(p->step[0][0].note == SEQ_DEFAULT_NOTE && step_len(p, 0, 0) == LEN_AUTO && p->mel[0][ML_SHAPE] == MEL_RANDOM,
          "UNDO: the steps back, the melody on");
    /* the project, and the global LFOs' CHANGE and SPREAD */
    {
        int rc = proj_save(1, "MELODY  ");
        memset(p->mel[0], 0, NML);
        rc |= proj_load(1);
        p = &pat[0];
        check(!rc && p->mel[0][ML_SHAPE] == MEL_RANDOM && p->mel[0][ML_LEN] == LEN_8, "project: the melody");
    }
    {
        int32_t lo = 99, hi = -99;
        gls[0].dest = GD_MCHANGE;
        gls[0].depth = 127;
        gls[0].len = 0;
        gls[0].wave = LFO_TRI;
        gls[0].mode = GM_FREE;
        play_from_start();
        for (i = 0; i < 60u; i++) {
            run_ms(10);
            lo = sq.mchg < lo ? sq.mchg : lo;
            hi = sq.mchg > hi ? sq.mchg : hi;
        }
        press(B_PLAY);
        check(lo < -50 && hi > 50, "LFO: MELODY CHANGE either way");
        memset(gls, 0, sizeof gls);
        run_ms(20);
    }
    press(B_HOME);
    p4_reset();
}

/* ------------------------------------------------------ Phase 21 (0.16) --- */
#include <time.h>
static int16_t p21_l[FS * 4], p21_r[FS * 4];
static uint32_t p21_sine;                            /* (fx_alone: the noise a sine of this many Hz instead) */
/* the FX bus alone for n blocks (the engine idle): a click into the left (and right) at sample 0, the output */
static void fx_alone(uint32_t blocks, int32_t cl, int32_t cr, uint32_t noise)
{
    uint32_t b, i, r = 12345u;
    for (b = 0; b < blocks; b++) {
        fxbus_early();
        for (i = 0; i < BLOCK; i++) {
            int32_t nz = 0;
            if (b * BLOCK + i < noise && p21_sine) {
                nz = (int32_t)(8000.0 * sin(2.0 * M_PI * p21_sine * (b * BLOCK + i) / FS));
            } else if (b * BLOCK + i < noise) {
                r = r * 1664525u + 1013904223u;
                nz = (int32_t)(r >> 20) - 2048;
            }
            fx_l[i] = (int16_t)((b == 0u && i == 0u ? cl : 0) + nz);
            fx_r[i] = (int16_t)((b == 0u && i == 0u ? cr : 0) + nz);
            mix_l[i] = mix_r[i] = 0;
            rev_in[i] = 0;
        }
        rev_fed = 0;
        fxbus_process();
        if (b * BLOCK + BLOCK <= sizeof p21_l / sizeof p21_l[0])
            for (i = 0; i < BLOCK; i++) {
                p21_l[b * BLOCK + i] = (int16_t)sat16(mix_l[i]);
                p21_r[b * BLOCK + i] = (int16_t)sat16(mix_r[i]);
            }
    }
}
static uint32_t first_peak(const int16_t *s, uint32_t from, uint32_t to, int32_t thr)
{
    uint32_t i, best = 0;
    int32_t m = thr;
    for (i = from; i < to; i++)
        if (abs(s[i]) > m) {
            m = abs(s[i]);
            best = i;
        }
    return best;
}
static void fx_fresh(uint32_t dtype)                 /* (1.0: the always-on delay at LEVEL 127, the bus OFF) */
{
    par_set(PAR_FX_TYPE, FX_OFF);
    par_set(PAR_FX_DELAY_TYPE, dtype);
    par_set(PAR_DLY_LEVEL, 127);
    par_set(PAR_DLY_ROUTE, 127);                     /* (1.0 Phase 16: FX > DLY, the bus into the delay) */
    par_set(PAR_FX_ROUTING, R_ST1);
    run_ms(30);                                      /* (1.0: a new MODE's line cleared over ~13 ms, its input faded in) */
    memset(fxbuf, 0, sizeof fxbuf);                  /* (the line from silence) */
    memset(&tp, 0, sizeof tp);
    fx.d_lp[0] = fx.d_lp[1] = 0;
}
static void phase21(void)
{
    uint32_t i, at, at2;
    int32_t pk;
    p4_reset();
    par_set(PAR_BPM, 120);
    /* 21b: SYNC on NORMAL: 1/8 at 120 BPM, 11025 samples */
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DSYNC, 1);
    par_set(PAR_DLY_TIME, 58);                          /* (TIME 58: 1/8) */
    par_set(PAR_DLY_FB, 40);
    par_set(PAR_DLY_TONE, 100);
    run_ms(10);
    fx_fresh(DELAY_NORMAL);
    fx_alone(400, 20000, 20000, 0);
    at = first_peak(p21_l, 100, 20000, 2000);
    printf("  NORMAL, SYNC 1/8 at 120: the echo at %u (11025)\n", at);
    check(at >= 11024u && at <= 11027u, "SYNC: NORMAL's echo a 1/8 later");
    /* 21a: TAPE: the same time at half rate */
    fx_fresh(DELAY_TAPE);
    fx_alone(400, 20000, 20000, 0);
    at = first_peak(p21_l, 100, 20000, 1000);
    printf("  TAPE, SYNC 1/8: the echo at %u\n", at);
    check(at >= 11020u && at <= 11032u, "TAPE: the echo a 1/8 later (half rate)");
    {
        int32_t e1 = abs(p21_l[at]), e2 = abs(p21_l[first_peak(p21_l, at + 5000u, at + 16000u, 0)]);
        printf("  TAPE: echo 1 %d, echo 2 %d\n", e1, e2);
        check(e2 < e1 && e2 > e1 / 8, "TAPE: the second echo quieter (FEEDBACK 40)");
    }
    /* TAPE PP: left first, then right */
    fx_fresh(DELAY_TAPEPP);
    fx_alone(800, 20000, 0, 0);
    at = first_peak(p21_l, 100, 20000, 1000);
    at2 = first_peak(p21_r, 100, 30000, 1000);
    printf("  TAPE PP: left echo at %u, right at %u\n", at, at2);
    check(at >= 11020u && at <= 11032u && at2 >= 22040u && at2 <= 22064u, "TAPE PP: left, then right a 1/8 later");
    /* TAPE -> TAPE PP mid-echo: the write head back inside the half line (0.23.1: it wrote past fxbuf) */
    fx_fresh(DELAY_TAPE);
    fxbus_process();
    tp.w = (FXBUF_SIZE >> 1) + 3u;                   /* (TAPE's head in the full line's second half) */
    par_set(PAR_FX_DELAY_TYPE, DELAY_TAPEPP);
    run_ms(10);                                      /* (the change reaches the engine, a few PP blocks run) */
    check(tp.w <= (FXBUF_MASK >> 1), "TAPE -> TAPE PP: the write head inside the PP half");
    /* feedback past 100%, SATURATE: a roar that stays bounded */
    fx_fresh(DELAY_TAPE);
    par_set(PAR_DLY_FB, 127);
    par_set(PAR_DSAT, 127);
    par_set(PAR_DLY_TONE, 100);
    run_ms(10);
    fx_alone(2700, 0, 0, 4410);                      /* (100 ms of noise, then 4 s) */
    for (pk = 0, i = FS * 3u; i < FS * 4u; i++)
        pk = abs(p21_l[i]) > pk ? abs(p21_l[i]) : pk;
    printf("  TAPE FEEDBACK 115%%, SATURATE 127: the last second's peak %d\n", pk);
    check(pk > 1000 && pk < 12000, "TAPE past 100%: it grows into a bounded roar");
    /* GLITCH, PITCH: they run, bounded */
    par_set(PAR_DLY_FB, 90);
    par_set(PAR_DSAT, 20);
    par_set(PAR_GLITCH, 127);
    par_set(PAR_DPITCH, DP_UP12);
    run_ms(10);
    fx_fresh(DELAY_TAPE);
    fx_alone(2700, 0, 0, 22050);
    for (pk = 0, i = FS; i < FS * 4u; i++)
        pk = abs(p21_l[i]) > pk ? abs(p21_l[i]) : pk;
    printf("  TAPE GLITCH 127, PITCH +12: peak %d\n", pk);
    check(pk > 200 && pk < 32767, "GLITCH, PITCH: echoes, bounded");
    {   /* PITCH +12: the second repeat an octave up (441 Hz in, 882 Hz back) */
        uint32_t zc = 0;
        par_set(PAR_GLITCH, 0);
        par_set(PAR_DLY_FB, 100);
        par_set(PAR_DLY_TONE, 127);
        par_set(PAR_DLY_WOW, 0);
        run_ms(10);
        fx_fresh(DELAY_TAPE);
        p21_sine = 441;
        fx_alone(600, 0, 0, 4410);
        p21_sine = 0;
        {   /* (the strongest frequency over the repeat, 300 .. 1500 Hz) */
            double best = 0;
            uint32_t hz;
            for (hz = 300; hz <= 1500u; hz += 3u) {
                double re = 0, im = 0, m;
                for (i = 22050u + 1000u; i < 22050u + 3205u; i++) {
                    re += p21_l[i] * cos(2.0 * M_PI * hz * i / FS);
                    im += p21_l[i] * sin(2.0 * M_PI * hz * i / FS);
                }
                m = re * re + im * im;
                if (m > best) {
                    best = m;
                    zc = hz;
                }
            }
        }
        printf("  PITCH +12: the second repeat at %u Hz (882)\n", zc);
        check(zc > 860u && zc < 905u, "PITCH +12: the repeat an octave up");
    }
    /* 21c: ECHO > REVERB */
    par_set(PAR_ECHOREV, 127);
    par_set(PAR_GLITCH, 0);
    par_set(PAR_DPITCH, DP_OFF);
    run_ms(10);
    fx_fresh(DELAY_TAPE);
    {
        int32_t any = 0;
        uint32_t b;
        fx_alone(1, 20000, 20000, 0);
        for (b = 0; b < 200u && !any; b++) {
            fx_alone(1, 0, 0, 0);
            for (i = 0; i < BLOCK; i++)
                any |= rev_in[i];
        }
        check(any && rev_fed, "ECHO > REVERB: the echoes into the reverb's send");
    }
    /* the cost: TAPE PP with everything on, a block on the host */
    par_set(PAR_GLITCH, 127);
    par_set(PAR_DPITCH, DP_UP7);
    par_set(PAR_DLY_WOW, 127);
    run_ms(10);
    fx_fresh(DELAY_TAPEPP);
    {
        struct timespec a, b;
        double us;
        clock_gettime(CLOCK_MONOTONIC, &a);
        fx_alone(4000, 0, 0, 1u << 30);
        clock_gettime(CLOCK_MONOTONIC, &b);
        us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3 / 4000.0;
        printf("  TAPE PP, everything on: %.2f us a block on the host (with the test's own fill)\n", us);
    }
    par_set(PAR_DLY_TIME, 127);                         /* (the screens: 1/2 at 120 is 1 s, NORMAL's 0.74 s: /2) */
    par_set(PAR_BPM, 60);
    run_ms(20);
    goto_mode(B_FX, M_FX);
    fx_goto(FXPG_DLY);
    shot("fx-tape");
    fx_goto(FXPG_DUB);
    {
        uint16_t pp[4];
        page_params(pp);
        check(pp[0] == PAR_DLY_WOW && pp[3] == PAR_DPITCH, "FX: DELAY: TAPE, WOW .. PITCH");
    }
    shot("fx-delay-dub");
    fx_goto(FXPG_DLY2);
    {
        uint16_t pp[4];
        page_params(pp);
        check(pp[0] == PAR_DLY_TONE && pp[1] == PAR_ECHOREV && pp[2] == PAR_DLY_FB && pp[3] == PAR_DLY_BAND,
              "FX: DELAY page 2: TONE, ECHO > REVERB, FEEDBACK, BAND");
    }
    shot("fx-delay-send");
    par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
    run_ms(20);
    fx_goto(FXPG_DLY);
    shot("fx-normal-sync-half");
    press(B_HOME);
    par_set(PAR_BPM, 120);
    par_set(PAR_FX_TYPE, FX_OFF);
    par_set(PAR_DLY_LEVEL, 0);
    par_set(PAR_DSYNC, 0);
    par_set(PAR_DSAT, 0);
    par_set(PAR_ECHOREV, 0);
    par_set(PAR_GLITCH, 0);
    par_set(PAR_DPITCH, 0);
    par_set(PAR_FX_DELAY_TYPE, 0);
    run_ms(20);
}

/* 22: PLATE (Clouds' reverb) rings after a hit, dies away and sleeps; SPACE holds its tail */
static double p22_hit(uint32_t space_at, uint32_t from, uint32_t to, uint32_t blocks)
{
    double e = 0;
    uint32_t i, j;
    voices_init();
    fxbus_init();
    reverb_init();
    kit_init_par();
    par[PAR_VELOD1] = 20;
    par[PAR_REV_TYPE] = 3;
    par[PAR_REV_SIZE] = 64;
    par[PAR_REV_SEND1] = 127;
    snd_post_all();
    snd_service();
    voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
    for (i = 0; i < blocks; i++) {
        if (i == space_at)
            space_on = 1;
        voices_block();
        for (j = 0; j < 64u; j++)
            if (i >= from && i < to)
                e += (double)mix_l[j] * mix_l[j] + (double)mix_r[j] * mix_r[j];
    }
    return e;
}
static void phase22(void)
{
    double e1 = p22_hit(~0u, 200, 400, 400), e2 = p22_hit(~0u, 2200, 2400, 7000), s1, s2;
    int32_t side = 0;
    uint32_t i;
    printf("  PLATE: tail %.3g, at 3.2 s %.3g; asleep at 10 s: %u\n", e1, e2, rv.idle);
    check(e1 > 1e6 && e2 < e1 * 0.01, "PLATE rings and dies away");
    check(rv.idle == 255, "PLATE: silent at the end, the reverb asleep");
    p22_hit(~0u, 0, 0, 300);
    for (i = 0; i < BLOCK; i++)
        side |= rev_side[i];
    check(side != 0, "PLATE: a stereo return (its two branches)");
    s1 = p22_hit(150, 600, 800, 800);
    s2 = p22_hit(150, 2200, 2400, 2400);
    printf("  PLATE, SPACE: at 0.9 s %.3g, at 3.2 s %.3g\n", s1, s2);
    check(s2 > s1 * 0.2, "PLATE: SPACE holds the tail");
    space_on = 0;
    reverb_init();
    voices_init();
    kit_load_ram(kit_loaded);
    snd_service();
    goto_mode(B_HOME, M_HOME);
    par_set(PAR_REV_TYPE, 3);
    press(B_FX);
    fx_goto(FXPG_REV);
    run_ms(40);
    check(rv.type == 3, "the reverb page: PLATE");
    shot("reverb-plate");
    press(B_HOME);
    par_set(PAR_REV_TYPE, 0);
    run_ms(20);
}

/* 23: DIRT. a: the new shapes, bounded, each its own; b: the bus's drive; c: the master's, each type bounded, near the
 * dry level, off at 0 */
static double p23_h3(const int32_t *x, uint32_t n)   /* the 3rd harmonic's share of a 441 Hz tone (100 samples) */
{
    double c = 0, s = 0, e = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        c += x[i] * cos(2 * M_PI * 3 * i / 100.0);
        s += x[i] * sin(2 * M_PI * 3 * i / 100.0);
        e += (double)x[i] * x[i];
    }
    return (c * c + s * s) * 2 / n / (e + 1);
}
static void phase_016t2(void)                        /* 0.16-test2: FX's page order; the HYRULE palette */
{
    uint32_t pal0;
#define FXSENDS FXPG_FSEND, FXPG_FSEND2, FXPG_DSEND, FXPG_DSEND2, FXPG_RSEND, FXPG_RSEND2   /* (31: the sends last;
                                                      * 1.0 Phase 16: the delay's between) */
    static const uint8_t OFF[] = {FXPG_REV, FXPG_BUS, FXPG_DLY, FXPG_DLY2, FXPG_ROUTE, FXPG_DIRT, FXPG_DJ, FXPG_COMP,
                                  FXPG_COMP2, FXSENDS};
    static const uint8_t PH[] = {FXPG_REV, FXPG_BUS, FXPG_PARS, FXPG_DLY, FXPG_DLY2, FXPG_ROUTE, FXPG_DIRT, FXPG_DJ,
                                 FXPG_COMP, FXPG_COMP2, FXSENDS};
    static const uint8_t DL[] = {FXPG_REV, FXPG_BUS, FXPG_DLY, FXPG_DLY2, FXPG_DUB, FXPG_ROUTE, FXPG_DIRT, FXPG_DJ,
                                 FXPG_COMP, FXPG_COMP2, FXSENDS};   /* (1.0: the always-on delay's; TAPE's page with TAPE) */
    static const struct { uint32_t type, dtype; const uint8_t *pg; uint32_t n; } C[3] = {
        {FX_OFF, DELAY_NORMAL, OFF, 15}, {FX_PHASER, DELAY_NORMAL, PH, 16}, {FX_OFF, DELAY_TAPE, DL, 16}};
    uint32_t k, i, ok;
    uint8_t l[NFXPG];
    press(B_HOME);
    press(B_FX);
    for (k = 0; k < 3u; k++) {
        par_set(PAR_FX_TYPE, C[k].type);
        par_set(PAR_FX_DELAY_TYPE, C[k].dtype);
        ok = fx_shown(l) == C[k].n && mode_pages() == C[k].n;
        for (i = 0; ok && i < C[k].n; i++)
            ok = l[i] == C[k].pg[i];
        check(ok, "FX: the pages in order, the reverb first and the type's pages together");
        ui.page = 0;
        ok = 1;
        for (i = 0; i < C[k].n; i++) {               /* SELECT walks them in that order */
            ok &= fx_page() == C[k].pg[i];
            turn(EN_SELECT, 1);
        }
        check(ok, "FX: SELECT walks the pages in order");
    }
    fx_goto(FXPG_DLY);
    ui.force = 1;
    run_ms(100);
    shot("fx-order-delay");
    {   /* 1.0: the delay's MODE knob (page 1, KNOB 1): OFF first, then MONO .. TAPE PP; the init kit's OFF */
        static const uint8_t M[] = {DELAY_OFF, DELAY_NORMAL, DELAY_PINGPONG, DELAY_TAPE, DELAY_TAPEPP, DELAY_GRAIN};
        uint16_t pp[4];
        uint8_t ik[KIT_LEN];
        page_params(pp);
        check(pp[0] == PAR_FX_DELAY_TYPE && pp[1] == PAR_DSYNC && pp[2] == PAR_DLY_TIME && pp[3] == PAR_DLY_LEVEL,
              "FX: DELAY page 1: MODE, SYNC, TIME, LEVEL");
        par_set(PAR_FX_DELAY_TYPE, DELAY_TAPEPP);
        turn(EN_K1, -10);
        ok = par[PAR_FX_DELAY_TYPE] == DELAY_OFF;
        for (i = 1; i < sizeof M; i++) {
            turn(EN_K1, 1);
            ok &= par[PAR_FX_DELAY_TYPE] == M[i];
        }
        turn(EN_K1, 1);
        ok &= par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN;
        check(ok, "FX: DELAY MODE turns OFF, MONO, PING PONG, TAPE, TAPE PP, GRAIN");
        kit_init_values(ik);
        check(ik[kit_idx(PAR_FX_DELAY_TYPE)] == DELAY_OFF, "FX: the init kit's delay MODE is OFF");
        par_set(PAR_FX_DELAY_TYPE, DELAY_OFF);
        par_set(PAR_DLY_LEVEL, 100);
        ui.force = 1;
        run_ms(100);
        check(!dly_active() || !fx.dl_e, "FX: DELAY MODE OFF: silent");
        shot("fx-delay-off");
        par_set(PAR_DLY_LEVEL, 0);
    }
    par_set(PAR_FX_TYPE, FX_OFF);
    par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
    for (k = 0; k < NPALETTES && strcmp(UI_PALETTES[k].name, "HYRULE"); k++)
        ;
    check(k == 10u, "HYRULE: palette 10 (saved ids stay: palettes are appended)");
    pal0 = kset.palette;                             /* (1.0: put back after, so later shots keep SAND's colours) */
    vp_set(VP_PALETTE, k);
    ui.page = 0;
    press(B_HOME);
    ui.force = 1;
    run_ms(100);
    shot("palette-hyrule");
    press(B_FX);
    fx_goto(FXPG_REV);
    ui.force = 1;
    run_ms(100);
    shot("palette-hyrule-fx");
    vp_set(VP_PALETTE, pal0);
}

static void phase23(void)
{
    static const char *const SN[4] = {"FUZZ", "TAPE", "SINE FOLD", "BITE"};
    int32_t x[6400], y[6400];
    uint32_t i, k, t;
    double rms_d = 0;
    for (i = 0; i < 6400u; i++) {
        x[i] = (int32_t)(16000 * sin(2 * M_PI * i / 100.0));
        rms_d += (double)x[i] * x[i];
    }
    rms_d = sqrt(rms_d / 6400);
    /* 23a */
    for (k = 0; k < 4u; k++) {
        voice_t v;
        int16_t b[BLOCK];
        int32_t pk = 0;
        double h3;
        memset(&v, 0, sizeof v);
        v.shape = (uint8_t)(SH_FUZZ + k);
        v.drive = 40000;
        for (i = 0; i < 6400u; i += BLOCK) {
            for (t = 0; t < BLOCK; t++)
                b[t] = (int16_t)x[i + t];
            shape_block(&v, b);
            for (t = 0; t < BLOCK; t++) {
                y[i + t] = b[t];
                pk = abs(b[t]) > pk ? abs(b[t]) : pk;
            }
        }
        h3 = p23_h3(y + 6300, 100);
        printf("  shape %-9s peak %d, 3rd harmonic %.3f\n", SN[k], pk, h3);
        check(pk > 4000 && pk <= 32768, "23a: a new shape sounds, bounded");
        check(h3 > 0.002, "23a: a new shape adds harmonics");
        check(strcmp(L_SHAPE[SH_FUZZ + k], SN[k]) == 0, "23a: the shape's name");
    }
    check(par_max_k(PAR_SHAPE1) == NSHAPE - 1u && NSHAPE == 9u, "23a: nine shapes");
    /* 23c: the master's DIRT */
    for (k = 0; k < NDIRT; k++) {
        double rms = 0, h3;
        int32_t pk = 0;
        memset(&dirt, 0, sizeof dirt);
        dirt.type = (uint8_t)k;
        dirt.amt = 65536;
        for (i = 0; i < 6400u; i += BLOCK) {
            int32_t l[BLOCK], r[BLOCK];
            for (t = 0; t < BLOCK; t++)
                l[t] = r[t] = x[i + t];
            dirt_master(l, r);
            for (t = 0; t < BLOCK; t++) {
                y[i + t] = l[t];
                rms += (double)l[t] * l[t];
                pk = abs(l[t]) > pk ? abs(l[t]) : pk;
            }
        }
        rms = sqrt(rms / 6400);
        h3 = p23_h3(y + 6300, 100);
        printf("  master DIRT %-5s peak %d, level %+.1f dB, 3rd harmonic %.3f\n", L_DIRT[k], pk,
               20 * log10(rms / rms_d), h3);
        check(pk <= 40000 && rms > rms_d * 0.5 && rms < rms_d * 2.0, "23c: DIRT bounded, near the dry level");
        check(h3 > 0.002, "23c: DIRT adds harmonics");
    }
    {
        int32_t l[BLOCK], r[BLOCK];
        int same = 1;
        memset(&dirt, 0, sizeof dirt);
        dirt.type = DT_FUZZ;
        for (t = 0; t < BLOCK; t++)
            l[t] = r[t] = x[t] * 3;
        dirt_master(l, r);
        for (t = 0; t < BLOCK; t++)
            same &= l[t] == x[t] * 3;
        check(same, "23c: AMOUNT 0: the master untouched");
    }
    /* 23b: the bus's drive, before its type (OFF here: the bus as it is) */
    {
        double e0 = 0, h0, h1;
        int16_t keep[BLOCK];
        int32_t a[BLOCK];
        fx.route = 0;
        for (t = 0; t < BLOCK; t++)
            fx_l[t] = fx_r[t] = keep[t] = (int16_t)x[t];
        dirt.bus = 0;
        dirt_bus();
        for (t = 0; t < BLOCK; t++)
            e0 += fx_l[t] == keep[t] ? 0 : 1;
        check(e0 == 0, "23b: bus DIRT 0: the bus untouched");
        for (t = 0; t < BLOCK; t++)
            a[t] = keep[t];
        h0 = p23_h3(a, 100 < BLOCK ? 100 : BLOCK);
        dirt.bus = 65536;
        dirt_bus();
        for (t = 0; t < BLOCK; t++)
            a[t] = fx_l[t];
        h1 = p23_h3(a, 100 < BLOCK ? 100 : BLOCK);
        printf("  bus DIRT 127: the 3rd harmonic %.3f (dry %.3f)\n", h1, h0);
        check(h1 > h0 + 0.002, "23b: bus DIRT drives the bus");
        memset(&dirt, 0, sizeof dirt);
    }
    /* the kit: the three bytes, the page */
    p4_reset();
    par_set(PAR_FXDIRT, 50);
    par_set(PAR_MDIRT, DT_DIODE);
    par_set(PAR_MDIRT_AMT, 90);
    run_ms(20);
    check(dirt.bus > 0 && dirt.type == DT_DIODE && dirt.amt > 40000, "23: the DIRT parameters set the stages");
    check(morph_excluded(PAR_MDIRT) && !morph_excluded(PAR_MDIRT_AMT), "23: DIRT's type doesn't morph, its amount does");
    goto_mode(B_HOME, M_HOME);
    press(B_FX);
    fx_goto(FXPG_DIRT);
    run_ms(40);
    shot("fx-dirt");
    press(B_HOME);
    par_set(PAR_FXDIRT, 0);
    par_set(PAR_MDIRT, 0);
    par_set(PAR_MDIRT_AMT, 0);
    run_ms(20);
    check(!dirt.bus && !dirt.amt, "23: DIRT off again");
}

/* 0.16-test3: TIME with SYNC a note value a detent; the master COMPRESSOR and its SIDECHAIN; STUTTER */
static void phase_016t3(void)
{
    uint32_t i, k, ok;
    /* SYNC: each detent of TIME the next note value, both ways */
    p4_reset();
    fx_fresh(DELAY_TAPE);
    par_set(PAR_DSYNC, 1);
    par_set(PAR_DLY_TIME, 0);
    run_ms(20);
    press(B_HOME);
    press(B_FX);
    fx_goto(FXPG_DLY);
    for (ok = 1, i = 0; i < NSYNCV - 1u; i++) {
        uint32_t a = (par[PAR_DLY_TIME] * 65536u / 127u * NSYNCV) >> 16, b;
        turn(EN_K3, 1);                              /* (1.0: TIME the delay page's third knob) */
        b = (par[PAR_DLY_TIME] * 65536u / 127u * NSYNCV) >> 16;
        ok &= b == a + 1u;
    }
    check(ok, "SYNC: TIME a note value a detent, up to 1/2");
    turn(EN_K3, -3);
    check(((par[PAR_DLY_TIME] * 65536u / 127u * NSYNCV) >> 16) == NSYNCV - 4u, "SYNC: .. and back down");
    par_set(PAR_DSYNC, 0);
    par_set(PAR_FX_TYPE, FX_OFF);
    par_set(PAR_DLY_LEVEL, 0);
    run_ms(20);
    /* the COMPRESSOR: OFF leaves the mix alone; 4:1 at -24 dB takes ~3/4 of the excess; a key ducks the rest, not
     * itself */
    {
        int32_t l[BLOCK], r[BLOCK], kl[BLOCK];
        double g;
        par_set(PAR_MC_RATIO, 0);
        run_ms(5);
        for (i = 0; i < BLOCK; i++)
            l[i] = r[i] = (int32_t)(16000 * sin(2 * M_PI * i / 32.0));
        comp_master(l, r);
        for (ok = 1, i = 0; i < BLOCK; i++)
            ok &= l[i] == (int32_t)(16000 * sin(2 * M_PI * i / 32.0));
        check(ok && mc.gr == 0, "COMP: RATIO OFF: the mix as it was");
        par_set(PAR_MC_RATIO, 4);                    /* (4:1) */
        par_set(PAR_MC_THR, 63);                     /* (-24 dB) */
        par_set(PAR_MC_ATK, 0);
        par_set(PAR_MC_REL, 30);
        par_set(PAR_MC_SRC, 0);
        par_set(PAR_MC_GAIN, 0);
        par_set(PAR_MC_MIX, 127);
        run_ms(5);
        for (k = 0; k < 400u; k++) {                 /* (a sine at -6 dB: 18 dB over, 13.5 dB taken) */
            for (i = 0; i < BLOCK; i++)
                l[i] = r[i] = (int32_t)(16384 * sin(2 * M_PI * (k * BLOCK + i) / 32.0));
            comp_master(l, r);
        }
        for (g = 0, i = 0; i < BLOCK; i++)
            g = fabs((double)l[i]) > g ? fabs((double)l[i]) : g;
        g = 20 * log10(g / 16384);
        printf("  COMP 4:1 at -24 dB, a sine at -6 dB: %.1f dB (-13.5), GR %.1f dB\n", g, mc.gr * 6.02 / 65536);
        check(g < -11.5 && g > -15.5, "COMP: 4:1 takes 3/4 of what is over");
        /* a key: D1's sound, its dry part kept out */
        par_set(PAR_MC_SRC, 1);
        run_ms(5);
        check(mc_key == 1u, "COMP: SIDECHAIN D1: D1's dry part is kept out");
        for (k = 0; k < 40u; k++) {
            for (i = 0; i < BLOCK; i++) {
                vbuf[0][i] = (int16_t)(k < 20u ? 30000 * (i & 1 ? 1 : -1) : 0);   /* (D1 loud, then silent) */
                l[i] = r[i] = 8000;
                mc_kl[i] = mc_kr[i] = kl[i] = 12345;
            }
            comp_master(l, r);
            if (k == 19u) {
                printf("  COMP keyed by D1: the rest at %d (8000), D1's own %d (12345)\n", l[0] - kl[0], kl[0]);
                check(l[0] - kl[0] < 3000, "COMP: D1's hit ducks the rest");
            }
        }
        par_set(PAR_MC_REL, 0);
        run_ms(5);
        for (k = 0; k < 200u; k++) {
            for (i = 0; i < BLOCK; i++) {
                vbuf[0][i] = 0;
                l[i] = r[i] = 8000;
                mc_kl[i] = mc_kr[i] = 0;
            }
            comp_master(l, r);
        }
        check(l[0] > 7800, "COMP: D1 silent, the rest back");
        par_set(PAR_MC_RATIO, 4);
        run_ms(5);
        mc.gr = 9 << 16 / 6;
        press(B_HOME);
        press(B_FX);
        fx_goto(FXPG_COMP);
        shot("comp");
        fx_goto(FXPG_COMP2);
        shot("comp-sidechain");
        press(B_HOME);
        par_set(PAR_MC_RATIO, 0);
        par_set(PAR_MC_SRC, 0);
        run_ms(5);
        check(mc_key == 0u, "COMP: OFF: no key");
    }
    /* STUTTER: CHANCE 100%, 1/16 over a 1/4, the clock stopped (its own): the first 1/16 live, then it again */
    {
        static int16_t in[FS], out[FS];
        int16_t l[BLOCK], r[BLOCK];
        uint32_t n = 0, b, rr = 777u;
        double c, xx, yy, xy;
        par_set(PAR_BPM, 120);
        run_ms(5);
        memset(&pf, 0, sizeof pf);
        pf.cut_g = 32767;
        pf.cut_open = 1;
        pf.dj = 64;
        pfs.st_chance = 127;
        pfs.st_size = 1;
        pfs.st_len = 2;
        pfs.st_vary = SV_OFF;
        for (b = 0; b < FS / BLOCK; b++) {
            for (i = 0; i < BLOCK; i++) {
                rr = rr * 1664525u + 1013904223u;
                l[i] = r[i] = in[b * BLOCK + i] = (int16_t)(((b * BLOCK + i) % 400u < 200u ? 6000 : -6000) +
                                                            (int32_t)(rr >> 22) - 512);
            }
            perf2_master(l, r);
            for (i = 0; i < BLOCK; i++)
                out[b * BLOCK + i] = l[i];
            if (pf.st_on && !n)
                n = b * BLOCK;                       /* (where the burst started) */
        }
        {
            uint32_t len = 5512u, s0 = n + len + 200u;    /* (a 1/16 at 120: 5512 samples; the second, after its fade) */
            for (xx = yy = xy = 0, i = 0; i < 4000u; i++) {
                double x = in[n + 200u + i], y = out[s0 + i];
                xx += x * x;
                yy += y * y;
                xy += x * y;
            }
            c = xy / sqrt(xx * yy + 1);
            printf("  STUTTER 1/16: the burst at %u, its second 1/16 against the first: %.3f\n", n, c);
            check(n < 5512u * 4u && c > 0.9, "STUTTER: the first 1/16 again");
        }
        for (k = SV_SHRINK; k < NSV; k++) {          /* (each VARY, the RANDOM size: it plays, bounded) */
            int32_t pk = 0;
            uint32_t bursts = 0;
            pfs.st_vary = (uint8_t)k;
            pfs.st_size = (uint8_t)(k == SV_RAND ? NSTSIZE - 1u : k - 1u);
            for (b = 0; b < 2u * FS / BLOCK; b++) {
                for (i = 0; i < BLOCK; i++) {
                    rr = rr * 1664525u + 1013904223u;
                    l[i] = r[i] = (int16_t)((int32_t)(rr >> 18) - 8192);
                }
                perf2_master(l, r);
                bursts += pf.st_on == 2u;
                for (i = 0; i < BLOCK; i++)
                    pk = abs(l[i]) > pk ? abs(l[i]) : pk;
            }
            printf("  STUTTER VARY %u: %u blocks repeating, peak %d\n", k, bursts, pk);
            check(bursts > 100u && pk <= 8192, "STUTTER: each VARY repeats, bounded");
        }
        pfs.st_size = 1;
        pfs.st_vary = SV_OFF;
        pfs.st_chance = 0;
        for (b = 0; b < 400u; b++)
            perf2_master(l, r);
        check(!pf.st_on && !pf.st_mix, "STUTTER: CHANCE 0: no more bursts");
        gls[0].dest = GD_STUT;                       /* (a global LFO on it) */
        gls[0].depth = 127;
        gls[0].mode = GM_FREE;
        gls[0].wave = LFO_TRI;
        gls[0].phase = 64;
        run_ms(50);
        check(gl.amt[GD_STUT] > 0, "STUTTER: a global LFO moves its chance");
        gls[0].dest = GD_OFF;
        gls[0].depth = 0;
        /* the page; OCT+ held there; the project keeps it */
        p4_reset();
        goto_mode(B_ARP, M_PERF);
        ui.page = PG_STUT;
        ui.force = 1;
        run_ms(40);
        {
            uint16_t kp[4];
            page_params(kp);
            check(kp[0] == VP_STCH && kp[3] == VP_STVARY, "STUTTER: its page, CHANCE .. VARY");
        }
        hold(B_OCTUP);
        check(pf.st_hold, "STUTTER: OCT+ held on its page");
        run_ms(400);
        check(pf.st_on != 0u, "STUTTER: .. stutters");
        shot("stutter");
        release(B_OCTUP);
        check(!pf.st_hold && !ui.copy, "STUTTER: OCT+ let go (not COPY)");
        pfs.st_chance = 50;
        pfs.st_size = 4;
        pfs.st_len = 3;
        pfs.st_vary = SV_PDOWN;
        {
            int rc = proj_save(1, "STUTTER ");
            perf_defaults();
            rc |= proj_load(1);
            check(rc == 0 && pfs.st_chance == 50 && pfs.st_size == 4 && pfs.st_len == 3 && pfs.st_vary == SV_PDOWN,
                  "STUTTER: the project keeps its settings");
            perf_defaults();
        }
    }
}

/* Phase 31: PERF RESET, the knob cards on a model change (P CHORD), sticky KEYS (phase9_keys), FX's sends, edits
 * under morph, DENSITY's VARY and COMMIT */
static void phase31(void)
{
    pattern_t *p = &pat[sq.active];
    uint32_t i, n, m0, c;
    uint16_t pp[4];
    char v[24];
    printf("phase 31: fixes and small things\n");
    goto_mode(B_HOME, M_HOME);
    /* the cards: a model change redraws them (P CHORD's knobs named and shown, not the last model's) */
    black(0);
    sound_open(S_MODEL, 0);
    par_set(PAR_MODEL1, MD_PL0 + PE_WTBL - (ELIXIR_WTABLE ? 0 : 1));   /* (0.24: from P ADD, past P WTABLE) */
    par_set(PAR_MDECAY1, 69);
    run_ms(100);
    c = rsig[R_CARD0 + 1];
    turn(EN_K1, 1);
    run_ms(100);
    page_params(pp);
    card_value(v, sizeof v, pp[1], vp_get(pp[1]));
    check(par[PAR_MODEL1] == MD_PL0 + PE_CHORD && rsig[R_CARD0 + 1] != c && !strcmp(par_name(pp[1]), "Chord") &&
          strcmp(v, "69") != 0, "a model change redraws its knobs' cards (P CHORD: Chord, a chord's name)");
    shot("p-chord");
    par_set(PAR_MODEL1, 0);
    par_set(PAR_MDECAY1, init_kit_k(PAR_MDECAY1));
    run_ms(20);
    /* FX: the sends last, as the mixer's */
    press(B_FX);
    fx_goto(FXPG_FSEND);
    page_params(pp);
    m0 = par[PAR_FX_SEND1];
    turn(EN_K1, 3);
    check(fx_page() == FXPG_FSEND && pp[0] == PAR_FX_SEND1 && par[PAR_FX_SEND1] == m0 + 3u, "FX: the FX sends");
    shot("fx-sends");
    fx_goto(FXPG_RSEND2);
    page_params(pp);
    check(pp[0] == PAR_REV_SEND1 + 4u && pp[2] == PAR_REV_LEVEL && ui.page == mode_pages() - 1u,
          "FX: the reverb sends, the last page");
    par_set(PAR_FX_SEND1, m0);
    /* edits under morph: the morph target moves too (BOTH KITS), or not (KIT ONLY) */
    goto_mode(B_HOME, M_HOME);
    black(0);
    sound_open(S_FILTER, 0);
    page_params(pp);
    vp_set(PAR_MORPH, 255);
    run_ms(20);
    morph_par[kit_idx(pp[1])] = 40;
    par_set(pp[1], 60);
    kset.medit = 2;
    turn(EN_K2, 5);
    check(par[pp[1]] == 65u && morph_par[kit_idx(pp[1])] == 45u && par_base(pp[1]) == 45u,
          "morph at 100%: an edit moves the target too, heard (BOTH KITS)");
    kset.medit = 1;
    turn(EN_K2, 5);
    check(par[pp[1]] == 70u && morph_par[kit_idx(pp[1])] == 45u, "KIT ONLY: the kit alone, the LXR's way");
    kset.medit = 0;                                  /* 1.0: SPLIT, the default: at 100% the morph kit alone */
    check(vp_get(pp[1]) == 45u, "SPLIT at 100%: the card shows the morph kit's value");
    turn(EN_K2, 5);
    run_ms(20);
    check(par[pp[1]] == 70u && morph_par[kit_idx(pp[1])] == 50u && par_base(pp[1]) == 50u,
          "SPLIT at 100%: an edit moves the morph kit alone, heard");
    kit_save_ram(7, 1);
    check(kit_at(7, kit_idx(pp[1])) == 50u, "SAVE MORPH at 100%: the morph kit as edited");
    vp_set(PAR_MORPH, 200);
    run_ms(20);
    turn(EN_K2, 5);
    check(par[pp[1]] == 75u && morph_par[kit_idx(pp[1])] == 50u && vp_get(pp[1]) == 75u,
          "SPLIT below 100%: the kit alone");
    vp_set(PAR_MORPH, 0);
    par_set(pp[1], init_kit_k(pp[1]) ? init_kit_k(pp[1]) : par[pp[1]]);
    run_ms(20);
    /* PERF RESET: SHIFT + HOME, from any page */
    perf_goto(0);
    hold(B_OCTDN);
    white(9);                                        /* (RPT .. FRZ latched) */
    white(13);
    white(1);                                        /* (a roll latched) */
    release(B_OCTDN);
    pfs.st_chance = 90;
    pfs.dens = -30;
    pf.dj = 20;
    press(B_FX);
    check(ui.pf_latch && ui.roll_latch, "PERF: effects latched");
    hold(B_OCTDN);
    press(B_HOME);
    release(B_OCTDN);
    run_ms(20);
    check(!pf.on && !ui.pf_latch && !ui.roll_latch && !sq.roll && !pfs.st_chance && !pfs.dens && pf.dj == 64 &&
          ui.mode == M_FX, "SHIFT + HOME: PERF RESET, every effect off, the page stays");
    goto_mode(B_HOME, M_HOME);
    hold(B_OCTDN);
    run_ms(20);
    shot("shift-legend");
    release(B_OCTDN);
    /* DENSITY's VARY: the order drawn again each time round; 0 as it was */
    goto_mode(B_HOME, M_HOME);
    seq_clear_track(0, p);
    for (i = 0; i < 16u; i++) {
        main_set(p, 0, i, 1);
        p->step[0][i].volume = 100;
    }
    sq.dex = 0;
    sq.dens = -40;
    sq.dvary = 0;
    {
        uint32_t a = 0, b = 0, d = 0, k;
        for (i = 0; i < 16u; i++) {
            a |= (uint32_t)dens_drop(p, 0, i, 0) << i;
            b |= (uint32_t)dens_drop(p, 0, i, 1) << i;
        }
        check(a == b, "VARY 0: the same steps go each time round");
        sq.dvary = 127;
        for (k = 0; k < 8u; k++) {
            uint32_t x = 0;
            for (i = 0; i < 16u; i++)
                x |= (uint32_t)dens_drop(p, 0, i, k) << i;
            d += x != a;
        }
        check(d >= 4u, "VARY 127: other steps go, time round after time round");
        sq.dens = -64;
        for (k = 0, d = 0; k < 8u; k++)
            d += !dens_drop(p, 0, 0, k);
        check(d == 8u, "VARY: the bar's first step stays at the least");
        sq.dvary = 0;
        sq.dens = 0;
    }
    /* DENSITY's COMMIT: OCT+ on its page; again at 0: UNDO */
    perf_goto(PG_DENS);
    turn(EN_K4, 10);
    check(pfs.dvary == 10u, "DENSITY: KNOB 4 VARY");
    turn(EN_K4, -10);
    pfs.dens = -40;
    run_ms(20);
    shot("density-commit");
    press(B_OCTUP);
    for (i = n = 0; i < 16u; i++)
        n += main_on(p, 0, i);
    check(n == 4u && pfs.dens == 0 && main_on(p, 0, 0) && main_on(p, 0, 4), "DENSITY COMMIT: the beats kept, at 0");
    press(B_OCTUP);
    for (i = n = 0; i < 16u; i++)
        n += main_on(p, 0, i);
    check(n == 16u && pfs.dens == -40, "OCT+ again: UNDO, the pattern and DENSITY as they were");
    pfs.dens = 50;
    pfs.build = DB_GHOSTS;
    pfs.ghost = 30;
    seq_clear_track(0, p);
    main_set(p, 0, 0, 1);
    run_ms(20);
    press(B_OCTUP);
    for (i = n = 0; i < 16u; i++)
        n += main_on(p, 0, i) && (p->step[0][i].volume & 127u) <= 31u;
    check(n == 3u && pfs.dens == 0, "DENSITY COMMIT up: its ghosts written as steps at GHOST's velocity");
    pfs.ghost = 40;
    seq_clear_track(0, p);
    goto_mode(B_HOME, M_HOME);
}
/* a generator's track, committed (the generator still on): a step's chance turned to 0 in step edit stops that step
 * (the edit reaches the live copy, which is what plays), and a held step's lock reaches it too */
static void gen_step_edit_test(void)
{
    pattern_t *pt = &pat[sq.active];
    uint32_t k, n0, n;
    printf("generator: step edits reach what plays\n");
    goto_mode(B_HOME, M_HOME);
    seq_clear_track(1, pt);
    for (k = 0; k < 16u; k += 4u)
        main_set(pt, 1, k, 1);
    pt->len[1] = 16;
    pt->gen[1][0] = GEN_MUTATE;
    pt->gen[1][1] = 0;                               /* (MUTATE at 0: the live track stays as the pattern) */
    sq.gsync |= 1u << 1;
    run_ms(20);
    seq_commit(1);
    ui.track = 1;
    run_ms(40);
    goto_mode(B_SEQ, M_STEP);
    ui.page = 1;                                     /* (0.23-t11: TRIG: Chance on K1) */
    ui.step = 4;
    run_ms(40);
    turn(EN_K1, -100);
    check(step_prob(&pt->step[1][4]) == 0u && step_prob(&gpat.step[1][4]) == 0u,
          "step edit's chance on a generator's track: the live copy too");
    n0 = trig_count[1];
    press(B_PLAY);
    run_ms(1800);                                    /* (a bar at 120 BPM: the track's 4 steps, one at chance 0) */
    press(B_PLAY);
    n = trig_count[1] - n0;
    printf("  D2: %u hits in a bar (want 3)\n", n);
    check(n == 3u, "chance 0 stops the step on a generator's track");
    goto_mode(B_EDIT, M_SOUND);
    key_hold(8);                                     /* a lock held on step 9: the live copy has it */
    turn(EN_K1, 3);
    key_up(8);
    check(gpat.step[1][8].p1n == pt->step[1][8].p1n && gpat.step[1][8].p1v == pt->step[1][8].p1v &&
          pt->step[1][8].p1n != NO_AUTOMATION, "a held step's lock on a generator's track: the live copy too");
    pt->gen[1][0] = GEN_OFF;
    seq_clear_track(1, pt);
    sq.gsync |= 1u << 1;
    ui.track = 0;
    run_ms(40);
}

/* ------------------------------------------------------- Phase 24 (0.17) --- */
/* 0.25: ENSEMBLE on any voice (CY: core 0's) through the amp EG; its knob names and cards; SN's K1 past CHIRP to it;
 * the AMP EG page heard on Plaits' engines that don't envelope themselves (P VA), not on P BD */
static void phase25_ensemble(void)
{
    uint16_t pp[4];
    char b[24];
    uint32_t k;
    printf("0.25: ENSEMBLE\n");
    goto_mode(B_HOME, M_HOME);
    ui.track = 4;
    run_ms(60);
    press(B_EDIT);
    for (k = 0; k < 12u && ui.sect != S_MODEL; k++)
        press(B_EDIT);
    check(ui.mode == M_SOUND && ui.sect == S_MODEL, "CY: EDIT to its MODEL page");
    par_set(PAR_MODEL1 + 4, MD_EN);
    run_ms(20);
    check(eno[4].on && !mi[4].type, "CY on ENSEMBLE");
    page_params(pp);
    check(!strcmp(par_name(pp[1]), "Spread") && !strcmp(par_name(pp[2]), "Warp") && !strcmp(par_name(pp[3]), "Twist") &&
          sect_half_used(4, S_MODEL, 1) && !strcmp(par_name(PAR_MPOS1 + 4u), "Balance") &&
          !strcmp(par_name(PAR_MTENS1 + 4u), "Scale"), "ENSEMBLE: its knob names, two pages");
    par_set(PAR_MDECAY1 + 4, 42);
    par_set(PAR_MDAMP1 + 4, 64);
    par_set(PAR_MMAT1 + 4, 0);
    par_set(PAR_MPOS1 + 4, 64);
    par_set(PAR_MTENS1 + 4, 70);
    card_value(b, sizeof b, PAR_MDAMP1 + 4u, 64);
    check(!strcmp(b, "CHEBY"), "ENSEMBLE: WARP's card");
    card_value(b, sizeof b, PAR_MTENS1 + 4u, 70);
    check(!strcmp(b, "MINOR"), "ENSEMBLE: SCALE's card");
    card_value(b, sizeof b, PAR_MDECAY1 + 4u, 42);
    check(!strcmp(b, "4.0 ST"), "ENSEMBLE: SPREAD's card");
    check(sect_half_used(4, S_AEG, 0), "ENSEMBLE: the AMP EG page shown");
    par_set(sect_par(4, S_OSC, 0, 0), 30);           /* (CY's COARSE: its oscillator sits near 12 kHz; down) */
    run_ms(20);
    printf("  CY: COARSE 30: %u Hz\n", (unsigned)(((uint64_t)vc[4].osc.inc * 44100u) >> 32));
    voice_trigger(4, 127, vc[4].osc.base_note);
    run_ms(30);
    check(venv[4] > 2000, "ENSEMBLE on CY sounds");
    shot("ensemble-cy");
    run_ms(3000);
    check(venv[4] < 200, "ENSEMBLE on CY: its amp EG ends it");
    /* SN: K1 from CHIRP up to ENSEMBLE */
    par_set(PAR_MODEL1 + 3, MD_MC0 + 9u);
    ui.track = 3;
    run_ms(60);
    sound_fix();
    turn(EN_K1, 1);
    check(par[PAR_MODEL1 + 3] == MD_NOISE && nzo[3].on && !eno[3].on && !mi[3].type, "SN: K1 from CHIRP to NOISE");
    turn(EN_K1, 1);
    check(par[PAR_MODEL1 + 3] == MD_EN && eno[3].on && !nzo[3].on && !mi[3].type, "SN: K1 from NOISE to ENSEMBLE");
    par_set(PAR_MODEL1 + 3, MD_LXR);
    par_set(PAR_MODEL1 + 4, MD_LXR);
    run_ms(20);
    check(!eno[3].on && !eno[4].on, "SN, CY: back to the LXR");
    /* D1: ENSEMBLE isn't one of the models the limit counts */
    ui.track = 0;
    run_ms(60);
    par_set(PAR_MODEL1, MD_EN);
    run_ms(20);
    check(eno[0].on && mdl[0].want == 0u && mdl[0].type == 0u, "D1 on ENSEMBLE: not a counted model");
    sound_fix();
    shot("ensemble-d1");
    /* the AMP EG: heard on P VA, not on P BD */
    par_set(PAR_MODEL1, MD_PL0 + PE_VA);
    run_ms(20);
    check(sect_half_used(0, S_AEG, 0), "P VA: the AMP EG page shown");
    ui.sect = S_AEG;
    ui.half = 0;
    run_ms(60);
    shot("plaits-va-aeg");
    par_set(PAR_MODEL1, MD_PL0 + PE_BD);
    run_ms(20);
    check(!sect_half_used(0, S_AEG, 0), "P BD: no AMP EG page (it envelopes itself)");
    par_set(PAR_MODEL1, MD_LXR);
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}

/* MICRO: the MODEL page on every track; on SN-HH the model alone until MICRO, K1 from LXR to CLICK past Rings' and
 * Plaits' (and back); MICRO's knob names, POSITION blank; a lock on the type; the kit keeps SN-HH off Rings' */
static void phase24(void)
{
    uint16_t pp[4];
    uint32_t k, seen = 0;
    pattern_t *pt = &pat[sq.active];
    printf("phase 24: MICRO\n");
    goto_mode(B_HOME, M_HOME);
    ui.track = 3;
    run_ms(60);
    press(B_EDIT);
    for (k = 0; k < 12u && ui.sect != S_MODEL; k++)
        press(B_EDIT);
    check(ui.mode == M_SOUND && ui.sect == S_MODEL, "SN: EDIT to its MODEL page");
    page_params(pp);
    check(pp[0] == PAR_MODEL1 + 3u && pp[1] == VP_NONE && pp[2] == VP_NONE && pp[3] == VP_NONE &&
          !sect_half_used(3, S_MODEL, 1), "SN on LXR: the model alone, one page");
    shot("micro-sn-lxr");
    turn(EN_K1, 1);
    check(par[PAR_MODEL1 + 3] == MD_MC0 && mi[3].type == 1u, "SN: K1 from LXR to CLICK (past Rings' and Plaits')");
    page_params(pp);
    check(pp[1] == PAR_MDECAY1 + 3u && !strcmp(par_name(pp[1]), "Length") && !strcmp(par_name(pp[2]), "Color") &&
          !strcmp(par_name(pp[3]), "Jitter"), "MICRO: Length, Color, Jitter");
    check(sect_par(3, S_MODEL, 1, 0) == PAR_MEXC1 + 3u && sect_par(3, S_MODEL, 1, 1) == PAR_MTENS1 + 3u &&
          !sect_par(3, S_MODEL, 1, 2) && !strcmp(par_name(PAR_MEXC1 + 3u), "Reverse") &&
          !strcmp(par_name(PAR_MTENS1 + 3u), "Tension"), "MICRO's second page: Reverse, Tension (no POSITION)");
    turn(EN_K1, 6);
    check(par[PAR_MODEL1 + 3] == MD_MC0 + 6u && mi[3].type == 7u, "SN: K1 to GRAIN");
    shot("micro-sn-grain");
    turn(EN_K1, -6);
    turn(EN_K1, -1);
    check(par[PAR_MODEL1 + 3] == MD_LXR && mi[3].type == 0u, "SN: K1 down from CLICK to LXR");
    turn(EN_K1, -1);
    check(par[PAR_MODEL1 + 3] == MD_LXR, "SN: nothing below LXR");
    /* D1 keeps the whole list */
    par_set(PAR_MODEL1, MD_LXR);
    ui.track = 0;
    run_ms(60);
    sound_fix();
    turn(EN_K1, 1);
    check(par[PAR_MODEL1] == MD_MODAL, "D1: K1 from LXR to MODAL");
    turn(EN_K1, 14 + (ELIXIR_WTABLE ? 1 : 0));       /* (STRING, FM, Plaits' 11 (1.0: past P GRAIN, NOISE, P MODAL;
                                                      * P PARTCL's number RAIN's, after CLOUD; 0.24: P WTABLE), CLICK) */
    check(par[PAR_MODEL1] == MD_MC0 && mi[0].type == 1u && mdl[0].want == 0u && mdl[0].type == 0u,
          "D1 on CLICK: MICRO, not a model the limit counts");
    page_params(pp);
    check(!strcmp(par_name(pp[1]), "Length"), "D1 on MICRO: its knob names");
    par_set(PAR_MODEL1, MD_LXR);
    /* the hats: one MODEL for CH and OH */
    ui.track = 6;
    run_ms(60);
    sound_fix();
    page_params(pp);
    check(ui.sect == S_MODEL && pp[0] == PAR_MODEL1 + 5u, "OH: the hats' MODEL");
    /* a lock on SN's type: CLICK, step 3 on GRAIN */
    ui.track = 3;
    run_ms(60);
    sound_fix();
    par_set(PAR_MODEL1 + 3, MD_MC0);
    run_ms(20);
    key_hold(2);
    turn(EN_K1, 6);
    key_up(2);
    k = autodest_to_par(lock_get(pt, 3, 2, 0)) == PAR_MODEL1 + 3u ? 0u : 1u;
    check(autodest_to_par(lock_get(pt, 3, 2, k)) == PAR_MODEL1 + 3u &&
          (k ? pt->step[3][2].p2v : pt->step[3][2].p1v) == MD_MC0 + 6u, "a step locks SN's MICRO type");
    press(B_PLAY);
    for (k = 0; k < 2200u; k += 5u) {
        run_ms(5);
        seen |= 1u << mi[3].type;
    }
    press(B_PLAY);
    check(seen & 1u << 7, "the locked step plays GRAIN");
    lock_set(pt, 3, 2, 0, NO_AUTOMATION);
    lock_set(pt, 3, 2, 1, NO_AUTOMATION);
    main_set(pt, 3, 2, 0);
    /* a kit with SN on MODAL (not its) loads it as LXR; the hats' DUST stays */
    {
        static uint8_t kb[KIT_LEN];
        kit_init_values(kb);
        kb[kit_idx(PAR_MODEL1 + 3)] = MD_MODAL;
        kb[kit_idx(PAR_MODEL1 + 5)] = MD_MC0 + 7u;
        kit_sanitize(kb);
        check(kb[kit_idx(PAR_MODEL1 + 3)] == MD_LXR && kb[kit_idx(PAR_MODEL1 + 5)] == MD_MC0 + 7u,
              "kit: SN off Rings', the hats on DUST");
    }
    if (!ELIXIR_WTABLE) {   /* 0.24: P WTABLE parked: a kit with it plays P ADD; a step lock or a morph to it, the LXR's */
        static uint8_t kb[KIT_LEN];
        kit_init_values(kb);
        kb[kit_idx(PAR_MODEL1 + 1)] = MD_PL0 + PE_WTBL;
        kit_sanitize(kb);
        check(kb[kit_idx(PAR_MODEL1 + 1)] == MD_PL0 + PE_ADD, "kit: D2 on P WTABLE (parked) loads on P ADD");
        check(!model_ok(0, MD_PL0 + PE_WTBL) && model_ok(0, MD_PL0 + PE_CHORD), "P WTABLE no model; P CHORD one");
    }
    /* a project keeps them */
    par_set(PAR_MODEL1 + 5, MD_MC0 + 2u);
    par_set(PAR_MDAMP1 + 5, 99);
    run_ms(20);
    {
        int rc = proj_save(1, "MICRO   ");
        par[PAR_MODEL1 + 5] = 0;
        par[PAR_MDAMP1 + 5] = 0;
        rc |= proj_load(1);
        run_ms(20);
        check(rc == 0 && par[PAR_MODEL1 + 5] == MD_MC0 + 2u && par[PAR_MDAMP1 + 5] == 99u && mi[5].type == 3u &&
              mi[5].col == 99u, "project: the hats' CRUNCH and its colour");
    }
    par_set(PAR_MODEL1 + 3, MD_LXR);
    par_set(PAR_MODEL1 + 5, MD_LXR);
    run_ms(20);
    ui.track = 0;
    goto_mode(B_HOME, M_HOME);
}

/* 25a: LFO and velocity targets past the LXR's: Elixir's parameters, of the LFO's voice (VEL DEST: its own). The
 * knob's order, the voice change, the names, the LFO and velocity moving them, BITS over its own range, the kit */
static void phase25a(void)
{
    uint32_t h, k, kk = 4, hh = 0, lo, hi, i;
    int32_t mn = 1 << 30, mx = -1;
    char b[24];
    uint16_t pp[4];
    printf("phase 25a: LFO and velocity targets\n");
    check(NUM_TARGETS_X == NUM_MOD_TARGETS + 27u && NUM_TARGETS_X <= 256u, "27 targets after the LXR's 229 (25b, 27a: 5)");
    goto_mode(B_HOME, M_HOME);
    ui.track = 0;
    run_ms(60);
    par_set(PAR_VOICE_LFO1, 1);                      /* (D1's LFO on D1) */
    sound_open(S_LFO, 0);
    run_ms(60);
    for (h = 0; h < 2u && kk == 4u; h++)
        for (k = 0; k < 4u; k++)
            if (sect_par(0, S_LFO, h, k) == PAR_TARGET_LFO1) {
                kk = k;
                hh = h;
                break;
            }
    check(kk < 4u, "D1's LFO page has the target");
    if (hh) {
        ui.half = 1;
        ui.force = 1;
        run_ms(60);
    }
    page_params(pp);
    target_range(PAR_TARGET_LFO1, &lo, &hi);
    card_name(b, sizeof b, PAR_TARGET_LFO1);
    check(!strcmp(b, "Dest"), "LFO 1's DEST card: Dest (0.25.2: was DstParam)");
    {                                                /* 0.25.2: the knob's order: by group as EDIT's pages, by name */
        uint8_t l[NTGT_LIST], seen[256] = {0};
        char na[32], nb[32];
        uint32_t n = tgt_list(PAR_TARGET_LFO1, l), i, ok = l[0] == 0u, ga, gb;
        for (i = 0; i < n; i++)
            ok &= !seen[l[i]]++ && (!l[i] || (l[i] >= lo && l[i] <= hi) || tgt_dly(l[i]) || (l[i] >= NUM_MOD_TARGETS && l[i] < NUM_TARGETS_X));
        check(ok && n == 1u + (hi - lo + 1u) + 4u + NXT, "D1's DEST list: OFF, then each of D1's targets once");
        for (ok = 1, i = 2; i < n; i++) {
            ga = tgt_full(na, sizeof na, 0, l[i - 1u]);
            gb = tgt_full(nb, sizeof nb, 0, l[i]);
            ok &= ga < gb || (ga == gb && !tgt_before(gb, nb, ga, na));
        }
        check(ok, "D1's DEST list: by group, each by name");
        ga = tgt_full(na, sizeof na, 0, 22);         /* (the LXR's D1 filter cutoff, Frequncy) */
        check(!strcmp(na, "Frequency") && !strcmp(tgt_group_name(ga), "FILTER"), "DEST's full name: FILTER Frequency");
        ga = tgt_full(na, sizeof na, 0, 13);         /* (the velocity's Amount) */
        check(!strcmp(na, "Velocity amount") && !strcmp(tgt_group_name(ga), "MOD EG / VELO"),
              "DEST's full name: MOD EG / VELO Velocity amount");
        ga = tgt_full(na, sizeof na, 0, NUM_MOD_TARGETS + 1u);
        check(!strcmp(na, "Reverb send") && ga == TG_MIX, "DEST's full name: MIX & SENDS Reverb send");
        for (i = 0; i < n && l[i] != 22u; i++)
            ;
        for (ga = 0; ga < n && l[ga] != 7u; ga++)    /* (AMP EG Decay) */
            ;
        for (gb = 0; gb < n && l[gb] != NUM_MOD_TARGETS + 1u; gb++)
            ;
        check(i < ga && ga < gb && gb < n, "DEST: FILTER before AMP EG, the sends after the pages");
        par_set(PAR_TARGET_LFO1, 0);
        run_ms(20);
        turn(EN_K1 + kk, -1);
        check(par[PAR_TARGET_LFO1] == 0u, "K below OFF: OFF");
        turn(EN_K1 + kk, 1);
        check(par[PAR_TARGET_LFO1] == l[1], "K up from OFF: the list's first");
        turn(EN_K1 + kk, 3);
        check(par[PAR_TARGET_LFO1] == l[4], "K: the list in its order");
        {                                            /* 1.0: on past D1's into the other voices' (Voice set) */
            uint8_t l2[NTGT_LIST];
            uint32_t n6, j;
            tgt_list_v(1, l2);
            par_set(PAR_TARGET_LFO1, l[n - 1u]);
            run_ms(20);
            turn(EN_K1 + kk, 1);
            check(par[PAR_TARGET_LFO1] == l2[1] && par[PAR_VOICE_LFO1] == 2u, "1.0: K on past D1's last: D2's first");
            par_format(b, PAR_TARGET_LFO1, par[PAR_TARGET_LFO1], sizeof b);
            check(!strncmp(b, "D2 ", 3), "1.0: another voice's Dest: its voice first");
            turn(EN_K1 + kk, -1);
            check(par[PAR_TARGET_LFO1] == l[n - 1u] && par[PAR_VOICE_LFO1] == 1u, "1.0: K back: D1's last");
            for (j = 0; j < 30u; j++)
                turn(EN_K1 + kk, 100);
            n6 = tgt_list_v(5, l2);
            check(par[PAR_TARGET_LFO1] == l2[n6 - 1u] && par[PAR_VOICE_LFO1] == 6u, "1.0: K to HH's last, no further");
            for (j = 0; j < 30u; j++)
                turn(EN_K1 + kk, -100);
            check(par[PAR_TARGET_LFO1] == 0u && par[PAR_VOICE_LFO1] == 1u, "1.0: K all the way back: OFF, its own voice");
        }
        par_set(PAR_TARGET_LFO1, 22);
        run_ms(20);
        shot("lfo-dest-filter");
    }
    par_format(b, PAR_TARGET_LFO1, NUM_MOD_TARGETS, sizeof b);
    check(!strcmp(b, "FX"), "its name: FX");
    par_set(PAR_TARGET_LFO1, NUM_MOD_TARGETS + 1u);  /* REV send */
    run_ms(20);
    shot("lfo-target-rev");
    vp_set(PAR_VOICE_LFO1, 4);                       /* (to SN: the target goes with it) */
    run_ms(20);
    check(par[PAR_TARGET_LFO1] == NUM_MOD_TARGETS + 1u && lfo_mod[0].dest == PAR_REV_SEND1 + 3u,
          "the LFO's voice to SN: SN's reverb send");
    /* the LFO moves SN's reverb send */
    par_set(PAR_REV_SEND1 + 3, 64);
    par_set(PAR_FREQ_LFO1, 100);
    par_set(PAR_AMOUNT_LFO1, 100);
    par_set(PAR_WAVE_LFO1, 0);
    for (i = 0; i < 100u; i++) {
        run_ms(5);
        mn = vc[3].rev_send < mn ? vc[3].rev_send : mn;
        mx = vc[3].rev_send > mx ? vc[3].rev_send : mx;
    }
    check(mx - mn > 8000, "LFO 1 sweeps SN's reverb send");
    par_set(PAR_AMOUNT_LFO1, 0);
    par_set(PAR_TARGET_LFO1, 0);
    vp_set(PAR_VOICE_LFO1, 1);
    run_ms(20);
    /* velocity: D2's own MICRO length; BITS over 0..15 */
    par_set(PAR_VEL_DEST_1 + 1, NUM_MOD_TARGETS + 9u);
    par_set(PAR_VELO_MOD_AMT_1 + 1, 127);
    par_set(PAR_MDECAY1 + 1, 64);
    run_ms(20);
    check(velo_mod[1].dest == PAR_MDECAY1 + 1u, "VEL DEST: D2's own model knob 1");
    voice_trigger(1, 127, SEQ_DEFAULT_NOTE);
    run_ms(10);
    k = mi[1].len;
    voice_trigger(1, 1, SEQ_DEFAULT_NOTE);
    run_ms(10);
    check(k > 120u && mi[1].len < 10u, "velocity moves D2's LENGTH");
    par_set(PAR_BITS1 + 1, 8);
    par_set(PAR_VEL_DEST_1 + 1, NUM_MOD_TARGETS + 3u);
    par_set(PAR_VELO_MOD_AMT_1 + 1, 30);
    voice_trigger(1, 64, SEQ_DEFAULT_NOTE);
    run_ms(10);
    check(vc[1].bits >= 7u && vc[1].bits <= 8u, "BITS: a middle velocity leaves it near 8 (its own range)");
    voice_trigger(1, 127, SEQ_DEFAULT_NOTE);
    run_ms(10);
    check(vc[1].bits > 8u, "BITS: a hard hit more");
    par_format(b, PAR_VEL_DEST_1 + 1, NUM_MOD_TARGETS + 3u, sizeof b);
    check(!strcmp(b, "Bits"), "its name: Bits");
    par_set(PAR_VEL_DEST_1 + 1, 0);
    par_set(PAR_VELO_MOD_AMT_1 + 1, 0);
    par_set(PAR_BITS1 + 1, 0);
    /* a kit keeps Elixir's targets, a byte past them is OFF */
    {
        static uint8_t kb[KIT_LEN];
        kit_init_values(kb);
        kb[PAR_TARGET_LFO1 + 2] = (uint8_t)(NUM_MOD_TARGETS + 5u);
        kb[PAR_VEL_DEST_1 + 2] = (uint8_t)NUM_TARGETS_X;
        kit_sanitize(kb);
        check(kb[PAR_TARGET_LFO1 + 2] == NUM_MOD_TARGETS + 5u && kb[PAR_VEL_DEST_1 + 2] == 0u,
              "kit: Elixir's target kept, one past them OFF");
    }
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}

/* 25b: LFO 2 on every track: the page, the target, a sweep, adding to LFO 1, morph, a sound moved, the round trips */
static void phase25b(void)
{
    uint32_t i, lo, hi, n;
    int32_t mn = 1 << 30, mx = -1;
    char b[24];
    uint16_t pp[4];
    uint32_t pg, np;
    uint8_t sv[SOUND_MAX], kb[KIT_LEN], k2[KIT_LEN];
    printf("phase 25b: LFO 2 on every track\n");
    goto_mode(B_HOME, M_HOME);
    ui.track = 1;
    run_ms(40);
    sound_open(S_LFO2, 0);
    run_ms(40);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(ui.sect == S_LFO2 && pp[0] == PAR_L2TGT1 + 1u && pp[1] == PAR_L2AMT1 + 1u && pp[2] == PAR_L2SYNC1 + 1u &&
          pp[3] == PAR_L2RATE1 + 1u && !strcmp(b, "LFO 2 (1/2)"), "LFO 2: D2's dest, depth, sync, rate (1.0)");
    ui.half = 1;
    ui.force = 1;
    run_ms(40);
    page_params(pp);
    check(pp[0] == PAR_L2WAVE1 + 1u && pp[1] == PAR_L2OFS1 + 1u && pp[2] == PAR_L2MODE1 + 1u && pp[3] == PAR_L2RTRG1 + 1u,
          "its second page: wave, phase, mode, retrigger (1.0)");
    ui.half = 0;
    ui.force = 1;
    run_ms(40);
    target_range(PAR_L2TGT1 + 1, &lo, &hi);
    check(lo == MOD_TARGET_VOICE[1][0] && hi == MOD_TARGET_VOICE[1][1], "its targets: D2's own");
    card_name(b, sizeof b, PAR_L2TGT1 + 1);
    check(!strcmp(b, "Dest"), "LFO 2's DEST card: Dest (0.25.2: was Target)");
    {
        uint8_t l[NTGT_LIST];
        char nm[32];
        tgt_list(PAR_L2TGT1 + 1, l);
        par_set(PAR_L2TGT1 + 1, 0);
        run_ms(20);
        turn(EN_K1, 1);
        check(par[PAR_L2TGT1 + 1] == l[1], "K1 up from OFF: D2's first target (1.0: Dest on K1)");
        par_set(PAR_L2TGT1 + 1, NUM_MOD_TARGETS);
        run_ms(20);
        check(lfo2_mod[1].dest == PAR_FX_SEND1 + 1u, "D2's FX send");
        turn(EN_K1, 1);
        run_ms(20);
        tgt_full(nm, sizeof nm, 1, par[PAR_L2TGT1 + 1]);
        check(!strcmp(nm, "Pan"), "K1 on from D2's FX send: Pan (MIX & SENDS by name)");
    }
    par_format(b, PAR_L2TGT1 + 1, NUM_MOD_TARGETS + 2u, sizeof b);
    check(!strcmp(b, "Tilt"), "its name: Tilt");
    /* LFO 2 sweeps D2's tilt */
    par_set(PAR_TILT1 + 1, 64);
    par_set(PAR_L2TGT1 + 1, NUM_MOD_TARGETS + 2u);
    par_set(PAR_L2RATE1 + 1, 100);
    par_set(PAR_L2AMT1 + 1, 100);
    run_ms(20);
    shot("lfo2-tilt");
    for (i = 0; i < 100u; i++) {
        run_ms(5);
        mn = vc[1].tilt < mn ? vc[1].tilt : mn;
        mx = vc[1].tilt > mx ? vc[1].tilt : mx;
    }
    check(mx - mn > 40, "LFO 2 sweeps D2's tilt");
    /* with LFO 1 on the same target: LFO 2 adds to it (both held at their top: square waves part way round) */
    par_set(PAR_TILT1 + 1, 20);
    par_set(PAR_VOICE_LFO1 + 1, 2);
    par_set(PAR_TARGET_LFO1 + 1, NUM_MOD_TARGETS + 2u);
    par_set(PAR_AMOUNT_LFO1 + 1, 40);
    par_set(PAR_L2AMT1 + 1, 50);
    par_set(PAR_WAVE_LFO1 + 1, LFO_REC);
    par_set(PAR_L2WAVE1 + 1, LFO_REC);
    par_set(PAR_FREQ_LFO1 + 1, 0);
    par_set(PAR_L2RATE1 + 1, 0);
    run_ms(20);
    vc[1].lfo.phase = vc[1].lfo2.phase = 0x90000000u;
    run_ms(5);
    n = vc[1].tilt;
    par_set(PAR_L2AMT1 + 1, 0);
    run_ms(5);
    check(n >= 108u && n <= 112u && vc[1].tilt >= 58u && vc[1].tilt <= 62u, "LFO 1 and LFO 2 on D2's tilt: they add");
    par_set(PAR_AMOUNT_LFO1 + 1, 0);
    par_set(PAR_TARGET_LFO1 + 1, 0);
    par_set(PAR_WAVE_LFO1 + 1, 0);
    par_set(PAR_TILT1 + 1, 64);
    /* morph: LFO 2's switches at 50%; with them different, its amount down to 0 there and up to the other's */
    for (i = 0; i < KIT_LEN; i++)
        morph_par[i] = par[kit_par(i)];
    par[PAR_L2TGT1 + 1] = (uint8_t)(NUM_MOD_TARGETS + 2u);
    morph_par[kit_idx(PAR_L2TGT1 + 1)] = (uint8_t)(NUM_MOD_TARGETS + 1u);
    check(morph_mix(PAR_L2AMT1 + 1, 100, 80, 64) == 50 && morph_mix(PAR_L2AMT1 + 1, 100, 80, 128) == 0 &&
          morph_mix(PAR_L2AMT1 + 1, 100, 80, 255) == 80 &&
          morph_mix(PAR_L2TGT1 + 1, NUM_MOD_TARGETS + 2u, NUM_MOD_TARGETS + 1u, 127) == NUM_MOD_TARGETS + 2u &&
          morph_mix(PAR_L2TGT1 + 1, NUM_MOD_TARGETS + 2u, NUM_MOD_TARGETS + 1u, 128) == NUM_MOD_TARGETS + 1u,
          "morph: the target switches at 50%, the amount through 0");
    morph_par[kit_idx(PAR_L2TGT1 + 1)] = (uint8_t)(NUM_MOD_TARGETS + 2u);
    check(morph_mix(PAR_L2AMT1 + 1, 100, 80, 128) == morph_lerp(100, 80, 128), "morph: the same switches: as any knob");
    /* a sound from D2 on D3: LFO 2's target (one of D2's) and its retrigger (D2) become D3's */
    par_set(PAR_L2TGT1 + 1, MOD_TARGET_VOICE[1][0] + 3u);
    par_set(PAR_L2RTRG1 + 1, 2);
    run_ms(10);
    n = voice_get_par(1, sv);
    voice_put_par(2, sv, n, 1);
    run_ms(10);
    printf("  a drum's sound: %u values (SOUND_MAX %u)\n", n, SOUND_MAX);
    check(n < SOUND_MAX && par[PAR_L2TGT1 + 2] == MOD_TARGET_VOICE[2][0] + 3u && par[PAR_L2RTRG1 + 2] == 3u &&
          lfo2_mod[2].dest == MOD_TARGETS[MOD_TARGET_VOICE[2][0] + 3u][1], "a sound from D2 on D3: LFO 2's D3's");
    /* the project and the library keep LFO 2 and the insert */
    par_set(PAR_L2RATE1 + 3, 77);
    par_set(PAR_L2SYNC1 + 3, 5);
    par_set(PAR_L2OFS1 + 3, 33);
    par_set(PAR_INS1 + 3, INS_RING);
    par_set(PAR_INSC1 + 3, 99);
    run_ms(10);
    {
        int rc = proj_save(1, "L2INS   ");
        par_set(PAR_L2RATE1 + 3, 1);
        par_set(PAR_INS1 + 3, 0);
        par_set(PAR_INSC1 + 3, 1);
        rc |= proj_load(1);
        run_ms(10);
        check(rc == 0 && par[PAR_L2RATE1 + 3] == 77 && par[PAR_L2SYNC1 + 3] == 5 && par[PAR_L2OFS1 + 3] == 33 &&
              par[PAR_INS1 + 3] == INS_RING && par[PAR_INSC1 + 3] == 99 && ins[3].type == INS_RING,
              "a project keeps LFO 2 and the insert");
        rc = libkit_save(5, "L2KIT   ", 0);
        par_set(PAR_L2RATE1 + 3, 1);
        par_set(PAR_INS1 + 3, 0);
        rc |= libkit_load(5, 0);
        run_ms(10);
        check(rc == 0 && par[PAR_L2RATE1 + 3] == 77 && par[PAR_INS1 + 3] == INS_RING, "a library kit keeps them");
        for (i = 0; i < KIT_LEN; i++)                /* a library kit from before them: their init values */
            kb[i] = par[kit_par(i)];
        rc = kl_write(KLK_KIT, 6, "OLD     ", kb, 398);
        rc |= libkit_get(6, k2);
        check(rc == 0 && k2[kit_idx(PAR_L2RATE1 + 3)] == 64 && k2[kit_idx(PAR_L2AMT1 + 1)] == 0 &&
              k2[kit_idx(PAR_INS1 + 3)] == INS_OFF && k2[kit_idx(PAR_INSA1 + 3)] == 64 &&
              k2[kit_idx(PAR_MC_MIX)] == kb[kit_idx(PAR_MC_MIX)], "a kit from 0.18-test4: LFO 2 and the insert at init");
        kl_write(KLK_KIT, 5, "", 0, 0);
        kl_write(KLK_KIT, 6, "", 0, 0);
    }
    for (i = PAR_L2RATE1; i < NPAR; i++)
        par_set(i, init_kit_k(i));
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}

/* 27a: the INSERT: its page on the filter button, the type's knob names and values, morph, sound */
static void phase27a(void)
{
    uint16_t pp[4];
    char b[24];
    uint32_t pg, np, i;
    int32_t pk = 0;
    printf("phase 27a: the insert\n");
    goto_mode(B_HOME, M_HOME);
    ui.track = 3;
    run_ms(40);
    press(B_SCL);
    for (i = 0; i < 6u && ui.sect != S_INSERT; i++)
        press(B_SCL);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(ui.sect == S_INSERT && pp[0] == PAR_INS1 + 3u && pp[1] == PAR_INSA1 + 3u && pp[2] == PAR_INSB1 + 3u &&
          pp[3] == PAR_INSC1 + 3u && !strcmp(b, "INSERT FX"), "the filter button: FILTER, SHAPE, then INSERT");
    press(B_SCL);
    check(ui.sect == S_FILTER, "again: back to FILTER");
    sound_open(S_INSERT, 0);
    run_ms(20);
    turn(EN_K1, 1);                                  /* (1.0: OFF, SHIFTER, RING: the dropped types passed) */
    run_ms(20);
    par_format(b, PAR_INSB1 + 3, 127, sizeof b);
    check(par[PAR_INS1 + 3] == INS_SHIFT && ins[3].type == INS_SHIFT && !strcmp(par_name(PAR_INSB1 + 3), "Shift") &&
          !strcmp(b, "+500Hz"), "K1 from OFF: SHIFTER (Shift, +500Hz at 127)");
    par_format(b, PAR_INSB1 + 3, 1, sizeof b);
    check(!strcmp(b, "-500Hz"), "..-500Hz at 1");
    par_format(b, PAR_INSC1 + 3, 64, sizeof b);
    check(!strcmp(par_name(PAR_INSC1 + 3), "Feedback") && !strcmp(b, "0"), "SHIFTER: Feedback (64: none)");
    shot("insert-shifter");
    turn(EN_K1, 1);
    run_ms(20);
    par_format(b, PAR_INSB1 + 3, 64, sizeof b);
    printf("  RING at 64: %s\n", b);
    check(par[PAR_INS1 + 3] == INS_RING && !strcmp(par_name(PAR_INSC1 + 3), "AM") && !strcmp(b, "230Hz"),
          "RING: Pitch in Hz, AM");
    shot("insert-ring");
    turn(EN_K1, 3);
    run_ms(20);
    check(par[PAR_INS1 + 3] == INS_RING, "RING the last");
    turn(EN_K1, -1);
    run_ms(20);
    check(par[PAR_INS1 + 3] == INS_SHIFT, "back: SHIFTER");
    /* the snare through the shifter at full amount: it sounds, and the lock list has the knobs */
    par_set(PAR_INSA1 + 3, 127);
    voice_trigger(3, 127, SEQ_DEFAULT_NOTE);
    for (i = 0; i < 40u; i++) {
        run_ms(1);
        pk = venv[3] > pk ? venv[3] : pk;
    }
    check(pk > 1000, "SN through the SHIFTER");
    check(par_lockable(PAR_INSB1 + 3) && par_lockable(PAR_INS1 + 3), "the insert's knobs lock");
    turn(EN_K1, -1);
    run_ms(20);
    check(par[PAR_INS1 + 3] == INS_OFF, "back: OFF (past the dropped types)");
    par_set(PAR_INS1 + 3, INS_TREMOLO);              /* (a lock or CC with a dropped type: OFF) */
    run_ms(5);
    par_format(b, PAR_INS1 + 3, INS_TREMOLO, sizeof b);
    check(ins[3].type == INS_OFF && !strcmp(b, "OFF"), "a dropped type plays and reads OFF");
    par_set(PAR_INS1 + 3, INS_SHIFT);
    /* morph: the type switches at 50%, AMOUNT through 0 when the types differ */
    for (i = 0; i < KIT_LEN; i++)
        morph_par[i] = par[kit_par(i)];
    morph_par[kit_idx(PAR_INS1 + 3)] = INS_RING;
    check(morph_mix(PAR_INS1 + 3, INS_SHIFT, INS_RING, 127) == INS_SHIFT &&
          morph_mix(PAR_INS1 + 3, INS_SHIFT, INS_RING, 128) == INS_RING &&
          morph_mix(PAR_INSA1 + 3, 127, 100, 128) == 0 && morph_mix(PAR_INSA1 + 3, 127, 100, 255) == 100 &&
          morph_mix(PAR_INSB1 + 3, 0, 100, 128) == morph_lerp(0, 100, 128), "morph: the type at 50%, AMOUNT through 0");
    /* the kit check: a type past the list is OFF */
    {
        uint8_t kb[KIT_LEN];
        kit_init_values(kb);
        kb[kit_idx(PAR_INS1 + 2)] = NINS;
        kit_sanitize(kb);
        check(kb[kit_idx(PAR_INS1 + 2)] == INS_OFF, "kit: an insert type past the list: OFF");
    }
    for (i = PAR_INS1; i < NPAR; i++)
        par_set(i, init_kit_k(i));
    run_ms(20);
    /* P CHORD: its first knob is named Chord and shows the chord the engine plays */
    par_set(PAR_MODEL1, MD_PL0 + PE_CHORD);
    par_set(PAR_MDECAY1, 0);
    run_ms(20);
    {
        char c0[12], c1[12], c2[12];
        par_format(c0, PAR_MDECAY1, 0, sizeof c0);
        par_set(PAR_MDECAY1, 127);
        run_ms(20);
        par_format(c1, PAR_MDECAY1, 127, sizeof c1);
        par_set(PAR_MDECAY1, 50);
        run_ms(20);
        par_format(c2, PAR_MDECAY1, 50, sizeof c2);
        printf("  P CHORD: 0 %s, 50 %s (plays %s), 127 %s\n", c0, c2, PL_CHORD_NAME[pl[0].u.ch.chord], c1);
        check(!strcmp(par_name(PAR_MDECAY1), "Chord") && !strcmp(c0, "OCT") && !strcmp(c1, "M") &&
              !strcmp(c2, PL_CHORD_NAME[pl[0].u.ch.chord]), "P CHORD: the knob names the chord it plays");
    }
    par_set(PAR_MODEL1, 0);
    par_set(PAR_MDECAY1, init_kit_k(PAR_MDECAY1));
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}

/* 26: GROOVE. The hats' 16 steps over two bars: the block each hit lands in and its velocity */
static uint32_t gr_rec(uint32_t *at, int32_t *vel, uint32_t max)
{
    uint32_t n = 0, b, c = trig_count[5];
    seq_start(1);                                    /* (two bars at 120, 4 s, less half a step) */
    for (b = 0; b < 4u * FS / BLOCK - 43u; b++) {
        web_render(BLOCK);
        if (trig_count[5] != c) {
            c = trig_count[5];
            if (n < max) {
                at[n] = b;
                vel[n] = vc[V_HAT].velo;
            }
            n++;
        }
    }
    seq_stop_now();
    run_ms(200);
    return n;
}
static int gr_near(const uint32_t *at, uint32_t n, double want)   /* the hit nearest a block, its index */
{
    uint32_t j;
    int best = 0;
    for (j = 1; j < n && j < 40u; j++)
        if (fabs(at[j] - want) < fabs(at[best] - want))
            best = (int)j;
    return best;
}
static int gr_same(const uint32_t *a, const int32_t *v, const uint32_t *b, const int32_t *w, uint32_t n)
{                                                    /* (a run starts anywhere in a block: within one) */
    uint32_t k;
    for (k = 0; k < n; k++)
        if ((a[k] > b[k] ? a[k] - b[k] : b[k] - a[k]) > 1u || v[k] != w[k])
            return 0;
    return 1;
}
static void phase26(void)
{
    static uint32_t a0[40], a1[40];
    static int32_t v0[40], v1[40];
    const double tick_blk = FS * 60.0 / 120.0 / 96.0 / BLOCK;   /* (a tick in blocks at 120) */
    pattern_t *p = &pat[sq.active];
    uint32_t k, n0, n1, i, ok, same, diff;
    uint16_t pp[4];
    char b[24];
    uint32_t pg, np;
    printf("phase 26: GROOVE\n");
    seq_clear_pattern(p);
    check(p->groove == 0 && p->gamt == GAMT_DEFAULT && !p->ghum && !p->gskip, "a cleared pattern: no groove");
    par_set(PAR_BPM, 120);
    p->shuffle = 0;
    par[PAR_SHUFFLE] = 0;
    for (k = 0; k < 16u; k++) {
        main_set(p, 5, k, 1);
        p->step[5][k].volume = 0x80u | 100u;
    }
    p->len[5] = 16;
    /* the page */
    seq_goto(PG_GROOVE);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[0] == VP_GROOVE && pp[1] == VP_GAMT && pp[2] == VP_GHUM && pp[3] == VP_GFOLLOW && !strcmp(b, "GROOVE"),
          "ARP: the GROOVE page after SCALE");
    p->gamt = 0;
    turn(EN_K1, 4);
    check(p->groove == 4 && p->gamt == GAMT_DEFAULT, "K1: SW 66%, the amount from 0 to 100");
    shot("groove-page");
    p->groove = 0;
    n0 = gr_rec(a0, v0, 40);
    check(n0 == 32, "no groove: 32 hits in two bars");
    /* each template at 100: its offsets in ticks and its velocities, step by step (the first bar) */
    for (i = 1; i <= NGROOVE; i++) {
        p->groove = (uint8_t)i;
        p->gamt = 100;
        n1 = gr_rec(a1, v1, 40);
        ok = n1 >= 31u;                              /* (a first step early can't play before the start) */
        for (k = 1; ok && k < 16u; k++) {
            double w = GROOVE_T[i - 1][k];
            int j = gr_near(a1, n1, a0[k] + w * tick_blk);
            double d = ((double)a1[j] - a0[k]) / tick_blk;
            int32_t wv = clamp32(100 + GROOVE_V[i - 1][k], 1, 127);
            ok = fabs(d - w) <= 1.0 && v1[j] == (int32_t)(wv * 32768u / 127u);
            if (!ok)
                printf("  groove %u step %u: %.1f ticks (want %.0f), velocity %d (want %d)\n", i, k, d, w,
                       v1[j] * 127 / 32768, wv);
        }
        check(ok, "a groove at 100: its ticks and velocities");
    }
    /* PUSH: the bar's first step early, before the loop ends (the next round's) */
    p->groove = 10;
    n1 = gr_rec(a1, v1, 40);
    {
        int j = gr_near(a1, n1, a0[16] - 3 * tick_blk);
        check(fabs(((double)a1[j] - a0[16]) / tick_blk + 3.0) <= 1.0 && a1[j] < a0[16],
              "PUSH: step 1 of the next round early");
    }
    /* amount 0 is no groove, bit for bit; a track that doesn't follow too */
    p->groove = 7;
    p->gamt = 0;
    n1 = gr_rec(a1, v1, 40);
    check(n1 == n0 && gr_same(a1, v1, a0, v0, n0), "amount 0: as no groove");
    p->gamt = 100;
    ui.track = 5;
    turn(EN_K4, -1);
    check(p->gskip == 1u << 5, "FOLLOW OFF: CH left out");
    n1 = gr_rec(a1, v1, 40);
    check(n1 == n0 && gr_same(a1, v1, a0, v0, n0), "CH not following: as no groove");
    turn(EN_K4, 1);
    /* half the amount: half the ticks */
    p->groove = 4;
    p->gamt = 50;
    n1 = gr_rec(a1, v1, 40);
    check(n1 == 32 && fabs(((double)(a1[1] - a1[0]) - (a0[1] - a0[0])) / tick_blk - 4.0) <= 1.0 &&
          (a1[0] > a0[0] ? a1[0] - a0[0] : a0[0] - a1[0]) <= 1u, "SW 66% at 50: 4 ticks");   /* (a run starts
                                                      * anywhere in a block) */
    /* with SHUFFLE too: every hit still plays */
    p->shuffle = 60;
    par[PAR_SHUFFLE] = 60;
    n1 = gr_rec(a1, v1, 40);
    check(n1 == 32, "SHUFFLE and a groove: 32 hits");
    p->shuffle = 0;
    par[PAR_SHUFFLE] = 0;
    /* HUMANIZE: within 3 ticks and 16 of velocity, the bars different, the same step's checks agreeing (32 hits) */
    p->groove = 0;
    p->ghum = 127;
    n1 = gr_rec(a1, v1, 40);
    ok = n1 == 32;
    same = diff = 0;
    for (k = 0; ok && k < 32u; k++) {
        double d = ((double)a1[k] - a0[k]) / tick_blk;
        ok = fabs(d) <= 4.0 && iabs(v1[k] - v0[k]) <= 17 * 32768 / 127;
        if (k < 16u)
            (a1[k] - a0[k] == a1[k + 16] - a0[k + 16] && v1[k] == v1[k + 16]) ? same++ : diff++;
    }
    check(ok && diff > 4u, "HUMANIZE 127: hits within 3 ticks, velocity within 16, each bar its own");
    /* a project keeps it */
    p->groove = 8;
    p->gamt = 90;
    p->ghum = 20;
    p->gskip = 1u << 2;
    {
        int rc = proj_save(1, "GROOVE  ");
        p->groove = p->gamt = p->ghum = p->gskip = 0;
        rc |= proj_load(1);
        p = &pat[sq.active];
        check(rc == 0 && p->groove == 8 && p->gamt == 90 && p->ghum == 20 && p->gskip == 1u << 2,
              "project: the groove, amount, HUMANIZE, follow");
    }
    seq_clear_pattern(p);
    goto_mode(B_HOME, M_HOME);
}

/* 29: the pages tidied: the sound pages in a synth's order, MODEL on LXR alone, LFO 1 as LFO 2, titles with their
 * page of the section's pages */
static void phase29(void)
{
    static const uint8_t ORD[] = {S_MODEL, S_OSC, S_FM, S_CLICK, S_FILTER, S_SHAPE, S_INSERT, S_AEG, S_GATE, S_MOD,
                                  S_LFO, S_LFOM, S_LFO2, S_RANDOM, S_XMOD, S_SENDS};
    uint8_t l[NSPG][2];
    uint16_t pp[4];
    char b[40];
    uint32_t i, j, n, pg, np, ok = 1;
    printf("phase 29: the pages\n");
    goto_mode(B_HOME, M_HOME);
    black(0);
    par_set(PAR_MODEL1, 0);
    run_ms(20);
    n = sound_pages(l);
    for (i = j = 0; i < n; i++) {                    /* (the sections in this order, each once in a row) */
        while (j < sizeof ORD && ORD[j] != l[i][0])
            j++;
        ok &= j < sizeof ORD && l[i][0] != S_MIX;
    }
    check(ok && l[0][0] == S_MODEL && l[n - 1u][0] == S_SENDS, "the sound pages: MODEL first, FX TRACK SENDS last (1.0 Phase 16), no MIX");
    sound_open(S_MODEL, 0);
    run_ms(20);
    page_params(pp);
    check(pp[0] == PAR_MODEL1 && pp[1] == VP_NONE && !sect_half_used(0, S_MODEL, 1), "MODEL on LXR: the model alone");
    turn(EN_K1, 1);
    run_ms(20);
    page_params(pp);
    check(par[PAR_MODEL1] == 1u && pp[1] != VP_NONE && sect_half_used(0, S_MODEL, 1), "MODEL on MODAL: Rings' knobs");
    turn(EN_K1, -1);
    run_ms(20);
    check(!sect_half_used(0, S_MODEL, 1), "back on LXR: the model alone again");
    sound_open(S_LFO, 0);
    run_ms(20);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[0] == PAR_TARGET_LFO1 && pp[1] == PAR_AMOUNT_LFO1 && pp[2] == PAR_SYNC_LFO1 && pp[3] == PAR_FREQ_LFO1 &&
          !strcmp(b, "LFO 1 (1/2)"), "1.0: LFO 1 (1/2): dest, depth, sync, rate");
    {
        static const char *const NM[4] = {"Dest", "Depth", "Sync", "Rate"};
        uint32_t k, ok = 1;
        for (k = 0; k < 4u; k++) {
            card_name(b, sizeof b, pp[k]);
            ok &= !strcmp(b, NM[k]);
        }
        check(ok, "1.0: LFO 1's names: Dest, Depth, Sync, Rate");
    }
    turn(EN_SELECT, 1);
    page_params(pp);
    title_text(b, sizeof b, &pg, &np);
    check(pp[0] == PAR_WAVE_LFO1 && pp[1] == PAR_OFFSET_LFO1 && pp[2] == PAR_LFOMODE1 && pp[3] == PAR_RETRIGGER_LFO1 &&
          !strcmp(b, "LFO 1 (2/2)"), "1.0: LFO 1 (2/2): wave, phase, mode, retrigger");
    {
        static const char *const NM[4] = {"Wave", "Phase", "Mode", "Retrigger"};
        uint32_t k, ok = 1;
        for (k = 0; k < 4u; k++) {
            card_name(b, sizeof b, pp[k]);
            ok &= !strcmp(b, NM[k]);
        }
        check(ok, "1.0: LFO 1's names: Wave, Phase, Mode, Retrigger");
    }
    turn(EN_SELECT, 1);
    check(ui.sect == S_LFO2 && ui.half == 0, "1.0: after LFO 1's two pages, LFO 2 (no MODE page)");
    sound_open(S_MOD, 0);
    run_ms(20);
    title_text(b, sizeof b, &pg, &np);
    check(!strcmp(b, "MOD EG / VELO (1/2)"), "a section of two pages: (1/2) in its title");
    sound_open(S_GATE, 0);
    run_ms(20);
    title_text(b, sizeof b, &pg, &np);
    check(!strcmp(b, "GATE"), "a section of one page: no count");
    goto_mode(B_HOME, M_HOME);
}

/* 28: Elixir's filter types after the LXR's OFF: the names, K-turns from OFF up to CHORD on the FILTER page, the
 * voice's type, a project round trip; an LXR kit's 0..7 keep their meaning */
static void phase28(void)
{
    static const char *const NM[] = {"LP4", "COMB+", "COMB-", "ALLPASS", "VOWEL", "DUB BP", "CHORD"};
    uint16_t pp[4];
    uint32_t k, h, kk = 4;
    char b[16];
    printf("phase 28: filter types\n");
    check(par_max(PAR_FILTER_TYPE_1) == F_LAST - 1u, "the filter type runs to CHORD");
    for (k = 0; k < 7u; k++) {
        par_format(b, PAR_FILTER_TYPE_1, F_OFF + k, sizeof b);
        check(!strcmp(b, NM[k]), NM[k]);
    }
    par_format(b, PAR_FILTER_TYPE_1, F_OFF - 1u, sizeof b);
    check(!strcmp(b, "OFF"), "OFF where it was");
    goto_mode(B_HOME, M_HOME);
    ui.track = 0;
    run_ms(60);
    sound_open(S_FILTER, 0);
    run_ms(60);
    for (h = 0; h < 2u && kk == 4u; h++)
        for (k = 0; k < 4u; k++)
            if (sect_par(0, S_FILTER, h, k) == PAR_FILTER_TYPE_1) {
                kk = k;
                break;
            }
    check(kk < 4u, "D1's FILTER page has the type");
    {   /* 1.0: FILTER's first page Type, Freq, Reso, Drive, named so on the cards; the flash parts both known */
        char c[4][16];
        uint32_t q;
        for (q = 0; q < 4u; q++)
            card_name(c[q], sizeof c[q], sect_par(0, S_FILTER, 0, q));
        check(sect_par(0, S_FILTER, 0, 0) == PAR_FILTER_TYPE_1 && sect_par(0, S_FILTER, 0, 1) == PAR_FILTER_FREQ_1 &&
                  sect_par(0, S_FILTER, 0, 2) == PAR_RESO_1 && sect_par(0, S_FILTER, 0, 3) == PAR_FILTER_DRIVE_1 &&
                  sect_par(5, S_FILTER, 0, 1) == PAR_FILTER_FREQ_6 && !strcmp(c[0], "Type") && !strcmp(c[1], "Freq") &&
                  !strcmp(c[2], "Reso") && !strcmp(c[3], "Drive"),
              "FILTER: Type, Freq, Reso, Drive");
        check(FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) && !FL_JEDEC_OK(0xEF4014u) && !FL_JEDEC_OK(0u),
              "flash: the P25Q80 and the UC25HQ80 known, others not");
    }
    if (h == 2u) {
        ui.half = 1;
        ui.force = 1;
        run_ms(60);
    }
    page_params(pp);
    par_set(PAR_FILTER_TYPE_1, F_OFF - 1u);
    run_ms(20);
    turn(EN_K1 + kk, 1);
    check(par[PAR_FILTER_TYPE_1] == F_LP4 - 1u && vc[0].ftype == F_LP4, "K from OFF to LP4");
    turn(EN_K1 + kk, 1);
    check(par[PAR_FILTER_TYPE_1] == F_COMBP - 1u && vc[0].ftype == F_COMBP, "K to COMB+");
    shot("filter-comb");
    turn(EN_K1 + kk, 2);
    check(par[PAR_FILTER_TYPE_1] == F_APF - 1u, "K to ALLPASS");
    shot("filter-allpass");
    turn(EN_K1 + kk, 3);
    check(par[PAR_FILTER_TYPE_1] == F_RESO - 1u && vc[0].ftype == F_RESO, "K up to CHORD");
    turn(EN_K1 + kk, 10);
    check(par[PAR_FILTER_TYPE_1] == F_LAST - 1u && vc[0].ftype == F_LADDER, "K up to LADDER, no further");
    shot("filter-ladder");
    /* 1.0: FILTER X: the character types' second page, named by type (ENV AMT, LADDER's BASS COMP); none on the others */
    check(sect_half_used(0, S_FILTER, 1) && sect_par(0, S_FILTER, 1, 0) == PAR_FLTX1 && !strcmp(par_name(PAR_FLTX1), "Bass comp"),
          "1.0: LADDER: BASS COMP on FILTER's second page");
    par_set(PAR_FILTER_TYPE_1, F_ACID - 1u);
    run_ms(20);
    check(sect_par(0, S_FILTER, 1, 0) == PAR_FLTX1 && !strcmp(par_name(PAR_FLTX1), "Env amt"), "ACID: ENV AMT there");
    ui.half = 1;
    ui.force = 1;
    run_ms(60);
    page_params(pp);
    turn(EN_K1, 40);
    check(pp[0] == PAR_FLTX1 && par[PAR_FLTX1] == 40 && vc[0].flt.xamt == 40 * 65536 / 127, "ENV AMT turned: the voice's");
    shot("filter-acid-env");
    ui.half = 0;
    ui.force = 1;
    run_ms(60);
    shot("filter-acid");
    par_set(PAR_FILTER_TYPE_1, F_LP4 - 1u);
    run_ms(20);
    check(!sect_half_used(0, S_FILTER, 1), "LP4: no second page");
    {                                                /* (the kit keeps it: a slot and back) */
        uint8_t k1[KIT_LEN];
        uint32_t i;
        for (i = 0; i < KIT_LEN; i++)
            k1[i] = par[kit_par(i)];
        kit_put(5, k1);
        par[PAR_FLTX1] = 0;
        kit_get(5, k1);
        check(k1[kit_idx(PAR_FLTX1)] == 40, "a kit slot keeps FILTER X");
    }
    par_set(PAR_FLTX1, 0);
    par_set(PAR_FILTER_TYPE_1, F_LAST - 1u);
    run_ms(20);
    /* playing D1 on each: the comb's line is D1's own, the note moves its pitch */
    for (k = F_LP4; k <= F_LAST; k++) {
        par_set(PAR_FILTER_TYPE_1, k - 1u);
        voice_trigger(0, 100, 72);
        run_ms(30);
        check(vc[0].ftype == k && vc[0].flt.was == (k == F_DUB ? 0u : k), "D1 renders the type");
    }
    check(vc[0].flt.line == flt_line[0] && vc[0].flt.key == 12, "D1: its own line, the note's +12");
    /* a project keeps them */
    par_set(PAR_FILTER_TYPE_1 + 3, F_DUB - 1u);
    par_set(PAR_FILTER_TYPE_1, F_COMBN - 1u);
    run_ms(20);
    {
        int rc = proj_save(1, "FILTERS ");
        par[PAR_FILTER_TYPE_1] = 0;
        par[PAR_FILTER_TYPE_1 + 3] = 0;
        rc |= proj_load(1);
        run_ms(20);
        check(rc == 0 && par[PAR_FILTER_TYPE_1] == F_COMBN - 1u && par[PAR_FILTER_TYPE_1 + 3] == F_DUB - 1u &&
              vc[0].ftype == F_COMBN && vc[3].ftype == F_DUB, "project: COMB- on D1, DUB BP on SN");
    }
    par_set(PAR_FILTER_TYPE_1, INIT_KIT[8u + PAR_FILTER_TYPE_1]);
    par_set(PAR_FILTER_TYPE_1 + 3, INIT_KIT[8u + PAR_FILTER_TYPE_1 + 3]);
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}

/* 30: the pages' pictures (ui_viz.c): each page draws (a shot of each), and while the pattern plays the sound page
 * redraws only its strip of steps, not the picture */
static void viz_page(uint32_t t, uint32_t sect, uint32_t half)
{
    goto_mode(B_HOME, M_HOME);
    black(t < 6u ? t : 5u);
    if (t == 6u)
        black(6);
    press(B_EDIT);
    ui.sect = (uint8_t)sect;
    ui.half = (uint8_t)half;
    ui.force = 1;
    run_ms(60);
}
static void viz_set(uint32_t sect, uint32_t half, uint32_t k, uint32_t v)
{
    vp_set(sect_par(ui.track, sect, half, k), v);
    run_ms(40);
}
static void sp_kit(uint8_t *k) { uint32_t i; for (i = 0; i < KIT_LEN; i++) k[i] = par[kit_par(i)]; }
static void sp_roll(void) { press(B_OCTUP); run_ms(SP_ROLL_MS + 60); }   /* OCT+: the die rolls, stops, casts */
static void phase32(void)
{
    pattern_t p0, p1;
    uint8_t k0[KIT_LEN], k1[KIT_LEN];
    uint32_t i, t, ok;
    printf("phase 32: spells\n");
    for (i = 0, ok = 1; i < NSPELL; i++) {           /* (the words fit: the lines beside the wizard, the cards) */
        uint32_t j, w;
        for (j = 0; j < 6u; j++) {
            const char *l = j < 2u ? SPELL[i].line[j] : SPELL[i].cl[j - 2u];
            if ((w = text_w(&AF_S, l)) > 164u)
                printf("  too wide (%u): %s\n", w, l), ok = 0;
        }
        for (j = 0; j < 4u; j++)
            if ((w = text_w(&AF_S, SPELL[i].cn[j])) > 52u)
                printf("  card name too wide (%u): %s\n", w, SPELL[i].cn[j]), ok = 0;
    }
    for (i = SPV_LIM; i < NSPV; i++) {              /* (LIMITS' cards) */
        char b[24];
        sp_card_name(b, sizeof b, i);
        ok &= text_w(&AF_S, b) <= 52u;
    }
    for (i = 0; i < NSPV; i++)
        if (text_w(&AF_S, sp_knob_line(i)) > 164u)
            printf("  too wide: %s\n", sp_knob_line(i)), ok = 0;
    for (i = 0; i < NSPMO + NSPML; i++) {            /* (0.19: MOTION's lines, each value its own) */
        if (i < NSPMO)
            sp.mot = (uint8_t)i;
        else
            sp.mlen = (uint8_t)(i - NSPMO);
        if (text_w(&AF_S, sp_knob_line(i < NSPMO ? SPV_MOT : SPV_MLEN)) > 164u)
            printf("  too wide: %s\n", sp_knob_line(i < NSPMO ? SPV_MOT : SPV_MLEN)), ok = 0;
    }
    sp.mot = SPMO_ONE;
    sp.mlen = SPML_EVEN;
    for (i = 0; i < 16u; i++)                        /* (the white keys' chips) */
        if (text_w(&AF_S, SC_SHORT[i]) > 28u)
            printf("  chip too wide (%u): %s\n", text_w(&AF_S, SC_SHORT[i]), SC_SHORT[i]), ok = 0;
    check(ok, "SPELLS: the words fit");
    goto_mode(B_HOME, M_HOME);
    perf_goto(PG_SPELL);
    {
        char b[40];
        uint32_t pg, np;
        title_text(b, sizeof b, &pg, &np);
        check(perf_spells() && !strcmp(b, "SPELLS: GRIMOIRE"), "ARP: SPELLS' first page, GRIMOIRE");
    }
    check(sp.sel == SP_NONE && sp.mask == SC_ALL, "SPELLS: NONE first, everything in scope");
    sp.tracks = 0;
    run_ms(60);
    shot("spells-grimoire-none");
    sp.sel = SP_POLTER;
    p0 = pat[sq.active];
    sp_kit(k0);
    press(B_OCTUP);
    run_ms(SP_ROLL_MS + 60);
    check(!sp.rolling && !memcmp(&p0, &pat[sq.active], sizeof p0), "no target: OCT+ casts nothing");
    black(NTRACK);                                   /* bar 1: ALL */
    check(sp.tracks == (1u << NTRACK) - 1u, "bar 1: all tracks targets");
    black(NTRACK);
    check(sp.tracks == 0u, "bar 1 again: none");
    black(0);
    black(3);
    black(5);
    check(sp.tracks == 0x29u, "black keys: the targets");
    run_ms(60);
    shot("spells-grimoire");
    press(B_OCTUP);
    run_ms(300);
    check(sp.rolling, "OCT+: the die rolls");
    shot("spells-roll");
    run_ms(SP_ROLL_MS);
    check(!sp.rolling && sp_pending(), "the die stops: the spell cast, pending");
    pattern_t pc = pat[sq.active];
    sp_kit(k1);
    ok = memcmp(&p0, &pc, sizeof p0) != 0 || memcmp(k0, k1, KIT_LEN) != 0;
    check(ok, "POLTERGEIST: the pattern or the kit changed");
    for (t = 0, ok = 1; t < NTRACK; t++)             /* (the tracks not targets: as they were) */
        if (!(sp.tracks >> t & 1u))
            ok &= !memcmp(p0.main[t], pc.main[t], sizeof p0.main[t]);
    check(ok, "the tracks not targets keep their steps");
    shot("spells-cast");
    hold(B_OCTDN);
    release(B_OCTDN);
    sp_kit(k1);
    check(!memcmp(&p0, &pat[sq.active], sizeof p0) && !memcmp(k0, k1, KIT_LEN) && !sp.pend,
          "OCT-: REVERSE, the pattern and the kit exactly as before");
    /* reroll: from before the cast, not on top of it */
    sp_roll();
    p1 = pat[sq.active];
    sp_roll();
    check(sp_pending(), "OCT+ again: a reroll, still pending");
    hold(B_OCTDN);
    release(B_OCTDN);
    check(!memcmp(&p0, &pat[sq.active], sizeof p0), "a reroll reverses to before the first cast");
    (void)p1;
    /* 0.25.3: the die shows SP_DIE_MS after the cast, then the spell's words (the cast still pending); the spell
     * knob turned ends it at once */
    sp_roll();
    check(sp_die_shown() && sp_pending(), "a cast: the die shown");
    run_ms(SP_DIE_MS);
    check(!sp_die_shown() && sp_pending(), "SP_DIE_MS on: the spell's words again, the cast still pending");
    shot("spells-cast-words");
    sp_roll();
    check(sp_die_shown(), "cast again: the die again");
    turn(EN_K1, 1);
    check(!sp_die_shown() && sp.sel == SP_POLTER + 1u, "the spell knob turned: the die gone at once");
    turn(EN_K1, -1);
    check(sp.sel == SP_POLTER && sp_pending(), "(the spell back, the cast still pending)");
    hold(B_OCTDN);
    release(B_OCTDN);
    check(!memcmp(&p0, &pat[sq.active], sizeof p0), "(reversed)");
    /* KEEP */
    sp_roll();
    pc = pat[sq.active];
    black(NTRACK + 1);
    check(!sp.pend, "bar 2: KEEP");
    hold(B_OCTDN);
    release(B_OCTDN);
    check(!memcmp(&pc, &pat[sq.active], sizeof pc), "a kept spell: OCT- leaves it");
    /* SKELETON: the pattern only, fewer hits, the kit as it was */
    pat[sq.active] = p0;
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k0[i];
    snd_post_all();
    sp_vset(SPV_SPELL, SP_SKELETON);
    sp.tracks = (1u << NTRACK) - 1u;
    sp_roll();
    sp_kit(k1);
    check(!memcmp(k0, k1, KIT_LEN), "SKELETON: the kit untouched");
    {
        uint32_t a = 0, b = 0, s;
        for (t = 0; t < NTRACK; t++)
            for (s = 0; s < 4u; s++) {
                a += (uint32_t)__builtin_popcount(p0.main[t][s]);
                b += (uint32_t)__builtin_popcount(pat[sq.active].main[t][s]);
            }
        check(b <= a, "SKELETON: no more hits than before");
    }
    hold(B_OCTDN);
    release(B_OCTDN);
    /* scope PATTERN on POLTERGEIST: the kit untouched; sound mutation 0 too */
    sp_vset(SPV_SPELL, SP_POLTER);
    sp_vset(SPV_SCOPE, 2);
    sp_roll();
    sp_kit(k1);
    check(!memcmp(k0, k1, KIT_LEN), "scope PATTERN: the kit untouched");
    hold(B_OCTDN);
    release(B_OCTDN);
    sp_vset(SPV_SCOPE, 0);
    i = sp.k[SP_POLTER][SPK_SOUND];
    sp.k[SP_POLTER][SPK_SOUND] = 0;
    sp_roll();
    sp_kit(k1);
    check(!memcmp(k0, k1, KIT_LEN), "SOUND MUTATION 0: the kit untouched");
    hold(B_OCTDN);
    release(B_OCTDN);
    sp.k[SP_POLTER][SPK_SOUND] = (uint8_t)i;
    sp.k[SP_POLTER][SPK_C1] = 5;                     /* (bar 4: the knobs as at first) */
    black(NTRACK + 3);
    check(!memcmp(sp.k[SP_POLTER], SPELL[SP_POLTER].def, NSPK), "bar 4: the spell's knobs as at first");
    /* the white keys: a part out, SHIFT (OCT-) + a key: only it; TIMING alone leaves the hits and the kit */
    white(0);
    check(!(sp.mask & SC_PITCH) && (sp.mask & SC_OSC), "a white key: that part out");
    run_ms(60);
    shot("spells-scope");
    white(0);
    hold(B_OCTDN);
    white(11);
    release(B_OCTDN);
    check(sp.mask == SC_TIME && !sp.pend, "SHIFT (OCT-) + a white key: only it");
    sp.k[SP_POLTER][SPK_PAT] = 127;
    sp_roll();
    sp_kit(k1);
    {
        int same = !memcmp(k0, k1, KIT_LEN);
        for (t = 0; t < NTRACK; t++)
            same &= !memcmp(p0.main[t], pat[sq.active].main[t], sizeof p0.main[t]);
        check(same, "TIMING alone: the hits and the kit as they were");
    }
    hold(B_OCTDN);
    release(B_OCTDN);
    sp.k[SP_POLTER][SPK_PAT] = SPELL[SP_POLTER].def[SPK_PAT];
    sp_vset(SPV_SCOPE, 0);
    check(sp.mask == SC_ALL, "SCOPE ALL: every white key");
    /* D1 stays a kick, whatever the spell */
    {
        uint32_t sv = sp.tracks, n;
        int kick = 1;
        sp.tracks = 1;
        for (n = 0; n < NSPELL; n++) {
            uint32_t r, snd = sp.k[n][SPK_SOUND], ch = sp.k[n][SPK_CHAOS];
            sp.sel = (uint8_t)n;
            sp.k[n][SPK_SOUND] = 126;                /* (127: FREE, no kick kept: below) */
            sp.k[n][SPK_CHAOS] = 100;
            for (r = 0; r < 6u; r++) {
                uint32_t md = par[PAR_MODEL1], f = par[PAR_FILTER_TYPE_1] + 1u;
                sp_roll();
                md = par[PAR_MODEL1];
                f = par[PAR_FILTER_TYPE_1] + 1u;
                if (!sp_kick_ok(md) || par[PAR_COARSE1] > 44u || (!md && par[PAR_OSC_WAVE_DRUM1] > 1u) ||
                    (f != F_LP && f != F_LP2 && f != F_LP4 && f != F_OFF && f != F_DUB) || par[PAR_RESO_1] > 72u) {
                    printf("  %s: D1 model %u coarse %u wave %u filter %u reso %u\n", SPELL[n].name, md,
                           par[PAR_COARSE1], par[PAR_OSC_WAVE_DRUM1], f, par[PAR_RESO_1]);
                    kick = 0;
                }
            }
            hold(B_OCTDN);
            release(B_OCTDN);
            sp.k[n][SPK_SOUND] = (uint8_t)snd;
            sp.k[n][SPK_CHAOS] = (uint8_t)ch;
        }
        check(kick, "D1 stays a kick (LXR, BASS DRUM or MODAL; low, a low-pass)");
        /* D2 and D3: toms, D3 above D2; the melody generator on them now and then */
        sp.tracks = 0x7Fu;
        {
            int toms = 1;
            uint32_t mel = 0;
            for (n = 0; n < NSPELL; n++) {
                uint32_t r;
                if (!(SPELL[n].cats & SC_PITCH))
                    continue;
                sp.sel = (uint8_t)n;
                for (r = 0; r < 6u; r++) {
                    sp_roll();
                    toms &= par[PAR_COARSE2] >= 34u && par[PAR_COARSE2] <= 48u && par[PAR_COARSE3] <= 56u &&
                            par[PAR_COARSE3] > par[PAR_COARSE2];
                    toms &= par[PAR_MODAMNT2] <= 30u || par[PAR_MOD_EG2] <= 72u;
                    mel += mel_on(&pat[sq.active], 1) + mel_on(&pat[sq.active], 2);
                }
                hold(B_OCTDN);
                release(B_OCTDN);
            }
            check(toms, "D2 a floor tom, D3 a rack tom above it; no long deep pitch drop");
            check(mel >= 10u, "the melody generator on the toms now and then");
            printf("  toms: %u melodies in 60 casts\n", mel);
        }
        /* LIMITS: the sends, the reverb's level and the FX mix stay under them, whatever the spell */
        sp.tracks = 0x7Fu;
        par[PAR_REV_LEVEL] = 127;
        for (n = 0; n < NSPELL; n++) {               /* (a spell that never touches FX leaves them as they are) */
            uint32_t r, v, snd = sp.k[n][SPK_SOUND];
            int under = 1;
            if (!(SPELL[n].cats & SC_FX))
                continue;
            sp.sel = (uint8_t)n;
            sp.k[n][SPK_SOUND] = 127;
            for (r = 0; r < 4u; r++) {
                sp_roll();
                for (v = 0; v < NVOICE; v++)
                    under &= par[PAR_REV_SEND1 + v] <= SP_LIM0[SPL_RVS] && par[PAR_FX_SEND1 + v] <= SP_LIM0[SPL_FXS];
                under &= par[PAR_REV_LEVEL] <= SP_LIM0[SPL_RLEV] && par[PAR_FX_DRY_WET] <= SP_LIM0[SPL_FXMIX];
            }
            if (!under)
                printf("  %s: over LIMITS\n", SPELL[n].name), kick = 0;
            hold(B_OCTDN);
            release(B_OCTDN);
            sp.k[n][SPK_SOUND] = (uint8_t)snd;
        }
        check(kick, "LIMITS: the sends, the reverb level and the FX mix kept under them");
        par[PAR_REV_LEVEL] = 100;
        /* 0.25.3: CONCRETE DRIFT's white noise. A spell's LFO 2 (HOLD) on a decay never takes it past SP_LFO_DEC:
         * a hit that held it at 127 (endless) rang on past STOP. And any decay modulated stops at MOD_DEC_MAX
         * (or the knob, if higher): only the knob makes a note endless */
        kick = 1;
        for (n = 0; n < NSPELL; n++) {
            uint32_t r, v, seen = 0;
            if (!(SPELL[n].fl & SF_LFO))
                continue;
            sp.sel = (uint8_t)n;
            for (r = 0; r < 40u; r++) {
                sp_snapshot();
                for (v = 0; v < NVOICE; v++)
                    if (!(r & 3u))
                        par[sp_role_par(R_DEC, v)] = (uint8_t)(100u + r % 28u);   /* (long decays, some over) */
                sp_cast(r * 2654435761u + n);
                for (v = 0; v < NVOICE; v++)
                    if (par[PAR_L2AMT1 + v] && MOD_TARGETS[par[PAR_L2TGT1 + v]][1] == sp_role_par(R_DEC, v)) {
                        seen++;
                        kick &= par[sp_role_par(R_DEC, v)] + par[PAR_L2AMT1 + v] <= SP_LFO_DEC;
                    }
                sp_restore();
                sp.pend = 0;
            }
            if (!seen)
                printf("  %s: no LFO on a decay in 40 casts\n", SPELL[n].name);
        }
        check(kick, "a spell's LFO on a decay: its top kept under SP_LFO_DEC");
        par[PAR_VELOD5] = 110;
        snd_apply(PAR_VELOD5, 127, 65536, 1);
        kick = vc[4].aeg.dec == EG_DEC[MOD_DEC_MAX] && EG_DEC[MOD_DEC_MAX] != 0u;
        par[PAR_VELOD5] = 124;
        snd_apply(PAR_VELOD5, 127, 65536, 1);
        kick &= vc[4].aeg.dec == EG_DEC[124];
        snd_apply(PAR_VELOD5, 90, 90u * 65536u / 127u, 1);
        kick &= vc[4].aeg.dec == EG_DEC[90];
        par[PAR_VELOD5] = 127;
        snd_apply(PAR_VELOD5, 127, 65536, 1);
        kick &= vc[4].aeg.dec == 0u;
        check(kick, "a decay modulated: never past MOD_DEC_MAX or the knob (127, endless: the knob's alone)");
        snd_post_all();
        sp.tracks = (uint8_t)sv;
        sp.sel = SP_POLTER;
    }
    /* 0.21: NONE plain chance (SOUND how far), SOUND 100% FREE, VOICES on MOTION's page */
    {
        uint8_t kb[NSPK], ka[KIT_LEN];
        uint32_t w, r, n, sv = sp.tracks, free = 0;
        int kick = 1, eng = 1;
        char b[24];
        for (w = 0, ok = 1; w < NSE; w++)
            if (text_w(&AF_S, SE_SHORT[w]) > 26u)
                printf("  VOICES key too wide: %s\n", SE_SHORT[w]), ok = 0;
        check(ok, "VOICES: the names fit the keys");
        sp_vset(SPV_SPELL, SP_NONE);
        memcpy(kb, sp.k[SP_NONE], NSPK);
        sp.tracks = 0x7Fu;
        sp_kit(ka);
        sp_roll();
        n = sp.nsnd;
        sp_kit(k1);
        for (i = 0, w = 0; i < KIT_LEN; i++)
            w += ka[i] != k1[i] && (ka[i] > k1[i] ? ka[i] - k1[i] : k1[i] - ka[i]) > 8u;
        hold(B_OCTDN);
        release(B_OCTDN);
        sp.k[SP_NONE][SPK_SOUND] = 110;
        sp_roll();
        sp_kit(k1);
        for (i = 0, r = 0; i < KIT_LEN; i++)
            r += ka[i] != k1[i] && (ka[i] > k1[i] ? ka[i] - k1[i] : k1[i] - ka[i]) > 8u;
        printf("  NONE: %u knobs moved at SOUND 24 (%u by over 8), %u at 87%% (%u by over 8)\n", n, w, sp.nsnd, r);
        check(n >= 5u && w <= n / 8u && r >= 6u * (w + 1u), "NONE: SOUND 24 nudges, SOUND up moves far further");
        for (r = 0; r < 6u; r++) {                   /* (still a kick and toms below 100%) */
            uint32_t f = par[PAR_FILTER_TYPE_1] + 1u;
            kick &= sp_kick_ok(par[PAR_MODEL1]) && par[PAR_COARSE1] >= 18u && par[PAR_COARSE1] <= 44u &&
                    (f == F_LP || f == F_LP2 || f == F_LP4 || f == F_OFF || f == F_DUB) && par[PAR_RESO_1] <= 72u;
            kick &= par[PAR_COARSE2] >= 34u && par[PAR_COARSE2] <= 48u && par[PAR_COARSE3] > par[PAR_COARSE2] &&
                    par[PAR_COARSE3] <= 56u;
            sp_roll();
        }
        check(kick, "NONE at 87%: D1 still a kick, D2 D3 toms");
        hold(B_OCTDN);
        release(B_OCTDN);
        sp.k[SP_NONE][SPK_SOUND] = 127;
        sp_card_value(b, sizeof b, SPV_MODE + 1u + SPK_SOUND, 127);
        check(!strcmp(b, "FREE"), "SOUND 100%: FREE");
        for (r = 0; r < 6u; r++) {
            sp_roll();
            free += par[PAR_COARSE1] < 18u || par[PAR_COARSE1] > 44u || par[PAR_COARSE2] < 34u ||
                    par[PAR_COARSE2] > 48u || par[PAR_COARSE3] > 56u;
        }
        check(free > 0u, "SOUND 100%: the kick and the toms let go");
        hold(B_OCTDN);
        release(B_OCTDN);
        sp.k[SP_NONE][SPK_SOUND] = 64;
        /* VOICES: MOTION's page, the sound row's keys 1-5; the pattern row still the scope */
        perf_goto(PG_SPELL + 4u);
        hold(B_OCTDN);
        white(3);
        release(B_OCTDN);
        check(sp.eng == SE_MICRO && sp.mask == SC_ALL, "VOICES: SHIFT (OCT-) + MICRO: MICRO only, the scope as it was");
        white(8);
        check(!(sp.mask & SC_HITS) && sp.eng == SE_MICRO, "MOTION's page: the pattern row still the scope");
        white(8);
        sp_roll();
        for (w = 1; w < NVOICE; w++)
            eng &= par[PAR_MODEL1 + w] >= MD_MC0 || par[PAR_MODEL1 + w] == MD_NOISE;   /* (1.0: NOISE with them) */
        eng &= par[PAR_MODEL1] == MD_MC0 + MC_POP || par[PAR_MODEL1] == MD_MC0 + MC_CHIRP;
        check(eng, "MICRO only: every voice MICRO, D1 a POP or a CHIRP");
        hold(B_OCTDN);
        release(B_OCTDN);
        white(4);
        check(sp.eng == (SE_MICRO | SE_KICK), "KICK: D1 always the LXR's");
        sp_roll();
        run_ms(60);
        shot("spells-voices");
        check(par[PAR_MODEL1] == MD_LXR && (par[PAR_MODEL1 + 2u] >= MD_MC0 || par[PAR_MODEL1 + 2u] == MD_NOISE),
              "KICK and MICRO: the LXR's kick, MICRO (or NOISE, with them) above");
        hold(B_OCTDN);
        release(B_OCTDN);
        sp.eng = 0;                                  /* (none: the engines as they are, any spell) */
        sp_vset(SPV_SPELL, SP_RITUAL);
        eng = 1;
        {
            uint8_t md[NVOICE];
            for (w = 0; w < NVOICE; w++)
                md[w] = par[PAR_MODEL1 + w];
            sp.k[SP_RITUAL][SPK_SOUND] = 126;
            for (r = 0; r < 4u; r++) {
                sp_roll();
                for (w = 0; w < NVOICE; w++)
                    eng &= par[PAR_MODEL1 + w] == md[w];
                hold(B_OCTDN);
                release(B_OCTDN);
            }
            sp.k[SP_RITUAL][SPK_SOUND] = SPELL[SP_RITUAL].def[SPK_SOUND];
        }
        check(eng, "VOICES none: the engines kept, whatever the spell");
        sp.eng = SE_LXR | SE_PLAITS | SE_KICK;
        {
            int rc = proj_save(1, "VOICES  ");
            sp_defaults();
            rc |= proj_load(1);
            check(rc == 0 && sp.eng == (SE_LXR | SE_PLAITS | SE_KICK), "the project: VOICES back");
        }
        sp.eng = SE_ALL;
        sp.sel = SP_SKELETON;                        /* (a cast that can change nothing says why) */
        sp.mask = SC_SOUND;
        check(!strcmp(sp_why(), ": NOTHING IN SCOPE"), "SKELETON, scope SOUND: NOTHING IN SCOPE");
        sp.sel = SP_NONE;
        sp.k[SP_NONE][SPK_SOUND] = 0;
        check(!strcmp(sp_why(), ": SOUND IS OFF"), "NONE, scope SOUND, SOUND OFF: SOUND IS OFF");
        sp.mask = SC_ALL;
        memcpy(sp.k[SP_NONE], kb, NSPK);
        perf_goto(PG_SPELL);
        sp.tracks = (uint8_t)sv;
        sp.sel = SP_POLTER;
    }
    /* recipes: SAVE, RECALL the same roll */
    memset(rcp, 0, sizeof rcp);
    sp.tracks = 0x05u;
    sp_roll();
    pc = pat[sq.active];
    sp_kit(k1);
    black(NTRACK + 2);
    check(sp.sel == NSPELL && rcp[0].spell == SP_POLTER + 1u && rcp[0].tracks == 0x05u && rcp[0].seed == sp.last,
          "bar 3: SAVE, the recipe U1");
    hold(B_OCTDN);
    release(B_OCTDN);
    sp.tracks = 0x7Fu;
    sp_vset(SPV_SPELL, 0);
    sp_vset(SPV_SPELL, NSPELL);
    check(sp.sel == NSPELL && sp.tracks == 0x05u, "a recipe picked: its targets");
    sp_vset(SPV_MODE, SPM_RECALL);
    sp_roll();
    {
        uint8_t k2[KIT_LEN];
        sp_kit(k2);
        check(!memcmp(&pc, &pat[sq.active], sizeof pc) && !memcmp(k1, k2, KIT_LEN), "ROLL SAME: the same cast again");
    }
    hold(B_OCTDN);
    release(B_OCTDN);
    sp_vset(SPV_MODE, SPM_RECAST);
    turn(EN_SELECT, 1);
    run_ms(60);
    shot("spells-cast-page");
    turn(EN_SELECT, 1);
    run_ms(60);
    shot("spells-character");
    turn(EN_SELECT, 1);
    turn(EN_K1, -3);
    run_ms(60);
    check(sp.lim[SPL_RVS] == SP_LIM0[SPL_RVS] - 3u, "LIMITS: KNOB 1 the most reverb send");
    shot("spells-limits");
    sp.lim[SPL_RVS] = SP_LIM0[SPL_RVS];
    turn(EN_SELECT, -1);
    {                                                /* (the knobs: a cast leaves them as set, playing too) */
        uint8_t kb[NSPK];
        sp_vset(SPV_SPELL, SP_GLITCH);
        sp_vset(SPV_VAR, 100);
        turn(EN_K2, -40);
        turn(EN_K2, 13);
        memcpy(kb, sp_knobs(), NSPK);
        press(B_PLAY);
        sp_roll();
        sp_roll();
        run_ms(500);
        press(B_PLAY);
        check(!memcmp(kb, sp_knobs(), NSPK), "a cast leaves the spell's knobs as set (VARIATION too)");
        hold(B_OCTDN);
        release(B_OCTDN);
        sp_vset(SPV_VAR, 0);
    }
    turn(EN_SELECT, -2);
    /* the project: KSP2 */
    sp.lim[SPL_FXMIX] = 17;
    {
        uint8_t k[NSPK];
        uint32_t sel = sp.sel, sd = rcp[0].seed;
        int rc;
        memcpy(k, sp.k[3], NSPK);
        sp.k[3][SPK_C2] = 99;
        rc = proj_save(1, "SPELLS  ");
        sp_defaults();
        memset(rcp, 0, sizeof rcp);
        rc |= proj_load(1);
        check(rc == 0 && sp.sel == sel && rcp[0].spell == SP_POLTER + 1u && rcp[0].seed == sd && sp.k[3][SPK_C2] == 99u &&
              rcp[0].mask == SC_ALL && sp.lim[SPL_FXMIX] == 17u,
              "the project: the spells and the recipes back");
        memcpy(sp.k[3], k, NSPK);
    }
    /* leaving SPELLS keeps the cast */
    sp_vset(SPV_SPELL, SP_POLTER);
    sp.tracks = 0x7Fu;
    sp_roll();
    press(B_ARP);
    run_ms(20);
    check(!perf_spells() && !sp.pend, "SPELLS left: the cast kept");
    pat[sq.active] = p0;
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k0[i];
    snd_post_all();
    memset(rcp, 0, sizeof rcp);
    sp_defaults();
    goto_mode(B_HOME, M_HOME);
}
/* 0.19-test11: a song row's repeats (KNOB 3), SPELLS' MOTION page, WHISPERING MOTES */
static uint32_t t11_moved(const pattern_t *p, uint32_t *bad, int one_way)   /* (the tracks off the pulse) */
{
    uint32_t t, n = 0;
    for (t = 0; t < NTRACK; t++) {
        if (p->clk[t] == CLK_1X && p->dir[t] == DIR_FWD)
            continue;
        n++;
        if (one_way && p->clk[t] != CLK_1X && p->dir[t] != DIR_FWD)
            (*bad)++;
        if (one_way && p->clk[t] != CLK_1X && p->clk[t] != 8u && p->clk[t] != 2u && p->clk[t] != 7u)
            (*bad)++;
    }
    return n;
}
static void phase_t11(void)
{
    pattern_t p0[2], base;
    uint8_t k0[KIT_LEN], s0[MAX_SONG_LENGTH];
    uint32_t i, t, n, bad, seen[8][3], ok;
    printf("0.19-test11: song repeats, MOTION, MOTES\n");
    /* the song: P1 twice, P2 three times, END */
    p0[0] = pat[0];
    p0[1] = pat[1];
    memcpy(s0, songs[0], sizeof s0);
    for (i = 0; i < 2u; i++)
        for (t = 0; t < NTRACK; t++) {
            pat[i].len[t] = 16;
            pat[i].clk[t] = CLK_1X;
        }
    memset(songs[0], SONG_END, MAX_SONG_LENGTH);
    songs[0][0] = 0;
    songs[0][1] = 1;
    sq.song = 0;
    goto_mode(B_HOME, M_SONG);
    ui.page = 0;                                     /* (37: KNOB 4 the repeats, KNOB 3 the pattern) */
    ui.song_row = 0;
    turn(EN_K4, 1);
    check(song_reps(0, 0) == 2u && vp_get(VP_SONG_REP) == 1u, "SONG: KNOB 4, the row's repeats (x2)");
    ui.song_row = 1;
    song_put(0, 1, 1, 2);                            /* (x3) */
    run_ms(60);
    shot("song-repeats");
    check(song_reps(0, 2) == 1u && song_row(songs[0][1]) == 1u && song_row(songs[0][2]) == SONG_END,
          "a row unset: once; the row's pattern kept beside its times");
    ui.song_row = 0;
    turn(EN_K3, 1);
    check(song_row(songs[0][0]) == 1u && song_reps(0, 0) == 2u, "KNOB 3: the row's pattern, its times kept");
    turn(EN_K3, -1);
    for (i = 0, ok = 1; i < NUM_PATTERN; i++)        /* (every pattern, every count: no END or LOOP among them) */
        for (t = 0; t < NSONGREP; t++) {
            song_put(0, 5, i, t);
            ok &= song_row(songs[0][5]) == i && song_rep_i(0, 5) == t;
        }
    check(ok, "a row holds any pattern with any of its times");
    songs[0][5] = SONG_END;
    ui.song_row = 1;
    hold(B_OCTDN);
    press(B_PLAY);
    release(B_OCTDN);
    n = 0;
    for (i = 0; i < 1500u && sq.running; i++) {      /* (each pattern start: the row, the time) */
        if (!n || seen[n - 1u][1] != sq.song_pos || seen[n - 1u][2] != sq.song_n) {
            if (n < 8u) {
                seen[n][0] = sq.active;
                seen[n][1] = sq.song_pos;
                seen[n][2] = sq.song_n;
            }
            n++;
        }
        run_ms(20);
    }
    ok = n == 5u && !sq.running;
    for (i = 0; ok && i < 5u; i++)
        ok = seen[i][0] == (i < 2u ? 0u : 1u) && seen[i][2] == (i < 2u ? i : i - 2u);
    check(ok, "the song: P1 twice, P2 three times, then END stops it");
    if (sq.running)
        press(B_PLAY);
    check(sq.song_mode == 1u, "37: SHIFT + PLAY leaves Play on SONG");
    sq.song_mode = 0;
    ui.song_row = 0;
    song_insert_row(0, 0);
    check(song_row(songs[0][1]) == 0u && song_reps(0, 1) == 2u && song_reps(0, 2) == 3u, "COPY: a row inserted, its repeats too");
    song_delete_row(0, 0);
    check(song_reps(0, 0) == 2u && song_reps(0, 1) == 3u && song_reps(0, 63) == 1u, "a row deleted: the repeats move up");
    {
        int rc = proj_save(1, "SONGREP ");
        memset(songs[0], SONG_END, MAX_SONG_LENGTH);
        rc |= proj_load(1);
        check(rc == 0 && song_reps(0, 0) == 2u && song_reps(0, 1) == 3u && song_reps(0, 2) == 1u &&
              song_row(songs[0][1]) == 1u, "the project: the song's repeats back");
    }
    memcpy(songs[0], s0, sizeof s0);
    pat[0] = p0[0];
    pat[1] = p0[1];
    /* MOTION: RITUAL on every track; SAME: all forward at the pattern's speed, ONE: one at most, in time */
    perf_goto(PG_SPELL + 4u);
    check(ui.page == PG_SPELL + 4u && sp.mot == SPMO_ONE && sp.mlen == SPML_EVEN, "SPELLS: MOTION, ONE and EVEN at first");
    shot("spells-motion");
    base = pat[sq.active];
    sp_kit(k0);
    for (t = 0; t < NTRACK; t++) {
        pat[sq.active].gen[t][0] = GEN_OFF;
        pat[sq.active].clk[t] = CLK_1X;
        pat[sq.active].dir[t] = DIR_FWD;
    }
    pat[sq.active].clk[2] = 8;
    pat[sq.active].dir[4] = DIR_REV;
    pat[sq.active].len[5] = 15;
    sp_vset(SPV_SPELL, SP_RITUAL);
    sp.tracks = (1u << NTRACK) - 1u;
    sp.mot = SPMO_SAME;
    sp.mlen = SPML_BARS;
    sp_roll();
    bad = 0;
    ok = t11_moved(&pat[sq.active], &bad, 0) == 0u;
    for (t = 0; t < NTRACK; t++)
        ok &= (track_len(&pat[sq.active], t) & 15u) == 0u;
    check(ok, "MOTION SAME, Lengths BARS: every track forward, at 1x, 16 / 32 / 64 steps");
    sp.mot = SPMO_ONE;
    for (i = 0, n = 0, ok = 1, bad = 0; i < 12u; i++) {
        uint32_t m;
        sp_roll();
        m = t11_moved(&pat[sq.active], &bad, 1);
        ok &= m <= 1u;
        n += m;
    }
    check(ok && n >= 6u && !bad, "MOTION ONE: one track its own speed (x2, 1/2, 3/2) or way, no more");
    sp.mot = SPMO_FEW;
    for (i = 0, ok = 1; i < 8u; i++) {
        sp_roll();
        ok &= t11_moved(&pat[sq.active], &bad, 0) <= 2u;
    }
    check(ok, "MOTION FEW: two tracks at most");
    hold(B_OCTDN);
    release(B_OCTDN);
    sp_vset(SPV_SPELL, SP_DUB);                      /* (test12: every spell, not only those with lengths) */
    sp.mot = SPMO_ONE;
    for (i = 0, n = 0, ok = 1; i < 10u; i++) {
        uint32_t m;
        sp_roll();
        m = t11_moved(&pat[sq.active], &bad, 0);
        ok &= m <= 1u;
        n += m;
    }
    check(ok && n >= 5u, "MOTION ONE on HAUNTED DUB: one track its own way");
    hold(B_OCTDN);
    release(B_OCTDN);
    sp_vset(SPV_SPELL, SP_RITUAL);                   /* (FREE kept, then SAME on another spell tames it all) */
    sp.mot = SPMO_FREE;
    sp.k[SP_RITUAL][SPK_CHAOS] = 127;
    for (i = 0, n = 0; i < 6u && n < 3u; i++) {
        sp_roll();
        n = t11_moved(&pat[sq.active], &bad, 0);
    }
    check(n >= 3u, "MOTION FREE: a free-for-all");
    black(NTRACK + 1);                               /* (KEEP) */
    for (t = 0; t < NTRACK; t++)
        pat[sq.active].gen[t][0] = t == 6u ? GEN_EUCLID : GEN_OFF;
    pat[sq.active].clk[0] = 8;
    pat[sq.active].dir[3] = DIR_REV;
    pat[sq.active].dir[6] = DIR_PING;
    sp_vset(SPV_SPELL, SP_DUB);                      /* (PROTECTION 100: the kick and snare protected) */
    sp.mot = SPMO_SAME;
    sp_roll();
    check(t11_moved(&pat[sq.active], &bad, 0) == 0u, "MOTION SAME after a kept FREE (DUB, kick and snare protected, a EUCLID track): all forward at 1x");
    black(NTRACK + 1);
    memcpy(sp.k[SP_RITUAL], SPELL[SP_RITUAL].def, NSPK);
    sp.mot = SPMO_ONE;
    sp.mot = SPMO_ONE;
    sp.mlen = SPML_EVEN;
    /* WHISPERING MOTES: MICRO on the voices (the snare: MICRO or its noise), a quiet kick */
    sp_vset(SPV_SPELL, SP_MOTES);
    for (i = 0, ok = 1; i < 6u; i++) {
        uint32_t v;
        sp_roll();
        for (v = 1; v < NVOICE; v++)
            ok &= v == 3u ? par[PAR_MODEL1 + v] == 0u || par[PAR_MODEL1 + v] >= MD_MC0 : par[PAR_MODEL1 + v] >= MD_MC0;
        ok &= par[PAR_VOL1] <= 98u && (par[PAR_MODEL1] == 0u || par[PAR_MODEL1] == MD_MODAL ||
                                       par[PAR_MODEL1] == MD_PL0 + PE_BD);
    }
    check(ok, "MOTES: MICRO on D2-HH (the snare MICRO or noise), D1 a hushed kick");
    run_ms(60);
    perf_goto(PG_SPELL);
    shot("spells-motes");
    hold(B_OCTDN);
    release(B_OCTDN);
    {
        int rc;
        sp.mot = SPMO_MANY;
        sp.mlen = SPML_ODD;
        sp.k[SP_MOTES][SPK_C1] = 33;
        sp.lim[SPL_RVS] = 21;
        rc = proj_save(1, "MOTION  ");
        sp_defaults();
        rc |= proj_load(1);
        check(rc == 0 && sp.mot == SPMO_MANY && sp.mlen == SPML_ODD && sp.k[SP_MOTES][SPK_C1] == 33u &&
              sp.lim[SPL_RVS] == 21u && sp.k[SP_BIO][SPK_SOUND] == SPELL[SP_BIO].def[SPK_SOUND],
              "the project: MOTION, MOTES' knobs and LIMITS back (KSP2)");
    }
    pat[sq.active] = base;
    sp_defaults();
    memset(rcp, 0, sizeof rcp);
    goto_mode(B_HOME, M_HOME);
}
static void phase30(void)
{
    uint32_t a, b;
    printf("phase 30: the pages' pictures\n");
    viz_page(0, S_FM, 0);
    viz_set(S_FM, 0, 0, 70);
    viz_set(S_FM, 0, 1, 88);
    shot("viz-fm");
    viz_page(4, S_FM, 0);
    viz_set(S_FM, 0, 2, 60);
    viz_set(S_FM, 0, 3, 40);
    shot("viz-fm-cy");
    viz_page(0, S_CLICK, 0);
    viz_set(S_CLICK, 0, 0, 7);
    viz_set(S_CLICK, 0, 1, 110);
    shot("viz-transient");
    viz_set(S_CLICK, 0, 0, 0);
    shot("viz-transient-snap");
    viz_page(0, S_MOD, 0);
    viz_set(S_MOD, 0, 0, 60);
    viz_set(S_MOD, 0, 2, 90);
    shot("viz-pitch-eg");
    viz_page(0, S_MOD, 1);
    viz_set(S_MOD, 1, 1, 100);
    shot("viz-velocity");
    viz_page(3, S_AEG, 0);
    viz_set(S_AEG, 0, 0, 30);
    viz_set(S_AEG, 0, 2, 3);
    viz_set(S_AEG, 0, 1, 60);
    shot("viz-aeg-clap");
    viz_page(0, S_GATE, 0);
    viz_set(S_GATE, 0, 0, 1);
    shot("viz-gate-on");
    viz_set(S_GATE, 0, 0, 0);
    viz_page(0, S_FILTER, 0);
    viz_set(S_FILTER, 0, 1, 70);                      /* (1.0: Type, Freq, Reso, Drive) */
    viz_set(S_FILTER, 0, 2, 110);
    shot("viz-filter-res");
    viz_set(S_FILTER, 0, 0, 1);
    shot("viz-filter-hp");
    viz_set(S_FILTER, 0, 0, 12);
    shot("viz-filter-vowel");
    viz_set(S_FILTER, 0, 0, 0);
    viz_set(S_FILTER, 0, 1, 127);
    viz_set(S_FILTER, 0, 2, 0);
    viz_page(0, S_LFO, 0);
    viz_set(S_LFO, 0, 0, 20);
    viz_set(S_LFO, 0, 1, 100);
    viz_set(S_LFO, 0, 2, 1);
    viz_set(S_LFO, 1, 2, 1);                          /* (RETRIGGER: D1's hits) */
    shot("viz-lfo-trig");
    viz_page(0, S_LFOM, 0);
    viz_set(S_LFOM, 0, 0, LM_HOLD);
    shot("viz-lfo-hold");
    viz_set(S_LFOM, 0, 0, LM_ONE);
    shot("viz-lfo-one");
    viz_set(S_LFOM, 0, 0, LM_TRIG);
    viz_page(0, S_SHAPE, 0);
    viz_set(S_SHAPE, 0, 2, 40);
    shot("viz-shape-srate");
    viz_set(S_SHAPE, 0, 2, 127);
    viz_page(0, S_INSERT, 0);
    viz_set(S_INSERT, 0, 0, INS_SHIFT);
    viz_set(S_INSERT, 0, 2, 90);
    shot("viz-insert-shifter");
    viz_set(S_INSERT, 0, 0, INS_OFF);
    viz_page(3, S_MODEL, 0);
    shot("viz-model-sn");
    viz_page(0, S_MODEL, 0);                          /* (30 rev: each model a picture, its pages marked) */
    shot("viz-model-lxr");
    {
        static const uint8_t M[] = {15, 11, 10, 4, 17, 1, 9, 8, MD_RAIN, 5, 6, 12, MD_CLOUD};
        static const char *const N[] = {"viz-model-pstring", "viz-model-pspeech", "viz-model-pchord", "viz-model-pva",
                                        "viz-model-pkick", "viz-model-modal", "viz-model-pwtable", "viz-model-paddtv",
                                        "viz-model-rain", "viz-model-pwshape", "viz-model-pfm", "viz-model-pswarm",
                                        "viz-model-cloud"};
        uint32_t i;
        for (i = 0; i < sizeof M; i++) {
            if (M[i] == MD_PL0 + PE_WTBL && !ELIXIR_WTABLE)   /* (0.24: parked) */
                continue;
            viz_set(S_MODEL, 0, 0, M[i]);
            viz_set(S_MODEL, 0, 1, 90);
            viz_set(S_MODEL, 0, 2, 70);
            viz_set(S_MODEL, 0, 3, 40);
            shot(N[i]);
        }
#if ELIXIR_WTABLE
        viz_set(S_MODEL, 0, 0, 9);                   /* (0.23-t7: P WTABLE's waves, another place; its cards) */
        viz_set(S_MODEL, 0, 1, 20);
        viz_set(S_MODEL, 0, 2, 64);
        viz_set(S_MODEL, 0, 3, 100);
        shot("viz-model-pwtable-2");
        {
            char b[24] = {0};
            card_value(b, sizeof b, sect_par(0, S_MODEL, 0, 1), 20);
            check(!strcmp(b, "B"), "P WTABLE: HARMONICS reads as the bank");
            {
                static const uint8_t V[8] = {0, 9, 18, 36, 64, 80, 100, 127};
                uint32_t k;
                printf("  P WTABLE banks:");
                for (k = 0; k < 8u; k++) {
                    card_value(b, sizeof b, sect_par(0, S_MODEL, 0, 1), V[k]);
                    printf(" %u=%s", V[k], b);
                }
                printf("\n");
            }
            card_value(b, sizeof b, sect_par(0, S_MODEL, 0, 2), 127);
            check(!strcmp(b, "100%"), "P WTABLE: TIMBRE reads as Bright");
            card_value(b, sizeof b, sect_par(0, S_MODEL, 0, 3), 64);
            check(!strcmp(b, "4-5"), "P WTABLE: MORPH reads as the Family, blending");
        }
#endif
        viz_set(S_MODEL, 0, 0, 10);                  /* (P CHORD: the table's waves past half) */
        viz_set(S_MODEL, 0, 3, 110);
        shot("viz-model-pchord-table");
        {
            char b[24] = {0};
            card_value(b, sizeof b, sect_par(0, S_MODEL, 0, 1), 0);
            check(!strcmp(b, "OCTAVE"), "P CHORD: the chord by name");
            card_value(b, sizeof b, sect_par(0, S_MODEL, 0, 3), 20);
            check(!strcmp(b, "ORGAN"), "P CHORD: WAVE's organ half");
        }
    }
    viz_set(S_MODEL, 0, 0, 15);
    viz_page(0, S_OSC, 0);
    shot("viz-model-pitch-osc");                     /* (0.19: COARSE, FINE; WAVE, PWM faint, locked) */
    check(par_unheard(0, sect_par(0, S_OSC, 0, 2)) == 2u && !par_unheard(0, sect_par(0, S_OSC, 0, 0)),
          "a model's OSC: only its pitch heard");
    a = vp_get(sect_par(0, S_OSC, 0, 2));
    knob_turn(2, 5);
    check(vp_get(sect_par(0, S_OSC, 0, 2)) == a, "a model's unheard knob locked");
    viz_page(0, S_AEG, 0);
    check(ui.sect != S_AEG && ui.sect != S_FM && ui.sect != S_CLICK, "a model's AMP EG page hidden");
    viz_page(0, S_MODEL, 0);
    viz_set(S_MODEL, 0, 0, 1);                       /* (Rings, not struck; the morph kit's EXCITER up: faint) */
    vp_set(PAR_MEXC1, 0);
    morph_par[kit_idx(PAR_MEXC1)] = 90;
    viz_page(0, S_FM, 0);
    check(ui.sect == S_FM && par_unheard(0, sect_par(0, S_FM, 0, 0)) == 1u, "Rings: FM faint when morphing strikes it");
    shot("viz-model-morph-fm");
    morph_par[kit_idx(PAR_MEXC1)] = 0;
    viz_page(0, S_MODEL, 0);
    viz_set(S_MODEL, 0, 0, 0);
    viz_page(3, S_MODEL, 0);
    viz_set(S_MODEL, 0, 0, 26);
    shot("viz-model-micro-grain");
    viz_set(S_MODEL, 0, 0, 21);
    shot("viz-model-micro-train");
    viz_set(S_MODEL, 0, 0, 0);
    viz_page(4, S_MODEL, 0);
    shot("viz-model-cy");
    viz_page(5, S_MODEL, 0);
    shot("viz-model-hh");
    viz_page(0, S_AEG, 0);
    shot("viz-units-aeg");
    viz_page(0, S_LFO, 0);
    shot("viz-units-lfo");
    viz_page(4, S_RANDOM, 0);
    viz_set(S_RANDOM, 0, 0, 127);
    viz_set(S_RANDOM, 0, 3, 64);
    shot("viz-random");
    viz_page(0, S_OSC, 0);                            /* (the playhead: the strip, not the picture) */
    if (!sq.running)
        press(B_PLAY);
    run_ms(100);
    a = rsig[R_BODY];
    b = rsig[R_STRIP];
    run_ms(500);
    check(a == rsig[R_BODY] && b != rsig[R_STRIP], "30: playing redraws the step strip, not the picture");
    seq_goto(PG_BOUNCE);
    shot("viz-bounce");
    seq_goto(PG_MELODY);
    vp_set(VP_MEL + ML_SHAPE, MEL_WALK);
    run_ms(1200);
    shot("viz-melody-playing");
    seq_goto(PG_MELODY + 1u);
    vp_set(VP_MEL + ML_DEJA, 100);
    run_ms(600);
    shot("viz-melody-loop");
    vp_set(VP_MEL + ML_SHAPE, MEL_OFF);
    seq_goto(PG_SCALE);
    shot("viz-scale");
    seq_goto(PG_GROOVE);
    vp_set(VP_GROOVE, 7);
    vp_set(VP_GHUM, 60);
    run_ms(40);
    shot("viz-groove-dilla");
    vp_set(VP_GROOVE, 0);
    vp_set(VP_GHUM, 0);
    press(B_FX);
    fx_goto(FXPG_REV);
    vp_set(PAR_REV_TYPE, 1);
    run_ms(40);
    shot("viz-reverb-spring");
    vp_set(PAR_REV_TYPE, 0);
    vp_set(PAR_REV_SIZE, 30);
    run_ms(40);
    shot("viz-reverb-room");
    vp_set(PAR_REV_TYPE, 2);
    vp_set(PAR_REV_SIZE, 120);
    run_ms(40);
    shot("viz-reverb-hall");
    vp_set(PAR_REV_TYPE, 3);
    vp_set(PAR_REV_SIZE, 70);
    run_ms(40);
    shot("viz-reverb-plate");
    fx_goto(FXPG_DLY2);                              /* (1.0: the always-on delay's pages) */
    vp_set(PAR_FX_DELAY_TYPE, 1);
    fx_goto(FXPG_DLY);
    vp_set(PAR_DLY_TIME, 40);
    vp_set(PAR_DLY_FB, 90);
    vp_set(PAR_DLY_LEVEL, 100);
    run_ms(40);
    shot("viz-fx-delay-pp");
    vp_set(PAR_FX_DELAY_TYPE, 3);
    run_ms(40);
    shot("viz-fx-delay-tapepp");
    fx_goto(FXPG_DUB);
    vp_set(PAR_DPITCH, 1);
    vp_set(PAR_GLITCH, 80);
    run_ms(40);
    shot("viz-fx-dub");
    vp_set(PAR_FX_DELAY_TYPE, 1);
    fx_goto(FXPG_DLY2);
    vp_set(PAR_ECHOREV, 90);
    run_ms(40);
    shot("viz-fx-send");
    fx_goto(FXPG_BUS);
#if FX_DRIVE_ON
    vp_set(PAR_FX_TYPE, FX_DRIVE);
    vp_set(PAR_FX_P1, 90);
    run_ms(40);
    shot("viz-fx-drive");
#endif
    vp_set(PAR_FX_TYPE, FX_COMP);
    vp_set(PAR_FX_P3, 60);
    vp_set(VP_FX_SUB, 3);
    run_ms(40);
    shot("viz-fx-comp");
    vp_set(PAR_FX_TYPE, FX_OFF);
    run_ms(40);
    shot("viz-fx-off");
    fx_goto(FXPG_DIRT);
    vp_set(PAR_MDIRT, 2);
    vp_set(PAR_MDIRT_AMT, 90);
    vp_set(PAR_FXDIRT, 50);
    run_ms(40);
    shot("viz-dirt");
    fx_goto(FXPG_COMP);
    vp_set(PAR_MC_RATIO, 4);
    vp_set(PAR_MC_THR, 60);
    run_ms(300);
    shot("viz-master-comp");
    fx_goto(FXPG_COMP2);
    vp_set(PAR_MC_SRC, 1);
    run_ms(40);
    shot("viz-sidechain");
    vp_set(PAR_MC_RATIO, 0);
    vp_set(PAR_MC_SRC, 0);
    vp_set(PAR_MDIRT_AMT, 0);
    vp_set(PAR_FXDIRT, 0);
    press(B_PLAY);
    goto_mode(B_HOME, M_HOME);
}

/* 37: refinements: the config pages' instructions, LOAD with the cores, the song's rows page, the play mode, the
 * patterns' voices (FREE / LOCKED / KIT), the reverb's and P SPEECH's pictures */
static void phase37(void)
{
    uint32_t i;
    printf("phase 37: refinements\n");
    goto_mode(B_GLO, M_CONFIG);
    turn(EN_SELECT, -(int32_t)ui.page);
    for (i = 0; i < NCONFIG; i++) {
        char nm[32];
        snprintf(nm, sizeof nm, "p37-config-%u", (unsigned)(i + 1u));
        run_ms(60);
        shot(nm);
        turn(EN_SELECT, 1);
    }
    {   /* the song: KNOB 2 the row, ROWS: INSERT / DELETE with OCT+, Play */
        uint8_t s0[MAX_SONG_LENGTH];
        memcpy(s0, songs[0], sizeof s0);
        memset(songs[0], SONG_END, MAX_SONG_LENGTH);
        songs[0][0] = 0;
        songs[0][1] = 1;
        songs[0][2] = 2;
        sq.song = 0;
        goto_mode(B_HOME, M_SONG);
        turn(EN_SELECT, -3);
        ui.song_row = 0;
        turn(EN_K2, 1);
        check(ui.page == 0 && ui.song_row == 1u, "SONG: KNOB 2 moves the row");
        turn(EN_SELECT, 1);
        check(ui.page == 1u, "SONG: SELECT opens ROWS");
        run_ms(60);
        shot("p37-song-rows-insert");
        press(B_OCTUP);
        check(song_row(songs[0][1]) == 1u && song_row(songs[0][2]) == 1u && song_row(songs[0][3]) == 2u,
              "ROWS: OCT+ inserts a row (the rows below move down)");
        turn(EN_K3, 2);
        check(song_row(songs[0][1]) == 3u, "ROWS: KNOB 3 picks the new row's pattern");
        turn(EN_K2, 1);
        check(ui.song_edit == SE_DELETE, "ROWS: KNOB 2 to DELETE");
        run_ms(60);
        shot("p37-song-rows-delete");
        press(B_OCTUP);
        check(song_row(songs[0][1]) == 1u && song_row(songs[0][2]) == 2u && song_row(songs[0][3]) == SONG_END,
              "ROWS: OCT+ deletes the row (the rows below move up)");
        turn(EN_K2, -1);
        turn(EN_K4, 1);
        check(sq.song_mode == 1u, "ROWS: Play SONG");
        press(B_PLAY);
        run_ms(100);
        check(sq.running && sq.song_mode && sq.active == 0u, "Play SONG: PLAY plays the song");
        press(B_PLAY);
        turn(EN_K4, -1);
        press(B_PLAY);
        run_ms(100);
        check(sq.running && !sq.song_mode, "Play PATTERN: PLAY plays the pattern");
        press(B_PLAY);
        turn(EN_SELECT, -1);
        memcpy(songs[0], s0, sizeof s0);
    }
    {   /* the patterns' voices: FREE shared, LOCKED its own (slot 32 + n), KIT the saved kit */
        pattern_t p0[3];
        uint8_t k5[KIT_LEN], k33[KIT_LEN], k34[KIT_LEN];
        char n33[KIT_NAME_LEN], n34[KIT_NAME_LEN];
        uint32_t P = kit_par(5), kl = kit_loaded;
        for (i = 0; i < 3u; i++)
            p0[i] = pat[i];
        kit_get(5, k5);
        kit_get(33, k33);
        kit_get(34, k34);
        memcpy(n33, kit_names[33], KIT_NAME_LEN);
        memcpy(n34, kit_names[34], KIT_NAME_LEN);
        if (sq.running)
            press(B_PLAY);
        seq_set_next_pattern(0);
        for (i = 0; i < 3u; i++)
            pat_set_voices(&pat[i], PV_FREE, 0);
        par[P] = 10;
        goto_mode(B_HOME, M_PATTERN);
        run_ms(60);
        shot("p37-pattern-free");
        white(1);
        check(sq.active == 1u && par[P] == 10u, "FREE: the next pattern keeps the voices");
        turn(EN_K1, 1);
        check(pat_voices(&pat[1]) == PV_LOCKED && par[P] == 10u && kit_at(33, 5) == 10u &&
              !memcmp(kit_names[33], "P02 LOCK", 8), "LOCKED: the voices heard kept in kit slot 33 (P02 LOCK)");
        run_ms(60);
        shot("p37-pattern-locked");
        par[P] = 20;
        white(0);
        check(sq.active == 0u && par[P] == 10u, "from a LOCKED pattern to a FREE one: the shared voices back");
        par[P] = 30;
        white(1);
        check(par[P] == 20u, "back to the LOCKED pattern: its own voices, edits kept");
        white(0);
        check(par[P] == 30u, "an edit on a FREE pattern stays shared");
        kit_set1(5, 5, 40);
        kit_loaded = 5;
        turn(EN_K1, 2);
        check(pat_voices(&pat[0]) == PV_KIT && pat_kit(&pat[0]) == 5u && par[P] == 40u,
              "KIT: the pattern plays its saved kit (slot 6)");
        run_ms(60);
        shot("p37-pattern-kit");
        par[P] = 41;
        white(1);
        check(par[P] == 20u, "KIT to LOCKED: the locked voices");
        white(0);
        check(par[P] == 40u, "back to KIT: the saved kit again (edits not kept)");
        white(1);
        {
            int rc = proj_save(1, "VOICES  ");
            par[P] = 99;
            pat_set_voices(&pat[0], PV_FREE, 0);
            rc |= proj_load(1);
            check(rc == 0 && sq.active == 1u && par[P] == 20u && pat_voices(&pat[0]) == PV_KIT,
                  "a project saved on a LOCKED pattern: its voices, the others' modes back");
            seq_set_next_pattern(0);
            check(par[P] == 40u, ".. and KIT's kit");
        }
        pv_set(PV_FREE);
        check(par[P] == 30u, "KIT to FREE: the shared voices, as saved with the project");
        pv_copy(1, 2);
        check(pat_voices(&pat[2]) == PV_LOCKED && kit_at(34, 5) == 20u, "COPY: a LOCKED pattern's voices go with it");
        pat_set_voices(&pat[2], PV_FREE, 0);
        pat_set_voices(&pat[1], PV_FREE, 3);
        par[PAR_PATTERN_MODE] = 1;
        proj_after_load();
        check(pat_voices(&pat[1]) == PV_KIT && pat_kit(&pat[1]) == 3u && par[PAR_PATTERN_MODE] == 0u,
              "an old project with Kit chg AUTO: its patterns on KIT");
        seq_set_next_pattern(0);
        for (i = 0; i < 3u; i++)
            pat[i] = p0[i];
        kit_put(5, k5);
        kit_put(33, k33);
        kit_put(34, k34);
        memcpy(kit_names[33], n33, KIT_NAME_LEN);
        memcpy(kit_names[34], n34, KIT_NAME_LEN);
        kit_load_ram(kl);
        goto_mode(B_HOME, M_HOME);
    }
    goto_mode(B_GLO, M_CONFIG);
    turn(EN_SELECT, (int32_t)CF_LOAD - (int32_t)ui.page);
    {
        static const uint16_t AVG[NLD] = {10, 105, 77, 8, 13, 15, 20}, PK[NLD] = {20, 133, 90, 10, 18, 28, 40};
        for (i = 0; i < NLD; i++) {
            ld_q8[i] = AVG[i];
            ld_pk[i] = PK[i];
        }
        cores_why = CW_TWO;
        run_ms(100);
        shot("p37-load");
        cores_why = CW_BUILD;
        memset(ld_q8, 0, sizeof ld_q8);
    }
    goto_mode(B_HOME, M_HOME);
}


/* 40: WANDER: the pages, the keys, setting out (threads move the voices, Energy the hits), REST, KEEP, a spell's side
 * journey, SHIFT home (the voices back exactly), the JOURNEY's end and its FATE, the mutes at the bottom, KWND */
static uint32_t wd_kit_diff(void)                    /* the kit's values not at home */
{
    uint32_t i, n = 0;
    for (i = 0; i < KIT_LEN; i++)
        n += par[kit_par(i)] != wd_home[i] || morph_par[i] != wd_mhome[i];
    return n;
}
static void wd_bars(uint32_t n) { run_ms(n * 240000u / seq_bpm() + 30u); }
static void wd_slot_copy(pattern_t *c)               /* (41) the slot with its footprints retraced */
{
    uint32_t i;
    *c = pat[wd.slot];
    for (i = wd.nfoot; i--;)
        wd_put(c, wd_fp[i].t & 7u, wd_fp[i].s, &wd_fp[i]);
}
static int wd_slot_home(const pattern_t *home)       /* .. home exactly */
{
    static pattern_t c;
    wd_slot_copy(&c);
    return !memcmp(&c, home, sizeof c);
}
static void phase40(void)
{
    pattern_t p0 = pat[0], pb, pbeat;
    uint8_t k0[KIT_LEN];
    uint16_t kp[4];
    uint32_t i, t, ok, b0;
    char b[40];
    printf("phase 40: wander\n");
    sp_kit(k0);
    if (sq.running)
        press(B_PLAY);
    seq_set_next_pattern(0);
    seq_clear_pattern(&pat[0]);
    for (t = 0; t < NTRACK; t++)                     /* (a beat: every track something) */
        for (i = 0; i < 16u; i++)
            if ((t == 0u && !(i & 3u)) || (t == 3u && (i & 7u) == 4u) || (t == 5u && !(i & 1u)) ||
                (t == 6u && (i & 3u) == 2u) || ((t == 1u || t == 2u || t == 4u) && i == 10u + t))
                main_set(&pat[0], t, i, 1);
    pb = pbeat = pat[0];
    par[PAR_BPM] = 240;
    seq_tempo_changed();
    wd_defaults();
    for (i = 0, ok = 1; i < NQUEST; i++) {           /* (the words fit: the cards, the top lines) */
        wd_card_value(b, sizeof b, WV_QUEST, i);
        ok &= text_w(&AF_S, b) <= 56 && text_w(&AF_M, QUEST[i].name) <= 160 && text_w(&AF_S, QUEST[i].line) <= 236;
    }
    for (i = 0; i < NGEST; i++)
        ok &= text_w(&AF_S, GEST[i].name) <= 62;
    for (i = 0; i < NWV * 8u; i++) {                 /* (every knob's values on its card) */
        if (wd_vdead(i / 8u))
            continue;
        wd_card_value(b, sizeof b, i / 8u, i % 8u > wd_vmax(i / 8u) ? wd_vmax(i / 8u) : i % 8u);
        ok &= text_w(&AF_S, b) <= 56;
    }
    check(ok, "WANDER: the quests' names and the gestures fit");
    perf_goto(PG_WANDER);
    title_text(b, sizeof b, &i, &t);
    page_params(kp);
    check(perf_wander() && !strcmp(b, "WANDER: QUEST") && kp[0] == VP_WD + WV_QUEST && kp[3] == VP_WD + WV_GOAL,
          "ARP: WANDER's first page, QUEST");
    check(wd.k[WV_QUEST] == Q_HOMECOMING && wd.tracks == 0x7Fu && wd.mask == WD_MASK0 && wd.state == WS_IDLE,
          "WANDER at first: HOMECOMING, every track, ENV FILTER FX MOD HITS ACC CHANCE, at home");
    turn(EN_K4, 3);
    check(wd.k[WV_GOAL] == 3u && !vp_dead(VP_WD + WV_GOAL), "GOAL: P03 (41)");
    turn(EN_K4, -3);
    turn(EN_K1, 1);
    check(wd.k[WV_QUEST] == Q_SPIRE && wd.k[WV_FATE] == WF_TREASURE && wd.k[WV_JOURNEY] == 4u,
          "QUEST: THE SPIRE, its FATE (TREASURE) and JOURNEY (256 bars)");
    run_ms(60);
    shot("p40-wander-quest-turned");
    ui.hot = 0;
    /* the keys: black the tracks, white the parts (SHIFT: only), bar 1 ALL / NONE */
    black(2);
    check(!((wd.tracks >> 2) & 1u), "a black key: the track stays home");
    black(2);
    white(4);
    check(!(wd.mask & SC_FILTER), "a white key: FILTER out");
    white(4);
    black(NTRACK);
    check(!wd.tracks, "bar 1: no track");
    press(B_OCTUP);
    check(wd.state == WS_IDLE, "no track: COPY (OCT+) doesn't set out");
    black(NTRACK);
    check(wd.tracks == 0x7Fu, "bar 1 again: every track");
    /* setting out: the voices move (threads, glided), Energy the hits */
    wd.k[WV_JOURNEY] = 1;                            /* (32 bars) */
    wd.k[WV_SOUND] = 1;
    wd.k[WV_RHYTHM] = 1;
    wd.k[WV_RANGE] = 127;
    wd.k[WV_STRIDE] = 127;
    wd.k[WV_FATE] = WF_HOME;
    wd.k[WV_RETURN] = 1;                             /* (1 bar) */
    press(B_PLAY);
    press(B_OCTUP);
    check(wd.state == WS_ROAD && wd.home_p == 0u && wd_kit_diff() == 0u && wd.quest == Q_SPIRE,
          "COPY (OCT+): setting out, home the pattern and the voices");
    {
        uint32_t th = 0, gl = 0, dn = 0, b6;
        for (b6 = 0; b6 < 6u; b6++) {                /* (1.0: threads seen at any bar: the seed is the clock's) */
            uint32_t n6 = 0;
            wd_bars(1);
            for (i = 0; i < NTHREAD; i++)
                n6 += wd.th[i].on;
            th = n6 > th ? n6 : th;
        }
        for (i = 0; i < NGLIDE; i++)
            gl += wgl[i].p != 0;
        for (t = 0; t < NTRACK; t++)
            dn += sq.wdens[t] != 0;
        printf("  6 bars: %u bars, %u kit values moved, %u threads, %u gliding, lvl %d, %u tracks' density\n",
               (unsigned)wd.bars, (unsigned)wd_kit_diff(), (unsigned)th, (unsigned)gl, wd.lvl, (unsigned)dn);
        check(wd.bars >= 5u && wd_kit_diff() > 0u && th >= 1u, "on the road: the threads move the voices");
        check(wd.lvl > 0 && sq.wdens[5] > 0 && sq.wdens[5] >= sq.wdens[0], "THE SPIRE: Energy up, the hats first");
        check(wd_slot_home(&pb) && (wd.cur[WM_L] | wd.cur[WM_S] | wd.cur[WM_G]) != 0,
              "the road's mood moves; its steps all in the footprints");
    }
    {                                                /* (only the parts the white keys let in) */
        uint32_t bad = 0;
        for (i = 0; i < KIT_LEN; i++) {
            uint32_t p = kit_par(i), r;
            if (par[p] == wd_home[i])
                continue;
            for (r = 0; r < NROLE; r++)
                for (t = 0; t < NVOICE; t++)
                    if (sp_role_par(r, t) == p && !(WD_MASK0 & sp_role_cat(r)))
                        bad++;
        }
        check(!bad, "the voices: only ENV, FILTER, FX, MOD moved");
    }
    turn(EN_SELECT, 1);
    run_ms(60);
    shot("p40-wander-pace");
    turn(EN_SELECT, 1);
    run_ms(60);
    shot("p40-wander-home");
    turn(EN_SELECT, -2);
    run_ms(60);
    shot("p40-wander-road");
    /* a knob a thread moves, turned by hand: it lets go */
    for (i = 0; i < NGLIDE && !wgl[i].p; i++)
        ;
    if (i < NGLIDE) {
        uint32_t p = wgl[i].p, th = wgl[i].th;
        par[p] = (uint8_t)(par[p] > 64u ? par[p] - 9u : par[p] + 9u);
        run_ms(5);
        check(!wgl[i].p && (!th || !wd.th[th - 1u].on), "a knob turned by hand: the glide and its thread let go");
    }
    /* REST: the road waits */
    black(NTRACK + 2);
    b0 = wd.bars;
    wd_bars(2);
    check(wd.rest && wd.bars == b0, "bar 3: REST, the road waits");
    run_ms(60);
    shot("p40-wander-rest");
    black(NTRACK + 2);
    wd_bars(1);
    check(!wd.rest && wd.bars > b0, "bar 3 again: on");
    /* a spell: a side journey */
    perf_goto(PG_SPELL);
    sp.sel = SP_DUB;
    sp.tracks = 0x7Fu;
    sp_roll();
    check(wd.side && sp_pending(), "a spell cast on the road: a side journey, the road waits");
    b0 = wd.bars;
    wd_bars(2);
    check(wd.bars == b0, "aside: no bars travelled");
    hold(B_OCTDN);
    release(B_OCTDN);
    check(!wd.side && !sp_pending(), "the spell reversed: on the road again");
    sp_roll();
    perf_goto(PG_WANDER);
    check(wd.side, "cast again, back on WANDER: aside");
    press(B_OCTUP);
    check(!wd.side && wd.state == WS_ROAD, "COPY (OCT+): on the road again, the spell's sound with it");
    /* SHIFT (OCT-): home over RETURN, the voices exactly */
    perf_goto(PG_SPELL);                             /* (a cast still to reverse when the road turns home: its
                                                      * pattern back; one kept, by leaving SPELLS: it stays) */
    wd.rest = 1;                                     /* (resting through the roll: the road's steps stay put) */
    pb = pat[0];
    wd_slot_copy(&pbeat);                            /* (the slot as the road found it: the cast kept above in it) */
    sp_roll();
    wd_home_go(WF_HOME, 0);
    check(wd.state == WS_RETURN && !sp_pending() && !memcmp(&pat[0], &pb, sizeof pb),
          "coming home: a spell still to reverse, its pattern back");
    perf_goto(PG_WANDER);
    run_ms(60);
    shot("p40-wander-coming-home");
    wd_bars(3);
    for (t = 0, ok = 1; t < NTRACK; t++)
        ok &= !sq.wdens[t];
    check(wd.state == WS_IDLE && wd_kit_diff() == 0u && ok && !sq.wmute, "home over a bar: the voices exactly, no Energy");
    check(!memcmp(&pat[0], &pbeat, sizeof pbeat) && !wd.nfoot, "home: the pattern exactly as it was");
    /* SHIFT twice: at once */
    wd.k[WV_RETURN] = 9;                             /* (256 bars) */
    press(B_OCTUP);
    wd_bars(3);
    hold(B_OCTDN);
    release(B_OCTDN);
    hold(B_OCTDN);
    release(B_OCTDN);
    run_ms(40);
    check(wd.state == WS_IDLE && wd_kit_diff() == 0u, "SHIFT (OCT-) twice: home at once");
    /* KEEP: here is home */
    press(B_OCTUP);
    wd_bars(4);
    black(NTRACK + 2);                               /* (resting: nothing glides on) */
    {
        pattern_t pk;
        sq.wdens[5] = 60;                            /* (the hats as busy as they get: ghosts) */
        pat[0].main[5][0] = 0x1111u;                 /* (the hats on the beats: room for ghosts) */
        pk = pat[0];
        black(NTRACK + 1);
        check(wd.state == WS_ROAD && wd_kit_diff() == 0u && !sq.wdens[5] && wd.lvl == 0 &&
              memcmp(pk.main[5], pat[0].main[5], sizeof pk.main[5]), "bar 2: KEEP, here is home (Energy written in)");
    }
    black(NTRACK + 2);
    hold(B_OCTDN);
    release(B_OCTDN);
    hold(B_OCTDN);
    release(B_OCTDN);
    run_ms(40);
    /* the JOURNEY's end: LOST keeps where it ended */
    wd_vset(WV_QUEST, Q_LONG);
    wd.k[WV_JOURNEY] = 0;                            /* (16 bars) */
    wd.k[WV_FATE] = WF_LOST;
    press(B_OCTUP);
    wd_bars(18);
    check(wd.state == WS_IDLE && wd_kit_diff() == 0u, "THE LONG ROAD, 16 bars, LOST: the end, here is home");
    /* TREASURE: home, the leg that moved most kept */
    wd.k[WV_FATE] = WF_TREASURE;
    wd.k[WV_RETURN] = 0;
    press(B_OCTUP);
    wd_bars(10);
    black(NTRACK + 2);                               /* (resting: nothing glides on) */
    {
        uint32_t bt = wd.best_t, bg = wd.best_g, best = wd.best, p = 0, v0 = 0;
        for (i = 0; i < 3u && !p; i++)
            if ((p = wd_par(GEST[bg].r[i], bt)) != 0u)
                v0 = par[p];
        hold(B_OCTDN);
        release(B_OCTDN);
        run_ms(40);
        check(best && wd.state == WS_IDLE && (!p || par[p] == v0), "TREASURE: home, the leg that moved most stays");
    }
    /* THE HOLLOW HILL, RANGE all the way: the colour mutes at the bottom; home unmutes */
    wd_vset(WV_QUEST, Q_HOLLOW);
    wd.k[WV_JOURNEY] = 2;                            /* (64 bars) */
    wd.k[WV_RANGE] = 127;
    wd.k[WV_FATE] = WF_HOME;
    wd.k[WV_RETURN] = 2;
    press(B_OCTUP);
    for (i = 0; i < 40u && !sq.wmute; i++)
        wd_bars(1);
    printf("  THE HOLLOW HILL: muted 0x%02x after %u bars (lvl %d, chapter %u)\n", (unsigned)sq.wmute,
           (unsigned)wd.bars, wd.lvl, (unsigned)wd.chap);
    check(sq.wmute && !(sq.wmute & 0x09u) && sq.wdens[0] >= -20, "THE HOLLOW HILL: the colour mutes, the kick and snare kept");
    run_ms(60);
    shot("p40-wander-hollow");
    hold(B_OCTDN);
    release(B_OCTDN);
    check(!sq.wmute, "SHIFT (OCT-): the mutes lifted");
    wd_bars(4);
    check(wd.state == WS_IDLE && !sq.wdens[5], "home: the hats' density back");
    /* the project: KWND */
    {
        int rc;
        wd.k[WV_STRIDE] = 99;
        wd.tracks = 0x35u;
        wd.mask = SC_FILTER | SC_HITS;
        rc = proj_save(1, "WANDER  ");
        wd_defaults();
        rc |= proj_load(1);
        check(rc == 0 && wd.k[WV_STRIDE] == 99u && wd.k[WV_QUEST] == Q_HOLLOW && wd.tracks == 0x35u &&
              wd.mask == (SC_FILTER | SC_HITS), "the project: WANDER's knobs and keys back");
    }
    press(B_PLAY);
    pat[0] = p0;
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k0[i];
    snd_post_all();
    wd_defaults();
    par[PAR_BPM] = 120;
    seq_tempo_changed();
    goto_mode(B_HOME, M_HOME);
}
static uint32_t wd_pdiff(const pattern_t *a, const pattern_t *b)   /* (41) the steps on in one, not the other */
{
    uint32_t t, s, n = 0;
    for (t = 0; t < NTRACK; t++)
        for (s = 0; s < NUM_STEPS; s++)
            n += main_on(a, t, s) != main_on(b, t, s);
    return n;
}
static void wd_steps(uint32_t n)                     /* (41) n of the rhythm's steps */
{
    uint32_t r = wd.rsteps, i;
    for (i = 0; i < 4000u && n; i++) {
        run_ms(10);
        if (wd.rsteps != r) {
            r = wd.rsteps;
            n--;
        }
    }
}
static void wd_beat(pattern_t *p, int b)             /* (41) two beats: A, B */
{
    uint32_t t, i;
    seq_clear_pattern(p);
    for (t = 0; t < NTRACK; t++)
        for (i = 0; i < 16u; i++) {
            int on = b ? (t == 0u && (i == 0u || i == 6u || i == 8u || i == 11u)) || (t == 3u && (i == 4u || i == 12u || i == 14u)) ||
                             t == 5u || (t == 6u && (i == 2u || i == 10u)) || (t == 4u && i == 0u)
                       : (t == 0u && !(i & 3u)) || (t == 3u && (i & 7u) == 4u) || (t == 5u && !(i & 1u)) ||
                             (t == 6u && (i & 3u) == 2u) || ((t == 1u || t == 2u || t == 4u) && i == 10u + t);
            if (on) {
                main_set(p, t, i, 1);
                p->step[t][i].volume = (uint8_t)(STEP_ACTIVE_MASK | (b && t == 5u ? (i & 1u ? 60u : 100u) : 100u));
            }
        }
}
static void phase41(void)
{
    pattern_t p0 = pat[0], p1 = pat[1], pa, pb, pc;
    uint8_t k0[KIT_LEN];
    uint32_t i, ok, n, seen;
    char b[40];
    printf("phase 41: wander's rhythm and destinations\n");
    sp_kit(k0);
    if (sq.running)
        press(B_PLAY);
    seq_set_next_pattern(0);
    wd_beat(&pat[0], 0);
    wd_beat(&pat[1], 1);
    pa = pat[0];
    pb = pat[1];
    par[PAR_BPM] = 240;
    seq_tempo_changed();
    wd_defaults();
    perf_goto(PG_WANDER);
    /* the cards: GOAL, MEMORY */
    wd_card_value(b, sizeof b, WV_GOAL, 7);
    ok = !strcmp(b, "P07");
    wd_card_value(b, sizeof b, WV_GOAL, 0);
    ok &= !strcmp(b, "NONE");
    wd_card_value(b, sizeof b, WV_MEMORY, 0);
    ok &= !strcmp(b, "NEVER");
    check(ok && !vp_dead(VP_WD + WV_MEMORY) && !vp_dead(VP_WD + WV_ORDER), "GOAL P07 / NONE, MEMORY NEVER; ORDER live (42)");
    for (i = Q_SIDE; i < NQUEST; i++) {              /* (the six new roads) */
        wd_vset(WV_QUEST, i);
        run_ms(40);
        snprintf(b, sizeof b, "p41-quest-%u", (unsigned)i);
        shot(b);
    }
    wd_vset(WV_QUEST, Q_WHEEL);
    check(wd.k[WV_SOUND] == 0u && wd.k[WV_MEMORY] == 96u && wd.k[WV_FATE] == WF_LOST && wd.k[WV_JOURNEY] == WJ_ENDLESS,
          "THE TURNING WHEEL: rhythm only, MEMORY up, LOST, ENDLESS");
    wd_vset(WV_QUEST, Q_HOMECOMING);
    check(wd.k[WV_SOUND] == WD_K0[WV_SOUND] && wd.k[WV_MEMORY] == WD_K0[WV_MEMORY], "away from the wheel: SOUND, MEMORY as at first");
    /* an open road: the rhythm wanders, every step it changes in the footprints */
    wd.k[WV_JOURNEY] = WJ_ENDLESS;
    wd.k[WV_RHYTHM] = 1;
    wd.k[WV_STRIDE] = 127;
    wd.mask = WD_MASK0 | SC_RAT;
    press(B_PLAY);
    press(B_OCTUP);
    for (i = 0, seen = 0; i < 16u; i++) {
        wd_bars(1);
        seen |= wd.rmove[0] != 0;
    }
    n = wd_pdiff(&pat[0], &pa);
    printf("  16 bars: %u steps differ, %u footprints, last move \"%s\"\n", (unsigned)n, (unsigned)wd.nfoot, wd.rmove);
    check(wd.state == WS_ROAD && seen && wd.nfoot && wd_slot_home(&pa), "an open road: the rhythm moves, all of it in the footprints");
    check(!memcmp(&pat[1], &pb, sizeof pb), "the other patterns untouched");
    {                                                /* (the kick and the snare's rhythm stay: not THE ROSARY ..) */
        check(!memcmp(pat[0].main[0], pa.main[0], sizeof pa.main[0]) && !memcmp(pat[0].main[3], pa.main[3], sizeof pa.main[3]),
              "the kick and the snare's hits as they were");
    }
    turn(EN_SELECT, 1);
    run_ms(60);
    shot("p41-wander-pace-rhythm");
    turn(EN_SELECT, -1);
    /* a pattern changed by hand: the one left exactly as it was, the road goes on in the new one */
    seq_set_next_pattern(1);
    wd_bars(2);
    check(sq.active == 1u && !memcmp(&pat[0], &pa, sizeof pa) && wd.slot == 1u && wd.state == WS_ROAD,
          "another pattern by hand: P01 exactly as it was, the road goes on on P02");
    hold(B_OCTDN);                                   /* (home at once: P01) */
    release(B_OCTDN);
    hold(B_OCTDN);
    release(B_OCTDN);
    wd_bars(5);
    check(wd.state == WS_IDLE && sq.active == 0u && !memcmp(&pat[0], &pa, sizeof pa) && !memcmp(&pat[1], &pb, sizeof pb) &&
              wd_kit_diff() == 0u, "home: P01, both patterns exactly as they were, the voices too");
    /* a destination: P01 to P02 over 16 bars */
    wd.k[WV_JOURNEY] = 0;                            /* (16 bars) */
    wd.k[WV_RHYTHM] = 0;                             /* (a destination's pace is the JOURNEY's) */
    turn(EN_K4, 2);
    check(wd.k[WV_GOAL] == 2u, "GOAL: P02");
    run_ms(60);
    shot("p41-wander-goal");
    press(B_OCTUP);
    n = wd.gl;
    wd_bars(8);
    printf("  P01 to P02: %u steps to change, %u after 8 bars\n", (unsigned)n, (unsigned)wd.gl);
    check(wd.goal == 2u && n && wd.gl < n && sq.active == 0u && wd_pdiff(&pat[0], &pb) < wd_pdiff(&pa, &pb),
          "on the way: P01 nearer P02 step by step");
    run_ms(60);
    shot("p41-wander-on-the-way");
    for (i = 0; i < 16u && !wd.arrived; i++)
        wd_bars(1);
    wd_card_value(b, sizeof b, WV_GOAL, wd.k[WV_GOAL]);
    check(wd.arrived && sq.active == 1u && !strcmp(b, "--") && !memcmp(&pat[0], &pa, sizeof pa) &&
              !memcmp(&pat[1], &pb, sizeof pb), "arrived: P02 plays, GOAL reads --, both patterns exactly as they were");
    run_ms(60);
    shot("p41-wander-arrived");
    press(B_OCTUP);
    check(wd.arrived, "arrived: COPY (OCT+) waits for another GOAL");
    /* chained: on to P01 again, by the road */
    turn(EN_K4, -1);
    check(wd.k[WV_GOAL] == 1u && !wd.gdash, "GOAL: P01");
    press(B_OCTUP);
    check(wd.state == WS_ROAD && !wd.arrived && wd.home_p == 0u, "a new course from P02, home still P01");
    for (i = 0; i < 24u && !wd.arrived; i++)
        wd_bars(1);
    check(wd.arrived && sq.active == 0u && !memcmp(&pat[1], &pb, sizeof pb) && !memcmp(&pat[0], &pa, sizeof pa),
          "back at P01 by the road: P02 as it was");
    /* home from another pattern: the rhythm back over RETURN, then home's */
    turn(EN_K4, 1);
    press(B_OCTUP);
    for (i = 0; i < 24u && !wd.arrived; i++)
        wd_bars(1);
    wd.k[WV_RETURN] = 3;                             /* (4 bars) */
    hold(B_OCTDN);
    release(B_OCTDN);
    wd_bars(2);
    check(wd.state == WS_RETURN && sq.active == 1u && wd_pdiff(&pat[1], &pa) < wd_pdiff(&pb, &pa),
          "SHIFT (OCT-) from P02: its rhythm walks back towards home's");
    for (i = 0; i < 10u && wd.state != WS_IDLE; i++)
        wd_bars(1);
    check(wd.state == WS_IDLE && sq.active == 0u && !memcmp(&pat[0], &pa, sizeof pa) && !memcmp(&pat[1], &pb, sizeof pb) &&
              wd_kit_diff() == 0u, "home: P01, both exactly, the voices exactly");
    turn(EN_K4, -2);
    check(!wd.k[WV_GOAL], "GOAL: NONE");
    /* THE CLOCKWORK MAZE: by rule, the same each time */
    wd_vset(WV_QUEST, Q_MAZE);
    wd.k[WV_RHYTHM] = 1;
    wd.k[WV_RETURN] = 0;
    press(B_OCTUP);
    wd_steps(4);
    pc = pat[0];
    check(wd_pdiff(&pc, &pa) && wd_slot_home(&pa), "THE CLOCKWORK MAZE: tracks turned");
    hold(B_OCTDN);
    release(B_OCTDN);
    wd_bars(2);
    check(wd.state == WS_IDLE && !memcmp(&pat[0], &pa, sizeof pa), "home: as it was");
    press(B_OCTUP);
    wd_steps(4);
    check(!memcmp(pat[0].main, pc.main, sizeof pc.main), "again: the same changes (the same seed)");
    hold(B_OCTDN);
    release(B_OCTDN);
    wd_bars(2);
    /* THE ROSARY: the hits leave one by one, then come back */
    wd_vset(WV_QUEST, Q_ROSARY);
    wd.k[WV_JOURNEY] = 1;                            /* (32 bars) */
    wd.k[WV_RHYTHM] = 1;
    press(B_OCTUP);
    for (i = 0, n = 999; i < 16u; i++) {
        uint32_t t, h = 0;
        wd_bars(1);
        for (t = 0; t < NTRACK; t++)
            h += wd_hits(&pat[0], t);
        n = h < n ? h : n;
    }
    printf("  THE ROSARY: down to %u hits (k %u)\n", (unsigned)n, (unsigned)wd.rosk);
    check(n <= 2u && n >= 1u, "THE ROSARY: down to a hit or two by half the road");
    for (i = 0; i < 24u && wd.state != WS_IDLE; i++)
        wd_bars(1);
    check(wd.state == WS_IDLE && !memcmp(&pat[0], &pa, sizeof pa), ".. and back, home");
    /* THE TURNING WHEEL: a beat that comes back to waymarks; its anchors kept */
    wd_vset(WV_QUEST, Q_WHEEL);
    wd.k[WV_RHYTHM] = 1;
    wd.k[WV_MEMORY] = 127;
    press(B_OCTUP);
    for (i = 0, seen = 0; i < 48u; i++) {
        wd_bars(1);
        seen |= !strcmp(wd.rmove, "A PLACE I KNOW");
    }
    for (i = 0, ok = 1; i < 16u; i += 4u)
        ok &= main_on(&pat[0], 0, i) && (i != 4u && i != 12u ? 1 : main_on(&pat[0], 3, i));
    check(seen && ok && wd_slot_home(&pa), "THE TURNING WHEEL: back at a waymark now and then; the kick's beats, the backbeat kept");
    /* POSTCARD: where you are, into the next empty pattern */
    hold(B_OCTDN);
    black(NTRACK + 2);
    release(B_OCTDN);
    for (i = 0, n = 0; i < NTRACK * 4u; i++)         /* (the road may have taken a step since: a hit or two off) */
        n += (uint32_t)__builtin_popcount((uint32_t)(pat[2].main[i / 4u][i % 4u] ^ pat[0].main[i / 4u][i % 4u]));
    check(wd.state == WS_ROAD && !pat_blank(&pat[2]) && n <= 2u && !wd.rest,
          "SHIFT (OCT-) + REST: POSTCARD into P03, still on the road");
    check(pat_voices(&pat[2]) == PV_LOCKED && pat_voices(&pat[0]) != PV_LOCKED,
          "POSTCARD: its voices LOCKED (as they sounded), home's left as they were");
    hold(B_OCTDN);
    release(B_OCTDN);
    hold(B_OCTDN);
    release(B_OCTDN);
    wd_bars(2);
    check(wd.state == WS_IDLE && !memcmp(&pat[0], &pa, sizeof pa), "home");
    seq_clear_pattern(&pat[2]);
    /* a model swap (OSC / MODELS in): the level down, the model, the level back */
    wd_vset(WV_QUEST, Q_HOMECOMING);
    wd.mask |= SC_OSC;
    press(B_OCTUP);
    for (i = 0; i < 200u && !wd.ms_t; i++)
        wd_swap_start();
    if (wd.ms_t) {
        uint32_t v = track_voice(wd.ms_t - 1u), vol = par[PAR_VOL1 + v], md = wd.ms_md;
        wd_bars(5);
        printf("  model swap: voice %u to %u, level %u -> %u\n", (unsigned)v, (unsigned)md, (unsigned)vol, (unsigned)par[PAR_VOL1 + v]);
        check(par[PAR_MODEL1 + v] == md && !wd.ms_t && par[PAR_VOL1 + v] >= vol - 2u, "a model swap: the new model, its level back");
    } else {
        check(0, "a model swap started");
    }
    hold(B_OCTDN);
    release(B_OCTDN);
    hold(B_OCTDN);
    release(B_OCTDN);
    wd_bars(2);
    check(wd.state == WS_IDLE && wd_kit_diff() == 0u, "home: the models back too");
    /* the project: GOAL, MEMORY */
    {
        int rc;
        wd.k[WV_GOAL] = 5;
        wd.k[WV_MEMORY] = 77;
        rc = proj_save(1, "WANDER  ");
        wd_defaults();
        rc |= proj_load(1);
        check(rc == 0 && wd.k[WV_GOAL] == 5u && wd.k[WV_MEMORY] == 77u, "the project: GOAL, MEMORY back");
    }
    press(B_PLAY);
    pat[0] = p0;
    pat[1] = p1;
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k0[i];
    snd_post_all();
    wd_defaults();
    par[PAR_BPM] = 120;
    seq_tempo_changed();
    goto_mode(B_HOME, M_HOME);
}
/* 42: the song's roads: ROADS on the SONG page (a song by its own chances), WANDER's GOAL SONG */
static void song_set(uint32_t s, const uint8_t *r, uint32_t n)
{
    memset(songs[s], SONG_END, MAX_SONG_LENGTH);
    memcpy(songs[s], r, n);
}
static void phase42(void)
{
    static const uint8_t S1[6] = {0, 1, 0, 1, 0, 2}, S2[7] = {0, 1, 2, 0, 3, 1, 4}, S3[4] = {0, 1, 0, 2}, S4[2] = {0, 1};
    uint8_t s0[MAX_SONG_LENGTH], r0 = song_rd[0], k0[KIT_LEN];
    pattern_t p0 = pat[0], p1 = pat[1], pa, pb;
    uint32_t i, ok, n, last, seen, legs[8], bad;
    char b[40];
    printf("phase 42: the song's roads\n");
    memcpy(s0, songs[0], sizeof s0);
    sp_kit(k0);
    if (sq.running)
        press(B_PLAY);
    sq.song = 0;
    /* the picker: the commonest next (SURPRISE 0), order 2, any row at the top */
    song_set(0, S1, sizeof S1);
    for (i = 0, ok = 1; i < 50u; i++)
        ok &= song_pat(0, song_road(0, 4, 0xFF, 0, 0xFF)) == 1u;
    check(ok && song_len(0) == 6u, "ROADS: SURPRISE 0, P01 goes on to its commonest next, P02");
    for (i = 0, seen = 0; i < 200u; i++)
        seen |= 1u << song_pat(0, song_road(0, 4, 0xFF, 64, 0xFF));
    check(seen == 7u, "ROADS: SURPRISE up, P03 (once in the song) now and then");
    song_set(0, S2, sizeof S2);
    for (i = 0, ok = 1; i < 50u; i++)
        ok &= song_pat(0, song_road(0, 5, 4, 0, 0xFF)) == 4u;
    for (i = 0, seen = 0; i < 100u; i++)
        seen |= 1u << song_pat(0, song_road(0, 5, 0xFF, 1, 0xFF));
    check(ok && seen == ((1u << 2) | (1u << 4)), "ROADS: order 2 (P04 P02: P05); order 1 (P02: P03 or P05)");
    for (i = 0, ok = 1; i < 50u; i++)
        ok &= song_pat(0, song_road(0, 0, 0xFF, 0, 1)) != 1u;
    check(ok, "ROADS: a pattern to avoid (the fourth in a row): another");
    /* the SONG page's third page: Way, Surprise, Home */
    song_set(0, S3, sizeof S3);
    song_rd[0] = 0;
    goto_mode(B_HOME, M_SONG);
    turn(EN_SELECT, 2);
    check(ui.page == 2u, "SONG: SELECT to its third page, ROADS");
    turn(EN_K2, 1);
    turn(EN_K3, -4);
    turn(EN_K4, 3);
    check((song_rd[0] & 0x80u) && vp_get(VP_RSUR) == 0u && SONG_HOME[vp_get(VP_RHOME)] == 32u && song_sur(0) == 0u,
          "ROADS: Way ROADS, Surprise 0%, Home 32 bars (this song's)");
    card_value(b, sizeof b, VP_RHOME, vp_get(VP_RHOME));
    check(!strcmp(b, "32 BARS"), "ROADS: the Home card");
    run_ms(60);
    shot("p42-song-roads");
    /* a ROADS song playing: P01 then P02 or P03, then P01 again; it never stops at the END */
    par[PAR_BPM] = 240;
    seq_tempo_changed();
    seq_set_next_pattern(0);
    seq_play_song(1);
    press(B_PLAY);
    last = 0xFF;
    n = seen = bad = 0;
    for (i = 0; i < 2400u && sq.running; i++) {
        if (sq.active != last) {
            if (last != 0xFFu && (last == 0u) == (sq.active == 0u))
                bad++;
            last = sq.active;
            seen |= 1u << last;
            n++;
        }
        run_ms(20);
    }
    printf("  ROADS: %u patterns played, seen %#x, %u wrong turns\n", (unsigned)n, (unsigned)seen, (unsigned)bad);
    check(sq.running && n > 12u && seen == 7u && !bad, "ROADS playing: P01, then P02 or P03 by chance, on and on");
    press(B_PLAY);
    seq_play_song(0);
    {                                                /* (the project keeps each song's ROADS) */
        int rc;
        song_rd[3] = 0xA7;
        rc = proj_save(1, "ROADS   ");
        memset(song_rd, 0, sizeof song_rd);
        rc |= proj_load(1);
        check(rc == 0 && song_rd[3] == 0xA7u && (song_rd[0] & 0x80u), "the project: each song's ROADS back");
        song_rd[3] = 0;
    }
    /* WANDER, GOAL SONG: the song's rows one by one, the voices wandering, the slots as they were */
    song_set(0, S4, sizeof S4);
    song_rd[0] = 0;
    seq_set_next_pattern(0);
    wd_beat(&pat[0], 0);
    wd_beat(&pat[1], 1);
    pa = pat[0];
    pb = pat[1];
    wd_defaults();
    perf_goto(PG_WANDER);
    wd_vset(WV_GOAL, WG_SONG);
    wd_card_value(b, sizeof b, WV_GOAL, WG_SONG);
    ok = !strcmp(b, "SONG");
    wd_card_value(b, sizeof b, WV_ORDER, WO_ROADS);
    ok &= !strcmp(b, "ROADS");
    wd_card_value(b, sizeof b, WV_SWAY, 0);
    ok &= !strcmp(b, "EVEN");
    check(ok, "WANDER: GOAL SONG, ORDER ROADS, SWAY EVEN");
    run_ms(40);
    shot("p42-wander-goal-song");
    wd.k[WV_JOURNEY] = 0;                            /* (16 bars a row) */
    wd.k[WV_ORDER] = WO_ORDER;
    sq.song_mode = 1;
    press(B_PLAY);
    press(B_OCTUP);
    check(wd.state == WS_ROAD && wd_song() && wd.sdest == 1u && wd.sleg == 16u && !sq.song_mode,
          "COPY (OCT+): on the road to the song's next row, P02, 16 bars (WANDER plays the rows)");
    wd_bars(8);
    run_ms(40);
    shot("p42-wander-song-road");
    for (i = 0; i < 12u && sq.active != 1u; i++)
        wd_bars(1);
    check(sq.active == 1u && wd.state == WS_ROAD && !wd.arrived && wd.sdest == 0u && !memcmp(&pat[0], &pa, sizeof pa),
          "there: P02 plays, P01's slot as it was; on to the next row, P01");
    for (i = 0; i < 20u && sq.active != 0u; i++)
        wd_bars(1);
    check(sq.active == 0u && wd.sdest == 1u && !memcmp(&pat[1], &pb, sizeof pb), "and round again: P01, its slot P02 as it was");
    wd.k[WV_SWAY] = 127;                             /* (SWAY WILD: a quarter to four times the JOURNEY) */
    for (i = 0, ok = 1, seen = 0; i < 8u; i++) {
        wd_leg();
        legs[i] = wd.sleg;
        ok &= wd.sleg >= 4u && wd.sleg <= 64u;
        seen |= legs[i] != legs[0];
    }
    printf("  SWAY WILD legs: %u %u %u %u %u %u %u %u\n", (unsigned)legs[0], (unsigned)legs[1], (unsigned)legs[2],
           (unsigned)legs[3], (unsigned)legs[4], (unsigned)legs[5], (unsigned)legs[6], (unsigned)legs[7]);
    check(ok && seen, "SWAY WILD: each row's time between 4 and 64 bars");
    hold(B_OCTDN);
    release(B_OCTDN);
    hold(B_OCTDN);
    release(B_OCTDN);
    for (i = 0; i < 6u && wd.state != WS_IDLE; i++)
        wd_bars(1);
    check(wd.state == WS_IDLE && sq.active == 0u && !memcmp(&pat[0], &pa, sizeof pa) && !memcmp(&pat[1], &pb, sizeof pb),
          "SHIFT (OCT-) twice: home in P01, both slots as they were");
    {                                                /* (an empty song: nothing to follow) */
        memset(songs[0], SONG_END, MAX_SONG_LENGTH);
        press(B_OCTUP);
        check(wd.state == WS_IDLE, "GOAL SONG, no rows: it stays home");
    }
    press(B_PLAY);
    memcpy(songs[0], s0, sizeof s0);
    song_rd[0] = r0;
    pat[0] = p0;
    pat[1] = p1;
    for (i = 0; i < KIT_LEN; i++)
        par[kit_par(i)] = k0[i];
    snd_post_all();
    wd_defaults();
    sq.song_mode = 0;
    par[PAR_BPM] = 120;
    seq_tempo_changed();
    goto_mode(B_HOME, M_HOME);
}

/* 0.23-test11: a step's long lengths (3/4 .. 64 bars, INF) beside its locks; STEP's pages as the Elektrons' */
static void phase_023t11(void)
{
    pattern_t *p;
    uint32_t i, ok;
    printf("0.23-test11: long lengths, INF, the STEP pages\n");
    p4_reset();
    p = &pat[sq.active];
    for (i = 0, ok = 1; i < NLEN_ALL; i++) {         /* (every length beside two locks of the highest codes) */
        lock_set(p, 0, 3, 0, NPAR - 1u);
        lock_set(p, 0, 3, 1, NPAR - 2u);
        step_len_set(p, 0, 3, i);
        ok &= step_len(p, 0, 3) == i && lock_get(p, 0, 3, 0) == NPAR - 1u && lock_get(p, 0, 3, 1) == NPAR - 2u;
        lock_set(p, 0, 3, i & 1u, NO_AUTOMATION);
        ok &= step_len(p, 0, 3) == i;
        ok &= len_knob(len_pos(i)) == i;
    }
    check(ok, "a step's length (all 23) and its two locks kept apart");
    {                                                /* (the arps' orders over 4 tones, as the user's table) */
        static const struct { uint8_t sh; char want[20]; } A[] = {
            {MEL_ARP_UP, "1234"}, {MEL_ARP_DN, "4321"}, {MEL_ARP_UD, "123432"}, {MEL_CONV, "1423"},
            {MEL_DIV, "2314"}, {MEL_CONDIV, "14232314"}, {MEL_LEAP, "1324"}, {MEL_SPIRAL, "1243"},
            {MEL_ALT, "1324"}, {MEL_PAIR, "2143"}, {MEL_ROTATE, "1234234134124123"}, {MEL_INVERT, "12344321"},
            {MEL_PEDAL, "121314"}};
        uint32_t j;
        for (i = 0, ok = 1; i < sizeof A / sizeof A[0]; i++) {
            char got[20];
            int32_t per = arp_per(A[i].sh, 4);
            for (j = 0; j < (uint32_t)per && j < 19u; j++)
                got[j] = (char)('1' + arp_idx(A[i].sh, (int32_t)j, 4, per));
            got[j] = 0;
            if (strcmp(got, A[i].want)) {
                printf("  arp %u: %s, want %s\n", A[i].sh, got, A[i].want);
                ok = 0;
            }
        }
        for (i = MEL_ARP_UP; i < NMEL; i++)          /* (every order in range for 2 .. 38 tones) */
            for (j = 2; j <= 38u && mel_arp(i); j++) {
                int32_t per = arp_per(i, (int32_t)j), k;
                for (k = 0; k < per; k++)
                    ok &= arp_idx(i, k, (int32_t)j, per) >= 0 && arp_idx(i, k, (int32_t)j, per) < (int32_t)j;
            }
        check(ok, "the arps' orders: the table's, and in range");
    }
    seq_clear_pattern(p);
    main_set(p, 0, 0, 1);
    goto_mode(B_SEQ, M_STEP);
    ui.page = 0;
    ui.step = 0;
    ui.track = 0;
    run_ms(40);
    check(P_STEP[0][0] == PAR_STEP_NOTE && P_STEP[0][1] == VP_LEN && P_STEP[0][2] == PAR_STEP_VOLUME &&
          P_STEP[1][0] == PAR_STEP_PROB && P_STEP[1][1] == VP_COND && P_STEP[1][2] == PAR_STEP_SHIFT &&
          P_STEP[1][3] == PAR_STEP_FLAM, "STEP: Note, Length, Velocity; TRIG: Chance, Condition, Shift, Flam");
    kset.accel = 0;
    turn(EN_K2, 12);                                 /* (AUTO + 12: 4 BARS) */
    kset.accel = 1;
    check(step_len(p, 0, 0) == LEN_LONG + 5u, "K2: 4 BARS");
    press(B_PLAY);
    run_ms(4000);                                    /* (2 bars at 120 BPM: still held) */
    check(sq.glen[0] > 0u && sq.glen[0] <= 64u * 24u * 2u + 48u, "4 BARS: held two bars in, two to go");
    kset.accel = 0;
    turn(EN_K2, 30);                                 /* (past INF: TIE, the last) */
    turn(EN_K2, -1);
    kset.accel = 1;
    check(step_len(p, 0, 0) == LEN_INF, "K2: INF before TIE");
    run_ms(8500);
    check(sq.glen[0] == 0u && (sq.gmidi & 1u) && !(sq.tie & 1u), "INF: held, no countdown, not a tie");
    press(B_PLAY);
    check(!(sq.gmidi & 1u), "INF: STOP lets it go");
    perf_goto(PG_WANDER);
    hold(B_OCTDN);
    shot("wander-shift-postcard");
    black(NTRACK + 2);
    check(!strncmp(ui.msg, "POSTCARD SAVED TO P", 19) && pat_voices(&pat[1]) == PV_LOCKED,
          "WANDER: SHIFT + black key 10: POSTCARD SAVED TO P02, its voices LOCKED");
    release(B_OCTDN);
    goto_mode(B_HOME, M_HOME);
}

/* 0.23-test7: a track's BASE note (0.23-t7), note locks, TRANSPOSE (0.23-t7), RUSH by the turn's speed (0.23-t7) */
/* 0.24: the CC map (the library page's; project.c kset.ccmap, midi.c midi_cc) */
static void phase_024(void)
{
    uint32_t g = midi_global_ch(), mp = 256u + 128u;   /* (NRPN 256: D1's model, a short list) */
    elixir_settings_t keep = kset;
    printf("0.24: the CC map\n");
    p4_reset();
    midi_gtakes = GT_ALL;                            /* (the map on the global channel: as the LXR's) */
    memset(kset.ccmap, 0, sizeof kset.ccmap);
    par[PAR_COARSE1] = 40;
    web_midi(0xB0 | g, 9, 50);
    run_ms(10);
    check(par[PAR_COARSE1] == 50, "no map: CC 9 is D1 coarse, as the LXR's");
    kset.ccmap[1] = 9;                               /* the mod wheel -> D1 coarse */
    kset.ccmap[9] = CCMAP_OFF;
    web_midi(0xB0 | g, 9, 70);
    run_ms(10);
    check(par[PAR_COARSE1] == 50, "CC 9 mapped to nothing: ignored");
    web_midi(0xB0 | midi_track_ch(0), 1, 90);
    run_ms(10);
    check(par[PAR_COARSE1] == 90, "CC 1 mapped to D1 coarse: on D1's channel");
    web_midi(0xB0 | midi_track_ch(3), 1, 20);
    run_ms(10);
    check(midi_track_ch(3) == midi_track_ch(0) || par[PAR_COARSE1] == 90, "CC 1 mapped: not on SN's channel");
    kset.ccmap[20] = (uint16_t)(mp + 1u);            /* CC 20 -> NRPN 256, stretched */
    web_midi(0xB0 | g, 20, 127);
    run_ms(10);
    check(par[mp] == par_max(mp), "a short list stretched: 127 is its last");
    web_midi(0xB0 | g, 20, 0);
    run_ms(10);
    check(par[mp] == par_min(mp), "0 is its first");
    kset.ccmap[6] = kset.ccmap[98] = kset.ccmap[99] = 9;   /* (NRPN's CCs: never mapped) */
    par[PAR_COARSE1] = 33;
    web_midi(0xB0 | g, 99, 2);                       /* (NRPN 256: MSB 2, LSB 0) */
    web_midi(0xB0 | g, 98, 0);
    web_midi(0xB0 | g, 6, 2);
    run_ms(10);
    check(par[PAR_COARSE1] == 33 && par[mp] == 2, "CC 6 / 98 / 99 stay NRPN's");
    kset.ccmap[6] = kset.ccmap[98] = kset.ccmap[99] = 0;
    st_save(OBJ_SETTINGS, &kset, sizeof kset);
    memset(kset.ccmap, 0, sizeof kset.ccmap);
    persist_boot();
    check(kset.ccmap[1] == 9 && kset.ccmap[9] == CCMAP_OFF && kset.ccmap[20] == mp + 1u, "the map kept at power-on");
    st_save(OBJ_SETTINGS, &kset, SET_LEN_016);       /* settings from 0.16 .. 0.23: no map */
    persist_boot();
    check(kset.ccmap[1] == 9, "settings from 0.23 (20 bytes) read, the map left as it was");
    kset = keep;
    st_save(OBJ_SETTINGS, &kset, sizeof kset);
    persist_boot();
    check(kset.ccmap[1] == 0 && kset.ccmap[20] == 0, "the map cleared again");
}

/* 0.24: Global in (CONFIG: MIDI CHANNELS): KEYS, PAT+KIT, ALL, OFF */
static int is_factory(uint32_t f)                    /* the edit buffer holds factory kit f */
{
    uint8_t fk[KIT_LEN];
    uint32_t i, same = 1;
    kit_factory_values(f, fk);
    for (i = 0; i < KIT_LEN; i++)
        same &= par[kit_par(i)] == fk[i];
    return (int)same;
}
static double note_rms(uint32_t ch)                  /* a note on ch, after quiet: the output */
{
    run_ms(1500);
    web_midi(0x90 | ch, 60, 100);
    return out_rms(150);
}
static void phase_024g(void)
{
    uint32_t g = midi_global_ch(), t1 = midi_track_ch(0);
    double q;
    printf("0.24: what the global channel takes\n");
    p4_reset();
    if (sq.running)
        press(B_PLAY);
    check(g != t1, "(the global channel isn't D1's)");
    ui.track = 0;
    /* KEYS */
    vp_set(VP_GTAKES, GT_KEYS);
    par[PAR_COARSE1] = 40;
    web_midi(0xB0 | g, 9, 50);
    run_ms(10);
    check(par[PAR_COARSE1] == 40, "KEYS: a sound CC on the global channel ignored");
    web_midi(0xB0 | t1, 9, 50);
    run_ms(10);
    check(par[PAR_COARSE1] == 50, "KEYS: .. on D1's own channel as before");
    web_midi(0xC0 | g, 5, 0);
    run_ms(10);
    check(sq.active == 5, "KEYS: program change 5 -> pattern 6");
    web_midi(0xB0 | g, 0, 9);                        /* (a keyboard's bank select first) */
    web_midi(0xB0 | g, 32, 0);
    run_ms(10);
    check(kit_loaded != 9, "KEYS: CC 0 / 32 (bank select) load nothing");
    web_midi(0xB0 | g, 102, 3);
    run_ms(10);
    check(kit_loaded == 3, "KEYS: CC 102 = 3 -> slot kit 4");
    web_midi(0xB0 | g, 103, 7);
    run_ms(10);
    check(is_factory(7) && kit_loaded == 3, "KEYS: CC 103 = 7 -> factory kit 8 (the slot stays)");
    web_midi(0xB0 | g, 103, 127);
    run_ms(10);
    check(is_factory(7), "KEYS: CC 103 past the 85 factory kits: nothing");
    web_midi(0xB0 | t1, 1, 7);
    run_ms(10);
    check(is_factory(7), "KEYS: CC 1 on a track's channel: nothing (a mod wheel)");
    pfs.m[0].par = PAR_COARSE1;                      /* MACRO 1 on D1's pitch, 0..127 */
    pfs.m[0].lo = 0;
    pfs.m[0].hi = 127;
    web_midi(0xB0 | g, 1, 100);
    run_ms(20);
    check(pfs.m[0].val == 100 && par[PAR_COARSE1] == 100, "KEYS: the mod wheel moves MACRO 1");
    pfs.m[0].par = 0;
    web_midi(0xE0 | g, 0, 127);                      /* (pitch bend: nothing, safely) */
    run_ms(10);
    q = note_rms(g);
    printf("  KEYS: a note on the global channel: RMS %.1f\n", q);
    check(q > 50.0, "KEYS: a note plays the selected track");
    /* PAT+KIT */
    vp_set(VP_GTAKES, GT_PATKIT);
    {
        double q0;                                   /* (1.0: against the quiet as it is: a state left by WANDER's
                                                      * road can leave an LSB of DC) */
        run_ms(1500);
        q0 = out_rms(150);
        q = note_rms(g);
        check(q < q0 + 1.0, "PAT+KIT: a note on the global channel plays nothing");
    }
    web_midi(0xB0 | g, 1, 20);
    run_ms(20);
    check(pfs.m[0].val == 100, "PAT+KIT: the wheel moves nothing");
    web_midi(0xC0 | g, 6, 0);                        /* (the pattern first: a pattern can bring its kit) */
    run_ms(10);
    web_midi(0xB0 | g, 102, 4);
    run_ms(10);
    check(kit_loaded == 4 && sq.active == 6, "PAT+KIT: kits and patterns");
    /* OFF */
    vp_set(VP_GTAKES, GT_OFF);
    web_midi(0xC0 | g, 7, 0);
    web_midi(0xB0 | g, 102, 9);
    run_ms(10);
    check(sq.active == 6 && kit_loaded == 4, "OFF: the global channel ignored");
    web_midi(0xB0 | t1, 0, 9);
    run_ms(10);
    check(kit_loaded == 9, "OFF: CC 0 on a track's channel still loads a kit (the LXR's)");
    /* ALL */
    vp_set(VP_GTAKES, GT_ALL);
    web_midi(0xB0 | g, 0, 2);                        /* (the kit first: it sets the sound) */
    run_ms(10);
    web_midi(0xB0 | g, 9, 77);
    run_ms(10);
    check(par[PAR_COARSE1] == 77 && kit_loaded == 2, "ALL: the LXR's way, a sound CC and CC 0's kit");
    q = note_rms(g);
    check(q > 50.0, "ALL: .. and a note plays the selected track");
    check(kset.gtakes == GT_ALL, "kept in the settings");
    /* 0.24.2: the basics on the remote's channels (15, 16) and CC 126 / 127 on the global one */
    {
        uint32_t rt = remote_ch, rg = remote_gch;
        check(rt == 14u && rg == 15u, "(the remote: channels 15 and 16)");
        web_midi(0xC0 | rt, 3, 0);
        run_ms(10);
        check(sq.active == 3, "remote TRACKS: program change 3 -> pattern 4");
        web_midi(0xC0 | rg, 8, 0);
        run_ms(10);
        check(sq.active == 8, "remote GLOBAL: program change 8 -> pattern 9");
        web_midi(0xB0 | rt, 126, 5);
        run_ms(10);
        check(kit_loaded == 5, "remote TRACKS: CC 126 = 5 -> slot kit 6");
        web_midi(0xB0 | rg, 127, 11);
        run_ms(10);
        check(is_factory(11) && kit_loaded == 5, "remote GLOBAL: CC 127 = 11 -> factory kit 12");
        web_midi(0xB0 | rg, 126, 6);
        run_ms(10);
        check(kit_loaded == 6, "remote GLOBAL: CC 126 = 6 -> slot kit 7");
        vp_set(VP_GTAKES, GT_KEYS);
        web_midi(0xB0 | g, 126, 7);
        run_ms(10);
        check(kit_loaded == 7, "KEYS: CC 126 = 7 -> slot kit 8 (as CC 102)");
        web_midi(0xB0 | g, 127, 2);
        run_ms(10);
        check(is_factory(2) && kit_loaded == 7, "KEYS: CC 127 = 2 -> factory kit 3 (as CC 103)");
        vp_set(VP_GTAKES, GT_ALL);
    }
    {   /* the card: CONFIG, MIDI CHANNELS (2), KNOB 4 */
        uint16_t pp[4];
        goto_mode(B_GLO, M_CONFIG);
        turn(EN_SELECT, 3 - (int32_t)ui.page);
        page_params(pp);
        check(pp[3] == VP_GTAKES, "CONFIG: MIDI CHANNELS (2), KNOB 4: Global in");
        vp_set(VP_GTAKES, GT_KEYS);
        run_ms(100);
        shot("config-global-in");
        vp_set(VP_GTAKES, GT_ALL);
        press(B_HOME);
    }
    web_midi(0xC0 | g, 0, 0);
    run_ms(10);
}

static void phase_t7(void)
{
    pattern_t *p;
    uint16_t pp[4];
    uint32_t n0, n1;
    printf("0.23-test7: BASE, note locks, TRANSPOSE, RUSH speed\n");
    p4_reset();
    if (sq.running)
        press(B_PLAY);
    p = &pat[sq.active];
    sq.muted = 0;
    kset.accel = 0;
    track_select(2);
    p->step[2][0].note = SEQ_DEFAULT_NOTE + 2u;      /* (a melody's note, plain) */
    p->step[2][1].note = (uint8_t)((SEQ_DEFAULT_NOTE + 7u) | NOTE_LOCK);   /* (locked: G4) */
    seq_goto(PG_SCALE);
    page_params(pp);
    check(pp[3] == VP_TBASE, "SCALE: KNOB 4 the track's BASE");
    turn(EN_K4, 5);
    check(p->tbase[2] == 5 && snote(p, 2, 0) == SEQ_DEFAULT_NOTE + 7u && snote(p, 2, 1) == SEQ_DEFAULT_NOTE + 7u,
          "BASE +5: a plain note moves, a locked one stays");
    shot("scale-base");
    n0 = note_keep(2, SEQ_DEFAULT_NOTE + 9u);
    check(n0 == SEQ_DEFAULT_NOTE + 4u && tb_note(2) == SEQ_DEFAULT_NOTE + 5u, "BASE: a played note kept as it plays back");
    ui.step = 0;
    step_note_lock(p, 2, 0, SEQ_DEFAULT_NOTE + 3u);
    check(p->step[2][0].note == ((SEQ_DEFAULT_NOTE + 3u) | NOTE_LOCK), "STEP Note: a lock");
    step_note_lock(p, 2, 0, SEQ_DEFAULT_NOTE + 5u);
    check(p->step[2][0].note == SEQ_DEFAULT_NOTE, "STEP Note at BASE: plain again");
    p->tbase[2] = 0;
    p->step[2][0].note = p->step[2][1].note = SEQ_DEFAULT_NOTE;
    /* TRANSPOSE: degrees of the pattern's scale */
    goto_mode(B_ARP, M_PERF);
    ui.page = PG_TRP;
    ui.force = 1;
    run_ms(40);
    page_params(pp);
    check(pp[0] == VP_TRP && pp[1] == VP_TRWHEN && pp[2] == VP_TRHOLD, "PERF: TRANSPOSE's page");
    p->scale = SC_CHROM;
    sq.trmask = 0;
    black(2);
    check(sq.trmask == 4u && ui.track == 2u, "TRANSPOSE: a black key, its track");
    white(10);
    check(sq.trp[2] == 3 && trp_fit(2, SEQ_DEFAULT_NOTE) == SEQ_DEFAULT_NOTE + 3u && trp_fit(1, SEQ_DEFAULT_NOTE) ==
          SEQ_DEFAULT_NOTE, "TRANSPOSE CHROMATIC: +3 semitones, the others not");
    p->scale = SC_MAJOR;
    p->sroot = 0;
    {
        int32_t c = (int32_t)(vc[2].osc.midi_freq >> 8), d = scale_q(SC_MAJOR, 0, c, 0) - c;
        n1 = trp_fit(2, (uint32_t)(SEQ_DEFAULT_NOTE + d));
        check((int32_t)n1 - (SEQ_DEFAULT_NOTE + d) == scale_q(SC_MAJOR, 0, c + d, 3) - (c + d),
              "TRANSPOSE MAJOR: 3 degrees up");
    }
    shot("transpose");
    white(7);
    check(sq.trp[2] == 0, "TRANSPOSE: the middle key, back to 0");
    sq.trhold = 1;
    keys |= 1u << WHITE_KEY[0];
    web_keys(keys);
    run_ms(40);
    check(sq.trp[2] == -7, "TRANSPOSE HOLD: an octave down while held");
    keys &= ~(1u << WHITE_KEY[0]);
    web_keys(keys);
    run_ms(40);
    check(sq.trp[2] == 0, "TRANSPOSE HOLD: let go, back");
    sq.trhold = 0;
    sq.trwhen = 2;
    press(B_PLAY);
    run_ms(100);
    white(9);
    check(sq.trp[2] == 0 && sq.trdue && sq.trpend[2] == 2, "TRANSPOSE WHEN BAR: waits");
    run_ms(2500);
    check(sq.trp[2] == 2 && !sq.trdue, "TRANSPOSE WHEN BAR: on the bar");
    press(B_PLAY);
    white(7);
    sq.trwhen = 0;
    sq.trmask = 0;
    p->scale = SC_CHROM;
    /* RUSH: slow 1% a detent, fast more */
    ui.page = PG_RUSH;
    ui.force = 1;
    run_ms(40);
    sq.ru_live = 0;
    turn(EN_K1, 3);
    {
        uint32_t slow = sq.ru_live, i;
        run_ms(200);
        for (i = 0; i < 10u; i++) {
            web_enc(EN_K1, 1);
            run_ms(4);
        }
        check(slow <= 6u && sq.ru_live >= slow + 30u, "RUSH: slow turns fine, fast ones far");
    }
    sq.ru_live = 0;
}
/* 0.24.1: Wyrd's data on the unit (a project, a library entry beside one of Elixir's kits, settings): asked at
 * power-on, nothing written while kept, asked again next power-on; erased: only Wyrd's records go */
/* 1.0 Phase 2: the buttons' conventions. SHIFT (OCT-) the menu on every page but PERF's, its keys 8..13; a knob
 * turned with it held fine (no acceleration); HOME's third page FILES; OCT+ held on the length: every track */
static void phase_100_2(void)
{
    uint32_t t, v, k5[NTRACK];
    pattern_t *p;
    printf("1.0 Phase 2: menus and buttons\n");
    p4_reset();
    goto_mode(B_ARP, M_PERF);                        /* (PERF visited: ui.perf on the sound pages after) */
    press(B_ENV);
    check(ui.mode == M_SOUND && ui.perf, "a sound page opened from PERF");
    hold(B_OCTDN);
    check(shift_menu(), "SHIFT on a sound page from PERF: the menu, not PERFORM");
    shot("shift-menu");
    white(7);
    release(B_OCTDN);
    check(ui.mode == M_LOAD && ui.io_type == IO_KIT && !ui.pf_latch, "SHIFT + 8 from there: LOAD KIT, nothing latched");
    goto_mode(B_ARP, M_PERF);
    hold(B_OCTDN);
    check(!shift_menu(), "SHIFT on PERF's own pages: its own (latches)");
    release(B_OCTDN);
    /* RESET SOUND (10): the track's voice as the kit slot has it */
    goto_mode(B_ENV, M_SOUND);
    black(0);
    kit_save_ram(5, 0);
    kit_load_ram(5);
    v = par[PAR_TILT1];
    par_set(PAR_TILT1, v > 60u ? 10u : 100u);
    par_set(PAR_TILT1 + 1u, 77);                     /* (another voice's: stays) */
    hold(B_OCTDN);
    white(9);
    release(B_OCTDN);
    check(par[PAR_TILT1] == v && par[PAR_TILT1 + 1u] == 77u, "SHIFT + 10: RESET SOUND, the track's voice as saved");
    hold(B_OCTDN);                                   /* SOLO (12), again off */
    white(11);
    check(sq.solo == 1u, "SHIFT + 12: SOLO the track");
    white(11);
    release(B_OCTDN);
    check(sq.solo == 0u, "SHIFT + 12 again: not");
    t = ui.mode;
    hold(B_OCTDN);                                   /* (14..16: nothing now; 10's SONG, 12's CONFIG gone) */
    white(13);
    white(14);
    white(15);
    release(B_OCTDN);
    check(ui.mode == t, "SHIFT + 14..16: free");
    /* fine adjust: OCT- held, a detent a step; the page stays (the menu hidden), its release does nothing */
    kset.accel = 1;
    goto_mode(B_HOME, M_HOME);
    v = vp_get(PAR_BPM);
    turn(EN_K1, 6);
    check(vp_get(PAR_BPM) > v + 6u, "a quick turn: accelerated");
    v = vp_get(PAR_BPM);
    hold(B_OCTDN);
    turn(EN_K1, -6);
    check(vp_get(PAR_BPM) == v - 6u && ui.fine && !(shift_menu() && !ui.fine), "OCT- held: fine, a step a detent, the page shown");
    shot("fine-adjust");
    release(B_OCTDN);
    check(!ui.fine && ui.mode == M_HOME, "OCT- let go: nothing else");
    /* HOME: GRID > PATTERNS > LOAD > SONG. LOAD's Type: KIT, MORPH, SOUND, PROJECT; KIT From SLOTS, LIBRARY,
     * FACTORY; SOUND's knobs Type, Track, Slot. SAVE's Type: the same, then PATTERN, SONG */
    {
        uint16_t pp[4];
        press(B_HOME);
        check(ui.mode == M_PATTERN, "HOME twice: the patterns");
        ui.io_type = IO_SONG;
        press(B_HOME);
        check(ui.mode == M_LOAD && ui.io_type == IO_KIT, "HOME three times: LOAD (no SONG there: KIT)");
        check(vp_max(VP_IO_TYPE) == 3u && vp_get(VP_IO_TYPE) == 0u, "LOAD: four types, KIT first");
        turn(EN_K1, 1);
        check(ui.io_type == IO_MORPH, "LOAD: MORPH after KIT");
        turn(EN_K1, 1);
        page_params(pp);
        check(ui.io_type == IO_SOUND && pp[1] == VP_IO_TRACK && pp[2] == VP_IO_SLOT, "LOAD SOUND: Type, Track, Slot");
        turn(EN_K1, 1);
        check(ui.io_type == IO_PROJECT, "LOAD: PROJECT last");
        turn(EN_K1, -3);
        check(ui.io_type == IO_KIT && vp_max(VP_IO_SRC) == 2u, "LOAD KIT: From SLOTS, LIBRARY, FACTORY");
        turn(EN_K2, 2);
        check(ui.io_type == IO_FACTORY && vp_get(VP_IO_TYPE) == 0u, "From FACTORY: the factory kits, still KIT");
        shot("load-kit-factory");
        turn(EN_K2, -2);
        check(ui.io_type == IO_KIT && !ui.io_src, "From SLOTS again");
        {                                            /* MORPH From FACTORY: a factory kit as the morph target */
            uint8_t want[KIT_LEN], kitpar[KIT_LEN];
            uint32_t i, same = 1, kept = 1;
            turn(EN_K1, 1);
            check(ui.io_type == IO_MORPH && vp_max(VP_IO_SRC) == 2u, "LOAD MORPH: From SLOTS, LIBRARY, FACTORY");
            turn(EN_K2, 2);
            check(ui.io_type == IO_FMORPH && vp_get(VP_IO_TYPE) == 1u && vp_get(VP_IO_SRC) == 2u &&
                      vp_max(VP_IO_SLOT) == FACTORY_NKITS - 1u, "MORPH From FACTORY: still MORPH, the kits (no ALL)");
            shot("load-morph-factory");
            for (i = 0; i < KIT_LEN; i++)
                kitpar[i] = par[kit_par(i)];
            vp_set(VP_IO_SLOT, 3);
            io_ok();
            kit_factory_values(3, want);
            for (i = 0; i < KIT_LEN; i++) {
                same &= morph_par[i] == want[i];
                kept &= par[kit_par(i)] == kitpar[i];
            }
            check(same && kept, "MORPH From FACTORY: factory kit 4 the morph target, the kit as it was");
            turn(EN_K2, -2);
            check(ui.io_type == IO_MORPH && !ui.io_src, "MORPH From SLOTS again");
            turn(EN_K1, -1);
        }
        press(B_HOME);
        check(ui.mode == M_SONG, "LOAD, HOME: the song");
        press(B_HOME);
        check(ui.mode == M_HOME, "the song, HOME: the grid");
        press(B_SAVE);
        check(ui.mode == M_SAVE && vp_max(VP_IO_TYPE) == 5u, "SAVE: six types");
        turn(EN_K1, 4);
        check(ui.io_type == IO_PATTERN, "SAVE: PATTERN after PROJECT");
        press(B_SAVE);
        check(ui.mode == M_LOAD && ui.io_type == IO_KIT, "SAVE PATTERN, then LOAD: KIT");
        goto_mode(B_HOME, M_HOME);
    }
    /* Control All on the length: OCT+ held, every track to the shown one's; let go, OCT+ reverts, OCT- keeps */
    kset.accel = 0;
    seq_goto(0);
    p = shown_pat();
    for (t = 0; t < NTRACK; t++)
        p->len[t] = (uint8_t)(16u + t * 4u);
    for (t = 0; t < NTRACK; t++)
        k5[t] = p->len[t];
    black(2);
    hold(B_OCTUP);
    turn(EN_K1, -4);
    for (v = 1, t = 0; t < NTRACK; t++)
        v &= p->len[t] == 20u;
    check(v && ui.ca && ui.ca_len, "OCT+ held, the length turned: every track's 20");
    release(B_OCTUP);
    check(ui.ca_ask && ui.ca_len, "let go: asked");
    shot("ca-length-ask");
    press(B_OCTUP);
    for (v = 1, t = 0; t < NTRACK; t++)
        v &= p->len[t] == k5[t];
    check(v && !ui.ca && !ui.ca_ask && !ui.ca_len, "OCT+: the lengths as they were");
    hold(B_OCTUP);
    turn(EN_K1, 4);
    release(B_OCTUP);
    press(B_OCTDN);
    for (v = 1, t = 0; t < NTRACK; t++)
        v &= p->len[t] == 28u;
    check(v && !ui.ca_ask && !ui.ca_len, "OCT-: kept, every track's 28");
    /* the generator's name and what it does over the page a while */
    seq_goto(PG_GEN);
    turn(EN_K1, 1);
    check(gen_pop(), "GENERATOR turned: its name and words shown");
    shot("gen-pop");
    run_ms(DEST_POP_MS + 100u);
    check(!gen_pop(), "a while after: the page again");
    turn(EN_K1, -1);
}

static void phase_0241(void)
{
    uint32_t ph, pg, sec, w, i, wpg = 0xFFFFu, cp;
    uint8_t used[PROJ_NPHYS] = {0}, kit[KIT_LEN], back[KIT_LEN];
    kl_hdr_t h;
    st_hdr_t a, b;
    int ha, hb;
    printf("0.24.1: another app's data\n");
    p4_reset();
    check(fx_found == 0 && !data_locked, "Elixir's own data: nothing asked");
    check(proj_save(3, "MINE    ") == 0, "an Elixir project saved");
    check(libkit_save(10, "MYKIT   ", 0) == 0 && kl_exists(KLK_KIT, 10), "an Elixir kit in the library");
    check(kl_peek(KLK_KIT, 10, kit, KIT_LEN) >= 0, "its bytes read");
    for (i = 0; i < PROJ_NLOG; i++)
        if (pj.phys[i] >= 0)
            used[pj.phys[i]] = 1;
    for (ph = 0; ph < PROJ_NPHYS && used[ph]; ph++)
        ;
    check(ph < PROJ_NPHYS, "a free project slot for Wyrd's");
    memset(nor + phys_base(ph), 0x5A, 4096);         /* Wyrd's project: "WYRD" and its bytes */
    memcpy(nor + phys_base(ph), "WYRD", 4);
    sec = (uint32_t)kl.at[KLK_KIT - 1u][10] / 16u;   /* Wyrd's kit, in the sector of Elixir's */
    for (pg = sec * 16u; pg < sec * 16u + 16u && wpg == 0xFFFFu; pg++)
        if (kl_is_erased(pg))
            wpg = pg;
    check(wpg != 0xFFFFu, "room for Wyrd's kit beside Elixir's");
    memset(&h, 0, sizeof h);
    h.magic = 0x5957u;
    h.kind = 1;
    h.slot = 10;
    h.seq = 1;
    memcpy(h.name, "WYRDKIT ", 8);
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + kl_off(wpg), &h, sizeof h);
    ha = st_head(OBJ_SETTINGS, 0, &a);               /* Wyrd's settings, in the copy Elixir's isn't in */
    hb = st_head(OBJ_SETTINGS, 1, &b);
    cp = ha ? 0u : hb ? 1u : (int32_t)(a.seq - b.seq) > 0 ? 1u : 0u;
    memset(nor + st_sector(OBJ_SETTINGS, cp), 0xFF, 4096);
    memcpy(nor + st_sector(OBJ_SETTINGS, cp), "WYST", 4);
    persist_boot();
    ui.dx_seen = 0;
    run_ms(100);
    check(fx_found == (FX_PROJ | FX_KITS | FX_SET) && fx_apps == 1u && data_locked, "power-on: Wyrd's data found, locked");
    check(ui.dx_ask == 1u && !strcmp(fx_name(), "WYRD'S"), "the screen asks");
    check(fx_kl(sec) != 0u, "kl_scan left Wyrd's kit (its sector not erased)");
    shot("wyrd-data-ask");
    w = web_flash_writes;
    turn(EN_K1, 3);
    press(B_ARP);
    check(ui.dx_ask == 1u, "the keys and knobs wait");
    press(B_OCTDN);
    check(ui.dx_ask == 0u && data_locked && !strcmp(ui.msg, "KEPT: ELIXIR SAVES NOTHING"), "SHIFT (OCT-): kept");
    check(proj_save(2, "NEW     ") == ST_LOCKED && libkit_save(11, "NEWKIT  ", 0) == ST_LOCKED, "saves refused");
    settings_later();
    par_set(PAR_COARSE1, 77);
    run_ms(40000);                                   /* (an autosave's time) */
    check(web_flash_writes == w, "kept: nothing written, not even the autosave or the settings");
    check(!memcmp(nor + phys_base(ph), "WYRD", 4) && fx_kl(sec) && fx_set(cp), "kept: Wyrd's records as they were");
    persist_boot();
    ui.dx_seen = 0;
    run_ms(100);
    check(ui.dx_ask == 1u && data_locked, "asked again at the next power-on");
    press(B_OCTUP);
    check(ui.dx_ask == 2u, "COPY (OCT+) once: are you sure");
    shot("wyrd-data-sure");
    press(B_OCTUP);
    run_ms(300);
    check(ui.dx_ask == 0u && fx_found == 0u && !data_locked && !strcmp(ui.msg, "WYRD'S DATA ERASED"),
          "COPY (OCT+) twice: Wyrd's data erased, unlocked");
    check(fx_word(phys_base(ph)) == 0xFFFFFFFFu && !fx_kl(sec) && !fx_set(cp), "Wyrd's project, kit, settings gone");
    check(proj_exists(3) && kl_exists(KLK_KIT, 10) && kl_peek(KLK_KIT, 10, back, KIT_LEN) >= 0 &&
          !memcmp(kit, back, KIT_LEN), "Elixir's project and kit kept (the kit moved out of the shared sector)");
    run_ms(3000);
    check(proj_save(2, "NEW     ") == 0 && libkit_save(11, "NEWKIT  ", 0) == 0, "saves work again");
    persist_boot();
    ui.dx_seen = 0;
    run_ms(100);
    check(fx_found == 0u && ui.dx_ask == 0u && proj_exists(2) && kl_exists(KLK_KIT, 11), "the next power-on: nothing asked");
}

/* 1.0 Phase 3: the LFOs made consistent: one order and names, the syncs by length (3/8, 5/16, dotted), the global
 * LFOs free or synced, DIRT as a global destination, morph's sweep skipping what didn't change */
static void phase_100_3(void)
{
    uint16_t pp[4];
    uint32_t i, ok, ph, d;
    char b[32];
    printf("1.0 Phase 3: LFOs\n");
    p4_reset();
    if (sq.running)
        press(B_PLAY);
    for (ok = 1, i = 1; i + 1u < NSYNC; i++)          /* (the knob's order: by length, fast .. slow: more bars to the right) */
        ok &= SYNC_SCALE_X60[SYNC_ORDER[i]] > SYNC_SCALE_X60[SYNC_ORDER[i + 1u]];
    check(ok && SYNC_ORDER[0] == 0u && par_max(PAR_SYNC_LFO1) == 15u && par_max_k(PAR_L2SYNC1) == 15u,
          "SYNC: OFF, then 15 by length (4 bits: the kits as they were)");
    for (ok = 1, i = 1; i < NGLEN; i++)
        ok &= GL_LEN[GL_ORDER[i]] < GL_LEN[GL_ORDER[i + 1u]];
    check(ok && GL_ORDER[0] == GL_FREE, "the global LFOs' SYNC: OFF, then fast .. slow (more bars to the right)");
    /* LFO 1 on the second page, its sync knob by length: 1/4 on to 3/16, 3/8 a dotted quarter */
    goto_mode(B_HOME, M_HOME);
    ui.track = 0;
    run_ms(20);
    sound_open(S_LFO, 0);
    run_ms(20);
    page_params(pp);
    check(pp[2] == PAR_SYNC_LFO1, "LFO 1: SYNC on K3");
    par_set(PAR_SYNC_LFO1, 6);                        /* (1/4) */
    run_ms(20);
    turn(EN_K3, 1);
    check(par[PAR_SYNC_LFO1] == 14u, "SYNC K3 on from 1/4: 5/16 (by length, longer to the right)");
    par_format(b, PAR_SYNC_LFO1, 14, sizeof b);
    check(!strcmp(b, "5/16"), "its name: 5/16");
    turn(EN_K3, -2);
    check(par[PAR_SYNC_LFO1] == 15u, "SYNC back two: 3/16");
    par_format(b, PAR_SYNC_LFO1, 5, sizeof b);
    check(!strcmp(b, "1/2T"), "the LXR's 1/3: a half-note triplet, 1/2T");
    par_set(PAR_SYNC_LFO1, 13);                       /* (3/8 at 120 BPM: 0.75 s a cycle) */
    run_ms(20);
    d = (uint32_t)(4294967296.0 * BLOCK / FS / 0.75);
    check(vc[0].lfo.inc > d - d / 200u && vc[0].lfo.inc < d + d / 200u, "SYNC 3/8: a cycle in 0.75 s at 120 BPM");
    check(morph_mix(PAR_SYNC_LFO1, 6, 13, 100) == 6u && morph_mix(PAR_SYNC_LFO1, 6, 13, 200) == 13u &&
          morph_mix(PAR_SYNC_LFO1, 6, 8, 128) == morph_lerp(6, 8, 128), "morph: a new sync switches at 50%, the LXR's as before");
    par_set(PAR_SYNC_LFO1, 0);
    /* the names: LFO 2 as LFO 1 */
    sound_open(S_LFO2, 0);
    run_ms(20);
    card_name(b, sizeof b, PAR_L2AMT1);
    ok = !strcmp(b, "Depth");
    card_name(b, sizeof b, PAR_L2OFS1);
    ok &= !strcmp(b, "Phase");
    card_name(b, sizeof b, PAR_L2RTRG1);
    check(ok && !strcmp(b, "Retrigger"), "LFO 2: Depth, Phase, Retrigger");
    mod_target_name(b, lock_idx_of(PAR_FREQ_LFO1 + 1), 1, sizeof b);
    check(!strcmp(b, "D2 L1 rate"), "a lock on LFO 1's rate: D2 L1 rate");
    /* the global LFOs: SYNC OFF and RATE in Hz; a triplet */
    memset(gls, 0, sizeof gls);
    gls[0].len = GL_FREE;
    gls[0].rate = 127;                               /* (~20.5 Hz) */
    gls[0].wave = LFO_SAW_UP;
    run_ms(10);
    ph = gl.l[0].inc;
    d = (uint32_t)(4294967296.0 * BLOCK / FS * 20.48);
    check(ph > d - d / 50u && ph < d + d / 50u, "GLOBAL LFO SYNC OFF: RATE 127, 20.5 Hz");
    gls[0].rate = 0;
    run_ms(10);
    d = (uint32_t)(4294967296.0 * BLOCK / FS * 0.01);
    check(gl.l[0].inc > d - d / 50u && gl.l[0].inc < d + d / 50u, "RATE 0: 0.01 Hz");
    card_value(b, sizeof b, GLV(0, 7), 0);
    check(!strcmp(b, "100 s"), "RATE 0 shown as its cycle: 100 s");
    card_value(b, sizeof b, GLV(0, 7), 127);
    check(!strncmp(b, "20", 2), "RATE 127: 20 Hz");
    gls[0].len = 20;                                 /* (1/16T at 120 BPM: 1/12 s) */
    run_ms(10);
    d = (uint32_t)(4294967296.0 * BLOCK / FS * 12.0);
    check(gl.l[0].inc > d - d / 100u && gl.l[0].inc < d + d / 100u, "GLOBAL LFO SYNC 1/16T: 12 cycles a second");
    card_value(b, sizeof b, GLV(0, 7), 64);
    check(!strcmp(b, "SYNC"), "RATE while synced: SYNC");
    card_value(b, sizeof b, GLV(0, 2), 20);
    check(!strcmp(b, "1/16T"), "its SYNC: 1/16T");
    perf_goto(PG_GLFO);
    gls[0].len = GL_FREE;
    turn(EN_K3, 1);
    check(gls[0].len == 19u, "SYNC K3 on from OFF: 1/32 (the shortest first, more bars to the right)");
    turn(EN_K3, -1);
    check(gls[0].len == GL_FREE, "SYNC back: OFF");
    turn(EN_K4, 5);
    check(gls[0].rate == 5u, "RATE on K4");
    shot("global-lfo-free");
    /* DIRT: the master's amount either way from its knob */
    par_set(PAR_MDIRT_AMT, 0);
    gls[0].dest = GD_DIRT;
    gls[0].depth = 127;
    gls[0].len = 0;
    gls[0].wave = LFO_TRI;
    for (ok = 0, i = 0; i < 200u; i++) {
        run_ms(1);
        ok |= dirt.lfo > 30000;
    }
    check(ok && gd_turn(GD_CUTS, 1) == GD_DIRT, "GLOBAL LFO DIRT: the master's dirt moved (MASTER FX, by name)");
    memset(gls, 0, sizeof gls);
    run_ms(20);
    check(dirt.lfo == 0, "DIRT: back with the LFO off");
    /* morph's sweep: a mixture that moved is heard, one that didn't isn't applied again */
    for (i = 0; i < KIT_LEN; i++)
        morph_par[i] = par[kit_par(i)];
    morph_par[kit_idx(PAR_FILTER_FREQ_1)] = (uint8_t)(par[PAR_FILTER_FREQ_1] > 60u ? 10u : 120u);
    kit_set_morph(255);
    run_ms(30);
    check(snd_mv[kit_idx(PAR_FILTER_FREQ_1)] == morph_par[kit_idx(PAR_FILTER_FREQ_1)],
          "morph: the sweep applies the moved mixture");
    vc[0].flt.fval = 12345;                          /* (the sweep again, the mixture the same: not applied) */
    snd_post_morph();
    run_ms(30);
    check(vc[0].flt.fval == 12345u, "morph: an unchanged mixture isn't applied again");
    kit_set_morph(0);
    run_ms(30);
    check(vc[0].flt.fval != 12345u && snd_mv[kit_idx(PAR_FILTER_FREQ_1)] == par[PAR_FILTER_FREQ_1],
          "morph back: the kit's value again");
    goto_mode(B_HOME, M_HOME);
}

/* 1.0 Phase 4: MIDI and performance: the master volume (settings, CC 124), the morph target by CC 125, the remote's
 * DENSITY a track (TRACKS' CC 96..103), its rolls a track (GLOBAL's CC 112..119), a track's MIDI out (CONFIG) */
static void phase_100_4(void)
{
    uint16_t pp[4];
    uint32_t i, c, ons, ok;
    char b[32];
    uint8_t k[KIT_LEN], m[KIT_LEN];
    printf("1.0 Phase 4: MIDI and performance\n");
    p4_reset();
    vp_set(VP_RCH, 15);
    vp_set(VP_RCH2, 16);
    vp_set(VP_RKNOBS, RK_ABS);
    /* the master volume: 127 by default, in the settings; CC 124 on either remote channel and the global one */
    check(sizeof kset >= SET_LEN_016 + 258u && __builtin_offsetof(elixir_settings_t, mvol) == SET_LEN_016 + 256u,
          "settings: the volume and MIDI OUT after the CC map");
    check(out_vol == 127u && vp_get(VP_MVOL) == 127u && out_gain == 4096, "VOLUME: 127 by default, full level");
    gcc(124, 64);
    run_ms(20);
    check(out_vol == 64u && kset.mvol == 64u && out_gain == out_vol_q12() && out_gain == 64 * 64 * 4096 / 16129,
          "VOLUME: CC 124 on GLOBAL, x^2, kept in the settings");
    rcc(124, 100);
    run_ms(20);
    check(out_vol == 100u && out_gain == out_vol_q12(), "VOLUME: CC 124 on TRACKS");
    {
        uint32_t gt = kset.gtakes;
        vp_set(VP_GTAKES, GT_KEYS);
        web_midi(0xB0 | midi_global_ch(), 124, 30);
        run_ms(20);
        check(out_vol == 30u && kset.mvol == 30u, "VOLUME: CC 124 on the global channel (KEYS)");
        vp_set(VP_GTAKES, gt);
    }
    {   /* (gliding: a step of 16 a sample at most, no zipper) */
        int32_t g0 = out_gain;
        web_midi(0xB0 | remote_gch, 124, 127);
        web_render(2u * BLOCK);                       /* (the CC at the first block's start) */
        check(out_gain > g0 && out_gain - g0 <= 2 * 16 * (int32_t)BLOCK, "VOLUME: glides to a new value");
        run_ms(30);
        check(out_gain == 4096, "VOLUME: back to full");
    }
    /* the morph target: CC 125 a slot kit, as LOAD > MORPH */
    kit_init_values(k);
    k[kit_idx(PAR_FILTER_FREQ_1)] = 33;
    kit_get(7, m);
    kit_put(7, k);
    gcc(125, 7);
    check(kit_morph_loaded == 7u && morph_par[kit_idx(PAR_FILTER_FREQ_1)] == 33u, "MORPH: CC 125 loads slot 8 as the target");
    rcc(125, 3);
    check(kit_morph_loaded == 3u, "MORPH: CC 125 on TRACKS too");
    gcc(125, 100);
    check(kit_morph_loaded == 3u, "MORPH: past the slots, nothing");
    kit_put(7, m);
    /* the remote's DENSITY a track, centred, on top of PERF's, not in a COMMIT */
    check(rm_par(RR_RNDP * 8u + 2u) == VP_TD + 2u && vp_get(VP_TD + 2u) == 64u, "TRACKS' row 12: DENSITY, centred");
    sq.dens = 0;
    sq.dex = 0;
    rcc(96 + 2, 0);
    rcc(96 + 4, 127);
    check(sq.tdens[2] == -64 && sq.tdens[4] == 63 && dens_at(2) == -64 && dens_at(4) == 63 && dens_at(0) == 0,
          "DENSITY: D3 thinned, CY filled, D1 as it was");
    sq.wonly = 2;
    check(dens_at(2) == 0, "DENSITY: a COMMIT takes the knob's alone");
    sq.wonly = 0;
    card_name(b, sizeof b, VP_TD + 2u);
    check(strcmp(b, "D3 dens") == 0, "DENSITY: the card's name");
    perf_reset();
    check(sq.tdens[2] == 0 && sq.tdens[4] == 0, "RESET PERF: the tracks' DENSITY centred");
    /* the rolls a track: the CC's value its rate */
    play_from_start();
    c = trig_count[3];
    gcc(112 + 3, 10);                                /* (1/16 on the grid) */
    run_ms(1000);                                    /* (half a bar at 120) */
    i = trig_count[3] - c;
    printf("  SN's roll at CC 10: %u hits in half a bar\n", i);
    check(i >= 7u && i <= 9u, "ROLL: CC 10 on SN's: 1/16");
    c = trig_count[3];
    gcc(112 + 3, 127);                               /* (1/256 free) */
    run_ms(500);
    i = trig_count[3] - c;
    printf("  SN's roll at CC 127: %u hits in a beat\n", i);
    check(i >= 60u && i <= 66u, "ROLL: CC 127: 1/256, off the grid");
    c = trig_count[3];
    ok = trig_count[1];
    gcc(112 + 1, 3);                                 /* (D2 at 1/4 alongside) */
    run_ms(1000);
    check(trig_count[3] - c > 100u && trig_count[1] - ok >= 1u && trig_count[1] - ok <= 3u,
          "ROLL: two tracks at once, each at its own rate");
    gcc(112 + 3, 0);
    gcc(112 + 1, 0);
    c = trig_count[3];
    run_ms(500);
    check(trig_count[3] == c, "ROLL: CC 0, the roll stops");
    ui.track = 5;
    gcc(119, 20);
    ui.track = 1;
    gcc(119, 30);
    check(sq.croll[5] == 30u && sq.croll[1] == 0u, "ROLL: CC 119, the track selected as it started, held");
    gcc(119, 0);
    gcc(119, 40);
    check(sq.croll[1] == 40u && sq.croll[5] == 0u, "ROLL: CC 119 again: the track selected now");
    gcc(119, 0);
    press(B_PLAY);
    /* a track's MIDI out: all off by default; CONFIG: MIDI OUT */
    check(kset.mout == 0u && vp_get(VP_MOUT + 3u) == 0u, "MIDI OUT: every track off by default");
    par_set(PAR_MIDI_FILT_TX, par[PAR_MIDI_FILT_TX] | MF_NOTE);
    midi_offs(0, 0);
    midi_out_note(3, 60, 100);
    midi_out_note_off(3);
    midi_offs(midi_track_ch(3), &ons);
    check(ons == 0u, "MIDI OUT off: no note");
    vp_set(VP_MOUT + 3u, 1);
    midi_out_note(3, 60, 100);
    midi_out_note_off(3);
    i = midi_offs(midi_track_ch(3), &ons);
    check(ons == 1u && i == 1u && kset.mout == 8u && midi_mout == 8u, "MIDI OUT on: SN's note out");
    vp_set(VP_MOUT + 3u, 0);
    goto_mode(B_GLO, M_CONFIG);
    turn(EN_SELECT, (int32_t)CF_MOUT - (int32_t)ui.page);
    page_params(pp);
    check(ui.page == CF_MOUT && pp[0] == VP_MOUT && pp[3] == VP_MOUT + 3u, "CONFIG: MIDI OUT, D1 .. SN");
    run_ms(50);
    shot("config-midi-out");
    turn(EN_SELECT, 1);
    page_params(pp);
    check(pp[0] == VP_MOUT + 4u && pp[2] == VP_MOUT + 6u && pp[3] == VP_MVOL, "CONFIG: MIDI OUT, CY .. OH, Volume");
    run_ms(50);
    shot("config-midi-out-2");
    turn(EN_SELECT, 1);                              /* (Phase 8: USB MIX, the voices in the stereo mix) */
    page_params(pp);
    check(ui.page == CF_UMIX && pp[0] == VP_UMIX && pp[3] == VP_UMIX + 3u, "CONFIG: USB MIX after MIDI OUT, D1 .. SN");
    {
        char nb[24], vb[24];
        card_name(nb, sizeof nb, VP_UMIX);
        card_value(vb, sizeof vb, VP_UMIX, vp_get(VP_UMIX));
        check(!strcmp(nb, "D1 mix") && !strcmp(vb, "IN") && kset.mixout == 0u && mix_out == 0u,
              "USB MIX: D1 mix IN, every voice in by default");
        vp_set(VP_UMIX, 1);
        vp_set(VP_UMIX + 5u, 1);
        card_value(vb, sizeof vb, VP_UMIX, vp_get(VP_UMIX));
        card_name(nb, sizeof nb, VP_UMIX + 5u);
        check(!strcmp(vb, "OUT") && !strcmp(nb, "HH mix") && kset.mixout == 0x21u && mix_out == 0x21u,
              "USB MIX: D1 and HH OUT (kset.mixout, voices.c mix_out)");
        run_ms(50);
        shot("config-usb-mix");
        {   /* saved with the settings, read back at power-on; older settings (0 in the padding): all in */
            elixir_settings_t k = kset;
            st_save(OBJ_SETTINGS, &kset, sizeof kset);
            kset.mixout = 0;
            mix_out = 0;
            persist_boot();
            check(kset.mixout == 0x21u && mix_out == 0x21u, "USB MIX: kept in the settings");
            k.mixout = 0;
            st_save(OBJ_SETTINGS, &k, sizeof k);
            persist_boot();
            check(kset.mixout == 0u && mix_out == 0u, "USB MIX: settings with 0 there: every voice in");
        }
    }
    turn(EN_SELECT, 1);
    page_params(pp);
    check(pp[0] == VP_UMIX + 4u && pp[1] == VP_UMIX + 5u && pp[2] == VP_NONE && pp[3] == VP_NONE,
          "CONFIG: USB MIX, CY HH");
    run_ms(50);
    shot("config-usb-mix-2");
    turn(EN_SELECT, 1);
    page_params(pp);
    check(pp[0] == VP_LEDS, "CONFIG: DEVICE after USB MIX");
    turn(EN_SELECT, (int32_t)CF_REMOTE - (int32_t)ui.page);
    page_params(pp);
    check(pp[0] == VP_RCH, "CONFIG: REMOTE at CF_REMOTE");
    goto_mode(B_HOME, M_HOME);
    check(P_FX[FXPG_RSEND2][3] == VP_MVOL && P_MIXER[NMIXER - 1u][3] == VP_MVOL, "FX, MIXER: Volume after the reverb's");
    vp_set(VP_MVOL, 127);
    run_ms(30);
}

/* 1.0 Phase 5: time, the delay, WANDER: the delay always on (old DELAY kits; its targets; drained), TIME STRETCH (the
 * steps at 50% / 200% / 99%; back into time; at STOP, at a pattern change with REALIGN; in a project), JOURNEY 1024,
 * BOILING FROG, THE CANYON */
static uint32_t p5_pos(uint32_t t)                    /* track t's steps since the start */
{
    return sq.loops[t] * track_len(&pat[sq.active], t) + (uint32_t)(sq.idx[t] < 0 ? 0 : sq.idx[t]);
}
/* 1.0 Phase 6: the character filters' UI (the rest: render_test); the global filter (the DJ filter's type) */
/* 1.0: the delay's BAND: its knob, a global LFO's destination, GLOBAL's CC 3; a morph turns it on or off halfway */
/* 1.0 (after Phase 8): CHORUS on the FX bus, the bus COMP's gain every 8 samples, the phaser's four stages */
static double fxb_rms(const int32_t *x, uint32_t n) { double a = 0; uint32_t i; for (i = 0; i < n; i++) a += (double)x[i] * x[i]; return sqrt(a / n); }
static void fxb_run(uint32_t type, double hz, int32_t amp, uint32_t blocks, double *rl, double *rr)
{
    static int32_t ol[200u * BLOCK], orr[200u * BLOCK];
    uint32_t k, i, n = 0;
    for (k = 0; k < blocks; k++) {
        for (i = 0; i < BLOCK; i++, n++) {
            fx_l[i] = fx_r[i] = (int16_t)(sin(2.0 * 3.14159265358979 * hz * n / FS) * amp);
            mix_l[i] = mix_r[i] = 0;
        }
#if FX_CHORUS_ON
        if (type == FX_CHORUS)
            fx_chorus();
        else
#endif
            fx_comp();
        fx.dw_last = fx.dw;
        if (k >= blocks - 100u) {
            memcpy(ol + (k - (blocks - 100u)) * BLOCK, mix_l, sizeof mix_l);
            memcpy(orr + (k - (blocks - 100u)) * BLOCK, mix_r, sizeof mix_r);
        }
    }
    *rl = fxb_rms(ol, 100u * BLOCK);
    *rr = fxb_rms(orr, 100u * BLOCK);
}
static void phase_100_fx2(void)
{
    double a, b, c, d;
    int ok;
    printf("1.0: CHORUS, the cheaper COMP\n");
    eng_reset(0);
    fx.route = R_ST1;
#if FX_CHORUS_ON
    fx.dw = fx.dw_last = 32768;                      /* (all wet: what the effect adds) */
    fx.type = fx.last_type = FX_CHORUS;
    fx.p[0] = 30000;
    fx.p[1] = 50000;
    fx.p[2] = 20000;
    fx.p[3] = 0;                                     /* LO CUT 20 Hz */
    fxb_run(FX_CHORUS, 50.0, 12000, 150, &a, &b);
    fx.p[3] = 65536;                                 /* .. 800 Hz */
    fxb_run(FX_CHORUS, 50.0, 12000, 150, &c, &d);
    printf("  CHORUS on 50 Hz: wet %.0f at LO CUT 20 Hz, %.0f at 800 Hz\n", a, c);
    check(a > 5000.0 && c < a * 0.15, "CHORUS: LO CUT keeps the kick's lows out of the voices");
    fx.p[3] = 0;
    {                                                /* (wide: the sides' taps a quarter turn apart) */
        static int32_t l2[100u * BLOCK];
        uint32_t k, i, n = 0;
        double dsum = 0;
        for (k = 0; k < 100u; k++) {
            for (i = 0; i < BLOCK; i++, n++) {
                fx_l[i] = fx_r[i] = (int16_t)(sin(2.0 * 3.14159265358979 * 1500.0 * n / FS) * 12000);
                mix_l[i] = mix_r[i] = 0;
            }
            fx_chorus();
            for (i = 0; i < BLOCK; i++)
                l2[k * BLOCK + i] = mix_l[i] - mix_r[i];
        }
        dsum = fxb_rms(l2 + 20u * BLOCK, 80u * BLOCK);
        check(dsum > 1000.0, "CHORUS: the sides differ (stereo)");
    }
#endif
    /* COMP: a loud tone held down, the gain without steps */
    fx.type = fx.last_type = FX_COMP;
    par_set(PAR_FX_TYPE, FX_COMP);
    par_set(PAR_FX_RATIO, 5);
    par_set(PAR_FX_P1, 10);
    par_set(PAR_FX_P2, 30);
    par_set(PAR_FX_P3, 40);                          /* (threshold ~ -66 dB) */
    par_set(PAR_FX_P4, 0);
    snd_service();
    fx.c_g = fx.c_gain;
    fx.dw = fx.dw_last = 32768;
    fxb_run(FX_COMP, 200.0, 20000, 150, &a, &b);
    printf("  COMP on a loud tone: %.0f of %.0f\n", a, 20000.0 / sqrt(2.0));
    ok = a < 20000.0 / sqrt(2.0) * 0.5;
    check(ok, "COMP (every 8 samples): still compresses");
#if FX_CHORUS_ON
    check(NFX == 8u && FX_CHORUS == 7u && !strcmp(L_FX[FX_CHORUS], "CHORUS"), "FX: CHORUS the eighth type (kits' 0..6 as they were)");
#else
    check(!strcmp(L_FX[FX_CHORUS], "OFF") && model_step(PAR_FX_TYPE, FX_FLANGER, FX_CHORUS) == FX_SHIFT &&
              model_step(PAR_FX_TYPE, FX_SHIFT, FX_CHORUS) == FX_FLANGER,
          "FX: CHORUS set aside (reads OFF; the knob from FLANGER to SHIFT and back)");
    par_set(PAR_FX_TYPE, FX_CHORUS);                 /* (a CC or a kit from before: plays as OFF) */
    snd_service();
    check(fx.type == FX_OFF, "FX: CHORUS plays as OFF");
    {
        uint8_t k[KIT_LEN];
        memset(k, 0, sizeof k);
        k[PAR_FX_TYPE] = FX_CHORUS;
        k[kit_idx(PAR_INS1 + 2)] = INS_PHASER;
        k[kit_idx(PAR_INS1 + 3)] = INS_RING;
        k[kit_idx(PAR_INS1 + 4)] = INS_AUTOPAN;
        k[kit_idx(PAR_INS1 + 5)] = INS_SHIFT;
        kit_sanitize(k);
        check(k[PAR_FX_TYPE] == FX_OFF && k[kit_idx(PAR_INS1 + 2)] == INS_OFF && k[kit_idx(PAR_INS1 + 3)] == INS_RING &&
              k[kit_idx(PAR_INS1 + 4)] == INS_OFF && k[kit_idx(PAR_INS1 + 5)] == INS_SHIFT,
              "a kit's CHORUS and dropped inserts load OFF, SHIFTER and RING kept");
    }
    par_set(PAR_FX_TYPE, FX_OFF);
#endif
#if !FX_DRIVE_ON
    par_set(PAR_FX_TYPE, FX_DRIVE);                  /* (Phase 10f: a CC or a kit from before: plays as OFF) */
    snd_service();
    check(fx.type == FX_OFF && !strcmp(L_FX[FX_DRIVE], "OFF"), "FX: DRIVE set aside, plays and reads OFF");
    check(model_step(PAR_FX_TYPE, FX_OFF, FX_DRIVE) == FX_RING && model_step(PAR_FX_TYPE, FX_RING, FX_DRIVE) == FX_OFF,
          "FX: the knob goes from OFF to RING and back");
    {
        uint8_t k[KIT_LEN];
        memset(k, 0, sizeof k);
        k[PAR_FX_TYPE] = FX_DRIVE;
        kit_sanitize(k);
        check(k[PAR_FX_TYPE] == FX_OFF, "a kit's DRIVE loads OFF");
    }
    par_set(PAR_FX_TYPE, FX_OFF);
#endif
    fx.type = fx.last_type = FX_OFF;
    eng_reset(0);
}

/* 1.0 Phase 10: the phaser's one chain (L = dry + wet, R = dry - wet), one multiply an allpass, the feedback clamped */
static double ph10_resp(uint32_t ph, double hz, int side)   /* the response at hz, the LFO held at phase ph */
{
    uint32_t k, i, n = 0;
    double e = 0, m = 0;
    memset(fx.ph_s, 0, sizeof fx.ph_s);
    fx.ph_a[0] = fx.ph_a[1] = 0;
    for (k = 0; k < 400u; k++) {
        for (i = 0; i < BLOCK; i++) {
            fx_l[i] = fx_r[i] = (int16_t)(10000.0 * sin(2.0 * 3.14159265358979 * hz * (n + i) / FS));
            mix_l[i] = mix_r[i] = 0;
        }
        fx.m_ph = ph;
        fx_phaser();
        for (i = 0; i < BLOCK; i++, n++)
            if (k >= 100u) {
                double x = side ? mix_r[i] : mix_l[i];
                e += x * x;
                m++;
            }
    }
    return sqrt(e / m) / (10000.0 / sqrt(2.0));
}
/* 1.0 Phase 10c: NOISE (noise.c) on its own: voice vi's NOISE at col, rate, env, the amp EG at gain (Q24) and the
 * voice's increment inc, N blocks: its RMS and the slope (dB an octave) over 125 Hz .. 8 kHz (Goertzel, averaged over
 * blocks of 4096); holds: how often the held white changed */
static void nz10c_measure(uint32_t vi, uint32_t col, uint32_t rate, uint32_t env, int32_t gain, uint32_t inc,
                          uint32_t nb, double *rms, double *slope, uint32_t *holds)
{
    static const double F[7] = {125, 250, 500, 1000, 2000, 4000, 8000};
    double band[7] = {0}, s1[7] = {0}, s2[7] = {0}, e = 0, cs[7];
    voice_t v = vc[vi];
    int16_t b[BLOCK];
    uint32_t k, i, j, n = 0, ch = 0;
    int32_t lw;
    memset(&nzo[vi], 0, sizeof nzo[vi]);
    nzo[vi].on = 1;
    nz_knob(vi, 0, (uint8_t)col);
    nz_knob(vi, 1, (uint8_t)rate);
    nz_knob(vi, 2, (uint8_t)env);
    v.gain = gain;
    v.osc.inc = inc;
    for (j = 0; j < 7u; j++)
        cs[j] = 2.0 * cos(2.0 * 3.14159265358979 * F[j] / FS);
    for (k = 0; k < 64u; k++)                        /* (the colours' filters settle) */
        nz_block(vi, &v, b);
    lw = nzo[vi].w;
    for (k = 0; k < nb; k++) {
        nz_block(vi, &v, b);
        ch += nzo[vi].w != lw;
        lw = nzo[vi].w;
        for (i = 0; i < BLOCK; i++, n++) {
            double x = b[i];
            e += x * x;
            for (j = 0; j < 7u; j++) {
                double s0 = x + cs[j] * s1[j] - s2[j];
                s2[j] = s1[j];
                s1[j] = s0;
            }
            if ((n & 4095u) == 4095u)
                for (j = 0; j < 7u; j++) {
                    band[j] += s1[j] * s1[j] + s2[j] * s2[j] - cs[j] * s1[j] * s2[j];
                    s1[j] = s2[j] = 0;
                }
        }
    }
    *rms = sqrt(e / n);
    *slope = 10.0 * log10(band[6] / band[0]) / 6.0;
    if (holds)
        *holds = ch;
    memset(&nzo[vi], 0, sizeof nzo[vi]);
}
/* Phase 21: RAIN's engine alone (plaits.c rn_render): struck at the start, the grains gated off after gate_s (0:
 * never), Aux's side only; OUT and AUX as float into o, a; returns n (a multiple of PLSUB) */
static uint32_t rn21_run(double note, double h, double ring, double dens, double unused, double gate_s, uint32_t n,
                         uint32_t only, float *o, float *a)
{
    pl_t *v = &pl[0];
    pl_par_t p;
    int32_t ob[PLSUB], ab[PLSUB];
    uint32_t k, i;
    (void)unused;
    pl_rng = 4242;
    memset(v, 0, sizeof *v);
    pl_engine_init(v, 0, PE_RAIN);
    p.note = (int32_t)(note * 65536);
    p.f0 = pl_note_f(p.note);
    p.harm = (int32_t)(h * PL1);
    p.timbre = ring >= 1.0 ? PL1 : (int32_t)(ring * PL1);
    p.morph = dens >= 1.0 ? PL1 : (int32_t)(dens * PL1);
    p.accent = PL1 / 2;
    p.only = (uint8_t)only;
    p.col = 0;
    n -= n % PLSUB;
    for (k = 0; k < n; k += PLSUB) {
        int on = gate_s <= 0 || k < gate_s * FS;
        p.trig = k == 0;
        p.eg0 = p.eg1 = on ? PL1 : 0;
        pl_engine_render(v, 0, &p, ob, ab, PLSUB);
        for (i = 0; i < PLSUB; i++) {
            o[k + i] = (float)ob[i] / PL1;
            a[k + i] = (float)ab[i] / PL1;
        }
    }
    return n;
}
static double rn21_goertzel(const float *x, uint32_t n, double hz)
{
    double w = 2.0 * 3.14159265358979 * hz / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0, s0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / n;
}
static void phase_100_10c(void)
{
    static const uint32_t COL[4] = {0, 42, 85, 127};
    static const double WANT[4] = {-6, -3, 0, 3};
    static const char *const NM[4] = {"brown", "pink", "white", "blue"};
    double r, sl, r2, sl2;
    uint32_t c, h, inc = (uint32_t)(261.63 * 4294967296.0 / FS);
    uint16_t pp[4];
    char b[24];
    int ok = 1;
    printf("1.0 Phase 10c: NOISE; P GRAIN, P NOISE, P MODAL dropped\n");
    eng_reset(0);
    for (c = 0; c < 4u; c++) {                       /* each colour: its slope, the same level */
        nz10c_measure(0, COL[c], 127, 0, 1 << 24, inc, 2048, &r, &sl, 0);
        printf("  %-5s (COLOUR %3u): RMS %5.0f (%+.2f dB of 8192), %+.2f dB an octave\n", NM[c], (unsigned)COL[c], r,
               20.0 * log10(r / 8192.0), sl);
        ok &= fabs(20.0 * log10(r / 8192.0)) < 1.0 && fabs(sl - WANT[c]) < 0.8;
    }
    check(ok, "NOISE: brown, pink, white, blue: -6, -3, 0, +3 dB an octave, each RMS 0.25 (+-1 dB)");
    nz10c_measure(0, 64, 127, 0, 1 << 24, inc, 512, &r, &sl, 0);
    check(fabs(20.0 * log10(r / 8192.0)) < 2.0 && sl > -3.0 && sl < 0.0, "NOISE: between pink and white, between");
    /* RATE: held at the pitch x 2^(8 r - 2): x1 at 32 (C4: 262 a second), x1/4 at 0 */
    {
        double want = 261.63 * 1000.0 * BLOCK / FS;  /* (the holds in 1000 blocks at C4; a block's: at most one) */
        nz10c_measure(0, 85, 32, 0, 1 << 24, inc, 1000, &r, &sl, &h);
        printf("  RATE 32 at C4: %u holds in 1000 blocks (%.0f); ", (unsigned)h, want);
        ok = fabs(h - want) < want * 0.1;
        nz10c_measure(0, 85, 0, 0, 1 << 24, inc, 1000, &r2, &sl2, &h);
        printf("RATE 0: %u (%.0f)\n", (unsigned)h, want / 4.0);
        check(ok && fabs(h - want / 4.0) < want * 0.04, "NOISE: RATE holds the noise at the voice's pitch x 1 (32), "
                                                         "x 1/4 (0)");
    }
    check(sl < -2.0, "NOISE: held, the noise darker (its steps)");
    /* ENV: the amp EG moves COLOUR up */
    nz10c_measure(0, 0, 127, 127, 1 << 24, inc, 256, &r, &sl, 0);
    nz10c_measure(0, 0, 127, 127, 0, inc, 256, &r2, &sl2, 0);
    printf("  ENV 127 on brown: amp EG full %+.2f dB an octave, closed %+.2f\n", sl, sl2);
    check(sl > 2.0 && sl2 < -5.0, "NOISE: ENV: brown at the EG's top is blue, brown at its end");
    /* on the MODEL page: knobs, cards; every voice; not counted */
    goto_mode(B_HOME, M_HOME);
    ui.track = 3;
    run_ms(60);
    press(B_EDIT);
    for (c = 0; c < 12u && ui.sect != S_MODEL; c++)
        press(B_EDIT);
    par_set(PAR_MODEL1 + 3, MD_NOISE);
    run_ms(20);
    check(nzo[3].on && !mi[3].type && !eno[3].on, "SN on NOISE");
    page_params(pp);
    check(!strcmp(par_name(pp[1]), "Color") && !strcmp(par_name(pp[2]), "Rate") && !strcmp(par_name(pp[3]), "Env") &&
          !sect_half_used(3, S_MODEL, 1) && sect_half_used(3, S_AEG, 0), "NOISE: COLOUR, RATE, ENV; one page; AMP EG");
    card_value(b, sizeof b, PAR_MDECAY1 + 3u, 0);
    ok = !strcmp(b, "BROWN");
    card_value(b, sizeof b, PAR_MDECAY1 + 3u, 64);
    ok &= !strcmp(b, "PNK-WHT");
    card_value(b, sizeof b, PAR_MDECAY1 + 3u, 127);
    ok &= !strcmp(b, "BLUE");
    card_value(b, sizeof b, PAR_MDAMP1 + 3u, 127);
    ok &= !strcmp(b, "OFF");
    card_value(b, sizeof b, PAR_MDAMP1 + 3u, 32);
    ok &= !strcmp(b, "x1.0");
    card_value(b, sizeof b, PAR_MDAMP1 + 3u, 0);
    ok &= !strcmp(b, "x0.25");
    card_value(b, sizeof b, PAR_MDAMP1 + 3u, 126);
    printf("  RATE's cards: 0 x0.25, 32 x1.0, 126 %s, 127 OFF\n", b);
    check(ok, "NOISE: COLOUR's and RATE's cards");
    par_set(PAR_MDECAY1 + 3, 64);
    par_set(PAR_MDAMP1 + 3, 127);
    par_set(PAR_MMAT1 + 3, 0);
    voice_trigger(3, 127, vc[3].osc.base_note);
    run_ms(30);
    check(venv[3] > 2000, "NOISE on SN sounds");
    shot("noise-sn");
    run_ms(3000);
    check(venv[3] < 200, "NOISE on SN: its amp EG ends it");
    par_set(PAR_MODEL1 + 3, MD_LXR);
    ui.track = 0;
    run_ms(60);
    par_set(PAR_MODEL1, MD_NOISE);
    run_ms(20);
    check(nzo[0].on && mdl[0].want == 0u && mdl[0].type == 0u, "D1 on NOISE: not a counted model");
    par_set(PAR_MODEL1, MD_PPART);                   /* (Phase 21: a kit's P PARTCL plays RAIN, in its number) */
    run_ms(20);
    check(mdl[0].want == MD_RAIN && mdl[0].type == MD_RAIN && !strcmp(L_MODEL[MD_PPART], "RAIN"),
          "P PARTCL's number plays RAIN (Phase 21)");
    par_set(PAR_MODEL1, MD_NOISE);
    run_ms(20);
    shot("noise-d1");
    /* the knob: past the dropped engines; NOISE after CHIRP */
    par_set(PAR_MODEL1, MD_PL0 + PE_FM);
    run_ms(20);
    turn(EN_K1, 1);
    ok = par[PAR_MODEL1] == MD_PL0 + PE_ADD;
    par_set(PAR_MODEL1, MD_PL0 + PE_SWARM);
    run_ms(20);
    turn(EN_K1, 1);
    ok &= par[PAR_MODEL1] == MD_PL0 + PE_STRING;     /* (1.0: past P PARTCL, dropped) */
    turn(EN_K1, 1);
    ok &= par[PAR_MODEL1] == MD_PL0 + PE_BD;
    par_set(PAR_MODEL1, MD_MC0 + 9u);
    run_ms(20);
    turn(EN_K1, 1);
    ok &= par[PAR_MODEL1] == MD_CLOUD;
    turn(EN_K1, 1);
    ok &= par[PAR_MODEL1] == MD_RAIN;
    turn(EN_K1, 1);
    ok &= par[PAR_MODEL1] == MD_NOISE;
    turn(EN_K1, 1);
    ok &= par[PAR_MODEL1] == MD_EN;
    turn(EN_K1, -4);
    ok &= par[PAR_MODEL1] == MD_MC0 + 9u;
    check(ok, "MODEL knob: past P MODAL; CLOUD, RAIN, NOISE after CHIRP, before ENSEMBLE (1.0, Phase 21)");

    par_set(PAR_MODEL1, MD_LXR);
    run_ms(20);
    /* kits and locks: P MODAL plays MODAL (D1-D3), P GRAIN the LXR, P NOISE's number NOISE (every voice) */
    {
        static uint8_t kb[KIT_LEN];
        kit_init_values(kb);
        kb[kit_idx(PAR_MODEL1 + 0)] = MD_PMODAL;
        kb[kit_idx(PAR_MODEL1 + 1)] = MD_PGRAIN;
        kb[kit_idx(PAR_MODEL1 + 2)] = MD_NOISE;
        kb[kit_idx(PAR_MODEL1 + 3)] = MD_PMODAL;
        kb[kit_idx(PAR_MODEL1 + 4)] = MD_NOISE;
        kit_sanitize(kb);
        check(kb[kit_idx(PAR_MODEL1)] == MD_MODAL && kb[kit_idx(PAR_MODEL1 + 1)] == MD_CLOUD &&
              kb[kit_idx(PAR_MODEL1 + 2)] == MD_NOISE && kb[kit_idx(PAR_MODEL1 + 3)] == MD_LXR &&
              kb[kit_idx(PAR_MODEL1 + 4)] == MD_NOISE,
              "kit: P MODAL loads MODAL (SN: the LXR), P GRAIN's number CLOUD, NOISE stays (on CY too)");
    }
    par_set(PAR_MODEL1, MD_PMODAL);                  /* (as a lock or a morph sets it) */
    run_ms(20);
    ok = mdl[0].want == MD_MODAL;
    par_set(PAR_MODEL1, MD_CLOUD);
    run_ms(20);
    ok &= mdl[0].want == MD_CLOUD && mdl[0].type == MD_CLOUD && !nzo[0].on;
    check(ok, "a lock on P MODAL plays MODAL, on P GRAIN's number CLOUD");
    {   /* 1.0: P BD SD HH ADDTV render the side Aux lets you hear alone: that side as with both, the other 0 */
        static const uint8_t E[4] = {PE_BD, PE_SD, PE_HH, PE_ADD};
        uint32_t e, side, k, i, ok = 1;
        for (e = 0; e < 4u; e++) {
            double en[3][2] = {{0}};
            int exact = 1;
            static int32_t keep[200 * PLSUB];
            for (side = 0; side < 3u; side++) {      /* (PLS_BOTH, PLS_OUT, PLS_AUX) */
                pl_t *v = &pl[0];
                pl_par_t pp;
                int32_t o[PLSUB], a[PLSUB];
                pl_rng = 777;
                memset(v, 0, sizeof *v);
                pl_engine_init(v, 0, E[e]);
                pp.note = 48 << 16;
                pp.f0 = pl_note_f(pp.note);
                pp.harm = PL1 / 2;
                pp.timbre = E[e] == PE_SD ? PL1 / 4 : PL1 / 2;   /* (P SD's 808 range: two resonators) */
                pp.morph = PL1 / 2;
                pp.accent = PL1 * 3 / 4;
                pp.only = (uint8_t)side;
                for (k = 0; k < 200u; k++) {
                    pp.trig = k == 0;
                    pl_engine_render(v, 0, &pp, o, a, PLSUB);
                    for (i = 0; i < PLSUB; i++) {
                        en[side][0] += (double)o[i] * o[i];
                        en[side][1] += (double)a[i] * a[i];
                        if (side == PLS_BOTH)
                            keep[k * PLSUB + i] = o[i];
                        else if (side == PLS_OUT && keep[k * PLSUB + i] != o[i])
                            exact = 0;
                    }
                }
            }
            ok &= en[PLS_OUT][1] == 0.0 && en[PLS_AUX][0] == 0.0 && en[PLS_BOTH][0] > 0.0 && en[PLS_BOTH][1] > 0.0;
            ok &= en[PLS_OUT][0] > en[PLS_BOTH][0] * 0.6 && en[PLS_OUT][0] < en[PLS_BOTH][0] * 1.6;
            ok &= en[PLS_AUX][1] > en[PLS_BOTH][1] * 0.6 && en[PLS_AUX][1] < en[PLS_BOTH][1] * 1.6;
            if (E[e] == PE_BD || E[e] == PE_ADD)     /* (their OUT draws no random numbers: the same samples) */
                ok &= exact;
        }
        check(ok, "P BD SD HH ADDTV: Aux at an end renders that side alone, as it sounds with both");
    }
    {   /* 1.0: CLOUD: sounds at the hit, isn't one of the two models, its knobs' names; quiet without a hit */
        double r0, r1;
        par_set(PAR_MODEL1, MD_MODAL);
        par_set(PAR_MODEL1 + 1, MD_PL0 + PE_SD);
        par_set(PAR_MODEL1 + 2, MD_CLOUD);
        par_set(PAR_MMAT1 + 2, 100);                 /* (DENSITY up: a busy cloud) */
        par_set(PAR_MDAMP1 + 2, 70);
        par_set(PAR_MDECAY1 + 2, 90);
        run_ms(30);
        check(mdl[0].type == MD_MODAL && mdl[1].type == MD_PL0 + PE_SD && mdl[2].type == MD_CLOUD,
              "CLOUD: a third with two models on, not counted (plays)");
        check(!strcmp(par_name(PAR_MDECAY1 + 2), "Scatter") && !strcmp(par_name(PAR_MDAMP1 + 2), "Size") &&
              !strcmp(par_name(PAR_MMAT1 + 2), "Density") && !strcmp(par_name(PAR_MEXC1 + 2), "Texture"),
              "CLOUD: Scatter, Size, Density; Aux its Texture");
        ui.track = 2;
        goto_mode(B_HOME, M_HOME);
        run_ms(20);
        r0 = out_rms(200);
        voice_trigger(2, 120, 48);
        r1 = out_rms(150);
        printf("  CLOUD: rms before %.1f, after a hit %.1f\n", r0, r1);
        check(r1 > r0 + 50.0, "CLOUD: a hit sounds");
        par_set(PAR_MEXC1 + 2, 127);                 /* (AUX: the texture) */
        voice_trigger(2, 120, 48);
        r1 = out_rms(150);
        check(r1 > 50.0, "CLOUD: AUX (its noise-band grains) sounds too");
        par_set(PAR_MEXC1 + 2, 0);
        par_set(PAR_MODEL1, MD_LXR);
        par_set(PAR_MODEL1 + 1, MD_LXR);
        par_set(PAR_MODEL1 + 2, MD_LXR);
        run_ms(20);
        ui.track = 0;
    }
    {   /* Phase 21: RAIN: not one of the two models, its knobs, a hit sounds on both sides; then the engine itself */
        double r0, r1, ra, rb;
        char b[24];
        uint32_t dk;
        par_set(PAR_MODEL1, MD_MODAL);
        par_set(PAR_MODEL1 + 1, MD_PL0 + PE_SD);
        par_set(PAR_MODEL1 + 2, MD_RAIN);
        par_set(PAR_MDECAY1 + 2, 0);                 /* (Chord NOTE, Ring long, Density up) */
        par_set(PAR_MDAMP1 + 2, 100);
        par_set(PAR_MMAT1 + 2, 90);
        par_set(PAR_MEXC1 + 2, 0);
        run_ms(30);
        check(mdl[0].type == MD_MODAL && mdl[1].type == MD_PL0 + PE_SD && mdl[2].type == MD_RAIN,
              "RAIN: a third with two models on, not counted (plays)");
        check(!strcmp(par_name(PAR_MDECAY1 + 2), "Chord") && !strcmp(par_name(PAR_MDAMP1 + 2), "Ring") &&
              !strcmp(par_name(PAR_MMAT1 + 2), "Density") && !strcmp(par_name(PAR_MEXC1 + 2), "Dry") &&
              !strcmp(par_name(PAR_MPOS1 + 2), "Color"),
              "RAIN: Chord, Ring, Density; Color; Aux its Dry");
        {
            static const char *const T[8] = {"NOTE", "OCTAVES", "FIFTHS", "MAJOR", "MINOR", "SUS4", "MIN7", "PENTA"};
            uint32_t k, ok = 1;
            for (k = 0; k < 8u; k++) {
                memset(b, 0, sizeof b);
                par_format(b, PAR_MDECAY1 + 2, k * 16u + 8u, sizeof b);
                ok &= !strcmp(b, T[k]);
            }
            memset(b, 0, sizeof b);
            par_format(b, PAR_MDECAY1 + 2, 127, sizeof b);
            check(ok && !strcmp(b, "PENTA"), "RAIN: Chord shows RING's tunings, eight across the knob");
        }
        ui.track = 2;
        goto_mode(B_HOME, M_HOME);
        run_ms(20);
        r0 = out_rms(200);
        voice_trigger(2, 120, 48);
        r1 = out_rms(300);
        par_set(PAR_MEXC1 + 2, 127);                 /* (Dry: the grains alone) */
        run_ms(3000);
        voice_trigger(2, 120, 48);
        ra = out_rms(300);
        printf("  RAIN: rms before %.1f, after a hit %.1f (bells), %.1f (Dry 127)\n", r0, r1, ra);
        check(r1 > r0 + 50.0 && ra > 50.0, "RAIN: a hit sounds, the bells and the dry grains");
        par_set(PAR_MEXC1 + 2, 0);
        dk = par[PAR_VELOD3];
        par_set(PAR_VELOD3, 10);                     /* (a short Decay: the grains stop, the bells ring on) */
        voice_trigger(2, 120, 48);
        run_ms(400);
        rb = out_rms(200);
        printf("  RAIN: 0.4 s after a hit with a short Decay, rms %.1f\n", rb);
        check(rb > 20.0, "RAIN: the amp EG shapes the grains; the bands ring on past it");
        par_set(PAR_VELOD3, dk);
        par_set(PAR_MODEL1, MD_LXR);
        par_set(PAR_MODEL1 + 1, MD_LXR);
        par_set(PAR_MODEL1 + 2, MD_LXR);
        run_ms(20);
        ui.track = 0;
    }
    {   /* .. the engine: the sides, its tuning, its ring, no runaway, asleep when done */
        static float o[44100 * 11], a[44100 * 11];
        double e0, e1, en[2], g[3], gn[3], pk = 0;
        uint32_t i, n;
        int ok = 1;
        check(sizeof(pl_rn_t) <= sizeof(pl_sw_t), "RAIN: its state no larger than P SWARM's (pl_t doesn't grow)");
        n = rn21_run(48, 0.5 / 8, 0.6, 0.5, 0.5, 0, 44100, PLS_OUT, o, a);   /* (Aux 0: OUT alone, AUX 0) */
        for (i = 0, en[0] = en[1] = 0; i < n; i++)
            en[0] += o[i] * o[i], en[1] += a[i] * a[i];
        ok &= en[0] > 0 && en[1] == 0;
        rn21_run(48, 0.5 / 8, 0.6, 0.5, 0.5, 0, 44100, PLS_AUX, o, a);   /* (Aux 127: AUX alone, the bands not rung) */
        for (i = 0, en[0] = en[1] = 0; i < n; i++)
            en[0] += o[i] * o[i], en[1] += a[i] * a[i];
        ok &= en[0] == 0 && en[1] > 0 && !pl[0].u.rn.live;
        check(ok, "RAIN: Aux 0 the bands alone, Aux 127 the dry grains alone (the bands asleep)");
        {   /* tuning at NOTE: the harmonics 1, 2, 3 against a semitone off; OCTAVES: 3 f0 low */
            double f0 = 440.0 * pow(2.0, (48 - 69) / 12.0);
            rn21_run(48, 0.5 / 8, 0.9, 0.5, 0, 2.0, 44100 * 3, PLS_OUT, o, a);
            for (i = 0; i < 3u; i++) {
                g[i] = rn21_goertzel(o + 22050, 88200, f0 * (i + 1));
                gn[i] = rn21_goertzel(o + 22050, 88200, f0 * (i + 1) * 1.0594631) +
                        rn21_goertzel(o + 22050, 88200, f0 * (i + 1) / 1.0594631);
            }
            printf("  RAIN NOTE: f0 %.4f (off %.4f), 2 f0 %.4f (%.4f), 3 f0 %.4f (%.4f)\n", g[0], gn[0], g[1], gn[1],
                   g[2], gn[2]);
            ok = g[0] > 3 * gn[0] && g[1] > 3 * gn[1] && g[2] > 3 * gn[2];
            rn21_run(48, 1.5 / 8, 0.9, 0.5, 0, 2.0, 44100 * 3, PLS_OUT, o, a);
            e0 = rn21_goertzel(o + 22050, 88200, f0 * 3);
            e1 = rn21_goertzel(o + 22050, 88200, f0 * 2);
            printf("  RAIN OCTAVES: 3 f0 %.4f, 2 f0 %.4f\n", e0, e1);
            check(ok && e0 * 5 < e1, "RAIN: tuned to the chord (NOTE: f0, 2 f0, 3 f0; OCTAVES: no 3 f0)");
        }
        {   /* Ring: the energy 1 s after the grains stop, longer at 127 than at 0 */
            uint32_t m = rn21_run(48, 3.5 / 8, 0.0, 0.5, 0, 1.0, 44100 * 2, PLS_OUT, o, a), j;
            for (j = m - 4410, e0 = 0; j < m; j++)
                e0 += o[j] * o[j];
            rn21_run(48, 3.5 / 8, 1.0, 0.5, 0, 1.0, 44100 * 2, PLS_OUT, o, a);
            for (j = m - 4410, e1 = 0; j < m; j++)
                e1 += o[j] * o[j];
            printf("  RAIN Ring: energy 0.9 .. 1 s after the grains, Ring 0 %.3g, Ring 127 %.3g\n", e0, e1);
            check(e1 > 1000 * e0 && e1 > 0, "RAIN: Ring 127 rings far longer than Ring 0");
        }
        {   /* no runaway: Density 127, Ring 127, Aux 0, 10 s; then the grains stop: the bands die away, asleep */
            n = rn21_run(36, 0.5 / 8, 1.0, 1.0, 0, 10.0, 44100 * 11, PLS_OUT, o, a);
            for (i = 0; i < 44100u * 10u; i++)
                pk = fabs(o[i]) > pk ? fabs(o[i]) : pk;
            printf("  RAIN Density 127, Ring 127, 10 s: peak %.3f; live at the end %d\n", pk, pl[0].u.rn.live);
            ok = pk < 2.0 && pk > 0.1;               /* (the engine's soft clip tops at 2; OUT's limiter after) */
            n = rn21_run(36, 0.5 / 8, 0.6, 1.0, 0, 1.0, 44100 * 6, PLS_OUT, o, a);
            check(ok && !pl[0].u.rn.live && o[n - 1] == 0.0f, "RAIN: bounded at its loudest; silent and asleep after");
        }
    }
    par_set(PAR_MODEL1, MD_LXR);
    run_ms(20);
    goto_mode(B_HOME, M_HOME);
}

/* 1.0 Phase 12: SHIFT, the frequency shifter (FX bus type 8) */
static void sh12_run(double hz, int32_t amp, uint32_t blocks, int32_t *ol, int32_t *orr, int st)
{
    uint32_t k, i, n = 0;
    for (k = 0; k < blocks; k++) {
        for (i = 0; i < BLOCK; i++, n++) {
            fx_l[i] = fx_r[i] = (int16_t)(hz > 0 ? sin(2.0 * 3.14159265358979 * hz * n / FS) * amp :
                                          ((n / 37u) & 1u ? amp : -amp));   /* (hz 0: a square, full of partials) */
            if (!st)
                fx_r[i] = 0;
            mix_l[i] = mix_r[i] = 0;
        }
        fx_shift();
        fx.dw_last = fx.dw;
        if (ol)
            for (i = 0; i < BLOCK; i++) {
                ol[k * BLOCK + i] = mix_l[i];
                orr[k * BLOCK + i] = mix_r[i];
            }
    }
}
static double sh12_level(const int32_t *x, uint32_t n, double hz)   /* Goertzel: the amplitude at hz */
{
    double w = 2.0 * 3.14159265358979 * hz / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0, s0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / n;
}
static void phase_100_12(void)
{
    static int32_t ol[400u * BLOCK], orr[400u * BLOCK];
    const uint32_t N = 256u * BLOCK;
    double up, img, dry, dn, dimg, mx;
    uint32_t i, inc;
    int ok = 1;
    char b[24];
    int32_t pk;
    printf("1.0 Phase 12: SHIFT\n");
    eng_reset(0);
    fx.route = R_ST1;
    fx.type = fx.last_type = FX_SHIFT;
    fx.dw = fx.dw_last = 32768;                      /* (all wet) */
    fx.push = 0;
    /* SHIFT for +100 Hz: the knob's Q16 (by the curve: 0.1 Hz x 10^(4 t)) */
    {
        int32_t p = 32768 + 300 + (int32_t)(32468.0 * log(100.0 / 0.1) / log(10000.0) + 0.5);
        fx.p[0] = p;
        inc = sh_inc(p);
        printf("  SHIFT's step for +100 Hz: %.2f Hz\n", inc * (double)FS / 4294967296.0);
        ok = fabs(inc * (double)FS / 4294967296.0 - 100.0) < 1.0 && sh_inc(65536 - p) == 0u - inc && sh_inc(32768 + 258) == 0u &&
             fabs(sh_inc(65536) * (double)FS / 4294967296.0 - 1000.0) < 15.0 && fabs(sh_inc(32768 + 301) * (double)FS / 4294967296.0 - 0.1) < 0.01;
        check(ok, "SHIFT: the knob's middle still, +-0.1 Hz just past it, +-1 kHz at the ends, exponential");
    }
    fx.p[1] = 0;
    fx.p[2] = 0;
    fx.p[3] = 65536;                                 /* (SPREAD full: the right goes down) */
    memset(fx.sh_s, 0, sizeof fx.sh_s);
    sh12_run(1000.0, 16000, 300, ol, orr, 1);
    up = sh12_level(ol + 40u * BLOCK, N, 1100.0);
    img = sh12_level(ol + 40u * BLOCK, N, 900.0);
    dry = sh12_level(ol + 40u * BLOCK, N, 1000.0);
    dn = sh12_level(orr + 40u * BLOCK, N, 900.0);
    dimg = sh12_level(orr + 40u * BLOCK, N, 1100.0);
    printf("  1 kHz shifted +100 Hz: 1.1 kHz %.0f, image 900 Hz %.1f dB, 1 kHz %.1f dB; the right (SPREAD): 900 Hz %.0f, "
           "image %.1f dB\n", up, 20 * log10(img / up), 20 * log10(dry / up), dn, 20 * log10(dimg / dn));
    check(up > 16000 * 0.85 && img < up * 0.0316 && dry < up * 0.0316 && dn > 16000 * 0.85 && dimg < dn * 0.0316,
          "SHIFT: 1 kHz + 100 Hz lands at 1.1 kHz, the image under -30 dB; SPREAD's right side at 900 Hz");
    for (i = 0; i < 4u; i++) {                       /* (the image across the band) */
        static const double F[4] = {80.0, 300.0, 4000.0, 12000.0};
        double u, m;
        memset(fx.sh_s, 0, sizeof fx.sh_s);
        sh12_run(F[i], 16000, 300, ol, orr, 1);
        u = sh12_level(ol + 40u * BLOCK, N, F[i] + 100.0);
        m = sh12_level(ol + 40u * BLOCK, N, F[i] - 100.0);
        printf("  %5.0f Hz: up %.0f, image %.1f dB\n", F[i], u, 20 * log10(m / u));
        ok &= m < u * 0.0316 && u > 16000 * 0.8;
    }
    check(ok, "SHIFT: the image under -30 dB from 80 Hz to 12 kHz");
    /* bounded: full FDBK and FB PUSH, a full-scale square, the shortest and longest DELAY */
    for (i = 0; i < 2u; i++) {
        uint32_t k;
        fx.p[1] = 65536;
        fx.p[2] = i ? 65536 : 0;
        fx.push = 65536;
        sh12_run(0, 32767, 400, ol, orr, 1);
        pk = 0;
        for (k = 0; k < 400u * BLOCK; k++)
            pk = iabs(ol[k]) > pk ? iabs(ol[k]) : pk;
        {                                            /* (the allpasses' states: within 4 x full scale) */
            int32_t st = 0, j;
            for (j = 0; j < 8; j++)
                for (k = 0; k < 4u; k++)
                    st = iabs(fx.sh_s[j][k] >> 10) > st ? iabs(fx.sh_s[j][k] >> 10) : st;
            printf("  full FDBK + PUSH, DELAY %s, a full-scale square: peak %d, states %d\n", i ? "20 ms" : "1.5 ms",
                   (int)pk, (int)st);
            ok &= pk <= 32768 && st < 4 * 32768;
        }
    }
    fx.push = 0;
    check(ok, "SHIFT: bounded at full feedback and FB PUSH");
    /* silence after: the line rings down with FDBK, then nothing (no limit cycle) */
    fx.p[1] = 32768;
    fx.p[2] = 20000;
    sh12_run(0, 0, 400, ol, orr, 1);
    pk = 0;
    for (i = 300u * BLOCK; i < 400u * BLOCK; i++)
        pk = iabs(ol[i]) > pk ? iabs(ol[i]) : pk;
    printf("  silence after, half FDBK: peak %d\n", (int)pk);
    check(pk <= 2, "SHIFT: silent after silence (half FDBK)");
    /* a type change clears its line and states */
    sh12_run(1000.0, 16000, 50, 0, 0, 1);
    fx.type = FX_PHASER;
    memset(fx_l, 0, sizeof fx_l);
    memset(fx_r, 0, sizeof fx_r);
    fxbus_process();
    for (i = 0, ok = 1; i < 8u; i++)
        ok &= !fx.sh_s[i][0] && !fx.sh_s[i][1] && !fx.sh_s[i][2] && !fx.sh_s[i][3];
    fx.type = FX_SHIFT;
    memset(mix_l, 0, sizeof mix_l);
    memset(mix_r, 0, sizeof mix_r);
    fxbus_process();
    for (i = 0; i < 2u * FL_LEN; i++)
        ok &= fl_buf[i] == 0;
    for (i = 0; i < BLOCK; i++)
        ok &= mix_l[i] == 0 && mix_r[i] == 0;
    check(ok, "SHIFT: a type change clears (its allpasses, then its line: silent from silence)");
    /* mono route: the left only, shifted up */
    fx.route = R_L1;
    fx.p[1] = 0;
    fx.p[0] = 32768 + 300 + (int32_t)(32468.0 * log(100.0 / 0.1) / log(10000.0) + 0.5);
    memset(fx.sh_s, 0, sizeof fx.sh_s);
    sh12_run(1000.0, 16000, 300, ol, orr, 0);
    up = sh12_level(ol + 40u * BLOCK, N, 1100.0);
    mx = sh12_level(orr + 40u * BLOCK, N, 1100.0);
    check(up > 16000 * 0.85 && mx < 1.0, "SHIFT on L1: the left, shifted, the right untouched");
    fx.route = R_ST1;
    /* the knob and the cards */
    goto_mode(B_FX, M_FX);
    par_set(PAR_FX_TYPE, FX_FLANGER);
    snd_service();
    check(model_step(PAR_FX_TYPE, FX_FLANGER, FX_CHORUS) == FX_SHIFT && vp_max(PAR_FX_TYPE) == FX_SHIFT &&
              !strcmp(L_FX[FX_SHIFT], "SHIFT"), "FX: the knob past FLANGER: SHIFT (the last)");
    par_set(PAR_FX_TYPE, FX_SHIFT);
    snd_service();
    check(fx.type == FX_SHIFT, "FX: SHIFT plays");
    card_name(b, sizeof b, PAR_FX_P1);
    ok = !strcmp(b, "Shift");
    card_value(b, sizeof b, PAR_FX_P1, 64);
    ok &= !strcmp(b, "0Hz");
    card_value(b, sizeof b, PAR_FX_P1, 127);
    printf("  SHIFT's card: 127 %s", b);
    ok &= !strcmp(b, "+1kHz");
    card_value(b, sizeof b, PAR_FX_P1, 65);
    printf(", 65 %s", b);
    card_value(b, sizeof b, PAR_FX_P1, 0);
    printf(", 0 %s", b);
    ok &= !strcmp(b, "-1kHz");
    card_value(b, sizeof b, PAR_FX_P3, 0);
    printf("; DELAY 0 %s", b);
    card_value(b, sizeof b, PAR_FX_P3, 127);
    printf(", 127 %s\n", b);
    ok &= !strcmp(b, "20 ms");
    card_name(b, sizeof b, PAR_FX_P4);
    ok &= !strcmp(b, "Spread");
    check(ok, "SHIFT's cards: Shift (0Hz in the middle, +-1kHz), Delay to 20 ms, Spread");
    par_set(PAR_FX_P1, 90);
    par_set(PAR_FX_P2, 80);
    par_set(PAR_FX_P4, 100);
    par_set(PAR_FX_DRY_WET, 64);
    snd_service();
    run_ms(100);
    for (i = 0; i < 4u && fx_page() != FXPG_BUS; i++)
        turn(EN_SELECT, 1);
    shot("fx-shift");
    {
        uint8_t k[KIT_LEN];
        memset(k, 0, sizeof k);
        k[PAR_FX_TYPE] = FX_SHIFT;
        kit_sanitize(k);
        ok = k[PAR_FX_TYPE] == FX_SHIFT;
        kit_put(0, k);
        kit_get(0, k);
        check(ok && k[PAR_FX_TYPE] == FX_SHIFT && kit_pk_bits <= KIT_PK * 8u, "a kit keeps SHIFT (FX type 4 bits, the packed kit fits)");
        printf("  packed kit: %u bits of %u\n", (unsigned)kit_pk_bits, (unsigned)(KIT_PK * 8u));
    }
    par_set(PAR_FX_TYPE, FX_OFF);
    par_set(PAR_FX_DRY_WET, 0);
    snd_service();
    goto_mode(B_HOME, M_HOME);
}

/* 1.0 Phase 13: the delay's GRAIN mode */
static uint32_t gr13_active(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < GR_N; k++)
        n += gs.g[k].left != 0u;
    return n;
}
/* the delay alone for n blocks: a sine (hz; 0: a full-scale square) for the first `in` blocks, then silence; the
 * output into ol / orr (if given); the most grains at once, the largest step from one sample to the next */
static uint32_t gr13_run(double hz, int32_t amp, uint32_t blocks, uint32_t in, int32_t *ol, int32_t *orr, int32_t *jump)
{
    static uint32_t n;
    static int32_t last;
    uint32_t k, i, most = 0;
    int16_t dl[BLOCK], dr[BLOCK];                    /* (the input as it was: dl_in's fade rewrites fx_l) */
    for (k = 0; k < blocks; k++, n += BLOCK) {
        fxbus_early();
        for (i = 0; i < BLOCK; i++) {
            int32_t x = k >= in ? 0 : hz > 0 ? (int32_t)(sin(2.0 * M_PI * hz * (n + i) / FS) * amp)
                                             : (((n + i) / 37u) & 1u ? amp : -amp);
            dl[i] = dr[i] = fx_l[i] = fx_r[i] = (int16_t)sat16(x);
            mix_l[i] = mix_r[i] = 0;
            rev_in[i] = 0;
        }
        fxbus_process();
        most = gr13_active() > most ? gr13_active() : most;
        for (i = 0; i < BLOCK; i++) {
            int32_t wl = mix_l[i] - dl[i];           /* (the delay's alone: the bus OFF passes its input on) */
            if (ol) {
                ol[k * BLOCK + i] = wl;
                orr[k * BLOCK + i] = mix_r[i] - dr[i];
            }
            if (jump && iabs(wl - last) > *jump)
                *jump = iabs(wl - last);
            last = wl;
        }
    }
    return most;
}
static double gr13_level(const int32_t *x, uint32_t n, double hz)   /* the rms of 1,024-sample frames' levels at hz */
{
    double p = 0;
    uint32_t i;
    for (i = 0; i + 1024u <= n; i += 1024u) {
        double a = sh12_level(x + i, 1024u, hz);
        p += a * a;
    }
    return sqrt(p / (n / 1024u));
}
/* 1.0 Phase 16: the delay sends, the route between the bus and the delay, REV > FX, the library's packed kits, FX
 * TRACK SENDS, the mixer's DLY SEND, the remote's row 7 */
static void bus_alone(uint32_t blocks, int32_t bus, int32_t dly)   /* a click into the bus and / or the delay send */
{
    uint32_t b, i;
    snd_service();                                   /* (the values set: as the ISR takes them) */
    for (b = 0; b < blocks; b++) {
        fxbus_early();
        for (i = 0; i < BLOCK; i++) {
            fx_l[i] = fx_r[i] = (int16_t)(b == 0u && i == 0u ? bus : 0);
            dly_l[i] = dly_r[i] = (int16_t)(b == 0u && i == 0u ? dly : 0);
            mix_l[i] = mix_r[i] = 0;
            rev_in[i] = 0;
        }
        dly_fed = b == 0u && dly;
        rev_fed = 0;
        fxbus_process();
        if (b * BLOCK + BLOCK <= sizeof p21_l / sizeof p21_l[0])
            for (i = 0; i < BLOCK; i++) {
                p21_l[b * BLOCK + i] = (int16_t)sat16(mix_l[i]);
                p21_r[b * BLOCK + i] = (int16_t)sat16(mix_r[i]);
            }
    }
    dly_fed = 0;
}
static int32_t peak_in(const int16_t *x, uint32_t from, uint32_t to)
{
    int32_t m = 0;
    uint32_t i;
    for (i = from; i < to; i++)
        m = abs(x[i]) > m ? abs(x[i]) : m;
    return m;
}
static void phase_100_16(void)
{
    uint32_t i, d;
    int32_t e127, e96, e64, at0, ech;
    int ok;
    char b[24];
    uint8_t k[KIT_LEN], k2[KIT_LEN];
    printf("1.0 Phase 16: delay sends, route, REV > FX\n");
    p4_reset();
    /* the kit: the new values fit, the init kit PARALLEL, a kit from before them FX > DLY */
    kit_init_values(k);
    check(kit_pk_bits <= KIT_PK * 8u && k[kit_idx(PAR_DLY_ROUTE)] == 64u && k[kit_idx(PAR_REV_FX)] == 0u &&
              k[kit_idx(PAR_DSEND1)] == 0u, "the init kit: PARALLEL, no delay sends, REV > FX 0; the slot packs");
    kit_old_fill(k, kit_idx(PAR_DSEND1));
    check(k[kit_idx(PAR_DLY_ROUTE)] == DLY_ROUTE_OLD, "a kit from before Phase 16: FX > DLY 127 (as it sounded)");
    /* the route, the bus alone: MONO at a short TIME, the bus OFF */
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DLY_TIME, 20);
    par_set(PAR_DLY_FB, 0);
    par_set(PAR_DLY_TONE, 127);
    run_ms(20);
    d = 0;
    par_set(PAR_DLY_ROUTE, 127);
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DLY_ROUTE, 127);
    bus_alone(200, 16000, 0);
    at0 = p21_l[0];
    d = first_peak(p21_l, 100, 12000, 1000);
    e127 = peak_in(p21_l, 100, 12000);
    printf("  FX > DLY 127: the bus's click %d, its echo %d at %u\n", at0, e127, d);
    check(at0 > 15000 && e127 > 4000 && d > 100u, "FX > DLY 127: the bus into the delay (as before Phase 16)");
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DLY_ROUTE, 96);
    bus_alone(200, 16000, 0);
    e96 = peak_in(p21_l, 100, 12000);
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DLY_ROUTE, 64);
    bus_alone(200, 16000, 0);
    e64 = peak_in(p21_l, 100, 12000);
    printf("  the bus's echo: FX > DLY 127 %d, 96 %d, PARALLEL %d\n", e127, e96, e64);
    check(e96 > e127 / 3 && e96 < e127 * 2 / 3 && e64 == 0 && p21_l[0] > 15000,
          "FX > DLY 50%: half the echo; PARALLEL: the FX send's sound is not delayed");
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DLY_ROUTE, 64);
    bus_alone(200, 0, 16000);
    ech = peak_in(p21_l, 100, 12000);
    check(abs(p21_l[0]) < 50 && ech > 4000 && first_peak(p21_l, 100, 12000, 1000) == d,
          "PARALLEL: the delay send's echo at TIME, nothing dry");
    /* DLY > FX: the echoes through the bus (OFF: as PARALLEL; RING: changed), none left in the mix past the bus */
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_DLY_ROUTE, 0);
    bus_alone(200, 0, 16000);
    ok = abs(peak_in(p21_l, 100, 12000) - ech) <= 2;
    par_set(PAR_FX_TYPE, FX_RING);
    par_set(PAR_FX_DRY_WET, 100);
    run_ms(20);
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_FX_TYPE, FX_RING);
    par_set(PAR_FX_DRY_WET, 100);
    par_set(PAR_DLY_ROUTE, 0);
    bus_alone(200, 0, 16000);
    {
        int32_t diff = 0, pk = peak_in(p21_l, 100, 12000);
        static int16_t par_l[FS];
        memcpy(par_l, p21_l, sizeof par_l);
        fx_fresh(DELAY_NORMAL);
        par_set(PAR_FX_TYPE, FX_RING);
        par_set(PAR_FX_DRY_WET, 100);
        par_set(PAR_DLY_ROUTE, 64);
        bus_alone(200, 0, 16000);
        for (i = 100; i < 12000u; i++)
            diff = abs(p21_l[i] - par_l[i]) > diff ? abs(p21_l[i] - par_l[i]) : diff;
        printf("  DLY > FX 100%% into RING: the echo's peak %d, %d from PARALLEL's\n", pk, diff);
        check(ok && diff > 1000, "DLY > FX: the echoes through the bus effect (OFF: the same echo)");
    }
    par_set(PAR_FX_TYPE, FX_OFF);
    /* the loop guard: REV > FX with FX > DLY: ECHO > REVERB held at 0 */
    fx_fresh(DELAY_NORMAL);
    par_set(PAR_ECHOREV, 127);
    par_set(PAR_REV_FX, 100);
    par_set(PAR_DLY_ROUTE, 127);
    {
        int32_t any = 0;
        uint32_t bk;
        for (bk = 0; bk < 60u; bk++) {
            bus_alone(1, bk ? 0 : 16000, 0);
            for (i = 0; i < BLOCK; i++)
                any |= rev_in[i];
        }
        card_value(b, sizeof b, PAR_ECHOREV, 127);
        ok = !any && dly_loop() && !strcmp(b, "LOOP");
        par_set(PAR_DLY_ROUTE, 64);
        fx_fresh(DELAY_NORMAL);
        par_set(PAR_DLY_ROUTE, 64);
        for (bk = 0, any = 0; bk < 60u; bk++) {
            bus_alone(1, 0, bk ? 0 : 16000);
            for (i = 0; i < BLOCK; i++)
                any |= rev_in[i];
        }
        check(ok && any && !dly_loop(), "the loop (REV > FX, FX > DLY): ECHO > REVERB held at 0, its card LOOP");
    }
    par_set(PAR_REV_FX, 0);
    par_set(PAR_ECHOREV, 0);
    /* the whole engine: D1's delay send echoes with no FX send; REV > FX with the bus OFF and PARALLEL sounds as 0 */
    {
        double r0, r1, t0, t1;
        eng_reset(0);
        kit_init_par();
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
        par_set(PAR_DLY_LEVEL, 127);
        par_set(PAR_DLY_TIME, 60);
        run_ms(50);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(250);
        t0 = out_rms(400);
        eng_reset(0);
        kit_init_par();
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
        par_set(PAR_DLY_LEVEL, 127);
        par_set(PAR_DLY_TIME, 60);
        par_set(PAR_DSEND1, 127);
        run_ms(50);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(250);
        t1 = out_rms(400);
        printf("  D1's tail: delay send 0 %.1f, 127 %.1f\n", t0, t1);
        check(t1 > t0 * 3.0 + 20.0, "a voice's DELAY SEND: its echoes, with no FX send");
        eng_reset(0);
        kit_init_par();
        par_set(PAR_REV_SEND1, 127);
        run_ms(50);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(100);
        r0 = out_rms(500);
        eng_reset(0);
        kit_init_par();
        par_set(PAR_REV_SEND1, 127);
        par_set(PAR_REV_FX, 127);
        run_ms(50);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        run_ms(100);
        r1 = out_rms(500);
        printf("  the reverb's tail: REV > FX 0 %.1f, 127 (bus OFF) %.1f\n", r0, r1);
        check(r0 > 50.0 && fabs(r1 - r0) < r0 * 0.1, "REV > FX into the bus OFF: the reverb as it was");
        eng_reset(0);
        kit_init_par();
    }
    /* the library: kits packed in 2 pages; one that won't pack, as it is in 3; a short one filled */
    {
        kl_hdr_t h;
        uint32_t j;
        int32_t n;
        kit_init_values(k);
        k[kit_idx(PAR_DSEND1 + 2)] = 99;
        k[kit_idx(PAR_DLY_ROUTE)] = 20;
        k[kit_idx(PAR_REV_FX)] = 77;
        ok = !kl_write(KLK_KIT, 120, "PACKED  ", k, KIT_LEN) && !kl_head((uint32_t)kl.at[0][120], &h) &&
             (h.kind & KL_PACKED) && h.rsv == KIT_LEN && kl_npages(h.len) <= 2u;
        printf("  a kit packed: %u bytes of %u\n", h.len, KIT_LEN);
        n = kl_read(KLK_KIT, 120, k2, KIT_LEN, 0);
        check(ok && n == (int32_t)KIT_LEN && !memcmp(k, k2, KIT_LEN), "the library: a kit packed (2 pages), read back");
        for (j = 0; j < KIT_LEN; j++)
            k[j] = (uint8_t)(200u + j % 50u);
        ok = !kl_write(KLK_KIT, 121, "RAW     ", k, KIT_LEN) && !kl_head((uint32_t)kl.at[0][121], &h) &&
             !(h.kind & KL_PACKED) && h.len == KIT_LEN && kl_npages(h.len) == 3u;
        n = kl_read(KLK_KIT, 121, k2, KIT_LEN, 0);
        check(ok && n == (int32_t)KIT_LEN && !memcmp(k, k2, KIT_LEN), "the library: one that won't pack, as it is (3 pages)");
        kit_init_values(k);
        k[kit_idx(PAR_DLY_LEVEL)] = 90;
        kl_write(KLK_KIT, 122, "OLDER   ", k, kit_idx(PAR_DSEND1));
        check(!libkit_get(122, k2) && k2[kit_idx(PAR_DLY_ROUTE)] == DLY_ROUTE_OLD && k2[kit_idx(PAR_DLY_LEVEL)] == 90u,
              "the library: a kit from before Phase 16 loads FX > DLY 127");
        kl_scan();                                   /* (power-on: the packed and the 3-page entries found again) */
        ok = kl_exists(KLK_KIT, 120) && kl_exists(KLK_KIT, 121) && kl_exists(KLK_KIT, 122);
        kl_write(KLK_KIT, 120, 0, 0, 0);
        kl_write(KLK_KIT, 121, 0, 0, 0);
        kl_write(KLK_KIT, 122, 0, 0, 0);
        check(ok && !kl_exists(KLK_KIT, 121), "the library: found again at power-on, deleted");
    }
    /* the cards */
    card_value(b, sizeof b, PAR_DLY_ROUTE, 64);
    ok = !strcmp(b, "PARALLEL");
    card_value(b, sizeof b, PAR_DLY_ROUTE, 127);
    ok &= !strcmp(b, "F>D 100%");
    card_value(b, sizeof b, PAR_DLY_ROUTE, 96);
    ok &= !strcmp(b, "F>D 50%");
    card_value(b, sizeof b, PAR_DLY_ROUTE, 0);
    ok &= !strcmp(b, "D>F 100%");
    check(ok, "Route's card: D>F 100% .. PARALLEL .. F>D 100%");
    /* FX TRACK SENDS: the last sound page; FX, Delay, Reverb; the returns under them */
    {
        uint8_t l[NSPG][2];
        uint16_t pp[4];
        uint32_t n;
        ui.mode = M_SOUND;
        ui.track = 1;
        n = sound_pages(l);
        ui.sect = S_SENDS;
        ui.half = 0;
        page_params(pp);
        ok = l[n - 1u][0] == S_SENDS && pp[0] == PAR_FX_SEND1 + 1 && pp[1] == PAR_DSEND1 + 1 &&
             pp[2] == PAR_REV_SEND1 + 1 && pp[3] == VP_NONE;
        card_name(b, sizeof b, pp[0]);
        ok &= !strcmp(b, "FX");
        card_name(b, sizeof b, pp[1]);
        ok &= !strcmp(b, "Delay");
        card_name(b, sizeof b, pp[2]);
        ok &= !strcmp(b, "Reverb");
        ui.force = 1;
        run_ms(100);
        shot("sends-track");
        ok &= l[n - 2u][0] != S_SENDS;               /* (1.0: one page; the returns are global, on FX and MIXER) */
        check(ok, "FX TRACK SENDS: the last page, one page: FX / Delay / Reverb");
        /* Control All on it: every track's delay send */
        ui.half = 0;
        par_set(PAR_FX_SEND1 + 1, 40);
        par_set(PAR_DSEND1 + 1, 90);
        par_set(PAR_REV_SEND1 + 1, 64);
        par_set(PAR_DLY_LEVEL, 100);
        par_set(PAR_DLY_ROUTE, 96);
        ui.force = 1;
        run_ms(100);
        shot("sends-track-set");
        par_set(PAR_DLY_ROUTE, 64);
        for (i = 0; i < NVOICE; i++)
            par_set(PAR_DSEND1 + i, 10u * i);
        ui.copy = 1;
        knob_turn(1, 5);
        ok = 1;
        for (i = 0; i < NVOICE; i++)
            ok &= par[PAR_DSEND1 + i] == 10u * i + 5u;
        control_all_undo();
        ui.copy = 0;
        for (i = 0; i < NVOICE; i++)
            ok &= par[PAR_DSEND1 + i] == 10u * i;
        check(ok, "Control All on FX TRACK SENDS: every track's delay send, reverted");
    }
    /* the mixer's DLY SEND, the FX pages' ROUTING and DELAY SEND */
    {
        uint8_t l[NFXPG];
        uint32_t n, j, rt = 99, ds = 99, di = 99;
        ok = NMIXER == 10u && P_MIXER[6][0] == PAR_DSEND1 && P_MIXER[7][2] == PAR_DLY_LEVEL &&
             !strcmp(MIXER_TITLE[3], "MIXER: DLY SEND");
        n = fx_shown(l);
        for (j = 0; j < n; j++) {
            rt = l[j] == FXPG_ROUTE ? j : rt;
            ds = l[j] == FXPG_DSEND ? j : ds;
            di = l[j] == FXPG_DIRT ? j : di;
        }
        ok &= rt + 1u == di && ds < n && l[ds + 1u] == FXPG_DSEND2 && l[ds + 2u] == FXPG_RSEND;
        check(ok, "the mixer's DLY SEND pages; FX: ROUTING before DIRT, DELAY SEND between FX's and REVERB's");
        ui.mode = M_MIXER;
        ui.page = 6;
        ui.force = 1;
        run_ms(100);
        shot("mixer-dly-send");
        ui.mode = M_FX;
        ui.page = rt;
        ui.force = 1;
        run_ms(100);
        shot("fx-routing");
        par_set(PAR_DLY_LEVEL, 100);
        par_set(PAR_DLY_ROUTE, 110);
        par_set(PAR_REV_FX, 80);
        par_set(PAR_ECHOREV, 60);
        ui.force = 1;
        run_ms(100);
        shot("fx-routing-loop");
        par_set(PAR_DLY_ROUTE, 20);
        ui.force = 1;
        run_ms(100);
        shot("fx-routing-dlyfx");
        par_set(PAR_DLY_ROUTE, 64);
        par_set(PAR_REV_FX, 0);
        par_set(PAR_ECHOREV, 0);
        ui.mode = M_HOME;
    }
    /* the remote: TRACKS row 7 (CC 56..63) the delay send */
    vp_set(VP_RKNOBS, RK_ABS);
    rm_in(remote_ch, 0xB0u, 56, 100);
    rm_in(remote_ch, 0xB0u, 59, 30);
    check(par[PAR_DSEND1] == 100u && par[PAR_DSEND1 + 3] == 30u, "the remote: TRACKS CC 56.. the delay sends (DRIVE's row)");
    eng_reset(0);
    kit_init_par();
}

static void phase_100_13(void)
{
    static int32_t ol[800u * BLOCK], orr[800u * BLOCK];
    uint32_t i, most;
    int32_t pk, j0, j1;
    double a1, a2, b1, b2;
    int ok = 1;
    char b[24];
    printf("1.0 Phase 13: GRAIN\n");
    p4_reset();
    /* the knob: past TAPE PP, the last; the list's name; a kit keeps it */
    check(order_turn(DLY_ORDER, NDLY_ORDER, DELAY_TAPEPP, 1) == DELAY_GRAIN &&
              order_turn(DLY_ORDER, NDLY_ORDER, DELAY_GRAIN, 1) == DELAY_GRAIN && vp_max(PAR_FX_DELAY_TYPE) == DELAY_GRAIN &&
              !strcmp(L_DELAY[DELAY_GRAIN], "GRAIN"), "GRAIN: the delay MODE knob's last, past TAPE PP");
    {
        uint8_t k[KIT_LEN];
        memset(k, 0, sizeof k);
        k[PAR_FX_DELAY_TYPE] = DELAY_GRAIN;
        k[PAR_DLY_LEVEL] = 100;
        kit_sanitize(k);
        ok = k[PAR_FX_DELAY_TYPE] == DELAY_GRAIN;
        kit_put(0, k);
        kit_get(0, k);
        check(ok && k[PAR_FX_DELAY_TYPE] == DELAY_GRAIN && kit_pk_bits <= KIT_PK * 8u, "a kit keeps GRAIN (3 bits)");
    }
    fx_fresh(DELAY_GRAIN);
    memset(&gs, 0, sizeof gs);
    check(fx.delay_type == DELAY_GRAIN && is_grain() && !is_tape(), "GRAIN plays");
    /* the cards */
    card_name(b, sizeof b, PAR_DLY_TIME);
    ok = !strcmp(b, "Position");
    card_name(b, sizeof b, PAR_DLY_WOW);
    ok &= !strcmp(b, "Size");
    card_name(b, sizeof b, PAR_GLITCH);
    ok &= !strcmp(b, "Density");
    card_name(b, sizeof b, PAR_DSAT);
    ok &= !strcmp(b, "Scatter");
    check(ok, "GRAIN's cards: Position, Size, Density, Scatter");
    card_value(b, sizeof b, PAR_DLY_WOW, 0);
    printf("  SIZE 0 %s", b);
    ok = !strcmp(b, "10 ms");
    card_value(b, sizeof b, PAR_DLY_WOW, 127);
    printf(", 127 %s", b);
    ok &= !strcmp(b, "400 ms");
    card_value(b, sizeof b, PAR_GLITCH, 0);
    printf("; DENSITY 0 %s", b);
    ok &= !strcmp(b, "OFF");
    card_value(b, sizeof b, PAR_GLITCH, 1);
    printf(", 1 %s", b);
    ok &= !strcmp(b, "0.5/s");
    card_value(b, sizeof b, PAR_GLITCH, 127);
    printf(", 127 %s", b);
    ok &= !strcmp(b, "100/s");
    card_value(b, sizeof b, PAR_DLY_TIME, 127);
    printf("; POSITION 127 %s\n", b);
    ok &= !strcmp(b, "740 ms");
    check(ok, "GRAIN's cards: SIZE 10 .. 400 ms, DENSITY OFF, 0.5 .. 100 a second, POSITION to 0.74 s");
    /* a click, SCATTER 0, short grains overlapping, POSITION 64: it comes back POSITION later (each grain reads from
     * there on) */
    par_set(PAR_DLY_TIME, 64);
    par_set(PAR_DLY_WOW, 20);
    par_set(PAR_GLITCH, 127);
    par_set(PAR_DSAT, 0);
    par_set(PAR_DLY_FB, 0);
    par_set(PAR_DPITCH, 0);
    snd_service();
    {
        uint32_t d = gr_pos(), at;
        fx_alone(400, 20000, 20000, 0);
        at = first_peak(p21_l, 100, 20000, 200);   /* (past the dry click: the bus passes its input) */
        printf("  a click, POSITION 64 (%u samples): back at %u\n", (unsigned)d, (unsigned)at);
        check(at + 800u >= d && at <= d + 800u, "GRAIN: a click comes back POSITION later");
    }
    /* grains at once: SIZE 400 ms at the most DENSITY: 8; under load (gr_cap) fewer */
    par_set(PAR_DLY_WOW, 127);
    par_set(PAR_DSAT, 127);
    snd_service();
    memset(&gs, 0, sizeof gs);
    most = gr13_run(300.0, 8000, 300, 300, 0, 0, 0);
    printf("  SIZE 400 ms, DENSITY 100/s: %u grains at once", (unsigned)most);
    ok = most == GR_N;
    gr_cap = 4;
    for (i = 0; i < GR_N; i++)
        gs.g[i].left = 0;
    most = gr13_run(300.0, 8000, 300, 300, 0, 0, 0);
    printf("; under load (cap 4) %u\n", (unsigned)most);
    gr_cap = GR_N;
    check(ok && most == 4u, "GRAIN: up to 8 grains at once, fewer under load");
    /* PITCH +12: half the grains an octave up (a 500 Hz sine: energy at 1 kHz); OFF: none there */
    par_set(PAR_DLY_TIME, 90);
    par_set(PAR_DLY_WOW, 60);
    par_set(PAR_GLITCH, 100);
    par_set(PAR_DSAT, 60);
    for (i = 0; i < 2u; i++) {
        par_set(PAR_DPITCH, i ? DP_UP12 : DP_OFF);
        snd_service();
        memset(&gs, 0, sizeof gs);
        gr13_run(500.0, 8000, 800, 800, ol, orr, 0);
        b1 = gr13_level(ol + 300u * BLOCK, 500u * BLOCK, 500.0);
        b2 = gr13_level(ol + 300u * BLOCK, 500u * BLOCK, 1000.0);
        if (i) {
            a1 = b1;
            a2 = b2;
        } else {
            j0 = (int32_t)(b2 * 1000.0 / (b1 + 1e-9));
        }
        printf("  PITCH %s: 500 Hz %.0f, 1 kHz %.0f\n", i ? "+12" : "OFF", b1, b2);
    }
    check(j0 < 30 && a2 > a1 * 0.5 && a1 > 500.0, "GRAIN: PITCH +12 puts grains an octave up (OFF: none there)");
    /* bounded: full FEEDBACK + PUSH, +12, DENSITY and SIZE at the most, a full-scale square */
    par_set(PAR_DLY_FB, 127);
    par_set(PAR_DLY_WOW, 127);
    par_set(PAR_GLITCH, 127);
    par_set(PAR_DPITCH, DP_UP12);
    snd_service();
    fx.push = 65536;
    memset(&gs, 0, sizeof gs);
    gr13_run(0, 32767, 800, 800, ol, orr, 0);
    fx.push = 0;
    for (i = 0, pk = 0; i < 800u * BLOCK; i++)
        pk = iabs(ol[i]) > pk ? iabs(ol[i]) : iabs(orr[i]) > pk ? iabs(orr[i]) : pk;
    printf("  full FEEDBACK + PUSH, +12, a full-scale square: peak %d\n", (int)pk);
    check(pk < 2 * 32768, "GRAIN: bounded at full feedback and density");
    /* silent after silence (half FEEDBACK: the loop dies away, no limit cycle) */
    par_set(PAR_DLY_FB, 64);
    par_set(PAR_DPITCH, DP_OFF);
    snd_service();
    gr13_run(300.0, 8000, 100, 100, 0, 0, 0);
    gr13_run(0, 0, 3000, 0, 0, 0, 0);
    gr13_run(0, 0, 100, 0, ol, orr, 0);
    for (i = 0, pk = 0; i < 100u * BLOCK; i++)
        pk = iabs(ol[i]) > pk ? iabs(ol[i]) : pk;
    printf("  silence after (4.4 s), FEEDBACK 64: peak %d\n", (int)pk);
    check(pk <= 2, "GRAIN: silent after silence");
    /* FREEZE: the line held (not written), the grains still playing it */
    par_set(PAR_DLY_FB, 0);
    snd_service();
    gr13_run(300.0, 8000, 400, 400, 0, 0, 0);
    {
        static int16_t keep[FXBUF_SIZE];
        uint32_t w = fx.d_write;
        memcpy(keep, fxbuf, sizeof keep);
        fx.freeze = 1;
        gr13_run(700.0, 8000, 400, 400, ol, orr, 0);
        fx.freeze = 0;
        a1 = sh12_level(ol + 200u * BLOCK, 200u * BLOCK, 300.0);
        a2 = sh12_level(ol + 200u * BLOCK, 200u * BLOCK, 700.0);
        printf("  FREEZE: 300 Hz (held) %.0f, 700 Hz (the input) %.1f\n", a1, a2);
        printf("  .. the line %s, the head %s\n", memcmp(keep, fxbuf, sizeof keep) ? "written" : "held", w == fx.d_write ? "held" : "moved");
        check(!memcmp(keep, fxbuf, sizeof keep) && w == fx.d_write && a1 > 500.0 && a2 < a1 * 0.01,
              "GRAIN: FREEZE holds the line, the grains play it on");
    }
    /* a MODE change: TAPE's echoes fade, the line cleared, then GRAIN's grains; no step bigger than the sound's own */
    fx_fresh(DELAY_TAPE);
    par_set(PAR_DLY_TIME, 40);
    par_set(PAR_DLY_FB, 80);
    par_set(PAR_DLY_WOW, 0);                         /* (TAPE plain: no wow, glitches, saturation) */
    par_set(PAR_GLITCH, 0);
    par_set(PAR_DSAT, 0);
    snd_service();
    {
        int32_t jt = 0, jg = 0, jp = 0, ja = 0, jb = 0;   /* (steady: TAPE, GRAIN, PING PONG; the changes) */
        gr13_run(200.0, 8000, 600, 600, 0, 0, 0);
        gr13_run(200.0, 8000, 300, 300, 0, 0, &jt);
        par_set(PAR_FX_DELAY_TYPE, DELAY_GRAIN);
        par_set(PAR_DLY_WOW, 60);
        par_set(PAR_GLITCH, 90);
        par_set(PAR_DSAT, 60);
        snd_service();
        gr13_run(200.0, 8000, 200, 200, ol, orr, &ja);    /* (the echoes out, the line cleared, the grains in) */
        ok = fx.delay_type == DELAY_GRAIN && gr13_active() > 0u;
        gr13_run(200.0, 8000, 400, 400, 0, 0, &jg);
        par_set(PAR_FX_DELAY_TYPE, DELAY_PINGPONG);
        snd_service();
        gr13_run(200.0, 8000, 200, 200, ol, orr, &jb);
        ok &= fx.delay_type == DELAY_PINGPONG && !gr13_active();
        gr13_run(200.0, 8000, 600, 600, 0, 0, 0);
        gr13_run(200.0, 8000, 300, 300, 0, 0, &jp);
        printf("  the largest step a sample: TAPE %d, -> GRAIN %d, GRAIN %d, -> PING PONG %d, PING PONG %d\n", (int)jt,
               (int)ja, (int)jg, (int)jb, (int)jp);
        check(ok && ja <= (jt > jg ? jt : jg) * 5 / 4 && jb <= (jg > jp ? jg : jp) * 5 / 4,
              "GRAIN: MODE changes in and out without a click (no step past either side's own)");
    }
    /* the pages: GRAIN's own second (SIZE, SCATTER, DENSITY, PITCH; the user's order), then TONE .. BAND */
    par_set(PAR_FX_DELAY_TYPE, DELAY_GRAIN);
    par_set(PAR_DLY_TIME, 70);
    par_set(PAR_DLY_WOW, 70);
    par_set(PAR_GLITCH, 90);
    par_set(PAR_DSAT, 80);
    par_set(PAR_DLY_FB, 60);
    par_set(PAR_DPITCH, DP_UP12);
    snd_service();
    goto_mode(B_FX, M_FX);
    {
        uint8_t o[NFXPG];
        uint32_t n = fx_shown(o), k;
        for (k = 0; k < n && o[k] != FXPG_DLY; k++)
            ;
        check(k + 2u < n && o[k + 1u] == FXPG_DUB && o[k + 2u] == FXPG_DLY2, "GRAIN: its page second, after MODE's");
        par_set(PAR_FX_DELAY_TYPE, DELAY_TAPE);
        n = fx_shown(o);
        for (k = 0; k < n && o[k] != FXPG_DLY; k++)
            ;
        check(k + 2u < n && o[k + 1u] == FXPG_DLY2 && o[k + 2u] == FXPG_DUB, "TAPE: its page third, as before");
        par_set(PAR_FX_DELAY_TYPE, DELAY_GRAIN);
        snd_service();
    }
    fx_goto(FXPG_DLY);
    ui.force = 1;
    run_ms(100);
    shot("fx-grain");
    fx_goto(FXPG_DUB);
    ui.force = 1;
    run_ms(100);
    {
        char t[32];
        uint32_t pg, np;
        title_text(t, sizeof t, &pg, &np);
        check(!strcmp(t, "GRAIN (2/3)"), "GRAIN: its page titled GRAIN (2/3)");
    }
    shot("fx-grain-2");
    /* 1.0 (the user, 2026-11-08): MODE into GRAIN with Density OFF starts a cloud; MODE doesn't morph, so at 100%
     * (SPLIT) the knob moves the kit's MODE and GRAIN's page shows */
    {
        uint32_t i;
        uint8_t ms[KIT_LEN];
        memcpy(ms, morph_par, KIT_LEN);
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
        par_set(PAR_GLITCH, 0);
        par_set(PAR_DLY_LEVEL, 0);
        for (i = 0; i < NVOICE; i++)
            par_set(PAR_DSEND1 + i, 0);
        snd_service();
        fx_goto(FXPG_DLY);
        ui.msg_ms = 0;
        turn(EN_K1, 10);
        check(par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN && par[PAR_GLITCH] == 84u && par[PAR_DLY_WOW] == 88u &&
                  par[PAR_DSAT] == 50u && par[PAR_DLY_LEVEL] == 100u && !strcmp(ui.msg, "GRAIN: TURN UP A DELAY SEND"),
              "GRAIN from Density OFF: Size, Scatter, Density, Level set; no delay send: said");
        par_set(PAR_GLITCH, 20);
        turn(EN_K1, -1);
        turn(EN_K1, 1);
        check(par[PAR_GLITCH] == 20u, "GRAIN with Density up: kept");
        turn(EN_K1, -10);
        for (i = 0; i < KIT_LEN; i++)
            morph_par[i] = par[kit_par(i)];
        kit_set_morph(255);
        snd_service();
        turn(EN_K1, 10);
        run_ms(300);
        turn(EN_SELECT, 1);
        check(par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN && fx.delay_type == DELAY_GRAIN && fx_page() == FXPG_DUB,
              "morph 100% (SPLIT): MODE is the kit's, GRAIN's page second");
        morph_par[kit_idx(PAR_FX_DELAY_TYPE)] = DELAY_TAPE;
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
        kit_set_morph(180);
        snd_service();
        run_ms(300);
        check(fx.delay_type == DELAY_NORMAL, "MODE doesn't morph (the morph kit's TAPE not reached)");
        kit_set_morph(0);
        memcpy(morph_par, ms, KIT_LEN);
    }
    par_set(PAR_FX_DELAY_TYPE, DELAY_OFF);
    par_set(PAR_DLY_LEVEL, 0);
    par_set(PAR_DPITCH, DP_OFF);
    par_set(PAR_GLITCH, 0);
    par_set(PAR_DLY_WOW, 0);
    par_set(PAR_DSAT, 0);
    snd_service();
    goto_mode(B_HOME, M_HOME);
    /* 1.0: the pattern page: CLEAR (SHIFT + COPY), a pattern, then COPY deletes it (SHIFT keeps it) */
    {
        pattern_t *p5 = &pat[4];
        p5->main[0][0] = 0x1111u;
        p5->gen[1][0] = 1;
        p5->shuffle = 9;
        mode_set(M_PATTERN);
        ui.bank = 0;
        run_ms(50);
        hold(B_OCTDN);
        press(B_OCTUP);
        release(B_OCTDN);
        ok = ui.pdel == 1u;
        white(4);
        ok &= ui.pdel == 2u && ui.pdel_n == 4u;
        run_ms(50);
        shot("pattern-delete-ask");
        white(6);                                    /* (keys wait while it asks) */
        ok &= ui.pdel == 2u && sq.pending != 6u;
        press(B_OCTDN);
        ok &= ui.pdel == 0u && p5->main[0][0] == 0x1111u;
        check(ok, "PATTERNS: CLEAR, a pattern: DELETE PATTERN P05?; SHIFT keeps it");
        hold(B_OCTDN);
        press(B_OCTUP);
        release(B_OCTDN);
        white(4);
        press(B_OCTUP);
        check(ui.pdel == 0u && pat_blank(p5) && !p5->shuffle && !p5->gen[1][0] && !strcmp(ui.msg, "P05 DELETED"),
              "PATTERNS: COPY deletes it (blank)");
        hold(B_OCTDN);
        press(B_OCTUP);
        release(B_OCTDN);
        press(B_OCTUP);
        check(ui.pdel == 0u, "PATTERNS: COPY alone after CLEAR cancels");
        p5->main[2][1] = 0x0101u;                    /* (SHIFT (OCT-) held + the pattern: the same question) */
        shot("pattern-page");
        hold(B_OCTDN);
        check(!shift_menu(), "PATTERNS: SHIFT held shows the patterns (no menu)");
        white(4);
        ok = ui.pdel == 2u && ui.pdel_n == 4u;
        press(B_OCTUP);
        release(B_OCTDN);
        check(ok && ui.pdel == 0u && pat_blank(p5), "PATTERNS: SHIFT (OCT-) + a pattern, COPY: deleted");
        goto_mode(B_HOME, M_HOME);
    }
}

/* 1.0 Phase 11: a global LFO's FOLLOW (an envelope follower on a voice), a 4th global LFO */
static uint32_t fol11_blocks(uint32_t r)             /* RELEASE r: blocks from full to a tenth, the source silent */
{
    uint32_t n = 0, i;
    gls[0].rate = (uint8_t)r;
    gls[0].phase = 0;
    gls[0].trig = GF_ALL;
    gl.fol[0] = 65536;
    for (i = 0; i < NVOICE; i++)
        vpk[i] = 0;
    while (gf_step(0) > 6554 && n < 100000u)
        n++;
    return n + 1u;
}
static void phase_100_11(void)
{
    uint16_t pp[4];
    char b[24];
    uint32_t i, n0, n64, n127;
    int32_t lo, hi, g0, gpk;
    int ok;
    printf("1.0 Phase 11: FOLLOW, a 4th global LFO\n");
    p4_reset();
    perf_defaults();
    /* the release: 10 ms .. 2 s to a tenth (a block 1.451 ms) */
    n0 = fol11_blocks(0);
    n64 = fol11_blocks(64);
    n127 = fol11_blocks(127);
    printf("  RELEASE to a tenth: 0 %.1f ms, 64 %.0f ms, 127 %.0f ms\n", n0 * 1.4512, n64 * 1.4512, n127 * 1.4512);
    check(n0 >= 6u && n0 <= 9u && n64 * 1.4512 > 120 && n64 * 1.4512 < 170 && n127 * 1.4512 > 1850 &&
          n127 * 1.4512 < 2150, "FOLLOW: RELEASE 10 ms .. 2 s (to a tenth), ~141 ms halfway");
    /* the threshold: a level under it reads 0; over, from 0; full 6 dB under the top */
    gl.fol[0] = 0;
    gls[0].trig = 1;
    gls[0].rate = 0;
    gls[0].phase = 64;                               /* (-36 dB: 520) */
    vpk[1] = 450;
    lo = gf_step(0);
    vpk[1] = 16384;
    hi = gf_step(0);
    vpk[0] = 32767;                                  /* (not the source) */
    vpk[1] = 0;
    for (i = 0; i < 40u; i++)
        g0 = gf_step(0);
    printf("  THRESHOLD -36 dB: %d under it, %d at -6 dB, %d with D1 loud and D2 the source\n", (int)lo, (int)hi, (int)g0);
    check(lo == 0 && hi > 60000 && g0 == 0, "FOLLOW: THRESHOLD, the SOURCE alone");
    gls[0].trig = GF_ALL;
    vpk[1] = 0;
    vpk[4] = 20000;
    check(gf_step(0) > 65000, "FOLLOW: SOURCE ALL takes the loudest (CY)");
    vpk[4] = 0;
    gl.fol[0] = 0;
    /* a kick through FOLLOW ducking the reverb's sends (REV SEND, -127) */
    gls[0].dest = GD_RSEND;
    gls[0].depth = -127;
    gls[0].wave = GW_FOLLOW;
    gls[0].trig = 0;                                 /* (D1) */
    gls[0].phase = 0;
    gls[0].rate = 40;                                /* (~57 ms) */
    run_ms(200);
    g0 = (int32_t)rev_g;
    voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
    gpk = 65536;
    hi = 0;
    for (i = 0; i < 20u; i++) {
        run_ms(1);
        gpk = (int32_t)rev_g < gpk ? (int32_t)rev_g : gpk;
        hi = gl.out[0] > hi ? gl.out[0] : hi;
    }
    run_ms(1500);
    printf("  D1 ducks REV SEND: rev_g %d before, %d at the hit (follower %d), %d 1.5 s on\n", (int)g0, (int)gpk,
           (int)hi, (int)rev_g);
    check(g0 > 65000 && hi > 30000 && gpk < 65536 - 15000 && rev_g > 65000u, "FOLLOW: a kick ducks REVERB SEND, back after");
    gls[0].depth = 127;
    voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
    gpk = 0;
    for (i = 0; i < 20u; i++) {
        run_ms(1);
        gpk = (int32_t)rev_g > gpk ? (int32_t)rev_g : gpk;
    }
    check(gpk > 65536 + 15000, "FOLLOW: + DEPTH pushes (from where it is, up only)");
    /* the pages: LFO 4's, FOLLOW's knobs */
    goto_mode(B_ARP, M_PERF);
    perf_goto(PG_GLFO + 7u);
    page_params(pp);
    check(pp[0] == GLV(3, 3) && pp[1] == GLV(3, 6) && pp[2] == GLV(3, 4) && pp[3] == GLV(3, 5) && ui.page == PG_MACRO - 1u,
          "PERF: LFO 4's second page before MACROS");
    turn(EN_K1, 30);
    check(gls[3].wave == GW_FOLLOW, "LFO 4: WAVE past EXP DN: FOLLOW");
    page_params(pp);
    card_name(b, sizeof b, pp[3]);
    ok = pp[2] == VP_NONE && !strcmp(b, "Source");
    card_name(b, sizeof b, pp[1]);
    ok &= !strcmp(b, "Thresh");
    turn(EN_K4, 20);
    card_value(b, sizeof b, pp[3], vp_get(pp[3]));
    ok &= gls[3].trig == GF_ALL && !strcmp(b, "ALL");
    check(ok, "FOLLOW: Source (to ALL), Thresh; MODE blank");
    turn(EN_K2, -200);
    card_value(b, sizeof b, pp[1], vp_get(pp[1]));
    ok = !strcmp(b, "OFF");
    gls[3].phase = 127;
    card_value(b, sizeof b, pp[1], 127);
    ok &= !strcmp(b, "-12 dB");
    card_value(b, sizeof b, pp[1], 1);
    ok &= !strcmp(b, "-60 dB");
    gls[3].phase = 0;
    check(ok, "FOLLOW: THRESHOLD OFF, -60 .. -12 dB");
    turn(EN_SELECT, -1);
    page_params(pp);
    card_name(b, sizeof b, pp[3]);
    ok = pp[2] == VP_NONE && !strcmp(b, "Release");
    card_value(b, sizeof b, pp[3], 0);
    ok &= !strcmp(b, "10 ms");
    card_value(b, sizeof b, pp[3], 127);
    ok &= !strcmp(b, "2.0 s");
    card_value(b, sizeof b, pp[3], 64);
    printf("  RELEASE's card at 64: %s\n", b);
    check(ok, "FOLLOW: Release 10 ms .. 2.0 s, SYNC blank");
    gls[3].dest = GD_PUMP;
    gls[3].depth = 100;
    play_from_start();
    run_ms(300);
    shot("glfo-follow");
    press(B_PLAY);
    turn(EN_SELECT, 1);
    turn(EN_K1, -1);
    check(gls[3].wave == LFO_DRIFT && gls[3].trig == GT_PLAY, "out of FOLLOW: its SOURCE back to a trigger (PLAY)");
    turn(EN_K1, 1);
    gls[3].trig = GF_ALL;
    /* the remote: CC 2 on GLOBAL's channel, LFO 4's depth */
    rm_in(remote_gch, 0xB0u, 2u, 0);
    lo = gls[3].depth;
    rm_in(remote_gch, 0xB0u, 2u, 127);
    check(lo == -127 && gls[3].depth == 127, "the remote: GLOBAL's CC 2, LFO 4's depth");
    /* the project: the 4th, FOLLOW's settings */
    gls[3].phase = 50;
    gls[3].rate = 70;
    {
        int rc = proj_save(1, "FOLLOW  ");
        perf_defaults();
        rc |= proj_load(1);
        check(rc == 0 && gls[3].wave == GW_FOLLOW && gls[3].trig == GF_ALL && gls[3].phase == 50 && gls[3].rate == 70 &&
                  gls[3].dest == GD_PUMP && gls[3].depth == 127 && gls[0].wave == GW_FOLLOW && gls[0].trig == 0,
              "project: LFO 4, FOLLOW (SOURCE ALL, THRESHOLD, RELEASE)");
    }
    perf_defaults();
    goto_mode(B_HOME, M_HOME);
}

static void phase_100_10(void)
{
    uint32_t e, k, i, n = 0;
    int32_t pk = 0;
    int ok = 1;
    printf("1.0 Phase 10: the phaser\n");
    eng_reset(0);
    fx.route = R_ST1;
    fx.dw = fx.dw_last = 16384;                      /* (50%: the notches deepest) */
    fx.type = fx.last_type = FX_PHASER;
    fx.p[0] = 0;                                     /* (the slowest LFO: it stands still over a measurement) */
    fx.p[1] = 50000;
    fx.p[2] = 0;
    fx.p[3] = 30000;
    fx.push = 0;
    for (e = 0; e < 2u; e++) {                       /* the LFO's extremes (sine_at: -cos) */
        uint32_t ph = e ? 0x80000000u : 0u;
        double a = ph_coef(ph) / 65536.0, nf[2], lo, hi;
        uint32_t j;
        /* where the 4 allpasses turn the phase 180 and 540 degrees: each stage pi/4 or 3 pi/4 behind; a first-order
         * allpass (-a + z^-1) / (1 - a z^-1) lags 2 atan(tan(w / 2) (1 + a) / (1 - a)) */
        for (j = 0; j < 2u; j++)
            nf[j] = 2.0 * atan(tan((j ? 3.0 : 1.0) * 3.14159265358979 / 8.0) * (1.0 - a) / (1.0 + a)) * FS /
                    (2.0 * 3.14159265358979);
        lo = ph10_resp(ph, nf[0], 0);
        hi = ph10_resp(ph, nf[1], 0);
        printf("  LFO at %s: notches %.0f Hz (%.3f), %.0f Hz (%.3f); the right there %.2f, %.2f\n", e ? "top" : "bottom",
               nf[0], lo, nf[1], hi, ph10_resp(ph, nf[0], 1), ph10_resp(ph, nf[1], 1));
        ok &= lo < 0.03 && hi < 0.03 && ph10_resp(ph, sqrt(nf[0] * nf[1]), 0) > 0.9;
        ok &= ph10_resp(ph, nf[0], 1) > 0.9 && ph10_resp(ph, nf[1], 1) > 0.9;   /* (R: its notches between) */
    }
    check(ok, "PHASER: the notches where the four allpasses put them (as before), the right's between (wide)");
    /* bounded: full-scale squares, the most feedback and FB PUSH, the corner low and high */
    fx.dw = fx.dw_last = 32768;
    for (e = 0; e < 2u; e++) {
        double hz;
        for (hz = 15.0; hz < 3000.0; hz *= 1.5) {
            memset(fx.ph_s, 0, sizeof fx.ph_s);
            fx.p[0] = fx.p[1] = fx.p[2] = 65536;
            fx.p[3] = e ? 65536 : 0;
            fx.push = 65536;
            for (k = 0; k < 400u; k++) {
                for (i = 0; i < BLOCK; i++, n++) {
                    int16_t v = fmod(n * hz / FS, 1.0) < 0.5 ? 32767 : -32768;
                    fx_l[i] = v;
                    fx_r[i] = (k & 64u) ? v : (int16_t)-v;
                    mix_l[i] = mix_r[i] = 0;
                }
                fx_phaser();
                for (i = 0; i < 5u; i++)
                    pk = iabs(fx.ph_s[0][i] >> 8) > pk ? iabs(fx.ph_s[0][i] >> 8) : pk;
                for (i = 0; i < BLOCK; i++)
                    ok &= mix_l[i] >= -65536 && mix_l[i] <= 65536 && mix_r[i] >= -65536 && mix_r[i] <= 65536;
            }
        }
    }
    printf("  full-scale squares at the most feedback: the states' peak %d (x %.1f full scale)\n", pk, pk / 32768.0);
    check(ok && pk < (1 << 26), "PHASER: bounded at full feedback and FB PUSH, no overflow");
    /* silence after it: the chain settles to 0 (the products' rounding leaves no DC) */
    fx.push = 0;
    fx.p[2] = 50000;
    fx.p[3] = 0;
    for (k = 0; k < 2000u; k++) {
        for (i = 0; i < BLOCK; i++)
            fx_l[i] = fx_r[i] = 0, mix_l[i] = mix_r[i] = 0;
        fx_phaser();
    }
    pk = 0;
    for (i = 0; i < BLOCK; i++)
        pk = iabs(mix_l[i]) > pk ? iabs(mix_l[i]) : iabs(mix_r[i]) > pk ? iabs(mix_r[i]) : pk;
    printf("  after silence: %d left, the states %d %d %d %d %d\n", pk, fx.ph_s[0][0], fx.ph_s[0][1], fx.ph_s[0][2], fx.ph_s[0][3], fx.ph_s[0][4]);
    check(pk <= 1, "PHASER: silent after silence (no DC left in the chain)");
    /* another type and back: the states from silence */
    fx.ph_s[0][2] = 12345;
    fx.ph_a[0] = 777;
    fx.type = FX_FLANGER;
    fxbus_process();
    check(fx.ph_s[0][2] == 0 && fx.ph_a[0] == 0, "PHASER: a type change clears its states");
    fx.type = fx.last_type = FX_OFF;
    eng_reset(0);
}
static void phase_100_6b(void)
{
    uint32_t i, ok;
    printf("1.0: the delay's BAND\n");
    p4_reset();
    goto_mode(B_HOME, M_HOME);
    press(B_FX);
    fx_goto(FXPG_DLY2);
    run_ms(40);
    check(par[PAR_DLY_BAND] == 0 && fx.band == 0, "BAND: OFF in a new kit");
    turn(EN_K4, 64);
    run_ms(40);
    check(par[PAR_DLY_BAND] == 64 && fx.band == 64, "BAND: K4 turns it on");
    shot("fx-delay-band");
    gls[0].dest = GD_DBAND;
    gls[0].depth = 127;
    gls[0].len = 0;
    gls[0].wave = LFO_TRI;
    for (ok = 0, i = 0; i < 200u; i++) {
        run_ms(1);
        ok |= fx.lfo_band > 30000 || fx.lfo_band < -30000;
    }
    check(ok && gd_turn(GD_MORPH, 1) == GD_DBAND, "GLOBAL LFO DELAY BAND: moved either way (DELAY's first, by name)");
    memset(gls, 0, sizeof gls);
    run_ms(20);
    check(fx.lfo_band == 0, "BAND: back with the LFO off");
    gcc(99, 127);
    check(par[PAR_DLY_BAND] == 127, "GLOBAL CC 99: BAND (1.0: the delay's row, 96 .. 103)");
    gcc(99, 0);
    check(par[PAR_DLY_BAND] == 0 && fx.band == 0, "GLOBAL CC 99 at 0: BAND OFF");
    /* 1.0: the global LFOs on RING's WET and TONE, either way from the knob's */
    gls[0].dest = GD_RWET;
    gls[0].depth = 127;
    gls[0].len = 0;
    gls[0].wave = LFO_TRI;
    gls[1] = gls[0];
    gls[1].dest = GD_RTONE;
    gls[1].depth = -64;
    {
        int32_t lo = 127, hi = 0, tlo = 127, thi = 0, w, t;
        for (i = 0; i < 300u; i++) {
            run_ms(1);
            w = ring_mod(pfs.dj_wet, GD_RWET);
            t = ring_mod(pfs.dj_tone, GD_RTONE);
            lo = w < lo ? w : lo;
            hi = w > hi ? w : hi;
            tlo = t < tlo ? t : tlo;
            thi = t > thi ? t : thi;
        }
        check(lo <= 4 && hi >= 123 && tlo <= 36 && tlo >= 30 && thi >= 92 && thi <= 98,
              "GLOBAL LFO RING WET (depth 127: the whole range), RING TONE (-64: +-32 about the middle)");
    }
    check(gd_turn(GD_LP, 1) == GD_RTONE && gd_turn(GD_RTONE, 1) == GD_RWET, "RING TONE, RING WET after the DJ filter's, by name");
    memset(gls, 0, sizeof gls);
    run_ms(20);
    check(ring_mod(64, GD_RWET) == 64 && ring_mod(64, GD_RTONE) == 64, "RING: the knobs' with the LFOs off");
    check(morph_mix(PAR_DLY_BAND, 0, 90, 100) == 0 && morph_mix(PAR_DLY_BAND, 0, 90, 140) == 90 &&
              morph_mix(PAR_DLY_BAND, 30, 90, 128) > 30u && morph_mix(PAR_DLY_BAND, 30, 90, 128) < 90u,
          "morph: BAND on or off halfway, between two settings smoothly");
    goto_mode(B_HOME, M_HOME);
}
static void phase_100_6(void)
{
    uint16_t pp[4];
    uint32_t i;
    int16_t l[BLOCK], r[BLOCK];
    printf("1.0 Phase 6: the character filters, the global filter\n");
    p4_reset();
    goto_mode(B_HOME, M_HOME);
    press(B_FX);
    fx_goto(FXPG_DJ);
    run_ms(40);
    page_params(pp);
    check(fx_page() == FXPG_DJ && pp[0] == VP_DJTYPE && pp[1] == VP_DJ && pp[2] == VP_DJRESO, "FX: DJ FILTER: Type, the knob, Reso");
    turn(EN_K1, 1);
    check(pfs.dj_ftype == DJT_RING, "Type: RING second (1.0)");
    turn(EN_K1, 1);
    check(pfs.dj_ftype == DJT_ACID, "Type: ACID");
    vp_set(VP_DJRESO, 110);
    pf.dj = 20;                                      /* (left: the low-pass) */
    for (i = 0; i < BLOCK; i++)
        l[i] = r[i] = (int16_t)(i & 8u ? 9000 : -9000);
    for (i = 0; i < 100u; i++)                       /* (1.0: Reso glides, ~10 ms) */
        perf_master(l, r);
    check(pf.dj_f[0].was == F_ACID && pf.dj_f[1].was == F_ACID && pf.dj_f[0].rk == 110 * 516, "the master through ACID, at its Reso");
    run_ms(40);
    shot("fx-dj-acid");
    turn(EN_K1, 1);
    pf.dj = 110;                                     /* (right: MS20's high-pass) */
    perf_master(l, r);
    check(pfs.dj_ftype == DJT_MS20 && pf.dj_f[0].was == F_MS20H, "MS20: its high-pass right of the middle");
    run_ms(40);
    shot("fx-dj-ms20");
    page_params(pp);
    check(pp[3] == VP_NONE, "Tune: not for MS20");
    turn(EN_K1, 5);
    check(pfs.dj_ftype == DJT_MS20, "Type: MS20 the last (1.0)");
    turn(EN_K1, -2);
    page_params(pp);
    check(pfs.dj_ftype == DJT_RING && pp[3] == VP_DJTUNE, "Type: RING, with Tune");
    {                                                /* 1.0: RING: the chord's notes ring, the others don't */
        char nm[16];
        uint32_t k, j, root;
        static int16_t y[200u * BLOCK];
        static float yf[200u * BLOCK];
        double g0, g7, g4, gx, g1;
        vp_set(VP_DJTUNE, RC_MAJ);
        vp_set(VP_DJRESO, 100);
        pf.dj = 100;
        root = ring_root((100 - 64) * 1040);
        card_name(nm, sizeof nm, VP_DJRESO);
        check(!strcmp(nm, "Decay"), "RING: Reso is Decay");
        card_value(nm, sizeof nm, VP_DJ, 100);
        check(root == 69u && !strcmp(nm, "A4"), "RING: the knob's root, by name");
        for (k = 0; k < 200u; k++) {
            for (j = 0; j < BLOCK; j++)
                l[j] = r[j] = (int16_t)(k == 100u && j == 0u ? 30000 : 0);   /* (after the Decay's glide) */
            perf_master(l, r);
            for (j = 0; j < BLOCK; j++)
                y[k * BLOCK + j] = (int16_t)((l[j] + r[j]) >> 1);
        }
        for (k = 0; k < 90u * BLOCK; k++)
            yf[k] = y[101u * BLOCK + k];
#define NOTE_HZ(n) (440.0 * pow(2.0, ((double)(n) - 69.0) / 12.0))
        g0 = goertzel(yf, 90u * BLOCK, NOTE_HZ(root));
        g4 = goertzel(yf, 90u * BLOCK, NOTE_HZ(root + 4u));
        g7 = goertzel(yf, 90u * BLOCK, NOTE_HZ(root + 7u));
        g1 = goertzel(yf, 90u * BLOCK, NOTE_HZ(root + 1u));
        gx = goertzel(yf, 90u * BLOCK, NOTE_HZ(root + 10u));
        printf("  RING MAJOR on A4: root %.2f, 3rd %.2f, 5th %.2f; b9 %.2f, b7 %.2f\n", g0, g4, g7, g1, gx);
        check(g0 > 3.0 * g1 && g4 > 3.0 * g1 && g7 > 3.0 * gx && g0 > 3.0 * gx, "RING: the chord rings, not the notes beside it");
        shot("fx-dj-ring");
        {                                            /* 1.0 Phase 10e: the bands at half rate: the top one (NOTE's 8th
                                                      * harmonic of F5, 5.59 kHz) rings at its pitch; a tone at its
                                                      * alias (22.05 - 5.59 kHz) doesn't ring it */
            double gt = 0, gb = 0, gs = 0, ga = 0, ft, fa;
            uint32_t p;
            vp_set(VP_DJTUNE, RC_NOTE);
            pf.dj = 115;
            root = ring_root((115 - 64) * 1040);
            ft = NOTE_HZ(root + 36u);
            fa = FS / 2.0 - ft;
            for (p = 0; p < 3u; p++) {               /* (a hit; a tone at the band; one at its alias) */
                memset(pf.rg_s, 0, sizeof pf.rg_s);
                for (k = 0; k < 120u; k++) {
                    for (j = 0; j < BLOCK; j++)
                        l[j] = r[j] = (int16_t)(p ? 4000.0 * sin(2 * M_PI * (p == 1u ? ft : fa) * (k * BLOCK + j) / FS)
                                                  : (k == 20u && j == 0u ? 30000 : 0));
                    perf_master(l, r);
                    for (j = 0; j < BLOCK; j++)
                        yf[k * BLOCK + j] = (float)((l[j] + r[j]) >> 1);
                }
                if (p == 0u) {
                    gt = goertzel(yf + 21u * BLOCK, 60u * BLOCK, ft);
                    gb = goertzel(yf + 21u * BLOCK, 60u * BLOCK, ft * 0.94);
                } else
                    *(p == 1u ? &gs : &ga) = goertzel(yf + 40u * BLOCK, 80u * BLOCK, ft);
            }
            printf("  RING NOTE on F5: the top band (%.0f Hz) rings %.3f, a semitone below %.3f; a tone there %.1f, "
                   "at its alias %.4f\n", ft, gt, gb, gs, ga);
            check(gt > 4.0 * gb && ga < gs * 0.01, "RING at half rate: the top band at its pitch, its alias -40 dB");
            vp_set(VP_DJTUNE, RC_MAJ);
            pf.dj = 100;
        }
    }
    {                                                /* (the project keeps it) */
        int rc = proj_save(2, "DJTYPE  ");
        pfs.dj_ftype = DJT_LXR;
        pfs.dj_tune = RC_NOTE;
        rc |= proj_load(2);
        check(rc == 0 && pfs.dj_ftype == DJT_RING && pfs.dj_reso == 100 && pfs.dj_tune == RC_MAJ,
              "a project keeps the DJ filter's type and RING's chord");
    }
    pf.dj = 64;
    pfs.dj_ftype = DJT_LXR;
    pfs.dj_reso = 0;
    perf_master(l, r);
    goto_mode(B_HOME, M_HOME);
}

static void phase_100_5(void)
{
    uint8_t k[KIT_LEN];
    uint32_t i, t, a, b, ok, lo, hi, moved;
    char c[32];
    printf("1.0 Phase 5: time, the delay, WANDER\n");
    p4_reset();
    /* an old kit: bus DELAY -> the delay on, the bus OFF; D2's FX P target -> D1's */
    kit_init_values(k);
    memset(&k[kit_idx(PAR_DLY_TIME)], 0, 5);
    k[kit_idx(PAR_FX_TYPE)] = FX_DELAY;
    k[kit_idx(PAR_FX_DELAY_TYPE)] = DELAY_TAPE;
    k[kit_idx(PAR_FX_P1)] = 70;
    k[kit_idx(PAR_FX_P2)] = 30;
    k[kit_idx(PAR_FX_P3)] = 90;
    k[kit_idx(PAR_FX_P4)] = 80;
    k[kit_idx(PAR_FX_DRY_WET)] = 50;
    k[PAR_TARGET_LFO1] = (uint8_t)(TGT_DLY + 1u);
    kit_sanitize(k);
    check(k[kit_idx(PAR_FX_TYPE)] == FX_OFF && k[kit_idx(PAR_DLY_LEVEL)] == 50u * 127u / 100u && k[kit_idx(PAR_DLY_FB)] == 80u &&
              k[kit_idx(PAR_DLY_TONE)] == 90u && k[kit_idx(PAR_DLY_TIME)] == 70u && k[kit_idx(PAR_DLY_WOW)] == 30u,
          "an old DELAY kit: the delay on (its level, feedback, tone, TAPE's time and wow), the bus OFF");
    check(k[PAR_TARGET_LFO1] == MOD_TARGET_VOICE[0][1] - 2u, "an old kit's D2 FX P2 target: D1's");
    check(mod_dest(TGT_DLY, 0) == PAR_DLY_TIME && mod_dest(TGT_DLY + 3u, 2) == PAR_DLY_LEVEL, "LFO targets: the delay's time .. level");
    /* MONO -> PING PONG: the write head in the smaller line */
    par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
    run_ms(5);
    fx.d_write = FXBUF_MASK - 3u;
    par_set(PAR_FX_DELAY_TYPE, DELAY_PINGPONG);
    run_ms(5);
    check(fx.d_write <= delay_mask(), "MONO -> PING PONG: the write head inside the line");
    /* the delay beside the bus's PHASER; LEVEL 0: it drains, then sleeps */
    par_set(PAR_FX_TYPE, FX_PHASER);
    par_set(PAR_DLY_LEVEL, 90);
    run_ms(50);
    check(fx.type == FX_PHASER && fx.dl_lvl > 0 && dly_active(), "the delay on beside the PHASER");
    par_set(PAR_DLY_LEVEL, 0);
    run_ms(1500);
    check(!dly_active() && !fx.dl_drain, "LEVEL 0: the echoes drained, the delay asleep");
    par_set(PAR_FX_TYPE, FX_OFF);
    /* TIME STRETCH */
    for (i = 0; i < 16u; i++)
        main_set(&pat[0], 0, i, 1), main_set(&pat[0], 1, i, 1), main_set(&pat[0], 2, i, 1);
    pat_backup = pat[0];
    seq_stretch_set(1, 5000);
    seq_stretch_set(2, 20000);
    play_from_start();
    run_ms(4000);                                    /* (2 bars at 120) */
    a = p5_pos(0);
    check(a >= 30u && p5_pos(1) * 2u + 1u >= a && p5_pos(1) * 2u <= a + 1u && p5_pos(2) + 1u >= 2u * a && p5_pos(2) <= 2u * a + 1u,
          "STRETCH 50% / 200%: half the steps, twice the steps");
    printf("  2 bars: %u steps at 100%%, %u at 50%%, %u at 200%%\n", (unsigned)a, (unsigned)p5_pos(1), (unsigned)p5_pos(2));
    press(B_PLAY);
    run_ms(20);
    check(sq.tacc[1] == 0u && sq.idx[1] < 0 && (sq.tfree & 6u) == 6u, "STOP: the tracks realigned (the speeds kept)");
    seq_stretch_set(1, TSTR_ONE);
    seq_stretch_set(2, TSTR_ONE);
    seq_stretch_set(1, 9900);
    play_from_start();
    b = sq.ticks;
    run_ms(4000);
    b = sq.ticks - b + 1u;
    check(-sq.toff[1] >= (int32_t)b * 655 && -sq.toff[1] <= (int32_t)(b + 24u) * 655 && p5_pos(1) <= p5_pos(0),
          "99%: falling behind the clock, a tick at a time");
    printf("  99%%: %d ticks behind after %u (toff %d, rate %u, pos %u %u)\n", (int)(-sq.toff[1] / 65536), (unsigned)b, (int)sq.toff[1], (unsigned)sq.trate[1], (unsigned)p5_pos(1), (unsigned)p5_pos(0));
    seq_stretch_home(1);
    for (i = 0; i < 400u && (sq.thome & 2u); i++)
        run_ms(100);
    run_ms(500);
    check(!(sq.thome & 2u) && sq.tstr[1] == TSTR_ONE && sq.tacc[1] == 0u && sq.toff[1] == 0 && sq.idx[1] == sq.idx[0] &&
              sq.tph[1] == sq.tph[0], "back into time: gliding, then 100% on the clock's steps");
    /* a pattern change: kept (the default), REALIGN: in time again */
    seq_stretch_set(1, 9900);
    run_ms(2000);
    pat[1] = pat[0];
    seq_set_next_pattern(1);
    a = sq.toff[1];
    for (i = 0; i < 100u && sq.active != 1u; i++)
        run_ms(50);
    check(sq.active == 1u && sq.toff[1] != 0 && (sq.tfree & 2u), "a pattern change: the stretched track keeps its place");
    vp_set(VP_REALIGN, 1);
    seq_set_next_pattern(0);
    for (i = 0; i < 100u && sq.active != 0u; i++)
        run_ms(50);
    check(sq.active == 0u && kset.realign && sq.toff[1] > -(int32_t)(4u * 65536u * 3u), "REALIGN: in time again at a pattern change");
    vp_set(VP_REALIGN, 0);
    press(B_PLAY);
    (void)a;
    /* in a project (KTS1) */
    seq_stretch_set(3, 12345);
    {
        int rc = proj_save(1, "STRETCH ");
        seq_stretch_set(3, TSTR_ONE);
        rc |= proj_load(1);
        check(rc == 0 && sq.tstr[3] == 12345u && sq.tstr[1] == 9900u, "a project: each track's stretch");
    }
    for (t = 0; t < NTRACK; t++)
        seq_stretch_set(t, TSTR_ONE);
    seq_reset_tracks();
    goto_mode(B_HOME, M_HOME);
    /* JOURNEY: 1024 BARS, then ENDLESS; an old link's ENDLESS (6) is still ENDLESS */
    wd_card_value(c, sizeof c, WV_JOURNEY, 6);
    ok = !strcmp(c, "1K BARS") && WD_BARS[6] == 1024u;
    wd_card_value(c, sizeof c, WV_JOURNEY, 7);
    check(ok && !strcmp(c, "ENDLESS") && wd_vmax(WV_JOURNEY) == WJ_ENDLESS, "JOURNEY: .. 512, 1024 BARS, ENDLESS");
    wd_defaults();
    a = web_share_get();
    web_share[WEB_SHARE_V2] = 1;
    web_share[WEB_SHARE_V2 + 1u + WV_JOURNEY] = 6;
    b = web_share_put(WEB_SHARE_V5);
    check(a == WEB_SHARE_V6 + NGLKGL + 1u && b == 0u && wd.k[WV_JOURNEY] == WJ_ENDLESS, "an older phone link's ENDLESS: ENDLESS");
    web_share[WEB_SHARE_V2 + 1u + WV_JOURNEY] = 6;
    b = web_share_put(a);
    check(b == 0u && wd.k[WV_JOURNEY] == 6u, "a new one's 1024 BARS: 1024 BARS");
    if (sq.running)
        press(B_PLAY);
    wd_defaults();
    p4_reset();
    /* BOILING FROG: the tempo with the heat, a track drifting out of time and back */
    for (i = 0; i < 16u; i++)
        main_set(&pat[0], 0, i & ~3u, 1), main_set(&pat[0], 5, i, 1), main_set(&pat[0], 6, i, 1);
    pat_backup = pat[0];
    perf_goto(PG_WANDER);
    wd_vset(WV_QUEST, Q_FROG);
    check(wd.k[WV_FATE] == WF_LOST && wd.k[WV_JOURNEY] == 5u, "BOILING FROG: LOST, 512 bars at first");
    wd.k[WV_JOURNEY] = 0;                            /* (16 bars) */
    wd.k[WV_RANGE] = 127;
    wd.k[WV_STRIDE] = 127;
    press(B_PLAY);
    press(B_OCTUP);
    lo = hi = 120;
    moved = 0;
    for (i = 0; i < 15u; i++) {
        wd_bars(1);
        lo = seq_bpm() < lo ? seq_bpm() : lo;
        hi = seq_bpm() > hi ? seq_bpm() : hi;
        for (t = 0; t < NTRACK; t++)
            moved |= (sq.tstr[t] != TSTR_ONE) << t;
    }
    printf("  BOILING FROG: %u .. %u BPM, drifting 0x%02x, the heat %d\n", (unsigned)lo, (unsigned)hi, (unsigned)moved, wd.fh);
    shot("p5-frog");
    check(hi - lo >= 6u && moved && !(moved & 9u), "BOILING FROG: the tempo creeps; the colour drifts (not the kick, the snare)");
    wd_bars(2);
    for (i = 0; i < 300u && sq.thome; i++)
        run_ms(100);
    for (t = 0, ok = 1; t < NTRACK; t++)
        ok &= sq.tstr[t] == TSTR_ONE && sq.toff[t] == 0;
    check(wd.state == WS_IDLE && ok && seq_bpm() != 120u, "its end (LOST): the drift in time again; the tempo stays");
    press(B_PLAY);
    p4_reset();
    /* THE CANYON: the voices in from the edges, each its own send, then apart; MONO -> PING PONG with the echoes
     * faded out first; the feedback rings at 126-127; THE WAY OUT: all of it home */
    for (i = 0; i < 16u; i += 2u)
        main_set(&pat[0], 1, i, 1), main_set(&pat[0], 4, i + 1u, 1);
    pat_backup = pat[0];
    par_set(PAR_DLY_LEVEL, 0);
    par_set(PAR_DSYNC, 0);
    par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
    par_set(PAR_PAN1, 10);
    par_set(PAR_PAN4, 118);
    for (t = 0; t < NVOICE; t++)
        par_set(PAR_DSEND1 + t, 0);                  /* (1.0 Phase 16: CANYON moves the delay sends) */
    a = par[PAR_REV_SIZE];
    perf_goto(PG_WANDER);
    wd_vset(WV_QUEST, Q_CANYON);
    wd.k[WV_JOURNEY] = 2;                            /* (64 bars) */
    wd.k[WV_RETURN] = 0;
    wd.k[WV_STRIDE] = 127;
    press(B_PLAY);
    press(B_OCTUP);
    {
        uint32_t fbmax = 0, pp = 0, cleanpp = 1, mode = par[PAR_FX_DELAY_TYPE], lvl = par[PAR_DLY_LEVEL], inward = 0;
        uint32_t diff = 0, panmin = 127, panmax = 0, sz = 0, lv = 0, lost = 0;
        for (i = 0; i < 70u && wd.state == WS_ROAD; i++) {
            run_ms(240000u / seq_bpm());
            if (par[PAR_FX_DELAY_TYPE] != mode) {
                pp |= par[PAR_FX_DELAY_TYPE] == DELAY_PINGPONG;
                cleanpp &= lvl == 0u || par[PAR_DLY_LEVEL] == 0u;   /* (the echoes gone before the mode changed) */
                mode = par[PAR_FX_DELAY_TYPE];
            }
            lvl = par[PAR_DLY_LEVEL];
            fbmax = par[PAR_DLY_FB] > fbmax ? par[PAR_DLY_FB] : fbmax;
            sz = par[PAR_REV_SIZE] > sz ? par[PAR_REV_SIZE] : sz;
            lv = par[PAR_DLY_LEVEL] > lv ? par[PAR_DLY_LEVEL] : lv;
            lost |= wd_cht() % 5u == 3u;             /* (the path is random, its seed the clock: LOST may not come) */
            if (wd_cht() == 0u && i == 6u)
                inward = par[PAR_PAN1] > 10u && par[PAR_PAN4] < 118u && par[PAR_PAN1] < 40u && par[PAR_PAN4] > 88u;
            if (par[PAR_DSEND1 + 1u] != par[PAR_DSEND1 + 4u])
                diff++;
            if (wd_cht() >= 2u && wd_cht() < 4u) {
                panmin = par[PAR_PAN2] < panmin ? par[PAR_PAN2] : panmin;
                panmax = par[PAR_PAN2] > panmax ? par[PAR_PAN2] : panmax;
            }
            if (i == 30u)
                shot("p5-canyon");
        }
        printf("  THE CANYON, %u bars: level to %u, feedback to %u, size %u -> %u; D2's pan %u..%u, sends apart %u bars%s\n",
               (unsigned)i, (unsigned)lv, (unsigned)fbmax, (unsigned)a, (unsigned)sz, (unsigned)panmin, (unsigned)panmax,
               (unsigned)diff, lost ? ", LOST IN ECHOES on the way" : ", never LOST");
        check(inward, "THE CANYON: at the mouth, the voices drawn in a little from where they were");
        check(diff >= 8u && panmax >= panmin + 10u, ".. each voice its own send, then the voices wander apart");
        check(pp && cleanpp, "MONO -> PING PONG, the echoes faded out first");
        check(lv >= 60u && sz > a && fbmax >= (lost ? 126u : 78u),
              ".. the echoes build, the reverb grows, the feedback rings at 126+ (when LOST)");
        run_ms(500);
        check(wd.state == WS_IDLE && par[PAR_DLY_LEVEL] == 0u && !par[PAR_DSYNC] && par[PAR_REV_SIZE] == a &&
                  par[PAR_FX_DELAY_TYPE] == DELAY_NORMAL && par[PAR_PAN1] == 10u && par[PAR_PAN4] == 118u &&
                  par[PAR_DSEND1 + 1u] == 0u, "out of the canyon (HOME): the delay, the reverb, the pans, the sends home");
    }
    wd.k[WV_JOURNEY] = WJ_ENDLESS;                   /* (ENDLESS: never out) */
    wd.quest = Q_CANYON;
    for (i = 0, ok = 1; i < 50u; i++) {
        wd_plan(wd_rnd(5));
        for (t = 0; t < wd.npath; t++)
            ok &= wd.path[t] != 4u;
    }
    check(ok, "THE CANYON, ENDLESS: the way out never comes");
    press(B_PLAY);
    wd_defaults();
    p4_reset();
    /* 1.0: HOWLING CHASM: stereo tape, the howl (Band on, Feedback pushed), the tape rung out (sends down) into GRAIN,
     * RING tuned low with its root sliding, back to tape halfway through THE DARK; home: the delay and the DJ filter */
    for (i = 0; i < 16u; i += 2u)
        main_set(&pat[0], 1, i, 1), main_set(&pat[0], 4, i + 1u, 1);
    pat_backup = pat[0];
    par_set(PAR_DLY_LEVEL, 0);
    par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
    par_set(PAR_DLY_BAND, 0);
    for (t = 0; t < NVOICE; t++)
        par_set(PAR_DSEND1 + t, 0);
    vp_set(VP_DJTYPE, DJT_LXR);
    vp_set(VP_DJ, 64);
    vp_set(VP_DJWET, 64);
    vp_set(VP_DJRESO, 0);
    perf_goto(PG_WANDER);
    wd_vset(WV_QUEST, Q_CHASM);
    check(wd.k[WV_FATE] == WF_HOME && wd.k[WV_JOURNEY] == 4u, "HOWLING CHASM: FATE HOME, 256 bars at first");
    wd.k[WV_JOURNEY] = 3;                            /* (128 bars) */
    wd.k[WV_RETURN] = 2;                             /* (2 bars: the ring fades out on the way) */
    wd.k[WV_STRIDE] = 127;
    press(B_PLAY);
    press(B_OCTUP);
    {
        uint32_t mode = par[PAR_FX_DELAY_TYPE], tpp = 0, grain = 0, back = 0, clean = 1, band = 0, fbmax = 0, ring = 0;
        uint32_t wet = 0, roots = 0, lastdj = 64, chs = 0, dark = 0;
        uint8_t snd[NVOICE];
        for (i = 0; i < 150u && wd.state == WS_ROAD; i++) {
            for (t = 0; t < NVOICE; t++)
                snd[t] = par[PAR_DSEND1 + t];
            run_ms(240000u / seq_bpm());
            if (par[PAR_FX_DELAY_TYPE] != mode) {
                if (par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN) {
                    grain = 1;
                    for (t = 0; t < NVOICE; t++)     /* (rung out: nothing went in for the last bars) */
                        clean &= snd[t] == 0u;
                } else if (par[PAR_FX_DELAY_TYPE] == DELAY_TAPEPP) {
                    tpp = 1;
                    back |= grain;
                }
                mode = par[PAR_FX_DELAY_TYPE];
            }
            band |= par[PAR_DLY_BAND] > 0u;
            fbmax = par[PAR_DLY_FB] > fbmax ? par[PAR_DLY_FB] : fbmax;
            ring |= pfs.dj_ftype == DJT_RING;
            wet = pfs.dj_ftype == DJT_RING && pfs.dj_wet > wet ? pfs.dj_wet : wet;
            if (pfs.dj_ftype == DJT_RING && pf.dj != lastdj && pf.dj < 64u && !(wd.djtg[0] == pf.dj && lastdj == 64u))
                roots++;
            lastdj = pf.dj;
            chs |= 1u << wd_cht();
            dark |= wd_cht() == 4u;
            if (wd_cht() == 3u && pfs.dj_wet > 60u && !(chs & 0x100u)) {
                chs |= 0x100u;
                shot("wander-chasm");
            }
        }
        printf("  HOWLING CHASM, %u bars: chapters %02x, feedback to %u, RING wet to %u, %u root steps\n", (unsigned)i,
               (unsigned)(chs & 0xFFu), (unsigned)fbmax, (unsigned)wet, (unsigned)roots);
        check(tpp && band && fbmax >= 110u, "HOWLING CHASM: TAPE PP, Band on, Feedback pushed into the howl");
        check(grain && clean, ".. the tape rung out (the delay sends at 0) before GRAIN");
        check(ring && wet >= 70u && roots >= 3u, ".. RING on the mix, tuned low, its root sliding");
        check(dark && back, ".. THE DARK: halfway, the grains rung out and the tape again");
        run_ms(4u * 240000u / seq_bpm());
        check(wd.state == WS_IDLE && par[PAR_FX_DELAY_TYPE] == DELAY_NORMAL && par[PAR_DLY_LEVEL] == 0u &&
                  par[PAR_DLY_BAND] == 0u && par[PAR_DSEND1 + 1u] == 0u && par[PAR_DPITCH] == DP_OFF,
              "out of the chasm (HOME): the delay as it was");
        check(pfs.dj_ftype == DJT_LXR && pf.dj == 64u && pfs.dj_wet == 64u && pfs.dj_reso == 0u && !wd.djon,
              ".. and the DJ filter: RING faded out, then home's type, position, wet and decay");
    }
    wd.k[WV_JOURNEY] = WJ_ENDLESS;                   /* (ENDLESS: never out) */
    wd.quest = Q_CHASM;
    for (i = 0, ok = 1; i < 50u; i++) {
        wd_plan(wd_rnd(6));
        for (t = 0; t < wd.npath; t++)
            ok &= wd.path[t] != HX_OUT;
    }
    check(ok, "HOWLING CHASM, ENDLESS: the way out never comes");
    check(wd_dj_at(36) < 56u && ring_root(((int32_t)wd_dj_at(36) - 64) * 1040) == 36u &&
              ring_root(((int32_t)wd_dj_at(30) - 64) * 1040) == 30u, "RING's root: C2, F#1 where they should be");
    press(B_PLAY);
    wd_defaults();
    p4_reset();
}

/* 1.0 Phase 17: Control All by MIDI (GLOBAL's 62 held, 63 latched, 71 revert), tracks left out of it */
static void phase_100_17(void)
{
    uint32_t v, ok;
    uint8_t k0[KIT_LEN], acc = kset.accel, rk = kset.rknobs;
    uint32_t i;
    printf("1.0 Phase 17: Control All by MIDI\n");
    p4_reset();
    track_select(0);
    kit_init_par();
    snd_post_all();
    kset.accel = 0;
    kset.caout = 0;
    vp_set(VP_RKNOBS, RK_ABS);
    for (v = 0; v < NVOICE; v++)
        par[PAR_FILTER_FREQ_1 + v] = (uint8_t)(20u + 15u * v);
    for (i = 0; i < KIT_LEN; i++)
        k0[i] = par[kit_par(i)];
    /* latched by a tap on 62 (1.0: 63 is RING Spread now): the remote's filter knob on D2 moves every track's filter by its step */
    for (i = 0; i < 40u; i++) {                      /* (the feedback settled; the out queue drained as it goes) */
        run_ms(10);
        (void)fb_seen(0, 0, 0);
    }
    gcc(62, 127);
    ok = fb_seen(0xB0u | remote_gch, 62, 127);
    gcc(62, 0);
    check(ca_rm == 2u && !ui.ca, "62 tapped: Control All latched, nothing moved yet");
    for (i = 0; i < 30u; i++) {                 /* (the out queue drained as it goes) */
        run_ms(10);
        ok |= fb_seen(0xB0u | remote_gch, 62, 127);
    }
    check(ok, "62's light on");
    rcc(24 + 1, 50);                                 /* (TRACKS' filter row, D2: 35 -> ABS 50) */
    for (v = 0, ok = 1; v < NVOICE; v++)
        ok &= par[PAR_FILTER_FREQ_1 + v] == 20u + 15u * v + 15u;
    check(ok && ui.ca, "a TRACKS filter knob: every voice's filter by D2's step, offsets kept");
    /* an LXR CC on a voice's channel (CC n: parameter n - 1): D1's filter, the others by its step */
    web_midi(0xB0 | midi_track_ch(0), PAR_FILTER_FREQ_1 + 1u, 25);
    run_ms(20);
    for (v = 0, ok = 1; v < NVOICE; v++)
        ok &= par[PAR_FILTER_FREQ_1 + v] == 20u + 15u * v + 5u;
    check(ok, "the LXR's filter CC on D1's channel: every voice by D1's step (-10)");
    /* an NRPN (Elixir's per-voice: the delay send) */
    par[PAR_DSEND1 + 3] = 10;
    web_midi(0xB0 | midi_track_ch(3), 99, (PAR_DSEND1 + 3u - 128u) >> 7);
    web_midi(0xB0 | midi_track_ch(3), 98, (PAR_DSEND1 + 3u - 128u) & 127u);
    web_midi(0xB0 | midi_track_ch(3), 6, 30);
    run_ms(20);
    check(par[PAR_DSEND1 + 3] == 30u && par[PAR_DSEND1] == 30u - 10u && par[PAR_DSEND1 + 5] == 20u,
          "an NRPN delay send on SN: every track's delay send by its step");
    /* a track left out (the remote's select 64 + 2: D3): back to the snapshot, then not moved */
    gbtn(64 + 2);
    check((kset.caout & 4u) && par[PAR_FILTER_FREQ_1 + 2] == k0[kit_idx(PAR_FILTER_FREQ_1 + 2)] &&
              par[PAR_DSEND1 + 2] == 0u, "select 66 with Control All on: D3 left out, its voice back as it was");
    check(ui.track == 0, "no track selected");
    rcc(24 + 0, 35);                                 /* D1 25 -> 35: +10 */
    check(par[PAR_FILTER_FREQ_1 + 1] == 50u && par[PAR_FILTER_FREQ_1 + 2] == 50u,
          "D3 left out: not moved; D2 moved");
    rcc(24 + 2, 60);
    check(par[PAR_FILTER_FREQ_1 + 2] == 60u && par[PAR_FILTER_FREQ_1 + 1] == 50u, "D3's own knob: D3 alone");
    /* the hats: out only when CH and OH both are */
    gbtn(64 + 5);
    v = par[PAR_FILTER_FREQ_1 + 5];
    rcc(24 + 0, 36);
    check(par[PAR_FILTER_FREQ_1 + 5] == v + 1u, "CH alone left out: the hat voice still in (OH)");
    gbtn(64 + 6);
    rcc(24 + 0, 37);
    check(par[PAR_FILTER_FREQ_1 + 5] == k0[kit_idx(PAR_FILTER_FREQ_1 + 5)], "CH and OH out: the hat voice out, as it was");
    run_ms(60);
    shot("ca-midi-strip");
    /* the panel: the knobs on a sound page as with OCT+ held; a track key out / in */
    black(1);
    check(kset.caout & 2u, "the panel's track key, Control All latched: D2 out");
    black(1);
    check(!(kset.caout & 2u) && ui.track == 0 && par[PAR_FILTER_FREQ_1 + 1] == 35u,
          "again: back in (left out, it went back as it was)");
    press(B_SCL);
    turn(EN_K2, 2);
    check(par[PAR_FILTER_FREQ_1] == 39u && par[PAR_FILTER_FREQ_1 + 1] == 37u && par[PAR_FILTER_FREQ_1 + 2] == 60u,
          "a panel knob on FILTER with Control All latched: every track in it");
    run_ms(60);
    shot("ca-midi-sound");
    /* off: kept, no dialog; 71 reverts until a kit loads */
    gbtn(62);
    check(!ca_rm && !ui.ca && !ui.ca_ask && par[PAR_FILTER_FREQ_1] == 39u, "62 again: off, kept, no dialog");
    for (i = ok = 0; i < 30u; i++) {
        run_ms(10);
        ok |= fb_seen(0xB0u | remote_gch, 71, 127);
    }
    check(ok, "71's light: a Control All to revert");
    gbtn(71);
    for (i = 0, ok = 1; i < KIT_LEN; i++)
        ok &= par[kit_par(i)] == k0[i];
    check(ok && !ui.ca_kept, "71: the kit as before the first Control All move");
    /* held by 62: on while held; OCT- on the panel reverts */
    gcc(62, 127);
    check(ca_rm == 1u, "62 held: on");
    rcc(24 + 0, 30);
    check(par[PAR_FILTER_FREQ_1 + 3] == k0[kit_idx(PAR_FILTER_FREQ_1 + 3)] + 10u, "62 held: a knob moves the tracks in");
    press(B_OCTDN);
    for (i = 0, ok = 1; i < KIT_LEN; i++)
        ok &= par[kit_par(i)] == k0[i];
    check(ok && !ui.shift, "OCT- with Control All on by MIDI: reverted, not SHIFT");
    rcc(24 + 0, 40);
    gcc(62, 0);
    check(!ca_rm && !ui.ca && par[PAR_FILTER_FREQ_1 + 3] == k0[kit_idx(PAR_FILTER_FREQ_1 + 3)] + 20u,
          "62 let go after a move (a short press): off, kept");
    gcc(62, 127);
    run_ms(400);
    gcc(62, 0);
    check(!ca_rm, "62 held long, nothing moved: off at the release");
    /* as a note, and a kit load ends the revert */
    gnote(62, 1);
    gnote(62, 0);
    check(ca_rm == 2u, "62 tapped as a note: latched");
    rcc(24 + 0, 50);
    check(ui.ca_kept && par[PAR_FILTER_FREQ_1 + 3] == k0[kit_idx(PAR_FILTER_FREQ_1 + 3)] + 30u, "a move by note's latch");
    gnote(62, 1);
    gnote(62, 0);
    check(!ca_rm, "62 as a note again: off");
    kit_load_ram(0);
    run_ms(20);
    v = par[PAR_FILTER_FREQ_1];
    gbtn(71);
    check(par[PAR_FILTER_FREQ_1] == v && msg_on() && ui.msg[13] == 'N', "a kit loaded: 71 has nothing to revert");
    /* off: CCs as before (one track) */
    v = par[PAR_FILTER_FREQ_1 + 1];
    rcc(24 + 0, 64);
    check(par[PAR_FILTER_FREQ_1] == 64u && par[PAR_FILTER_FREQ_1 + 1] == v, "Control All off: D1 alone");
    /* the mask is kept in the settings */
    check(kset.caout == 0x64u, "the left-out tracks in the settings (D3, CH, OH)");
    kset.caout = 0;
    kset.accel = acc;
    kset.rknobs = rk;
    goto_mode(B_HOME, M_HOME);
    kit_init_par();
    snd_post_all();
}

/* 1.0 Phase 18: RING (the DJ filter) (2/2): SPREAD, SLOPE, TONE, WET */
#define RG18_HASH 0x30837059u                        /* (RING's output before Phase 18: the defaults reproduce it) */
static uint32_t rg18_run(uint32_t in, float *yl, float *yr, uint32_t nb)   /* RING on in (0 a hit at block 20, 1 DC,
                                                      * 2 noise then silence) from rest; its hash */
{
    int16_t l[BLOCK], r[BLOCK];
    uint32_t k, j, h = 2166136261u, x = 12345u;
    pf.dj_type = 0;                                  /* (its state reset) */
    pf.dj_rs = (int32_t)pfs.dj_reso << 8;            /* (Decay without its glide) */
    for (k = 0; k < nb; k++) {
        for (j = 0; j < BLOCK; j++) {
            x = x * 1103515245u + 12345u;
            l[j] = r[j] = (int16_t)(in == 0u ? (k == 20u && j == 0u ? 30000 : 0) : in == 1u ? 8000
                                    : k < nb / 2u ? (int32_t)(x >> 16) % 12000 - 6000 : 0);
            if (in == 2u)
                r[j] = (int16_t)(l[j] / 2);
        }
        perf_master(l, r);
        for (j = 0; j < BLOCK; j++) {
            h = (h ^ (uint16_t)l[j]) * 16777619u;
            h = (h ^ (uint16_t)r[j]) * 16777619u;
            if (yl)
                yl[k * BLOCK + j] = l[j];
            if (yr)
                yr[k * BLOCK + j] = r[j];
        }
    }
    return h;
}
static void phase_100_18(void)
{
    static float yl[200u * BLOCK], yr[200u * BLOCK];
    uint16_t pp[4];
    uint32_t h, root, k, ok;
    double a0, a1, b[3], e0, e1;
    printf("1.0 Phase 18: RING (2/2): Spread, Slope, Tone, Wet\n");
    p4_reset();
    goto_mode(B_HOME, M_HOME);
    press(B_FX);
    pfs.dj_ftype = DJT_LXR;
    fx_goto(FXPG_DJ2);
    check(fx_page() != FXPG_DJ2, "RING (2/2): not with the LXR's filter");
    pfs.dj_ftype = DJT_RING;
    fx_goto(FXPG_DJ2);
    run_ms(40);
    page_params(pp);
    check(fx_page() == FXPG_DJ2 && pp[0] == VP_DJSPREAD && pp[1] == VP_DJSLOPE && pp[2] == VP_DJTONE && pp[3] == VP_DJWET,
          "RING (2/2): Spread, Slope, Tone, Wet");
    check(pfs.dj_spread == 64 && pfs.dj_slope == 64 && pfs.dj_tone == 64 && pfs.dj_wet == 64, "RING (2/2): 64s by default");
    turn(EN_K4, -3);
    check(pfs.dj_wet == 61, "RING (2/2): Wet by the knob");
    {
        char nm[16];
        card_value(nm, sizeof nm, VP_DJWET, 127);
        ok = !strcmp(nm, "100%");
        card_value(nm, sizeof nm, VP_DJSPREAD, 0);
        ok &= !strcmp(nm, "MONO");
        card_value(nm, sizeof nm, VP_DJTONE, 80);
        ok &= !strcmp(nm, "+16");
        card_value(nm, sizeof nm, VP_DJSLOPE, 60);
        check(ok && !strcmp(nm, "-4"), "RING (2/2): the cards' values");
    }
    pfs.dj_wet = 64;
    pfs.dj_tune = RC_MAJ;
    pfs.dj_reso = 100;
    pf.dj = 100;                                     /* (A4) */
    root = ring_root((100 - 64) * 1040);
    h = rg18_run(2u, NULL, NULL, 120u);
    printf("  RING's hash at the defaults: 0x%08x\n", h);
    check(h == RG18_HASH, "RING (2/2): the defaults bit-exact with before");
    pfs.dj_wet = 0;                                  /* (Wet 0: the dry alone) */
    rg18_run(1u, yl, NULL, 40u);
    for (ok = 1, k = 20u * BLOCK; k < 40u * BLOCK; k++)
        ok &= yl[k] == 8000.0f;
    check(ok, "RING: Wet 0 the dry exactly");
    pfs.dj_wet = 127;                                /* (Wet 127: no dry: DC rings no band) */
    rg18_run(1u, yl, NULL, 120u);
    for (e0 = 0, k = 60u * BLOCK; k < 120u * BLOCK; k++)   /* (the mean: the bands still ring from the step) */
        e0 += yl[k] / (60.0 * BLOCK);
    printf("  RING Wet 100%%, DC 8000 in: %.1f out (the mean)\n", e0);
    check(fabs(e0) < 80.0, "RING: Wet 127 no dry");
    pfs.dj_wet = 64;
    for (k = 0; k < 2u; k++) {                       /* (Tone: the top band (MAJOR's +28) against the root) */
        pfs.dj_tone = k ? 127 : 0;
        rg18_run(0u, yl, NULL, 120u);
        *(k ? &a1 : &a0) = goertzel(yl + 21u * BLOCK, 60u * BLOCK, NOTE_HZ(root + 28u)) /
                           goertzel(yl + 21u * BLOCK, 60u * BLOCK, NOTE_HZ(root));
    }
    printf("  RING Tone: the top band / the root: dark %.3f, bright %.3f\n", a0, a1);
    check(a1 > 16.0 * a0, "RING: Tone tilts the bands' level");
    pfs.dj_tone = 64;
    pfs.dj_reso = 30;                                /* (Decay short: room above for Slope; the bands' damping has a
                                                      * floor, ring_block's q >= 33) */
    for (k = 0; k < 3u; k++) {                       /* (Slope: the top band's ring late against early, the root's) */
        pfs.dj_slope = (uint8_t)(k * 127u / 2u);
        rg18_run(0u, yl, NULL, 200u);
        e0 = goertzel(yl + 70u * BLOCK, 30u * BLOCK, NOTE_HZ(root + 28u)) / goertzel(yl + 21u * BLOCK, 30u * BLOCK, NOTE_HZ(root + 28u));
        e1 = goertzel(yl + 70u * BLOCK, 30u * BLOCK, NOTE_HZ(root)) / goertzel(yl + 21u * BLOCK, 30u * BLOCK, NOTE_HZ(root));
        b[k] = e0 / e1;
    }
    printf("  RING Slope: the top band's decay against the root's: left %.4f, middle %.3f, right %.2f\n", b[0], b[1], b[2]);
    check(b[0] < 0.1 * b[1] && b[2] > 2.0 * b[1], "RING: Slope shortens / lengthens the top bands' ring");
    pfs.dj_slope = 64;
    pfs.dj_reso = 100;
    pfs.dj_spread = 0;                               /* (Spread 0: all in the middle) */
    rg18_run(0u, yl, yr, 60u);
    for (ok = 1, k = 0; k < 60u * BLOCK; k++)
        ok &= yl[k] == yr[k];
    check(ok, "RING: Spread 0 mono");
    pfs.dj_spread = 127;                             /* (127: the 3rd (band 1) on the right alone) */
    rg18_run(0u, yl, yr, 120u);
    a0 = goertzel(yl + 21u * BLOCK, 60u * BLOCK, NOTE_HZ(root + 4u));
    a1 = goertzel(yr + 21u * BLOCK, 60u * BLOCK, NOTE_HZ(root + 4u));
    printf("  RING Spread 127: the 3rd left %.3f, right %.3f\n", a0, a1);
    check(a0 * 8.0 < a1, "RING: Spread 127 hard left / right");
    pfs.dj_spread = 64;
    pfs.dj_slope = 96;                               /* (the page: the bands' heights by Slope, their light by Tone) */
    pfs.dj_tone = 30;
    rg18_run(0u, NULL, NULL, 2u);
    run_ms(40);
    shot("fx-dj-ring2");
    pfs.dj_slope = pfs.dj_tone = 64;
    {                                                /* (the project keeps them) */
        int rc;
        pfs.dj_spread = 10;
        pfs.dj_slope = 20;
        pfs.dj_tone = 30;
        pfs.dj_wet = 40;
        rc = proj_save(2, "RING2   ");
        pfs.dj_spread = pfs.dj_slope = pfs.dj_tone = pfs.dj_wet = 64;
        rc |= proj_load(2);
        check(rc == 0 && pfs.dj_spread == 10 && pfs.dj_slope == 20 && pfs.dj_tone == 30 && pfs.dj_wet == 40,
              "a project keeps RING's Spread, Slope, Tone, Wet");
    }
    pfs.dj_spread = pfs.dj_slope = pfs.dj_tone = pfs.dj_wet = 64;
    pfs.dj_reso = 0;
    pf.dj = 64;
    pfs.dj_ftype = DJT_LXR;
    {
        int16_t l[BLOCK] = {0}, r[BLOCK] = {0};
        perf_master(l, r);
    }
    goto_mode(B_HOME, M_HOME);
}

/* 1.0: the copy and picture pass's screens: a long attack */
static void copy_pass(void)
{
    ui.mode = M_SOUND;
    ui.track = 0;
    ui.sect = S_AEG;
    ui.half = 0;
    par_set(PAR_VELOA1, 90);                         /* (a long attack: rising slowly, then into the decay) */
    ui.force = 1;
    run_ms(60);
    shot("aeg-attack");
    ui.sect = S_XMOD;                                /* (cross mod with no source: a short line, an empty chip) */
    par_set(PAR_XSRC1, 0);
    ui.force = 1;
    run_ms(60);
    shot("xmod-off");
    {                                                /* (MELODY: each Shape drawn abstractly) */
        static const uint8_t SH[6] = {MEL_OFF, MEL_RANDOM, MEL_WALK, MEL_ARP_UP, MEL_MOTIF, MEL_PEDAL};
        pattern_t *mp = shown_pat();
        uint8_t sh0 = mp->mel[0][ML_SHAPE];
        uint32_t i;
        ui.mode = M_EUCLID;
        ui.page = PG_MELODY;
        ui.track = 0;
        for (i = 0; i < 6u; i++) {
            mp->mel[0][ML_SHAPE] = SH[i];
            ui.force = 1;
            run_ms(40);
            shot("melody-shape");
        }
        mp->mel[0][ML_SHAPE] = sh0;
    }
    ui.mode = M_PERF;                                /* (WANDER's HOME: Order and Sway only with Goal SONG) */
    {
        uint16_t pp[4];
        uint32_t g0 = wd.k[WV_GOAL];
        ui.page = PG_WANDER + 2u;
        wd.k[WV_GOAL] = 0;
        page_params(pp);
        check(pp[0] == VP_WD + WV_RETURN && pp[2] == VP_NONE && pp[3] == VP_NONE, "WANDER HOME: no Order, Sway off a song");
        wd.k[WV_GOAL] = WG_SONG;
        page_params(pp);
        check(pp[2] == VP_WD + WV_ORDER && pp[3] == VP_WD + WV_SWAY, ".. Goal SONG: Order and Sway (1.0)");
        wd.k[WV_GOAL] = (uint8_t)g0;
    }
    {                                                /* (1.0: CPU OVERLOAD: flashing, with why) */
        uint8_t cw = cores_why;
        uint32_t c0 = cpu_q8;
        uint16_t q[NLD];
        memcpy(q, ld_q8, sizeof q);
        par_set(PAR_MODEL1 + 1, MD_MODAL);
        par_set(PAR_MODEL1 + 2, MD_PL0 + PE_SD);
        run_ms(20);
        cores_why = CW_TWO;                          /* (core 1 behind: its models) */
        cpu_q8 = 230;
        ld_q8[LD_WAIT] = 120;
        ld_q8[LD_D13] = 250;
        ui.mode = M_FX;
        fx_goto(FXPG_DLY);
        run_ms(OVL_MS + 100u);                       /* (lone lates, 2 s apart: no warning) */
        audio_late++;
        run_ms(OVL_MS + 100u);
        check(!ovl_on, "CPU OVERLOAD: a lone late half isn't one");
        audio_late++;
        run_ms(40);
        check(!ovl_on, "CPU OVERLOAD: nor one 2 s after another");
        audio_late++;                                /* (a second within 2 s: the warning) */
        run_ms(40);
        check(ovl_on && !strcmp(ovl_why, "CPU OVERLOAD: MODAL + P SD"), "CPU OVERLOAD: core 1's models named (1.0)");
        shot("cpu-overload");
        run_ms(250);
        shot("cpu-overload");
        cores_why = CW_BUILD;                        /* (one core: its biggest part) */
        memset(ld_q8, 0, sizeof ld_q8);
        ld_q8[LD_FX] = 140;
        ld_q8[LD_D13] = 90;
        audio_late++;
        run_ms(40);
        check(!strcmp(ovl_why, "CPU OVERLOAD: FX BUS"), "CPU OVERLOAD: core 0's biggest part named");
        run_ms(OVL_MS + 100u);
        check(!ovl_on, "CPU OVERLOAD: gone 2 s after the last late half");
        cores_why = cw;
        cpu_q8 = c0;
        memcpy(ld_q8, q, sizeof q);
        par_set(PAR_MODEL1 + 1, 0);
        par_set(PAR_MODEL1 + 2, 0);
        run_ms(20);
    }
    {                                                /* (1.0: GRAIN's knobs and TAPE's kept apart) */
        par_set(PAR_FX_DELAY_TYPE, DELAY_TAPEPP);
        kit_loads++;                                 /* (as after a kit load: nothing kept yet) */
        par_set(PAR_DLY_WOW, 40);
        par_set(PAR_DSAT, 30);
        par_set(PAR_GLITCH, 20);
        par_set(PAR_DPITCH, DP_UP7);
        ui.mode = M_FX;
        fx_goto(FXPG_DLY);
        turn(EN_K1, 1);
        check(par[PAR_FX_DELAY_TYPE] == DELAY_GRAIN && par[PAR_DLY_WOW] == 88u && par[PAR_DSAT] == 50u &&
              par[PAR_GLITCH] == 84u && par[PAR_DPITCH] == DP_OFF, "TAPE PP > GRAIN: GRAIN's starting cloud");
        par_set(PAR_GLITCH, 100);
        turn(EN_K1, -1);
        check(par[PAR_FX_DELAY_TYPE] == DELAY_TAPEPP && par[PAR_DLY_WOW] == 40u && par[PAR_DSAT] == 30u &&
              par[PAR_GLITCH] == 20u && par[PAR_DPITCH] == DP_UP7, ".. and back: TAPE PP's own numbers, not GRAIN's");
        turn(EN_K1, 1);
        check(par[PAR_GLITCH] == 100u, ".. GRAIN again: its own");
        kit_loads++;
        turn(EN_K1, -1);
        check(par[PAR_DLY_WOW] == 0u && par[PAR_DSAT] == 0u && par[PAR_GLITCH] == 0u && par[PAR_DPITCH] == DP_OFF,
              "GRAIN > TAPE PP after a kit load: the tape's zeros, not GRAIN's numbers");
    }
    {                                                /* (GRAIN's cloud, moving: frames apart) */
        uint32_t i;
        uint32_t f0;
        par_set(PAR_FX_DELAY_TYPE, DELAY_GRAIN);
        par_set(PAR_DLY_TIME, 70);
        par_set(PAR_DLY_WOW, 70);
        par_set(PAR_GLITCH, 90);
        par_set(PAR_DSAT, 80);
        par_set(PAR_DLY_FB, 60);
        par_set(PAR_DPITCH, DP_UP12);
        ui.mode = M_FX;
        fx_goto(FXPG_DLY);
        ui.force = 1;
        run_ms(100);
        f0 = vz_frame;
        for (i = 0; i < 4u; i++) {
            run_ms(150);
            shot("grain-anim");
        }
        check(vz_frame >= f0 + 10u, "GRAIN: its cloud moves on its own (1.0)");
    }
    {                                                /* (the other delays' echoes, as simply, moving too) */
        static const uint8_t M[3] = {DELAY_NORMAL, DELAY_PINGPONG, DELAY_TAPEPP};
        uint32_t i, m, f0;
        par_set(PAR_DLY_TIME, 50);
        par_set(PAR_DLY_FB, 90);
        par_set(PAR_DLY_LEVEL, 110);
        par_set(PAR_DLY_TONE, 80);
        for (m = 0; m < 3u; m++) {
            par_set(PAR_FX_DELAY_TYPE, M[m]);
            fx_goto(FXPG_DLY);
            ui.force = 1;
            run_ms(100);
            f0 = vz_frame;
            for (i = 0; i < 2u; i++) {
                run_ms(170);
                shot("echo-anim");
            }
            check(vz_frame >= f0 + 6u, "the delay's echoes move on their own (1.0)");
        }
        fx_goto(FXPG_DUB);                           /* (TAPE PP's WOW, SATURATE, GLITCH, PITCH) */
        ui.force = 1;
        run_ms(170);
        shot("echo-anim-tape");
        par_set(PAR_DPITCH, DP_OFF);
        par_set(PAR_GLITCH, 0);
        par_set(PAR_FX_DELAY_TYPE, DELAY_OFF);
    }
}
/* 1.1: the LFO's SMOOTH (RANDOM's draws, glided) and DRIFT (a random walk, glided) */
static void lfo_smooth_drift(void)
{
    lfo_t l;
    uint32_t i, w, n, ok = 1, wraps = 0;
    int32_t v, last, big[3] = {0}, tmax = 0;
    for (w = 0; w < 3u; w++) {                       /* RANDOM, SMOOTH, DRIFT: the largest move a block */
        memset(&l, 0, sizeof l);
        l.wave = (uint8_t)(w == 0u ? LFO_NOISE : w == 1u ? LFO_SMOOTH : LFO_DRIFT);
        l.inc = 0xFFFFFFFFu / 200u;                  /* (200 blocks a cycle) */
        last = lfo_calc(&l);
        for (i = 0; i < 200u * 40u; i++) {
            uint32_t pr = l.rnd;
            v = lfo_calc(&l);
            ok &= v >= 0 && v <= 65536;
            if (i > 400u)
                big[w] = iabs(v - last) > big[w] ? iabs(v - last) : big[w];
            if (w == 2u && (uint32_t)l.rnd != pr) {
                tmax = iabs(l.rnd - (int32_t)pr) > tmax ? iabs(l.rnd - (int32_t)pr) : tmax;
                wraps++;
            }
            last = v;
        }
    }
    printf("  the largest move a block, 200 blocks a cycle: RANDOM %d, SMOOTH %d, DRIFT %d; DRIFT's largest step %d\n",
           big[0], big[1], big[2], tmax);
    check(ok, "SMOOTH, DRIFT: 0..1");
    check(big[0] > 20000 && big[1] < 1200 && big[2] < 400, "SMOOTH, DRIFT: no steps (RANDOM's are)");
    check(wraps >= 39u && tmax <= 16384, "DRIFT: a new value each cycle, a quarter at most from the last");
    /* a hit (TRIG): a new glide from where it is, no jump */
    memset(&l, 0, sizeof l);
    l.wave = LFO_SMOOTH;
    l.inc = 0xFFFFFFFFu / 200u;
    for (i = 0; i < 333u; i++)
        last = lfo_calc(&l);
    n = 0;
    for (i = 0; i < 20u; i++) {
        int32_t was = l.rnd;
        lfo_hit(&l);
        v = lfo_calc(&l);
        n += iabs(v - last) < 1200 && l.rnd != was;
        last = v;
        for (w = 0; w < 37u; w++)
            last = lfo_calc(&l);
    }
    check(n == 20u, "SMOOTH: a hit starts a new glide from where it is");
    /* the names fit the knob's card; the global LFOs' WAVE through a project, FOLLOW still 8 */
    check(!strcmp(L_LFOW[LFO_SMOOTH], "SMOOTH") && !strcmp(L_LFOW[LFO_DRIFT], "DRIFT") &&
          text_w(&AF_S, "SMOOTH") <= 52, "Wave: SMOOTH, DRIFT, on the card");
    {
        uint8_t keep[4];
        int rc;
        for (i = 0; i < 4u; i++)
            keep[i] = gls[i].wave;
        gls[0].wave = LFO_SMOOTH; gls[1].wave = GW_FOLLOW; gls[2].wave = LFO_DRIFT; gls[3].wave = LFO_EXP_DOWN;
        rc = proj_save(1, "LFOW    ");
        for (i = 0; i < 4u; i++)
            gls[i].wave = LFO_SINE;
        rc |= proj_load(1);
        check(rc == 0 && gls[0].wave == LFO_SMOOTH && gls[1].wave == GW_FOLLOW && gls[2].wave == LFO_DRIFT &&
              gls[3].wave == LFO_EXP_DOWN, "project: the global LFOs' SMOOTH, DRIFT, FOLLOW, EXP DN");
        for (i = 0; i < 4u; i++)
            gls[i].wave = keep[i];
    }
    /* the LFO page's picture */
    p4_reset();
    black(0);
    press(B_LFO);
    vp_set(PAR_WAVE_LFO1, LFO_SMOOTH);
    vp_set(PAR_SYNC_LFO1, 6);                        /* (1/4: four cycles a bar) */
    vp_set(PAR_AMOUNT_LFO1, 127);
    run_ms(100);
    shot("lfo-smooth");
    vp_set(PAR_WAVE_LFO1, LFO_DRIFT);
    vp_set(PAR_SYNC_LFO1, 10);                       /* (1/16) */
    run_ms(100);
    shot("lfo-drift");
    vp_set(PAR_WAVE_LFO1, LFO_SINE);
}

int main(int argc, char **argv)
{
    uint32_t t;
    setvbuf(stdout, NULL, _IOLBF, 0);                /* (a line at a time: a hang shows where) */
    if (argc > 1)
        shots = argv[1];
    web_nor_erase();
    web_boot();
    check(kset.gtakes == GT_KEYS && midi_gtakes == GT_KEYS, "0.24: Global in starts at KEYS");
    midi_gtakes = GT_ALL;                            /* (the tests below send the LXR's CCs there: phase_024g) */
    run_ms(200);
    shot("splash");
    check(text_w(&AF_M, SPLASH_LINE) <= 232 && text_w(&AF_S, "free and unofficial, " ELIXIR_VERSION) <= 232,
          "splash: its lines fit the screen");
    run_ms(1400);
    shot("home");
    /* program a beat on the grid: kick 1 5 9 13 */
    for (t = 0; t < 16; t += 4)
        white(t);
    check(main_on(&pat[0], 0, 0) && main_on(&pat[0], 0, 12) && !main_on(&pat[0], 0, 1), "steps toggled on D1");
    black(3);                                         /* snare */
    white(4);
    white(12);
    black(5);                                         /* closed hat */
    for (t = 0; t < 16; t += 2)
        white(t);
    check(ui.track == 5, "the voice key selects the track");
    shot("beat");
    press(B_PLAY);
    run_ms(1000);
    check(sq.running, "PLAY starts");
    shot("playing");
    /* the sound pages */
    black(0);
    press(B_EDIT);
    check(ui.mode == M_SOUND && ui.sect == S_MODEL, "EDIT: the MODEL page first (29)");
    shot("model");
    press(B_EDIT);
    check(ui.mode == M_SOUND && ui.sect == S_OSC, "EDIT again: the oscillator page");
    shot("osc");
    turn(EN_K1, -5);                                  /* coarse down */
    check(par[PAR_COARSE1] != INIT_KIT[8 + PAR_COARSE1], "K1 moves the coarse tune");
    shot("osc-turned");
    press(B_ENV);
    shot("aeg");
    press(B_ENV);
    shot("mod");
    press(B_SCL);
    shot("filter");
    press(B_LFO);
    shot("lfo");
    turn(EN_SELECT, 1);
    shot("lfo-2");
    press(B_GLO);
    check(ui.mode == M_MIXER, "GLO: the mixer (29: no MIX pages)");
    shot("mixer");
    press(B_GLO);
    shot("config");
    press(B_FX);
    fx_goto(FXPG_BUS);
    turn(EN_K1, 3);                                   /* (past DELAY and DRIVE, not types now: PHASER) */
    check(par[PAR_FX_TYPE] == FX_PHASER, "FX type: 3 detents from OFF to PHASER, past DRIVE and DELAY");
    shot("fx");
    turn(EN_SELECT, 1);
    shot("fx-2");
    /* step edit */
    press(B_HOME);
    press(B_SEQ);
    check(ui.mode == M_EUCLID && ui.page == PG_TRACK, "SEQ: the TRACK page");
    shot("track");
    press(B_SEQ);
    check(ui.mode == M_STEP, "SEQ again: the steps");
    white(4);
    turn(EN_K3, -30);
    check((pat[0].step[0][4].volume & 127u) < 127u, "STEP: K3 sets the velocity");
    shot("step");
    turn(EN_SELECT, 1);
    turn(EN_K1, 2);
    shot("step-timing");
    press(B_SEQ);
    /* a lock: hold a step, turn a knob on the filter page */
    press(B_SCL);
    keys |= 1u << WHITE_KEY[8];
    web_keys(keys);
    run_ms(40);
    turn(EN_K1, -20);
    shot("lock");
    keys &= ~(1u << WHITE_KEY[8]);
    web_keys(keys);
    run_ms(40);
    check(pat[0].step[0][8].p1n != NO_AUTOMATION || pat[0].step[0][8].p2n != NO_AUTOMATION, "the step holds a lock");
    /* SHIFT: the legend */
    hold(B_OCTDN);
    shot("shift");
    release(B_OCTDN);
    /* patterns, song */
    press(B_HOME);
    press(B_HOME);
    shot("patterns");
    white(1);
    run_ms(2500);
    check(sq.active == 1, "the queued pattern plays after this one");
    press(B_HOME);
    shot("song");
    press(B_HOME);
    /* perform */
    press(B_ARP);
    shot("perf");
    press(B_ARP);
    shot("global-mod");
    seq_goto(PG_GEN);
    turn(EN_K2, 3);
    shot("euclid");
    /* save the kit, load page, kit browser */
    press(B_SAVE);
    shot("save");
    press(B_OCTUP);
    shot("save-name");
    press(B_OCTUP);
    press(B_SAVE);
    shot("load");
    turn(EN_PRESET, 2);
    shot("kits");
    press(B_OCTDN);
    /* CLEAR */
    press(B_HOME);
    hold(B_OCTDN);
    press(B_OCTUP);
    shot("clear");
    release(B_OCTDN);
    press(B_PLAY);
    check(!sq.running, "PLAY stops");
    /* MIDI sync: BPM below its minimum is MIDI; 140 BPM of clock from the page, START, then STOP */
    {
        uint32_t i;
        par_set(PAR_BPM, 1);
        run_ms(40);
        check(sq.sync_ext, "BPM at MIDI: external sync");
        web_midi(0xFA, 0, 0);
        for (i = 0; i < 24u * 4u; i++) {          /* 4 beats at 140 BPM: a pulse every 60000/140/24 ms */
            web_midi(0xF8, 0, 0);
            run_ms(i % 7u == 6u ? 17u : 18u);       /* 17.857 ms */
        }
        check(sq.running, "MIDI START starts");
        printf("  ext tempo %u.%u BPM\n", sq.bpm_ext_x10 / 10u, sq.bpm_ext_x10 % 10u);
        check(sq.bpm_ext_x10 > 1380u && sq.bpm_ext_x10 < 1420u, "the tempo follows the clock in");
        web_midi(0xFC, 0, 0);
        run_ms(100);
        check(!sq.running, "MIDI STOP stops");
        par_set(PAR_BPM, 120);
        run_ms(40);
    }
    /* a project into the flash and back */
    {
        uint8_t keep = pat[0].main[0][0] & 0xFFu;
        int rc = proj_save(0, "TEST    ");
        check(rc == 0, "project save");
        seq_clear_pattern(&pat[0]);
        rc = proj_load(0);
        check(rc == 0 && (pat[0].main[0][0] & 0xFFu) == keep, "project load restores the pattern");
    }
    /* ---------------------------------------------------- Phase 1 (0.2) --- */
    /* 32 patterns: two banks; program change past them ignored */
    check(NUM_PATTERN == 32, "32 patterns");
    goto_mode(B_HOME, M_PATTERN);
    black(NTRACK + 3);                                /* bar key 4: no bank 4 */
    check(ui.bank < 2, "the pattern page has 2 banks");
    black(NTRACK + 1);
    check(ui.bank == 1, "bar key 2: bank 2");
    shot("patterns-32");
    web_midi(0xC0 | ((par[PAR_MIDI_CHAN_GLOBAL] - 1u) & 15u), 40, 0);
    run_ms(40);
    check(sq.pending < NUM_PATTERN, "program change 40 is ignored");
    press(B_HOME);
    /* the lock numbers of Elixir's parameters go both ways */
    {
        uint32_t p, ok = 1;
        for (p = NUM_PARAMS; p < PAR_REV_TYPE; p++)
            ok &= autodest_to_par(par_to_autodest(p)) == p && par_to_autodest(p) != NO_AUTOMATION;
        for (p = 1; p < END_OF_SOUND_PARAMETERS; p++)
            if (par_lockable(p) && p != 127u)          /* (127: the LXR's own clash, 126 and 127 both 127) */
                ok &= autodest_to_par(par_to_autodest(p)) == p;
        check(ok && !par_lockable(PAR_REV_TYPE), "lock numbers: the LXR's and Elixir's");
    }
    /* Out = FX BUS from MIDI (NRPN 215 - 128): the voice's FX send at 127, its output STEREO */
    {
        uint32_t ch = (par[PAR_MIDI_CHAN_GLOBAL] - 1u) & 15u, n = PAR_AUDIO_OUT2 - 128u;
        web_midi(0xB0 | ch, 99, n >> 7);
        web_midi(0xB0 | ch, 98, n & 127u);
        web_midi(0xB0 | ch, 6, 6);
        run_ms(40);
        check(par[PAR_AUDIO_OUT2] == 0 && par[PAR_FX_SEND1 + 1] == 127 && vc[1].fx_send >= 32767,
              "MIDI Out = FX BUS: FX send 127");
        n = PAR_REV_SEND1 + 2u - 128u;                 /* NRPN reaches Elixir's own */
        web_midi(0xB0 | ch, 99, n >> 7);
        web_midi(0xB0 | ch, 98, n & 127u);
        web_midi(0xB0 | ch, 6, 90);
        run_ms(40);
        check(par[PAR_REV_SEND1 + 2] == 90, "NRPN sets a reverb send");
    }
    /* the sends: FX send 127 is the old route through the bus: same output */
    {
        static int32_t a[64 * 200], b[64 * 200];
        uint32_t i, k, same = 1;
        int32_t *o[2] = {a, b};
        for (k = 0; k < 2; k++) {
            memset(&fx, 0, sizeof fx);                 /* (both runs from the same state) */
            memset(vc, 0, sizeof vc);
            noise_rng = 0x9E3779B9u;
            voices_init();
            fxbus_init();
            reverb_init();
            kit_init_par();
            par[PAR_FX_TYPE] = FX_RING;               /* (Phase 10f: DRIVE set aside) */
            par[PAR_FX_DRY_WET] = 60;
            par[PAR_FX_P1] = 90;
            if (k == 0)
                par[PAR_AUDIO_OUT1] = 6;               /* the LXR's way */
            else
                par[PAR_FX_SEND1] = 127;               /* Elixir's */
            snd_post_all();
            snd_service();
            voice_trigger(0, 120, SEQ_DEFAULT_NOTE);
            for (i = 0; i < 200u; i++) {
                voices_block();
                memcpy(o[k] + i * 64u, mix_l, sizeof mix_l);
            }
        }
        for (i = 0; i < 64u * 200u; i++)
            same &= a[i] == b[i];
        check(same, "FX send 127 sounds as Out = FX BUS");
    }
    /* the reverb: each model rings after the voice, then dies away, and sleeps */
    {
        uint32_t m, i;
        for (m = 0; m <= NREV; m++) {
            double e1 = 0, e2 = 0, dry = 0;
            voices_init();
            fxbus_init();
            reverb_init();
            kit_init_par();
            par[PAR_VELOD1] = 20;                      /* a short kick */
            par[PAR_REV_TYPE] = (uint8_t)(m < NREV ? m : 0u);
            par[PAR_REV_SIZE] = 90;
            par[PAR_REV_SEND1] = m < NREV ? 127 : 0;   /* (the last: no send, no tail) */
            snd_post_all();
            snd_service();
            voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
            for (i = 0; i < 2400u; i++) {              /* 3.5 s */
                voices_block();
                for (uint32_t j = 0; j < 64u; j++) {
                    double x = mix_l[j] + mix_r[j];
                    if (i < 40u)
                        dry += x * x;
                    else if (i >= 200u && i < 400u)
                        e1 += x * x;
                    else if (i >= 2200u)
                        e2 += x * x;
                }
            }
            printf("  reverb %-6s tail %.1f dB, at 3.2 s %.1f dB (below the hit)\n",
                   m < NREV ? L_REVMODEL[m] : "none", 10 * log10((e1 + 1) / (dry + 1)), 10 * log10((e2 + 1) / (dry + 1)));
            if (m < NREV)
                check(e1 > dry * 1e-4 && e2 < e1 * 0.1, "the reverb rings and dies away");
            else
                check(e1 == 0 && rv.idle == 255, "no send: no tail, the reverb asleep");
        }
        reverb_init();
        voices_init();
        kit_load_ram(kit_loaded);
        snd_service();
    }
    /* the mixer's send pages and the reverb page; a send locked on a step */
    goto_mode(B_HOME, M_HOME);
    black(0);
    goto_mode(B_GLO, M_MIXER);
    turn(EN_SELECT, 4);
    shot("mixer-fx-send");
    turn(EN_K1, 40);
    check(par[PAR_FX_SEND1] >= 40, "the mixer's K1: D1's FX send");
    turn(EN_SELECT, 4);                             /* (1.0 Phase 16: past DLY SEND's two) */
    turn(EN_K1, 30);
    check(par[PAR_REV_SEND1] >= 30, "the mixer's K1: D1's reverb send");
    keys |= 1u << WHITE_KEY[2];                       /* a lock on step 3: D1's reverb send */
    web_keys(keys);
    run_ms(40);
    turn(EN_K1, 20);
    shot("mixer-rev-send-lock");
    keys &= ~(1u << WHITE_KEY[2]);
    web_keys(keys);
    run_ms(40);
    {
        const step_t *st = &pat[sq.active].step[0][2];
        (void)st;
        check(autodest_to_par(lock_get(&pat[sq.active], 0, 2, 0)) == PAR_REV_SEND1 ||
              autodest_to_par(lock_get(&pat[sq.active], 0, 2, 1)) == PAR_REV_SEND1, "a step locks a reverb send");
    }
    press(B_SEQ);
    white(2);
    turn(EN_SELECT, 2);
    shot("step-locks-send");
    press(B_SEQ);
    press(B_FX);
    fx_goto(FXPG_REV);
    turn(EN_K1, 2);
    check(par[PAR_REV_TYPE] == 2 && rv.type == 2, "the reverb page: HALL");
    run_ms(100);
    shot("reverb");
    goto_mode(B_GLO, M_CONFIG);
    shot("config-cpu");
    {   /* 0.5.1: the battery at the CPU line's end: USB in the page (it is the USB host), else its bars */
        static const int32_t RAW[4] = {500, 540, 570, 620};
        static const char *NAME[4] = {"config-batt-empty", "config-batt-1", "config-batt-2", "config-batt-full"};
        uint32_t j;
        check(batt_level() == 4, "the battery: USB power in the page");
        usb.config = 0;
        for (j = 0; j < 4u; j++) {
            batt_raw = RAW[j];
            check(batt_level() == (int32_t)j, "the battery: its level from the reading");
            run_ms(100);
            if (j == 0u || j == 2u)
                shot(NAME[j]);
        }
        usb.config = 1;
        run_ms(100);
    }
    /* projects: version 2 keeps Elixir's parameters and the locks */
    {
        int rc;
        uint32_t send = par[PAR_REV_SEND1], ln = lock_get(&pat[sq.active], 0, 2, 0), ln2 = lock_get(&pat[sq.active], 0, 2, 1);
        kit_set1(7, kit_idx(PAR_REV_SIZE), 111);
        rc = proj_save(1, "V2      ");
        check(rc == 0, "project save (version 2)");
        par[PAR_REV_SEND1] = 0;
        kit_set1(7, kit_idx(PAR_REV_SIZE), 0);
        seq_clear_pattern(&pat[sq.active]);
        rc = proj_load(1);
        check(rc == 0 && par[PAR_REV_SEND1] == send && kit_at(7, kit_idx(PAR_REV_SIZE)) == 111 &&
              lock_get(&pat[sq.active], 0, 2, 0) == ln && lock_get(&pat[sq.active], 0, 2, 1) == ln2,
              "project load: sends, reverb, locks");
    }
    /* projects: a version-1 project (64 patterns, tests/data/proj_v1.bin, made by Elixir 0.1) loads */
    {
        FILE *f = fopen("tests/data/proj_v1.bin", "rb");
        uint8_t img[0x4000];
        size_t n = f ? fread(img, 1, sizeof img, f) : 0;
        int rc;
        if (f)
            fclose(f);
        uint32_t ph, l, used = 0;
        proj_hdr_t h;
        check(n > 256, "the version-1 project fixture");
        for (l = 0; l < PROJ_NLOG; l++)
            if (pj.phys[l] >= 0)
                used |= 1u << pj.phys[l];
        for (ph = 0; used & (1u << ph); ph++)          /* (a slot no project uses: there is always one) */
            ;
        memset(nor + PROJ_BASE + ph * PROJ_SLOT, 0xFF, PROJ_SLOT);
        memcpy(nor + PROJ_BASE + ph * PROJ_SLOT, img, n);   /* (its header says project 0) */
        check(hdr_read(ph, &h) == 0 && h.version == 1, "the fixture is a version-1 project");
        pj.phys[0] = (int8_t)ph;                       /* (project 0 as the fixture, whatever seq it has) */
        rc = proj_load(0);
        check(rc == 0, "version-1 project loads");
        check(par[PAR_BPM] == 133 && sq.active == 5 && main_on(&pat[5], 3, 4) && main_on(&pat[0], 0, 8) &&
              pat[0].step[0][8].volume == (0x80 | 64), "version 1: settings and patterns");
        check(songs[0][0] == 5 && songs[0][1] == NUM_PATTERN - 1u && songs[0][2] == 0 && songs[0][3] == SONG_LOOP,
              "version 1: a song's pattern 41 becomes 32");
        check(par[PAR_AUDIO_OUT2] == 0 && par[PAR_FX_SEND1 + 1] == 127 && kit_at(3, PAR_AUDIO_OUT4) == 0 &&
              kit_at(3, kit_idx(PAR_FX_SEND1 + 3)) == 127 && par[PAR_REV_LEVEL] == 100,
              "version 1: Out = FX BUS becomes FX send 127");
        proj_scan();
    }
    /* how big projects get. Busy: every pattern's 7 tracks 32 steps long, each step's velocity and note set, every
     * kit 40 parameters away from INIT. Then dense: 64 steps, every field of every step, every kit parameter */
    {
        uint32_t i, t, k, pass;
        for (pass = 0; pass < 2u; pass++) {
            int rc;
            uint32_t steps = pass ? 64u : 32u;
            for (i = 0; i < NUM_PATTERN; i++)
                for (t = 0; t < NTRACK; t++) {
                    pat[i].len[t] = (uint8_t)steps;
                    for (k = 0; k < 4u; k++)
                        pat[i].main[t][k] = 0x5555;
                    for (k = 0; k < steps; k++) {
                        step_t *st = &pat[i].step[t][k];
                        st->volume = (uint8_t)(0x80 | (k * 7u + t) % 127u);
                        st->note = (uint8_t)(40 + (k + i) % 30u);
                        if (pass) {                    /* + both locks, chance, timing */
                            st->p1n = 37;
                            st->p1v = (uint8_t)k;
                            st->p2n = 95;
                            st->p2v = (uint8_t)i;
                            st->prob = 50;
                            pat[i].timing[t][k] = 0x82;
                        }
                    }
                }
            for (i = 0; i < NUM_KITS; i++) {
                uint8_t kb[KIT_LEN];
                kit_get(i, kb);
                for (k = 1; k < KIT_LEN; k++)
                    if (pass || k % 6u == 0u)
                        kb[k] = (uint8_t)((k * 13u + i + 1u) & 127u);
                kit_put(i, kb);
            }
            rc = proj_save(2, "DENSE   ");
            printf("  %s project: %s, %u bytes (a slot holds %u)\n", pass ? "dense" : "busy",
                   rc == 0 ? "saved" : rc == -20 ? "PROJECT TOO BIG" : "failed", rc == 0 ? pj_len(2) : pw.off,
                   (unsigned)PROJ_PAYLOAD);
            check(pass ? rc == -20 : rc == 0, pass ? "a dense project: PROJECT TOO BIG" : "a busy project fits");
        }
    }
    /* the AUTO save of a session too big: reported once, not tried again until it changes */
    {
        uint32_t w;
        autosave_on = 1;
        pj.saved_sig = 0;
        for (w = 0; w < 200u && !pj.failed_sig; w++)
            run_ms(100);
        check(pj.failed_sig != 0, "autosave fails (too big)");
        run_ms(100);
        check(msg_on() && ui.msg[0] == 'A', "the screen says AUTOSAVE ...");
        shot("autosave-too-big");
        w = web_flash_writes;
        run_ms(20000);
        check(web_flash_writes == w, "a failed autosave is not tried again");
    }
    phase2();
    phase3();
    phase4();
    grids_test();
    hush_test();
    phase5a();
    phase5b();
    phase5c();
    phase5d();
    phase9_perf();
    phase39_rush();
    phase9_keys();
    phase9_library();
    phase9_patkeep();
    phase11();
    phase11_sound_solo();
    phase13_control_all();
    phase12_remote();
    phase14();
    phase19();
    phase20();
    phase21();
    phase22();
    phase23();
    phase_016t2();
    phase_016t3();
    phase24();
    phase25_ensemble();
    phase25a();
    phase25b();
    phase27a();
    {   /* 25-0: the packed kit slots: the widths fit; a kit of each value's largest, and the init kit, back as put */
        uint8_t a[KIT_LEN], b[KIT_LEN];
        uint32_t i, ok = 1;
        check(kit_pk_init() == 0, "the packed kit fits its slot");
        printf("  kit slots: %u bits a kit, %u bytes (KIT_PK %u, KIT_LEN %u)\n", kit_pk_bits, (kit_pk_bits + 7u) / 8u,
               KIT_PK, KIT_LEN);
        kit_get(9, b);
        for (i = 0; i < KIT_LEN; i++) {
            uint32_t p = kit_par(i);
            a[i] = (uint8_t)(p < NUM_PARAMS ? par_max(p) : par_max_k(p));
        }
        kit_put(9, a);
        kit_get(9, a + 0);
        for (i = 0; i < KIT_LEN; i++) {
            uint32_t p = kit_par(i);
            ok &= a[i] == (uint8_t)(p < NUM_PARAMS ? par_max(p) : par_max_k(p));
        }
        kit_init_values(a);
        kit_put(9, a);
        kit_get(9, a);
        {
            uint8_t c[KIT_LEN];
            kit_init_values(c);
            ok &= memcmp(a, c, KIT_LEN) == 0;
        }
        kit_put(9, b);
        check(ok, "kit slots: each value's largest and the init kit back as put");
    }
    phase26();
    phase28();
    phase29();
    phase31();
    phase32();
    phase_t11();
    phase30();
    gen_step_edit_test();
    phase37();
    phase40();
    phase41();
    phase42();
    phase_t7();
    phase_023t11();
    phase_024();
    phase_024g();
    phase_0241();
    phase_100_2();
    phase_100_3();
    phase_100_4();
    phase_100_5();
    phase_100_6();
    phase_100_fx2();
    phase_100_6b();
    phase_100_10();
    phase_100_10c();
    phase_100_11();
    phase_100_12();
    phase_100_13();
    phase_100_16();
    phase_100_17();
    phase_100_18();
    lfo_smooth_drift();
    copy_pass();                                     /* (1.0's copy and picture pass: last, as it runs time) */
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: ok (%d shots)\n", fails ? fails : shot_n);
    return fails != 0;
}


