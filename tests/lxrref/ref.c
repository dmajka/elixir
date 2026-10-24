/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle tests: the LXR firmware's own float voices (Core/Src/Soundengine/DSPAudio, built for the host with the
 * shims in shim/), as the reference the fixed-point port is compared with (compare_test.c). ref_set follows
 * MidiParser.c midiParser_ccHandler for the voices' parameters. Copyright of the voices: (C) 2012-2026 Erica Synths,
 * GPL-3.0-only. */
#include <stdint.h>
#include <math.h>
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/SlopeEg2.h"
#include "../../firmware/src/lxr_params.h"

/* ---- what the voices call that the reference leaves out ---- */
ModulationNode velocityModulators[6];
void modNode_init(ModulationNode *vm) { (void)vm; }
void modNode_updateValue(ModulationNode *vm, float val) { (void)vm; (void)val; }
void lfo_init(Lfo *lfo) { (void)lfo; }
void lfo_retrigger(uint8_t voice) { (void)voice; }
uint16_t seq_getBpm(void) { return 120; }
static uint32_t ref_rng = 0x9E3779B9u;           /* the port's xorshift32, so the noise matches sample for sample */
uint32_t GetRngValue(void)
{
    uint32_t x = ref_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ref_rng = x;
    return x;
}
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h) { (void)h; return GetRngValue(); }
#define SEMITONE_UP 1.0594630943592952645618252949463f
float midiParser_calcDetune(uint8_t value) { return 1 + (value / 127.f - 0.5f) * (SEMITONE_UP - 1); }
static float shaper(float val, float shape)
{
    const float k = 2 * shape / (1.0001f - shape);
    return (1 + k) * val / (1 + k * fabsf(val));
}
extern float mixer_decimation_rate_ref[7];
float mixer_decimation_rate_ref[7] = {1, 1, 1, 1, 1, 1, 1};

static OscInfo *main_osc(int v)
{
    return v < 3 ? &voiceArray[v].osc : v == 3 ? &snareVoice.osc : v == 4 ? &cymbalVoice.osc : &hatVoice.osc;
}
static ResonantFilter *flt(int v)
{
    return v < 3 ? &voiceArray[v].filter : v == 3 ? &snareVoice.filter : v == 4 ? &cymbalVoice.filter : &hatVoice.filter;
}
static SlopeEg2 *aeg(int v)
{
    return v < 3 ? &voiceArray[v].oscVolEg : v == 3 ? &snareVoice.oscVolEg : v == 4 ? &cymbalVoice.oscVolEg : &hatVoice.oscVolEg;
}
static TransientGenerator *tg(int v)
{
    return v < 3 ? &voiceArray[v].transGen : v == 3 ? &snareVoice.transGen : v == 4 ? &cymbalVoice.transGen : &hatVoice.transGen;
}
static void coarse(OscInfo *o, uint8_t v) { o->midiFreq = (o->midiFreq & 0x00ff) | v << 8; osc_recalcFreq(o); }

