#pragma once
#include <span>
// Logical scene lights own no GPU resources. Attachments are resolved by ID each frame.
struct PointLight {
 int id=0,container=0,objectId=0;
 std::array<float,3> position{0,0,0},color{1,1,1};
 float intensity=1,range=1;
 bool enabled=true,spot=false;
 std::array<float,3> direction{0,0,1};
 float innerConeAngle=.35f,outerConeAngle=.6f;
};
constexpr size_t maxPointLights=8;
struct ResolvedPointLight {std::array<float,3> position,color;float intensity,range;
 std::array<float,3> direction{0,0,1};float innerCos=1,outerCos=-2;
};
template<class ObjectType>
static std::array<float,3> rotateLightVector(const ObjectType &object,std::array<float,3> v){
 const float x=v[0],y=v[1],z=-v[2];
 const float cr=std::cos(object.roll),sr=std::sin(object.roll),cp=std::cos(object.pitch),sp=std::sin(object.pitch),cy=std::cos(object.yaw),sy=std::sin(object.yaw);
 const float rx=cr*x-sr*y,ry=sr*x+cr*y,py=cp*ry-sp*z,pz=sp*ry+cp*z;
 return {cy*rx+sy*pz,py,sy*rx-cy*pz};
}
template<class SceneType>
static std::vector<ResolvedPointLight> resolvePointLights(const SceneType &scene){
 std::vector<ResolvedPointLight> result;result.reserve(scene.lights.size());
 for(const auto &light:scene.lights){
  if(!light.enabled||light.intensity==0)continue;
  auto position=light.position,direction=light.direction;
  if(light.objectId){
   const auto *object=findSceneItem(scene.objects,light.objectId);if(!object)continue;
   // Local offsets are meters, independent of scale; direction rotates without translation.
   position=rotateLightVector(*object,position);direction=rotateLightVector(*object,direction);
   position[0]+=object->x;position[1]+=object->y;position[2]+=object->z;
  }
  const float length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
  for(auto &component:direction)component/=length;
  result.push_back({position,light.color,light.intensity,light.range,direction,std::cos(light.innerConeAngle),light.spot?std::cos(light.outerConeAngle):-2.f});
 }
 return result;
}

struct ObjectLightSet {
 std::array<ResolvedPointLight,maxPointLights> lights{};size_t count=0;
 std::span<const ResolvedPointLight> values()const{return {lights.data(),count};}
};
template<class ObjectType,class Geometry>
static ObjectLightSet selectObjectLights(const ObjectType &object,const Geometry &geometry,std::span<const ResolvedPointLight> lights){
 // Origin-centered sphere encloses every full/deformed mesh corner under rotation.
 // Double arithmetic and a small meter-space margin keep boundary cases conservative.
 bool valid=std::isfinite(object.scale)&&object.scale>0;
 double radius2=0;for(int axis=0;axis<3;axis++){valid=valid&&std::isfinite(geometry.low[axis])&&std::isfinite(geometry.high[axis])&&geometry.low[axis]<=geometry.high[axis];const double extent=std::max(std::abs(double(geometry.low[axis])),std::abs(double(geometry.high[axis])));radius2+=extent*extent;}
 const double radius=std::sqrt(radius2)*object.scale;
 ObjectLightSet result;
 for(const auto &light:lights){
  const double dx=double(light.position[0])-object.x,dy=double(light.position[1])-object.y,dz=double(light.position[2])-object.z;
  const double magnitude=std::max({std::abs(double(object.x)),std::abs(double(object.y)),std::abs(double(object.z)),std::abs(double(light.position[0])),std::abs(double(light.position[1])),std::abs(double(light.position[2]))});
  const double reach=radius+light.range+1e-4+1e-5*(radius+magnitude);
  if(valid&&std::isfinite(radius)&&dx*dx+dy*dy+dz*dz>reach*reach)continue;
  if(result.count<result.lights.size())result.lights[result.count++]=light;
 }
 return result;
}
template<class SceneType>
static void updateObjectLights(const SceneType &scene,std::span<const ResolvedPointLight> lights,std::vector<ObjectLightSet> &sets){
 sets.resize(scene.objects.size());
 if(lights.empty()){for(auto &set:sets)set.count=0;return;}
 for(size_t i=0;i<scene.objects.size();i++){
  const auto &object=scene.objects[i];const auto &asset=scene.assets[size_t(object.asset)-1];
  if(!object.visible||!asset.pbr){sets[i].count=0;continue;}
  if(lights.size()<4){auto &set=sets[i];set.count=lights.size();std::copy(lights.begin(),lights.end(),set.lights.begin());continue;}
  const auto &geometry=object.animationPose?object.animationPose->geometry:asset;
  sets[i]=selectObjectLights(object,geometry,lights);
 }
}
