/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Kettle: boot and the main loop, after Felucca's main.c. TIMER5 (10 kHz) scans the panel and polls USB and the
 * TRS MIDI input; ALNK0 renders the audio (audio.c); the main loop runs the UI (~60 frames a second), the storage
 * and the update entry. OCT- + OCT+ held 5 s: UBOOT (the FM-1's own update mode, as Felucca). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

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
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(120, 0, 0));
    draw_text_line(0, 8, 240, &AF_M, "KETTLE CRASH", RGB(255, 255, 255), RGB(120, 0, 0), 1);
    hexs(b, c->vec);
    draw_text_line(10, 40, 220, &AF_M, b, RGB(255, 255, 255), RGB(120, 0, 0), 0);
    hexs(b, c->pc);
    draw_text_line(10, 60, 220, &AF_M, b, RGB(255, 255, 255), RGB(120, 0, 0), 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void uboot_watch(void)                     /* OCT- + OCT+ held 5 s: the FM-1's UBOOT (updates) */
{
    static uint32_t t0;
    uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    if ((fm1_in.buttons & both) != both) {
        t0 = fm1_ms;
        return;
    }
    if (fm1_ms - t0 > 5000u) {
        fm1_audio_stop();
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
        usb_detach();
        fm1_delay_ms(30);
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
}

static void kettle_boot(void)                     /* the instrument at power-on */
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
    kettle_boot();
    master_boot();
    audio_init();
    usb_start();
    uart_midi_init();
    fm1_led_dim_level(kset.leds == 1u);
    fm1_led_anim_start(kset.leds != 0u);
    fm1_timer5_start(isr_timer5, 4);              /* above ALNK0 (3): the scan nests into the render */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    for (ms = 0; ms < 600u; ms += 10u) {          /* the splash */
        fm1_delay_ms(10);
        master_poll();
    }
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
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
        uboot_watch();
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

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_reset_reason();
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
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
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                  /* the flash driver's code that runs from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_main();
    for (;;)
        ;
}
