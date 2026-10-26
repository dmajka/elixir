/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle: MIDI. In: USB and the TRS jack (usb.c, midi_uart.c fill midi_in_q), handled in the audio ISR at a
 * block start. Out: USB (the FM-1 has no MIDI out jack): the sequencer's notes, MIDI clock, start / stop and
 * program changes. As the LXR (Core/Src/Soundengine/MIDI/MidiParser.c, sequencer.c; Copyright (C) 2012-2026
 * Erica Synths, GPL-3.0-only):
 *   - each track has a channel (MIDI page) and a note: ANY plays the voice at the received note, a note plays it
 *     (at the default pitch) on that note only; the global channel plays the selected track;
 *   - CC n sets parameter n - 1 (the LXR's CC map), NRPN n (CC 99 / 98, data entry CC 6) parameter n + 128 (Kettle's
 *     own after the LXR's: NRPN 174..189, sound.c), CC 0 loads a kit; the channel: the parameter's voice's, the
 *     global one, or (no voice) any of them; Out = FX BUS (the LXR's) sets the voice's FX SEND to 127;
 *   - program change 0..31 on the global channel queues a pattern;
 *   - MIDI clock / start / continue / stop drive the sequencer when BPM is MIDI;
 *   - the filters (RX / TX: N notes, R realtime, C CC, P program change) and the soft thru (TRS in -> USB out:
 *     M2U, M2A; the LXR's routes to its DIN out have no jack here);
 *   - 0.12: the remote channel (CONFIG: REMOTE): its CCs and notes go to the remote map (remote.c, the main loop)
 *     and nowhere else, past the filters; the remote's feedback goes out from here. */

enum { MF_NOTE = 1, MF_RT = 2, MF_CC = 4, MF_PC = 8 };
enum { MR_OFF, MR_U2M, MR_M2M, MR_A2M, MR_M2U, MR_M2A };

static uint8_t midi_note_on_of[NTRACK];             /* the note a track's MIDI out is holding, 0 = none */
static uint8_t midi_note_mask;
static uint16_t nrpn_num;
static volatile uint8_t midi_seen;                  /* MIDI arrived (the UI's indicator) */
static volatile uint16_t midi_cc_par;               /* the last parameter MIDI changed + 1 (the UI shows it) */

/* 0.12: the remote: its channel (0..15, 0xFF off; the settings set it), what came in on it for the main loop
 * (remote.c), what the main loop sends back (its feedback) */
#define RQ 64u
static volatile uint8_t remote_ch = 15u;
static volatile uint32_t rin_q[RQ], rout_q[RQ];
static volatile uint8_t rin_w, rin_r, rout_w, rout_r;
static void remote_in_post(uint32_t pkt)             /* (the audio ISR) */
{
    if ((uint8_t)(rin_w - rin_r) >= RQ)
        return;
    rin_q[rin_w % RQ] = pkt;
    RING_PUBLISH();
    rin_w++;
}
static int remote_out_post(uint32_t st, uint32_t d1, uint32_t d2)   /* (the main loop) 0: the queue is full */
{
    if ((uint8_t)(rout_w - rout_r) >= RQ)
        return 0;
    rout_q[rout_w % RQ] = (st >> 4) | st << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24;
    RING_PUBLISH();
    rout_w++;
    return 1;
}

/* channels: the settings hold 1..16 (DTYPE_1B16) */
static uint32_t midi_track_ch(uint32_t t)
{
    return ((t < 6u ? par[PAR_MIDI_CHAN_1 + t] : par[PAR_MIDI_CHAN_7]) - 1u) & 15u;
}
static uint32_t midi_global_ch(void) { return (par[PAR_MIDI_CHAN_GLOBAL] - 1u) & 15u; }

/* ---------------------------------------------------------------- out --- */
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >= 0xF0u ? 0xFu : st >> 4;
    midi_out_event(cin | st << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24);
}
static int midi_tx(uint32_t f) { return (par[PAR_MIDI_FILT_TX] & f) != 0u; }

static void midi_out_note(uint32_t t, uint32_t note, uint32_t vel)   /* seq_sendMidiNoteOn */
{
    uint32_t n = par[PAR_MIDI_NOTE1 + t] ? par[PAR_MIDI_NOTE1 + t] : note;
    if (!midi_tx(MF_NOTE) || t >= NTRACK || !vel)
        return;
    midi_send(0x90u | midi_track_ch(t), n, vel);
    midi_note_on_of[t] = (uint8_t)n;
    midi_note_mask |= (uint8_t)(1u << t);
}
static void midi_out_note_off(uint32_t t)            /* seq_midiNoteOff (sent even if TX N went off meanwhile) */
{
    if (t >= NTRACK || !(midi_note_mask & (1u << t)))
        return;
    midi_note_mask &= (uint8_t)~(1u << t);
    midi_send(0x80u | midi_track_ch(t), midi_note_on_of[t], 0);
}
static void midi_out_rt(uint32_t st)
{
    if (midi_tx(MF_RT))
        midi_send(st, 0, 0);
}
static void midi_out_pc(uint32_t pattern)
{
    if (midi_tx(MF_PC))
        midi_send(0xC0u | midi_global_ch(), pattern, 0);
}

/* ----------------------------------------------------------------- in --- */
/* midiParser_cc2voiceNr: the voice (1..6) a CC's parameter belongs to; 0 none, 7 all */
static uint32_t cc_voice(uint32_t cc)
{
    uint32_t p = cc ? cc - 1u : 0u;
    switch (cc) {
    case 0: case 1: case 6: case 98: case 99:
        return 0;
    case 115:
        return 7;
    default:
        break;
    }
    return par_voice(p) + 1u;
}

static int ui_step_note_edit(uint32_t note);        /* (ui.c) the step page takes a played note */

/* midiParser_noteOn */
static void midi_note_on(uint32_t t, uint32_t note, uint32_t vel, int rec)
{
    if (ui_step_note_edit(note))
        return;
    if (seq_mutes() & (1u << t))
        return;
    if (par[PAR_MIDI_NOTE1 + t]) {
        if (note != par[PAR_MIDI_NOTE1 + t])
            return;
        note = SEQ_DEFAULT_NOTE;
    }
    midi_out_note_off(t);
    voice_trigger(t, vel, note);
    if (rec) {
        seq_add_note(t, vel, note);
        midi_out_note(t, note, vel);
    }
}

/* a parameter from MIDI (a CC or an NRPN): the edit buffer and the sound at once, recorded when recording */
static void midi_param(uint32_t p, uint32_t v)
{
    if (p >= NPAR || p == 0u)
        return;
    if (p == PAR_NRPN_DATA_ENTRY_COARSE || p == PAR_NRPN_FINE || p == PAR_NRPN_COARSE || p == PAR_BANK_CHANGE)
        return;
    if (p == PAR_MORPH) {                            /* 0.12: as the panel sets them (par[] alone did nothing) */
        kit_set_morph(v * 255u / 127u);
        return;
    }
    if (p == PAR_ROLL) {
        sq.roll_rate = (uint8_t)(v > 13u ? 13u : v);
        par[PAR_ROLL] = sq.roll_rate;
        sq.roll_free = 0;
        return;
    }
    par[p] = (uint8_t)v;
    if (p >= PAR_AUDIO_OUT1 && p <= PAR_AUDIO_OUT6 && v >= 6u) {   /* the LXR's Out = FX BUS: FX SEND 127 */
        out_migrate(&par[PAR_AUDIO_OUT1], &par[PAR_FX_SEND1]);
        snd_refresh(PAR_FX_SEND1 + p - PAR_AUDIO_OUT1);
    }
    if (par_sound(p)) {
        snd_refresh(p);
        if (par_lockable(p))
            seq_record_automation(ui_track(), p, v);
    }
    midi_cc_par = (uint16_t)(p + 1u);
}

static void midi_cc(uint32_t ch, uint32_t cc, uint32_t v)
{
    uint32_t vo = cc_voice(cc), t, ok = 0;
    if (!(par[PAR_MIDI_FILT_RX] & MF_CC))
        return;
    if (vo == 0u || vo >= 7u) {                      /* any channel the LXR listens on */
        ok = ch == midi_global_ch();
        for (t = 0; t < NTRACK; t++)
            ok |= ch == midi_track_ch(t);
    } else {
        ok = ch == midi_track_ch(vo - 1u) || ch == midi_global_ch();
    }
    if (!ok)
        return;
    switch (cc) {
    case 0:                                          /* bank select: the kit */
        if (v < NUM_KITS)
            kit_load_ram(v);
        return;
    case 1:
        return;
    case 6:                                          /* NRPN data entry */
        midi_param(nrpn_num + 128u, v);
        return;
    case 98:
        nrpn_num = (uint16_t)((nrpn_num & ~0x7Fu) | v);
        return;
    case 99:
        nrpn_num = (uint16_t)((nrpn_num & 0x7Fu) | v << 7);
        return;
    default:
        midi_param(cc - 1u, v);
        return;
    }
}

static void midi_message(uint32_t pkt, uint32_t src)   /* midiParser_parseMidiMessage */
{
    uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    uint32_t rx = par[PAR_MIDI_FILT_RX], route = par[PAR_MIDI_ROUTING];
    midi_seen = 1;
    if (src == 2u && (route == MR_M2U || route == MR_M2A))   /* soft thru: the TRS input to USB */
        midi_out_event(pkt);
    if (st < 0xF0u && (st & 15u) == remote_ch && ((st & 0xE0u) == 0x80u || (st & 0xF0u) == 0xB0u)) {
        remote_in_post(pkt);                         /* 0.12: the remote's notes and CCs */
        return;
    }
    if (st >= 0xF8u) {
        if (!(rx & MF_RT))
            return;
        if (st == 0xF8u)
            seq_ext_pulse();
        else
            seq_ext_transport(st);
        return;
    }
    switch (st & 0xF0u) {
    case 0x90u:
    case 0x80u: {
        uint32_t ch = st & 15u, t, q = 0xFFu;
        if (!(rx & MF_NOTE) || (st & 0xF0u) == 0x80u || d2 == 0u)
            return;                                  /* (drum voices ignore note off) */
        if (ch == midi_global_ch()) {
            q = ui_track();
            midi_note_on(q, d1, d2, 1);
        }
        for (t = 0; t < NTRACK; t++)
            if (t != q && midi_track_ch(t) == ch)
                midi_note_on(t, d1, d2, 1);
        return;
    }
    case 0xC0u:
        if ((rx & MF_PC) && (st & 15u) == midi_global_ch())
            seq_set_next_pattern(d1);
        return;
    case 0xB0u:
        midi_cc(st & 15u, d1, d2);
        return;
    default:
        return;
    }
}

/* 0.8: the notes the panel plays (KEYS; SHIFT + 1..7 audition), posted by the main loop: the audio ISR plays them at
 * its next block as a MIDI note in would be (MIDI out, recorded under REC; an audition is not recorded) */
#define KQ 8u
#define KQ_NOREC 0x1000000u
static volatile uint32_t key_q[KQ];
static volatile uint8_t key_w, key_r;
static void key_note_post(uint32_t t, uint32_t note, uint32_t vel, int rec)
{
    if ((uint8_t)(key_w - key_r) >= KQ || t >= NTRACK)
        return;
    key_q[key_w % KQ] = t | (note & 127u) << 8 | (vel & 127u) << 16 | (rec ? 0u : KQ_NOREC);
    RING_PUBLISH();
    key_w++;
}
static void key_service(void)
{
    while (key_r != key_w) {
        uint32_t e = key_q[key_r % KQ], t = e & 7u, note = (e >> 8) & 127u, vel = (e >> 16) & 127u;
        RING_PUBLISH();
        key_r++;
        midi_out_note_off(t);
        voice_trigger(t, vel ? vel : 1u, note);
        if (!(e & KQ_NOREC))
            seq_add_note(t, vel ? vel : 1u, note);
        midi_out_note(t, note, vel ? vel : 1u);
    }
}

/* the audio ISR, at a block start: what came in. A broken stream (the ring overflowed): dropped */
static void midi_service(void)
{
    if (midi_in_overflow) {
        mi_r = mi_w;
        midi_in_overflow = 0;
        return;
    }
    while (rout_r != rout_w) {                       /* 0.12: the remote's feedback */
        uint32_t pkt = rout_q[rout_r % RQ];
        RING_PUBLISH();
        rout_r++;
        midi_out_event(pkt);
    }
    while (mi_r != mi_w) {
        uint32_t i = mi_r % MQ, pkt, src;
        RING_PUBLISH();
        pkt = midi_in_q[i];
        src = midi_in_source[i];
        RING_PUBLISH();
        mi_r++;
        midi_message(pkt, src);
    }
}
