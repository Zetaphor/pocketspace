#include "physics_world.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <tuple>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <limits>

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
  // asin loses the tiny cosine near +/-90 degrees, selecting an unstable
  // yaw/roll branch. Measure that cosine from the matrix itself instead.
  const float cosine=std::hypot(m(0,2),m(2,2));
  pose.pitch=std::atan2(-m(1,2),cosine);
  if(cosine>1e-5f){pose.yaw=std::atan2(m(0,2),m(2,2));pose.roll=std::atan2(m(1,0),m(1,1));}
  else {pose.yaw=std::atan2(-m(2,0),m(0,0));pose.roll=0;}
  return pose;
}
JPH::EMotionType motion(Type t){return t==Type::Static?JPH::EMotionType::Static:t==Type::Kinematic?JPH::EMotionType::Kinematic:JPH::EMotionType::Dynamic;}
void finite(Vec3 v){if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::invalid_argument("physics vectors must be finite");}
void finite(const Pose &p){finite(p.position);finite(Vec3{p.pitch,p.yaw,p.roll});}
void bounded(float v,float lo,float hi){if(!std::isfinite(v)||v<lo||v>hi)throw std::invalid_argument("physics parameter outside supported range");}
JointSettings validateJoint(const JointSettings &requested){
  JointSettings s=requested;
  if(s.breakForce)bounded(*s.breakForce,.001f,1e9f);
  if(s.breakTorque){bounded(*s.breakTorque,.001f,1e9f);if(s.type==JointType::Distance)throw std::invalid_argument("breakTorque requires a fixed or hinge joint");}
  if(s.spring){
    if(s.type!=JointType::Distance)throw std::invalid_argument("springs require a distance joint");
    bounded(s.spring->frequency,.01f,30);bounded(s.spring->damping,0,10);
  }
  for(auto v:{s.anchor,s.anchorA,s.anchorB})if(v)finite(*v);
  bounded(s.minAngle,-3.14159265359f,0);bounded(s.maxAngle,0,3.14159265359f);
  if(s.type==JointType::Hinge&&s.minAngle==s.maxAngle)throw std::invalid_argument("use a fixed joint for a locked hinge");
  bounded(s.minDistance,0,10000);if(s.maxDistance)bounded(*s.maxDistance,0,10000);
  finite(s.axis);auto axis=angular(s.axis);
  if(axis.LengthSq()<1e-12f||!std::isfinite(axis.LengthSq()))throw std::invalid_argument("joint axis must be nonzero and finite");
  axis=axis.Normalized();s.axis={axis.GetX(),axis.GetY(),axis.GetZ()};
  return s;
}
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
// Update has released body locks. Contact aggregation is protected for workers.
struct World::Impl : JPH::ContactListener {
  struct JointFilter final : JPH::GroupFilter {
    std::map<std::pair<int,int>,unsigned> blocked;
    bool CanCollide(const JPH::CollisionGroup &a,const JPH::CollisionGroup &b)const override{
      return !blocked.contains(std::minmax(int(a.GetGroupID()),int(b.GetGroupID())));
    }
  };
  JPH::Ref<JointFilter> jointFilter=new JointFilter;
  struct Joint {JointState state;JPH::Ref<JPH::TwoBodyConstraint> constraint;JPH::Vec3 localA,localB;Pose referenceA,referenceB;};
  std::map<int,Joint> joints;std::map<int,std::set<int>> connections;int nextJoint=1;
  std::vector<JointBreak> breaks;
  struct Sample {double duration;JPH::Vec3 velocity,angular;};
  struct Record {JPH::BodyID body;Settings settings;Pose target;Collider collider;bool held=false,canThrow=true,syncDirty=true;std::deque<Sample> samples;};
  using Pair=std::pair<JPH::uint32,JPH::uint32>;
  struct Contact {CollisionEvent event;JPH::BodyID a,b;};
  std::mutex contactMutex;
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
    std::lock_guard lock(contactMutex);
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
  std::unique_ptr<JPH::JobSystemThreadPool> pool;int workerSetting=-1;Performance perf;
  std::map<int,JPH::RefConst<JPH::ConvexHullShape>> hulls;
  struct Mesh {JPH::RefConst<JPH::Shape> shape;unsigned triangles;};
  std::map<int,Mesh> meshes;
  JPH::PhysicsSystem system;std::map<int,Record> bodies;double accumulator=0;
  Impl(){system.Init(512,0,2048,2048,broad,broadFilter,pairs);}
  Record &record(int id){auto it=bodies.find(id);if(it==bodies.end())throw std::invalid_argument("object has no physics body");return it->second;}
  void refreshMotorSleep(int id){
    auto &r=record(id);if(r.settings.type==Type::Static)return;
    bool powered=false;
    for(int key:connections[id]){const auto &j=joints.at(key);if(j.state.motor.mode!=MotorMode::Off&&j.state.motor.maxTorque>0){powered=true;break;}}
    system.GetBodyLockInterfaceNoLock().TryGetBody(r.body)->SetAllowSleeping(!powered);
  }
};
World::World(){initialize();impl=std::make_unique<Impl>();}
World::~World(){while(!impl->bodies.empty())remove(impl->bodies.begin()->first);}
bool World::contains(int id)const{return impl->bodies.contains(id);}
bool World::hasConvexHull(int key)const{return impl->hulls.contains(key);}
bool World::hasMesh(int key)const{return impl->meshes.contains(key);}
void World::set(int id,const Settings &s,const Pose &p,Vec3 low,Vec3 high,GeometrySource source){
  if(hasJoints(id))throw std::invalid_argument("remove attached joints before replacing or scaling a physics body");
  if(s.shape==Shape::Mesh&&s.type!=Type::Static)throw std::invalid_argument("mesh collider requires type static");
  finite(p);finite(low);finite(high);bounded(s.mass,.001f,10000);bounded(s.friction,0,1);bounded(s.restitution,0,1);
  bounded(s.linearDamping,0,1);bounded(s.angularDamping,0,1);bounded(s.gravityFactor,0,10);
  if(s.halfExtents&&s.shape!=Shape::Box)throw std::invalid_argument("halfExtents requires shape box");
  if(s.radius&&s.shape!=Shape::Sphere)throw std::invalid_argument("radius requires shape sphere");
  if(s.halfExtents){finite(*s.halfExtents);bounded(s.halfExtents->x,.001f,100);bounded(s.halfExtents->y,.001f,100);bounded(s.halfExtents->z,.001f,100);}
  if(s.radius)bounded(*s.radius,.001f,100);
  if(s.offset){finite(*s.offset);bounded(s.offset->x,-100,100);bounded(s.offset->y,-100,100);bounded(s.offset->z,-100,100);}
  JPH::Vec3 half((high.x-low.x)*.5f,(high.y-low.y)*.5f,(high.z-low.z)*.5f);
  if(half.GetX()<0||half.GetY()<0||half.GetZ()<0||(!s.halfExtents&&!s.radius&&half.LengthSq()==0))throw std::invalid_argument("invalid collider bounds");
  // Only automatic dimensions are clamped; authored dimensions are validated.
  half=s.halfExtents?JPH::Vec3(s.halfExtents->x,s.halfExtents->y,s.halfExtents->z):JPH::Vec3::sMax(half,JPH::Vec3(.001f,.001f,.001f));
  const float radius=s.radius.value_or(half.ReduceMax());
  JPH::RefConst<JPH::Shape> shape;
  const JPH::Vec3 boundsCenter((low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f);
  const JPH::Vec3 center=s.offset?linear(*s.offset):boundsCenter;
  unsigned vertexCount=0,triangleCount=0;
  if(s.shape==Shape::Convex){
    bounded(source.scale,1e-6f,100);
    auto cached=impl->hulls.find(source.key);
    if(cached==impl->hulls.end()){
      std::vector<Vec3> points(source.vertices.begin(),source.vertices.end());
      for(auto point:points)finite(point);
      auto key=[](Vec3 v){return std::tuple(v.x,v.y,v.z);};
      std::sort(points.begin(),points.end(),[&](Vec3 a,Vec3 b){return key(a)<key(b);});
      points.erase(std::unique(points.begin(),points.end(),[&](Vec3 a,Vec3 b){return key(a)==key(b);}),points.end());
      if(points.size()<4)throw std::invalid_argument("convex collider needs at least four noncoplanar points");
      JPH::Array<JPH::Vec3> vertices;vertices.reserve(points.size());
      for(auto point:points)vertices.emplace_back(point.x,point.y,point.z);
      JPH::ConvexHullShapeSettings hullSettings(vertices,.01f);
      auto result=hullSettings.Create();
      if(result.HasError())throw std::invalid_argument("invalid convex hull: "+std::string(result.GetError()));
      auto *hull=static_cast<const JPH::ConvexHullShape*>(result.Get().GetPtr());
      if(!std::isfinite(hull->GetVolume())||hull->GetVolume()<=0)throw std::invalid_argument("convex collider needs nonzero volume");
      cached=impl->hulls.emplace(source.key,hull).first;
    }
    vertexCount=cached->second->GetNumPoints();
    auto scaled=JPH::ScaledShapeSettings(cached->second,JPH::Vec3::sReplicate(source.scale)).Create();
    if(scaled.HasError())throw std::invalid_argument("invalid convex scale: "+std::string(scaled.GetError()));
    shape=scaled.Get();
    half=shape->GetLocalBounds().GetExtent();
    shape=new JPH::RotatedTranslatedShape(center-boundsCenter,JPH::Quat::sIdentity(),shape);
  }else if(s.shape==Shape::Mesh){
    bounded(source.scale,1e-6f,100);
    auto cached=impl->meshes.find(source.key);
    if(cached==impl->meshes.end()){
      if(source.vertices.empty()||source.vertices.size()%3)throw std::invalid_argument("mesh collider requires a triangle stream");
      JPH::TriangleList triangles;triangles.reserve(source.vertices.size()/3);
      for(size_t i=0;i<source.vertices.size();i+=3){
        JPH::Vec3 points[3];
        for(int j=0;j<3;j++){const auto v=source.vertices[i+j];finite(v);points[j]=JPH::Vec3(v.x,v.y,v.z);}
        triangles.emplace_back(points[0],points[1],points[2]);
      }
      JPH::MeshShapeSettings meshSettings(triangles); // Welds vertices and removes duplicate/degenerate triangles.
      if(meshSettings.mIndexedTriangles.empty())throw std::invalid_argument("mesh collider has no valid triangles");
      auto result=meshSettings.Create();
      if(result.HasError())throw std::invalid_argument("invalid mesh collider: "+std::string(result.GetError()));
      cached=impl->meshes.emplace(source.key,Impl::Mesh{result.Get(),unsigned(meshSettings.mIndexedTriangles.size())}).first;
    }
    triangleCount=cached->second.triangles;
    auto scaled=JPH::ScaledShapeSettings(cached->second.shape,JPH::Vec3::sReplicate(source.scale)).Create();
    if(scaled.HasError())throw std::invalid_argument("invalid mesh scale: "+std::string(scaled.GetError()));
    shape=scaled.Get();half=shape->GetLocalBounds().GetExtent();
    shape=new JPH::RotatedTranslatedShape(center-boundsCenter,JPH::Quat::sIdentity(),shape);
  }else{
    if(s.shape==Shape::Sphere)shape=new JPH::SphereShape(radius);
    else shape=new JPH::BoxShape(half,std::min(.01f,half.ReduceMin()*.1f));
    shape=new JPH::RotatedTranslatedShape(center,JPH::Quat::sIdentity(),shape);
  }
  const Collider collider{{half.GetX(),half.GetY(),half.GetZ()},scene(center),radius,scene(shape->GetCenterOfMass()),vertexCount,triangleCount};
  JPH::BodyCreationSettings settings(shape,linear(p.position),rotation(p),motion(s.type),s.type==Type::Static?0:1);
  settings.mUserData=static_cast<JPH::uint64>(id);
  settings.mCollisionGroup=JPH::CollisionGroup(impl->jointFilter,id,0);
  settings.mAllowDynamicOrKinematic=s.type!=Type::Static;
  settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;
  settings.mMassPropertiesOverride.mMass=s.mass;
  settings.mFriction=s.friction;settings.mRestitution=s.restitution;
  settings.mLinearDamping=s.linearDamping;settings.mAngularDamping=s.angularDamping;settings.mGravityFactor=s.gravityFactor;
  settings.mMotionQuality=s.continuous?JPH::EMotionQuality::LinearCast:JPH::EMotionQuality::Discrete;
  auto &b=impl->system.GetBodyInterface();auto *created=b.CreateBody(settings);
  if(!created)throw std::runtime_error("physics body capacity exhausted");
  const auto body=created->GetID();
  // Create first: rejected configuration must not destroy a working body.
  const bool wasHeld=contains(id)&&impl->record(id).held;
  remove(id);b.AddBody(body,s.type==Type::Static?JPH::EActivation::DontActivate:JPH::EActivation::Activate);
  impl->bodies.emplace(id,Impl::Record{body,s,p,collider});
  if(wasHeld)hold(id);
}
void World::remove(int id){
  while(hasJoints(id))removeJoint(*impl->connections.at(id).begin());
  impl->connections.erase(id);
  auto it=impl->bodies.find(id);if(it==impl->bodies.end())return;impl->forgetContacts(id);auto &b=impl->system.GetBodyInterface();b.RemoveBody(it->second.body);b.DestroyBody(it->second.body);impl->bodies.erase(it);}
State World::get(int id)const{
  const auto &r=impl->record(id);auto &b=impl->system.GetBodyInterface();JPH::RVec3 p;JPH::Quat q;b.GetPositionAndRotation(r.body,p,q);
  auto a=b.GetAngularVelocity(r.body);return {r.settings,poseOf(p,q),scene(b.GetLinearVelocity(r.body)),{a.GetX(),a.GetY(),a.GetZ()},r.held,!b.IsActive(r.body),r.collider,hasJoints(id)};
}
int World::createJoint(int a,int b,const JointSettings &requested){
  if(a==b)throw std::invalid_argument("a joint needs two different physics objects");
  auto &ra=impl->record(a);auto &rb=impl->record(b);
  if(ra.settings.type!=Type::Dynamic&&rb.settings.type!=Type::Dynamic)throw std::invalid_argument("a joint needs at least one dynamic body");
  JointSettings s=validateJoint(requested);const auto axis=angular(s.axis);
  if(impl->nextJoint==std::numeric_limits<int>::max())throw std::runtime_error("physics joint ID limit reached");
  if(impl->joints.size()>=512)throw std::runtime_error("physics joint capacity exhausted");
  auto &locks=impl->system.GetBodyLockInterfaceNoLock();
  auto &bodyA=*locks.TryGetBody(ra.body);auto &bodyB=*locks.TryGetBody(rb.body);
  const Vec3 midpoint=scene((bodyA.GetPosition()+bodyB.GetPosition())*.5f);
  Vec3 pointA=s.type==JointType::Distance?s.anchorA.value_or(scene(bodyA.GetPosition())):s.anchor.value_or(midpoint);
  Vec3 pointB=s.type==JointType::Distance?s.anchorB.value_or(scene(bodyB.GetPosition())):pointA;
  const float initialDistance=(linear(pointA)-linear(pointB)).Length();
  if(s.type==JointType::Distance){s.maxDistance=s.maxDistance.value_or(initialDistance);if(*s.maxDistance<s.minDistance)throw std::invalid_argument("maxDistance must be at least minDistance");}
  JPH::Ref<JPH::TwoBodyConstraint> constraint;
  if(s.type==JointType::Fixed){
    JPH::FixedConstraintSettings c;c.mNumPositionStepsOverride=8;c.mNumVelocityStepsOverride=20;c.mPoint1=linear(pointA);c.mPoint2=linear(pointB);constraint=c.Create(bodyA,bodyB);
  }else if(s.type==JointType::Hinge){
    JPH::HingeConstraintSettings c;c.mNumPositionStepsOverride=8;c.mNumVelocityStepsOverride=20;c.mPoint1=linear(pointA);c.mPoint2=linear(pointB);
    c.mHingeAxis1=c.mHingeAxis2=axis;
    c.mNormalAxis1=c.mNormalAxis2=axis.Cross(std::abs(axis.GetY())<.9f?JPH::Vec3::sAxisY():JPH::Vec3::sAxisX()).Normalized();
    c.mLimitsMin=s.minAngle;c.mLimitsMax=s.maxAngle;constraint=c.Create(bodyA,bodyB);
  }else{
    JPH::DistanceConstraintSettings c;c.mNumPositionStepsOverride=8;c.mNumVelocityStepsOverride=20;c.mPoint1=linear(pointA);c.mPoint2=linear(pointB);
    c.mMinDistance=s.minDistance;c.mMaxDistance=*s.maxDistance;
    if(s.spring)c.mLimitsSpringSettings=JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping,s.spring->frequency,s.spring->damping);
    constraint=c.Create(bodyA,bodyB);
  }
  const int id=impl->nextJoint++;
  impl->joints.emplace(id,Impl::Joint{{id,a,b,s,pointA,pointB,initialDistance,0},constraint,
    bodyA.GetInverseCenterOfMassTransform()*linear(pointA),bodyB.GetInverseCenterOfMassTransform()*linear(pointB),
    poseOf(bodyA.GetPosition(),bodyA.GetRotation()),poseOf(bodyB.GetPosition(),bodyB.GetRotation())});
  impl->connections[a].insert(id);impl->connections[b].insert(id);
  if(!s.collideConnected)++impl->jointFilter->blocked[std::minmax(a,b)];
  impl->system.AddConstraint(constraint);
  for(auto *record:{&ra,&rb})if(record->held&&record->settings.type==Type::Dynamic)
    impl->system.GetBodyInterface().SetMotionType(record->body,JPH::EMotionType::Dynamic,JPH::EActivation::Activate);
  auto &bi=impl->system.GetBodyInterface();for(auto body:{ra.body,rb.body}){bi.InvalidateContactCache(body);bi.ActivateBody(body);}
  return id;
}
bool World::hasJoints(int id)const{auto it=impl->connections.find(id);return it!=impl->connections.end()&&!it->second.empty();}
bool World::containsJoint(int id)const{return impl->joints.contains(id);}
JointState World::joint(int id)const{
  const auto it=impl->joints.find(id);if(it==impl->joints.end())throw std::invalid_argument("unknown physics joint");
  const auto &j=it->second;auto state=j.state;const auto &locks=impl->system.GetBodyLockInterfaceNoLock();
  auto *a=locks.TryGetBody(impl->bodies.at(state.objectA).body);auto *b=locks.TryGetBody(impl->bodies.at(state.objectB).body);
  state.anchorA=scene(a->GetCenterOfMassTransform()*j.localA);state.anchorB=scene(b->GetCenterOfMassTransform()*j.localB);
  state.distance=(linear(state.anchorA)-linear(state.anchorB)).Length();
  if(state.settings.type==JointType::Hinge)state.angle=static_cast<const JPH::HingeConstraint*>(j.constraint.GetPtr())->GetCurrentAngle();
  return state;
}
std::vector<JointSnapshot> World::jointSnapshots()const{
  std::vector<JointSnapshot> result;result.reserve(impl->joints.size());
  for(const auto &[id,j]:impl->joints)result.push_back({joint(id),j.referenceA,j.referenceB});return result;
}
int World::nextJointId()const{return impl->nextJoint;}
void World::appendSnapshot(const std::vector<BodyRestore> &bodies,const std::vector<JointSnapshot> &joints){
  if(impl->joints.size()+joints.size()>512||joints.size()>size_t(std::numeric_limits<int>::max()-impl->nextJoint))throw std::runtime_error("physics joint capacity exhausted");
  std::set<int> ids,hulls,meshes;for(const auto &b:bodies)if(contains(b.id)||!ids.insert(b.id).second)throw std::invalid_argument("restore requires fresh body IDs");
  for(const auto &j:joints)if(!ids.contains(j.state.objectA)||!ids.contains(j.state.objectB))throw std::invalid_argument("restored joints require two restored bodies");
  for(const auto &[key,shape]:impl->hulls)hulls.insert(key);for(const auto &[key,shape]:impl->meshes)meshes.insert(key);
  const int next=impl->nextJoint;
  try{
    for(const auto &b:bodies){set(b.id,b.state.settings,b.state.pose,b.low,b.high,b.geometry);}
    for(const auto &j:joints){
      finite(j.referenceA);finite(j.referenceB);
      const auto a=get(j.state.objectA).pose,b=get(j.state.objectB).pose;
      pose(j.state.objectA,j.referenceA);pose(j.state.objectB,j.referenceB);
      const int id=createJoint(j.state.objectA,j.state.objectB,j.state.settings);
      pose(j.state.objectA,a);pose(j.state.objectB,b);
      if(j.state.settings.type==JointType::Hinge)jointMotor(id,j.state.motor);
    }
    for(const auto &b:bodies)if(b.state.settings.type==Type::Dynamic){velocity(b.id,b.state.velocity);angularVelocity(b.id,b.state.angularVelocity);}
  }catch(...){
    for(const auto &b:bodies)remove(b.id);impl->nextJoint=next;
    std::erase_if(impl->hulls,[&](const auto &entry){return !hulls.contains(entry.first);});
    std::erase_if(impl->meshes,[&](const auto &entry){return !meshes.contains(entry.first);});
    throw;
  }
}
void World::updateJoint(int id,const JointSettings &requested){
  auto it=impl->joints.find(id);if(it==impl->joints.end())throw std::invalid_argument("unknown physics joint");
  auto &j=it->second;const auto &old=j.state.settings;
  auto s=validateJoint(requested);
  if(requested.type!=old.type||requested.anchor!=old.anchor||requested.anchorA!=old.anchorA||requested.anchorB!=old.anchorB||requested.axis!=old.axis)throw std::invalid_argument("joint type, anchors and axis are immutable");
  s.axis=old.axis;
  if(s.type==JointType::Distance&&(!s.maxDistance||*s.maxDistance<s.minDistance))throw std::invalid_argument("maxDistance must be at least minDistance");
  if(s.type==JointType::Hinge){
    auto *c=static_cast<JPH::HingeConstraint*>(j.constraint.GetPtr());c->SetLimits(s.minAngle,s.maxAngle);
    j.state.motor.targetAngle=std::clamp(j.state.motor.targetAngle,s.minAngle,s.maxAngle);c->SetTargetAngle(j.state.motor.targetAngle);
  }else if(s.type==JointType::Distance){
    auto *c=static_cast<JPH::DistanceConstraint*>(j.constraint.GetPtr());c->SetDistance(s.minDistance,*s.maxDistance);
    c->SetLimitsSpringSettings(s.spring?JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping,s.spring->frequency,s.spring->damping):JPH::SpringSettings{});
  }
  if(s.collideConnected!=old.collideConnected){
    const auto key=std::minmax(j.state.objectA,j.state.objectB);
    if(s.collideConnected){auto pair=impl->jointFilter->blocked.find(key);if(--pair->second==0)impl->jointFilter->blocked.erase(pair);}
    else ++impl->jointFilter->blocked[key];
  }
  j.state.settings=s;j.constraint->ResetWarmStart();
  auto &bi=impl->system.GetBodyInterface();for(int object:{j.state.objectA,j.state.objectB}){const auto body=impl->record(object).body;bi.InvalidateContactCache(body);bi.ActivateBody(body);}
}
void World::jointMotor(int id,const JointMotor &motor){
  auto it=impl->joints.find(id);if(it==impl->joints.end())throw std::invalid_argument("unknown physics joint");
  auto &joint=it->second;
  if(joint.state.settings.type!=JointType::Hinge)throw std::invalid_argument("motors require a hinge joint");
  bounded(motor.targetAngle,joint.state.settings.minAngle,joint.state.settings.maxAngle);
  bounded(motor.targetVelocity,-30,30);bounded(motor.maxTorque,0,10000);
  bounded(motor.frequency,.01f,30);bounded(motor.damping,0,10);
  auto *hinge=static_cast<JPH::HingeConstraint*>(joint.constraint.GetPtr());
  auto &settings=hinge->GetMotorSettings();
  settings.mSpringSettings=JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping,motor.frequency,motor.damping);
  settings.SetTorqueLimit(motor.maxTorque);
  hinge->SetTargetAngle(motor.targetAngle);hinge->SetTargetAngularVelocity(motor.targetVelocity);
  hinge->SetMotorState(motor.mode==MotorMode::Off?JPH::EMotorState::Off:motor.mode==MotorMode::Position?JPH::EMotorState::Position:JPH::EMotorState::Velocity);
  joint.state.motor=motor;
  auto &bi=impl->system.GetBodyInterface();for(int object:{joint.state.objectA,joint.state.objectB}){impl->refreshMotorSleep(object);bi.ActivateBody(impl->record(object).body);}
}
std::vector<JointBreak> World::takeJointBreaks(){auto result=std::move(impl->breaks);impl->breaks.clear();return result;}
void World::removeJoint(int id){
  auto it=impl->joints.find(id);if(it==impl->joints.end())return;
  const auto state=it->second.state;impl->system.RemoveConstraint(it->second.constraint);
  if(!state.settings.collideConnected){auto pair=impl->jointFilter->blocked.find(std::minmax(state.objectA,state.objectB));if(--pair->second==0)impl->jointFilter->blocked.erase(pair);}
  impl->connections[state.objectA].erase(id);impl->connections[state.objectB].erase(id);
  impl->joints.erase(it);
  auto &bi=impl->system.GetBodyInterface();for(int object:{state.objectA,state.objectB})if(contains(object)){impl->refreshMotorSleep(object);const auto &r=impl->record(object);const auto body=r.body;if(r.held&&r.settings.type==Type::Dynamic&&!hasJoints(object))bi.SetMotionType(body,JPH::EMotionType::Kinematic,JPH::EActivation::Activate);bi.InvalidateContactCache(body);bi.ActivateBody(body);}
}
void World::gravity(Vec3 v){finite(v);impl->system.SetGravity(linear(v));}
void World::velocity(int id,Vec3 v){finite(v);auto &r=impl->record(id);if(r.settings.type!=Type::Dynamic||r.held)throw std::invalid_argument("velocity requires an unheld dynamic body");impl->system.GetBodyInterface().SetLinearVelocity(r.body,linear(v));}
void World::angularVelocity(int id,Vec3 v){finite(v);auto &r=impl->record(id);if(r.settings.type!=Type::Dynamic||r.held)throw std::invalid_argument("angular velocity requires an unheld dynamic body");impl->system.GetBodyInterface().SetAngularVelocity(r.body,angular(v));}
void World::impulse(int id,Vec3 v){finite(v);auto &r=impl->record(id);if(r.settings.type!=Type::Dynamic||r.held)throw std::invalid_argument("impulse requires an unheld dynamic body");impl->system.GetBodyInterface().AddImpulse(r.body,linear(v));}
void World::pose(int id,const Pose &p){finite(p);auto &r=impl->record(id);
  auto &b=impl->system.GetBodyInterface();
  if((b.GetPosition(r.body)-linear(p.position)).LengthSq()>1e-12f||std::fabs(b.GetRotation(r.body).Dot(rotation(p)))<.999999f)impl->forgetContacts(id);
  r.syncDirty=true;r.target=p;impl->system.GetBodyInterface().SetPositionAndRotation(r.body,linear(p.position),rotation(p),r.settings.type==Type::Static?JPH::EActivation::DontActivate:JPH::EActivation::Activate);r.samples.clear();}
