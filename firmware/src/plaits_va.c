/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: plaits.c's pitched engines, part one: virtual analog (Plaits' VA_VARIANT 2: VariableShapeOscillator,
 * VariableSawOscillator), waveshaping and 2-op FM. Phases Q24 (0 .. 1, as Plaits' floats), frequencies Q24. */

/* the step of a ParameterInterpolator over n samples */
static inline int32_t pl_ip(int32_t from, int32_t to, uint32_t n) { return (to - from) / (int32_t)n; }

/* ------------------------------------------------ variable shape osc --- */
typedef struct { int32_t mph, sph, next, ppw, mf, sf, pw, ws; uint8_t high; } pl_vs_t;
static void vs_init(pl_vs_t *o)
{
    memset(o, 0, sizeof *o);
    o->ppw = o->pw = PL1 >> 1;
    o->sf = 167772;                                  /* 0.01 */
}
/* ComputeNaiveSample: the phase's saw, morphed to square (sq) and triangle (tri) */
static inline int32_t vs_naive(int32_t ph, int32_t pw, int32_t tri, int32_t sq)
{
    int32_t saw = ph, square = ph < pw ? 0 : PL1, t;
    saw += pm(square - saw, sq);
    if (tri) {
        t = ph < pw ? pl_div(ph, pw) : PL1 - pl_div(ph - pw, PL1 - pw);
        saw += pm(t - saw, tri);
    }
    return saw;
}
/* (slope_up + slope_down) frequency, the triangle's step */
static inline int32_t vs_tstep(int32_t f, int32_t pw) { return pl_div(f, pw) + pl_div(f, PL1 - pw); }
/* Render (sync: by mf, the master's frequency) */
static void vs_render(pl_vs_t *o, int sync, int32_t mf, int32_t f, int32_t pw, int32_t ws, int32_t *out, uint32_t n)
{
    int32_t smf, sff, spw, sws, ns = o->next;
    uint32_t i;
    mf = pmin(mf, PL1 >> 2);
    f = pmin(f, PL1 >> 2);
    pw = f >= (PL1 >> 2) ? PL1 >> 1 : pclamp(pw, 2 * f, PL1 - 2 * f);
    smf = pl_ip(o->mf, mf, n);
    sff = pl_ip(o->sf, f, n);
    spw = pl_ip(o->pw, pw, n);
    sws = pl_ip(o->ws, ws, n);
    for (i = 0; i < n; i++) {
        int reset = 0, tdr = 0;
        int32_t rt = 0, ts = ns, sq, tri, fr, w;
        ns = 0;
        o->mf += smf;
        o->sf += sff;
        o->pw += spw;
        o->ws += sws;
        fr = o->sf;
        w = o->pw;
        sq = pmax(o->ws - (PL1 >> 1), 0) * 2;
        tri = pmax(PL1 - 2 * o->ws, 0);
        if (sync) {
            o->mph += o->mf;
            if (o->mph >= PL1) {
                int32_t spr, val;
                o->mph -= PL1;
                rt = pclamp(pl_div(o->mph, o->mf), 0, PL1);
                spr = o->sph + pm(PL1 - rt, fr);
                reset = 1;
                if (spr >= PL1) {
                    spr -= PL1;
                    tdr = 1;
                }
                if (!o->high && spr >= w)
                    tdr = 1;
                val = vs_naive(spr, w, tri, sq);
                ts -= pm(val, blep_this(rt));
                ns -= pm(val, blep_next(rt));
            }
        }
        o->sph += fr;
        while (tdr || !reset) {
            if (!o->high) {
                int32_t t, tst;
                if (o->sph < w)
                    break;
                t = pclamp(pl_div(o->sph - w, o->ppw - w + fr), 0, PL1);
                tst = tri ? pm(vs_tstep(fr, w), tri) : 0;
                ts += pm(sq, blep_this(t));
                ns += pm(sq, blep_next(t));
                ts -= pm(tst, iblep_this(t));
                ns -= pm(tst, iblep_next(t));
                o->high = 1;
            }
            if (o->high) {
                int32_t t, tst;
                if (o->sph < PL1)
                    break;
                o->sph -= PL1;
                t = pclamp(pl_div(o->sph, fr), 0, PL1);
                tst = tri ? pm(vs_tstep(fr, w), tri) : 0;
                ts -= pm(PL1 - tri, blep_this(t));
                ns -= pm(PL1 - tri, blep_next(t));
                ts += pm(tst, iblep_this(t));
                ns += pm(tst, iblep_next(t));
                o->high = 0;
            }
        }
        if (sync && reset) {
            o->sph = pm(rt, fr);
            o->high = 0;
        }
        ns += vs_naive(o->sph, w, tri, sq);
        o->ppw = w;
        out[i] = 2 * ts - PL1;
    }
    o->mf = mf;
    o->sf = f;
    o->pw = pw;
    o->ws = ws;
    o->next = ns;
}

/* -------------------------------------------------- variable saw osc --- */
#define PL_NOTCH 3355443                             /* kVariableSawNotchDepth, 0.2 */
typedef struct { int32_t ph, next, ppw, f, pw, ws; uint8_t high; } pl_vsaw_t;
static void vsaw_init(pl_vsaw_t *o)
{
    memset(o, 0, sizeof *o);
    o->ppw = o->pw = PL1 >> 1;
    o->f = 167772;
}
static void vsaw_render(pl_vsaw_t *o, int32_t f, int32_t pw, int32_t ws, int32_t *out, uint32_t n)
{
    int32_t sff, spw, sws, ns = o->next;
    uint32_t i;
    f = pmin(f, PL1 >> 2);
    pw = f >= (PL1 >> 2) ? PL1 >> 1 : pclamp(pw, 2 * f, PL1 - 2 * f);
    sff = pl_ip(o->f, f, n);
    spw = pl_ip(o->pw, pw, n);
    sws = pl_ip(o->ws, ws, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns, fr, w, tri, notch, x;
        ns = 0;
        o->f += sff;
        o->pw += spw;
        o->ws += sws;
        fr = o->f;
        w = o->pw;
        tri = o->ws;
        notch = PL1 - o->ws;
        o->ph += fr;
        if (!o->high && o->ph >= w) {
            int32_t tst = pm(vs_tstep(fr, w), tri), nt = pm(PL_NOTCH + PL1 - w, notch);
            int32_t t = pclamp(pl_div(o->ph - w, o->ppw - w + fr), 0, PL1);
            ts += pm(nt, blep_this(t));
            ns += pm(nt, blep_next(t));
            ts -= pm(tst, iblep_this(t));
            ns -= pm(tst, iblep_next(t));
            o->high = 1;
        } else if (o->ph >= PL1) {
            int32_t tst = pm(vs_tstep(fr, w), tri), nt = pm(PL_NOTCH + PL1, notch), t;
            o->ph -= PL1;
            t = pclamp(pl_div(o->ph, fr), 0, PL1);
            ts -= pm(nt, blep_this(t));
            ns -= pm(nt, blep_next(t));
            ts += pm(tst, iblep_this(t));
            ns += pm(tst, iblep_next(t));
            o->high = 0;
        }
        x = pm(o->ph < w ? o->ph : PL1 + PL_NOTCH, notch);
        if (tri)
            x += pm(o->ph < w ? pl_div(o->ph, w) : PL1 - pl_div(o->ph - w, PL1 - w), tri);
        ns += x;
        o->ppw = w;
        out[i] = pm(2 * ts - PL1, 13981013);         /* / 1.2 */
    }
    o->f = f;
    o->pw = pw;
    o->ws = ws;
    o->next = ns;
}

/* ---------------------------------------------------- virtual analog --- */
typedef struct {
    pl_vs_t pri, aux, sync;
    pl_vsaw_t saw;
    int32_t sqg, swg;                                /* auxiliary_amount_, xmod_amount_ */
} pl_va_t;
static void va_init(pl_va_t *d)
{
    vs_init(&d->pri);
    vs_init(&d->aux);
    vs_init(&d->sync);
    vsaw_init(&d->saw);
    d->aux.mph = PL1 >> 2;
    d->sqg = d->swg = 0;
}
static inline int32_t pl_squash(int32_t x) { return pm(pm(x, x), 3 * PL1 - 2 * x); }
static int32_t va_detune(int32_t h)                  /* ComputeDetuning: semitones, Q16 */
{
    static const int32_t iv[5] = {0, 459407, 787087, 1245839, 1573519};   /* 0, 7.01, 12.01, 19.01, 24.01 */
    int32_t d = pclamp(pm(h, 34393293) - 17196646, -PL1, PL1), neg = d < 0, i, fr, r;   /* 2.05 h - 1.025 */
    d = pm(pabs(d), 67107186);                       /* x 3.9999 */
    i = d >> 24;
    fr = d & 0xFFFFFF;
    r = iv[i] + (int32_t)(((int64_t)(iv[i + 1] - iv[i]) * pl_squash(pl_squash(fr))) >> 24);
    return neg ? -r : r;
}
static void va_render(pl_va_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t tmp[PLSUB], sa = pm(p->timbre, p->timbre), det = va_detune(p->harm), t = p->timbre, m = p->morph;
    int32_t pf = (int32_t)(p->f0 >> 8), af = (int32_t)(pl_note_f(p->note + det) >> 8);
    int32_t sn = (int32_t)(((int64_t)sa * 48) >> 8);   /* sync_amount x 48, Q16 */
    int32_t psf = (int32_t)(pl_note_f(p->note + sn) >> 8), asf = (int32_t)(pl_note_f(p->note + det + sn) >> 8);
    int32_t shape = pclamp(m + (m >> 1), 0, PL1);
    int32_t pw = pclamp((PL1 >> 1) + pm(m - 11072963, 24494735), PL1 >> 1, 16693937);   /* (m - 0.66) 1.46, .. 0.995 */
    int32_t spw, ssr, sqg, swpw, sws, swg, norm, gs, gw;
    uint32_t i;
    vs_render(&d->pri, 1, pf, psf, pw, shape, out, n);
    vs_render(&d->aux, 1, af, asf, pw, shape, aux, n);
    for (i = 0; i < n; i++)
        aux[i] = (aux[i] - out[i]) >> 1;
    spw = pclamp(pm(t, 21810381) - 2516582, 83886, PL1 >> 1);   /* 1.3 t - 0.15, 0.005 .. 0.5 */
    ssr = t < (PL1 >> 1) ? 0 : (int32_t)(((int64_t)pm(t - (PL1 >> 1), t - (PL1 >> 1)) * 192) >> 8);   /* Q16 */
    sqg = pmin(8 * t, PL1);
    swpw = m < (PL1 >> 1) ? m + (PL1 >> 1) : PL1 - 2 * (m - (PL1 >> 1));
    swpw = pclamp(pm(swpw, 18454938), 83886, PL1);   /* x 1.1 */
    sws = pclamp(10 * PL1 - 21 * m, 0, PL1);
    swg = pclamp(8 * (PL1 - m), 335544, PL1);        /* 0.02 .. 1 */
    vs_render(&d->sync, 1, pf, (int32_t)(pl_note_f(p->note + ssr) >> 8), spw, PL1, tmp, n);
    vsaw_render(&d->saw, af, swpw, sws, out, n);
    norm = pl_div(PL1, pmax(sqg, swg));
    gs = pl_ip(d->sqg, pm(pm(sqg, 5033165), norm), n);   /* x 0.3 */
    gw = pl_ip(d->swg, pm(swg >> 1, norm), n);
    for (i = 0; i < n; i++) {
        d->sqg += gs;
        d->swg += gw;
        out[i] = pm(out[i], d->swg) + pm(d->sqg, tmp[i]);
    }
    d->sqg = pm(pm(sqg, 5033165), norm);
    d->swg = pm(swg >> 1, norm);
}

/* ------------------------------------------------------ slope oscillator --- */
/* Oscillator<OSCILLATOR_SHAPE_SLOPE>: a triangle of variable slope (pw), its corners polyBLAMPed */
typedef struct { int32_t ph, next, f, pw; uint8_t high; } pl_slope_t;
static void slope_init(pl_slope_t *o)
{
    o->ph = PL1 >> 1;
    o->next = 0;
    o->f = 16777;                                    /* 0.001 */
    o->pw = PL1 >> 1;
    o->high = 1;
}
static void slope_render(pl_slope_t *o, int32_t f, int32_t pw, int32_t *out, uint32_t n)
{
    int32_t sff, spw, ns = o->next;
    uint32_t i;
    f = pclamp(f, 17, PL1 >> 2);
    pw = pclamp(pw, 2 * f, PL1 - 2 * f);
    sff = pl_ip(o->f, f, n);
    spw = pl_ip(o->pw, pw, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns, fr, w, disc;
        ns = 0;
        o->f += sff;
        o->pw += spw;
        fr = o->f;
        w = o->pw;
        o->ph += fr;
        disc = vs_tstep(fr, w);
        if (o->high ^ (o->ph < w)) {
            int32_t t = pl_div(o->ph - w, fr);
            ts -= pm(iblep_this(t), disc);
            ns -= pm(iblep_next(t), disc);
            o->high = o->ph < w;
        }
        if (o->ph >= PL1) {
            int32_t t;
            o->ph -= PL1;
            t = pl_div(o->ph, fr);
            ts += pm(iblep_this(t), disc);
            ns += pm(iblep_next(t), disc);
            o->high = 1;
        }
        ns += o->high ? pl_div(o->ph, w) : PL1 - pl_div(o->ph - w, PL1 - w);
        out[i] = 2 * ts - PL1;
    }
    o->f = f;
    o->pw = pw;
    o->next = ns;
}

/* -------------------------------------------------------- waveshaping --- */
typedef struct { pl_slope_t slope, tri; int32_t shape, wfg, ovg; } pl_ws_t;
static void ws_init(pl_ws_t *d)
{
    slope_init(&d->slope);
    slope_init(&d->tri);
    d->shape = d->wfg = d->ovg = 0;
}
/* Tame: how much of a shaping a pitch takes, f0 x harmonics against the order's limit 0.5 / order (mf) */
static int32_t ws_tame(int32_t f0, int32_t harm, int32_t mf)
{
    int32_t f = pm(f0, harm), a = pclamp(PL1 - pl_div(f - mf, (PL1 >> 1) - mf), 0, PL1);
    return pm(pm(a, a), a);
}
/* InterpolateHermite(table + 1, index, 512), the Q15 fold tables */
static int32_t ws_hermite(const int16_t *tb, int32_t idx)
{
    int32_t i = (idx >> 15) + 1, f = (idx & 0x7FFF) << 9;
    int32_t xm1 = tb[i - 1] << 9, x0 = tb[i] << 9, x1 = tb[i + 1] << 9, x2 = tb[i + 2] << 9;
    int32_t c = (x1 - xm1) >> 1, v = x0 - x1, w = c + v, a = w + v + ((x2 - x0) >> 1), bn = w + a;
    return pm(pm(pm(a, f) - bn, f) + c, f) + x0;
}
static void ws_render(pl_ws_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t f0 = (int32_t)(p->f0 >> 8), pw = pm(p->morph, 7549747) + (PL1 >> 1);   /* 0.45 */
    int32_t slope = 3 * PL1 + 5 * pabs(p->morph - (PL1 >> 1)), sa = 2 * pabs(p->harm - (PL1 >> 1));
    int32_t sat = ws_tame(f0, slope, PL1 / 32), wfat, og, ss, sw, so, tsh, twf, tov;
    uint32_t i;
    slope_render(&d->slope, f0, pw, out, n);
    slope_render(&d->tri, f0, PL1 >> 1, aux, n);
    wfat = ws_tame(f0, pm(slope, 3 * PL1 + 5 * pm(sa, sat)), PL1 / 24);
    tsh = (PL1 >> 1) + pm(p->harm - (PL1 >> 1), sat);
    twf = 503316 + pm(7717519, pm(p->timbre, wfat));  /* 0.03 + 0.46 .. */
    og = pm(p->timbre, 2 * PL1 - p->timbre);
    tov = pm(og, 2 * PL1 - og);
    ss = pl_ip(d->shape, tsh, n);
    sw = pl_ip(d->wfg, twf, n);
    so = pl_ip(d->ovg, tov, n);
    for (i = 0; i < n; i++) {
        int32_t sh, si, sf, wi, wf, x0, x1, x, y0, y1, y, mix, idx, sine;
        const int16_t *t1, *t2;
        d->shape += ss;
        d->wfg += sw;
        d->ovg += so;
        sh = pm(d->shape, 67107186);                 /* x 3.9999 */
        si = sh >> 24;
        sf = sh & 0xFFFFFF;
        t1 = PL_WS + si * 257;
        t2 = t1 + 257;
        {   /* 127 out + 128 (unsigned: 128 is past int32's Q24) */
            uint32_t u = (uint32_t)(127 * out[i]) + (128u << 24);
            wf = (int32_t)(u & 0xFFFFFFu);
            wi = (int32_t)((u >> 24) & 255u);
        }
        x0 = t1[wi] << 9;
        x1 = t1[wi + 1] << 9;
        x = x0 + pm(x1 - x0, wf);
        y0 = t2[wi] << 9;
        y1 = t2[wi + 1] << 9;
        y = y0 + pm(y1 - y0, wf);
        mix = x + pm(y - x, sf);
        idx = pclamp(pm(mix, d->wfg) + (PL1 >> 1), 0, PL1 - 1);
        sine = pl_sin(pl_fq((aux[i] >> 2) + (PL1 >> 1)));
        out[i] = ws_hermite(PL_FOLD, idx);
        aux[i] = sine + pm(-ws_hermite(PL_FOLD2, idx) - sine, d->ovg);
    }
    d->shape = tsh;
    d->wfg = twf;
    d->ovg = tov;
}

/* ---------------------------------------------------------------- FM --- */
/* two operators and a sub, 4x oversampled */
typedef struct { uint32_t cph, mph, sph, pcf, pmf; int32_t amt, fb, prev, cfir, sfir; } pl_fm_t;
static const int32_t PL_DS_FIR[4] = {409769, 1559831, 2803966, 3615042};   /* lut_4x_downsampler_fir */
static void fm_init(pl_fm_t *d)
{
    memset(d, 0, sizeof *d);
    d->pcf = d->pmf = 5356535u;                      /* a0 */
}
static inline int32_t pl_sinpm(uint32_t ph, int32_t pmod) { return pl_sin(ph + ((uint32_t)pmod << 8)); }
static void fm_render(pl_fm_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t note = p->note - (24 << 16), i24 = PL_FM_Q[0], ratio, mnote, hft, tam, tfb, sa, sf, scf, smf;
    uint32_t tmf, tcf, i;
    {   /* Interpolate(lut_fm_frequency_quantizer, harmonics, 128) */
        int32_t x = p->harm * 128 >> 8, xi = x >> 16, xf = x & 0xFFFF;   /* Q16 */
        i24 = PL_FM_Q[xi] + (int32_t)(((int64_t)(PL_FM_Q[xi + 1] - PL_FM_Q[xi]) * xf) >> 16);
    }
    ratio = i24;
    mnote = note + ratio;
    tmf = pl_note_f(mnote);
    tmf = tmf > 0x80000000u ? 0x80000000u : tmf;
    hft = pclamp(PL1 - (int32_t)(((int64_t)(mnote - (72 << 16)) * 419430) >> 16), 0, PL1);   /* x 0.025 */
    hft = pm(hft, hft);
    tcf = pl_note_f(note);
    tam = pm(2 * pm(p->timbre, p->timbre), hft);
    tfb = 2 * p->morph - PL1;
    scf = ((int32_t)tcf - (int32_t)d->pcf) / (int32_t)n;
    smf = ((int32_t)tmf - (int32_t)d->pmf) / (int32_t)n;
    sa = pl_ip(d->amt, tam, n);
    sf = pl_ip(d->fb, tfb, n);
    for (i = 0; i < n; i++) {
        int32_t pfb, mfb, ch = d->cfir, ct = 0, sh = d->sfir, st = 0, j;
        uint32_t ci, mf;
        d->amt += sa;
        d->fb += sf;
        d->pcf += (uint32_t)scf;
        d->pmf += (uint32_t)smf;
        pfb = d->fb < 0 ? pm(d->fb, d->fb) >> 1 : 0;
        mfb = d->fb > 0 ? pm(d->fb, d->fb) >> 2 : 0;
        ci = d->pcf;
        mf = d->pmf;
        for (j = 0; j < 4; j++) {
            int32_t mod, car, sub;
            d->mph += (uint32_t)(((uint64_t)mf * (uint32_t)(PL1 + pm(d->prev, pfb))) >> 24);
            d->cph += ci;
            d->sph += ci >> 1;
            mod = pl_sinpm(d->mph, pm(mfb, d->prev));
            car = pl_sinpm(d->cph, pm(d->amt, mod));
            sub = pl_sinpm(d->sph, pm(pm(d->amt, car), PL1 >> 2));
            PL_ONE_POLE(d->prev, car, 838861);       /* 0.05 */
            ch += pm(car, PL_DS_FIR[3 - j]);
            ct += pm(car, PL_DS_FIR[j]);
            sh += pm(sub, PL_DS_FIR[3 - j]);
            st += pm(sub, PL_DS_FIR[j]);
        }
        out[i] = ch;
        aux[i] = sh;
        d->cfir = ct;
        d->sfir = st;
    }
    d->amt = tam;
    d->fb = tfb;
    d->pcf = tcf;
    d->pmf = tmf;
}

/* -------------------------------------------------------- grain/formant --- */
static inline int32_t pl_sine(int32_t ph) { return pl_sin(pl_fq(ph)); }   /* Sine(phase), phase Q24, wrapped */
typedef struct { int32_t cph, fph, next, cf, ff, shape, bleed; } pl_grl_t;
static int32_t grl_carrier(int32_t ph, int32_t shape)
{
    int32_t s = 3 * shape, si = s >> 24, t = PL1 - (s & 0xFFFFFF), t3 = pm(pm(t, t), t);
    if (si == 0) {
        ph = pmin(pm(ph, PL1 + 15 * t3), PL1) + (3 << 22);
    } else if (si == 1) {
        int32_t bp = 16777 + pm(8371831, t3);         /* 0.001 + 0.499 t^3 */
        ph = ph < bp ? pl_div(ph >> 1, bp) : (PL1 >> 1) + pl_div((ph - bp) >> 1, PL1 - bp);
        ph += 3 << 22;
    } else {
        t = PL1 - t;
        ph = pmin((PL1 >> 2) + pm(ph, (PL1 >> 1) + pm(243269632, pm(pm(t, t), t))), 3 << 22);   /* 14.5 */
    }
    return (pl_sine(ph) + PL1) >> 2;
}
static inline int32_t grl_grain(int32_t cph, int32_t fph, int32_t shape, int32_t bleed)
{
    return pl_div(pm(grl_carrier(cph, shape), pl_sine(fph) + bleed), PL1 + bleed);
}
static void grl_render(pl_grl_t *o, int32_t cf, int32_t ff, int32_t shape, int32_t bleed, int32_t *out, uint32_t n)
{
    int32_t scf, sff, ssh, sbl, ns = o->next;
    uint32_t i;
    cf = pmin(cf, PL1 >> 3);
    ff = pmin(ff, PL1 >> 2);
    scf = pl_ip(o->cf, cf, n);
    sff = pl_ip(o->ff, ff, n);
    ssh = pl_ip(o->shape, shape, n);
    sbl = pl_ip(o->bleed, bleed, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns, f0, f1;
        ns = 0;
        o->cf += scf;
        o->ff += sff;
        f0 = o->cf;
        f1 = o->ff;
        o->cph += f0;
        if (o->cph >= PL1) {
            int32_t rt, bt, before, after, dis;
            o->cph -= PL1;
            rt = pclamp(pl_div(o->cph, f0), 0, PL1);
            bt = PL1 - rt;
            before = grl_grain(PL1, o->fph + pm(bt, f1), o->shape + pm(ssh, bt), o->bleed + pm(sbl, bt));
            after = grl_grain(0, 0, o->shape + ssh, o->bleed + sbl);
            dis = after - before;
            ts += pm(dis, blep_this(rt));
            ns += pm(dis, blep_next(rt));
            o->fph = pm(rt, f1);
        } else {
            o->fph += f1;
            if (o->fph >= PL1)
                o->fph -= PL1;
        }
        o->shape += ssh;
        o->bleed += sbl;
        ns += grl_grain(o->cph, o->fph, o->shape, o->bleed);
        out[i] = ts;
    }
    o->cf = cf;
    o->ff = ff;
    o->shape = shape;
    o->bleed = bleed;
    o->next = ns;
}
typedef struct { int32_t cph, dph, fph, next, cf, ff, shape, mode; } pl_z_t;
static int32_t z_z(int32_t c, int32_t d, int32_t f, int32_t shape, int32_t mode)
{
    int32_t rd = (PL1 + pl_sine((d >> 1) + (PL1 >> 2))) >> 1, off, ps, dis, ct;
    if (mode < 5586944) {                            /* 0.333 */
        off = PL1;
        ps = (PL1 >> 2) + pm(mode, 25165824);         /* 1.5 */
    } else if (mode < 11173888) {                    /* 0.666 */
        ps = 12574474 - pm(mode - 5536481, 12582912);   /* 0.7495 - (mode - 0.33) 0.75 */
        off = -pl_sine(ps);
    } else {
        ps = 12574474 - pm(mode - 5536481, 12582912);
        off = 16777;                                 /* 0.001 */
    }
    dis = pl_sine(f + ps);
    if (shape < (PL1 >> 1)) {
        shape *= 2;
        if (c >= (PL1 >> 1))
            rd = pm(rd, shape);
        ct = PL1 + pm(pl_sine(c + (PL1 >> 2)) - PL1, shape);
    } else {
        ct = pl_sine(c + (shape >> 1));
    }
    return pm(pm(rd, off + dis) - off, ct);
}
static void z_render(pl_z_t *o, int32_t cf, int32_t ff, int32_t shape, int32_t mode, int32_t *out, uint32_t n)
{
    int32_t scf, sff, ssh, smd, ns = o->next;
    uint32_t i;
    cf = pmin(cf, PL1 >> 3);
    ff = pmin(ff, PL1 >> 2);
    scf = pl_ip(o->cf, cf, n);
    sff = pl_ip(o->ff, ff, n);
    ssh = pl_ip(o->shape, shape, n);
    smd = pl_ip(o->mode, mode, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns, f0, f1;
        ns = 0;
        o->cf += scf;
        o->ff += sff;
        f0 = o->cf;
        f1 = o->ff;
        o->dph += 2 * f0;
        o->cph += f0;
        if (o->dph >= PL1) {
            int32_t rt, bt, before, after, dis;
            o->dph -= PL1;
            rt = pclamp(pl_div(o->dph, 2 * f0), 0, PL1);
            bt = PL1 - rt;
            before = z_z(o->cph >= PL1 ? PL1 : PL1 >> 1, PL1, o->fph + pm(bt, f1), o->shape + pm(ssh, bt),
                         o->mode + pm(smd, bt));
            after = z_z(o->cph >= PL1 ? 0 : PL1 >> 1, 0, 0, o->shape + ssh, o->mode + smd);
            dis = after - before;
            ts += pm(dis, blep_this(rt));
            ns += pm(dis, blep_next(rt));
            o->fph = pm(rt, f1);
            if (o->cph > PL1)
                o->cph = o->dph >> 1;
        } else {
            o->fph += f1;
            if (o->fph >= PL1)
                o->fph -= PL1;
        }
        if (o->cph >= PL1)
            o->cph -= PL1;
        o->shape += ssh;
        o->mode += smd;
        ns += z_z(o->cph, o->dph, o->fph, o->shape, o->mode);
        out[i] = ts;
    }
    o->cf = cf;
    o->ff = ff;
    o->shape = shape;
    o->mode = mode;
    o->next = ns;
}
typedef struct { pl_grl_t g[2]; pl_z_t z; pl_op_t dc[2]; } pl_gr_t;
static void gr_init(pl_gr_t *d) { memset(d, 0, sizeof *d); }
static void gr_render(pl_gr_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t f0 = (int32_t)(p->f0 >> 8), h = p->harm;
    int32_t f1 = (int32_t)(pl_note_f((24 << 16) + (int32_t)(((int64_t)p->timbre * 84) >> 8)) >> 8);
    int32_t ratio = (int32_t)pl_semi(-(24 << 16) + (int32_t)(((int64_t)h * 48) >> 8), 24);
    int32_t cb = h < (PL1 >> 1) ? PL1 - 2 * h : 0, cbf = pm(cb, 2 * PL1 - cb);
    int32_t cs = 5536481 + pm(p->morph - 5536481, pmax(PL1 - 24 * f0, 0));   /* 0.33 */
    uint32_t i, cut = pl_note_f(p->note + (int32_t)(((int64_t)p->timbre * 96) >> 8));
    grl_render(&d->g[0], f0, f1, cs, cbf, out, n);
    grl_render(&d->g[1], f0, (int32_t)pmin((int32_t)(((int64_t)f1 * ratio) >> 24), PL1), cs, cbf, aux, n);
    pl_op_f(&d->dc[0], (uint32_t)(((uint64_t)p->f0 * 5033165) >> 24), TAN_DIRTY);   /* 0.3 f0 */
    for (i = 0; i < n; i++) {
        int32_t x = out[i] + aux[i];
        out[i] = x - pl_op_lp(&d->dc[0], x);
    }
    z_render(&d->z, f0, (int32_t)(cut >> 8), p->morph, h, aux, n);
    pl_op_f(&d->dc[1], (uint32_t)(((uint64_t)p->f0 * 5033165) >> 24), TAN_DIRTY);
    for (i = 0; i < n; i++)
        aux[i] -= pl_op_lp(&d->dc[1], aux[i]);
}

/* ------------------------------------------------------------ additive --- */
/* HarmonicOscillator<12>: a batch of 12 harmonics from k1, by the recurrence on sin(phase), Q28 inside */
typedef struct { uint32_t ph, f; int32_t amp[12]; } pl_hosc_t;
static void hosc_render(pl_hosc_t *o, int k1, uint32_t f, const int32_t *amps, int32_t *out, int add, uint32_t n)
{
    int32_t sa[12], sf;
    uint32_t i;
    int k;
    if (f > 0x80000000u)
        f = 0x80000000u;
    for (k = 0; k < 12; k++) {
        uint64_t fk = (uint64_t)f * (uint32_t)(k1 + k);
        int32_t att = fk >= 0x80000000ull ? 0 : PL1 - (int32_t)(fk >> 7);   /* 1 - 2 f k */
        sa[k] = pl_ip(o->amp[k], pm(amps[k], att), n);
    }
    sf = ((int32_t)(f >> 1) - (int32_t)(o->f >> 1)) / (int32_t)n;   /* (halved: f up to 0.5) */
    for (i = 0; i < n; i++) {
        int32_t two_x, prev, cur, sum = 0;
        o->f += (uint32_t)(sf * 2);
        o->ph += o->f;
        two_x = pl_sin(o->ph) << 5;                  /* 2 sin, Q28 */
        if (k1 == 1) {
            prev = 1 << 28;
            cur = two_x >> 1;
        } else {
            prev = pl_sin(o->ph * (uint32_t)(k1 - 1) + 0x40000000u) << 4;
            cur = pl_sin(o->ph * (uint32_t)k1) << 4;
        }
        for (k = 0; k < 12; k++) {
            int32_t t = cur;
            o->amp[k] += sa[k];
            sum += (int32_t)(((int64_t)o->amp[k] * cur) >> 28);
            cur = (int32_t)(((int64_t)two_x * cur) >> 28) - prev;
            prev = t;
        }
        out[i] = add ? out[i] + sum : sum;
    }
    for (k = 0; k < 12; k++)
        o->amp[k] = pm(amps[k], (int32_t)(f * (uint64_t)(k1 + k) >= 0x80000000ull ? 0
                                           : PL1 - (int32_t)(((uint64_t)f * (uint32_t)(k1 + k)) >> 7)));
    o->f = f;
}
typedef struct { pl_hosc_t h[3]; int32_t amps[36]; } pl_add_t;
static void add_init(pl_add_t *d) { memset(d, 0, sizeof *d); }
static void add_amps(int32_t centroid, int32_t slope, int32_t bumps, int32_t *amps, const uint8_t *idx, int nh)
{
    int32_t margin = pl_div(pl_div(PL1, slope) - PL1, PL1 + bumps);
    int32_t center = (int32_t)(((int64_t)centroid * ((nh - 1) * PL1 + margin)) >> 24) - (margin >> 1);
    int32_t sum = 16777, inv;                        /* 0.001 */
    int i;
    for (i = 0; i < nh; i++) {
        int32_t order = pm(pabs(i * PL1 - center), slope), g = PL1 - order, bf;
        int64_t g2, g4;
        uint32_t b = 0x40000000u + (uint32_t)(((int64_t)order * bumps) >> 16);   /* 0.25 + order bumps, Q32 */
        int32_t *a = &amps[idx[i]];
        g += pabs(g);
        g = pm(g, g);
        bf = PL1 + pl_sin(b);
        g = pm(g, bf);
        g2 = ((int64_t)g * g) >> 24;
        g4 = (g2 * g2) >> 24;
        *a += (int32_t)(((g4 - *a) * 16777) >> 24);   /* ONE_POLE 0.001 */
        sum += *a;
    }
    inv = pl_div(PL1, sum);
    for (i = 0; i < nh; i++)
        amps[idx[i]] = pm(amps[idx[i]], inv);
}
static void add_render(pl_add_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    static const uint8_t ints[24] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
    static const uint8_t organ[8] = {0, 1, 2, 3, 5, 7, 9, 11};
    int32_t rb = p->harm, rs = pm(PL1 - pm(10066330, rb), p->morph);   /* (1 - 0.6 bumps) morph */
    int32_t slope = 167772 + pm(33386906, pm(pm(rs, rs), rs)), bumps = 16 * pm(rb, rb);   /* 0.01 + 1.99 s^3 */
    add_amps(p->timbre, slope, bumps, d->amps, ints, 24);
    hosc_render(&d->h[0], 1, p->f0, d->amps, out, 0, n);
    hosc_render(&d->h[1], 13, p->f0, d->amps + 12, out, 1, n);
    add_amps(p->timbre, slope, bumps, d->amps + 24, organ, 8);
    hosc_render(&d->h[2], 1, p->f0, d->amps + 24, aux, 0, n);
}

/* ----------------------------------------------------------- wavetable --- */
/* 4 banks of 8 x 8 integrated waves (the last, the others' waves scrambled), 128 samples (+ 4) each, differentiated */
typedef struct { uint32_t ph, pf0; int32_t xpl, ypl, zpl, xl, yl, zl, px, py, pz, dprev, dlp; } pl_wt_t;
static void wt_init(pl_wt_t *d)
{
    memset(d, 0, sizeof *d);
    d->pf0 = 5356535u;                               /* a0 */
}
/* InterpolateWaveHermite, Q12 of the table's int16 */
static int32_t wt_read(int x, int y, int z, int32_t pi, int32_t pf)
{
    int i = x + y * 8 + z * 64, w = i >= 192 ? i * 101 % 192 : i;
    const int16_t *t = PL_WAVES + w * 132 + pi;
    int32_t xm1 = t[0] << 12, x0 = t[1] << 12, x1 = t[2] << 12, x2 = t[3] << 12;
    int32_t c = (x1 - xm1) >> 1, v = x0 - x1, ww = c + v, a = ww + v + ((x2 - x0) >> 1), bn = ww + a;
    return pm(pm(pm(a, pf) - bn, pf) + c, pf) + x0;
}
static inline int32_t wt_clamp(int32_t x) { return pclamp(16 * (x - (PL1 >> 1)), -(PL1 >> 1), PL1 >> 1) + (PL1 >> 1); }
static void wt_render(pl_wt_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t q, lpc, f0 = (int32_t)(p->f0 >> 8), x, y, z, xf, yf, zf, sx, sy, sz, sf;
    uint32_t i;
    PL_ONE_POLE(d->xpl, pm(p->timbre, 117438841), 3355443);   /* x 6.9999, 0.2 */
    PL_ONE_POLE(d->ypl, pm(p->morph, 117438841), 3355443);
    PL_ONE_POLE(d->zpl, pm(p->harm, 117438841), 838861);       /* 0.05 */
    q = pclamp(d->zpl - 3 * PL1, 0, PL1);
    lpc = pclamp(pm(2 * f0, 4 * PL1 - 3 * q), 167772, 1677722);   /* 0.01 .. 0.1 */
    xf = d->xpl & 0xFFFFFF;
    yf = d->ypl & 0xFFFFFF;
    zf = d->zpl & 0xFFFFFF;
    x = (d->xpl & ~0xFFFFFF) + xf + pm(q, wt_clamp(xf) - xf);
    y = (d->ypl & ~0xFFFFFF) + yf + pm(q, wt_clamp(yf) - yf);
    z = (d->zpl & ~0xFFFFFF) + zf + pm(q, wt_clamp(zf) - zf);
    sx = pl_ip(d->px, x, n);
    sy = pl_ip(d->py, y, n);
    sz = pl_ip(d->pz, z, n);
    sf = ((int32_t)p->f0 - (int32_t)d->pf0) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t g, cut, xi, yi, zi, x0, x1, y0, y1, z0, z1, pi, pf, a, b, c0, c1, mix, sh;
        uint32_t fq, r;
        d->px += sx;
        d->py += sy;
        d->pz += sz;
        d->pf0 += (uint32_t)sf;
        fq = d->pf0 >> 8;
        {   /* gain = (0.95 - f0) / (f0 131072): of the Q12 samples to Q24, (0.95 - f0) / (32 f0), Q16 */
            r = rg_rcp(fq ? fq : 1u, &sh);           /* 1 / fq = r 2^(sh - 48); 2^40 / (32 fq) = r 2^(sh - 13) */
            g = (int32_t)((((uint64_t)r << (sh > 13 ? sh - 13 : 0)) >> (sh < 13 ? 13 - sh : 0)));
            g = (int32_t)(((int64_t)g * (15938355 - (int32_t)fq)) >> 24);   /* 0.95 */
        }
        cut = pmin((int32_t)fq * 128, PL1);
        PL_ONE_POLE(d->xl, d->px, lpc);
        PL_ONE_POLE(d->yl, d->py, lpc);
        PL_ONE_POLE(d->zl, d->pz, lpc);
        xi = d->xl >> 24;
        yi = d->yl >> 24;
        zi = d->zl >> 24;
        xf = d->xl & 0xFFFFFF;
        yf = d->yl & 0xFFFFFF;
        zf = d->zl & 0xFFFFFF;
        d->ph += d->pf0;
        pi = (int32_t)(d->ph >> 25);
        pf = (int32_t)((d->ph >> 1) & 0xFFFFFF);
        x0 = xi;
        x1 = xi + 1;
        y0 = yi;
        y1 = yi + 1;
        z0 = zi >= 4 ? 7 - zi : zi;
        z1 = zi + 1 >= 4 ? 6 - zi : zi + 1;
        a = wt_read(x0, y0, z0, pi, pf);
        a += pm(wt_read(x1, y0, z0, pi, pf) - a, xf);
        b = wt_read(x0, y1, z0, pi, pf);
        b += pm(wt_read(x1, y1, z0, pi, pf) - b, xf);
        c0 = a + pm(b - a, yf);
        a = wt_read(x0, y0, z1, pi, pf);
        a += pm(wt_read(x1, y0, z1, pi, pf) - a, xf);
        b = wt_read(x0, y1, z1, pi, pf);
        b += pm(wt_read(x1, y1, z1, pi, pf) - b, xf);
        c1 = a + pm(b - a, yf);
        mix = c0 + pm(c1 - c0, zf);
        PL_ONE_POLE(d->dlp, mix - d->dprev, cut);    /* Differentiator */
        d->dprev = mix;
        mix = (int32_t)(((int64_t)d->dlp * g) >> 16);
        out[i] = mix;
        aux[i] = mix < 0 ? -((-mix >> 19) << 19) : (mix >> 19) << 19;   /* (int)(x 32) / 32 */
    }
    d->px = x;
    d->py = y;
    d->pz = z;
    d->pf0 = p->f0;
}

