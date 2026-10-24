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
 * (project.c).
 * 0.6: PERFORM 2, the second bank (the PERFORM 2 pages: ui.c), its settings the project's too (KPF2):
 *   DRILL      keys 1..7: the track's hits become ratchets of 2 .. 8 at random rates, velocities, pitches (seq.c)
 *   HALF       the sequencer at half speed, the voices as they were (seq.c sq.half)
 *   JITTER     each hit late by a random part of up to a 32nd (seq.c sq.jit, the held-back hits)
 *   CUTS       the master gated by a random 1/32 pattern, 0.7 ms fades
 *   SHORT      every voice's amp decay cut to a click
 *   PUMP       every voice but D1 ducked by D1's hits, back over an 8th (voices.c pump_g*)
 *   COMB       a tuned comb (its note, its feedback) on the master: drums ring at a pitch
 *   SPACE      the reverb held (its tail kept going), the dry sound dipped (voices.c space_on, reverb.c)
 *   CRUSH      the master's bits and sample rate down
 *   REVERSE    the last 1/16 .. 1/4 played backwards, again while held (a recorder: 0.25 s, mono, 22 kHz) */

enum { PF_FILL, PF_STUT, PF_FREEZE, PF_THROW_R, PF_THROW_F, PF_PUSH, PF_TAPE, PF_SWEEP_LP, PF_SWEEP_HP, NPF };
enum { PF_HALF = NPF, PF_JITTER, PF_CUTS, PF_SHORT, PF_PUMP, PF_COMB, PF_SPACE, PF_CRUSH, PF_REVERSE, NPF2 };
#define COMB_LEN 4096u                               /* the comb's line: down to ~11 Hz */
#define REC_LEN 5512u                                /* REVERSE's recorder: 0.25 s at 22.05 kHz */
#define SHORT_DEC 2097152u                           /* SHORT: the amp EG's decay rate (an eighth a block) */
static int16_t comb_buf[COMB_LEN] POOL;
static int16_t rec_buf[REC_LEN] POOL;
#define NMACRO 4
static struct {
    volatile uint32_t on;                            /* the effects held or latched (bit PF_*) */
    volatile uint8_t throw_v;                        /* the voice THROW sends */
    uint8_t dj;                                      /* the DJ filter's knob, 64 off */
    int32_t sweep;                                   /* the sweeps: -65536 (low-pass end) .. 65536 (high-pass) */
    int32_t dj_d;                                    /* the filter's position in use, -65536 .. 65536 */
    uint8_t dj_type;                                 /* the type the filter ran last (a change: its state reset) */
    svf_t dj_f[2];
    /* 0.6: PERFORM 2's states: CUTS (samples to the next 32nd, the gate, its gain Q15); PUMP (D1's hits seen, the
     * envelope Q15, rising); COMB (its write index, the loop's low-pass); CRUSH (the held sample, samples to the
     * next); REVERSE (the recorder's write index, half a sample; the part playing: its end, length, position in
     * 44.1 kHz samples, the mix Q15) */
    int32_t cut_n, cut_g;
    uint8_t cut_open;
    uint32_t pump_seen;
    int32_t pump_env;
    uint8_t pump_up;
    uint32_t comb_w;
    int32_t comb_lp;
    int32_t cr_l, cr_r, cr_n;
    uint32_t rec_w;
    int32_t rec_half;
    uint32_t rv_end, rv_len, rv_pos;
    int32_t rv_mix;
} pf;
/* the project's: the macros and the effects' settings */
static struct {
    struct { uint16_t par; uint8_t lo, hi, val; } m[NMACRO];   /* par 0: none */
    uint8_t repeat;                                  /* the STUTTER length: 1/4 1/8 1/16 1/32 */
    uint8_t tape;                                    /* the TAPE STOP time, 0..127: 0.2 .. 4 s */
    /* 0.6: PERFORM 2: DRILL's density, JITTER's most, CUTS' density, PUMP's depth, COMB's note (MIDI) and feedback,
     * CRUSH's amount (0..127 each, but the note), REVERSE's length (1/16 1/8 1/4) */
    uint8_t drill, jitter, cuts, pump, comb_note, comb_fb, crush, rev_len;
} pfs;
#define NPF2SET 8u                                   /* (KPF2: the bytes from drill) */
static const uint8_t REPEAT_TICKS[4] = {96, 48, 24, 12};

