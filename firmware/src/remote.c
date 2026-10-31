/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: the remote map (0.12; 0.16: two channels, every number used once), for an 8-column controller (a Novation
 * Launch Control XL). CONFIG: REMOTE sets the two channels; midi.c hands over the notes and CCs that come in on
 * them. A button is a CC or a note of its number, as the controller sends it (a CC above 0 or a note on: down; 0 or
 * a note off: up); a knob is a CC.
 * TRACKS (channel 15): rows of eight, columns 1..7 the tracks D1 D2 D3 SN CY CH OH (CH and OH both the hat voice,
 * but for their decays), column 8 the selected track.
 *   knobs, CC 8 + 8 x row + column:
 *     8 volume    16 pan    24 filter    32 resonance    40 decay    48 pitch    56 drive    64 FX send
 *     72 reverb send    80 sample rate    88 LFO amount    96 random pitch
 *   buttons, 104 + 8 x row + column: 104 mute    112 solo
 * GLOBAL (channel 16): the performance, the FX and the rest.
 *   knobs, CC 8 + 8 x row + column:
 *     8   tempo, shuffle, morph, SRate, DJ filter, roll rate, repeat length, tape time
 *     16  FX dry/wet, P1, P2, P3, P4, bus dirt, echo > reverb, saturate
 *     24  reverb level, size, damping, master dirt, density, ghost velocity, build, -
 *     32  macros 1..4, DRILL, JITTER, CUTS, PUMP (PERFORM 2's settings)
 *     40  COMB note, COMB feedback, CRUSH, REVERSE length, global LFO 1, 2, 3 depth, -
 *   buttons, 48 + 8 x row + column:
 *     48  PLAY / STOP, REC, FILL, REPEAT, FREEZE, THROW REV, THROW FX, FB PUSH (held)
 *     56  TAPE STOP, LP sweep, HP sweep, REVERSE (held), the previous pattern, the next, -, -
 *     64  select D1 .. OH    72 roll D1 .. OH, the selected track    80 DRILL, the same
 *     88  HALF JITTER CUTS SHORT PUMP COMB SPACE CRUSH, held    96 the same, latched
 *     104 DENSITY: D1 .. OH left out (again: back in), all back in
 * Knobs (CONFIG): PICKUP (a pot acts once it passes the value: nothing jumps), ABS, REL (encoders: 64 +/- n, or
 * two's complement). Under REC a track's knob records a lock on its own track, as a panel knob on the selected one.
 * Feedback: a value changed from anywhere (the panel, a kit, MIDI) goes back as its CC, a button's state (127 on, 0
 * off) as what that button last sent (a CC or a note; both until it has sent one), a few at a time; out over USB
 * only. The kit's values: a lock playing isn't sent. */

#define RM_CC0 8u
#define RM_NTK 96u                                   /* TRACKS' knobs: CC 8 .. 103 */
#define RM_TB0 104u                                  /* .. its buttons: 104 .. 119 */
#define RM_NTB 16u
#define RM_NGK 40u                                   /* GLOBAL's knobs: CC 8 .. 47 */
#define RM_GB0 48u                                   /* .. its buttons: 48 .. 111 */
#define RM_NGB 64u
#define RM_NK (RM_NTK + RM_NGK)                      /* the knobs: TRACKS' then GLOBAL's */
#define RM_NB (RM_NTB + RM_NGB)                      /* the buttons: the same */
enum { RR_VOL, RR_PAN, RR_FILTER, RR_RESO, RR_DECAY, RR_PITCH, RR_DRIVE, RR_FXS, RR_REVS, RR_SRATE, RR_LFO, RR_RNDP };
enum { RB_MUTE, RB_SOLO };                           /* TRACKS' button rows */
enum { RG_PERF, RG_PERF2, RG_SELECT, RG_ROLL, RG_DRILL, RG_HOLD, RG_LATCH, RG_DENS };   /* GLOBAL's */
static const uint16_t RM_GK[RM_NGK] = {
    PAR_BPM, PAR_SHUFFLE, PAR_MORPH, PAR_VOICE_DECIMATION_ALL, VP_DJ, PAR_ROLL, VP_REPEAT, VP_TAPE,
    PAR_FX_DRY_WET, PAR_FX_P1, PAR_FX_P2, PAR_FX_P3, PAR_FX_P4, PAR_FXDIRT, PAR_ECHOREV, PAR_DSAT,
    PAR_REV_LEVEL, PAR_REV_SIZE, PAR_REV_DAMP, PAR_MDIRT_AMT, VP_DENS, VP_GHOST, VP_BUILD, VP_DVARY,
    VP_M1, VP_M1 + 1, VP_M1 + 2, VP_M1 + 3, VP_P2, VP_P2 + 1, VP_P2 + 2, VP_P2 + 3,
    VP_P2 + 4, VP_P2 + 5, VP_P2 + 6, VP_P2 + 7, GLV(0, 1), GLV(1, 1), GLV(2, 1), VP_NONE};
#define RM_PLAY 0xFEu
#define RM_REC 0xFDu
#define RM_PREV 0xFCu
#define RM_NEXT 0xFBu
#define RM_NONE 0xFFu
static const uint8_t RM_PF[16] = {RM_PLAY, RM_REC, PF_FILL, PF_STUT, PF_FREEZE, PF_THROW_R, PF_THROW_F, PF_PUSH,
                                  PF_TAPE, PF_SWEEP_LP, PF_SWEEP_HP, PF_REVERSE, RM_PREV, RM_NEXT, RM_NONE, RM_NONE};
static const uint8_t RM_FX[8] = {PF_HALF, PF_JITTER, PF_CUTS, PF_SHORT, PF_PUMP, PF_COMB, PF_SPACE, PF_CRUSH};

static struct {
    uint8_t last[RM_NK];                             /* PICKUP: the knob's last value, 0xFF none yet */
    uint8_t sent[RM_NK];                             /* the value sent (or received) last, 0xFF none */
    uint32_t picked[(RM_NK + 31u) / 32u];            /* PICKUP: the knobs that have passed the value */
    uint32_t nsent[(RM_NB + 31u) / 32u];             /* the buttons' states sent: on */
    uint32_t nknown[(RM_NB + 31u) / 32u];            /* .. sent at all */
    uint32_t heard[(RM_NB + 31u) / 32u];             /* .. have sent something (as_cc: a CC) */
    uint32_t as_cc[(RM_NB + 31u) / 32u];
    uint8_t roll_t[8];                               /* the track a roll button rolls (the selected one, at the press) */
    uint8_t cur;                                     /* feedback: where the scan goes on */
    uint8_t init, usb_on;                            /* (USB configured again: everything sent again) */
    uint32_t ms;
} rm;
static inline int rm_bit(const uint32_t *w, uint32_t i) { return (w[i / 32u] >> (i & 31u)) & 1u; }
static inline void rm_put(uint32_t *w, uint32_t i, int on)
{
    w[i / 32u] = on ? w[i / 32u] | 1u << (i & 31u) : w[i / 32u] & ~(1u << (i & 31u));
}

static uint32_t rm_track(uint32_t c) { return c < 7u ? c : ui.track; }   /* (column 8: the selected track) */
/* the parameter of knob i (TRACKS' row x 8 + column, then GLOBAL's); VP_NONE: none */
static uint32_t rm_par(uint32_t i)
{
    uint32_t r = i / 8u, t = rm_track(i % 8u), v = track_voice(t);
    if (i >= RM_NTK)
        return RM_GK[i - RM_NTK];
    switch (r) {
    case RR_VOL: return PAR_VOL1 + v;
    case RR_PAN: return v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u;
    case RR_FILTER: return PAR_FILTER_FREQ_1 + v;
    case RR_RESO: return PAR_RESO_1 + v;
    case RR_DECAY: return t == 6u ? PAR_VELOD6_OPEN : PAR_VELOD1 + 2u * v;
    case RR_PITCH: return PAR_COARSE1 + 2u * v;
    case RR_DRIVE: return PAR_DRIVE1 + v;
    case RR_FXS: return PAR_FX_SEND1 + v;
    case RR_REVS: return PAR_REV_SEND1 + v;
    case RR_SRATE: return PAR_VOICE_DECIMATION1 + v;
    case RR_LFO: return PAR_AMOUNT_LFO1 + v;
    default: return PAR_RND_PITCH1 + v;
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

static void remote_reset(void)                       /* (a channel or the mode changed) all sent again */
{
    memset(rm.last, 0xFF, sizeof rm.last);
    memset(rm.sent, 0xFF, sizeof rm.sent);
    memset(rm.picked, 0, sizeof rm.picked);
    memset(rm.nknown, 0, sizeof rm.nknown);
}

static void rm_knob(uint32_t i, uint32_t v)
{
    uint32_t p = rm_par(i), lo, hi, now;
    int32_t n;
    if (p == VP_NONE)
        return;
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
        if (kset.rknobs == RK_PICKUP && !rm_bit(rm.picked, i)) {
            uint32_t c = rm_to_cc(p), l = rm.last[i];
            rm.last[i] = (uint8_t)v;
            if (!(v + 1u >= c && v <= c + 1u) && !(l != 0xFFu && (l < c) != (v < c)))
                return;                              /* (not there yet, nor passed it) */
            rm_put(rm.picked, i, 1);
        }
        now = lo + (v * (hi - lo) + 63u) / 127u;
    }
    morph_follow(p, (int32_t)now - (int32_t)vp_get(p));   /* (31: as the panel's knobs) */
    vp_set(p, now);
    rm.sent[i] = (uint8_t)rm_to_cc(p);               /* (no echo of what came in) */
    if (i < RM_NTK && sq.rec && par_lockable(p))
        seq_record_automation(rm_track(i % 8u), p, now);   /* (its own track's lock) */
    ui.force = 1;
}

static void rm_held(uint32_t pf_bit, int down)       /* a PERF effect held from the remote */
{
    uint32_t b = 1u << pf_bit;
    ui.rm_held = down ? ui.rm_held | b : ui.rm_held & ~b;
    if (down && pf_bit == PF_FREEZE && fx.type != FX_DELAY)
        ui_message("FREEZE: THE FX BUS IS NOT A DELAY");
    perf_apply();
}
static void rm_button(uint32_t j, int down)          /* button j (TRACKS' 0 .., then GLOBAL's) */
{
    uint32_t r = (j < RM_NTB ? j : j - RM_NTB) / 8u, c = j % 8u, t = rm_track(c), b = 1u << t;
    if (j < RM_NTB) {
        if (!down)
            return;
        if (r == RB_MUTE)
            sq.muted ^= (uint8_t)b;
        else
            sq.solo ^= (uint8_t)b;
    } else if (r == RG_PERF || r == RG_PERF2) {
        uint32_t f = RM_PF[(r - RG_PERF) * 8u + c];
        if (f == RM_PLAY && down) {
            if (sq.running) {
                seq_stop_now();
            } else {
                sq.song_mode = 0;
                seq_start(1);
            }
        } else if (f == RM_REC && down) {
            sq.rec ^= 1u;
        } else if ((f == RM_PREV || f == RM_NEXT) && down) {
            seq_set_next_pattern((sq.active + (f == RM_NEXT ? 1u : NUM_PATTERN - 1u)) % NUM_PATTERN);
        } else if (f < NPF2) {
            rm_held(f, down);
        }
    } else if (r == RG_SELECT) {
        if (down && c < 7u)
            track_select(c);
    } else if (r == RG_ROLL) {
        if (down)
            rm.roll_t[c] = (uint8_t)t;
        t = rm.roll_t[c];
        if (down)
            seq_set_roll(t, 1);
        else if (!(ui.roll_latch & 1u << t))
            seq_set_roll(t, 0);
    } else if (r == RG_DRILL) {
        ui.rm_drill = (uint8_t)(down ? ui.rm_drill | b : ui.rm_drill & ~b);
        perf_apply();
    } else if (r == RG_HOLD) {
        rm_held(RM_FX[c], down);
    } else if (r == RG_LATCH) {
        if (down) {
            ui.rm_latch ^= 1u << RM_FX[c];
            perf_apply();
        }
    } else if (down) {                               /* RG_DENS: a track left out of DENSITY, or all back in */
        pfs.dex = (uint8_t)(c < 7u ? pfs.dex ^ (1u << c) : 0u);
    }
    ui.force = 1;
}

/* a button's state, for its light */
static int rm_state(uint32_t j)
{
    uint32_t r = (j < RM_NTB ? j : j - RM_NTB) / 8u, c = j % 8u, t = rm_track(c);
    if (j < RM_NTB)
        return r == RB_MUTE ? (sq.muted >> t) & 1u : (sq.solo >> t) & 1u;
    switch (r) {
    case RG_PERF:
    case RG_PERF2: {
        uint32_t f = RM_PF[(r - RG_PERF) * 8u + c];
        return f == RM_PLAY ? sq.running != 0 : f == RM_REC ? sq.rec != 0 : f < NPF2 ? (pf.on >> f) & 1u : 0;
    }
    case RG_SELECT: return c < 7u && ui.track == c;
    case RG_ROLL: return (sq.roll >> t) & 1u;
    case RG_DRILL: return (sq.drill >> t) & 1u;
    case RG_HOLD: return (pf.on >> RM_FX[c]) & 1u;
    case RG_LATCH: return (ui.rm_latch >> RM_FX[c]) & 1u;
    default: return c < 7u ? (pfs.dex >> c) & 1u : 0;
    }
}

/* what came in on a remote channel: a knob, or a button as a CC or a note */
static void rm_in(uint32_t ch, uint32_t st, uint32_t d1, uint32_t d2)
{
    int glob = ch == remote_gch, cc = st == 0xB0u;
    uint32_t nk = glob ? RM_NGK : RM_NTK, b0 = glob ? RM_GB0 : RM_TB0, nb = glob ? RM_NGB : RM_NTB, j;
    if (cc && d1 >= RM_CC0 && d1 < RM_CC0 + nk) {
        rm_knob(d1 - RM_CC0 + (glob ? RM_NTK : 0u), d2);
    } else if (d1 >= b0 && d1 < b0 + nb) {
        j = d1 - b0 + (glob ? RM_NTB : 0u);
        rm_put(rm.heard, j, 1);
        rm_put(rm.as_cc, j, cc);
        rm_button(j, (cc || st == 0x90u) && d2);
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
        uint32_t pkt = rin_q[rin_r % RQ], st = (pkt >> 8) & 0xFFu;
        RING_PUBLISH();
        rin_r++;
        rm_in(st & 15u, st & 0xF0u, (pkt >> 16) & 127u, (pkt >> 24) & 127u);
    }
    if ((remote_ch > 15u && remote_gch > 15u) || fm1_ms - rm.ms < 4u)
        return;
    rm.ms = fm1_ms;
    for (i = n = 0; i < RM_NK + RM_NB && n < 12u; i++) {
        uint32_t k = (rm.cur + i) % (RM_NK + RM_NB);
        if (k < RM_NK) {
            uint32_t p = rm_par(k), ch = k < RM_NTK ? remote_ch : remote_gch, v;
            if (ch > 15u || p == VP_NONE || (v = rm_to_cc(p)) == rm.sent[k])
                continue;
            if (!remote_out_post(0xB0u | ch, RM_CC0 + (k < RM_NTK ? k : k - RM_NTK), v))
                break;
            if (rm.sent[k] != 0xFFu)                 /* (changed elsewhere: PICKUP waits for the pot again) */
                rm_put(rm.picked, k, 0);
            rm.sent[k] = (uint8_t)v;
        } else {
            uint32_t j = k - RM_NK, ch = j < RM_NTB ? remote_ch : remote_gch, on = rm_state(j) != 0;
            uint32_t d1 = j < RM_NTB ? RM_TB0 + j : RM_GB0 + j - RM_NTB;
            if (ch > 15u || (rm_bit(rm.nknown, j) && rm_bit(rm.nsent, j) == (int)on))
                continue;
            if ((!rm_bit(rm.heard, j) || rm_bit(rm.as_cc, j)) && !remote_out_post(0xB0u | ch, d1, on ? 127u : 0u))
                break;
            if ((!rm_bit(rm.heard, j) || !rm_bit(rm.as_cc, j)) && !remote_out_post(0x90u | ch, d1, on ? 127u : 0u))
                break;
            rm_put(rm.nknown, j, 1);
            rm_put(rm.nsent, j, (int)on);
        }
        n++;
    }
    rm.cur = (uint8_t)((rm.cur + i) % (RM_NK + RM_NB));
}
