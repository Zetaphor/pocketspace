#include "physics_world.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <stdexcept>

namespace physics {
namespace {
constexpr double step=1.0/90;
struct Runtime {
  Runtime(){JPH::RegisterDefaultAllocator();JPH::Factory::sInstance=new JPH::Factory;JPH::RegisterTypes();}
  ~Runtime(){JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}
};
void initialize(){static Runtime runtime;}
JPH::Vec3 linear(Vec3 v){return {v.x,v.y,-v.z};}
Vec3 scene(JPH::Vec3 v){return {v.GetX(),v.GetY(),-v.GetZ()};}
JPH::Vec3 angular(Vec3 v){return {v.x,v.y,v.z};}
JPH::Quat rotation(const Pose &p){return JPH::Quat::sRotation(JPH::Vec3::sAxisY(),p.yaw)*JPH::Quat::sRotation(JPH::Vec3::sAxisX(),p.pitch)*JPH::Quat::sRotation(JPH::Vec3::sAxisZ(),p.roll);}
Pose poseOf(JPH::RVec3 p,JPH::Quat q){
  const auto m=JPH::Mat44::sRotation(q);
  Pose pose;pose.position=scene(p);
  pose.pitch=std::asin(std::clamp(-m(1,2),-1.f,1.f));
  if(std::fabs(std::cos(pose.pitch))>1e-5f){pose.yaw=std::atan2(m(0,2),m(2,2));pose.roll=std::atan2(m(1,0),m(1,1));}
  else {pose.yaw=std::atan2(-m(2,0),m(0,0));pose.roll=0;}
  return pose;
}
JPH::EMotionType motion(Type t){return t==Type::Static?JPH::EMotionType::Static:t==Type::Kinematic?JPH::EMotionType::Kinematic:JPH::EMotionType::Dynamic;}
void finite(Vec3 v){if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::invalid_argument("physics vectors must be finite");}
void finite(const Pose &p){finite(p.position);finite(Vec3{p.pitch,p.yaw,p.roll});}
void bounded(float v,float lo,float hi){if(!std::isfinite(v)||v<lo||v>hi)throw std::invalid_argument("physics parameter outside supported range");}
struct BroadPhase final:JPH::BroadPhaseLayerInterface {
  JPH::uint GetNumBroadPhaseLayers()const override{return 2;}
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer)const override{return JPH::BroadPhaseLayer(layer);}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer)const override{return "physics";}
#endif
};
struct BroadFilter final:JPH::ObjectVsBroadPhaseLayerFilter {
  bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b)const override{return a!=0||b!=JPH::BroadPhaseLayer(0);}
};
struct PairFilter final:JPH::ObjectLayerPairFilter {
  bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b)const override{return a!=0||b!=0;}
};
}
// The listener only records immutable solver data. JavaScript runs later, after
// Update has released body locks. The job system is explicitly single-threaded.
struct World::Impl : JPH::ContactListener {
  struct Sample {double duration;JPH::Vec3 velocity,angular;};
  struct Record {JPH::BodyID body;Settings settings;Pose target;bool held=false,canThrow=true;std::deque<Sample> samples;};
  using Pair=std::pair<JPH::uint32,JPH::uint32>;
  struct Contact {CollisionEvent event;JPH::BodyID a,b;};
  bool eventsEnabled=false,stayEvents=false;
  std::map<Pair,Contact> contacts,seen;
  std::vector<CollisionEvent> pending;
  void emit(Contact contact,CollisionPhase phase){
    contact.event.phase=phase;pending.push_back(contact.event);
    if(pending.size()>4096)pending.erase(pending.begin(),pending.begin()+1024);
  }
  void forgetContacts(int id){
    for(auto it=contacts.begin();it!=contacts.end();) {
      if(it->second.event.objectId==id||it->second.event.otherObjectId==id){emit(it->second,CollisionPhase::End);it=contacts.erase(it);}
      else ++it;
    }
  }
  void observe(const JPH::Body &a,const JPH::Body &b,const JPH::ContactManifold &m){
    if(m.mRelativeContactPointsOn1.size()==0||m.mRelativeContactPointsOn2.size()==0)return;
    const Pair key{a.GetID().GetIndexAndSequenceNumber(),b.GetID().GetIndexAndSequenceNumber()};
    const bool reversed=a.GetUserData()>b.GetUserData();
    Contact contact;contact.a=a.GetID();contact.b=b.GetID();
    auto &event=contact.event;
    event.objectId=int(reversed?b.GetUserData():a.GetUserData());
    event.otherObjectId=int(reversed?a.GetUserData():b.GetUserData());
    auto normal=m.mWorldSpaceNormal;
    // One representative point per object pair, rather than a stream of every
    // manifold point. The deepest observed manifold supplies the geometry.
    const auto point=(m.GetWorldSpaceContactPointOn1(0)+m.GetWorldSpaceContactPointOn2(0))*.5f;
    event.point=scene(point);event.normal=scene(reversed?-normal:normal);
    event.penetration=std::max(0.f,m.mPenetrationDepth);
    event.relativeSpeed=std::max(0.f,(a.GetPointVelocity(point)-b.GetPointVelocity(point)).Dot(normal));
    auto [it,inserted]=seen.emplace(key,contact);
    if(!inserted){
      const float speed=std::max(event.relativeSpeed,it->second.event.relativeSpeed);
      if(event.penetration>it->second.event.penetration)it->second=contact;
      it->second.event.relativeSpeed=speed;
    }
  }
  void OnContactAdded(const JPH::Body &a,const JPH::Body &b,const JPH::ContactManifold &m,JPH::ContactSettings&)override{observe(a,b,m);}
  void OnContactPersisted(const JPH::Body &a,const JPH::Body &b,const JPH::ContactManifold &m,JPH::ContactSettings&)override{observe(a,b,m);}
  // Removal also fires for sleeping bodies. Reconcile after Update, when it is
  // safe to read activity; explicit teleports/removal invalidate cached pairs.
  void finishContacts(){
    auto &b=system.GetBodyInterface();
    for(auto it=contacts.begin();it!=contacts.end();) {
      auto current=seen.find(it->first);
      if(current!=seen.end()){
        it->second=current->second;seen.erase(current);
        if(stayEvents)emit(it->second,CollisionPhase::Stay);
        ++it;
      }else if(!b.IsActive(it->second.a)&&!b.IsActive(it->second.b)){
        it->second.event.relativeSpeed=0;
        if(stayEvents)emit(it->second,CollisionPhase::Stay);
        ++it;
      }else {emit(it->second,CollisionPhase::End);it=contacts.erase(it);}
    }
    for(const auto &[key,contact]:seen){contacts.emplace(key,contact);emit(contact,CollisionPhase::Start);}
    seen.clear();
  }
  BroadPhase broad;BroadFilter broadFilter;PairFilter pairs;
  JPH::TempAllocatorImpl temporary{8*1024*1024};JPH::JobSystemSingleThreaded jobs{2048};
  JPH::PhysicsSystem system;std::map<int,Record> bodies;double accumulator=0;
  Impl(){system.Init(512,0,2048,2048,broad,broadFilter,pairs);}
  Record &record(int id){auto it=bodies.find(id);if(it==bodies.end())throw std::invalid_argument("object has no physics body");return it->second;}
};
World::World(){initialize();impl=std::make_unique<Impl>();}
World::~World(){while(!impl->bodies.empty())remove(impl->bodies.begin()->first);}
bool World::contains(int id)const{return impl->bodies.contains(id);}
void World::set(int id,const Settings &s,const Pose &p,Vec3 low,Vec3 high){
  finite(p);finite(low);finite(high);bounded(s.mass,.001f,10000);bounded(s.friction,0,1);bounded(s.restitution,0,1);
  bounded(s.linearDamping,0,1);bounded(s.angularDamping,0,1);bounded(s.gravityFactor,0,10);
  JPH::Vec3 half((high.x-low.x)*.5f,(high.y-low.y)*.5f,(high.z-low.z)*.5f);
  if(half.GetX()<0||half.GetY()<0||half.GetZ()<0||half.LengthSq()==0)throw std::invalid_argument("collider bounds must have positive volume");
  // Small triangle/plane dimensions receive a millimeter minimum thickness.
  half=JPH::Vec3::sMax(half,JPH::Vec3(.001f,.001f,.001f));
  JPH::RefConst<JPH::Shape> shape;
  if(s.shape==Shape::Sphere)shape=new JPH::SphereShape(half.ReduceMax());
  else shape=new JPH::BoxShape(half,std::min(.01f,half.ReduceMin()*.1f));
  const JPH::Vec3 center((low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f);
  shape=new JPH::RotatedTranslatedShape(center,JPH::Quat::sIdentity(),shape);
  JPH::BodyCreationSettings settings(shape,linear(p.position),rotation(p),motion(s.type),s.type==Type::Static?0:1);
  settings.mUserData=static_cast<JPH::uint64>(id);
  settings.mAllowDynamicOrKinematic=s.type!=Type::Static;
  settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;
  settings.mMassPropertiesOverride.mMass=s.mass;
  settings.mFriction=s.friction;settings.mRestitution=s.restitution;
  settings.mLinearDamping=s.linearDamping;settings.mAngularDamping=s.angularDamping;settings.mGravityFactor=s.gravityFactor;
  settings.mMotionQuality=JPH::EMotionQuality::LinearCast;
  auto &b=impl->system.GetBodyInterface();auto *created=b.CreateBody(settings);
  if(!created)throw std::runtime_error("physics body capacity exhausted");
  const auto body=created->GetID();
  // Create first: rejected configuration must not destroy a working body.
  const bool wasHeld=contains(id)&&impl->record(id).held;
  remove(id);b.AddBody(body,s.type==Type::Static?JPH::EActivation::DontActivate:JPH::EActivation::Activate);
  impl->bodies.emplace(id,Impl::Record{body,s,p});
  if(wasHeld)hold(id);
}
void World::remove(int id){auto it=impl->bodies.find(id);if(it==impl->bodies.end())return;impl->forgetContacts(id);auto &b=impl->system.GetBodyInterface();b.RemoveBody(it->second.body);b.DestroyBody(it->second.body);impl->bodies.erase(it);}
State World::get(int id)const{
  const auto &r=impl->record(id);auto &b=impl->system.GetBodyInterface();JPH::RVec3 p;JPH::Quat q;b.GetPositionAndRotation(r.body,p,q);
  auto a=b.GetAngularVelocity(r.body);return {r.settings,poseOf(p,q),scene(b.GetLinearVelocity(r.body)),{a.GetX(),a.GetY(),a.GetZ()},r.held,!b.IsActive(r.body)};
}
void World::gravity(Vec3 v){finite(v);impl->system.SetGravity(linear(v));}
void World::velocity(int id,Vec3 v){finite(v);auto &r=impl->record(id);if(r.settings.type!=Type::Dynamic||r.held)throw std::invalid_argument("velocity requires an unheld dynamic body");impl->system.GetBodyInterface().SetLinearVelocity(r.body,linear(v));}
void World::angularVelocity(int id,Vec3 v){finite(v);auto &r=impl->record(id);if(r.settings.type!=Type::Dynamic||r.held)throw std::invalid_argument("angular velocity requires an unheld dynamic body");impl->system.GetBodyInterface().SetAngularVelocity(r.body,angular(v));}
void World::impulse(int id,Vec3 v){finite(v);auto &r=impl->record(id);if(r.settings.type!=Type::Dynamic||r.held)throw std::invalid_argument("impulse requires an unheld dynamic body");impl->system.GetBodyInterface().AddImpulse(r.body,linear(v));}
void World::pose(int id,const Pose &p){finite(p);auto &r=impl->record(id);
  auto &b=impl->system.GetBodyInterface();
  if((b.GetPosition(r.body)-linear(p.position)).LengthSq()>1e-12f||std::fabs(b.GetRotation(r.body).Dot(rotation(p)))<.999999f)impl->forgetContacts(id);
  r.target=p;impl->system.GetBodyInterface().SetPositionAndRotation(r.body,linear(p.position),rotation(p),r.settings.type==Type::Static?JPH::EActivation::DontActivate:JPH::EActivation::Activate);r.samples.clear();}
void World::hold(int id){auto &r=impl->record(id);if(r.held)return;r.target=get(id).pose;r.held=true;r.canThrow=true;r.samples.clear();if(r.settings.type==Type::Dynamic)impl->system.GetBodyInterface().SetMotionType(r.body,JPH::EMotionType::Kinematic,JPH::EActivation::Activate);}
void World::cancelThrow(int id){if(contains(id)){auto &r=impl->record(id);r.canThrow=false;r.samples.clear();}}
void World::release(int id,bool allowThrow){
  if(!contains(id))return;auto &r=impl->record(id);if(!r.held)return;r.held=false;
  if(r.settings.type!=Type::Dynamic)return;
  JPH::Vec3 v=JPH::Vec3::sZero(),a=v;double duration=0;
  if(allowThrow&&r.canThrow)for(const auto &sample:r.samples){v+=sample.velocity*float(sample.duration);a+=sample.angular*float(sample.duration);duration+=sample.duration;}
  if(duration>0){v/=float(duration);a/=float(duration);}
  v=v.NormalizedOr(JPH::Vec3::sZero())*std::min(v.Length(),20.f);
  a=a.NormalizedOr(JPH::Vec3::sZero())*std::min(a.Length(),30.f);
  auto &b=impl->system.GetBodyInterface();b.SetMotionType(r.body,JPH::EMotionType::Dynamic,JPH::EActivation::Activate);b.SetLinearAndAngularVelocity(r.body,v,a);r.samples.clear();
}
void World::target(int id,const Pose &p,double elapsed){
  finite(p);auto &r=impl->record(id);
  if(r.held&&r.settings.type==Type::Static){pose(id,p);return;}
  if(r.held&&elapsed>0&&elapsed<=.1){
    JPH::Vec3 axis;float angle;auto delta=rotation(p)*rotation(r.target).Conjugated();if(delta.GetW()<0)delta=-delta;delta.GetAxisAngle(axis,angle);
    r.samples.push_back({elapsed,(linear(p.position)-linear(r.target.position))/float(elapsed),axis*(angle/float(elapsed))});
    double duration=0;for(auto it=r.samples.rbegin();it!=r.samples.rend();++it){duration+=it->duration;if(duration>=.10){r.samples.erase(r.samples.begin(),it.base()-1);break;}}
  }else if(r.held)r.samples.clear();
  r.target=p;
}
void World::collisionEvents(bool enabled,bool stay){
  impl->stayEvents=stay;
  if(enabled==impl->eventsEnabled)return;
  impl->eventsEnabled=enabled;impl->contacts.clear();impl->seen.clear();impl->pending.clear();
  impl->system.SetContactListener(enabled?impl.get():nullptr);
  if(enabled)for(const auto &[id,r]:impl->bodies)if(r.settings.type!=Type::Static)impl->system.GetBodyInterface().ActivateBody(r.body);
}
std::vector<CollisionEvent> World::takeCollisionEvents(){auto events=std::move(impl->pending);impl->pending.clear();return events;}
void World::advance(double elapsed){
  if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("physics time must be finite and nonnegative");
  impl->accumulator=std::min(impl->accumulator+elapsed,8*step);
  auto &b=impl->system.GetBodyInterface();
  while(impl->accumulator+1e-10>=step){
    for(const auto &[id,r]:impl->bodies)if(r.held||r.settings.type==Type::Kinematic){
      if(r.settings.type==Type::Static)b.SetPositionAndRotation(r.body,linear(r.target.position),rotation(r.target),JPH::EActivation::DontActivate);
      else b.MoveKinematic(r.body,linear(r.target.position),rotation(r.target),float(step));
    }
    impl->seen.clear();
    if(impl->system.Update(float(step),1,&impl->temporary,&impl->jobs)!=JPH::EPhysicsUpdateError::None)throw std::runtime_error("physics simulation capacity exceeded");
    if(impl->eventsEnabled)impl->finishContacts();
    impl->accumulator-=step;
  }
}
}
