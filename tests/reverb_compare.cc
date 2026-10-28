// SPDX-License-Identifier: GPL-3.0-only
// Elixir host test: reverb.c's PLATE against Mutable Instruments Clouds' reverb (clouds/dsp/fx/reverb.h, MIT), its
// float code, in a copy (tests/reverb_compare.sh) at Elixir's half rate: the lengths scaled x 0.689, the LFOs' swings
// off (their phases differ), 32-bit float lines (FORMAT_32_BIT; Elixir's are Q14, the hardware's 12-bit). The same
// input (a click and a drum-like burst, through Elixir's 1 2 1 decimation), both impulse responses compared: their
// correlation over the first second and their decay (the energy at 0.5, 1, 2 s).
#include <cmath>
#include <cstdio>
#include <cstring>
namespace clouds {
struct FloatFrame { float l, r; };                   // (Clouds' clouds/dsp/frame.h)
}
#include "clouds/dsp/fx/reverb.h"

extern "C" void rv_core(int size, int damp, const float *in, float *mid, float *side, int n);

static const int FSR = 44100, N = FSR * 3;
static float in[N], mid[N], side[N], cl[N / 2], cr[N / 2];
static uint16_t buf[16384 * 2];

int main() {
  int fails = 0;
  for (int i = 0; i < N; ++i) in[i] = 0;
  for (int i = 0; i < 400; ++i) in[i] = 0.5f * expf(-i / 60.0f) * (i & 1 ? -1.0f : 1.0f) + (i == 0 ? 0.5f : 0.0f);
  struct { int size, damp; } cs[3] = {{64, 48}, {127, 0}, {20, 110}};
  for (int c = 0; c < 3; ++c) {
    // Clouds, at the half rate: its input the decimated send (1 2 1), l + r = the send, the wet out
    static clouds::Reverb r;
    memset(buf, 0, sizeof buf);
    r.Init(reinterpret_cast<float*>(buf));
    r.set_amount(1.0f);
    r.set_input_gain(0.2f);
    r.set_time((11469 + cs[c].size * 162) / 32768.0f);
    r.set_diffusion(0.625f);
    r.set_lp((32767 - cs[c].damp * 193) / 32768.0f);
    float xp = 0;
    for (int i = 0; i < N / 2; ++i) {
      clouds::FloatFrame f;
      float x = (xp + 2 * in[2 * i] + in[2 * i + 1]) / 4;
      xp = in[2 * i + 1];
      f.l = f.r = x * 0.5f;
      r.Process(&f, 1);
      cl[i] = f.l;
      cr[i] = f.r;
    }
    rv_core(cs[c].size, cs[c].damp, in, mid, side, N);
    // Elixir's mid (6 x (l + r) x 4096 / 32768 in Q15 units) at the half rate: its odd samples
    double sxy = 0, sxx = 0, syy = 0, e[2][3] = {{0}};
    const double k = 6.0 * 4096.0 / 32768.0;
    for (int i = 200; i < N / 2; ++i) {
      double a = (cl[i] + cr[i]) * k, b = mid[2 * i + 1];
      if (i < FSR / 2) {
        sxy += a * b;
        sxx += a * a;
        syy += b * b;
      }
      int w = i >= FSR / 4 && i < FSR / 4 + 2205 ? 0 : i >= FSR / 2 && i < FSR / 2 + 2205 ? 1 : i >= FSR && i < FSR + 2205 ? 2 : -1;
      if (w >= 0) {
        e[0][w] += a * a;
        e[1][w] += b * b;
      }
    }
    double corr = sxy / sqrt(sxx * syy + 1e-30);
    printf("SIZE %3d DAMP %3d: correlation %.4f over 1 s; energy at 0.5/1/2 s, Clouds %.2e %.2e %.2e, Elixir %.2e %.2e %.2e\n",
           cs[c].size, cs[c].damp, corr, e[0][0], e[0][1], e[0][2], e[1][0], e[1][1], e[1][2]);
    // the decay: within 0.5 .. 2 x Clouds' energy where it is above the lines' floor (a couple of their LSBs: 6e-5
    // here), below it where not
    int decay = 1;
    for (int w = 0; w < 3; ++w) {
      double r2 = e[1][w] / (e[0][w] + 1e-30);
      if (e[0][w] > 6e-5 ? r2 < 0.5 || r2 > 2.0 : e[1][w] > 6e-5) decay = 0;
    }
    if (corr < 0.98 || !decay) {
      printf("FAIL: PLATE differs from Clouds\n");
      fails++;
    }
  }
  printf(fails ? "reverb_compare: %d FAILED\n" : "reverb_compare: ok\n", fails);
  return fails != 0;
}
