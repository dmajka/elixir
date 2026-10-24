/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Kettle: Felucca's reverb (Felucca/firmware/src/fx.c: ROOM, SPRING, HALL in ~12 KB of shared buffers) as a send
 * bus after the FX bus. Each voice feeds it by its REVERB SEND (voices.c rev_in); its return, scaled by LEVEL, joins
 * the mix in stereo (HALL's side signal to the left and from the right). The parameters (sound.c): PAR_REV_TYPE,
 * PAR_REV_SIZE, PAR_REV_DAMP, PAR_REV_LEVEL, kit parameters as the FX bus's are. Felucca runs it on blocks of 32
 * samples (CTL); here a block is the engine's BLOCK (64): the drifts' increments are per block, and HALL's half
 * rate block (HL_NC) is 32 samples. Felucca's comments, abridged where they spoke of its other effects. */

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* m / 2^sh rounded towards zero (a product that feeds back must not floor: a loop of floors holds an offset and a
 * little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }

static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] POOL;
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u POOL;
#define rev_ap (rev_u.ap)
static struct {
    uint8_t type, size, damp;            /* the parameters: the model (0 ROOM, 1 SPRING, 2 HALL), SIZE, DAMP */
    int32_t level;                       /* the return, Q15 */
    uint8_t rtype;                       /* the model running */
    uint8_t idle;                        /* blocks with no send and a silent tail (it sleeps: no work) */
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
} rv;

/* SPRING (type 1): one spring of a spring tank, mono, in the ROOM's own buffers (no RAM of its own): the input and
 * the loop's return -> a low cut (~110 Hz: a spring carries little bass) -> SP_N stretched first-order allpasses,
 * (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below fs / 8 = 5.5 kHz the group delay rises with
 * frequency, the chirp; each pass round the loop adds more of it: the "boing", the drips) -> the loop's delay line
 * (rev_comb, SP_LEN) -> back through a one-pole low-pass (DAMP) and the decay gain (SIZE). The output: the spring's
 * far end, half way along the loop (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow
 * wobble of a sample or two on it), plus a second, quieter pickup at three quarters (a shorter spring beside it:
 * denser). SIZE sets the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses'
 * states: 4 samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears
 * both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
#define SP_WOB ((uint32_t)(2.0 * 0.1768 * BLOCK / FS * 4294967296.0))   /* the wobble: ~0.35 Hz (Felucca's) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* HALL (type 2): a smooth, dense, stereo reverb, again in ROOM's buffers (its few states and one block of work in
 * the pool). ROOM's 4 combs each ring on their own (the metallic tone); here every echo goes round 8 lines that all
 * feed each other, and they run at half the rate (22.05 kHz: the same memory holds twice the time, so twice the
 * resonances, at half the work; the tail stops near 10 kHz, as a hall's air does). The send (every other sample of
 * it, low-passed 1 2 1; ROOM's level) -> a low cut (~110 Hz: no boom in a long tail) -> 2 allpass diffusers
 * (rev_ap: a drum's click smeared into a burst) -> into the 8 lines (rev_comb, mutually prime lengths, 21 .. 34 ms).
 * Lines 1, 2, 4, 7 are read through slow sines (+-HL_MOD samples, between samples; 0.37 or 0.74 Hz, each in its
 * own phase): the resonances drift instead of ringing. Lines 0, 3, 5, 6, read at their ends, go through a one-pole
 * low-pass (DAMP: the mixing takes every echo through them); every line through its gain (from its length: all
 * decay alike; RT60 below the damping 0.5 s at SIZE 0, 4 s at 90, 9 s at 127); then the 8 x 8 mixing: a conference
 * matrix (Paley, order 8: every line feeds the 7 others, +-1 each, none itself: no flutter at a line's own length),
 * with the send added. Stereo: left lines 0 3 4 -7, right 1 2 5 6; the return (mono) gets left + right, rev_side
 * left - right. Back to 44.1 kHz between samples. Rounding: a loud line's gain rounds to the nearest (the tail
 * decays as it should), a quiet one's towards zero (it ends in exact silence: no limit cycle), and the low-passes
 * keep their steps' remainders (no offset). */
