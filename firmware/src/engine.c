/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the engine's block, in the audio ISR: the parameters the main loop posted, MIDI in, the sequencer's
 * clock, the six voices, the FX bus and the reverb, then the outputs summed, clipped as the LXR's 16-bit DAC clips
 * them, PERF's DJ filter (perform.c) and scaled by MASTER (the FM-1's volume pot: after the DAC, as the LXR's) and the master volume (1.0). */

static int32_t master_q12 = 2048;                   /* MASTER, Q12 (master.c sets it from the pot) */
static int16_t scope[256];                          /* the last output, for the screen */
static uint32_t scope_w;
/* the output's hush (0.5.1): a flash erase holds the CPU ~17 ms at a time (storage_hw.c silences the buffer for each),
 * and the reverb and delay tails between the erases came out as bursts of noise. A save fades the output out (out_hush:
 * ~6 ms), keeps it silent while it writes and fades it back in (flash_hush). quiet_blocks: the blocks since the output
 * (before MASTER) was last above ~-60 dBFS (autosave waits for half a second of it: project.c) */
#define QUIET_LEVEL 32
static volatile uint8_t out_hush;
static volatile int32_t out_gain = 4096;            /* Q12: towards 0 (hushed) or the volume, 16 a sample */
/* 1.0: the master volume (midi.c out_vol), after the output stage and MASTER: x^2 as the pot, out_gain gliding to
 * it (no zipper from a CC) */
static inline int32_t out_vol_q12(void) { return (int32_t)((uint32_t)out_vol * out_vol * 4096u / 16129u); }
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

static void sp_defaults(void);                      /* (32: SPELLS, spells.c) */
static void wd_defaults(void);                      /* (40: WANDER, wander.c) */
static void engine_init(void)
{
    uint32_t i;
    params_init();
    voices_init();
    pl_words_scan();                                /* (0.15: Plaits' speech words, found once here: ~7 ms on the FM-1,
                                                     * which in a block, the first time SPEECH played, passed the second
                                                     * core's deadline and left the FM-1 on one core) */
    fxbus_init();
    reverb_init();
    perf_init();
    sp_defaults();
    wd_defaults();
    seq_init();
    kit_pk_init();                                  /* (25-0: the slots' packing; ui_test checks it fits) */
    {
        uint8_t k[KIT_LEN];
        kit_init_values(k);
        for (i = 0; i < NUM_KITS; i++) {
            kit_put(i, k);
            memcpy(kit_names[i], "Init    ", KIT_NAME_LEN);
        }
    }
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
    DG_START();
    sq.sync_ext = par[PAR_BPM] <= 1u;
    if (par[PAR_BPM] != last_bpm) {                 /* (the tempo from the UI, a project) */
        last_bpm = par[PAR_BPM];
        seq_tempo_changed();
    }
    snd_service();
    DG_MARK(DG_SND);
    midi_service();
    key_service();                                  /* (0.8: the notes the keys played) */
    perf_block();
    DG_MARK(DG_IN);
    seq_block();
    DG_MARK(DG_SEQ);
    voices_block();
    comp_master(mix_l, mix_r);                      /* (0.16: the master's COMPRESSOR, off at RATIO OFF) */
    dirt_master(mix_l, mix_r);                      /* (23c: the master's DIRT, off at 0) */
    for (i = 0; i < BLOCK; i++) {
        ml[i] = (int16_t)sat16(mix_l[i]);
        mr[i] = (int16_t)sat16(mix_r[i]);
    }
    perf_master(ml, mr);                            /* the DJ filter, TAPE STOP's fade */
#if UAC_STEMS
    {   /* 1.0: the USB audio's 1-2: the stereo mix as the FM-1's DAC takes it, before MASTER and the master volume
         * (the stems' level: voices.c); then the block goes out with its stems */
        int16_t *ub = uac_block();
        if (ub) {
            for (i = 0; i < BLOCK; i++, ub += UAC_NCH) {
                ub[0] = ml[i];
                ub[1] = mr[i];
            }
            uac_block_done();
        }
    }
#endif
    {
        int32_t g = out_gain, gt = out_hush ? 0 : out_vol_q12(), pk = 0;
        for (i = 0; i < BLOCK; i++) {
            int32_t l = ml[i], r = mr[i];
            if (iabs(l) > pk)
                pk = iabs(l);
            if (iabs(r) > pk)
                pk = iabs(r);
            g += g < gt - 16 ? 16 : g > gt + 16 ? -16 : gt - g;
            out[2u * i] = (((l * master_q12) >> 12) * g) >> 12;
            out[2u * i + 1u] = (((r * master_q12) >> 12) * g) >> 12;
            if ((i & 3u) == 0u)
                scope[scope_w++ & 255u] = (int16_t)((l + r) >> 1);
        }
        out_gain = g;
        quiet_blocks = pk > QUIET_LEVEL ? 0u : quiet_blocks + (quiet_blocks < 0xFFFFFFu);
    }
    LOAD_MARK(LD_MIX);                              /* (the master's: the mixer's) */
    voices_end();                                   /* (1.0 Phase 10f: core 1's D1-D3 for the next block, waited for) */
}
