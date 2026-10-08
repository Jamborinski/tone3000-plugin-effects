// Side-by-side comparison: broadband noise burst through
// (a) convolution with the real spring IR  =  reference
// (b) our Spring algorithm                 =  candidate
// Then compare spectral envelope, decay, and character.
#include "Reverb.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <dirent.h>

namespace {

constexpr int kFs = 48000;
constexpr int kBurstMs = 50;   // 50 ms noise burst (like a drum hit)

// 32-bit float WAV reader
std::vector<float> readWav(const char* path, int& out_fs) {
  FILE* f = fopen(path, "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<char> raw(sz);
  fread(raw.data(), 1, sz, f); fclose(f);
  if (sz < 44 || memcmp(raw.data(),"RIFF",4) != 0) return {};
  int ch=0, fs=0, bits=0;
  const char* data = nullptr; long data_len = 0;
  size_t off = 12;
  while (off + 8 <= raw.size()) {
    const char* cid = raw.data() + off;
    long csize = *(int*)(raw.data() + off + 4);
    const char* body = raw.data() + off + 8;
    if (memcmp(cid,"fmt ",4)==0) {
      ch = *(short*)(body+2); fs = *(int*)(body+4); bits = *(short*)(body+14);
    } else if (memcmp(cid,"data",4)==0) {
      data = body; data_len = csize;
    }
    off += 8 + csize + (csize & 1);
    if (memcmp(cid,"data",4)==0) break;
  }
  if (!data) return {};
  std::vector<float> out;
  if (bits==32) {
    long n = data_len/4; out.resize(n); memcpy(out.data(), data, n*4);
  } else if (bits==24) {
    long n = data_len/3; out.resize(n);
    for (long i=0;i<n;++i) {
      int v = (unsigned char)data[i*3] | ((unsigned char)data[i*3+1]<<8) | (data[i*3+2]<<16);
      if (data[i*3+2] & 0x80) v -= 0x1000000;
      out[i] = (float)v / 8388608.0f;
    }
  } else if (bits==16) {
    long n = data_len/2; out.resize(n);
    for (long i=0;i<n;++i) out[i] = (float)*((short*)(data+i*2))/32768.0f;
  } else return {};
  if (ch==2) { std::vector<float>L(out.size()/2);
    for(long i=0;i<(long)out.size()/2;++i) L[i]=out[i*2]; out=std::move(L); }
  out_fs = fs;
  return out;
}

// O(N*M) convolution
std::vector<float> convolve(const std::vector<float>& x, const std::vector<float>& ir) {
  long n = x.size(), m = ir.size();
  std::vector<float> y(n + m - 1, 0.0f);
  for (long i = 0; i < n; ++i)
    for (long j = 0; j < m; ++j)
      y[i+j] += x[i]*ir[j];
  return y;
}

// Full FFT via JUCE
double fftMagAt(float* sig, long N, int fs, double freq) {
  double w = 2.0*M_PI*freq/fs;
  double re=0, im=0;
  for (long k=0;k<N;++k) { re += sig[k]*std::cos(w*k); im -= sig[k]*std::sin(w*k); }
  return std::sqrt(re*re+im*im)/(double)N;
}

void reportSpectrum(const char* name, const float* sig, long N, int fs, int ref_n) {
  // Use a fixed-length window (first ref_n samples) for fair comparison
  long M = std::min(N, (long)ref_n);
  double freqs[] = {63, 125, 250, 500, 1000, 2000, 4000, 8000, 12000};
  const char* lbls[] = {"63H","125H","250H","500H","1kH","2kH","4kH","8kH","12k"};
  double raw[9];
  for (int i=0;i<9;++i)
    raw[i] = fftMagAt((float*)sig, M, fs, freqs[i]);
  double mx=1e-15; for(int i=0;i<9;++i) mx=std::max(mx,raw[i]);
  printf("  %-10s", name);
  for (int i=0;i<9;++i) printf("  %s:%+5.1f", lbls[i], 20.0*std::log10(raw[i]/mx+1e-15));
  printf("\n");
}

void reportDecay(const char* name, const float* sig, long N, int fs) {
  int win = fs/10;  // 100ms
  int maxw = std::min((int)(N/win), 30);
  float peak=0;
  for (long i=0;i<200*fs/fs;++i) peak=std::max(peak, std::abs(sig[i]));
  if (peak<1e-6) peak=1e-6;
  printf("  %-10s decay:", name);
  for (int w=0; w<maxw && w<30; ++w) {
    float s=0;
    for (long i=w*win; i<(w+1)*win && i<N; ++i) s+=sig[i]*sig[i];
    float r=std::sqrt(s/((float)win));
    printf(" %0.0fs:%+.0fdB", w*0.1, 20.0f*std::log10(r/(peak+1e-15f)));
  }
  printf("\n");
}

// Simple LCG noise (deterministic, no external deps)
long lcg_state = 123456789L;
float lcg() {
  lcg_state = lcg_state * 1103515245L + 12345L;
  return ((float)((lcg_state >> 16) & 0x7FFF) / 32768.0f) * 2.0f - 1.0f;
}

}  // anonymous

TEST(SpringCompare, NoiseBurst) {
  // Load the Nevo MR-III IR (the cleanest reference)
  const char* paths[] = {
    "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
    "IR - WAV/NEVO - Studio Springs/NEVO - Great British Spring/1. NEVO - GBS, Stereo.wav",
    "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/Nevo Plates & Springs/"
    "IR - WAV/NEVO - NEVO - Springs/1. NEVO - Springs - 1s.wav",
  };
  int ir_fs = 0;
  std::vector<float> ir;
  const char* used_path = nullptr;
  for (auto p : paths) {
    ir = readWav(p, ir_fs);
    if (!ir.empty()) { used_path = p; break; }
  }
  if (ir.empty()) {
    // Walk the directory to find any WAV
    DIR* d = opendir(
      "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
      "IR - WAV/NEVO - Studio Springs/NEVO - Master Room, MR-III");
    if (d) {
      struct dirent* e;
      while ((e = readdir(d)) != nullptr) {
        if (strstr(e->d_name, ".wav")) {
          char full[1024];
          snprintf(full, sizeof(full),
            "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
            "IR - WAV/NEVO - Studio Springs/NEVO - Master Room, MR-III/%s", e->d_name);
          ir = readWav(full, ir_fs);
          if (!ir.empty()) { used_path = full; break; }
        }
      }
      closedir(d);
    }
  }
  if (ir.empty()) {
    ADD_FAILURE() << "No IR found";
    return;
  }
  printf("IR: %s  (%ld samples @ %d Hz)\n", used_path, (long)ir.size(), ir_fs);

  // Normalise IR peak to 1.0
  float ir_pk=0; for(float v:ir) ir_pk=std::max(ir_pk,std::abs(v));
  float ir_n = 1.0f/(ir_pk+1e-15f);
  for(float&v:ir) v*=ir_n;

  // Input: 50 ms noise burst (deterministic LCG), padded with silence to 5s
  const long total = ir_fs * 5;
  const long burst_n = ir_fs * kBurstMs / 1000;
  std::vector<float> sig(total, 0.0f);
  for (long i=0;i<burst_n;++i) sig[i] = lcg();
  // Normalise signal peak to 0.5 (gentle drive)
  float s_pk=0; for(float v:sig) s_pk=std::max(s_pk,std::abs(v));
  float s_n = 0.5f/(s_pk+1e-15f);
  for(float&v:sig) v*=s_n;

  // --- REFERENCE: convolve with IR ---
  auto ref = convolve(sig, ir);
  float ref_pk=0; for(float v:ref) ref_pk=std::max(ref_pk,std::abs(v));
  for(float&v:ref) v/=(ref_pk+1e-15f);

  // --- CANDIDATE: our Spring algorithm (3 springs, sag 30%) ---
  Reverb r; r.prepare(kFs);
  Reverb::Params p;
  p.decayMs=2000.0; p.tone=0.50; p.size=0.60; p.width=0.0; p.preMs=0.0;
  p.mode=1; p.springs=0.4; p.sag=0.30;
  r.setParams(p);
  juce::AudioBuffer<float> buf(1, (int)total);
  for(long i=0;i<total;++i) buf.setSample(0,(int)i,sig[i]);
  r.process(buf);
  std::vector<float> cand(total,0.0f);
  for(long i=0;i<total;++i) cand[i]=buf.getSample(0,(int)i);
  float cand_pk=0; for(float v:cand) cand_pk=std::max(cand_pk,std::abs(v));
  for(float&v:cand) v/=(cand_pk+1e-15f);

  // Truncate to same length for fair comparison
  long M = (long)std::min({(int)ref.size(), (int)cand.size(), ir_fs*4});

  printf("\n=== SPECTRAL ENVELOPE (noise burst, both peak-normalised, %ld samples) ===\n",(long)M);
  float rtrunc[M]; std::memcpy(rtrunc, ref.data(), M*sizeof(float));
  float ctrunc[M]; std::memcpy(ctrunc, cand.data(), M*sizeof(float));
  reportSpectrum("IR ref",  rtrunc, M, ir_fs, M);
  reportSpectrum("Ours",    ctrunc, M, ir_fs, M);

  printf("\n=== DECAY PROFILE (seconds: dB below peak) ===\n");
  reportDecay("IR ref", rtrunc, M, ir_fs);
  reportDecay("Ours",   ctrunc, M, ir_fs);

  // --- Quantitative comparison metric: spectral distance ---
  // Use 1/3-octave-equivalent bands at: 125, 250, 500, 1k, 2k, 4k, 8k
  double freqs[]    = {125, 250, 500, 1000, 2000, 4000, 8000};
  const char* lbls[] = {"125H","250H","500H","1kH","2kH","4kH","8kH"};
  double rb[7], cb[7];
  for (int i=0;i<7;++i) {
    rb[i] = fftMagAt((float*)rtrunc, M, ir_fs, freqs[i]);
    cb[i] = fftMagAt((float*)ctrunc, M, ir_fs, freqs[i]);
  }
  double rmx=1e-15, cmx=1e-15;
  for(int i=0;i<7;++i){rmx=std::max(rmx,rb[i]);cmx=std::max(cmx,cb[i]);}
  printf("\n=== BAND DELTA (Ours - IR, both normalised to their own peaks) ===\n");
  printf("      ");
  for(int i=0;i<7;++i) printf("%6s   ", lbls[i]);
  printf("\n");
  printf("  IR  ");
  for(int i=0;i<7;++i) printf("%+6.1f  ", 20*std::log10(rb[i]/rmx+1e-15));
  printf("\n");
  printf("  Ours");
  for(int i=0;i<7;++i) printf("%+6.1f  ", 20*std::log10(cb[i]/cmx+1e-15));
  printf("\n");
  printf("  DELTA");
  double sum_sq=0;
  for(int i=0;i<7;++i) {
    double d = (20*std::log10(cb[i]/cmx+1e-15)) - (20*std::log10(rb[i]/rmx+1e-15));
    sum_sq += d*d;
    printf("%+6.1f  ", d);
  }
  printf("\n");
  printf("  RMS spectral distance: %.1f dB  (0=perfect match, >10=big difference)\n",
         std::sqrt(sum_sq/7.0));
}
