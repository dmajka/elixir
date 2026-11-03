/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir 0.25 (test): ENSEMBLE, a MODEL on any voice after 4ms' Ensemble Oscillator (github.com/4ms/enosc, src/
 * oscillator.hh, distortion.hh, polyptic_oscillator.hh, quantizer.hh, dynamic_data.cc; Copyright (c) 2020 Matthias
 * Puech and Dan Green, BSD 2-clause: "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that ... the above copyright notice, this list of conditions and the
 * disclaimer" are kept; LICENSES/ has it in full).
 *
 * Eight sine oscillators stacked from the voice's pitch: each SPREAD semitones above the last, then quantized to a
 * SCALE. Like enosc, an oscillator between two of the scale's notes is a pair crossfading between them (the pair
 * alternates which of its two holds the lower note, so a sounding one is never retuned), DETUNE spreads them
 * alternately up and down, growing, and BALANCE tilts their levels (geometric: the lows or the highs louder). Each
 * oscillator's phase is TWISTed (FEEDBACK: phase modulation by its own low-passed output; PULSAR: the cycle squeezed
 * into its start; CRUSH: the phase's bits XORed with themselves) and its sine WARPed (FOLD: enosc's folder; CHEBY:
 * Chebyshev T1 .. T16, as cos(2 pi n (phase - 1/4)) of the sine's phase, exact, no table; SEGMENT: eight piecewise-linear
 * curves). Every amount fades towards Nyquist as enosc's Antialias does. TWIST and WARP each pass through their three
 * types in turn on one knob, each type's amount rising and falling back to the clean sine, so a sweep never jumps.
 * Differences from enosc: 8 oscillators (not 16), no cross-modulation, no stereo split, no freeze or learned scales,
 * phases set on a hit (a struck voice: the same attack each time), and the voice's amp EG shapes it.
 *
 * Knobs (the MODEL page's): SPREAD (DECAY: 0 .. 12 semitones an oscillator), WARP (BRIGHT), TWIST (STRUCT), BALANCE
 * (POSITION: 64 even), DETUNE (EXCITER), SCALE (TENSION: FREE OCTAVE FIFTH MAJOR MINOR PENTA HARMONIC SEMI).
 * Pitch: the voice's oscillator increment (its note, pitch EG, random, tape). Samples Q15, phases Q32, amounts Q16. */

#define NEO 8u                                       /* oscillators */
#define EN_SUB 32u                                   /* samples a pass (scratch on core 1's 4 KB stack) */
enum { EW_FOLD, EW_CHEBY, EW_SEGMENT, EW_NONE };
enum { ET_FEEDBACK, ET_PULSAR, ET_CRUSH };
enum { ES_FREE, ES_OCTAVE, ES_FIFTH, ES_MAJOR, ES_MINOR, ES_PENTA, ES_HARMONIC, ES_SEMI, NES };

typedef struct {
    uint32_t ph[2];                                  /* the pair's phases */
    int16_t fb[2];                                   /* FEEDBACK: each one's output, low-passed */
    int16_t xf;                                      /* the pair's crossfade to its second, Q15 (last block's) */
    int16_t pad;
} en_osc_t;
typedef struct {
    en_osc_t o[NEO];
    uint8_t on, strike;                              /* ENSEMBLE plays this voice; a hit to set up */
    uint8_t spread, warp, twist, bal, det, scale;    /* the knobs, 0..127 */
    int16_t lvl[NEO];                                /* the UI's picture: each oscillator's level (Q15) .. */
    int16_t pos[NEO];                                /* .. and its pitch above the voice's, semitones Q8 */
} ensemble_t;
static ensemble_t eno[NVOICE] RAMBUF;                 /* (RAMTEXT's .ram_buf: the SRAM reserve is short) */

static const int16_t EN_FOLD[514] = {   /* enosc's folder: -(x + sin 16xg) g, g = 1 / (1 + |x|), x -6 .. 6 */
    32355, 32338, 32322, 32304, 32287, 32268, 32250, 32230, 32211, 32190, 32169, 32148, 32126, 32103, 32080, 32056,
    32032, 32007, 31981, 31954, 31927, 31900, 31871, 31842, 31812, 31781, 31750, 31717, 31684, 31650, 31616, 31580,
    31544, 31506, 31468, 31429, 31389, 31348, 31306, 31263, 31218, 31173, 31127, 31080, 31031, 30982, 30931, 30879,
    30826, 30772, 30716, 30659, 30601, 30542, 30481, 30419, 30355, 30290, 30224, 30156, 30086, 30016, 29943, 29869,
    29793, 29716, 29636, 29556, 29473, 29389, 29303, 29215, 29125, 29033, 28939, 28843, 28746, 28646, 28544, 28440,
    28334, 28226, 28115, 28003, 27888, 27770, 27651, 27529, 27404, 27277, 27148, 27016, 26882, 26745, 26605, 26463,
    26318, 26171, 26020, 25867, 25711, 25553, 25391, 25227, 25060, 24890, 24717, 24541, 24362, 24180, 23996, 23808,
    23617, 23424, 23227, 23028, 22825, 22620, 22412, 22201, 21987, 21770, 21551, 21329, 21104, 20877, 20647, 20414,
    20180, 19943, 19704, 19463, 19220, 18975, 18729, 18481, 18232, 17982, 17731, 17479, 17227, 16975, 16723, 16471,
    16220, 15970, 15721, 15474, 15229, 14987, 14748, 14512, 14280, 14053, 13830, 13613, 13403, 13199, 13003, 12815,
    12636, 12467, 12308, 12161, 12026, 11904, 11796, 11703, 11626, 11566, 11525, 11503, 11501, 11520, 11562, 11628,
    11718, 11835, 11979, 12151, 12352, 12584, 12848, 13144, 13473, 13837, 14235, 14669, 15138, 15644, 16185, 16763,
    17375, 18023, 18703, 19416, 20158, 20928, 21723, 22539, 23372, 24218, 25071, 25924, 26771, 27605, 28415, 29193,
    29927, 30607, 31219, 31751, 32186, 32512, 32710, 32766, 32661, 32380, 31905, 31220, 30311, 29165, 27770, 26120,
    24209, 22041, 19622, 16967, 14099, 11051, 7866, 4599, 1318, -1896, -4950, -7737, -10143, -12042, -13309, -13819,
    -13459, -12138, -9797, -6424, -2070, 3135, 8967, 15101, 21100, 26431, 30479, 32598, 32174, 28725, 22021, 12221,
    0, -12221, -22021, -28725, -32174, -32598, -30479, -26431, -21100, -15101, -8967, -3135, 2070, 6424, 9797, 12138,
    13459, 13819, 13309, 12042, 10143, 7737, 4950, 1896, -1318, -4599, -7866, -11051, -14099, -16967, -19622, -22041,
    -24209, -26120, -27770, -29165, -30311, -31220, -31905, -32380, -32661, -32766, -32710, -32512, -32186, -31751, -31219, -30607,
    -29927, -29193, -28415, -27605, -26771, -25924, -25071, -24218, -23372, -22539, -21723, -20928, -20158, -19416, -18703, -18023,
    -17375, -16763, -16185, -15644, -15138, -14669, -14235, -13837, -13473, -13144, -12848, -12584, -12352, -12151, -11979, -11835,
    -11718, -11628, -11562, -11520, -11501, -11503, -11525, -11566, -11626, -11703, -11796, -11904, -12026, -12161, -12308, -12467,
    -12636, -12815, -13003, -13199, -13403, -13613, -13830, -14053, -14280, -14512, -14748, -14987, -15229, -15474, -15721, -15970,
    -16220, -16471, -16723, -16975, -17227, -17479, -17731, -17982, -18232, -18481, -18729, -18975, -19220, -19463, -19704, -19943,
    -20180, -20414, -20647, -20877, -21104, -21329, -21551, -21770, -21987, -22201, -22412, -22620, -22825, -23028, -23227, -23424,
    -23617, -23808, -23996, -24180, -24362, -24541, -24717, -24890, -25060, -25227, -25391, -25553, -25711, -25867, -26020, -26171,
    -26318, -26463, -26605, -26745, -26882, -27016, -27148, -27277, -27404, -27529, -27651, -27770, -27888, -28003, -28115, -28226,
    -28334, -28440, -28544, -28646, -28746, -28843, -28939, -29033, -29125, -29215, -29303, -29389, -29473, -29556, -29636, -29716,
    -29793, -29869, -29943, -30016, -30086, -30156, -30224, -30290, -30355, -30419, -30481, -30542, -30601, -30659, -30716, -30772,
    -30826, -30879, -30931, -30982, -31031, -31080, -31127, -31173, -31218, -31263, -31306, -31348, -31389, -31429, -31468, -31506,
    -31544, -31580, -31616, -31650, -31684, -31717, -31750, -31781, -31812, -31842, -31871, -31900, -31927, -31954, -31981, -32007,
    -32032, -32056, -32080, -32103, -32126, -32148, -32169, -32190, -32211, -32230, -32250, -32268, -32287, -32304, -32322, -32338,
    -32355, -32355,
};
static const uint16_t EN_FMAX[66] = {   /* its gain, Q12, at amounts 0 .. 1/16 (above: the last) */
    65535, 38311, 19452, 13201, 10103, 8268, 7064, 6222, 5607, 5144, 4789, 4514, 4299, 4131, 4003, 3906,
    3838, 3794, 3772, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768,
    3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768,
    3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768, 3768,
    3768, 3768,
};
/* SEGMENT's curves (enosc's triangles), in twelfths, -1 .. 1 in 8 segments */
static const int8_t EN_TRI[8][9] = {
    {-12, -9, -6, -3, 0, 3, 6, 9, 12}, {-12, -12, -8, -4, 0, 4, 8, 12, 12}, {-12, -12, -12, -6, 0, 6, 12, 12, 12},
    {-12, -12, -12, -12, 0, 12, 12, 12, 12}, {-12, -6, -12, -6, 0, 6, 12, 6, 12}, {-12, -6, 0, -12, 0, 12, 0, 6, 12},
    {-12, -6, 12, -12, 0, 12, -12, 6, 12}, {12, -12, 12, -12, 0, 12, -12, 12, -12}};
/* the scales (enosc's defaults), semitones Q8; the last is the period */
static const uint16_t EN_SC_HARM[9] = {0, 522, 989, 1411, 1797, 2152, 2480, 2786, 3072};   /* (harmonics 8 .. 16) */
static const uint16_t EN_SC_ET[NES][7] = {
    [ES_OCTAVE] = {0, 3072}, [ES_FIFTH] = {0, 1792, 3072}, [ES_MAJOR] = {0, 1024, 1792, 3072},
    [ES_MINOR] = {0, 768, 1792, 3072}, [ES_PENTA] = {0, 512, 1024, 1792, 2304, 3072}, [ES_SEMI] = {0, 256}};
static const uint8_t EN_SC_N[NES] = {0, 2, 3, 4, 4, 6, 9, 2};
static const char *const EN_SC_NAME[NES] = {"FREE", "OCTAVE", "FIFTH", "MAJOR", "MINOR", "PENTA", "HARMONIC", "SEMI"};

/* sin(2 pi ph), Q15 (dsp.c's sine_at is -cos) */
static inline int32_t en_sin(uint32_t ph) { return sine_at(ph + 0x40000000u); }
static inline uint32_t en_mq16(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 16); }
static inline uint32_t en_sub(uint32_t a) { return a < 65536u ? 65536u - a : 0u; }   /* (1 - a, not below 0, Q16) */
/* a TWIST or WARP knob: its type (zone) and amount (a hump over the zone), Q16 */
static uint32_t en_zone(uint32_t k, uint32_t *amt)
{
    uint32_t k6 = k * 6u, z = k6 / 254u, u, t;
    z = z > 2u ? 2u : z;
    u = k6 - z * 254u;
    t = u <= 127u ? u : 254u - u;
    *amt = t >= 124u ? 65536u : t * 529u;
    return z;
}
static inline uint32_t en_scale_of(uint32_t k) { return k * NES / 128u; }
static inline const uint16_t *en_scale(uint32_t s) { return s == ES_HARMONIC ? EN_SC_HARM : EN_SC_ET[s]; }