#define HL_NL 8u                         /* lines */
#define HL_NAP 2u                        /* diffusers */
#define HL_NC (BLOCK / 2u)               /* a block at half the rate */
#define HL_MOD 8                         /* the taps' swing, samples (read 2 .. 18 samples short of the end) */
#define HL_LPM 0x69u                     /* the lines with a low-pass */
#define HL_KD(d) (32767 - (d) * 180)     /* its coefficient (Q15) for DAMP d: none at 0 .. ~1 kHz at 127 */
#define HL_LOUD 32                       /* a line this loud (two samples' sum) rounds its gain to the nearest */
#define HL_OUT 4                         /* the outputs' gain: the level of ROOM's tail */
static const uint16_t HL_LEN[HL_NL] = {467, 509, 547, 593, 631, 673, 709, 743};   /* (+1 each: the mirror) */
static const uint16_t HL_AP[HL_NAP] = {131, 307};
static const int16_t HL_APG[HL_NAP] = {11469, 10240};               /* Q14: 0.7, 0.625 */
_Static_assert(467 + 509 + 547 + 593 + 631 + 673 + 709 + 743 + HL_NL <= sizeof rev_comb / 2u &&
               131 + 307 <= sizeof rev_u.ap / 2u, "HALL in ROOM's buffers");
_Static_assert(BLOCK % 2u == 0u && HL_NC + 2u * HL_MOD + 2u < 467u && HL_NC <= 131u, "HALL: one wrap a block at most");
static struct {                          /* HALL's state (in the pool) */
    uint16_t i[HL_NL], d[HL_NAP];        /* the lines' write indices, the diffusers' */
    int32_t lp[HL_NL], le[HL_NL];        /* the low-passes and their steps' remainders */
    int32_t x, m, s, hp, he;             /* the send's last sample, its low cut and that step's remainder; the
                                          * last mid and side out (half rate) */
    int16_t g[HL_NL], sz;                /* the lines' gains / sqrt(7) (Q14), for SIZE sz - 1 (0: none yet) */
    uint32_t ph;                         /* the taps' drift */
    uint8_t side;                        /* sd holds this block's stereo difference (HALL running) */
    int32_t sd[BLOCK];                   /* the block's left minus right */
    int32_t bx[HL_NC], l[HL_NC], r[HL_NC];   /* the block at half the rate: the diffused send, left, right */
    int16_t y[HL_NL][HL_NC];             /* the lines' ends, then what goes back into them */
} hl POOL;
#define rev_side (hl.sd)

/* ROOM (type 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out. Kettle: the loops' products
 * round towards zero (Felucca's floor them and its master's DC blocker takes out the offset they leave, -33 here
 * for ever after a hit; Kettle has no DC blocker) */
