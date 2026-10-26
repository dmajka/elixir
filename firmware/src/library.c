/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the library (0.7). A computer reads and writes what the unit holds, over USB MIDI SysEx: backing up and
 * restoring projects, and kits in and out (an LXR-02 .SND file is a kit's name and its first 247 bytes). The page is
 * web/library.html; the browser emulator takes the same messages (elixir_web.c web_lib_in).
 *
 * A message: F0 7D 'K' 'T' pack7(cmd tag payload.. chk) F7. pack7: 8-to-7, LSB first (as ota.c's). chk: ~ the sum of
 * cmd .. payload. The answer: cmd | 0x80, the same tag, a status (LS_*), its payload, chk. One message at a time: the
 * host waits for each answer.
 *
 *   01 INFO                                   -> proto, version[8], NLOG, NUM_KITS, KIT_LEN (2), loaded, kit_loaded,
 *                                                then per project: exists, format version, length (4), seq (4),
 *                                                name[8]
 *   02 KIT_NAMES first, count (<= 32)         -> first, count, name[8] each
 *   10 READ slot, off (4), n (2, <= 256)      -> slot, off (4), n (2), the project's image there. The image is its
 *                                                slot in flash: the 256-byte header block, then the payload (the
 *                                                header's len); a backup is that, as it is
 *   20 WRITE_BEGIN slot, len (4), name[8]     -> erases a free slot (the newest copy of every project stays)
 *   21 WRITE_DATA off (4), n (2), data        -> off a multiple of 256, in order; n <= 256
 *   22 WRITE_END                              -> checks the image (magic, format, length, payload CRC as written),
 *                                                then writes its header for this slot (name: WRITE_BEGIN's, or the
 *                                                image's if that was blank) last: the restore counts only then
 *   23 WRITE_ABORT
 *   24 LOAD slot                              -> the project becomes the session (as LOAD PROJECT)
 *   25 SAVE slot, name[8]                     -> the session into the slot (4: the AUTO session)
 *   30 KIT_PUT idx, flags, name[8], n (2), data[n]
 *                                             -> the kit slot: data is the kit layout (kit.c), n up to KIT_LEN; past
 *                                                n, the LXR's part is 0 (as the LXR reads a short .SND) and Elixir's
 *                                                its init values. Flags bit 0: load it into the edit buffer too
 *   31 KIT_GET idx                            -> idx, name[8], n (2) = KIT_LEN, the kit layout
 *   32 PAT_BEGIN idx                          -> 0.8, an LXR-02 pattern in (the page reads the .PAT): the pattern
 *                                                cleared (no shuffle, kit 1, Elixir's own settings off)
 *   33 PAT_STEPS idx, track, first, n (<= 32), steps[n] (7 bytes each: the LXR's Step), timing[n]
 *   34 PAT_END idx, main[7][4] (2 each), shuffle, len[7], kit
 *                                             -> the pattern complete (the playing one: as loaded)
 *   35 SONG_PUT idx, entries[64]              -> a song (pattern numbers, SONG_END, SONG_LOOP)
 *   40 KL_LIST kind, first, count (<= 32)     -> 0.8, the kit library (kitlib.c; kind 1 kits, 2 sounds): kind, first,
 *                                                count, then per entry: exists, name[8]
 *   41 KL_GET kind, slot                      -> kind, slot, name[8], n (2), the payload (a kit: the kit layout, a
 *                                                sound: kitlib.c's)
 *   42 KL_PUT kind, slot, name[8], n (2), data
 *                                             -> the entry written; n 0 deletes it. A kit's data: the kit layout, up
 *                                                to KIT_LEN (past n: as KIT_PUT)
 *
 * A write locks the projects (proj_lock: no autosave, no SAVE) until WRITE_END, WRITE_ABORT or 10 s without data;
 * the output is hushed meanwhile (flash writes hold the processor). */

enum { LS_OK, LS_BAD, LS_ARGS, LS_BUSY, LS_FLASH, LS_CRC, LS_SIZE, LS_EMPTY, LS_ORDER };
#define LIB_PROTO 1u
#define LIB_MAX 400u                                 /* a decoded message: a whole kit (KIT_LEN) and its header */
_Static_assert(KIT_LEN + 16u <= LIB_MAX, "LIB_MAX: a kit (KL_PUT: 13 header bytes) must fit");

static void lib_send(const uint8_t *wire, uint32_t n);   /* the hook: one F0 .. F7 message to the host */

static struct {
    uint8_t in[LIB_MAX], out[LIB_MAX];
    uint8_t wire[4u + (LIB_MAX * 8u + 6u) / 7u + 2u];
    uint32_t on;                                     /* the answer's length so far */
    /* a write in progress */
    uint8_t writing, slot, phys;
    uint32_t len, next, ms;
    char name[8];
    uint8_t head[256];                               /* the image's header block, written last */
} lib POOL;

static uint32_t lib_rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void lib_put(uint32_t b)
{
    if (lib.on < LIB_MAX - 1u)
        lib.out[lib.on++] = (uint8_t)b;
}
static void lib_put32(uint32_t v)
{
    lib_put(v);
    lib_put(v >> 8);
    lib_put(v >> 16);
    lib_put(v >> 24);
}
static void lib_puts(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        lib_put(*b++);
}
static void lib_reply(void)                          /* the answer in lib.out[0 .. on), with its checksum, sent */
{
    uint32_t i, s = 0, w = 4, acc = 0, nb = 0;
    for (i = 0; i < lib.on; i++)
        s += lib.out[i];
    lib.out[lib.on++] = (uint8_t)~s;
    lib.wire[0] = 0xF0;
    lib.wire[1] = 0x7D;
    lib.wire[2] = 'K';
    lib.wire[3] = 'T';
    for (i = 0; i < lib.on; i++) {
        acc |= (uint32_t)lib.out[i] << nb;
        nb += 8u;
        while (nb >= 7u) {
            lib.wire[w++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7u;
        }
    }
    if (nb)
        lib.wire[w++] = (uint8_t)(acc & 0x7Fu);
    lib.wire[w++] = 0xF7;
    lib_send(lib.wire, w);
}
static void lib_status(uint32_t cmd, uint32_t tag, uint32_t st)
{
    lib.on = 0;
    lib_put(cmd | 0x80u);
    lib_put(tag);
    lib_put(st);
}

static void lib_write_stop(void)
{
    if (!lib.writing)
        return;
    lib.writing = 0;
    proj_lock = 0;
    flash_unhush();
}

static void lib_info(void)
{
    static const char V[] = ELIXIR_VERSION;
    uint32_t l, i;
    lib_put(LIB_PROTO);
    for (i = 0; i < 8u; i++)
        lib_put(i < sizeof V - 1u ? (uint8_t)V[i] : 0u);
    lib_put(PROJ_NLOG);
    lib_put(NUM_KITS);
    lib_put(KIT_LEN & 0xFFu);
    lib_put(KIT_LEN >> 8);
    lib_put(pj.loaded);
    lib_put(kit_loaded);
    for (l = 0; l < PROJ_NLOG; l++) {
        proj_hdr_t h;
        int ok = proj_exists(l) && !hdr_read((uint32_t)pj.phys[l], &h);
        lib_put(ok);
        lib_put(ok ? h.version : 0u);
        lib_put32(ok ? h.len : 0u);
        lib_put32(ok ? h.seq : 0u);
        lib_puts(pj.name[l], 8);
    }
}

static uint32_t lib_read(const uint8_t *a, uint32_t n)
{
    proj_hdr_t h;
    uint32_t l, off, cnt;
    if (n < 7u)
        return LS_BAD;
    l = a[0];
    off = lib_rd32(a + 1);
    cnt = a[5] | (uint32_t)a[6] << 8;
    if (l >= PROJ_NLOG || cnt > 256u)
        return LS_ARGS;
    if (!proj_exists(l) || hdr_read((uint32_t)pj.phys[l], &h))
        return LS_EMPTY;
    if (off > 256u + h.len || cnt > 256u + h.len - off)
        return LS_ARGS;
    lib_put(l);
    lib_put32(off);
    lib_put(cnt);
    lib_put(cnt >> 8);
    if (lib.on + cnt > LIB_MAX - 1u || st_read(phys_base((uint32_t)pj.phys[l]) + off, lib.out + lib.on, cnt))
        return LS_FLASH;
    lib.on += cnt;
    return LS_OK;
}

static uint32_t lib_write_begin(const uint8_t *a, uint32_t n)
{
    uint32_t ph, l2, s, len;
    int used[PROJ_NPHYS] = {0}, target = -1;
    if (n < 13u)
        return LS_BAD;
    if (!flash_ok)
        return LS_FLASH;
    lib_write_stop();
    len = lib_rd32(a + 1);
    if (a[0] >= PROJ_NLOG)
        return LS_ARGS;
    if (len < 256u + 1u || len > PROJ_SLOT)
        return LS_SIZE;
    for (l2 = 0; l2 < PROJ_NLOG; l2++)
        if (pj.phys[l2] >= 0)
            used[pj.phys[l2]] = 1;
    for (ph = 0; ph < PROJ_NPHYS && target < 0; ph++)
        if (!used[ph])
            target = (int)ph;
    if (target < 0)
        return LS_BUSY;
    proj_lock = 1;
    flash_hush();
    lib.writing = 1;
    for (s = 0; s < PROJ_SLOT; s += 0x1000u)
        if (st_erase(phys_base((uint32_t)target) + s)) {
            lib_write_stop();
            return LS_FLASH;
        }
    lib.slot = a[0];
    lib.phys = (uint8_t)target;
    lib.len = len;
    lib.next = 0;
    memcpy(lib.name, a + 5, 8);
    lib.ms = fm1_ms;
    return LS_OK;
}

static uint32_t lib_write_data(const uint8_t *a, uint32_t n)
{
    uint32_t off, cnt;
    if (!lib.writing)
        return LS_ORDER;
    if (n < 6u)
        return LS_BAD;
    off = lib_rd32(a);
    cnt = a[4] | (uint32_t)a[5] << 8;
    if (n < 6u + cnt || cnt > 256u)
        return LS_BAD;
    if (off != lib.next || (off & 255u) || off + cnt > lib.len || (cnt < 256u && off + cnt != lib.len))
        return LS_ORDER;
    lib.ms = fm1_ms;
    if (off == 0u) {                                 /* the header block: kept for WRITE_END */
        memset(lib.head, 0xFF, sizeof lib.head);
        memcpy(lib.head, a + 6, cnt);
    } else if (st_prog(phys_base(lib.phys) + off, a + 6, cnt)) {
        lib_write_stop();
        return LS_FLASH;
    }
    lib.next = off + cnt;
    return LS_OK;
}

static uint32_t lib_write_end(void)
{
    proj_hdr_t h;
    uint32_t c = 0xFFFFFFFFu, off = 0, i, blank = 1;
    if (!lib.writing)
        return LS_ORDER;
    if (lib.next != lib.len) {
        lib_write_stop();
        return LS_ORDER;
    }
    memcpy(&h, lib.head, sizeof h);
    if (h.magic != PROJ_MAGIC || h.version < 1u || h.version > PROJ_VERSION || h.hcrc != st_crc32(&h, sizeof h - 4u)) {
        lib_write_stop();
        return LS_BAD;
    }
    if (h.len != lib.len - 256u) {
        lib_write_stop();
        return LS_SIZE;
    }
    while (off < h.len) {                            /* the payload as it now is in flash */
        uint32_t k = h.len - off > 256u ? 256u : h.len - off;
        if (st_read(phys_base(lib.phys) + 256u + off, pr_buf, k)) {
            lib_write_stop();
            return LS_FLASH;
        }
        c = st_crc32_add(c, pr_buf, k);
        off += k;
    }
    if (~c != h.crc) {
        lib_write_stop();
        return LS_CRC;
    }
    for (i = 0; i < 8u; i++)
        if (lib.name[i] && lib.name[i] != ' ')
            blank = 0;
    if (!blank)
        memcpy(h.name, lib.name, 8);
    h.logical = lib.slot;
    h.seq = pj.seq[lib.slot] + 1u;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if (st_prog(phys_base(lib.phys), &h, sizeof h)) {
        lib_write_stop();
        return LS_FLASH;
    }
    lib_write_stop();
    proj_scan();
    return pj.phys[h.logical] == (int8_t)lib.phys ? LS_OK : LS_FLASH;
}

static uint32_t lib_load(uint32_t l)
{
    if (l >= PROJ_NLOG || !proj_exists(l))
        return LS_EMPTY;
    if (sq.running)
        seq_stop_now();
    if (proj_load(l))
        return LS_FLASH;
    pj.loaded = (uint8_t)l;
    pj.saved_sig = session_sig();
    if (l < PROJ_USER) {
        kset.project = (uint8_t)l;
        settings_later();
    }
    return LS_OK;
}

static uint32_t lib_save(const uint8_t *a, uint32_t n)
{
    int rc;
    if (n < 9u)
        return LS_BAD;
    if (a[0] >= PROJ_NLOG)
        return LS_ARGS;
    rc = proj_save(a[0], (const char *)a + 1);
    if (rc == -20)
        return LS_SIZE;
    if (rc)
        return LS_FLASH;
    if (a[0] < PROJ_USER) {
        pj.loaded = a[0];
        kset.project = a[0];
        settings_later();
    }
    pj.saved_sig = session_sig();
    return LS_OK;
}

static uint32_t lib_kit_put(const uint8_t *a, uint32_t n)
{
    uint32_t idx, cnt, i;
    if (n < 12u)
        return LS_BAD;
    idx = a[0];
    cnt = a[10] | (uint32_t)a[11] << 8;
    if (idx >= NUM_KITS || cnt > KIT_LEN)
        return LS_ARGS;
    if (n < 12u + cnt)
        return LS_BAD;
    kit_init_values(kits[idx]);                      /* Elixir's part: the init kit's */
    for (i = 0; i < END_OF_SOUND_PARAMETERS; i++)   /* the LXR's part: as read, 0 past it */
        kits[idx][i] = i < cnt ? a[12 + i] : 0u;
    for (; i < cnt; i++)
        kits[idx][i] = a[12 + i];
    kit_sanitize(kits[idx]);
    for (i = 0; i < KIT_NAME_LEN; i++) {             /* (a name's zeros and controls as spaces) */
        uint8_t c = a[2 + i];
        kit_names[idx][i] = (char)(c >= 32u && c < 127u ? c : ' ');
    }
    if (a[1] & 1u)
        kit_load_ram(idx);
    return LS_OK;
}

/* 0.8: LXR-02 patterns and songs */
static uint32_t lib_pat(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    pattern_t *p;
    uint32_t i, t;
    if (n < 1u)
        return LS_BAD;
    if (a[0] >= NUM_PATTERN)
        return LS_ARGS;
    p = &pat[a[0]];
    if (cmd == 0x32u) {
        seq_clear_pattern(p);
        p->shuffle = 0;
        p->kit = 0;
        p->gx = p->gy = p->gchaos = 0;
        return LS_OK;
    }
    if (cmd == 0x33u) {
        uint32_t first, cnt;
        if (n < 4u)
            return LS_BAD;
        t = a[1];
        first = a[2];
        cnt = a[3];
        if (t >= NTRACK || cnt > 32u || first + cnt > NUM_STEPS)
            return LS_ARGS;
        if (n < 4u + cnt * 8u)
            return LS_BAD;
        for (i = 0; i < cnt; i++) {
            memcpy(&p->step[t][first + i], a + 4 + i * 7u, 7);
            p->timing[t][first + i] = a[4 + cnt * 7u + i];
            p->lhi[t][first + i] = 0;
        }
        return LS_OK;
    }
    if (n < 1u + 56u + 1u + 7u + 1u)
        return LS_BAD;
    for (t = 0; t < NTRACK; t++) {
        for (i = 0; i < 4u; i++)
            p->main[t][i] = (uint16_t)(a[1 + (t * 4u + i) * 2u] | a[2 + (t * 4u + i) * 2u] << 8);
        p->len[t] = a[58 + t] >= 1u && a[58 + t] <= NUM_STEPS ? a[58 + t] : 16u;
    }
    p->shuffle = a[57] > 127u ? 127u : a[57];
    p->kit = a[65] < NUM_KITS ? a[65] : 0u;
    if (a[0] == sq.active) {                         /* the playing pattern: as loaded */
        pat_backup = *p;
        sq.gsync = (1u << NTRACK) - 1u;
    }
    return LS_OK;
}
static uint32_t lib_song(const uint8_t *a, uint32_t n)
{
    uint32_t i;
    if (n < 1u + MAX_SONG_LENGTH)
        return LS_BAD;
    if (a[0] >= NUM_SONGS)
        return LS_ARGS;
    for (i = 0; i < MAX_SONG_LENGTH; i++) {
        uint8_t v = a[1 + i];
        songs[a[0]][i] = v < NUM_PATTERN || v == SONG_END || v == SONG_LOOP ? v : (uint8_t)SONG_END;
    }
    return LS_OK;
}

/* 0.8: the kit library */
static uint32_t lib_kl_list(const uint8_t *a, uint32_t n)
{
    uint32_t k, c, max;
    if (n < 3u)
        return LS_BAD;
    max = a[0] == KLK_KIT ? KL_KITS : KL_SOUNDS;
    if (a[0] < KLK_KIT || a[0] > KLK_SOUND || a[1] >= max || a[2] > 32u)
        return LS_ARGS;
    if (!kl.ok)
        return LS_FLASH;
    c = a[2] > max - a[1] ? max - a[1] : a[2];
    lib_put(a[0]);
    lib_put(a[1]);
    lib_put(c);
    for (k = 0; k < c; k++) {
        char nm[8];
        int e = !kl_name(a[0], a[1] + k, nm);
        lib_put(e);
        if (e)
            lib_puts(nm, 8);
        else
            lib_puts("        ", 8);
    }
    return LS_OK;
}
static uint32_t lib_kl_get(const uint8_t *a, uint32_t n)
{
    uint8_t b[KIT_LEN];
    char nm[8];
    int32_t len;
    if (n < 2u)
        return LS_BAD;
    if (a[0] < KLK_KIT || a[0] > KLK_SOUND || a[1] >= 128u)
        return LS_ARGS;
    len = kl_read(a[0], a[1], b, sizeof b, nm);
    if (len < 0)
        return LS_EMPTY;
    lib_put(a[0]);
    lib_put(a[1]);
    lib_puts(nm, 8);
    lib_put((uint32_t)len);
    lib_put((uint32_t)len >> 8);
    lib_puts(b, (uint32_t)len);
    return LS_OK;
}
static uint32_t lib_kl_put(const uint8_t *a, uint32_t n)
{
    uint32_t cnt, i;
    char nm[8];
    int rc;
    if (n < 12u)
        return LS_BAD;
    cnt = a[10] | (uint32_t)a[11] << 8;
    if (n < 12u + cnt)
        return LS_BAD;
    if (a[0] < KLK_KIT || a[0] > KLK_SOUND || a[1] >= 128u ||
        (a[0] == KLK_KIT ? cnt > KIT_LEN : cnt && (cnt < 3u || cnt > 3u + SOUND_MAX || a[12] > SK_HAT)))
        return LS_ARGS;
    if (!kl.ok)
        return LS_FLASH;
    for (i = 0; i < 8u; i++)                         /* (a name's zeros and controls as spaces) */
        nm[i] = (char)(a[2 + i] >= 32u && a[2 + i] < 127u ? a[2 + i] : ' ');
    rc = kl_write(a[0], a[1], nm, a + 12, cnt);
    return rc == -20 ? LS_SIZE : rc ? LS_FLASH : LS_OK;
}

static uint32_t lib_kit_get(uint32_t idx)
{
    if (idx >= NUM_KITS)
        return LS_ARGS;
    lib_put(idx);
    lib_puts(kit_names[idx], KIT_NAME_LEN);
    lib_put(KIT_LEN & 0xFFu);
    lib_put(KIT_LEN >> 8);
    lib_puts(kits[idx], KIT_LEN);
    return LS_OK;
}

/* one message: the bytes between F0 and F7 (7D 'K' 'T' first); 0 if it isn't the library's */
static int lib_in(const uint8_t *p, uint32_t n)
{
    uint32_t d = 0, acc = 0, nb = 0, i, s = 0, cmd, tag, st;
    if (n < 3u || p[0] != 0x7Du || p[1] != 'K' || p[2] != 'T')
        return 0;
    for (i = 3; i < n; i++) {
        acc |= (uint32_t)(p[i] & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) {
            if (d < LIB_MAX)
                lib.in[d] = (uint8_t)acc;
            d++;
            acc >>= 8;
            nb -= 8u;
        }
    }
    if (d < 3u || d > LIB_MAX)
        return 1;                                    /* (garbled: no answer, the host retries) */
    for (i = 0; i + 1u < d; i++)
        s += lib.in[i];
    if ((uint8_t)~s != lib.in[d - 1u])
        return 1;
    cmd = lib.in[0];
    tag = lib.in[1];
    lib_status(cmd, tag, LS_OK);
    {
        const uint8_t *a = lib.in + 2;
        uint32_t an = d - 3u;
        switch (cmd) {
        case 0x01: lib_info(); st = LS_OK; break;
        case 0x02:
            if (an < 2u || a[0] >= NUM_KITS || a[1] > 32u) {
                st = LS_ARGS;
                break;
            }
            {
                uint32_t k, c = a[1] > NUM_KITS - a[0] ? NUM_KITS - a[0] : a[1];
                lib_put(a[0]);
                lib_put(c);
                for (k = 0; k < c; k++)
                    lib_puts(kit_names[a[0] + k], KIT_NAME_LEN);
            }
            st = LS_OK;
            break;
        case 0x10: st = lib_read(a, an); break;
        case 0x20: st = lib_write_begin(a, an); break;
        case 0x21: st = lib_write_data(a, an); break;
        case 0x22: st = lib_write_end(); break;
        case 0x23: lib_write_stop(); st = LS_OK; break;
        case 0x24: st = an ? lib_load(a[0]) : LS_BAD; break;
        case 0x25: st = lib_save(a, an); break;
        case 0x30: st = lib_kit_put(a, an); break;
        case 0x31: st = an ? lib_kit_get(a[0]) : LS_BAD; break;
        case 0x32:
        case 0x33:
        case 0x34: st = lib_pat(cmd, a, an); break;
        case 0x35: st = lib_song(a, an); break;
        case 0x40: st = lib_kl_list(a, an); break;
        case 0x41: st = lib_kl_get(a, an); break;
        case 0x42: st = lib_kl_put(a, an); break;
        default: st = LS_BAD; break;
        }
    }
    if (st != LS_OK)
        lib_status(cmd, tag, st);
    lib_reply();
    return 1;
}

static void lib_poll(void)                           /* the main loop: a write the host left gives up after 10 s */
{
    if (lib.writing && fm1_ms - lib.ms > 10000u)
        lib_write_stop();
}

#if FELUCCA_OTA
static void lib_send(const uint8_t *wire, uint32_t n) { ota_wire_send(wire, n); }
static void lib_service(void)                        /* a received SysEx frame: the library's, or left for ota.c */
{
    const uint8_t *p;
    uint32_t n;
    lib_poll();
    if (ota_frame_get(&p, &n) && lib_in(p, n))
        ota_frame_done();
}
#endif
