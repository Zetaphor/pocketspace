#pragma once
#include <cmath>
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
  if(candidate.instanceId<=0||candidate.localId<=0||!std::isfinite(candidate.distance)||candidate.distance<0)return;
  if(!winner||better(candidate,*winner))winner=candidate;
}

struct PanelOwner {int instanceId=0,localId=0;};
class PanelHoverOwners {
  std::array<PanelOwner,2> owners_{};
public:
  std::optional<PanelOwner> move(int hand,PanelOwner next){
    if(hand<0||hand>1||next.instanceId<=0||next.localId<=0)return {};
    const PanelOwner previous=owners_[hand];
    if(previous.instanceId==next.instanceId&&previous.localId==next.localId)return {};
    owners_[hand]=next;
    return previous.instanceId?std::optional<PanelOwner>{previous}:std::nullopt;
  }
  bool clear(int hand,PanelOwner owner){
    if(hand<0||hand>1||owners_[hand].instanceId!=owner.instanceId||owners_[hand].localId!=owner.localId)return false;
    owners_[hand]={};return true;
  }
  void clearInstance(int instanceId){for(auto &owner:owners_)if(owner.instanceId==instanceId)owner={};}
};
}
