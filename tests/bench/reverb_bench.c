/* Elixir: the reverb's models (1.4, docs/completed/TO_DO_REVERBS.md step 0): a hash per model (the bit-identical check) and
 * WAVs to listen to and measure.
 *   cc -O2 -w -I build/gen -I firmware/src -I tests tests/bench/reverb_bench.c -o build/reverb_bench
 *   build/reverb_bench                   the hashes (one per model the case starts on)
 *   (-DREV_LOFI=1: LOFI as a fifth model, and build/reverb_bench lofi DIR TAG its listening set, -DLF_*: its variants)
 *   build/reverb_bench wav build/rev/    the WAVs (the directory must exist), then
 *   .venv/bin/python tools/reverb_measures.py build/rev/
 * The hash: each case a model, SIZE, DAMP and LEVEL at random, a few hundred blocks of noise and hits (some loud
 * enough to clip a line), then one at a time: SIZE, DAMP, LEVEL moved, SPACE held and let go, a change of model and
 * back (rev_switch's fade), silence until it sleeps (255 blocks) and a hit that wakes it. The return (rev_ret) and
 * rev_ok hashed every block.
 * The WAVs (44.1 kHz): ir_<MODEL>_s<SIZE>.wav, the return (stereo, 32-bit float, LEVEL at 1) to a one-sample
 * impulse of 4.0 (the send's sum may pass full scale; PLATE and HALL clamp it at 8), 6 s; irq_ the same to 0.5 (the
 * loops' rounding shortens a quiet tail: the measures compare them); beat_<MODEL>_s<SIZE>.wav, the init kit's beat (render_test's, twice, then 3 s for the tail;
 * the reverb's own return off) with half of it sent to the reverb (as every voice's REV SEND at 64), dry + the
 * return at LEVEL 100, 16-bit; dry.wav the beat alone. SIZE 32, 64, 100; DAMP 48 (the init kit's). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "engine_host.h"

static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
static uint32_t rs = 12345u;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static uint8_t knob(void)
{
    static const uint8_t K[] = {0, 1, 20, 37, 50, 63, 64, 65, 77, 90, 101, 115, 126, 127};
    uint32_t r = rnd() >> 16;
    return r & 1 ? K[(r >> 1) % sizeof K] : (uint8_t)((r >> 4) % 128u);
}
static uint64_t H = 1469598103934665603ull;
static void hash32(const int32_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        H ^= (uint32_t)b[i];
        H *= 1099511628211ull;
    }
}
static const char *const NAME[5] = {"ROOM", "SPRING", "HALL", "PLATE", "LOFI"};

/* the reverb from silence, model m (as after a model change; the states' memsets: a fresh boot's zeros) */
static void rv_reset(uint32_t m, uint32_t size, uint32_t damp, int32_t level)
{
    memset(&rv, 0, sizeof rv);
    memset(&hl, 0, sizeof hl);
    memset(&plt, 0, sizeof plt);
    memset(&spr, 0, sizeof spr);
#if REV_LOFI
    memset(&lf, 0, sizeof lf);
#endif
    rev_clear();
    rev_ok[0] = rev_ok[1] = 0;
    space_on = 0;
    rv.type = rv.rtype = (uint8_t)m;
    rv.size = (uint8_t)size;
    rv.damp = (uint8_t)damp;
    rv.level = rv.lvl_last = level;
}
static uint32_t rv_block(const int32_t *in)   /* one block through it (rev_ret[0]); fed: any send */
{
    uint32_t i, fed = 0;
    for (i = 0; i < BLOCK; i++)
        fed |= in[i] != 0;
    reverb_process(in, fed, 0);
    return rev_ok[0];
}

/* the send: lvl 0 silence, 1 quiet noise, 2 loud noise, 3 hits (a decaying burst every 6 blocks), 4 past full scale */
static void sig_in(int32_t *b, uint32_t blk, uint32_t lvl)
{
    static const int32_t L[] = {0, 600, 12000, 3000, 60000};
    for (uint32_t i = 0; i < BLOCK; i++) {
        int32_t v = (int32_t)(rnd() >> 16) - 32768;
        v = (int32_t)((int64_t)v * L[lvl] / 32768);
        if (lvl == 3u && blk % 6u == 0u)
            v += (32767 - (int32_t)i * 400) * ((i & 4u) ? 1 : -1);
        b[i] = v;
    }
}

