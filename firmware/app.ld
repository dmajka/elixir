/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * RAMTEXT 0x01C00000.. is buffers only (2026-11-21 Phase 1; it held the RAM code before): .ram_buf (18a: zeroed
 * buffers, as the pool's; the AC79 SDK's own apps use this RAM as any other: nothing else needs it while the app
 * runs, and the update loader runs only after a reset), up to crt0's status words at 0x01C07F20..0x01C07F34
 * (written at every start; 0x01C07F34..0x01C08000 left free).
 * RAM  : .data/.bss (64 KiB): what the audio and key-scan interrupts touch, small variables
 * POOL : big zero-initialised buffers (FX lines, canvases, the parts' engine memory, the main loop's work
 *        buffers), section .pool. The same SRAM: the RAM / POOL boundary only splits the budget (1.5: 96 / 336
 *        KiB -> 72 / 360 KiB; 2026-11-21: 64 KiB / the rest less HOTCODE; Phase 1d: HOTCODE 15 -> 19.7 KiB); tools/build.py reads the lengths here
 * HOTCODE: all the code that runs from RAM, at the top of the SRAM just under the stacks (2026-11-21 Phase 1):
 *        .ram_text (code that must run while the flash is busy: the flash driver), .c1_text (the second core's
 *        waiting loop, hal/fm1_cpu1.h: an idle core 1 never fetches from the flash), .ram_hot (control code out of
 *        the flash's cache misses; ELIXIR_RAMHOT=0 leaves it empty). One window for the guard's RAM PC limit (fm1_guard.h).
 *        pi32v2's call reaches +-4 MiB (a 22-bit halfword offset): from here every function in XIP (code ends
 *        ~0x0205xxxx; .rodata follows it) and back; RAMTEXT, 4 MiB under XIP, reached none. A call out of reach
 *        fails the link. Its length is cut from POOL's; check build.py's HOTCODE line
 * STACKS with 256-byte guard bands (1.0 Phase 1: user 12 KB, was 24; deepest seen 3.9 KB, static worst ~6.6 KB);
 * NOINIT survives resets and UBOOT entry: its records (.noinit), then whole buffers zeroed at every start
 * (.noinit_buf, main.c; painted idle on the unit, 0.25.3).
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rw) : ORIGIN = 0x01C00000, LENGTH = 0x7F20
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 64K
    POOL   (rw)  : ORIGIN = 0x01C18000, LENGTH = 0x5A140
    HOTCODE (rwx): ORIGIN = 0x01C72140, LENGTH = 0x4EC0
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > HOTCODE AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > HOTCODE AT > XIP
    _c1_load = LOADADDR(.c1_text);
    /* 1.0: .ram_hot, the audio interrupt's control code (the sequencer's steps ..) out of the flash's cache misses:
     * it runs only with the flash on (the flash driver turns it off with interrupts off), so it may call XIP code */
    .ram_hot : { _rh_start = .; KEEP(*(.ram_hot*)) . = ALIGN(4); _rh_end = .; } > HOTCODE AT > XIP
    _rh_load = LOADADDR(.ram_hot);
    .ram_buf (NOLOAD) : { . = ALIGN(4); _rb_start = .; *(.ram_buf*) . = ALIGN(4); _rb_end = .; } > RAMTEXT
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit) *(.noinit.*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    .noinit_buf (NOLOAD) : { . = ALIGN(4); _nb_start = .; *(.noinit_buf*) . = ALIGN(4); _nb_end = .; } > NOINIT
    _guard0 = 0x01C77000; _ustack_lo = 0x01C77100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
