/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 0.17 (Phase 24): MICRO, microsound voices. A MODEL on any track (the hats' voice: CH and OH) whose sound
 * replaces the voice's oscillators, FM and transient: a very short sound for glitch and microhouse beats. The design
 * follows GLITCH (ttreu's FM-1 firmware, github.com/ttreu/fm1-glitch, app/glitch_dsp.c, GPL-3.0): its sources, their
 * lengths, colours and levels; written here in fixed point (the FM-1 has no FPU).
 *   CLICK   a square pulse 0.1 .. 1.5 ms; COLOUR adds the opposite half (a doublet)
 *   TRAIN   3 .. 20 clicks, alternating and fading; COLOUR: speeding up .. slowing down
 *   CRUNCH  sample-and-hold noise, quantized, at the pitch x 8; COLOUR: smooth steps .. harsh, jittery
 *   BITS    an LFSR's bit, clocked at the pitch x 4; COLOUR: a short register (tonal) .. a long one (noise)
 *   BURST   a burst of noise, 2 .. 22 ms; COLOUR: dark .. bright
 *   SNAP    a gated band of noise with a click; COLOUR: dark .. bright
 *   GRAIN   noise through a two-pole resonator at the pitch x 4, a Hann window; COLOUR: wide .. resonant
 *   DUST    a crackle of random clicks; COLOUR: sparse .. dense
 *   POP     a jump that slides back; COLOUR: soft .. sharp edge
 *   CHIRP   a sine swept 4 octaves over its length; COLOUR: down .. up
 * The knobs (the MODEL page's): LENGTH (DECAY), COLOUR (BRIGHT), JITTER (STRUCT: each hit's length and colour, a
 * train's gaps), REVERSE (EXCITER: the chance a hit plays backwards: a swell into a cut, a train growing, a sweep the
 * other way), TENSION (a hit's pitch up, falling back). The voice's pitch (its note, pitch EG, random, tape) sets the
 * rate or the centre, relative to C4; its amp EG, filter, shape and the rest apply as to its own sound.
 *
 * A hit sets up at the voice's next block (mi_block, on the core that renders the voice), from the pitch then; each
 * voice has its own noise, so the sound doesn't depend on the core or the order. Samples Q15, envelopes Q30. */

enum { MI_CLICK, MI_TRAIN, MI_CRUNCH, MI_BITS, MI_BURST, MI_SNAP, MI_GRAIN, MI_DUST, MI_POP, MI_CHIRP, NMI };
_Static_assert(NMI == NMICRO, "rings.c: the MODEL list's MICRO types");
#define MI_C4 25480320u                              /* the oscillator's increment at C4 (261.63 Hz x 2^32 / FS) */
#define MI_HZ 97391u                                 /* .. at 1 Hz */

typedef struct {
    uint8_t type;                                    /* the kit's: 1 + MC_* (0: not MICRO) */
    uint8_t len, col, jit, rev, tens;                /* the knobs, 0..127 */
    uint8_t strike, on, st, back, hp;                /* a hit to set up; sounding; its type; backwards; the low cut */
    uint8_t vel;
    uint32_t rng;                                    /* its noise */
    int32_t n, cnt;                                  /* the sound's length and its sample */
    int32_t env, dmul;                               /* Q30; the multiplier a sample (Q30, 0: no envelope) */
    int32_t a1, a2, amp;                             /* coefficients */
    int32_t y1, y2, lp, held, lc;                    /* states (Q15; GRAIN's Q15 too) */
    uint32_t ph, inc, wph, winc;                     /* the rate's phase, its window's (GRAIN) */
    int32_t sweep;                                   /* CHIRP: the increment's multiplier a sample, Q30 */
    int32_t reps, gap, hold, width, ratio, spacing;  /* TRAIN: clicks left, the gap so far, its length; the ratio Q16;
                                                      * the gap Q8 */
    int32_t levels, inv_lv, sp;                      /* CRUNCH: the levels, 65536 / levels, the hold's speed Q16 */
    int32_t l, c;                                    /* this hit's LENGTH and COLOUR, Q16 */
    int32_t ten;                                     /* TENSION: Q16 over 1 */
    int32_t inv_n;                                   /* 2^31 / n */
} micro_t;
static micro_t mi[NVOICE];

/* the types' levels, Q12: GLITCH's loudness (A-weighted, measured there) x 0.25 (GRAIN's x 1.4, POP's x 0.75), so a
 * struck type peaks near an LXR voice (tests/render_test.c) */
static const uint16_t MI_GAIN[NMI] = {
    [MI_CLICK] = 2048, [MI_TRAIN] = 2253, [MI_CRUNCH] = 2253, [MI_BITS] = 3584, [MI_BURST] = 4608,
    [MI_SNAP] = 717, [MI_GRAIN] = 2007, [MI_DUST] = 2560, [MI_POP] = 2688, [MI_CHIRP] = 1024};

static inline int32_t mi_noise(micro_t *m) { return (int32_t)xs32(&m->rng) >> 16; }   /* -32768 .. 32767 */
static inline int32_t mi_q16(uint32_t k) { return (int32_t)(k >= 127u ? 65536u : k * 65536u / 127u); }
static inline int32_t mi_mul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }   /* (Q16) */
/* the multiplier a sample that falls 60 dB in n samples (1000^(-1/n) = 1 - x + x^2 / 2, x = ln 1000 / n), Q30;
 * up: its inverse (1 + x + x^2 / 2) */
static int32_t mi_fall(int32_t n, int up)
{
    int64_t x = (int64_t)(3708607808u / (uint32_t)(n < 8 ? 8 : n)) * 2, x2 = (x * x) >> 31;   /* (no 64-bit divide) */
    return (int32_t)((1ll << 30) + (up ? x : -x) + x2);
}
/* the level after s samples of a fall of n, Q30: 1000^(-s / n) */
static int32_t mi_after(int32_t s, int32_t n)
{
    uint32_t q = ((uint32_t)s << 16) / (uint32_t)(n < 8 ? 8 : n);   /* (s / n, Q16; s < 65536) */
    return exp2_q16(-(int32_t)(((int64_t)653124 * q) >> 16)) << 14;   /* (-log2 1000 x s / n) */
}
/* ms (Q16) in samples (x FS / 1000 = 44.1: 2^32 x 44.1 / 65536) */
static inline int32_t mi_samples(int32_t ms_q16) { return (int32_t)(((int64_t)ms_q16 * 2890138) >> 32); }

/* a hit (voice_trigger, core 0): set up at the voice's next block */
static void mi_trigger(uint32_t vi, uint32_t vel)
{
    mi[vi].strike = 1;
    mi[vi].vel = (uint8_t)vel;
}

/* the hit's sound from the voice's increment then (its pitch: r, Q16 over C4) */
static void mi_setup(micro_t *m, uint32_t inc)
{
    int32_t j = mi_q16(m->jit), l, c, r, n = 0, fall = 0;   /* (fall: samples to -60 dB; 0: no envelope) */
    uint32_t t = m->type - 1u;
    r = (int32_t)(inc / (MI_C4 >> 16));             /* (Q16: MI_C4 / 65536 = 388.8) */
    r = clamp32(r, 4096, 1048576);                   /* (1/16 .. 16) */
    l = mi_q16(m->len) + mi_mul(mi_mul(j, mi_noise(m) << 1), 16384);   /* (JITTER: +-0.25 at the most) */
    c = mi_q16(m->col) + mi_mul(mi_mul(j, mi_noise(m) << 1), 9830);    /* (.. +-0.15) */
    m->l = l = clamp32(l, 0, 65536);
    m->c = c = clamp32(c, 0, 65536);
    m->st = (uint8_t)t;
    m->back = (uint8_t)(m->rev && (m->rev >= 127u || (xs32(&m->rng) >> 25) < m->rev));
    m->on = 1;
    m->cnt = 0;
    m->env = 1 << 30;
    m->dmul = 0;
    m->y1 = m->y2 = m->lp = m->lc = 0;
    m->ph = m->wph = 0;
    m->hp = t != MI_POP;                             /* (the microsounds stay out of the bass; POP is the bass) */
    m->held = (xs32(&m->rng) & 0x400u) ? 32767 : -32767;
    switch (t) {
    case MI_CLICK:                                   /* 4 .. 66 samples at C4, shorter higher */
        n = ((4 << 16) + 62 * l) / r;
        m->held = mi_mul(m->held, 58982);            /* (0.9) */
        break;
    case MI_TRAIN: {                                 /* 3 .. 20 clicks, 2 .. 12 ms apart at C4 */
        int32_t i;
        m->reps = 3 + (17 * l >> 16);
        m->spacing = (int32_t)(((uint32_t)mi_samples(131072 + mi_mul(655360, 65536 - mi_mul(45875, l))) << 20) /
                               (uint32_t)(r >> 4));   /* (Q8) */
        m->ratio = 52429 + mi_mul(26214, c);         /* (0.8 .. 1.2) */
        m->width = 2 + (int32_t)(xs32(&m->rng) & 3u);
        m->held = mi_mul(m->held, 52429);            /* (0.8) */
        if (m->back) {                               /* quiet and wide first, loud and close last */
            for (i = 1; i < m->reps; i++) {
                m->held = -mi_mul(m->held, 55705);   /* (x -0.85) */
                if (i > 1)
                    m->spacing = mi_mul(m->spacing, m->ratio);
            }
            m->ratio = (int32_t)(0xFFFFFFFFu / (uint32_t)m->ratio);
        }
        m->hold = clamp32(m->spacing >> 8, 8, 2000);
        m->gap = 0;
        n = 0x7FFFFFFF;                              /* (it ends after its clicks) */
        break;
    }
    case MI_CRUNCH:
        fall = mi_samples(393216 + mi_mul(11796480, l));             /* 6 .. 186 ms */
        n = fall * 7 / 10;
        m->levels = 2 + (14 * (65536 - c) >> 16);
        m->inv_lv = 65536 / m->levels;
        m->sp = 65536;
        m->a1 = 9830 + mi_mul(22938, c);             /* (the steps' smoothing, Q15: 0.3 .. 1) */
        break;
    case MI_BITS: {
        uint32_t b = 4u + (uint32_t)(11 * c >> 16);  /* (the register: 4 .. 15 bits) */
        m->width = (int32_t)b;
        m->lp = (int32_t)((1u << b) - 1u);
        n = mi_samples(327680 + mi_mul(6553600, l)); /* 5 .. 105 ms */
        fall = n * 6 / 5;
        break;
    }
    case MI_BURST:
        n = mi_samples(131072 + mi_mul(1310720, l)); /* 2 .. 22 ms */
        fall = n * 3 / 2;
        m->a1 = clamp32(mi_mul(1638 + mi_mul(31130, c), r), 328, 32767);   /* (Q15: 0.05 .. 1, brighter higher) */
        break;
    case MI_SNAP:
        n = mi_samples(262144 + mi_mul(4587520, l)); /* 4 .. 74 ms */
        m->a1 = clamp32(mi_mul(3932 + mi_mul(26214, c), r), 655, 31130);   /* (the band's top, Q15) */
        m->a2 = mi_mul(m->a1, 7864);                 /* (its bottom: x 0.12) */
        break;
    case MI_GRAIN:
        n = mi_samples(262144 + mi_mul(3932160, l)); /* 4 .. 64 ms */
        m->winc = 0x80000000u / (uint32_t)(n < 2 ? 2 : n);   /* (half a turn over the grain) */
        break;
    case MI_DUST: {
        int64_t d = 60 * 65536 + 3000ll * mi_mul(c, c);              /* 60 .. 3060 clicks a second, Q16 */
        d = (d * clamp32(r, 16384, 262144)) >> 16;   /* (x 0.25 .. 4 with the pitch) */
        d = (d * 97391) >> 16;                       /* (the chance a sample, of 2^32) */
        m->inc = (uint32_t)(d > 0xFFFFFFFFll ? 0xFFFFFFFFll : d);
        n = mi_samples(3276800 + mi_mul(39321600, l));   /* 50 .. 650 ms */
        m->held = m->back ? 0 : m->held >> 1;        /* (a click at the hit, however sparse) */
        break;
    }
    case MI_POP:
        fall = mi_samples(196608 + mi_mul(7864320, mi_mul(l, l)));   /* 3 .. 123 ms */
        n = fall * 7 / 10 + 64;
        m->a1 = clamp32(mi_mul(328 + mi_mul(29491, mi_mul(c, c)), r), 98, 32767);
        break;
    default: {                                       /* MI_CHIRP: from the pitch x 2.68 (700 Hz at C4) */
        int32_t oct = (c - 32768) * 8, e;            /* (-4 .. +4 octaves, Q16) */
        uint64_t f = ((uint64_t)inc * 175636u) >> 16;
        n = mi_samples(327680 + mi_mul(5242880, l)); /* 5 .. 85 ms */
        f = f < 60u * MI_HZ ? 60u * MI_HZ : f > 12000u * MI_HZ ? 12000u * MI_HZ : f;
        e = (oct >> 4) * 11357 / n << 4;            /* (2^(oct / n) a sample: 1 + e, e = ln 2 oct / n, Q30) */
        if (m->back) {                               /* the sweep runs the other way: 1 / (1 + e) = 1 - e + e^2 */
            f = (f * (uint32_t)exp2_q16(oct)) >> 16;
            f = f < 30u * MI_HZ ? 30u * MI_HZ : f > 18000u * MI_HZ ? 18000u * MI_HZ : f;
            e = -e + (int32_t)(((int64_t)e * e) >> 30);
        }
        m->sweep = (1 << 30) + e;
        m->inc = (uint32_t)f;
        break;
    }
    }
    if (fall) {
        if (m->back) {                               /* the fall turned round: a swell from -40 dB into the cut */
            int32_t span = fall * 2 / 3;
            n = n < span ? n : span;
            m->env = mi_after(n, fall);
            m->dmul = mi_fall(fall, 1);
        } else {
            m->dmul = mi_fall(fall, 0);
        }
    }
    m->n = n < 2 ? 2 : n;
    m->inv_n = (int32_t)(0x7FFFFFFF / m->n);
    m->ten = (int32_t)(((uint32_t)m->tens * m->vel * 65536u) / 16129u);   /* (an octave at 127, full velocity) */
}

/* one block of the voice's MICRO sound into buf (Q15) */
static void mi_block(uint32_t vi, const voice_t *v, int16_t *buf)
{
    micro_t *m = &mi[vi];
    uint32_t i, f;
    int32_t g;
    f = (uint32_t)(((uint64_t)v->osc.inc * (uint32_t)(65536 + m->ten)) >> 16);
    if (m->strike) {
        m->strike = 0;
        mi_setup(m, f);
    }
    m->ten -= m->ten >> 5;                           /* (TENSION falls back, ~45 ms) */
    if (!m->on) {
        memset(buf, 0, BLOCK * sizeof buf[0]);
        return;
    }
    g = MI_GAIN[m->st];
    if (m->st == MI_CRUNCH || m->st == MI_BITS) {    /* the rate follows the pitch (its EG, TENSION) */
        uint64_t k = (uint64_t)f << (m->st == MI_CRUNCH ? 3 : 2);
        uint64_t lo = (m->st == MI_CRUNCH ? 200u : 100u) * (uint64_t)MI_HZ;
        uint64_t hi = (m->st == MI_CRUNCH ? 20000u : 22000u) * (uint64_t)MI_HZ;
        m->inc = (uint32_t)(k < lo ? lo : k > hi ? hi : k);
    } else if (m->st == MI_GRAIN) {                  /* .. the centre: 2 r cos w, r^2, the input (Q30, Q30, Q15) */
        uint64_t k = (uint64_t)f << 2, lo = 100u * (uint64_t)MI_HZ, hi = 12000u * (uint64_t)MI_HZ;
        int32_t rr = (1 << 30) - (int32_t)(3221225ll + ((85899346ll * (65536 - m->c)) >> 16));   /* Q30 */
        uint32_t w = (uint32_t)(k < lo ? lo : k > hi ? hi : k);
        m->a1 = (int32_t)((-(int64_t)sine_at(w) * 2 * rr) >> 15);       /* (sine_at: -cos) */
        m->a2 = (int32_t)(((int64_t)rr * rr) >> 30);
        /* the input (1 - r) 2 sin w x 3: the peak's gain the same at any centre and resonance (GLITCH's (1 - r) 6
         * rings louder low down) */
        m->amp = (int32_t)((((1ll << 30) - rr) * 6 * sine_at(w + 0x40000000u)) >> 30);
    }
    for (i = 0; i < BLOCK; i++) {
        int32_t y = 0, x;
        if (m->cnt >= m->n) {
            m->on = 0;
            for (; i < BLOCK; i++)
                buf[i] = 0;
            return;
        }
        x = (int32_t)(((int64_t)m->cnt * m->inv_n) >> 16);   /* 0 .. 32767 through the sound */
        switch (m->st) {
        case MI_CLICK: {
            int first = m->cnt < m->n / 2;
            if (m->back)
                first = !first;
            y = first ? m->held : -mi_mul(m->c, m->held);
            break;
        }
        case MI_TRAIN:
            if (m->gap < m->width)
                y = m->held;
            if (++m->gap >= m->hold) {
                int32_t s = m->spacing;
                m->gap = 0;
                m->held = m->back ? -(int32_t)(((int64_t)m->held * 77101) >> 16)   /* (x -1/0.85: growing) */
                                  : -mi_mul(m->held, 55705);
                m->held = clamp32(m->held, -32767, 32767);
                s = mi_mul(s, m->ratio);
                m->spacing = s;
                if (m->jit)                          /* (JITTER: each gap roughened, up to +-30%) */
                    s += mi_mul(mi_mul(s, mi_noise(m) << 1), mi_mul(mi_q16(m->jit), 19661));
                m->hold = clamp32(s >> 8, 8, 2000);
                if (--m->reps <= 0) {
                    m->on = 0;
                    for (; i < BLOCK; i++)
                        buf[i] = 0;
                    return;
                }
            }
            break;
        case MI_CRUNCH: {
            uint32_t last = m->ph;
            m->ph += (uint32_t)(((uint64_t)m->inc * (uint32_t)m->sp) >> 16);
            if (m->ph < last) {                      /* a new level; with COLOUR, fewer and at uneven times */
                int32_t q = (mi_noise(m) * m->levels) >> 15;
                m->held = (q * 32767 * m->inv_lv) >> 16;
                m->sp = clamp32(65536 + mi_mul(mi_mul(104858, m->c), mi_noise(m) << 1), 13107, 170394);
            }
            m->lp += (int32_t)(((int64_t)(m->held - m->lp) * m->a1) >> 15);
            y = mi_mul(m->lp, 36045);                /* (0.55) */
            break;
        }
        case MI_BITS: {
            uint32_t last = m->ph;
            m->ph += m->inc;
            if (m->ph < last) {
                uint32_t s = (uint32_t)m->lp, b = (s ^ (s >> 1)) & 1u;
                m->lp = (int32_t)((s >> 1) | (b << (m->width - 1)));
            }
            y = (m->lp & 1) ? 16384 : -16384;
            break;
        }
        case MI_BURST: {
            int32_t nz = mi_noise(m), br = nz - m->y1;
            m->y1 = nz;
            m->lp += (int32_t)(((int64_t)(nz - m->lp) * m->a1) >> 15);
            y = mi_mul(m->lp + mi_mul(m->c, (br >> 1) - m->lp), 45875);   /* (0.7) */
            break;
        }
        case MI_SNAP: {
            int32_t nz = mi_noise(m), gate, x2 = (x * x) >> 15;
            int click = m->back ? m->cnt + 3 >= m->n : m->cnt < 3;
            m->lp += (int32_t)(((int64_t)(nz - m->lp) * m->a1) >> 15);
            m->y1 += (int32_t)(((int64_t)(m->lp - m->y1) * m->a2) >> 15);
            gate = m->back ? x2 : 32767 - x2;        /* flat, then a quick close (backwards: a ramp) */
            y = (int32_t)(((int64_t)(m->lp - m->y1) * gate * 3) >> 16) + (click ? m->held : 0);
            break;
        }
        case MI_GRAIN: {
            int32_t nz = mi_noise(m), out, w;
            out = (int32_t)(((int64_t)nz * m->amp) >> 15) + (int32_t)(((int64_t)m->a1 * m->y1) >> 30) -
                  (int32_t)(((int64_t)m->a2 * m->y2) >> 30);
            m->y2 = m->y1;
            m->y1 = out;
            w = m->back ? x : sine_at(m->wph + 0x40000000u);   /* (sin pi x: a Hann window squared; backwards a ramp) */
            m->wph += m->winc;
            w = w < 0 ? 0 : w;
            y = (int32_t)(((int64_t)out * ((w * w) >> 15)) >> 15);
            break;
        }
        case MI_DUST: {
            uint32_t k = xs32(&m->rng);
            if (k < m->inc || (m->back && m->cnt == m->n - 48)) {   /* (backwards: a click before the cut) */
                int32_t nz = mi_noise(m);
                m->held = nz > 0 ? 16384 + (nz >> 1) : -16384 + (nz >> 1);
            }
            y = m->held;
            m->held = mi_mul(m->held, 22938);        /* (x 0.35) */
            y = mi_mul(y, (m->back ? x : 32767 - x) << 1);
            break;
        }
        case MI_POP:
            m->lp += (int32_t)(((int64_t)(m->held - m->lp) * m->a1) >> 15);   /* the edge of the jump */
            y = mi_mul(m->lp, 52429);                /* (0.8) */
            break;
        default: {                                   /* MI_CHIRP */
            int32_t e = m->back ? mi_mul(x << 1, clamp32((m->n - m->cnt < 22 ? m->n - m->cnt : 22) * 2979, 0, 65536))
                                : mi_mul(clamp32((m->cnt < 22 ? m->cnt : 22) * 2979, 0, 65536), (32767 - x) << 1);
            uint64_t k;
            m->ph += m->inc;
            k = ((uint64_t)m->inc * (uint32_t)m->sweep) >> 30;
            m->inc = (uint32_t)(k < 30u * (uint64_t)MI_HZ ? 30u * MI_HZ : k > 18000u * (uint64_t)MI_HZ ? 18000u * MI_HZ : k);
            y = mi_mul(sine_at(m->ph + 0x40000000u), mi_mul(e, 39322));    /* (0.6) */
            break;
        }
        }
        if (m->back && m->n - m->cnt < 32)           /* (no click at the cut) */
            y = y * (m->n - m->cnt) >> 5;
        m->cnt++;
        if (m->hp) {                                 /* (a one-pole low cut, ~210 Hz) */
            m->lc += ((y - m->lc) * 983) >> 15;
            y -= m->lc;
        }
        if (m->dmul) {
            y = (int32_t)(((int64_t)y * m->env) >> 30);
            m->env = (int32_t)(((int64_t)m->env * m->dmul) >> 30);
            if (m->env > 1 << 30)
                m->env = 1 << 30;
            if (!m->back && m->env < 1 << 19)        /* (-66 dB) */
                m->n = m->cnt;
        }
        buf[i] = (int16_t)sat16((y * g) >> 12);
    }
}
