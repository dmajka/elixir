/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host test: the browser build (web/emu/kettle_web.c) natively: boot, play with the panel as a user would,
 * check what the sequencer and the sound did, and write the screen of each step to build/shots/NN-name.ppm.
 *   cc -O1 -I build/gen -I firmware/src tests/ui_test.c -o build/ui_test && build/ui_test build/shots */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../web/emu/kettle_web.c"

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
static void goto_mode(uint32_t b, uint32_t m)        /* press b until the mode is m */
{
    uint32_t i;
    for (i = 0; i < 6u && ui.mode != m; i++)
        press(b);
    check(ui.mode == m, "the mode a button cycles to");
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
    /* a version-2 lock (Kettle's parameters at the LXR's free numbers) moves to its own number */
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
    for (i = 0; i < NUM_KITS; i++)
        kit_init_values(kits[i]);
    pat_backup = pat[sq.active];
    eng_reset(0);
    goto_mode(B_HOME, M_HOME);
    black(0);
    press(B_SCL);
    press(B_SCL);
    check(ui.mode == M_SOUND && ui.sect == S_SHAPE, "SCL twice: SHAPE");
    turn(EN_K1, 1);
    turn(EN_K3, 4);
    check(par[PAR_SHAPE1] == SH_FOLD && par[PAR_BITS1] == 4, "SHAPE: K1 the shaper, K3 the bits");
    shot("shape");
    keys |= 1u << WHITE_KEY[6];                       /* a lock on step 7: D1's tilt */
    web_keys(keys);
    run_ms(40);
    turn(EN_K4, 20);
    keys &= ~(1u << WHITE_KEY[6]);
    web_keys(keys);
    run_ms(40);
    check(autodest_to_par(lock_get(&pat[sq.active], 0, 6, 0)) == PAR_TILT1 ||
          autodest_to_par(lock_get(&pat[sq.active], 0, 6, 1)) == PAR_TILT1, "a step locks the tilt");
    press(B_LFO);                                     /* LFO, its second half, then RANDOM */
    press(B_LFO);
    press(B_LFO);
    check(ui.sect == S_RANDOM, "LFO three times: RANDOM");
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
    check(ui.sect == S_LFO, "LFO again: back to the LFO page");
    press(B_SEQ);
    black(0);
    white(6);
    turn(EN_SELECT, 2);
    shot("step-locks-tilt");
    press(B_SEQ);
    press(B_FX);
    turn(EN_K1, -10);
    turn(EN_K1, 5);
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
    /* FREEZE: the delay keeps what it holds; THROW REV: the throw voice's reverb send at 127 while held */
    {
        double fr, nf;
        eng_reset(0);
        kit_init_par();
        par_set(PAR_FX_TYPE, FX_DELAY);
        par_set(PAR_FX_DRY_WET, 60);
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
    turn(EN_SELECT, 2);
    check(ui.page == 2, "PERF: MACRO SETUP");
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
    press(B_ARP);
    press(B_ARP);
    check(ui.mode == M_EUCLID, "ARP twice: EUCLID");
    turn(EN_SELECT, 1);
    turn(EN_K1, 1);
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
        turn(EN_SELECT, -1);
        kset.accel = 0;
        turn(EN_K4, ACT_COMMIT);
        kset.accel = 1;
        press(B_OCTUP);
        check(p->main[0][0] == live, "COMMIT: the live track into the pattern");
        turn(EN_K4, ACT_UNDO - ACT_NONE);
        press(B_OCTUP);
        check(p->main[0][0] == 0x1111u, "UNDO: the pattern as it was before COMMIT");
        /* DICE: Euclid's hits anywhere over its length; UNDO */
        eu_len[0] = 16;
        eu_steps[0] = 5;
        turn(EN_K4, ACT_DICE);
        press(B_OCTUP);
        n = (uint32_t)__builtin_popcount(p->main[0][0]);
        check(n == 5, "DICE: 5 hits");
        shot("diced");
        turn(EN_K4, ACT_UNDO);
        press(B_OCTUP);
        check(p->main[0][0] == 0x1111u, "UNDO after DICE");
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
    /* the track clock page; the step pages: condition, ratchet */
    turn(EN_SELECT, 2);
    turn(EN_K2, 3);
    check(p->clk[0] == CLK_1X + 3, "TRACK CLOCK: x2");
    shot("track-clock");
    p->clk[0] = CLK_1X;
    p->gen[0][0] = GEN_OFF;
    press(B_SEQ);
    white(0);
    kset.accel = 0;
    turn(EN_K4, 9 + 1);
    kset.accel = 1;
    check(p->cond[0][0] == COND_AB + 1, "STEP: the condition (2:2)");
    shot("step-cond");
    turn(EN_SELECT, 3);
    turn(EN_K1, 2);
    turn(EN_K2, 1);
    turn(EN_K3, 2);
    check((p->rat[0][0] & 7u) == 2 && ((p->rat[0][0] >> 3) & 3u) == 1 && (p->rat[0][0] >> 5) == RF_UP,
          "RATCHET: 3 hits, 1/48, up");
    shot("step-ratchet");
    press(B_SEQ);
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
    goto_mode(B_ARP, M_PERF);
    press(B_ARP);
    turn(EN_SELECT, 1);
    shot("grids-generator");
    turn(EN_SELECT, 2);
    kset.accel = 0;
    turn(EN_K1, -20);
    kset.accel = 1;
    check(p->gx == 107, "GRIDS MAP: K1 moves X");
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

int main(int argc, char **argv)
{
    uint32_t t;
    if (argc > 1)
        shots = argv[1];
    web_nor_erase();
    web_boot();
    run_ms(200);
    shot("splash");
    run_ms(800);
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
    check(ui.mode == M_SOUND && ui.sect == S_OSC, "EDIT: the oscillator page");
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
    shot("mix");
    press(B_GLO);
    press(B_GLO);
    shot("mixer");
    press(B_GLO);
    shot("config");
    press(B_FX);
    turn(EN_K1, 4);                                   /* DELAY */
    check(par[PAR_FX_TYPE] == FX_DELAY, "FX type to DELAY");
    shot("fx");
    turn(EN_SELECT, 1);
    shot("fx-2");
    /* step edit */
    press(B_HOME);
    press(B_SEQ);
    white(4);
    turn(EN_K1, -30);
    check((pat[0].step[0][4].volume & 127u) < 127u, "STEP: K1 sets the velocity");
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
    /* the lock numbers of Kettle's parameters go both ways */
    {
        uint32_t p, ok = 1;
        for (p = NUM_PARAMS; p < PAR_REV_TYPE; p++)
            ok &= autodest_to_par(par_to_autodest(p)) == p && par_to_autodest(p) != NO_AUTOMATION;
        for (p = 1; p < END_OF_SOUND_PARAMETERS; p++)
            if (par_lockable(p) && p != 127u)          /* (127: the LXR's own clash, 126 and 127 both 127) */
                ok &= autodest_to_par(par_to_autodest(p)) == p;
        check(ok && !par_lockable(PAR_REV_TYPE), "lock numbers: the LXR's and Kettle's");
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
        n = PAR_REV_SEND1 + 2u - 128u;                 /* NRPN reaches Kettle's own */
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
            par[PAR_FX_TYPE] = FX_DRIVE;
            par[PAR_FX_DRY_WET] = 60;
            par[PAR_FX_P1] = 90;
            if (k == 0)
                par[PAR_AUDIO_OUT1] = 6;               /* the LXR's way */
            else
                par[PAR_FX_SEND1] = 127;               /* Kettle's */
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
        for (m = 0; m < 4u; m++) {
            double e1 = 0, e2 = 0, dry = 0;
            voices_init();
            fxbus_init();
            reverb_init();
            kit_init_par();
            par[PAR_VELOD1] = 20;                      /* a short kick */
            par[PAR_REV_TYPE] = (uint8_t)(m < 3u ? m : 0u);
            par[PAR_REV_SIZE] = 90;
            par[PAR_REV_SEND1] = m < 3u ? 127 : 0;     /* (4th: no send, no tail) */
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
                   m < 3u ? L_REVMODEL[m] : "none", 10 * log10((e1 + 1) / (dry + 1)), 10 * log10((e2 + 1) / (dry + 1)));
            if (m < 3u)
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
    turn(EN_SELECT, 2);
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
    turn(EN_SELECT, 2);
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
    /* projects: version 2 keeps Kettle's parameters and the locks */
    {
        int rc;
        uint32_t send = par[PAR_REV_SEND1], ln = lock_get(&pat[sq.active], 0, 2, 0), ln2 = lock_get(&pat[sq.active], 0, 2, 1);
        kits[7][kit_idx(PAR_REV_SIZE)] = 111;
        rc = proj_save(1, "V2      ");
        check(rc == 0, "project save (version 2)");
        par[PAR_REV_SEND1] = 0;
        kits[7][kit_idx(PAR_REV_SIZE)] = 0;
        seq_clear_pattern(&pat[sq.active]);
        rc = proj_load(1);
        check(rc == 0 && par[PAR_REV_SEND1] == send && kits[7][kit_idx(PAR_REV_SIZE)] == 111 &&
              lock_get(&pat[sq.active], 0, 2, 0) == ln && lock_get(&pat[sq.active], 0, 2, 1) == ln2,
              "project load: sends, reverb, locks");
    }
    /* projects: a version-1 project (64 patterns, tests/data/proj_v1.bin, made by Kettle 0.1) loads */
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
        check(par[PAR_AUDIO_OUT2] == 0 && par[PAR_FX_SEND1 + 1] == 127 && kits[3][PAR_AUDIO_OUT4] == 0 &&
              kits[3][kit_idx(PAR_FX_SEND1 + 3)] == 127 && par[PAR_REV_LEVEL] == 100,
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
            for (i = 0; i < NUM_KITS; i++)
                for (k = 1; k < KIT_LEN; k++)
                    if (pass || k % 6u == 0u)
                        kits[i][k] = (uint8_t)((k * 13u + i + 1u) & 127u);
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
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: ok (%d shots)\n", fails ? fails : shot_n);
    return fails != 0;
}
