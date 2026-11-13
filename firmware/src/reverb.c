/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Elixir: Felucca's reverb (Felucca/firmware/src/fx.c: ROOM, SPRING, HALL in ~12 KB of shared buffers) as a send
 * bus after the FX bus. Each voice feeds it by its REVERB SEND (voices.c rev_in); its return, scaled by LEVEL, joins
 * the mix in stereo (HALL's side signal to the left and from the right). The parameters (sound.c): PAR_REV_TYPE,
 * PAR_REV_SIZE, PAR_REV_DAMP, PAR_REV_LEVEL, kit parameters as the FX bus's are. Felucca runs it on blocks of 32
 * samples (CTL); here a block is the engine's BLOCK (64): the drifts' increments are per block, and HALL's half
 * rate block (HL_NC) is 32 samples. Felucca's comments, abridged where they spoke of its other effects. */

#define NREV 4u                          /* the models (22: PLATE the fourth) */
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* m / 2^sh rounded towards zero (a product that feeds back must not floor: a loop of floors holds an offset and a
 * little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
static inline int32_t sh_rn(int32_t m, uint32_t sh) { return (m + (1 << (sh - 1u))) >> sh; }   /* (to nearest: PLATE's tail) */

static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static union {                          /* ROOM's combs; SPRING's loop (int32) */
    int16_t s[1116 + 1188 + 1277 + 1356];
    int32_t l[(1116 + 1188 + 1277 + 1356) / 2];
} rev_cu RAMBUF;                         /* (18a: in RAMTEXT) */
#define rev_comb (rev_cu.s)
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u RAMBUF;
#define rev_ap (rev_u.ap)
static int16_t pl_buf[1139 + 2350 + 1318 + 1146] POOL;   /* PLATE's D1A, DEL1, D2A, D2B; HALL's lines */
static struct {
    uint8_t type, size, damp;            /* the parameters: the model (0 ROOM, 1 SPRING, 2 HALL, 3 PLATE), SIZE, DAMP */
    int32_t level;                       /* the return, Q15 */
    int32_t lvl_last;                    /* (1.0) the return as the last block ended (the ramp to level) */
    uint8_t rtype;                       /* the model running */
    uint8_t idle;                        /* blocks with no send and a silent tail (it sleeps: no work) */
    uint16_t comb_w, ap_w;               /* ROOM: the combs' write pointer, the allpasses' (rev_room) */
    int32_t comb_lp[4];
} rv;

/* SPRING (type 1, 1.4: the drip): a dub spring tank, after Valimaki, Parker and Abel's model ("Parametric Spring
 * Reverberation Effect", JAES 2010) as Parker makes it cheaper ("Efficient Dispersion Generation Structures for
 * Spring Reverb Emulation", EURASIP 2011: its low chirps' loop at a quarter of the rate). A spring delays its high
 * frequencies more than its lows (dispersion, up to a transition near 5 kHz), so each echo arrives as a chirp, lows
 * first and sweeping up over tens of milliseconds, and every trip round the spring bends it more: the drip. Here,
 * at 11.025 kHz (fs / 4, blocks of SP_NQ): the send (low-passed, 1 2 3 4 3 2 1, every 4th sample) -> the dwell, a
 * soft clip driven hard (a tank's input is driven near saturation: drip grows with it) -> + the loop's return ->
 * a low cut (~110 Hz) -> SP_N first-order allpasses (a + z^-1) / (1 + a z^-1), whose group delay peaks at the
 * quarter rate's Nyquist, 5.5 kHz (SP_N (1 + a) / (1 - a) samples: the chirp) -> the loop's line (int32: a quiet
 * tail decays as a loud one does) -> back through a one-pole low-pass (DAMP) and the decay gain (SIZE). SIZE sets
 * the loop (40 .. 80 ms), its decay and the drip (the stages' coefficient: a longer spring disperses more, the
 * chirp reaching further down); DAMP the loop's low-pass; the loop's length wobbles slowly (the tail's pitch
 * drifts a little). Two pickups on the line, half and three eighths of the loop back, give the left and right.
 * Back to 44.1 kHz between samples. The allpasses' states in rev_ap's memory, the line in rev_comb's. */
#define SP_Q 4u                          /* the tank's rate: fs / SP_Q */
#define SP_NQ (BLOCK / SP_Q)             /* a block at that rate */
#define SP_LEN 2048u                     /* the loop's line (int32, 186 ms) */
#define SP_MASK (SP_LEN - 1u)
#ifndef SP_N
#define SP_N 48u                         /* allpass stages */
#endif
#define SP_A0 3523                       /* their coefficient, Q12: 0.86 at SIZE 0 .. (- SP_AS x (SIZE / 127)^2) */
#define SP_AS 900                        /* .. 0.64 at 127 (0.80 at 64): lower spreads the chirp further down */
#ifndef SP_DRIVE
#define SP_DRIVE (3 * 4096)              /* the dwell: the send into the soft clip, Q12 */
#endif
#ifndef SP_OUT
#define SP_OUT 4                         /* the pickups' gain, / 8 */
#endif
#define SP_WOB ((uint32_t)(0.35 * BLOCK / FS * 4294967296.0))   /* the wobble: ~0.35 Hz */
_Static_assert(sizeof rev_cu.l / 4u >= SP_LEN && sizeof rev_u.sp / 4u >= SP_N + 1u && BLOCK % SP_Q == 0u,
               "SPRING in ROOM's buffers");
static struct {
    int32_t h1, h2, h3;                  /* the send's last three samples (the decimator) */
    int32_t lp, hp, he, size;            /* the loop's low-pass, low cut (and its step's remainder); its length (Q8,
                                          * glides) */
    int32_t m, s;                        /* the last mid and side out (quarter rate) */
    int32_t om, os;                      /* the output's low-pass (44.1 kHz: the images above 5.5 kHz down) */
    uint32_t ph;                         /* the wobble */
    int32_t a;                           /* the stages' coefficient (Q12, glides to SIZE's) */
    uint16_t w;                          /* the line's write index */
    int32_t x[SP_NQ];                    /* the block at the quarter rate */
} spr;
#define SP_AP(i, y) sh_tz(((i) - (y)) * ka, 12)    /* a stage's product (towards zero: no offset, no limit
                                                     * cycle in the loop) */
_Static_assert(SP_N % 4u == 0u && SP_Q == 4u, "SPRING: the stages 4 at a time, the output 4 samples a step");
static inline int32_t tz15_64(int32_t a, int32_t b)  /* a b / 2^15 towards zero (64-bit product) */
{
    int64_t m = (int64_t)a * b;
    return (int32_t)((m + ((m >> 63) & 32767)) >> 15);
}

/* HALL (type 2): a smooth, dense, stereo reverb, in the shared buffers (its few states and one block of work in
 * the pool). ROOM's 4 combs each ring on their own (the metallic tone); here every echo goes round 8 lines that all
 * feed each other, and they run at half the rate (22.05 kHz: the same memory holds twice the time, so twice the
 * resonances, at half the work; the tail stops near 10 kHz, as a hall's air does). The send (every other sample of
 * it, low-passed 1 2 1; ROOM's level) -> a low cut (~110 Hz: no boom in a long tail) -> 2 allpass diffusers
 * (rev_ap: a drum's click smeared into a burst) -> into the 8 lines (pl_buf, mutually prime lengths, 21 .. 34 ms;
 * 1.4: each with a block's spare samples after its end, so the matrix writes a block straight in and only what
 * passed the end is copied to the start).
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
_Static_assert(467 + 509 + 547 + 593 + 631 + 673 + 709 + 743 + HL_NL * HL_NC <= sizeof pl_buf / 2u &&
               131 + 307 <= sizeof rev_u.ap / 2u, "HALL in PLATE's buffer and ROOM's allpasses");
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
    int16_t y[HL_NL][HL_NC];             /* the lines' ends */
} hl RAMBUF;
#define rev_side (hl.sd)

