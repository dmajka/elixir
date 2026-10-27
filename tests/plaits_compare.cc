// SPDX-License-Identifier: GPL-3.0-only
// Elixir host test: plaits.c (Elixir's fixed-point port of Mutable Instruments Plaits' engines) against Plaits' own
// float code: each engine struck once, with the same parameters and the same random generator (stmlib's), over a
// few settings and pitches. Prints, for OUT and AUX, the correlation of the two and the error's level against the
// signal's, and the levels. Fails below a correlation or above an error; for the engines whose detail drifts apart
// (noise clocks, beating, feedback: float and fixed point round differently), off by more than 1.5 dB in level.
// Plaits' sources aren't in this tree (MIT licence, github.com/pichenettes/eurorack and stmlib);
// tests/plaits_compare.sh builds this with them, its sample rate set to Elixir's. PL_DUMP=<part of a case's name>
// writes that case's four signals to build/pl_dump.raw (float32: ref out, port out, ref aux, port aux, N each).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

extern "C" void pl_core(int eng, double note, double harm, double timbre, double morph, double accent, unsigned seed,
                        int n, float *out, float *aux);

enum { PE_VA, PE_WSHP, PE_FM, PE_GRAIN, PE_ADD, PE_WTBL, PE_CHORD, PE_SPEECH,
       PE_SWARM, PE_NOISE, PE_PART, PE_STRING, PE_MODAL, PE_BD, PE_SD, PE_HH };
static const char *NAMES[] = {"VA", "WSHP", "FM", "GRAIN", "ADD", "WTBL", "CHORD", "SPEECH",
                              "SWARM", "NOISE", "PART", "STRING", "MODAL", "BD", "SD", "HH"};
static const int N = 22016;                          // 0.5 s, a multiple of 16
static const unsigned SEED = 0x21;
static char ram[65536];
static int fails;

static plaits::Engine *make(int e)
{
  static plaits::VirtualAnalogEngine va; static plaits::WaveshapingEngine ws; static plaits::FMEngine fm;
  static plaits::GrainEngine gr; static plaits::AdditiveEngine ad; static plaits::WavetableEngine wt;
  static plaits::ChordEngine ch; static plaits::SpeechEngine sp; static plaits::SwarmEngine sw;
  static plaits::NoiseEngine nz; static plaits::ParticleEngine pt; static plaits::StringEngine st;
  static plaits::ModalEngine md; static plaits::BassDrumEngine bd; static plaits::SnareDrumEngine sd;
  static plaits::HiHatEngine hh;
  plaits::Engine *t[] = {&va, &ws, &fm, &gr, &ad, &wt, &ch, &sp, &sw, &nz, &pt, &st, &md, &bd, &sd, &hh};
  return t[e];
}

static void ref(int e, double note, double h, double t, double m, double acc, float *out, float *aux)
{
  stmlib::BufferAllocator al(ram, sizeof ram);
  plaits::Engine *eng = make(e);
  std::memset(ram, 0, sizeof ram);
  eng->Init(&al);
  eng->LoadUserData(NULL);
  eng->Reset();
  stmlib::Random::Seed(SEED);
  for (int k = 0; k < N; k += 16) {
    plaits::EngineParameters p;
    bool env = false;
    p.trigger = k == 0 ? plaits::TRIGGER_RISING_EDGE | plaits::TRIGGER_HIGH : plaits::TRIGGER_LOW;
    p.note = note;
    p.harmonics = h;
    p.timbre = t;
    p.morph = m;
    p.accent = acc;
    eng->Render(p, out + k, aux + k, 16, &env);
  }
}

static void stats(const float *a, const float *b, double *corr, double *err, double *la, double *lb)
{
  double sab = 0, saa = 0, sbb = 0, se = 0;
  for (int i = 0; i < N; i++) {
    sab += (double)a[i] * b[i];
    saa += (double)a[i] * a[i];
    sbb += (double)b[i] * b[i];
    se += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
  }
  *corr = saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : (saa == sbb ? 1.0 : 0.0);
  *err = saa > 0 ? 10 * std::log10(se / saa + 1e-30) : (se > 0 ? 0 : -300);
  *la = 10 * std::log10(saa / N + 1e-30);
  *lb = 10 * std::log10(sbb / N + 1e-30);
}

static void run(int e, double note, double h, double t, double m, double acc, double min_corr, double max_err)
{
  static float ro[N], ra[N], po[N], pa[N];
  char name[96];
  std::snprintf(name, sizeof name, "%s n%.0f h%.2f t%.2f m%.2f a%.1f", NAMES[e], note, h, t, m, acc);
  ref(e, note, h, t, m, acc, ro, ra);
  pl_core(e, note, h, t, m, acc, SEED, N, po, pa);
  double c1, e1, l1, l2, c2, e2, l3, l4;
  stats(ro, po, &c1, &e1, &l1, &l2);
  stats(ra, pa, &c2, &e2, &l3, &l4);
  // the engines whose detail drifts (noise clocks, beating, feedback: a sample's timing apart grows), by level
  static const bool by_level[16] = {1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1};
  bool bad = by_level[e] ? std::fabs(l1 - l2) > 1.5 || std::fabs(l3 - l4) > 1.5
                         : c1 < min_corr || e1 > max_err || c2 < min_corr || e2 > max_err;
  std::printf("%-6s %-38s OUT %8.5f %7.1f dB (%6.1f %6.1f)  AUX %8.5f %7.1f dB (%6.1f %6.1f)\n", bad ? "FAIL" : "ok",
              name, c1, e1, l1, l2, c2, e2, l3, l4);
  fails += bad;
  const char *dump = std::getenv("PL_DUMP");
  if (dump && std::strstr(name, dump)) {
    FILE *f = std::fopen("build/pl_dump.raw", "wb");
    std::fwrite(ro, 4, N, f); std::fwrite(po, 4, N, f); std::fwrite(ra, 4, N, f); std::fwrite(pa, 4, N, f);
    std::fclose(f);
  }
}

int main(int argc, char **argv)
{
  std::string only = argc > 1 ? argv[1] : "";
  const double notes[] = {36, 48, 60, 72};
  const double knobs[][3] = {{0.5, 0.5, 0.5}, {0.0, 0.2, 0.8}, {0.9, 0.8, 0.2}, {0.3, 1.0, 1.0}, {0.7, 0.0, 0.0}};
  for (int e = 0; e < 16; e++) {
    if (!only.empty() && only != NAMES[e]) continue;
    for (double note : notes)
      for (auto &k : knobs)
        run(e, note, k[0], k[1], k[2], 0.8, 0.98, -20.0);
  }
  std::printf(fails ? "plaits_compare: %d FAILED\n" : "plaits_compare: all passed\n", fails);
  return fails != 0;
}
