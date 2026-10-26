// SPDX-License-Identifier: GPL-3.0-only
// Elixir host test: rings.c (Elixir's fixed-point port of Mutable Instruments Rings' resonators) against Rings'
// own float code, fed the same signal: MODAL (rings::Resonator, 32 modes), STRING (rings::String with dispersion)
// and FM (rings::FMVoice), over their parameters and pitches. Prints, for each, the correlation of the two outputs
// and the error's level against the signal's; fails below 0.99 / above -30 dB. Rings' sources aren't in this tree
// (MIT licence, github.com/pichenettes/eurorack and stmlib); tests/rings_compare.sh builds this with them, its
// sample rate set to Elixir's. RG_DUMP=<part of a case's name> prints that case's level and correlation over time
// and its first samples (from RG_FROM, else the hit).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define private public                               // (their ramps and states: from where Elixir's start)
#include "rings/dsp/fm_voice.h"
#include "rings/dsp/resonator.h"
#include "rings/dsp/string.h"
#undef private
#include "rings/resources.h"
#include "stmlib/dsp/units.h"

extern "C" void rg_core(int model, double f, double s, double b, double d, double p, int env, const float *in, int n,
                        float *out);

static const int N = 44096;                          // 1 s, a multiple of 64
static const int P = 2048;                           // after Rings' parameters' ramps in from Init()
static int fails;

static void ref(int model, double f, double s, double b, double d, double p, int env, const float *in, float *out)
{
  static rings::Resonator r;
  static rings::String st;
  static rings::FMVoice fm;
  float o[64], a[64];
  if (model == 1) {
    r.Init();
    r.set_resolution(32);
  } else if (model == 2) {
    st.Init(true);
    st.src_phase_ = 1.0f;                            // (Init() leaves it)
  } else {
    fm.Init();
    fm.previous_sample_ = 0.0f;                      // (Init() leaves it: the last case's)
  }
  for (int k = 0; k < N; k += 64) {
    std::memset(o, 0, sizeof o);
    std::memset(a, 0, sizeof a);
    if (model == 1) {
      r.set_frequency(f);
      r.set_structure(s);
      r.set_brightness(b * b);
      r.set_position(p);
      r.set_damping(d);
      r.Process(in + k, o, a, 64);
    } else if (model == 2) {
      float disp = s < 0.24f ? (s - 0.24f) * 4.166f : (s > 0.26f ? (s - 0.26f) * 1.35135f : 0.0f);
      st.set_dispersion(disp);
      st.set_frequency(f);
      st.set_brightness(b);
      st.set_position(p);
      st.set_damping(d);
      st.Process(in + k, o, a, 64);
    } else {
      if (k == 0 && env) fm.TriggerInternalEnvelope();
      fm.set_frequency(f);
      fm.set_ratio(s);
      fm.set_brightness(b);
      fm.set_feedback_amount(p);
      fm.set_position(0.0f);
      fm.set_damping(d);
      if (k == 0) {                                  // Elixir's first block has no ramps: Rings' from its Init()
        float mf = f * stmlib::SemitonesToRatio(stmlib::Interpolate(rings::lut_fm_frequency_quantizer, s, 128.0f));
        fm.previous_carrier_frequency_ = f;
        fm.previous_modulator_frequency_ = mf > 0.5f ? 0.5f : mf;
        fm.previous_brightness_ = b;
        fm.previous_feedback_amount_ = (p - 0.5f) * 2.0f;
      }
      fm.Process(in + k, o, a, 64);
    }
    for (int i = 0; i < 64; i++) out[k + i] = o[i] + a[i];
  }
}

static double q16(double v) { return std::floor(v * 65536.0 + 0.5 > 65535.0 ? 65535.0 : v * 65536.0 + 0.5) / 65536.0; }

