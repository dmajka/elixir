/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir: SPELLS (32), PERF's second set (ARP: PERFORM > SPELLS > GLOBAL MOD). Guided randomization: NONE (a little
 * variation) and ten spells, each its own way of changing the voices and the steps of the tracks the black keys pick
 * (docs/SPELLS.md), in the parts of them the white keys pick (sound 1-8, pattern 9-16). D1 stays a kick. The pages:
 *   GRIMOIRE   SPELL (a spell, or a recipe saved from one: U1 ..), VARIATION, SCOPE (the white keys' presets), ROLL
 *   CAST       SOUND (how much the voices change; 0 none), PATTERN (how much of the steps), DENSITY (how many
 *              hits: 64 as many as now), CHAOS (the spell's musical rules .. anything)
 *   CHARACTER  the spell's own four: a sound one, a rhythm one, EVOLUTION, PROTECTION
 *   LIMITS     the most any spell sets: a voice's reverb send, its FX send, the reverb's level, the FX bus's mix
 *   MOTION     (0.19) how far the tracks' speed and direction part (SAME: every target forward at the pattern's
 *              speed .. FREE: any), the lengths a cast may give (bars, even, odd)
 * OCT+ casts (a d12 rolls, then the spell plays while the pattern runs); cast again (OCT+) and it is cast afresh on
 * what was there before it; OCT- REVERSE puts that back; bar key 2 KEEPs it (and leaving SPELLS does). Bar key 1:
 * every track or none, bar key 3: SAVE the spell's settings, targets and roll as a recipe, bar key 4: the spell's
 * knobs as at first. ROLL: NEW rolls a new
 * seed each time, SAME the last one (a recipe: its own); VARIATION moves the knobs a cast uses (0: as set), never
 * the knobs themselves. The spell before a cast is kept in upat (the pattern:
 * DENSITY's and a track's UNDO give theirs up) and ca_snap / ca_msnap (the kit and the morph target: Control All's,
 * which can't run here). Included by ui.c. */

#define NSPELL 12u
#define NSPELL0 11u                                  /* (the spells before 0.19-test11: KSP2 has their knobs
                                                      * before LIMITS) */
#define NRECIPE 8u
#define SP_ROLL_MS 900u                              /* the die's roll, then the cast */
enum { SPK_SOUND, SPK_PAT, SPK_DENS, SPK_CHAOS, SPK_C1, SPK_C2, SPK_C3, SPK_C4, NSPK };
enum { SPL_RVS, SPL_FXS, SPL_RLEV, SPL_FXMIX, NSPL };
enum { SPV_SPELL, SPV_VAR, SPV_SCOPE, SPV_MODE, SPV_LIM = SPV_MODE + 1 + NSPK, SPV_MOT = SPV_LIM + NSPL, SPV_MLEN,
       NSPV };                                       /* the cards (VP_SP + n) */
/* MOTION: how many targets a cast sends their own speed or way (the rest: forward, as the pattern) */
enum { SPMO_SAME, SPMO_ONE, SPMO_FEW, SPMO_MANY, SPMO_FREE, NSPMO };
enum { SPML_BARS, SPML_EVEN, SPML_ODD, NSPML };      /* .. and the lengths it may give */
static const uint8_t SP_LIM0[NSPL] = {30, 30, 80, 64};   /* (the LIMITS at first: the effects never drown it) */
enum { SPM_RECAST, SPM_RECALL, NSPM };
enum { SP_NONE, SP_POLTER, SP_SKELETON, SP_GLITCH, SP_DUB, SP_RITUAL, SP_RASTER, SP_DRIFT, SP_RUST, SP_GLASS, SP_BIO, SP_MOTES };
/* what a cast may change: the white keys, sound 1-8 then pattern 9-16 (bit k: key k + 1) */
enum { SC_PITCH = 1, SC_OSC = 2, SC_TRANS = 4, SC_ENV = 8, SC_FILTER = 16, SC_DRIVE = 32, SC_FX = 64, SC_MOD = 128,
       SC_HITS = 256, SC_ACC = 512, SC_CHANCE = 1024, SC_TIME = 2048, SC_RAT = 4096, SC_NOTES = 8192,
       SC_LEN = 16384, SC_LINKS = 32768 };
#define SC_VOICE (SC_PITCH | SC_OSC | SC_TRANS)
#define SC_SHAPE (SC_FILTER | SC_DRIVE)
#define SC_SOUND 0x00FFu
#define SC_RHYTHM (SC_HITS | SC_ACC | SC_CHANCE | SC_TIME | SC_RAT)
#define SC_STRUCT (SC_LEN | SC_LINKS)
#define SC_PATTERN 0xFF00u
#define SC_ALL 0xFFFFu
static const char *const SC_SHORT[16] = {"PIT", "OSC", "TRN", "ENV", "FLT", "DRV", "FX", "MOD",
                                         "HIT", "ACC", "PRB", "TIM", "RAT", "NOT", "LEN", "LNK"};
static const char *const SC_LONG[16] = {"PITCH", "OSCILLATOR, MODELS", "TRANSIENT", "ENVELOPES, LEVEL", "FILTER",
                                        "DRIVE, BITS", "FX, SENDS, PAN", "MODULATION", "HITS", "ACCENTS",
                                        "CHANCE, CONDITIONS", "TIMING, FLAMS", "RATCHETS", "NOTES", "LENGTH, CLOCK",
                                        "LINKS, GENERATORS"};
/* SCOPE: presets for the white keys (CUSTOM when they match none) */
#define NSCOPE 8u
static const char *const SCOPE_NAME[NSCOPE] = {"ALL", "SOUND", "PATTERN", "VOICE", "SHAPE", "SPACE", "RHYTHM", "NOTES"};
static const uint16_t SCOPE_CATS[NSCOPE] = {SC_ALL, SC_SOUND, SC_PATTERN, SC_VOICE | SC_ENV, SC_SHAPE, SC_FX | SC_MOD,
                                            SC_RHYTHM, SC_NOTES};

/* the roles a spell moves, each voice's parameter for it (sp_role_par) */
enum { R_COARSE, R_FINE, R_WAVE, R_FMAMT, R_FMFREQ, R_FMWAVE, R_FM2AMT, R_FM2FREQ, R_NOISE, R_SNMIX, R_TWAVE, R_TVOL,
       R_TFREQ, R_ATK, R_DEC, R_SLOPE, R_REPEAT, R_MODDEC, R_PSLOPE, R_MODAMT, R_FFREQ, R_RESO, R_FTYPE, R_FDRIVE,
       R_DRIVE, R_DECIM, R_BITS, R_SHAPE, R_TILT, R_INS, R_INSA, R_INSB, R_INSC, R_PAN, R_FXS, R_RVS, R_RNDP, R_RNDD,
       R_RNDF, R_RNDPAN, R_XAMT, R_M1, R_M2, R_M3, R_M4, R_M5, R_M6, NROLE };
static uint32_t sp_role_par(uint32_t r, uint32_t v)  /* 0: the voice has none */
{
    static const uint8_t WAVE[NVOICE] = {1, 2, 3, 4, 6, 7};
    switch (r) {
    case R_COARSE: return PAR_COARSE1 + 2u * v;
    case R_FINE: return PAR_FINE1 + 2u * v;
    case R_WAVE: return WAVE[v];
    case R_FMAMT: return v < 3u ? PAR_FMAMNT1 + 2u * v : v == 4u ? PAR_MOD_OSC_GAIN1_CYM : v == 5u ? PAR_MOD_OSC_GAIN1 : 0u;
    case R_FMFREQ: return v < 3u ? PAR_FM_FREQ1 + 2u * v : v == 4u ? PAR_MOD_OSC_F1_CYM : v == 5u ? PAR_MOD_OSC_F1 : 0u;
    case R_FMWAVE: return v < 3u ? PAR_MOD_WAVE_DRUM1 + v : v == 4u ? PAR_WAVE2_CYM : v == 5u ? PAR_WAVE2_HH : 0u;
    case R_FM2AMT: return v == 4u ? PAR_MOD_OSC_GAIN2_CYM : v == 5u ? PAR_MOD_OSC_GAIN2 : 0u;
    case R_FM2FREQ: return v == 4u ? PAR_MOD_OSC_F2_CYM : v == 5u ? PAR_MOD_OSC_F2 : 0u;
    case R_NOISE: return v == 3u ? PAR_NOISE_FREQ1 : 0u;
    case R_SNMIX: return v == 3u ? PAR_MIX1 : 0u;
    case R_TWAVE: return PAR_TRANS1_WAVE + v;
    case R_TVOL: return PAR_TRANS1_VOL + v;
    case R_TFREQ: return PAR_TRANS1_FREQ + v;
    case R_ATK: return PAR_VELOA1 + 2u * v;
    case R_DEC: return v < 5u ? PAR_VELOD1 + 2u * v : PAR_VELOD6_CLOSED;
    case R_SLOPE: return PAR_VOL_SLOPE1 + v;
    case R_REPEAT: return v == 3u ? PAR_REPEAT4 : v == 4u ? PAR_REPEAT5 : 0u;
    case R_MODDEC: return v < 4u ? PAR_MOD_EG1 + v : 0u;
    case R_PSLOPE: return v < 4u ? PAR_PITCH_SLOPE1 + v : 0u;
    case R_MODAMT: return v < 4u ? PAR_MODAMNT1 + v : 0u;
    case R_FFREQ: return PAR_FILTER_FREQ_1 + v;
    case R_RESO: return PAR_RESO_1 + v;
    case R_FTYPE: return PAR_FILTER_TYPE_1 + v;
    case R_FDRIVE: return PAR_FILTER_DRIVE_1 + v;
    case R_DRIVE: return PAR_DRIVE1 + v;
    case R_DECIM: return PAR_VOICE_DECIMATION1 + v;
    case R_BITS: return PAR_BITS1 + v;
    case R_SHAPE: return PAR_SHAPE1 + v;
    case R_TILT: return PAR_TILT1 + v;
    case R_INS: return PAR_INS1 + v;
    case R_INSA: return PAR_INSA1 + v;
    case R_INSB: return PAR_INSB1 + v;
    case R_INSC: return PAR_INSC1 + v;
    case R_PAN: return v < 3u ? PAR_PAN1 + v : PAR_PAN4 + v - 3u;
    case R_FXS: return PAR_FX_SEND1 + v;
    case R_RVS: return PAR_REV_SEND1 + v;
    case R_RNDP: return PAR_RND_PITCH1 + v;
    case R_RNDD: return PAR_RND_DECAY1 + v;
    case R_RNDF: return PAR_RND_FILTER1 + v;
    case R_RNDPAN: return PAR_RND_PAN1 + v;
    case R_XAMT: return PAR_XAMT1 + v;
    case R_M1: case R_M2: case R_M3: case R_M4: case R_M5: case R_M6: {   /* (the model's knobs: with a model on) */
        static const uint16_t M[6] = {PAR_MDECAY1, PAR_MDAMP1, PAR_MMAT1, PAR_MPOS1, PAR_MEXC1, PAR_MTENS1};
        return par[PAR_MODEL1 + v] ? M[r - R_M1] + v : 0u;
    }
    default: return 0;
    }
}
static uint32_t sp_role_cat(uint32_t r)
{
    return r <= R_FINE ? SC_PITCH : r <= R_SNMIX || r >= R_M1 ? SC_OSC : r <= R_TFREQ ? SC_TRANS
         : r <= R_MODAMT ? SC_ENV : r <= R_FDRIVE ? SC_FILTER : r <= R_TILT ? SC_DRIVE : r <= R_RVS ? SC_FX : SC_MOD;
}
static int sp_kick_role(uint32_t r)                  /* (D1: the kick's own, sp_kick's) */
{
    return r <= R_MODAMT || r == R_FFREQ || r == R_FTYPE || r == R_DRIVE || r == R_PAN || (r >= R_INS && r <= R_INSC) ||
           r >= R_M1;
}

/* a spell's sound move: a role pulled towards lo .. hi (MF_C1: the spell's first CHARACTER knob picks where in it;
 * lo above hi: the other way), or (MF_SET) a value lo .. hi, (MF_LIST) one of SP_LIST[lo]'s first hi, (MF_NEAR) up
 * to hi either way of where it is. Weight 1..4 quarters (MF_W*). MF_PROT: PROTECTION keeps it; MF_EVO: EVOLUTION
 * sets it (random per hit and the like) */
typedef struct { uint8_t r, lo, hi, f; } smove_t;
#define MF_W1 0u
#define MF_W2 1u
#define MF_W3 2u
#define MF_W4 3u
#define MF_C1 0x04u
#define MF_SET 0x08u
#define MF_LIST 0x10u
#define MF_NEAR 0x20u
#define MF_PROT 0x40u
#define MF_EVO 0x80u
enum { SL_FMETAL, SL_FDARK, SL_FGLASS, SL_SHMETAL, SL_SHDIRT, SL_SHWARM, SL_INS, NSL };
#define FK(f) ((f) - 1u)                             /* (a filter type's kit value) */
static const uint8_t SP_LIST[NSL][5] = {{FK(F_BP), FK(F_HP), FK(F_PEAK), FK(F_COMBP), FK(F_COMBN)},
                                        {FK(F_LP), FK(F_LP2), FK(F_DUB), FK(F_LP4), FK(F_LP)},
                                        {FK(F_LP), FK(F_BP), FK(F_PEAK), FK(F_UBP), FK(F_LP2)}, {SH_FOLD, SH_SFOLD, SH_RECT, SH_FOLD, SH_SFOLD},
                                        {SH_CLIP, SH_BITE, SH_FUZZ, SH_RECT, SH_CLIP}, {SH_TAPE, SH_LXR, SH_ASYM, SH_TAPE, SH_LXR},
                                        {INS_PHASER, INS_FLANGER, INS_CHORUS, INS_SHIFT, INS_RING}};

static const smove_t MV_NONE[] = {                  /* (a little of everything, near where it is) */
    {R_COARSE, 0, 3, MF_W2 | MF_NEAR | MF_PROT}, {R_FINE, 0, 10, MF_W2 | MF_NEAR}, {R_FMAMT, 0, 16, MF_W2 | MF_NEAR},
    {R_FMFREQ, 0, 12, MF_W1 | MF_NEAR}, {R_NOISE, 0, 16, MF_W2 | MF_NEAR}, {R_SNMIX, 0, 16, MF_W2 | MF_NEAR},
    {R_TVOL, 0, 20, MF_W2 | MF_NEAR}, {R_TFREQ, 0, 16, MF_W1 | MF_NEAR}, {R_DEC, 0, 14, MF_W3 | MF_NEAR | MF_PROT},
    {R_SLOPE, 0, 16, MF_W1 | MF_NEAR}, {R_MODAMT, 0, 12, MF_W1 | MF_NEAR}, {R_MODDEC, 0, 12, MF_W1 | MF_NEAR},
    {R_FFREQ, 0, 18, MF_W3 | MF_NEAR}, {R_RESO, 0, 12, MF_W1 | MF_NEAR}, {R_DRIVE, 0, 12, MF_W1 | MF_NEAR},
    {R_TILT, 0, 10, MF_W2 | MF_NEAR}, {R_PAN, 0, 14, MF_W1 | MF_NEAR}, {R_M1, 0, 14, MF_W2 | MF_NEAR},
    {R_M2, 0, 14, MF_W2 | MF_NEAR}, {R_M3, 0, 14, MF_W2 | MF_NEAR}, {R_RVS, 0, 10, MF_W1 | MF_NEAR},
    {R_RNDP, 0, 12, MF_W2 | MF_EVO}, {R_RNDF, 0, 20, MF_W2 | MF_EVO}};
static const smove_t MV_POLTER[] = {
    {R_FMAMT, 20, 100, MF_W3 | MF_C1}, {R_FMFREQ, 10, 127, MF_W3}, {R_FMWAVE, 0, 7, MF_W2 | MF_SET},
    {R_FM2AMT, 40, 127, MF_W3 | MF_C1}, {R_FM2FREQ, 10, 127, MF_W3}, {R_WAVE, 0, 5, MF_W1 | MF_SET},
    {R_FINE, 0, 127, MF_W2 | MF_PROT}, {R_TVOL, 50, 127, MF_W3}, {R_TWAVE, 0, 11, MF_W2 | MF_SET},
    {R_TFREQ, 30, 127, MF_W2}, {R_DEC, 12, 95, MF_W3 | MF_PROT}, {R_SLOPE, 0, 127, MF_W2},
    {R_MODAMT, 20, 100, MF_W2 | MF_PROT}, {R_MODDEC, 10, 80, MF_W2}, {R_FFREQ, 50, 127, MF_W2},
    {R_RESO, 30, 105, MF_W2 | MF_C1}, {R_FTYPE, SL_FMETAL, 5, MF_W1 | MF_LIST}, {R_SHAPE, SL_SHMETAL, 3, MF_W1 | MF_LIST},
    {R_DRIVE, 0, 60, MF_W1}, {R_TILT, 70, 110, MF_W1}, {R_M2, 60, 127, MF_W2 | MF_C1}, {R_M3, 0, 127, MF_W2},
    {R_RNDP, 0, 30, MF_W3 | MF_EVO}, {R_RNDF, 0, 50, MF_W3 | MF_EVO}};
static const smove_t MV_GLITCH[] = {
    {R_BITS, 0, 12, MF_W3 | MF_C1}, {R_DECIM, 127, 40, MF_W3 | MF_C1}, {R_DEC, 4, 50, MF_W4},
    {R_COARSE, 30, 100, MF_W2 | MF_PROT}, {R_FINE, 0, 127, MF_W2}, {R_TVOL, 80, 127, MF_W2},
    {R_TWAVE, 0, 11, MF_W2 | MF_SET}, {R_FFREQ, 40, 127, MF_W2}, {R_RESO, 0, 90, MF_W2},
    {R_SHAPE, SL_SHDIRT, 4, MF_W2 | MF_LIST}, {R_DRIVE, 20, 100, MF_W2 | MF_C1}, {R_PAN, 0, 127, MF_W1},
    {R_FMAMT, 0, 127, MF_W1}, {R_M1, 0, 40, MF_W2}, {R_RNDP, 0, 60, MF_W3 | MF_EVO}, {R_RNDD, 0, 60, MF_W3 | MF_EVO},
    {R_RNDPAN, 0, 90, MF_W2 | MF_EVO}};
static const smove_t MV_DUB[] = {
    {R_DEC, 40, 110, MF_W3}, {R_FFREQ, 95, 20, MF_W4 | MF_C1}, {R_RESO, 20, 80, MF_W2},
    {R_FTYPE, SL_FDARK, 4, MF_W2 | MF_LIST}, {R_TILT, 70, 20, MF_W3 | MF_C1}, {R_DRIVE, 0, 40, MF_W1},
    {R_SHAPE, SL_SHWARM, 3, MF_W1 | MF_LIST}, {R_PAN, 20, 107, MF_W2}, {R_RVS, 20, 110, MF_W4},
    {R_FXS, 0, 100, MF_W3}, {R_M2, 60, 10, MF_W2 | MF_C1}, {R_RNDF, 0, 40, MF_W3 | MF_EVO},
    {R_RNDPAN, 0, 60, MF_W3 | MF_EVO}};
static const smove_t MV_RITUAL[] = {
    {R_COARSE, 0, 127, MF_W3 | MF_PROT}, {R_FINE, 0, 127, MF_W2}, {R_WAVE, 0, 5, MF_W2 | MF_SET},
    {R_FMAMT, 0, 127, MF_W3}, {R_FMFREQ, 0, 127, MF_W3}, {R_FMWAVE, 0, 7, MF_W2 | MF_SET}, {R_FM2AMT, 0, 127, MF_W2},
    {R_NOISE, 0, 127, MF_W2}, {R_SNMIX, 0, 127, MF_W2}, {R_TVOL, 0, 127, MF_W2}, {R_TWAVE, 0, 11, MF_W2 | MF_SET},
    {R_TFREQ, 0, 127, MF_W2}, {R_ATK, 0, 60, MF_W2}, {R_DEC, 5, 120, MF_W3 | MF_PROT}, {R_SLOPE, 0, 127, MF_W2},
    {R_MODAMT, 0, 127, MF_W2}, {R_MODDEC, 0, 127, MF_W2}, {R_FFREQ, 20, 127, MF_W3}, {R_RESO, 0, 110, MF_W3},
    {R_FTYPE, 0, FK(F_LAST), MF_W2 | MF_SET}, {R_FDRIVE, 0, 110, MF_W2}, {R_SHAPE, 0, NSHAPE - 1, MF_W2 | MF_SET},
    {R_DRIVE, 0, 110, MF_W2 | MF_C1}, {R_BITS, 0, 10, MF_W1}, {R_TILT, 20, 110, MF_W2}, {R_PAN, 0, 127, MF_W2},
    {R_FXS, 0, 110, MF_W2}, {R_RVS, 0, 110, MF_W2}, {R_INS, SL_INS, 5, MF_W1 | MF_LIST}, {R_INSA, 30, 110, MF_W1},
    {R_INSB, 0, 127, MF_W1}, {R_INSC, 0, 127, MF_W1}, {R_M1, 0, 127, MF_W2}, {R_M2, 0, 127, MF_W2},
    {R_M3, 0, 127, MF_W2}, {R_M4, 0, 127, MF_W2}, {R_RNDP, 0, 50, MF_W2 | MF_EVO}, {R_RNDD, 0, 60, MF_W2 | MF_EVO},
    {R_RNDF, 0, 60, MF_W2 | MF_EVO}, {R_XAMT, 30, 110, MF_W2 | MF_EVO}};
static const smove_t MV_RASTER[] = {
    {R_DEC, 0, 30, MF_W3}, {R_FFREQ, 60, 127, MF_W2}, {R_RESO, 0, 70, MF_W1 | MF_C1}, {R_BITS, 0, 8, MF_W2 | MF_C1},
    {R_DECIM, 127, 60, MF_W2 | MF_C1}, {R_TILT, 64, 110, MF_W2}, {R_PAN, 0, 127, MF_W3}, {R_M1, 0, 45, MF_W3},
    {R_M2, 10, 120, MF_W3 | MF_C1}, {R_M3, 0, 100, MF_W2 | MF_EVO}, {R_M5, 0, 60, MF_W2}, {R_M6, 0, 90, MF_W2},
    {R_RNDPAN, 0, 100, MF_W3 | MF_EVO}, {R_RNDF, 0, 60, MF_W2 | MF_EVO}};
static const smove_t MV_DRIFT[] = {
    {R_FINE, 0, 8, MF_W3 | MF_NEAR}, {R_FMAMT, 0, 10, MF_W2 | MF_NEAR}, {R_DEC, 0, 12, MF_W3 | MF_NEAR},
    {R_FFREQ, 0, 15, MF_W4 | MF_NEAR}, {R_RESO, 0, 10, MF_W2 | MF_NEAR}, {R_DRIVE, 0, 6, MF_W1 | MF_NEAR},
    {R_TILT, 0, 8, MF_W2 | MF_NEAR}, {R_M1, 0, 10, MF_W2 | MF_NEAR}, {R_M2, 0, 10, MF_W2 | MF_NEAR},
    {R_M3, 0, 10, MF_W2 | MF_NEAR}, {R_RVS, 0, 12, MF_W1 | MF_NEAR}, {R_RNDF, 0, 25, MF_W3 | MF_EVO},
    {R_RNDD, 0, 20, MF_W2 | MF_EVO}};
static const smove_t MV_RUST[] = {
    {R_FMAMT, 30, 110, MF_W3}, {R_FMFREQ, 30, 127, MF_W3}, {R_FM2AMT, 50, 127, MF_W2}, {R_TVOL, 80, 127, MF_W3},
    {R_TWAVE, 0, 11, MF_W2 | MF_SET}, {R_DEC, 5, 60, MF_W3 | MF_PROT}, {R_RESO, 40, 110, MF_W2 | MF_C1},
    {R_FDRIVE, 40, 127, MF_W3 | MF_C1}, {R_SHAPE, SL_SHDIRT, 4, MF_W2 | MF_LIST}, {R_DRIVE, 30, 105, MF_W3 | MF_C1},
    {R_BITS, 0, 9, MF_W2 | MF_C1}, {R_DECIM, 127, 50, MF_W1 | MF_C1}, {R_TILT, 64, 100, MF_W1},
    {R_NOISE, 60, 127, MF_W2}, {R_M2, 30, 127, MF_W2}, {R_M3, 60, 127, MF_W2}, {R_RNDP, 0, 25, MF_W2 | MF_EVO}};
static const smove_t MV_GLASS[] = {
    {R_M1, 60, 120, MF_W3}, {R_M2, 20, 127, MF_W4 | MF_C1}, {R_M3, 0, 127, MF_W3}, {R_M4, 0, 127, MF_W3},
    {R_M5, 0, 40, MF_W1}, {R_M6, 0, 40, MF_W1}, {R_FFREQ, 60, 127, MF_W2 | MF_C1}, {R_RESO, 0, 60, MF_W2},
    {R_FTYPE, SL_FGLASS, 5, MF_W1 | MF_LIST}, {R_DEC, 50, 120, MF_W3}, {R_ATK, 0, 20, MF_W1},
    {R_MODAMT, 0, 20, MF_W1}, {R_RVS, 40, 110, MF_W4}, {R_TILT, 64, 100, MF_W2 | MF_C1},
    {R_FMAMT, 0, 50, MF_W1}, {R_RNDF, 0, 30, MF_W2 | MF_EVO}};
static const smove_t MV_MOTES[] = {                 /* (MICRO's knobs: LENGTH, COLOUR, JITTER, REVERSE, TENSION) */
    {R_M1, 4, 40, MF_W3}, {R_M2, 20, 110, MF_W3 | MF_C1}, {R_M3, 0, 90, MF_W2 | MF_EVO}, {R_M5, 0, 50, MF_W2},
    {R_M6, 0, 50, MF_W2}, {R_DEC, 4, 40, MF_W3 | MF_PROT}, {R_FFREQ, 70, 127, MF_W2}, {R_RESO, 0, 50, MF_W1},
    {R_FTYPE, SL_FGLASS, 5, MF_W1 | MF_LIST}, {R_NOISE, 70, 127, MF_W3}, {R_SNMIX, 90, 127, MF_W3},
    {R_TVOL, 0, 40, MF_W2}, {R_DRIVE, 0, 10, MF_W2}, {R_TILT, 64, 100, MF_W1 | MF_C1}, {R_PAN, 0, 127, MF_W3},
    {R_RVS, 30, 100, MF_W3}, {R_RNDPAN, 0, 80, MF_W3 | MF_EVO}, {R_RNDP, 0, 30, MF_W2 | MF_EVO},
    {R_RNDD, 0, 40, MF_W2 | MF_EVO}};
static const smove_t MV_BIO[] = {
    {R_RNDP, 0, 30, MF_W3 | MF_EVO}, {R_RNDD, 0, 40, MF_W3 | MF_EVO}, {R_RNDF, 0, 50, MF_W3 | MF_EVO},
    {R_RNDPAN, 0, 60, MF_W2 | MF_EVO}, {R_DEC, 0, 10, MF_W2 | MF_NEAR}, {R_FFREQ, 0, 12, MF_W2 | MF_NEAR}};

/* the spells. Their pattern's ways (0..127 each): FILL the most of a track's steps DENSITY's top makes hits; SYNC
 * off-beats, CLUS hits in clusters, VEL accents, PROB chances, COND conditions, SHIFT micro-timing, FLAM, RAT
 * ratchets, NOTE notes, STRC the track's length, clock, direction. SF_*: what else it does */
#define SF_C1PAT 0x01u                               /* (its first CHARACTER knob is the accents' contrast) */
#define SF_RINGS 0x02u                               /* (it may put Rings' models on D1-D3) */
#define SF_PLAITS 0x04u                              /* (.. Plaits') */
#define SF_MICRO 0x08u                               /* (.. MICRO's, on any voice) */
#define SF_LINKS 0x10u                               /* (it links tracks) */
#define SF_XMOD 0x20u                                /* (it cross-modulates them) */
#define SF_LFO 0x40u                                 /* (EVOLUTION: LFO 2 slowly on the filter) */
#define SF_MUTATE 0x80u                              /* (EVOLUTION: MUTATE on the track, slowly) */
#define SF_SCALE 0x100u                              /* (a scale for the notes, if the pattern has none) */
#define SF_DUB 0x200u                                /* (the delay, dub style: with every track picked) */
#define SF_FXALL 0x400u                              /* (the FX bus and the reverb: with every track picked) */
#define SF_PLATE 0x800u                              /* (a bright plate or hall: with every track picked) */
#define SF_SOFT 0x1000u                              /* (its new hits and accents softer; the kick quieter) */
typedef struct {
    const char *name, *card, *line[2];               /* its name, on the card, two lines on what it does */
    const char *cn[4], *cl[4];                       /* its CHARACTER knobs: names, what each does */
    uint8_t def[NSPK];                               /* its knobs at first */
    uint16_t cats;
    uint8_t fill, sync, clus, vel, prob, cond, shift, flam, rat, note, strc;
    uint16_t fl;
    const smove_t *mv;
    uint8_t nmv;
} spell_t;
#define MV(a) a, sizeof a / sizeof a[0]
static const spell_t SPELL[NSPELL] = {
    {"NO SPELL", "NONE", {"A little variation: the", "sound and the steps nudged"},
     {"Range", "Detail", "Motion", "Protect"},
     {"Small to wider nudges", "Plain to busy details", "Still to moving", "Keeps kick and snare"},
     {24, 24, 64, 16, 32, 24, 0, 96}, SC_SOUND | SC_RHYTHM | SC_NOTES,
     50, 20, 10, 40, 20, 10, 20, 10, 20, 0, 0, 0, MV(MV_NONE)},
    {"POLTERGEIST", "POLTER", {"Metallic, unstable hits,", "rattles, the rhythm kept"},
     {"Metal", "Rattle", "Haunting", "Preserve"},
     {"Harmonic to inharmonic", "Clean strikes to rattles", "Still to drifting tones", "Keeps pitch and decay"},
     {80, 0, 64, 40, 80, 50, 30, 40}, SC_SOUND | SC_RHYTHM | SC_NOTES,
     70, 20, 30, 40, 30, 20, 20, 50, 40, 0, 0, SF_RINGS | SF_XMOD | SF_LFO, MV(MV_POLTER)},
    {"SKELETON", "SKELETON", {"Strips the groove to its", "bones: accents and gaps"},
     {"Emphasis", "Syncope", "Variety", "Anchors"},
     {"Even to hard accents", "On the beat to off it", "Steady to conditional", "Keeps kick and snare"},
     {0, 80, 36, 30, 80, 40, 40, 100}, SC_RHYTHM,
     45, 70, 0, 110, 60, 50, 10, 20, 10, 0, 0, SF_C1PAT, 0, 0},
    {"GLITCH STORM", "GLITCH", {"Digital errors, bursts,", "cuts and chaotic fills"},
     {"Corrupt", "Fragment", "Unstable", "Groove"},
     {"Clean to degraded", "Some glitches to bursts", "Repeating to evolving", "Keeps the beat"},
     {70, 80, 80, 60, 70, 80, 50, 50}, SC_SOUND | SC_RHYTHM | SC_NOTES,
     90, 40, 110, 70, 60, 50, 60, 50, 120, 20, 10, SF_MICRO, MV(MV_GLITCH)},
    {"HAUNTED DUB", "DUB", {"Dark filters, echoes and", "space, the kick kept dry"},
     {"Darkness", "Echo", "Drift", "Dry Kick"},
     {"Bright to dark and warm", "Dry to delayed space", "Still to slow movement", "Keeps the kick dry"},
     {80, 30, 56, 30, 80, 80, 60, 100}, SC_SOUND | SC_RHYTHM | SC_NOTES,
     45, 50, 0, 60, 50, 20, 20, 10, 5, 0, 0, SF_LFO | SF_DUB, MV(MV_DUB)},
    {"FORBIDDEN RITUAL", "RITUAL", {"All at once: the models,", "steps, links, the effects"},
     {"Alchemy", "Disorder", "Possess", "Protect"},
     {"Timbre to new models", "Steady to structural", "Little to deep coupling", "Keeps what it guards"},
     {90, 90, 72, 90, 70, 70, 60, 20}, SC_ALL,
     85, 80, 60, 90, 70, 60, 50, 40, 70, 40, 90,
     SF_RINGS | SF_PLAITS | SF_MICRO | SF_LINKS | SF_XMOD | SF_LFO | SF_FXALL, MV(MV_RITUAL)},
    {"RASTER SEANCE", "RASTER", {"Microsound: clicks, grains,", "cuts and rhythmic silence"},
     {"Grain", "Fragment", "Jitter", "Backbone"},
     {"Clean clicks to noise", "Sparse to clustered", "Still to jittering time", "Keeps the anchors"},
     {80, 80, 64, 50, 50, 70, 60, 60}, SC_SOUND | SC_RHYTHM | SC_STRUCT | SC_NOTES,
     70, 60, 100, 80, 70, 40, 90, 60, 80, 10, 50, SF_MICRO, MV(MV_RASTER)},
    {"CONCRETE DRIFT", "CONCRETE", {"Minimal: small changes", "that move over many bars"},
     {"Texture", "Motion", "Drift", "Stability"},
     {"A little to more timbre", "Still to moving steps", "Slow to faster change", "Stays near the groove"},
     {50, 40, 64, 20, 50, 40, 70, 90}, SC_SOUND | SC_RHYTHM | SC_NOTES,
     55, 30, 0, 50, 80, 70, 25, 10, 15, 10, 10, SF_LFO | SF_MUTATE, MV(MV_DRIFT)},
    {"RUST CIRCUIT", "RUST", {"Industrial machines: metal,", "drive, interlocking cycles"},
     {"Corrode", "Machine", "Malfunct", "Pulse"},
     {"Clean metal to corroded", "Regular to interlocked", "Steady to slips, bursts", "Keeps the main pulse"},
     {80, 70, 72, 40, 70, 60, 40, 70}, SC_SOUND | SC_RHYTHM | SC_STRUCT | SC_NOTES,
     75, 30, 40, 100, 40, 40, 30, 70, 80, 10, 40, SF_RINGS | SF_MICRO | SF_LINKS, MV(MV_RUST)},
    {"GLASS CATHEDRAL", "GLASS", {"Resonant, tuned, glassy", "percussion in one key"},
     {"Brilliance", "Harmony", "Shimmer", "Tuning"},
     {"Dark to crystalline", "One note to melodies", "Still to shimmering", "Keeps the key and pitch"},
     {80, 60, 56, 30, 80, 60, 60, 60}, SC_SOUND | SC_RHYTHM | SC_NOTES,
     50, 40, 0, 60, 50, 30, 10, 10, 10, 120, 20, SF_RINGS | SF_LFO | SF_SCALE | SF_PLATE, MV(MV_GLASS)},
    {"BIOFEEDBACK", "BIOFEED", {"Tracks that answer each", "other: links, ducking"},
     {"Coupling", "Response", "Adapt", "Freedom"},
     {"Loose to tight coupling", "Few to many answers", "Fixed to evolving", "Keeps tracks their own"},
     {60, 60, 64, 40, 70, 70, 60, 30}, SC_MOD | SC_FILTER | SC_RHYTHM | SC_STRUCT | SC_NOTES,
     60, 40, 20, 50, 70, 70, 20, 10, 30, 0, 70, SF_LINKS | SF_XMOD | SF_LFO | SF_MUTATE, MV(MV_BIO)},
    {"WHISPERING MOTES", "MOTES", {"Microsound, softly: dust,", "grains and a hushed kick"},
     {"Grain", "Scatter", "Whisper", "Anchors"},
     {"Soft dust to bright grain", "Sparse to scattered", "Still to shimmering", "Keeps kick and snare"},
     {80, 60, 52, 30, 50, 60, 60, 80}, SC_SOUND | SC_RHYTHM | SC_LEN | SC_NOTES,
     60, 50, 90, 70, 60, 30, 60, 30, 70, 20, 30, SF_MICRO | SF_LFO | SF_PLATE | SF_SOFT, MV(MV_MOTES)}};

/* a recipe: a spell and its settings, saved (bar key 3) into the project's GRIMOIRE */
typedef struct { uint8_t spell, tracks, k[NSPK]; uint16_t mask; uint32_t seed; } recipe_t;   /* (spell: + 1, 0 empty) */
static recipe_t rcp[NRECIPE] POOL;
static struct {
    uint8_t sel;                                     /* SPELL: 0.. the spells, NSPELL + r recipe r */
    uint8_t var, scope, mode;                        /* (scope: the preset last picked) */
    uint8_t k[NSPELL][NSPK];                         /* each spell's CAST and CHARACTER knobs */
    uint8_t tracks;                                  /* the targets (bit t) */
    uint16_t mask;                                   /* what it may change (SC_*: the white keys) */
    uint16_t wdown;                                  /* (the white keys pressed on SPELLS) */
    uint8_t lim[NSPL];                               /* LIMITS: the most a spell sets (SPL_*) */
    uint8_t mot, mlen;                               /* MOTION (SPMO_*, SPML_*) */
    uint8_t pend, pend_p, pend_loads;                /* a cast to audition: its pattern, kit_loads then */
    uint8_t rolling, face;                           /* the die rolling; its face (1..12) */
    uint32_t roll_ms, seed, last;                    /* .. since; the seed it rolled, the last cast's */
    uint16_t nsnd, nstep;                            /* what the last cast changed */
} sp POOL;
static uint32_t sp_x;                                /* the cast's random numbers (xorshift32 from its seed) */

static uint32_t sp_rnd(uint32_t n)                   /* 0 .. n - 1 */
{
    sp_x ^= sp_x << 13;
    sp_x ^= sp_x >> 17;
    sp_x ^= sp_x << 5;
    return n ? (sp_x >> 4) % n : 0u;
}
static int32_t sp_pm(int32_t a)                      /* -a .. a */
{
    return a > 0 ? (int32_t)sp_rnd(2u * (uint32_t)a + 1u) - a : 0;
}

static void sp_defaults(void)
{
    uint32_t i;
    memset(&sp, 0, sizeof sp);
    memset(rcp, 0, sizeof rcp);
    for (i = 0; i < NSPELL; i++)
        memcpy(sp.k[i], SPELL[i].def, NSPK);
    sp.tracks = (uint8_t)((1u << NTRACK) - 1u);
    sp.mask = SC_ALL;
    memcpy(sp.lim, SP_LIM0, NSPL);
    sp.mot = SPMO_ONE;
    sp.mlen = SPML_EVEN;
    sp.last = 0x2545F491u;
}
static int sp_is_rcp(void) { return sp.sel >= NSPELL; }
static uint32_t sp_spell(void) { return sp_is_rcp() ? rcp[sp.sel - NSPELL].spell - 1u : sp.sel; }
static uint8_t *sp_knobs(void) { return sp_is_rcp() ? rcp[sp.sel - NSPELL].k : sp.k[sp.sel]; }
static int sp_castable(void) { return sp.tracks && !sp.rolling; }

/* ------------------------------------------------------------- sound --- */
typedef struct { uint16_t cats; uint8_t sm, pm, dens, chaos, c[4]; uint8_t all, mot; } spc_t;   /* a cast's settings
                                                                     * (mot: MOTION applies) */

static void sp_par(uint32_t p, int32_t v)            /* a sound parameter, in its range (CONFIG MORPH: the target too) */
{
    int32_t lo = (int32_t)vp_min(p), hi = (int32_t)vp_max(p);
    v = clamp32(v, lo, hi);
    if (v == (int32_t)par[p])
        return;
    morph_follow(p, v - (int32_t)par[p]);
    par[p] = (uint8_t)v;
    sp.nsnd++;
}
/* D2 a floor tom, D3 a rack tom above it (COARSE: the note); whatever the spell, their pitch stays there */
static const int32_t TOM[2][2] = {{34, 48}, {40, 56}};
static void sp_move(const smove_t *m, uint32_t t, uint32_t v, const spc_t *c)
{
    uint32_t p = sp_role_par(m->r, v), amt = c->sm, w = (m->f & 3u) + 1u, str;
    int32_t cur, lo = m->lo, hi = m->hi, tg, mn, mx;
    if (!p || !(c->cats & sp_role_cat(m->r)) || par_unheard(t, p) || (v == 0u && sp_kick_role(m->r)))
        return;
    if (m->r == R_COARSE && (v == 1u || v == 2u) && !(m->f & MF_NEAR)) {   /* (D2, D3: the toms' registers) */
        lo = TOM[v - 1u][0] + lo * (TOM[v - 1u][1] - TOM[v - 1u][0]) / 127;
        hi = TOM[v - 1u][0] + hi * (TOM[v - 1u][1] - TOM[v - 1u][0]) / 127;
    }
    if (m->r == R_RVS || m->r == R_FXS) {            /* (the sends: within LIMITS, 0 .. the most) */
        uint32_t l = sp.lim[m->r == R_RVS ? SPL_RVS : SPL_FXS];
        lo = lo * (int32_t)l / 127;
        hi = hi * (int32_t)l / 127;
    }
    if (m->f & MF_EVO)                               /* (random per hit and the like: EVOLUTION's) */
        amt = amt * c->c[2] >> 7;
    if (m->f & MF_PROT)                              /* (pitch, decay, the identity: PROTECTION keeps them) */
        amt = amt * (127u - c->c[3]) >> 7;
    if (v == 0u && (m->r == R_COARSE || m->r == R_MODAMT) && c->c[3] > 32u)   /* (the kick's pitch, more so) */
        amt >>= 2;
    if (!amt || sp_rnd(128) >= 16u + (amt * w >> 2))
        return;
    cur = par[p];
    if (m->f & (MF_SET | MF_LIST)) {                 /* (a switch: another of its values) */
        uint32_t n = m->f & MF_LIST ? m->hi : (uint32_t)(hi - lo + 1), i;
        i = m->f & MF_C1 ? clamp32((int32_t)(c->c[0] * n >> 7) + sp_pm((int32_t)(c->chaos >> 5)), 0, (int32_t)n - 1)
                         : sp_rnd(n);
        sp_par(p, m->f & MF_LIST ? SP_LIST[m->lo][i] : lo + (int32_t)i);
        return;
    }
    mn = (int32_t)vp_min(p);
    mx = (int32_t)vp_max(p);
    if (m->f & MF_NEAR) {                            /* (a drift: near where it is, more with the first knob) */
        int32_t d = (hi * (int32_t)(32u + c->c[0]) >> 7) * (int32_t)(32u + c->sm) / 128;
        sp_par(p, cur + sp_pm(d + 1) + sp_pm((int32_t)(c->chaos >> 4)));
        return;
    }
    tg = m->f & MF_C1 ? lo + (hi - lo) * (int32_t)c->c[0] / 127 : lo + (int32_t)sp_rnd((uint32_t)(hi > lo ? hi - lo : lo - hi) + 1u) * (hi > lo ? 1 : -1);
    tg += sp_pm((hi > lo ? hi - lo : lo - hi) / 2 + (mx - mn) * (int32_t)c->chaos / 256);   /* (each cast its own) */
    if (c->chaos < 96u)                              /* (CHAOS low: in the spell's range) */
        tg = clamp32(tg, lo < hi ? lo : hi, lo < hi ? hi : lo);
    str = 40u + (amt * 3u >> 2) + sp_rnd(24);
    sp_par(p, cur + (tg - cur) * (int32_t)(str > 127u ? 127u : str) / 127);
}
/* the safeguards: resonance and drive bounded together, a short decay for a hard FM, the bits and the levels */
static void sp_safe(uint32_t v, const spc_t *c)
{
    uint32_t r = PAR_RESO_1 + v, fd = PAR_FILTER_DRIVE_1 + v, d = PAR_DRIVE1 + v, e = sp_role_par(R_DEC, v);
    uint32_t f = sp_role_par(R_FMAMT, v);
    if (c->chaos < 112u && par[r] > 112u)
        par[r] = 112;
    if (par[r] > 96u && par[fd] > 90u)
        par[fd] = 90;
    if (par[r] > 96u && par[d] > 96u)
        par[d] = 96;
    if (f && par[f] > 110u && par[e] > 100u)
        par[e] = 100;
    if (c->chaos < 100u && par[PAR_BITS1 + v] > 12u)
        par[PAR_BITS1 + v] = 12;
    if (v <= 2u && (c->cats & SC_ENV) && par[PAR_MODAMNT1 + v] > 30u && par[PAR_MOD_EG1 + v] > 72u)
        par[PAR_MOD_EG1 + v] = 72;                   /* (D1-D3: a deep pitch drop, not a long slow moan) */
    {                                                /* (a band, a peak or a comb: not whistling) */
        uint32_t f = par[PAR_FILTER_TYPE_1 + v] + 1u;
        if (c->chaos < 112u && par[r] > 88u && f != F_LP && f != F_LP2 && f != F_LP4 && f != F_OFF && f != F_DUB)
            par[r] = 88;
    }
    if (v == 0u && par[r] > 72u)                     /* (the kick: no ringing filter, little random pitch) */
        par[r] = 72;
    if (v == 0u && par[PAR_RND_PITCH1] > 16u)
        par[PAR_RND_PITCH1] = 16;
}
/* the models: Rings' and Plaits' (D1-D3, as many as play: model_cap) or MICRO's, each with its knobs set */
static uint32_t sp_models(uint32_t skip)             /* the Rings or Plaits models on D1-D3 (but voice skip's) */
{
    uint32_t v, n = 0;
    for (v = 0; v < 3u; v++)
        n += v != skip && par[PAR_MODEL1 + v] && par[PAR_MODEL1 + v] < MD_MC0;
    return n;
}
static void sp_model(uint32_t v, uint32_t md)
{
    static const uint8_t D[6] = {64, 64, 32, 40, 0, 0};
    uint32_t i;
    if (!model_ok(v, md) || (md && md < MD_MC0 && (v > 2u || sp_models(v) >= model_cap)) || par[PAR_MODEL1 + v] == md)
        return;
    sp_par(PAR_MODEL1 + v, (int32_t)md);
    if (md >= MD_MC0 || md <= MD_FM)                 /* (a fresh model's knobs: as the init kit has them) */
        for (i = 0; i < 6u; i++)
            par[PAR_MDECAY1 + 6u * i + v] = md >= MD_MC0 && i == 0u ? 20u : D[i];
}
static void sp_models_cast(const spell_t *S, uint32_t v, const spc_t *c)
{
    uint32_t want = S == &SPELL[SP_MOTES] ? 128u : c->sm * (64u + c->c[0]) >> 7, md = par[PAR_MODEL1 + v];
    if (!(c->cats & SC_OSC) || sp_rnd(128) >= want || v == 0u)   /* (D1: sp_kick's) */
        return;
    if (S == &SPELL[SP_MOTES]) {                     /* (MOTES: MICRO's quiet ones on every voice; the snare at
                                                      * times its own noise, hushed) */
        static const uint8_t MM[4][4] = {{6, 8, 9, 0}, {4, 5, 7, 6}, {7, 6, 1, 7}, {0, 1, 7, 3}};   /* (the toms:
            * GRAIN POP CHIRP CLICK; the snare: BURST SNAP DUST GRAIN; the cymbal: DUST GRAIN TRAIN; the hats: CLICK
            * TRAIN DUST BITS) */
        if (v == 3u && sp_rnd(2))
            sp_model(v, 0);
        else
            sp_model(v, MD_MC0 + MM[v <= 2u ? 0u : v - 2u][sp_rnd(4)]);
        return;
    }
    if ((S->fl & SF_MICRO) && (v >= 3u || !(S->fl & (SF_RINGS | SF_PLAITS)) || sp_rnd(3) == 0u) &&
        (v >= 3u || c->chaos > 64u || S == &SPELL[SP_RASTER])) {
        static const uint8_t MS[3][4] = {{3, 4, 0, 6}, {0, 6, 7, 8}, {2, 3, 5, 1}};   /* (BITS BURST CLICK GRAIN;
                                                                                       * CLICK GRAIN DUST POP; ..) */
        const uint8_t *l = MS[S == &SPELL[SP_RASTER] ? 1 : S == &SPELL[SP_RUST] ? 2 : 0];
        sp_model(v, MD_MC0 + l[sp_rnd(4)]);
        return;
    }
    if (v > 2u || (md && md < MD_MC0 && sp_rnd(2)))  /* (a model on already: often kept, its knobs moved) */
        return;
    if ((S->fl & SF_RINGS) && (c->chaos > 48u || S == &SPELL[SP_GLASS])) {
        static const uint8_t RG[3] = {MD_MODAL, MD_STRING, MD_FM};
        sp_model(v, S == &SPELL[SP_GLASS] ? RG[sp_rnd(2)] : S == &SPELL[SP_POLTER] ? (sp_rnd(2) ? MD_FM : MD_MODAL)
                                                                                 : RG[sp_rnd(3)]);
    } else if ((S->fl & SF_PLAITS) && c->chaos > 80u) {
        sp_model(v, MD_PL0 + sp_rnd(16));
    }
}
/* D1 stays a kick: the LXR's (a sine or triangle), Plaits' BASS DRUM or Rings' MODAL, low. Each spell has its kind
 * of kick: ranges (kit values) for the pitch, the decay, the pitch EG's amount and decay, the click (the transient's
 * level), drive, FM, the low-pass and the level. The click, drive, FM and filter follow the first CHARACTER knob
 * (lo at 0, hi at 127: either way round); the rest fall anywhere in their range. KE_NEAR: nudged (hi: how far) */
enum { KK_PIT, KK_DEC, KK_PAMT, KK_PDEC, KK_CLK, KK_DRV, KK_FM, KK_FLT, KK_VOL, NKK };
#define KE_PLAITS 1u
#define KE_RINGS 2u
#define KE_NEAR 4u
typedef struct { uint8_t r[NKK][2], eng; } kick_t;
static const kick_t KICK[NSPELL] = {
    {{{0, 2}, {0, 6}, {0, 12}, {0, 10}, {0, 14}, {0, 10}, {0, 8}, {0, 14}, {0, 0}}, KE_NEAR},   /* NONE */
    {{{26, 36}, {14, 40}, {50, 110}, {25, 60}, {40, 110}, {10, 60}, {20, 80}, {70, 127}, {110, 127}}, KE_RINGS},
    {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, KE_NEAR},       /* SKELETON: none */
    {{{28, 40}, {6, 22}, {60, 127}, {15, 45}, {70, 127}, {30, 90}, {0, 40}, {70, 127}, {115, 127}}, 0},
    {{{20, 28}, {30, 70}, {20, 60}, {40, 90}, {25, 0}, {20, 0}, {0, 10}, {60, 25}, {92, 108}}, KE_PLAITS},   /* DUB */
    {{{22, 34}, {20, 60}, {80, 127}, {30, 80}, {60, 127}, {60, 110}, {0, 60}, {70, 127}, {120, 127}},
     KE_PLAITS | KE_RINGS},
    {{{30, 42}, {4, 16}, {40, 100}, {10, 35}, {60, 127}, {0, 30}, {0, 20}, {80, 127}, {105, 125}}, 0},   /* RASTER */
    {{{0, 1}, {0, 5}, {0, 8}, {0, 8}, {0, 10}, {0, 6}, {0, 6}, {0, 10}, {0, 0}}, KE_NEAR},       /* DRIFT */
    {{{24, 34}, {12, 40}, {60, 120}, {20, 60}, {80, 127}, {70, 110}, {30, 90}, {60, 110}, {115, 127}}, KE_RINGS},
    {{{24, 32}, {25, 60}, {20, 60}, {40, 90}, {0, 40}, {0, 20}, {0, 30}, {60, 110}, {100, 120}}, KE_RINGS},   /* GLASS */
    {{{0, 1}, {0, 6}, {0, 8}, {0, 8}, {0, 12}, {0, 8}, {0, 8}, {0, 12}, {0, 0}}, KE_NEAR},       /* BIO */
    {{{26, 36}, {8, 26}, {20, 60}, {10, 30}, {0, 30}, {0, 8}, {0, 10}, {40, 80}, {84, 98}}, 0}};   /* MOTES: hushed */
static const uint8_t KK_LIM[NKK][2] = {{18, 44}, {4, 90}, {0, 127}, {8, 110}, {0, 127}, {0, 115}, {0, 90}, {20, 127},
                                       {80, 127}};
static const uint8_t KK_PAR[NKK] = {PAR_COARSE1, PAR_VELOD1, PAR_MODAMNT1, PAR_MOD_EG1, PAR_TRANS1_VOL, PAR_DRIVE1,
                                    PAR_FMAMNT1, PAR_FILTER_FREQ_1, PAR_VOL1};
static const uint16_t KK_CAT[NKK] = {SC_PITCH, SC_ENV, SC_ENV, SC_ENV, SC_TRANS, SC_DRIVE, SC_OSC, SC_FILTER, SC_ENV};
static int sp_kick_ok(uint32_t md) { return !md || md == MD_MODAL || md == MD_PL0 + PE_BD; }
static void sp_kick(const spell_t *S, const spc_t *c)
{
    const kick_t *K = &KICK[S - SPELL];
    uint32_t k, md = par[PAR_MODEL1], amt = c->sm;
    for (k = 0; k < NKK; k++) {                      /* (the kick's knobs) */
        int32_t lo = K->r[k][0], hi = K->r[k][1], cur = par[KK_PAR[k]], tg;
        uint32_t a = k <= KK_DEC ? amt * (127u - c->c[3]) >> 7 : amt;   /* (PROTECTION keeps pitch and decay) */
        if (!(c->cats & KK_CAT[k]))
            continue;
        if (a && sp_rnd(128) < 16u + (a * 3u >> 2) && (lo || hi)) {
            if (K->eng & KE_NEAR) {
                int32_t d = (hi * (int32_t)(32u + c->c[0]) >> 7) * (int32_t)(32u + a) / 128;
                tg = cur + sp_pm(d + 1);
            } else {
                int32_t w = hi > lo ? hi - lo : lo - hi;
                uint32_t str = 48u + (a * 3u >> 2) + sp_rnd(24);
                tg = k >= KK_CLK && k <= KK_FLT ? lo + (hi - lo) * (int32_t)c->c[0] / 127
                                                 : (lo < hi ? lo : hi) + (int32_t)sp_rnd((uint32_t)w + 1u);
                tg += sp_pm(w / 4 + (int32_t)(c->chaos >> 3));
                tg = cur + (tg - cur) * (int32_t)(str > 127u ? 127u : str) / 127;
            }
            sp_par(KK_PAR[k], tg);
        }
        if (c->chaos < 112u || k == KK_PIT || k == KK_VOL)   /* (a kick, whatever it was) */
            sp_par(KK_PAR[k], clamp32(par[KK_PAR[k]], KK_LIM[k][0], KK_LIM[k][1]));
    }
    if ((S->fl & SF_SOFT) && (c->cats & SC_ENV) && par[PAR_VOL1] > K->r[KK_VOL][1])   /* (a quiet kick, always) */
        sp_par(PAR_VOL1, K->r[KK_VOL][0] + (int32_t)sp_rnd((uint32_t)(K->r[KK_VOL][1] - K->r[KK_VOL][0]) + 1u));
    if (c->cats & SC_FILTER) {                       /* (a low-pass, never a high-pass, a band or a comb) */
        uint32_t f = par[PAR_FILTER_TYPE_1] + 1u;
        if (f != F_LP && f != F_LP2 && f != F_LP4 && f != F_OFF && f != F_DUB)
            sp_par(PAR_FILTER_TYPE_1, FK(F_LP));
    }
    if (!(c->cats & SC_OSC))
        return;
    if (!sp_kick_ok(md) || (!(K->eng & KE_NEAR) && sp_rnd(256) < (amt * (32u + c->c[0]) >> 7))) {   /* (the engine) */
        uint32_t n = 0;
        uint8_t e[3];
        e[n++] = MD_LXR;
        if ((K->eng & KE_PLAITS) || (!sp_kick_ok(md) && md >= MD_PL0 && md < MD_MC0))
            e[n++] = MD_PL0 + PE_BD;
        if ((K->eng & KE_RINGS) || (!sp_kick_ok(md) && md && md < MD_PL0))
            e[n++] = MD_MODAL;
        md = e[sp_rnd(n)];
        if (md && (sp_models(0) >= model_cap || !model_ok(0, md)))
            md = MD_LXR;
        if (md != par[PAR_MODEL1])
            sp_par(PAR_MODEL1, (int32_t)md);
    }
    md = par[PAR_MODEL1];
    if (md == MD_LXR) {                              /* (the LXR's: a sine or a triangle) */
        if (par[PAR_OSC_WAVE_DRUM1] > 1u)
            sp_par(PAR_OSC_WAVE_DRUM1, (int32_t)sp_rnd(2));
    } else if (md == MD_PL0 + PE_BD) {               /* (Plaits: HARMONICS the punch, TIMBRE the tone, MORPH decay) */
        sp_par(PAR_MDECAY1, 16 + (int32_t)(par[PAR_TRANS1_VOL] * 3u >> 2));
        sp_par(PAR_MDAMP1, (int32_t)par[PAR_FILTER_FREQ_1] * 3 / 4);
        sp_par(PAR_MMAT1, 30 + (int32_t)par[PAR_VELOD1]);
        sp_par(PAR_MTENS1, 0);
    } else if (md == MD_MODAL) {                     /* (Rings: low, damped, a membrane's structure, TENSION's drop) */
        sp_par(PAR_MDECAY1, 20 + (int32_t)par[PAR_VELOD1]);
        sp_par(PAR_MDAMP1, (int32_t)par[PAR_FILTER_FREQ_1] / 3);
        sp_par(PAR_MMAT1, (int32_t)sp_rnd(32));
        sp_par(PAR_MPOS1, 30 + (int32_t)sp_rnd(30));
        sp_par(PAR_MEXC1, 0);
        sp_par(PAR_MTENS1, (int32_t)par[PAR_MODAMNT1] / 3);
    }
}
/* the toms' pitch: somewhere in their register when it was outside, else (SOUND, less with PROTECTION) a few
 * semitones on, or anywhere in it (a nudging spell: a step or two) */
static void sp_tom(const spell_t *S, uint32_t v, const spc_t *c)
{
    uint32_t p = PAR_COARSE1 + 2u * v, a = c->sm * (127u - c->c[3]) >> 7;
    int32_t lo = TOM[v - 1u][0], hi = TOM[v - 1u][1], cur = par[p];
    if (!(c->cats & SC_PITCH))
        return;
    if (cur < lo || cur > hi)
        sp_par(p, lo + (int32_t)sp_rnd((uint32_t)(hi - lo) + 1u));
    else if (sp_rnd(128) < a)
        sp_par(p, clamp32(KICK[S - SPELL].eng & KE_NEAR ? cur + sp_pm(2)
                        : sp_rnd(3) ? cur + sp_pm(2 + (int32_t)(c->chaos >> 4)) : lo + (int32_t)sp_rnd((uint32_t)(hi - lo) + 1u),
                          lo, hi));
}
/* LFO 2 on the voice's filter (or its decay), slow and in time: EVOLUTION's movement, if LFO 2 isn't in use */
static uint32_t sp_target_of(uint32_t v, uint32_t p)
{
    uint32_t i;
    for (i = MOD_TARGET_VOICE[v][0]; i <= MOD_TARGET_VOICE[v][1]; i++)
        if (MOD_TARGETS[i][1] == p)
            return i;
    return 0;
}
static void sp_lfo(uint32_t v, const spc_t *c, int force)
{
    uint32_t a = c->sm * c->c[2] >> 7, tg;
    if (!(c->cats & SC_MOD) || (par[PAR_L2AMT1 + v] && !force) || sp_rnd(128) >= a)
        return;
    tg = sp_target_of(v, sp_rnd(3) ? PAR_FILTER_FREQ_1 + v : sp_role_par(R_DEC, v));
    if (!tg)
        return;
    sp_par(PAR_L2TGT1 + v, (int32_t)tg);
    sp_par(PAR_L2SYNC1 + v, (int32_t)(1u + sp_rnd(3)));   /* (4/1, 2/1 or 1/1) */
    sp_par(PAR_L2WAVE1 + v, (int32_t)(sp_rnd(3) == 0u ? 5u : sp_rnd(2)));   /* (SINE, TRI, RANDOM) */
    sp_par(PAR_L2MODE1 + v, LM_HOLD);
    sp_par(PAR_L2AMT1 + v, (int32_t)(8u + (c->c[2] * 40u >> 7)));
}
static void sp_xmod(uint32_t v, uint32_t tv, const spc_t *c)   /* voice v moved by voice tv (DUCK, FILTER, RING) */
{
    uint32_t a = c->sm * c->c[0] >> 7;
    if (!(c->cats & SC_MOD) || tv == v || sp_rnd(160) >= a)
        return;
    sp_par(PAR_XSRC1 + v, (int32_t)tv + 1);
    sp_par(PAR_XMODE1 + v, (int32_t)(c->chaos > 96u ? sp_rnd(NXMODE) : sp_rnd(2)));
    sp_par(PAR_XAMT1 + v, (int32_t)(30u + (c->c[0] * 80u >> 7)));
}
/* the shared effects: only with every track picked (they'd change the tracks left out too), or the delay as it is */
static void sp_fx(const spell_t *S, const spc_t *c)
{
    if (!(c->cats & SC_FX) || !c->sm)
        return;
    if ((S->fl & SF_DUB) && (c->all || par[PAR_FX_TYPE] == FX_DELAY)) {
        if (c->all) {
            sp_par(PAR_FX_TYPE, FX_DELAY);
            sp_par(PAR_FX_DELAY_TYPE, (int32_t)(DELAY_TAPE + sp_rnd(2)));
            sp_par(PAR_FX_DRY_WET, (int32_t)((44u + sp_rnd(30)) * sp.lim[SPL_FXMIX] / 127u));
        }
        sp_par(PAR_DSYNC, 1);
        sp_par(PAR_FX_P1, (int32_t)((2u * (6u + sp_rnd(3)) + 1u) * 127u / 24u));   /* (1/8 .. 1/4: dotted 8th) */
        sp_par(PAR_FX_P4, (int32_t)(60u + (c->c[1] * 34u >> 7)));                   /* (the feedback, < 95) */
        sp_par(PAR_FX_P3, (int32_t)(80u - (c->c[0] * 50u >> 7)));
        sp_par(PAR_DSAT, (int32_t)(30u + sp_rnd(50)));
        sp_par(PAR_ECHOREV, (int32_t)(20u + sp_rnd(40)));
        sp_par(PAR_GLITCH, 0);
        sp_par(PAR_DPITCH, 0);
        if (c->all) {
            sp_par(PAR_REV_TYPE, (int32_t)(sp_rnd(2) ? 2u : 3u));
            sp_par(PAR_REV_SIZE, (int32_t)(80u + sp_rnd(40)));
            sp_par(PAR_REV_DAMP, (int32_t)(60u + (c->c[0] * 40u >> 7)));
        }
    }
    if ((S->fl & SF_PLATE) && c->all) {
        sp_par(PAR_REV_TYPE, (int32_t)(sp_rnd(2) ? 2u : 3u));
        sp_par(PAR_REV_SIZE, (int32_t)(70u + sp_rnd(40)));
        sp_par(PAR_REV_DAMP, (int32_t)(50u - (c->c[0] * 40u >> 7)));
    }
    if ((S->fl & SF_FXALL) && c->all && sp_rnd(128) < c->sm) {
        static const uint8_t T[5] = {FX_DELAY, FX_PHASER, FX_FLANGER, FX_RING, FX_DRIVE};
        sp_par(PAR_FX_TYPE, T[sp_rnd(5)]);
        sp_par(PAR_FX_DRY_WET, (int32_t)((30u + sp_rnd(60)) * sp.lim[SPL_FXMIX] / 127u));
        sp_par(PAR_FX_P1, (int32_t)sp_rnd(128));
        sp_par(PAR_FX_P2, (int32_t)sp_rnd(128));
        sp_par(PAR_FX_P3, (int32_t)sp_rnd(128));
        sp_par(PAR_FX_P4, (int32_t)sp_rnd(96));      /* (feedback in each type but the drive's volume: bounded) */
        sp_par(PAR_MDIRT_AMT, (int32_t)sp_rnd(c->chaos > 80u ? 60u : 20u));
    }
}

/* ----------------------------------------------------------- pattern --- */
static uint32_t sp_metric(uint32_t t, uint32_t s)    /* a step's weight in the bar, for track t's part (0 .. 6) */
{
    uint32_t i = s & 15u, c = i == 0u ? 3u : (i & 3u) == 0u ? 2u : (i & 1u) == 0u ? 1u : 0u;
    switch (t) {
    case 0: return c + ((i & 3u) == 0u ? 2u : 0u);            /* (the kick: the beats) */
    case 3: return c + ((i & 7u) == 4u ? 3u : 0u);            /* (the snare: the backbeat) */
    case 4: return c + (i == 0u ? 2u : 0u);                    /* (the cymbal: the bar) */
    case 5: return c + ((i & 1u) == 0u ? 1u : 0u);            /* (the closed hat: the 8ths and 16ths) */
    case 6: return c + ((i & 3u) == 2u ? 3u : 0u);            /* (the open hat: the off-beats) */
    default: return c;
    }
}
static int sp_anchor(uint32_t t, uint32_t s)
{
    return (t == 0u && (s & 3u) == 0u) || (t == 3u && (s & 7u) == 4u) || ((s & 15u) == 0u && t < 3u);
}
static uint32_t sp_cond(uint32_t a, uint32_t b) { return COND_AB + (b - 1u) * b / 2u - 1u + a - 1u; }   /* A:B */
/* the track's length, clock and direction (0.19-test12: every spell, LENGTH, CLOCK in and PATTERN up: c->mot).
 * Lengths: the spell's own (STRC), from Lengths' list; BARS sets others to bars. MOTION: the tracks sp_motion picks
 * (sp_div) their own speed or way, ONE and FEW in time (x2, 1/2, 3/2 ..; backwards, ping-pong), MANY any; FREE any
 * track may; the others back forward at the pattern's speed, whatever an earlier cast did. PROTECTION up keeps the
 * kick and snare from going their own way (not from coming back) */
static uint8_t sp_div;
static int sp_prot(uint32_t t, const spc_t *c) { return (t == 0u || t == 3u) && c->c[3] > 64u; }
static void sp_motion(const spell_t *S, const pattern_t *p, const spc_t *c)
{
    uint8_t l[NTRACK];
    uint32_t t, n = 0, want, i;
    (void)S;
    (void)p;
    sp_div = 0;
    if (!c->mot)
        return;
    for (t = 0; t < NTRACK; t++)
        if (((sp.tracks >> t) & 1u) && !sp_prot(t, c))
            l[n++] = (uint8_t)t;
    want = sp.mot == SPMO_ONE ? 1u : sp.mot == SPMO_FEW ? 2u : sp.mot == SPMO_MANY ? (n + 1u) / 2u + (n > 4u) : 0u;
    for (i = 0; i < want && i < n; i++) {            /* (want of them, at random) */
        uint32_t j = i + sp_rnd(n - i);
        uint8_t x = l[i];
        l[i] = l[j];
        l[j] = x;
        sp_div |= (uint8_t)(1u << l[i]);
    }
}
static void sp_struct(const spell_t *S, pattern_t *p, uint32_t t, const spc_t *c)
{
    static const uint8_t LEN[NSPML][8] = {{16, 32, 16, 32, 64, 16, 32, 64}, {16, 12, 24, 32, 16, 12, 24, 32},
                                          {16, 12, 24, 32, 15, 14, 20, 7}};
    uint32_t a = S->strc * c->pm >> 7, ck = p->clk[t], dr = p->dir[t], len = track_len(p, t);
    if (!c->mot)
        return;
    if (p->gen[t][0] != GEN_EUCLID) {                /* (a EUCLID track: its length is EUCLID's) */
        if ((c->cats & SC_LEN) && !sp_prot(t, c) && sp_rnd(160) < a)
            len = LEN[sp.mlen % NSPML][sp_rnd(8)];
        if (sp.mlen == SPML_BARS && (len & 15u))     /* (BARS: 16, 32 or 64 steps) */
            len = len <= 24u ? 16u : len <= 48u ? 32u : 64u;
        if (len != track_len(p, t)) {
            p->len[t] = (uint8_t)len;
            sp.nstep++;
        }
    }
    if (sp.mot >= SPMO_FREE) {                       /* (FREE: as it falls, more with CHAOS) */
        uint32_t f = (S->strc > 64u ? S->strc : 64u) * c->pm >> 7;
        f = f * (32u + c->chaos) >> 7;
        if (!sp_prot(t, c) && sp_rnd(200) < f) {
            static const uint8_t CK[7] = {CLK_1X, 4, 6, 7, 8, 2, 9};   /* (as written, 3/4, 4/3, 3/2, x2, 1/2, x3) */
            ck = CK[sp_rnd(7)];
            if (ck < CLK_1X && len * CLK_TICKS[ck] > 64u * CLK_TICKS[CLK_1X])
                ck = 8;
        }
        if (!sp_prot(t, c) && sp_rnd(260) < f) {
            static const uint8_t DR[5] = {DIR_FWD, DIR_REV, DIR_PING, DIR_PING2, DIR_RAND};
            dr = DR[sp_rnd(5)];
        }
    } else if ((sp_div >> t) & 1u) {                 /* (its own: a speed, or a way; MANY: either or both) */
        static const uint8_t CK[7] = {8, 2, 7, 4, 6, 3, 9};   /* (x2, 1/2, 3/2; 3/4, 4/3; 2/3, x3) */
        static const uint8_t DR[4] = {DIR_REV, DIR_PING, DIR_PING2, DIR_RAND};
        uint32_t nck = sp.mot == SPMO_ONE ? 3u : sp.mot == SPMO_FEW ? 5u : 7u, ndr = sp.mot == SPMO_MANY ? 4u : 2u;
        uint32_t k = sp.mot == SPMO_MANY ? sp_rnd(3) : sp_rnd(3) ? 0u : 1u;   /* (0 speed, 1 way, 2 both) */
        ck = CLK_1X;
        dr = DIR_FWD;
        if (k != 1u) {
            ck = CK[sp_rnd(nck)];
            if (ck < CLK_1X && len * CLK_TICKS[ck] > 64u * CLK_TICKS[CLK_1X])   /* (slower: within 4 bars) */
                ck = 8;
        }
        if (k != 0u)
            dr = DR[sp_rnd(ndr)];
    } else {                                         /* (the rest: with the pattern) */
        ck = CLK_1X;
        dr = DIR_FWD;
    }
    if (ck != p->clk[t]) {
        p->clk[t] = (uint8_t)ck;
        sp.nstep++;
    }
    if (dr != p->dir[t]) {
        p->dir[t] = (uint8_t)dr;
        sp.nstep++;
    }
}
static void sp_track(const spell_t *S, pattern_t *p, uint32_t t, const spc_t *c, uint32_t *nrat_all)
{
    uint8_t on0[NUM_STEPS], on1[NUM_STEPS];
    uint16_t sc[NUM_STEPS];
    uint32_t len, s, n0 = 0, n, i, base, note = SEQ_DEFAULT_NOTE, nrat = 0, cmax;
    uint32_t sync = S->sync * c->c[1] >> 7, clus = S->clus * c->c[1] >> 7, ctr[4], nc;
    sp_struct(S, p, t, c);
    len = track_len(p, t);
    for (s = 0; s < len; s++) {
        on0[s] = (uint8_t)main_on(p, t, s);
        n0 += on0[s];
        if (on0[s] && note == SEQ_DEFAULT_NOTE)
            note = p->step[t][s].note;
        nrat += on0[s] && (p->rat[t][s] & 7u);
    }
    if (!(c->cats & SC_RHYTHM))
        goto notes;
    if (!(c->cats & SC_HITS)) {                      /* (HITS left out: the same hits, the rest of the rhythm moved) */
        memcpy(on1, on0, len);
        goto steps;
    }
    /* how many hits: DENSITY 64 as many as now (an empty track: a quarter of its steps), below fewer, above more
     * up to the spell's FILL */
    base = n0 ? n0 : (len + 3u) / 4u;
    if (c->dens <= 64u) {
        n = (base * c->dens + 32u) >> 6;
        if (!n && n0 && c->dens)
            n = 1;
    } else {
        uint32_t cap = len * S->fill >> 7;
        cap = cap < base ? base : cap;
        n = base + (cap - base) * (c->dens - 64u) / 63u;
    }
    if (!n0 && !c->pm && c->dens <= 64u)             /* (an empty track, PATTERN 0, DENSITY as is: as it is) */
        n = 0;
    /* each step's claim: its place in the bar (less with CHAOS), off-beats (SYNC), clusters, chance (CHAOS), being
     * on already (less with PATTERN), being an anchor (PROTECTION) */
    nc = 1u + len / 16u;
    for (i = 0; i < nc && i < 4u; i++)
        ctr[i] = sp_rnd(len);
    for (s = 0; s < len; s++) {
        uint32_t m = sp_metric(t, s);
        int32_t v = (int32_t)(m * (140u - (c->chaos >> 1)));
        if (s & 1u)
            v += (int32_t)sync;
        else if (!(s & 3u))
            v -= (int32_t)(sync >> 2);
        for (i = 0; i < nc && i < 4u; i++) {
            uint32_t d = s > ctr[i] ? s - ctr[i] : ctr[i] - s;
            if (d < 3u)
                v += (int32_t)(clus * (3u - d) * 2u);
        }
        v += (int32_t)sp_rnd(c->chaos * 4u + 24u);
        if (on0[s])
            v += (int32_t)((127u - c->pm) * 12u + 64u);
        if (on0[s] && sp_anchor(t, s))
            v += (int32_t)(c->c[3] * 16u);
        sc[s] = (uint16_t)clamp32(v + 1, 1, 65535);
        on1[s] = 0;
    }
    for (i = 0; i < n && i < len; i++) {             /* (the n best) */
        uint32_t b = 0, bv = 0;
        for (s = 0; s < len; s++)
            if (!on1[s] && sc[s] > bv) {
                bv = sc[s];
                b = s;
            }
        on1[b] = 1;
    }
steps:
    cmax = S->rat > 100u ? len / 3u : len / 4u;      /* (the ratchets a track may have) */
    for (s = 0; s < len; s++) {
        step_t *x = &p->step[t][s];
        uint32_t m = sp_metric(t, s), fresh = on1[s] && !on0[s];
        if (on0[s] != on1[s]) {
            main_set(p, t, s, on1[s]);
            sp.nstep++;
        }
        if (fresh) {                                 /* (a new hit: the track's note, plain) */
            x->note = (uint8_t)note;
            x->prob = (uint8_t)((x->prob & ~PROB_MASK) | 100u);
            x->volume = (uint8_t)((x->volume & STEP_ACTIVE_MASK) |
                                  clamp32((int32_t)(S->fl & SF_SOFT ? 40u + m * 8u : 64u + m * 10u) + sp_pm(10), 20, 127));
            lock_set(p, t, s, 0, NO_AUTOMATION);
            lock_set(p, t, s, 1, NO_AUTOMATION);
            x->p1v = x->p2v = 0;
            p->cond[t][s] = 0;
            if (p->rat[t][s] & 7u)
                nrat--;
            p->rat[t][s] = 0;
            p->timing[t][s] = 7u << 4;
        }
        if (!on1[s] || (!fresh && sp_rnd(128) >= c->pm))
            continue;
        if ((c->cats & SC_ACC) && S->vel && sp_rnd(128) < S->vel) {        /* (the accents: by the bar, with SKELETON's contrast) */
            uint32_t k = S->fl & SF_C1PAT ? c->c[0] : 64u, vel = x->volume & STEP_VOLUME_MASK;
            int32_t tg = (int32_t)(127u - (6u - (m > 6u ? 6u : m)) * (6u + (k * 12u >> 7)));
            if (S->fl & SF_SOFT)
                tg = tg * 3 / 4;
            tg += sp_pm((int32_t)(c->chaos >> 3));
            x->volume = (uint8_t)((x->volume & STEP_ACTIVE_MASK) | clamp32(((int32_t)vel + 3 * tg) / 4, 10, 127));
        }
        if ((c->cats & SC_CHANCE) && m < 2u && !sp_anchor(t, s) && sp_rnd(256) < (S->prob * c->c[2] >> 7)) {
            x->prob = (uint8_t)((x->prob & ~PROB_MASK) | (35u + sp_rnd(60)));
            sp.nstep++;
        }
        if ((c->cats & SC_CHANCE) && m < 3u && !sp_anchor(t, s) && sp_rnd(320) < (S->cond * c->c[2] >> 7)) {
            static const uint8_t AB[6][2] = {{1, 2}, {2, 2}, {1, 4}, {3, 4}, {2, 4}, {4, 8}};
            uint32_t r = sp_rnd(c->chaos > 90u ? 8u : 6u);
            p->cond[t][s] = (uint8_t)((p->cond[t][s] & ~COND_MASK) | (r >= 6u ? (r == 6u ? COND_FILL : COND_NPRE)
                                                                    : sp_cond(AB[r][0], AB[r][1])));
            sp.nstep++;
        }
        if ((c->cats & SC_TIME) && sp_rnd(256) < (S->shift * c->c[1] >> 7) && !(sp_anchor(t, s) && c->c[3] > 64u)) {
            int32_t d = 1 + (int32_t)(c->chaos >> 5) + (S->shift > 80u ? 2 : 0);
            int32_t sh = clamp32(7 + sp_pm(d), 0, 14);
            p->timing[t][s] = (uint8_t)((p->timing[t][s] & 0x0Fu) | (uint32_t)sh << 4);
            sp.nstep++;
        }
        if ((c->cats & SC_TIME) && sp_rnd(400) < (S->flam * c->c[1] >> 7)) {
            p->timing[t][s] = (uint8_t)((p->timing[t][s] & 0xF0u) | (1u + sp_rnd(3)));
            sp.nstep++;
        }
        if ((c->cats & SC_RAT) && !(p->rat[t][s] & RAT_BOUNCE) && !(p->rat[t][s] & 7u) && nrat < cmax &&
            sp_rnd(320) < (S->rat * c->c[1] >> 7)) {
            uint32_t h = 1u + sp_rnd(1u + (c->chaos >> 5));
            p->rat[t][s] = (uint8_t)((h > 7u ? 7u : h) | sp_rnd(4) << 3 | sp_rnd(3) << 5);
            nrat++;
            sp.nstep++;
        }
    }
    *nrat_all += nrat;
notes:
    if ((c->cats & SC_NOTES) && S->note && c->pm) {  /* (the notes: in the pattern's scale, a spread by the rhythm
                                                      * knob for GLASS, a little for the others) */
        uint32_t sc0 = p->scale < NSCALE ? p->scale : SC_CHROM, r = p->sroot % 12u;
        int32_t sp_ = 1 + (int32_t)(S->note * c->c[1] >> 11);
        if (sc0 == SC_CHROM)
            sc0 = SC_MINPENT;
        if ((c->c[3] > 96u && S->note < 100u) || (t == 0u && c->c[3] > 48u))
            return;
        for (s = 0; s < len; s++)
            if (main_on(p, t, s) && sp_rnd(256) < (S->note * c->pm >> 7)) {
                int32_t nn = scale_q(sc0, r, (int32_t)note, sp_pm(sp_));
                p->step[t][s].note = (uint8_t)clamp32(nn, 24, 108);
                sp.nstep++;
            }
    }
}
/* the melody generator on the toms (D2, D3), in the pattern's scale (a spell's own when it has none), round the
 * voice's tune: each spell its kind (shape, DEJA VU, SPREAD, STEPS: how many of the scale's notes) and how often */
typedef struct { uint8_t chance, shape, deja, sp_lo, sp_hi, steps, bias; } smel_t;
static const smel_t SP_MEL[NSPELL] = {
    {50, MEL_WALK, 110, 16, 40, 60, 64},             /* NONE: a slow walk */
    {40, MEL_RANDOM, 80, 20, 50, 40, 64},            /* POLTERGEIST */
    {0, MEL_OFF, 0, 0, 0, 0, 64},                    /* SKELETON */
    {60, MEL_RANDOM, 30, 30, 60, 20, 64},            /* GLITCH: notes everywhere */
    {70, MEL_WALK, 100, 16, 36, 90, 52},             /* DUB: roots and fifths, low */
    {80, MEL_MOTIF, 60, 30, 64, 30, 64},             /* RITUAL */
    {40, MEL_RANDOM, 60, 20, 50, 60, 64},            /* RASTER */
    {50, MEL_WALK, 120, 12, 30, 60, 64},             /* DRIFT: barely moving */
    {50, MEL_ARP_UD, 90, 20, 44, 90, 58},            /* RUST: machine arpeggios */
    {110, MEL_ARP_UD, 90, 30, 60, 30, 70},           /* GLASS: chimes */
    {50, MEL_WALK, 100, 16, 40, 60, 64},             /* BIOFEEDBACK */
    {30, MEL_RANDOM, 90, 16, 40, 40, 72}};           /* MOTES: a few high plinks */
static void sp_melody(const spell_t *S, pattern_t *p, const spc_t *c)
{
    static const uint8_t SCL[6] = {SC_MINPENT, SC_MAJPENT, SC_DORIAN, SC_MINOR, SC_HIRA, SC_PELOG};
    static const uint8_t LOOP[4] = {4, 8, 8, 16};
    const smel_t *M = &SP_MEL[S - SPELL];
    uint32_t t;
    if (!(c->cats & SC_NOTES) || !M->chance || !c->pm)
        return;
    for (t = 1; t < 3u; t++) {
        uint8_t *g = p->mel[t];
        if (!((sp.tracks >> t) & 1u) || sp_rnd(128) >= (M->chance * (64u + c->pm) >> 7))
            continue;
        if (!p->scale || p->scale >= NSCALE) {
            p->scale = S == &SPELL[SP_DUB] ? SC_MINPENT : SCL[sp_rnd(c->chaos > 64u ? 6u : 4u)];
            sp.nstep++;
        }
        p->sfollow |= (uint8_t)(1u << t);
        g[ML_SHAPE] = (uint8_t)(c->chaos > 96u ? 1u + sp_rnd(NMEL - 1u) : M->shape);
        g[ML_SPREAD] = (uint8_t)(M->sp_lo + sp_rnd((uint32_t)(M->sp_hi - M->sp_lo) + 1u) * c->c[1] / 127u);
        g[ML_BIAS] = (uint8_t)clamp32((int32_t)M->bias + sp_pm(6), 40, 88);
        g[ML_STEPS] = (uint8_t)clamp32((int32_t)M->steps + sp_pm(20), 0, 127);
        g[ML_DEJA] = (uint8_t)clamp32((int32_t)M->deja + (int32_t)(c->c[3] >> 3) - (int32_t)(c->chaos >> 3), 0, 127);
        g[ML_LOOP] = LOOP[sp_rnd(4)];
        g[ML_LEN] = LEN_AUTO;
        g[ML_LVAR] = 0;
        sp.nstep++;
    }
}
/* LINKS: a few between the picked tracks, in an order (none back up it: no loops); those to them made again */
static void sp_links(const spell_t *S, pattern_t *p, const spc_t *c)
{
    uint8_t ord[NTRACK];
    uint32_t i, j, n = 0, want, l;
    if (!(S->fl & SF_LINKS) || !(c->cats & SC_LINKS) || !c->pm)
        return;
    for (i = 0; i < NTRACK; i++)
        if ((sp.tracks >> i) & 1u)
            ord[n++] = (uint8_t)i;
    if (n < 2u)
        return;
    for (i = n - 1u; i > 0u; i--) {                  /* (a random order) */
        uint8_t x = ord[i];
        j = sp_rnd(i + 1u);
        ord[i] = ord[j];
        ord[j] = x;
    }
    for (l = 0; l < NLINK; l++)                      /* (the links into a picked track: made again) */
        if ((p->link[l][1] & 15u) && ((sp.tracks >> (p->link[l][0] >> 3 & 7u)) & 1u)) {
            memset(p->link[l], 0, 4);
            sp.nstep++;
        }
    want = 1u + (c->c[1] * 3u >> 7);
    if (S == &SPELL[SP_BIO])
        want = want * (160u - c->c[3]) >> 7;
    for (i = 0; i < want; i++) {
        static const uint8_t EB[6] = {LE_DUCK, LE_ONLY, LE_TRIG, LE_VEL, LE_DECAY, LE_PITCH};
        static const uint8_t ER[3] = {LE_RAT, LE_TRIG, LE_SHIFT};
        uint32_t a = sp_rnd(n - 1u), b = a + 1u + sp_rnd(n - 1u - a), e;
        int32_t amt;
        for (l = 0; l < NLINK && (p->link[l][1] & 15u); l++)
            ;
        if (l >= NLINK)
            break;
        e = S == &SPELL[SP_RUST] ? ER[sp_rnd(3)] : EB[sp_rnd(6)];
        amt = LK_DEF[e] + sp_pm((LK_HI[e] - LK_LO[e]) * (int32_t)c->c[0] / 512);
        p->link[l][0] = (uint8_t)(ord[a] | ord[b] << 3 | sp_rnd(e == LE_ONLY || e == LE_TRIG ? 3u : 2u) << 6);
        p->link[l][1] = (uint8_t)(e | sp_rnd(3) << 4);
        p->link[l][2] = (uint8_t)(int8_t)clamp32(amt, LK_LO[e], LK_HI[e]);
        p->link[l][3] = 0;
        sp.nstep++;
    }
    if (!p->cdepth)
        p->cdepth = 127;
}

/* ------------------------------------------------------------- casts --- */
static void sp_cast(uint32_t seed)                   /* the spell on the targets (the pattern shown, the kit) */
{
    const spell_t *S = &SPELL[sp_spell() % NSPELL];
    pattern_t *p = &pat[sq.active];
    const uint8_t *k = sp_knobs();
    uint8_t kk[NSPK];
    spc_t c;
    uint32_t i, t, v, vdone = 0, nrat = 0, on = 0;
    sp_x = seed | 1u;
    for (i = 0; i < NSPK; i++)                       /* (VARIATION: the knobs moved, this cast only) */
        kk[i] = (uint8_t)clamp32((int32_t)k[i] + sp_pm((int32_t)sp.var / 2), 0, 127);
    c.cats = (uint16_t)(S->cats & sp.mask);
    c.sm = kk[SPK_SOUND];
    c.pm = kk[SPK_PAT];
    c.dens = kk[SPK_DENS];
    c.chaos = kk[SPK_CHAOS];
    memcpy(c.c, kk + SPK_C1, 4);
    c.all = sp.tracks == (1u << NTRACK) - 1u;
    c.mot = (uint8_t)((sp.mask & SC_LEN) && c.pm);   /* (MOTION: any spell) */
    if (!c.sm)
        c.cats &= (uint16_t)~SC_SOUND;
    sp.nsnd = sp.nstep = 0;
    if ((S->fl & SF_SCALE) && (c.cats & SC_NOTES) && c.pm && (!p->scale || p->scale >= NSCALE)) {
        static const uint8_t SCL[6] = {SC_MINPENT, SC_MAJPENT, SC_DORIAN, SC_LYD, SC_HIRA, SC_PELOG};
        p->scale = SCL[sp_rnd(6)];
        p->sfollow |= sp.tracks & 7u;
        sp.nstep++;
    }
    for (t = 0; t < NTRACK; t++) {                   /* the voices (the hats' once) */
        if (!((sp.tracks >> t) & 1u) || ((vdone >> (v = track_voice(t))) & 1u))
            continue;
        vdone |= 1u << v;
        if (c.cats & SC_SOUND) {
            if (v == 0u)                             /* (D1: a kick, the spell's kind) */
                sp_kick(S, &c);
            if (S->fl & (SF_RINGS | SF_PLAITS | SF_MICRO))
                sp_models_cast(S, v, &c);
            for (i = 0; i < S->nmv; i++)
                sp_move(&S->mv[i], t, v, &c);
            if (S->fl & SF_LFO)
                sp_lfo(v, &c, S == &SPELL[SP_RITUAL]);
            if (S->fl & SF_XMOD) {
                uint32_t u = sp_rnd(NTRACK);
                if ((sp.tracks >> u) & 1u)
                    sp_xmod(v, track_voice(u), &c);
            }
            if (v == 0u && (S->fl & SF_DUB) && c.c[3] > 32u) {   /* (HAUNTED DUB's dry kick) */
                par[PAR_REV_SEND1] = (uint8_t)(par[PAR_REV_SEND1] * (127u - c.c[3]) >> 7);
                par[PAR_FX_SEND1] = (uint8_t)(par[PAR_FX_SEND1] * (127u - c.c[3]) >> 7);
            }
            if (v == 1u || v == 2u)
                sp_tom(S, v, &c);
            sp_safe(v, &c);
        }
    }
    if ((c.cats & SC_PITCH) && (sp.tracks & 6u) && par[PAR_COARSE3] < par[PAR_COARSE2] + 3u)   /* (D3 above D2) */
        sp_par(PAR_COARSE3, clamp32((int32_t)par[PAR_COARSE2] + 3 + (int32_t)sp_rnd(5), TOM[1][0], TOM[1][1]));
    sp_fx(S, &c);
    if (c.cats & SC_FX) {                            /* (LIMITS: the targets' sends; with every track, the shared) */
        for (t = 0; t < NTRACK; t++)
            if ((sp.tracks >> t) & 1u) {
                v = track_voice(t);
                if (par[PAR_REV_SEND1 + v] > sp.lim[SPL_RVS])
                    sp_par(PAR_REV_SEND1 + v, sp.lim[SPL_RVS]);
                if (par[PAR_FX_SEND1 + v] > sp.lim[SPL_FXS])
                    sp_par(PAR_FX_SEND1 + v, sp.lim[SPL_FXS]);
            }
        if (c.all && par[PAR_REV_LEVEL] > sp.lim[SPL_RLEV])
            sp_par(PAR_REV_LEVEL, sp.lim[SPL_RLEV]);
        if (c.all && par[PAR_FX_DRY_WET] > sp.lim[SPL_FXMIX])
            sp_par(PAR_FX_DRY_WET, sp.lim[SPL_FXMIX]);
    }
    sp_motion(S, p, &c);
    for (t = 0; t < NTRACK; t++)                     /* the steps */
        if ((sp.tracks >> t) & 1u) {
            sp_track(S, p, t, &c, &nrat);
            if ((S->fl & SF_MUTATE) && (c.cats & SC_LINKS) && c.c[2] > 90u && p->gen[t][0] == GEN_OFF &&
                sp_rnd(128) < c.pm) {                /* (EVOLUTION up: MUTATE moves the steps, a little, bar by bar) */
                p->gen[t][0] = GEN_MUTATE;
                p->gen[t][1] = (uint8_t)(2u + ((c.c[2] - 90u) >> 2));
                p->gen[t][2] = p->gen[t][3] = 0;
                sp.nstep++;
            }
            if ((p->rat[t][0] & 7u) && (c.cats & SC_ENV) && c.sm)   /* (a ratchet on the bar's first: shorter) */
                par[sp_role_par(R_DEC, track_voice(t))] = (uint8_t)(par[sp_role_par(R_DEC, track_voice(t))] * 3u >> 2);
        }
    sp_links(S, p, &c);
    sp_melody(S, p, &c);
    for (t = 0; t < NTRACK; t++)                     /* (never all silent: the strongest first step back) */
        for (i = 0; i < track_len(p, t); i++)
            on += main_on(p, t, i);
    if (!on && (c.cats & SC_HITS)) {
        for (t = 0; t < NTRACK && !((sp.tracks >> t) & 1u); t++)
            ;
        if (t < NTRACK)
            main_set(p, t, 0, 1);
    }
    (void)nrat;
    snd_post_all();
    sq.gsync = (uint8_t)((1u << NTRACK) - 1u);
}

static void sp_snapshot(void)                        /* the state a cast starts from (REVERSE's) */
{
    uint32_t i;
    upat = pat[sq.active];
    undo_t = -1;                                     /* (upat is the spell's now: no track's UNDO, no DENSITY's) */
    dundo = 0;
    for (i = 0; i < KIT_LEN; i++) {
        ca_snap[i] = par[kit_par(i)];
        ca_msnap[i] = morph_par[i];
    }
    sp.pend = 1;
    sp.pend_p = sq.active;
    sp.pend_loads = kit_loads;
}
static int sp_pending(void)                          /* a cast REVERSE can still take back */
{
    return sp.pend && sp.pend_p == sq.active && sp.pend_loads == kit_loads && undo_t < 0 && !dundo;
}
static void sp_restore(void)
{
    uint32_t i;
    pat[sq.active] = upat;
    for (i = 0; i < KIT_LEN; i++) {
        par[kit_par(i)] = ca_snap[i];
        morph_par[i] = ca_msnap[i];
    }
    snd_post_all();
    sq.gsync = (uint8_t)((1u << NTRACK) - 1u);
}

/* --------------------------------------------------------------- keys --- */
static void sp_cast_key(void)                        /* OCT+: the die rolls (sp_service casts when it stops) */
{
    if (sp.rolling)
        return;
    if (!sp.tracks) {
        ui_message("PICK A TRACK FIRST (BLACK KEYS)");
        return;
    }
    if (sp.mode == SPM_RECALL && !sp_pending())      /* (SAME: the recipe's roll, or the last; again: anew) */
        sp.seed = sp_is_rcp() && rcp[sp.sel - NSPELL].seed ? rcp[sp.sel - NSPELL].seed : sp.last;
    else
        sp.seed = (sp.last * 1103515245u + 12345u) ^ (fm1_ms * 2654435761u) ^ (uint32_t)sq.ticks;
    sp.face = (uint8_t)(sp.seed % 12u + 1u);
    sp.rolling = 1;
    sp.roll_ms = fm1_ms;
    ui.force = 1;
}
static void sp_service(void)                         /* each ms: the roll's end; a cast kept when SPELLS is left */
{
    if (sp.pend && (!perf_spells() || !sp_pending()))
        sp.pend = 0;
    if (sp.rolling && (!perf_spells() || fm1_ms - sp.roll_ms >= SP_ROLL_MS)) {
        char b[32];
        sp.rolling = 0;
        if (!perf_spells())
            return;
        if (sp_pending())
            sp_restore();
        else
            sp_snapshot();
        sp_cast(sp.seed);
        sp.last = sp.seed;
        put_str(b, SPELL[sp_spell() % NSPELL].card, sizeof b);
        put_str(b + str_len(b), sp.nsnd || sp.nstep ? " CAST (OCT-: UNDO)" : ": NOTHING CHANGED", sizeof b - str_len(b));
        ui_message(b);
    }
}
static int sp_reverse_key(void)                      /* OCT- alone: the cast taken back (1: there was one) */
{
    if (sp.rolling)
        return 1;
    if (!sp_pending()) {
        ui_message("NO SPELL TO REVERSE");
        return 0;
    }
    sp_restore();
    sp.pend = 0;
    ui_message("SPELL REVERSED");
    return 1;
}
static void sp_black(uint32_t t)                     /* a track key: the track a target (again: not) */
{
    char b[24];
    sp.tracks ^= (uint8_t)(1u << t);
    put_str(b, VOICE_SHORT[t], sizeof b);
    put_str(b + str_len(b), sp.tracks >> t & 1u ? ": A TARGET" : ": LEFT ALONE", sizeof b - str_len(b));
    ui_message(b);
    ui.force = 1;
}
static uint32_t sp_scope_of(uint16_t m)              /* the preset the white keys match (NSCOPE: none) */
{
    uint32_t i;
    for (i = 0; i < NSCOPE && SCOPE_CATS[i] != m; i++)
        ;
    return i;
}
static void sp_white(uint32_t w, int only)           /* a white key: that part in (again: out); SHIFT: only it */
{
    char b[32];
    sp.wdown |= (uint16_t)(1u << w);
    if (only)
        sp.mask = (uint16_t)(1u << w);
    else
        sp.mask ^= (uint16_t)(1u << w);
    put_str(b, SC_LONG[w], sizeof b);
    put_str(b + str_len(b), only ? " ONLY" : (sp.mask >> w) & 1u ? ": IN" : ": OUT", sizeof b - str_len(b));
    ui_message(b);
    ui.force = 1;
}
static int sp_white_up(uint32_t w)                   /* (1: the key was SPELLS') */
{
    if (!((sp.wdown >> w) & 1u))
        return 0;
    sp.wdown &= (uint16_t)~(1u << w);
    return 1;
}
static void sp_bar(uint32_t k)                       /* bar keys: 1 ALL / NONE, 2 KEEP, 3 SAVE, 4 DEFAULTS */
{
    uint32_t r;
    switch (k) {
    case 0:
        sp.tracks = (uint8_t)(sp.tracks == (1u << NTRACK) - 1u ? 0u : (1u << NTRACK) - 1u);
        ui_message(sp.tracks ? "ALL TRACKS" : "NO TRACKS");
        return;
    case 1:
        if (sp_pending()) {
            sp.pend = 0;
            ui_message("SPELL KEPT");
        } else {
            ui_message("NO SPELL TO KEEP");
        }
        return;
    case 2:
        r = sp_is_rcp() ? sp.sel - NSPELL : NRECIPE;
        if (r >= NRECIPE)
            for (r = 0; r < NRECIPE && rcp[r].spell; r++)
                ;
        if (r >= NRECIPE) {
            ui_message("GRIMOIRE FULL: PICK ONE TO SAVE OVER");
            return;
        }
        if (!sp_is_rcp())
            memcpy(rcp[r].k, sp.k[sp.sel], NSPK);
        rcp[r].spell = (uint8_t)(sp_spell() + 1u);
        rcp[r].mask = sp.mask;
        rcp[r].tracks = sp.tracks;
        rcp[r].seed = sp.last;
        sp.sel = (uint8_t)(NSPELL + r);
        {
            char b[24];
            put_str(b, "SAVED AS U", sizeof b);
            put_int(b + str_len(b), (int32_t)r + 1);
            ui_message(b);
        }
        return;
    case 3: {                                        /* (the knobs as at first) */
        char b[32];
        memcpy(sp_knobs(), SPELL[sp_spell() % NSPELL].def, NSPK);
        put_str(b, SPELL[sp_spell() % NSPELL].card, sizeof b);
        put_str(b + str_len(b), ": KNOBS AS AT FIRST", sizeof b - str_len(b));
        ui_message(b);
        ui.force = 1;
        return;
    }
    default:
        return;
    }
}

/* ------------------------------------------------------------ knobs --- */
static uint32_t sp_vget(uint32_t i)
{
    switch (i) {
    case SPV_SPELL: return sp.sel;
    case SPV_VAR: return sp.var;
    case SPV_SCOPE: return sp_scope_of(sp.mask) < NSCOPE ? sp_scope_of(sp.mask) : sp.scope;
    case SPV_MODE: return sp.mode;
    case SPV_MOT: return sp.mot;
    case SPV_MLEN: return sp.mlen;
    default: return i >= SPV_LIM ? sp.lim[(i - SPV_LIM) % NSPL] : sp_knobs()[(i - SPV_MODE - 1u) % NSPK];
    }
}
static uint32_t sp_vmax(uint32_t i)
{
    return i == SPV_SPELL ? NSPELL + NRECIPE - 1u : i == SPV_SCOPE ? NSCOPE - 1u : i == SPV_MODE ? NSPM - 1u
         : i == SPV_MOT ? NSPMO - 1u : i == SPV_MLEN ? NSPML - 1u : 127u;
}
static void sp_vset(uint32_t i, uint32_t v)
{
    switch (i) {
    case SPV_SPELL: {                                /* (past the spells: the recipes saved, the empty ones skipped) */
        int32_t d = v > sp.sel ? 1 : -1, n = (int32_t)v;
        while (n >= (int32_t)NSPELL && n < (int32_t)(NSPELL + NRECIPE) && !rcp[n - (int32_t)NSPELL].spell)
            n += d;
        if (n < 0 || n >= (int32_t)(NSPELL + NRECIPE))
            return;
        sp.sel = (uint8_t)n;
        if (sp_is_rcp()) {
            sp.tracks = rcp[n - (int32_t)NSPELL].tracks;
            sp.mask = rcp[n - (int32_t)NSPELL].mask;
        }
        return;
    }
    case SPV_VAR: sp.var = (uint8_t)v; return;
    case SPV_SCOPE:
        sp.scope = (uint8_t)(v % NSCOPE);
        sp.mask = SCOPE_CATS[sp.scope];
        return;
    case SPV_MODE: sp.mode = (uint8_t)v; return;
    case SPV_MOT: sp.mot = (uint8_t)(v < NSPMO ? v : SPMO_FREE); return;
    case SPV_MLEN: sp.mlen = (uint8_t)(v < NSPML ? v : SPML_ODD); return;
    default:
        if (i >= SPV_LIM)
            sp.lim[(i - SPV_LIM) % NSPL] = (uint8_t)v;
        else
            sp_knobs()[(i - SPV_MODE - 1u) % NSPK] = (uint8_t)v;
        return;
    }
}

/* ------------------------------------------------------------ project --- */
static void sp_write(void)                           /* KSP2 (project.c; 0.19-test6's KSP1 is skipped) */
{
    uint32_t i;
    pw_byte(7u + NSPELL0 * NSPK + NSPL + 2u + (NSPELL - NSPELL0) * NSPK);   /* (0.19-test11: MOTION and the spells
                                                                         * after NSPELL0 after LIMITS) */
    pw_byte(sp.sel);
    pw_byte(sp.var);
    pw_byte(sp.scope);
    pw_byte(sp.mode);
    pw_byte(sp.tracks);
    pw_byte(sp.mask & 0xFFu);
    pw_byte(sp.mask >> 8);
    for (i = 0; i < NSPELL0; i++)
        pw_bytes(sp.k[i], NSPK);
    pw_bytes(sp.lim, NSPL);
    pw_byte(sp.mot);
    pw_byte(sp.mlen);
    for (i = NSPELL0; i < NSPELL; i++)
        pw_bytes(sp.k[i], NSPK);
    pw_byte(NRECIPE);
    for (i = 0; i < NRECIPE; i++) {
        pw_byte(4u + NSPK + 4u);
        pw_byte(rcp[i].spell);
        pw_byte(rcp[i].tracks);
        pw_byte(rcp[i].mask & 0xFFu);
        pw_byte(rcp[i].mask >> 8);
        pw_bytes(rcp[i].k, NSPK);
        pw_byte(rcp[i].seed & 0xFFu);
        pw_byte(rcp[i].seed >> 8 & 0xFFu);
        pw_byte(rcp[i].seed >> 16 & 0xFFu);
        pw_byte(rcp[i].seed >> 24);
    }
}
static void sp_read(void)
{
    enum { B_LIM = 7u + NSPELL0 * NSPK, B_MOT = B_LIM + NSPL, B_K2 = B_MOT + 2u };
    uint8_t b[B_K2 + (NSPELL - NSPELL0) * NSPK];
    uint32_t n = pr_byte(), i, k;
    sp_defaults();
    memcpy(b + 7, sp.k, NSPELL0 * NSPK);
    memcpy(b + B_LIM, sp.lim, NSPL);                /* (none saved: as at first) */
    b[B_MOT] = sp.mot;
    b[B_MOT + 1u] = sp.mlen;
    memcpy(b + B_K2, sp.k[NSPELL0], (NSPELL - NSPELL0) * NSPK);
    memset(b, 0, 7);
    b[4] = sp.tracks;
    b[5] = b[6] = 0xFF;
    pr_bytes(b, n < sizeof b ? n : sizeof b);
    pr_skip(n > sizeof b ? n - sizeof b : 0u);
    sp.sel = (uint8_t)(b[0] < NSPELL + NRECIPE ? b[0] : 0u);
    sp.var = (uint8_t)(b[1] & 127u);
    sp.scope = (uint8_t)(b[2] % NSCOPE);
    sp.mode = (uint8_t)(b[3] % NSPM);
    sp.tracks = (uint8_t)(b[4] & ((1u << NTRACK) - 1u));
    sp.mask = (uint16_t)(b[5] | b[6] << 8);
    for (i = 0; i < NSPELL; i++)
        for (k = 0; k < NSPK; k++)
            sp.k[i][k] = (uint8_t)(b[(i < NSPELL0 ? 7u + i * NSPK : B_K2 + (i - NSPELL0) * NSPK) + k] & 127u);
    for (k = 0; k < NSPL; k++)
        sp.lim[k] = (uint8_t)(b[B_LIM + k] & 127u);
    sp.mot = (uint8_t)(b[B_MOT] < NSPMO ? b[B_MOT] : SPMO_ONE);
    sp.mlen = (uint8_t)(b[B_MOT + 1u] < NSPML ? b[B_MOT + 1u] : SPML_EVEN);
    n = pr_byte();
    for (i = 0; i < n; i++) {
        uint8_t r[4u + NSPK + 4u] = {0};
        uint32_t c = pr_byte();
        pr_bytes(r, c < sizeof r ? c : sizeof r);
        pr_skip(c > sizeof r ? c - sizeof r : 0u);
        if (i >= NRECIPE)
            continue;
        rcp[i].spell = (uint8_t)(r[0] <= NSPELL ? r[0] : 0u);
        rcp[i].tracks = (uint8_t)(r[1] & ((1u << NTRACK) - 1u));
        rcp[i].mask = (uint16_t)(r[2] | r[3] << 8);
        for (k = 0; k < NSPK; k++)
            rcp[i].k[k] = (uint8_t)(r[4u + k] & 127u);
        rcp[i].seed = (uint32_t)r[4u + NSPK] | (uint32_t)r[5u + NSPK] << 8 | (uint32_t)r[6u + NSPK] << 16 |
                      (uint32_t)r[7u + NSPK] << 24;
    }
    if (sp.sel >= NSPELL && !rcp[sp.sel - NSPELL].spell)
        sp.sel = 0;
}
