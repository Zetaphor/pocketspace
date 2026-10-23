#include "physics_world.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace physics;
// Report regression failures without aborting into the desktop crash reporter.
static void check(bool ok,const char *expression,int line){if(!ok){std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::exit(EXIT_FAILURE);}}
#define CHECK(expression) check(bool(expression),#expression,__LINE__)
static void tick(World &w,int n){for(int i=0;i<n;i++)w.advance(1./90);}
static void body(World &w,int id,Type type,Pose p){Settings s;s.type=type;w.set(id,s,p,{-.04f,-.04f,-.04f},{.04f,.04f,.04f});}
template<class F>static void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
int main(){
  World w;body(w,1,Type::Static,{{0,1,0}});body(w,2,Type::Dynamic,{{.3f,1,0},.2f,.3f,-.1f});
  JointSettings fixed;int weld=w.createJoint(1,2,fixed);tick(w,180);
  auto state=w.get(2);CHECK(std::abs(state.pose.position.x-.3f)<.005f&&std::abs(state.pose.position.y-1)<.005f);
  CHECK(std::abs(state.pose.yaw-.3f)<.005f&&state.jointed);
  rejects([&]{body(w,2,Type::Dynamic,{});});CHECK(w.containsJoint(weld));
  w.removeJoint(weld);w.removeJoint(weld);tick(w,60);CHECK(w.get(2).pose.position.y<0);
  w.pose(2,{{.3f,1,0}});w.velocity(2,{});w.angularVelocity(2,{});
  JointSettings hinge;hinge.type=JointType::Hinge;hinge.anchor=Vec3{0,1,0};hinge.axis={0,0,1};hinge.minAngle=-.4f;hinge.maxAngle=.4f;
  int door=w.createJoint(1,2,hinge);tick(w,180);
  CHECK(std::abs(w.joint(door).angle)<=.42f&&w.joint(door).distance<.01f);
  w.hold(2);for(int i=0;i<180;i++){w.target(2,{{.8f,1.5f,.3f},0,0,2},1./90);w.advance(1./90);}
  CHECK(w.joint(door).distance<.03f&&std::abs(w.joint(door).angle)<.5f);
  w.release(2,false);tick(w,90);CHECK(w.joint(door).distance<.01f);
  w.remove(1);CHECK(!w.containsJoint(door)&&!w.get(2).jointed);
  body(w,1,Type::Static,{{0,1,0}});w.pose(2,{{0,.7f,.1f}});w.velocity(2,{});w.angularVelocity(2,{});
  JointSettings rope;rope.type=JointType::Distance;rope.minDistance=.2f;rope.maxDistance=.4f;
  int tether=w.createJoint(1,2,rope);tick(w,180);
  CHECK(w.joint(tether).distance<=.405f&&w.joint(tether).distance>=.195f);
  rejects([&]{w.createJoint(1,1,rope);});rejects([&]{w.createJoint(1,99,rope);});
  rope.maxDistance=.1f;rejects([&]{w.createJoint(1,2,rope);});CHECK(w.containsJoint(tether));
  w.hold(2);w.removeJoint(tether);w.target(2,{{0,2,0}},1./90);tick(w,1);CHECK(std::abs(w.get(2).pose.position.y-2)<.001f);w.release(2,false);
  // A disabled collision pair remains disabled until every disabling joint is removed.
  World contacts;contacts.gravity({});contacts.collisionEvents(true);
  body(contacts,1,Type::Static,{});body(contacts,2,Type::Dynamic,{});
  int first=contacts.createJoint(1,2,fixed),second=contacts.createJoint(1,2,fixed);
  tick(contacts,2);CHECK(contacts.takeCollisionEvents().empty());
  contacts.removeJoint(first);tick(contacts,2);CHECK(contacts.takeCollisionEvents().empty());
  contacts.removeJoint(second);tick(contacts,2);CHECK(!contacts.takeCollisionEvents().empty());
  World motors;motors.gravity({});body(motors,1,Type::Static,{});body(motors,2,Type::Dynamic,{});
  hinge.anchor=Vec3{0,0,0};hinge.axis={0,1,0};hinge.minAngle=-.8f;hinge.maxAngle=.8f;
  const int powered=motors.createJoint(1,2,hinge);
  JointMotor motor;motor.mode=MotorMode::Position;motor.targetAngle=.6f;motor.maxTorque=.2f;
  motors.jointMotor(powered,motor);tick(motors,270);
  CHECK(std::abs(motors.joint(powered).angle-.6f)<.02f);
  tick(motors,270); // Updating a sleeping assembly must wake it.
  motor.targetAngle=-.6f;motors.jointMotor(powered,motor);tick(motors,270);
  CHECK(std::abs(motors.joint(powered).angle+.6f)<.02f);
  const auto before=motors.joint(powered).motor;
  motor.targetAngle=1;rejects([&]{motors.jointMotor(powered,motor);});
  CHECK(motors.joint(powered).motor.targetAngle==before.targetAngle);
  motor.targetAngle=0;motor.mode=MotorMode::Velocity;motor.targetVelocity=1;
  motors.jointMotor(powered,motor);tick(motors,270);
  CHECK(motors.joint(powered).angle>.76f&&motors.joint(powered).angle<.83f); // velocity respects limits
  motor.mode=MotorMode::Off;motors.jointMotor(powered,motor);
  CHECK(motors.joint(powered).motor.mode==MotorMode::Off);
  motors.removeJoint(powered);rejects([&]{motors.jointMotor(powered,motor);});
  // No torque means no motor acceleration; velocity mode has a signed speed target.
  World free;free.gravity({});body(free,1,Type::Static,{});body(free,2,Type::Dynamic,{});
  hinge.minAngle=-3.14159265359f;hinge.maxAngle=3.14159265359f;
  int rotor=free.createJoint(1,2,hinge);motor.mode=MotorMode::Velocity;motor.targetVelocity=-.5f;motor.maxTorque=0;
  free.jointMotor(rotor,motor);tick(free,90);CHECK(std::abs(free.joint(rotor).angle)<1e-5);
  motor.maxTorque=.2f;free.jointMotor(rotor,motor);tick(free,90);
  CHECK(free.joint(rotor).angle<-.45f&&free.joint(rotor).angle>-.55f);
  motor.mode=MotorMode::Off;free.jointMotor(rotor,motor);const float angle=free.joint(rotor).angle;tick(free,45);
  CHECK(free.joint(rotor).angle<angle-.15f); // off coasts, rather than braking
  // Match a panel whose center is away from the hinge, including hand opposition.
  World panel;body(panel,1,Type::Static,{});
  Settings slab;slab.mass=.25f;panel.set(2,slab,{{.15f,0,0},1.57079632679f,0,0},{-.15f,-.0075f,-.15f},{.15f,.0075f,.15f});
  hinge.minAngle=-1.2f;hinge.maxAngle=1.2f;
  int panelJoint=panel.createJoint(1,2,hinge);motor.mode=MotorMode::Position;motor.targetAngle=.9f;motor.frequency=3;motor.maxTorque=.15f;
  panel.jointMotor(panelJoint,motor);tick(panel,270);CHECK(std::abs(panel.joint(panelJoint).angle-.9f)<.04f);
  panel.hold(2);for(int i=0;i<90;i++){panel.target(2,{{.15f,0,0},1.57079632679f,-.5f,0},1./90);panel.advance(1./90);}
  CHECK(panel.joint(panelJoint).distance<.03f);panel.release(2,false);tick(panel,450);
  CHECK(std::abs(panel.joint(panelJoint).angle-.9f)<.04f);
  // Reconstruct the rendered basis while a panel rotates through many turns
  // at the Euler pitch singularity. Angle wrapping must not change its pose.
  for(float pitch:{1.57079632679f,-1.57079632679f}){
    World spinning;spinning.gravity({});body(spinning,1,Type::Static,{});
    body(spinning,2,Type::Dynamic,{{0,0,0},pitch,0,0});
    hinge.minAngle=-3.14159265359f;hinge.maxAngle=3.14159265359f;
    const int joint=spinning.createJoint(1,2,hinge);
    motor.mode=MotorMode::Velocity;motor.targetVelocity=pitch>0?.8f:-.8f;spinning.jointMotor(joint,motor);
    for(int frame=0;frame<3000;frame++){
      spinning.advance(1./90);const auto pose=spinning.get(2).pose;const float angle=spinning.joint(joint).angle;
      const float cp=std::cos(pose.pitch),sp=std::sin(pose.pitch),cy=std::cos(pose.yaw),sy=std::sin(pose.yaw),cr=std::cos(pose.roll),sr=std::sin(pose.roll);
      CHECK(std::abs(cy*cr+sy*sp*sr-std::cos(angle))<.0001f);
      CHECK(std::abs(-sy*cr+cy*sp*sr+std::sin(angle))<.0001f);
      CHECK(std::abs(-sp+std::sin(pitch))<.0001f);
      CHECK(std::abs(cp)<.0001f);
    }
  }
  World elastic;body(elastic,1,Type::Static,{{0,.5f,0}});body(elastic,2,Type::Dynamic,{{0,-.1f,0}});
  JointSettings spring;spring.type=JointType::Distance;spring.minDistance=.3f;spring.maxDistance=.3f;spring.spring=JointSpring{2,.15f};
  const int bounce=elastic.createJoint(1,2,spring);
  const float equilibrium=.5f-.3f-9.81f/(4*3.14159265359f*3.14159265359f*4);
  int crossings=0;float previous=elastic.get(2).pose.position.y-equilibrium,early=0,late=0;
  for(int i=0;i<360;i++){
    elastic.advance(1./90);const float error=elastic.get(2).pose.position.y-equilibrium;
    if(error*previous<0)crossings++;previous=error;
    if(i<90)early=std::max(early,std::abs(error));if(i>=270)late=std::max(late,std::abs(error));
  }
  CHECK(crossings>=2&&early>.06f&&late<.015f);
  CHECK(elastic.joint(bounce).distance>.34f); // Gravity stretches beyond the rest length.
  spring.spring->frequency=0;rejects([&]{elastic.createJoint(1,2,spring);});
  spring.type=JointType::Fixed;spring.spring->frequency=2;rejects([&]{elastic.createJoint(1,2,spring);});
  elastic.hold(2);for(int i=0;i<60;i++){elastic.target(2,{{0,-.1f,0}},1./90);elastic.advance(1./90);}
  elastic.release(2,false);tick(elastic,360);CHECK(std::abs(elastic.get(2).pose.position.y-equilibrium)<.015f);
  elastic.remove(1);CHECK(!elastic.containsJoint(bounce));
  World fracture;body(fracture,1,Type::Static,{{0,.5f,0}});body(fracture,2,Type::Dynamic,{{0,.2f,0}});
  JointSettings fuse;fuse.type=JointType::Distance;fuse.minDistance=.3f;fuse.maxDistance=.3f;fuse.breakForce=20;
  int link=fracture.createJoint(1,2,fuse);tick(fracture,30);CHECK(fracture.containsJoint(link)&&fracture.takeJointBreaks().empty());
  fracture.impulse(2,{0,-1,0});tick(fracture,1);CHECK(!fracture.containsJoint(link));
  auto broken=fracture.takeJointBreaks();CHECK(broken.size()==1&&broken[0].jointId==link&&broken[0].force>20&&broken[0].torque==0);
  CHECK(fracture.takeJointBreaks().empty()&&!fracture.get(2).jointed);
  fuse.breakTorque=1;rejects([&]{fracture.createJoint(1,2,fuse);});
  World twist;twist.gravity({});body(twist,1,Type::Static,{});body(twist,2,Type::Dynamic,{});
  fuse=JointSettings{};fuse.breakTorque=.1f;link=twist.createJoint(1,2,fuse);twist.angularVelocity(2,{0,0,10});tick(twist,1);
  CHECK(!twist.containsJoint(link)&&twist.takeJointBreaks().front().torque>.1f);
  World pull;pull.gravity({});body(pull,1,Type::Static,{});body(pull,2,Type::Dynamic,{});
  fuse=JointSettings{};fuse.breakForce=20;link=pull.createJoint(1,2,fuse);pull.hold(2);pull.target(2,{{1,0,0}},1./90);tick(pull,1);
  CHECK(!pull.containsJoint(link)&&pull.get(2).held);tick(pull,1);CHECK(std::abs(pull.get(2).pose.position.x-1)<.001f);
  CHECK(pull.takeJointBreaks().size()==1);pull.release(2,false);tick(pull,5);CHECK(pull.takeJointBreaks().empty());
  World pickup;body(pickup,1,Type::Static,{{0,.3f,0}});
  Settings weight;weight.mass=.25f;pickup.set(2,weight,{}, {-.06f,-.06f,-.06f},{.06f,.06f,.06f});
  fuse=JointSettings{};fuse.breakForce=40;fuse.anchor=Vec3{0,0,0};link=pickup.createJoint(1,2,fuse);tick(pickup,90);
  pickup.hold(2);const auto resting=pickup.get(2).pose;
  for(int frame=0;frame<45;frame++){
    auto target=resting;target.position.x+=.015f*std::sin(float(frame)*.3f);target.roll+=.05f*std::sin(float(frame)*.2f);
    pickup.target(2,target,1./90);pickup.advance(1./90);
  }
  CHECK(pickup.containsJoint(link));
  auto deliberate=resting;deliberate.position.x+=.3f;pickup.target(2,deliberate,1./90);tick(pickup,10);
  CHECK(!pickup.containsJoint(link)&&pickup.takeJointBreaks().size()==1);
  // The demonstration must permit pickup, lifting toward the anchor, and
  // moderate extension before a deliberate long pull breaks the connection.
  for(float minimum:{0.f,.3f}){
    World demo;body(demo,1,Type::Static,{{0,.3f,0}});demo.set(2,weight,{}, {-.06f,-.06f,-.06f},{.06f,.06f,.06f});
    fuse=JointSettings{};fuse.type=JointType::Distance;fuse.minDistance=minimum;fuse.maxDistance=.3f;fuse.spring=JointSpring{2,.35f};fuse.breakForce=minimum==0?18:25;
    link=demo.createJoint(1,2,fuse);tick(demo,90);demo.hold(2);
    const float start=demo.get(2).pose.position.y;
    for(int frame=0;frame<90;frame++){demo.target(2,{{0,start+.25f*float(frame)/89,0}},1./90);demo.advance(1./90);}
    CHECK(demo.containsJoint(link));
    for(int frame=0;frame<90;frame++){demo.target(2,{{0,start+.25f-.5f*float(frame)/89,0}},1./90);demo.advance(1./90);}
    CHECK(demo.containsJoint(link));
    for(int frame=0;frame<180&&demo.containsJoint(link);frame++){demo.target(2,{{0,start-.25f-.9f*float(frame)/179,0}},1./90);demo.advance(1./90);}
    CHECK(!demo.containsJoint(link)&&demo.takeJointBreaks().size()==1);
  }
  World adjustable;body(adjustable,1,Type::Static,{{0,.5f,0}});body(adjustable,2,Type::Dynamic,{{0,.2f,0}});
  JointSettings limits;limits.type=JointType::Distance;limits.minDistance=.3f;limits.maxDistance=.3f;
  int adjustableId=adjustable.createJoint(1,2,limits);tick(adjustable,90);
  limits=adjustable.joint(adjustableId).settings;limits.minDistance=.5f;limits.maxDistance=.5f;limits.spring=JointSpring{2,1};
  adjustable.updateJoint(adjustableId,limits);tick(adjustable,360);
  CHECK(std::abs(adjustable.joint(adjustableId).distance-(.5f+9.81f/(16*3.14159265359f*3.14159265359f)))<.015f);
  limits.spring.reset();adjustable.updateJoint(adjustableId,limits);tick(adjustable,180);CHECK(std::abs(adjustable.joint(adjustableId).distance-.5f)<.01f);
  limits.maxDistance=.1f;rejects([&]{adjustable.updateJoint(adjustableId,limits);});CHECK(*adjustable.joint(adjustableId).settings.maxDistance==.5f);
  limits=adjustable.joint(adjustableId).settings;limits.breakForce=1;adjustable.updateJoint(adjustableId,limits);tick(adjustable,1);CHECK(!adjustable.containsJoint(adjustableId));
  World narrowed;narrowed.gravity({});body(narrowed,1,Type::Static,{});body(narrowed,2,Type::Dynamic,{});
  hinge.axis={.3f,.7f,.2f};hinge.minAngle=-1.2f;hinge.maxAngle=1.2f;int narrowId=narrowed.createJoint(1,2,hinge);
  motor.mode=MotorMode::Position;motor.targetAngle=.9f;narrowed.jointMotor(narrowId,motor);tick(narrowed,180);
  limits=narrowed.joint(narrowId).settings;limits.minAngle=-.4f;limits.maxAngle=.4f;narrowed.updateJoint(narrowId,limits);tick(narrowed,180);
  CHECK(std::abs(narrowed.joint(narrowId).angle-.4f)<.02f&&narrowed.joint(narrowId).motor.targetAngle==.4f);
  // Collision filtering updates retain the same pair reference counts.
  World filtering;filtering.gravity({});filtering.collisionEvents(true);body(filtering,1,Type::Static,{});body(filtering,2,Type::Dynamic,{});
  const int f1=filtering.createJoint(1,2,JointSettings{}),f2=filtering.createJoint(1,2,JointSettings{});
  limits=filtering.joint(f1).settings;limits.collideConnected=true;filtering.updateJoint(f1,limits);tick(filtering,2);CHECK(filtering.takeCollisionEvents().empty());
  limits=filtering.joint(f2).settings;limits.collideConnected=true;filtering.updateJoint(f2,limits);tick(filtering,2);CHECK(!filtering.takeCollisionEvents().empty());
  puts("PASS joint updates: same-ID distance/spring changes, hard reset, invalid rollback, break threshold, motor clamp, oblique axis and collision pair counts");
  puts("PASS breakable joints: load threshold, impulse/torque fracture, held continuation, one-shot events and validation");
  puts("PASS springs: elastic extension, damped oscillation, gravity equilibrium, held release, validation and cleanup");
  puts("PASS motors: position targets, wake/reversal, torque limit, signed velocity, hinge limits, off/coasting and atomic rejection");
  puts("PASS joints: fixed transforms, hinge limits and held pivot, distance range, cleanup, rebuild rejection, collision filtering and held removal");
}