static __attribute__((noinline)) void rev_room(const int32_t *in_, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = space_on ? 32440 : 25000 + rv.size * 50, damp = 32767 - rv.damp * 200;   /* (SPACE: held) */
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(in_[i], 2580);              /* 1/8 at -4 dB */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[rv.comb_i[k]];
            rv.comb_lp[k] = o + sh_tz((rv.comb_lp[k] - o) * (32767 - damp), 15);
            c[rv.comb_i[k]] = (int16_t)clamp32(in + sh_tz(rv.comb_lp[k] * size, 15), -32768, 32767);
            if (++rv.comb_i[k] >= REV_COMB[k])
                rv.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[rv.ap_i[k]];
            int32_t v = a + sh_tz(o, 1);
            c[rv.ap_i[k]] = (int16_t)clamp32(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++rv.ap_i[k] >= REV_AP[k])
                rv.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see above), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *in_, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = rv.size;
    int32_t g = space_on ? 32112 : 19661 + (int32_t)s * 85;   /* the loop's gain: 0.6 .. 0.93 (SPACE: 0.98) */
    int32_t kl = 26000 - rv.damp * 160;                 /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!rv.sp_size)
        rv.sp_size = len;
    rv.sp_size += clamp32(len - rv.sp_size, -256, 256); /* SIZE glides (a sample a block at most) */
    L = rv.sp_size >> 8;
    rv.sp_ph += SP_WOB;                                 /* the wobble: a slow sine, 1.5 samples deep */
    w = (rv.sp_size >> 1) + ((sine_at(rv.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = rv.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(in_[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        rv.sp_lp += mulq15(r - rv.sp_lp, kl);
        o = rv.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - rv.sp_hp + rv.sp_he;                    /* the low cut, its step's remainder kept: no dead band */
        rv.sp_he = o & 63;                              /* to hold an offset in the loop */
        rv.sp_hp += o >> 6;
        x -= rv.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp32(x, -32768, 32767);
        rv.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* HALL's helpers. 2^-x, x Q16 (0 .. 16): Q15 (32768 at x = 0). 2^-x = 2^u / 2^(i + 1), u = 1 - frac(x); 2^u by a
 * cubic (1e-4) */
static uint32_t exp2_neg(uint32_t x)
{
    uint32_t i = x >> 16, u = 32768u - ((x & 0xFFFFu) >> 1), p = 2579u;
    p = ((p * u) >> 15) + 7409u;
    p = ((p * u) >> 15) + 22780u;
    p = ((p * u) >> 15) + 32768u;
    return i < 15u ? p >> (i + 1u) : 0u;
}
/* the lines' gains for SIZE s: RT60 = 0.5 s * 2^(s / 30); a line of D samples (22.05 kHz) loses 60 dB in
 * RT60 * 22050 / D passes: g = 2^(-D * 3 log2(10) / (22050 * RT60)) = 2^(-D * 59.2 / 65536 * 2^(-s / 30)),
 * at most 0.99, over sqrt(7) (the matrix's rows have 7 entries of +-1: / sqrt(7) makes it orthogonal) */
static __attribute__((noinline)) void hall_gains(int32_t s)
{
    uint32_t q = exp2_neg((uint32_t)s * 65536u / 30u), k, g;
    for (k = 0; k < HL_NL; k++) {
        g = exp2_neg((HL_LEN[k] * 59u * q) >> 15);
        hl.g[k] = (int16_t)(((g > 32440u ? 32440u : g) * 6193u) >> 15);
    }
    hl.sz = (int16_t)(s + 1);
}

/* an allpass diffuser over x[0 .. n) from q (its next samples, no wrap): its state rounded towards zero */
static inline void hall_ap(int16_t *q, int32_t g, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = q[i], v = x[i] + sh_tz(y * g, 14);
        q[i] = (int16_t)clamp32(v, -32768, 32767);
        x[i] = y - ((v * g + 8192) >> 14);
    }
}
/* a line's end over y[0 .. n) from b (no wrap; b[n] is still the line's: its mirror at the end): between b[i]
 * and b[i + 1], f / 256 of the way (rounded to the nearest), times its gain g (Q14). The gain's product: rounded
 * to the nearest while the line is loud (lo 0: the tail decays as it should), towards zero once it is quiet (lo 1:
 * the last steps of the tail, which then ends in exact silence) */
static inline void hall_end(const int16_t *b, int32_t f, int16_t *y, uint32_t n, int32_t g, int lo)
{
    uint32_t i;
    if (lo)
        for (i = 0; i < n; i++)
            y[i] = (int16_t)sh_tz((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g, 14);
    else
        for (i = 0; i < n; i++)
            y[i] = (int16_t)(((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g + 8192) >> 14);
}
/* a line's end over y[0 .. n) from b (no wrap), through the low-pass (kd, Q15; the state *lp and its step's
 * remainder *le), times its gain g (rounded as hall_end's) */
static inline void hall_end_lp(const int16_t *b, int16_t *y, uint32_t n, int32_t g, int lo, int32_t *lp, int32_t *le,
                               int32_t kd)
{
    uint32_t i;
    int32_t l = *lp, r = *le, e;
    if (lo)
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)sh_tz(l * g, 14);
        }
    else
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)((l * g + 8192) >> 14);
        }
    *lp = l;
    *le = r;
}
/* the block's HL_NC samples of a circular buffer of len from index d in at most two runs, no wrap inside them:
 * body with s (the run's start in the block) and k (its length); d moves on */
#define HL_RUNS(d, len, k, s, body)                                                         \
    for (s = 0; s < HL_NC; s += k) {                                                        \
        k = (len) - (d) < HL_NC - s ? (len) - (d) : HL_NC - s;                              \
        body;                                                                               \
        d = (d) + k >= (len) ? 0u : (d) + k;                                                \
    }

/* HALL (see above): a block of the send (n = BLOCK samples) -> the mid added to out, the side into rev_side. At
 * half the rate (HL_NC samples) in passes over the block, each with few states (every line is longer than a block:
 * what the lines give in this block was written before it) */
#define HL_LFO ((uint32_t)(0.37 * BLOCK / FS * 4294967296.0))   /* the drift: 0.37 Hz (even lines), 0.74 Hz (odd) */
static __attribute__((noinline)) void rev_hall(const int32_t *in_, int32_t *out, uint32_t n)
{
    int16_t *b = rev_comb, *a = rev_ap;
    uint32_t i, k, s, m, d;
    int32_t kd = HL_KD(rv.damp);
    int32_t xp = hl.x, hp = hl.hp, he = hl.he, mp = hl.m, sp = hl.s;
    (void)n;
    if (hl.sz != (space_on ? 241 : rv.size + 1))     /* (SPACE: a tail of ~130 s, the lines at their 0.99) */
        hall_gains(space_on ? 240 : rv.size);
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (xp + 2 * in_[2u * i] + in_[2u * i + 1u]) >> 2, v;   /* every other sample, 1 2 1 */
        xp = in_[2u * i + 1u];
        x = mulq15(clamp32(x, -262143, 262143), 2580);  /* (ROOM's level) */
        v = x - hp + he;                                /* the low cut, its step's remainder kept */
        he = v & 31;
        hp += v >> 5;
        hl.bx[i] = x - hp;
    }
    hl.x = xp, hl.hp = hp, hl.he = he;
    for (k = 0; k < HL_NAP; k++) {                      /* the diffusers */
        d = hl.d[k];
        HL_RUNS(d, HL_AP[k], m, s, hall_ap(a + d, HL_APG[k], hl.bx + s, m));
        hl.d[k] = (uint16_t)d;
        a += HL_AP[k];
    }
    hl.ph += HL_LFO;
    for (k = 0; k < HL_NL; k++) {                       /* the lines' ends */
        int32_t g = hl.g[k], f = 0, lo;
        if ((HL_LPM >> k) & 1u)
            d = hl.i[k];                                /* (low-passed: the line's end) */
        else {                                          /* (drifting: HL_MOD + 2 +- HL_MOD samples short of it) */
            uint32_t r = (uint32_t)(((HL_MOD + 2) << 8) +
                                    ((sine_at(hl.ph * (1u + (k & 1u)) + k * 0x2C000000u) * HL_MOD) >> 7));
            f = (int32_t)(r & 255u);
            d = hl.i[k] + (r >> 8);
            d = d >= HL_LEN[k] ? d - HL_LEN[k] : d;
        }
        s = d + HL_LEN[k] / 2u;                         /* quiet? (two samples half a line apart) */
        s = s >= HL_LEN[k] ? s - HL_LEN[k] : s;
        lo = (b[d] < 0 ? -b[d] : b[d]) + (b[s] < 0 ? -b[s] : b[s]) < HL_LOUD;
        if ((HL_LPM >> k) & 1u) {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end_lp(b + d, hl.y[k] + s, m, g, lo, &hl.lp[k], &hl.le[k], kd));
        } else {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end(b + d, f, hl.y[k] + s, m, g, lo));
        }
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {
        int32_t y0 = hl.y[0][i], y1 = hl.y[1][i], y2 = hl.y[2][i], y3 = hl.y[3][i];
        int32_t y4 = hl.y[4][i], y5 = hl.y[5][i], y6 = hl.y[6][i], y7 = hl.y[7][i], x = hl.bx[i];
        int32_t t, p0, p1, p2, p3, p4, p5, p6;
        hl.l[i] = y0 + y3 + y4 - y7;                    /* the outputs */
        hl.r[i] = y1 + y2 + y5 + y6;
        /* the matrix: C = [0 1; -1 Q], Q (7 x 7) = chi(j - i), the quadratic character mod 7 (+1 at distances
         * 1 2 4, -1 at 3 5 6): row 1 + i of Q y = 2 (the three at +1) - (the sum of the 7) + y_1+i */
        t = y1 + y2 + y3 + y4 + y5 + y6 + y7;
        p0 = y2 + y3 + y5, p1 = y3 + y4 + y6, p2 = y4 + y5 + y7, p3 = y5 + y6 + y1;
        p4 = y6 + y7 + y2, p5 = y7 + y1 + y3, p6 = y1 + y2 + y4;
        y0 += t;
        hl.y[0][i] = (int16_t)clamp32(t + x, -32768, 32767);   /* (+ the send: signs spread over the rows) */
        hl.y[1][i] = (int16_t)clamp32(2 * p0 + y1 - y0 + x, -32768, 32767);
        hl.y[2][i] = (int16_t)clamp32(2 * p1 + y2 - y0 + x, -32768, 32767);
        hl.y[3][i] = (int16_t)clamp32(2 * p2 + y3 - y0 + x, -32768, 32767);
        hl.y[4][i] = (int16_t)clamp32(2 * p3 + y4 - y0 + x, -32768, 32767);
        hl.y[5][i] = (int16_t)clamp32(2 * p4 + y5 - y0 + x, -32768, 32767);
        hl.y[6][i] = (int16_t)clamp32(2 * p5 + y6 - y0 + x, -32768, 32767);
        hl.y[7][i] = (int16_t)clamp32(2 * p6 + y7 - y0 - x, -32768, 32767);
    }
    b = rev_comb;
    for (k = 0; k < HL_NL; k++) {                       /* back into the lines (and the mirror after a wrap) */
        d = hl.i[k];
        HL_RUNS(d, HL_LEN[k], m, s, for (i = 0; i < m; i++) b[d + i] = hl.y[k][s + i]; if (!d) b[HL_LEN[k]] = b[0]);
        hl.i[k] = (uint16_t)d;
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {                       /* back to 44.1 kHz, between samples */
        int32_t mm = (hl.l[i] + hl.r[i]) * HL_OUT, ss = (hl.l[i] - hl.r[i]) * HL_OUT;
        out[2u * i] += (mp + mm) >> 1;
        out[2u * i + 1u] += mm;
        rev_side[2u * i] = (sp + ss) >> 1;
        rev_side[2u * i + 1u] = ss;
        mp = mm;
        sp = ss;
    }
    hl.m = mp, hl.s = sp;
    hl.side = 1;
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)          /* (int16: 997 of them; the int32 view misses one) */
        rev_u.ap[i] = 0;
    for (i = 0; i < 4u; i++)
        rv.comb_lp[i] = 0;
    for (i = 0; i < HL_NL; i++)
        hl.lp[i] = hl.le[i] = 0;
    rv.sp_lp = rv.sp_hp = rv.sp_he = 0;
    hl.x = hl.m = hl.s = hl.hp = hl.he = 0;
}

