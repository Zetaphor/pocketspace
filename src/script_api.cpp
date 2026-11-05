// JavaScript bindings and script loading, compiled once and linked by every target.
#include "engine_state.h"
JSValue inputEventValue(JSContext *ctx,const frame_input::Event &event){JSValue item=JS_NewObject(ctx);JS_SetPropertyStr(ctx,item,"type",JS_NewString(ctx,event.type.c_str()));JS_SetPropertyStr(ctx,item,"hand",JS_NewString(ctx,event.hand.c_str()));JS_SetPropertyStr(ctx,item,"control",JS_NewString(ctx,event.control.c_str()));JS_SetPropertyStr(ctx,item,"objectId",JS_NewInt32(ctx,event.objectId));JS_SetPropertyStr(ctx,item,"x",JS_NewFloat64(ctx,event.x));JS_SetPropertyStr(ctx,item,"y",JS_NewFloat64(ctx,event.y));JS_SetPropertyStr(ctx,item,"down",JS_NewBool(ctx,event.component.boolean));JS_SetPropertyStr(ctx,item,"value",JS_NewFloat64(ctx,event.component.value));JS_SetPropertyStr(ctx,item,"dx",JS_NewFloat64(ctx,event.dx));JS_SetPropertyStr(ctx,item,"dy",JS_NewFloat64(ctx,event.dy));JS_SetPropertyStr(ctx,item,"dz",JS_NewFloat64(ctx,event.dz));JS_SetPropertyStr(ctx,item,"dpitch",JS_NewFloat64(ctx,event.dpitch));JS_SetPropertyStr(ctx,item,"dyaw",JS_NewFloat64(ctx,event.dyaw));JS_SetPropertyStr(ctx,item,"droll",JS_NewFloat64(ctx,event.droll));if(event.type=="physics.jointBreak"){
  JS_SetPropertyStr(ctx,item,"jointId",JS_NewInt32(ctx,event.jointId));JS_SetPropertyStr(ctx,item,"otherObjectId",JS_NewInt32(ctx,event.otherObjectId));
  JS_SetPropertyStr(ctx,item,"force",JS_NewFloat64(ctx,event.force));JS_SetPropertyStr(ctx,item,"torque",JS_NewFloat64(ctx,event.torque));
}else if(event.type.starts_with("overlay.meshHover")||event.type=="overlay.grabStart"){
  JSValue point=JS_NewObject(ctx);for(int i=0;i<3;i++)JS_SetPropertyStr(ctx,point,i==0?"x":i==1?"y":"z",JS_NewFloat64(ctx,event.point[i]));
  JS_SetPropertyStr(ctx,item,"point",point);JS_SetPropertyStr(ctx,item,"distance",JS_NewFloat64(ctx,event.distance));
}else if(event.type.starts_with("physics.collision")){
  JS_SetPropertyStr(ctx,item,"otherObjectId",JS_NewInt32(ctx,event.otherObjectId));
  auto vector=[&](const std::array<float,3> &v){JSValue o=JS_NewObject(ctx);for(int i=0;i<3;i++)JS_SetPropertyStr(ctx,o,i==0?"x":i==1?"y":"z",JS_NewFloat64(ctx,v[i]));return o;};
  JS_SetPropertyStr(ctx,item,"point",vector(event.point));JS_SetPropertyStr(ctx,item,"normal",vector(event.normal));
  JS_SetPropertyStr(ctx,item,"penetration",JS_NewFloat64(ctx,event.penetration));JS_SetPropertyStr(ctx,item,"relativeSpeed",JS_NewFloat64(ctx,event.relativeSpeed));
}return item;}
static JSValue jsGetEngineStats(JSContext *ctx,JSValueConst,int,JSValueConst*){
  JSValue value=JS_NewObject(ctx);
  auto number=[&](const char *name,auto n){JS_SetPropertyStr(ctx,value,name,n?JS_NewFloat64(ctx,double(*n)):JS_NULL);};
  number("fps",engineStats.fps);number("cpuPercent",engineStats.cpuPercent);number("ramBytes",engineStats.ramBytes);number("vramBytes",engineStats.vramBytes);
  JS_SetPropertyStr(ctx,value,"gpuAllocatedEstimateBytes",engineStats.sampleId?JS_NewFloat64(ctx,double(engineStats.gpuAllocatedEstimateBytes)):JS_NULL);
  JS_SetPropertyStr(ctx,value,"sampleSeconds",JS_NewFloat64(ctx,engineStats.sampleSeconds));JS_SetPropertyStr(ctx,value,"sampleId",JS_NewFloat64(ctx,double(engineStats.sampleId)));
  JS_SetPropertyStr(ctx,value,"objectCount",JS_NewInt32(ctx,int(currentScene->objects.size())));JS_SetPropertyStr(ctx,value,"panelCount",JS_NewInt32(ctx,int(currentScene->panels.size())));
  const auto &shadow=spotShadowDiagnostics;JSValue shadowState=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,shadowState,"active",JS_NewBool(ctx,shadow.active));JS_SetPropertyStr(ctx,shadowState,"lightId",shadow.lightId?JS_NewInt32(ctx,shadow.lightId):JS_NULL);JS_SetPropertyStr(ctx,shadowState,"status",JS_NewString(ctx,shadow.status));JS_SetPropertyStr(ctx,shadowState,"resolution",JS_NewInt32(ctx,shadow.resolution));JS_SetPropertyStr(ctx,shadowState,"triangles",JS_NewFloat64(ctx,double(shadow.triangles)));JS_SetPropertyStr(ctx,shadowState,"gpuBytes",JS_NewFloat64(ctx,double(shadow.bytes)));JS_SetPropertyStr(ctx,shadowState,"gpuMs",shadow.gpuMs?JS_NewFloat64(ctx,*shadow.gpuMs):JS_NULL);JS_SetPropertyStr(ctx,value,"shadow",shadowState);
  const auto perf=currentScene->physics?currentScene->physics->performance():physics::Performance{};
  JS_SetPropertyStr(ctx,value,"physicsAdvanceMs",JS_NewFloat64(ctx,perf.advanceMs));
  JS_SetPropertyStr(ctx,value,"physicsSolverMs",JS_NewFloat64(ctx,perf.solverMs));
  JS_SetPropertyStr(ctx,value,"physicsSteps",JS_NewUint32(ctx,perf.steps));
  JS_SetPropertyStr(ctx,value,"physicsActiveBodies",JS_NewUint32(ctx,perf.activeBodies));
  JS_SetPropertyStr(ctx,value,"physicsWorkers",JS_NewUint32(ctx,perf.workers));
  return value;
}
static JSValue jsLog(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc){const char *s=JS_ToCString(ctx,argv[0]);if(s){
    if(auto *out=evaluation::capture){
      if(out->size()<evaluation::maxOutputBytes)out->append(s).append("\n");
      if(out->size()>evaluation::maxOutputBytes)out->resize(evaluation::maxOutputBytes);
    }else fprintf(stdout,"[js] %s\n",s);
    JS_FreeCString(ctx,s);}}
  return JS_UNDEFINED;
}
static JSValue jsGetController(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getController(hand): hand is left or right");
  const char *hand=JS_ToCString(ctx,argv[0]);if(!hand)return JS_EXCEPTION;
  int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;JS_FreeCString(ctx,hand);
  if(index<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &c=controllers[index];
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,c.valid));
  JS_SetPropertyStr(ctx,object,"connected",JS_NewBool(ctx,c.connected));
  JS_SetPropertyStr(ctx,object,"x",JS_NewFloat64(ctx,c.x));
  JS_SetPropertyStr(ctx,object,"y",JS_NewFloat64(ctx,c.y));
  JS_SetPropertyStr(ctx,object,"z",JS_NewFloat64(ctx,c.z));
  JSValue rotation=JS_NewArray(ctx);
  for(int i=0;i<9;i++)JS_SetPropertyUint32(ctx,rotation,i,JS_NewFloat64(ctx,c.rotation[i]));
  JS_SetPropertyStr(ctx,object,"rotation",rotation);
  return object;
}
static int handIndex(JSContext *ctx,JSValueConst value){
  const char *hand=JS_ToCString(ctx,value);if(!hand)return -1;
  int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;
  JS_FreeCString(ctx,hand);return index;
}
static JSValue trackingVector(JSContext *ctx,const float *values,int count){
  JSValue value=JS_NewObject(ctx);const char *axes[]={"x","y","z"};
  for(int i=0;i<count;i++)JS_SetPropertyStr(ctx,value,axes[i],JS_NewFloat64(ctx,values[i]));
  return value;
}
static JSValue trackingAge(JSContext *ctx,tracking::Clock::time_point time){
  const double age=tracking::ageMs(time);return age<0?JS_NULL:JS_NewFloat64(ctx,age);
}
bool parseCalibration(JSContext *ctx,JSValueConst value,tracking::EyeCalibration &result){
  auto stringIs=[&](const char *key,const char *expected){JSValue v=JS_GetPropertyStr(ctx,value,key);const char *s=JS_ToCString(ctx,v);bool ok=s&&std::string(s)==expected;if(s)JS_FreeCString(ctx,s);JS_FreeValue(ctx,v);return ok;};
  JSValue version=JS_GetPropertyStr(ctx,value,"version");double n=0;bool ok=JS_IsNumber(version)&&JS_ToFloat64(ctx,&n,version)==0&&n==1;JS_FreeValue(ctx,version);
  ok=ok&&stringIs("source","openvr")&&stringIs("model","head-angular-affine")&&stringIs("units","radians");
  auto array=[&](const char *key,double *out,int count){JSValue a=JS_GetPropertyStr(ctx,value,key),length=JS_GetPropertyStr(ctx,a,"length");int32_t size=0;bool valid=JS_IsArray(a)&&JS_ToInt32(ctx,&size,length)==0&&size==count;JS_FreeValue(ctx,length);for(int i=0;i<count&&valid;i++){JSValue v=JS_GetPropertyUint32(ctx,a,i);valid=JS_IsNumber(v)&&JS_ToFloat64(ctx,&out[i],v)==0&&std::isfinite(out[i]);JS_FreeValue(ctx,v);}JS_FreeValue(ctx,a);return valid;};
  ok=ok&&array("yaw",result.yaw,3)&&array("pitch",result.pitch,3)&&array("bounds",result.bounds,4);
  for(double v:result.yaw)ok=ok&&std::abs(v)<=10;
  for(double v:result.pitch)ok=ok&&std::abs(v)<=10;
  ok=ok&&result.bounds[0]<result.bounds[1]&&result.bounds[2]<result.bounds[3];
  for(double v:result.bounds)ok=ok&&std::abs(v)<=1.5;
  result.loaded=ok;return ok;
}
static JSValue jsGetEyeCalibration(JSContext *ctx,JSValueConst,int,JSValueConst *){
  JSValue info=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,info,"loaded",JS_NewBool(ctx,eyeCalibration.loaded));
  JS_SetPropertyStr(ctx,info,"outputPath",calibrationOutput.empty()?JS_NULL:JS_NewString(ctx,calibrationOutput.c_str()));
  return info;
}
static JSValue jsSaveEyeCalibration(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(calibrationOutput.empty())return JS_ThrowTypeError(ctx,"start with --eye-calibration-output FILE to save calibration");
  tracking::EyeCalibration candidate;
  if(argc!=1||!parseCalibration(ctx,argv[0],candidate))return JS_ThrowTypeError(ctx,"invalid eye calibration format");
  JSValue json=JS_JSONStringify(ctx,argv[0],JS_UNDEFINED,JS_UNDEFINED);if(JS_IsException(json))return json;const char *data=JS_ToCString(ctx,json);
  if(!data){JS_FreeValue(ctx,json);return JS_EXCEPTION;}
  // Restrict writes to the startup-selected path; rename only a fully flushed file.
  const std::string temp=calibrationOutput+".tmp."+std::to_string(getpid());
  std::ofstream file(temp,std::ios::binary|std::ios::trunc);file<<data<<"\n";file.flush();bool ok=file.good();file.close();ok=ok&&!file.fail();
  JS_FreeCString(ctx,data);JS_FreeValue(ctx,json);
  std::error_code error;if(ok)std::filesystem::rename(temp,calibrationOutput,error);
  if(!ok||error){std::filesystem::remove(temp,error);return JS_ThrowTypeError(ctx,"cannot save eye calibration file");}
  eyeCalibration=candidate;return JS_NewString(ctx,calibrationOutput.c_str());
}
static JSValue jsGetEyeTracking(JSContext *ctx,JSValueConst,int,JSValueConst *){
  currentScene->eyeTrackingRequested=true;
  const auto &eye=controllerInput.eye();
  const auto ray=trackingAnchorValid?tracking::sceneRay(eye.origin,eye.target,trackingAnchor.m):tracking::Ray{};
  const bool fresh=tracking::fresh(eye.polledAt);
  const bool valid=eye.active&&eye.valid&&fresh&&ray.valid&&!eye.suppressed;
  const char *status=eye.status;
  if(eye.active&&eye.valid){if(!fresh)status="stale";else if(!trackingAnchorValid)status="anchorUnavailable";else if(!ray.valid)status="invalidGeometry";}
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"source",JS_NewString(ctx,"openvr"));
  JS_SetPropertyStr(ctx,object,"space",JS_NewString(ctx,"scene"));
  JS_SetPropertyStr(ctx,object,"configured",JS_NewBool(ctx,eye.configured));
  JS_SetPropertyStr(ctx,object,"available",JS_NewBool(ctx,eye.active));
  JS_SetPropertyStr(ctx,object,"active",JS_NewBool(ctx,eye.active));
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,valid));
  JS_SetPropertyStr(ctx,object,"reportedValid",JS_NewBool(ctx,eye.valid));
  JS_SetPropertyStr(ctx,object,"tracked",JS_NewBool(ctx,eye.tracked));
  JS_SetPropertyStr(ctx,object,"fresh",JS_NewBool(ctx,fresh));
  JS_SetPropertyStr(ctx,object,"status",JS_NewString(ctx,status));
  JS_SetPropertyStr(ctx,object,"suppressed",JS_NewBool(ctx,eye.suppressed));
  JS_SetPropertyStr(ctx,object,"sceneAppPid",JS_NewUint32(ctx,eye.scenePid));
  JS_SetPropertyStr(ctx,object,"error",eye.error||eye.polledAt!=tracking::Clock::time_point{}?JS_NewInt32(ctx,eye.error):JS_NULL);
  JS_SetPropertyStr(ctx,object,"pollAgeMs",trackingAge(ctx,eye.polledAt));
  JS_SetPropertyStr(ctx,object,"sampleAgeMs",JS_NULL);
  JS_SetPropertyStr(ctx,object,"origin",valid?trackingVector(ctx,ray.origin,3):JS_NULL);
  JS_SetPropertyStr(ctx,object,"target",valid?trackingVector(ctx,ray.target,3):JS_NULL);
  JS_SetPropertyStr(ctx,object,"direction",valid?trackingVector(ctx,ray.direction,3):JS_NULL);
  JSValue basis=JS_NULL;
  if(valid&&headBasisValid){basis=JS_NewArray(ctx);for(int r=0;r<3;r++)for(int c=0;c<3;c++)JS_SetPropertyUint32(ctx,basis,r*3+c,JS_NewFloat64(ctx,headBasis[r][c]));}
  JS_SetPropertyStr(ctx,object,"headBasis",basis);
  return object;
}
static JSValue jsGetCalibratedEyeTracking(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  JSValue object=jsGetEyeTracking(ctx,self,argc,argv),validValue=JS_GetPropertyStr(ctx,object,"valid");
  bool valid=JS_ToBool(ctx,validValue);JS_FreeValue(ctx,validValue);
  const auto &eye=controllerInput.eye();auto ray=tracking::sceneRay(eye.origin,eye.target,trackingAnchor.m);
  float direction[3]{};bool applied=valid&&headBasisValid&&eyeCalibration.correct(ray.direction,headBasis,direction);
  if(applied){float length=0;for(int i=0;i<3;i++)length+=(ray.target[i]-ray.origin[i])*(ray.target[i]-ray.origin[i]);length=std::sqrt(length);for(int i=0;i<3;i++)ray.target[i]=ray.origin[i]+direction[i]*length;
    JS_SetPropertyStr(ctx,object,"direction",trackingVector(ctx,direction,3));JS_SetPropertyStr(ctx,object,"target",trackingVector(ctx,ray.target,3));}
  JS_SetPropertyStr(ctx,object,"calibrationLoaded",JS_NewBool(ctx,eyeCalibration.loaded));
  JS_SetPropertyStr(ctx,object,"calibrationApplied",JS_NewBool(ctx,applied));
  JS_SetPropertyStr(ctx,object,"calibrationStatus",JS_NewString(ctx,!eyeCalibration.loaded?"notLoaded":!valid?"gazeUnavailable":!headBasisValid?"headUnavailable":applied?"applied":"outOfRange"));
  return object;
}
static JSValue jsGetEyeFoveationCenters(JSContext *ctx,JSValueConst,int,JSValueConst *){
  currentScene->foveationRequested=true;
  const auto &state=controllerInput.foveation();
  const bool valid=state.valid&&tracking::fresh(state.polledAt);
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"source",JS_NewString(ctx,"openvr"));
  JS_SetPropertyStr(ctx,object,"space",JS_NewString(ctx,"ndc"));
  JS_SetPropertyStr(ctx,object,"projection",JS_NewString(ctx,"IVRSystem.GetProjectionMatrix"));
  JS_SetPropertyStr(ctx,object,"available",JS_NewBool(ctx,state.available));
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,valid));
  JS_SetPropertyStr(ctx,object,"pollAgeMs",trackingAge(ctx,state.polledAt));
  JS_SetPropertyStr(ctx,object,"sampleAgeMs",JS_NULL);
  JS_SetPropertyStr(ctx,object,"left",valid?trackingVector(ctx,state.left,2):JS_NULL);
  JS_SetPropertyStr(ctx,object,"right",valid?trackingVector(ctx,state.right,2):JS_NULL);
  return object;
}
static JSValue jsGetFingerTracking(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getFingerTracking(hand): hand is left or right");
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &state=frameInput.hand(h);
  const bool connected=frameInput.started();
  const bool fresh=connected&&state.valid&&tracking::fresh(state.updatedAt);
  JSValue object=JS_NewObject(ctx),sensors=JS_NewObject(ctx),touches=JS_NewObject(ctx);
  bool available=false;
  auto field=[&](const std::string &path,bool boolean){
    const auto it=state.raw.find(path);
    const bool present=it!=state.raw.end()&&it->second.isBoolean==boolean&&(boolean||std::isfinite(it->second.value));
    JSValue result=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,result,"available",JS_NewBool(ctx,present));
    JS_SetPropertyStr(ctx,result,"value",present&&fresh?(boolean?JS_NewBool(ctx,it->second.boolean):JS_NewFloat64(ctx,it->second.value)):JS_NULL);
    JS_SetPropertyStr(ctx,result,"updateAgeMs",present?trackingAge(ctx,it->second.updatedAt):JS_NULL);
    return result;
  };
  for(const char *name:{"ring","pinky","grip","trigger","bumper","thumbrest","thumbstick","a","b","x","y","dpad","system","menu"}){
    const std::string path=std::string("/input/capsense/")+name;
    const auto it=state.raw.find(path);available|=it!=state.raw.end()&&!it->second.isBoolean&&std::isfinite(it->second.value);
    JS_SetPropertyStr(ctx,sensors,name,field(path,false));
  }
  for(const char *name:{"handgrip1","handgrip2","thumbrest","grip","trigger","bumper","thumbstick","a","b","x","y","dpad_up","dpad_down","dpad_left","dpad_right","view","menu","system"})
    JS_SetPropertyStr(ctx,touches,name,field(std::string("/input/")+name+"/touch",true));
  JS_SetPropertyStr(ctx,object,"source",JS_NewString(ctx,"vrserver"));
  JS_SetPropertyStr(ctx,object,"hand",JS_NewString(ctx,h?"right":"left"));
  JS_SetPropertyStr(ctx,object,"connected",JS_NewBool(ctx,connected));
  JS_SetPropertyStr(ctx,object,"available",JS_NewBool(ctx,available));
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,fresh&&available));
  JS_SetPropertyStr(ctx,object,"fresh",JS_NewBool(ctx,fresh));
  JS_SetPropertyStr(ctx,object,"ageMs",trackingAge(ctx,state.updatedAt));
  JS_SetPropertyStr(ctx,object,"staleAfterMs",JS_NewFloat64(ctx,tracking::kFreshMs));
  JS_SetPropertyStr(ctx,object,"sensors",sensors);JS_SetPropertyStr(ctx,object,"touches",touches);
  JS_SetPropertyStr(ctx,object,"skeleton",JS_NULL);
  return object;
}
static JSValue jsGetControllerInput(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getControllerInput(hand): hand is left or right");
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &state=frameInput.hand(h);JSValue object=JS_NewObject(ctx);JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));JS_SetPropertyStr(ctx,object,"hand",JS_NewString(ctx,h?"right":"left"));
  JSValue raw=JS_NewObject(ctx);for(const auto &[name,value]:state.raw){JS_SetPropertyStr(ctx,raw,name.c_str(),value.isBoolean?JS_NewBool(ctx,value.boolean):JS_NewFloat64(ctx,value.value));}JS_SetPropertyStr(ctx,object,"raw",raw);
  auto rawValue=[&](const char *name){auto it=state.raw.find(name);return it==state.raw.end()?0.f:it->second.isBoolean?(it->second.boolean?1.f:0.f):it->second.value;};
  JSValue buttons=JS_NewObject(ctx);const char *controls[]={"trigger","grip","bumper","thumbstick","a","b","x","y","menu","system","view","dpad_up","dpad_down","dpad_left","dpad_right"};for(const char *name:controls){JSValue item=JS_NewObject(ctx);std::string base=std::string("/input/")+name;JS_SetPropertyStr(ctx,item,"down",JS_NewBool(ctx,rawValue((base+"/click").c_str())!=0));JS_SetPropertyStr(ctx,item,"touched",JS_NewBool(ctx,rawValue((base+"/touch").c_str())!=0));JS_SetPropertyStr(ctx,buttons,name, item);}JS_SetPropertyStr(ctx,object,"buttons",buttons);
  JSValue analog=JS_NewObject(ctx);JS_SetPropertyStr(ctx,analog,"trigger",JS_NewFloat64(ctx,rawValue("/input/trigger/value")));JS_SetPropertyStr(ctx,analog,"grip",JS_NewFloat64(ctx,rawValue("/input/grip/value")));JSValue stick=JS_NewObject(ctx);JS_SetPropertyStr(ctx,stick,"x",JS_NewFloat64(ctx,rawValue("/input/thumbstick/x")));JS_SetPropertyStr(ctx,stick,"y",JS_NewFloat64(ctx,rawValue("/input/thumbstick/y")));JS_SetPropertyStr(ctx,analog,"thumbstick",stick);JS_SetPropertyStr(ctx,object,"analog",analog);
  JSValue capacitive=JS_NewObject(ctx);for(const char *name:controls){std::string path=std::string("/input/capsense/")+name;JS_SetPropertyStr(ctx,capacitive,name,JS_NewFloat64(ctx,rawValue(path.c_str())));}for(const char *name:{"ring","pinky","thumbrest","dpad"}){std::string path=std::string("/input/capsense/")+name;JS_SetPropertyStr(ctx,capacitive,name,JS_NewFloat64(ctx,rawValue(path.c_str())));}JS_SetPropertyStr(ctx,object,"capacitive",capacitive);return object;
}
static bool containerId(JSContext *ctx,JSValueConst value,int &id,bool issued=false){
  double n=0;
  if(!currentScene||!JS_IsNumber(value)||JS_ToFloat64(ctx,&n,value)<0||!std::isfinite(n)||n<0||n>=std::numeric_limits<int>::max()||n!=std::floor(n)){JS_ThrowRangeError(ctx,"container must be an integer scene container ID");return false;}
  id=int(n);
  if(id&&(issued?id>=currentScene->nextContainerId:!currentScene->containers.count(id))){JS_ThrowRangeError(ctx,"unknown or disposed scene container");return false;}
  return true;
}
static bool containerOption(JSContext *ctx,JSValueConst options,int &id){
  JSValue value=JS_GetPropertyStr(ctx,options,"container");if(JS_IsException(value))return false;
  const bool valid=JS_IsUndefined(value)||containerId(ctx,value,id);JS_FreeValue(ctx,value);return valid;
}
static JSValue jsOnInputEvent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1||!JS_IsFunction(ctx,argv[0]))return JS_ThrowTypeError(ctx,"onInputEvent(callback, options)");
  int owner=0;
  if(argc>1&&!JS_IsUndefined(argv[1])){if(!JS_IsObject(argv[1])||JS_IsNull(argv[1]))return JS_ThrowTypeError(ctx,"callback options must be an object");if(!containerOption(ctx,argv[1],owner))return JS_EXCEPTION;}
  if(inputCallbackCtx&&inputCallbackCtx!=ctx)clearInputCallback(inputCallbackCtx);else clearInputCallback(ctx);
  inputCallbackCtx=ctx;inputCallback=JS_DupValue(ctx,argv[0]);inputCallbackContainer=owner;return JS_UNDEFINED;
}
std::string dispatchInputCallbacks(){
  if(inputCallbackCtx){auto events=sceneInput().takeCallbackEvents();for(const auto &event:events){
      if(!inputCallbackCtx)break;
      JSContext *ctx=inputCallbackCtx;JSValue callback=JS_DupValue(ctx,inputCallback),value=inputEventValue(ctx,event);
      JSValue result=JS_Call(ctx,callback,JS_UNDEFINED,1,&value);JS_FreeValue(ctx,value);JS_FreeValue(ctx,callback);
      if(JS_IsException(result)){
        JSValue exception=JS_GetException(ctx);
        const char *message=JS_ToCString(ctx,exception);
        std::string error=message?message:"input callback failed";
        if(message)JS_FreeCString(ctx,message);
        JS_FreeValue(ctx,exception);JS_FreeValue(ctx,result);
        return error;
      }
      JS_FreeValue(ctx,result);
    }}
  return {};
}
static JSValue jsPollInputEvents(JSContext *ctx,JSValueConst,int,JSValueConst *){
  JSValue array=JS_NewArray(ctx);if(currentScene!=liveScene)return array;
  auto events=sceneInput().takeEvents();uint32_t i=0;
  for(const auto &event:events)JS_SetPropertyUint32(ctx,array,i++,inputEventValue(ctx,event));
  return array;
}
static JSValue jsGetInput(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getInput(hand): hand is left or right");
  inputPollingRequested=true;
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &state=controllerInput.hand(h);
  auto value=[&](controller_input::Control c)->const controller_input::Value &{return state.values[controller_input::index(c)];};
  JSValue object=JS_NewObject(ctx);JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));
  auto digital=[&](const char *name,controller_input::Control c){JS_SetPropertyStr(ctx,object,name,JS_NewBool(ctx,value(c).active&&value(c).down));};
  auto analog=[&](const char *name,controller_input::Control c){JS_SetPropertyStr(ctx,object,name,JS_NewFloat64(ctx,value(c).active?value(c).x:0));};
  digital("gripClick",controller_input::Control::GripClick);digital("gripTouch",controller_input::Control::GripTouch);analog("gripValue",controller_input::Control::GripValue);
  digital("triggerClick",controller_input::Control::TriggerClick);digital("triggerTouch",controller_input::Control::TriggerTouch);analog("triggerValue",controller_input::Control::TriggerValue);
  digital("thumbstickClick",controller_input::Control::ThumbstickClick);digital("thumbstickTouch",controller_input::Control::ThumbstickTouch);
  JSValue stick=JS_NewObject(ctx);const auto &axis=value(controller_input::Control::Thumbstick);
  JS_SetPropertyStr(ctx,stick,"x",JS_NewFloat64(ctx,axis.active?axis.x:0));JS_SetPropertyStr(ctx,stick,"y",JS_NewFloat64(ctx,axis.active?axis.y:0));
  JS_SetPropertyStr(ctx,object,"thumbstick",stick);
  digital("bumperClick",controller_input::Control::BumperClick);digital("bumperTouch",controller_input::Control::BumperTouch);
  digital("systemClick",controller_input::Control::SystemClick);digital("systemTouch",controller_input::Control::SystemTouch);
  digital("menuClick",controller_input::Control::MenuClick);digital("viewClick",controller_input::Control::ViewClick);
  digital("aClick",controller_input::Control::AClick);digital("bClick",controller_input::Control::BClick);
  digital("xClick",controller_input::Control::XClick);digital("yClick",controller_input::Control::YClick);
  digital("dpadUpClick",controller_input::Control::DpadUpClick);digital("dpadDownClick",controller_input::Control::DpadDownClick);
  digital("dpadLeftClick",controller_input::Control::DpadLeftClick);digital("dpadRightClick",controller_input::Control::DpadRightClick);
  return object;
}
static JSValue jsPulse(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"pulse(hand, seconds=0.05, amplitude=0.5)");
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  double seconds=.05,amplitude=.5;if(argc>1&&JS_ToFloat64(ctx,&seconds,argv[1])<0)return JS_EXCEPTION;
  if(argc>2&&JS_ToFloat64(ctx,&amplitude,argv[2])<0)return JS_EXCEPTION;
  if(!std::isfinite(seconds)||!std::isfinite(amplitude)||seconds<0||seconds>1||amplitude<0||amplitude>1)return JS_ThrowRangeError(ctx,"invalid pulse duration or amplitude");
  return JS_NewBool(ctx,controllerInput.pulse(h,(float)seconds,(float)amplitude));
}
static int handArgument(JSContext *ctx,JSValueConst value){
  const char *hand=JS_ToCString(ctx,value);if(!hand)return -2;
  const int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;JS_FreeCString(ctx,hand);
  if(index<0)JS_ThrowRangeError(ctx,"hand must be left or right");
  return index;
}
static JSValue jsSetLaserVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<2)return JS_ThrowTypeError(ctx,"setLaserVisible(hand, visible)");
  const int index=handArgument(ctx,argv[0]);if(index<0)return JS_EXCEPTION;
  laserVisible[index]=JS_ToBool(ctx,argv[1])>0;
  return JS_UNDEFINED;
}
static JSValue jsGetLaserVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getLaserVisible(hand)");
  const int index=handArgument(ctx,argv[0]);if(index<0)return JS_EXCEPTION;
  return JS_NewBool(ctx,laserVisible[index]);
}
static JSValue jsGetMeshHover(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getMeshHover(hand)");
  const int index=handArgument(ctx,argv[0]);if(index<0)return JS_EXCEPTION;
  // Candidate scripts are not live yet, so they never report a hover.
  const int id=currentScene==liveScene?meshHoveredObject[index]:0;
  JSValue result=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,result,"hit",JS_NewBool(ctx,id!=0));
  JS_SetPropertyStr(ctx,result,"objectId",JS_NewInt32(ctx,id));
  JS_SetPropertyStr(ctx,result,"distance",JS_NewFloat64(ctx,id?meshHitDistance[index]:0));
  JSValue point=JS_NewObject(ctx);for(int i=0;i<3;i++)JS_SetPropertyStr(ctx,point,i==0?"x":i==1?"y":"z",JS_NewFloat64(ctx,id?meshHitPoint[index][i]:0));
  JS_SetPropertyStr(ctx,result,"point",point);
  return result;
}
static JSValue jsPanelClear(JSContext *,JSValueConst,int,JSValueConst*);static JSValue jsPanelText(JSContext *,JSValueConst,int,JSValueConst*);static JSValue jsPanelPresent(JSContext *,JSValueConst,int,JSValueConst*);
Panel *findPanel(int id){return currentScene?findSceneItem(currentScene->panels,id):nullptr;}
static void panelHandleProperty(JSContext *ctx,JSValueConst object,const char *key,JSValue value){JS_DefinePropertyValueStr(ctx,object,key,value,JS_PROP_C_W_E);}
static JSValue panelObject(JSContext *ctx,int id){JSValue object=JS_NewObject(ctx);panelHandleProperty(ctx,object,"id",JS_NewInt32(ctx,id));return object;}
static JSValue jsPanelMethodClear(JSContext *ctx,JSValueConst thisVal,int argc,JSValueConst *argv){int32_t id=0;JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JS_ToInt32(ctx,&id,idv);JS_FreeValue(ctx,idv);JSValue args[5]={JS_NewInt32(ctx,id),argc>0?argv[0]:JS_NewInt32(ctx,0),argc>1?argv[1]:JS_NewInt32(ctx,0),argc>2?argv[2]:JS_NewInt32(ctx,0),argc>3?argv[3]:JS_NewInt32(ctx,255)};auto result=jsPanelClear(ctx,JS_UNDEFINED,5,args);for(int i=0;i<5;i++)if(i==0||i>argc)JS_FreeValue(ctx,args[i]);return result;}
static JSValue jsPanelMethodText(JSContext *ctx,JSValueConst thisVal,int argc,JSValueConst *argv){int32_t id=0;JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JS_ToInt32(ctx,&id,idv);JS_FreeValue(ctx,idv);if(argc<3)return JS_ThrowTypeError(ctx,"panel.canvas.text(x,y,text,...)");JSValue args[9]={JS_NewInt32(ctx,id),argv[0],argv[1],argv[2],argc>3?argv[3]:JS_NewInt32(ctx,255),argc>4?argv[4]:JS_NewInt32(ctx,255),argc>5?argv[5]:JS_NewInt32(ctx,255),argc>6?argv[6]:JS_NewInt32(ctx,255),argc>7?argv[7]:JS_NewInt32(ctx,32)};auto result=jsPanelText(ctx,JS_UNDEFINED,9,args);for(int i=4;i<9;i++)if(i>argc)JS_FreeValue(ctx,args[i]);JS_FreeValue(ctx,args[0]);return result;}
static int colorPart(JSContext *,JSValueConst,int);
static JSValue jsPanelMethodRect(JSContext *ctx,JSValueConst thisVal,int argc,JSValueConst *argv){int32_t id=0;JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JS_ToInt32(ctx,&id,idv);JS_FreeValue(ctx,idv);if(argc<8)return JS_ThrowTypeError(ctx,"panel.canvas.rect(x,y,w,h,r,g,b,a,fill)");JSValue args[9]={JS_NewInt32(ctx,id),argv[0],argv[1],argv[2],argv[3],argv[4],argv[5],argv[6],argv[7]};auto*p=findPanel(id);if(p){int x=0,y=0,w=0,h=0;JS_ToInt32(ctx,&x,args[1]);JS_ToInt32(ctx,&y,args[2]);JS_ToInt32(ctx,&w,args[3]);JS_ToInt32(ctx,&h,args[4]);panelRect(*p,x,y,w,h,colorPart(ctx,args[5],255),colorPart(ctx,args[6],255),colorPart(ctx,args[7],255),colorPart(ctx,args[8],255),argc>8&&JS_ToBool(ctx,argv[8])>0);}JS_FreeValue(ctx,args[0]);return JS_UNDEFINED;}
static JSValue jsPanelMethodLine(JSContext *ctx,JSValueConst thisVal,int argc,JSValueConst *argv){int32_t id=0;JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JS_ToInt32(ctx,&id,idv);JS_FreeValue(ctx,idv);if(argc<8)return JS_ThrowTypeError(ctx,"panel.canvas.line(x1,y1,x2,y2,r,g,b,a)");auto*p=findPanel(id);if(p){int x1=0,y1=0,x2=0,y2=0;JS_ToInt32(ctx,&x1,argv[0]);JS_ToInt32(ctx,&y1,argv[1]);JS_ToInt32(ctx,&x2,argv[2]);JS_ToInt32(ctx,&y2,argv[3]);panelLine(*p,x1,y1,x2,y2,colorPart(ctx,argv[4],255),colorPart(ctx,argv[5],255),colorPart(ctx,argv[6],255),colorPart(ctx,argv[7],255));}return JS_UNDEFINED;}
static JSValue jsPanelMethodCircle(JSContext *ctx,JSValueConst thisVal,int argc,JSValueConst *argv){int32_t id=0;JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JS_ToInt32(ctx,&id,idv);JS_FreeValue(ctx,idv);if(argc<7)return JS_ThrowTypeError(ctx,"panel.canvas.circle(cx,cy,radius,r,g,b,a,fill)");auto*p=findPanel(id);if(p){int x=0,y=0,radius=0;JS_ToInt32(ctx,&x,argv[0]);JS_ToInt32(ctx,&y,argv[1]);JS_ToInt32(ctx,&radius,argv[2]);panelCircle(*p,x,y,radius,colorPart(ctx,argv[3],255),colorPart(ctx,argv[4],255),colorPart(ctx,argv[5],255),colorPart(ctx,argv[6],255),argc>7&&JS_ToBool(ctx,argv[7])>0);}return JS_UNDEFINED;}
static JSValue jsPanelMethodImage(JSContext *ctx,JSValueConst thisVal,int argc,JSValueConst *argv){int32_t id=0;JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JS_ToInt32(ctx,&id,idv);JS_FreeValue(ctx,idv);if(argc<3)return JS_ThrowTypeError(ctx,"panel.canvas.image(x,y,path)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*path=JS_ToCString(ctx,argv[2]);if(!path)return JS_EXCEPTION;int x=0,y=0;if(JS_ToInt32(ctx,&x,argv[0])<0||JS_ToInt32(ctx,&y,argv[1])<0){JS_FreeCString(ctx,path);return JS_EXCEPTION;}bool ok=panelImage(*p,x,y,path);JS_FreeCString(ctx,path);return JS_NewBool(ctx,ok);}
static JSValue jsPanelMethodPresent(JSContext *ctx,JSValueConst thisVal,int,JSValueConst*){JSValue idv=JS_GetPropertyStr(ctx,thisVal,"id");JSValue args[1]={idv};auto result=jsPanelPresent(ctx,JS_UNDEFINED,1,args);JS_FreeValue(ctx,idv);return result;}
static int panelArgumentId(JSContext *ctx,JSValueConst value){int id=0;if(JS_IsObject(value)){JSValue field=JS_GetPropertyStr(ctx,value,"id");JS_ToInt32(ctx,&id,field);JS_FreeValue(ctx,field);}else JS_ToInt32(ctx,&id,value);return id;}
static int colorPart(JSContext *ctx,JSValueConst value,int fallback){if(JS_IsNumber(value)){int n=0;JS_ToInt32(ctx,&n,value);return std::clamp(n,0,255);}return fallback;}
static JSValue panelHandle(JSContext *ctx,int id){
  JSValue object=panelObject(ctx,id);JSValue canvas=JS_NewObject(ctx);panelHandleProperty(ctx,canvas,"id",JS_NewInt32(ctx,id));panelHandleProperty(ctx,canvas,"clear",JS_NewCFunction(ctx,jsPanelMethodClear,"clear",5));panelHandleProperty(ctx,canvas,"text",JS_NewCFunction(ctx,jsPanelMethodText,"text",8));panelHandleProperty(ctx,canvas,"rect",JS_NewCFunction(ctx,jsPanelMethodRect,"rect",9));panelHandleProperty(ctx,canvas,"line",JS_NewCFunction(ctx,jsPanelMethodLine,"line",8));panelHandleProperty(ctx,canvas,"circle",JS_NewCFunction(ctx,jsPanelMethodCircle,"circle",8));panelHandleProperty(ctx,canvas,"image",JS_NewCFunction(ctx,jsPanelMethodImage,"image",3));panelHandleProperty(ctx,canvas,"present",JS_NewCFunction(ctx,jsPanelMethodPresent,"present",0));panelHandleProperty(ctx,object,"canvas",canvas);panelHandleProperty(ctx,object,"present",JS_NewCFunction(ctx,jsPanelMethodPresent,"present",0));return object;}
static JSValue jsCreatePanel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<1||!JS_IsObject(argv[0])||JS_IsNull(argv[0]))return JS_ThrowTypeError(ctx,"createPanel(options) requires an object");
  int owner=0,w=1024,h=512;double wm=1.2;if(!containerOption(ctx,argv[0],owner))return JS_EXCEPTION;
  for(auto [name,target]:{std::pair{"widthPixels",&w},std::pair{"heightPixels",&h}}){JSValue value=JS_GetPropertyStr(ctx,argv[0],name);if(JS_IsException(value))return value;const bool ok=JS_IsUndefined(value)||JS_ToInt32(ctx,target,value)==0;JS_FreeValue(ctx,value);if(!ok)return JS_EXCEPTION;}
  JSValue value=JS_GetPropertyStr(ctx,argv[0],"widthMeters");if(JS_IsException(value))return value;const bool ok=JS_IsUndefined(value)||JS_ToFloat64(ctx,&wm,value)==0;JS_FreeValue(ctx,value);if(!ok)return JS_EXCEPTION;
  if(w<1||h<1||w>4096||h>4096||!std::isfinite(wm)||wm<=0||wm>10)return JS_ThrowRangeError(ctx,"invalid panel dimensions");
  JSValue interactive=JS_GetPropertyStr(ctx,argv[0],"interactive");if(JS_IsException(interactive))return interactive;const bool enabled=JS_ToBool(ctx,interactive)>0;JS_FreeValue(ctx,interactive);
  if(owner&&!currentScene->containers.count(owner))return JS_ThrowRangeError(ctx,"scene container was disposed during panel option evaluation");
  Panel panel;
  if(!panelInit(panel,currentScene->nextPanelId++,w,h,(float)wm,(float)(wm*h/w),""))return JS_ThrowInternalError(ctx,"panel initialization failed");
  panel.container=owner;panel.interactive=enabled;
  currentScene->panels.push_back(std::move(panel));int id=currentScene->panels.back().id;
  return panelHandle(ctx,id);}
