/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir (22): the lookup tables packed in flash, unpacked into RAM once at boot. Flash is what runs out; smooth
 * curves (sine, tanh, the filters' and the LFO's) differenced and Rice coded take about an eighth of their size
 * (tools/tblpack.py: the code, the list, build/gen/tblpack_data.h). Lossless: the tables are the same numbers, each
 * checked against its CRC-32. Included after every header with packed tables (elixir_tables.h, rings_tables.h,
 * plaits_tables.h). tables_unpack runs first in main.c fm1_main, before anything reads a table; the host builds
 * call it from engine_init (and their own boots). */
#include "tblpack_data.h"

typedef struct {
    void *dst;
    uint16_t n, off;                                 /* the values; the first bit's byte in tbl_bits */
    uint8_t w, code;                                 /* bytes a value; delta order << 5 | Rice k */
    uint32_t crc;                                    /* zlib CRC-32 of the unpacked bytes */
} tbl_t;
#define TBL_ENT(name, off, code, crc) {name, sizeof name / sizeof name[0], off, sizeof name[0], code, crc},
static const tbl_t TBLS[TBL_N] = {TBL_LIST(TBL_ENT)};
enum { TBL_PACKED, TBL_OK, TBL_BAD };
static uint8_t tbl_state;                            /* TBL_BAD: a table's CRC failed (a bug: the boot and CONFIG >
                                                      * ELIXIR say TABLES BAD) */

/* zlib CRC-32, 4 bits per step; running: from 0xFFFFFFFF, the CRC is ~c (storage.c's, kitlib.c's, library.c's) */
static uint32_t st_crc32_add(uint32_t c, const void *p, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = (const uint8_t *)p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}

/* c bits from *pos, each byte's low bit first */
static uint32_t tbl_get(uint32_t *pos, uint32_t c)
{
    uint32_t v = 0, i;
    for (i = 0; i < c; i++, (*pos)++)
        v |= (uint32_t)(tbl_bits[*pos >> 3] >> (*pos & 7u) & 1u) << i;
    return v;
}

/* every table against its CRC (again on opening CONFIG > ELIXIR: the tables are writable now); 0 if one fails */
static uint32_t tables_check(void)
{
    uint32_t t;
    for (t = 0; t < TBL_N; t++)
        if (~st_crc32_add(0xFFFFFFFFu, TBLS[t].dst, (uint32_t)TBLS[t].n * TBLS[t].w) != TBLS[t].crc) {
            tbl_state = TBL_BAD;
            return 0;
        }
    return 1;
}

static void tables_unpack(void)
{
    uint32_t t, i, j;
    if (tbl_state != TBL_PACKED)
        return;
    for (t = 0; t < TBL_N; t++) {
        const tbl_t *d = &TBLS[t];
        uint32_t pos = d->off * 8u, k = d->code & 31u, acc[3] = {0, 0, 0};
        for (i = 0; i < d->n; i++) {
            uint32_t q = 0, u, x;
            while (q < TBL_QMAX && tbl_get(&pos, 1))  /* u >> k in unary; at TBL_QMAX 1s, u in 32 bits */
                q++;
            u = q == TBL_QMAX ? tbl_get(&pos, 32) : q << k | tbl_get(&pos, k);
            x = (u >> 1) ^ (0u - (u & 1u));          /* (zigzag: 0, 1, 2, 3 .. -> 0, -1, 1, -2 ..) */
            for (j = d->code >> 5; j-- > 0;)         /* the deltas undone: order running sums */
                x = acc[j] += x;
            if (d->w == 1u)
                ((uint8_t *)d->dst)[i] = (uint8_t)x;
            else if (d->w == 2u)
                ((uint16_t *)d->dst)[i] = (uint16_t)x;
            else
                ((uint32_t *)d->dst)[i] = x;
        }
    }
    tbl_state = TBL_OK;
    tables_check();
}