/* a hit (voice_trigger, core 0): the phases set at the voice's next block */
static void en_trigger(uint32_t vi) { eno[vi].strike = 1; }

/* the knobs: 0 SPREAD 1 WARP 2 TWIST 3 BALANCE 4 DETUNE 5 SCALE */
static void en_knob(uint32_t vi, uint32_t k, uint8_t v)
{
    ensemble_t *e = &eno[vi];
    switch (k) {
    case 0: e->spread = v; break;
    case 1: e->warp = v; break;
    case 2: e->twist = v; break;
    case 3: e->bal = v; break;
    case 4: e->det = v; break;
    default: e->scale = v; break;
    }
}

/* the oscillator i's pair of pitches (semitones Q8 above the voice's) and the crossfade between them (Q15) */
static void en_pitch(const ensemble_t *e, uint32_t i, int32_t *p1, int32_t *p2, int32_t *cf)
{
    uint32_t s = en_scale_of(e->scale), n, max, oct, semi, j;
    int32_t pos = (int32_t)(i * e->spread * 3072u / 127u), a, b, c;   /* (SPREAD: 0 .. 12 semitones each) */
    const uint16_t *sc;
    if (s == ES_FREE) {
        *p1 = *p2 = pos;
        *cf = 0;
        return;
    }
    sc = en_scale(s);
    n = EN_SC_N[s];
    max = sc[n - 1u];
    oct = (uint32_t)pos / max;
    semi = (uint32_t)pos - oct * max;
    for (j = 0; j + 2u < n && semi >= sc[j + 1u]; j++)
        ;
    a = (int32_t)(sc[j] + oct * max);
    b = (int32_t)(sc[j + 1u] + oct * max);
    c = (int32_t)(((semi - sc[j]) << 15) / (uint32_t)(sc[j + 1u] - sc[j]));
    if ((j + oct * (n + 1u)) & 1u) {                 /* (enosc: the pair takes turns holding the lower note) */
        int32_t t = a;
        a = b;
        b = t;
        c = 32768 - c;
    }
    c = (c - 4096) * 4 / 3;                          /* (crop 1/8 at each end: the notes easier to find) */
    *p1 = a;
    *p2 = b;
    *cf = c < 0 ? 0 : c > 32767 ? 32767 : c;
}

