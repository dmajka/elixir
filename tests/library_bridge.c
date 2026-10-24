/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host bridge for tests/library_test.mjs: the emulator's device (kettle_web.c) with the library, on stdin and
 * stdout. A line in: a SysEx message in hex (or "run MS": the device runs MS milliseconds; "flash": the NOR's write
 * count). A line out: the answer in hex ("-" for none), or the value.
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
