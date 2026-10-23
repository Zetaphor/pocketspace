#include "physics_world.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <exception>
#include <chrono>
#include <vector>
#include <algorithm>
#include <string>
using namespace physics;
static void tick(World &w,int frames){for(int i=0;i<frames;i++)w.advance(1./90);}
static void benchmark(int count){
  World w;Settings fixed;fixed.type=Type::Static;
  w.set(1,fixed,{{0,-.5f,0}},{-3,-.1f,-3},{3,.1f,3});
  Settings body;std::vector<double> times;
  for(int i=0;i<count;i++)w.set(i+2,body,{{float(i%8)*.22f-.8f,.1f+float(i/64)*.22f,float(i/8%8)*.22f-.8f}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  for(int frame=0;frame<1800;frame++){
    if(frame%180==0)for(int i=0;i<count;i++){
      w.pose(i+2,{{float(i%8)*.22f-.8f,.1f+float(i/64)*.22f,float(i/8%8)*.22f-.8f}});
      w.velocity(i+2,{0,0,0});
    }
    auto start=std::chrono::steady_clock::now();w.advance(1./90);
    if(frame>=90)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
  double sum=0;for(double t:times)sum+=t;std::sort(times.begin(),times.end());
  printf("physics benchmark bodies=%d mean_ms=%.4f p95_ms=%.4f max_ms=%.4f\n",count,sum/times.size(),times[size_t(times.size()*.95)],times.back());
}
static void customColliders(){
  World w;Settings floor;floor.type=Type::Static;floor.halfExtents=Vec3{.8f,.025f,.8f};
  w.set(1,floor,{{0,-.425f,0}},{-.5f,-.5f,-.5f},{.5f,.5f,.5f});
  Settings box;box.halfExtents=Vec3{.1f,.05f,.1f};box.offset=Vec3{0,-.2f,0};
  w.set(2,box,{{0,.5f,0}},{-.5f,-.5f,-.5f},{.5f,.5f,.5f});tick(w,400);
  assert(std::abs(w.get(2).pose.position.y+.15f)<.01f);
  assert(w.get(2).collider.halfExtents.y==.05f&&w.get(2).collider.offset.y==-.2f);
  // Explicit dimensions and offset are independent of changed model bounds.
  w.set(2,box,w.get(2).pose,{-1,-1,-1},{1,1,1});
  assert(w.get(2).collider.halfExtents.y==.05f&&w.get(2).collider.offset.y==-.2f);
  const auto stable=w.get(2);
  auto rejects=[&](Settings bad){try{w.set(2,bad,{}, {-.1f,-.1f,-.1f},{.1f,.1f,.1f});assert(false);}catch(const std::exception&){}assert(w.get(2).collider.halfExtents.y==stable.collider.halfExtents.y);};
  auto bad=box;bad.halfExtents=Vec3{0,.1f,.1f};rejects(bad);
  bad=box;bad.halfExtents=Vec3{.1f,101,.1f};rejects(bad);
  bad=box;bad.offset=Vec3{0,0,std::numeric_limits<float>::quiet_NaN()};rejects(bad);
  bad=box;bad.offset=Vec3{101,0,0};rejects(bad);
  bad=box;bad.radius=.1f;rejects(bad);
  bad=box;bad.shape=Shape::Sphere;rejects(bad);
  box.halfExtents.reset();box.offset.reset();
  w.set(2,box,{{0,.2f,0}}, {-.1f,-.1f,-.1f},{.1f,.1f,.1f});tick(w,400);
  assert(std::abs(w.get(2).pose.position.y+.3f)<.01f);
  Settings sphere;sphere.shape=Shape::Sphere;sphere.radius=.08f;
  w.set(3,sphere,{{.3f,.5f,0}},{-.5f,-.5f,-.5f},{.5f,.5f,.5f});tick(w,400);
  assert(w.get(3).collider.radius==.08f&&std::abs(w.get(3).pose.position.y+.32f)<.01f);
  sphere.radius.reset();w.set(3,sphere,{{.3f,.5f,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});assert(w.get(3).collider.radius==.1f);
  // Local +Z-forward offset rotates with the engine's existing yaw convention.
  World rotated;floor.halfExtents=Vec3{.05f,.05f,.05f};floor.offset=Vec3{0,0,.2f};
  rotated.set(1,floor,{{0,0,0},0,float(std::acos(-1)/2),0},{-.5f,-.5f,-.5f},{.5f,.5f,.5f});
  sphere.radius=.02f;rotated.set(2,sphere,{{-.2f,.3f,0}},{-.5f,-.5f,-.5f},{.5f,.5f,.5f});tick(rotated,400);
  assert(std::abs(rotated.get(2).pose.position.y-.07f)<.01f);
  // Throw velocity must follow the offset center of mass during rotation.
  World orbit;orbit.gravity({0,0,0});box.halfExtents=Vec3{.05f,.05f,.05f};box.offset=Vec3{.2f,0,0};
  orbit.set(1,box,{}, {-.5f,-.5f,-.5f},{.5f,.5f,.5f});orbit.hold(1);
  for(int i=1;i<=15;i++){orbit.target(1,{{0,0,0},0,float(i)*.01f,0},1./90);orbit.advance(1./90);}
  orbit.release(1,true);assert(orbit.get(1).velocity.z>.17f&&orbit.get(1).velocity.z<.19f);
  assert(std::abs(orbit.get(1).angularVelocity.y-.9f)<.01f);
  orbit.advance(1./90);assert(std::abs(orbit.get(1).pose.position.z)<.001f);
  puts("PASS custom colliders: thin floor, box dimensions, sphere radius, offset resting height, scale independence, rotated local Z, null defaults, rejected replacement and center-of-mass throwing");
}
static void convexColliders(){
  World w;w.gravity({0,0,0});Settings s;s.shape=Shape::Convex;
  const std::vector<Vec3> tetra{{0,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0}};
  w.set(1,s,{}, {0,0,0},{1,1,1},{17,1,tetra});
  auto c=w.get(1).collider;assert(c.vertexCount==4);
  assert(std::abs(c.centerOfMass.x-.25f)<1e-5&&std::abs(c.centerOfMass.z+.25f)<1e-5);
  assert(std::abs(c.halfExtents.x-.5f)<1e-5&&w.hasConvexHull(17));
  w.set(2,s,{{2,0,0}},{0,0,0},{2,2,2},{17,2,{}});
  assert(std::abs(w.get(2).collider.centerOfMass.y-.5f)<1e-5);
  s.offset=Vec3{.2f,.3f,.4f};
  w.set(1,s,{}, {0,0,0},{1,1,1},{17,1,{}});
  c=w.get(1).collider;assert(std::abs(c.centerOfMass.x+.05f)<1e-5&&std::abs(c.centerOfMass.z-.65f)<1e-5);
  w.hold(1);
  for(int i=1;i<=10;i++){w.target(1,{{0,0,0},0,float(i)*.01f,0},1./90);w.advance(1./90);}
  w.release(1,true);assert(w.get(1).angularVelocity.y>.8f&&w.get(1).velocity.x<-.5f);
  const auto previous=w.get(1).pose;
  const std::vector<Vec3> plane{{0,0,0},{1,0,0},{0,1,0},{1,1,0}};
  try{w.set(1,s,{}, {0,0,0},{1,1,0},{18,1,plane});assert(false);}catch(const std::exception&){}
  assert(w.get(1).pose.yaw==previous.yaw&&!w.hasConvexHull(18));
  // The empty corner of a tetrahedron must not collide as its bounds box would.
  World contacts;contacts.gravity({0,0,0});contacts.collisionEvents(true);
  Settings fixed=s;fixed.offset.reset();fixed.type=Type::Static;
  contacts.set(1,fixed,{}, {0,0,0},{1,1,1},{17,1,tetra});
  Settings ball;ball.shape=Shape::Sphere;ball.radius=.04f;
  contacts.set(2,ball,{{.8f,.8f,-.8f}}, {-.04f,-.04f,-.04f},{.04f,.04f,.04f});
  contacts.advance(1./90);assert(contacts.takeCollisionEvents().empty());
  contacts.pose(2,{{.1f,.1f,-.1f}});contacts.advance(1./90);
  assert(!contacts.takeCollisionEvents().empty());
  puts("PASS convex: deduplication, shared hull scaling, asymmetric center of mass, offsets, angular throwing and degenerate replacement rollback");
}
int main(int argc,char **argv){
  if(argc>1&&std::string(argv[1])=="--benchmark"){for(int count:{16,64,128})benchmark(count);return 0;}

  customColliders();
  convexColliders();
  World world;Settings fixed;fixed.type=Type::Static;
  world.set(1,fixed,{{0,-.5f,0}}, {-2,-.1f,-2},{2,.1f,2});
  Settings dynamic;dynamic.linearDamping=0;dynamic.angularDamping=0;
  world.set(2,dynamic,{{0,.5f,0}}, {-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  tick(world,20);assert(world.get(2).pose.position.y<.4f);
  tick(world,350);auto resting=world.get(2);assert(std::abs(resting.pose.position.y+.3f)<.02f);assert(resting.sleeping);
  world.set(3,dynamic,{{0,.2f,0}}, {-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  tick(world,350);assert(std::abs(world.get(3).pose.position.y+.1f)<.025f);
  world.gravity({0,0,0});world.pose(3,{{0,1,0},.2f,.6f,-.3f});
  auto rotated=world.get(3).pose;assert(std::abs(rotated.pitch-.2f)<1e-5&&std::abs(rotated.yaw-.6f)<1e-5&&std::abs(rotated.roll+.3f)<1e-5);
  world.velocity(3,{0,0,1});tick(world,45);assert(world.get(3).pose.position.z>.49f);
  world.impulse(3,{1,0,0});tick(world,45);assert(world.get(3).pose.position.x>.49f);
  world.hold(3);for(int i=0;i<15;i++){world.target(3,{{float(i)*.01f,1,0}},1./90);world.advance(1./90);}
  assert(world.get(3).held);world.release(3,true);assert(world.get(3).velocity.x>.8f);
  world.hold(3);world.target(3,{{1,1,0}},1./90);world.advance(1./90);world.release(3,false);assert(world.get(3).velocity.x==0);
  world.hold(3);world.target(3,{{2,1,0}},1./90);world.cancelThrow(3);world.release(3,true);assert(world.get(3).velocity.x==0);
  world.hold(3);
  for(int i=0;i<15;i++){world.target(3,{{2,1,0},0,float(i)*.01f,0},1./90);world.advance(1./90);}
  world.release(3,true);assert(world.get(3).angularVelocity.y>.8f);
  Settings kinematic;kinematic.type=Type::Kinematic;
  const auto before=world.get(3).pose;auto bad=dynamic;bad.mass=-1;
  try{world.set(3,bad,{}, {-.1f,-.1f,-.1f},{.1f,.1f,.1f});assert(false);}catch(const std::exception&){}
  assert(world.get(3).pose.position.x==before.position.x);
  world.remove(2);assert(!world.contains(2));
  // A second world has independent gravity and bodies (reload candidates).
  World candidate;candidate.gravity({0,0,0});candidate.set(1,dynamic,{{0,1,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});tick(candidate,90);assert(candidate.get(1).pose.position.y==1);
  candidate.set(3,kinematic,{{0,3,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});
  candidate.target(3,{{0,3,.5f}},1./90);candidate.advance(1./90);assert(std::abs(candidate.get(3).pose.position.z-.5f)<1e-5);
  auto sphere=dynamic;sphere.shape=Shape::Sphere;candidate.set(2,sphere,{{0,2,0}},{-.1f,-.1f,-.1f},{.1f,.1f,.1f});tick(candidate,10);
  candidate.advance(10);assert(std::isfinite(candidate.get(2).pose.position.y));
  puts("PASS physics: gravity, resting contacts, stacking, sleep, orientation, scene Z, impulses, held throwing, cancellation, rejected replacement, removal, isolated worlds, sphere and catch-up");
}