/* -------------------------------------------------------------- chords --- */
/* StringSynthOscillator: a divide-down organ, saws at 8', 4', 2', 1' from one phase (0 .. 8, Q24) */
typedef struct { int32_t ph, next, seg, f, g8, g4, g2, g1; } pl_ss_t;
static void ss_render(pl_ss_t *o, int32_t f, const int32_t *ureg, int32_t gain, int32_t *out, uint32_t n)
{
    int32_t reg[7], s8, s4, s2, s1, sf, t8, t4, t2, t1, ns = o->next;
    uint32_t i, shift = 0, k;
    f *= 8;
    while (f > (PL1 >> 1)) {
        shift += 2;
        f >>= 1;
    }
    if (shift >= 8)
        return;
    for (k = 0; k < 7; k++)
        reg[k] = k < shift ? 0 : ureg[k - shift];
    t8 = pm(reg[0] + 2 * reg[1], gain);
    t4 = pm(reg[2] - reg[1] + 2 * reg[3], gain);
    t2 = pm(reg[4] - reg[3] + 2 * reg[5], gain);
    t1 = pm(reg[6] - reg[5], gain);
    sf = pl_ip(o->f, f, n);
    s8 = pl_ip(o->g8, t8, n);
    s4 = pl_ip(o->g4, t4, n);
    s2 = pl_ip(o->g2, t2, n);
    s1 = pl_ip(o->g1, t1, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns, nseg;
        ns = 0;
        o->f += sf;
        o->g8 += s8;
        o->g4 += s4;
        o->g2 += s2;
        o->g1 += s1;
        o->ph += o->f;
        nseg = o->ph >> 24;
        if (nseg != o->seg) {
            int32_t dis = 0;
            if (nseg == 8) {
                o->ph -= 8 * PL1;
                nseg -= 8;
                dis -= o->g8;
            }
            if ((nseg & 3) == 0)
                dis -= o->g4;
            if ((nseg & 1) == 0)
                dis -= o->g2;
            dis -= o->g1;
            if (dis) {
                int32_t t = pl_div(o->ph - (nseg << 24), o->f);
                ts += pm(blep_this(t), dis);
                ns += pm(blep_next(t), dis);
            }
        }
        o->seg = nseg;
        ns += pm(o->ph - 4 * PL1, o->g8) >> 3;
        ns += pm(o->ph - ((nseg & 4) << 24) - 2 * PL1, o->g4) >> 2;
        ns += pm(o->ph - ((nseg & 6) << 24) - PL1, o->g2) >> 1;
        ns += pm(o->ph - ((nseg & 7) << 24) - (PL1 >> 1), o->g1);
        out[i] += 2 * ts;
    }
    o->f = f;
    o->g8 = t8;
    o->g4 = t4;
    o->g2 = t2;
    o->g1 = t1;
    o->next = ns;
}
/* WavetableOscillator<128, 15>: the chords' waves (linear), differentiated; samples Q12 of the int16 */
typedef struct { int32_t ph, f, amp, wf, lp, dprev, dlp; } pl_wto_t;
static const uint16_t PL_CH_WAVES[15] = {   /* (bank 64 + row 8 + column) */
    2 * 64 + 6 * 8 + 1, 2 * 64 + 6 * 8 + 6, 2 * 64 + 6 * 8 + 4, 6 * 8 + 0, 6 * 8 + 1, 6 * 8 + 2, 6 * 8 + 7,
    2 * 64 + 4 * 8 + 7, 2 * 64 + 4 * 8 + 6, 2 * 64 + 4 * 8 + 5, 2 * 64 + 4 * 8 + 4, 2 * 64 + 4 * 8 + 3,
    2 * 64 + 4 * 8 + 2, 2 * 64 + 4 * 8 + 1, 2 * 64 + 4 * 8 + 0};
