#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>

namespace tracking {
using Clock=std::chrono::steady_clock;
constexpr double kFreshMs=250;
inline double ageMs(Clock::time_point time,Clock::time_point now=Clock::now()){
  return time==Clock::time_point{}?-1:std::chrono::duration<double,std::milli>(now-time).count();
}
inline bool fresh(Clock::time_point time,Clock::time_point now=Clock::now()){
  const double age=ageMs(time,now);return age>=0&&age<=kFreshMs;
}
inline bool canPollGaze(bool requested,bool configured,uint32_t scenePid){return requested&&configured&&scenePid==0;}
struct Ray {float origin[3]{},target[3]{},direction[3]{};bool valid=false;};
// Standing -> fixed scene anchor, including the scene's +Z-forward convention.
inline Ray sceneRay(const float origin[3],const float target[3],const float anchor[3][4]){
  Ray ray;
  for(int axis=0;axis<3;axis++){
    for(int row=0;row<3;row++){
      ray.origin[axis]+=(origin[row]-anchor[row][3])*anchor[row][axis];
      ray.target[axis]+=(target[row]-anchor[row][3])*anchor[row][axis];
    }
    if(axis==2){ray.origin[axis]=-ray.origin[axis];ray.target[axis]=-ray.target[axis];}
    if(!std::isfinite(ray.origin[axis])||!std::isfinite(ray.target[axis]))return ray;
    ray.direction[axis]=ray.target[axis]-ray.origin[axis];
  }
  const float length=std::sqrt(ray.direction[0]*ray.direction[0]+ray.direction[1]*ray.direction[1]+ray.direction[2]*ray.direction[2]);
  if(!std::isfinite(length)||length<1e-6f)return ray;
  for(float &value:ray.direction)value/=length;
  ray.valid=true;return ray;
}
} // namespace tracking