static JSValue jsPanelClear(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<1)return JS_ThrowTypeError(ctx,"panel.clear(id,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");int r=argc>1?colorPart(ctx,argv[1],0):0,g=argc>2?colorPart(ctx,argv[2],0):0,b=argc>3?colorPart(ctx,argv[3],0):0,a=argc>4?colorPart(ctx,argv[4],255):255;panelClear(*p,r,g,b,a);return JS_UNDEFINED;}
static JSValue jsPanelSetFont(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"panelLoadFont(panel,path)");id=panelArgumentId(ctx,argv[0]);auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*path=JS_ToCString(ctx,argv[1]);if(!path)return JS_EXCEPTION;p->fontPath=path;JS_FreeCString(ctx,path);return JS_UNDEFINED;}
static JSValue jsPanelText(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED),x=0,y=0;if(argc<4||JS_ToInt32(ctx,&x,argv[1])<0||JS_ToInt32(ctx,&y,argv[2])<0)return JS_ThrowTypeError(ctx,"panel.text(id,x,y,text,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*t=JS_ToCString(ctx,argv[3]);if(!t)return JS_EXCEPTION;int r=argc>4?colorPart(ctx,argv[4],255):255,g=argc>5?colorPart(ctx,argv[5],255):255,b=argc>6?colorPart(ctx,argv[6],255):255,a=argc>7?colorPart(ctx,argv[7],255):255;bool ok=panelText(*p,x,y,t,r,g,b,a,argc>8?colorPart(ctx,argv[8],32):32);JS_FreeCString(ctx,t);return JS_NewBool(ctx,ok);}
static JSValue jsPanelSetTransform(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<5)return JS_ThrowTypeError(ctx,"panel.setTransform(id,mode,x,y,z)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*mode=JS_ToCString(ctx,argv[1]);if(mode){p->mode=mode;JS_FreeCString(ctx,mode);}double x=0,y=0,z=0;if(JS_ToFloat64(ctx,&x,argv[2])<0||JS_ToFloat64(ctx,&y,argv[3])<0||JS_ToFloat64(ctx,&z,argv[4])<0)return JS_EXCEPTION;p->x=(float)x;p->y=(float)y;p->z=(float)z;return JS_UNDEFINED;}
static JSValue jsPanelSetRotation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<10)return JS_ThrowTypeError(ctx,"panel.setRotation(panel,pitch,yaw,roll,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");for(int i=0;i<9;i++){double value=0;if(JS_ToFloat64(ctx,&value,argv[i+1])<0)return JS_EXCEPTION;p->rotation[i]=(float)value;}return JS_UNDEFINED;}
static JSValue jsPanelRemove(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(argc<1)return JS_ThrowTypeError(ctx,"removePanel(panel)");int id=panelArgumentId(ctx,argv[0]);auto &panels=currentScene->panels;auto it=std::find_if(panels.begin(),panels.end(),[id](const Panel &p){return p.id==id;});if(it==panels.end())return JS_ThrowRangeError(ctx,"unknown panel");currentScene->retiredPanels.push_back(std::move(*it));panels.erase(it);return JS_UNDEFINED;}
static JSValue jsPanelVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"panelSetVisible(panel,visible)");id=panelArgumentId(ctx,argv[0]);auto*p=findPanel(id);if(p)p->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;}
static JSValue jsPanelPresent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<1)return JS_EXCEPTION;auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");p->dirty=true;return JS_UNDEFINED;}
Object *findObject(int id){
  return currentScene?findSceneItem(currentScene->objects,id):nullptr;
}
static JSValue animationState(JSContext *ctx,const Object &object){
  const auto &state=object.animation;const auto &asset=currentScene->assets[size_t(object.asset)-1];
  const auto *clip=state.clip>=0?&asset.animations[state.clip]:nullptr;
  JSValue value=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,value,"clip",clip?JS_NewInt32(ctx,state.clip):JS_NULL);
  JS_SetPropertyStr(ctx,value,"name",clip?JS_NewString(ctx,clip->name.c_str()):JS_NULL);
  JS_SetPropertyStr(ctx,value,"time",JS_NewFloat64(ctx,state.time));
  JS_SetPropertyStr(ctx,value,"duration",JS_NewFloat64(ctx,clip?clip->duration:0));
  JS_SetPropertyStr(ctx,value,"speed",JS_NewFloat64(ctx,state.speed));
  JS_SetPropertyStr(ctx,value,"loop",JS_NewBool(ctx,state.loop));
  JS_SetPropertyStr(ctx,value,"playing",JS_NewBool(ctx,state.playing));
  JS_SetPropertyStr(ctx,value,"finished",JS_NewBool(ctx,state.finished));
  JSValue transition=JS_NULL;
  if(state.transition.active()){
    const auto &blend=state.transition;transition=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,transition,"duration",JS_NewFloat64(ctx,blend.duration));
    JS_SetPropertyStr(ctx,transition,"elapsed",JS_NewFloat64(ctx,blend.elapsed));
    JS_SetPropertyStr(ctx,transition,"progress",JS_NewFloat64(ctx,blend.elapsed/blend.duration));
    JS_SetPropertyStr(ctx,transition,"sourceClip",blend.source.clip>=0?JS_NewInt32(ctx,blend.source.clip):JS_NULL);
  }
  JS_SetPropertyStr(ctx,value,"transition",transition);
  JS_SetPropertyStr(ctx,value,"status",JS_NewString(ctx,!clip?"stopped":state.playing?"playing":state.finished?"finished":"paused"));
  return value;
}
static Object *animationObject(JSContext *ctx,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0){JS_ThrowTypeError(ctx,"animation control requires an object id");return nullptr;}
  auto *object=findObject(id);if(!object)JS_ThrowRangeError(ctx,"unknown object id");return object;
}
static JSValue jsGetAnimations(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||!loadedAsset(*currentScene,id))return JS_ThrowRangeError(ctx,"getAnimations(assetId) requires a loaded asset");
  JSValue array=JS_NewArray(ctx);const auto &clips=currentScene->assets[size_t(id)-1].animations;
  for(size_t i=0;i<clips.size();i++){
    const auto &clip=clips[i];JSValue item=JS_NewObject(ctx);
    JS_SetPropertyStr(ctx,item,"index",JS_NewInt32(ctx,int(i)));JS_SetPropertyStr(ctx,item,"name",JS_NewString(ctx,clip.name.c_str()));
    JS_SetPropertyStr(ctx,item,"duration",JS_NewFloat64(ctx,clip.duration));JS_SetPropertyStr(ctx,item,"supported",JS_NewBool(ctx,clip.supported()));
    JS_SetPropertyStr(ctx,item,"unsupportedReason",clip.supported()?JS_NULL:JS_NewString(ctx,clip.unsupportedReason.c_str()));
    JS_SetPropertyUint32(ctx,array,uint32_t(i),item);
  }
  return array;
}
static JSValue jsGetAnimationState(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){auto *object=animationObject(ctx,argc,argv);return object?animationState(ctx,*object):JS_EXCEPTION;}
static JSValue jsPlayAnimation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  auto *object=animationObject(ctx,argc,argv);if(!object)return JS_EXCEPTION;
  const auto &asset=currentScene->assets[size_t(object->asset)-1];int index=0;
  if(argc>1&&!JS_IsUndefined(argv[1])){
    if(JS_IsString(argv[1])){
      const char *name=JS_ToCString(ctx,argv[1]);if(!name)return JS_EXCEPTION;index=-1;
      for(size_t i=0;i<asset.animations.size();i++)if(asset.animations[i].name==name){index=int(i);break;}JS_FreeCString(ctx,name);
    }else{
      double number=0;if(!JS_IsNumber(argv[1])||JS_ToFloat64(ctx,&number,argv[1])<0||!std::isfinite(number)||number<0||number>=double(asset.animations.size())||number!=std::floor(number))return JS_ThrowRangeError(ctx,"clip must be a valid name or integer index");index=int(number);
    }
  }
  if(index<0||size_t(index)>=asset.animations.size())return JS_ThrowRangeError(ctx,"unknown animation clip");
  const auto &clip=asset.animations[index];if(!clip.supported())return JS_ThrowTypeError(ctx,"unsupported animation: %s",clip.unsupportedReason.c_str());
  animation::Playback state;state.clip=index;state.playing=true;double blendDuration=0;
  if(argc>2&&!JS_IsUndefined(argv[2])){
    if(!JS_IsObject(argv[2])||JS_IsNull(argv[2]))return JS_ThrowTypeError(ctx,"animation options must be an object");
    JSValue loop=JS_GetPropertyStr(ctx,argv[2],"loop");if(JS_IsException(loop))return JS_EXCEPTION;
    if(!JS_IsUndefined(loop)&&!JS_IsBool(loop)){JS_FreeValue(ctx,loop);return JS_ThrowTypeError(ctx,"loop must be boolean");}
    if(!JS_IsUndefined(loop))state.loop=JS_ToBool(ctx,loop)>0;JS_FreeValue(ctx,loop);
    auto read=[&](const char *name,double &target){JSValue value=JS_GetPropertyStr(ctx,argv[2],name);if(JS_IsException(value))return false;bool ok=JS_IsUndefined(value)||(JS_IsNumber(value)&&JS_ToFloat64(ctx,&target,value)==0&&std::isfinite(target));JS_FreeValue(ctx,value);return ok;};
    if(!read("speed",state.speed)||!read("time",state.time)||!read("blendDuration",blendDuration))return JS_ThrowTypeError(ctx,"speed, time and blendDuration must be finite numbers");
  }
  if(state.speed<=0||state.speed>100||state.time<0||state.time>clip.duration)return JS_ThrowRangeError(ctx,"speed must be in (0,100] and time within the clip");
  if(blendDuration<0||blendDuration>60)return JS_ThrowRangeError(ctx,"blendDuration must be in [0,60] seconds");
  if(blendDuration>0){
    auto &blend=state.transition;blend.duration=blendDuration;
    if(object->animation.clip>=0&&!object->animation.transition.active())blend.source=static_cast<const animation::Cursor&>(object->animation);
    else blend.frozen=object->animationPose?object->animationPose->nodes:asset.nodes;
  }
  auto pose=std::make_shared<ModelPose>();evaluateModelPose(asset,state,*pose);
  object->animation=state;object->animationPose=std::move(pose);return animationState(ctx,*object);
}
static JSValue jsPauseAnimation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){auto *object=animationObject(ctx,argc,argv);if(!object)return JS_EXCEPTION;object->animation.playing=false;return animationState(ctx,*object);}
static JSValue jsResumeAnimation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){auto *object=animationObject(ctx,argc,argv);if(!object)return JS_EXCEPTION;if(object->animation.clip<0)return JS_ThrowRangeError(ctx,"play an animation before resuming");object->animation.playing=true;object->animation.finished=false;return animationState(ctx,*object);}
static JSValue jsStopAnimation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){auto *object=animationObject(ctx,argc,argv);if(!object)return JS_EXCEPTION;object->animation={};object->animationPose.reset();return animationState(ctx,*object);}
static JSValue jsSeekAnimation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  auto *object=animationObject(ctx,argc,argv);if(!object)return JS_EXCEPTION;
  double time=0;if(argc<2||!JS_IsNumber(argv[1])||JS_ToFloat64(ctx,&time,argv[1])<0||!std::isfinite(time))return JS_ThrowTypeError(ctx,"seekAnimation requires a finite time");
  const auto &asset=currentScene->assets[size_t(object->asset)-1];auto &state=object->animation;
  if(state.clip<0||time<0||time>asset.animations[state.clip].duration)return JS_ThrowRangeError(ctx,"select a clip and seek within its duration");
  state.time=time;state.finished=false;state.transition={};evaluateModelPose(asset,state,*object->animationPose);return animationState(ctx,*object);
}
float objectRadius(const Object &object){return object.animationPose?std::max(1.f,object.animationPose->radius):1.f;}
static bool readGrabButton(JSContext *ctx,JSValueConst options,GrabButton &button){
  JSValue value=JS_GetPropertyStr(ctx,options,"grabButton");
  if(JS_IsException(value))return false;
  if(JS_IsUndefined(value)){JS_FreeValue(ctx,value);return true;}
  if(!JS_IsString(value)){JS_FreeValue(ctx,value);JS_ThrowTypeError(ctx,"grabButton must be trigger, grip, or both");return false;}
  const char *text=JS_ToCString(ctx,value);JS_FreeValue(ctx,value);if(!text)return false;
  std::string name=text;JS_FreeCString(ctx,text);
  if(name=="trigger")button=GrabButton::Trigger;
  else if(name=="grip")button=GrabButton::Grip;
  else if(name=="both")button=GrabButton::Both;
  else{JS_ThrowRangeError(ctx,"grabButton must be trigger, grip, or both");return false;}
  return true;
}
static JSValue jsEnableObjectInteraction(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||!findObject(id))return JS_ThrowRangeError(ctx,"enableObjectInteraction(objectId) requires a live object");
  auto *object=findObject(id);GrabButton button=object->grabButton;
  const bool options=argc>1&&!JS_IsUndefined(argv[1]);
  if(options&&(!JS_IsObject(argv[1])||JS_IsNull(argv[1])))return JS_ThrowTypeError(ctx,"enableObjectInteraction(id, options) requires an options object");
  if(options&&!readGrabButton(ctx,argv[1],button))return JS_EXCEPTION;
  object->grabButton=button;object->grabbable=true;
  return JS_UNDEFINED;
}
static JSValue jsSetObjectInteraction(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!JS_IsObject(argv[1]))return JS_ThrowTypeError(ctx,"setObjectInteraction(id, options)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  GrabButton button=object->grabButton;if(!readGrabButton(ctx,argv[1],button))return JS_EXCEPTION;
  object->grabButton=button;
  auto readBool=[&](const char *name,bool &value){JSValue field=JS_GetPropertyStr(ctx,argv[1],name);if(!JS_IsUndefined(field))value=JS_ToBool(ctx,field)>0;JS_FreeValue(ctx,field);};
  readBool("laserPassThrough",object->laserPassThrough);
  readBool("grabbable",object->grabbable);
  readBool("directGrab",object->directGrab);
  readBool("laserYaw",object->laserYaw);
  readBool("laserPitchWithStickClick",object->laserPitchWithStickClick);
  readBool("depthAdjust",object->depthAdjust);
  JSValue axes=JS_GetPropertyStr(ctx,argv[1],"handRotationAxes");
  if(JS_IsObject(axes)){
    auto readAxis=[&](const char *name,bool &value){JSValue field=JS_GetPropertyStr(ctx,axes,name);if(!JS_IsUndefined(field))value=JS_ToBool(ctx,field)>0;JS_FreeValue(ctx,field);};
    readAxis("pitch",object->handPitch);readAxis("yaw",object->handYaw);readAxis("roll",object->handRoll);
  }
  JS_FreeValue(ctx,axes);
  return JS_UNDEFINED;
}
static bool number(JSContext *ctx,JSValueConst value,float &out){
  double n=0;if(JS_ToFloat64(ctx,&n,value)<0||!std::isfinite(n))return false;out=(float)n;return std::isfinite(out);
}
static JSValue jsLoadModel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<1)return JS_ThrowTypeError(ctx,"loadModel(path) requires a path");
  const char *path=JS_ToCString(ctx,argv[0]);if(!path)return JS_EXCEPTION;
  std::string name(path);JS_FreeCString(ctx,path);
  int eyePixels=kDefaultEyePixels;
  if(argc>1&&JS_ToInt32(ctx,&eyePixels,argv[1])<0)return JS_EXCEPTION;
  if(eyePixels<256||eyePixels>2048||eyePixels%64!=0)return JS_ThrowRangeError(ctx,"eyePixels must be a multiple of 64 from 256 to 2048");
  bool pbr=false,explicitPbr=false;
  bool optimizeVertexCache=false,explicitCache=false,gpuInstancing=true,explicitInstancing=false,lod=true,explicitLod=false;
  if(argc>2&&!JS_IsUndefined(argv[2])){
    if(!JS_IsObject(argv[2])||JS_IsNull(argv[2]))return JS_ThrowTypeError(ctx,"model options must be an object");
    JSValue option=JS_GetPropertyStr(ctx,argv[2],"pbr");if(JS_IsException(option))return option;
    explicitPbr=!JS_IsUndefined(option);
    if(explicitPbr&&!JS_IsBool(option)){JS_FreeValue(ctx,option);return JS_ThrowTypeError(ctx,"pbr must be a boolean");}
    if(explicitPbr)pbr=JS_ToBool(ctx,option)>0;JS_FreeValue(ctx,option);
    option=JS_GetPropertyStr(ctx,argv[2],"optimizeVertexCache");if(JS_IsException(option))return option;
    explicitCache=!JS_IsUndefined(option);
    if(explicitCache&&!JS_IsBool(option)){JS_FreeValue(ctx,option);return JS_ThrowTypeError(ctx,"optimizeVertexCache must be a boolean");}
    if(explicitCache)optimizeVertexCache=JS_ToBool(ctx,option)>0;JS_FreeValue(ctx,option);
    option=JS_GetPropertyStr(ctx,argv[2],"gpuInstancing");if(JS_IsException(option))return option;
    explicitInstancing=!JS_IsUndefined(option);
    if(explicitInstancing&&!JS_IsBool(option)){JS_FreeValue(ctx,option);return JS_ThrowTypeError(ctx,"gpuInstancing must be a boolean");}
    if(explicitInstancing)gpuInstancing=JS_ToBool(ctx,option)>0;JS_FreeValue(ctx,option);
    option=JS_GetPropertyStr(ctx,argv[2],"lod");if(JS_IsException(option))return option;
    explicitLod=!JS_IsUndefined(option);
    if(explicitLod&&!JS_IsBool(option)){JS_FreeValue(ctx,option);return JS_ThrowTypeError(ctx,"lod must be a boolean");}
    if(explicitLod)lod=JS_ToBool(ctx,option)>0;JS_FreeValue(ctx,option);
  }
  for(size_t i=0;i<currentScene->assets.size();i++)if(currentScene->assets[i].path==name){
    if(argc>1&&currentScene->assets[i].eyePixels!=eyePixels)return JS_ThrowRangeError(ctx,"model already loaded at a different eyePixels resolution");
    if(explicitCache&&currentScene->assets[i].optimizeVertexCache!=optimizeVertexCache)return JS_ThrowRangeError(ctx,"model already loaded with different optimizeVertexCache options");
    if(explicitInstancing&&currentScene->assets[i].gpuInstancing!=gpuInstancing)return JS_ThrowRangeError(ctx,"model already loaded with different gpuInstancing options");
    if(explicitPbr&&currentScene->assets[i].pbr!=pbr)return JS_ThrowRangeError(ctx,"model already loaded with different pbr options");
    if(explicitLod&&currentScene->assets[i].lod!=lod)return JS_ThrowRangeError(ctx,"model already loaded with different lod options");
    return JS_NewInt32(ctx,(int)i+1);
  }
  if(loadedAssetCount(*currentScene)>=32||currentScene->assets.size()>=size_t(INT_MAX-1))return JS_ThrowRangeError(ctx,"model asset limit reached (32 live assets)");
  ModelAsset asset;asset.pbr=pbr;
  if(!loadModelFile(name,asset))return JS_ThrowTypeError(ctx,"could not load model: %s",name.c_str());
  asset.eyePixels=eyePixels;
  asset.optimizeVertexCache=optimizeVertexCache;asset.gpuInstancing=gpuInstancing;asset.lod=lod;
  currentScene->assets.push_back(std::move(asset));
  return JS_NewInt32(ctx,(int)currentScene->assets.size());
}
static JSValue jsUnloadModel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
 double value=0;int id=0;if(!argc||!JS_IsNumber(argv[0])||JS_ToFloat64(ctx,&value,argv[0])<0||!std::isfinite(value)||value<1||value>=INT_MAX||value!=std::floor(value)||!loadedAsset(*currentScene,id=int(value)))return JS_ThrowRangeError(ctx,"unloadModel requires a live asset ID");
 if(std::any_of(currentScene->objects.begin(),currentScene->objects.end(),[&](const Object &object){return object.asset==id;}))return JS_ThrowRangeError(ctx,"model is used by a live object");
 if(currentScene->physics)currentScene->physics->releaseGeometry(id);
 auto &asset=currentScene->assets[size_t(id)-1];deleteModel(asset);asset=ModelAsset{};asset.uploaded=true;currentScene->assetsReleased=true;
 return JS_UNDEFINED;
}
static JSValue jsSetModelLighting(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;
  if(!currentScene||argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!loadedAsset(*currentScene,id))return JS_ThrowRangeError(ctx,"unknown asset id");
  if(!JS_IsObject(argv[1])||JS_IsNull(argv[1]))return JS_ThrowTypeError(ctx,"lighting options must be an object");
  // Copy before running user getters, which may append/reallocate shared assets.
  const auto path=currentScene->assets[id-1].path;auto candidate=currentScene->assets[id-1].lighting;
  if(!currentScene->assets[id-1].pbr)return JS_ThrowTypeError(ctx,"lighting requires a model loaded with pbr: true");
  for(auto [name,destination,low,high]:{std::tuple{"intensity",&candidate.intensity,0.f,64.f},std::tuple{"ambient",&candidate.ambient,0.f,16.f},std::tuple{"exposure",&candidate.exposure,.001f,64.f},std::tuple{"environmentIntensity",&candidate.environmentIntensity,0.f,64.f},std::tuple{"environmentRotation",&candidate.environmentRotation,-10000.f,10000.f}}){
    JSValue v=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(v))return v;
    float n=0;bool missing=JS_IsUndefined(v),valid=missing||(JS_IsNumber(v)&&number(ctx,v,n)&&n>=low&&n<=high);JS_FreeValue(ctx,v);
    if(!valid)return JS_ThrowRangeError(ctx,"invalid lighting %s",name);if(!missing)*destination=n;
  }
  for(auto [name,destination,isColor]:{std::tuple{"direction",candidate.direction,false},std::tuple{"color",candidate.color,true}}){
    JSValue v=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(v))return v;
    if(JS_IsUndefined(v)){JS_FreeValue(ctx,v);continue;}
    if(!JS_IsArray(v)){JS_FreeValue(ctx,v);return JS_ThrowTypeError(ctx,"lighting vectors must be arrays");}
    JSValue length=JS_GetPropertyStr(ctx,v,"length");if(JS_IsException(length)){JS_FreeValue(ctx,v);return length;}int count=0;bool valid=JS_IsArray(v)&&JS_ToInt32(ctx,&count,length)==0&&count==3;JS_FreeValue(ctx,length);
    for(int i=0;i<3&&valid;i++){JSValue component=JS_GetPropertyUint32(ctx,v,i);if(JS_IsException(component)){JS_FreeValue(ctx,v);return component;}float n=0;valid=JS_IsNumber(component)&&number(ctx,component,n)&&n>=(isColor?0.f:-64.f)&&n<=64;JS_FreeValue(ctx,component);if(valid)destination[i]=n;}
    JS_FreeValue(ctx,v);if(!valid)return JS_ThrowRangeError(ctx,"lighting %s must be an array of three finite numbers",name);
  }
  JSValue environment=JS_GetPropertyStr(ctx,argv[1],"environment");if(JS_IsException(environment))return environment;
  if(JS_IsNull(environment))candidate.environment.clear();
  else if(!JS_IsUndefined(environment)){
    if(!JS_IsString(environment)){JS_FreeValue(ctx,environment);return JS_ThrowTypeError(ctx,"environment must be a path string or null");}
    size_t size=0;const char *path=JS_ToCStringLen(ctx,&size,environment);if(!path){JS_FreeValue(ctx,environment);return JS_EXCEPTION;}
    candidate.environment.assign(path,size);JS_FreeCString(ctx,path);
    if(candidate.environment.empty()||candidate.environment.size()>4096||candidate.environment.find('\0')!=std::string::npos){JS_FreeValue(ctx,environment);return JS_ThrowRangeError(ctx,"invalid environment path");}
  }
  JS_FreeValue(ctx,environment);
  double length=0;for(float v:candidate.direction)length+=double(v)*v;if(length<1e-12)return JS_ThrowRangeError(ctx,"light direction must be nonzero");
  if(size_t(id)>currentScene->assets.size()||currentScene->assets[id-1].path!=path)return JS_ThrowRangeError(ctx,"model changed during lighting option evaluation");
  auto stagedEnvironment=candidate.environment.empty()?std::shared_ptr<LightingEnvironment>{}:loadLightingEnvironment(candidate.environment);
  if(!candidate.environment.empty()&&!stagedEnvironment)return JS_ThrowTypeError(ctx,"could not load environment: %s",candidate.environment.c_str());
  currentScene->assets[id-1].lighting=candidate;currentScene->assets[id-1].environment=std::move(stagedEnvironment);return JS_UNDEFINED;
}
static JSValue jsSetRenderGroupAutoSplit(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  double group=0;
  if(!currentScene||argc<1||!JS_IsNumber(argv[0])||JS_ToFloat64(ctx,&group,argv[0])<0||!std::isfinite(group)||group<1||group>256||group!=std::floor(group))return JS_ThrowRangeError(ctx,"render group must be an integer from 1 to 256");
  if(argc>1&&JS_IsBool(argv[1])&&!JS_ToBool(ctx,argv[1])){currentScene->renderGroupSplits.erase(int(group));return JS_UNDEFINED;}
  RenderGroupSplitPolicy policy;
  if(argc>1&&!JS_IsUndefined(argv[1])){
    if(!JS_IsObject(argv[1])||JS_IsNull(argv[1]))return JS_ThrowTypeError(ctx,"auto split options must be an object or false");
    for(auto [name,destination,low,high,step]:{std::tuple{"maxSurfaces",&policy.maxSurfaces,1,16,1},std::tuple{"targetEyePixels",&policy.targetEyePixels,256,4096,64}}){
      JSValue value=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(value))return value;
      double number=0;const bool missing=JS_IsUndefined(value);
      const bool valid=missing||(JS_IsNumber(value)&&JS_ToFloat64(ctx,&number,value)==0&&std::isfinite(number)&&number>=low&&number<=high&&std::fmod(number,step)==0);
      JS_FreeValue(ctx,value);if(!valid)return JS_ThrowRangeError(ctx,"invalid auto split %s",name);
      if(!missing)*destination=int(number);
    }
    for(auto [name,destination]:{std::pair{"merge",&policy.merge},std::pair{"rebalance",&policy.rebalance}}){
      JSValue value=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(value))return value;
      if(!JS_IsUndefined(value)&&!JS_IsBool(value)){JS_FreeValue(ctx,value);return JS_ThrowTypeError(ctx,"auto split %s must be a boolean",name);}
      if(!JS_IsUndefined(value))*destination=JS_ToBool(ctx,value)>0;JS_FreeValue(ctx,value);
    }
  }
  currentScene->renderGroupSplits[int(group)]=policy;return JS_UNDEFINED;
}
static JSValue jsCreateObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<4||currentScene->objects.size()>=256)return JS_ThrowRangeError(ctx,"createObject needs asset, x, y, z; limit is 256 objects");
  int asset=0;if(JS_ToInt32(ctx,&asset,argv[0])<0||!loadedAsset(*currentScene,asset))return JS_ThrowRangeError(ctx,"unknown asset id");
  float values[4]={0,0,0,.55f};
  for(int i=1;i<std::min(argc,5);i++)if(!number(ctx,argv[i],values[i-1]))return JS_ThrowTypeError(ctx,"object coordinates and scale must be finite numbers");
  if(values[3]<=0||values[3]>2)return JS_ThrowRangeError(ctx,"scale must be greater than 0 and at most 2 meters");
  int group=0,owner=0;
  if(argc>5&&!JS_IsUndefined(argv[5])){
    if(!JS_IsObject(argv[5])||JS_IsNull(argv[5]))return JS_ThrowTypeError(ctx,"createObject options must be an object");
    if(!containerOption(ctx,argv[5],owner))return JS_EXCEPTION;
    JSValue value=JS_GetPropertyStr(ctx,argv[5],"renderGroup");if(JS_IsException(value))return value;
    double number=0;const bool valid=JS_IsUndefined(value)||(JS_IsNumber(value)&&JS_ToFloat64(ctx,&number,value)==0&&std::isfinite(number)&&number>=0&&number<=256&&number==std::floor(number));
    JS_FreeValue(ctx,value);if(!valid)return JS_ThrowRangeError(ctx,"renderGroup must be an integer from 0 to 256");group=int(number);
  }
  if(owner&&!currentScene->containers.count(owner))return JS_ThrowRangeError(ctx,"scene container was disposed during object option evaluation");
  if(!loadedAsset(*currentScene,asset))return JS_ThrowRangeError(ctx,"model unloaded during object option evaluation");
  int tile=0;double nearest=INFINITY;
  if(group)for(const auto &other:currentScene->objects)if(other.renderGroup==group){
    const double dx=double(other.x)-values[0],dy=double(other.y)-values[1],dz=double(other.z)-values[2],distance=dx*dx+dy*dy+dz*dz;
    if(distance<nearest){nearest=distance;tile=other.renderTile;}
  }
  int id=currentScene->nextId++;
  if(currentScene==liveScene&&reserveObjectOverlay){const auto error=reserveObjectOverlay(group?-(group+256*tile):id);if(!error.empty())return JS_ThrowRangeError(ctx,"%s",error.c_str());}
  currentScene->objects.push_back({id,asset,values[0],values[1],values[2],values[3],0});
  currentScene->objects.back().renderGroup=group;currentScene->objects.back().renderTile=tile;currentScene->objects.back().container=owner;
  return JS_NewInt32(ctx,id);
}
static physics::Pose physicsPose(const Object &o){return {{o.x,o.y,o.z},o.pitch,o.yaw,o.roll};}
static physics::World &physicsWorld(Scene &scene){if(!scene.physics)scene.physics=std::make_unique<physics::World>();return *scene.physics;}
static void configurePhysics(Scene &scene,const Object &object,const physics::Settings &settings){
  const auto &asset=scene.assets[size_t(object.asset)-1];
  physics::Vec3 low{asset.low[0]*object.scale,asset.low[1]*object.scale,asset.low[2]*object.scale};
  physics::Vec3 high{asset.high[0]*object.scale,asset.high[1]*object.scale,asset.high[2]*object.scale};
  auto &world=physicsWorld(scene);
  std::vector<physics::Vec3> vertices;
  if((settings.shape==physics::Shape::Convex&&!world.hasConvexHull(object.asset))||
     (settings.shape==physics::Shape::Mesh&&!world.hasMesh(object.asset)))
    for(const auto &part:asset.primitives)for(size_t i=0;i+4<part.vertices.size();i+=5)
      vertices.push_back({part.vertices[i],part.vertices[i+1],part.vertices[i+2]});
  world.set(object.id,settings,physicsPose(object),low,high,{object.asset,object.scale,vertices});
}
static void teleportPhysics(Object &object){
  if(!currentScene->physics||!currentScene->physics->contains(object.id))return;
  auto &world=*currentScene->physics;
  if(world.get(object.id).settings.type==physics::Type::Kinematic)world.target(object.id,physicsPose(object),0);
  else world.pose(object.id,physicsPose(object));
}
static JSValue physicsVector(JSContext *ctx,physics::Vec3 v){JSValue o=JS_NewObject(ctx);JS_SetPropertyStr(ctx,o,"x",JS_NewFloat64(ctx,v.x));JS_SetPropertyStr(ctx,o,"y",JS_NewFloat64(ctx,v.y));JS_SetPropertyStr(ctx,o,"z",JS_NewFloat64(ctx,v.z));return o;}
static JSValue physicsState(JSContext *ctx,const physics::State &s){
  JSValue o=JS_NewObject(ctx);const auto &c=s.settings;
  JS_SetPropertyStr(ctx,o,"type",JS_NewString(ctx,c.type==physics::Type::Static?"static":c.type==physics::Type::Kinematic?"kinematic":"dynamic"));
  JS_SetPropertyStr(ctx,o,"shape",JS_NewString(ctx,c.shape==physics::Shape::Box?"box":c.shape==physics::Shape::Sphere?"sphere":c.shape==physics::Shape::Convex?"convex":"mesh"));
  for(const auto &[name,value]:std::initializer_list<std::pair<const char*,float>>{{"mass",c.mass},{"friction",c.friction},{"restitution",c.restitution},{"linearDamping",c.linearDamping},{"angularDamping",c.angularDamping},{"gravityFactor",c.gravityFactor}})JS_SetPropertyStr(ctx,o,name,JS_NewFloat64(ctx,value));
  JS_SetPropertyStr(ctx,o,"continuous",JS_NewBool(ctx,c.continuous));
  JS_SetPropertyStr(ctx,o,"halfExtents",c.halfExtents?physicsVector(ctx,*c.halfExtents):JS_NULL);
  JS_SetPropertyStr(ctx,o,"radius",c.radius?JS_NewFloat64(ctx,*c.radius):JS_NULL);
  JS_SetPropertyStr(ctx,o,"offset",c.offset?physicsVector(ctx,*c.offset):JS_NULL);
  JSValue collider=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,collider,"halfExtents",c.shape!=physics::Shape::Sphere?physicsVector(ctx,s.collider.halfExtents):JS_NULL);
  JS_SetPropertyStr(ctx,collider,"radius",c.shape==physics::Shape::Sphere?JS_NewFloat64(ctx,s.collider.radius):JS_NULL);
  JS_SetPropertyStr(ctx,collider,"centerOfMass",physicsVector(ctx,s.collider.centerOfMass));
  JS_SetPropertyStr(ctx,collider,"triangleCount",c.shape==physics::Shape::Mesh?JS_NewUint32(ctx,s.collider.triangleCount):JS_NULL);
  JS_SetPropertyStr(ctx,collider,"vertexCount",c.shape==physics::Shape::Convex?JS_NewUint32(ctx,s.collider.vertexCount):JS_NULL);
  JS_SetPropertyStr(ctx,collider,"offset",physicsVector(ctx,s.collider.offset));JS_SetPropertyStr(ctx,o,"collider",collider);
  JS_SetPropertyStr(ctx,o,"position",physicsVector(ctx,s.pose.position));
  JS_SetPropertyStr(ctx,o,"rotation",physicsVector(ctx,{s.pose.pitch,s.pose.yaw,s.pose.roll}));
  JS_SetPropertyStr(ctx,o,"velocity",physicsVector(ctx,s.velocity));JS_SetPropertyStr(ctx,o,"angularVelocity",physicsVector(ctx,s.angularVelocity));
  JS_SetPropertyStr(ctx,o,"jointed",JS_NewBool(ctx,s.jointed));
  JS_SetPropertyStr(ctx,o,"held",JS_NewBool(ctx,s.held));JS_SetPropertyStr(ctx,o,"sleeping",JS_NewBool(ctx,s.sleeping));return o;
}
static Object *physicsObject(JSContext *ctx,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0){JS_ThrowTypeError(ctx,"physics requires an object id");return nullptr;}
  auto *o=findObject(id);if(!o)JS_ThrowRangeError(ctx,"unknown object id");return o;
}
static bool readColliderVector(JSContext *ctx,JSValueConst options,const char *name,std::optional<physics::Vec3> &result){
  JSValue value=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(value))return false;
  if(JS_IsUndefined(value)){JS_FreeValue(ctx,value);return true;}
  if(JS_IsNull(value)){result.reset();JS_FreeValue(ctx,value);return true;}
  if(!JS_IsObject(value)){JS_FreeValue(ctx,value);JS_ThrowTypeError(ctx,"%s must be null or {x,y,z}",name);return false;}
  physics::Vec3 vector;float *components[]={&vector.x,&vector.y,&vector.z};
  for(int i=0;i<3;i++){
    JSValue field=JS_GetPropertyStr(ctx,value,i==0?"x":i==1?"y":"z");
    if(JS_IsException(field)){JS_FreeValue(ctx,value);return false;}
    double number=0;const bool ok=JS_IsNumber(field)&&JS_ToFloat64(ctx,&number,field)==0&&std::isfinite(number);
    JS_FreeValue(ctx,field);
    if(!ok){JS_FreeValue(ctx,value);JS_ThrowTypeError(ctx,"%s requires finite numeric x, y, z",name);return false;}
    *components[i]=float(number);
  }
  JS_FreeValue(ctx,value);result=vector;return true;
}
static JSValue jsSetObjectPhysics(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  auto *o=physicsObject(ctx,argc,argv);if(!o)return JS_EXCEPTION;
  if(argc>1&&(!JS_IsObject(argv[1])||JS_IsNull(argv[1])))return JS_ThrowTypeError(ctx,"physics options must be an object");
  physics::Settings settings;
  if(currentScene->physics&&currentScene->physics->contains(o->id))settings=currentScene->physics->get(o->id).settings;
  if(argc>1){
    auto text=[&](const char *name,std::string &out){JSValue v=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(v))return false;if(JS_IsUndefined(v)){JS_FreeValue(ctx,v);return true;}if(!JS_IsString(v)){JS_FreeValue(ctx,v);JS_ThrowTypeError(ctx,"physics type and shape must be strings");return false;}const char *s=JS_ToCString(ctx,v);JS_FreeValue(ctx,v);if(!s)return false;out=s;JS_FreeCString(ctx,s);return true;};
    std::string type=settings.type==physics::Type::Static?"static":settings.type==physics::Type::Kinematic?"kinematic":"dynamic",shape=settings.shape==physics::Shape::Box?"box":settings.shape==physics::Shape::Sphere?"sphere":settings.shape==physics::Shape::Convex?"convex":"mesh";
    if(!text("type",type)||!text("shape",shape))return JS_EXCEPTION;
    if(type=="static")settings.type=physics::Type::Static;else if(type=="dynamic")settings.type=physics::Type::Dynamic;else if(type=="kinematic")settings.type=physics::Type::Kinematic;else return JS_ThrowRangeError(ctx,"physics type must be static, dynamic, or kinematic");
    if(shape=="box")settings.shape=physics::Shape::Box;else if(shape=="sphere")settings.shape=physics::Shape::Sphere;else if(shape=="convex")settings.shape=physics::Shape::Convex;else if(shape=="mesh")settings.shape=physics::Shape::Mesh;else return JS_ThrowRangeError(ctx,"physics shape must be box, sphere, convex, or mesh");
    JSValue continuous=JS_GetPropertyStr(ctx,argv[1],"continuous");
    if(JS_IsException(continuous))return JS_EXCEPTION;
    if(!JS_IsUndefined(continuous)){
      if(!JS_IsBool(continuous)){JS_FreeValue(ctx,continuous);return JS_ThrowTypeError(ctx,"continuous must be a boolean");}
      settings.continuous=JS_ToBool(ctx,continuous)>0;
    }JS_FreeValue(ctx,continuous);
    if(shape!="sphere")settings.radius.reset();
    if(shape!="box")settings.halfExtents.reset();
    if(!readColliderVector(ctx,argv[1],"halfExtents",settings.halfExtents)||!readColliderVector(ctx,argv[1],"offset",settings.offset))return JS_EXCEPTION;
    JSValue radius=JS_GetPropertyStr(ctx,argv[1],"radius");if(JS_IsException(radius))return JS_EXCEPTION;
    if(JS_IsNull(radius))settings.radius.reset();
    else if(!JS_IsUndefined(radius)){
      double number=0;
      if(!JS_IsNumber(radius)||JS_ToFloat64(ctx,&number,radius)<0||!std::isfinite(number)){JS_FreeValue(ctx,radius);return JS_ThrowTypeError(ctx,"radius must be null or a finite number");}
      settings.radius=float(number);
    }
    JS_FreeValue(ctx,radius);

    for(auto [name,value]:std::initializer_list<std::pair<const char*,float*>>{{"mass",&settings.mass},{"friction",&settings.friction},{"restitution",&settings.restitution},{"linearDamping",&settings.linearDamping},{"angularDamping",&settings.angularDamping},{"gravityFactor",&settings.gravityFactor}}){
      JSValue v=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(v))return JS_EXCEPTION;
      if(!JS_IsUndefined(v)){double number;if(!JS_IsNumber(v)||JS_ToFloat64(ctx,&number,v)<0||!std::isfinite(number)){JS_FreeValue(ctx,v);return JS_ThrowTypeError(ctx,"physics parameters must be finite numbers");}*value=float(number);}JS_FreeValue(ctx,v);
    }
  }
  try{configurePhysics(*currentScene,*o,settings);return physicsState(ctx,currentScene->physics->get(o->id));}catch(const std::exception &e){return JS_ThrowRangeError(ctx,"%s",e.what());}
}
static JSValue jsGetObjectPhysics(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){auto *o=physicsObject(ctx,argc,argv);if(!o)return JS_EXCEPTION;if(!currentScene->physics||!currentScene->physics->contains(o->id))return JS_NULL;return physicsState(ctx,currentScene->physics->get(o->id));}
static JSValue jsRemoveObjectPhysics(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){auto *o=physicsObject(ctx,argc,argv);if(!o)return JS_EXCEPTION;if(currentScene->physics)currentScene->physics->remove(o->id);return JS_UNDEFINED;}
static bool readPhysicsVector(JSContext *ctx,int argc,JSValueConst *argv,int offset,physics::Vec3 &v){
  if(argc<offset+3){JS_ThrowTypeError(ctx,"physics vector requires x, y, z");return false;}
  float *fields[]={&v.x,&v.y,&v.z};for(int i=0;i<3;i++){double n;if(!JS_IsNumber(argv[offset+i])||JS_ToFloat64(ctx,&n,argv[offset+i])<0||!std::isfinite(n)||std::fabs(n)>10000){JS_ThrowRangeError(ctx,"physics vectors require finite components within +/-10000");return false;}*fields[i]=float(n);}return true;
}
static JSValue jsSetPhysicsWorkers(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  double n=0;if(argc<1||!JS_IsNumber(argv[0])||JS_ToFloat64(ctx,&n,argv[0])<0||!std::isfinite(n)||n!=std::floor(n)||n < -1||n>4)return JS_ThrowRangeError(ctx,"physics workers must be an integer from -1 to 4");
  physicsWorld(*currentScene).workers(int(n));return JS_UNDEFINED;
}
static JSValue jsSetPhysicsGravity(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){physics::Vec3 v;if(!readPhysicsVector(ctx,argc,argv,0,v))return JS_EXCEPTION;physicsWorld(*currentScene).gravity(v);return JS_UNDEFINED;}
template<int Operation> static JSValue jsPhysicsVectorOperation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  auto *o=physicsObject(ctx,argc,argv);if(!o)return JS_EXCEPTION;physics::Vec3 v;if(!readPhysicsVector(ctx,argc,argv,1,v))return JS_EXCEPTION;
  if(!currentScene->physics)return JS_ThrowRangeError(ctx,"object has no physics body");
  try{auto &w=*currentScene->physics;if constexpr(Operation==0)w.velocity(o->id,v);else if constexpr(Operation==1)w.angularVelocity(o->id,v);else w.impulse(o->id,v);}catch(const std::exception &e){return JS_ThrowRangeError(ctx,"%s",e.what());}return JS_UNDEFINED;
}
static JSValue jointState(JSContext *ctx,const physics::JointState &s){
  JSValue value=JS_NewObject(ctx);
  for(auto [name,id]:std::initializer_list<std::pair<const char*,int>>{{"id",s.id},{"objectA",s.objectA},{"objectB",s.objectB}})JS_SetPropertyStr(ctx,value,name,JS_NewInt32(ctx,id));
  JS_SetPropertyStr(ctx,value,"type",JS_NewString(ctx,s.settings.type==physics::JointType::Fixed?"fixed":s.settings.type==physics::JointType::Hinge?"hinge":"distance"));
  JS_SetPropertyStr(ctx,value,"anchorA",physicsVector(ctx,s.anchorA));JS_SetPropertyStr(ctx,value,"anchorB",physicsVector(ctx,s.anchorB));
  JS_SetPropertyStr(ctx,value,"axis",s.settings.type==physics::JointType::Hinge?physicsVector(ctx,s.settings.axis):JS_NULL);
  JS_SetPropertyStr(ctx,value,"angle",s.settings.type==physics::JointType::Hinge?JS_NewFloat64(ctx,s.angle):JS_NULL);
  JS_SetPropertyStr(ctx,value,"distance",JS_NewFloat64(ctx,s.distance));
  JSValue motor=JS_NULL;
  if(s.settings.type==physics::JointType::Hinge){
    motor=JS_NewObject(ctx);JS_SetPropertyStr(ctx,motor,"mode",JS_NewString(ctx,s.motor.mode==physics::MotorMode::Off?"off":s.motor.mode==physics::MotorMode::Position?"position":"velocity"));
    for(auto [name,number]:std::initializer_list<std::pair<const char*,float>>{{"targetAngle",s.motor.targetAngle},{"targetVelocity",s.motor.targetVelocity},{"maxTorque",s.motor.maxTorque},{"frequency",s.motor.frequency},{"damping",s.motor.damping}})JS_SetPropertyStr(ctx,motor,name,JS_NewFloat64(ctx,number));
  }
  JS_SetPropertyStr(ctx,value,"motor",motor);
  JSValue spring=JS_NULL;
  if(s.settings.spring){spring=JS_NewObject(ctx);JS_SetPropertyStr(ctx,spring,"frequency",JS_NewFloat64(ctx,s.settings.spring->frequency));JS_SetPropertyStr(ctx,spring,"damping",JS_NewFloat64(ctx,s.settings.spring->damping));}
  JS_SetPropertyStr(ctx,value,"spring",spring);
  JS_SetPropertyStr(ctx,value,"breakForce",s.settings.breakForce?JS_NewFloat64(ctx,*s.settings.breakForce):JS_NULL);
  JS_SetPropertyStr(ctx,value,"breakTorque",s.settings.breakTorque?JS_NewFloat64(ctx,*s.settings.breakTorque):JS_NULL);
  JS_SetPropertyStr(ctx,value,"collideConnected",JS_NewBool(ctx,s.settings.collideConnected));
  for(auto [name,number]:std::initializer_list<std::pair<const char*,float>>{{"minAngle",s.settings.minAngle},{"maxAngle",s.settings.maxAngle},{"minDistance",s.settings.minDistance},{"maxDistance",s.settings.maxDistance.value_or(0)}})
    JS_SetPropertyStr(ctx,value,name,((s.settings.type==physics::JointType::Hinge&&std::strstr(name,"Angle"))||(s.settings.type==physics::JointType::Distance&&std::strstr(name,"Distance")))?JS_NewFloat64(ctx,number):JS_NULL);
  return value;
}
static JSValue jsSetPhysicsJointMotor(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;
  if(argc<2||!JS_IsNumber(argv[0])||JS_ToInt32(ctx,&id,argv[0])<0||!JS_IsObject(argv[1])||JS_IsNull(argv[1]))return JS_ThrowTypeError(ctx,"setPhysicsJointMotor(id, options)");
  auto *world=currentScene->physics.get();
  if(!world||!world->containsJoint(id))return JS_ThrowRangeError(ctx,"unknown physics joint");
  auto motor=world->joint(id).motor;
  JSValue mode=JS_GetPropertyStr(ctx,argv[1],"mode");if(JS_IsException(mode))return JS_EXCEPTION;
  if(!JS_IsUndefined(mode)){
    if(!JS_IsString(mode)){JS_FreeValue(ctx,mode);return JS_ThrowTypeError(ctx,"motor mode must be a string");}
    const char *text=JS_ToCString(ctx,mode);JS_FreeValue(ctx,mode);if(!text)return JS_EXCEPTION;
    std::string name=text;JS_FreeCString(ctx,text);
    if(name=="off")motor.mode=physics::MotorMode::Off;else if(name=="position")motor.mode=physics::MotorMode::Position;else if(name=="velocity")motor.mode=physics::MotorMode::Velocity;else return JS_ThrowRangeError(ctx,"motor mode must be off, position, or velocity");
  }else JS_FreeValue(ctx,mode);
  for(auto [name,field]:std::initializer_list<std::pair<const char*,float*>>{{"targetAngle",&motor.targetAngle},{"targetVelocity",&motor.targetVelocity},{"maxTorque",&motor.maxTorque},{"frequency",&motor.frequency},{"damping",&motor.damping}}){
    JSValue value=JS_GetPropertyStr(ctx,argv[1],name);if(JS_IsException(value))return JS_EXCEPTION;
    if(!JS_IsUndefined(value)){
      double n=0;const bool ok=JS_IsNumber(value)&&JS_ToFloat64(ctx,&n,value)==0&&std::isfinite(n);JS_FreeValue(ctx,value);
      if(!ok)return JS_ThrowTypeError(ctx,"motor settings must be finite numbers");*field=float(n);
    }else JS_FreeValue(ctx,value);
  }
  try{world->jointMotor(id,motor);return jointState(ctx,world->joint(id));}catch(const std::exception &e){return JS_ThrowRangeError(ctx,"%s",e.what());}
}
static JSValue readJointOptions(JSContext *ctx,JSValueConst options,physics::JointSettings &settings,bool update){
  if(!JS_IsObject(options)||JS_IsNull(options))return JS_ThrowTypeError(ctx,"joint options must be an object");
  if(update)for(const char *name:{"type","anchor","anchorA","anchorB","axis"}){
    JSValue v=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(v))return JS_EXCEPTION;
    const bool supplied=!JS_IsUndefined(v);JS_FreeValue(ctx,v);
    if(supplied)return JS_ThrowRangeError(ctx,"joint type, anchors and axis cannot be updated");
  }
    JSValue type=JS_GetPropertyStr(ctx,options,"type");if(JS_IsException(type))return JS_EXCEPTION;
    if(!JS_IsUndefined(type)){
      if(!JS_IsString(type)){JS_FreeValue(ctx,type);return JS_ThrowTypeError(ctx,"joint type must be a string");}
      const char *text=JS_ToCString(ctx,type);JS_FreeValue(ctx,type);if(!text)return JS_EXCEPTION;
      std::string name=text;JS_FreeCString(ctx,text);
      if(name=="fixed")settings.type=physics::JointType::Fixed;else if(name=="hinge")settings.type=physics::JointType::Hinge;else if(name=="distance")settings.type=physics::JointType::Distance;else return JS_ThrowRangeError(ctx,"joint type must be fixed, hinge, or distance");
    }else JS_FreeValue(ctx,type);
    if(!readColliderVector(ctx,options,"anchor",settings.anchor)||!readColliderVector(ctx,options,"anchorA",settings.anchorA)||!readColliderVector(ctx,options,"anchorB",settings.anchorB))return JS_EXCEPTION;
    std::optional<physics::Vec3> axis;
    if(!readColliderVector(ctx,options,"axis",axis))return JS_EXCEPTION;
    if(axis)settings.axis=*axis;
    if((settings.type==physics::JointType::Distance&&(settings.anchor||axis))||(settings.type!=physics::JointType::Distance&&(settings.anchorA||settings.anchorB))||(settings.type==physics::JointType::Fixed&&axis))return JS_ThrowRangeError(ctx,"joint anchors or axis do not apply to this type");
    for(const char *name:{"minAngle","maxAngle","minDistance","maxDistance"}){
      JSValue field=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(field))return JS_EXCEPTION;
      if(!JS_IsUndefined(field)){
        double n=0;const bool ok=JS_IsNumber(field)&&JS_ToFloat64(ctx,&n,field)==0&&std::isfinite(n);JS_FreeValue(ctx,field);
        if(!ok)return JS_ThrowTypeError(ctx,"joint limits must be finite numbers");
        const bool angle=std::strstr(name,"Angle");
        if((angle&&settings.type!=physics::JointType::Hinge)||(!angle&&settings.type!=physics::JointType::Distance))return JS_ThrowRangeError(ctx,"joint limit does not apply to this type");
        if(std::strcmp(name,"minAngle")==0)settings.minAngle=float(n);else if(std::strcmp(name,"maxAngle")==0)settings.maxAngle=float(n);else if(std::strcmp(name,"minDistance")==0)settings.minDistance=float(n);else settings.maxDistance=float(n);
      }else JS_FreeValue(ctx,field);
    }
    JSValue spring=JS_GetPropertyStr(ctx,options,"spring");if(JS_IsException(spring))return JS_EXCEPTION;
    if(!JS_IsUndefined(spring)&&!JS_IsNull(spring)){
      if(!JS_IsObject(spring)){JS_FreeValue(ctx,spring);return JS_ThrowTypeError(ctx,"spring must be null or an object");}
      physics::JointSpring config=settings.spring.value_or(physics::JointSpring{});
      for(auto [name,field]:std::initializer_list<std::pair<const char*,float*>>{{"frequency",&config.frequency},{"damping",&config.damping}}){
        JSValue value=JS_GetPropertyStr(ctx,spring,name);if(JS_IsException(value)){JS_FreeValue(ctx,spring);return JS_EXCEPTION;}
        if(!JS_IsUndefined(value)){
          double n=0;const bool ok=JS_IsNumber(value)&&JS_ToFloat64(ctx,&n,value)==0&&std::isfinite(n);JS_FreeValue(ctx,value);
          if(!ok){JS_FreeValue(ctx,spring);return JS_ThrowTypeError(ctx,"spring settings must be finite numbers");}*field=float(n);
        }else JS_FreeValue(ctx,value);
      }
      settings.spring=config;
    }
    if(JS_IsNull(spring))settings.spring.reset();
    JS_FreeValue(ctx,spring);
    for(auto [name,field]:std::initializer_list<std::pair<const char*,std::optional<float>*>>{{"breakForce",&settings.breakForce},{"breakTorque",&settings.breakTorque}}){
      JSValue value=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(value))return JS_EXCEPTION;
      if(!JS_IsUndefined(value)&&!JS_IsNull(value)){
        double n=0;const bool ok=JS_IsNumber(value)&&JS_ToFloat64(ctx,&n,value)==0&&std::isfinite(n);JS_FreeValue(ctx,value);
        if(!ok)return JS_ThrowTypeError(ctx,"break thresholds must be null or finite numbers");*field=float(n);
      }else {if(JS_IsNull(value))field->reset();JS_FreeValue(ctx,value);}
    }
    JSValue collide=JS_GetPropertyStr(ctx,options,"collideConnected");if(JS_IsException(collide))return JS_EXCEPTION;
    if(!JS_IsUndefined(collide)&&!JS_IsBool(collide)){JS_FreeValue(ctx,collide);return JS_ThrowTypeError(ctx,"collideConnected must be a boolean");}
    if(!JS_IsUndefined(collide))settings.collideConnected=JS_ToBool(ctx,collide)>0;JS_FreeValue(ctx,collide);
  return JS_UNDEFINED;
}
static JSValue jsCreatePhysicsJoint(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int a=0,b=0;
  if(argc<2||!JS_IsNumber(argv[0])||!JS_IsNumber(argv[1])||JS_ToInt32(ctx,&a,argv[0])<0||JS_ToInt32(ctx,&b,argv[1])<0)return JS_ThrowTypeError(ctx,"createPhysicsJoint(objectA, objectB, options)");
  physics::JointSettings settings;
  if(argc>2&&JS_IsException(readJointOptions(ctx,argv[2],settings,false)))return JS_EXCEPTION;
  try{return JS_NewInt32(ctx,physicsWorld(*currentScene).createJoint(a,b,settings));}catch(const std::exception &e){return JS_ThrowRangeError(ctx,"%s",e.what());}
}
static JSValue jsSetPhysicsJoint(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<2||!JS_IsNumber(argv[0])||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"setPhysicsJoint(id, options)");
  auto *world=currentScene->physics.get();if(!world||!world->containsJoint(id))return JS_ThrowRangeError(ctx,"unknown physics joint");
  auto settings=world->joint(id).settings;
  if(JS_IsException(readJointOptions(ctx,argv[1],settings,true)))return JS_EXCEPTION;
  try{world->updateJoint(id,settings);return jointState(ctx,world->joint(id));}catch(const std::exception &e){return JS_ThrowRangeError(ctx,"%s",e.what());}
}
template<bool Remove> static JSValue jsPhysicsJoint(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||!JS_IsNumber(argv[0])||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"joint id must be numeric");
  auto *world=currentScene->physics.get();
  if constexpr(Remove){if(world)world->removeJoint(id);return JS_UNDEFINED;}
  else {if(!world||!world->containsJoint(id))return JS_NULL;return jointState(ctx,world->joint(id));}
}
static JSValue jsSetPhysicsCollisionEvents(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1||!JS_IsBool(argv[0]))return JS_ThrowTypeError(ctx,"setPhysicsCollisionEvents(enabled, options) requires a boolean");
  bool stay=false;
  if(argc>1){
    if(!JS_IsObject(argv[1])||JS_IsNull(argv[1]))return JS_ThrowTypeError(ctx,"collision event options must be an object");
    JSValue value=JS_GetPropertyStr(ctx,argv[1],"stay");if(JS_IsException(value))return JS_EXCEPTION;
    if(!JS_IsUndefined(value)&&!JS_IsBool(value)){JS_FreeValue(ctx,value);return JS_ThrowTypeError(ctx,"stay must be a boolean");}
    stay=JS_ToBool(ctx,value)>0;JS_FreeValue(ctx,value);
  }
  const bool enabled=JS_ToBool(ctx,argv[0])>0;
  if(enabled||currentScene->physics)physicsWorld(*currentScene).collisionEvents(enabled,stay);
  if(!enabled&&currentScene==liveScene)sceneInput().clearPhysicsEvents();
  return JS_UNDEFINED;
}
void updateScenePhysics(Scene &scene,double elapsed){
  if(!scene.physics)return;
  for(const auto &o:scene.objects)if(scene.physics->needsTarget(o.id))scene.physics->target(o.id,physicsPose(o),elapsed);
  scene.physics->advance(elapsed);
  auto breaks=scene.physics->takeJointBreaks();
  if(&scene==liveScene)for(const auto &broken:breaks){
    frame_input::Event event;event.type="physics.jointBreak";event.jointId=broken.jointId;event.objectId=broken.objectA;event.otherObjectId=broken.objectB;event.force=broken.force;event.torque=broken.torque;sceneInput().addEvent(std::move(event));
  }
  auto collisions=scene.physics->takeCollisionEvents();
  if(&scene==liveScene)for(const auto &collision:collisions){
    frame_input::Event event;
    event.type=collision.phase==physics::CollisionPhase::Start?"physics.collisionStart":collision.phase==physics::CollisionPhase::Stay?"physics.collisionStay":"physics.collisionEnd";
    event.objectId=collision.objectId;event.otherObjectId=collision.otherObjectId;
    event.point={collision.point.x,collision.point.y,collision.point.z};
    event.normal={collision.normal.x,collision.normal.y,collision.normal.z};
    event.penetration=collision.penetration;event.relativeSpeed=collision.relativeSpeed;
    sceneInput().addEvent(std::move(event));
  }

  const auto updates=scene.physics->takePoseUpdates();
  for(auto &o:scene.objects){auto it=std::lower_bound(updates.begin(),updates.end(),o.id,[](const auto &u,int id){return u.objectId<id;});
    if(it==updates.end()||it->objectId!=o.id)continue;
    const auto &p=it->pose;o.x=p.position.x;o.y=p.position.y;o.z=p.position.z;o.pitch=p.pitch;o.yaw=p.yaw;o.roll=p.roll;
  }
}

