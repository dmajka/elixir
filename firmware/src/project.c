/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: projects in flash. A project is what the LXR keeps on its SD card per project folder: the 64 kits, the
 * patterns (32 here, 64 on the LXR), the 64 songs and the global settings; here also the edit buffer and the morph
 * target, so a project comes back exactly as it was saved. Four project slots and the AUTO slot (the session, saved
 * while stopped and idle, loaded at power-on: the FM-1 runs on a battery, and the LXR would lose an unsaved session at
 * power-off).
 *
 * Flash: FL_DATA 0x97000.., six physical slots of 48 KiB. A project goes to a slot no current project uses (there is
 * always one), the commit record last; the newest valid record of a project wins at boot, so a cut-off save leaves the
 * previous copy in charge. Sparse encoding: a kit as its difference from the init kit, a pattern as the tracks and
 * steps that differ from an empty one (a step: the fields that differ), a song up to its last entry. The slots stay six
 * of 48 KiB, so version-1 projects stay where they are; in version 2 a step with its velocity and note set takes 3
 * bytes instead of 8. A save that does not fit reports PROJECT TOO BIG (the AUTO save too: autosave_poll). */

#define PROJ_BASE 0x97000u
#define PROJ_SLOT 0xC000u                           /* 48 KiB, 12 sectors */
#define PROJ_NPHYS 6u
#define PROJ_USER 4u                                /* projects 0..3 */
#define PROJ_AUTO 4u                                /* the session */
#define PROJ_NLOG 5u
#define PROJ_MAGIC 0x4A52504Bu                      /* "KPRJ" (Kettle's, kept: its projects load) */
#define PROJ_VERSION 11u                            /* (1 .. 10 still load: proj_read_payload) */
#define PROJ_PAYLOAD (PROJ_SLOT - 256u)

typedef struct {
    uint32_t magic, version, logical, seq, len, crc;
    char name[8];
    uint32_t rsv[3];
    uint32_t hcrc;
} proj_hdr_t;

static struct {
    int8_t phys[PROJ_NLOG];                         /* the physical slot holding each project's newest copy, -1 none */
    uint32_t seq[PROJ_NLOG];
    char name[PROJ_NLOG][8];
    uint8_t loaded;                                 /* the project slot the session came from (0xFF none) */
    uint32_t saved_sig;                             /* the session's signature at the last autosave / load */
    uint32_t failed_sig;                            /* .. at an autosave that failed (not tried again until it
                                                     * changes) */
    int8_t auto_err;                                /* that failure (the UI says so once, then clears it), 0 none */
    uint32_t sig_ms, idle_since;
    uint32_t last_sig;                              /* (0.5.1) the signature at the last check: a change restarts the
                                                     * rest */
} pj;

/* ------------------------------------------------- the stream to flash --- */
static struct {
    uint32_t base, off, crc, n;
    int err;
    uint8_t buf[256];
} pw;
static uint8_t pr_buf[256];
static struct { uint32_t base, off, end, n, pos, crc; int err; } pr;

static void pw_flush(void)
{
    if (pw.err || !pw.n)
        return;
    if (pw.off + 256u > PROJ_PAYLOAD) {
        pw.err = -20;                               /* the project does not fit its slot */
        return;
    }
    while (pw.n < 256u)
        pw.buf[pw.n++] = 0xFF;
    if (st_prog(pw.base + 256u + pw.off, pw.buf, 256u))
        pw.err = -21;
    pw.off += 256u;
    pw.n = 0;
}
static void pw_byte(uint32_t b)
{
    if (pw.err)                                     /* (too big: the rest is dropped, not written past buf) */
        return;
    pw.buf[pw.n++] = (uint8_t)b;
    pw.crc = st_crc32_add(pw.crc, &pw.buf[pw.n - 1u], 1);
    if (pw.n == 256u) {
        pw.n = 256u;
        pw_flush();
    }
}
static void pw_bytes(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        pw_byte(*b++);
}
static uint32_t pr_byte(void)
{
    uint8_t b;
    if (pr.err || pr.pos >= pr.end) {
        pr.err = -30;
        return 0;
    }
    if (pr.n == 0u) {
        if (st_read(pr.base + 256u + pr.off, pr_buf, 256u))
            pr.err = -31;
        pr.off += 256u;
        pr.n = 256u;
    }
    b = pr_buf[256u - pr.n];
    pr.n--;
    pr.pos++;
    pr.crc = st_crc32_add(pr.crc, &b, 1);
    return b;
}
static void pr_bytes(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = (uint8_t)pr_byte();
}

static void sp_defaults(void);                      /* (32: SPELLS, spells.c: KSP2) */
static void sp_write(void);
static void sp_read(void);

/* ------------------------------------------------------------ format --- */
/* Version 2 (Elixir 0.2): 32 patterns; Elixir's parameters (the sends, the reverb) in the edit buffer and the
 * kits; every count written, so a later version can add to the end of a part; a step as a mask of the fields that
 * differ from an empty step's, then those fields. Version 3 (0.3): the same, and a step's locks' high bits (SF_HI,
 * seq.c lhi); more of Elixir's parameters (their counts say so). Version 2 still loads (lock_v2 moves Elixir's locks
 * to their numbers), and so does version 1 (64 patterns, a step all of its 8 bytes): its first 32 patterns, songs
 * pointing past them to the last one, a voice's Out = FX BUS as FX SEND 127. Version 4 (0.5): a track's clock
 * and generator after its length (CLK_1X and GEN_OFF before), a step's condition and ratchet (SF_X). Version 5
 * (0.5): a pattern's GRIDS map (X, Y, chaos) after its shuffle and kit (0 before). Version 8 (0.10): a track's
 * direction after its bounce (DIR_FWD before). Version 9 (0.13): a track's second Euclid pulse after its direction
 * (none before); the global LFOs after PERFORM 2's settings (KGL1). Version 10 (0.16): a pattern's scale (root, scale,
 * the tracks that follow it) after its links (CHROMATIC, D1-D3 before); a track's melody (8 B) after its second Euclid
 * pulse (off before); a step's length in its condition's and
 * chance's top bits; three global LFOs (two before: the third off); the scale last chosen, DENSITY and STUTTER after
 * the LFOs (KSC1; STUTTER off before). Version 11 (0.18): a pattern's GROOVE (groove, amount, HUMANIZE, the tracks
 * that don't follow) after its scale (off before). */