static void wto_render(pl_wto_t *o, int32_t f, int32_t amp, int32_t wf, int32_t *out, uint32_t n)
{
    int32_t sf, sa, sw, sh, a;
    uint32_t i, r;
    f = pclamp(f, 2, PL1 >> 2);
    /* amplitude (1 - 2 f) / (f 131072), of Q12 samples to Q24: (1 - 2 f) / (32 f), Q16 */
    r = rg_rcp((uint32_t)f, &sh);
    a = (int32_t)((((uint64_t)r << (sh > 13 ? sh - 13 : 0)) >> (sh < 13 ? 13 - sh : 0)));
    a = (int32_t)(((int64_t)a * pm(amp, PL1 - 2 * f)) >> 24);
    wf = pm(wf, 234879877);                          /* x 13.9999 */
    sf = pl_ip(o->f, f, n);
    sa = pl_ip(o->amp, a, n);
    sw = pl_ip(o->wf, wf, n);
    for (i = 0; i < n; i++) {
        int32_t cut, wi, wfr, pi, pf, x0, x1, s;
        const int16_t *t0, *t1;
        o->f += sf;
        o->amp += sa;
        o->wf += sw;
        cut = pmin(o->f * 128, PL1);
        o->ph += o->f;
        if (o->ph >= PL1)
            o->ph -= PL1;
        wi = o->wf >> 24;
        wfr = o->wf & 0xFFFFFF;
        pi = o->ph >> 17;
        pf = (o->ph & 0x1FFFF) << 7;
        t0 = PL_WAVES + PL_CH_WAVES[wi] * 132 + pi;
        t1 = PL_WAVES + PL_CH_WAVES[wi + 1] * 132 + pi;
        x0 = (t0[0] << 12) + pm((t0[1] - t0[0]) << 12, pf);
        x1 = (t1[0] << 12) + pm((t1[1] - t1[0]) << 12, pf);
        s = x0 + pm(x1 - x0, wfr);
        PL_ONE_POLE(o->dlp, s - o->dprev, cut);
        o->dprev = s;
        PL_ONE_POLE(o->lp, o->dlp, cut);
        out[i] += (int32_t)(((int64_t)o->amp * o->lp) >> 16);
    }
    o->f = f;
    o->amp = a;
    o->wf = wf;
}
#define PL_CHN 11
static const int16_t PL_CHORDS[PL_CHN][4] = {      /* semitones x 100 */
    {0, 1, 1199, 1200}, {0, 700, 701, 1200}, {0, 500, 700, 1200}, {0, 300, 700, 1200}, {0, 300, 700, 1000},
    {0, 300, 1000, 1400}, {0, 300, 1000, 1700}, {0, 200, 900, 1600}, {0, 400, 1100, 1400}, {0, 400, 700, 1100},
    {0, 400, 700, 1200}};
