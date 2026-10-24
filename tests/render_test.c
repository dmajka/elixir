/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host test: render a beat with the init kit (and every voice alone) to WAV, check the levels and that
 * every voice sounds, the clock's tempo and the MIDI clock out.
 *   cc -O2 -I build/gen -I firmware/src tests/render_test.c -o build/render_test && build/render_test build/ */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
    printf(fails ? "render_test: %u FAILED\n" : "render_test: ok\n", fails);
    return fails != 0;
}