/* ROOM (type 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out. Elixir: the loops' products
 * round towards zero (Felucca's floor them and its master's DC blocker takes out the offset they leave, -33 here
 * for ever after a hit; Elixir has no DC blocker). 1.4: the 4 combs fill rev_comb and share one write pointer w,
 * moving back a sample at a time (Clouds' way; the same values as a pointer a comb): comb k writes at P_k = w + the
 * lengths before it and reads at P_k + its length = P_k+1 (comb 3: P_4 = w + all of them = w), the sample it wrote
 * its length ago. Every comb reads before any writes (comb k reads where comb k+1 writes this sample), so the 4
 * reads and writes take 4 places (3 wraps), not 8. The 2 allpasses in rev_ap the same way */
#define RC_T (1116u + 1188u + 1277u + 1356u)
#define RA_T (556u + 441u)
_Static_assert(sizeof rev_comb / 2u == RC_T && sizeof rev_u.ap / 2u == RA_T, "ROOM: the combs fill rev_comb, the allpasses rev_ap");
static inline int32_t room_comb(int16_t *c, uint32_t pw, int32_t o, int32_t *lp, int32_t in, int32_t size, int32_t kd)
{
    int32_t l = o + sh_tz((*lp - o) * kd, 15);
    *lp = l;
    c[pw] = (int16_t)clamp32(in + sh_tz(l * size, 15), -32768, 32767);
    return o;
}
static __attribute__((noinline)) void rev_room(const int32_t *in_, int32_t *out, uint32_t n)
{
    uint32_t i, w = rv.comb_w, v = rv.ap_w;
    int32_t size = space_on ? 32440 : 25000 + rv.size * 50, kd = rv.damp * 200;   /* (SPACE: held) */
    int32_t l0 = rv.comb_lp[0], l1 = rv.comb_lp[1], l2 = rv.comb_lp[2], l3 = rv.comb_lp[3];
    int16_t *c = rev_comb, *ap = rev_ap;
    for (i = 0; i < n; i++) {
        uint32_t p1, p2, p3, q1;
        int32_t in = mulq15(in_[i], 2580), a, o0, o1, y;   /* 1/8 at -4 dB */
        w = w ? w - 1u : RC_T - 1u;
        p1 = w + 1116u, p1 = p1 >= RC_T ? p1 - RC_T : p1;
        p2 = w + 1116u + 1188u, p2 = p2 >= RC_T ? p2 - RC_T : p2;
        p3 = w + 1116u + 1188u + 1277u, p3 = p3 >= RC_T ? p3 - RC_T : p3;
        {
            int32_t r0 = c[p1], r1 = c[p2], r2 = c[p3], r3 = c[w];   /* (the reads first) */
            a = room_comb(c, w, r0, &l0, in, size, kd);
            a += room_comb(c, p1, r1, &l1, in, size, kd);
            a += room_comb(c, p2, r2, &l2, in, size, kd);
            a += room_comb(c, p3, r3, &l3, in, size, kd);
        }
        v = v ? v - 1u : RA_T - 1u;
        q1 = v + 556u, q1 = q1 >= RA_T ? q1 - RA_T : q1;
        o0 = ap[q1], o1 = ap[v];
        y = a + sh_tz(o0, 1);                           /* Freeverb: out = buf - in (o - v is a notch comb) */
        ap[v] = (int16_t)clamp32(y, -32768, 32767);
        a = o0 - a;
        y = a + sh_tz(o1, 1);
        ap[q1] = (int16_t)clamp32(y, -32768, 32767);
        out[i] += o1 - a;
    }
    rv.comb_w = (uint16_t)w, rv.ap_w = (uint16_t)v;
    rv.comb_lp[0] = l0, rv.comb_lp[1] = l1, rv.comb_lp[2] = l2, rv.comb_lp[3] = l3;
}