static const int32_t PL_CH_REG[8][6] = {           /* the divide-down's registrations, Q24 */
    {0, PL1, 0, 0, 0, 0}, {PL1, 0, 0, 0, 0, 0}, {PL1 / 2, 0, PL1 / 2, 0, 0, 0},
    {5536481, 0, 5536481, 0, 5536481, 0}, {5536481, 0, 0, 5536481, 0, 5536481}, {PL1 / 2, 0, 0, 0, 0, PL1 / 2},
    {0, PL1 / 2, 0, 0, 0, PL1 / 2}, {0, 1677722, 1677722, 0, 3355443, 10066330}};
static const int32_t PL_CH_FADE[5] = {9227469, 7885291, 8220836, 8556380, 8891924};   /* .55 .47 .49 .51 .53 */
static const char *const PL_CHORD_NAME[PL_CHN] = {"OCT", "5", "sus4", "m", "m7", "m9", "m11", "69", "M9", "M7", "M"};
/* HysteresisQuantizer2(11, 0.075): the chord HARMONICS picks, from the last one (the UI names it the same way) */
static int32_t ch_quant(int32_t harm, int32_t last)
{
    int32_t x = pm(pm(harm, 17112760), PL_CHN * PL1) - (PL1 >> 1);   /* x 1.02 x 11 - 0.5 */
    int32_t q = (x + (x > last * PL1 ? -1258291 : 1258291) + (PL1 >> 1)) >> 24;
    return pclamp(q, 0, PL_CHN - 1);
}
typedef struct { pl_ss_t dd[5]; pl_wto_t wt[5]; int32_t mlp, tlp, chord; } pl_ch_t;
static void ch_init(pl_ch_t *d) { memset(d, 0, sizeof *d); { int i; for (i = 0; i < 5; i++) d->dd[i].f = 16777; } }
static void ch_render(pl_ch_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t reg[7], ratio[5], amp[5], rg, ri, rf, inv, ii, ifr, rot, rn, mask = 0, f0, wform, k, v;
    uint32_t i;
    PL_ONE_POLE(d->mlp, p->morph, 1677722);          /* 0.1 */
    PL_ONE_POLE(d->tlp, p->timbre, 1677722);
    d->chord = ch_quant(p->harm, d->chord);
    rg = pm(pmax(PL1 - pm(d->mlp, 36071014), 0), 117423063);   /* (1 - 2.15 m) x 6.999 */
    ri = rg >> 24;
    rf = rg & 0xFFFFFF;
    for (k = 0; k < 6; k++)
        reg[k] = PL_CH_REG[ri][k] + pm(PL_CH_REG[ri + 1][k] - PL_CH_REG[ri][k], rf);
    reg[6] = 0;
    /* ComputeChordInversion */
    inv = d->tlp * 20;
    ii = inv >> 24;
    ifr = inv & 0xFFFFFF;
    rot = ii / 4;
    rn = ii % 4;
    for (k = 0; k < 4; k++) {
        int32_t base = (int32_t)pl_semi(PL_CHORDS[d->chord][k] * 65536 / 100, 24);
        int32_t tr = (int32_t)((1 << ((3 + ii - k) / 4)) * (PL1 >> 2) >> 0);
        int tv = (k - rot + 5) % 5, pv = (tv - 1 + 5) % 5;
        int32_t r = (int32_t)(((int64_t)base * tr) >> 24);
        if (k == rn) {
            ratio[tv] = r;
            ratio[pv] = 2 * r;
            amp[pv] = pm(PL1 >> 2, ifr);
            amp[tv] = pm(PL1 >> 2, PL1 - ifr);
        } else if (k < rn) {
            ratio[pv] = r;
            amp[pv] = PL1 >> 2;
        } else {
            ratio[tv] = r;
            amp[tv] = PL1 >> 2;
        }
        if (k == 0) {
            if (k >= rn)
                mask |= 1 << tv;
            if (k <= rn)
                mask |= 1 << pv;
        }
    }
    for (i = 0; i < n; i++)
        out[i] = aux[i] = 0;
    f0 = pm((int32_t)(p->f0 >> 8), 16743662);        /* x 0.998 */
    wform = pmax(pm(d->mlp - 8975794, 36071014), 0);   /* (m - 0.535) 2.15 */
    for (v = 0; v < 5; v++) {
        int32_t wa = pclamp(50 * (d->mlp - PL_CH_FADE[v]), 0, PL1), dda = PL1 - wa;
        int32_t *dst = (1 << v) & mask ? aux : out;
        int32_t nf = (int32_t)(((int64_t)f0 * ratio[v]) >> 24), ddg = pclamp(4 * PL1 - 32 * nf, 0, PL1);
        dda = pm(dda, ddg);
        if (wa)
            wto_render(&d->wt[v], pm(nf, 16844179), pm(amp[v], wa), wform, dst, n);   /* x 1.004 */
        if (dda)
            ss_render(&d->dd[v], nf, reg, pm(amp[v], dda), dst, n);
    }
    for (i = 0; i < n; i++) {
        out[i] += aux[i];
        aux[i] *= 3;
    }
}

