#pragma once
#include <span>
#include "spot_shadow_settings.h"
// Logical scene lights own no GPU resources. Attachments are resolved by ID each frame.
struct PointLight {
 int id=0,container=0,objectId=0;
 std::array<float,3> position{0,0,0},color{1,1,1};
 float intensity=1,range=1;
 bool enabled=true,spot=false,directional=false;
 SpotShadowSettings shadow;uint64_t shadowRevision=0;
 std::array<float,3> direction{0,0,1};
 float innerConeAngle=.35f,outerConeAngle=.6f;
};
constexpr size_t maxPointLights=8;
struct ResolvedPointLight {std::array<float,3> position,color;float intensity,range;
 std::array<float,3> direction{0,0,1};float innerCos=1,outerCos=-2;int id=0;
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
  result.push_back({position,light.color,light.intensity,light.directional?-1.f:light.range,direction,std::cos(light.innerConeAngle),light.spot?std::cos(light.outerConeAngle):-2.f,light.id});
 }
 return result;
}

struct ObjectLightSet {
 std::array<ResolvedPointLight,maxPointLights> lights{};size_t count=0;
 std::span<const ResolvedPointLight> values()const{return {lights.data(),count};}
};
// An infinite convex cone lies behind each tangent plane at its side and apex.
// Reject only if the entire padded sphere is outside one such plane.
static bool sphereOutsideSpotlight(const ResolvedPointLight &light,double x,double y,double z,double radius){
 if(!(light.outerCos>0&&light.outerCos<=1))return false; // Points and hemispherical/wider cones.
 const auto &axis=light.direction;
 const double length=std::sqrt(double(axis[0])*axis[0]+double(axis[1])*axis[1]+double(axis[2])*axis[2]);
 if(!std::isfinite(length)||length<1e-8)return false;
 const double along=(x*axis[0]+y*axis[1]+z*axis[2])/length;
 // The GPU uses the stored float direction without renormalizing it. Widen the
 // CPU cone slightly to cover its length/rounding and shader dot-product error.
 const double cosine=std::clamp(double(light.outerCos)/length-1e-6,0.,1.);
 const double sine=std::sqrt(std::max(0.,1.-cosine*cosine));
 const double radial=std::sqrt(std::max(0.,x*x+y*y+z*z-along*along));
 return along < -radius || radial*cosine-along*sine > radius;
}
template<class ObjectType,class Geometry>
static ObjectLightSet selectObjectLights(const ObjectType &object,const Geometry &geometry,std::span<const ResolvedPointLight> lights,bool filterCones=true){
 // Origin-centered sphere encloses every full/deformed mesh corner under rotation.
 // Double arithmetic and a small meter-space margin keep boundary cases conservative.
 bool valid=std::isfinite(object.scale)&&object.scale>0;
 double radius2=0;for(int axis=0;axis<3;axis++){valid=valid&&std::isfinite(geometry.low[axis])&&std::isfinite(geometry.high[axis])&&geometry.low[axis]<=geometry.high[axis];const double extent=std::max(std::abs(double(geometry.low[axis])),std::abs(double(geometry.high[axis])));radius2+=extent*extent;}
 const double radius=std::sqrt(radius2)*object.scale;
 ObjectLightSet result;
 for(const auto &light:lights){
  // Negative range marks an infinite directional source; never range/cone-cull it.
  if(light.range<0){if(result.count<result.lights.size())result.lights[result.count++]=light;continue;}
  const double dx=double(light.position[0])-object.x,dy=double(light.position[1])-object.y,dz=double(light.position[2])-object.z;
  const double magnitude=std::max({std::abs(double(object.x)),std::abs(double(object.y)),std::abs(double(object.z)),std::abs(double(light.position[0])),std::abs(double(light.position[1])),std::abs(double(light.position[2]))});
  const double reach=radius+light.range+1e-4+1e-5*(radius+magnitude);
  if(valid&&std::isfinite(radius)&&dx*dx+dy*dy+dz*dz>reach*reach)continue;
  if(filterCones&&valid&&std::isfinite(radius)&&sphereOutsideSpotlight(light,-dx,-dy,-dz,reach-light.range))continue;
  if(result.count<result.lights.size())result.lights[result.count++]=light;
 }
 return result;
}
template<class SceneType>
static void updateObjectLights(const SceneType &scene,std::span<const ResolvedPointLight> lights,std::vector<ObjectLightSet> &sets,bool filterCones=true){
 sets.resize(scene.objects.size());
 if(lights.empty()){for(auto &set:sets)set.count=0;return;}
 for(size_t i=0;i<scene.objects.size();i++){
  const auto &object=scene.objects[i];const auto &asset=scene.assets[size_t(object.asset)-1];
  if(!object.visible||!asset.pbr){sets[i].count=0;continue;}
  if(lights.size()<4){auto &set=sets[i];set.count=lights.size();std::copy(lights.begin(),lights.end(),set.lights.begin());continue;}
  const auto &geometry=object.animationPose?object.animationPose->geometry:asset;
  sets[i]=selectObjectLights(object,geometry,lights,filterCones);
 }
}
