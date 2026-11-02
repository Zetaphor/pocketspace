#include "../src/input_arbitration.h"
#include "../src/panel_pointer.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <algorithm>
#include <cassert>
#include <vector>

static bool near(float a,float b){return std::fabs(a-b)<.001f;}

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
  // meshHit reports a miss as the largest float, which must never select a target.
  {
    std::optional<Target> none;
    inputArbitration::consider(none,{1,1,std::numeric_limits<float>::max()});
    assert(!none);
  }
  // The closest of a panel and a model wins; an exact tie goes to the panel.
  std::optional<Target> model=Target{1,1,.5f};
  assert(inputArbitration::panelWins(.4f,model)&&inputArbitration::panelWins(.5f,model));
  assert(!inputArbitration::panelWins(.6f,model)&&inputArbitration::panelWins(9.f,std::nullopt));

  using namespace panelPointer;
  // A 1x.5 m panel, 1 m ahead on -Z, facing the viewer with Y up.
  const float pose[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,-1}};
  const float origin[3]={0,0,0},ahead[3]={0,0,-1};
  auto center=intersect(origin,ahead,pose,1,.5f,200,100);
  assert(center&&near(center->distance,1)&&near(center->x,100)&&near(center->y,50));
  const float topLeft[3]={-.4f,.2f,0};
  auto corner=intersect(topLeft,ahead,pose,1,.5f,200,100);
  assert(corner&&near(corner->x,20)&&near(corner->y,10)); // Canvas Y runs down.
  const float outside[3]={.6f,0,0},above[3]={0,.3f,0},behind[3]={0,0,-2},sideways[3]={1,0,0};
  assert(!intersect(outside,ahead,pose,1,.5f,200,100)&&!intersect(above,ahead,pose,1,.5f,200,100));
  assert(!intersect(behind,ahead,pose,1,.5f,200,100)&&!intersect(origin,sideways,pose,1,.5f,200,100));
  assert(!intersect(origin,ahead,pose,0,.5f,200,100));

  auto types=[](const std::vector<Event> &events){std::vector<EventType> out;for(const auto &event:events)out.push_back(event.type);return out;};
  auto step=[&](Pointer &pointer,std::optional<panelPointer::Target> target,bool trigger){std::vector<Event> out;pointer.step(target,trigger,out);return types(out);};
  using T=EventType;using List=std::vector<EventType>;
  const panelPointer::Target blue{1,1,10,10},orange{2,1,20,20};
  Pointer pointer;
  assert(step(pointer,blue,false)==(List{T::HoverStart,T::Hover}));
  assert(step(pointer,blue,false).empty()); // An unmoved ray is quiet.
  assert(step(pointer,panelPointer::Target{1,1,10.2f,10},false).empty());
  assert(step(pointer,panelPointer::Target{1,1,12,10},false)==(List{T::Hover}));
  // The same local ID in another script is a different panel.
  assert(step(pointer,orange,false)==(List{T::HoverEnd,T::HoverStart,T::Hover}));
  assert(step(pointer,orange,true)==(List{T::Down})&&pointer.pressed());
  assert(step(pointer,orange,true).empty()); // Holding is one click.
  // A press stays on its panel when the ray moves away, then releases there.
  assert(step(pointer,blue,true).empty()&&pointer.current().instanceId==2);
  assert(step(pointer,std::nullopt,true).empty()&&pointer.pressed());
  assert(step(pointer,blue,false)==(List{T::Up,T::HoverEnd,T::HoverStart,T::Hover})&&!pointer.pressed());
  assert(step(pointer,std::nullopt,false)==(List{T::HoverEnd}));
  // A trigger already held when the ray arrives is not a click, until pressed again.
  assert(step(pointer,std::nullopt,true).empty());
  assert(step(pointer,blue,true)==(List{T::HoverStart,T::Hover})&&!pointer.pressed());
  assert(step(pointer,blue,false).empty());
  assert(step(pointer,blue,true)==(List{T::Down}));
  // Removing a pressed panel still balances its events.
  {std::vector<Event> out;pointer.drop(out);assert(types(out)==(List{T::Up,T::HoverEnd})&&!pointer.pressed()&&!pointer.current().panelId);}
  // Reload or stop forgets silently, and only for that script.
  Pointer other;step(pointer,blue,false);step(other,orange,false);
  pointer.forgetInstance(1);other.forgetInstance(1);
  assert(!pointer.current().panelId&&other.current().panelId==1);
  puts("PASS input arbitration: model ties, panel/model selection, panel ray pixels, hover, press capture and cleanup");
}
