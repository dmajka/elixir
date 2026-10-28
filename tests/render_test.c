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
                par_set(PAR_FX_P1, 70);
                n = render(BLOCK);
                par_set(PAR_FX_P1, 40);
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
                                                 "P CHORD", "P SPEECH", "P SWARM", "P NOISE", "P PARTCL", "P STRING",
                                                 "P MODAL", "P BD", "P SD", "P HH"};
        uint32_t e;
        par_set(PAR_MDECAY1, 64);
        par_set(PAR_MDAMP1, 64);
        par_set(PAR_MMAT1, 64);
        for (e = 0; e < 16u; e++) {
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
        par_set(PAR_MODEL1, 0);
    }
    {   /* 0.15 (17c): D1-D3 render on the second core while core 0 renders SN-HH. A voice must touch only its own
         * state, so the output is the same, bit for bit, whichever group renders first: the beat with MODAL on D1,
         * P NOISE on D2 and D3 on the noise wave (the generators that were shared), in two processes */
        int fd[2];
        pid_t pid;
        uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
        uint32_t side, i;
        par_set(PAR_MODEL1, MD_MODAL);
        par_set(PAR_MODEL1 + 1, MD_PL0 + 9);
        par_set(PAR_OSC_WAVE_DRUM3, W_NOISE);
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
        if (mdl[0].type != MD_MODAL || mdl[1].type != MD_PL0 + 9 || mdl[2].type != 0 || mdl[2].want != MD_PL0) {
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
        if (mdl[1].type != MD_PL0 + 9 || mdl[2].type != 0) {
            printf("FAIL: one core: more than one model plays\n");
            fails++;
        }
        model_cap = 2;
        par_set(PAR_MODEL1 + 1, 0);
        par_set(PAR_MODEL1 + 2, 0);
    }
    {   /* 18b: the reverb runs a block behind (on core 0 while the second core renders), the delay reads early when it
         * can. Every voice through the FX bus (DELAY, a time change half way) and into the reverb: the delay's output
         * must be the same bit for bit as with the effects after the mixer, and the reverb's return the same one
         * block later */
        static int16_t a[FS * 2 * 3], b[FS * 2 * 3], c[FS * 2 * 3];
        uint8_t vol[NVOICE];
        uint32_t v, i, n = FS * 3 / BLOCK * BLOCK, bad = 0, clip = 0, wet = 0;
        for (v = 0; v < NVOICE; v++) {
            par_set(PAR_FX_SEND1 + v, 127);
            par_set(PAR_REV_SEND1 + v, 0);
        }
        par_set(PAR_FX_TYPE, FX_DELAY);
        par_set(PAR_FX_DRY_WET, 50);
        par_set(PAR_FX_P1, 100);
        par_set(PAR_FX_P2, 60);
        par_set(PAR_FX_P4, 90);
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
            render_child(0, 100, b, n);              /* a block behind */
            render_child(0, 0, c, n);                /* the dry sound */
            for (i = bad = 0; i < n * 2u; i++) {
                int32_t now = i >= 2u * BLOCK ? a[i - 2u * BLOCK] - c[i - 2u * BLOCK] : 0, late = b[i] - c[i];
                clip += iabs(a[i]) >= 32767 || iabs(b[i]) >= 32767;
                wet += late != 0;
                bad += now != late;
            }
            printf("  reverb %u a block behind: %u samples differ (%u wet)\n", k, bad, wet);
            if (bad || clip || !wet) {
                printf("FAIL: the reverb's return isn't the same one block later%s\n", clip ? " (it clipped)" : "");
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
    printf(fails ? "render_test: %u FAILED\n" : "render_test: ok\n", fails);
    return fails != 0;
}
