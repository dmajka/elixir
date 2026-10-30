/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the Erica Synths LXR-02 drum machine's firmware (its sound engine, sequencer and MIDI, GPL-3.0-only,
 * Copyright (C) 2012-2026 Erica Synths) ported to the M-VAVE FM-1, on Felucca's FM-1 hardware layer (GPL-3.0-only,
 * Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments). An unofficial port: not affiliated with, endorsed by
 * or supported by Erica Synths or M-VAVE.
 * One compilation unit (the HAL is header-only); each file uses what the files above it define. */
#ifndef ELIXIR_VERSION
#define ELIXIR_VERSION "v0.18"
#endif
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 1
#define FELUCCA_CPU2 1                   /* 17b: the second core, started at power-on (main.c cores_boot, audio.c) */
#define FM1_CPU1_USED 1
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_900"            /* the package identity the updaters check (build.py) */
#endif
#define FELUCCA_VERSION ELIXIR_VERSION

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_cpu1.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define POOL __attribute__((section(".pool")))
#define RAMBUF __attribute__((section(".ram_buf")))   /* 18a: zeroed, in RAMTEXT after the RAM code (app.ld) */
static volatile uint32_t fm1_ms;        /* milliseconds since boot (TIMER5) */
#define BOOTGUARD_MAGIC 0x42475245u     /* (0.15: c2 added) */
/* c2 (main.c cores_boot): 1 = two cores run, 2 = and then a crash: the next start is one core */
struct { uint32_t magic, failed, pending, c2; } bootguard __attribute__((section(".noinit")));
static uint32_t load_clock(void);       /* 17a: audio.c; dsp.c LOAD_MARK */
#define LOAD_CLOCK load_clock
#define STACK_MARK 0x57AC57ACu          /* 18a: the stacks' unused words (main.c stacks_paint, audio.c) */

/* ------------------------------------------------- tables, display --- */
#include "elixir_tables.h"               /* generated (tools/gen_tables.py) */
#include "lxr_tables.h"
#include "lxr_params.h"
#include "grids_map.h"
#include "libc.c"
#include "lcd.c"
#include "gfx.c"

/* ------------------------------------------------------------- MIDI --- */
#include "usb.c"
#include "midi_uart.c"

/* ------------------------------------------------------------ sound --- */
#include "dsp.c"
#include "voices.c"
#include "fxbus.c"
#include "reverb.c"
#include "sound.c"
#include "kit.c"
static uint32_t ui_track(void);
static uint32_t ui_pattern(void);
#include "seq.c"
#include "perform.c"
#include "params.c"
#include "midi.c"
#include "engine.c"
#include "audio.c"

/* ---------------------------------------------- storage, the panel --- */
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#include "kitlib.c"
#include "project.c"
#include "ota.c"
#include "library.c"
#include "ota_hw.c"
#include "master.c"
#include "panel.c"
#include "ui.c"
#include "remote.c"
#include "ui_draw.c"
#include "main.c"
