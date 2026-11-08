/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir host test: render a beat with the init kit (and every voice alone) to WAV, check the levels and that
 * every voice sounds, the clock's tempo and the MIDI clock out.
 *   cc -O2 -I build/gen -I firmware/src tests/render_test.c -o build/render_test && build/render_test build/
 * And (0.15) that the output doesn't depend on which voices render first (the second core), and the models' limit;
 * (18b) that the effects' early work changes nothing but the reverb's timing (one block). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#define VOICES_SWAP                      /* (voices.c: SN-HH rendered before D1-D3, as two cores may) */
#define FX_NOW                           /* (voices.c: the effects all after the mixer, as before 18b) */
#include "engine_host.h"

static void wav_write(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = frames * 4u, v;
    uint16_t h;
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite("RIFF", 1, 4, f);
    v = 36u + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    h = 1;
    fwrite(&h, 2, 1, f);
    h = 2;
    fwrite(&h, 2, 1, f);
    v = FS;
    fwrite(&v, 4, 1, f);
    v = FS * 4u;
    fwrite(&v, 4, 1, f);
    h = 4;
    fwrite(&h, 2, 1, f);
    h = 16;
    fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

static int16_t out[FS * 2 * 8];
static uint32_t render(uint32_t frames)
{
    int32_t blk[BLOCK * 2];
    uint32_t n = 0, i;
    while (n + BLOCK <= frames) {
        engine_block(blk);
        for (i = 0; i < BLOCK * 2u; i++)
            out[2u * n + i] = (int16_t)sat16(blk[i]);
        n += BLOCK;
    }
    return n;
}

/* 18b: frames rendered in a child process (fx_now as given, the reverb's level lv), into dst */
static void render_child(uint32_t now, uint32_t lv, int16_t *dst, uint32_t frames)
{
    int fd[2];
    pid_t pid;
    uint32_t got = 0;
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        uint32_t k, n, c;
        close(fd[0]);
        fx_now = (uint8_t)now;
        par_set(PAR_REV_LEVEL, (uint8_t)lv);
        seq_start(1);
        for (k = c = 0; k < frames; k += n, c++) {   /* (frames: whole blocks) */
            if (c == 2u) {                           /* (a time change: the crossfade, then a time waiting for it) */
                par_set(PAR_DLY_TIME, 70);
                n = render(BLOCK);
                par_set(PAR_DLY_TIME, 40);
            } else {
                n = render(frames - k < 690u * BLOCK ? frames - k : 690u * BLOCK);
            }
            if (!n || write(fd[1], out, n * 4u) != (ssize_t)(n * 4u))
                _exit(2);
        }
        _exit(0);
    }
    close(fd[1]);
    while (got < frames * 4u) {
        ssize_t r = read(fd[0], (char *)dst + got, frames * 4u - got);
        if (r <= 0)
            break;
        got += (uint32_t)r;
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    if (got < frames * 4u)
        memset((char *)dst + got, 0, frames * 4u - got);
}

/* 1.0 (Phase 8): the USB audio's stems. In a child process (the same state each time): recording or not, mix_out,
 * the beat (track < 0) or one track struck; the output into dst, the ring's frames into st */
static int16_t st_out[2][FS * 2 * 2], st_ring[3][FS * 2][UAC_NCH];
static void stems_child(uint32_t rec, uint32_t mask, int32_t track, int16_t *dst, int16_t (*st)[UAC_NCH], uint32_t frames)
{
    int fd[2];
    pid_t pid;
    uint32_t got = 0, want = frames * 4u + frames * 2u * UAC_NCH;
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        uint32_t k, n;
        close(fd[0]);
        host_stems_on = (uint8_t)rec;
        host_stem_w = 0;
        mix_out = (uint8_t)mask;
        if (track == -1)
            seq_start(1);
        else if (track >= 0)
            voice_trigger((uint32_t)track, 127, SEQ_DEFAULT_NOTE);
        for (k = 0; k < frames; k += n) {
            n = render(frames - k < 690u * BLOCK ? frames - k : 690u * BLOCK);
            if (!n || write(fd[1], out, n * 4u) != (ssize_t)(n * 4u))
                _exit(2);
        }
        if (!rec)
            memset(host_stems, 0, frames * 2u * UAC_NCH);
        if (write(fd[1], host_stems, frames * 2u * UAC_NCH) != (ssize_t)(frames * 2u * UAC_NCH))
            _exit(2);
        _exit(0);
    }
    close(fd[1]);
    while (got < want) {
        ssize_t r = read(fd[0], got < frames * 4u ? (char *)dst + got : (char *)st + got - frames * 4u,
                         got < frames * 4u ? frames * 4u - got : want - got);
        if (r <= 0)
            break;
        got += (uint32_t)r;
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    if (got < want) {
        printf("FAIL: the stems' child process\n");
        exit(1);
    }
}
static int32_t st_peak(int16_t (*st)[UAC_NCH], uint32_t c, uint32_t frames)
{
    int32_t pk = 0;
    uint32_t i;
    for (i = 0; i < frames; i++)
        pk = abs(st[i][c]) > pk ? abs(st[i][c]) : pk;
    return pk;
}
static uint32_t usb_stems_test(void)
{
    const uint32_t frames = FS * 2u / BLOCK * BLOCK;
    uint32_t fails = 0, c, i, t, same;
    int32_t pk;
    double e0 = 0, e1 = 0;
    printf("usb stems: the beat, 2 s\n");
    stems_child(0, 0, -1, st_out[0], st_ring[0], frames);         /* the reference: nobody records */
    stems_child(1, 0, -1, st_out[1], st_ring[1], frames);         /* recorded, every voice in the mix */
    if (memcmp(st_out[0], st_out[1], frames * 4u)) {
        printf("FAIL: recording the stems changes the output\n");
        fails++;
    }
    for (i = same = 0; i < frames; i++)
        same += st_ring[1][i][0] == st_out[1][2u * i] && st_ring[1][i][1] == st_out[1][2u * i + 1u];
    if (same != frames) {
        printf("FAIL: USB 1-2 aren't the output (%u of %u frames the same; MASTER full, volume 127)\n", same, frames);
        fails++;
    }
    for (c = 2; c < UAC_NCH; c++) {
        pk = st_peak(st_ring[1], c, frames);
        printf("  USB %u %s stem peak %6d\n", c + 1u, c == 7u ? "HH" : VOICE_SHORT[c - 2u], pk);
        if (pk < 500) {
            printf("FAIL: the %s stem is silent in the beat\n", VOICE_SHORT[c - 2u]);
            fails++;
        }
    }
    stems_child(1, 1, -1, st_out[0], st_ring[2], frames);         /* D1 out of the mix */
    for (i = same = 0; i < frames; i++)
        for (c = 2; c < UAC_NCH; c++)
            same += st_ring[2][i][c] == st_ring[1][i][c];
    if (same != frames * (UAC_NCH - 2u)) {
        printf("FAIL: D1 OUT of the mix changes the stems (%u of %u samples the same)\n", same, frames * 6u);
        fails++;
    }
    for (i = 0; i < frames; i++) {                   /* what OUT took away from 1-2 */
        double d = (double)st_ring[1][i][0] - st_ring[2][i][0], dd = (double)st_ring[1][i][2];
        e0 += d * d;
        e1 += dd * dd;
    }
    printf("  D1 OUT: the mix lost rms %.1f (the D1 stem's rms %.1f)\n", sqrt(e0 / frames), sqrt(e1 / frames));
    if (e0 < e1 * 0.05) {
        printf("FAIL: D1 OUT of the mix: the mix barely changed\n");
        fails++;
    }
    stems_child(1, 63, -1, st_out[0], st_ring[2], frames);        /* every voice out: the mix silent, stems as before */
    pk = st_peak(st_ring[2], 0, frames) > st_peak(st_ring[2], 1, frames) ? st_peak(st_ring[2], 0, frames)
                                                                          : st_peak(st_ring[2], 1, frames);
    printf("  every voice OUT: the mix's peak %d\n", pk);
    if (pk > 4) {
        printf("FAIL: every voice OUT of the mix, and the mix still sounds (peak %d)\n", pk);
        fails++;
    }
    for (i = same = 0; i < frames; i++)
        for (c = 2; c < UAC_NCH; c++)
            same += st_ring[2][i][c] == st_ring[1][i][c];
    if (same != frames * (UAC_NCH - 2u)) {
        printf("FAIL: every voice OUT changes the stems\n");
        fails++;
    }
    stems_child(0, 63, -1, st_out[1], st_ring[2], frames);        /* not recording: OUT does nothing */
    stems_child(0, 0, -1, st_out[0], st_ring[2], frames);
    if (memcmp(st_out[0], st_out[1], frames * 4u)) {
        printf("FAIL: USB MIX OUT changes the output while nobody records\n");
        fails++;
    }
    stems_child(1, 0, -2, st_out[0], st_ring[1], FS / 2u / BLOCK * BLOCK);   /* none struck: the beat's tails */
    for (t = 0; t < NTRACK; t++) {                   /* each track struck: its voice's stem alone (7: the hi-hat's) */
        uint32_t v = t < NVOICE ? t : V_HAT, n = FS / 2u / BLOCK * BLOCK, d;
        stems_child(1, 0, (int32_t)t, st_out[0], st_ring[2], n);
        for (c = 2; c < UAC_NCH; c++) {
            pk = st_peak(st_ring[2], c, n);
            for (i = d = 0; i < n; i++)
                d += st_ring[2][i][c] != st_ring[1][i][c];
            if (c - 2u == v ? pk < 500 : d != 0) {
                printf("FAIL: track %u struck: the %s stem (peak %d, %u samples changed)\n", t + 1u,
                       c == 7u ? "HH" : VOICE_SHORT[c - 2u], pk, d);
                fails++;
            }
        }
    }
    return fails;
}

/* 1.0 (the user's crackles): the delay's MODE changed while its echoes sound: they fade out (fxbus.c dt_next) rather than
 * stop dead. In a child: D1 struck into the delay, then (sw) MONO -> mode at block k; the output into dst */
static int16_t dm_out[3][1200u * BLOCK * 2u];
static void dmode_child(uint32_t sw, uint32_t mode, uint32_t k, int16_t *dst, uint32_t frames)
{
    int fd[2];
    pid_t pid;
    uint32_t got = 0;
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        uint32_t b, n = 0;
        close(fd[0]);
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);
        par_set(PAR_DLY_LEVEL, sw >= 4u ? 0 : 127);   /* (4: the walk's pattern dry, no delay; 5: one hit's) */
        par_set(PAR_DLY_TIME, 50);
        par_set(PAR_DLY_FB, 100);
        par_set(PAR_FX_SEND1, 64);
        if (sw >= 2u && sw <= 4u) {                  /* (the walk's D1: a slow attack, no transient: no edge of its own) */
            par_set(PAR_VELOA1, 40);
            par_set(PAR_TRANS1_VOL, 0);
            par_set(PAR_VELOD1, 30);
        }
        render(BLOCK);
        voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
        for (b = 0; n < frames; b++) {
            if ((sw < 2u || sw == 5u) && b == k - 4u)              /* (struck again: a loud sound going in as the new line starts) */
                voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
            if (sw == 1u && b == k)
                par_set(PAR_FX_DELAY_TYPE, mode);
            if (sw >= 2u && sw <= 4u && b % 20u == 0u)            /* (the walk: a pattern of D1, smooth enough that a click shows; 3: the mode turned slowly) */
                voice_trigger(0, b % 40u ? 90 : 127, SEQ_DEFAULT_NOTE);
            if (sw == 3u && b >= k && (b - k) % 60u == 0u) {
                static const uint8_t W[] = {1, 2, 3, 2, 1, 0, DELAY_OFF, 3, 0, DELAY_OFF, 2, 0, 1, 3, 1, 0};
                uint32_t j = (b - k) / 60u;
                if (j < sizeof W)
                    par_set(PAR_FX_DELAY_TYPE, W[j]);
            }
            n += render(BLOCK);
            if (write(fd[1], out, BLOCK * 4u) != (ssize_t)(BLOCK * 4u))
                _exit(2);
        }
        _exit(0);
    }
    close(fd[1]);
    while (got < frames * 4u) {
        ssize_t r = read(fd[0], (char *)dst + got, frames * 4u - got);
        if (r <= 0)
            break;
        got += (uint32_t)r;
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
}
static uint32_t delay_mode_test(void)
{
    static const char *const MN[NDELAY + 1u] = {"MONO", "PING PONG", "TAPE", "TAPE PP", "OFF"};
    const uint32_t frames = FS / BLOCK * BLOCK, k = 200;    /* (block 200: ~0.29 s, the echoes sounding) */
    uint32_t m, i, fails = 0;
    dmode_child(0, 0, k, dm_out[0], frames);
    dmode_child(5, 0, k, dm_out[2], frames);         /* (the dry sound: no delay) */
    for (m = 1; m <= DELAY_OFF; m++) {
        int32_t step = 0, echo = 0;
        dmode_child(1, m, k, dm_out[1], frames);
        for (i = k * BLOCK * 2u; i < (k * BLOCK + 32u) * 2u; i++) {   /* the first 32 samples after the change */
            int32_t d = abs(dm_out[1][i] - dm_out[0][i]);
            step = d > step ? d : step;
            echo = abs(dm_out[0][i]) > echo ? abs(dm_out[0][i]) : echo;
        }
        printf("  delay MONO -> %-9s: the first 32 samples moved %5d (the echoes' peak there %5d)\n", MN[m], step, echo);
        if (echo < 500 || step * 4 > echo) {
            printf("FAIL: the delay's MODE change cuts its echoes (or there were none to test)\n");
            fails++;
        }
        {   /* .. no step anywhere in the next ~120 ms beyond the steepest the steady MONO output takes */
            int32_t s0 = 0, s1 = 0;
            for (i = k * BLOCK * 2u; i < (k + 80u) * BLOCK * 2u; i++) {
                int32_t a = abs(dm_out[0][i] - dm_out[0][i - 2u]), b = abs(dm_out[1][i] - dm_out[1][i - 2u]);
                s0 = a > s0 ? a : s0;
                s1 = b > s1 ? b : s1;
            }
            printf("  delay MONO -> %-9s: steepest step after %5d (steady MONO %5d)\n", MN[m], s1, s0);
            if (s1 > s0 * 2 + 200) {
                printf("FAIL: the delay's MODE change jumps after its fade (%s)\n", MN[m]);
                fails++;
            }
        }
        if (m == DELAY_OFF) {                        /* .. OFF: only the dry sound left, ~6 ms on */
            int32_t left = 0;
            for (i = (k + 8u) * BLOCK * 2u; i < (k + 40u) * BLOCK * 2u; i++)
                left = abs(dm_out[1][i] - dm_out[2][i]) > left ? abs(dm_out[1][i] - dm_out[2][i]) : left;
            printf("  delay MONO -> OFF      : the echoes left after its fade %d\n", left);
            if (left) {
                printf("FAIL: the delay's MODE OFF leaves echoes\n");
                fails++;
            }
        } else {   /* .. and the mode changed after the fade (the line cleared: the old echoes gone, ~6 ms on) */
            int32_t late = 0;
            for (i = (k + 8u) * BLOCK * 2u; i < (k + 40u) * BLOCK * 2u; i++)
                late = abs(dm_out[1][i] - dm_out[0][i]) > late ? abs(dm_out[1][i] - dm_out[0][i]) : late;
            if (late < 500) {
                printf("FAIL: the delay's MODE didn't change after its fade (%s)\n", MN[m]);
                fails++;
            }
        }
    }
    {   /* the mode turned slowly through every change while a pattern plays: no step the steady pattern doesn't take */
        const uint32_t wf = 1200u * BLOCK;
        int32_t s0 = 0, s1 = 0;
        dmode_child(2, 0, k, dm_out[0], wf);
        dmode_child(3, 0, k, dm_out[1], wf);
        dmode_child(4, 0, k, dm_out[2], wf);
        for (i = 2u * BLOCK * 2u; i < wf * 2u; i++) {   /* (the echoes alone: the output less the dry pattern's) */
            int32_t a = abs(dm_out[0][i] - dm_out[2][i] - dm_out[0][i - 2u] + dm_out[2][i - 2u]);
            int32_t b = abs(dm_out[1][i] - dm_out[2][i] - dm_out[1][i - 2u] + dm_out[2][i - 2u]);
            s0 = a > s0 ? a : s0;
            s1 = b > s1 ? b : s1;
        }
        printf("  delay modes walked under a pattern: the echoes' steepest step %5d (steady MONO %5d)\n", s1, s0);
        if (s1 > s0 * 2 + 200) {
            printf("FAIL: the delay's MODE changes jump under a pattern\n");
            fails++;
        }
    }
    return fails;
}

