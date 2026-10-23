#include "physics_world.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace physics;
static void tick(World &w,int n){for(int i=0;i<n;i++)w.advance(1./90);}
static void body(World &w,int id,Type type,Pose p){Settings s;s.type=type;w.set(id,s,p,{-.04f,-.04f,-.04f},{.04f,.04f,.04f});}
template<class F>static void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}assert(failed);}
int main(){
  World w;body(w,1,Type::Static,{{0,1,0}});body(w,2,Type::Dynamic,{{.3f,1,0},.2f,.3f,-.1f});
  JointSettings fixed;int weld=w.createJoint(1,2,fixed);tick(w,180);
  auto state=w.get(2);assert(std::abs(state.pose.position.x-.3f)<.005f&&std::abs(state.pose.position.y-1)<.005f);
  assert(std::abs(state.pose.yaw-.3f)<.005f&&state.jointed);
  rejects([&]{body(w,2,Type::Dynamic,{});});assert(w.containsJoint(weld));
  w.removeJoint(weld);w.removeJoint(weld);tick(w,60);assert(w.get(2).pose.position.y<0);
  w.pose(2,{{.3f,1,0}});w.velocity(2,{});w.angularVelocity(2,{});
  JointSettings hinge;hinge.type=JointType::Hinge;hinge.anchor=Vec3{0,1,0};hinge.axis={0,0,1};hinge.minAngle=-.4f;hinge.maxAngle=.4f;
  int door=w.createJoint(1,2,hinge);tick(w,180);
  assert(std::abs(w.joint(door).angle)<=.42f&&w.joint(door).distance<.01f);
  w.hold(2);for(int i=0;i<180;i++){w.target(2,{{.8f,1.5f,.3f},0,0,2},1./90);w.advance(1./90);}
  assert(w.joint(door).distance<.03f&&std::abs(w.joint(door).angle)<.5f);
  w.release(2,false);tick(w,90);assert(w.joint(door).distance<.01f);
  w.remove(1);assert(!w.containsJoint(door)&&!w.get(2).jointed);
  body(w,1,Type::Static,{{0,1,0}});w.pose(2,{{0,.7f,.1f}});w.velocity(2,{});w.angularVelocity(2,{});
  JointSettings rope;rope.type=JointType::Distance;rope.minDistance=.2f;rope.maxDistance=.4f;
  int tether=w.createJoint(1,2,rope);tick(w,180);
  assert(w.joint(tether).distance<=.405f&&w.joint(tether).distance>=.195f);
  rejects([&]{w.createJoint(1,1,rope);});rejects([&]{w.createJoint(1,99,rope);});
  rope.maxDistance=.1f;rejects([&]{w.createJoint(1,2,rope);});assert(w.containsJoint(tether));
  w.hold(2);w.removeJoint(tether);w.target(2,{{0,2,0}},1./90);tick(w,1);assert(std::abs(w.get(2).pose.position.y-2)<.001f);w.release(2,false);
  // A disabled collision pair remains disabled until every disabling joint is removed.
  World contacts;contacts.gravity({});contacts.collisionEvents(true);
  body(contacts,1,Type::Static,{});body(contacts,2,Type::Dynamic,{});
  int first=contacts.createJoint(1,2,fixed),second=contacts.createJoint(1,2,fixed);
  tick(contacts,2);assert(contacts.takeCollisionEvents().empty());
  contacts.removeJoint(first);tick(contacts,2);assert(contacts.takeCollisionEvents().empty());
  contacts.removeJoint(second);tick(contacts,2);assert(!contacts.takeCollisionEvents().empty());
  puts("PASS joints: fixed transforms, hinge limits and held pivot, distance range, cleanup, rebuild rejection, collision filtering and held removal");
}