#define SF_VOL 0x01u
#define SF_PROB 0x02u
#define SF_NOTE 0x04u
#define SF_L1 0x08u
#define SF_L2 0x10u
#define SF_TIME 0x20u
#define SF_HI 0x40u
#define SF_X 0x80u
static uint32_t step_diff(const pattern_t *p, uint32_t t, uint32_t k)   /* the fields that differ (SF_*) */
{
    const step_t *s = &p->step[t][k];
    return (s->volume != ((k & 7u) == 0u ? 0xFFu : 127u) ? SF_VOL : 0u) | (s->prob != 100u ? SF_PROB : 0u) |
           (s->note != SEQ_DEFAULT_NOTE ? SF_NOTE : 0u) | (s->p1n != NO_AUTOMATION || s->p1v ? SF_L1 : 0u) |
           (s->p2n != NO_AUTOMATION || s->p2v ? SF_L2 : 0u) | (p->timing[t][k] != 0x70u ? SF_TIME : 0u) |
           (p->lhi[t][k] ? SF_HI : 0u) | (p->cond[t][k] || p->rat[t][k] ? SF_X : 0u);
}
static int track_is_default(const pattern_t *p, uint32_t t)
{
    uint32_t k;
    if (p->len[t] != 16u || p->main[t][0] || p->main[t][1] || p->main[t][2] || p->main[t][3] ||
        p->clk[t] != CLK_1X || p->gen[t][0] || p->gen[t][1] || p->gen[t][2] || p->gen[t][3] ||
        p->bnc[t] != BNC_DEFAULT || p->dir[t] != DIR_FWD || p->eu2[t][0] || p->eu2[t][1] || p->eu2[t][2])
        return 0;
    for (k = 0; k < NML; k++)
        if (p->mel[t][k])
            return 0;
    for (k = 0; k < NUM_STEPS; k++)
        if (step_diff(p, t, k))
            return 0;
    return 1;
}
static void pw_u16(uint32_t v)
{
    pw_byte(v & 0xFFu);
    pw_byte(v >> 8);
}
static uint32_t pr_u16(void)
{
    uint32_t lo = pr_byte();
    return lo | pr_byte() << 8;
}
static void pr_skip(uint32_t n)
{
    while (n--)
        (void)pr_byte();
}

