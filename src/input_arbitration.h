#pragma once
#include <cmath>
#include <optional>

namespace inputArbitration {
struct Target {
  int instanceId=0,localId=0,priority=0;
  float distance=0;
};

inline bool better(const Target &candidate,const Target &current){
  if(candidate.priority!=current.priority)return candidate.priority>current.priority;
  if(candidate.distance!=current.distance)return candidate.distance<current.distance;
  if(candidate.instanceId!=current.instanceId)return candidate.instanceId<current.instanceId;
  return candidate.localId<current.localId;
}

inline void consider(std::optional<Target> &winner,const Target &candidate){
  if(candidate.instanceId<=0||candidate.localId<=0||!std::isfinite(candidate.distance)||candidate.distance<0)return;
  if(!winner||better(candidate,*winner))winner=candidate;
}
}
