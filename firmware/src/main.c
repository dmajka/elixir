/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Elixir: boot and the main loop, after Felucca's main.c. TIMER5 (10 kHz) scans the panel and polls USB and the
 * TRS MIDI input; ALNK0 renders the audio (audio.c); the main loop runs the UI (~60 frames a second), the storage
 * and the update entry. OCT- + OCT+ held 5 s: UBOOT (the FM-1's own update mode, as Felucca). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
extern uint32_t _c1_start[], _c1_end[], _c1_load[], _rb_start[], _rb_end[];

void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: the USB SIE */
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: the TRS MIDI input */
    if (++sub == 10u)
        sub = 0;
    if (in_audio) {                             /* nested in the audio ISR: the scan only (main.c of Felucca) */
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
    if (owed & 2u)
        uart_midi_poll();
    owed = 0;
}
extern void isr_timer5(void);

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}
static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    if (bootguard.c2)
        bootguard.c2 = 2;                         /* a crash with two cores: the next start is one core */
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(120, 0, 0));
    draw_text_line(0, 8, 240, &AF_M, "ELIXIR CRASH", RGB(255, 255, 255), RGB(120, 0, 0), 1);
    hexs(b, c->vec);
    draw_text_line(10, 40, 220, &AF_M, b, RGB(255, 255, 255), RGB(120, 0, 0), 0);
    hexs(b, c->pc);
    draw_text_line(10, 60, 220, &AF_M, b, RGB(255, 255, 255), RGB(120, 0, 0), 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* OCT- + OCT+ held from power-on for 5 s: the FM-1's UBOOT (updates). 0.17: only from power-on (uboot_held, after the
 * first scans); let go of either and it's gone until the next power-on. Held during play the two are SHIFT + COPY
 * (clearing a selection held them long enough to send a unit to UBOOT: it looked switched off) */