static void proj_write_payload(void)
{
    uint32_t i, k, t;
    uint8_t init[KIT_LEN];
    kit_init_values(init);
    pw_bytes("KPR5", 4);
    pw_u16(NPAR);
    if (pat_voices(&pat[sq.active]) == PV_FREE) {
        pw_bytes(par, NPAR);                       /* the edit buffer and the settings */
    } else {                                       /* (37: the shared voices, a LOCKED pattern's own in its slot) */
        uint8_t e[NPAR], k[KIT_LEN];
        if (pat_voices(&pat[sq.active]) == PV_LOCKED)
            kit_stash(PV_SLOT(sq.active));
        memcpy(e, par, NPAR);
        kit_get_row(KIT_SHARED, k);
        for (i = 0; i < KIT_LEN; i++)
            e[kit_par(i)] = k[i];
        pw_bytes(e, NPAR);
    }
    pw_u16(KIT_LEN);
    pw_bytes(morph_par, KIT_LEN);
    pw_byte(par[PAR_MORPH]);                       /* (0.13: the knob; the global LFOs move it from there) */
    pw_byte(pat_voices(&pat[sq.active]) == PV_FREE ? kit_loaded : pv_from);
    pw_byte(kit_morph_loaded);
    pw_byte(sq.active);
    pw_byte(sq.song);
    pw_byte(sq.roll_rate);
    pw_byte(sq.lane);
    for (k = 0; k < 3u * NTRACK; k++)               /* (0.10: unused, the Euclid settings before: now the pattern's) */
        pw_byte(0);
    pw_byte(NUM_KITS);
    for (k = 0; k < NUM_KITS; k++) {                /* kits: the name, which parameters differ from INIT, those */
        uint8_t bits[(KIT_LEN + 7) / 8] = {0}, kb[KIT_LEN];
        kit_get(k, kb);                             /* (25-0: a byte a value here, as before) */
        pw_bytes(kit_names[k], KIT_NAME_LEN);
        for (i = 0; i < KIT_LEN; i++)
            if (kb[i] != init[i])
                bits[i >> 3] |= (uint8_t)(1u << (i & 7u));
        pw_bytes(bits, sizeof bits);
        for (i = 0; i < KIT_LEN; i++)
            if (bits[i >> 3] & (1u << (i & 7u)))
                pw_byte(kb[i]);
    }
    pw_byte(NUM_PATTERN);
    for (i = 0; i < NUM_PATTERN; i++) {             /* patterns: the tracks not empty, their steps that differ */
        const pattern_t *p = &pat[i];
        uint32_t tm = 0;
        for (t = 0; t < NTRACK; t++)
            if (!track_is_default(p, t))
                tm |= 1u << t;
        uint32_t lm = 0;
        for (k = 0; k < NLINK; k++)
            if (p->link[k][1] & 15u)
                lm |= 1u << k;
        if (p->shuffle || p->kit || p->gx || p->gy || p->gchaos || lm || p->cdepth != 127u || p->sroot || p->scale ||
            p->sfollow != SFOLLOW_DEFAULT || p->groove || p->gamt != GAMT_DEFAULT || p->ghum || p->gskip)
            tm |= 0x80u;
        pw_byte(tm);
        if (tm & 0x80u) {
            pw_byte(p->shuffle);
            pw_byte(p->kit);
            pw_byte(p->gx);
            pw_byte(p->gy);
            pw_byte(p->gchaos);
            pw_byte(p->cdepth);                     /* (version 6: the links, those set) */
            pw_byte(lm);
            for (k = 0; k < NLINK; k++)
                if (lm & (1u << k))
                    pw_bytes(p->link[k], 4);
            pw_byte(p->sroot);                      /* (version 10: the scale) */
            pw_byte(p->scale);
            pw_byte(p->sfollow);
            pw_byte(p->groove);                     /* (version 11: the groove) */
            pw_byte(p->gamt);
            pw_byte(p->ghum);
            pw_byte(p->gskip);
        }
        for (t = 0; t < NTRACK; t++) {
            uint8_t bits[8] = {0};
            if (!(tm & (1u << t)))
                continue;
            pw_byte(p->len[t]);
            pw_byte(p->clk[t]);
            pw_bytes(p->gen[t], 4);
            pw_byte(p->bnc[t]);
            pw_byte(p->dir[t]);
            pw_bytes(p->eu2[t], 3);
            pw_bytes(p->mel[t], NML);               /* (version 10: the melody) */
            for (k = 0; k < 4u; k++)
                pw_u16(p->main[t][k]);
            for (k = 0; k < NUM_STEPS; k++)
                if (step_diff(p, t, k))
                    bits[k >> 3] |= (uint8_t)(1u << (k & 7u));
            pw_bytes(bits, 8);
            for (k = 0; k < NUM_STEPS; k++) {
                const step_t *st = &p->step[t][k];
                uint32_t m = step_diff(p, t, k);
                if (!m)
                    continue;
                pw_byte(m);
                if (m & SF_VOL)
                    pw_byte(st->volume);
                if (m & SF_PROB)
                    pw_byte(st->prob);
                if (m & SF_NOTE)
                    pw_byte(st->note);
                if (m & SF_L1) {
                    pw_byte(st->p1n);
                    pw_byte(st->p1v);
                }
                if (m & SF_L2) {
                    pw_byte(st->p2n);
                    pw_byte(st->p2v);
                }
                if (m & SF_TIME)
                    pw_byte(p->timing[t][k]);
                if (m & SF_HI)
                    pw_byte(p->lhi[t][k]);
                if (m & SF_X) {
                    pw_byte(p->cond[t][k]);
                    pw_byte(p->rat[t][k]);
                }
            }
        }
    }
    pw_byte(NUM_SONGS);
    for (i = 0; i < NUM_SONGS; i++) {               /* songs: up to the last entry that is not END */
        uint32_t n = MAX_SONG_LENGTH;
        while (n && songs[i][n - 1u] == SONG_END)
            n--;
        pw_byte(n);
        pw_bytes(songs[i], n);
    }
    pw_bytes("KPF1", 4);                            /* PERF (0.4, after the songs: 0.3 reads up to them) */
    pw_byte(NMACRO);
    for (i = 0; i < NMACRO; i++) {
        pw_u16(pfs.m[i].par);
        pw_byte(pfs.m[i].lo);
        pw_byte(pfs.m[i].hi);
        pw_byte(pfs.m[i].val);
    }
    pw_byte(pfs.repeat);
    pw_byte(pfs.tape);
    pw_bytes("KPF2", 4);                            /* PERFORM 2's settings (0.6) */
    pw_byte(NPF2SET);
    pw_bytes(&pfs.drill, NPF2SET);
    pw_bytes("KGL1", 4);                            /* the global LFOs (0.13) */
    pw_byte(NGLFO);
    for (i = 0; i < NGLFO; i++) {
        pw_byte(NGLSET);
        pw_byte(gls[i].dest);
        pw_byte((uint8_t)gls[i].depth);
        pw_byte(gls[i].len);
        pw_byte(gls[i].wave);
        pw_byte(gls[i].mode);
        pw_byte(gls[i].trig);
        pw_byte(gls[i].phase);
    }
    pw_bytes("KSC1", 4);                            /* 19: the scale last chosen, DENSITY, STUTTER (0.16), VARY (31) */
    pw_byte(11);
    pw_byte(scl_last.root);
    pw_byte(scl_last.scale);
    pw_byte((uint8_t)pfs.dens);
    pw_byte(pfs.ghost);
    pw_byte(pfs.build);
    pw_byte(pfs.dex);
    pw_byte(pfs.st_chance);
    pw_byte(pfs.st_size);
    pw_byte(pfs.st_len);
    pw_byte(pfs.st_vary);
    pw_byte(pfs.dvary);
    pw_bytes("KSP2", 4);                            /* 32: SPELLS: the knobs, the targets, the GRIMOIRE's recipes */
    sp_write();
}

