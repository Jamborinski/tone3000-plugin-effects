// Full spring family comparison: noise burst through all 5 Nevo units
// + our algorithm. Average the 5 IRs to get the "typical spring" target.
#include "Reverb.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <dirent.h>

namespace {

constexpr int kFs = 48000;
constexpr int kBurstMs = 30;  // 30 ms noise burst (tighter, like a drum hit)

std::vector<float> readWav(const char* path, int& out_fs) {
  FILE* f = fopen(path, "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<char> raw(sz); fread(raw.data(), 1, sz, f); fclose(f);
  if (sz < 44 || memcmp(raw.data(),"RIFF",4) != 0) return {};
  int ch=0, fs=0, bits=0;
  const char* data=nullptr; long data_len=0;
  size_t off=12;
  while (off+8 <= raw.size()) {
    const char* cid = raw.data()+off;
    long csize = *(int*)(raw.data()+off+4);
    const char* body = raw.data()+off+8;
    if (memcmp(cid,"fmt ",4)==0)    { ch=*(short*)(body+2); fs=*(int*)(body+4); bits=*(short*)(body+14); }
    else if (memcmp(cid,"data",4)==0) { data=body; data_len=csize; }
    off += 8+csize+(csize&1);
    if (memcmp(cid,"data",4)==0) break;
  }
  if (!data) return {};
  std::vector<float> out;
  if (bits==32)         { long n=data_len/4; out.resize(n); memcpy(out.data(), data, n*4); }
  else if (bits==24) { long n=data_len/3; out.resize(n);
    for(long i=0;i<n;++i){int v=(uint8_t)data[i*3]|((uint8_t)data[i*3+1]<<8)|(data[i*3+2]<<16);
      if(data[i*3+2]&0x80) v-=0x1000000; out[i]=(float)v/8388608.0f;} }
  else if (bits==16) { long n=data_len/2; out.resize(n);
    for(long i=0;i<n;++i) out[i]=(float)*((short*)(data+i*2))/32768.0f; }
  else return {};
  if (ch==2) { std::vector<float>L(out.size()/2);
    for(long i=0;i<(long)out.size()/2;++i) L[i]=out[i*2]; out=std::move(L); }
  out_fs=fs; return out;
}

std::vector<float> convolve(const std::vector<float>& x, const std::vector<float>& ir) {
  long n=x.size(), m=ir.size();
  std::vector<float> y(n+m-1, 0.0f);
  for (long i=0;i<n;++i) for(long j=0;j<m;++j) y[i+j]+=x[i]*ir[j];
  return y;
}

long lcg_s=1234567L;
float lcg(){lcg_s=lcg_s*1103515245L+12345L;return ((float)((lcg_s>>16)&0x7FFF)/32768.0f)*2.0f-1.0f;}

double magAt(const float* sig, long N, int fs, double f) {
  double w=2.0*M_PI*f/fs, re=0, im=0;
  for(long k=0;k<N;++k){re+=sig[k]*std::cos(w*k); im-=sig[k]*std::sin(w*k);}
  return std::sqrt(re*re+im*im)/(double)N;
}

struct BandResult { double val; bool valid; };

// Average band measurement across K springs (all peak-normalised to their own IR)
BandResult avgBand(const std::vector<std::vector<float>>& outputs, long M, int fs, double freq) {
  double sum=0; int n=0;
  for (auto& out : outputs) {
    if (out.size() < M) continue;
    double mag = magAt(out.data(), M, fs, freq);
    // Peak-normalise to this output's peak
    float pk=0; for(long i=0;i<M;++i) pk=std::max(pk, std::abs(out[i]));
    double norm_pk = mag / (pk+1e-15);
    sum += 20.0*std::log10(norm_pk + 1e-15);
    ++n;
  }
  if (n==0) return {-999.0, false};
  return {sum/(double)n, true};
}

}  // anonymous

TEST(SpringCompare, FullFamily) {
  // All 5 Nevo units (one WAV each, the first in each folder)
  const char* base =
    "/mnt/c/Impulse Responses/Convolution Reverb IRs/Nevo Studios/NEVO - Studio Springs/"
    "IR - WAV/NEVO - Studio Springs/";
  const char* units[] = {
    "NEVO - Great British Spring/1. NEVO - GBS, Stereo.wav",
    "NEVO - Master Room, MR-III/1. NEVO - MR-III, Flat.wav",
    "NEVO - Master Room, MR-IV/1. NEVO - MR-IV, Flat.wav",
    "NEVO - Master Room, XL-515/1. NEVO - XL-515, Hall - Short.wav",
    
  };

  // Input: 30 ms noise burst, padded to 5 s
  const long total = kFs * 5;
  const long burst_n = kFs * kBurstMs / 1000;
  std::vector<float> sig(total, 0.0f);
  for (long i=0;i<burst_n;++i) sig[i]=lcg();
  float s_pk=0; for(float v:sig) s_pk=std::max(s_pk,std::abs(v));
  for(float&v:sig) v*=(0.5f/(s_pk+1e-15f));

  // Reference outputs (convolved with each IR)
  std::vector<std::vector<float>> ir_outs;
  int good_n=0;
  for (auto rel : units) {
    char path[1024]; snprintf(path, sizeof(path), "%s%s", base, rel);
    int fs=0; auto ir = readWav(path, fs);
    if (ir.empty()) { printf("  skip: %s\n", rel); continue; }
    float pk=0; for(float v:ir) pk=std::max(pk,std::abs(v));
    for(float&v:ir) v*=(1.0f/(pk+1e-15f));
    auto out = convolve(sig, ir);
    out.resize(total, 0.0f);  // pad or truncate to `total`
    float opk=0; for(float v:out) opk=std::max(opk,std::abs(v));
    for(float&v:out) v*=(1.0f/(opk+1e-15f));
    ir_outs.push_back(std::move(out));
    printf("  loaded: %s (%ld samples)\n", rel, (long)ir.size());
    ++good_n;
  }
  if (good_n < 2) { ADD_FAILURE() << "Need at least 2 IRs"; return; }

  long M = std::min<long>(total, 96000);  // 2 seconds of output

  printf("\n=== INDIVIDUAL SPRINGS (peak-normalised, 2 s window) ===\n");
  const double freqs[]  = {63, 125, 250, 500, 1000, 2000, 4000, 8000, 12000};
  const char* lbls[]    = {"63H","125H","250H","500H","1kH","2kH","4kH","8kH","12k"};
  for (int k=0; k<(int)ir_outs.size(); ++k) {
    printf("  Spring %d:", k);
    for (int i=0;i<9;++i) {
      double mag = magAt(ir_outs[k].data(), M, kFs, freqs[i]);
      float pk=0; for(long j=0;j<M;++j) pk=std::max(pk,std::abs(ir_outs[k][j]));
      printf(" %s:%+5.1f", lbls[i], 20.0*std::log10(mag/(pk+1e-15)+1e-15));
    }
    printf("\n");
  }

  printf("\n=== AVERAGE OF %d SPRINGS (the target) ===\n", good_n);
  double avg[9], our_b[9];
  for (int i=0;i<9;++i) {
    auto r = avgBand(ir_outs, M, kFs, freqs[i]);
    avg[i] = r.val;
  }
  printf("  Average: ");
  for (int i=0;i<9;++i) printf("%s:%+5.1f  ", lbls[i], avg[i]);
  printf("\n");

  // Our algorithm (3 springs, sag 30%)
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
  float cpk=0; for(float v:cand) cpk=std::max(cpk,std::abs(v));
  for(float&v:cand) v*=(1.0f/(cpk+1e-15f));

  for (int i=0;i<9;++i) {
    double mag = magAt(cand.data(), M, kFs, freqs[i]);
    our_b[i] = 20.0*std::log10(mag/(cpk+1e-15)+1e-15);
  }
  printf("  Ours:    ");
  for (int i=0;i<9;++i) printf("%s:%+5.1f  ", lbls[i], our_b[i]);
  printf("\n");

  // Delta and RMS
  printf("  DELTA:   ");
  double sum_sq=0;
  for (int i=0;i<9;++i) {
    double d = our_b[i] - avg[i];
    sum_sq += d*d;
    printf("%s:%+5.1f  ", lbls[i], d);
  }
  printf("\n");
  printf("  RMS spectral distance: %.1f dB  (0=perfect, <5 dB=close, >10 dB=big mismatch)\n",
         std::sqrt(sum_sq/9.0));

  // Decay comparison (average across all IRs)
  printf("\n=== DECAY (average of %d springs, vs ours) ===\n", good_n);
  int win = kFs/10;  // 100 ms
  int maxw = std::min((int)(M/win), 30);
  for (int w = 0; w < maxw && w < 20; ++w) {
    // IR average decay
    double ir_avg = 0; int cnt=0;
    for (auto& out : ir_outs) {
      float s=0; for(long i=w*win;i<(w+1)*win && i<M;++i) s+=out[i]*out[i];
      float r2=std::sqrt(s/((float)win));
      float pk=0; for(long j=0;j<M;++j) pk=std::max(pk,std::abs(out[j]));
      ir_avg += 20.0*std::log10(r2/(pk+1e-15)+1e-15); ++cnt;
    }
    ir_avg /= std::max(1,cnt);
    // Our decay
    float s2=0; for(long i=w*win;i<(w+1)*win && i<M;++i) s2+=cand[i]*cand[i];
    float o2=std::sqrt(s2/((float)win));
    float opk=0; for(long j=0;j<M;++j) opk=std::max(opk,std::abs(cand[j]));
    double our = 20.0*std::log10(o2/(opk+1e-15)+1e-15);
    printf("  %0.1fs:  IR avg:%+5.1f dB   Ours:%+5.1f dB   delta:%+5.1f dB\n",
           w*0.1, ir_avg, our, our-ir_avg);
  }
}