static JSValue jsSetObjectPosition(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float x,y,z;
  if(argc<4||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],x)||!number(ctx,argv[2],y)||!number(ctx,argv[3],z))return JS_ThrowTypeError(ctx,"setObjectPosition(id, x, y, z)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->x=x;object->y=y;object->z=z;teleportPhysics(*object);return JS_UNDEFINED;
}
static JSValue jsSetObjectYaw(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float yaw;
  if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],yaw))return JS_ThrowTypeError(ctx,"setObjectYaw(id, radians)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->yaw=yaw;teleportPhysics(*object);return JS_UNDEFINED;
}
static JSValue jsSetObjectRotation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float pitch,yaw,roll;
  if(argc<4||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],pitch)||!number(ctx,argv[2],yaw)||!number(ctx,argv[3],roll))
    return JS_ThrowTypeError(ctx,"setObjectRotation(id, pitch, yaw, roll)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->pitch=pitch;object->yaw=yaw;object->roll=roll;teleportPhysics(*object);return JS_UNDEFINED;
}
static JSValue jsSetObjectScale(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float scale;
  if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],scale))return JS_ThrowTypeError(ctx,"setObjectScale(id, meters)");
  if(scale<=0||scale>2)return JS_ThrowRangeError(ctx,"scale must be greater than 0 and at most 2 meters");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  if(currentScene->physics&&currentScene->physics->contains(id)){
    auto &w=*currentScene->physics;const auto state=w.get(id);const float old=object->scale;object->scale=scale;
    try{configurePhysics(*currentScene,*object,state.settings);if(state.settings.type==physics::Type::Dynamic&&!state.held){w.velocity(id,state.velocity);w.angularVelocity(id,state.angularVelocity);}}
    catch(const std::exception &e){object->scale=old;return JS_ThrowRangeError(ctx,"%s",e.what());}
  }else object->scale=scale;return JS_UNDEFINED;
}
static JSValue jsSetObjectVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"setObjectVisible(id, visible)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;
}
void forgetObjectInteraction(int id){
  for(int h=0;h<2;h++){
    if(meshHoveredObject[h]==id){sceneInput().addMeshEvent("overlay.meshHoverEnd",id,h,meshHitDistance[h],meshHitPoint[h]);meshHoveredObject[h]=0;meshHitPoint[h]={};}
    if(meshCurrentObject[h]==id){meshCurrentObject[h]=0;meshHitDistance[h]=2;meshLastHit[h]={};}
  }
}
static void removeSceneObject(int id){
  auto *object=findObject(id);if(!object)return;
  std::erase_if(currentScene->lights,[&](const PointLight &light){return light.objectId==id;});
  if(currentScene->physics)currentScene->physics->remove(id);
  if(currentScene==liveScene){
    forgetObjectInteraction(id);const int key=objectSurfaceKey(*object);
    const bool shared=std::any_of(currentScene->objects.begin(),currentScene->objects.end(),[&](const Object &other){return other.id!=id&&objectSurfaceKey(other)==key;});
    if(!shared&&releaseObjectOverlay)releaseObjectOverlay(key);
  }
  currentScene->objects.erase(std::remove_if(currentScene->objects.begin(),currentScene->objects.end(),[id](const Object &entry){return entry.id==id;}),currentScene->objects.end());
}
static JSValue jsRemoveObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"removeObject(id)");
  if(!findObject(id))return JS_ThrowRangeError(ctx,"unknown object id");removeSceneObject(id);return JS_UNDEFINED;
}
static JSValue jsCreateSceneContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
 int parent=0;
 if(argc&&!JS_IsUndefined(argv[0])){
  if(!JS_IsObject(argv[0])||JS_IsNull(argv[0]))return JS_ThrowTypeError(ctx,"container options must be an object");
  JSValue value=JS_GetPropertyStr(ctx,argv[0],"parent");if(JS_IsException(value))return value;
  const bool valid=JS_IsUndefined(value)||containerId(ctx,value,parent);JS_FreeValue(ctx,value);if(!valid)return JS_EXCEPTION;
 }
 if(parent&&!currentScene->containers.count(parent))return JS_ThrowRangeError(ctx,"parent container was disposed");
 if(currentScene->containers.size()>=256||currentScene->nextContainerId==INT_MAX)return JS_ThrowRangeError(ctx,"scene container limit reached");
 const int id=currentScene->nextContainerId;currentScene->containers.insert(id);currentScene->containerParents[id]=parent;++currentScene->nextContainerId;return JS_NewInt32(ctx,id);
}
static JSValue jsSetSceneContainerParent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
 int id=0,parent=0;if(argc<2)return JS_ThrowTypeError(ctx,"setSceneContainerParent(id, parent)");
 if(!containerId(ctx,argv[0],id)||!containerId(ctx,argv[1],parent))return JS_EXCEPTION;
 if(!id)return JS_ThrowRangeError(ctx,"root cannot be reparented");
 if(containerSubtree(*currentScene,id).count(parent))return JS_ThrowRangeError(ctx,"container cycle");
 currentScene->containerParents[id]=parent;return JS_UNDEFINED;
}
static JSValue jsGetSceneContainerState(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1)return JS_ThrowTypeError(ctx,"getSceneContainerState(id)");if(!containerId(ctx,argv[0],id,true))return JS_EXCEPTION;
  if(!id)return JS_ThrowRangeError(ctx,"root is not a disposable scene container");
  JSValue state=JS_NewObject(ctx),objects=JS_NewArray(ctx),panels=JS_NewArray(ctx),lights=JS_NewArray(ctx);uint32_t n=0;
  for(const auto &object:currentScene->objects)if(object.container==id)JS_SetPropertyUint32(ctx,objects,n++,JS_NewInt32(ctx,object.id));n=0;
  for(const auto &panel:currentScene->panels)if(panel.container==id)JS_SetPropertyUint32(ctx,panels,n++,JS_NewInt32(ctx,panel.id));
  n=0;for(const auto &light:currentScene->lights)if(light.container==id)JS_SetPropertyUint32(ctx,lights,n++,JS_NewInt32(ctx,light.id));
  JS_SetPropertyStr(ctx,state,"lightIds",lights);
  JSValue children=JS_NewArray(ctx);uint32_t childIndex=0;for(auto [child,parent]:currentScene->containerParents)if(parent==id)JS_SetPropertyUint32(ctx,children,childIndex++,JS_NewInt32(ctx,child));
  JS_SetPropertyStr(ctx,state,"childIds",children);JS_SetPropertyStr(ctx,state,"parentId",JS_NewInt32(ctx,currentScene->containerParents.count(id)?currentScene->containerParents.at(id):0));
  JS_SetPropertyStr(ctx,state,"id",JS_NewInt32(ctx,id));JS_SetPropertyStr(ctx,state,"disposed",JS_NewBool(ctx,!currentScene->containers.count(id)));
  JS_SetPropertyStr(ctx,state,"objectIds",objects);JS_SetPropertyStr(ctx,state,"panelIds",panels);
  JS_SetPropertyStr(ctx,state,"inputCallback",JS_NewBool(ctx,inputCallbackCtx==ctx&&inputCallbackContainer==id));return state;
}
static JSValue jsDisposeSceneContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1)return JS_ThrowTypeError(ctx,"disposeSceneContainer(id)");if(!containerId(ctx,argv[0],id,true))return JS_EXCEPTION;
  if(!id)return JS_ThrowRangeError(ctx,"root is not a disposable scene container");
  const auto owners=containerSubtree(*currentScene,id);if(owners.empty())return JS_UNDEFINED;
  for(int owner:owners){currentScene->containers.erase(owner);currentScene->containerParents.erase(owner);}
  std::erase_if(currentScene->lights,[&](const PointLight &light){return owners.count(light.container);});
  if(inputCallbackCtx==ctx&&owners.count(inputCallbackContainer))clearInputCallback(ctx);
  std::vector<int> objects;for(const auto &object:currentScene->objects)if(owners.count(object.container))objects.push_back(object.id);
  for(int object:objects)removeSceneObject(object);
  auto &panels=currentScene->panels;
  for(auto it=panels.begin();it!=panels.end();)if(owners.count(it->container)){currentScene->retiredPanels.push_back(std::move(*it));it=panels.erase(it);}else ++it;
  return JS_UNDEFINED;
}
static JSValue jsSetObjectContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int object=0,owner=0;if(argc<2||JS_ToInt32(ctx,&object,argv[0])<0)return JS_ThrowTypeError(ctx,"setObjectContainer(objectId, containerId)");
  if(!containerId(ctx,argv[1],owner))return JS_EXCEPTION;auto *entry=findObject(object);if(!entry)return JS_ThrowRangeError(ctx,"unknown object id");entry->container=owner;return JS_UNDEFINED;
}
static JSValue jsSetPanelContainer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<2)return JS_ThrowTypeError(ctx,"setPanelContainer(panel, containerId)");
  JSValue value=JS_IsObject(argv[0])?JS_GetPropertyStr(ctx,argv[0],"id"):JS_DupValue(ctx,argv[0]);if(JS_IsException(value))return value;
  int id=0;const bool ok=JS_ToInt32(ctx,&id,value)==0;JS_FreeValue(ctx,value);if(!ok)return JS_EXCEPTION;
  int owner=0;if(!containerId(ctx,argv[1],owner))return JS_EXCEPTION;
  auto *panel=findPanel(id);if(!panel)return JS_ThrowRangeError(ctx,"unknown panel");panel->container=owner;return JS_UNDEFINED;
}