/* SPRING (see above): a block (n = BLOCK), the mid added to out, the side into rev_side */
static __attribute__((noinline)) void rev_spring(const int32_t *in_, int32_t *out, uint32_t n)
{
    uint32_t j, k, s = rv.size, w = spr.w, L, f, LA, LB;
    int32_t g = space_on ? 32112 : 19661 + (int32_t)s * 85;   /* the loop's gain: 0.6 .. 0.93 (SPACE: 0.98) */
    int32_t kl = 32767 - rv.damp * 124;                 /* its low-pass: none .. ~1.3 kHz */
    int32_t len = (int32_t)(441u + ((s * 441u) >> 7)) << 8, d;
    int32_t h1 = spr.h1, h2 = spr.h2, h3 = spr.h3, lp = spr.lp, hp = spr.hp, he = spr.he, mp = spr.m, sp = spr.s;
    int32_t om = spr.om, os = spr.os;
    int32_t *ln = rev_cu.l, *st = rev_u.sp, *x = spr.x, pin, ka = SP_A0 - (int32_t)(SP_AS * s * s / (127u * 127u));
    (void)n;
    if (!spr.size)
        spr.size = len, spr.a = ka;
    spr.a += clamp32(ka - spr.a, -4, 4);                /* (the drip glides: ~0.1% a block) */
    ka = spr.a;
    spr.size += clamp32(len - spr.size, -256, 256);     /* SIZE glides (a sample a block at most) */
    spr.ph += SP_WOB;
    d = spr.size + ((sine_at(spr.ph) * 3) >> 9);        /* the loop's length, +-0.75 samples of wobble, Q8 */
    L = (uint32_t)d >> 8;
    f = (uint32_t)d & 255u;
    LA = (uint32_t)spr.size >> 9;                        /* the pickups: half the loop back, three eighths */
    LB = (uint32_t)(spr.size * 3) >> 11;
    for (j = 0; j < SP_NQ; j++) {                       /* the send in, the loop's return added */
        const int32_t *v = in_ + SP_Q * j;
        int32_t y = (h1 + 2 * h2 + 3 * h3 + 4 * v[0] + 3 * v[1] + 2 * v[2] + v[3]) >> 4, r0, r1;
        h1 = v[1], h2 = v[2], h3 = v[3];
        y = softclip_in(clamp32(y, -65535, 65535), SP_DRIVE) >> 3;   /* the dwell (Q16 -> the tank's Q13) */
        r0 = ln[(w + j - L) & SP_MASK];
        r1 = ln[(w + j - L - 1u) & SP_MASK];
        lp += tz15_64(r0 + (int32_t)(((int64_t)(r1 - r0) * (int32_t)f) >> 8) - lp, kl);
        y += tz15_64(lp, g);                            /* (towards zero: a loop of floors holds an offset) */
        r0 = y - hp + he;                               /* the low cut, its step's remainder kept */
        he = r0 & 15;
        hp += r0 >> 4;
        x[j] = clamp32(y - hp, -131071, 131071);
    }
    pin = st[0];                                        /* the chain, 4 stages at a time over the block: stage k's */
    st[0] = x[SP_NQ - 1u];                              /* .. state its last output, pin its input's last */
    for (k = 1; k < SP_N; k += 4u) {
        int32_t y1 = st[k], y2 = st[k + 1u], y3 = st[k + 2u], y4 = st[k + 3u], p = pin, *q = x;
        const int32_t *e = x + SP_NQ;
        pin = y4;
        FM1_OPAQUE(q);
        do {                                            /* (a stage's input before: the last stage's output before) */
            int32_t i = *q, o;
            o = SP_AP(i, y1) + p, p = i;
            i = SP_AP(o, y2) + y1, y1 = o;
            o = SP_AP(i, y3) + y2, y2 = i;
            i = SP_AP(o, y4) + y3, y3 = o, y4 = i;
            *q++ = i;
        } while (q != e);
        st[k] = y1, st[k + 1u] = y2, st[k + 2u] = y3, st[k + 3u] = y4;
    }
    for (j = 0; j < SP_NQ; j++) {                       /* into the line; the pickups out, back to 44.1 kHz: */
        int32_t a, b, dm, ds, *o = out + SP_Q * j, *r = rev_side + SP_Q * j;   /* .. between samples, then a */
        ln[(w + j) & SP_MASK] = x[j];                   /* .. one-pole (~5 kHz: the images above 5.5 kHz down) */
        a = ln[(w + j - LA) & SP_MASK];
        b = ln[(w + j - LB) & SP_MASK];
        a = (a + b) * SP_OUT >> 3, b = a - ((b * SP_OUT) >> 2);   /* (the mid, the side) */
        dm = (a - mp) >> 2, ds = (b - sp) >> 2;         /* (the last step lands on it: no floor held) */
        mp += dm, sp += ds, om = sh_tz(om + mp, 1), os = sh_tz(os + sp, 1), o[0] += om, r[0] = os;
        mp += dm, sp += ds, om = sh_tz(om + mp, 1), os = sh_tz(os + sp, 1), o[1] += om, r[1] = os;
        mp += dm, sp += ds, om = sh_tz(om + mp, 1), os = sh_tz(os + sp, 1), o[2] += om, r[2] = os;
        mp = a, sp = b, om = sh_tz(om + mp, 1), os = sh_tz(os + sp, 1), o[3] += om, r[3] = os;
    }
    spr.w = (uint16_t)(w + SP_NQ);
    spr.h1 = h1, spr.h2 = h2, spr.h3 = h3, spr.lp = lp, spr.hp = hp, spr.he = he, spr.m = mp, spr.s = sp;
    spr.om = om, spr.os = os;
    hl.side = 1;
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
    int16_t *b = pl_buf, *a = rev_ap, *o0, *o1, *o2, *o3, *o4, *o5, *o6, *o7;
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
        b += HL_LEN[k] + HL_NC;
    }
    b = pl_buf;                                         /* (each line's block written from its index on: past its */
    o0 = b + hl.i[0], b += HL_LEN[0] + HL_NC;           /* .. end into its spare samples, copied below) */
    o1 = b + hl.i[1], b += HL_LEN[1] + HL_NC;
    o2 = b + hl.i[2], b += HL_LEN[2] + HL_NC;
    o3 = b + hl.i[3], b += HL_LEN[3] + HL_NC;
    o4 = b + hl.i[4], b += HL_LEN[4] + HL_NC;
    o5 = b + hl.i[5], b += HL_LEN[5] + HL_NC;
    o6 = b + hl.i[6], b += HL_LEN[6] + HL_NC;
    o7 = b + hl.i[7];
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
        y0 = x - (y0 + t);                              /* (rows 1 .. 6: + the send - y0 - t, row 7: - the send) */
        o0[i] = (int16_t)clamp32(t + x, -32768, 32767);   /* (+ the send: signs spread over the rows) */
        o1[i] = (int16_t)clamp32(2 * p0 + y1 + y0, -32768, 32767);
        o2[i] = (int16_t)clamp32(2 * p1 + y2 + y0, -32768, 32767);
        o3[i] = (int16_t)clamp32(2 * p2 + y3 + y0, -32768, 32767);
        o4[i] = (int16_t)clamp32(2 * p3 + y4 + y0, -32768, 32767);
        o5[i] = (int16_t)clamp32(2 * p4 + y5 + y0, -32768, 32767);
        o6[i] = (int16_t)clamp32(2 * p5 + y6 + y0, -32768, 32767);
        o7[i] = (int16_t)clamp32(2 * p6 + y7 + y0 - 2 * x, -32768, 32767);
    }
    b = pl_buf;
    for (k = 0; k < HL_NL; k++) {                       /* past the end: to the start; the mirror (b[len] = b[0]) */
        d = hl.i[k] + HL_NC;
        if (d > HL_LEN[k]) {                            /* (b[len] written above: position 0's) */
            d -= HL_LEN[k];
            for (i = 0; i < d; i++)
                b[i] = b[HL_LEN[k] + i];
        } else {
            if (!hl.i[k])                               /* (b[0] written above) */
                b[HL_LEN[k]] = b[0];
            d = d == HL_LEN[k] ? 0u : d;
        }
        hl.i[k] = (uint16_t)d;
        b += HL_LEN[k] + HL_NC;
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

/* PLATE (type 3, 22): Clouds' reverb (Mutable Instruments Clouds, clouds/dsp/fx/reverb.h, Emilie Gillet, MIT;
 * Rings and Elements use the same): Jon Dattorro's plate, the Griesinger topology. The send -> 4 allpass diffusers
 * (the first smeared by a slow LFO inside its own loop) -> a figure eight of two branches, each a delay (the other
 * branch's, read with a krt gain; the first through a slow LFO, a chorus on the tail), a one-pole low-pass, 2
 * allpasses, then its delay: the left out of the first branch, the right of the second. Clouds runs at 32 kHz; here
 * at half rate (22.05 kHz, as HALL) with its lengths scaled to it (x 0.689: the same plate, 11,283 samples), in
 * fixed point, finer than Clouds' 12-bit format (a line's int16 holds 16384 to the unit: 2x headroom, the send in at
 * 0.2). The lines: the 4 diffusers in ROOM's allpass buffer, two in its combs, four in pl_buf (the pool). SIZE:
 * the decay (Clouds' reverb time, 0.35 .. 0.98; SPACE 0.995), DAMP: the low-pass (none .. ~900 Hz), Clouds'
 * diffusion (0.625) as it is. Elixir: the loops' products round to the nearest (towards zero, the tail died far
 * sooner than Clouds' float), towards zero once the return is quiet (it ends in silence: no limit cycle) */
#define PL_N 10u
enum { PL_AP1, PL_AP2, PL_AP3, PL_AP4, PL_D1A, PL_D1B, PL_DEL1, PL_D2A, PL_D2B, PL_DEL2 };
static const uint16_t PL_LEN[PL_N] = {78, 112, 166, 275, 1139, 1404, 2350, 1318, 1146, 3295};   /* (Clouds' x 0.689) */
_Static_assert(78 + 112 + 166 + 275 <= sizeof rev_u.ap / 2u && 1404 + 3295 <= sizeof rev_comb / 2u,
               "PLATE: the diffusers in ROOM's allpasses, D1B and DEL2 in its combs");
#define PL_KAP 20480                     /* Clouds' diffusion, 0.625 (Q15) */
#define PL_GAIN 6554                     /* the send in (Clouds' input gain 0.2; Q15) */
#define PL_QUIET 24u                     /* the outputs below it (Q14 x 2, ~-60 dB): the products towards zero */
#define PL_OUT 6                         /* the outputs' gain: about HALL's level (a kick's tail, a snare's) */
#ifndef PL_SW1                           /* the LFOs' swings, samples (Clouds' 60 and 100, scaled; the host's compare
                                          * sets them to 0) */
