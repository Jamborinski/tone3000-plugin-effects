// Spring comparison: generate our impulse response and measure the
// same properties as the IR analysis (spectral bands, crest, onset).
#include "Reverb.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

double magBin(const float* x, int N, int fs, double f) {
  const double w = 2.0*M_PI*f/fs;
  double re=0.0, im=0.0;
  for (int n = 0; n < N; ++n) { re += x[n]*std::cos(w*n); im -= x[n]*std::sin(w*n); }
  return std::sqrt(re*re+im*im) / N;
}

void report(const char* label, const std::vector<float>& out, int fs) {
  int N = out.size();
  // Peak timing
  float peak = 0; int pk = 0;
  for (int i = 0; i < N; ++i) { float a = std::abs(out[i]); if (a > peak) { peak = a; pk = i; } }
  float pk_ms = (float)pk / fs * 1000.0;
  // Crest factor (200ms..2s)
  int t0 = 0.2f*fs, t1 = std::min((int)(2.0f*fs), N);
  float sp = 0, sq = 0;
  for (int i = t0; i < t1; ++i) { float a = std::abs(out[i]); if (a > sp) sp = a; sq += out[i]*out[i]; }
  float sr = std::sqrt(sq / std::max(1, t1-t0));
  float crest = 20.0f*std::log10f(sp/(sr+1e-15f));
  // Half-life: -3 dB
  int win = fs/10;
  int max_wins = std::min(N, 50*win)/win;
  float max_lvl = 0;
  for (int w = 0; w < std::min(10, max_wins); ++w) {
    float s = 0; for (int i = w*win; i < (w+1)*win && i < N; ++i) s += out[i]*out[i];
    float r = std::sqrt(s/win); if (r > max_lvl) max_lvl = r;
  }
  float t3 = -1;
  for (int w = 0; w < max_wins; ++w) {
    float s = 0; for (int i = w*win; i < (w+1)*win && i < N; ++i) s += out[i]*out[i];
    float r = std::sqrt(s/win);
    if (r < max_lvl * 0.707f) { t3 = w*0.1f; break; }
  }
  // Spectral bands
  const double freqs[] = {125, 250, 500, 1000, 2000, 4000, 8000, 12000};
  double raw[8]; for (int i = 0; i < 8; ++i) raw[i] = magBin(out.data(), N, fs, freqs[i]);
  double mx = 1e-15; for (int i = 0; i < 8; ++i) mx = std::max(mx, raw[i]);
  const char* lbls[] = {"125H","250H","500H","1kH","2kH","4kH","8kH","12k"};
  printf("%s  dur=%5.1fs pk=%5.0fms crest=%+5.1fdB T3=%.2fs  ", label, N/(float)fs, pk_ms, crest, t3);
  for (int i = 0; i < 8; ++i) printf("%s:%+5.1f ", lbls[i], 20.0*std::log10(raw[i]/mx));
  printf("\n");
}

}  // anonymous

TEST(SpringIR, CompareWithReference) {
  const int fs = 48000;
  const int dur_s = 5;  // 5 second IR
  const int N = fs * dur_s;

  // Feed a 48 kHz impulse (1-sample, full scale) through our Spring
  for (int N_lines : {1, 3, 6}) {  // 1, 3, 6 springs
    Reverb r; r.prepare(fs);
    double dm, pr, to, sz, wi; Reverb::defaultDialsForMode(1, dm, pr, to, sz, wi);
    Reverb::Params p; p.decayMs = dm; p.preMs = pr; p.tone = to; p.size = sz; p.width = 0.0;
    p.mode = 1; p.springs = (N_lines == 1) ? 0.0 : (N_lines == 3) ? 0.4 : 1.0;
    p.sag = 0.30;
    r.setParams(p);

    juce::AudioBuffer<float> buf(1, N);
    for (int i = 0; i < N; ++i) buf.setSample(0, i, (i == 0) ? 1.0f : 0.0f);
    r.process(buf);

    std::vector<float> out(N);
    for (int i = 0; i < N; ++i) out[i] = buf.getSample(0, i);

    char label[32];
    snprintf(label, sizeof(label), "Spring N=%d", N_lines);
    report(label, out, fs);
  }

  printf("\n--- Reference (avg of 30 IRs, normalised to peak) ---\n");
  printf("  dur= 4.4-20s pk= 28-162ms crest=17-27dB  T6=0.10-0.20s\n");
  printf("  125H:-4.2 250H:-11.2 500H:-3.0 1kH:-10.3 2kH:-12.4 4kH:-34.0 8kH:-62.2 12k:-70.2\n");
}
