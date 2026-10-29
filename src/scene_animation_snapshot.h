#pragma once
// Included inside scenePersistence after strict JSON helpers and physics values.
struct SavedCursor {animation::Cursor state;std::string name;double duration=0;};
struct SavedAnimation {animation::Playback playback;std::string name;double duration=0;SavedCursor source;};
static JSValue cursorValue(JSContext *ctx,const animation::Cursor &state,const ModelAsset &asset){
 const auto &clip=asset.animations.at(state.clip);Value value(ctx,JS_NewObject(ctx));
 put(ctx,value,"clip",JS_NewInt32(ctx,state.clip));put(ctx,value,"name",JS_NewString(ctx,clip.name.c_str()));
 put(ctx,value,"duration",JS_NewFloat64(ctx,clip.duration));put(ctx,value,"time",JS_NewFloat64(ctx,state.time));put(ctx,value,"speed",JS_NewFloat64(ctx,state.speed));
 put(ctx,value,"loop",JS_NewBool(ctx,state.loop));put(ctx,value,"playing",JS_NewBool(ctx,state.playing));put(ctx,value,"finished",JS_NewBool(ctx,state.finished));return JS_DupValue(ctx,value);
}
static SavedCursor cursorRead(JSContext *ctx,JSValueConst value){
 SavedCursor saved;auto &s=saved.state;s.clip=int(number(ctx,value,"clip",0,INT_MAX-1,true));saved.name=textField(ctx,value,"name");
 saved.duration=number(ctx,value,"duration",0,std::numeric_limits<double>::max());s.time=number(ctx,value,"time",0,saved.duration);s.speed=number(ctx,value,"speed",0,100);require(s.speed>0,"animation speed must be greater than zero");
 s.loop=boolean(ctx,value,"loop");s.playing=boolean(ctx,value,"playing");s.finished=boolean(ctx,value,"finished");
 require(!s.finished||(!s.playing&&s.time==saved.duration&&(!s.loop||saved.duration==0)),"inconsistent finished animation state");return saved;
}
template<class Values> static JSValue floatArrayValue(JSContext *ctx,const Values &values){
 Value array(ctx,JS_NewArray(ctx));for(size_t i=0;i<values.size();i++){require(std::isfinite(values[i]),"non-finite transition pose");putIndex(ctx,array,uint32_t(i),JS_NewFloat64(ctx,values[i]));}return JS_DupValue(ctx,array);
}
static std::vector<float> floatArrayRead(JSContext *ctx,JSValueConst value,uint32_t limit){
 const auto n=length(ctx,value,limit);std::vector<float> values;values.reserve(n);
 for(uint32_t i=0;i<n;i++)values.push_back(float(number(ctx,value,std::to_string(i).c_str(),-floatLimit,floatLimit)));
 return values;
}
static JSValue animationValue(JSContext *ctx,const Object &o,const ModelAsset &asset){
 if(o.animation.clip<0)return JS_NULL;
 const auto &state=o.animation;Value value(ctx,cursorValue(ctx,state,asset));JSValue transition=JS_NULL;
 const auto &blend=state.transition;
 if(blend.active()){
  Value item(ctx,JS_NewObject(ctx));put(ctx,item,"duration",JS_NewFloat64(ctx,blend.duration));put(ctx,item,"elapsed",JS_NewFloat64(ctx,blend.elapsed));
  put(ctx,item,"source",blend.source.clip>=0?cursorValue(ctx,blend.source,asset):JS_NULL);
  JSValue frozen=JS_NULL;
  if(blend.source.clip<0){
   require(blend.frozen.size()<=65536,"transition pose node limit");Value nodes(ctx,JS_NewArray(ctx));
   for(size_t i=0;i<blend.frozen.size();i++){
    const auto &node=blend.frozen[i];Value local(ctx,JS_NewObject(ctx));
    put(ctx,local,"translation",floatArrayValue(ctx,node.translation));put(ctx,local,"rotation",floatArrayValue(ctx,node.rotation));
    put(ctx,local,"scale",floatArrayValue(ctx,node.scale));put(ctx,local,"weights",floatArrayValue(ctx,node.weights));putIndex(ctx,nodes,uint32_t(i),JS_DupValue(ctx,local));
   }
   frozen=JS_DupValue(ctx,nodes);
  }
  put(ctx,item,"frozen",frozen);transition=JS_DupValue(ctx,item);
 }
 put(ctx,value,"transition",transition);return JS_DupValue(ctx,value);
}
static SavedAnimation animationRead(JSContext *ctx,JSValueConst value,int version){
 const auto cursor=cursorRead(ctx,value);SavedAnimation saved;static_cast<animation::Cursor&>(saved.playback)=cursor.state;saved.name=cursor.name;saved.duration=cursor.duration;
 auto transition=field(ctx,value,"transition");
 if(version<5){require(JS_IsUndefined(transition)||JS_IsNull(transition),"transitions require snapshot version 5");return saved;}
 require(!JS_IsUndefined(transition),"missing transition field");if(JS_IsNull(transition))return saved;
 auto &blend=saved.playback.transition;blend.duration=number(ctx,transition,"duration",0,60);require(blend.duration>0,"transition duration must be positive");
 blend.elapsed=number(ctx,transition,"elapsed",0,blend.duration);require(blend.elapsed<blend.duration,"completed transition must be null");
 require(!saved.playback.finished,"finished animation cannot have an active transition");
 auto source=field(ctx,transition,"source"),frozen=field(ctx,transition,"frozen");
 if(!JS_IsNull(source)){
  saved.source=cursorRead(ctx,source);blend.source=saved.source.state;require(JS_IsNull(frozen),"live source cannot have a frozen pose");
 }else{
  const auto n=length(ctx,frozen,65536);blend.frozen.resize(n);
  for(uint32_t i=0;i<n;i++){
   Value node(ctx,JS_GetPropertyUint32(ctx,frozen,i));auto &out=blend.frozen[i];
   auto read=[&](const char *key,auto &array){auto v=field(ctx,node,key);auto values=floatArrayRead(ctx,v,uint32_t(array.size()));require(values.size()==array.size(),"transition vector size");std::copy(values.begin(),values.end(),array.begin());};
   read("translation",out.translation);read("rotation",out.rotation);read("scale",out.scale);
   double norm=0;for(float v:out.rotation)norm+=double(v)*v;require(norm>1e-20,"invalid transition quaternion");
   auto weights=field(ctx,node,"weights");out.weights=floatArrayRead(ctx,weights,maxBytes/2);
  }
 }
 return saved;
}
static void validateAnimation(const SavedAnimation &saved,const ModelAsset &asset,animation::Playback &state){
 auto validate=[&](const animation::Cursor &cursor,const std::string &name,double duration){
  require(cursor.clip>=0&&size_t(cursor.clip)<asset.animations.size(),"saved animation clip no longer exists");const auto &clip=asset.animations[cursor.clip];
  require(clip.supported(),"saved animation clip is unsupported");require(clip.name==name&&clip.duration==duration,"saved animation metadata does not match model");
 };
 validate(saved.playback,saved.name,saved.duration);state=saved.playback;
 if(!state.transition.active())return;
 auto &blend=state.transition;
 if(blend.source.clip>=0)validate(blend.source,saved.source.name,saved.source.duration);
 else{
  require(blend.frozen.size()==asset.nodes.size(),"transition pose node count differs from asset");
  for(size_t i=0;i<blend.frozen.size();i++){
   auto &node=blend.frozen[i];require(node.weights.size()==asset.nodes[i].weights.size(),"transition morph count differs from asset");
   node.parent=asset.nodes[i].parent;node.matrixOnly=asset.nodes[i].matrixOnly;node.matrix=asset.nodes[i].matrix;
  }
 }
}
