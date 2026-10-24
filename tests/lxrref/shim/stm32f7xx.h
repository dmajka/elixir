/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle tests: what the LXR's DSP sources need of the STM32 HAL and CMSIS, for the host (the reference). */
#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#define I2S_AUDIOFREQ_44K 44100u
static inline int32_t __SSAT(int32_t v, int b) { int32_t m = 1 << (b - 1); return v > m - 1 ? m - 1 : v < -m ? -m : v; }
static inline uint32_t __QADD16(uint32_t a, uint32_t b)
{
    int32_t lo = __SSAT((int16_t)a + (int16_t)b, 16), hi = __SSAT((int16_t)(a >> 16) + (int16_t)(b >> 16), 16);
    return (uint16_t)lo | (uint32_t)(uint16_t)hi << 16;
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b)
{
    int32_t lo = __SSAT((int16_t)a - (int16_t)b, 16), hi = __SSAT((int16_t)(a >> 16) - (int16_t)(b >> 16), 16);
    return (uint16_t)lo | (uint32_t)(uint16_t)hi << 16;
}
static inline uint32_t __PKHBT(uint32_t a, uint32_t b, int s) { return (a & 0xFFFFu) | ((b << s) & 0xFFFF0000u); }
static inline uint8_t __CLZ(uint32_t v) { return v ? (uint8_t)__builtin_clz(v) : 32; }
typedef struct { int dummy; } RNG_HandleTypeDef;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef *h);
typedef union { struct { uint32_t r : 28, V : 1, C : 1, Z : 1, N : 1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void) { return 0; }
