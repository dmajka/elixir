/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host bridge for tests/library_test.mjs: the emulator's device (kettle_web.c) with the library, on stdin and
 * stdout. A line in: a SysEx message in hex (or "run MS": the device runs MS milliseconds; "flash": the NOR's write
 * count; "pat N": pattern N's steps, timing, mains, lengths, shuffle and kit in hex; "song N": song N's 64 entries;
 * "boot": the power-on scan of the kit library again). A line out: the answer in hex ("-" for none), or the value.
 *   cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/library_bridge.c -o build/library_bridge */
#include <stdio.h>
#include <stdlib.h>
#include "../web/emu/kettle_web.c"

int main(void)
{
    static char line[8192];
    web_nor_erase();
    web_boot();
    web_render(1024);
    setvbuf(stdout, NULL, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        uint32_t n = 0, i, r;
        if (!strncmp(line, "run ", 4)) {
            uint32_t ms = (uint32_t)atoi(line + 4), k = ms * FS / 1000u;
            while (k) {
                uint32_t m = k > 1024u ? 1024u : k;
                web_render(m);
                k -= m;
            }
            printf("ok\n");
            continue;
        }
        if (!strncmp(line, "pat ", 4)) {
            const pattern_t *p = &pat[atoi(line + 4) & 31];
            uint32_t t, k;
            for (t = 0; t < NTRACK; t++) {
                for (k = 0; k < NUM_STEPS * 7u; k++)
                    printf("%02x", ((const uint8_t *)p->step[t])[k]);
                for (k = 0; k < NUM_STEPS; k++)
                    printf("%02x", p->timing[t][k]);
            }
            for (t = 0; t < NTRACK; t++)
                for (k = 0; k < 4u; k++)
                    printf("%02x%02x", p->main[t][k] & 255u, p->main[t][k] >> 8);
            printf("%02x", p->shuffle);
            for (t = 0; t < NTRACK; t++)
                printf("%02x", p->len[t]);
            printf("%02x\n", p->kit);
            continue;
        }
        if (!strncmp(line, "song ", 5)) {
            for (i = 0; i < MAX_SONG_LENGTH; i++)
                printf("%02x", songs[atoi(line + 5) & 63][i]);
            printf("\n");
            continue;
        }
        if (!strncmp(line, "boot", 4)) {
            kl_scan();
            printf("ok\n");
            continue;
        }
        if (!strncmp(line, "flash", 5)) {
            printf("%u\n", web_flash_writes_count());
            continue;
        }
        for (i = 0; line[i] && line[i + 1] && line[i] != '\n' && n < sizeof web_lib_inb; i += 2) {
            unsigned v;
            sscanf(line + i, "%2x", &v);
            web_lib_inb[n++] = (uint8_t)v;
        }
        r = web_lib_in(n);
        if (!r) {
            printf("-\n");
            continue;
        }
        for (i = 0; i < r; i++)
            printf("%02x", web_lib_outb[i]);
        printf("\n");
    }
    return 0;
}
