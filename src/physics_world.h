#pragma once
#include <memory>
#include <vector>

// Public positions/linear vectors use scene coordinates (+Z forward). Angular
// vectors follow the same axes/signs as the engine's model rotation matrices.
namespace physics {
struct Vec3 {float x=0,y=0,z=0;};
struct Pose {Vec3 position;float pitch=0,yaw=0,roll=0;};
enum class Type { Static, Dynamic, Kinematic };
enum class Shape { Box, Sphere };
struct Settings {
  Type type=Type::Dynamic;Shape shape=Shape::Box;
  float mass=1,friction=.5f,restitution=0,linearDamping=.05f,angularDamping=.05f,gravityFactor=1;
};
struct State {
  Settings settings;Pose pose;Vec3 velocity,angularVelocity;
  bool held=false,sleeping=false;
};
enum class CollisionPhase { Start, Stay, End };
struct CollisionEvent {
  CollisionPhase phase=CollisionPhase::Start;
  int objectId=0,otherObjectId=0;
  Vec3 point,normal;
  float penetration=0,relativeSpeed=0;
};
class World {
public:
  World();~World();
  World(const World&)=delete;World& operator=(const World&)=delete;
  void set(int id,const Settings&,const Pose&,Vec3 low,Vec3 high);
  void remove(int id);
  bool contains(int id) const;
  State get(int id) const;
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