#define PL_SW1 41u
#define PL_SW2 69u
#endif
#define PL_LFO1 ((uint32_t)(0.5 * BLOCK / FS * 4294967296.0))   /* Clouds' LFOs: 0.5 Hz (the diffuser), 0.3 Hz */
#define PL_LFO2 ((uint32_t)(0.3 * BLOCK / FS * 4294967296.0))
/* The lines (1.4: Clouds' own way, one write pointer a buffer, each line at a fixed offset from it): the lines
 * in one buffer share its pointer, which moves back a sample at a time and wraps at the buffer's lines' total (3
 * pointers, not a head a line); line k's sample d back is the buffer's (w + PL_OFF[k] + d) mod PL_T. The same values
 * as a buffer a line (each starts from zeros) */
#define PL_T0 (78u + 112u + 166u + 275u)        /* rev_u.ap: AP1 .. AP4 */
#define PL_T1 (1404u + 3295u)                   /* rev_comb: D1B, DEL2 */
#define PL_T2 (1139u + 2350u + 1318u + 1146u)   /* pl_buf: D1A, DEL1, D2A, D2B */
static const uint8_t PL_GRP[PL_N] = {0, 0, 0, 0, 2, 1, 2, 2, 2, 1};
static const uint16_t PL_OFF[PL_N] = {0, 78, 78 + 112, 78 + 112 + 166, 0, 0, 1139, 1139 + 2350, 1139 + 2350 + 1318,
                                      1404};
