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
int main(int argc,char **argv){
  if(argc>1&&std::string(argv[1])=="--benchmark"){for(int count:{16,64,128})benchmark(count);return 0;}

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
