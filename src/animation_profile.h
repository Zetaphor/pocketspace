#pragma once
// Built only into excluded animation profiling targets; normal builds have no
// animation phase timers or profiling state.
#ifdef SF_ANIMATION_PROFILE
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
struct AnimationProfileFrame {
 double poseMs=0,deformBoundsMs=0,bvhMs=0,uploadMs=0,renderMs=0,finishMs=0,workMs=0;
 uint64_t evaluations=0,vertices=0,uploads=0,uploadBytes=0;
 void add(const AnimationProfileFrame &b){
  poseMs+=b.poseMs;deformBoundsMs+=b.deformBoundsMs;bvhMs+=b.bvhMs;uploadMs+=b.uploadMs;renderMs+=b.renderMs;finishMs+=b.finishMs;workMs+=b.workMs;
  evaluations+=b.evaluations;vertices+=b.vertices;uploads+=b.uploads;uploadBytes+=b.uploadBytes;
 }
};
inline AnimationProfileFrame *animationProfileFrame=nullptr;
using AnimationProfileClock=std::chrono::steady_clock;
inline double animationProfileMs(AnimationProfileClock::time_point start){return std::chrono::duration<double,std::milli>(AnimationProfileClock::now()-start).count();}
struct AnimationProfileWindow {
 static constexpr size_t capacity=120;
 AnimationProfileFrame total;std::array<double,capacity> work{};size_t frames=0,objects=0,playing=0;unsigned epoch=0;
 AnimationProfileClock::time_point start=AnimationProfileClock::now();
 void record(const AnimationProfileFrame &frame,size_t count,size_t active,unsigned generation){
  const auto now=AnimationProfileClock::now();
  if(count!=objects||active!=playing||generation!=epoch){objects=count;playing=active;epoch=generation;frames=0;total={};start=now;return;}
  total.add(frame);work[frames++]=frame.workMs;if(frames<capacity)return;
  auto sorted=work;std::sort(sorted.begin(),sorted.end());const double n=double(frames),seconds=std::chrono::duration<double>(now-start).count();
  std::fprintf(stdout,"ANIMATION_PROFILE {\"frames\":%zu,\"objects\":%zu,\"playing\":%zu,\"epoch\":%u,\"fps\":%.3f,\"seconds\":%.6f,\"poseMs\":%.6f,\"deformBoundsMs\":%.6f,\"bvhMs\":%.6f,\"uploadSubmitMs\":%.6f,\"renderSubmitMs\":%.6f,\"gpuWaitMs\":%.6f,\"workMs\":%.6f,\"workP95Ms\":%.6f,\"evaluations\":%.3f,\"vertices\":%.3f,\"uploads\":%.3f,\"uploadBytes\":%.3f}\n",frames,objects,playing,epoch,n/seconds,seconds,total.poseMs/n,total.deformBoundsMs/n,total.bvhMs/n,total.uploadMs/n,total.renderMs/n,total.finishMs/n,total.workMs/n,sorted[(capacity*95+99)/100-1],double(total.evaluations)/n,double(total.vertices)/n,double(total.uploads)/n,double(total.uploadBytes)/n);
  std::fflush(stdout);frames=0;total={};start=now;
 }
};
#endif