/* a version-2 lock code: Elixir's parameters were at the numbers the LXR leaves free (247..254: PAR_FX_SEND1 ..,
 * 221..227: the next 7); now at their own (lock_set) */
static void lock_v2(pattern_t *p, uint32_t t, uint32_t k, uint32_t lane)
{
    uint32_t c = lock_get(p, t, k, lane);
    if (c >= 247u && c <= 254u)
        lock_set(p, t, k, lane, NUM_PARAMS + c - 247u);
    else if (c >= 221u && c <= 227u)
        lock_set(p, t, k, lane, NUM_PARAMS + 8u + c - 221u);
}
/* a step of version ver into p (step k of track t) */
static void pr_step(uint32_t ver, pattern_t *p, uint32_t t, uint32_t k)
{
    step_t *st = &p->step[t][k];
    uint32_t m;
    if (ver < 2u) {
        pr_bytes(st, sizeof(step_t));
        p->timing[t][k] = (uint8_t)pr_byte();
        return;
    }
    m = pr_byte();
    if (m & SF_VOL)
        st->volume = (uint8_t)pr_byte();
    if (m & SF_PROB)
        st->prob = (uint8_t)pr_byte();
    if (m & SF_NOTE)
        st->note = (uint8_t)pr_byte();
    if (m & SF_L1) {
        st->p1n = (uint8_t)pr_byte();
        st->p1v = (uint8_t)pr_byte();
    }
    if (m & SF_L2) {
        st->p2n = (uint8_t)pr_byte();
        st->p2v = (uint8_t)pr_byte();
    }
    if (m & SF_TIME)
        p->timing[t][k] = (uint8_t)pr_byte();
    if (m & SF_HI)
        p->lhi[t][k] = (uint8_t)pr_byte();
    if (m & SF_X) {
        uint32_t c = pr_byte();                      /* (version 10: the length in the top bits) */
        p->cond[t][k] = (uint8_t)((c & COND_MASK) < NCOND ? c : c & ~COND_MASK);
        p->rat[t][k] = (uint8_t)(pr_byte() & (ver >= 6u ? 0xFFu : 0x7Fu));   /* (version 6: RAT_BOUNCE) */
    }
    if (ver == 2u) {
        lock_v2(p, t, k, 0);
        lock_v2(p, t, k, 1);
    }
}

/* 0.9: a kit's (kit layout) model from before 0.9 as Rings' nearest: SKIN BAR BELL PLATE (struck bodies) MODAL, TUBE
 * (a plucked line) STRING */
