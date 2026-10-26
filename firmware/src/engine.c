/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the engine's block, in the audio ISR: the parameters the main loop posted, MIDI in, the sequencer's
 * clock, the six voices, the FX bus and the reverb, then the outputs summed, clipped as the LXR's 16-bit DAC clips
 * them, PERF's DJ filter (perform.c) and scaled by MASTER (the FM-1's volume pot: after the DAC, as the LXR's). */

static int32_t master_q12 = 2048;                   /* MASTER, Q12 (master.c sets it from the pot) */
static int16_t scope[256];                          /* the last output, for the screen */
static uint32_t scope_w;
/* the output's hush (0.5.1): a flash erase holds the CPU ~17 ms at a time (storage_hw.c silences the buffer for each),
 * and the reverb and delay tails between the erases came out as bursts of noise. A save fades the output out (out_hush:
 * ~6 ms), keeps it silent while it writes and fades it back in (flash_hush). quiet_blocks: the blocks since the output
 * (before MASTER) was last above ~-60 dBFS (autosave waits for half a second of it: project.c) */
#define QUIET_LEVEL 32
static volatile uint8_t out_hush;
static volatile int32_t out_gain = 4096;            /* Q12: towards 0 (hushed) or 4096, 16 a sample */
static volatile uint32_t quiet_blocks;

static void engine_globals_default(void)            /* the LXR's settings as a new project has them */
{
    uint32_t t;
    par[PAR_BPM] = 120;
    for (t = 0; t < 6u; t++)
        par[PAR_MIDI_CHAN_1 + t] = (uint8_t)(t + 1u);
    par[PAR_MIDI_CHAN_7] = 7;
    par[PAR_MIDI_CHAN_GLOBAL] = 10;
    for (t = 0; t < NTRACK; t++)
        par[PAR_MIDI_NOTE1 + t] = 0;
    par[PAR_MIDI_FILT_TX] = 15;
    par[PAR_MIDI_FILT_RX] = 15;
    par[PAR_MIDI_ROUTING] = MR_OFF;
    par[PAR_FOLLOW] = 1;
    par[PAR_PATTERN_MODE] = 0;
    par[PAR_SHUFFLE] = 0;
    par[PAR_ROLL] = 5;
    par[PAR_EUKLID_LENGTH] = 16;
    par[PAR_EUKLID_STEPS] = 4;
    par[PAR_STEP_PROB] = 100;
}

static void engine_init(void)
{
    uint32_t i;
    params_init();
    voices_init();
    fxbus_init();
    reverb_init();
    perf_init();
    seq_init();
    for (i = 0; i < NUM_KITS; i++) {
        kit_init_values(kits[i]);
        memcpy(kit_names[i], "Init    ", KIT_NAME_LEN);
    }
#if ELIXIR_FACTORY_KITS
    kit_factory_fill();
#endif
    kit_init_values(morph_par);
    engine_globals_default();
    kit_load_ram(0);
    snd_service();                                  /* (the voices from the kit before the first block) */
    seq_tempo_changed();
}

/* one BLOCK: out gets BLOCK stereo frames, Q15 (L R L R ..) */
static void engine_block(int32_t *out)
{
    static uint8_t last_bpm;
    static int16_t ml[BLOCK], mr[BLOCK];
    uint32_t i;
    sq.sync_ext = par[PAR_BPM] <= 1u;
    if (par[PAR_BPM] != last_bpm) {                 /* (the tempo from the UI, a project) */
        last_bpm = par[PAR_BPM];
        seq_tempo_changed();
    }
    snd_service();
    midi_service();
    key_service();                                  /* (0.8: the notes the keys played) */
    perf_block();
    seq_block();
    voices_block();
    for (i = 0; i < BLOCK; i++) {
        ml[i] = (int16_t)sat16(mix_l[i]);
        mr[i] = (int16_t)sat16(mix_r[i]);
    }
    perf_master(ml, mr);                            /* the DJ filter, TAPE STOP's fade */
    {
        int32_t g = out_gain, gt = out_hush ? 0 : 4096, pk = 0;
        for (i = 0; i < BLOCK; i++) {
            int32_t l = ml[i], r = mr[i];
            if (iabs(l) > pk)
                pk = iabs(l);
            if (iabs(r) > pk)
                pk = iabs(r);
            g += g < gt ? 16 : g > gt ? -16 : 0;
            out[2u * i] = (((l * master_q12) >> 12) * g) >> 12;
            out[2u * i + 1u] = (((r * master_q12) >> 12) * g) >> 12;
            if ((i & 3u) == 0u)
                scope[scope_w++ & 255u] = (int16_t)((l + r) >> 1);
        }
        out_gain = g;
        quiet_blocks = pk > QUIET_LEVEL ? 0u : quiet_blocks + (quiet_blocks < 0xFFFFFFu);
    }
}
