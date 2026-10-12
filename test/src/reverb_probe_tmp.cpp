// P2 plate fb-ceiling sweep (throwaway probe; deleted before closeout).
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include "Reverb.h"
#include "juce_audio_formats/juce_audio_formats.h"
namespace {
const int kFs = 48000, kBlock = 128;
double rmsAt(const std::vector<float>& x,int off,int len){
  if(off<0)off=0; len=std::min(len, std::max(0,(int)x.size()-off));
  double s=0; for(int i=0;i<len;++i)s+=(double)x[off+i]*x[off+i];
  return len?std::sqrt(s/len):0.0;}
struct PlateSw { // (unused: fields are public)
  static void setCeil(Reverb& r, double v){ r.plateCeilTest_ = v; }
  static void setLift(Reverb& r, double v){ r.plateLiftTest_ = v; }
};
// friend declaration added to Reverb.h
}
TEST(PlateProbe, CeilSweep){
  auto base = [&](){
    Reverb::Params p;
    double d,pr,to,sz,wi; Reverb::defaultDialsForMode(0,d,pr,to,sz,wi);
    p.decayMs=2000; p.preMs=pr; p.tone=to; p.size=sz; p.width=1.0f;
    p.mode=0; p.bright=0.5; p.bloom=0.5; return p;
  };
  std::vector<float> click(3.2f*kFs,0.0f); click[0]=1.0f;
  auto peakOf = [](const std::vector<float>& x){ double m=0; for(float v:x) m=std::max(m,(double)std::fabs(v)); return m; };
  auto run = [&](Reverb& r, const std::vector<float>& in){
    juce::AudioBuffer<float> buf(1,kBlock);
    for(int b=0;b<64;++b){buf.clear();r.process(buf);}
    std::vector<float> out(in.size(),0.0f);
    for(int off=0;off+kBlock<=(int)in.size();off+=kBlock){
      buf.clear(); for(int i=0;i<kBlock;++i)buf.setSample(0,i,in[off+i]);
      r.process(buf); for(int i=0;i<kBlock;++i)out[off+i]=buf.getSample(0,i);}
    return out;
  };
  auto report = [&](const char* tag, Reverb& r){
    auto out = run(r, click);
    double pk=peakOf(out);
    if (pk > 1.2) { std::cout<<" "<<tag<<" : **UNSTABLE peak="<<pk<<"**\n"; return; }
    double pkDb=20*std::log10(pk+1e-15);
    auto norm=[&](std::vector<float>& x){ if(pk>1e-15) for(auto&v:x) v=(float)(v/pk); };
    norm(out);
    double f=0; for(float v:out) f+=std::fabs(v);
    auto combWin=[&](double s0,double s1){
      double mx=0,ss=0; int nn=0;
      for(size_t i=s0*kFs; i<s1*kFs && i<out.size(); ++i){ double a=std::fabs(out[i]); mx=std::max(mx,a); ss+=a; ++nn; }
      return nn?20*std::log10(mx/(ss/nn)+1e-15):0.0; };
    auto rel=[&](double t){ return 20*std::log10(rmsAt(out,(int)(t*kFs),480)+1e-15)-20*std::log10(pk+1e-15); };
    std::cout<<" "<<tag<<" : full="<<f
             <<" comb(0.3-0.8s)="<<combWin(0.3,0.8)
             <<" comb(1.15-1.8s)="<<combWin(1.15,1.8)
             <<" |t1000="<<rel(1.0)<<"|t2000="<<rel(2.0)<<"|"<<std::endl;
  };
  std::cout<<"CEIL SWEEP @2000ms decay (ref plate: t1000~-55, t2000~-70):"<<std::endl;
  const double lifts[] = {1.104, 1.13, 1.15, 1.17};
  const double ceils[]  = {0.965};   // 0.965*1.03(drift)=0.9940 <1 -> stable
  for(double lift : lifts){
    for(double ceil : ceils){
      Reverb r; r.prepare(kFs);
      PlateSw::setLift(r, lift); PlateSw::setCeil(r, ceil);
      auto p = base(); r.setParams(p);
      report(("ceil="+std::to_string(ceil)+" lift="+std::to_string(lift)).c_str(), r);
    }
  }
  SUCCEED();
}
