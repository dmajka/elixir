/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the kit library (0.8). Kits and single sounds kept in flash outside the projects, read one at a time, so
 * more of them cost no RAM: 128 kits and 128 sounds (one voice's settings), each with a name. LOAD and SAVE reach them
 * (a kit's source LIBRARY, the type SOUND), LOAD MORPH takes its target from there, the sound browser lists them, and
 * the library page backs them up (library.c).
 *
 * Flash: the FM-1 package's USR area, 0xEA000..0xFBFFF (18 sectors), which no updater writes and the stock firmware's
 * own store (0x93000..0xE8FFF) leaves alone. A log of entries in 256-byte pages: a kit takes 2 pages, a sound 1. An
 * entry is its 32-byte header (the commit record: written last, after its payload) then the payload; a slot's newest
 * valid entry (seq) wins at boot, so a write torn at any point leaves the slot as it was. A deleted slot is an entry of
 * no payload. One sector is kept erased: when no sector has room, the one with the most dead pages has its live entries
 * copied into it and is erased, becoming the erased one. */

#define KL_BASE 0xEA000u
#define KL_NSEC 18u
#define KL_PAGES (KL_NSEC * 16u)
#define KL_KITS 128u
#define KL_SOUNDS 128u
#define KL_MAGIC 0x4C4Bu                            /* "KL" */
#define KL_HDR 32u
enum { KLK_KIT = 1, KLK_SOUND = 2 };

typedef struct {
    uint16_t magic;
    uint8_t kind, slot;
    uint32_t seq;
    uint16_t len, rsv;
    uint32_t crc;                                    /* the payload's */
    char name[8];
    uint32_t hcrc;                                   /* the header's above */
} kl_hdr_t;
_Static_assert(sizeof(kl_hdr_t) == 28u, "the library's commit record");
_Static_assert(KL_HDR + KIT_LEN <= 512u, "a library kit: 2 pages");

static struct {
    int16_t at[2][128];                              /* [kind - 1][slot]: the first page of its newest entry, -1 none */
    uint8_t has[2][16];                              /* .. bit: it holds something (not deleted) */
    uint8_t erased[KL_PAGES / 8u];                   /* bit: the page is erased */
    uint8_t spare;                                   /* the sector kept erased */
    uint8_t ok;
    uint32_t seq;                                    /* the next entry's */
} kl;

static uint32_t kl_off(uint32_t pg) { return KL_BASE + pg * 256u; }
static int kl_is_erased(uint32_t pg) { return (kl.erased[pg >> 3] >> (pg & 7u)) & 1u; }
static void kl_set_erased(uint32_t pg, int on)
{
    kl.erased[pg >> 3] = (uint8_t)(on ? kl.erased[pg >> 3] | 1u << (pg & 7u) : kl.erased[pg >> 3] & ~(1u << (pg & 7u)));
}
static uint32_t kl_npages(uint32_t len) { return (KL_HDR + len + 255u) / 256u; }
static int kl_exists(uint32_t kind, uint32_t slot)
{
    return kind >= 1u && kind <= 2u && slot < 128u && ((kl.has[kind - 1u][slot >> 3] >> (slot & 7u)) & 1u);
}
static int kl_head(uint32_t pg, kl_hdr_t *h)          /* a valid commit record at page pg: 0 */
{
    if (st_read(kl_off(pg), h, sizeof *h))
        return -1;
    if (h->magic != KL_MAGIC || h->hcrc != st_crc32(h, sizeof *h - 4u) || h->kind < 1u || h->kind > 2u ||
        h->slot >= 128u || KL_HDR + h->len > 512u || (pg & 15u) + kl_npages(h->len) > 16u)
        return -1;
    return 0;
}
static uint32_t kl_payload_crc(uint32_t pg, uint32_t len)
{
    uint32_t c = 0xFFFFFFFFu, off = 0;
    while (off < len) {
        uint8_t b[64];
        uint32_t k = len - off > 64u ? 64u : len - off;
        if (st_read(kl_off(pg) + KL_HDR + off, b, k))
            return 0;
        c = st_crc32_add(c, b, k);
        off += k;
    }
    return ~c;
}
/* the pages the newest entries take: bit per page */
static void kl_live(uint8_t *live)
{
    uint32_t k, s;
    memset(live, 0, KL_PAGES / 8u);
    for (k = 0; k < 2u; k++)
        for (s = 0; s < 128u; s++) {
            kl_hdr_t h;
            uint32_t pg, i;
            if (kl.at[k][s] < 0)
                continue;
            pg = (uint32_t)kl.at[k][s];
            i = k == 0u ? 2u : 1u;                   /* (a kit: 2 pages; a deleted kit only its first) */
            if (!kl_exists(k + 1u, s) || (k == 0u && !kl_head(pg, &h) && kl_npages(h.len) == 1u))
                i = 1u;
            while (i--)
                live[(pg + i) >> 3] |= (uint8_t)(1u << ((pg + i) & 7u));
        }
}

static void kl_scan(void)                            /* power-on: the newest entry of every slot */
{
    uint32_t pg, s;
    uint8_t live[KL_PAGES / 8u];
    memset(&kl, 0, sizeof kl);
    memset(kl.at, 0xFF, sizeof kl.at);
    if (!flash_ok)
        return;
    for (pg = 0; pg < KL_PAGES; pg++) {
        kl_hdr_t h;
        if (!kl_head(pg, &h) && kl_payload_crc(pg, h.len) == h.crc) {
            int16_t *at = &kl.at[h.kind - 1u][h.slot];
            kl_hdr_t o;
            if (*at < 0 || kl_head((uint32_t)*at, &o) || (int32_t)(h.seq - o.seq) > 0) {
                *at = (int16_t)pg;
                if (h.len)
                    kl.has[h.kind - 1u][h.slot >> 3] |= (uint8_t)(1u << (h.slot & 7u));
                else
                    kl.has[h.kind - 1u][h.slot >> 3] &= (uint8_t)~(1u << (h.slot & 7u));
            }
            if ((int32_t)(h.seq + 1u - kl.seq) > 0)
                kl.seq = h.seq + 1u;
            pg += kl_npages(h.len) - 1u;             /* (its payload's pages) */
            continue;
        }
        {
            uint8_t b[64];
            uint32_t off, all = 1, i;
            for (off = 0; off < 256u && all; off += 64u) {
                if (st_read(kl_off(pg) + off, b, 64))
                    all = 0;
                for (i = 0; i < 64u && all; i++)
                    all = b[i] == 0xFFu;
            }
            kl_set_erased(pg, (int)all);
        }
    }
    kl_live(live);
    kl.spare = 0xFF;
    for (s = 0; s < KL_NSEC && kl.spare == 0xFFu; s++) {   /* the erased sector */
        uint32_t n = 0;
        for (pg = s * 16u; pg < s * 16u + 16u; pg++)
            n += (uint32_t)kl_is_erased(pg);
        if (n == 16u)
            kl.spare = (uint8_t)s;
    }
    for (s = 0; s < KL_NSEC && kl.spare == 0xFFu; s++) {   /* none (a copy cut short): a sector with nothing live */
        uint32_t n = 0;
        for (pg = s * 16u; pg < s * 16u + 16u; pg++)
            n += (live[pg >> 3] >> (pg & 7u)) & 1u;
        if (n == 0u && !st_erase(KL_BASE + s * 4096u)) {
            for (pg = s * 16u; pg < s * 16u + 16u; pg++)
                kl_set_erased(pg, 1);
            kl.spare = (uint8_t)s;
        }
    }
    kl.ok = kl.spare != 0xFFu;
}

/* np erased pages in a row, outside the spare sector: the first, or -1 */
static int32_t kl_find(uint32_t np)
{
    uint32_t s, pg, run;
    for (s = 0; s < KL_NSEC; s++) {
        if (s == kl.spare)
            continue;
        for (pg = s * 16u, run = 0; pg < s * 16u + 16u; pg++) {
            run = kl_is_erased(pg) ? run + 1u : 0u;
            if (run == np)
                return (int32_t)(pg + 1u - np);
        }
    }
    return -1;
}
/* garbage collection: the sector with the most dead pages, its live entries into the spare, erased: 0, or -1 */
static int kl_gc(void)
{
    uint8_t live[KL_PAGES / 8u];
    uint32_t s, pg, best = 0xFF, most = 0, to = (uint32_t)kl.spare * 16u, k, sl;
    kl_live(live);
    for (s = 0; s < KL_NSEC; s++) {
        uint32_t dead = 0;
        if (s == kl.spare)
            continue;
        for (pg = s * 16u; pg < s * 16u + 16u; pg++)
            dead += !kl_is_erased(pg) && !((live[pg >> 3] >> (pg & 7u)) & 1u);
        if (dead > most) {
            most = dead;
            best = s;
        }
    }
    if (best == 0xFFu)
        return -1;
    for (k = 0; k < 2u; k++)
        for (sl = 0; sl < 128u; sl++) {
            kl_hdr_t h;
            uint32_t from, i, n;
            if (kl.at[k][sl] < 0 || (uint32_t)kl.at[k][sl] / 16u != best)
                continue;
            from = (uint32_t)kl.at[k][sl];
            if (kl_head(from, &h))
                continue;
            n = kl_npages(h.len);
            for (i = 0; i < n; i++) {                /* (as it is: its commit record comes along) */
                if (st_read(kl_off(from + i), st_buf, 256) || st_prog(kl_off(to + i), st_buf, 256))
                    return -1;
                kl_set_erased(to + i, 0);
            }
            kl.at[k][sl] = (int16_t)to;
            to += n;
        }
    if (st_erase(KL_BASE + best * 4096u))
        return -1;
    for (pg = best * 16u; pg < best * 16u + 16u; pg++)
        kl_set_erased(pg, 1);
    kl.spare = (uint8_t)best;
    return 0;
}

/* a slot's new contents (len 0: deleted): 0, -20 the library is full, else a flash error */
static int kl_write(uint32_t kind, uint32_t slot, const char *name, const uint8_t *data, uint32_t len)
{
    kl_hdr_t h;
    int32_t pg;
    uint32_t i;
    int rc = 0;
    if (!kl.ok || kind < 1u || kind > 2u || slot >= 128u || KL_HDR + len > 512u)
        return -1;
    flash_hush();
    pg = kl_find(kl_npages(len));
    if (pg < 0 && !kl_gc())
        pg = kl_find(kl_npages(len));
    if (pg < 0) {
        flash_unhush();
        return -20;
    }
    for (i = 0; i < kl_npages(len); i++)             /* (claimed: a failure below leaves them dead, not free) */
        kl_set_erased((uint32_t)pg + i, 0);
    memset(&h, 0, sizeof h);
    h.magic = KL_MAGIC;
    h.kind = (uint8_t)kind;
    h.slot = (uint8_t)slot;
    h.seq = kl.seq++;
    h.len = (uint16_t)len;
    h.crc = ~st_crc32_add(0xFFFFFFFFu, data, len);
    for (i = 0; i < 8u; i++)
        h.name[i] = name && name[i] ? name[i] : ' ';
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    for (i = 0; i < len && !rc; i += 256u) {         /* the payload, then the record */
        uint32_t off = KL_HDR + i, n = len - i > 256u ? 256u : len - i;
        rc = st_prog(kl_off((uint32_t)pg) + off, data + i, n);
    }
    if (!rc)
        rc = st_prog(kl_off((uint32_t)pg), &h, sizeof h);
    flash_unhush();
    if (rc)
        return rc;
    kl.at[kind - 1u][slot] = (int16_t)pg;
    if (len)
        kl.has[kind - 1u][slot >> 3] |= (uint8_t)(1u << (slot & 7u));
    else
        kl.has[kind - 1u][slot >> 3] &= (uint8_t)~(1u << (slot & 7u));
    return 0;
}
/* a slot's payload into buf (up to max bytes) and its name: its length, or -1 (none, damaged) */
static int32_t kl_read(uint32_t kind, uint32_t slot, uint8_t *buf, uint32_t max, char *name)
{
    kl_hdr_t h;
    uint32_t pg;
    if (!kl_exists(kind, slot))
        return -1;
    pg = (uint32_t)kl.at[kind - 1u][slot];
    if (kl_head(pg, &h) || h.len > max || st_read(kl_off(pg) + KL_HDR, buf, h.len) ||
        ~st_crc32_add(0xFFFFFFFFu, buf, h.len) != h.crc)
        return -1;
    if (name)
        memcpy(name, h.name, 8);
    return h.len;
}
/* a slot's first n payload bytes, unchecked (the lists): 0, or -1 */
static int kl_peek(uint32_t kind, uint32_t slot, uint8_t *buf, uint32_t n)
{
    kl_hdr_t h;
    uint32_t pg;
    if (!kl_exists(kind, slot))
        return -1;
    pg = (uint32_t)kl.at[kind - 1u][slot];
    return kl_head(pg, &h) || h.len < n || st_read(kl_off(pg) + KL_HDR, buf, n) ? -1 : 0;
}
static int kl_name(uint32_t kind, uint32_t slot, char *name)   /* its name (8): 0, or -1 (empty) */
{
    kl_hdr_t h;
    if (!kl_exists(kind, slot) || kl_head((uint32_t)kl.at[kind - 1u][slot], &h))
        return -1;
    memcpy(name, h.name, 8);
    return 0;
}
static uint32_t kl_count(uint32_t kind)
{
    uint32_t s, n = 0;
    for (s = 0; s < 128u; s++)
        n += (uint32_t)kl_exists(kind, s);
    return n;
}

/* ------------------------------------------------------------ sounds --- */
/* A sound: one voice's settings, the kit's parameters of that voice (par_voice) in the kit's order, Elixir's after
 * the LXR's. The three DRUM voices have the same list (D1's, D2's, D3's in step), so a drum's sound goes to any drum
 * track; the others only to their own. A sound's payload: its kind (SK_*), the voice it was taken from, the count, the
 * values. Moved to another drum, what pointed at its own voice (a velocity or LFO target, the LFO's retrigger, the
 * cross modulation's source) points at the new one. */
enum { SK_DRUM, SK_SNARE, SK_CYM, SK_HAT };
#define SOUND_MAX 96u                               /* (25b, 27a: 64 -> 96) */
static uint32_t voice_kind(uint32_t v) { return v < 3u ? SK_DRUM : v == 3u ? SK_SNARE : v == 4u ? SK_CYM : SK_HAT; }
/* a kit's (kit layout) voice v as a sound's values: their count */
static uint32_t voice_get(const uint8_t *kit, uint32_t v, uint8_t *out)
{
    uint32_t i, n = 0;
    for (i = 0; i < KIT_LEN && n < SOUND_MAX; i++)
        if (par_voice(kit_par(i)) == v)
            out[n++] = kit[i];
    return n;
}
/* .. the edit buffer's */
static uint32_t voice_get_par(uint32_t v, uint8_t *out)
{
    uint32_t i, n = 0;
    for (i = 0; i < KIT_LEN && n < SOUND_MAX; i++)
        if (par_voice(kit_par(i)) == v)
            out[n++] = par[kit_par(i)];
    return n;
}
/* a value of voice `from` as voice `to` has it (the references to its own voice moved along) */
static uint32_t voice_move(uint32_t p, uint32_t x, uint32_t from, uint32_t to)
{
    uint32_t lo = MOD_TARGET_VOICE[from][0], hi = MOD_TARGET_VOICE[from][1];
    if (from == to)
        return x;
    if ((p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6) || (p >= PAR_L2TGT1 && p < PAR_L2TGT1 + 6u))
        return x >= lo && x <= hi ? x - lo + MOD_TARGET_VOICE[to][0] : x;
    if ((p >= PAR_VOICE_LFO1 && p <= PAR_VOICE_LFO6) || (p >= PAR_RETRIGGER_LFO1 && p <= PAR_RETRIGGER_LFO6) ||
        (p >= PAR_XSRC1 && p < PAR_XSRC1 + 6u) || (p >= PAR_L2RTRG1 && p < PAR_L2RTRG1 + 6u))
        return x == from + 1u ? to + 1u : x;
    return x;
}
/* a sound's values (taken from voice `from`) into the edit buffer's voice `to`, posted */
static void voice_put_par(uint32_t to, const uint8_t *vals, uint32_t n, uint32_t from)
{
    uint32_t i, k = 0, lfo_own = 0;
    for (i = 0; i < KIT_LEN && k < n; i++) {         /* (an LFO on its own voice: its target moves along) */
        uint32_t p = kit_par(i);
        if (par_voice(p) == to && p >= PAR_VOICE_LFO1 && p <= PAR_VOICE_LFO6)
            lfo_own = vals[k] == from + 1u;
        if (par_voice(p) == to)
            k++;
    }
    for (i = 0, k = 0; i < KIT_LEN && k < n; i++) {
        uint32_t p = kit_par(i), x, m;
        if (par_voice(p) != to)
            continue;
        x = voice_move(p, vals[k++], from, to);
        if (p >= PAR_TARGET_LFO1 && p <= PAR_TARGET_LFO6 && lfo_own && x >= MOD_TARGET_VOICE[from][0] &&
            x <= MOD_TARGET_VOICE[from][1])
            x = x - MOD_TARGET_VOICE[from][0] + MOD_TARGET_VOICE[to][0];
        m = p < NUM_PARAMS ? par_max(p) : par_max_k(p);
        par[p] = (uint8_t)(x > m ? m : x);
    }
    snd_post_all();
}
/* the init kit's voice v: the browser leaves out kits' voices still as it */
static int voice_is_init(const uint8_t *kit, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t p = kit_par(i);
        if (par_voice(p) == v &&
            kit[i] != (i >= END_OF_SOUND_PARAMETERS ? init_kit_k(p) : i + 8u < sizeof INIT_KIT ? INIT_KIT[i + 8u] : 0u))
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ LOAD / SAVE --- */
/* the edit buffer (with morph: the mixture that sounds) into library kit n: 0, else kl_write's */
static int libkit_save(uint32_t n, const char *name, int morph)
{
    uint8_t k[KIT_LEN];
    uint32_t i;
    for (i = 0; i < KIT_LEN; i++) {
        uint32_t p = kit_par(i);
        k[i] = morph && morph_amt && !morph_excluded(p) ? (uint8_t)morph_mix(p, par[p], morph_par[i], morph_amt) : par[p];
    }
    return kl_write(KLK_KIT, n, name, k, KIT_LEN);
}
/* library kit n (kit layout) into k: 0, or -1 */
static int libkit_get(uint32_t n, uint8_t *k)
{
    uint32_t i;
    int32_t len = kl_read(KLK_KIT, n, k, KIT_LEN, 0);
    if (len < 0)
        return -1;
    for (i = (uint32_t)len; i < KIT_LEN; i++)        /* (a kit from before a parameter was added: its init value) */
        k[i] = (uint8_t)(i >= END_OF_SOUND_PARAMETERS ? init_kit_k(kit_par(i)) : 0u);
    kit_sanitize(k);
    return 0;
}
/* .. into the edit buffer, or as the morph target: 0, or -1 */
static int libkit_load(uint32_t n, int morph)
{
    uint8_t k[KIT_LEN];
    uint32_t i;
    if (libkit_get(n, k))
        return -1;
    for (i = 0; i < KIT_LEN; i++) {
        if (morph)
            morph_par[i] = k[i];
        else
            par[kit_par(i)] = k[i];
    }
    kit_loads++;
    snd_post_all();
    return 0;
}
/* the edit buffer's voice v into library sound n */
static int libsound_save(uint32_t n, const char *name, uint32_t v)
{
    uint8_t b[3u + SOUND_MAX];
    b[0] = (uint8_t)voice_kind(v);
    b[1] = (uint8_t)v;
    b[2] = (uint8_t)voice_get_par(v, b + 3);
    return kl_write(KLK_SOUND, n, name, b, 3u + b[2]);
}
/* library sound n into s (3 + values): its count, -1 none, -2 not this voice's kind */
static int32_t libsound_get(uint32_t n, uint32_t v, uint8_t *s)
{
    int32_t len = kl_read(KLK_SOUND, n, s, 3u + SOUND_MAX, 0);
    if (len < 3 || s[1] > 5u || (int32_t)s[2] + 3 > len)
        return -1;
    return s[0] != voice_kind(v) || voice_kind(s[1]) != s[0] ? -2 : (int32_t)s[2];
}
/* .. into the edit buffer's voice v: 0, -1, -2 */
static int libsound_load(uint32_t n, uint32_t v)
{
    uint8_t s[3u + SOUND_MAX];
    int32_t c = libsound_get(n, v, s);
    if (c < 0)
        return (int)c;
    voice_put_par(v, s + 3, (uint32_t)c, s[1]);
    return 0;
}

/* ------------------------------------------------------------ the sound browser --- */
/* 0.8: PRESETS on a track's sound pages lists the sounds it can take: the library's sounds, then the voices of the
 * library's kits, the slots' kits, the factory kits (a drum track: all three drums of each); a kit's voice still as
 * the init kit's is left out. An entry: its source (SB_*) << 14 | the voice << 11 | the number. */
enum { SB_SOUND, SB_LIB, SB_SLOT, SB_FACTORY };
#ifndef FACTORY_NKITS
#define FACTORY_NKITS 0u
#endif
#define SB_MAX (KL_SOUNDS + (KL_KITS + NUM_KITS + FACTORY_NKITS) * 3u)
static uint16_t sb_list[SB_MAX];
static uint32_t sb_n;

static void sb_add(uint32_t src, uint32_t v, uint32_t i) { sb_list[sb_n++] = (uint16_t)(src << 14 | v << 11 | i); }
static void sb_add_kit(uint32_t src, uint32_t i, const uint8_t *k, uint32_t v)
{
    uint32_t w;
    for (w = 0; w < 6u; w++)
        if (voice_kind(w) == voice_kind(v) && (w == v || voice_kind(v) == SK_DRUM) && !voice_is_init(k, w))
            sb_add(src, w, i);
}
static void sb_build(uint32_t v)                     /* the list for voice v */
{
    uint8_t k[KIT_LEN];
    uint32_t i;
    sb_n = 0;
    for (i = 0; i < KL_SOUNDS; i++) {
        uint8_t h[3];
        if (!kl_peek(KLK_SOUND, i, h, 3) && h[0] == voice_kind(v))
            sb_add(SB_SOUND, 0, i);
    }
    for (i = 0; i < KL_KITS; i++)
        if (kl_exists(KLK_KIT, i) && !libkit_get(i, k))
            sb_add_kit(SB_LIB, i, k, v);
    for (i = 0; i < NUM_KITS; i++) {
        uint32_t n0 = sb_n, j, w;
        uint8_t ks[KIT_LEN];
        kit_get(i, ks);                              /* (25-0: the slot unpacked) */
        sb_add_kit(SB_SLOT, i, ks, v);
#if ELIXIR_FACTORY_KITS
        if (i < FACTORY_NKITS) {                     /* (a slot still as the factory kit it started with: once) */
            uint8_t a[SOUND_MAX], c[SOUND_MAX];
            kit_factory_values(i, k);
            for (j = n0; j < sb_n;) {
                w = (sb_list[j] >> 11) & 7u;
                if (voice_get(ks, w, a) == voice_get(k, w, c) && !memcmp(a, c, voice_get(k, w, c)))
                    sb_list[j] = sb_list[--sb_n];
                else
                    j++;
            }
        }
#else
        (void)n0;
        (void)j;
        (void)w;
#endif
    }
#if ELIXIR_FACTORY_KITS
    for (i = 0; i < FACTORY_NKITS; i++) {
        kit_factory_values(i, k);
        sb_add_kit(SB_FACTORY, i, k, v);
    }
#endif
}
/* entry e's values (out) and the voice they were taken from: their count, or 0 */
static uint32_t sb_values(uint32_t e, uint32_t v, uint8_t *out, uint32_t *from)
{
    uint32_t src = sb_list[e] >> 14, w = (sb_list[e] >> 11) & 7u, i = sb_list[e] & 0x7FFu;
    uint8_t k[KIT_LEN];
    if (e >= sb_n)
        return 0;
    *from = w;
    switch (src) {
    case SB_SOUND: {
        uint8_t s[3u + SOUND_MAX];
        int32_t c = libsound_get(i, v, s);
        if (c < 0)
            return 0;
        *from = s[1];
        memcpy(out, s + 3, (uint32_t)c);
        return (uint32_t)c;
    }
    case SB_LIB:
        if (libkit_get(i, k))
            return 0;
        return voice_get(k, w, out);
    case SB_SLOT:
        kit_get(i, k);
        return voice_get(k, w, out);
    default:
#if ELIXIR_FACTORY_KITS
        kit_factory_values(i, k);
        return voice_get(k, w, out);
#else
        return 0;
#endif
    }
}
/* entry e's name (8) and source label ("SND 12", "LIB 3 D2", ..) */
static void sb_label(uint32_t e, uint32_t v, char *name, char *tag, uint32_t n)
{
    static const char *const SRC[4] = {"SND ", "LIB ", "KIT ", "FAC "};
    static const char *const VS[6] = {" D1", " D2", " D3", "", "", ""};
    uint32_t src = sb_list[e] >> 14, w = (sb_list[e] >> 11) & 7u, i = sb_list[e] & 0x7FFu;
    memset(name, ' ', 8);
    if (src == SB_SOUND)
        (void)kl_name(KLK_SOUND, i, name);
    else if (src == SB_LIB)
        (void)kl_name(KLK_KIT, i, name);
    else if (src == SB_SLOT)
        memcpy(name, kit_names[i], 8);
#if ELIXIR_FACTORY_KITS
    else
        kit_factory_name(i, name);
#endif
    put_str(tag, SRC[src], n);
    put_int(tag + 4, (int32_t)i + 1);
    if (src != SB_SOUND && voice_kind(v) == SK_DRUM)
        put_str(tag + str_len(tag), VS[w], n - str_len(tag));
}
