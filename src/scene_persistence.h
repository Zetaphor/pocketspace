#pragma once
// Included after the scene bindings: persistence shares the normal asset cache
// and overlay hooks, but stages all native changes until validation succeeds.
#include <cerrno>
#include <climits>
#include <stdexcept>
#include <sys/stat.h>
namespace scenePersistence {
constexpr size_t maxBytes=1024*1024;
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
  auto data=string(ctx,encoded);require(data.size()<=maxBytes,"snapshot exceeds 1 MiB");return data;
}
static JSValue capture(JSContext *ctx,int owner){
  require(owner&&currentScene->containers.count(owner),"capture requires an active scene container");
  for(const auto &p:currentScene->panels)require(p.container!=owner,"v1 snapshots do not support owned panels");
  Value result(ctx,JS_NewObject(ctx)),models(ctx,JS_NewArray(ctx)),objects(ctx,JS_NewArray(ctx));std::map<int,int> modelIds;uint32_t index=0;
  put(ctx,result,"format",JS_NewString(ctx,"sf-overlay-engine.scene-container"));
  put(ctx,result,"version",JS_NewInt32(ctx,1));
  for(const auto &o:currentScene->objects)if(o.container==owner){
    require(!o.animationPose,"v1 snapshots do not support animated poses");
    require(!currentScene->physics||!currentScene->physics->contains(o.id),"v1 snapshots do not support physics bodies");
    if(!modelIds.count(o.asset)){
      const auto &a=currentScene->assets.at(o.asset-1);const int i=int(modelIds.size());modelIds[o.asset]=i;
      JSValue m=JS_NewObject(ctx);put(ctx,m,"path",JS_NewString(ctx,a.path.c_str()));put(ctx,m,"eyePixels",JS_NewInt32(ctx,a.eyePixels));
      put(ctx,m,"optimizeVertexCache",JS_NewBool(ctx,a.optimizeVertexCache));put(ctx,m,"gpuInstancing",JS_NewBool(ctx,a.gpuInstancing));put(ctx,m,"lod",JS_NewBool(ctx,a.lod));putIndex(ctx,models,i,m);
    }
    for(auto [key,member]:transforms)require(std::isfinite(o.*member),"non-finite object transform");
    JSValue item=JS_NewObject(ctx);put(ctx,item,"id",JS_NewInt32(ctx,o.id));put(ctx,item,"model",JS_NewInt32(ctx,modelIds.at(o.asset)));
    put(ctx,item,"renderGroup",JS_NewInt32(ctx,o.renderGroup));
    for(auto [key,member]:transforms)put(ctx,item,key,JS_NewFloat64(ctx,o.*member));
    for(auto [key,member]:flags)put(ctx,item,key,JS_NewBool(ctx,o.*member));
    put(ctx,item,"grabButton",JS_NewString(ctx,o.grabButton==GrabButton::Both?"both":o.grabButton==GrabButton::Grip?"grip":"trigger"));
    put(ctx,item,"interactive",JS_NewBool(ctx,std::find(currentScene->interactiveIds.begin(),currentScene->interactiveIds.end(),o.id)!=currentScene->interactiveIds.end()));
    putIndex(ctx,objects,index++,item);
  }
  put(ctx,result,"models",JS_DupValue(ctx,models));put(ctx,result,"objects",JS_DupValue(ctx,objects));
  json(ctx,result);return JS_DupValue(ctx,result);
}
static JSValue restore(JSContext *ctx,JSValueConst snapshot){
  // Detach from script getters/proxies before inspecting or reserving resources.
  const auto data=json(ctx,snapshot);Value root(ctx,JS_ParseJSON(ctx,data.data(),data.size(),"scene snapshot"));
  require(!JS_IsException(root),"invalid scene JSON");
  auto format=field(ctx,root,"format");require(string(ctx,format)=="sf-overlay-engine.scene-container","unsupported snapshot format");
  require(number(ctx,root,"version",1,1,true)==1,"unsupported snapshot version");
  auto models=field(ctx,root,"models"),objects=field(ctx,root,"objects");
  const auto nm=length(ctx,models,32),no=length(ctx,objects,256);auto &scene=*currentScene;
  require(scene.containers.size()<256&&scene.nextContainerId<INT_MAX,"scene container limit reached");
  require(scene.objects.size()+no<=256&&scene.nextId<=INT_MAX-int(no),"object limit reached");
  std::vector<ModelAsset> pending;std::vector<int> assets;std::set<std::string> paths;
  for(uint32_t i=0;i<nm;i++){
    Value m(ctx,JS_GetPropertyUint32(ctx,models,i));auto path=field(ctx,m,"path");ModelAsset a;a.path=string(ctx,path);require(!a.path.empty()&&paths.insert(a.path).second,"empty or duplicate model path");
    a.eyePixels=int(number(ctx,m,"eyePixels",256,2048,true));require(a.eyePixels%64==0,"eyePixels must be a multiple of 64");
    a.optimizeVertexCache=boolean(ctx,m,"optimizeVertexCache");a.gpuInstancing=boolean(ctx,m,"gpuInstancing");a.lod=boolean(ctx,m,"lod");
    int id=0;for(size_t j=0;j<scene.assets.size();j++)if(scene.assets[j].path==a.path){const auto &old=scene.assets[j];require(old.eyePixels==a.eyePixels&&old.optimizeVertexCache==a.optimizeVertexCache&&old.gpuInstancing==a.gpuInstancing&&old.lod==a.lod,"cached model options conflict");id=int(j)+1;break;}
    if(!id){require(scene.assets.size()+pending.size()<32,"model asset limit reached");id=int(scene.assets.size()+pending.size())+1;pending.push_back(std::move(a));}assets.push_back(id);
  }
  std::set<int> occupied,sourceIds;for(const auto &o:scene.objects)if(o.renderGroup)occupied.insert(o.renderGroup);
  for(const auto &[id,policy]:scene.renderGroupSplits)occupied.insert(id);
  std::map<int,int> groups;std::vector<Object> restored;std::vector<int> interactive;
  for(uint32_t i=0;i<no;i++){
    Value item(ctx,JS_GetPropertyUint32(ctx,objects,i));Object o{};
    const int source=int(number(ctx,item,"id",1,INT_MAX-1,true));require(sourceIds.insert(source).second,"duplicate source object id");
    o.id=scene.nextId+int(i);o.container=scene.nextContainerId;
    const int model=int(number(ctx,item,"model",0,double(nm)-1,true));o.asset=assets.at(model);
    const int group=int(number(ctx,item,"renderGroup",0,256,true));
    if(group&&!groups.count(group)){int free=1;while(free<=256&&occupied.count(free))free++;require(free<=256,"no free render groups");groups[group]=free;occupied.insert(free);}
    o.renderGroup=group?groups.at(group):0;
    for(auto [key,member]:transforms)o.*member=float(number(ctx,item,key,-std::numeric_limits<float>::max(),std::numeric_limits<float>::max()));
    require(o.scale>0&&o.scale<=2,"scale must be greater than zero and at most 2");
    for(auto [key,member]:flags)o.*member=boolean(ctx,item,key);
    auto button=field(ctx,item,"grabButton");auto name=string(ctx,button);require(name=="both"||name=="trigger"||name=="grip","invalid grabButton");o.grabButton=name=="both"?GrabButton::Both:name=="grip"?GrabButton::Grip:GrabButton::Trigger;
    if(boolean(ctx,item,"interactive"))interactive.push_back(o.id);
    restored.push_back(std::move(o));
  }
  // Load CPU data only after the entire schema has passed. No shared cache or
  // native overlays change until every asset is available.
  for(auto &a:pending){const auto path=a.path;require(loadModelFile(path,a),"could not load snapshot model");}
  scene.assets.reserve(scene.assets.size()+pending.size());scene.objects.reserve(scene.objects.size()+no);scene.interactiveIds.reserve(scene.interactiveIds.size()+interactive.size());
  Value result(ctx,JS_NewObject(ctx)),ids(ctx,JS_NewArray(ctx)),mapping(ctx,JS_NewObject(ctx)),groupMap(ctx,JS_NewObject(ctx));
  for(uint32_t i=0;i<no;i++){
    Value item(ctx,JS_GetPropertyUint32(ctx,objects,i));auto source=std::to_string(int(number(ctx,item,"id",1,INT_MAX-1,true)));
    putIndex(ctx,ids,i,JS_NewInt32(ctx,restored[i].id));put(ctx,mapping,source.c_str(),JS_NewInt32(ctx,restored[i].id));
  }
  for(auto [from,to]:groups)put(ctx,groupMap,std::to_string(from).c_str(),JS_NewInt32(ctx,to));
  put(ctx,result,"containerId",JS_NewInt32(ctx,scene.nextContainerId));put(ctx,result,"objectIds",JS_DupValue(ctx,ids));put(ctx,result,"objectMap",JS_DupValue(ctx,mapping));put(ctx,result,"renderGroups",JS_DupValue(ctx,groupMap));
  // This also detects JS allocation failures before committing native state.
  if(JS_HasException(ctx))throw std::runtime_error("snapshot result allocation failed");
  std::set<int> reserved;bool committed=false;
  ScopeExit rollback{[&]{if(!committed&&releaseObjectOverlay)for(int key:reserved)releaseObjectOverlay(key);}};
  if(currentScene==liveScene&&reserveObjectOverlay)for(const auto &o:restored){int key=objectSurfaceKey(o);if(!reserved.count(key)){auto error=reserveObjectOverlay(key);require(error.empty(),error.c_str());reserved.insert(key);}}
  scene.containers.insert(scene.nextContainerId);scene.nextContainerId++;
  for(auto &a:pending)scene.assets.push_back(std::move(a));for(auto &o:restored)scene.objects.push_back(std::move(o));
  scene.interactiveIds.insert(scene.interactiveIds.end(),interactive.begin(),interactive.end());scene.nextId+=int(no);committed=true;
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
  struct stat st{};require(fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0&&uint64_t(st.st_size)<=maxBytes,"scene file must be a regular file of at most 1 MiB");
  std::string data;char buffer[4096];for(;;){auto n=::read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;require(n>=0,"could not read scene file");if(!n)break;require(data.size()+size_t(n)<=maxBytes,"scene exceeds 1 MiB");data.append(buffer,size_t(n));}return data;
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
