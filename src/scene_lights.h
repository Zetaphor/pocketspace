#pragma once
// Logical scene lights own no GPU resources. Attachments are resolved by ID each frame.
struct PointLight {
 int id=0,container=0,objectId=0;
 std::array<float,3> position{0,0,0},color{1,1,1};
 float intensity=1,range=1;
 bool enabled=true;
};
constexpr size_t maxPointLights=8;
struct ResolvedPointLight {std::array<float,3> position,color;float intensity,range;};
template<class SceneType>
static std::vector<ResolvedPointLight> resolvePointLights(const SceneType &scene){
 std::vector<ResolvedPointLight> result;result.reserve(scene.lights.size());
 for(const auto &light:scene.lights){
  if(!light.enabled||light.intensity==0)continue;
  auto position=light.position;
  if(light.objectId){
   const auto *object=findSceneItem(scene.objects,light.objectId);if(!object)continue;
   // Offset is in object-local meters (+Z forward), independent of model scale.
   float x=position[0],y=position[1],z=-position[2];
   const float cr=std::cos(object->roll),sr=std::sin(object->roll),cp=std::cos(object->pitch),sp=std::sin(object->pitch),cy=std::cos(object->yaw),sy=std::sin(object->yaw);
   float rx=cr*x-sr*y,ry=sr*x+cr*y;
   float py=cp*ry-sp*z,pz=sp*ry+cp*z;
   position={object->x+cy*rx+sy*pz,object->y+py,object->z+sy*rx-cy*pz};
  }
  result.push_back({position,light.color,light.intensity,light.range});
 }
 return result;
}
