/* the LXR's Soundengine/config.h, its sections and HAL out (the reference build) */
#pragma once
#include "stm32f7xx_hal.h"
#define true 1
#define false 0
#define USE_BIG_SAMPLE_BUFFER 1
#define SMOOTH_MIXER_VOL_PAN_DRIVE 1
#define OUTPUT_DMA_SIZE 64
#define UNIT_GAIN_DRIVE 0
#define USE_FILTER_DRIVE 0
#define AMP_EG_SYNC 0
#define ENABLE_MIX_OSC 1
#define ENABLE_DRUM_SVF 1
#define PITCH_AMOUNT_FACTOR 32
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define SYNTH_FS I2S_AUDIOFREQ_44K
#define REAL_FS (float)44100.f
#define REAL_FS2 (REAL_FS / 2.f)
#define NUM_DRUM_VOICES 3
#define INDTCM
#define INDTCMZ
#define IN_ITCM_RAM