static void rev_model(const int32_t *in_, int32_t *out, uint32_t n)   /* the model running, added to out */
{
    if (rv.rtype == 2u)
        rev_hall(in_, out, n);
    else if (rv.rtype)
        rev_spring(in_, out, n);
    else
        rev_room(in_, out, n);
}

/* the model changed (to rt): the old one's block fades out (HALL's stereo difference with it), its buffers are
 * cleared, the new one starts from silence */
static int32_t rev_wet[BLOCK], rev_tmp[BLOCK];      /* the return; the fading model's block */
static __attribute__((noinline)) void rev_switch(const int32_t *in_, int32_t *wet, uint32_t n, uint32_t rt)
{
    int32_t *t = rev_tmp, g = 65536, d = 65536 / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++)
        t[i] = 0;
    rev_model(in_, t, n);
    for (i = 0; i < n; i++, g -= d) {
        wet[i] += mulq16(t[i], g);
        rev_side[i] = mulq16(rev_side[i], g);           /* (HALL only: hl.side set) */
    }
    rev_clear();
    rv.rtype = (uint8_t)rt;
}

/* the voices' block (voices_block, after the FX bus): the send through the model, the return into the mix. With
 * no send and a return below 4 for 255 blocks (0.37 s), it sleeps (the voices' CPU back); a send wakes it */
