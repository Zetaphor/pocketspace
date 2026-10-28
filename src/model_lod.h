#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
namespace modelLod {
// Conservative projected bounding-sphere diameter in headset pixels. All eyes
// and surfaces consume the one frame-level decision; no per-pass selection.
inline float projectedDiameter(float radius,float focal,float distanceSquared,float depth){
  if(!(focal>0)||depth<=radius)return std::numeric_limits<float>::infinity();
  const float offAxis=std::sqrt(std::max(0.f,distanceSquared-depth*depth))/depth;
  return 2*radius*focal*(1+offAxis)/(depth-radius);
}
inline int select(float pixels,float nearestDistance,int previous){
  if(!std::isfinite(pixels)||!std::isfinite(nearestDistance)||nearestDistance<(previous==0?.40f:.35f))return 0;
  if(previous==2){if(pixels<=138)return 2;return pixels<=345?1:0;}
  if(previous==1){if(pixels<102)return 2;return pixels<=345?1:0;}
  return pixels<102?2:pixels<255?1:0;
}
}
