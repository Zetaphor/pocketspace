#include "physics_world.h"
#include "frame_input.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace physics;
static void tick(World &world,int frames){for(int i=0;i<frames;i++)world.advance(1./90);}
int main(){
  World world;Settings fixed;fixed.type=Type::Static;
  Settings dynamic;dynamic.restitution=0;
  world.set(9,fixed,{{0,-.5f,0}},{-2,-.1f,-2},{2,.1f,2});
  world.set(3,dynamic,{{0,.5f,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  tick(world,400);assert(world.get(3).sleeping&&world.takeCollisionEvents().empty());
  // Enabling on an already sleeping pair wakes it to establish contact state.
  world.collisionEvents(true,true);
  int starts=0,stays=0,ends=0;
  for(int i=0;i<400;i++){
    world.advance(1./90);
    for(const auto &event:world.takeCollisionEvents()){
      assert(event.objectId==3&&event.otherObjectId==9);
      assert(event.normal.y<-.99f&&std::abs(event.point.y+.4f)<.03f);
      assert(std::isfinite(event.relativeSpeed)&&event.relativeSpeed>=0);
      starts+=event.phase==CollisionPhase::Start;stays+=event.phase==CollisionPhase::Stay;ends+=event.phase==CollisionPhase::End;
    }
  }
  assert(starts==1&&stays==399&&ends==0&&world.get(3).sleeping);
  world.advance(1./90);auto sleeping=world.takeCollisionEvents();assert(sleeping.size()==1&&sleeping[0].relativeSpeed==0);
  world.pose(3,world.get(3).pose);assert(world.takeCollisionEvents().empty());
  world.pose(3,{{0,1,0}});auto end=world.takeCollisionEvents();assert(end.size()==1&&end[0].phase==CollisionPhase::End);
  world.velocity(3,{0,0,0});starts=0;
  for(int i=0;i<200;i++){
    world.advance(1./90);for(const auto &event:world.takeCollisionEvents())if(event.phase==CollisionPhase::Start){starts++;assert(event.relativeSpeed>1);}
  }
  assert(starts==1);
  // Replacing a collider ends the old pair and starts a new body generation.
  world.set(3,dynamic,world.get(3).pose,{-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  auto replaced=world.takeCollisionEvents();assert(replaced.size()==1&&replaced[0].phase==CollisionPhase::End);
  world.advance(1./90);auto replacedStart=world.takeCollisionEvents();assert(replacedStart.size()==1&&replacedStart[0].phase==CollisionPhase::Start);
  world.remove(3);auto removed=world.takeCollisionEvents();assert(removed.size()==1&&removed[0].phase==CollisionPhase::End);
  tick(world,2);assert(world.takeCollisionEvents().empty());
  world.set(4,dynamic,{{0,-.3f,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  tick(world,2);auto reused=world.takeCollisionEvents();assert(!reused.empty());for(const auto &event:reused)assert(event.objectId==4&&event.otherObjectId==9);
  world.collisionEvents(true,false);tick(world,20);assert(world.takeCollisionEvents().empty());
  world.collisionEvents(false);tick(world,20);assert(world.takeCollisionEvents().empty());
  // Normals/points use scene +Z-forward, with sorted public IDs.
  World forward;forward.gravity({0,0,0});forward.collisionEvents(true);
  forward.set(1,fixed,{{0,0,.3f}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  forward.set(2,dynamic,{{0,0,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});forward.velocity(2,{0,0,1});
  tick(forward,30);auto hit=forward.takeCollisionEvents();bool found=false;
  for(const auto &event:hit)if(event.phase==CollisionPhase::Start){found=true;assert(event.normal.z<-.99f&&event.point.z>.15f&&event.relativeSpeed>.5f);}
  assert(found);
  // Pending module events remain bounded when a consumer does not drain them.
  world.collisionEvents(true,true);tick(world,5000);assert(world.takeCollisionEvents().size()<=4096);
  frame_input::Monitor monitor;frame_input::Event event;event.type="physics.collisionStart";event.objectId=4;event.otherObjectId=9;event.point={1,2,3};
  monitor.addEvent(event);monitor.stop(); // Input loss must not discard physics.
  auto polled=monitor.takeEvents(),callbacks=monitor.takeCallbackEvents();
  assert(polled.size()==1&&callbacks.size()==1&&polled[0].point[2]==3&&callbacks[0].otherObjectId==9);
  monitor.addEvent(event);monitor.clearPhysicsEvents();assert(monitor.takeEvents().empty()&&monitor.takeCallbackEvents().empty());
  monitor.addEvent(event);monitor.resetScene();assert(monitor.takeEvents().empty()&&monitor.takeCallbackEvents().empty());
  puts("PASS collision events: opt-in, sleeping contact continuity, canonical IDs/normal, impact speed, teleport/removal/replacement, body ID reuse, scene Z, stay toggle, queue bounds, independent delivery and input-loss isolation");
}