static void stats(const char *name, uint32_t frames, int32_t *peak_out, double *rms_out)
{
    int32_t peak = 0;
    double acc = 0;
    uint32_t i;
    for (i = 0; i < frames * 2u; i++) {
        int32_t a = abs(out[i]);
        if (a > peak)
            peak = a;
        acc += (double)out[i] * out[i];
    }
    *peak_out = peak;
    *rms_out = sqrt(acc / (frames * 2.0));
    printf("  %-10s peak %6d  rms %8.1f\n", name, peak, *rms_out);
}

/* 28: a filter alone: n samples of in through a fresh svf_t of a type (cutoff fval Q16, resonance 0..127, the
 * note's offset key), into o */
static int16_t f28_line[FLT_LINE];
static void f28_setup(svf_t *f, uint32_t fval, uint32_t reso, int key)
{
    int32_t q = 65536 - (int32_t)(reso * 65536u / 127u);
    memset(f, 0, sizeof *f);
    svf_init(f);
    f->line = f28_line;
    f->fval = fval;
    f->q = q < 6554 ? 1311 : q;
    f->rk = (int32_t)(reso * 65536u / 127u);
    f->key = (int8_t)key;
    f->dirty = 1;
}
static void f28_run(svf_t *f, uint32_t type, const int16_t *in, int16_t *o, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + BLOCK <= n; i += BLOCK) {
        memcpy(o + i, in + i, BLOCK * sizeof o[0]);
        svf_block(f, type, o + i);
    }
}
static double f28_mag(const int16_t *x, uint32_t n, double hz)   /* |DFT| at hz */
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        re += x[i] * cos(2 * M_PI * hz * i / FS);
        im -= x[i] * sin(2 * M_PI * hz * i / FS);
    }
    return sqrt(re * re + im * im);
}
static double f28_sine_gain(uint32_t type, uint32_t fval, uint32_t reso, double hz)   /* steady sine, out / in rms */
{
    static int16_t in[8192], o[8192];
    svf_t f;
    double a = 0, b = 0, m = 0;
    uint32_t i;
    for (i = 0; i < 8192u; i++)
        in[i] = (int16_t)(8000 * sin(2 * M_PI * hz * i / FS));
    f28_setup(&f, fval, reso, 0);
    f28_run(&f, type, in, o, 8192);
    for (i = 4096; i < 8192u; i++)
        m += o[i] / 4096.0;
    for (i = 4096; i < 8192u; i++) {               /* (without the DC the roundings leave) */
        a += (double)in[i] * in[i];
        b += (o[i] - m) * (o[i] - m);
    }
    return sqrt(b / a);
}