void World::hold(int id){auto &r=impl->record(id);if(r.held)return;r.target=get(id).pose;r.held=true;r.canThrow=true;r.samples.clear();if(r.settings.type==Type::Dynamic&&!hasJoints(id))impl->system.GetBodyInterface().SetMotionType(r.body,JPH::EMotionType::Kinematic,JPH::EActivation::Activate);}
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
    const auto offset=linear(r.collider.centerOfMass);
    const auto previousCenter=linear(r.target.position)+rotation(r.target)*offset;
    const auto nextCenter=linear(p.position)+rotation(p)*offset;
    r.samples.push_back({elapsed,(nextCenter-previousCenter)/float(elapsed),axis*(angle/float(elapsed))});
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
bool World::needsTarget(int id)const{
  auto it=impl->bodies.find(id);return it!=impl->bodies.end()&&(it->second.held||it->second.settings.type==Type::Kinematic);
}
std::vector<PoseUpdate> World::takePoseUpdates(){
  std::vector<PoseUpdate> updates;auto &b=impl->system.GetBodyInterface();
  for(auto &[id,r]:impl->bodies){
    if(r.settings.type!=Type::Dynamic||(r.held&&!hasJoints(id)))continue;
    if(!r.syncDirty&&!b.IsActive(r.body))continue;
    JPH::RVec3 p;JPH::Quat q;b.GetPositionAndRotation(r.body,p,q);
    updates.push_back({id,poseOf(p,q)});r.syncDirty=false;
  }
  return updates;
}
void World::workers(int count){
  if(count < -1 || count > 4)throw std::invalid_argument("physics workers must be an integer from -1 to 4");
  if(count!=impl->workerSetting){impl->pool.reset();impl->workerSetting=count;}
}
Performance World::performance()const{return impl->perf;}
void World::advance(double elapsed){
  if(!std::isfinite(elapsed)||elapsed<0)throw std::invalid_argument("physics time must be finite and nonnegative");
  const auto start=std::chrono::steady_clock::now();impl->perf={};
  impl->accumulator=std::min(impl->accumulator+elapsed,8*step);
  auto &b=impl->system.GetBodyInterface();
  while(impl->accumulator+1e-10>=step){
    for(auto &[id,r]:impl->bodies)if(r.settings.type==Type::Dynamic&&b.IsActive(r.body))r.syncDirty=true;
    for(const auto &[id,r]:impl->bodies)if(r.held||r.settings.type==Type::Kinematic){
      if(r.settings.type==Type::Static)b.SetPositionAndRotation(r.body,linear(r.target.position),rotation(r.target),JPH::EActivation::DontActivate);
      else if(r.held&&r.settings.type==Type::Dynamic&&hasJoints(id)){
        // Keep the held body dynamic so the solver can enforce its joints.
        const auto body=impl->system.GetBodyLockInterfaceNoLock().TryGetBody(r.body);
        auto velocity=(linear(r.target.position)+rotation(r.target)*linear(r.collider.centerOfMass)-body->GetCenterOfMassPosition())*(.25f/float(step));
        JPH::Vec3 axis;float angle;auto delta=rotation(r.target)*body->GetRotation().Conjugated();if(delta.GetW()<0)delta=-delta;delta.GetAxisAngle(axis,angle);
        auto spin=axis*(angle*.25f/float(step));
        velocity=velocity.NormalizedOr(JPH::Vec3::sZero())*std::min(velocity.Length(),3.f);
        spin=spin.NormalizedOr(JPH::Vec3::sZero())*std::min(spin.Length(),12.f);
        b.SetLinearAndAngularVelocity(r.body,velocity,spin);
      }else b.MoveKinematic(r.body,linear(r.target.position),rotation(r.target),float(step));
    }
    impl->seen.clear();
    const unsigned active=impl->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    const int automatic=std::min(2,int(std::max(1u,std::thread::hardware_concurrency())-1));
    const int workers=impl->workerSetting<0?(active>=64?automatic:0):impl->workerSetting;
    if(workers&&!impl->pool){impl->pool=std::make_unique<JPH::JobSystemThreadPool>(2048,8,workers);}
    auto *jobs=workers?static_cast<JPH::JobSystem*>(impl->pool.get()):static_cast<JPH::JobSystem*>(&impl->jobs);
    const auto solveStart=std::chrono::steady_clock::now();
    const auto result=impl->system.Update(float(step),1,&impl->temporary,jobs);
    impl->perf.solverMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solveStart).count();
    impl->perf.workers=workers;++impl->perf.steps;
    if(result!=JPH::EPhysicsUpdateError::None)throw std::runtime_error("physics simulation capacity exceeded");
    if(impl->eventsEnabled)impl->finishContacts();
    std::vector<JointBreak> broken;
    for(const auto &[id,joint]:impl->joints){
      const auto &s=joint.state.settings;
      if((!s.breakForce&&!s.breakTorque)||!joint.constraint->IsActive())continue;
      float force=0,torque=0;
      if(s.type==JointType::Fixed){
        const auto *c=static_cast<const JPH::FixedConstraint*>(joint.constraint.GetPtr());
        force=c->GetTotalLambdaPosition().Length()/float(step);torque=c->GetTotalLambdaRotation().Length()/float(step);
      }else if(s.type==JointType::Hinge){
        const auto *c=static_cast<const JPH::HingeConstraint*>(joint.constraint.GetPtr());
        force=c->GetTotalLambdaPosition().Length()/float(step);
        const auto perpendicular=c->GetTotalLambdaRotation();const float axial=c->GetTotalLambdaRotationLimits()+c->GetTotalLambdaMotor();
        torque=std::sqrt(perpendicular.LengthSq()+axial*axial)/float(step);
      }else force=std::abs(static_cast<const JPH::DistanceConstraint*>(joint.constraint.GetPtr())->GetTotalLambdaPosition())/float(step);
      if((s.breakForce&&force>*s.breakForce)||(s.breakTorque&&torque>*s.breakTorque))broken.push_back({id,joint.state.objectA,joint.state.objectB,force,torque});
    }
    for(const auto &event:broken){removeJoint(event.jointId);impl->breaks.push_back(event);if(impl->breaks.size()>4096)impl->breaks.erase(impl->breaks.begin(),impl->breaks.begin()+1024);}
    impl->accumulator-=step;
  }
  impl->perf.activeBodies=impl->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
  impl->perf.advanceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}
