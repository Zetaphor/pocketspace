#include "physics_world.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
using namespace physics;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void box(World &w,int id,Type type,Vec3 p,Vec3 half={.05f,.05f,.05f},bool continuous=true){Settings s;s.type=type;s.continuous=continuous;w.set(id,s,{p},{-half.x,-half.y,-half.z},half);}
int main(){
  for(int workers:{0,1,2,4,-1}){
    World w;w.workers(workers);w.collisionEvents(true,true);
    box(w,1,Type::Static,{0,-.05f,0},{3,.05f,3});
    for(int i=0;i<96;i++)box(w,i+2,Type::Dynamic,{float(i%12)*.15f-.8f,.3f+float(i/12)*.16f,0});
    bool contacts=false,usedWorkers=false;std::map<int,Pose> rendered;
    for(int frame=0;frame<360;frame++){
      w.advance(1./90);auto p=w.performance();CHECK(p.steps==1&&p.solverMs>=0&&p.advanceMs>=p.solverMs);
      usedWorkers|=p.workers>0;contacts|=!w.takeCollisionEvents().empty();for(const auto &u:w.takePoseUpdates())rendered[u.objectId]=u.pose;
    }
    CHECK(contacts);CHECK(usedWorkers==(workers!=0));
    for(int i=2;i<98;i++){auto s=w.get(i);CHECK(std::isfinite(s.pose.position.y)&&s.pose.position.y>-.01f);CHECK(std::abs(rendered.at(i).position.y-s.pose.position.y)<1e-6f);}
    // Sleeping poses are synchronized once, then omitted until changed.
    World quiet;quiet.workers(workers);box(quiet,1,Type::Dynamic,{0,0,0});quiet.gravity({0,0,0});
    for(int i=0;i<360;i++){quiet.advance(1./90);quiet.takePoseUpdates();}
    CHECK(quiet.get(1).sleeping);CHECK(quiet.takePoseUpdates().empty());
    quiet.pose(1,{{1,2,3}});auto updates=quiet.takePoseUpdates();CHECK(updates.size()==1&&updates[0].pose.position.x==1);
    // Connection cache handles duplicate joints, replacement and body removal.
    box(quiet,2,Type::Dynamic,{1.2f,2,3});auto a=quiet.createJoint(1,2,{}),b=quiet.createJoint(1,2,{});
    quiet.removeJoint(a);CHECK(quiet.hasJoints(1));quiet.removeJoint(b);CHECK(!quiet.hasJoints(1)&&!quiet.hasJoints(2));
    a=quiet.createJoint(1,2,{});quiet.remove(2);CHECK(!quiet.hasJoints(1)&&!quiet.containsJoint(a));
    // Solver constraints and motors also run through each selected job system.
    World motor;motor.workers(workers);motor.gravity({0,0,0});
    box(motor,1,Type::Static,{0,0,0});box(motor,2,Type::Dynamic,{0,0,0});
    JointSettings hinge;hinge.type=JointType::Hinge;hinge.anchor=Vec3{0,0,0};
    auto joint=motor.createJoint(1,2,hinge);JointMotor drive;drive.mode=MotorMode::Position;drive.targetAngle=.6f;drive.maxTorque=10;
    motor.jointMotor(joint,drive);for(int frame=0;frame<180;frame++)motor.advance(1./90);
    CHECK(std::abs(motor.joint(joint).angle-.6f)<.05f);
    motor.hold(2);motor.target(2,{{.25f,.05f,0}},1./90);motor.advance(1./90);CHECK(motor.get(2).held&&motor.get(2).jointed);
    quiet.workers(0);quiet.advance(0);CHECK(quiet.performance().steps==0&&quiet.performance().workers==0);
  }
  // Continuous default catches a fast sphere crossing a thin floor in one step.
  for(bool continuous:{false,true}){
    World w;box(w,1,Type::Static,{0,0,0},{2,.005f,2});Settings s;s.shape=Shape::Sphere;s.radius=.02f;s.continuous=continuous;
    w.set(2,s,{{0,.5f,0}},{-.02f,-.02f,-.02f},{.02f,.02f,.02f});w.velocity(2,{0,-100,0});w.advance(1./90);
    CHECK(w.get(2).settings.continuous==continuous);CHECK(continuous?w.get(2).pose.position.y>0:w.get(2).pose.position.y<0);
  }
  puts("PASS physics performance: worker modes, concurrent contacts, sleeping synchronization, joint cache cleanup and continuous/discrete collision");
}
