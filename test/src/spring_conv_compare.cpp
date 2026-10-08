// Side-by-side comparison: same input signal through
// (a) convolution with the real spring IR  =  reference / ground truth
// (b) our Spring algorithm                 =  candidate
// Then compare: spectral envelope, decay profile, peak timing, THD.
#include "Reverb.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kFs = 48000;

// --- 1. Read a float WAV (supports 32-bit float and 16-bit PCM) ---
std::vector<float> readWav(const char* path, int& out_fs) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "  can't open %s\n", path); return {}; }
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<char> raw(sz);
  fread(raw.data(), 1, sz, f); fclose(f);
  if (sz < 44 || memcmp(raw.data(),"RIFF",4) != 0) return {};
  // Parse chunks
  int ch=0, fs=0, bits=0;
  const char* data = nullptr; long data_len = 0;
  size_t off = 12;
  while (off + 8 <= raw.size()) {
    const char* cid = raw.data() + off;
    long csize = *(int*)(raw.data() + off + 4);
    const char* body = raw.data() + off + 8;
    if (memcmp(cid,"fmt ",4)==0) {
      short fmt = *(short*)(body+0);
      ch = *(short*)(body+2); fs = *(int*)(body+4); bits = *(short*)(body+14);
    } else if (memcmp(cid,"data",4)==0) {
      data = body; data_len = csize;
    }
    off += 8 + csize + (csize & 1);
    if (memcmp(cid,"data",4)==0) break;
  }
  if (!data) return {};
  std::vector<float> out;
  if (bits==32 && ch>=1) {
    out.resize(data_len/4);
    memcpy(out.data(), data, data_len/4*4);   // 32-bit float
  } else if (bits==24) {
    long n = data_len/(3*ch);
    out.resize(n);
    for (long i=0;i<n;++i) {
      int v24 = (unsigned char)data[i*3*ch] | ((unsigned char)data[i*3*ch+1]<<8) |
                ((unsigned char)(data[i*3*ch+2]&0x7f)<<16);
      if (data[i*3*ch+2]&0x80) v24 -= 0x1000000>>8;
      int v24b = (unsigned char)data[i*3*ch] | ((unsigned char)data[i*3*ch+1]<<8)
                | (((unsigned char)data[i*3*ch+2])<<16);
      out[i] = (float)((v24b >= 0x800000) ? v24b-0x1000000 : v24b) / 8388608.0f;
    }
  } else if (bits==16) {
    long n = data_len/2/ch;
    out.resize(n);
    for (long i=0;i<n;++i)
      out[i] = (float)*((short*)(data+i*2*ch))/32768.0f;
  } else {
    fprintf(stderr, "  unsupported bits=%d\n", bits);
    return {};
  }
  out_fs = fs;
  // Take left channel (0) if stereo
  if (ch==2) {
    std::vector<float> L(out.size()/2);
    for (long i=0;i<(long)out.size()/2;++i) L[i]=out[i*2];
    out = std::move(L);
  }
  return out;
}

// --- 2. Simple O(N*M) convolution (good enough for short test signals) ---
std::vector<float> convolve(const std::vector<float>& x, const std::vector<float>& ir) {
  long n = x.size(), m = ir.size();
  std::vector<float> y(n + m - 1, 0.0f);
  for (long i = 0; i < n; ++i)
    for (long j = 0; j < m; ++j)
      y[i+j] += x[i] * ir[j];
  return y;
}

// --- 3. Spectral band measurement ---
void bands(const float* sig, long N, int fs, const char* label) {
  double freqs[]   = {125, 250, 500, 1000, 2000, 4000, 8000};
  const char* lbls[] = {"125H","250H","500H","1kH","2kH","4kH","8kH"};
  double raw[7];
  for (int i = 0; i < 7; ++i) {
    double w = 2.0*M_PI*freqs[i]/fs, re=0, im=0;
    for (int k = 0; k < N; ++k) { re += sig[k]*std::cos(w*k); im -= sig[k]*std::sin(w*k); }
    raw[i] = std::sqrt(re*re+im*im)/N;
  }
  double mx = 1e-15; for (int i=0;i<7;++i) mx = std::max(mx, raw[i]);
  printf("  %-10s", label);
  for (int i=0;i<7;++i) printf("  %s:%+5.1f", lbls[i], 20*std::log10(raw[i]/mx+1e-15));
  printf("\n");
}

// --- 4. Decay profile (RMS in 100ms windows) ---
void decay(const float* sig, long N, int fs, const char* label) {
  int win = fs/10;
  printf("  %-10s decay:", label);
  for (int w = 0; w < 20 && (w*win) < N; ++w) {
    float s = 0;
    for (int i = w*win; i < (w+1)*win && i < N; ++i) s += sig[i]*sig[i];
    float r = std::sqrt(s/win);
    printf(" %.1fms:%+.0fdB", w*100, 20*std::log10(r+1e-15));
  }
  printf("\n");
}

}  // anonymous

