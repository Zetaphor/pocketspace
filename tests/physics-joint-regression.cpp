#include "physics_world.h"
#include <cstdlib>
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
  puts("PASS motors: position targets, wake/reversal, torque limit, signed velocity, hinge limits, off/coasting and atomic rejection");
  puts("PASS joints: fixed transforms, hinge limits and held pivot, distance range, cleanup, rebuild rejection, collision filtering and held removal");
}