#ifndef NOTHER
#define NOTHER NREV                       /* (the models a case switches among: 4 for 1.4's step-1 hashes) */
#endif
static uint32_t slept;                    /* cases that reached sleep (a check the hash covers it) */
static uint64_t bench_model(uint32_t m, uint32_t ncase)
{
    int32_t in[BLOCK];
    uint32_t c, b, ok;
    H = 1469598103934665603ull;
    for (c = 0; c < ncase; c++) {
        uint32_t lvl = 1u + c % 4u, other = (m + 1u + (rnd() >> 16) % (NOTHER - 1u)) % NOTHER;
        rv_reset(m, knob(), knob(), (int32_t)(rnd() >> 17));
        for (b = 0; b < 1610u; b++) {
            switch (b) {                             /* (one input at a time) */
            case 60: rv.size = knob(); break;
            case 90: rv.damp = knob(); break;
            case 120: rv.level = (int32_t)(rnd() >> 17); break;
            case 150: space_on = 1; break;
            case 190: space_on = 0; break;
            case 230: rv.type = (uint8_t)other; break;
            case 240: rv.type = (uint8_t)m; break;
            case 250: rv.size = (uint8_t)(rv.size ^ 64u); break;
            }
            sig_in(in, b, b < 260u ? (b >= 200u && b < 230u ? 0u : lvl) : b < 1600u ? 0u : 3u);
            ok = rv_block(in);
            if (b == 1599u && rv.idle == 255u)
                slept++;
            H ^= ok + 2u * rv.idle;
            H *= 1099511628211ull;
            if (ok) {
                hash32(rev_ret[0][0], BLOCK);
                hash32(rev_ret[0][1], BLOCK);
            }
        }
        if (c == 0 && rv.idle != 0u) {               /* (woken by the hit at 1600) */
            printf("FAIL: %s didn't wake\n", NAME[m]);
            exit(1);
        }
    }
    return H;
}

/* WAVs: stereo, 16-bit or 32-bit float */
static void wav_write(const char *path, const void *s, uint32_t frames, int fl)
{
    FILE *f = fopen(path, "wb");
    uint32_t bps = fl ? 4u : 2u, bytes = frames * 2u * bps, v;
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
    h = fl ? 3 : 1;
    fwrite(&h, 2, 1, f);
    h = 2;
    fwrite(&h, 2, 1, f);
    v = FS;
    fwrite(&v, 4, 1, f);
    v = FS * 2u * bps;
    fwrite(&v, 4, 1, f);
    h = (uint16_t)(2u * bps);
    fwrite(&h, 2, 1, f);
    h = (uint16_t)(8u * bps);
    fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(s, 2u * bps, frames, f);
    fclose(f);
}

#define BEAT (FS * 4u)                   /* the beat: 4 s (render_test's 2 s bar, twice) */
#define TAIL (FS * 3u)
#define IRLEN (FS * 6u)
static int16_t dry[(BEAT + TAIL) * 2u], mixo[(BEAT + TAIL) * 2u];
static float irb[IRLEN * 2u];

static void render_dry(void)
{
    int32_t blk[BLOCK * 2];
    uint32_t n = 0, i, k;
    engine_init();
    master_q12 = 4096;
    for (k = 0; k < 16; k += 4)                      /* render_test's beat */
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
    par_set(PAR_REV_LEVEL, 0);                       /* (the engine's own reverb out of it) */
    seq_start(1);
    while (n + BLOCK <= BEAT) {
        engine_block(blk);
        for (i = 0; i < BLOCK * 2u; i++)
            dry[2u * n + i] = (int16_t)sat16(blk[i]);
        n += BLOCK;
    }
}

static void wavs(const char *dir)
{
    static const uint8_t SZ[] = {32, 64, 100};
    char path[512];
    int32_t in[BLOCK];
    uint32_t m, z, n, i, q;
    render_dry();
    snprintf(path, sizeof path, "%sdry.wav", dir);
    wav_write(path, dry, BEAT + TAIL, 0);
    for (m = 0; m < NREV; m++)
        for (z = 0; z < sizeof SZ; z++) {
            for (q = 0; q < 2u; q++) {               /* (loud: 4.0, near the send's clamp; quiet: 0.5) */
                rv_reset(m, SZ[z], 48, 32767);
                for (n = 0; n + BLOCK <= IRLEN; n += BLOCK) {
                    for (i = 0; i < BLOCK; i++)
                        in[i] = n + i == 0u ? (q ? 16384 : 131072) : 0;
                    rv_block(in);
                    for (i = 0; i < BLOCK; i++) {
                        irb[2u * (n + i)] = rev_ok[0] ? rev_ret[0][0][i] / 32768.0f : 0.0f;
                        irb[2u * (n + i) + 1u] = rev_ok[0] ? rev_ret[0][1][i] / 32768.0f : 0.0f;
                    }
                }
                snprintf(path, sizeof path, "%s%s_%s_s%u.wav", dir, q ? "irq" : "ir", NAME[m], SZ[z]);
                wav_write(path, irb, IRLEN, 1);
            }
            rv_reset(m, SZ[z], 48, 25800);
            for (n = 0; n + BLOCK <= BEAT + TAIL; n += BLOCK) {
                for (i = 0; i < BLOCK; i++)
                    in[i] = (dry[2u * (n + i)] + dry[2u * (n + i) + 1u]) / 4;   /* (mono, half) */
                rv_block(in);
                for (i = 0; i < 2u * BLOCK; i++)
                    mixo[2u * n + i] = (int16_t)sat16(dry[2u * n + i] + (rev_ok[0] ? rev_ret[0][i & 1u][i >> 1] : 0));
            }
            snprintf(path, sizeof path, "%sbeat_%s_s%u.wav", dir, NAME[m], SZ[z]);
            wav_write(path, mixo, BEAT + TAIL, 0);
        }
    printf("WAVs in %s\n", dir);
}