TEST(SpringCompare, ConvVsAlgo) {
  // Test IR: MR-III (the cleanest, most typical Nevo spring)
  const char* ir_path =
    "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
    "IR - WAV/NEVO - Studio Springs/NEVO - Master Room, MR-III/1. NEVO - MR-III, Flat.wav";

  int ir_fs = 0;
  auto ir = readWav(ir_path, ir_fs);
  if (ir.empty()) {
    // Try the XL-515 if MR-III not found
    ir_path =
      "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
      "IR - WAV/NEVO - Studio Springs/NEVO - Master Room, XL-515/1. NEVO - XL-515, Stereo.wav";
    ir = readWav(ir_path, ir_fs);
  }
  if (ir.empty()) {
    // Try the GBS
    ir_path =
      "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
      "IR - WAV/NEVO - Studio Springs/NEVO - Great British Spring/1. NEVO - GBS, Stereo.wav";
    ir = readWav(ir_path, ir_fs);
  }
  if (ir.empty()) {
    ADD_FAILURE() << "No IR loaded — check path";
    return;
  }
  printf("IR loaded: %ld samples @ %d Hz  (%.1f sec)\n", (long)ir.size(), ir_fs, (double)ir.size()/ir_fs);

  // Normalise IR to peak 1.0
  float ir_peak = 0;
  for (float v : ir) ir_peak = std::max(ir_peak, std::abs(v));
  float ir_norm = 1.0f / (ir_peak + 1e-15f);
  for (float& v : ir) v *= ir_norm;

  // --- Test signal: a 440 Hz tone, 200 ms, amplitude 1.0, then silence for 2 s ---
  const long sig_len = ir_fs * 2;  // 2 seconds total
  std::vector<float> sig(sig_len, 0.0f);
  int tone_n = ir_fs / 5;  // 200 ms of tone
  for (int i = 0; i < tone_n; ++i)
    sig[i] = std::sin(2.0f*M_PI*440.0f*i/ir_fs);

  // --- REFERENCE: convolve signal with the real IR ---
  auto ref = convolve(sig, ir);
  // Normalise reference to peak 1.0
  float ref_peak = 0;
  for (float v : ref) ref_peak = std::max(ref_peak, std::abs(v));
  for (float& v : ref) v /= (ref_peak + 1e-15f);

  // --- CANDIDATE: run the same signal through our Spring algorithm ---
  // Use N=3 springs, sag 30%, decay 1200 ms (middle of the range)
  Reverb r; r.prepare(kFs);
  double dm,pr,to,sz,wi; Reverb::defaultDialsForMode(1,dm,pr,to,sz,wi);
  Reverb::Params p;
  p.decayMs = 1200.0; p.tone = 0.50; p.size = 0.60; p.width = 0.0; p.preMs = 0.0;
  p.mode = 1; p.springs = 0.4;  // 3 springs
  p.sag = 0.30;
  r.setParams(p);

  juce::AudioBuffer<float> buf(1, sig_len);
  for (long i = 0; i < sig_len; ++i) buf.setSample(0, i, sig[i]);
  r.process(buf);
  std::vector<float> cand(sig_len, 0.0f);
  for (long i = 0; i < sig_len; ++i) cand[i] = buf.getSample(0, i);

  // Normalise candidate to peak 1.0 (apples-to-apples)
  float cand_peak = 0;
  for (float v : cand) cand_peak = std::max(cand_peak, std::abs(v));
  for (float& v : cand) v /= (cand_peak + 1e-15f);

  // --- COMPARE ---
  printf("\n=== SPECTRAL ENVELOPE (same signal, peak-normalised) ===\n");
  bands(ref.data(),  (long)ref.size(),  ir_fs,  "IR ref");
  bands(cand.data(), (long)cand.size(), ir_fs,  "Our algo");

  printf("\n=== DECAY PROFILE (RMS in 100ms windows, log scale) ===\n");
  decay(ref.data(),  (long)ref.size(),  ir_fs,  "IR ref");
  decay(cand.data(), (long)cand.size(), ir_fs,  "Our algo");

  // Cross-correlation of the spectral shape (similarity metric)
  // Compare band-by-band relative differences
  printf("\n=== BAND-TO-BAND DELTA (Our - IR, in dB) ===\n");
  double freqs[] = {125,250,500,1000,2000,4000,8000};
  const char* lbls[] = {"125H","250H","500H","1kH","2kH","4kH","8kH"};
  double ref_b[7], cand_b[7];
  for (int i = 0; i < 7; ++i) {
    double w=2.0*M_PI*freqs[i]/(double)ir_fs, re=0,im=0;
    for (int k=0;k<(int)ref.size();++k){re+=ref[k]*std::cos(w*k);im-=ref[k]*std::sin(w*k);}
    ref_b[i]=20*std::log10(std::sqrt(re*re+im*im)/ref.size()+1e-15);
    w=2.0*M_PI*freqs[i]/(double)ir_fs; re=im=0;
    for (int k=0;k<(int)cand.size();++k){re+=cand[k]*std::cos(w*k);im-=cand[k]*std::sin(w*k);}
    cand_b[i]=20*std::log10(std::sqrt(re*re+im*im)/cand.size()+1e-15);
  }
  // Normalise each to its own peak
  double rp=1e-15,cp=1e-15;
  for(int i=0;i<7;++i){rp=std::max(rp,ref_b[i]);cp=std::max(cp,cand_b[i]);}
  for (int i = 0; i < 7; ++i) {
    printf("  %s: IR:%+5.1f  Ours:%+5.1f  delta=%+5.1f dB\n",
           lbls[i], ref_b[i]-rp, cand_b[i]-cp, (cand_b[i]-cp)-(ref_b[i]-rp));
  }
}