void ref_set(uint32_t p, uint32_t itg)
{
    const float f = itg / 127.f;
    if (p >= PAR_COARSE1 && p <= PAR_FINE6) {
        OscInfo *o = main_osc((p - PAR_COARSE1) >> 1);
        if ((p - PAR_COARSE1) & 1) {
            o->midiFreq = (o->midiFreq & 0xff00) | itg;
            osc_recalcFreq(o);
        } else {
            coarse(o, itg);
        }
        return;
    }
    if (p >= PAR_FILTER_FREQ_1 && p <= PAR_FILTER_FREQ_6) { SVF_directSetFilterValue(flt(p - PAR_FILTER_FREQ_1), shaper(f, FILTER_SHAPER)); return; }
    if (p >= PAR_RESO_1 && p <= PAR_RESO_6) { SVF_setReso(flt(p - PAR_RESO_1), f); return; }
    if (p >= PAR_FILTER_DRIVE_1 && p <= PAR_FILTER_DRIVE_6) { SVF_setDrive(flt(p - PAR_FILTER_DRIVE_1), f); return; }
    if (p >= PAR_VOL_SLOPE1 && p <= PAR_VOL_SLOPE6) { slopeEg2_setSlope(aeg(p - PAR_VOL_SLOPE1), itg); return; }
    if (p >= PAR_TRANS1_VOL && p <= PAR_TRANS6_VOL) { tg(p - PAR_TRANS1_VOL)->volume = f; return; }
    if (p >= PAR_TRANS1_WAVE && p <= PAR_TRANS6_WAVE) { transient_setWaveform(tg(p - PAR_TRANS1_WAVE), itg); return; }
    if (p >= PAR_TRANS1_FREQ && p <= PAR_TRANS6_FREQ) { tg(p - PAR_TRANS1_FREQ)->pitch = 1.f + ((itg / 33.9f) - 0.75f); return; }
    if (p >= PAR_FILTER_TYPE_1 && p <= PAR_FILTER_TYPE_3) { voiceArray[p - PAR_FILTER_TYPE_1].filterType = itg + 1; return; }
    if (p >= PAR_PWM_1 && p <= PAR_PWM_3) { voiceArray[p - PAR_PWM_1].pwm = f; return; }
    if (p >= PAR_VOLUME_MOD_ON_OFF1 && p <= PAR_VOLUME_MOD_ON_OFF3) { voiceArray[p - PAR_VOLUME_MOD_ON_OFF1].volumeMod = itg != 0; return; }
    if (p >= PAR_MIX_MOD_1 && p <= PAR_MIX_MOD_3) { voiceArray[p - PAR_MIX_MOD_1].mixOscs = itg != 0; return; }
    switch (p) {
    case PAR_OSC_WAVE_DRUM1: case PAR_OSC_WAVE_DRUM2: case PAR_OSC_WAVE_DRUM3: voiceArray[p - PAR_OSC_WAVE_DRUM1].osc.waveform = itg; break;
    case PAR_OSC_WAVE_SNARE: snareVoice.osc.waveform = itg; break;
    case PAR_WAVE1_CYM: cymbalVoice.osc.waveform = itg; break;
    case PAR_WAVE1_HH: hatVoice.osc.waveform = itg; break;
    case PAR_MOD_WAVE_DRUM1: case PAR_MOD_WAVE_DRUM2: case PAR_MOD_WAVE_DRUM3: voiceArray[p - PAR_MOD_WAVE_DRUM1].modOsc.waveform = itg; break;
    case PAR_WAVE2_CYM: cymbalVoice.modOsc.waveform = itg; break;
    case PAR_WAVE3_CYM: cymbalVoice.modOsc2.waveform = itg; break;
    case PAR_WAVE2_HH: hatVoice.modOsc.waveform = itg; break;
    case PAR_WAVE3_HH: hatVoice.modOsc2.waveform = itg; break;
    case PAR_NOISE_FREQ1: snareVoice.noiseOsc.freq = f * 22000; break;
    case PAR_MIX1: snareVoice.mix = f; break;
    case PAR_MOD_OSC_F1_CYM: coarse(&cymbalVoice.modOsc, itg); break;
    case PAR_MOD_OSC_F2_CYM: coarse(&cymbalVoice.modOsc2, itg); break;
    case PAR_MOD_OSC_GAIN1_CYM: cymbalVoice.fmModAmount1 = f; break;
    case PAR_MOD_OSC_GAIN2_CYM: cymbalVoice.fmModAmount2 = f; break;
    case PAR_MOD_OSC_F1: coarse(&hatVoice.modOsc, itg); break;
    case PAR_MOD_OSC_F2: coarse(&hatVoice.modOsc2, itg); break;
    case PAR_MOD_OSC_GAIN1: hatVoice.fmModAmount1 = f; break;
    case PAR_MOD_OSC_GAIN2: hatVoice.fmModAmount2 = f; break;
    case PAR_VELOA1: case PAR_VELOA2: case PAR_VELOA3: slopeEg2_setAttack(&voiceArray[(p - PAR_VELOA1) / 2].oscVolEg, itg, 0); break;
    case PAR_VELOD1: case PAR_VELOD2: case PAR_VELOD3: slopeEg2_setDecay(&voiceArray[(p - PAR_VELOD1) / 2].oscVolEg, itg, 0); break;
    case PAR_VELOA4: slopeEg2_setAttack(&snareVoice.oscVolEg, itg, 0); break;
    case PAR_VELOD4: slopeEg2_setDecay(&snareVoice.oscVolEg, itg, 0); break;
    case PAR_VELOA5: slopeEg2_setAttack(&cymbalVoice.oscVolEg, itg, 0); break;
    case PAR_VELOD5: slopeEg2_setDecay(&cymbalVoice.oscVolEg, itg, 0); break;
    case PAR_VELOA6: slopeEg2_setAttack(&hatVoice.oscVolEg, itg, 0); break;
    case PAR_VELOD6_CLOSED: hatVoice.decayClosed = slopeEg2_calcDecay(itg); break;
    case PAR_VELOD6_OPEN: hatVoice.decayOpen = slopeEg2_calcDecay(itg); break;
    case PAR_REPEAT4: snareVoice.oscVolEg.repeat = itg; break;
    case PAR_REPEAT5: cymbalVoice.oscVolEg.repeat = itg; break;
    case PAR_MOD_EG1: case PAR_MOD_EG2: case PAR_MOD_EG3: DecayEg_setDecay(&voiceArray[p - PAR_MOD_EG1].oscPitchEg, itg); break;
    case PAR_MOD_EG4: DecayEg_setDecay(&snareVoice.oscPitchEg, itg); break;
    case PAR_MODAMNT1: case PAR_MODAMNT2: case PAR_MODAMNT3: voiceArray[p - PAR_MODAMNT1].egPitchModAmount = f * f * PITCH_AMOUNT_FACTOR; break;
    case PAR_MODAMNT4: snareVoice.egPitchModAmount = f * f * PITCH_AMOUNT_FACTOR; break;
    case PAR_PITCH_SLOPE1: case PAR_PITCH_SLOPE2: case PAR_PITCH_SLOPE3: DecayEg_setSlope(&voiceArray[p - PAR_PITCH_SLOPE1].oscPitchEg, itg > 126 ? 126 : itg); break;
    case PAR_PITCH_SLOPE4: DecayEg_setSlope(&snareVoice.oscPitchEg, itg > 126 ? 126 : itg); break;
    case PAR_FMAMNT1: case PAR_FMAMNT2: case PAR_FMAMNT3: voiceArray[(p - PAR_FMAMNT1) / 2].fmModAmount = f; break;
    case PAR_FM_FREQ1: case PAR_FM_FREQ2: case PAR_FM_FREQ3: coarse(&voiceArray[(p - PAR_FM_FREQ1) / 2].modOsc, itg); break;
    case PAR_VOL1: case PAR_VOL2: case PAR_VOL3: voiceArray[p - PAR_VOL1].vol = f; break;
    case PAR_VOL4: snareVoice.vol = f; break;
    case PAR_VOL5: cymbalVoice.vol = f; break;
    case PAR_VOL6: hatVoice.vol = f; break;
    case PAR_DRIVE1: case PAR_DRIVE2: case PAR_DRIVE3: setDistortionShape(&voiceArray[p - PAR_DRIVE1].distortion, f); break;
    case PAR_SNARE_DISTORTION: setDistortionShape(&snareVoice.distortion, f); break;
    case PAR_CYMBAL_DISTORTION: setDistortionShape(&cymbalVoice.distortion, f); break;
    case PAR_HAT_DISTORTION: setDistortionShape(&hatVoice.distortion, f); break;
    case PAR_FILTER_TYPE_4: snareVoice.filterType = itg + 1; break;
    case PAR_FILTER_TYPE_5: cymbalVoice.filterType = itg + 1; break;
    case PAR_FILTER_TYPE_6: hatVoice.filterType = itg + 1; break;
    case PAR_PWM_4: snareVoice.pwm = f; break;
    case PAR_PWM_5: cymbalVoice.pwm = f; break;
    case PAR_PWM_6: hatVoice.pwm = f; break;
    case PAR_VOLUME_MOD_ON_OFF4: snareVoice.volumeMod = itg != 0; break;
    case PAR_VOLUME_MOD_ON_OFF5: cymbalVoice.volumeMod = itg != 0; break;
    case PAR_VOLUME_MOD_ON_OFF6: hatVoice.volumeMod = itg != 0; break;
    default: break;
    }
}