/* 27a: the insert alone on voice 5's state: n samples of in through it (type, a, b, c), into o; fresh: from rest */
static void i27_run(uint32_t type, uint32_t a, uint32_t b, uint32_t c, const int16_t *in, int16_t *o, uint32_t n,
                    int fresh)
{
    uint32_t i;
    if (fresh) {
        memset(&ins[5], 0, sizeof ins[5]);
        ins[5].was = 0xFF;
    }
    ins[5].type = (uint8_t)type;
    ins[5].a = (uint8_t)a;
    ins[5].b = (uint8_t)b;
    ins[5].c = (uint8_t)c;
    for (i = 0; i + BLOCK <= n; i += BLOCK) {
        memcpy(o + i, in + i, BLOCK * sizeof o[0]);
        ins_block(5, o + i);
    }
}
static void i27_sine(int16_t *x, uint32_t n, double hz, double amp)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        x[i] = (int16_t)(amp * sin(2 * M_PI * hz * i / FS));
}

/* 1.0: the delay's BAND, the delay alone: a noise burst in, FEEDBACK as given; the echoes' peak over the run and their
 * rms over its last second, and the tail's level at the band's centre against an octave either side */
static void band_run(uint32_t ty, uint32_t band, uint32_t fb, int32_t *pk, double *tail, double *focus)
{
    static int16_t t[FS];
    uint32_t b, i, nb = FS * 4u / BLOCK, rng = 1u, tn = 0;
    double acc = 0;
    par_set(PAR_FX_DELAY_TYPE, ty);
    par_set(PAR_DLY_TIME, 30);
    par_set(PAR_DLY_FB, fb);
    par_set(PAR_DLY_TONE, 127);
    par_set(PAR_DLY_LEVEL, 127);
    par_set(PAR_DLY_BAND, band);
    snd_service();                                   /* (the posted settings, as at a block start) */
    delay_type_now(ty);                              /* (1.0: fx_delay alone: no fade, no clear a block, no input */
    fx.dt_clr = 0;                                   /* .. fade-in: fxbus_process's; the mode now, at full) */
    fx.dl_g = fx.dl_in = 32768;
    fx.dl_e = dl_gain();
    memset(fxbuf, 0, sizeof fxbuf);
    memset(&tp, 0, sizeof tp);
    memset(fx.bd, 0, sizeof fx.bd);
    fx.d_lp[0] = fx.d_lp[1] = 0;
    *pk = 0;
    for (b = 0; b < nb; b++) {
        for (i = 0; i < BLOCK; i++) {
            rng = rng * 1664525u + 1013904223u;
            fx_l[i] = fx_r[i] = (int16_t)(b < 40u ? (int32_t)(rng >> 16) - 32768 >> 2 : 0);
        }
        memset(mix_l, 0, sizeof mix_l);
        memset(mix_r, 0, sizeof mix_r);
        memset(rev_in, 0, BLOCK * sizeof *rev_in);
        d_read = 0;
        fx_delay();
        for (i = 0; i < BLOCK; i++) {
            int32_t a = abs(mix_l[i]) > abs(mix_r[i]) ? abs(mix_l[i]) : abs(mix_r[i]);
            *pk = a > *pk ? a : *pk;
            if (b >= nb - FS / BLOCK) {
                acc += (double)mix_l[i] * mix_l[i];
                if (tn < FS)
                    t[tn++] = (int16_t)(mix_l[i] > 32767 ? 32767 : mix_l[i] < -32768 ? -32768 : mix_l[i]);
            }
        }
    }
    *tail = sqrt(acc / tn);
    if (focus) {                                     /* (the centre: 150 Hz x 2^(5.06 (band - 1) / 126)) */
        double hz = 150.0 * pow(2.0, 5.06 * (band - 1.0) / 126.0), c = 0, o = 0, f;
        for (f = hz * 0.97; f <= hz * 1.03; f += hz * 0.005)
            c = fmax(c, f28_mag(t, tn, f));
        for (f = hz * 0.4; f <= hz * 0.6; f += hz * 0.01)
            o = fmax(o, f28_mag(t, tn, f));
        for (f = hz * 1.7; f <= hz * 2.3; f += hz * 0.02)
            o = fmax(o, f28_mag(t, tn, f));
        *focus = 20 * log10((c + 1) / (o + 1));
    }
    par_set(PAR_DLY_BAND, 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/";
    char path[512];
    uint32_t t, k, fails = 0, frames;
    int32_t peak;
    double rms;
    engine_init();
    master_q12 = 4096;
    if (!pl_words_ready) {                           /* 0.15: at boot, not in a block (it passed core 1's deadline) */
        printf("FAIL: Plaits' speech words not found at boot\n");
        fails++;
    }
    /* a beat: kick 1 5 9 13, snare 5 13, clap 13 (cymbal voice), closed hat on the eighths, open hat on 15 */
    for (k = 0; k < 16; k += 4)
        main_set(&pat[0], 0, k, 1);
    main_set(&pat[0], 3, 4, 1);
    main_set(&pat[0], 3, 12, 1);
    main_set(&pat[0], 4, 12, 1);
    for (k = 0; k < 16; k += 2)
        main_set(&pat[0], 5, k, 1);
    main_set(&pat[0], 6, 14, 1);
    main_set(&pat[0], 1, 10, 1);
    main_set(&pat[0], 2, 7, 1);
    pat_backup = pat[0];
    printf("render: 120 BPM, the init kit\n");
    seq_start(1);
    frames = render(FS * 4);
    snprintf(path, sizeof path, "%sbeat.wav", dir);
    wav_write(path, out, frames);
    stats("beat", frames, &peak, &rms);
    if (peak < 3000 || rms < 300) {
        printf("FAIL: the beat is (nearly) silent\n");
        fails++;
    }
    {   /* MIDI clock out: 24 a beat -> 2 s at 120 BPM = 4 beats = 96 clocks (+ the one at the start) */
        uint32_t n = 0, i;
        for (i = 0; i < host_midi_out_n; i++)
            if (((host_midi_out[i] >> 8) & 0xFFu) == 0xF8u)
                n++;
        printf("  midi clock out: %u in 4 s (want ~192)\n", n);
        if (n < 188 || n > 196) {
            printf("FAIL: MIDI clock count\n");
            fails++;
        }
    }
    seq_stop_now();
    render(FS / 2);
    fails += usb_stems_test();                       /* (1.0, Phase 8) */
    fails += delay_mode_test();
    for (t = 0; t < NTRACK; t++) {                   /* each voice alone, triggered */
        voice_trigger(t, 127, SEQ_DEFAULT_NOTE);
        frames = render(FS);
        snprintf(path, sizeof path, "%svoice%u.wav", dir, t + 1u);
        wav_write(path, out, frames);
        stats(VOICE_NAME[t], frames, &peak, &rms);
        if (peak < 1000) {
            printf("FAIL: %s is silent\n", VOICE_NAME[t]);
            fails++;
        }
    }
    {   /* 0.15: each Plaits engine on DRUM 1, struck (the knobs at their middle, the amp EG as the kit's) */
        static const char *const PE_NAMES[16] = {"P VA", "P WSHAPE", "P FM", "P GRAIN", "P ADDTV", "P WTABLE",
                                                 "P CHORD", "P SPEECH", "P SWARM", "NOISE", "P PARTCL", "P STRING",
                                                 "P MODAL", "P BD", "P SD", "P HH"};   /* (1.0: NOISE, noise.c, in
                                                      * P NOISE's number) */
        uint32_t e;
        par_set(PAR_MDECAY1, 64);
        par_set(PAR_MDAMP1, 64);
        par_set(PAR_MMAT1, 64);
        for (e = 0; e < 16u; e++) {
            if ((e == 5u && !ELIXIR_WTABLE) || e == 3u || e == 12u)   /* (0.24: P WTABLE parked, rings.c; 1.0: P GRAIN,
                                                      * P MODAL dropped) */
                continue;
            par_set(PAR_MODEL1, MD_PL0 + e);
            render(FS / 4);
            voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
            frames = render(FS);
            snprintf(path, sizeof path, "%splaits%02u.wav", dir, e);
            wav_write(path, out, frames);
            stats(PE_NAMES[e], frames, &peak, &rms);
            if (peak < 1000 || peak >= 32767) {
                printf("FAIL: %s is silent or clips\n", PE_NAMES[e]);
                fails++;
            }
        }
        {   /* 0.24: P CHORD bit-exact across its knobs (its 15 waves now a table of their own, P WTABLE parked) */
            uint32_t k, h = 2166136261u, i;
            par_set(PAR_MODEL1, MD_PL0 + 6u);        /* (PE_CHORD) */
            for (k = 0; k < 27u; k++) {              /* (Phase 10f: the output a block later, so each window too: its
                                                      * last block the next render's first) */
                par_set(PAR_MDECAY1, (k % 3u) * 63u);
                par_set(PAR_MDAMP1, ((k / 3u) % 3u) * 63u);
                par_set(PAR_MMAT1, (k / 9u) * 63u);
                render(FS / 16);
                for (i = 0; k && i < BLOCK * 2u; i++)
                    h = (h ^ (uint16_t)out[i]) * 16777619u;
                voice_trigger(0, 127, SEQ_DEFAULT_NOTE);
                frames = render(FS / 8);
                for (i = BLOCK * 2u; i < frames * 2u; i++)
                    h = (h ^ (uint16_t)out[i]) * 16777619u;
            }
            render(BLOCK);
            for (i = 0; i < BLOCK * 2u; i++)
                h = (h ^ (uint16_t)out[i]) * 16777619u;
            if (h != 0xb5fecaa3u) {                  /* (0.23's, before the split) */
                printf("FAIL: P CHORD hash %08x, not 0.23's b5fecaa3\n", h);
                fails++;
            }
        }
        par_set(PAR_MODEL1, 0);
    }
    {   /* 24: MICRO, each type on each voice, struck (LENGTH, COLOUR at the middle): it sounds, doesn't clip, ends;
         * backwards it sounds too; its level (over where it sounds) near the others'; SN-HH can't take Rings' */
        static const char *const MI_NAMES[NMICRO] = {"CLICK", "TRAIN", "CRUNCH", "BITS", "BURST", "SNAP", "GRAIN",
                                                     "DUST", "POP", "CHIRP"};
        uint32_t v, e, back, i;
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_MDECAY1 + v, 64);
            par_set(PAR_MDAMP1 + v, 64);
            par_set(PAR_MMAT1 + v, 0);
            par_set(PAR_MTENS1 + v, 0);
            par_set(PAR_FILTER_TYPE_1 + v, 7);           /* (the filter OFF: the init kit's cymbal is a high pass) */
        }
        for (v = 0; v < NVOICE; v++)
            for (back = 0; back < 2u; back++)
                for (e = 0; e < NMICRO; e++) {
                    uint32_t last = 0, loud = 0;
                    int32_t pk = 0;
                    double acc = 0;
                    par_set(PAR_MODEL1 + v, MD_MC0 + e);
                    par_set(PAR_MEXC1 + v, back ? 127 : 0);
                    render(FS / 8);
                    voice_trigger(v, 127, SEQ_DEFAULT_NOTE);
                    frames = render(FS / 2);
                    for (i = 0; i < frames * 2u; i++) {
                        int32_t a = abs(out[i]);
                        if (a > pk)
                            pk = a;
                        if (a > 64) {
                            last = i / 2u;
                            loud++;
                            acc += (double)out[i] * out[i];
                        }
                    }
                    if (v == 0u || v == V_HAT) {
                        snprintf(path, sizeof path, "%smicro%u%s_%s.wav", dir, v + 1u, back ? "r" : "", MI_NAMES[e]);
                        wav_write(path, out, frames);
                    }
                    if (v == 0u || pk < 500 || pk >= 32767 || last > frames * 3u / 4u)
                        printf("  %s %-6s %s peak %6d  rms %7.1f  length %6.1f ms\n", VOICE_SHORT[v], MI_NAMES[e],
                               back ? "rev" : "   ", pk, loud ? sqrt(acc / loud) : 0.0, last * 1000.0 / FS);
                    if (pk < 500 || pk >= 32767) {
                        printf("FAIL: MICRO %s on %s%s is silent or clips\n", MI_NAMES[e], VOICE_NAME[v],
                               back ? " (reverse)" : "");
                        fails++;
                    }
                    if (last > frames * 3u / 4u) {
                        printf("FAIL: MICRO %s on %s%s doesn't end\n", MI_NAMES[e], VOICE_NAME[v],
                               back ? " (reverse)" : "");
                        fails++;
                    }
                }
        for (e = 0; e < NMICRO; e++)                     /* (D1: LENGTH and COLOUR at their ends, high and low; */
            for (i = 0; i < 16u; i++) {                  /* .. forwards and backwards) */
                uint32_t j;
                int32_t pk = 0;
                par_set(PAR_MODEL1, MD_MC0 + e);
                par_set(PAR_MDECAY1, i & 1u ? 127 : 0);
                par_set(PAR_MDAMP1, i & 2u ? 127 : 0);
                par_set(PAR_MEXC1, i & 8u ? 127 : 0);
                render(FS / 8);
                voice_trigger(0, 127, i & 4u ? 96 : 24);
                frames = render(FS / 2);
                for (j = 0; j < frames * 2u; j++)
                    pk = abs(out[j]) > pk ? abs(out[j]) : pk;
                if (pk < 200 || pk >= 32767) {
                    printf("FAIL: MICRO %s, length %u colour %u note %u%s: peak %d\n", MI_NAMES[e], i & 1u ? 127 : 0,
                           i & 2u ? 127 : 0, i & 4u ? 96 : 24, i & 8u ? " reverse" : "", pk);
                    fails++;
                }
            }
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_MODEL1 + v, 0);
            par_set(PAR_MEXC1 + v, 0);
            par_set(PAR_FILTER_TYPE_1 + v, INIT_KIT[8u + PAR_FILTER_TYPE_1 + v]);
        }
        render(BLOCK);
        for (v = 0; v < NVOICE; v++) {
            if (mi[v].type) {
                printf("FAIL: MICRO stays on %s\n", VOICE_NAME[v]);
                fails++;
            }
        }
        par_set(PAR_MODEL1 + 3, MD_MODAL);               /* (SN: Rings' isn't its: it plays the LXR's) */
        render(BLOCK);
        if (mi[3].type) {
            printf("FAIL: SN on MODAL\n");
            fails++;
        }
        par_set(PAR_MODEL1 + 3, 0);
    }
    {   /* 28: Elixir's filter types. On each voice, struck at RESONANCE 0 and 127: it sounds, ends; alone: COMB's
         * echoes at the pitch (and the note), COMB-'s negative; LP4's slope against LP's; VOWEL's formants; ALLPASS's
         * notch; RESONATOR's three pitches; a switch clears the line; morph switches at 50% */
        static const char *const FT_NAMES[] = {"LP4", "COMB+", "COMB-", "ALLPASS", "VOWEL", "DUB BP", "RESONATOR",
                                               "ACID", "MS20LP", "MS20HP", "LADDER"};
        _Static_assert(sizeof FT_NAMES / sizeof FT_NAMES[0] == F_LAST - F_LP4 + 1, "a name each");
        static int16_t in[16384], o[16384];
        uint32_t v, ty, r, i;
        svf_t f;
        uint32_t base[NVOICE];
        for (v = 0; v < NVOICE; v++) {                   /* (each voice's length unfiltered: the cymbal's long) */
            base[v] = 0;
            par_set(PAR_FILTER_TYPE_1 + v, F_OFF - 1u);
            render(FS / 8);
            voice_trigger(v, 127, SEQ_DEFAULT_NOTE);
            frames = render(FS);
            for (i = 0; i < frames * 2u; i++)
                if (abs(out[i]) > 64)
                    base[v] = i / 2u;
        }
        for (v = 0; v < NVOICE; v++)
            for (ty = F_LP4; ty <= F_LAST; ty++)
                for (r = 0; r < 2u; r++) {
                    uint32_t last = 0;
                    int32_t pk = 0;
                    par_set(PAR_FILTER_TYPE_1 + v, ty - 1u);
                    par_set(PAR_RESO_1 + v, r ? 127 : 0);
                    par_set(PAR_FILTER_DRIVE_1 + v, r ? 127 : 20);
                    par_set(PAR_FILTER_FREQ_1 + v, ty == F_MS20H ? 12u : INIT_KIT[8u + PAR_FILTER_FREQ_1 + v]);   /* (1.0:
                                                      * a 12 dB high-pass at the kick's cutoff leaves nothing of it) */
                    render(FS / 8);
                    voice_trigger(v, 127, SEQ_DEFAULT_NOTE);
                    frames = render(FS);
                    for (i = 0; i < frames * 2u; i++) {
                        int32_t a = abs(out[i]);
                        pk = a > pk ? a : pk;
                        if (a > 64)
                            last = i / 2u;
                    }
                    if (v == 0u && r) {
                        snprintf(path, sizeof path, "%sfilter_%s.wav", dir, FT_NAMES[ty - F_LP4]);
                        wav_write(path, out, frames);
                    }
                    if (pk < 150 || (last > frames * 3u / 4u && last > base[v] + FS / 8u)) {
                        printf("FAIL: %s with %s at resonance %u: peak %d, length %.1f ms\n", VOICE_NAME[v],
                               FT_NAMES[ty - F_LP4], r ? 127 : 0, pk, last * 1000.0 / FS);
                        fails++;
                    }
                }
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_FILTER_TYPE_1 + v, INIT_KIT[8u + PAR_FILTER_TYPE_1 + v]);
            par_set(PAR_RESO_1 + v, INIT_KIT[8u + PAR_RESO_1 + v]);
            par_set(PAR_FILTER_DRIVE_1 + v, INIT_KIT[8u + PAR_FILTER_DRIVE_1 + v]);
            par_set(PAR_FILTER_FREQ_1 + v, INIT_KIT[8u + PAR_FILTER_FREQ_1 + v]);
        }
        memset(in, 0, sizeof in);
        in[0] = 20000;
        for (r = 0; r < 4u; r++) {                       /* COMB: cutoff 0.5 = 86 Hz x 2^2.5: 90.5 samples; +12: 45.25 */
            uint32_t ty2 = r & 1u ? F_COMBN : F_COMBP, best = 0;
            double want = r & 2u ? 45.25 : 90.51;
            int32_t bv = 0;
            f28_setup(&f, 32768, 100, r & 2u ? 12 : 0);
            f28_run(&f, ty2, in, o, 1024);
            for (i = 8; i < 160u; i++)
                if (abs(o[i]) > abs(bv)) {
                    bv = o[i];
                    best = i;
                }
            printf("  %s key %d: first echo at %u (%.2f), %d\n", ty2 == F_COMBP ? "COMB+" : "COMB-", r & 2u ? 12 : 0,
                   best, want, bv);
            if (fabs(best - want) > 1.5 || (ty2 == F_COMBP ? bv <= 0 : bv >= 0)) {
                printf("FAIL: COMB's echo\n");
                fails++;
            }
        }
        {
            double lp_hi = f28_sine_gain(F_LP, 16384, 0, 4000), lp4_hi = f28_sine_gain(F_LP4, 16384, 0, 4000);
            double lp_lo = f28_sine_gain(F_LP, 16384, 0, 60), lp4_lo = f28_sine_gain(F_LP4, 16384, 0, 60);
            printf("  LP4 (cutoff 342 Hz): %.1f dB at 60 Hz, %.1f dB at 4 kHz (LP %.1f, %.1f dB)\n", 20 * log10(lp4_lo),
                   20 * log10(lp4_hi), 20 * log10(lp_lo), 20 * log10(lp_hi));
            if (lp4_lo < lp_lo * 0.7 || lp4_hi > lp_hi * 0.1) {
                printf("FAIL: LP4's slope\n");
                fails++;
            }
        }
        {
            double mn = 9, dc = f28_sine_gain(F_APF, 32768, 0, 30), hz;
            for (hz = 50; hz < 15000; hz *= 1.06) {
                double g = f28_sine_gain(F_APF, 32768, 64, hz);
                mn = g < mn ? g : mn;
            }
            printf("  ALLPASS: %.1f dB at 30 Hz, its notch %.1f dB\n", 20 * log10(dc), 20 * log10(mn));
            if (dc < 0.25 || mn > dc * 0.3) {
                printf("FAIL: ALLPASS's notch\n");
                fails++;
            }
        }
        {
            double f1, mid, hi;
            f28_setup(&f, 0, 100, 0);                    /* (A: 800, 1150 Hz) */
            f28_run(&f, F_VOWEL, in, o, 4096);
            f1 = f28_mag(o, 4096, 800);
            mid = f28_mag(o, 4096, 3000);
            hi = f28_mag(o, 4096, 6000);
            printf("  VOWEL A: 800 Hz / 3 kHz %.1f dB, / 6 kHz %.1f dB\n", 20 * log10(f1 / mid), 20 * log10(f1 / hi));
            if (f1 < mid * 4 || f1 < hi * 8) {
                printf("FAIL: VOWEL's formants\n");
                fails++;
            }
        }
        {
            double root = 86.13 * pow(2, 2.5), m[4];
            f28_setup(&f, 32768, 127, 0);
            f28_run(&f, F_RESO, in, o, 16384);
            m[0] = f28_mag(o, 16384, root);
            m[1] = f28_mag(o, 16384, root * 1.5);
            m[2] = f28_mag(o, 16384, root * 2);
            m[3] = f28_mag(o, 16384, root * 1.25);
            printf("  RESONATOR: root %.0f, fifth %.0f, octave %.0f, between %.0f\n", m[0], m[1], m[2], m[3]);
            if (m[0] < m[3] * 3 || m[1] < m[3] * 3 || m[2] < m[3] * 3) {
                printf("FAIL: RESONATOR's pitches\n");
                fails++;
            }
        }
        {   /* 1.0: the character filters. ACID's slope softer than LP4's, steeper than LP's; LADDER's as LP4's; MS20 HP's
             * low cut; LADDER's BASS COMP brings the bass back at full resonance; the resonance peaks (curved knob) */
            double lp4 = f28_sine_gain(F_LP4, 16384, 0, 1500), acid = f28_sine_gain(F_ACID, 16384, 0, 1500);
            double lp = f28_sine_gain(F_LP, 16384, 0, 1500), lad = f28_sine_gain(F_LADDER, 16384, 0, 1500);
            double a_lo = f28_sine_gain(F_ACID, 16384, 0, 30), h_lo = f28_sine_gain(F_MS20H, 32768, 0, 60);
            double h_hi = f28_sine_gain(F_MS20H, 32768, 0, 8000), m_lo = f28_sine_gain(F_MS20L, 32768, 0, 60);
            double m_hi = f28_sine_gain(F_MS20L, 32768, 0, 8000), b0, b1, pk = 0, hz;
            printf("  ACID 1.5 kHz %.1f dB (30 Hz %.1f); LADDER %.1f, LP4 %.1f, LP %.1f dB\n", 20 * log10(acid),
                   20 * log10(a_lo), 20 * log10(lad), 20 * log10(lp4), 20 * log10(lp));
            printf("  MS20 HP 60 Hz %.1f, 8 kHz %.1f dB; MS20 LP 60 Hz %.1f, 8 kHz %.1f dB\n", 20 * log10(h_lo),
                   20 * log10(h_hi), 20 * log10(m_lo), 20 * log10(m_hi));
            double sa = 20 * log10(f28_sine_gain(F_ACID, 8000, 0, 1000) / f28_sine_gain(F_ACID, 8000, 0, 2000));
            double s4 = 20 * log10(f28_sine_gain(F_LP4, 8000, 0, 250) / f28_sine_gain(F_LP4, 8000, 0, 500));
            printf("  slopes an octave: ACID %.1f dB, LP4 %.1f dB\n", sa, s4);
            if (sa < 12 || sa > 22 || s4 < 20 || fabs(20 * log10(lad / lp4)) > 1.0 || h_lo > h_hi * 0.05 ||
                m_hi > m_lo * 0.05 || a_lo < 0.2) {
                printf("FAIL: the character filters' slopes\n");
                fails++;
            }
            {
                svf_t g;
                static int16_t a[4096];
                double lo0 = 0, lo1 = 0;
                for (i = 0; i < 4096u; i++)
                    a[i] = (int16_t)(4000 * sin(2 * M_PI * 50 * i / FS));
                f28_setup(&g, 32768, 127, 0);
                f28_run(&g, F_LADDER, a, o, 4096);
                for (i = 2048; i < 4096u; i++)
                    lo0 += (double)o[i] * o[i];
                f28_setup(&g, 32768, 127, 0);
                g.xamt = 65536;
                f28_run(&g, F_LADDER, a, o, 4096);
                for (i = 2048; i < 4096u; i++)
                    lo1 += (double)o[i] * o[i];
                b0 = 10 * log10(lo0 + 1);
                b1 = 10 * log10(lo1 + 1);
            }
            {
                double pf = 0, tr;
                for (hz = 300; hz < 12000; hz *= 1.03) {
                    double gg = f28_sine_gain(F_ACID, 32768, 127, hz);
                    if (gg > pk) {
                        pk = gg;
                        pf = hz;
                    }
                }
                tr = f28_sine_gain(F_ACID, 32768, 127, pf / 4);
                printf("  LADDER at full resonance, 50 Hz: BASS COMP 0 -> 127 %+.1f dB; ACID's peak (%.0f Hz) %+.1f dB "
                       "over 2 octaves below\n", b1 - b0, pf, 20 * log10(pk / tr));
                if (b1 - b0 < 6 || pk < tr * 2.5) {
                    printf("FAIL: BASS COMP / ACID's resonance\n");
                    fails++;
                }
            }
        }
        {   /* 1.0: ENV AMT. At 0 the amp EG changes nothing; up, it opens the cutoff (the hit's start brighter); a
             * switch away and back starts from rest */
            svf_t a, b;
            static int16_t sq[4096], oa[4096], ob[4096];
            uint32_t ty2, same = 1;
            double e0 = 0, e1 = 0;
            for (i = 0; i < 4096u; i++)
                sq[i] = (int16_t)(i & 64u ? 8000 : -8000);
            for (ty2 = F_ACID; ty2 <= F_MS20H; ty2++) {
                f28_setup(&a, 16384, 90, 0);
                f28_setup(&b, 16384, 90, 0);
                for (i = 0; i + BLOCK <= 4096u; i += BLOCK) {
                    memcpy(oa + i, sq + i, BLOCK * 2);
                    memcpy(ob + i, sq + i, BLOCK * 2);
                    a.eg = 65536 - (int32_t)(i * 16);    /* (a decay) */
                    b.eg = 0;
                    svf_block(&a, ty2, oa + i);
                    svf_block(&b, ty2, ob + i);
                }
                same &= !memcmp(oa, ob, sizeof oa);
            }
            f28_setup(&a, 16384, 90, 0);
            a.xamt = 40000;
            for (i = 0; i + BLOCK <= 4096u; i += BLOCK) {
                memcpy(oa + i, sq + i, BLOCK * 2);
                a.eg = i < 2048u ? 65536 - (int32_t)(i * 32) : 0;
                svf_block(&a, F_ACID, oa + i);
            }
            for (i = 0; i < 1024u; i++) {                /* (its first and last quarter's change sample to sample) */
                e0 += fabs((double)oa[i + 1] - oa[i]);
                e1 += fabs((double)oa[3072 + i + 1] - oa[3072 + i]);
            }
            printf("  ENV AMT: at 0 the EG %s; up, the hit %.1f x brighter than its tail\n", same ? "changes nothing" : "CHANGES IT",
                   e0 / (e1 + 1));
            if (!same || e0 < e1 * 1.5) {
                printf("FAIL: ENV AMT\n");
                fails++;
            }
            for (ty2 = F_ACID; ty2 <= F_LADDER; ty2++) { /* (loud, to LP a block, back, silent: from rest) */
                int32_t pk = 0;
                f28_setup(&a, 50000, 127, 0);
                f28_run(&a, ty2, sq, oa, 1024);
                memset(ob, 0, sizeof ob);
                f28_run(&a, F_LP, ob, oa, 64);
                f28_run(&a, ty2, ob, oa, 1024);
                for (i = 0; i < 1024u; i++)
                    pk = abs(oa[i]) > pk ? abs(oa[i]) : pk;
                if (pk > 2) {
                    printf("FAIL: %s rings on after a switch (%d)\n", FT_NAMES[ty2 - F_LP4], pk);
                    fails++;
                }
            }
        }
        {                                                /* a switch: COMB's line cleared, nothing left ringing */
            int32_t pk = 0;
            for (i = 0; i < 1024u; i++)
                in[i] = (int16_t)(i & 32u ? 12000 : -12000);
            f28_setup(&f, 32768, 127, 0);
            f28_run(&f, F_COMBP, in, o, 1024);
            memset(in, 0, sizeof in);
            f28_run(&f, F_LP, in, o, 64);
            f28_run(&f, F_COMBP, in, o, 1024);
            for (i = 0; i < 1024u; i++)
                pk = abs(o[i]) > pk ? abs(o[i]) : pk;
            if (pk) {
                printf("FAIL: COMB rings on after a switch (%d)\n", pk);
                fails++;
            }
            in[0] = 20000;
        }
        {                                                /* full resonance and drive, loud: bounded, then silence */
            for (ty = F_LP4; ty <= F_LAST; ty++) {
                int32_t pk = 0;
                for (i = 0; i < 8192u; i++)
                    in[i] = (int16_t)(i < 4096u ? (i & 16u ? 32767 : -32768) : 0);
                f28_setup(&f, 60000, 127, 0);
                f.drive = 1638 + 6 * 4096;
                f28_run(&f, ty, in, o, 8192 + 8192);
                if (flt_char(ty)) {                      /* (1.0: theirs a slow, low tail (-60 dB at 0.2 s): at ~0.9 s) */
                    static int16_t z[16384];
                    f28_run(&f, ty, z, o, 16384);
                    f28_run(&f, ty, z, o, 16384);
                }
                for (i = 16384 - 2048; i < 16384u; i++)
                    pk = abs(o[i]) > pk ? abs(o[i]) : pk;
                if (pk > 8) {
                    printf("FAIL: %s still sounds 0.2 s after loud input (%d)\n", FT_NAMES[ty - F_LP4], pk);
                    fails++;
                }
            }
            memset(in, 0, sizeof in);
            in[0] = 20000;
        }
        if (morph_mix(PAR_FILTER_TYPE_1, 0, 9, 127) != 0 || morph_mix(PAR_FILTER_TYPE_1, 0, 9, 128) != 9 ||
            morph_mix(PAR_FILTER_TYPE_1, 0, 6, 128) != 3 || morph_mix(PAR_FILTER_TYPE_1, 12, 3, 200) != 3) {
            printf("FAIL: morph of the filter type\n");
            fails++;
        }
    }
    {   /* 27a: the INSERT. Each type at full AMOUNT, feedback either way: bounded, and silent 1.5 s after loud input;
         * AMOUNT 0: the input bit for bit; then what each does */
        static const char *const IN[NINS] = {"OFF", "PHASER", "FLANGER", "CHORUS", "TREMOLO", "AUTOPAN", "SHIFTER",
                                             "RING"};
        static int16_t in[FS * 2], o[FS * 2];
        uint32_t ty, i, c, j;
        printf("phase 27a: the insert\n");
        for (j = 1; j < 3u; j++)                     /* (1.0: SHIFTER and RING, the others dropped) */
            for (ty = INS_LIVE[j], c = 0; c <= 127u; c += 127u) {
                int32_t pk = 0;
                for (i = 0; i < FS * 2u; i++)
                    in[i] = (int16_t)(i < 8192u ? (i & 32u ? 32767 : -32768) : 0);
                i27_run(ty, 127, 100, c, in, o, FS * 2u, 1);
                for (i = FS * 2u - 4096u; i < FS * 2u; i++)
                    pk = abs(o[i]) > pk ? abs(o[i]) : pk;
                if (pk > 8) {
                    printf("FAIL: %s (c %u) still sounds 1.5 s after loud input (%d)\n", IN[ty], c, pk);
                    fails++;
                }
                for (i = 0; i < 8192u; i++)
                    in[i] = (int16_t)((i * 7919u) & 0x7FFFu) - 16384;
                i27_run(ty, 0, 100, c, in, o, 8192, 1);
                if (memcmp(in, o, 8192 * sizeof o[0])) {
                    printf("FAIL: %s at AMOUNT 0 isn't the input\n", IN[ty]);
                    fails++;
                }
            }
        {   /* SHIFTER: 1 kHz up 500 Hz (b 127) and down (b 1): the shifted line well above the rest */
            double up, dn, mid, r[2];
            for (c = 0; c < 2u; c++) {
                i27_sine(in, 16384, 1000, 8000);
                i27_run(INS_SHIFT, 127, c ? 1 : 127, 64, in, o, 16384, 1);
                up = f28_mag(o + 8192, 8192, 1500);
                dn = f28_mag(o + 8192, 8192, 500);
                mid = f28_mag(o + 8192, 8192, 1000);
                r[c] = (c ? dn : up) / ((c ? up : dn) + mid + 1);
            }
            printf("  shifter: 1 kHz +500 Hz %.0fx the rest, -500 Hz %.0fx\n", r[0], r[1]);
            if (r[0] < 30 || r[1] < 30) {
                printf("FAIL: the shifter's line isn't clear of the rest\n");
                fails++;
            }
        }
        {   /* RING (c 0): 1 kHz against 226 Hz (b 64): the sidebands, no carrier; AM (c 127): the carrier too */
            double sb, car, car_am;
            i27_sine(in, 16384, 1000, 8000);
            i27_run(INS_RING, 127, 64, 0, in, o, 16384, 1);
            sb = f28_mag(o + 8192, 8192, 1000 + 20 * pow(2, 64 * 7 / 127.0)) + f28_mag(o + 8192, 8192, 1000 - 20 * pow(2, 64 * 7 / 127.0));
            car = f28_mag(o + 8192, 8192, 1000);
            i27_run(INS_RING, 127, 64, 127, in, o, 16384, 1);
            car_am = f28_mag(o + 8192, 8192, 1000);
            if (car * 20 > sb || car_am * 4 < sb) {
                printf("FAIL: RING's sidebands %.0f, carrier %.0f (AM %.0f)\n", sb, car, car_am);
                fails++;
            }
        }
        memset(&ins[5], 0, sizeof ins[5]);
    }
    {   /* 0.15 (17c): D1-D3 render on the second core while core 0 renders SN-HH. A voice must touch only its own
         * state, so the output is the same, bit for bit, whichever group renders first: the beat with MODAL on D1,
         * P PARTCL on D2 (1.0: was P NOISE) and D3 on the noise wave (the generators that were shared), NOISE on CY
         * (1.0: its own noise), in two processes */
        int fd[2];
        pid_t pid;
        uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
        uint32_t side, i;
        par_set(PAR_MODEL1, MD_MODAL);
        par_set(PAR_MODEL1 + 1, MD_PL0 + 10);
        par_set(PAR_OSC_WAVE_DRUM3, W_NOISE);
        par_set(PAR_MODEL1 + 4, MD_NOISE);
        par_set(PAR_MODEL1 + 3, MD_MC0 + 2);             /* (24: MICRO on SN (CRUNCH) and the hats (DUST), with */
        par_set(PAR_MODEL1 + 5, MD_MC0 + 7);             /* .. JITTER and REVERSE: their noise their own too) */
        par_set(PAR_MMAT1 + 3, 90);
        par_set(PAR_MEXC1 + 5, 64);
        par_set(PAR_INS1 + 1, INS_RING);                 /* (27a: inserts' state their own too) */
        par_set(PAR_INS1 + 3, INS_SHIFT);
        par_set(PAR_INSC1 + 3, 100);
        par_set(PAR_INS1 + 4, INS_SHIFT);
        par_set(PAR_L2AMT1 + 1, 100);                    /* (25b: and LFO 2's) */
        par_set(PAR_L2TGT1 + 1, NUM_MOD_TARGETS + 2u);   /* (TILT: the reverb test after starts silent) */
        for (k = 0; k < 16; k += 3)
            main_set(&pat[0], 1, k, 1);
        for (k = 1; k < 16; k += 4)
            main_set(&pat[0], 2, k, 1);
        if (pipe(fd) != 0 || (pid = fork()) < 0) {
            perror("fork");
            return 1;
        }
        side = pid == 0;
        voices_swap = (uint8_t)side;
        seq_start(1);
        for (k = 0; k < 4u; k++) {
            frames = render(FS);
            for (i = 0; i < frames * 2u; i++)
                h[side] = (h[side] ^ (uint16_t)out[i]) * 1099511628211ull;
        }
        seq_stop_now();
        if (side) {
            if (write(fd[1], &h[1], sizeof h[1]) != (ssize_t)sizeof h[1])
                _exit(2);
            _exit(0);
        }
        if (read(fd[0], &h[1], sizeof h[1]) != (ssize_t)sizeof h[1])
            h[1] = ~h[0];
        waitpid(pid, 0, 0);
        printf("  split: D1-D3 first %016llx, SN-HH first %016llx\n", (unsigned long long)h[0],
               (unsigned long long)h[1]);
        if (h[0] != h[1]) {
            printf("FAIL: the output depends on which voices render first\n");
            fails++;
        }
        /* the models' limit: two play; a third plays as an LXR voice; on one core, one */
        par_set(PAR_MODEL1 + 2, MD_PL0);
        render(BLOCK);
        if (mdl[0].type != MD_MODAL || mdl[1].type != MD_PL0 + 10 || mdl[2].type != 0 || mdl[2].want != MD_PL0) {
            printf("FAIL: the third model plays\n");
            fails++;
        }
        par_set(PAR_MODEL1, 0);
        render(BLOCK);
        if (mdl[2].type != MD_PL0) {
            printf("FAIL: D3's model doesn't play when D1's goes\n");
            fails++;
        }
        model_cap = 1;
        render(BLOCK);
        if (mdl[1].type != MD_PL0 + 10 || mdl[2].type != 0) {
            printf("FAIL: one core: more than one model plays\n");
            fails++;
        }
        model_cap = 2;
        par_set(PAR_MODEL1 + 1, 0);
        par_set(PAR_MODEL1 + 2, 0);
        par_set(PAR_MODEL1 + 3, 0);
        par_set(PAR_MODEL1 + 4, 0);
        par_set(PAR_MODEL1 + 5, 0);
        par_set(PAR_MEXC1 + 5, 0);
        par_set(PAR_INS1 + 1, INS_OFF);
        par_set(PAR_INS1 + 3, INS_OFF);
        par_set(PAR_INSC1 + 3, 64);
        par_set(PAR_INS1 + 4, INS_OFF);
        par_set(PAR_L2AMT1 + 1, 0);
        par_set(PAR_L2TGT1 + 1, 0);
    }
    {   /* 18b: the reverb runs a block behind (on core 0 while the second core renders), the delay reads early when it
         * can. Every voice through the FX bus (DELAY, a time change half way) and into the reverb: the delay's output
         * must be the same bit for bit as with the effects after the mixer, and the reverb's return the same two
         * blocks later (Phase 10f: the mixer a block behind the voices, the reverb on its last send) */
        static int16_t a[FS * 2 * 3], b[FS * 2 * 3], c[FS * 2 * 3];
        uint8_t vol[NVOICE];
        uint32_t v, i, n = FS * 3 / BLOCK * BLOCK, bad = 0, clip = 0, wet = 0;
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_FX_SEND1 + v, 127);
            par_set(PAR_REV_SEND1 + v, 0);
        }
        par_set(PAR_FX_DELAY_TYPE, DELAY_NORMAL);    /* (1.0: the init kit's MODE is OFF) */
        par_set(PAR_DLY_LEVEL, 64);                  /* (1.0: the always-on delay) */
        par_set(PAR_DLY_TIME, 100);
        par_set(PAR_DLY_FB, 90);
        master_q12 = 1024;
        render_child(1, 100, a, n);
        render_child(0, 100, b, n);
        for (i = 0; i < n * 2u; i++)
            bad += a[i] != b[i];
        d_early_blocks = 0;
        render(64u * BLOCK);
        printf("  delay, reads early: %u samples differ (%u of 64 blocks read early)\n", bad, d_early_blocks);
        if (bad || !d_early_blocks) {
            printf("FAIL: the delay sounds different with its reads early\n");
            fails++;
        }
        par_set(PAR_FX_TYPE, FX_OFF);
        par_set(PAR_DLY_LEVEL, 0);
        master_q12 = 4096;                           /* (unity: the output is the mix, so the dry run subtracts) */
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_FX_SEND1 + v, 0);
            par_set(PAR_REV_SEND1 + v, 90);
            vol[v] = par[PAR_VOL1 + v];
            par_set(PAR_VOL1 + v, 40);               /* (no clipping: the dry run must subtract) */
        }
        for (k = 0; k < NREV; k++) {
            par_set(PAR_REV_TYPE, k);
            render_child(1, 100, a, n);              /* the reverb as before */
            render_child(0, 100, b, n);              /* two blocks behind (Phase 10f) */
            render_child(0, 0, c, n);                /* the dry sound */
            for (i = bad = 0; i < n * 2u; i++) {
                int32_t now = i >= 4u * BLOCK ? a[i - 4u * BLOCK] - c[i - 4u * BLOCK] : 0, late = b[i] - c[i];
                clip += iabs(a[i]) >= 32767 || iabs(b[i]) >= 32767;
                wet += late != 0;
                bad += now != late;
            }
            printf("  reverb %u two blocks behind: %u samples differ (%u wet)\n", k, bad, wet);
            if (bad || clip || !wet) {
                printf("FAIL: the reverb's return isn't the same two blocks later%s\n", clip ? " (it clipped)" : "");
                fails++;
            }
            wet = clip = 0;
        }
        par_set(PAR_REV_TYPE, 0);
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_REV_SEND1 + v, 0);
            par_set(PAR_VOL1 + v, vol[v]);
        }
        master_q12 = 4096;
    }
    {   /* 1.0: the delay's BAND: OFF as before; on, a howl at its frequency that holds (the soft clip), in each type */
        static const char *const TN[4] = {"MONO", "PING PONG", "TAPE", "TAPE PP"};
        uint32_t ty, lv = par[PAR_DLY_LEVEL], tm = par[PAR_DLY_TIME], f = par[PAR_DLY_FB], tn = par[PAR_DLY_TONE];
        if (getenv("BANDSWEEP"))
            for (ty = 0; ty < 4u; ty++) {
                uint32_t fbv;
                for (fbv = 20; fbv <= 127; fbv += 10) {
                    int32_t pk;
                    double t0, t1;
                    band_run(ty, 0, fbv, &pk, &t0, 0);
                    band_run(ty, 64, fbv, &pk, &t1, 0);
                    printf("  sweep %s FB %3u: off %8.1f on %8.1f\n", TN[ty], fbv, t0, t1);
                }
            }
        for (ty = 0; ty < 4u; ty++) {
            int32_t pk0, pk1;
            double t0, t1, fo;
            band_run(ty, 0, 127, &pk0, &t0, 0);
            band_run(ty, 64, 127, &pk1, &t1, &fo);
            printf("  delay band %-9s FB 127: tail rms %7.1f off, %7.1f on (peak %d), the centre %+.1f dB\n", TN[ty],
                   t0, t1, pk1, fo);
            if (t1 < 2000 || (ty < 2u && t1 < 2 * t0) || pk1 > 65536 || fo < 10) {   /* (TAPE: howls anyway, at 115%) */
                printf("FAIL: the delay's BAND doesn't howl at its frequency at the top of FEEDBACK (%s)\n", TN[ty]);
                fails++;
            }
            band_run(ty, 0, 60, &pk0, &t0, 0);       /* (halfway: a colour, no howl) */
            band_run(ty, 64, 60, &pk1, &t1, 0);
            if (t1 > 4 * t0 + 50) {
                printf("FAIL: the delay's BAND howls at FEEDBACK 60 (%s: %.1f, %.1f off)\n", TN[ty], t1, t0);
                fails++;
            }
            band_run(ty, 127, 127, &pk1, &t1, 0);
            if (pk1 > 65536) {
                printf("FAIL: the delay's BAND at the top, FEEDBACK full: not held (%s, peak %d)\n", TN[ty], pk1);
                fails++;
            }
        }
        par_set(PAR_FX_DELAY_TYPE, 0);
        par_set(PAR_DLY_LEVEL, lv);
        par_set(PAR_DLY_TIME, tm);
        par_set(PAR_DLY_FB, f);
        par_set(PAR_DLY_TONE, tn);
        memset(fxbuf, 0, sizeof fxbuf);
        memset(&tp, 0, sizeof tp);
    }
    printf(fails ? "render_test: %u FAILED\n" : "render_test: ok\n", fails);
    return fails != 0;
}