static struct {
    uint16_t w[3];                       /* the buffers' write pointers */
    int32_t lp1, lp2, x, m, s;           /* the branches' low-passes; the send's last sample; the last mid, side */
    uint32_t ph1, ph2;
    uint8_t lo;                          /* the last block was quiet: towards zero (as HALL's lo) */
} plt;
#define PL_AI __attribute__((always_inline)) static inline
PL_AI int16_t *pl_at(const uint32_t *w, uint32_t k, uint32_t d)   /* line k, d samples back (d < its length) */
{
    uint32_t g = PL_GRP[k], t = g == 0u ? PL_T0 : g == 1u ? PL_T1 : PL_T2, i = w[g] + PL_OFF[k] + d;
    return (g == 0u ? rev_u.ap : g == 1u ? rev_comb : pl_buf) + (i >= t ? i - t : i);
}
PL_AI int32_t pl_rd(const uint32_t *w, uint32_t k, uint32_t d) { return *pl_at(w, k, d); }
PL_AI void pl_wr(const uint32_t *w, uint32_t k, uint32_t d, int32_t v) { *pl_at(w, k, d) = (int16_t)clamp32(v, -32768, 32767); }
PL_AI int32_t pl_sh(int32_t m, uint32_t sh, int lo)   /* the loops' products: to the nearest, or towards zero */
{
    return lo ? sh_tz(m, sh) : sh_rn(m, sh);
}
PL_AI int32_t pl_interp(const uint32_t *w, uint32_t k, uint32_t d8, int lo)   /* .. d8 / 256 back, between samples */
{
    int32_t a = pl_rd(w, k, d8 >> 8), b = pl_rd(w, k, (d8 >> 8) + 1u);
    return a + pl_sh((b - a) * (int32_t)(d8 & 255u), 8, lo);
}
PL_AI int32_t pl_ap(const uint32_t *w, uint32_t k, int32_t acc, int32_t g, int lo)   /* an allpass: Read TAIL x g,
                                                                                   * WriteAllPass x -g */
{
    int32_t r = pl_rd(w, k, PL_LEN[k] - 1u);
    acc += pl_sh(r * g, 15, lo);
    pl_wr(w, k, 0, acc);
    return pl_sh(clamp32(acc, -32768, 32767) * -g, 15, lo) + r;
}
/* a block (lo: the rounding, a constant: a function each, no test a product) */
PL_AI uint32_t plate_run(const int32_t *in_, int32_t *out, int lo)
{
    int32_t krt = space_on ? 32604 : 11469 + (int32_t)rv.size * 162;   /* 0.35 .. 0.98 (Q15) */
    int32_t klp = 32767 - (int32_t)rv.damp * 193, xp = plt.x, mp = plt.m, sp = plt.s, lp1 = plt.lp1, lp2 = plt.lp2;
    uint32_t i, l1, l2, pk = 0, w[3] = {plt.w[0], plt.w[1], plt.w[2]};
    plt.ph1 += PL_LFO1;
    plt.ph2 += PL_LFO2;
    l1 = (uint32_t)(sine_at(plt.ph1) + 32768) >> 1;   /* (0 .. 32767: Clouds' cosines, 0 .. 1) */
    l2 = (uint32_t)(sine_at(plt.ph2) + 32768) >> 1;
    l1 = (7u << 8) + ((PL_SW1 * l1) >> 7);              /* (the taps, d8) */
    l2 = (3225u << 8) + ((PL_SW2 * l2) >> 7);
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (xp + 2 * in_[2u * i] + in_[2u * i + 1u]) >> 2, acc, ap, wl, wr, mm, ss;   /* (1 2 1) */
        xp = in_[2u * i + 1u];
        w[0] = w[0] ? w[0] - 1u : PL_T0 - 1u;
        w[1] = w[1] ? w[1] - 1u : PL_T1 - 1u;
        w[2] = w[2] ? w[2] - 1u : PL_T2 - 1u;
        acc = pl_interp(w, PL_AP1, l1, lo);             /* the smear: AP1 into itself, 69 back */
        pl_wr(w, PL_AP1, 69, acc);
        acc = mulq15(clamp32(x, -262143, 262143), PL_GAIN) >> 1;   /* (Q15 audio -> the lines' Q14) */
        acc = pl_ap(w, PL_AP1, acc, PL_KAP, lo);
        acc = pl_ap(w, PL_AP2, acc, PL_KAP, lo);
        acc = pl_ap(w, PL_AP3, acc, PL_KAP, lo);
        ap = pl_ap(w, PL_AP4, acc, PL_KAP, lo);
        acc = clamp32(ap + pl_sh(pl_interp(w, PL_DEL2, l2, lo) * krt, 15, lo), -32768, 32767);
        lp1 = acc + pl_sh((lp1 - acc) * (32767 - klp), 15, lo);   /* (towards the input: no stuck DC) */
        acc = pl_ap(w, PL_D1A, lp1, -PL_KAP, lo);
        acc = pl_ap(w, PL_D1B, acc, PL_KAP, lo);
        pl_wr(w, PL_DEL1, 0, acc);
        wl = 2 * clamp32(acc, -32768, 32767);
        acc = clamp32(ap + pl_sh(pl_rd(w, PL_DEL1, PL_LEN[PL_DEL1] - 1u) * krt, 15, lo), -32768, 32767);
        lp2 = acc + pl_sh((lp2 - acc) * (32767 - klp), 15, lo);
        acc = pl_ap(w, PL_D2A, lp2, PL_KAP, lo);
        acc = pl_ap(w, PL_D2B, acc, -PL_KAP, lo);
        pl_wr(w, PL_DEL2, 0, acc);
        wr = 2 * clamp32(acc, -32768, 32767);
        mm = ((wl + wr) * PL_OUT) >> 2;              /* (Q14 x 2: back to the others' level) */
        ss = ((wl - wr) * PL_OUT) >> 2;
        out[2u * i] += (mp + mm) >> 1;               /* back to 44.1 kHz, between samples */
        out[2u * i + 1u] += mm;
        rev_side[2u * i] = (sp + ss) >> 1;
        rev_side[2u * i + 1u] = ss;
        mp = mm;
        sp = ss;
        pk |= (uint32_t)iabs(wl) | (uint32_t)iabs(wr);
    }
    plt.x = xp, plt.m = mp, plt.s = sp, plt.lp1 = lp1, plt.lp2 = lp2;
    plt.w[0] = (uint16_t)w[0], plt.w[1] = (uint16_t)w[1], plt.w[2] = (uint16_t)w[2];
    return pk;
}
static __attribute__((noinline)) uint32_t plate_rn(const int32_t *in_, int32_t *out) { return plate_run(in_, out, 0); }
static __attribute__((noinline)) uint32_t plate_tz(const int32_t *in_, int32_t *out) { return plate_run(in_, out, 1); }
static void rev_plate(const int32_t *in_, int32_t *out, uint32_t n)
{
    (void)n;
    plt.lo = (plt.lo ? plate_tz(in_, out) : plate_rn(in_, out)) < PL_QUIET;
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
    spr.h1 = spr.h2 = spr.h3 = spr.lp = spr.hp = spr.he = spr.m = spr.s = spr.om = spr.os = 0;
    hl.x = hl.m = hl.s = hl.hp = hl.he = 0;
    for (i = 0; i < sizeof pl_buf / 2u; i++)
        pl_buf[i] = 0;
    plt.lp1 = plt.lp2 = plt.x = plt.m = plt.s = 0;
}

