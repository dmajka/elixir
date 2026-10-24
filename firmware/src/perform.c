/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: PERF's effects (0.4). The panel (ui.c) holds or latches them in pf.on, the audio ISR applies them at
 * each block's start (perf_block) and on the master (perf_master):
 *   FILL       the steps play whatever their probability (seq.c sq.fill)
 *   STUTTER    the sequencer's play position loops at the REPEAT length (1/4 .. 1/32) from the next point of that
 *              grid; let go, it goes on where it would have been (seq.c sq.stut_*)
 *   FREEZE     the FX bus's DELAY loops what it holds: its input off, its feedback 100% (fxbus.c fx.freeze)
 *   THROW REV  the throw voice's (the selected track's when the key went down) reverb send at 127 ..
 *   THROW FX   .. its FX send at 127; the tail rings out after (voices.c mix_voice)
 *   FB PUSH    the FX bus's feedback (DELAY ~0.95, PHASER and FLANGER their most) (fxbus.c fx.push)
 *   TAPE STOP  the tempo and every voice's pitch slow down together to a stop over TAPE time, then the sound fades;
 *              let go, they spin back up in 0.25 s (voices.c tape_q16)
 *   LP / HP SWEEP  the DJ filter pushed towards its low-pass (high-pass) end over a bar, back in a quarter of that
 * The DJ FILTER (PERF's knob, 64 off): the master through the voices' filter (dsp.c svf), low-pass left of the
 * middle, high-pass right, faded in from the middle (no click where it turns from one to the other).
 * MACROS: four knobs, each a parameter between its MIN and MAX (ui.c); they and REPEAT and TAPE are the project's
 * (project.c). */

enum { PF_FILL, PF_STUT, PF_FREEZE, PF_THROW_R, PF_THROW_F, PF_PUSH, PF_TAPE, PF_SWEEP_LP, PF_SWEEP_HP, NPF };
#define NMACRO 4
static struct {
    volatile uint16_t on;                            /* the effects held or latched (bit PF_*) */
    volatile uint8_t throw_v;                        /* the voice THROW sends */
    uint8_t dj;                                      /* the DJ filter's knob, 64 off */
    int32_t sweep;                                   /* the sweeps: -65536 (low-pass end) .. 65536 (high-pass) */
    int32_t dj_d;                                    /* the filter's position in use, -65536 .. 65536 */
    uint8_t dj_type;                                 /* the type the filter ran last (a change: its state reset) */
    svf_t dj_f[2];
} pf;
/* the project's: the macros and the effects' settings */
static struct {
    struct { uint16_t par; uint8_t lo, hi, val; } m[NMACRO];   /* par 0: none */
    uint8_t repeat;                                  /* the STUTTER length: 1/4 1/8 1/16 1/32 */
    uint8_t tape;                                    /* the TAPE STOP time, 0..127: 0.2 .. 4 s */
} pfs;
static const uint8_t REPEAT_TICKS[4] = {96, 48, 24, 12};

static void perf_defaults(void)
{
    memset(&pfs, 0, sizeof pfs);
    pfs.repeat = 2;                                  /* 1/16 */
    pfs.tape = 64;                                   /* ~0.9 s */
}
static void perf_init(void)
{
    uint32_t c;
    perf_defaults();
    pf.dj = 64;
    for (c = 0; c < 2u; c++) {
        svf_init(&pf.dj_f[c]);
        pf.dj_f[c].q = 39322;                        /* (0.6: a little resonance) */
    }
}

/* the audio ISR, a block's start: the held effects into the engine */
static void perf_block(void)
{
    uint32_t on = pf.on, v;
    int32_t target, step;
    sq.fill = (on >> PF_FILL) & 1u;
    sq.stut_req = (on >> PF_STUT) & 1u;
    sq.stut_len = REPEAT_TICKS[pfs.repeat & 3u];
    fx.freeze = (on >> PF_FREEZE) & 1u;
    fx.push = (on >> PF_PUSH) & 1u;
    for (v = 0; v < NVOICE; v++) {
        vc[v].throw_rev = (uint8_t)(v == pf.throw_v && ((on >> PF_THROW_R) & 1u));
        vc[v].throw_fx = (uint8_t)(v == pf.throw_v && ((on >> PF_THROW_F) & 1u));
    }
    if ((on >> PF_TAPE) & 1u) {                      /* TAPE STOP: linear in speed over its time (T ms) */
        int32_t t_ms = (int32_t)(((int64_t)200 * exp2_q16(pfs.tape * 2232)) >> 16);   /* 200 x 20^(t / 127) */
        step = (int32_t)(95109 / (uint32_t)(t_ms > 1 ? t_ms : 1));   /* 65536 x BLOCK / FS per ms */
        tape_q16 = tape_q16 > (uint32_t)step ? tape_q16 - (uint32_t)step : 0u;
    } else if (tape_q16 < 65536u) {
        tape_q16 = tape_q16 + 380u < 65536u ? tape_q16 + 380u : 65536u;   /* (back in 0.25 s) */
    }
    /* the sweeps: a bar to their end, back in a quarter of it. A bar is 384 ticks of tick_q16 / 65536 samples:
     * 65536 x BLOCK / (384 x tick_q16 / 65536) = 2^38 / 384 / tick_q16 = (2^32 / 6) / tick_q16 (32-bit divide) */
    step = (int32_t)(715827883u / (sq.tick_q16 ? sq.tick_q16 : 1u));
    target = ((on >> PF_SWEEP_HP) & 1u ? 65536 : 0) - ((on >> PF_SWEEP_LP) & 1u ? 65536 : 0);
    if (pf.sweep < target)
        pf.sweep = pf.sweep + step * (pf.sweep < 0 ? 4 : 1) < target ? pf.sweep + step * (pf.sweep < 0 ? 4 : 1)
                                                                        : target;
    else if (pf.sweep > target)
        pf.sweep = pf.sweep - step * (pf.sweep > 0 ? 4 : 1) > target ? pf.sweep - step * (pf.sweep > 0 ? 4 : 1)
                                                                        : target;
}

/* the master (engine_block): the DJ filter, the tape's fade; l, r: the block, saturated */
static void perf_master(int16_t *l, int16_t *r)
{
    int32_t d = clamp32(((int32_t)pf.dj - 64) * 1040 + pf.sweep, -65536, 65536), a = iabs(d), w;
    uint32_t i, c, type;
    int16_t f[BLOCK];
    pf.dj_d = d;
    if (a > 0) {
        type = d < 0 ? F_LP : F_HP;
        w = a >= 8192 ? 32768 : a * 4;               /* (faded in over the first eighth: no click at the middle) */
        if (type != pf.dj_type) {
            svf_reset(&pf.dj_f[0]);
            svf_reset(&pf.dj_f[1]);
            pf.dj_type = (uint8_t)type;
        }
        for (c = 0; c < 2u; c++) {
            int16_t *x = c ? r : l;
            svf_t *s = &pf.dj_f[c];
            uint32_t fv = d < 0 ? 65536u - (uint32_t)((a * 58982) >> 16) : (uint32_t)((a * 45875) >> 16);
            if (s->fval != fv) {
                s->fval = fv;
                s->dirty = 1;
            }
            memcpy(f, x, sizeof f);
            svf_block(s, type, f);
            for (i = 0; i < BLOCK; i++)
                x[i] = (int16_t)sat16(x[i] + (((f[i] - x[i]) * w) >> 15));
        }
    }
    if (tape_q16 < 16384u) {                         /* the tape's last quarter of speed: fades to silence */
        int32_t g = (int32_t)tape_q16 * 2;
        for (i = 0; i < BLOCK; i++) {
            l[i] = (int16_t)((l[i] * g) >> 15);
            r[i] = (int16_t)((r[i] * g) >> 15);
        }
    }
}