/* -------------------------------------------------------------- speech --- */
/* Oscillator<OSCILLATOR_SHAPE_IMPULSE_TRAIN> (the naive synth's glottal pulse) */
typedef struct { int32_t ph, next, f, lp, hp; } pl_imp_t;
static void imp_render(pl_imp_t *o, int32_t f, int32_t *out, uint32_t n)
{
    int32_t sf, ns = o->next;
    uint32_t i;
    f = pclamp(f, 17, PL1 >> 2);
    sf = pl_ip(o->f, f, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns;
        ns = 0;
        o->f += sf;
        o->ph += o->f;
        if (o->ph >= PL1) {
            int32_t t;
            o->ph -= PL1;
            t = pl_div(o->ph, o->f);
            ts -= blep_this(t);
            ns -= blep_next(t);
        }
        ns += o->ph;
        o->lp += ((o->hp - ts) - o->lp) >> 2;
        out[i] = 4 * o->lp;
        o->hp = ts;
    }
    o->f = f;
    o->next = ns;
}
/* NaiveSpeechSynth: five formant filters on a pulse; [phoneme][register][formant]: (note, amplitude) */
static const uint8_t PL_NAIVE[5][5][5][2] = {
    {{{74, 255}, {83, 114}, {97, 90}, {98, 90}, {100, 25}}, {{75, 255}, {84, 128}, {100, 114}, {101, 101}, {103, 20}},
     {{76, 255}, {85, 128}, {100, 18}, {102, 16}, {104, 3}}, {{79, 255}, {85, 161}, {101, 25}, {104, 4}, {110, 0}},
     {{79, 255}, {85, 128}, {101, 6}, {106, 25}, {110, 0}}},
    {{{67, 255}, {91, 64}, {98, 90}, {101, 64}, {102, 32}}, {{67, 255}, {92, 51}, {99, 64}, {103, 51}, {105, 25}},
     {{69, 255}, {93, 51}, {100, 32}, {102, 25}, {103, 25}}, {{67, 255}, {91, 16}, {100, 8}, {103, 4}, {110, 0}},
     {{65, 255}, {95, 25}, {101, 45}, {105, 2}, {110, 0}}},
    {{{59, 255}, {92, 8}, {99, 40}, {102, 20}, {104, 10}}, {{61, 255}, {94, 45}, {101, 32}, {103, 25}, {105, 8}},
     {{60, 255}, {93, 16}, {101, 16}, {104, 4}, {105, 4}}, {{65, 255}, {92, 25}, {100, 8}, {105, 4}, {110, 0}},
     {{60, 255}, {96, 64}, {101, 12}, {106, 12}, {110, 1}}},
    {{{67, 255}, {78, 72}, {98, 22}, {99, 25}, {101, 2}}, {{67, 255}, {79, 80}, {99, 64}, {101, 64}, {102, 12}},
     {{68, 255}, {79, 80}, {100, 12}, {102, 20}, {103, 5}}, {{69, 255}, {79, 90}, {101, 40}, {104, 10}, {110, 0}},
     {{69, 255}, {79, 72}, {101, 20}, {106, 20}, {110, 0}}},
    {{{65, 255}, {74, 25}, {98, 6}, {100, 10}, {101, 4}}, {{65, 255}, {74, 25}, {100, 36}, {101, 51}, {103, 12}},
     {{66, 255}, {75, 25}, {100, 18}, {102, 8}, {104, 5}}, {{63, 255}, {77, 64}, {99, 8}, {104, 2}, {110, 0}},
     {{63, 255}, {77, 40}, {100, 4}, {106, 2}, {110, 0}}}};
