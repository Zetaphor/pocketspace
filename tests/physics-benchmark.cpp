#include "physics_world.h"
#include <cstdio>
#include <chrono>
using namespace physics;
int main(){
  for(int round=0;round<3;round++)for(int count:{48,128,256})for(int mode=0;mode<4;mode++)for(bool continuous:{true,false}){
    const int workers=(round%2?std::vector<int>{-1,2,1,0}:std::vector<int>{0,1,2,-1})[mode];
    World w;w.workers(workers);w.collisionEvents(true);
    Settings floor;floor.type=Type::Static;w.set(1,floor,{{0,-.1f,0}},{-3,-.1f,-3},{3,.1f,3});
    Settings s;s.continuous=continuous;
    for(int i=0;i<count;i++)w.set(i+2,s,{{float(i%8)*.12f-.42f,.15f+float(i/64)*.12f,float((i/8)%8)*.12f-.42f}},{-.05f,-.05f,-.05f},{.05f,.05f,.05f});
    double total=0;unsigned active=0;
    for(int frame=0;frame<420;frame++){
      // Keep the pile active so a sleeping workload cannot disguise solver cost.
      if(frame%30==0)for(int id=2;id<count+2;id++)w.impulse(id,{0,.1f,0});
      w.advance(1./90);w.takeCollisionEvents();w.takePoseUpdates();
      if(frame>=120){total+=w.performance().solverMs;active+=w.performance().activeBodies;}
    }
    printf("BENCH round=%d bodies=%d workers=%d continuous=%d solver_ms=%.4f active=%.1f\n",round,count,workers,int(continuous),total/300,double(active)/300);fflush(stdout);
  }
  World quiet;quiet.gravity({0,0,0});Settings settings;
  for(int id=1;id<=256;id++)quiet.set(id,settings,{{float(id),0,0}},{-.05f,-.05f,-.05f},{.05f,.05f,.05f});
  for(int i=0;i<180;i++){quiet.advance(1./90);quiet.takePoseUpdates();}
  using Clock=std::chrono::steady_clock;double sink=0;
  auto start=Clock::now();for(int frame=0;frame<2000;frame++)for(int id=1;id<=256;id++){sink+=quiet.get(id).pose.position.x;sink+=quiet.get(id).pose.position.y;}
  const double oldMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/2000;
  start=Clock::now();for(int frame=0;frame<2000;frame++){for(int id=1;id<=256;id++)sink+=quiet.needsTarget(id);sink+=quiet.takePoseUpdates().size();}
  const double newMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/2000;
  printf("BENCH sleeping_sync bodies=256 full_state_twice_ms=%.4f changed_pose_ms=%.4f checksum=%.0f\n",oldMs,newMs,sink);
}
