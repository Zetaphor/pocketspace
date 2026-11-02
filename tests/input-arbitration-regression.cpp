#include "../src/input_arbitration.h"
#include <algorithm>
#include <cassert>
#include <vector>

int main(){
  using inputArbitration::Target;
  for(int reverse=0;reverse<2;reverse++){
    std::vector<Target> hits{{2,1,0,.4f},{1,1,0,.3f}};
    if(reverse)std::reverse(hits.begin(),hits.end());
    std::optional<Target> winner;
    for(const auto &hit:hits)inputArbitration::consider(winner,hit);
    assert(winner&&winner->instanceId==1&&winner->localId==1);
  }
  std::optional<Target> winner;
  inputArbitration::consider(winner,{2,1,0,.3f});
  inputArbitration::consider(winner,{1,1,0,.3f});
  assert(winner->instanceId==1); // Stable instance-ID tie.
  inputArbitration::consider(winner,{2,1,1,.8f});
  assert(winner->instanceId==2); // Explicit priority outranks distance.
  inputArbitration::consider(winner,{3,1,1,.2f});
  assert(winner->instanceId==3); // Distance within a priority tier.
  inputArbitration::consider(winner,{4,1,2,NAN});
  inputArbitration::consider(winner,{4,1,2,-1});
  assert(winner->instanceId==3);
}