static void rev_model(const int32_t *in_, int32_t *out, uint32_t n)   /* the model running, added to out */
{
    if (rv.rtype == 3u)
        rev_plate(in_, out, n);
    else if (rv.rtype == 2u)
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

/* 18b: the reverb runs on the mixer's last send while the voices render (either core: voices.c rev_c1). 1.0 Phase 10f:
 * the mixer a block behind the voices, the send (in, fed: voices.c rev_ib, the half the mixer filled last block)
 * goes through the model into one half of the return (rev_ret[o]) while the mixer adds the other (rev_rs), last
 * block's: two blocks late (2.9 ms), inaudible on a send. With no send and a return below 4 for 255 blocks (0.37 s),
 * it sleeps (the voices' CPU back); a send wakes it. A return far past any level the models reach (a corrupted
 * line) clears them and mutes the block (JIANT's range check) */
static int32_t rev_ret[2][2][BLOCK] POOL;           /* the return, a pair, each left and right */
static uint8_t rev_ok[2];                           /* rev_ret[o] holds a return */
static uint8_t rev_rs;                              /* the half reverb_add takes; reverb_job fills the other */
#define REV_WILD (1 << 22)
_Static_assert(BLOCK == 64u, "reverb_process: the LEVEL ramp's / BLOCK as a shift");
static void reverb_process(const int32_t *in, uint32_t fed, uint32_t o)
{
    int32_t *wet = rev_wet, *wl = rev_ret[o][0], *wr = rev_ret[o][1];
    uint32_t i, rt = rv.type < NREV ? rv.type : 0u;
    int32_t pk = 0;
    rev_ok[o] = 0;
    if (fed || space_on)
        rv.idle = 0;
    else if (rv.idle >= 255u)
        return;
    for (i = 0; i < BLOCK; i++)
        wet[i] = 0;
    hl.side = 0;
    if (rt != rv.rtype)
        rev_switch(in, wet, BLOCK, rt);
    else
        rev_model(in, wet, BLOCK);
    if (rv.level == rv.lvl_last && !hl.side) {      /* (LEVEL still: one gain; mono; 1.4) */
        int32_t g = rv.level << 1;
        for (i = 0; i < BLOCK; i++) {
            int32_t w = wet[i];
            wl[i] = wr[i] = mulq16(w, g);
            pk |= iabs(w);
        }
    } else if (rv.level == rv.lvl_last) {           /* (.. stereo) */
        int32_t g = rv.level << 1;
        for (i = 0; i < BLOCK; i++) {
            int32_t w = wet[i], s = rev_side[i];
            wl[i] = mulq16(w + s, g);
            wr[i] = mulq16(w - s, g);
            pk |= iabs(w) | iabs(s);
        }
    } else
        for (i = 0; i < BLOCK; i++) {               /* (LEVEL ramped from the last block's: no zipper as it moves) */
            int32_t w = wet[i], s = hl.side ? rev_side[i] : 0,
                    g = rv.lvl_last + sh_tz((rv.level - rv.lvl_last) * (int32_t)(i + 1u), 6);   /* (/ BLOCK) */
            wl[i] = mulq16(w + s, g << 1);
            wr[i] = mulq16(w - s, g << 1);
            pk |= iabs(w) | iabs(s);
        }
    rv.lvl_last = rv.level;
    if (pk >= REV_WILD) {
        rev_clear();
        return;
    }
    rev_ok[o] = 1;
    if (!fed && pk < 4)                              /* (silent: count towards sleep, 255 = asleep) */
        rv.idle = (uint8_t)(rv.idle + 1u);
    else if (!fed)
        rv.idle = 0;
}
static void reverb_job(void)                         /* (either core, beside the voices) */
{
    reverb_process(rev_ib[rev_in == rev_ib[0]], rev_fed_r, rev_rs ^ 1u);
}
#ifdef FX_NOW
static void reverb_now(void)                         /* (host tests: this block's send, into the return added now) */
{
    reverb_process(rev_in, rev_fed, rev_rs);
}
#endif
static void reverb_turn(void)                        /* (voices_turn) */
{
    rev_rs ^= 1u;
}
/* 1.0 Phase 16: REV > FX (fx.rev_fx, Q15): that share of the return into the FX bus's input (before fxbus_process),
 * the rest into the mix (reverb_add, after it). The return is blocks old already: no new delay */
static uint32_t rev_half(void)
{
#ifdef FX_NOW
    if (fx_now)                                      /* (host tests: reverb_now fills this block's after the bus: the */
        return rev_rs ^ 1u;                          /* .. bus takes the last block's) */
#endif
    return rev_rs;
}
static void reverb_bus(void)
{
    uint32_t i, o = rev_half();
    const int32_t *wl = rev_ret[o][0], *wr = rev_ret[o][1];
    const int32_t a = fx.rev_fx;
    if (!a || !rev_ok[o])
        return;
    for (i = 0; i < BLOCK; i++) {
        fx_l[i] = (int16_t)sat16(fx_l[i] + (int32_t)(((int64_t)wl[i] * a) >> 15));
        fx_r[i] = (int16_t)sat16(fx_r[i] + (int32_t)(((int64_t)wr[i] * a) >> 15));
    }
}
static void reverb_add(void)                         /* the return into the mix (after the mixer and SPACE's dip) */
{
    uint32_t i;
    const int32_t *wl = rev_ret[rev_rs][0], *wr = rev_ret[rev_rs][1];
    const int32_t a = fx.rev_fx;
    if (!rev_ok[rev_rs])
        return;
    if (a) {                                         /* (REV > FX: the rest; the bus has its share) */
        for (i = 0; i < BLOCK; i++) {
            mix_l[i] += wl[i] - (int32_t)(((int64_t)wl[i] * a) >> 15);
            mix_r[i] += wr[i] - (int32_t)(((int64_t)wr[i] * a) >> 15);
        }
        return;
    }
    for (i = 0; i < BLOCK; i++) {
        mix_l[i] += wl[i];
        mix_r[i] += wr[i];
    }
}

static void reverb_init(void)
{
    rev_clear();
    rev_ok[0] = rev_ok[1] = 0;
    rv.type = rv.rtype = 0;
    rv.size = 64;
    rv.damp = 48;
    rv.level = 25800;                                /* 100 / 127 */
    rv.idle = 255;
}