static void one(const char *name, int model, double hz, double s, double b, double d, double p, int env, int input)
{
  std::vector<float> in(N, 0.0f), x(N), y(N);
  unsigned seed = 12345;
  if (input == 1) {
    in[P] = 1.0f;                                    // an impulse
  } else if (input == 3) {                           // a smooth pulse (where skipped samples matter little)
    for (int i = 0; i < 400; i++)
      in[P + i] = 0.5f * std::sin(3.14159265f * i / 400) * std::sin(3.14159265f * i / 400) * std::sin(6.2831853f * i / 200);
  } else if (input == 2) {
    for (int i = 0; i < 300; i++) {                  // a decaying noise burst
      seed = seed * 1664525u + 1013904223u;
      in[P + i] = ((int)(seed >> 9) / 4194304.0f - 1.0f) * 0.5f * expf(-i / 60.0f);
    }
  }
  s = q16(s), b = q16(b), d = q16(d), p = q16(p);   // the knobs as Elixir holds them, the same on both sides
  ref(model, hz / 44100.0, s, b, d, p, env, in.data(), x.data());
  rg_core(model, hz / 44100.0, s, b, d, p, env, in.data(), N, y.data());
  double sxy = 0, sxx = 0, syy = 0, see = 0;
  for (int i = 0; i < N; i++) {
    sxy += (double)x[i] * y[i];
    sxx += (double)x[i] * x[i];
    syy += (double)y[i] * y[i];
    see += ((double)x[i] - y[i]) * ((double)x[i] - y[i]);
  }
  double c = sxy / std::sqrt(sxx * syy + 1e-30), e = 10.0 * std::log10(see / (sxx + 1e-30) + 1e-30);
  double lv = 10.0 * std::log10(sxx / N + 1e-30), lk = 10.0 * std::log10(syy / N + 1e-30);
  if (std::getenv("RG_DUMP") && std::strstr(name, std::getenv("RG_DUMP"))) {
    for (int w = 0; w < N; w += 2205) {
      double a = 0, b = 0, ab = 0, ma = 0, mb = 0;
      for (int i = w; i < w + 2205 && i < N; i++) a += x[i] * x[i], b += y[i] * y[i], ab += x[i] * y[i], ma += x[i], mb += y[i];
      std::printf("    %5d ms  ref %7.1f  elixir %7.1f dB  corr %.3f\n", w * 1000 / 44100, 10 * std::log10(a / 2205 + 1e-30),
                  10 * std::log10(b / 2205 + 1e-30), ab / std::sqrt(a * b + 1e-30));
      std::printf("              mean %.6f %.6f\n", ma / 2205, mb / 2205);
    }
    for (int i = std::getenv("RG_FROM") ? std::atoi(std::getenv("RG_FROM")) : P, j = i + 12; i < j; i++) std::printf("    [%d] %.6f %.6f\n", i, x[i], y[i]);
  }
  bool ok = c > 0.99 && e < -30.0;
  std::printf("  %-34s corr %.5f  err %6.1f dB  level %6.1f / %6.1f dB%s\n", name, c, e, lv, lk, ok ? "" : "  FAIL");
  fails += !ok;
}

int main()
{
  // MODAL: harmonic (structure 0.27), inharmonic and stiff; damping, brightness, position; low and high
  one("MODAL 110 Hz harmonic", 1, 110, 0.27, 0.5, 0.5, 0.3, 0, 1);
  one("MODAL 55 Hz long", 1, 55, 0.27, 0.5, 0.8, 0.3, 0, 1);
  one("MODAL 220 Hz inharmonic (0.6)", 1, 220, 0.6, 0.7, 0.5, 0.5, 0, 1);
  one("MODAL 440 Hz stiff (0.95)", 1, 440, 0.95, 0.9, 0.4, 0.2, 0, 1);
  one("MODAL 80 Hz compressed (0.1)", 1, 80, 0.1, 0.3, 0.6, 0.7, 0, 1);
  one("MODAL 1 kHz short, bright", 1, 1000, 0.4, 1.0, 0.1, 0.5, 0, 2);
  one("MODAL 300 Hz noise burst", 1, 300, 0.5, 0.5, 0.5, 0.1, 0, 2);
  // STRING: dispersion off (0.25), curved bridge (0.1), stiff (0.6); damping, brightness, pickup
  one("STRING 110 Hz plain", 2, 110, 0.25, 0.5, 0.5, 0.3, 0, 2);
  one("STRING 55 Hz long", 2, 55, 0.25, 0.7, 0.8, 0.5, 0, 2);
  one("STRING 220 Hz curved bridge (0.1)", 2, 220, 0.1, 0.5, 0.5, 0.3, 0, 2);
  one("STRING 330 Hz stiff (0.6)", 2, 330, 0.6, 0.6, 0.6, 0.7, 0, 2);
  one("STRING 660 Hz dark, short", 2, 660, 0.3, 0.1, 0.2, 0.5, 0, 2);
  one("STRING 30 Hz (below the line)", 2, 30, 0.25, 0.6, 0.6, 0.4, 0, 3);   // (it skips input samples: a smooth one)
  // FM: its envelope from the hit; ratio, index, feedback (both ways), decay; and from its input
  one("FM 110 Hz ratio 1", 3, 110, 0.2, 0.5, 0.5, 0.5, 1, 0);
  one("FM 220 Hz ratio 2, bright", 3, 220, 0.45, 0.9, 0.6, 0.5, 1, 0);
  one("FM 330 Hz feedback +", 3, 330, 0.3, 0.6, 0.5, 0.9, 1, 0);
  one("FM 165 Hz feedback -", 3, 165, 0.7, 0.6, 0.5, 0.1, 1, 0);
  one("FM 440 Hz from its input", 3, 440, 0.5, 0.7, 0.4, 0.5, 0, 2);
  std::printf(fails ? "rings_compare: %d FAILED\n" : "rings_compare: ok\n", fails);
  return fails ? 1 : 0;
}