static void proj_models_08(uint8_t *k)
{
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        uint8_t *m = &k[kit_idx(PAR_MODEL1 + i)];
        *m = *m >= 1u && *m <= 4u ? 1u : *m == 5u ? 2u : *m;
    }
}
static int proj_read_payload(uint32_t ver)
{
    uint32_t i, k, t, n, nk, npat;
    uint8_t init[KIT_LEN], tag[4];
    kit_init_values(init);
    pr_bytes(tag, 4);
    if (memcmp(tag, ver < 2u ? "KPR1" : ver < 3u ? "KPR2" : ver < 4u ? "KPR3" : ver < 5u ? "KPR4" : "KPR5", 4))
        return -32;
    kit_init_par();                                 /* (Elixir's parameters a version-1 project lacks) */
    n = ver < 2u ? NUM_PARAMS : pr_u16();
    pr_bytes(par, n < NPAR ? n : NPAR);
    pr_skip(n > NPAR ? n - NPAR : 0u);
    nk = ver < 2u ? END_OF_SOUND_PARAMETERS : pr_u16();
    if (nk > 8u * 64u)
        return -33;
    memcpy(morph_par, init, KIT_LEN);
    pr_bytes(morph_par, nk < KIT_LEN ? nk : KIT_LEN);
    pr_skip(nk > KIT_LEN ? nk - KIT_LEN : 0u);
    par[PAR_MORPH] = morph_amt = (uint8_t)pr_byte();   /* (0.13: the knob; the LFOs' push: morph_update) */
    kit_loaded = (uint8_t)(pr_byte() % NUM_KITS);
    kit_morph_loaded = (uint8_t)(pr_byte() % NUM_KITS);
    sq.active = (uint8_t)pr_byte();
    if (sq.active >= NUM_PATTERN)
        sq.active = 0;
    sq.pending = sq.active;
    sq.song = (uint8_t)(pr_byte() % NUM_SONGS);
    sq.roll_rate = (uint8_t)(pr_byte() % 14u);
    sq.lane = (uint8_t)(pr_byte() & 1u);
    for (k = 0; k < 3u * NTRACK; k++)               /* (the Euclid settings before 0.10: the pattern's now) */
        (void)pr_byte();
    n = ver < 2u ? NUM_KITS : pr_byte();
    for (k = 0; k < n; k++) {                       /* (kits past NUM_KITS: read, dropped) */
        uint8_t bits[64], v, kb[KIT_LEN], *kp = k < NUM_KITS ? kb : 0;
        char nm[KIT_NAME_LEN];
        pr_bytes(nm, KIT_NAME_LEN);
        pr_bytes(bits, (nk + 7u) / 8u);
        for (i = 0; i < nk; i++) {
            v = i < KIT_LEN ? init[i] : 0u;
            if (bits[i >> 3] & (1u << (i & 7u)))
                v = (uint8_t)pr_byte();
            if (kp && i < KIT_LEN)
                kp[i] = v;
        }
        if (kp) {
            for (i = nk; i < KIT_LEN; i++)
                kp[i] = init[i];
            memcpy(kit_names[k], nm, KIT_NAME_LEN);
            if (ver < 7u)
                proj_models_08(kp);
            kit_sanitize(kp);
            kit_put(k, kp);
        }
    }
    if (ver < 7u) {
        uint32_t i2;
        proj_models_08(morph_par);
        for (i2 = 0; i2 < 3u; i2++)                  /* (the edit buffer: par[], not the kit layout) */
            par[PAR_MODEL1 + i2] = par[PAR_MODEL1 + i2] >= 1u && par[PAR_MODEL1 + i2] <= 4u ? 1u
                                 : par[PAR_MODEL1 + i2] == 5u ? 2u : par[PAR_MODEL1 + i2];
    }
    kit_sanitize(morph_par);
    out_migrate(&par[PAR_AUDIO_OUT1], &par[PAR_FX_SEND1]);
    scl_last.root = scl_last.scale = 0;             /* (a pattern without a scale: CHROMATIC; KSC1 sets the last) */
    npat = ver < 2u ? 64u : pr_byte();
    for (i = 0; i < npat; i++) {                    /* (patterns past NUM_PATTERN: read into pat_backup, dropped) */
        pattern_t *p = i < NUM_PATTERN ? &pat[i] : &pat_backup;
        uint32_t tm = pr_byte();
        seq_clear_pattern(p);
        p->shuffle = p->kit = 0;
        p->gx = p->gy = p->gchaos = 0;
        if (tm & 0x80u) {
            p->shuffle = (uint8_t)pr_byte();
            {   /* (37: bits 6-7 the pattern's voices, PV_*) */
                uint32_t kb = pr_byte();
                p->kit = (uint8_t)((kb & 63u) | ((kb >> 6) < NPV ? kb & 0xC0u : 0u));
            }
            if (ver >= 5u) {
                p->gx = (uint8_t)(pr_byte() & 127u);
                p->gy = (uint8_t)(pr_byte() & 127u);
                p->gchaos = (uint8_t)(pr_byte() & 127u);
            }
            if (ver >= 6u) {
                uint32_t lm;
                p->cdepth = (uint8_t)(pr_byte() & 127u);
                lm = pr_byte();
                for (k = 0; k < NLINK; k++)
                    if (lm & (1u << k))
                        pr_bytes(p->link[k], 4);
            }
            if (ver >= 10u) {
                p->sroot = (uint8_t)(pr_byte() % 12u);
                p->scale = (uint8_t)pr_byte();
                p->sfollow = (uint8_t)(pr_byte() & ((1u << NTRACK) - 1u));
                if (p->scale >= NSCALE)
                    p->scale = SC_CHROM;
            }
            if (ver >= 11u) {
                p->groove = (uint8_t)pr_byte();
                p->gamt = (uint8_t)(pr_byte() & 127u);
                p->ghum = (uint8_t)(pr_byte() & 127u);
                p->gskip = (uint8_t)(pr_byte() & ((1u << NTRACK) - 1u));
                if (p->groove > NGROOVE)
                    p->groove = 0;
            }
        }
        for (t = 0; t < NTRACK; t++) {
            uint8_t bits[8];
            if (!(tm & (1u << t)))
                continue;
            p->len[t] = (uint8_t)pr_byte();
            if (p->len[t] > NUM_STEPS)
                p->len[t] = NUM_STEPS;
            if (ver >= 4u) {
                p->clk[t] = (uint8_t)pr_byte();
                pr_bytes(p->gen[t], 4);
                if (p->clk[t] >= NCLK)
                    p->clk[t] = CLK_1X;
                if (p->gen[t][0] >= NGEN)
                    p->gen[t][0] = GEN_OFF;
            }
            if (ver >= 6u) {
                p->bnc[t] = (uint8_t)pr_byte();
                if ((p->bnc[t] >> 2) > 24u)
                    p->bnc[t] = BNC_DEFAULT;
            }
            if (ver >= 8u) {
                p->dir[t] = (uint8_t)pr_byte();
                if (p->dir[t] >= NDIR)
                    p->dir[t] = DIR_FWD;
            }
            if (ver >= 9u) {
                pr_bytes(p->eu2[t], 3);
                if (p->eu2[t][2] >= NEUOP)
                    p->eu2[t][2] = EU_OR;
            }
            if (ver >= 10u) {
                pr_bytes(p->mel[t], NML);
                if (p->mel[t][ML_SHAPE] >= NMEL)
                    p->mel[t][ML_SHAPE] = MEL_OFF;
            }
            for (k = 0; k < 4u; k++)
                p->main[t][k] = (uint16_t)pr_u16();
            pr_bytes(bits, 8);
            for (k = 0; k < NUM_STEPS; k++)
                if (bits[k >> 3] & (1u << (k & 7u)))
                    pr_step(ver, p, t, k);
        }
    }
    for (i = npat; i < NUM_PATTERN; i++) {
        seq_clear_pattern(&pat[i]);
        pat[i].shuffle = pat[i].kit = 0;
        pat[i].gx = pat[i].gy = pat[i].gchaos = 0;
    }
    n = ver < 2u ? NUM_SONGS : pr_byte();
    for (i = 0; i < n; i++) {                       /* (a song's pattern past the last: the last) */
        uint32_t len = pr_byte();
        for (k = 0; k < len; k++) {
            uint32_t v = pr_byte();
            if (i < NUM_SONGS && k < MAX_SONG_LENGTH)
                songs[i][k] = (uint8_t)(ver < 2u && v >= NUM_PATTERN && v < SONG_END ? NUM_PATTERN - 1u
                                        : song_row(v) == SONG_END && v != SONG_END ? SONG_END : v);   /* (0.19: the
                                                                            * times in bits 5-7, song_row) */
        }
        for (k = len; i < NUM_SONGS && k < MAX_SONG_LENGTH; k++)
            songs[i][k] = SONG_END;
    }
    perf_defaults();                                /* PERF's settings, when the project has them */
    sp_defaults();                                  /* (32: SPELLS', KSP2) */
    if (!pr.err && pr.pos + 4u <= pr.end) {
        pr_bytes(tag, 4);
        if (!memcmp(tag, "KPF1", 4)) {
            n = pr_byte();
            for (i = 0; i < n; i++) {
                uint32_t p = pr_u16(), lo = pr_byte(), hi = pr_byte(), v = pr_byte();
                if (i < NMACRO) {
                    pfs.m[i].par = (uint16_t)(par_lockable(p) ? p : 0u);
                    pfs.m[i].lo = (uint8_t)lo;
                    pfs.m[i].hi = (uint8_t)hi;
                    pfs.m[i].val = (uint8_t)(v & 127u);
                }
            }
            pfs.repeat = (uint8_t)(pr_byte() & 3u);
            pfs.tape = (uint8_t)(pr_byte() & 127u);
        }
    }
    if (!pr.err && pr.pos + 4u <= pr.end) {
        pr_bytes(tag, 4);
        if (!memcmp(tag, "KPF2", 4)) {
            n = pr_byte();
            for (i = 0; i < n; i++) {
                uint32_t v = pr_byte();
                if (i < NPF2SET)
                    (&pfs.drill)[i] = (uint8_t)(i == 4u ? clamp32((int32_t)v, 24, 96) : i == 7u ? v % 3u : v & 127u);
            }
        }
    }
    if (!pr.err && pr.pos + 4u <= pr.end) {
        pr_bytes(tag, 4);
        if (!memcmp(tag, "KGL1", 4)) {               /* 0.13: the global LFOs */
            n = pr_byte();
            for (i = 0; i < n; i++) {
                uint8_t b[NGLSET] = {0};
                uint32_t c = pr_byte();
                pr_bytes(b, c < NGLSET ? c : NGLSET);
                pr_skip(c > NGLSET ? c - NGLSET : 0u);
                if (i >= NGLFO || c < NGLSET)
                    continue;
                gls[i].dest = (uint8_t)(b[0] < NGD ? b[0] : GD_OFF);
                gls[i].depth = (int8_t)clamp32((int8_t)b[1], -127, 127);
                gls[i].len = (uint8_t)(b[2] < NGLEN ? b[2] : 7u);
                gls[i].wave = (uint8_t)(b[3] % NLFO);
                gls[i].mode = (uint8_t)(b[4] < NGM ? b[4] : GM_FREE);
                gls[i].trig = (uint8_t)(b[5] < NGT ? b[5] : GT_PLAY);
                gls[i].phase = (uint8_t)(b[6] & 127u);
            }
        }
    }
    if (!pr.err && pr.pos + 4u <= pr.end) {
        pr_bytes(tag, 4);
        if (!memcmp(tag, "KSC1", 4)) {               /* 19: the scale last chosen, DENSITY; STUTTER */
            uint8_t b[11] = {0, 0, 0, 40, DB_GHOSTS, 0, 0, 1, 2, SV_OFF, 0};   /* (31: VARY 0 before) */
            n = pr_byte();
            pr_bytes(b, n < 11u ? n : 11u);
            pr_skip(n > 11u ? n - 11u : 0u);
            scl_last.root = (uint8_t)(b[0] % 12u);
            scl_last.scale = (uint8_t)(b[1] < NSCALE ? b[1] : SC_CHROM);
            pfs.dens = (int8_t)clamp32((int8_t)b[2], -64, 63);
            pfs.ghost = (uint8_t)(b[3] & 127u ? b[3] & 127u : 1u);
            pfs.build = (uint8_t)(b[4] < NDB ? b[4] : DB_GHOSTS);
            pfs.dex = (uint8_t)(b[5] & ((1u << NTRACK) - 1u));
            pfs.st_chance = (uint8_t)(b[6] & 127u);
            pfs.st_size = (uint8_t)(b[7] < NSTSIZE ? b[7] : 1u);
            pfs.st_len = (uint8_t)(b[8] < NSTLEN ? b[8] : 2u);
            pfs.st_vary = (uint8_t)(b[9] < NSV ? b[9] : SV_OFF);
            pfs.dvary = (uint8_t)(b[10] & 127u);
        }
    }
    if (!pr.err && pr.pos + 4u <= pr.end) {
        pr_bytes(tag, 4);
        if (!memcmp(tag, "KSP2", 4))                 /* 32: SPELLS (0.19-test6's KSP1: left, the defaults) */
            sp_read();
    }
    return pr.err;
}

