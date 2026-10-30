#pragma once
// Included after the scene bindings: persistence shares the normal asset cache
// and overlay hooks, but stages all native changes until validation succeeds.
#include <cerrno>
#include <climits>
#include <stdexcept>
#include <sys/stat.h>
namespace scenePersistence {
constexpr size_t maxBytes=16*1024*1024;
static std::string directory;
struct Value {
  JSContext *ctx;JSValue value;
  Value(JSContext *c,JSValue v):ctx(c),value(v){}
  ~Value(){JS_FreeValue(ctx,value);}
  Value(const Value&)=delete;
  operator JSValueConst()const{return value;}
};
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static Value field(JSContext *ctx,JSValueConst object,const char *key){
  if(!JS_IsObject(object))return {ctx,JS_UNDEFINED};
  JSPropertyDescriptor desc{};JSAtom atom=JS_NewAtom(ctx,key);const int found=JS_GetOwnProperty(ctx,&desc,object,atom);JS_FreeAtom(ctx,atom);
  if(found<=0)return {ctx,found<0?JS_EXCEPTION:JS_UNDEFINED};
  JS_FreeValue(ctx,desc.getter);JS_FreeValue(ctx,desc.setter);return {ctx,desc.value};
}
static void put(JSContext *ctx,JSValueConst object,const char *key,JSValue value){JS_DefinePropertyValueStr(ctx,object,key,value,JS_PROP_C_W_E);}
static void putIndex(JSContext *ctx,JSValueConst object,uint32_t index,JSValue value){JS_DefinePropertyValueUint32(ctx,object,index,value,JS_PROP_C_W_E);}
static double number(JSContext *ctx,JSValueConst o,const char *key,double lo,double hi,bool integer=false){
  auto v=field(ctx,o,key);double n=0;
  require(JS_IsNumber(v)&&JS_ToFloat64(ctx,&n,v)==0&&std::isfinite(n)&&n>=lo&&n<=hi&&(!integer||n==std::floor(n)),key);return n;
}
static bool boolean(JSContext *ctx,JSValueConst o,const char *key){auto v=field(ctx,o,key);require(JS_IsBool(v),key);return JS_ToBool(ctx,v)>0;}
static std::string string(JSContext *ctx,JSValueConst value){
  require(JS_IsString(value),"expected string");size_t size=0;const char *p=JS_ToCStringLen(ctx,&size,value);require(p,"string allocation failed");
  std::string result(p,size);JS_FreeCString(ctx,p);require(result.find('\0')==std::string::npos,"embedded NUL in string");return result;
}
static uint32_t length(JSContext *ctx,JSValueConst array,uint32_t limit){require(JS_IsArray(array),"expected array");return uint32_t(number(ctx,array,"length",0,limit,true));}
static constexpr std::pair<const char*,bool Object::*> flags[]={
  {"visible",&Object::visible},{"laserPassThrough",&Object::laserPassThrough},{"grabbable",&Object::grabbable},
  {"directGrab",&Object::directGrab},{"autoManipulate",&Object::autoManipulate},{"laserYaw",&Object::laserYaw},
  {"laserPitchWithStickClick",&Object::laserPitchWithStickClick},{"depthAdjust",&Object::depthAdjust},
  {"handPitch",&Object::handPitch},{"handYaw",&Object::handYaw},{"handRoll",&Object::handRoll}};
static constexpr std::pair<const char*,float Object::*> transforms[]={
  {"x",&Object::x},{"y",&Object::y},{"z",&Object::z},{"pitch",&Object::pitch},{"yaw",&Object::yaw},{"roll",&Object::roll},{"scale",&Object::scale}};
static std::string json(JSContext *ctx,JSValueConst value){
  Value encoded(ctx,JS_JSONStringify(ctx,value,JS_UNDEFINED,JS_UNDEFINED));
  if(JS_IsException(encoded))throw std::runtime_error("snapshot cannot be encoded as JSON");
  auto data=string(ctx,encoded);require(data.size()<=maxBytes,"snapshot exceeds 16 MiB");return data;
}
#include "scene_physics_snapshot.h"
#include "scene_animation_snapshot.h"
#include "scene_panel_snapshot.h"
static JSValue capture(JSContext *ctx,int owner){
  require(owner&&currentScene->containers.count(owner),"capture requires an active scene container");
  const auto owners=containerSubtree(*currentScene,owner);
  Value result(ctx,JS_NewObject(ctx)),models(ctx,JS_NewArray(ctx)),objects(ctx,JS_NewArray(ctx)),joints(ctx,JS_NewArray(ctx)),panels(ctx,JS_NewArray(ctx));std::map<int,int> modelIds;uint32_t index=0;
  put(ctx,result,"format",JS_NewString(ctx,"sf-overlay-engine.scene-container"));
  put(ctx,result,"version",JS_NewInt32(ctx,13));
  put(ctx,result,"rootContainer",JS_NewInt32(ctx,owner));
  Value hierarchy(ctx,JS_NewArray(ctx));uint32_t hierarchyIndex=0;
  for(int id:owners){Value item(ctx,JS_NewObject(ctx));put(ctx,item,"id",JS_NewInt32(ctx,id));put(ctx,item,"parent",JS_NewInt32(ctx,id==owner?0:currentScene->containerParents.at(id)));putIndex(ctx,hierarchy,hierarchyIndex++,JS_DupValue(ctx,item));}
  put(ctx,result,"containers",JS_DupValue(ctx,hierarchy));
  for(auto o:currentScene->objects)if(owners.count(o.container)){
    const bool body=currentScene->physics&&currentScene->physics->contains(o.id);
    std::optional<physics::State> state;if(body){state=currentScene->physics->get(o.id);o.x=state->pose.position.x;o.y=state->pose.position.y;o.z=state->pose.position.z;o.pitch=state->pose.pitch;o.yaw=state->pose.yaw;o.roll=state->pose.roll;}
    if(!modelIds.count(o.asset)){
      const auto &a=currentScene->assets.at(o.asset-1);const int i=int(modelIds.size());modelIds[o.asset]=i;
      JSValue m=JS_NewObject(ctx);put(ctx,m,"path",JS_NewString(ctx,a.path.c_str()));put(ctx,m,"eyePixels",JS_NewInt32(ctx,a.eyePixels));
      put(ctx,m,"optimizeVertexCache",JS_NewBool(ctx,a.optimizeVertexCache));put(ctx,m,"gpuInstancing",JS_NewBool(ctx,a.gpuInstancing));put(ctx,m,"lod",JS_NewBool(ctx,a.lod));put(ctx,m,"pbr",JS_NewBool(ctx,a.pbr));
      Value lighting(ctx,JS_NewObject(ctx));
      for(auto [name,values]:{std::pair{"direction",a.lighting.direction},std::pair{"color",a.lighting.color}}){Value array(ctx,JS_NewArray(ctx));for(int c=0;c<3;c++)putIndex(ctx,array,c,JS_NewFloat64(ctx,values[c]));put(ctx,lighting,name,JS_DupValue(ctx,array));}
      put(ctx,lighting,"intensity",JS_NewFloat64(ctx,a.lighting.intensity));put(ctx,lighting,"ambient",JS_NewFloat64(ctx,a.lighting.ambient));put(ctx,lighting,"exposure",JS_NewFloat64(ctx,a.lighting.exposure));put(ctx,lighting,"environment",JS_NewString(ctx,a.lighting.environment.c_str()));put(ctx,lighting,"environmentIntensity",JS_NewFloat64(ctx,a.lighting.environmentIntensity));put(ctx,lighting,"environmentRotation",JS_NewFloat64(ctx,a.lighting.environmentRotation));put(ctx,m,"lighting",JS_DupValue(ctx,lighting));putIndex(ctx,models,i,m);
    }
    for(auto [key,member]:transforms)require(std::isfinite(o.*member),"non-finite object transform");
    JSValue item=JS_NewObject(ctx);put(ctx,item,"id",JS_NewInt32(ctx,o.id));put(ctx,item,"model",JS_NewInt32(ctx,modelIds.at(o.asset)));
    put(ctx,item,"renderGroup",JS_NewInt32(ctx,o.renderGroup));put(ctx,item,"container",JS_NewInt32(ctx,o.container));
    for(auto [key,member]:transforms)put(ctx,item,key,JS_NewFloat64(ctx,o.*member));
    for(auto [key,member]:flags)put(ctx,item,key,JS_NewBool(ctx,o.*member));
    put(ctx,item,"grabButton",JS_NewString(ctx,o.grabButton==GrabButton::Both?"both":o.grabButton==GrabButton::Grip?"grip":"trigger"));
    put(ctx,item,"interactive",JS_NewBool(ctx,std::find(currentScene->interactiveIds.begin(),currentScene->interactiveIds.end(),o.id)!=currentScene->interactiveIds.end()));
    put(ctx,item,"physics",state?bodyValue(ctx,*state):JS_NULL);
    put(ctx,item,"animation",animationValue(ctx,o,currentScene->assets.at(o.asset-1)));
    putIndex(ctx,objects,index++,item);
  }
  uint32_t jointIndex=0;if(currentScene->physics)for(const auto &j:currentScene->physics->jointSnapshots()){
    const auto *a=findObject(j.state.objectA),*b=findObject(j.state.objectB);const bool ownA=a&&owners.count(a->container),ownB=b&&owners.count(b->container);
    require(ownA==ownB,"joint crosses scene container boundary; save both endpoints in one container");
    if(ownA)putIndex(ctx,joints,jointIndex++,jointValue(ctx,j));
  }
  uint32_t panelIndex=0;size_t panelBytes=0;for(const auto &p:currentScene->panels)if(owners.count(p.container)){require(panelIndex<maxPanels,"snapshot panel limit is 32");Value item(ctx,panelValue(ctx,p,panelBytes));put(ctx,item,"container",JS_NewInt32(ctx,p.container));putIndex(ctx,panels,panelIndex++,JS_DupValue(ctx,item));}
  Value lights(ctx,JS_NewArray(ctx));uint32_t lightIndex=0;
  for(const auto &light:currentScene->lights){
    const auto *object=light.objectId?findObject(light.objectId):nullptr;
    require(!object||(bool(owners.count(object->container))==bool(owners.count(light.container))),"light attachment crosses scene container boundary");
    if(owners.count(light.container))putIndex(ctx,lights,lightIndex++,pointLightValue(ctx,light));
  }
  put(ctx,result,"lights",JS_DupValue(ctx,lights));
  put(ctx,result,"panels",JS_DupValue(ctx,panels));
  put(ctx,result,"joints",JS_DupValue(ctx,joints));
  put(ctx,result,"models",JS_DupValue(ctx,models));put(ctx,result,"objects",JS_DupValue(ctx,objects));
  json(ctx,result);return JS_DupValue(ctx,result);
}
static JSValue restore(JSContext *ctx,JSValueConst snapshot){
  // Detach from script getters/proxies before inspecting or reserving resources.
  const auto data=json(ctx,snapshot);Value root(ctx,JS_ParseJSON(ctx,data.data(),data.size(),"scene snapshot"));
  require(!JS_IsException(root),"invalid scene JSON");
  auto format=field(ctx,root,"format");require(string(ctx,format)=="sf-overlay-engine.scene-container","unsupported snapshot format");
  const int version=int(number(ctx,root,"version",1,13,true));
  auto models=field(ctx,root,"models"),objects=field(ctx,root,"objects");
  const auto nm=length(ctx,models,32),no=length(ctx,objects,256);auto &scene=*currentScene;
  std::map<int,int> containerMap,newParents;int restoredRoot=scene.nextContainerId;
  if(version>=11){
    auto hierarchy=field(ctx,root,"containers");const auto count=length(ctx,hierarchy,256);require(count>0,"empty container hierarchy");
    const int sourceRoot=int(number(ctx,root,"rootContainer",1,INT_MAX-1,true));
    require(scene.containers.size()+count<=256&&scene.nextContainerId<=INT_MAX-int(count),"scene container limit reached");
    std::map<int,int> sources;
    for(uint32_t i=0;i<count;i++){Value item(ctx,JS_GetPropertyUint32(ctx,hierarchy,i));const int source=int(number(ctx,item,"id",1,INT_MAX-1,true));const int parent=int(number(ctx,item,"parent",0,INT_MAX-1,true));require(sources.emplace(source,parent).second,"duplicate container ID");containerMap[source]=scene.nextContainerId+int(i);}
    require(sources.count(sourceRoot)&&sources.at(sourceRoot)==0,"invalid root container");restoredRoot=containerMap.at(sourceRoot);
    for(auto [source,parent]:sources){
      if(source!=sourceRoot)require(parent&&sources.count(parent),"unknown container parent");
      std::set<int> visited;int cursor=source;while(cursor){require(visited.insert(cursor).second,"container cycle");auto found=sources.find(cursor);require(found!=sources.end(),"unknown ancestor");cursor=found->second;}
      newParents[containerMap.at(source)]=parent?containerMap.at(parent):0;
    }
  }else{require(scene.containers.size()<256&&scene.nextContainerId<INT_MAX,"scene container limit reached");newParents[scene.nextContainerId]=0;}
  auto restoredOwner=[&](JSValueConst item){if(version<11)return restoredRoot;const int source=int(number(ctx,item,"container",1,INT_MAX-1,true));require(containerMap.count(source),"unknown resource container");return containerMap.at(source);};
  // Allocate hierarchy nodes before native publication/reservation.
  auto stagedContainers=scene.containers;auto stagedParents=scene.containerParents;
  for(auto [id,parent]:newParents){stagedContainers.insert(id);stagedParents[id]=parent;}

  require(scene.objects.size()+no<=256&&scene.nextId<=INT_MAX-int(no),"object limit reached");
  std::vector<ModelAsset> pending;std::vector<int> assets;std::set<std::string> paths;
  for(uint32_t i=0;i<nm;i++){
    Value m(ctx,JS_GetPropertyUint32(ctx,models,i));auto path=field(ctx,m,"path");ModelAsset a;a.path=string(ctx,path);require(!a.path.empty()&&paths.insert(a.path).second,"empty or duplicate model path");
    a.eyePixels=int(number(ctx,m,"eyePixels",256,2048,true));require(a.eyePixels%64==0,"eyePixels must be a multiple of 64");
    a.optimizeVertexCache=boolean(ctx,m,"optimizeVertexCache");a.gpuInstancing=boolean(ctx,m,"gpuInstancing");a.lod=boolean(ctx,m,"lod");
    if(version>=6){
      a.pbr=boolean(ctx,m,"pbr");auto lighting=field(ctx,m,"lighting");
      a.lighting.intensity=float(number(ctx,lighting,"intensity",0,64));a.lighting.ambient=float(number(ctx,lighting,"ambient",0,16));a.lighting.exposure=float(number(ctx,lighting,"exposure",.001,64));
      for(auto [name,values,isColor]:{std::tuple{"direction",a.lighting.direction,false},std::tuple{"color",a.lighting.color,true}}){auto array=field(ctx,lighting,name);require(length(ctx,array,3)==3,"lighting vectors must have three components");for(int c=0;c<3;c++){Value v(ctx,JS_GetPropertyUint32(ctx,array,c));double n=0;require(JS_IsNumber(v)&&JS_ToFloat64(ctx,&n,v)==0&&std::isfinite(n)&&n>=(isColor?0:-64)&&n<=64,"invalid lighting vector");values[c]=float(n);}}
      double magnitude=0;for(float v:a.lighting.direction)magnitude+=double(v)*v;require(magnitude>=1e-12,"light direction must be nonzero");
    }
    if(version>=7){
      auto lighting=field(ctx,m,"lighting"),environment=field(ctx,lighting,"environment");a.lighting.environment=string(ctx,environment);require(a.lighting.environment.size()<=4096,"invalid environment path");
      a.lighting.environmentIntensity=float(number(ctx,lighting,"environmentIntensity",0,64));a.lighting.environmentRotation=float(number(ctx,lighting,"environmentRotation",-10000,10000));
      require(a.pbr||a.lighting.environment.empty(),"environment requires PBR");
    }
    int id=0;for(size_t j=0;j<scene.assets.size();j++)if(scene.assets[j].path==a.path){const auto &old=scene.assets[j];require(old.eyePixels==a.eyePixels&&old.optimizeVertexCache==a.optimizeVertexCache&&old.gpuInstancing==a.gpuInstancing&&old.lod==a.lod&&old.pbr==a.pbr&&(!a.pbr||old.lighting==a.lighting),"cached model options conflict");id=int(j)+1;break;}
    if(!id){require(loadedAssetCount(scene)+pending.size()<32&&scene.assets.size()+pending.size()<size_t(INT_MAX-1),"model asset limit reached");id=int(scene.assets.size()+pending.size())+1;pending.push_back(std::move(a));}assets.push_back(id);
  }
  std::set<int> occupied,sourceIds;for(const auto &o:scene.objects)if(o.renderGroup)occupied.insert(o.renderGroup);
  for(const auto &[id,policy]:scene.renderGroupSplits)occupied.insert(id);
  std::map<int,int> groups;std::vector<Object> restored;std::vector<int> interactive;
  std::map<int,int> objectIds;
  std::map<int,int> bodyMap;std::vector<physics::BodyRestore> bodies;std::vector<std::vector<physics::Vec3>> geometry;std::vector<std::optional<SavedAnimation>> animations;
  for(uint32_t i=0;i<no;i++){
    Value item(ctx,JS_GetPropertyUint32(ctx,objects,i));Object o{};
    const int source=int(number(ctx,item,"id",1,INT_MAX-1,true));require(sourceIds.insert(source).second,"duplicate source object id");
    o.id=scene.nextId+int(i);o.container=restoredOwner(item);objectIds[source]=o.id;
    const int model=int(number(ctx,item,"model",0,double(nm)-1,true));o.asset=assets.at(model);
    const int group=int(number(ctx,item,"renderGroup",0,256,true));
    if(group&&!groups.count(group)){int free=1;while(free<=256&&occupied.count(free))free++;require(free<=256,"no free render groups");groups[group]=free;occupied.insert(free);}
    o.renderGroup=group?groups.at(group):0;
    for(auto [key,member]:transforms)o.*member=float(number(ctx,item,key,-std::numeric_limits<float>::max(),std::numeric_limits<float>::max()));
    require(o.scale>0&&o.scale<=2,"scale must be greater than zero and at most 2");
    for(auto [key,member]:flags)o.*member=boolean(ctx,item,key);
    auto button=field(ctx,item,"grabButton");auto name=string(ctx,button);require(name=="both"||name=="trigger"||name=="grip","invalid grabButton");o.grabButton=name=="both"?GrabButton::Both:name=="grip"?GrabButton::Grip:GrabButton::Trigger;
    if(boolean(ctx,item,"interactive"))interactive.push_back(o.id);
    auto body=field(ctx,item,"physics");
    if(version>=2){require(!JS_IsUndefined(body),"missing physics field");if(!JS_IsNull(body)){bodyMap[source]=o.id;bodies.push_back({o.id,bodyRead(ctx,body,o)});}}
    else require(JS_IsUndefined(body)||JS_IsNull(body),"physics requires snapshot version 2");
    auto animation=field(ctx,item,"animation");std::optional<SavedAnimation> saved;
    if(version>=3){require(!JS_IsUndefined(animation),"missing animation field");if(!JS_IsNull(animation))saved=animationRead(ctx,animation,version);}
    else require(JS_IsUndefined(animation)||JS_IsNull(animation),"animation requires snapshot version 3");
    animations.push_back(std::move(saved));restored.push_back(std::move(o));
  }
  std::vector<physics::JointSnapshot> jointBatch;std::set<int> jointSources;
  auto jointArray=field(ctx,root,"joints");
  const uint32_t nj=version>=2?length(ctx,jointArray,512):0;
  if(version==1&&!JS_IsUndefined(jointArray))require(length(ctx,jointArray,512)==0,"joints require snapshot version 2");
  for(uint32_t i=0;i<nj;i++){Value item(ctx,JS_GetPropertyUint32(ctx,jointArray,i));auto j=jointRead(ctx,item,bodyMap);require(jointSources.insert(j.state.id).second,"duplicate source joint ID");jointBatch.push_back(std::move(j));}
  const int firstJoint=scene.physics?scene.physics->nextJointId():1;require(nj<=uint32_t(INT_MAX-firstJoint),"joint ID limit reached");
  auto panelArray=field(ctx,root,"panels");const uint32_t np=version>=4?length(ctx,panelArray,maxPanels):0;
  if(version<4&&!JS_IsUndefined(panelArray))require(length(ctx,panelArray,maxPanels)==0,"panels require snapshot version 4");
  require(scene.nextPanelId<=INT_MAX-int(np),"panel ID limit reached");std::vector<Panel> restoredPanels;std::vector<int> panelSources;std::set<int> uniquePanels;size_t pixelBytes=0;
  for(uint32_t i=0;i<np;i++){Value item(ctx,JS_GetPropertyUint32(ctx,panelArray,i));const int source=int(number(ctx,item,"id",1,INT_MAX-1,true));require(uniquePanels.insert(source).second,"duplicate source panel ID");panelSources.push_back(source);restoredPanels.push_back(panelRead(ctx,item,scene.nextPanelId+int(i),restoredOwner(item),pixelBytes));}
  auto lightArray=field(ctx,root,"lights");const uint32_t nl=version>=8?length(ctx,lightArray,maxPointLights):0;
  if(version<8&&!JS_IsUndefined(lightArray))require(length(ctx,lightArray,maxPointLights)==0,"lights require snapshot version 8");
  require(scene.lights.size()+nl<=maxPointLights&&scene.nextLightId<=INT_MAX-int(nl),"point light limit reached");
  std::vector<PointLight> restoredLights;std::vector<int> lightSources;std::set<int> uniqueLights;
  for(uint32_t i=0;i<nl;i++){
    Value item(ctx,JS_GetPropertyUint32(ctx,lightArray,i));PointLight light;
    const int source=int(number(ctx,item,"id",1,INT_MAX-1,true));require(uniqueLights.insert(source).second,"duplicate light ID");lightSources.push_back(source);
    light.id=scene.nextLightId+int(i);light.container=restoredOwner(item);
    light.intensity=float(number(ctx,item,"intensity",0,64));light.enabled=boolean(ctx,item,"enabled");
    auto type=field(ctx,item,"type");
    if(version>=9){const auto name=string(ctx,type);require(name=="point"||name=="spot"||(version>=10&&name=="directional"),"invalid light type");light.spot=name=="spot";light.directional=name=="directional";}
    else require(JS_IsUndefined(type)||string(ctx,type)=="point","spotlights require snapshot version 9");
    if(!light.directional)light.range=float(number(ctx,item,"range",.001,64));
    else for(const char *name:{"position","range","innerConeAngle","outerConeAngle"}){auto unused=field(ctx,item,name);require(JS_IsUndefined(unused),"unsupported directional light field");}
    for(auto [name,destination,low,high]:{std::tuple{"position",&light.position,-10000.f,10000.f},std::tuple{"color",&light.color,0.f,64.f}}){
      if(light.directional&&std::string(name)=="position")continue;
      auto array=field(ctx,item,name);require(length(ctx,array,3)==3,"light vector must contain three numbers");
      for(int c=0;c<3;c++){Value component(ctx,JS_GetPropertyUint32(ctx,array,c));double n=0;require(JS_IsNumber(component)&&JS_ToFloat64(ctx,&n,component)==0&&std::isfinite(n)&&n>=low&&n<=high,"invalid light vector");(*destination)[c]=float(n);}
    }
    if(light.spot){
      light.innerConeAngle=float(number(ctx,item,"innerConeAngle",0,float(std::acos(-1)/2)));light.outerConeAngle=float(number(ctx,item,"outerConeAngle",.001f,float(std::acos(-1)/2)));
      require(light.innerConeAngle<=light.outerConeAngle,"invalid spotlight cone angles");
    }
    if(light.spot||light.directional){
      auto direction=field(ctx,item,"direction");require(length(ctx,direction,3)==3,"spotlight direction must contain three numbers");double magnitude=0;
      for(int c=0;c<3;c++){Value component(ctx,JS_GetPropertyUint32(ctx,direction,c));double n=0;require(JS_IsNumber(component)&&JS_ToFloat64(ctx,&n,component)==0&&std::isfinite(n)&&n>=-64&&n<=64,"invalid spotlight direction");light.direction[c]=float(n);magnitude+=n*n;}
      require(magnitude>=1e-12,"spotlight direction must be nonzero");
    }
    auto shadow=field(ctx,item,"shadow");
    if((version>=12&&light.spot)||(version>=13&&light.directional)){
      light.shadow.enabled=boolean(ctx,shadow,"enabled");
      light.shadow.resolution=int(number(ctx,shadow,"resolution",256,1024,true));require((light.shadow.resolution&(light.shadow.resolution-1))==0,"invalid shadow resolution");
      light.shadow.maxTriangles=int(number(ctx,shadow,"maxTriangles",1,1000000,true));
      if(light.directional){
        auto center=field(ctx,shadow,"center");require(length(ctx,center,3)==3,"invalid directional shadow center");
        for(int c=0;c<3;c++){Value component(ctx,JS_GetPropertyUint32(ctx,center,c));double n=0;require(JS_IsNumber(component)&&JS_ToFloat64(ctx,&n,component)==0&&std::isfinite(n)&&n>=-10000&&n<=10000,"invalid directional shadow center");light.shadow.center[c]=float(n);}
        light.shadow.size=float(number(ctx,shadow,"size",.01f,64));light.shadow.depth=float(number(ctx,shadow,"depth",.01f,64));
        auto nearValue=field(ctx,shadow,"near");require(JS_IsUndefined(nearValue),"directional shadow does not support near");
      }else {light.shadow.nearPlane=float(number(ctx,shadow,"near",.001f,1));for(const char *name:{"center","size","depth"}){auto unused=field(ctx,shadow,name);require(JS_IsUndefined(unused),"directional-only shadow field");}}
      light.shadow.bias=float(number(ctx,shadow,"bias",0,.05f));light.shadow.normalBias=float(number(ctx,shadow,"normalBias",0,.1f));light.shadow.maxGpuMs=float(number(ctx,shadow,"maxGpuMs",.001f,16));
      require(!light.spot||!light.shadow.enabled||(light.shadow.nearPlane<light.range&&light.outerConeAngle<=1.5f),"invalid shadow camera");
    }else if(!JS_IsUndefined(shadow))require(light.spot&&!boolean(ctx,shadow,"enabled"),"shadow settings require a supported light and snapshot version");
    auto attachment=field(ctx,item,"objectId");if(!JS_IsNull(attachment)){const int id=int(number(ctx,item,"objectId",1,INT_MAX-1,true));require(objectIds.count(id),"light attachment must be inside snapshot");light.objectId=objectIds.at(id);}
    restoredLights.push_back(light);
  }
  size_t shadowSlots=0;for(const auto &lights:{std::cref(scene.lights),std::cref(restoredLights)})for(const auto &light:lights.get())shadowSlots+=(light.spot||light.directional)&&light.enabled&&light.shadow.enabled;
  require(shadowSlots<=1,"only one enabled shadow-casting light is supported");
  // Load CPU data only after the entire schema has passed. No shared cache or
  // native overlays change until every asset is available.
  for(auto &a:pending){const auto path=a.path;require(loadModelFile(path,a),"could not load snapshot model");if(!a.lighting.environment.empty()){a.environment=loadLightingEnvironment(a.lighting.environment);require(bool(a.environment),"could not load snapshot environment");}}
  for(size_t i=0;i<restored.size();i++)if(animations[i]){
    auto &o=restored[i];const auto &saved=*animations[i];const auto &a=o.asset<=int(scene.assets.size())?scene.assets[o.asset-1]:pending[o.asset-int(scene.assets.size())-1];
    validateAnimation(saved,a,o.animation);o.animationPose=std::make_shared<ModelPose>();evaluateModelPose(a,o.animation,*o.animationPose);
  }
  geometry.reserve(bodies.size());
  for(auto &body:bodies){
    const auto &o=*std::find_if(restored.begin(),restored.end(),[&](const Object &o){return o.id==body.id;});
    const auto &a=o.asset<=int(scene.assets.size())?scene.assets[o.asset-1]:pending[o.asset-int(scene.assets.size())-1];
    body.low={a.low[0]*o.scale,a.low[1]*o.scale,a.low[2]*o.scale};body.high={a.high[0]*o.scale,a.high[1]*o.scale,a.high[2]*o.scale};
    geometry.emplace_back();auto &vertices=geometry.back();
    if(body.state.settings.shape==physics::Shape::Convex||body.state.settings.shape==physics::Shape::Mesh)for(const auto &part:a.primitives)for(size_t i=0;i+4<part.vertices.size();i+=5)vertices.push_back({part.vertices[i],part.vertices[i+1],part.vertices[i+2]});
    body.geometry={o.asset,o.scale,vertices};
  }
  scene.lights.reserve(scene.lights.size()+nl);
  scene.panels.reserve(scene.panels.size()+np);
  scene.assets.reserve(scene.assets.size()+pending.size());scene.objects.reserve(scene.objects.size()+no);scene.interactiveIds.reserve(scene.interactiveIds.size()+interactive.size());
  Value result(ctx,JS_NewObject(ctx)),ids(ctx,JS_NewArray(ctx)),mapping(ctx,JS_NewObject(ctx)),groupMap(ctx,JS_NewObject(ctx)),jointIds(ctx,JS_NewArray(ctx)),jointMap(ctx,JS_NewObject(ctx)),panelIds(ctx,JS_NewArray(ctx)),panelMap(ctx,JS_NewObject(ctx)),panelHandles(ctx,JS_NewArray(ctx));
  for(uint32_t i=0;i<no;i++){
    Value item(ctx,JS_GetPropertyUint32(ctx,objects,i));auto source=std::to_string(int(number(ctx,item,"id",1,INT_MAX-1,true)));
    putIndex(ctx,ids,i,JS_NewInt32(ctx,restored[i].id));put(ctx,mapping,source.c_str(),JS_NewInt32(ctx,restored[i].id));
  }
  for(auto [from,to]:groups)put(ctx,groupMap,std::to_string(from).c_str(),JS_NewInt32(ctx,to));
  Value restoredContainers(ctx,JS_NewArray(ctx)),restoredContainerMap(ctx,JS_NewObject(ctx));uint32_t containerIndex=0;
  for(auto [id,parent]:newParents)putIndex(ctx,restoredContainers,containerIndex++,JS_NewInt32(ctx,id));
  for(auto [source,id]:containerMap)put(ctx,restoredContainerMap,std::to_string(source).c_str(),JS_NewInt32(ctx,id));
  put(ctx,result,"containerIds",JS_DupValue(ctx,restoredContainers));put(ctx,result,"containerMap",JS_DupValue(ctx,restoredContainerMap));
  put(ctx,result,"containerId",JS_NewInt32(ctx,restoredRoot));put(ctx,result,"objectIds",JS_DupValue(ctx,ids));put(ctx,result,"objectMap",JS_DupValue(ctx,mapping));put(ctx,result,"renderGroups",JS_DupValue(ctx,groupMap));
  for(uint32_t i=0;i<nj;i++){putIndex(ctx,jointIds,i,JS_NewInt32(ctx,firstJoint+int(i)));put(ctx,jointMap,std::to_string(jointBatch[i].state.id).c_str(),JS_NewInt32(ctx,firstJoint+int(i)));}
  put(ctx,result,"jointIds",JS_DupValue(ctx,jointIds));put(ctx,result,"jointMap",JS_DupValue(ctx,jointMap));
  for(uint32_t i=0;i<np;i++){const int id=restoredPanels[i].id;putIndex(ctx,panelIds,i,JS_NewInt32(ctx,id));put(ctx,panelMap,std::to_string(panelSources[i]).c_str(),JS_NewInt32(ctx,id));putIndex(ctx,panelHandles,i,panelHandle(ctx,id));}
  put(ctx,result,"panelIds",JS_DupValue(ctx,panelIds));put(ctx,result,"panelMap",JS_DupValue(ctx,panelMap));put(ctx,result,"panels",JS_DupValue(ctx,panelHandles));
  Value lightIds(ctx,JS_NewArray(ctx)),lightMap(ctx,JS_NewObject(ctx));
  for(uint32_t i=0;i<nl;i++){putIndex(ctx,lightIds,i,JS_NewInt32(ctx,restoredLights[i].id));put(ctx,lightMap,std::to_string(lightSources[i]).c_str(),JS_NewInt32(ctx,restoredLights[i].id));}
  put(ctx,result,"lightIds",JS_DupValue(ctx,lightIds));put(ctx,result,"lightMap",JS_DupValue(ctx,lightMap));
  // This also detects JS allocation failures before committing native state.
  if(JS_HasException(ctx))throw std::runtime_error("snapshot result allocation failed");
  std::set<int> reserved;std::vector<int> reservedPanels;reservedPanels.reserve(np);bool committed=false;
  ScopeExit rollback{[&]{if(!committed){if(releaseObjectOverlay)for(int key:reserved)releaseObjectOverlay(key);if(releasePanelOverlay)for(int id:reservedPanels)releasePanelOverlay(id);}}};
  if(currentScene==liveScene&&reserveObjectOverlay)for(const auto &o:restored){int key=objectSurfaceKey(o);if(!reserved.count(key)){auto error=reserveObjectOverlay(key);require(error.empty(),error.c_str());reserved.insert(key);}}
  if(currentScene==liveScene&&reservePanelOverlay)for(const auto &p:restoredPanels){const auto error=reservePanelOverlay(p);require(error.empty(),error.c_str());reservedPanels.push_back(p.id);}
  std::unique_ptr<physics::World> newWorld;
  if(!bodies.empty()){
    auto *world=scene.physics.get();if(!world){newWorld=std::make_unique<physics::World>();world=newWorld.get();}
    world->appendSnapshot(bodies,jointBatch);
  }
  scene.containers.swap(stagedContainers);scene.containerParents.swap(stagedParents);scene.nextContainerId+=int(newParents.size());
  for(auto &a:pending)scene.assets.push_back(std::move(a));for(auto &o:restored)scene.objects.push_back(std::move(o));
  for(auto &p:restoredPanels)scene.panels.push_back(std::move(p));scene.nextPanelId+=int(np);
  for(const auto &light:restoredLights)scene.lights.push_back(light);scene.nextLightId+=int(nl);
  scene.interactiveIds.insert(scene.interactiveIds.end(),interactive.begin(),interactive.end());scene.nextId+=int(no);if(newWorld)scene.physics=std::move(newWorld);committed=true;
  return JS_DupValue(ctx,result);
}
static std::filesystem::path slot(JSContext *ctx,JSValueConst name){
  auto n=string(ctx,name);require(!n.empty()&&n.size()<=64&&std::all_of(n.begin(),n.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';}),"scene name must contain 1..64 ASCII letters, digits, underscores or hyphens");
  std::filesystem::path base=directory;
  if(base.empty()){const char *state=getenv("XDG_STATE_HOME");if(state&&std::filesystem::path(state).is_absolute())base=state;else {const char *home=getenv("HOME");require(home&&*home,"HOME or --scene-state-dir required");base=std::filesystem::path(home)/".local/state";}base/="sf-overlay-engine/scenes";}
  return base/(n+".json");
}
static void write(const std::filesystem::path &path,const std::string &data){
  std::filesystem::create_directories(path.parent_path());std::string temp=path.string()+".tmp-XXXXXX";std::vector<char> buffer(temp.begin(),temp.end());buffer.push_back(0);
  int fd=mkstemp(buffer.data());require(fd>=0,"could not create temporary scene file");
  ScopeExit cleanup{[&]{if(fd>=0)close(fd);unlink(buffer.data());}};
  size_t done=0;while(done<data.size()){auto n=::write(fd,data.data()+done,data.size()-done);if(n<0&&errno==EINTR)continue;require(n>0,"could not write scene file");done+=size_t(n);}
  require(fsync(fd)==0,"could not sync scene file");const int status=close(fd);fd=-1;require(status==0,"could not close scene file");require(rename(buffer.data(),path.c_str())==0,"could not replace scene file");
}
static std::string read(const std::filesystem::path &path){
  const int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);require(fd>=0,"could not open scene file");ScopeExit cleanup{[&]{close(fd);}};
  struct stat st{};require(fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0&&uint64_t(st.st_size)<=maxBytes,"scene file must be a regular file of at most 16 MiB");
  std::string data;char buffer[4096];for(;;){auto n=::read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;require(n>=0,"could not read scene file");if(!n)break;require(data.size()+size_t(n)<=maxBytes,"scene exceeds 16 MiB");data.append(buffer,size_t(n));}return data;
}
}
static JSValue jsCaptureSceneContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(!argc)return JS_ThrowTypeError(ctx,"captureSceneContainer(id)");if(!containerId(ctx,argv[0],id))return JS_EXCEPTION;
  try{return scenePersistence::capture(ctx,id);}catch(const std::exception &e){if(JS_HasException(ctx))return JS_EXCEPTION;return JS_ThrowTypeError(ctx,"%s",e.what());}
}
static JSValue jsRestoreSceneContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!argc)return JS_ThrowTypeError(ctx,"restoreSceneContainer(snapshot)");
  try{return scenePersistence::restore(ctx,argv[0]);}catch(const std::exception &e){if(JS_HasException(ctx))return JS_EXCEPTION;return JS_ThrowTypeError(ctx,"%s",e.what());}
}
static JSValue jsSaveSceneContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<2)return JS_ThrowTypeError(ctx,"saveSceneContainer(id, name)");int id=0;if(!containerId(ctx,argv[0],id))return JS_EXCEPTION;
  try{auto path=scenePersistence::slot(ctx,argv[1]);scenePersistence::Value value(ctx,scenePersistence::capture(ctx,id));scenePersistence::write(path,scenePersistence::json(ctx,value));return JS_NewString(ctx,path.c_str());}catch(const std::exception &e){if(JS_HasException(ctx))return JS_EXCEPTION;return JS_ThrowTypeError(ctx,"%s",e.what());}
}
static JSValue jsLoadSceneContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!argc)return JS_ThrowTypeError(ctx,"loadSceneContainer(name)");
  try{auto path=scenePersistence::slot(ctx,argv[0]);auto data=scenePersistence::read(path);scenePersistence::Value value(ctx,JS_ParseJSON(ctx,data.data(),data.size(),path.c_str()));if(JS_IsException(value))return JS_EXCEPTION;return scenePersistence::restore(ctx,value);}catch(const std::exception &e){if(JS_HasException(ctx))return JS_EXCEPTION;return JS_ThrowTypeError(ctx,"%s",e.what());}
}
