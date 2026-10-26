/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the remote map (0.12), for an 8-column controller (a Novation Launch Control XL). On the remote channel
 * (CONFIG: REMOTE; midi.c hands over what came in on it), a row of eight: columns 1..7 the tracks D1 D2 D3 SN CY CH OH
 * (CH and OH both the hat voice, but for their decays), column 8 the master.
 *   knobs, CC 8 + 8 x row + column:
 *     8 volume (FX dry/wet)   16 pan (reverb size)      24 filter (DJ filter)      32 resonance (FX P1)
 *     40 decay (tape time)    48 pitch (tempo)          56 drive (SRate)           64 FX send (FX P2)
 *     72 reverb send (level)  80 sample rate (shuffle)  88 LFO amount (morph)      96 random pitch (roll rate)
 *     104 macros 1..4, DRILL, JITTER, CUTS, PUMP        112 random decay (repeat length)
 *   buttons, notes 36 + 8 x row + column (on: down, off or velocity 0: up):
 *     36 mute (PLAY / STOP)   44 solo (REC)             52 select (FILL)           60 roll (REPEAT)
 *     68 DRILL (FREEZE)       76 TAPE HALF CUTS PUMP COMB SPACE CRUSH REVERSE, held   84 the same, latched
 *     92, 93 the previous, next pattern
 * Knobs (CONFIG): PICKUP (a pot acts once it passes the value: nothing jumps), ABS, REL (encoders: 64 +/- n, or
 * two's complement). Under REC a track's knob records a lock on its own track, as a panel knob on the selected one.
 * Feedback: a value changed from anywhere (the panel, a kit, MIDI) goes back as its CC, a button's state as its note
 * (velocity 127 on, 0 off), a few at a time; out over USB only. The kit's values: a lock playing isn't sent. */

#define RM_CC0 8u
#define RM_NROW 14u
#define RM_NOTE0 36u
#define RM_NNOTE 58u                                 /* (36 .. 93) */
enum { RR_VOL, RR_PAN, RR_FILTER, RR_RESO, RR_DECAY, RR_PITCH, RR_DRIVE, RR_FXS, RR_REVS, RR_SRATE, RR_LFO, RR_RNDP,
       RR_MACRO, RR_RNDD };
enum { RB_MUTE, RB_SOLO, RB_SELECT, RB_ROLL, RB_DRILL, RB_HOLD, RB_LATCH, RB_PATTERN };
static const uint16_t RM_MASTER[RM_NROW] = {PAR_FX_DRY_WET, PAR_REV_SIZE, VP_DJ, PAR_FX_P1, VP_TAPE, PAR_BPM,
                                            PAR_VOICE_DECIMATION_ALL, PAR_FX_P2, PAR_REV_LEVEL, PAR_SHUFFLE,
                                            PAR_MORPH, PAR_ROLL, VP_P2 + 3, VP_REPEAT};
static const uint8_t RM_FX[8] = {PF_TAPE, PF_HALF, PF_CUTS, PF_PUMP, PF_COMB, PF_SPACE, PF_CRUSH, PF_REVERSE};

static struct {
    uint8_t last[RM_NROW * 8u];                      /* PICKUP: the knob's last value, 0xFF none yet */
    uint8_t sent[RM_NROW * 8u];                      /* the value sent (or received) last, 0xFF none */
    uint32_t picked[4];                              /* PICKUP: the knobs that have passed the value */
    uint32_t nsent[2];                               /* the buttons' states sent: on */
    uint32_t nknown[2];                              /* .. sent at all */
    uint8_t cur;                                     /* feedback: where the scan goes on */
    uint8_t init, usb_on;                            /* (USB configured again: everything sent again) */
    uint32_t ms;
} rm;

/* the parameter of knob i (row x 8 + column) */
static uint32_t rm_par(uint32_t i)
{
    uint32_t r = i / 8u, c = i % 8u, v = track_voice(c);
    if (r == RR_MACRO)
        return c < 4u ? VP_M1 + c : VP_P2 + c - 4u;
    if (c == 7u)
        return RM_MASTER[r];
    switch (r) {
    case RR_VOL: return PAR_VOL1 + v;
    case RR_PAN: return v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u;
    case RR_FILTER: return PAR_FILTER_FREQ_1 + v;
    case RR_RESO: return PAR_RESO_1 + v;
    case RR_DECAY: return c == 6u ? PAR_VELOD6_OPEN : PAR_VELOD1 + 2u * v;
    case RR_PITCH: return PAR_COARSE1 + 2u * v;
    case RR_DRIVE: return PAR_DRIVE1 + v;
    case RR_FXS: return PAR_FX_SEND1 + v;
    case RR_REVS: return PAR_REV_SEND1 + v;
    case RR_SRATE: return PAR_VOICE_DECIMATION1 + v;
    case RR_LFO: return PAR_AMOUNT_LFO1 + v;
    case RR_RNDP: return PAR_RND_PITCH1 + v;
    default: return PAR_RND_DECAY1 + v;
    }
}
/* the range a knob's 0..127 spans (the tempo: 60..187 BPM, a step a BPM; never down to MIDI sync) */
static void rm_range(uint32_t p, uint32_t *lo, uint32_t *hi)
{
    if (p == PAR_BPM) {
        *lo = 60;
        *hi = 187;
        return;
    }
    *lo = vp_min(p);
    *hi = vp_max(p);
}
static uint32_t rm_to_cc(uint32_t p)                 /* the value as the knob's 0..127 */
{
    uint32_t lo, hi, v = vp_get(p);
    rm_range(p, &lo, &hi);
    if (hi <= lo || v <= lo)
        return 0;
    return v >= hi ? 127u : ((v - lo) * 127u + (hi - lo) / 2u) / (hi - lo);
}

static void remote_reset(void)                       /* (the channel or the mode changed) all sent again */
{
    memset(rm.last, 0xFF, sizeof rm.last);
    memset(rm.sent, 0xFF, sizeof rm.sent);
    memset(rm.picked, 0, sizeof rm.picked);
    rm.nknown[0] = rm.nknown[1] = 0;
}

static void rm_knob(uint32_t i, uint32_t v)
{
    uint32_t p = rm_par(i), lo, hi, b = 1u << (i & 31u), now;
    int32_t n;
    rm_range(p, &lo, &hi);
    if (kset.rknobs == RK_REL) {                     /* 64 +/- n, or two's complement (1 up, 127 down) */
        uint32_t st = hi - lo > 127u ? (hi - lo) / 127u : 1u;
        n = v >= 96u ? (int32_t)v - 128 : v < 32u ? (int32_t)v : (int32_t)v - 64;
        if (p == PAR_BPM) {
            lo = 20;
            hi = 255;
        }
        now = (uint32_t)clamp32((int32_t)vp_get(p) + n * (int32_t)st, (int32_t)lo, (int32_t)hi);
    } else {
        if (kset.rknobs == RK_PICKUP && !(rm.picked[i / 32u] & b)) {
            uint32_t c = rm_to_cc(p), l = rm.last[i];
            rm.last[i] = (uint8_t)v;
            if (!(v + 1u >= c && v <= c + 1u) && !(l != 0xFFu && (l < c) != (v < c)))
                return;                              /* (not there yet, nor passed it) */
            rm.picked[i / 32u] |= b;
        }
        now = lo + (v * (hi - lo) + 63u) / 127u;
    }
    vp_set(p, now);
    rm.sent[i] = (uint8_t)rm_to_cc(p);               /* (no echo of what came in) */
    if (i % 8u < 7u && i / 8u != RR_MACRO && sq.rec && par_lockable(p))
        seq_record_automation(i % 8u, p, now);       /* (its own track's lock) */
    ui.force = 1;
}

static const int8_t RM_HOLD[5] = {-1, -1, PF_FILL, PF_STUT, PF_FREEZE};   /* the master column's held effects */
static void rm_button(uint32_t n, int down)
{
    uint32_t r = n / 8u, c = n % 8u, b = 1u << c;
    if (r == RB_HOLD || r == RB_LATCH) {             /* the effects: eight a row, the master column's too */
        b = 1u << RM_FX[c];
        if (r == RB_HOLD)
            ui.rm_held = down ? ui.rm_held | b : ui.rm_held & ~b;
        else if (down)
            ui.rm_latch ^= b;
        perf_apply();
    } else if (r == RB_PATTERN) {                    /* 92, 93: the previous, next pattern */
        if (down && c < 2u)
            seq_set_next_pattern((sq.active + (c ? 1u : NUM_PATTERN - 1u)) % NUM_PATTERN);
    } else if (c == 7u) {                            /* the master column: PLAY / STOP, REC, held effects */
        if (r == RB_MUTE && down) {
            if (sq.running) {
                seq_stop_now();
            } else {
                sq.song_mode = 0;
                seq_start(1);
            }
        } else if (r == RB_SOLO && down) {
            sq.rec ^= 1u;
        } else if (RM_HOLD[r] >= 0) {
            b = 1u << RM_HOLD[r];
            ui.rm_held = down ? ui.rm_held | b : ui.rm_held & ~b;
            if (down && RM_HOLD[r] == PF_FREEZE && fx.type != FX_DELAY)
                ui_message("FREEZE: THE FX BUS IS NOT A DELAY");
            perf_apply();
        }
    } else {
        switch (r) {
        case RB_MUTE: if (down) sq.muted ^= (uint8_t)b; break;
        case RB_SOLO: if (down) sq.solo ^= (uint8_t)b; break;
        case RB_SELECT: if (down) track_select(c); break;
        case RB_ROLL:
            if (down)
                seq_set_roll(c, 1);
            else if (!(ui.roll_latch & b))
                seq_set_roll(c, 0);
            break;
        default:                                     /* DRILL */
            ui.rm_drill = (uint8_t)(down ? ui.rm_drill | b : ui.rm_drill & ~b);
            perf_apply();
            break;
        }
    }
    ui.force = 1;
}

/* a button's state, for its light */
static int rm_state(uint32_t n)
{
    uint32_t r = n / 8u, c = n % 8u;
    if (r == RB_PATTERN)
        return 0;
    if (c == 7u && r < RB_HOLD) {
        if (r == RB_MUTE)
            return sq.running != 0;
        if (r == RB_SOLO)
            return sq.rec != 0;
        return RM_HOLD[r] >= 0 && ((pf.on >> RM_HOLD[r]) & 1u);
    }
    switch (r) {
    case RB_MUTE: return (sq.muted >> c) & 1u;
    case RB_SOLO: return (sq.solo >> c) & 1u;
    case RB_SELECT: return ui.track == c;
    case RB_ROLL: return (sq.roll >> c) & 1u;
    case RB_DRILL: return (sq.drill >> c) & 1u;
    case RB_HOLD: return (pf.on >> RM_FX[c]) & 1u;
    default: return (ui.rm_latch >> RM_FX[c]) & 1u;
    }
}

/* the main loop: what the remote sent, then (every few ms) a pass of feedback, a few messages at most */
static void remote_service(void)
{
    uint32_t i, n;
    if (!rm.init || rm.usb_on != (usb.config != 0)) {
        remote_reset();
        rm.init = 1;
        rm.usb_on = usb.config != 0;
    }
    while (rin_r != rin_w) {
        uint32_t pkt = rin_q[rin_r % RQ], st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 127u, d2 = (pkt >> 24) & 127u;
        RING_PUBLISH();
        rin_r++;
        if (st == 0xB0u && d1 >= RM_CC0 && d1 < RM_CC0 + RM_NROW * 8u)
            rm_knob(d1 - RM_CC0, d2);
        else if (st != 0xB0u && d1 >= RM_NOTE0 && d1 < RM_NOTE0 + RM_NNOTE)
            rm_button(d1 - RM_NOTE0, st == 0x90u && d2);
    }
    if (remote_ch > 15u || fm1_ms - rm.ms < 4u)
        return;
    rm.ms = fm1_ms;
    for (i = n = 0; i < RM_NROW * 8u + RM_NNOTE && n < 12u; i++) {
        uint32_t k = (rm.cur + i) % (RM_NROW * 8u + RM_NNOTE);
        if (k < RM_NROW * 8u) {
            uint32_t v = rm_to_cc(rm_par(k));
            if (v == rm.sent[k])
                continue;
            if (!remote_out_post(0xB0u | remote_ch, RM_CC0 + k, v))
                break;
            if (rm.sent[k] != 0xFFu)                 /* (changed elsewhere: PICKUP waits for the pot again) */
                rm.picked[k / 32u] &= ~(1u << (k & 31u));
            rm.sent[k] = (uint8_t)v;
        } else {
            uint32_t j = k - RM_NROW * 8u, b = 1u << (j & 31u), on = rm_state(j) ? b : 0u;
            if ((rm.nknown[j / 32u] & b) && (rm.nsent[j / 32u] & b) == on)
                continue;
            if (!remote_out_post(0x90u | remote_ch, RM_NOTE0 + j, on ? 127u : 0u))
                break;
            rm.nknown[j / 32u] |= b;
            rm.nsent[j / 32u] = (rm.nsent[j / 32u] & ~b) | on;
        }
        n++;
    }
    rm.cur = (uint8_t)((rm.cur + i) % (RM_NROW * 8u + RM_NNOTE));
}
