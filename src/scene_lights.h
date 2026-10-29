#pragma once
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