#if REV_LOFI
/* LOFI's listening set (docs/TO_DO_REVERBS_2.md): the beat at SIZE 0 / 32 / 64 / 100 / 127, DAMP 10 and 110;
 * its IR (loud) at the same; sweep: the beat's first bar then SIZE swept 100 -> 20 -> 127 while it rings; havoc: the
 * beat with SPACE held over its second bar, then let go */
static void lofi_wavs(const char *dir, const char *tag)
{
    static const uint8_t SZ[] = {0, 32, 64, 100, 127}, DP[] = {10, 110};
    char path[512];
    int32_t in[BLOCK];
    uint32_t z, q, n, i, v;
    render_dry();
    for (z = 0; z < sizeof SZ; z++)
        for (q = 0; q < sizeof DP; q++) {
            rv_reset(4, SZ[z], DP[q], 32767);
            for (n = 0; n + BLOCK <= IRLEN; n += BLOCK) {
                for (i = 0; i < BLOCK; i++)
                    in[i] = n + i == 0u ? 32767 : 0;
                rv_block(in);
                for (i = 0; i < BLOCK; i++) {
                    irb[2u * (n + i)] = rev_ok[0] ? rev_ret[0][0][i] / 32768.0f : 0.0f;
                    irb[2u * (n + i) + 1u] = rev_ok[0] ? rev_ret[0][1][i] / 32768.0f : 0.0f;
                }
            }
            snprintf(path, sizeof path, "%s%sir_s%u_d%u.wav", dir, tag, SZ[z], DP[q]);
            wav_write(path, irb, IRLEN, 1);
            rv_reset(4, SZ[z], DP[q], 25800);
            for (n = 0; n + BLOCK <= BEAT + TAIL; n += BLOCK) {
                for (i = 0; i < BLOCK; i++)
                    in[i] = (dry[2u * (n + i)] + dry[2u * (n + i) + 1u]) / 4;
                rv_block(in);
                for (i = 0; i < 2u * BLOCK; i++)
                    mixo[2u * n + i] = (int16_t)sat16(dry[2u * n + i] + (rev_ok[0] ? rev_ret[0][i & 1u][i >> 1] : 0));
            }
            snprintf(path, sizeof path, "%s%sbeat_s%u_d%u.wav", dir, tag, SZ[z], DP[q]);
            wav_write(path, mixo, BEAT + TAIL, 0);
        }
    for (v = 0; v < 2u; v++) {                      /* (0: the sweep, 1: HAVOC) */
        rv_reset(4, 64, 64, 25800);
        for (n = 0; n + BLOCK <= BEAT + TAIL; n += BLOCK) {
            uint32_t t = n * 100u / FS;              /* (centiseconds) */
            int beat = v ? n < BEAT : n < BEAT / 2u;
            if (!v && t >= 200u) {                   /* (100 at 2.0 s -> 20 at 3.5 s -> 127 at 5.5 s) */
                rv.size = (uint8_t)(t < 350u ? 100u - (t - 200u) * 80u / 150u : t < 550u ? 20u + (t - 350u) * 107u / 200u : 127u);
            }
            if (v)
                space_on = n >= BEAT / 2u && n < BEAT;
            for (i = 0; i < BLOCK; i++)
                in[i] = beat ? (dry[2u * (n + i)] + dry[2u * (n + i) + 1u]) / 4 : 0;
            rv_block(in);
            for (i = 0; i < 2u * BLOCK; i++)
                mixo[2u * n + i] = (int16_t)sat16((beat ? dry[2u * n + i] : 0) + (rev_ok[0] ? rev_ret[0][i & 1u][i >> 1] : 0));
        }
        snprintf(path, sizeof path, "%s%s%s.wav", dir, tag, v ? "havoc" : "sweep");
        wav_write(path, mixo, BEAT + TAIL, 0);
    }
    printf("LOFI WAVs in %s (%s)\n", dir, tag);
}

#endif

int main(int argc, char **argv)
{
    uint32_t m, ncase = 40;
    tables_unpack();
#if REV_LOFI
    if (argc > 1 && !strcmp(argv[1], "lofi")) {
        lofi_wavs(argc > 2 ? argv[2] : "build/lofi/", argc > 3 ? argv[3] : "");
        return 0;
    }
#endif
    if (argc > 1 && !strcmp(argv[1], "wav")) {
        wavs(argc > 2 ? argv[2] : "build/rev/");
        return 0;
    }
    if (argc > 1)
        ncase = (uint32_t)atoi(argv[1]);
    for (m = 0; m < NREV; m++) {
        uint64_t t0 = now(), h;
        slept = 0;
        h = bench_model(m, ncase);
        printf("%-7s %016llx  %.1f us/case  (asleep before the last hit: %u of %u)\n", NAME[m], (unsigned long long)h,
               (now() - t0) / 1000.0 / ncase, slept, ncase);
    }
    return 0;
}