static void reverb_process(void)
{
    int32_t *wet = rev_wet;
    uint32_t i, rt = rv.type < 3u ? rv.type : 0u;
    int32_t pk = 0;
    if (rev_fed || space_on)
        rv.idle = 0;
    else if (rv.idle >= 255u)
        return;
    for (i = 0; i < BLOCK; i++)
        wet[i] = 0;
    hl.side = 0;
    if (rt != rv.rtype)
        rev_switch(rev_in, wet, BLOCK, rt);
    else
        rev_model(rev_in, wet, BLOCK);
    for (i = 0; i < BLOCK; i++) {
        int32_t w = wet[i], s = hl.side ? rev_side[i] : 0;
        mix_l[i] += mulq16(w + s, rv.level << 1);
        mix_r[i] += mulq16(w - s, rv.level << 1);
        pk |= iabs(w) | iabs(s);
    }
    if (!rev_fed && pk < 4)                          /* (silent: count towards sleep, 255 = asleep) */
        rv.idle = (uint8_t)(rv.idle + 1u);
    else if (!rev_fed)
        rv.idle = 0;
}

static void reverb_init(void)
{
    rev_clear();
    rv.type = rv.rtype = 0;
    rv.size = 64;
    rv.damp = 48;
    rv.level = 25800;                                /* 100 / 127 */
    rv.idle = 255;
}