/* one oscillator for n samples: its phase twisted (kept in x for CHEBY), its sine, warped, x its gain ramp (g .. + gs a
 * sample, Q15), added to acc */
static void en_osc(uint32_t *php, int16_t *fbp, uint32_t inc, uint32_t tw, uint32_t ta, uint32_t wt, uint32_t wa,
                   int32_t g, int32_t gs, int32_t *acc, uint32_t n)
{
    uint32_t x[EN_SUB], i, ph = *php;
    int16_t s[EN_SUB];
    int32_t fb = *fbp;
    switch (tw) {                                    /* the phase twisted, its sine */
    case ET_FEEDBACK: {
        uint32_t off = ta << 16;                     /* (enosc: + the amount as a phase too) */
        for (i = 0; i < n; i++) {
            int32_t y;
            ph += inc;
            x[i] = ph + off + (uint32_t)(fb * (int32_t)ta);   /* (its low-passed output x the amount, half a cycle a unit) */
            y = en_sin(x[i]);
            fb += (y - fb) >> 2;
            s[i] = (int16_t)y;
        }
        break;
    }
    case ET_PULSAR:                                  /* (ta: the squeeze, Q8, 256 .. 16384) */
        for (i = 0; i < n; i++) {
            uint32_t q;
            ph += inc;
            q = (ph >> 16) * ta;
            x[i] = q >= 1u << 24 ? 0u : q << 8;
            s[i] = (int16_t)en_sin(x[i]);
        }
        break;
    default:                                         /* CRUSH */
        for (i = 0; i < n; i++) {
            ph += inc;
            x[i] = ph ^ ((ph >> 16) * ta) ^ (16384u * ta);
            s[i] = (int16_t)en_sin(x[i]);
        }
        break;
    }
    switch (wt) {                                    /* the sine warped */
    case EW_FOLD: {
        uint32_t gi = wa >> 6, gf = wa & 63u;        /* (its gain: EN_FMAX over 0 .. 1/16) */
        int32_t gn = gi >= 64u ? EN_FMAX[64]
                               : EN_FMAX[gi] + (int32_t)((((int32_t)EN_FMAX[gi + 1u] - (int32_t)EN_FMAX[gi]) * (int32_t)gf) >> 6);
        for (i = 0; i < n; i++) {
            uint32_t q = (uint32_t)(((s[i] * (int32_t)wa) >> 16) + 32768), k = q >> 7;   /* (512 intervals) */
            int32_t a = EN_FOLD[k], y = a + (((EN_FOLD[k + 1u] - a) * (int32_t)((q & 127u) << 8)) >> 15);
            s[i] = (int16_t)sat16((y * gn) >> 12);
        }
        break;
    }
    case EW_CHEBY: {                                 /* T(n1) .. T(n1 + 1), n1 = 1 + the amount x 14 */
        uint32_t q = wa * 14u, n1 = 1u + (q >> 16), fr = (q & 0xFFFFu) >> 1;
        if (n1 >= 15u) {
            n1 = 14u;
            fr = 32767u;
        }
        if (n1 == 1u && fr == 0u)
            break;
        for (i = 0; i < n; i++) {                    /* (sin 2 pi f = cos 2 pi (f - 1/4): T_n of it is cos 2 pi n (f - 1/4),
                                                      * -sine_at's) */
            uint32_t f = x[i] - 0x40000000u;
            int32_t a = -sine_at(f * n1), b = -sine_at(f * (n1 + 1u));
            s[i] = (int16_t)sat16(a + (((b - a) * (int32_t)fr) >> 15));
        }
        break;
    }
    case EW_NONE:
        break;
    default: {                                       /* SEGMENT: curve k and k + 1, blended */
        uint32_t q = wa * 7u, k = q >> 16, fr = (q & 0xFFFFu) >> 1;
        const int8_t *c0, *c1;
        if (k > 6u) {
            k = 6u;
            fr = 32767u;
        }
        if (k == 0u && fr == 0u)
            break;
        c0 = EN_TRI[k];
        c1 = EN_TRI[k + 1u];
        for (i = 0; i < n; i++) {
            uint32_t u = (uint32_t)(s[i] + 32768) * 8u, j = u >> 16, f = (u & 0xFFFFu) >> 1;
            int32_t a = (((c0[j] << 15) + (c0[j + 1u] - c0[j]) * (int32_t)f) * 2731) >> 15;   /* (twelfths to Q15) */
            int32_t b = (((c1[j] << 15) + (c1[j + 1u] - c1[j]) * (int32_t)f) * 2731) >> 15;
            s[i] = (int16_t)sat16(a + (((b - a) * (int32_t)fr) >> 15));
        }
        break;
    }
    }
    for (i = 0; i < n; i++, g += gs)
        acc[i] += (s[i] * g) >> 15;
    *php = ph;
    *fbp = (int16_t)fb;
}

