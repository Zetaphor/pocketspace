#include "../src/input_arbitration.h"
#include <algorithm>
#include <cassert>
#include <vector>

int main(){
  using inputArbitration::Target;
  for(int reverse=0;reverse<2;reverse++){
    std::vector<Target> hits{{2,1,.4f},{1,1,.3f}};
    if(reverse)std::reverse(hits.begin(),hits.end());
    std::optional<Target> winner;
    for(const auto &hit:hits)inputArbitration::consider(winner,hit);
    assert(winner&&winner->instanceId==1&&winner->localId==1);
  }
  std::optional<Target> winner;
  inputArbitration::consider(winner,{2,1,.3f});
  inputArbitration::consider(winner,{1,1,.3f});
  assert(winner->instanceId==1); // Stable instance-ID tie.
  inputArbitration::consider(winner,{3,1,.2f});
  assert(winner->instanceId==3);
  inputArbitration::consider(winner,{4,1,NAN});
  inputArbitration::consider(winner,{4,1,-1});
  assert(winner->instanceId==3);
  inputArbitration::PanelHoverOwners panels;
  assert(!panels.move(0,{1,1}));
  assert(!panels.move(0,{1,1}));
  auto previous=panels.move(0,{2,1});
  assert(previous&&previous->instanceId==1&&previous->localId==1);
  assert(!panels.clear(0,{1,1})); // Stale focus leave cannot clear the new owner.
  assert(!panels.move(1,{2,1})); // Each hand has independent focus.
  panels.clearInstance(2);
  assert(!panels.move(0,{3,1})&&!panels.move(1,{3,1}));
}
