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
    turn(EN_SELECT, 5);
    check(ui.page == 5, "PERF: MACRO SETUP");
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
    check(ui.mode == M_EUCLID && ui.page == PG_GEN, "ARP twice: GENERATOR");
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
    turn(EN_SELECT, 2);
    turn(EN_K2, 3);
    check(p->clk[0] == CLK_1X + 3, "TRACK: clock x2");
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
    check(ui.mode == M_STEP && ui.page == 0, "SEQ again from RATCHET: STEP");
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
    press(B_SEQ);
    white(0);
    turn(EN_SELECT, 3);
    turn(EN_K4, 1);
    turn(EN_K1, 5);
    check((p->rat[0][0] & RAT_BOUNCE) && (p->rat[0][0] & 7u) == 5u, "RATCHET page: BOUNCE, 8 hits");
    shot("step-bounce");
    press(B_SEQ);
    goto_mode(B_ARP, M_EUCLID);
    turn(EN_SELECT, 3);
    turn(EN_K1, 1);
    turn(EN_K2, -5);
    check((p->bnc[0] & 3u) == 2u && (p->bnc[0] >> 2) == 7u, "BOUNCE: from 1/16, -5 semitones");
    shot("track-clock-bounce");
    /* the roll sweep: a roll key held in PERF, KNOB 3 the roll's rate, faster as it turns */
    p->bnc[0] = BNC_DEFAULT;
    p->rat[0][0] = 0;
    pat_backup = *p;
    goto_mode(B_ARP, M_PERF);
    while (ui.page)
        turn(EN_SELECT, -1);
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
    check(ui.mode == M_EUCLID && ui.page == PG_LINKS, "SHIFT + SEQ: LINKS");
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
    while (ui.page)
        turn(EN_SELECT, -1);
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
    goto_mode(B_ARP, M_PERF);
    turn(EN_SELECT, (int32_t)PG_PERF2);
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
    mdl[1].type = (uint8_t)md;
    mdl[1].dec = (uint8_t)dec;
    mdl[1].damp = (uint8_t)bright;
    mdl[1].mat = (uint8_t)mat;
    mdl[1].pos = (uint8_t)pos;
    mdl[1].exc = (uint8_t)exc;
    v.osc.inc = (uint32_t)(hz / FS * 4294967296.0);
    model_reset(&mdl[1], 1);                         /* (as a block would, the model new: before the hit) */
    noise_rng = 0x9E3779B9u;                         /* (the plucker's noise: the same each run) */
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
    goto_mode(B_ARP, M_PERF);
    press(B_ARP);
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