static uint8_t uboot_armed;
static void uboot_held(void)
{
    uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    uboot_armed = (fm1_in.buttons & both) == both;
}
static void uboot_watch(void)
{
    uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    if (!uboot_armed)
        return;
    if ((fm1_in.buttons & both) != both) {
        uboot_armed = 0;
        return;
    }
    if (fm1_ms > 5000u) {
        fm1_audio_stop();
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
        usb_detach();
        fm1_delay_ms(30);
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
}

/* 17b: the second core (hal/fm1_cpu1.h; 17c: it renders D1-D3, voices.c). Started at power-on, before the audio and
 * the timers (Felucca's order: started later it never reaches its entry), through the chip's ROM, so the PC limits
 * are open meanwhile. One core instead, for the whole power-on, after a crash or a hang (the watchdog) while two ran,
 * a boot that died in its first 30 s, or no answer; with REC held at power-on (cores_held); and from the first bad
 * read, missed deadline or fault (cores_poll). On one core only one model plays (rings.c model_cap) */
static uint32_t crash_at_boot;
static void cores_boot(void)
{
    uint32_t crashed = bootguard.c2 == 2u || (bootguard.c2 == 1u && (fm1_boot.p3_rst & 4u));
    crash_at_boot = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0;
    bootguard.c2 = 0;
    model_cap = 1;                                /* (one core until it answers) */
    if (crashed || bootguard.failed) {
        cores_why = CW_CRASH;
        return;
    }
    fm1_guard_pc_open();
    if (fm1_cpu1_start() == 0) {
        cores_why = CW_TWO;
        model_cap = 2;
        bootguard.c2 = 1;
    } else {
        cores_why = CW_NOSTART;
    }
    fm1_guard_enable(FM1_GUARD_PC);
}
static void cores_held(void)                      /* REC held at power-on (after the first scans): one core */
{
    if (cores_why != CW_TWO || !(fm1_in.buttons & (1u << panel.btn[B_REC])))
        return;
    fm1_irq_off();                                /* (no job in flight: the audio ISR hands one over and waits for it) */
    fm1_cpu1_hold();
    fm1_irq_on();
    bootguard.c2 = 0;
    model_cap = 1;
    cores_why = CW_HELD;
}
static void cores_poll(void)
{
    if (cores_why != CW_TWO || !*(volatile uint8_t *)&fm1_c1_gave_up)
        return;
    bootguard.c2 = 0;
    model_cap = 1;                                /* (voices.c model_limit: from the next block) */
    cores_why = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.core == 1u && fm1_crash.count != crash_at_boot ? CW_FAULT
              : fm1_c1_gave_up == 1u ? CW_BAD : CW_NOANS;
    ui_message("SECOND CORE OFF: CONFIG");
}

static void elixir_boot(void)                     /* the instrument at power-on */
{
    palette_apply();
    engine_init();
    session_boot();
}

static void fm1_main(void)
{
    uint32_t ms;
    persist_boot();
    if (flash_ok)
        ota_boot_cleanup();                       /* the staging area an update left */
    lcd_init();
    palette_apply();
    draw_splash();
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    elixir_boot();
    master_boot();
    cores_boot();
    audio_init();
    usb_start();
    uart_midi_init();
    fm1_led_dim_level(kset.leds == 1u);
    fm1_led_anim_start(kset.leds != 0u);
    fm1_timer5_start(isr_timer5, 4);              /* above ALNK0 (3): the scan nests into the render */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    for (ms = 0; ms < 1200u; ms += 10u) {         /* the splash (0.14: 1.2 s, to see the potion) */
        fm1_delay_ms(10);
        master_poll();
        if (ms == 50u) {
            cores_held();
            uboot_held();
        }
    }
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    if (cores_why == CW_HELD) {
        fm1_input_edges(0);                       /* (REC's press: not a REC) */
        ui_message("ONE CORE (REC HELD)");
    }
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        master_poll();
        batt_poll();
        cores_poll();
        uboot_watch();
        lib_service();                            /* 0.7: the library's messages (library.c) */
        ota_service();                            /* the M-UPGRADE handshake (the web installer, M-VAVE's updater) */
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (sq.running)
                seq_stop_now();
            if (flash_ok)
                ota_session();                    /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            ui.force = 1;
        }
        if (usb.uboot_req) {                      /* SysEx F0 22 24 35 7D F7 */
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, "UBOOT (USB)", T_THEME, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
        ui_input();
        settings_poll();
        autosave_poll();
        ui_leds();
        ui_draw();
        while (fm1_ms - m < 15u)                  /* ~60 frames a second at most */
            ui_input();
    }
}

/* 18a: mark the main stacks below where this runs, so LOAD can show their deepest use (audio.c stack_depth) */
static void __attribute__((noinline)) stacks_paint(void)
{
    volatile uint32_t here = 0;
    uint32_t lim = ((uint32_t)(uintptr_t)&here - 256u) & ~3u, k, *p;
    uint32_t *const LO[2] = {(uint32_t *)(uintptr_t)_ustack_lo, (uint32_t *)(uintptr_t)_sstack_lo};
    uint32_t *const TOP[2] = {(uint32_t *)(uintptr_t)_guard1, (uint32_t *)(uintptr_t)_sstack_top};
    for (k = 0; k < 2u; k++) {
        uint32_t *end = lim > (uint32_t)(uintptr_t)LO[k] && lim < (uint32_t)(uintptr_t)TOP[k] ? (uint32_t *)(uintptr_t)lim : TOP[k];
        for (p = LO[k]; p < end; p++)
            *p = STACK_MARK;
    }
    (void)here;
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_cpu1_halt();                              /* (as Felucca's): a second core still running from before this reset
                                                   * would read the RAM cleared below */
    fm1_time_init();
    fm1_reset_reason();
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
        bootguard.c2 = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {                 /* two boots in a row died early: UBOOT, so an update can fix it */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (d = _rb_start; d < _rb_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                  /* the flash driver's code that runs from RAM */
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;                                  /* the second core's waiting loop (hal/fm1_cpu1.h) */
    fm1_mailbox_clear();
    stacks_paint();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_main();
    for (;;)
        ;
}