void ref_init(uint32_t seed)
{
    ref_rng = seed;
    initDrumVoice();
    Snare_init();
    Cymbal_init();
    HiHat_init();
}
void ref_trigger(uint32_t track, uint32_t vel, uint32_t note)   /* voiceControl_noteOn */
{
    if (track < 3)
        Drum_trigger(track, vel, note);
    else if (track < 4)
        Snare_trigger(vel, note);
    else if (track < 5)
        Cymbal_trigger(vel, note);
    else
        HiHat_trigger(vel, track - 5, note);
}
void ref_block(int16_t out[6][64])                 /* mixer_calcNextSampleBlock's voice part (no LFO, no mod) */
{
    calcDrumVoiceAsync(0);
    calcDrumVoiceAsync(1);
    calcDrumVoiceAsync(2);
    Snare_calcAsync();
    Cymbal_calcAsync();
    HiHat_calcAsync();
    calcDrumVoiceSyncBlock(0, out[0], 64);
    calcDrumVoiceSyncBlock(1, out[1], 64);
    calcDrumVoiceSyncBlock(2, out[2], 64);
    Snare_calcSyncBlock(out[3], 64);
    Cymbal_calcSyncBlock(out[4], 64);
    HiHat_calcSyncBlock(out[5], 64);
}