/* ------------------------------------------------------- Phase 9 (0.8) --- */
/* 9b, 9g: PERF's pages (the performance ones, then the macros, then PERFORM 2), MIX, the global sample rate */
static void phase9_perf(void)
{
    static const char *const T[NPERF] = {"PERFORM", "PERFORM: MORE", "PERFORM: MIX", "PERFORM: MIX 2", "MACROS",
                                         "MACRO SETUP", "PERFORM 2", "PERFORM 2: SETUP", "PERFORM 2: SETUP 2"};
    pattern_t *p = &pat[0];
    uint16_t kp[4];
    uint32_t i, ok = 1, d0, d1;
    p4_reset();
    goto_mode(B_ARP, M_PERF);
    for (i = 0; i < NPERF; i++) {
        uint32_t pg, np;
        char b[40];
        title_text(b, sizeof b, &pg, &np);
        ok &= !strcmp(b, T[i]) && pg == i && (i >= PG_PERF2) == (uint32_t)perf_bank2();
        turn(EN_SELECT, 1);
    }
    check(ok, "PERF: its pages in order, PERFORM 2's bank last");
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
    /* MIX: the volumes on the knobs, the faders on the screen */
    turn(EN_SELECT, (int32_t)PG_MIX);
    page_params(kp);
    check(kp[0] == PAR_VOL1 && kp[3] == PAR_VOL4, "MIX: D1 D2 D3 SN");
    par_set(PAR_VOL1, 100);
    turn(EN_K1, -40);
    check(par[PAR_VOL1] == 60, "MIX: KNOB 1 rides D1's volume");
    shot("perf-mix");
    turn(EN_SELECT, 1);
    page_params(kp);
    check(kp[0] == PAR_VOL5 && kp[1] == PAR_VOL6 && kp[2] == PAR_FX_DRY_WET && kp[3] == PAR_REV_LEVEL,
          "MIX 2: CY HH, the FX bus, the reverb");
    white(0);                                        /* (the keys still roll there) */
    check(ui.mode == M_PERF, "MIX: the keys still PERF's");
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
    press(B_HOME);
    check(!ui.kplay, "HOME: KEYS let go");
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
    check(kp[2] == VP_IO_SRC && vp_max(VP_IO_SLOT) == KL_KITS - 1u, "SAVE KIT: To LIBRARY, 128 kits");
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
    /* 0.9.2: the kit browser (PRESETS): KNOB 1 lists the slots or the library's kits (the empty ones left out) */
    par[PAR_VOL2] = 10;
    turn(EN_PRESET, 1);
    check(ui.kb_open && !ui.kb_lib && ui.kb_sel == kit_loaded, "PRESETS: the kit browser, the slots, on the loaded kit");
    turn(EN_PRESET, 100);
    check(ui.kb_sel == NUM_KITS - 1u, "kit browser: the slots end at 64");
    {
        uint16_t kp[4];
        page_params(kp);
        check(kp[0] == VP_KB_SRC, "kit browser: KNOB 1 is From");
    }
    turn(EN_K1, 1);
    check(ui.kb_lib && ui.kb_sel == NUM_KITS + 99u, "kit browser: From LIBRARY, on library kit 100 (the only one)");
    shot("kits-library");
    turn(EN_PRESET, -5);
    check(ui.kb_sel == NUM_KITS + 99u, "kit browser: the library only, its empty slots left out");
    press(B_OCTUP);
    check(!ui.kb_open && par[PAR_VOL2] == 77 && ui.lib_kit == 99u, "kit browser: OCT+ loads the library kit");
    turn(EN_PRESET, 1);
    check(ui.kb_open && ui.kb_lib && ui.kb_sel == NUM_KITS + 99u, "kit browser: it opens on the library again");
    turn(EN_K1, -1);
    check(!ui.kb_lib && ui.kb_sel == kit_loaded, "kit browser: From SLOTS, on the loaded kit");
    press(B_OCTDN);
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
    turn(EN_PRESET, 1);
    turn(EN_K1, 1);
    check(ui.kb_open && ui.kb_lib && ui.kb_sel == NUM_KITS, "kit browser: From LIBRARY, the library empty");
    shot("kits-library-empty");
    press(B_OCTUP);
    check(!ui.kb_open, "kit browser: OCT+ on an empty library loads nothing");
    ui.kb_lib = 0;
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
    check(kp[2] == VP_IO_TRACK && vp_max(VP_IO_SLOT) == KL_SOUNDS - 1u, "SAVE SOUND: the track, 128 sounds");
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
    kits[2][kit_idx(PAR_OSC_WAVE_DRUM3)] = 4;      /* (slot 3's D3: something to find) */
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
    for (i = 0; i < sb_n && sb_list[i] != (SB_SLOT << 14 | 2u << 11 | 2u); i++)
        ;
    check(i < sb_n, "SOUND BROWSER: a slot kit's D3 for D2");
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
    kits[2][kit_idx(PAR_OSC_WAVE_DRUM3)] = INIT_KIT[8u + PAR_OSC_WAVE_DRUM3];
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
    /* 11a: SEQ again steps through the step pages, as every button does, and stays in step edit */
    press(B_LFO);
    press(B_SEQ);
    check(ui.mode == M_STEP && ui.page == 0, "SEQ: step edit, STEP");
    for (i = 1; i < 5u; i++) {
        press(B_SEQ);
        check(ui.mode == M_STEP && ui.page == i % 4u, "SEQ again: the next step page (round to STEP)");
    }
    /* 11b, 11c: one GENERATOR page; EUCLID from 0 hits over the track's length, nothing written until the hits */
    for (i = 0; i < 16u; i += 2)
        main_set(p, 0, i, 1);
    p->len[0] = 12;
    goto_mode(B_ARP, M_EUCLID);
    check(ui.page == PG_GEN, "ARP twice: the GENERATOR page first");
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
    goto_mode(B_ARP, M_EUCLID);
    turn(EN_SELECT, 2);
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
    turn(EN_K1, 5);
    for (v = 0, ok = 1; v < NVOICE; v++)
        ok &= par[PAR_FILTER_FREQ_1 + v] == (v == 4u ? 127u : 25u + 15u * v);
    check(ok, "CONTROL ALL: every voice's filter +5 from its own value, the cymbal's clamped at 127");
    check(ui.ca, "CONTROL ALL under way");
    shot("control-all");
    /* a voice key now selects, it doesn't copy */
    black(2);
    check(ui.track == 2 && !ui.copy_src, "CONTROL ALL: a voice key selects the track, no COPY");
    release(B_OCTUP);
    check(!ui.ca && ui.ca_ask && par[PAR_FILTER_FREQ_1] == 25u, "letting go of OCT+: the dialog, the changes still sounding");
    run_ms(50);
    shot("control-all-ask");
    turn(EN_K1, 3);
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
    turn(EN_K1, -10);
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
    press(B_EDIT);
    press(B_EDIT);
    check(ui.sect == S_FM, "EDIT twice: FM");
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
    /* a Kettle parameter (blocks of six): bits on every voice */
    press(B_SCL);
    press(B_SCL);
    check(ui.sect == S_SHAPE, "SCL again: SHAPE");
    hold(B_OCTUP);
    turn(EN_K3, 2);
    release(B_OCTUP);
    press(B_OCTDN);
    for (v = 0, ok = 1; v < NVOICE; v++)
        ok &= par[PAR_BITS1 + v] == 2u;
    check(ok, "CONTROL ALL on a Kettle parameter: every voice's bits");
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
    kit_set_morph(255);
    run_ms(20);
    vp_set(VP_CAKIT, 0);
    black(0);
    press(B_SCL);
    v = par_base(PAR_FILTER_FREQ_1 + 2);
    hold(B_OCTUP);
    turn(EN_K1, 6);
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
    turn(EN_K1, 4);
    release(B_OCTUP);
    press(B_OCTDN);
    check(par_base(PAR_FILTER_FREQ_1 + 2) == v + 4u, "half morph, BOTH KITS: heard 4 higher");
    kit_set_morph(255);
    vp_set(VP_CAKIT, 1);
    run_ms(20);
    v = par_base(PAR_FILTER_FREQ_1 + 2);
    hold(B_OCTUP);
    turn(EN_K1, 6);
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

/* 0.12 (Phase 12): the remote map on its channel (16), its knob modes, its buttons, its feedback; NRPN morph and roll */
static void rcc(uint32_t cc, uint32_t v) { web_midi(0xB0 | remote_ch, cc, v); run_ms(10); }
static void rnote(uint32_t n, int on) { web_midi((on ? 0x90 : 0x80) | remote_ch, n, on ? 100 : 0); run_ms(10); }
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
    uint32_t ch, i, ok, bpm, t;
    p4_reset();
    kit_init_par();
    snd_post_all();
    vp_set(VP_RCH, 16);
    check(remote_ch == 15u && kset.remote == 16u, "REMOTE: channel 16");
    vp_set(VP_RKNOBS, RK_ABS);
    goto_mode(B_GLO, M_CONFIG);
    while (ui.page != 7u)
        turn(EN_SELECT, 1);
    run_ms(50);
    shot("remote-config");
    goto_mode(B_HOME, M_HOME);
    /* ABS: a track's knobs and the master column, scaled to their ranges */
    rcc(24, 127);
    rcc(8 + 7, 127);
    rcc(88 + 7, 127);
    rcc(24 + 7, 0);
    check(par[PAR_FILTER_FREQ_1] == 127 && par[PAR_FX_DRY_WET] == vp_max(PAR_FX_DRY_WET) && morph_amt == 255 &&
          pf.dj == 0, "ABS: D1's filter, FX dry/wet, morph, DJ filter from the remote");
    pf.dj = 64;
    kit_set_morph(0);
    rcc(40 + 5, 11);
    rcc(40 + 6, 22);
    check(par[PAR_VELOD6_CLOSED] == 11 && par[PAR_VELOD6_OPEN] == 22, "CH's decay, OH's decay: each its own");
    i = par[PAR_FILTER_FREQ_1];
    rcc(38, 5);                                      /* (the LXR's CC 38 is D1's filter: here OH's resonance) */
    check(par[PAR_FILTER_FREQ_1] == i && par[PAR_RESO_6] == 5, "the remote channel: the remote map, not the LXR's");
    rcc(48 + 7, 0);
    check(par[PAR_BPM] == 60, "the tempo knob: 60 BPM at 0, never MIDI sync");
    rcc(48 + 7, 60);
    check(par[PAR_BPM] == 120, "120 BPM at 60");
    rcc(104, 127);
    rcc(104 + 4, 10);
    check(pfs.drill == 10, "the macro row: DRILL's density");
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
    rcc(48 + 7, 66);
    check(ok && par[PAR_BPM] == bpm + 2u, "REL: 64 +/- n, two's complement, the tempo a BPM a step");
    vp_set(VP_RKNOBS, RK_PICKUP);
    /* buttons: mute, solo, select, held FILL, a roll, DRILL, an effect held and latched, PLAY, patterns */
    sq.muted = sq.solo = 0;
    rnote(36 + 1, 1);
    rnote(36 + 1, 0);
    rnote(44 + 3, 1);
    rnote(52 + 4, 1);
    check(sq.muted == 2u && sq.solo == 8u && ui.track == 4, "buttons: D2 muted, SN soloed, CY selected");
    sq.muted = sq.solo = 0;
    rnote(52 + 7, 1);
    ok = (pf.on >> PF_FILL) & 1u;
    rnote(52 + 7, 0);
    check(ok && !((pf.on >> PF_FILL) & 1u), "FILL while held");
    rnote(60, 1);
    ok = sq.roll & 1u;
    rnote(60, 0);
    rnote(68 + 2, 1);
    ok &= (sq.drill >> 2) & 1u;
    rnote(68 + 2, 0);
    check(ok && !(sq.roll & 1u) && !sq.drill, "a roll and a DRILL while held");
    rnote(76 + 1, 1);
    ok = (pf.on >> PF_HALF) & 1u;
    rnote(76 + 1, 0);
    rnote(84 + 6, 1);
    rnote(84 + 6, 0);
    ok &= !((pf.on >> PF_HALF) & 1u) && ((pf.on >> PF_CRUSH) & 1u);
    goto_mode(B_ARP, M_PERF);
    goto_mode(B_HOME, M_HOME);                       /* (leaving PERF lets the panel's latches go, not the remote's) */
    ok &= (pf.on >> PF_CRUSH) & 1u;
    rnote(84 + 6, 1);
    check(ok && !((pf.on >> PF_CRUSH) & 1u), "HALF held; CRUSH latched (past PERF), then off");
    rnote(93, 1);
    check(sq.active == 1, "the next pattern");
    rnote(92, 1);
    check(sq.active == 0, "the previous pattern");
    /* REC: a track's knob records a lock on its own track */
    for (i = 0; i < 16u; i++)
        main_set(p, 2, i, 1);
    pat_backup = *p;
    rnote(36 + 7, 1);
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
    rnote(36 + 7, 1);
    check(!sq.running, "STOP from the remote");
    /* feedback: a panel change, a mute; nothing back for what the remote itself sent */
    run_ms(100);
    (void)fb_seen(0, 0, 0);
    par_set(PAR_FILTER_FREQ_1 + 1, 77);
    sq.muted = 4u;
    run_ms(30);
    ch = 0xB0u | remote_ch;
    ok = fb_seen(ch, 25, 77);
    sq.muted = 0;
    run_ms(30);
    check(ok, "feedback: D2's filter changed on the panel goes back as CC 25");
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
    sq.muted = 1u << 2;
    run_ms(30);
    check(fb_seen(0x90u | remote_ch, 36 + 2, 127), "feedback: D3 muted lights its mute button");
    sq.muted = 0;
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
    /* OFF: the channel is a plain one again */
    vp_set(VP_RCH, 0);
    par[PAR_RESO_1] = 9;
    web_midi(0xB0 | 15, 32, 100);
    run_ms(20);
    check(remote_ch == 0xFFu && kset.remote == 17u && par[PAR_RESO_1] == 9, "REMOTE OFF: nothing from the map");
    vp_set(VP_RCH, 16);
    vp_set(VP_RKNOBS, RK_PICKUP);
    kit_init_par();
    snd_post_all();
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
    phase5a();
    phase5b();
    phase5c();
    phase5d();
    phase9_perf();
    phase9_keys();
    phase9_library();
    phase9_patkeep();
    phase11();
    phase11_sound_solo();
    phase13_control_all();
    phase12_remote();
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: ok (%d shots)\n", fails ? fails : shot_n);
    return fails != 0;
}