typedef struct { pl_imp_t pulse; pl_svf_t f[5], col; } pl_nv_t;
static void nv_init(pl_nv_t *d)
{
    memset(d, 0, sizeof *d);
    d->pulse.ph = PL1 >> 1;
    d->pulse.lp = PL1;
    d->pulse.f = 16777;
    pl_svf_fq(&d->col, (uint32_t)(800ull * 4294967296ull / FS), 128, TAN_DIRTY);
}
static void nv_render(pl_nv_t *d, int32_t f0, int32_t phon, int32_t reg, int32_t *exc, int32_t *out, uint32_t n)
{
    int32_t p = pm(phon, 67092020), r = pm(reg, 67092020), pi = p >> 24, pf = p & 0xFFFFFF, ri = r >> 24;   /* x 3.999 */
    int32_t rf = r & 0xFFFFFF, k;
    uint32_t i;
    imp_render(&d->pulse, f0, exc, n);
    for (i = 0; i < n; i++) {
        exc[i] = 4 * pl_svf_bp(&d->col, exc[i]);
        out[i] = 0;
    }
    for (k = 0; k < 5; k++) {
        const uint8_t *a = PL_NAIVE[pi][ri][k], *b = PL_NAIVE[pi][ri + 1][k];
        const uint8_t *c = PL_NAIVE[pi + 1][ri][k], *e = PL_NAIVE[pi + 1][ri + 1][k];
        int32_t f1 = (a[0] << 16) + (int32_t)(((int64_t)((b[0] - a[0]) << 16) * rf) >> 24);
        int32_t f2 = (c[0] << 16) + (int32_t)(((int64_t)((e[0] - c[0]) << 16) * rf) >> 24);
        int32_t fn = f1 + (int32_t)(((int64_t)(f2 - f1) * pf) >> 24);   /* Q16 */
        int32_t a1 = (a[1] << 16) + pm((b[1] - a[1]) << 16, rf), a2 = (c[1] << 16) + pm((e[1] - c[1]) << 16, rf);
        int32_t g = a1 + pm(a2 - a1, pf);            /* / 256: Q24 */
        pl_svf_fq(&d->f[k], pl_note_f(pmin(fn, 160 << 16)), 20 * 256, TAN_DIRTY);
        for (i = 0; i < n; i++)
            out[i] += pm(g, pl_svf_bp(&d->f[k], exc[i]));
    }
}
/* SAMSpeechSynth: three sine formants, reset by a pulse */
static const uint8_t PL_SAM[18][3][2] = {
    {{60, 15}, {90, 13}, {200, 1}}, {{40, 13}, {114, 12}, {139, 6}}, {{33, 14}, {155, 12}, {209, 7}},
    {{22, 13}, {189, 10}, {247, 8}}, {{51, 15}, {99, 12}, {195, 1}}, {{29, 13}, {65, 8}, {180, 0}},
    {{13, 12}, {103, 3}, {182, 0}}, {{20, 15}, {114, 3}, {213, 0}}, {{13, 7}, {164, 3}, {222, 14}},
    {{13, 9}, {121, 9}, {254, 0}}, {{40, 12}, {112, 10}, {114, 5}}, {{24, 13}, {54, 8}, {157, 0}},
    {{33, 14}, {155, 12}, {166, 7}}, {{36, 14}, {83, 8}, {249, 1}}, {{40, 14}, {114, 12}, {139, 6}},
    {{13, 5}, {58, 5}, {182, 5}}, {{13, 7}, {164, 10}, {222, 14}}, {{13, 7}, {164, 10}, {222, 14}}};
static const int32_t PL_SAM_AMP[16] = {524288, 630199, 757505, 910531, 1094473, 1315575, 1581343, 1900804, 2284799,
                                       2746371, 3301183, 3968066, 4769663, 5733193, 6891424, 8283583};