#include "scene_light_api.h"
#include "scene_persistence.h"
JSValue capturePanelSnapshot(JSContext *ctx,const Panel &panel,size_t &budgetUsed){return scenePersistence::panelValue(ctx,panel,budgetUsed);}

static JSValue jsPublish(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<1||!JS_IsString(argv[0]))return JS_ThrowTypeError(ctx,"publish(topic, payload)");
  const char *text=JS_ToCString(ctx,argv[0]);if(!text)return JS_EXCEPTION;std::string topic=text;JS_FreeCString(ctx,text);
  if(!bus::scriptPublishable(topic))return JS_ThrowRangeError(ctx,"publish topics are space.* names of at most 128 bytes without *");
  std::string error;std::string payload=argc>1&&!JS_IsUndefined(argv[1])?bus::encodePayload(ctx,argv[1],error):"null";
  if(!error.empty())return JS_ThrowTypeError(ctx,"%s",error.c_str());
  bus::Message message{0,std::move(topic),std::move(payload),activeInstanceId};
  if(currentScene!=liveScene){
    // A loading candidate publishes only if it commits.
    auto &staged=currentScene->stagedPublications;size_t bytes=message.bytes();for(const auto &m:staged)bytes+=m.bytes();
    if(!eventBus.hasRoom(message.source,bytes,staged.size())){eventBus.countDrop();return JS_NULL;}
    message.sequence=eventBus.reserveSequence();const double sequence=double(message.sequence);staged.push_back(std::move(message));return JS_NewFloat64(ctx,sequence);
  }
  const auto sequence=eventBus.publish(message.source,std::move(message.topic),std::move(message.payload));
  return sequence?JS_NewFloat64(ctx,double(*sequence)):JS_NULL;
}
static JSValue jsSubscribe(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<2||!JS_IsString(argv[0])||!JS_IsFunction(ctx,argv[1]))return JS_ThrowTypeError(ctx,"subscribe(topic, callback)");
  const char *text=JS_ToCString(ctx,argv[0]);if(!text)return JS_EXCEPTION;std::string topic=text;JS_FreeCString(ctx,text);
  if(!bus::subscribable(topic))return JS_ThrowRangeError(ctx,"subscribe topics are \"*\" or space.* and engine.* names of at most 128 bytes");
  const uint64_t id=eventBus.newSubscriptionId();
  currentScene->busSubscriptions.push_back({id,std::move(topic),JS_DupValue(ctx,argv[1])});
  return JS_NewFloat64(ctx,double(id));
}
static JSValue jsUnsubscribe(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  double id=0;if(!currentScene||argc<1||JS_ToFloat64(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"unsubscribe(subscriptionId)");
  auto &subscriptions=currentScene->busSubscriptions;
  const auto found=std::find_if(subscriptions.begin(),subscriptions.end(),[&](const bus::Subscription &s){return double(s.id)==id;});
  if(found==subscriptions.end())return JS_FALSE;
  JS_FreeValue(ctx,found->callback);subscriptions.erase(found);return JS_TRUE;
}
// Scratch's scripts object. Queries return the control protocol's results; start,
// stop and reload are checked now, applied between frames, and return undefined.
enum ScratchOp {ScriptsCatalog,ScriptsList,ScriptsStatus,ScriptsStart,ScriptsStop,ScriptsReload};
// An Error carrying the control protocol's error code.
static JSValue throwCoded(JSContext *ctx,JSValue code,const char *message){
  JSValue error=JS_NewError(ctx);
  JS_SetPropertyStr(ctx,error,"message",JS_NewString(ctx,message));JS_SetPropertyStr(ctx,error,"code",code);
  return JS_Throw(ctx,error);
}
static JSValue jsScripts(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv,int op){
  static const char *names[]={"catalog","list","status","start","stop","reload"};
  static const char *usage[]={"scripts.catalog()","scripts.list()","scripts.status(instanceId)","scripts.start(name)","scripts.stop(instanceId, generation)","scripts.reload(instanceId, generation)"};
  std::string request=std::string("{\"v\":1,\"op\":\"")+names[op]+"\"";
  auto integer=[&](int index,const char *name)->bool{
    double value=0;
    if(argc<=index||!JS_IsNumber(argv[index])||JS_ToFloat64(ctx,&value,argv[index])<0||!std::isfinite(value)||value<0||value!=std::floor(value)||value>9007199254740991.)return false;
    request+=std::string(",\"")+name+"\":"+std::to_string(uint64_t(value));return true;
  };
  bool valid=true;
  if(op==ScriptsStatus)valid=integer(0,"instanceId");
  else if(op==ScriptsStop||op==ScriptsReload)valid=integer(0,"instanceId")&&integer(1,"generation");
  else if(op==ScriptsStart){
    const char *name=argc&&JS_IsString(argv[0])?JS_ToCString(ctx,argv[0]):nullptr;
    if(name){request+=",\"name\":"+bus::jsonText(name);JS_FreeCString(ctx,name);}else valid=false;
  }
  if(!valid)return JS_ThrowTypeError(ctx,"%s",usage[op]);
  if(!scratchControl)return throwCoded(ctx,JS_NewString(ctx,"unavailable"),"scripts management is unavailable");
  const std::string reply=scratchControl(request+"}");
  JSValue response=JS_ParseJSON(ctx,reply.c_str(),reply.size(),"scripts");
  if(JS_IsException(response))return response;
  JSValue okValue=JS_GetPropertyStr(ctx,response,"ok");const bool ok=JS_ToBool(ctx,okValue)>0;JS_FreeValue(ctx,okValue);
  if(!ok){
    JSValue failure=JS_GetPropertyStr(ctx,response,"error");JS_FreeValue(ctx,response);
    JSValue message=JS_GetPropertyStr(ctx,failure,"message");const char *text=JS_ToCString(ctx,message);
    JSValue thrown=throwCoded(ctx,JS_GetPropertyStr(ctx,failure,"code"),text?text:"scripts request failed");
    if(text)JS_FreeCString(ctx,text);JS_FreeValue(ctx,message);JS_FreeValue(ctx,failure);
    return thrown;
  }
  JSValue result=JS_GetPropertyStr(ctx,response,"result");JS_FreeValue(ctx,response);
  if(op>=ScriptsStart){JS_FreeValue(ctx,result);return JS_UNDEFINED;}
  if(op==ScriptsStatus)return result;
  JSValue list=JS_GetPropertyStr(ctx,result,op==ScriptsCatalog?"scripts":"instances");JS_FreeValue(ctx,result);return list;
}
static void bindScripts(JSContext *ctx){
  static const JSCFunctionListEntry functions[]={
    JS_CFUNC_MAGIC_DEF("catalog",0,jsScripts,ScriptsCatalog),JS_CFUNC_MAGIC_DEF("list",0,jsScripts,ScriptsList),
    JS_CFUNC_MAGIC_DEF("status",1,jsScripts,ScriptsStatus),JS_CFUNC_MAGIC_DEF("start",1,jsScripts,ScriptsStart),
    JS_CFUNC_MAGIC_DEF("stop",2,jsScripts,ScriptsStop),JS_CFUNC_MAGIC_DEF("reload",2,jsScripts,ScriptsReload),
  };
  JSValue global=JS_GetGlobalObject(ctx),scripts=JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx,scripts,functions,int(std::size(functions)));
  JS_SetPropertyStr(ctx,global,"scripts",scripts);JS_FreeValue(ctx,global);
}
static void bind(JSContext *ctx,const char *name,JSCFunction *function,int argc){
  JSValue global=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global,name,JS_NewCFunction(ctx,function,name,argc));JS_FreeValue(ctx,global);
}
void closeScript(ScriptHost &s){
  for(auto &asset:s.scene.assets)deleteModel(asset);
  for(auto &panel:s.scene.panels)panelDestroy(panel);
  for(auto &panel:s.scene.retiredPanels)panelDestroy(panel);
  if(currentScene==&s.scene)currentScene=nullptr;
  if(liveScene==&s.scene)liveScene=nullptr;
  if(s.ctx)for(auto &subscription:s.scene.busSubscriptions)JS_FreeValue(s.ctx,subscription.callback);
  s.scene.busSubscriptions.clear();
  if(s.ctx){clearInputCallback(s.ctx);JS_FreeValue(s.ctx,s.callback);JS_FreeValue(s.ctx,s.update);JS_FreeContext(s.ctx);}if(s.rt)JS_FreeRuntime(s.rt);
  if(activeSceneInput==s.inputQueue.get())activeSceneInput=&frameInput;
  // The host keeps its identity, so a later restart loads as the same instance.
  const int instanceId=s.instanceId;std::string saveNamespace=std::move(s.saveNamespace);
  s={};s.instanceId=instanceId;s.saveNamespace=std::move(saveNamespace);
}
static int interruptEvaluation(JSContext*,void*){
  if(!evaluation::deadline||std::chrono::steady_clock::now()<*evaluation::deadline)return 0;
  evaluation::interrupted=true;return 1;
}
bool loadScript(ScriptHost &active,const std::string &path,const std::function<void()> &beforeCommit){
  std::ifstream file(path);if(!file){fprintf(stderr,"script unreadable: %s\n",path.c_str());return false;}
  std::ostringstream stream;stream<<file.rdbuf();
  return loadScriptSource(active,path,stream.str(),beforeCommit);
}
bool loadScriptSource(ScriptHost &active,const std::string &path,const std::string &source,const std::function<void()> &beforeCommit){
  ScriptHost candidate;candidate.rt=JS_NewRuntime();if(!candidate.rt)return false;
  JS_SetMemoryLimit(candidate.rt,128*1024*1024);
  JS_SetInterruptHandler(candidate.rt,interruptEvaluation,nullptr);
  candidate.ctx=JS_NewContext(candidate.rt);if(!candidate.ctx){closeScript(candidate);return false;}
  currentScene=&candidate.scene;
  JSContext *previousCallbackCtx=inputCallbackCtx;JSValue previousCallback=inputCallback;const int previousCallbackContainer=inputCallbackContainer;inputCallbackCtx=nullptr;inputCallback=JS_UNDEFINED;inputCallbackContainer=0;
  bind(candidate.ctx,"print",jsLog,1);
  bind(candidate.ctx,"getController",jsGetController,1);
  bind(candidate.ctx,"getControllerInput",jsGetControllerInput,1);
  bind(candidate.ctx,"getEyeTracking",jsGetEyeTracking,0);
  bind(candidate.ctx,"getCalibratedEyeTracking",jsGetCalibratedEyeTracking,0);
  bind(candidate.ctx,"saveEyeTrackingCalibration",jsSaveEyeCalibration,1);
  bind(candidate.ctx,"getEyeTrackingCalibration",jsGetEyeCalibration,0);
  bind(candidate.ctx,"getEyeFoveationCenters",jsGetEyeFoveationCenters,0);
  bind(candidate.ctx,"getFingerTracking",jsGetFingerTracking,1);
  bind(candidate.ctx,"pollInputEvents",jsPollInputEvents,0);
  bind(candidate.ctx,"onInputEvent",jsOnInputEvent,1);
  bind(candidate.ctx,"getInput",jsGetInput,1);
  bind(candidate.ctx,"pulse",jsPulse,3);
  bind(candidate.ctx,"setLaserVisible",jsSetLaserVisible,2);
  bind(candidate.ctx,"enableObjectInteraction",jsEnableObjectInteraction,2);
  bind(candidate.ctx,"setObjectInteraction",jsSetObjectInteraction,2);
  bind(candidate.ctx,"getLaserVisible",jsGetLaserVisible,1);
  bind(candidate.ctx,"getMeshHover",jsGetMeshHover,1);
  bind(candidate.ctx,"getEngineStats",jsGetEngineStats,0);
  bind(candidate.ctx,"setRenderGroupAutoSplit",jsSetRenderGroupAutoSplit,2);
  bind(candidate.ctx,"setObjectPhysics",jsSetObjectPhysics,2);
  bind(candidate.ctx,"setPhysicsJointMotor",jsSetPhysicsJointMotor,2);
  bind(candidate.ctx,"setPhysicsJoint",jsSetPhysicsJoint,2);
  bind(candidate.ctx,"createPhysicsJoint",jsCreatePhysicsJoint,3);
  bind(candidate.ctx,"getPhysicsJoint",jsPhysicsJoint<false>,1);
  bind(candidate.ctx,"removePhysicsJoint",jsPhysicsJoint<true>,1);
  bind(candidate.ctx,"getObjectPhysics",jsGetObjectPhysics,1);
  bind(candidate.ctx,"removeObjectPhysics",jsRemoveObjectPhysics,1);
  bind(candidate.ctx,"setPhysicsWorkers",jsSetPhysicsWorkers,1);
  bind(candidate.ctx,"setPhysicsGravity",jsSetPhysicsGravity,3);
  bind(candidate.ctx,"setPhysicsCollisionEvents",jsSetPhysicsCollisionEvents,2);
  bind(candidate.ctx,"setObjectVelocity",jsPhysicsVectorOperation<0>,4);
  bind(candidate.ctx,"setObjectAngularVelocity",jsPhysicsVectorOperation<1>,4);
  bind(candidate.ctx,"applyObjectImpulse",jsPhysicsVectorOperation<2>,4);
  bind(candidate.ctx,"getAnimations",jsGetAnimations,1);
  bind(candidate.ctx,"playAnimation",jsPlayAnimation,3);
  bind(candidate.ctx,"pauseAnimation",jsPauseAnimation,1);
  bind(candidate.ctx,"resumeAnimation",jsResumeAnimation,1);
  bind(candidate.ctx,"stopAnimation",jsStopAnimation,1);
  bind(candidate.ctx,"seekAnimation",jsSeekAnimation,2);
  bind(candidate.ctx,"getAnimationState",jsGetAnimationState,1);
  bind(candidate.ctx,"createPanel",jsCreatePanel,1);
  bind(candidate.ctx,"panelLoadFont",jsPanelSetFont,2);
  bind(candidate.ctx,"panelSetTransform",jsPanelSetTransform,5);
  bind(candidate.ctx,"panelSetRotation",jsPanelSetRotation,10);
  bind(candidate.ctx,"panelSetVisible",jsPanelVisible,2);
  bind(candidate.ctx,"removePanel",jsPanelRemove,1);
  bind(candidate.ctx,"loadModel",jsLoadModel,1);
  bind(candidate.ctx,"setModelLighting",jsSetModelLighting,2);
  bind(candidate.ctx,"createDirectionalLight",jsCreateDirectionalLight,1);
  bind(candidate.ctx,"setDirectionalLight",jsSetDirectionalLight,2);
  bind(candidate.ctx,"getDirectionalLight",jsGetDirectionalLight,1);
  bind(candidate.ctx,"removeDirectionalLight",jsRemoveDirectionalLight,1);
  bind(candidate.ctx,"createPointLight",jsCreatePointLight,1);
  bind(candidate.ctx,"createSpotLight",jsCreateSpotLight,1);
  bind(candidate.ctx,"setSpotLight",jsSetSpotLight,2);
  bind(candidate.ctx,"getSpotLight",jsGetSpotLight,1);
  bind(candidate.ctx,"removeSpotLight",jsRemoveSpotLight,1);
  bind(candidate.ctx,"setPointLight",jsSetPointLight,2);
  bind(candidate.ctx,"getPointLight",jsGetPointLight,1);
  bind(candidate.ctx,"removePointLight",jsRemovePointLight,1);
  bind(candidate.ctx,"createObject",jsCreateObject,6);
  bind(candidate.ctx,"setObjectPosition",jsSetObjectPosition,4);
  bind(candidate.ctx,"setObjectYaw",jsSetObjectYaw,2);
  bind(candidate.ctx,"setObjectRotation",jsSetObjectRotation,4);
  bind(candidate.ctx,"setObjectScale",jsSetObjectScale,2);
  bind(candidate.ctx,"setObjectVisible",jsSetObjectVisible,2);
  bind(candidate.ctx,"removeObject",jsRemoveObject,1);
  bind(candidate.ctx,"captureSceneContainer",jsCaptureSceneContainer,1);
  bind(candidate.ctx,"restoreSceneContainer",jsRestoreSceneContainer,1);
  bind(candidate.ctx,"saveSceneContainer",jsSaveSceneContainer,2);
  bind(candidate.ctx,"loadSceneContainer",jsLoadSceneContainer,1);
  bind(candidate.ctx,"importSceneContainer",jsImportSceneContainer,2);
  bind(candidate.ctx,"createSceneContainer",jsCreateSceneContainer,1);
  bind(candidate.ctx,"setSceneContainerParent",jsSetSceneContainerParent,2);
  bind(candidate.ctx,"unloadModel",jsUnloadModel,1);
  bind(candidate.ctx,"disposeSceneContainer",jsDisposeSceneContainer,1);
  bind(candidate.ctx,"getSceneContainerState",jsGetSceneContainerState,1);
  bind(candidate.ctx,"setObjectContainer",jsSetObjectContainer,2);
  bind(candidate.ctx,"setPanelContainer",jsSetPanelContainer,2);
  bind(candidate.ctx,"publish",jsPublish,2);
  bind(candidate.ctx,"subscribe",jsSubscribe,2);
  bind(candidate.ctx,"unsubscribe",jsUnsubscribe,1);
  // Only the scratch scene manages other scripts.
  if(active.instanceId==0)bindScripts(candidate.ctx);
  JSValue result=JS_Eval(candidate.ctx,source.c_str(),source.size(),path.c_str(),JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(candidate.ctx);const char *message=JS_ToCString(candidate.ctx,exception);
    fprintf(stderr,"[js] load error: %s\n",message?message:"unknown");if(message)JS_FreeCString(candidate.ctx,message);
    JS_FreeValue(candidate.ctx,exception);JS_FreeValue(candidate.ctx,result);closeScript(candidate);inputCallbackCtx=previousCallbackCtx;inputCallback=previousCallback;inputCallbackContainer=previousCallbackContainer;currentScene=&active.scene;return false;
  }
  JS_FreeValue(candidate.ctx,result);
  JSValue global=JS_GetGlobalObject(candidate.ctx);candidate.update=JS_GetPropertyStr(candidate.ctx,global,"update");JS_FreeValue(candidate.ctx,global);
  std::error_code ec;if(std::filesystem::is_regular_file(path,ec))candidate.mtime=std::filesystem::last_write_time(path,ec);
  if(previousCallbackCtx)JS_FreeValue(previousCallbackCtx,previousCallback);
  if(beforeCommit)beforeCommit();
  const int instanceId=active.instanceId;std::string saveNamespace=std::move(active.saveNamespace);
  const bool queueActive=activeSceneInput==active.inputQueue.get();
  closeScript(active);active=std::move(candidate);active.instanceId=instanceId;active.saveNamespace=std::move(saveNamespace);
  if(queueActive)activeSceneInput=active.inputQueue.get();
  currentScene=liveScene=&active.scene;
  // Committed: the candidate's subscriptions replaced the old ones with the scene.
  for(auto &message:active.scene.stagedPublications)eventBus.enqueue(std::move(message));
  active.scene.stagedPublications.clear();
  fprintf(stdout,"[js] loaded %s objects=%zu assets=%zu\n",path.c_str(),active.scene.objects.size(),active.scene.assets.size());fflush(stdout);return true;
}
