#pragma once
#include <memory>
#include <optional>
#include <span>
#include <vector>

// Public positions/linear vectors use scene coordinates (+Z forward). Angular
// vectors follow the same axes/signs as the engine's model rotation matrices.
namespace physics {
struct Vec3 {float x=0,y=0,z=0;bool operator==(const Vec3&)const=default;};
struct Pose {Vec3 position;float pitch=0,yaw=0,roll=0;};
enum class Type { Static, Dynamic, Kinematic };
enum class Shape { Box, Sphere, Convex, Mesh };
struct Settings {
  Type type=Type::Dynamic;Shape shape=Shape::Box;
  float mass=1,friction=.5f,restitution=0,linearDamping=.05f,angularDamping=.05f,gravityFactor=1;
  std::optional<Vec3> halfExtents,offset;
  std::optional<float> radius;
};
struct Collider {Vec3 halfExtents,offset;float radius=0;Vec3 centerOfMass;unsigned vertexCount=0,triangleCount=0;};
// Vertices use normalized model coordinates (local +Z backward), before scale.
// A key identifies immutable geometry within this World; cached calls may omit vertices.
struct GeometrySource {int key=0;float scale=1;std::span<const Vec3> vertices;};
struct State {
  Settings settings;Pose pose;Vec3 velocity,angularVelocity;
  bool held=false,sleeping=false;
  Collider collider;bool jointed=false;
};
enum class CollisionPhase { Start, Stay, End };
struct CollisionEvent {
  CollisionPhase phase=CollisionPhase::Start;
  int objectId=0,otherObjectId=0;
  Vec3 point,normal;
  float penetration=0,relativeSpeed=0;
};
enum class JointType {Fixed,Hinge,Distance};
struct JointSpring {float frequency=2,damping=.5f;};
struct JointSettings {
  JointType type=JointType::Fixed;
  std::optional<Vec3> anchor,anchorA,anchorB;
  Vec3 axis{0,1,0};
  float minAngle=-3.14159265359f,maxAngle=3.14159265359f;
  float minDistance=0;std::optional<float> maxDistance;
  bool collideConnected=false;
  std::optional<JointSpring> spring;
  std::optional<float> breakForce,breakTorque;
};
enum class MotorMode {Off,Position,Velocity};
struct JointMotor {
  MotorMode mode=MotorMode::Off;
  float targetAngle=0,targetVelocity=0,maxTorque=1,frequency=2,damping=1;
};
struct JointState {int id,objectA,objectB;JointSettings settings;Vec3 anchorA,anchorB;float distance=0,angle=0;JointMotor motor;};
struct JointBreak {int jointId,objectA,objectB;float force,torque;};
class World {
public:
  World();~World();
  World(const World&)=delete;World& operator=(const World&)=delete;
  void set(int id,const Settings&,const Pose&,Vec3 low,Vec3 high,GeometrySource source={});
  bool hasConvexHull(int key) const;
  bool hasMesh(int key) const;
  void remove(int id);
  bool contains(int id) const;
  State get(int id) const;
  int createJoint(int objectA,int objectB,const JointSettings&);
  bool containsJoint(int id) const;
  bool hasJoints(int objectId) const;
  JointState joint(int id) const;
  void removeJoint(int id);
  void jointMotor(int id,const JointMotor&);
  void updateJoint(int id,const JointSettings&);
  std::vector<JointBreak> takeJointBreaks();
  void gravity(Vec3);
  void velocity(int id,Vec3);
  void angularVelocity(int id,Vec3);
  void impulse(int id,Vec3);
  void pose(int id,const Pose&); // teleport, retaining velocities
  void target(int id,const Pose&,double elapsed); // hand/script target
  void hold(int id);
  void release(int id,bool allowThrow);
  void cancelThrow(int id);
  void collisionEvents(bool enabled,bool stay=false);
  std::vector<CollisionEvent> takeCollisionEvents();
  void advance(double elapsed); // fixed 90 Hz, at most 8 catch-up steps
private:
  struct Impl;std::unique_ptr<Impl> impl;
};
}