#define EN_K 26214u                                  /* the level's balance term (enosc: 0.5 + a normalization; here
                                                      * 0.4: eight in phase at a hit, SPREAD 0, at 0.9) */
#define EN_G 65536u                                  /* the output's gain, Q16 */
/* one block of the voice's ENSEMBLE into buf (Q15): the voice's filter, amp EG and the rest follow */
static void en_block(uint32_t vi, const voice_t *v, int16_t *buf)
{
    ensemble_t *e = &eno[vi];
    struct { uint32_t inc, ta, wa; int32_t g, gs; uint8_t k, m; } mb[2u * NEO];
    int32_t acc[EN_SUB], x, bq, dcur, dacc = 0;
    uint32_t base = v->osc.inc, wt, wk, tt, tk, w, t, r, a, sum = 0, atten, amp[NEO], k, m, nm = 0, i, sb, d2;
    if (e->strike) {                                 /* (a hit: every phase at the sine's zero, rising) */
        e->strike = 0;
        for (k = 0; k < NEO; k++) {
            e->o[k].ph[0] = e->o[k].ph[1] = 0u;
            e->o[k].fb[0] = e->o[k].fb[1] = 0;
        }
    }
    wt = en_zone(e->warp, &wk);
    tt = en_zone(e->twist, &tk);
    if (!wk)                                         /* (WARP off: no pass; TWIST off: CRUSH by 0, the phase as it is) */
        wt = EW_NONE;
    if (!tk)
        tt = ET_CRUSH;
    w = wt == EW_FOLD ? en_mq16(en_mq16(wk, wk), 58982u) + 262u : wk;   /* (FOLD: x^2 0.9 + 0.004) */
    t = tt == ET_FEEDBACK ? en_mq16(en_mq16(tk, tk), 45875u)            /* (x^2 0.7) */
      : tt == ET_PULSAR ? (uint32_t)exp2_q16((int32_t)(6u * en_mq16(tk, tk))) >> 8   /* (2^(6 x^2), Q8) */
                        : en_mq16(tk, tk) >> 1;                         /* (x^2 0.5) */
    /* BALANCE: each oscillator r times the last's level, r = 2^(4 b^3), b -1 .. 1; the louder end at 1 */
    x = (int32_t)(e->bal * 131072u / 127u) - 65536;
    bq = (int32_t)(((((int64_t)x * x) >> 16) * x) >> 16) * 4;
    r = (uint32_t)exp2_q16(bq < 0 ? bq : -bq);
    for (k = 0, a = 65536u; k < NEO; k++) {
        amp[bq > 0 ? NEO - 1u - k : k] = a;
        sum += a;
        a = en_mq16(a, r);
    }
    atten = ((32768u + en_mq16(EN_K, r)) << 8) / (sum >> 8);   /* (Q16) */
    atten = en_mq16(atten, EN_G);
    d2 = (uint32_t)e->det * e->det;                  /* DETUNE: (d^4) 1.25 semitones, Q8 */
    dcur = (int32_t)(((uint64_t)d2 * d2 * 5283u) >> 32);
    for (k = 0; k < NEO; k++) {
        en_osc_t *o = &e->o[k];
        int32_t p[2], cf, xo = o->xf, xn;
        en_pitch(e, k, &p[0], &p[1], &cf);
        xn = xo + (((cf - xo) * 13) >> 4);
        o->xf = (int16_t)xn;
        if (!xo && !xn)                              /* (one of the pair silent: it follows the other) */
            o->ph[1] = o->ph[0];
        else if (xo >= 32767 && xn >= 32767)
            o->ph[0] = o->ph[1];
        e->pos[k] = (int16_t)(p[xn >= 16384] + dacc);
        e->lvl[k] = (int16_t)(en_mq16(amp[k], atten) >> 1);
        for (m = 0; m < 2u; m++) {
            int32_t oct = ((p[m] + dacc) << 8) / 12, g0, g1;   /* (semitones Q8 to octaves Q16) */
            uint64_t inc = ((uint64_t)base * (uint32_t)exp2_q16(oct)) >> 16;
            uint32_t f, ga, u;
            g0 = m ? xo : 32768 - xo;
            g1 = m ? xn : 32768 - xn;
            if ((!g0 && !g1) || inc >= 0x7FFFFFFFu)
                continue;
            f = (uint32_t)inc >> 16;                 /* (cycles a sample, Q16) */
            ga = en_mq16(en_mq16(amp[k], atten), en_sub(4u * en_mq16(f, f)));   /* (enosc: fade 1 - 4 f^2) */
            if (!ga)
                continue;
            mb[nm].inc = (uint32_t)inc;
            mb[nm].k = (uint8_t)k;
            mb[nm].m = (uint8_t)m;
            mb[nm].g = (int32_t)(((int64_t)ga * g0) >> 16);   /* (Q15) */
            mb[nm].gs = ((int32_t)(((int64_t)ga * g1) >> 16) - mb[nm].g) / (int32_t)BLOCK;
            u = en_sub(2u * f);                      /* (1 - 2f) */
            if (tt == ET_FEEDBACK)
                mb[nm].ta = en_mq16(t, en_mq16(u, u));
            else if (tt == ET_PULSAR) {
                u = en_mq16(u, u); u = en_mq16(u, u); u = en_mq16(u, u); u = en_mq16(u, u);   /* (^16) */
                mb[nm].ta = 256u + en_mq16(t - 256u, u);
            } else
                mb[nm].ta = t;
            if (wt == EW_FOLD) {
                u = en_sub(8u * f);
                u = en_mq16(u, u);
                u = en_mq16(w, en_mq16(u, u));
                mb[nm].wa = u > 262u ? u : 262u;
            } else if (wt == EW_CHEBY || wt == EW_NONE) {
                mb[nm].wa = en_mq16(w, en_sub(6u * f));
            } else {
                u = en_sub(4u * f);
                mb[nm].wa = en_mq16(w, en_mq16(u, en_mq16(u, u)));
            }
            nm++;
        }
        dcur = -dcur * 77 / 64;                      /* (enosc: x -1.2, accumulated) */
        dacc += dcur;
    }
    for (sb = 0; sb < BLOCK; sb += EN_SUB) {
        memset(acc, 0, sizeof acc);
        for (i = 0; i < nm; i++) {
            en_osc_t *o = &e->o[mb[i].k];
            en_osc(&o->ph[mb[i].m], &o->fb[mb[i].m], mb[i].inc, tt, mb[i].ta, wt, mb[i].wa, mb[i].g + mb[i].gs * (int32_t)sb,
                   mb[i].gs, acc, EN_SUB);
        }
        for (i = 0; i < EN_SUB; i++)
            buf[sb + i] = (int16_t)sat16(acc[i]);
    }
}