typedef struct { int32_t ph, f, next, lp; uint32_t fph[3]; } pl_sam_t;
static inline int32_t pl_sine_raw(uint32_t ph) { return RG_SINE[(ph >> 23) << 3] << 9; }   /* (512 points) */
static void sam_render(pl_sam_t *d, int32_t f, int32_t vowel, int32_t shift, int32_t *exc, int32_t *out, uint32_t n)
{
    int32_t ph = pm(vowel, 150993568), pi = ph >> 24, pf = ph & 0xFFFFFF, amp[3], sf, ns = d->next, k;   /* x 8.9999 */
    int32_t fs = PL1 + pm(shift, 41943040);          /* 1 + 2.5 shift */
    uint32_t ff[3], i;
    f = pmin(f, PL1 >> 4);
    for (k = 0; k < 3; k++) {
        int32_t f1 = PL_SAM[pi][k][0], f2 = PL_SAM[pi + 1][k][0];
        int32_t fr = (f1 << 16) + (int32_t)(((int64_t)((f2 - f1) << 16) * pf) >> 24);   /* Q16 */
        /* x 8 shift 2^32 / FS */
        ff[k] = (uint32_t)((((uint64_t)fr * (uint32_t)fs) >> 24) * 8u * 97391ull >> 16);   /* 2^32 / 44100 = 97391.5 */
        amp[k] = PL_SAM_AMP[PL_SAM[pi][k][1]] + pm(PL_SAM_AMP[PL_SAM[pi + 1][k][1]] - PL_SAM_AMP[PL_SAM[pi][k][1]], pf);
    }
    sf = pl_ip(d->f, f, n);
    for (i = 0; i < n; i++) {
        int32_t ts = ns, dd, s;
        ns = 0;
        d->f += sf;
        d->ph += d->f;
        if (d->ph >= PL1) {
            int32_t t;
            d->ph -= PL1;
            t = pl_div(d->ph, d->f);
            for (k = 0; k < 3; k++)
                d->fph[k] = (uint32_t)(((uint64_t)(uint32_t)t * ff[k]) >> 24);
            ts -= blep_this(t);
            ns -= blep_next(t);
        } else {
            for (k = 0; k < 3; k++)
                d->fph[k] += ff[k];
        }
        ns += d->ph;
        dd = ts - (PL1 >> 1) - d->lp;
        d->lp += pm(pmin(16 * d->f, PL1), dd);
        exc[i] = dd;
        s = pm(pl_sine_raw(d->fph[0]), amp[0]) + pm(pl_sine_raw(d->fph[1]), amp[1]) + pm(pl_sine_raw(d->fph[2]), amp[2]);
        out[i] = pm(s, PL1 - d->ph);
    }
    d->next = ns;
}
/* LPCSpeechSynth: a 10-pole lattice on pulses and noise, clocked at ~7.35 kHz; its frames, TI's LPC10 */
typedef struct { uint8_t energy, period; int16_t k0, k1; int8_t k[8]; } pl_lpcf_t;
static const pl_lpcf_t PL_LPC_PH[15] = {             /* the vowels, then the consonants */
    {192, 80, -18368, 11584, {52, 29, 23, 14, -17, 79, 37, 4}}, {192, 80, -14528, 1536, {38, 29, 11, 14, -41, 79, 57, 4}},
    {192, 80, 14528, 9216, {25, -54, -70, 36, 19, 79, 57, 22}}, {192, 80, -14528, -13440, {38, 57, 57, 14, -53, 7, 37, 77}},
    {192, 80, -26368, 4160, {11, 15, -1, 36, -41, 31, 77, 22}}, {15, 0, 5184, 9216, {-29, -12, 0, 0, 0, 0, 0, 0}},
    {10, 0, 27968, 17856, {25, 43, -24, -20, -53, 55, -4, -51}}, {128, 160, 14528, -3712, {-43, -26, -24, -20, -53, 55, -4, -51}},
    {128, 160, 10048, 11584, {-16, 15, 0, 0, 0, 0, 0, 0}}, {224, 100, 18368, -13440, {-97, -26, -12, -53, -41, 7, 57, 32}},
    {192, 80, -10048, 9216, {-70, 15, 34, -20, -17, 31, -24, 22}}, {96, 160, -18368, 17856, {-29, -12, -35, 3, -5, 7, 37, 22}},
    {64, 80, -21632, -6272, {-83, 29, 57, 3, -5, 7, 16, 32}}, {192, 80, 0, -1088, {11, -26, -24, -9, -5, 55, 37, 22}},
    {64, 80, 21632, -17536, {-97, 85, 57, -20, -17, 31, -4, 59}}};
static const uint8_t PL_LPC_ENERGY[16] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x0a, 0x0f, 0x14, 0x20, 0x29, 0x39, 0x51,
                                          0x72, 0xa1, 0xff};
static const uint8_t PL_LPC_PERIOD[64] = {0, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
                                          36, 37, 38, 39, 40, 41, 42, 43, 45, 47, 49, 51, 53, 54, 57, 59, 61, 63, 66, 69, 71,
                                          73, 77, 79, 81, 85, 87, 92, 95, 99, 102, 106, 110, 115, 119, 123, 128, 133, 138,
                                          143, 149, 154, 160};
static const int16_t PL_LPC_K0[32] = {-32064, -31872, -31808, -31680, -31552, -31424, -31232, -30848, -30592, -30336,
                                      -30016, -29696, -29376, -28928, -28480, -27968, -26368, -24256, -21632, -18368,
                                      -14528, -10048, -5184, 0, 5184, 10048, 14528, 18368, 21632, 24256, 26368, 27968};
static const int16_t PL_LPC_K1[32] = {-20992, -19328, -17536, -15552, -13440, -11200, -8768, -6272, -3712, -1088, 1536,
                                      4160, 6720, 9216, 11584, 13824, 15936, 17856, 19648, 21248, 22656, 24000, 25152,
                                      26176, 27072, 27840, 28544, 29120, 29632, 30080, 30464, 32384};
static const int8_t PL_LPC_K2_6[5][16] = {
    {-110, -97, -83, -70, -56, -43, -29, -16, -2, 11, 25, 38, 52, 65, 79, 92},
    {-82, -68, -54, -40, -26, -12, 1, 15, 29, 43, 57, 71, 85, 99, 113, 126},
    {-82, -70, -59, -47, -35, -24, -12, -1, 11, 23, 34, 46, 57, 69, 81, 92},
    {-64, -53, -42, -31, -20, -9, 3, 14, 25, 36, 47, 58, 69, 80, 91, 102},
    {-77, -65, -53, -41, -29, -17, -5, 7, 19, 31, 43, 55, 67, 79, 90, 102}};
static const int8_t PL_LPC_K7_9[3][8] = {{-64, -40, -16, 7, 31, 55, 79, 102}, {-64, -44, -24, -4, 16, 37, 57, 77},
                                         {-51, -33, -15, 4, 22, 32, 59, 77}};
