/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from the SLOOP dualcore branch (github.com/isod89/sloop-fm1, Andre Rezemini, GPL-3.0) for Elixir:
 * the second core of the AC7911: started at power-on (main.c fm1_main), proved with a self-test job, then the
 * render hands it a share of the voices each block (voices.c voices_block, through cpu1_run / cpu1_wait).
 * The HAL is fm1_cpu1.h's: one job at a time, core 0 hands it over and waits; the waiting loop lives in RAM
 * (.c1_text), so an idle core 1 never fetches from the flash and the flash writes need no parking (the erases
 * run with interrupts off: storage_hw.c). FM1_CPU1_USED=0 builds without any of it. */
enum { CPU1_OFF, CPU1_NOSTART, CPU1_FAILED, CPU1_OK };   /* not built / did not report in / self-test wrong / running */
#if FM1_CPU1_USED
static uint8_t cpu1_state = CPU1_OFF;
static uint32_t cpu1_timeouts;                           /* jobs core 1 did not finish in time */
static volatile uint32_t cpu1_test_out;

/* fm1_cpu1.h's API, with the state: the render hands its jobs through these */
static int cpu1_on(void) { return cpu1_state == CPU1_OK && fm1_c1_on; }
static int cpu1_run(void (*fn)(uint32_t), uint32_t arg) { return cpu1_state == CPU1_OK ? fm1_cpu1_run(fn, arg) : 0; }
static int cpu1_wait(void)
{
    if (fm1_cpu1_wait() == 0)
        return 0;
    cpu1_timeouts++;                                     /* fm1_cpu1_wait held the core: one core from here on */
    cpu1_state = CPU1_FAILED;
    return -1;
}

static uint32_t cpu1_lcg(uint32_t x)                     /* 64 steps of an LCG: the self-test's work */
{
    uint32_t i;
    for (i = 0; i < 64u; i++)
        x = x * 1664525u + 1013904223u;
    return x;
}

static void cpu1_selftest(uint32_t arg)                  /* runs on core 1, from the flash, as the render will */
{
    cpu1_test_out = cpu1_lcg(arg);
}

/* at power-on, before the audio and the timers (X0X: started later, core 1 never reached its entry). The
 * reserved top of RAM is not locked yet (fm1_guard_lock_top comes after): its entry word can be written.
 * The PC limits are opened for the start (it boots through the chip's ROM) and armed again after. */
static void cpu1_boot(void)
{
    fm1_guard_pc_open();
    if (fm1_cpu1_start() != 0) {
        fm1_guard_enable(FM1_GUARD_PC);
        cpu1_state = CPU1_NOSTART;
        return;
    }
    fm1_guard_enable(FM1_GUARD_PC);
    cpu1_test_out = 0;
    if (!fm1_cpu1_run(cpu1_selftest, 20261010u) || fm1_cpu1_wait() != 0 || cpu1_test_out != cpu1_lcg(20261010u)) {
        fm1_cpu1_hold();                                 /* answered, but wrong or late: one core from here on */
        cpu1_state = CPU1_FAILED;
        return;
    }
    cpu1_state = CPU1_OK;
}

static const char *cpu1_label(void)                      /* the config page: the hardware's state (short) */
{
    return cpu1_state != CPU1_OK ? "1 CORE" : "2 CORES";
}
#else
static void cpu1_boot(void) {}
static const char *cpu1_label(void) { return "1 CORE"; }
static int cpu1_on(void) { (void)0; return 0; }
static int cpu1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
static int cpu1_wait(void) { return 0; }
#endif