/* ------------------------------------------------------- the slots --- */
static uint32_t phys_base(uint32_t ph) { return PROJ_BASE + ph * PROJ_SLOT; }
static int hdr_read(uint32_t ph, proj_hdr_t *h)
{
    if (st_read(phys_base(ph), h, sizeof *h) || h->magic != PROJ_MAGIC || h->version < 1u ||
        h->version > PROJ_VERSION || h->logical >= PROJ_NLOG || h->len > PROJ_PAYLOAD ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}
static void proj_scan(void)                         /* the newest copy of each project */
{
    uint32_t ph, l;
    for (l = 0; l < PROJ_NLOG; l++) {
        pj.phys[l] = -1;
        pj.seq[l] = 0;
        memcpy(pj.name[l], l == PROJ_AUTO ? "SESSION " : "        ", 8);
    }
    if (!flash_ok)
        return;
    for (ph = 0; ph < PROJ_NPHYS; ph++) {
        proj_hdr_t h;
        if (hdr_read(ph, &h))
            continue;
        if (pj.phys[h.logical] < 0 || (int32_t)(h.seq - pj.seq[h.logical]) > 0) {
            pj.phys[h.logical] = (int8_t)ph;
            pj.seq[h.logical] = h.seq;
            memcpy(pj.name[h.logical], h.name, 8);
        }
    }
}
static int proj_exists(uint32_t l) { return l < PROJ_NLOG && pj.phys[l] >= 0; }

static uint8_t proj_lock;                           /* 0.7: the library is writing a project (library.c) */
static int proj_write(uint32_t l, const char *name)
{
    uint32_t ph, l2, s;
    proj_hdr_t h;
    int used[PROJ_NPHYS] = {0}, target = -1;
    if (!flash_ok || l >= PROJ_NLOG || proj_lock)
        return -1;
    for (l2 = 0; l2 < PROJ_NLOG; l2++)
        if (pj.phys[l2] >= 0)
            used[pj.phys[l2]] = 1;
    for (ph = 0; ph < PROJ_NPHYS && target < 0; ph++)
        if (!used[ph])
            target = (int)ph;
    if (target < 0)
        return -2;
    pat_backup = pat[sq.active];                    /* (the LXR saves the playing pattern into its slot first) */
    for (s = 0; s < PROJ_SLOT; s += 0x1000u)
        if (st_erase(phys_base((uint32_t)target) + s))
            return -3;
    memset(&pw, 0, sizeof pw);
    pw.base = phys_base((uint32_t)target);
    pw.crc = 0xFFFFFFFFu;
    proj_write_payload();
    {
        uint32_t len = pw.off + pw.n;
        pw_flush();
        if (pw.err)
            return pw.err;
        memset(&h, 0xFF, sizeof h);
        h.magic = PROJ_MAGIC;
        h.version = PROJ_VERSION;
        h.logical = l;
        h.seq = pj.seq[l] + 1u;
        h.len = len;
        h.crc = ~pw.crc;
        memcpy(h.name, name, 8);
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        if (st_prog(phys_base((uint32_t)target), &h, sizeof h))
            return -4;
    }
    proj_scan();
    return pj.phys[l] == target ? 0 : -5;
}

static int proj_save(uint32_t l, const char *name)  /* 0.5.1: hushed (storage_hw.c flash_hush) */
{
    int rc;
    flash_hush();
    rc = proj_write(l, name);
    flash_unhush();
    return rc;
}

static void proj_after_load(void)                   /* the session as the project left it */
{
    uint32_t t;
    if (par[PAR_PATTERN_MODE] == 1u) {              /* (37: KIT CHANGE AUTO, from before 0.20: every pattern KIT) */
        for (t = 0; t < NUM_PATTERN; t++)
            if (pat_voices(&pat[t]) == PV_FREE)
                pat_set_voices(&pat[t], PV_KIT, pat_kit(&pat[t]));
    }
    par[PAR_PATTERN_MODE] = 0;
    pv_skip = 0;                                    /* (the edit buffer: the shared voices) */
    pv_switch(PV_FREE, sq.active, sq.active);
    pat_backup = pat[sq.active];
    sq.gsync = (1u << NTRACK) - 1u;                 /* (the generators' live tracks from the loaded pattern) */
    for (t = 0; t < NTRACK; t++)
        sq.idx[t] = -1;
    morph_par[0] = 0;
    kit_loads++;
    snd_post_all();
}
static int proj_load(uint32_t l)
{
    proj_hdr_t h;
    int rc;
    if (!proj_exists(l) || hdr_read((uint32_t)pj.phys[l], &h))
        return -1;
    memset(&pr, 0, sizeof pr);
    pr.base = phys_base((uint32_t)pj.phys[l]);
    pr.end = h.len;
    pr.crc = 0xFFFFFFFFu;
    {   /* the whole payload's CRC first: a damaged project is not half loaded */
        uint32_t c = 0xFFFFFFFFu, off = 0;
        while (off < h.len) {
            uint32_t n = h.len - off > 256u ? 256u : h.len - off;
            if (st_read(pr.base + 256u + off, pr_buf, n))
                return -2;
            c = st_crc32_add(c, pr_buf, n);
            off += n;
        }
        if (~c != h.crc)
            return -3;
    }
    rc = proj_read_payload(h.version);
    proj_after_load();
    return rc;
}

/* -------------------------------------------------------- the session --- */
static uint32_t session_sig(void)                   /* FNV-1a over what a project holds */
{
    uint32_t s = 0x811C9DC5u, i;
    const uint8_t *b;
#define SIG(p, n) for (b = (const uint8_t *)(p), i = 0; i < (uint32_t)(n); i++) s = (s ^ b[i]) * 16777619u
    SIG(par, NPAR);
    SIG(kits, sizeof kits);
    SIG(kit_names, sizeof kit_names);
    SIG(pat, sizeof pat);
    SIG(songs, sizeof songs);
    SIG(morph_par, sizeof morph_par);
    SIG(&pfs, sizeof pfs);
    SIG(gls, sizeof gls);
    SIG(&scl_last, sizeof scl_last);
#undef SIG
    return s;
}
/* the main loop: the session into AUTO once it changed and has rested 8 s, stopped (an erase silences the audio).
 * 0.5.1: the rest starts at the stop and at each change (it used to count from the last save, so a save came within
 * 2 s of a stop after a long play), and the output must have been quiet for half a second (the reverb and delay
 * tails gone: the save's hush cuts nothing you hear); after 30 s at rest it saves anyway (a frozen delay rings on).
 * A save that fails (PROJECT TOO BIG: -20) is reported (pj.auto_err, the UI's message) and not tried again until
 * the session changes: each try erases a 48 KiB slot */
static uint8_t autosave_on = 1;
static void autosave_poll(void)
{
    if (!flash_ok || !autosave_on || proj_lock)
        return;
    if (sq.running) {
        pj.idle_since = fm1_ms;
        return;
    }
    if (fm1_ms - pj.sig_ms < 2000u)
        return;
    pj.sig_ms = fm1_ms;
    {
        uint32_t sig = session_sig();
        int rc;
        if (sig == pj.saved_sig || sig == pj.failed_sig || sig != pj.last_sig) {
            pj.idle_since = fm1_ms;
            pj.last_sig = sig;
            return;
        }
        if (fm1_ms - pj.idle_since < 8000u)
            return;
        if (quiet_blocks < FS / BLOCK / 2u && fm1_ms - pj.idle_since < 30000u)
            return;
        rc = proj_save(PROJ_AUTO, "SESSION ");
        if (rc == 0) {
            pj.saved_sig = sig;
        } else {
            pj.failed_sig = sig;
            pj.auto_err = (int8_t)(rc < -127 ? -127 : rc);
        }
        pj.idle_since = fm1_ms;
    }
}

/* ------------------------------------------------------- settings --- */
#define SET_MAGIC 0x3154454Bu                       /* "KET1" (Kettle's, kept) */
/* 0.8: kvel, kacc: the keys' velocity and accent (seq.c key_vel, key_acc; 0, as settings before 0.8 hold: 100, 127).
 * 0.9: pdrop: a pattern's unsaved edits dropped when another plays (the LXR's way; 0, the default: kept).
 * 0.12: remote: the remote's channel 1..16, 17 off (0, as settings before 0.12 hold: 16); rknobs: its knobs (RK_*);
 * cakit: Control All moves the kit only, not the morph target too (0, the default: both).
 * 0.13: pmem: a button opens the page it showed last (ui.c pmem_*; 0, the default: its first).
 * 0.16: remote is the TRACKS channel (0: 15), rglobal the GLOBAL one (1..16, 17 off, 0: 16); settings from before
 * (16 bytes) start both at their defaults, or both off if the remote was.
 * 31: medit: a knob's edit with morph on moves the morph target too (0, the default: both kits; 1 the kit only). In
 * the struct's padding before: settings from 0.16 .. 0.19 hold 0 there.
 * 0.19 (33): kbslots: the kit browser lists the slots first (0, the default: the factory kits first); in the padding too */
#define SET_LEN_015 16u
typedef struct {
    uint32_t magic;
    uint8_t palette, leds, autosave, accel, project, kvel, kacc, pdrop, remote, rknobs, cakit, pmem, rglobal, medit, kbslots;
} elixir_settings_t;
static elixir_settings_t kset = {SET_MAGIC, 2, 1, 1, 1, 0xFF, 100, 127, 0, 0, 0, 0, 0, 0, 0, 0};   /* (palette 2: AMBER) */
static uint8_t remote_of(uint32_t v, uint32_t dflt) { return (uint8_t)(v > 16u ? 0xFFu : v ? v - 1u : dflt - 1u); }
static uint8_t settings_dirty;
static uint32_t settings_ms;
static void settings_later(void)
{
    settings_dirty = 1;
    settings_ms = fm1_ms;
}
static void settings_poll(void)                     /* queued: written stopped, a second after the last change */
{
    if (!settings_dirty || !flash_ok || sq.running || fm1_ms - settings_ms < 1000u)
        return;
    settings_dirty = 0;
    kset.autosave = autosave_on;
    st_save(OBJ_SETTINGS, &kset, sizeof kset);
}

static void persist_boot(void)                      /* power-on: the flash, the settings, the projects, the session */
{
    uint32_t f = irq_save();
    elixir_settings_t s = kset;
    int n;
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;  /* the expected 1 MiB part, else RAM only */
    irq_restore(f);
    pj.loaded = 0xFF;
    if (!flash_ok)
        return;
    n = st_load(OBJ_SETTINGS, &s, sizeof s);
    if (n >= (int)SET_LEN_015 && s.magic == SET_MAGIC) {
        if (n < (int)sizeof s) {                     /* (before 0.16: one remote channel; the map is new) */
            s.remote = s.remote > 16u ? 17u : 0u;
            s.rglobal = s.remote;
        }
        kset = s;
    }
    autosave_on = kset.autosave;
    key_vel = kset.kvel ? kset.kvel : 100u;
    key_acc = kset.kacc ? kset.kacc : 127u;
    pat_keep = !kset.pdrop;
    remote_ch = remote_of(kset.remote, 15u);
    remote_gch = remote_of(kset.rglobal, 16u);
    proj_scan();
    kl_scan();
}
static void session_boot(void)                      /* after engine_init: the last session */
{
    if (proj_exists(PROJ_AUTO) && proj_load(PROJ_AUTO) == 0) {
        pj.loaded = kset.project;
        pj.saved_sig = session_sig();
    } else {
        pj.saved_sig = session_sig();
    }
    pj.idle_since = pj.sig_ms = fm1_ms;
}