static void perf_defaults(void)
{
    memset(&pfs, 0, sizeof pfs);
    pfs.repeat = 2;                                  /* 1/16 */
    pfs.tape = 64;                                   /* ~0.9 s */
    pfs.drill = 80;
    pfs.jitter = 64;
    pfs.cuts = 50;
    pfs.pump = 100;
    pfs.comb_note = 45;                              /* A2 */
    pfs.comb_fb = 100;
    pfs.crush = 64;
    pfs.rev_len = 1;                                 /* 1/8 */
}
static void perf_init(void)
{
    uint32_t c;
    perf_defaults();
    pf.dj = 64;
    pf.cut_g = 32767;
    pf.cut_open = 1;
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
    sq.half = (on >> PF_HALF) & 1u;
    sq.jit = (on >> PF_JITTER) & 1u ? (uint8_t)(1u + pfs.jitter * 11u / 127u) : 0u;
    sq.drill_span = (uint8_t)(1u + pfs.drill * 6u / 127u);
    space_on = (on >> PF_SPACE) & 1u;
    if ((on >> PF_SHORT) & 1u)                       /* SHORT: every voice's decay a click, as long as it's on */
        for (v = 0; v < NVOICE; v++)
            if (vc[v].aeg.dec < SHORT_DEC)
                vc[v].aeg.dec = SHORT_DEC;
    pump_g0 = pump_g1;                               /* PUMP: D1's hit: down in 2 blocks, back over an 8th */
    if ((on >> PF_PUMP) & 1u) {
        if (trig_count[0] != pf.pump_seen)
            pf.pump_up = 1;
        if (pf.pump_up) {
            pf.pump_env += 16384;
            if (pf.pump_env >= 32767) {
                pf.pump_env = 32767;
                pf.pump_up = 0;
            }
        } else {
            pf.pump_env -= (int32_t)(32767u * BLOCK / (48u * (sq.tick_q16 >> 16) + 1u)) + 1;
            if (pf.pump_env < 0)
                pf.pump_env = 0;
        }
        pump_g1 = 32768 - ((pf.pump_env * pf.pump_env) >> 15) * (int32_t)pfs.pump / 127;
    } else {
        pf.pump_env = 0;
        pump_g1 = pump_g1 + 2048 < 32768 ? pump_g1 + 2048 : 32768;
    }
    pf.pump_seen = trig_count[0];
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

/* 0.6: PERFORM 2 on the master, in this order: COMB, CUTS, CRUSH, then the recorder and REVERSE */
static void perf2_master(int16_t *l, int16_t *r)
{
    uint32_t on = pf.on, i;
    if ((on >> PF_COMB) & 1u) {                      /* COMB: y = x (1 - fb) 4 + fb lp(y[n - D]); out x / 2 + y */
        int32_t f8 = (int32_t)(((uint64_t)112640u * (uint32_t)exp2_q16(((int32_t)pfs.comb_note - 69) * 65536 / 12)) >>
                               16);                  /* the note's Hz, Q8 */
        uint32_t d8 = ((uint32_t)FS << 16) / (uint32_t)(f8 > 0 ? f8 : 1), di = d8 >> 8, df = d8 & 255u;
        int32_t fb = 16384 + pfs.comb_fb * 125, gi = (32768 - fb) * 4;
        if (di < 2u)
            di = 2u;
        if (di > COMB_LEN - 2u)
            di = COMB_LEN - 2u;
        for (i = 0; i < BLOCK; i++) {
            uint32_t a = (pf.comb_w - di) & (COMB_LEN - 1u), b = (a - 1u) & (COMB_LEN - 1u);
            int32_t x = (l[i] + r[i]) >> 1, y = comb_buf[a] + (((comb_buf[b] - comb_buf[a]) * (int32_t)df) >> 8);
            pf.comb_lp += ((y - pf.comb_lp) * 24576) >> 15;   /* (a gentle low-pass in the loop) */
            y = sat16(((x * gi) >> 15) + ((pf.comb_lp * fb) >> 15));
            comb_buf[pf.comb_w++ & (COMB_LEN - 1u)] = (int16_t)y;
            l[i] = (int16_t)sat16((l[i] >> 1) + y);
            r[i] = (int16_t)sat16((r[i] >> 1) + y);
        }
    }
    if ((on >> PF_CUTS) & 1u || pf.cut_g < 32767) {  /* CUTS: each 32nd open or shut at random, 32-sample fades */
        int32_t per = (int32_t)((12u * (sq.tick_q16 >> 8)) >> 8);
        for (i = 0; i < BLOCK; i++) {
            if (!((on >> PF_CUTS) & 1u))
                pf.cut_open = 1;
            else if (--pf.cut_n <= 0) {
                pf.cut_n = per > 64 ? per : 64;
                pf.cut_open = (int32_t)(seq_rand() % 100u) >= pfs.cuts * 100 / 127;
            }
            pf.cut_g = pf.cut_open ? (pf.cut_g + 1024 < 32767 ? pf.cut_g + 1024 : 32767)
                                   : (pf.cut_g > 1024 ? pf.cut_g - 1024 : 0);
            l[i] = (int16_t)((l[i] * pf.cut_g) >> 15);
            r[i] = (int16_t)((r[i] * pf.cut_g) >> 15);
        }
    }
    if ((on >> PF_CRUSH) & 1u) {                     /* CRUSH: held 1 .. 16 samples, 16 .. 4 bits */
        uint32_t hold = 1u + pfs.crush * 15u / 127u, bits = 16u - pfs.crush * 12u / 127u;
        int32_t mask = (int32_t)(0xFFFFFFFFu << (16u - bits));
        for (i = 0; i < BLOCK; i++) {
            if (--pf.cr_n <= 0) {
                pf.cr_n = (int32_t)hold;
                pf.cr_l = l[i] & mask;
                pf.cr_r = r[i] & mask;
            }
            l[i] = (int16_t)pf.cr_l;
            r[i] = (int16_t)pf.cr_r;
        }
    }
    /* REVERSE: the recorder (mono, every other sample) stops while it plays; the part from its end backwards, again
     * and again, 64-sample fades at its ends and in and out */
    if ((on >> PF_REVERSE) & 1u && !pf.rv_len) {
        uint32_t n = (uint32_t)(((uint64_t)(24u << (pfs.rev_len % 3u)) * sq.tick_q16) >> 17);   /* 22 kHz */
        pf.rv_len = n < 64u ? 64u : n > REC_LEN ? REC_LEN : n;
        pf.rv_end = pf.rec_w;
        pf.rv_pos = 0;
    }
    if (pf.rv_len) {
        int32_t tgt = (on >> PF_REVERSE) & 1u ? 32767 : 0;
        for (i = 0; i < BLOCK; i++) {
            uint32_t k = pf.rv_pos >> 1, e = pf.rv_len - k, f = k < e ? k : e;
            int32_t v = rec_buf[(pf.rv_end + REC_LEN - 1u - k) % REC_LEN];
            v = f < 32u ? v * (int32_t)f / 32 : v;
            pf.rv_mix += pf.rv_mix < tgt ? 512 : pf.rv_mix > tgt ? -512 : 0;
            pf.rv_mix = clamp32(pf.rv_mix, 0, 32767);
            l[i] = (int16_t)(l[i] + (((v - l[i]) * pf.rv_mix) >> 15));
            r[i] = (int16_t)(r[i] + (((v - r[i]) * pf.rv_mix) >> 15));
            if (++pf.rv_pos >= pf.rv_len * 2u)
                pf.rv_pos = 0;
        }
        if (!tgt && !pf.rv_mix)
            pf.rv_len = 0;
    } else {
        for (i = 0; i < BLOCK; i += 2) {
            rec_buf[pf.rec_w] = (int16_t)((l[i] + r[i] + l[i + 1u] + r[i + 1u]) >> 2);
            pf.rec_w = pf.rec_w + 1u < REC_LEN ? pf.rec_w + 1u : 0u;
        }
    }
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
    perf2_master(l, r);
    if (tape_q16 < 16384u) {                         /* the tape's last quarter of speed: fades to silence */
        int32_t g = (int32_t)tape_q16 * 2;
        for (i = 0; i < BLOCK; i++) {
            l[i] = (int16_t)((l[i] * g) >> 15);
            r[i] = (int16_t)((r[i] * g) >> 15);
        }
    }
}