static const uint8_t *const PL_BANKS[5] = {PL_BANK0, PL_BANK1, PL_BANK2, PL_BANK3, PL_BANK4};
static const uint16_t PL_BANK_LEN[5] = {sizeof PL_BANK0, sizeof PL_BANK1, sizeof PL_BANK2, sizeof PL_BANK3, sizeof PL_BANK4};
/* BitStream (the words' bits, reversed in each byte) */
typedef struct { const uint8_t *p; int16_t avail; uint16_t bits; } pl_bits_t;
static inline uint8_t pl_rev(uint8_t b)
{
    b = (uint8_t)((b >> 4) | (b << 4));
    b = (uint8_t)(((b & 0xcc) >> 2) | ((b & 0x33) << 2));
    return (uint8_t)(((b & 0xaa) >> 1) | ((b & 0x55) << 1));
}
static uint8_t pl_getbits(pl_bits_t *s, int nb)
{
    int sh = nb;
    uint8_t r;
    if (nb > s->avail) {
        s->bits = (uint16_t)(s->bits << s->avail);
        sh -= s->avail;
        s->bits |= pl_rev(*s->p++);
        s->avail += 8;
    }
    s->bits = (uint16_t)(s->bits << sh);
    r = (uint8_t)(s->bits >> 8);
    s->bits &= 0xff;
    s->avail -= nb;
    return r;
}
/* the next frame of a word into f (its fields kept from the frame before, as Plaits' loader does); 0 at its end */
static int pl_lpc_next(pl_bits_t *s, pl_lpcf_t *f)
{
    int e = pl_getbits(s, 4), k;
    if (e == 0xf)
        return 0;
    if (e == 0) {
        f->energy = 0;
        return 1;
    }
    f->energy = PL_LPC_ENERGY[e];
    {
        int rep = pl_getbits(s, 1);
        f->period = PL_LPC_PERIOD[pl_getbits(s, 6)];
        if (!rep) {
            f->k0 = PL_LPC_K0[pl_getbits(s, 5)];
            f->k1 = PL_LPC_K1[pl_getbits(s, 5)];
            f->k[0] = PL_LPC_K2_6[0][pl_getbits(s, 4)];
            f->k[1] = PL_LPC_K2_6[1][pl_getbits(s, 4)];
            if (f->period) {
                for (k = 2; k < 5; k++)
                    f->k[k] = PL_LPC_K2_6[k][pl_getbits(s, 4)];
                for (k = 0; k < 3; k++)
                    f->k[5 + k] = PL_LPC_K7_9[k][pl_getbits(s, 3)];
            }
        }
    }
    return 1;
}
/* the words of each bank: their offsets (found once, at boot) */
#define PL_MAXW 32
static uint16_t pl_wofs[5][PL_MAXW];
static uint8_t pl_nwords[5], pl_words_ready;
static void pl_words_scan(void)
{
    int b;
    for (b = 0; b < 5; b++) {
        uint32_t pos = 0;
        uint8_t nw = 0;
        while (pos < PL_BANK_LEN[b] && nw < PL_MAXW) {
            pl_bits_t s = {PL_BANKS[b] + pos, 0, 0};
            pl_lpcf_t f;
            memset(&f, 0, sizeof f);
            pl_wofs[b][nw++] = (uint16_t)pos;
            while (pl_lpc_next(&s, &f))
                ;
            while (s.avail)                          /* Flush */
                pl_getbits(&s, 1);
            pos = (uint32_t)(s.p - PL_BANKS[b]);
        }
        pl_nwords[b] = nw;
    }
    pl_words_ready = 1;
}
typedef struct {
    int32_t ph, f, noise, pulse, next, k[10], s[10];
    int16_t pidx;
    /* the controller */
    int32_t clk, smp[2], nsmp[2], gain;
    int16_t play, last;                              /* playback_frame_ (-1: scan), last_playback_frame_ */
    int32_t remain;
    int8_t bank;                                     /* the bank loaded (-1: none) */
    uint8_t ended;                                   /* the word's last frame reached: held */
    pl_bits_t bs;
    pl_lpcf_t fr;
} pl_lpc_t;
static void lpc_init(pl_lpc_t *d)
{
    memset(d, 0, sizeof *d);
    d->play = d->last = -1;
    d->bank = -1;
}
static void lpc_frame(pl_lpc_t *d, const pl_lpcf_t *a, const pl_lpcf_t *b, int32_t bl)   /* PlayFrame */
{
    int32_t e1 = a->energy << 16, e2 = b->energy << 16, k;
    int32_t n1 = a->period ? 0 : e1, n2 = b->period ? 0 : e2, p1 = a->period ? e1 : 0, p2 = b->period ? e2 : 0;
    d->noise = n1 + pm(n2 - n1, bl);
    d->pulse = p1 + pm(p2 - p1, bl);
    d->k[0] = (a->k0 << 9) + pm((b->k0 - a->k0) << 9, bl);
    d->k[1] = (a->k1 << 9) + pm((b->k1 - a->k1) << 9, bl);
    for (k = 0; k < 8; k++)
        d->k[2 + k] = (a->k[k] << 17) + pm((b->k[k] - a->k[k]) << 17, bl);
}
static void lpc_tick(pl_lpc_t *d, int32_t f, int32_t *exc, int32_t *out)   /* LPCSpeechSynth::Render, a sample */
{
    int32_t ts = d->next, e[11], k;
    d->next = 0;
    d->ph += f;
    if (d->ph >= PL1) {
        int32_t rt, rs, dis = 0;
        d->ph -= PL1;
        rt = pl_div(d->ph, f);
        rs = (32 * rt) >> 24;
        if (d->pidx < 640) {
            d->pidx = (int16_t)pmax(d->pidx - rs, 0);
            dis = pm(PL_LPC_PULSE[d->pidx] << 17, d->pulse);
        }
        ts -= pm(dis, blep_this(rt));
        d->next -= pm(dis, blep_next(rt));
        d->pidx = (int16_t)rs;
    }
    e[10] = (int16_t)(pl_word() >> 16) > 0 ? d->noise : -d->noise;
    if (d->pidx < 640) {
        d->next += pm(PL_LPC_PULSE[d->pidx] << 17, d->pulse);
        d->pidx += 32;
    }
    e[10] += ts;
    e[10] += e[10] >> 1;
    for (k = 9; k >= 0; k--)
        e[k] = e[k + 1] - pm(d->k[k], d->s[k]);
    e[0] = pclamp(e[0], -2 * PL1, 2 * PL1);
    for (k = 9; k >= 1; k--)
        d->s[k] = d->s[k - 1] + pm(d->k[k - 1], e[k - 1]);
    d->s[0] = e[0];
    *exc = e[10];
    *out = e[0];
}
/* LPCSpeechSynthController::Render (no prosody, no speed: Plaits' with its TRIGGER patched and nothing else) */
static void lpc_render(pl_lpc_t *d, int trig, int bank, int32_t f0, int32_t addr, int32_t shift, int32_t gain,
                       int32_t *exc, int32_t *out, uint32_t n)
{
    int32_t rr = (int32_t)pl_semi(pm(shift - (PL1 >> 1), 36 << 16), 24), rate = rr / 6, sg, f, ts;
    uint32_t i;
    f = pmin(pl_div(pm(f0, 92484403), rr), PL1 >> 1);   /* 100 / 8000 x f0 / (rr 100 / FS): f0 FS / 8000 / rr */
    ts = shift < 6710886 ? -pm(shift - 6710886, 45 << 16) : shift > 10066330 ? -pm(shift - 10066330, 45 << 16) : 0;
    if (bank != -1) {
        if (!pl_words_ready)                         /* (found at boot: engine.c engine_init) */
            pl_words_scan();
        if (bank != d->bank) {
            d->bank = (int8_t)bank;
            d->play = d->last = -1;
        }
    }
    if (trig) {
        if (bank == -1) {
            int r = (int)(((int64_t)(addr + 3 * shift + 7 * f0) * 8) >> 24);
            d->play = (int16_t)(r % 10 + 5);
            d->last = (int16_t)(d->play + 1);
        } else {
            int w = (int)(((int64_t)addr * pl_nwords[bank]) >> 24);
            if (w >= pl_nwords[bank])
                w = pl_nwords[bank] - 1;
            d->bs.p = PL_BANKS[bank] + pl_wofs[bank][w];
            d->bs.avail = 0;
            d->bs.bits = 0;
            memset(&d->fr, 0, sizeof d->fr);
            d->ended = !pl_lpc_next(&d->bs, &d->fr);
            d->play = 0;
            d->last = 1;
        }
        d->remain = 0;
    }
    if (d->play == -1 && d->remain == 0) {
        if (bank == -1) {
            int32_t a = pm(addr, 67107186), ai = a >> 24;   /* x (5 - 1.0001) */
            lpc_frame(d, &PL_LPC_PH[ai], &PL_LPC_PH[ai + 1], a & 0xFFFFFF);
        } else {                                     /* (Plaits scans the bank's frames: here, silence till a hit) */
            static const pl_lpcf_t z;
            lpc_frame(d, &z, &z, 0);
        }
    } else {
        if (d->remain == 0) {
            if (bank == -1) {
                lpc_frame(d, &PL_LPC_PH[d->play], &PL_LPC_PH[d->play], 0);
                if (++d->play >= d->last)
                    d->play = -1;
            } else {
                lpc_frame(d, &d->fr, &d->fr, 0);
                if (!d->ended && !pl_lpc_next(&d->bs, &d->fr))
                    d->ended = 1;                    /* (the word's last frame, held) */
            }
            d->remain = (int32_t)(((int64_t)pl_semi(ts, 16) * 72253440) >> 32);   /* FS / 40 x stretch */
        }
        d->remain -= pmin((int32_t)n, d->remain);
    }
    sg = pl_ip(d->gain, gain, n);
    for (i = 0; i < n; i++) {
        int32_t th[2] = {d->nsmp[0], d->nsmp[1]};
        d->nsmp[0] = d->nsmp[1] = 0;
        d->clk += rate;
        if (d->clk >= PL1) {
            int32_t rt, nw[2], dis0, dis1;
            d->clk -= PL1;
            rt = pl_div(d->clk, rate);
            lpc_tick(d, f, &nw[0], &nw[1]);
            dis0 = nw[0] - d->smp[0];
            dis1 = nw[1] - d->smp[1];
            th[0] += pm(dis0, blep_this(rt));
            d->nsmp[0] += pm(dis0, blep_next(rt));
            th[1] += pm(dis1, blep_this(rt));
            d->nsmp[1] += pm(dis1, blep_next(rt));
            d->smp[0] = nw[0];
            d->smp[1] = nw[1];
        }
        d->nsmp[0] += d->smp[0];
        d->nsmp[1] += d->smp[1];
        d->gain += sg;
        exc[i] = pm(th[0], d->gain);
        out[i] = pm(th[1], d->gain);
    }
    d->gain = gain;
}
typedef struct { pl_nv_t nv; pl_sam_t sam; pl_lpc_t lpc; int32_t wbq; uint8_t env; } pl_sp_t;
static void sp_init(pl_sp_t *d)
{
    memset(d, 0, sizeof *d);
    nv_init(&d->nv);
    lpc_init(&d->lpc);
}
static void sp_render(pl_sp_t *d, const pl_par_t *p, int32_t *out, int32_t *aux, uint32_t n)
{
    int32_t f0 = (int32_t)(p->f0 >> 8), group = 6 * p->harm;
    uint32_t i;
    if (group <= 2 * PL1) {
        int32_t t0[PLSUB], t1[PLSUB], bl = group;
        d->env = 0;
        if (group <= PL1) {
            nv_render(&d->nv, f0, p->morph, p->timbre, aux, out, n);
        } else {
            lpc_render(&d->lpc, p->trig, -1, f0, p->morph, p->timbre, PL1, aux, out, n);
            bl = 2 * PL1 - bl;
        }
        sam_render(&d->sam, f0, p->morph, p->timbre, t0, t1, n);
        bl = pm(bl, pm(bl, 3 * PL1 - 2 * bl));
        bl = pm(bl, pm(bl, 3 * PL1 - 2 * bl));
        for (i = 0; i < n; i++) {
            aux[i] += pm(t0[i] - aux[i], bl);
            out[i] += pm(t1[i] - out[i], bl);
        }
    } else {
        int32_t x = pm(group - 2 * PL1, 4613734) * 6 - (PL1 >> 1);   /* x 0.275, HysteresisQuantizer2(6, 0.1) */
        int32_t q = (x + (x > d->wbq * PL1 ? -1677722 : 1677722) + (PL1 >> 1)) >> 24, wb;
        d->wbq = pclamp(q, 0, 5);
        wb = d->wbq - 1;
        d->env = wb >= 0;
        lpc_render(&d->lpc, p->trig, wb, f0, p->morph, p->timbre, wb >= 0 ? p->accent : PL1, aux, out, n);
    }
}
