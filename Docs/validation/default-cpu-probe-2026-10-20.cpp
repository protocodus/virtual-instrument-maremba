#include "MarembaEngine.h"
#include "MarembaControls.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <vector>
int main() {
 constexpr int warm=256, measured=4096, size=64; constexpr double rate=48000, deadline=1e6*size/rate;
 std::cout << std::setprecision(9) << "model,rate,frames,quality,voice_limit,max_active,warmup_blocks,measured_blocks,wall_mean_us,wall_p95_us,wall_p99_us,wall_max_us,cpu_mean_us,cpu_p95_us,cpu_p99_us,cpu_max_us,wall_misses,cpu_misses\n";
 for(int model=0;model<4;++model) {
  auto c=maremba::kControlDefaults;c[maremba::kModel]=model;
  maremba::MarembaEngine e(rate);e.SetParameters(maremba::EngineParametersFromControls(c));
  std::array<std::array<float,size>,2> audio{};
  std::vector<double> wall,cpu;wall.reserve(measured);cpu.reserve(measured);int active=0,wm=0,cm=0;
  for(int b=0;b<warm+measured;++b) {
   timespec x{},y{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&x);auto start=std::chrono::steady_clock::now();
   if(b%375==0)for(int interval:{0,4,7,12})e.NoteOn(43+3*((b/375)%4)+interval,80.f/127);
   e.RenderBatch(audio[0].data(),audio[1].data(),nullptr,nullptr,nullptr,nullptr,nullptr,size);
   double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();clock_gettime(CLOCK_THREAD_CPUTIME_ID,&y);
   double charged=(y.tv_sec-x.tv_sec)*1e6+(y.tv_nsec-x.tv_nsec)*1e-3;
   active=std::max(active,e.GetActiveVoiceCount());
   for(const auto& channel:audio)for(float v:channel)if(!std::isfinite(v)||std::abs(v)>.4466856)return 1;
   if(b>=warm){wall.push_back(elapsed);cpu.push_back(charged);wm+=elapsed>deadline;cm+=charged>deadline;}
  }
  std::cout<<model<<",48000,64,2,16,"<<active<<','<<warm<<','<<measured;
  for(auto* v:{&wall,&cpu}){double sum=0;for(double x:*v)sum+=x;std::sort(v->begin(),v->end());std::cout<<','<<sum/measured<<','<<(*v)[measured*95/100]<<','<<(*v)[measured*99/100]<<','<<v->back();}
  std::cout<<','<<wm<<','<<cm<<std::endl;
 }
}
