#pragma once
#include <cmath>
#include <limits>
#include <optional>
#include <array>

namespace inputArbitration {
struct Target {
  int instanceId=0,localId=0;
  float distance=0;
};

inline bool better(const Target &candidate,const Target &current){
  if(candidate.distance!=current.distance)return candidate.distance<current.distance;
  if(candidate.instanceId!=current.instanceId)return candidate.instanceId<current.instanceId;
  return candidate.localId<current.localId;
}

inline void consider(std::optional<Target> &winner,const Target &candidate){
  if(candidate.instanceId<=0||candidate.localId<=0||!std::isfinite(candidate.distance)||candidate.distance<0||candidate.distance>=std::numeric_limits<float>::max())return;
  if(!winner||better(candidate,*winner))winner=candidate;
}

// A panel floats in front of what it covers, so it also wins an exact tie.
inline bool panelWins(float panelDistance,const std::optional<Target> &model){
  return !model||panelDistance<=model->distance;
}
}
