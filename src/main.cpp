#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <openvr.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <utility>
#include <functional>
#include <quickjs.h>
#include "model_asset.h"
#include "model_shaders.h"
#include "controller_input.h"
#include "frame_input.h"
#include "panel.h"
#include "eye_calibration.h"
#include "scope_exit.h"
#include "physics_world.h"
#include "runtime_stats.h"

constexpr int kDefaultEyePixels=768;
struct Buffer {gbm_bo *bo=nullptr;int fd=-1;vr::SharedTextureHandle_t handle=0;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint color=0,depth=0,fbo=0;bool ownsDepth=false;};
struct Surface {int objectId=0,eyePixels=kDefaultEyePixels;float width=.7f;vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid;Buffer buffers[3];bool shown=false,interactive=false,panel=false;};
static PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC imageRenderbuffer=nullptr;
static std::atomic<bool> running{true};
enum class GrabButton { Trigger, Grip, Both };
struct Object {
  int id,asset;float x,y,z,scale,yaw,pitch=0,roll=0;
  bool visible=true,laserPassThrough=true,grabbable=false,directGrab=true,autoManipulate=false;
  GrabButton grabButton=GrabButton::Trigger;
  bool laserYaw=false,laserPitchWithStickClick=false,depthAdjust=false;
  bool handPitch=false,handYaw=false,handRoll=false;
  animation::Playback animation;
  std::shared_ptr<ModelPose> animationPose;
};
struct Scene {std::unique_ptr<physics::World> physics;std::vector<Object> objects;std::vector<ModelAsset> assets;std::vector<Panel> panels,retiredPanels;std::vector<int> interactiveIds;int nextId=1,nextPanelId=1;bool laserVisible[2]{};bool eyeTrackingRequested=false,foveationRequested=false;};
static Scene *currentScene=nullptr,*liveScene=nullptr;
static diagnostics::Sampler engineStats;
struct ControllerSnapshot {bool valid=false,connected=false;float x=0,y=0,z=0;float rotation[9]{};};
static ControllerSnapshot controllers[2];
static vr::HmdMatrix34_t trackingAnchor{};static bool trackingAnchorValid=false;
static controller_input::Input controllerInput;
static frame_input::Monitor frameInput;
static bool inputPollingRequested=false;static int hoveredObject[2]={0,0},meshHoveredObject[2]={0,0},meshCurrentObject[2]={0,0};static float meshHitDistance[2]={2.f,2.f};
static std::chrono::steady_clock::time_point meshLastHit[2]{};
struct PointerSnapshot {bool valid=false,down=false,secondaryDown=false;float x=0,y=0,scrollX=0,scrollY=0;int hand=-1;};
static std::map<int,PointerSnapshot> pointerStates;
static JSContext *inputCallbackCtx=nullptr;static JSValue inputCallback=JS_UNDEFINED;
static void clearInputCallback(JSContext *ctx){if(inputCallbackCtx==ctx){JS_FreeValue(ctx,inputCallback);inputCallback=JS_UNDEFINED;inputCallbackCtx=nullptr;}}
static void stop(int){running=false;}
static JSValue inputEventValue(JSContext *ctx,const frame_input::Event &event){JSValue item=JS_NewObject(ctx);JS_SetPropertyStr(ctx,item,"type",JS_NewString(ctx,event.type.c_str()));JS_SetPropertyStr(ctx,item,"hand",JS_NewString(ctx,event.hand.c_str()));JS_SetPropertyStr(ctx,item,"control",JS_NewString(ctx,event.control.c_str()));JS_SetPropertyStr(ctx,item,"objectId",JS_NewInt32(ctx,event.objectId));JS_SetPropertyStr(ctx,item,"x",JS_NewFloat64(ctx,event.x));JS_SetPropertyStr(ctx,item,"y",JS_NewFloat64(ctx,event.y));JS_SetPropertyStr(ctx,item,"down",JS_NewBool(ctx,event.component.boolean));JS_SetPropertyStr(ctx,item,"value",JS_NewFloat64(ctx,event.component.value));JS_SetPropertyStr(ctx,item,"dx",JS_NewFloat64(ctx,event.dx));JS_SetPropertyStr(ctx,item,"dy",JS_NewFloat64(ctx,event.dy));JS_SetPropertyStr(ctx,item,"dz",JS_NewFloat64(ctx,event.dz));JS_SetPropertyStr(ctx,item,"dpitch",JS_NewFloat64(ctx,event.dpitch));JS_SetPropertyStr(ctx,item,"dyaw",JS_NewFloat64(ctx,event.dyaw));JS_SetPropertyStr(ctx,item,"droll",JS_NewFloat64(ctx,event.droll));if(event.type=="physics.jointBreak"){
  JS_SetPropertyStr(ctx,item,"jointId",JS_NewInt32(ctx,event.jointId));JS_SetPropertyStr(ctx,item,"otherObjectId",JS_NewInt32(ctx,event.otherObjectId));
  JS_SetPropertyStr(ctx,item,"force",JS_NewFloat64(ctx,event.force));JS_SetPropertyStr(ctx,item,"torque",JS_NewFloat64(ctx,event.torque));
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
  const auto perf=currentScene->physics?currentScene->physics->performance():physics::Performance{};
  JS_SetPropertyStr(ctx,value,"physicsAdvanceMs",JS_NewFloat64(ctx,perf.advanceMs));
  JS_SetPropertyStr(ctx,value,"physicsSolverMs",JS_NewFloat64(ctx,perf.solverMs));
  JS_SetPropertyStr(ctx,value,"physicsSteps",JS_NewUint32(ctx,perf.steps));
  JS_SetPropertyStr(ctx,value,"physicsActiveBodies",JS_NewUint32(ctx,perf.activeBodies));
  JS_SetPropertyStr(ctx,value,"physicsWorkers",JS_NewUint32(ctx,perf.workers));
  return value;
}
static JSValue jsLog(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc){const char *s=JS_ToCString(ctx,argv[0]);if(s){fprintf(stdout,"[js] %s\n",s);JS_FreeCString(ctx,s);}}
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
static tracking::EyeCalibration eyeCalibration;
static std::string calibrationOutput;
static float headBasis[3][3]{};static bool headBasisValid=false;
static bool parseCalibration(JSContext *ctx,JSValueConst value,tracking::EyeCalibration &result){
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
static JSValue jsOnInputEvent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(argc<1||!JS_IsFunction(ctx,argv[0]))return JS_ThrowTypeError(ctx,"onInputEvent(callback)");if(inputCallbackCtx&&inputCallbackCtx!=ctx)clearInputCallback(inputCallbackCtx);else clearInputCallback(ctx);inputCallbackCtx=ctx;inputCallback=JS_DupValue(ctx,argv[0]);return JS_UNDEFINED;}
static JSValue jsPollInputEvents(JSContext *ctx,JSValueConst,int,JSValueConst *){
  JSValue array=JS_NewArray(ctx);if(currentScene!=liveScene)return array;
  auto events=frameInput.takeEvents();uint32_t i=0;
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
static JSValue jsSetLaserVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<2)return JS_ThrowTypeError(ctx,"setLaserVisible(hand, visible)");
  const char *hand=JS_ToCString(ctx,argv[0]);if(!hand)return JS_EXCEPTION;
  int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;JS_FreeCString(ctx,hand);
  if(index<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  currentScene->laserVisible[index]=JS_ToBool(ctx,argv[1])>0;
  return JS_UNDEFINED;
}
static JSValue jsGetPointer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc>0&&JS_ToInt32(ctx,&id,argv[0])<0)return JS_EXCEPTION;
  if(argc==0&&currentScene&&currentScene->interactiveIds.size()==1)id=currentScene->interactiveIds.front();
  const auto found=pointerStates.find(id);
  PointerSnapshot state=currentScene!=liveScene||found==pointerStates.end()?PointerSnapshot{}:found->second;
  if(currentScene)for(const auto &object:currentScene->objects)if(object.id==id&&!object.grabbable)state.down=false;
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));
  JS_SetPropertyStr(ctx,object,"down",JS_NewBool(ctx,state.down));
  JS_SetPropertyStr(ctx,object,"x",JS_NewFloat64(ctx,state.x));
  JS_SetPropertyStr(ctx,object,"y",JS_NewFloat64(ctx,state.y));
  JS_SetPropertyStr(ctx,object,"hand",state.hand==0?JS_NewString(ctx,"left"):state.hand==1?JS_NewString(ctx,"right"):JS_NULL);
  return object;
}
static JSValue jsPanelClear(JSContext *,JSValueConst,int,JSValueConst*);static JSValue jsPanelText(JSContext *,JSValueConst,int,JSValueConst*);static JSValue jsPanelPresent(JSContext *,JSValueConst,int,JSValueConst*);
static Panel *findPanel(int id){if(!currentScene)return nullptr;for(auto &panel:currentScene->panels)if(panel.id==id)return &panel;return nullptr;}
static JSValue panelObject(JSContext *ctx,int id){JSValue object=JS_NewObject(ctx);JS_SetPropertyStr(ctx,object,"id",JS_NewInt32(ctx,id));return object;}
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
static JSValue jsCreatePanel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(!currentScene||argc<1)return JS_ThrowTypeError(ctx,"createPanel(options)");if(!JS_IsObject(argv[0]))return JS_ThrowTypeError(ctx,"createPanel options must be an object");int w=1024,h=512;double wm=1.2;JSValue f=JS_GetPropertyStr(ctx,argv[0],"widthPixels");if(!JS_IsUndefined(f))JS_ToInt32(ctx,&w,f);JS_FreeValue(ctx,f);f=JS_GetPropertyStr(ctx,argv[0],"heightPixels");if(!JS_IsUndefined(f))JS_ToInt32(ctx,&h,f);JS_FreeValue(ctx,f);f=JS_GetPropertyStr(ctx,argv[0],"widthMeters");if(!JS_IsUndefined(f))JS_ToFloat64(ctx,&wm,f);JS_FreeValue(ctx,f);Panel panel;if(w<1||h<1||w>4096||h>4096||wm<=0||wm>10)return JS_ThrowRangeError(ctx,"invalid panel dimensions");if(!panelInit(panel,currentScene->nextPanelId++,w,h,(float)wm,(float)(wm*h/w),""))return JS_ThrowInternalError(ctx,"panel initialization failed");JSValue interactive=JS_GetPropertyStr(ctx,argv[0],"interactive");panel.interactive=JS_ToBool(ctx,interactive)>0;JS_FreeValue(ctx,interactive);currentScene->panels.push_back(std::move(panel));int id=currentScene->panels.back().id;JSValue object=panelObject(ctx,id);JSValue canvas=JS_NewObject(ctx);JS_SetPropertyStr(ctx,canvas,"id",JS_NewInt32(ctx,id));JS_SetPropertyStr(ctx,canvas,"clear",JS_NewCFunction(ctx,jsPanelMethodClear,"clear",5));JS_SetPropertyStr(ctx,canvas,"text",JS_NewCFunction(ctx,jsPanelMethodText,"text",8));JS_SetPropertyStr(ctx,canvas,"rect",JS_NewCFunction(ctx,jsPanelMethodRect,"rect",9));JS_SetPropertyStr(ctx,canvas,"line",JS_NewCFunction(ctx,jsPanelMethodLine,"line",8));JS_SetPropertyStr(ctx,canvas,"circle",JS_NewCFunction(ctx,jsPanelMethodCircle,"circle",8));JS_SetPropertyStr(ctx,canvas,"image",JS_NewCFunction(ctx,jsPanelMethodImage,"image",3));JS_SetPropertyStr(ctx,canvas,"present",JS_NewCFunction(ctx,jsPanelMethodPresent,"present",0));JS_SetPropertyStr(ctx,object,"canvas",canvas);JS_SetPropertyStr(ctx,object,"present",JS_NewCFunction(ctx,jsPanelMethodPresent,"present",0));return object;}
static JSValue jsPanelClear(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<1)return JS_ThrowTypeError(ctx,"panel.clear(id,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");int r=argc>1?colorPart(ctx,argv[1],0):0,g=argc>2?colorPart(ctx,argv[2],0):0,b=argc>3?colorPart(ctx,argv[3],0):0,a=argc>4?colorPart(ctx,argv[4],255):255;panelClear(*p,r,g,b,a);return JS_UNDEFINED;}
static JSValue jsPanelSetFont(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"panelLoadFont(panel,path,pixels)");id=panelArgumentId(ctx,argv[0]);auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*path=JS_ToCString(ctx,argv[1]);if(!path)return JS_EXCEPTION;p->fontPath=path;if(argc>2)JS_ToInt32(ctx,&p->fontPixels,argv[2]);JS_FreeCString(ctx,path);return JS_UNDEFINED;}
static JSValue jsPanelText(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED),x=0,y=0;if(argc<4||JS_ToInt32(ctx,&x,argv[1])<0||JS_ToInt32(ctx,&y,argv[2])<0)return JS_ThrowTypeError(ctx,"panel.text(id,x,y,text,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*t=JS_ToCString(ctx,argv[3]);if(!t)return JS_EXCEPTION;int r=argc>4?colorPart(ctx,argv[4],255):255,g=argc>5?colorPart(ctx,argv[5],255):255,b=argc>6?colorPart(ctx,argv[6],255):255,a=argc>7?colorPart(ctx,argv[7],255):255;bool ok=panelText(*p,x,y,t,r,g,b,a,argc>8?colorPart(ctx,argv[8],p->fontPixels):p->fontPixels);JS_FreeCString(ctx,t);return JS_NewBool(ctx,ok);}
static JSValue jsPanelSetTransform(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<5)return JS_ThrowTypeError(ctx,"panel.setTransform(id,mode,x,y,z)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*mode=JS_ToCString(ctx,argv[1]);if(mode){p->mode=mode;JS_FreeCString(ctx,mode);}double x=0,y=0,z=0;if(JS_ToFloat64(ctx,&x,argv[2])<0||JS_ToFloat64(ctx,&y,argv[3])<0||JS_ToFloat64(ctx,&z,argv[4])<0)return JS_EXCEPTION;p->x=(float)x;p->y=(float)y;p->z=(float)z;return JS_UNDEFINED;}
static JSValue jsPanelSetRotation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<10)return JS_ThrowTypeError(ctx,"panel.setRotation(panel,pitch,yaw,roll,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");for(int i=0;i<9;i++){double value=0;if(JS_ToFloat64(ctx,&value,argv[i+1])<0)return JS_EXCEPTION;p->rotation[i]=(float)value;}return JS_UNDEFINED;}
static JSValue jsPanelRemove(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(argc<1)return JS_ThrowTypeError(ctx,"removePanel(panel)");int id=panelArgumentId(ctx,argv[0]);auto &panels=currentScene->panels;auto it=std::find_if(panels.begin(),panels.end(),[id](const Panel &p){return p.id==id;});if(it==panels.end())return JS_ThrowRangeError(ctx,"unknown panel");currentScene->retiredPanels.push_back(std::move(*it));panels.erase(it);return JS_UNDEFINED;}
static JSValue jsPanelVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"panelSetVisible(panel,visible)");id=panelArgumentId(ctx,argv[0]);auto*p=findPanel(id);if(p)p->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;}
static JSValue jsPanelPresent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<1)return JS_EXCEPTION;auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");p->dirty=true;return JS_UNDEFINED;}
static Object *findObject(int id){
  if(!currentScene)return nullptr;
  for(auto &object:currentScene->objects)if(object.id==id)return &object;
  return nullptr;
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
  JS_SetPropertyStr(ctx,value,"status",JS_NewString(ctx,!clip?"stopped":state.playing?"playing":state.finished?"finished":"paused"));
  return value;
}
static Object *animationObject(JSContext *ctx,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0){JS_ThrowTypeError(ctx,"animation control requires an object id");return nullptr;}
  auto *object=findObject(id);if(!object)JS_ThrowRangeError(ctx,"unknown object id");return object;
}
static JSValue jsGetAnimations(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||id<1||size_t(id)>currentScene->assets.size())return JS_ThrowRangeError(ctx,"getAnimations(assetId) requires a loaded asset");
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
  animation::Playback state;state.clip=index;state.playing=true;
  if(argc>2&&!JS_IsUndefined(argv[2])){
    if(!JS_IsObject(argv[2])||JS_IsNull(argv[2]))return JS_ThrowTypeError(ctx,"animation options must be an object");
    JSValue loop=JS_GetPropertyStr(ctx,argv[2],"loop");if(JS_IsException(loop))return JS_EXCEPTION;
    if(!JS_IsUndefined(loop)&&!JS_IsBool(loop)){JS_FreeValue(ctx,loop);return JS_ThrowTypeError(ctx,"loop must be boolean");}
    if(!JS_IsUndefined(loop))state.loop=JS_ToBool(ctx,loop)>0;JS_FreeValue(ctx,loop);
    auto read=[&](const char *name,double &target){JSValue value=JS_GetPropertyStr(ctx,argv[2],name);if(JS_IsException(value))return false;bool ok=JS_IsUndefined(value)||(JS_IsNumber(value)&&JS_ToFloat64(ctx,&target,value)==0&&std::isfinite(target));JS_FreeValue(ctx,value);return ok;};
    if(!read("speed",state.speed)||!read("time",state.time))return JS_ThrowTypeError(ctx,"speed and time must be finite numbers");
  }
  if(state.speed<=0||state.speed>100||state.time<0||state.time>clip.duration)return JS_ThrowRangeError(ctx,"speed must be in (0,100] and time within the clip");
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
  state.time=time;state.finished=false;evaluateModelPose(asset,state,*object->animationPose);return animationState(ctx,*object);
}
static float objectRadius(const Object &object){return object.animationPose?std::max(1.f,object.animationPose->radius):1.f;}
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
  object->grabButton=button;object->laserPassThrough=false;object->grabbable=true;
  // Keep the legacy one-argument helper available to scripts doing their own movement.
  if(options)object->autoManipulate=true;
  auto &ids=currentScene->interactiveIds;
  if(std::find(ids.begin(),ids.end(),id)==ids.end()){
    ids.push_back(id);
  }
  return JS_UNDEFINED;
}
static JSValue jsEnableObjectSurface(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||!findObject(id))return JS_ThrowRangeError(ctx,"enableObjectSurface(objectId) requires a live object");
  // Kept for existing scripts: every object now owns a full-sphere surface.
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
  object->autoManipulate=object->grabbable;
  if(object->grabbable)object->laserPassThrough=false;
  auto &ids=currentScene->interactiveIds;
  ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
  if(!object->laserPassThrough)ids.push_back(id);
  return JS_UNDEFINED;
}
static bool number(JSContext *ctx,JSValueConst value,float &out){
  double n=0;if(JS_ToFloat64(ctx,&n,value)<0||!std::isfinite(n))return false;out=(float)n;return std::isfinite(out);
}
static JSValue jsLoadModel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<1||currentScene->assets.size()>=32)return JS_ThrowRangeError(ctx,"loadModel(path) requires a path; limit is 32 assets");
  const char *path=JS_ToCString(ctx,argv[0]);if(!path)return JS_EXCEPTION;
  std::string name(path);JS_FreeCString(ctx,path);
  int eyePixels=kDefaultEyePixels;
  if(argc>1&&JS_ToInt32(ctx,&eyePixels,argv[1])<0)return JS_EXCEPTION;
  if(eyePixels<256||eyePixels>2048||eyePixels%64!=0)return JS_ThrowRangeError(ctx,"eyePixels must be a multiple of 64 from 256 to 2048");
  for(size_t i=0;i<currentScene->assets.size();i++)if(currentScene->assets[i].path==name){
    if(argc>1&&currentScene->assets[i].eyePixels!=eyePixels)return JS_ThrowRangeError(ctx,"model already loaded at a different eyePixels resolution");
    return JS_NewInt32(ctx,(int)i+1);
  }
  ModelAsset asset;
  if(!loadModelFile(name,asset))return JS_ThrowTypeError(ctx,"could not load model: %s",name.c_str());
  asset.eyePixels=eyePixels;
  currentScene->assets.push_back(std::move(asset));
  return JS_NewInt32(ctx,(int)currentScene->assets.size());
}
static JSValue jsCreateObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<4||currentScene->objects.size()>=256)return JS_ThrowRangeError(ctx,"createObject needs asset, x, y, z; limit is 256 objects");
  int asset=0;if(JS_ToInt32(ctx,&asset,argv[0])<0||asset<1||asset>(int)currentScene->assets.size())return JS_ThrowRangeError(ctx,"unknown asset id");
  float values[4]={0,0,0,.55f};
  for(int i=1;i<std::min(argc,5);i++)if(!number(ctx,argv[i],values[i-1]))return JS_ThrowTypeError(ctx,"object coordinates and scale must be finite numbers");
  if(values[3]<=0||values[3]>2)return JS_ThrowRangeError(ctx,"scale must be greater than 0 and at most 2 meters");
  int id=currentScene->nextId++;
  currentScene->objects.push_back({id,asset,values[0],values[1],values[2],values[3],0});
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
  if(!enabled&&currentScene==liveScene)frameInput.clearPhysicsEvents();
  return JS_UNDEFINED;
}
static void updateScenePhysics(Scene &scene,double elapsed){
  if(!scene.physics)return;
  for(const auto &o:scene.objects)if(scene.physics->needsTarget(o.id))scene.physics->target(o.id,physicsPose(o),elapsed);
  scene.physics->advance(elapsed);
  auto breaks=scene.physics->takeJointBreaks();
  if(&scene==liveScene)for(const auto &broken:breaks){
    frame_input::Event event;event.type="physics.jointBreak";event.jointId=broken.jointId;event.objectId=broken.objectA;event.otherObjectId=broken.objectB;event.force=broken.force;event.torque=broken.torque;frameInput.addEvent(std::move(event));
  }
  auto collisions=scene.physics->takeCollisionEvents();
  if(&scene==liveScene)for(const auto &collision:collisions){
    frame_input::Event event;
    event.type=collision.phase==physics::CollisionPhase::Start?"physics.collisionStart":collision.phase==physics::CollisionPhase::Stay?"physics.collisionStay":"physics.collisionEnd";
    event.objectId=collision.objectId;event.otherObjectId=collision.otherObjectId;
    event.point={collision.point.x,collision.point.y,collision.point.z};
    event.normal={collision.normal.x,collision.normal.y,collision.normal.z};
    event.penetration=collision.penetration;event.relativeSpeed=collision.relativeSpeed;
    frameInput.addEvent(std::move(event));
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
static void forgetObjectInteraction(int id){
  pointerStates.erase(id);
  for(int h=0;h<2;h++){
    if(hoveredObject[h]==id){frameInput.addOverlayEvent("overlay.surfaceHoverEnd",id,h);hoveredObject[h]=0;frameInput.setHoveredObject(h,0);}
    if(meshHoveredObject[h]==id){frameInput.addOverlayEvent("overlay.meshHoverEnd",id,h);meshHoveredObject[h]=0;}
    if(meshCurrentObject[h]==id){meshCurrentObject[h]=0;meshHitDistance[h]=2;meshLastHit[h]={};}
  }
}
static JSValue jsRemoveObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"removeObject(id)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  if(currentScene->physics)currentScene->physics->remove(id);
  auto &ids=currentScene->interactiveIds;
  ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
  if(currentScene==liveScene)forgetObjectInteraction(id);
  currentScene->objects.erase(std::remove_if(currentScene->objects.begin(),currentScene->objects.end(),[id](const Object &entry){return entry.id==id;}),currentScene->objects.end());
  return JS_UNDEFINED;
}
struct ScriptHost {JSRuntime *rt=nullptr;JSContext *ctx=nullptr;JSValue update=JS_UNDEFINED;std::filesystem::file_time_type mtime{};Scene scene;};
static void bind(JSContext *ctx,const char *name,JSCFunction *function,int argc){
  JSValue global=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global,name,JS_NewCFunction(ctx,function,name,argc));JS_FreeValue(ctx,global);
}
static void closeScript(ScriptHost &s){
  for(auto &asset:s.scene.assets)deleteModel(asset);
  for(auto &panel:s.scene.panels)panelDestroy(panel);
  for(auto &panel:s.scene.retiredPanels)panelDestroy(panel);
  if(currentScene==&s.scene)currentScene=nullptr;
  if(liveScene==&s.scene)liveScene=nullptr;
  if(s.ctx){clearInputCallback(s.ctx);JS_FreeValue(s.ctx,s.update);JS_FreeContext(s.ctx);}if(s.rt)JS_FreeRuntime(s.rt);
  s={};
}
static bool loadScript(ScriptHost &active,const std::string &path,const std::function<void()> &beforeCommit={}){
  std::ifstream file(path);if(!file){fprintf(stderr,"script unreadable: %s\n",path.c_str());return false;}
  std::ostringstream stream;stream<<file.rdbuf();std::string source=stream.str();
  ScriptHost candidate;candidate.rt=JS_NewRuntime();if(!candidate.rt)return false;
  JS_SetMemoryLimit(candidate.rt,128*1024*1024);
  candidate.ctx=JS_NewContext(candidate.rt);if(!candidate.ctx){closeScript(candidate);return false;}
  currentScene=&candidate.scene;
  JSContext *previousCallbackCtx=inputCallbackCtx;JSValue previousCallback=inputCallback;inputCallbackCtx=nullptr;inputCallback=JS_UNDEFINED;
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
  bind(candidate.ctx,"enableObjectSurface",jsEnableObjectSurface,1);
  bind(candidate.ctx,"setObjectInteraction",jsSetObjectInteraction,2);
  bind(candidate.ctx,"getPointer",jsGetPointer,0);
  bind(candidate.ctx,"getEngineStats",jsGetEngineStats,0);
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
  bind(candidate.ctx,"panelClear",jsPanelClear,5);
  bind(candidate.ctx,"panelText",jsPanelText,9);
  bind(candidate.ctx,"panelLoadFont",jsPanelSetFont,3);
  bind(candidate.ctx,"panelPresent",jsPanelPresent,1);
  bind(candidate.ctx,"panelCanvasClear",jsPanelClear,5);
  bind(candidate.ctx,"panelCanvasText",jsPanelText,9);
  bind(candidate.ctx,"panelSetTransform",jsPanelSetTransform,5);
  bind(candidate.ctx,"panelSetRotation",jsPanelSetRotation,10);
  bind(candidate.ctx,"panelSetVisible",jsPanelVisible,2);
  bind(candidate.ctx,"removePanel",jsPanelRemove,1);
  bind(candidate.ctx,"loadModel",jsLoadModel,1);
  bind(candidate.ctx,"createObject",jsCreateObject,5);
  bind(candidate.ctx,"setObjectPosition",jsSetObjectPosition,4);
  bind(candidate.ctx,"setObjectYaw",jsSetObjectYaw,2);
  bind(candidate.ctx,"setObjectRotation",jsSetObjectRotation,4);
  bind(candidate.ctx,"setObjectScale",jsSetObjectScale,2);
  bind(candidate.ctx,"setObjectVisible",jsSetObjectVisible,2);
  bind(candidate.ctx,"removeObject",jsRemoveObject,1);
  JSValue result=JS_Eval(candidate.ctx,source.c_str(),source.size(),path.c_str(),JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(candidate.ctx);const char *message=JS_ToCString(candidate.ctx,exception);
    fprintf(stderr,"[js] load error: %s\n",message?message:"unknown");if(message)JS_FreeCString(candidate.ctx,message);
    JS_FreeValue(candidate.ctx,exception);JS_FreeValue(candidate.ctx,result);closeScript(candidate);inputCallbackCtx=previousCallbackCtx;inputCallback=previousCallback;currentScene=&active.scene;return false;
  }
  JS_FreeValue(candidate.ctx,result);
  JSValue global=JS_GetGlobalObject(candidate.ctx);candidate.update=JS_GetPropertyStr(candidate.ctx,global,"update");JS_FreeValue(candidate.ctx,global);
  std::error_code ec;candidate.mtime=std::filesystem::last_write_time(path,ec);
  if(previousCallbackCtx)JS_FreeValue(previousCallbackCtx,previousCallback);
  if(beforeCommit)beforeCommit();
  closeScript(active);active=std::move(candidate);currentScene=liveScene=&active.scene;
  fprintf(stdout,"[js] loaded %s objects=%zu assets=%zu\n",path.c_str(),active.scene.objects.size(),active.scene.assets.size());fflush(stdout);return true;
}
static bool initEgl(EGLDisplay &display,EGLContext &context,EGLSurface &surface) {
  display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(display,nullptr,nullptr))return false;
  EGLConfig config;EGLint count=0;
  const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)return false;
  const EGLint ctxAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttrs);
  const EGLint surfAttrs[]={EGL_WIDTH,kDefaultEyePixels,EGL_HEIGHT,kDefaultEyePixels,EGL_NONE};
  surface=eglCreatePbufferSurface(display,config,surfAttrs);
  return eglMakeCurrent(display,surface,surface,context)==EGL_TRUE;
}
static void makeDepthStorage(Buffer &buffer,int width,int height,GLuint sharedDepth=0){
  buffer.depth=sharedDepth;
  if(!buffer.depth){glGenRenderbuffers(1,&buffer.depth);buffer.ownsDepth=true;glBindRenderbuffer(GL_RENDERBUFFER,buffer.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);}
}
static bool makeBuffer(Buffer &b,gbm_device *gbm,EGLDisplay display,int eyePixels,GLuint sharedDepth=0) {
  const int width=2*eyePixels,height=eyePixels;
  b.bo=gbm_bo_create(gbm,width,height,GBM_FORMAT_ABGR8888,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
  if(!b.bo||(b.fd=gbm_bo_get_fd(b.bo))<0)return false;
  vr::DmabufAttributes_t a{};
  a.unWidth=width;a.unHeight=height;a.unDepth=a.unMipLevels=a.unArrayLayers=a.unSampleCount=1;
  a.unFormat=DRM_FORMAT_ABGR8888;a.ulModifier=DRM_FORMAT_MOD_LINEAR;a.unPlaneCount=1;
  a.plane[0].nFd=b.fd;a.plane[0].unOffset=gbm_bo_get_offset(b.bo,0);a.plane[0].unStride=gbm_bo_get_stride(b.bo);
  if(!vr::VRIPCResourceManager()->ImportDmabuf(vr::VRApplication_Overlay,&a,&b.handle)||!b.handle)return false;
  const EGLint attrs[]={EGL_WIDTH,width,EGL_HEIGHT,height,EGL_LINUX_DRM_FOURCC_EXT,EGLint(DRM_FORMAT_ABGR8888),EGL_DMA_BUF_PLANE0_FD_EXT,b.fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,EGLint(a.plane[0].unOffset),EGL_DMA_BUF_PLANE0_PITCH_EXT,EGLint(a.plane[0].unStride),EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,EGLint(DRM_FORMAT_MOD_LINEAR&0xffffffff),EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,EGLint(DRM_FORMAT_MOD_LINEAR>>32),EGL_NONE};
  b.image=createImage(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
  if(b.image==EGL_NO_IMAGE_KHR){fprintf(stderr,"eglCreateImage error=%x\n",(unsigned)eglGetError());return false;}
  glGenRenderbuffers(1,&b.color);glBindRenderbuffer(GL_RENDERBUFFER,b.color);imageRenderbuffer(GL_RENDERBUFFER,b.image);
  makeDepthStorage(b,width,height,sharedDepth);
  glGenFramebuffers(1,&b.fbo);glBindFramebuffer(GL_FRAMEBUFFER,b.fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,b.color);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,b.depth);
  bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;glBindFramebuffer(GL_FRAMEBUFFER,0);return ok;
}
static void destroyBuffer(Buffer &buffer,EGLDisplay display){
  if(buffer.fbo)glDeleteFramebuffers(1,&buffer.fbo);
  if(buffer.depth&&buffer.ownsDepth)glDeleteRenderbuffers(1,&buffer.depth);
  if(buffer.color)glDeleteRenderbuffers(1,&buffer.color);
  if(buffer.image!=EGL_NO_IMAGE_KHR)destroyImage(display,buffer.image);
  if(buffer.handle)vr::VRIPCResourceManager()->UnrefResource(buffer.handle);
  if(buffer.fd>=0)close(buffer.fd);
  if(buffer.bo)gbm_bo_destroy(buffer.bo);
  buffer={};
}
static void destroySurface(Surface &surface,EGLDisplay display){
  if(surface.overlay!=vr::k_ulOverlayHandleInvalid){
    vr::VROverlay()->ClearOverlayTexture(surface.overlay);
    vr::VROverlay()->DestroyOverlay(surface.overlay);
  }
  for(auto &buffer:surface.buffers)destroyBuffer(buffer,display);
  surface.overlay=vr::k_ulOverlayHandleInvalid;
}
static bool createSurface(Surface &surface,int id,float width,int eyePixels,bool interactive,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &anchor){
  surface.objectId=id;surface.width=width;surface.eyePixels=eyePixels;surface.interactive=interactive;
  const std::string key="sf_overlay_engine."+std::to_string(id);
  auto *overlays=vr::VROverlay();
  auto error=overlays->CreateOverlay(key.c_str(),key.c_str(),&surface.overlay);
  if(error!=vr::VROverlayError_None){fprintf(stderr,"[surface] create id=%d error=%d\n",id,(int)error);return false;}
  error=overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_SideBySide_Parallel,true);
  if(error!=vr::VROverlayError_None)return false;
  error=overlays->SetOverlayWidthInMeters(surface.overlay,width);
  if(error!=vr::VROverlayError_None)return false;
  vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(surface.overlay,&bounds);
  overlays->SetOverlayTransformAbsolute(surface.overlay,vr::TrackingUniverseStanding,&anchor);
  if(interactive){
    overlays->SetOverlayInputMethod(surface.overlay,vr::VROverlayInputMethod_Mouse);
    overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,true);
    overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_SendVRSmoothScrollEvents,true);
    vr::VROverlayIntersectionMaskPrimitive_t mask{};
    mask.m_nPrimitiveType=vr::OverlayIntersectionPrimitiveType_Circle;
    mask.m_Primitive.m_Circle.m_flCenterX=.5f;
    mask.m_Primitive.m_Circle.m_flCenterY=.5f;
    mask.m_Primitive.m_Circle.m_flRadius=.3f;
    overlays->SetOverlayIntersectionMask(surface.overlay,&mask,1);
    vr::HmdVector2_t scale{{1,1}};overlays->SetOverlayMouseScale(surface.overlay,&scale);
  }
  // The compositor imports color only. Rendering is ordered on one GL context,
  // so the three color FBOs can reuse one depth attachment without extra fences.
  GLuint sharedDepth=0;
  for(auto &buffer:surface.buffers){
    if(!makeBuffer(buffer,gbm,display,eyePixels,sharedDepth)){fprintf(stderr,"[surface] buffer id=%d failed\n",id);return false;}
    sharedDepth=buffer.depth;
  }
  fprintf(stdout,"[surface] created id=%d width=%.3f eye_px=%d\n",id,width,eyePixels);fflush(stdout);
  return true;
}
static GLuint compile(GLenum type,const char *source) {
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);fprintf(stderr,"shader error: %s\n",log);}
  return shader;
}
static void setVec3(GLint location,float x,float y,float z){glUniform3f(location,x,y,z);}
static void faceViewer(vr::HmdMatrix34_t &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,float right[3],float up[3],float forward[3]){
  float length=0;
  for(int i=0;i<3;i++){forward[i]=panel.m[i][3]-head.m[i][3];length+=forward[i]*forward[i];}
  if(length<.0001f){for(int i=0;i<3;i++)forward[i]=-anchor.m[i][2];}
  else {length=std::sqrt(length);for(int i=0;i<3;i++)forward[i]/=length;}
  float hint[3]={anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]};
  float alignment=forward[0]*hint[0]+forward[1]*hint[1]+forward[2]*hint[2];
  if(std::fabs(alignment)>.95f)for(int i=0;i<3;i++)hint[i]=anchor.m[i][2];
  right[0]=forward[1]*hint[2]-forward[2]*hint[1];
  right[1]=forward[2]*hint[0]-forward[0]*hint[2];
  right[2]=forward[0]*hint[1]-forward[1]*hint[0];
  length=std::sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
  for(int i=0;i<3;i++)right[i]/=length;
  up[0]=right[1]*forward[2]-right[2]*forward[1];
  up[1]=right[2]*forward[0]-right[0]*forward[2];
  up[2]=right[0]*forward[1]-right[1]*forward[0];
  for(int i=0;i<3;i++){panel.m[i][0]=right[i];panel.m[i][1]=up[i];panel.m[i][2]=-forward[i];}
}
static bool createLaser(vr::VROverlayHandle_t &handle,int hand){
  auto *overlays=vr::VROverlay();const std::string key="sf_overlay_engine.laser."+std::to_string(hand);
  if(overlays->CreateOverlay(key.c_str(),key.c_str(),&handle)!=vr::VROverlayError_None)return false;
  unsigned char pixels[256*2*4]{};
  for(int x=0;x<256;x++)for(int y=0;y<2;y++){
    const int i=(y*256+x)*4;pixels[i]=hand?40:255;pixels[i+1]=hand?220:150;pixels[i+2]=255;
    pixels[i+3]=(unsigned char)(x<4?x*38:x>251?(255-x)*38:150);
  }
  if(overlays->SetOverlayRaw(handle,pixels,256,2,4)!=vr::VROverlayError_None)return false;
  overlays->SetOverlayInputMethod(handle,vr::VROverlayInputMethod_None);
  overlays->SetOverlayWidthInMeters(handle,2.f);
  return true;
}
static void updateLaser(vr::VROverlayHandle_t handle,bool &shown,bool visible,int hand,const vr::TrackedDevicePose_t &controller,const vr::HmdMatrix34_t &head,float hitDistance){
  auto *overlays=vr::VROverlay();
  if(!visible||!controller.bPoseIsValid){if(shown){overlays->HideOverlay(handle);shown=false;}return;}
  const auto &pose=controller.mDeviceToAbsoluteTracking;
  // Frame controller /pose/tip and /pose/openxr_aim: 40 degrees about local X.
  constexpr float tilt=.6981317008f;
  const float localOrigin[3]={hand==0?.012694f:-.012694f,-.02522f,.020687f};
  const float length=std::clamp(hitDistance,.02f,2.f);
  float direction[3],origin[3],center[3],normal[3],up[3],viewLength=0;
  for(int i=0;i<3;i++){
    origin[i]=pose.m[i][3];
    for(int axis=0;axis<3;axis++)origin[i]+=pose.m[i][axis]*localOrigin[axis];
    direction[i]=-std::sin(tilt)*pose.m[i][1]-std::cos(tilt)*pose.m[i][2];
    center[i]=origin[i]+direction[i]*length*.5f;normal[i]=head.m[i][3]-center[i];
  }
  float along=normal[0]*direction[0]+normal[1]*direction[1]+normal[2]*direction[2];
  for(int i=0;i<3;i++){normal[i]-=along*direction[i];viewLength+=normal[i]*normal[i];}
  if(viewLength<.0001f){for(int i=0;i<3;i++)normal[i]=pose.m[i][1];}
  else {viewLength=std::sqrt(viewLength);for(int i=0;i<3;i++)normal[i]/=viewLength;}
  up[0]=normal[1]*direction[2]-normal[2]*direction[1];
  up[1]=normal[2]*direction[0]-normal[0]*direction[2];
  up[2]=normal[0]*direction[1]-normal[1]*direction[0];
  vr::HmdMatrix34_t beam{};
  for(int i=0;i<3;i++){beam.m[i][0]=direction[i];beam.m[i][1]=up[i];beam.m[i][2]=normal[i];beam.m[i][3]=center[i];}
  const auto widthError=overlays->SetOverlayWidthInMeters(handle,length);
  if(widthError!=vr::VROverlayError_None)fprintf(stderr,"[laser] width hand=%d error=%d\n",hand,(int)widthError);
  overlays->SetOverlayTransformAbsolute(handle,vr::TrackingUniverseStanding,&beam);
  if(!shown){overlays->ShowOverlay(handle);shown=true;}
}
static bool grabNeedsRelease[2]{};
struct GrabState {
  int objectId=0;bool direct=false,rawGrab=false;int releaseFrames=0;
  float offset[3]{},startHand[9]{},startPitch=0,startYaw=0,startRoll=0,laserDistance=2.f;
};
static void beginGrab(GrabState &grab,const Object &object,const ControllerSnapshot &hand,bool direct){
  grab={};grab.objectId=object.id;grab.direct=direct;
  const float delta[3]={object.x-hand.x,object.y-hand.y,object.z-hand.z};
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++)grab.offset[axis]+=hand.rotation[row*3+axis]*delta[row];
  std::copy(hand.rotation,hand.rotation+9,grab.startHand);
  grab.startPitch=object.pitch;grab.startYaw=object.yaw;grab.startRoll=object.roll;
}
static void objectRotationMatrix(float pitch,float yaw,float roll,float m[9]){
  const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
  m[0]=cy*cr+sy*sp*sr;m[1]=-cy*sr+sy*sp*cr;m[2]=sy*cp;
  m[3]=cp*sr;m[4]=cp*cr;m[5]=-sp;
  m[6]=-sy*cr+cy*sp*sr;m[7]=sy*sr+cy*sp*cr;m[8]=cy*cp;
}
static bool frameButtonDown(int hand,const char *path){auto it=frameInput.hand(hand).raw.find(path);return it!=frameInput.hand(hand).raw.end()&&it->second.isBoolean&&it->second.boolean;}
static float frameAxis(int hand,const char *path){auto it=frameInput.hand(hand).raw.find(path);return it==frameInput.hand(hand).raw.end()?0.f:it->second.value;}
static bool frameTriggerDown(int hand){return frameButtonDown(hand,"/input/trigger/click")||frameAxis(hand,"/input/trigger/value")>.2f;}
static bool frameGrabDown(int hand,GrabButton button){
  const bool trigger=button!=GrabButton::Grip&&frameTriggerDown(hand);
  const bool grip=button!=GrabButton::Trigger&&(frameButtonDown(hand,"/input/grip/click")||frameAxis(hand,"/input/grip/value")>.2f);
  return trigger||grip;
}
static void updateManipulation(Scene &scene,std::array<GrabState,2> &grabs,float dt){
  for(int h=0;h<2;h++){
    const auto &hand=controllers[h];
    auto &grab=grabs[h];
    auto ownedByOther=[&](int id){return grabs[1-h].objectId==id;};
    const int previousGrab=grab.objectId;
    Object *object=nullptr;for(auto &candidate:scene.objects)if(candidate.id==grab.objectId){object=&candidate;break;}
    if(previousGrab&&(!object||!hand.connected||!object->visible||!object->autoManipulate||!object->grabbable)){if(scene.physics)scene.physics->release(previousGrab,false);frameInput.addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;grabNeedsRelease[h]=true;}
    if(object){
      auto state=pointerStates.find(object->id);
      const bool buttonDown=frameGrabDown(h,object->grabButton);
      const bool nativeDown=object->grabButton!=GrabButton::Grip&&state!=pointerStates.end()&&state->second.hand==h&&state->second.down;
      if(grab.rawGrab)grab.releaseFrames=buttonDown?0:grab.releaseFrames+1;
      if((grab.rawGrab&&grab.releaseFrames>=5)||(!grab.rawGrab&&!nativeDown&&!buttonDown)){
        if(scene.physics)scene.physics->release(previousGrab,true);
        frameInput.addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;
      }
    }
    if(grabNeedsRelease[h]){
      bool nativeDown=false,nativeKnown=false;
      for(const auto &[id,pointer]:pointerStates)if(pointer.hand==h){nativeDown|=pointer.down;nativeKnown|=pointer.valid;}
      const bool releaseKnown=frameInput.started()?frameInput.hand(h).valid:nativeKnown;
      if(hand.connected&&releaseKnown&&!frameGrabDown(h,GrabButton::Both)&&!nativeDown)grabNeedsRelease[h]=false;
    }
    if(!object&&hand.valid&&hand.connected&&!grabNeedsRelease[h]){
      for(auto &candidate:scene.objects){
        if(!candidate.visible||!candidate.autoManipulate||!candidate.grabbable||ownedByOther(candidate.id))continue;
        auto pointer=pointerStates.find(candidate.id);
        // Native surfaces are rectangles: custom lasers must acquire only a current mesh hit.
        const bool nativeDown=candidate.grabButton!=GrabButton::Grip&&!scene.laserVisible[h]&&pointer!=pointerStates.end()&&pointer->second.down&&pointer->second.hand==h;
        const bool laserDown=meshCurrentObject[h]==candidate.id&&frameGrabDown(h,candidate.grabButton);
        if(nativeDown||laserDown){
          const float dx=candidate.x-hand.x,dy=candidate.y-hand.y,dz=candidate.z-hand.z;
          const float reach=.10f+candidate.scale*.5f;
          const bool direct=candidate.directGrab&&dx*dx+dy*dy+dz*dz<reach*reach;
          beginGrab(grab,candidate,hand,direct);
          if(scene.physics&&scene.physics->contains(candidate.id))scene.physics->hold(candidate.id);
          grab.rawGrab=frameGrabDown(h,candidate.grabButton);
          const float centerDistance=std::sqrt(dx*dx+dy*dy+dz*dz);
          grab.laserDistance=std::clamp(std::min(meshHitDistance[h],centerDistance-std::max(.1f,candidate.scale*.5f)),.02f,2.f);
          frameInput.addOverlayEvent("overlay.grabStart",candidate.id,h);object=&candidate;break;
        }
      }
    }
    if(object&&!hand.valid&&scene.physics)scene.physics->cancelThrow(object->id);
    if(object&&hand.valid){
      const float oldX=object->x,oldY=object->y,oldZ=object->z,oldPitch=object->pitch,oldYaw=object->yaw,oldRoll=object->roll;
      auto pointer=pointerStates.find(object->id);
      const float x=pointer!=pointerStates.end()?std::clamp(pointer->second.scrollX,-1.f,1.f):0;
      const float y=pointer!=pointerStates.end()?std::clamp(pointer->second.scrollY,-1.f,1.f):0;
      if(!grab.direct&&(x||y)){
        if(x){
          if(pointer!=pointerStates.end()&&pointer->second.secondaryDown&&object->laserPitchWithStickClick)object->pitch+=x*6.f*dt;
          else if(object->laserYaw)object->yaw+=x*6.f*dt;
        }
        if(y&&object->depthAdjust){
          const float length=std::sqrt(grab.offset[0]*grab.offset[0]+grab.offset[1]*grab.offset[1]+grab.offset[2]*grab.offset[2]);
          if(length>.001f){const float next=std::max(.06f,length+y*3.2f*dt);for(float &v:grab.offset)v*=next/length;grab.laserDistance=std::clamp(grab.laserDistance+next-length,.02f,2.f);}
        }
      }
      const auto &r=hand.rotation;
      object->x=hand.x+r[0]*grab.offset[0]+r[1]*grab.offset[1]+r[2]*grab.offset[2];
      object->y=hand.y+r[3]*grab.offset[0]+r[4]*grab.offset[1]+r[5]*grab.offset[2];
      object->z=hand.z+r[6]*grab.offset[0]+r[7]*grab.offset[1]+r[8]*grab.offset[2];
      frameInput.addOverlayEvent("overlay.grabMove",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
      if(std::fabs(object->x-oldX)+std::fabs(object->y-oldY)+std::fabs(object->z-oldZ)>.00001f)frameInput.addOverlayEvent("overlay.move",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
      if(grab.direct&&(object->handPitch||object->handYaw||object->handRoll)){
        float delta[9]{};
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)for(int k=0;k<3;k++)delta[row*3+col]+=r[row*3+k]*grab.startHand[col*3+k];
        // Controller snapshots use forward-positive Z; model vertices use backward-positive Z.
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)if((row==2)!=(col==2))delta[row*3+col]=-delta[row*3+col];
        const float pitch=object->handPitch?std::asin(std::clamp(-delta[5],-1.f,1.f)):0;
        const float yaw=object->handYaw?std::atan2(delta[2],delta[8]):0;
        const float roll=object->handRoll?std::atan2(delta[3],delta[4]):0;
        float gesture[9],initial[9],combined[9]{};
        objectRotationMatrix(pitch,yaw,roll,gesture);
        objectRotationMatrix(grab.startPitch,grab.startYaw,grab.startRoll,initial);
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)for(int k=0;k<3;k++)combined[row*3+col]+=gesture[row*3+k]*initial[k*3+col];
        object->pitch=std::asin(std::clamp(-combined[5],-1.f,1.f));
        object->yaw=std::atan2(combined[2],combined[8]);
        object->roll=std::atan2(combined[3],combined[4]);
      }
      if(std::fabs(object->pitch-oldPitch)+std::fabs(object->yaw-oldYaw)+std::fabs(object->roll-oldRoll)>.00001f)frameInput.addOverlayEvent("overlay.rotate",object->id,h,0,0,0,object->pitch-oldPitch,object->yaw-oldYaw,object->roll-oldRoll);
    }
  }
}
struct ProjectedBounds {float x0=INFINITY,x1=-INFINITY,y0=INFINITY,y1=-INFINITY,near=INFINITY,far=-INFINITY;bool valid=true;};
static ProjectedBounds projectedBounds(const Object &object,const ModelAsset &geometry,const vr::HmdMatrix34_t &anchor,const float eye[3],const float right[3],const float up[3],const float forward[3]){
  ProjectedBounds result;float rotation[9];objectRotationMatrix(object.pitch,object.yaw,object.roll,rotation);
  for(int corner=0;corner<8;corner++){
    float local[3],rotated[3]{},position[3]={object.x,object.y,object.z};
    for(int axis=0;axis<3;axis++)local[axis]=(corner&(1<<axis))?geometry.high[axis]:geometry.low[axis];
    for(int row=0;row<3;row++)for(int axis=0;axis<3;axis++)rotated[row]+=rotation[row*3+axis]*local[axis];
    for(int axis=0;axis<3;axis++)position[axis]+=object.scale*rotated[axis]*(axis==2?-1.f:1.f);
    float ray[3];for(int row=0;row<3;row++)ray[row]=anchor.m[row][3]+anchor.m[row][0]*position[0]+anchor.m[row][1]*position[1]-anchor.m[row][2]*position[2]-eye[row];
    float x=0,y=0,d=0;for(int axis=0;axis<3;axis++){x+=ray[axis]*right[axis];y+=ray[axis]*up[axis];d+=ray[axis]*forward[axis];}
    if(d<=.05f){result.valid=false;return result;}
    result.x0=std::min(result.x0,x/d);result.x1=std::max(result.x1,x/d);result.y0=std::min(result.y0,y/d);result.y1=std::max(result.y1,y/d);
    result.near=std::min(result.near,d);result.far=std::max(result.far,d);
  }
  return result;
}
static bool mayOcclude(const ProjectedBounds &occluder,const ProjectedBounds &target){
  if(!occluder.valid||!target.valid)return true;
  constexpr float margin=1e-4f;
  return occluder.near<=target.far+margin&&occluder.x1+margin>=target.x0&&occluder.x0-margin<=target.x1&&occluder.y1+margin>=target.y0&&occluder.y0-margin<=target.y1;
}
struct ScissorRegion {int x=0,y=0,width=0,height=0;};
// Convert the target's conservative angular bounds to its overlay pixels.
// Invalid/near-plane bounds retain the full viewport. Padding covers raster edges.
static ScissorRegion projectedScissor(const ProjectedBounds &bounds,const float center[3],const float eye[3],const float right[3],const float up[3],const float forward[3],float halfWidth,int pixels){
  const ScissorRegion full{0,0,pixels,pixels};
  if(!bounds.valid||halfWidth<=0)return full;
  float plane=0,centerX=0,centerY=0;
  for(int axis=0;axis<3;axis++){const float delta=center[axis]-eye[axis];plane+=delta*forward[axis];centerX+=delta*right[axis];centerY+=delta*up[axis];}
  if(plane<=0)return full;
  const float x0=((plane*bounds.x0-centerX)/halfWidth*.5f+.5f)*pixels;
  const float x1=((plane*bounds.x1-centerX)/halfWidth*.5f+.5f)*pixels;
  const float y0=((plane*bounds.y0-centerY)/halfWidth*.5f+.5f)*pixels;
  const float y1=((plane*bounds.y1-centerY)/halfWidth*.5f+.5f)*pixels;
  if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1))return full;
  const int lowX=int(std::floor(std::clamp(x0-2,0.f,float(pixels)))),lowY=int(std::floor(std::clamp(y0-2,0.f,float(pixels))));
  const int highX=int(std::ceil(std::clamp(x1+2,0.f,float(pixels)))),highY=int(std::ceil(std::clamp(y1+2,0.f,float(pixels))));
  return {lowX,lowY,std::max(0,highX-lowX),std::max(0,highY-lowY)};
}
// Occluders write depth only; masked holes keep their alpha-test behavior.
// Blended surfaces must not become opaque occluders.
template<class Draw> static void renderObjectLayers(int targetId,Draw &&draw,const ScissorRegion *region=nullptr){
  if(region){glEnable(GL_SCISSOR_TEST);glScissor(region->x,region->y,region->width,region->height);}
  glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_TRUE);glDisable(GL_BLEND);
  draw(targetId,-1);
  if(region)glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  for(int pass=0;pass<2;pass++){
    if(pass){glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}
    else {glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
    draw(targetId,pass);
  }
  glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
static bool rayTriangle(const float o[3],const float d[3],const float a[3],const float b[3],const float c[3],float &t){float e1[3],e2[3],p[3],q[3],s[3];for(int i=0;i<3;i++){e1[i]=b[i]-a[i];e2[i]=c[i]-a[i];}for(int i=0;i<3;i++){p[i]=d[(i+1)%3]*e2[(i+2)%3]-d[(i+2)%3]*e2[(i+1)%3];}float det=e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2];if(std::fabs(det)<1e-6f)return false;float inv=1.f/det;for(int i=0;i<3;i++)s[i]=o[i]-a[i];float u=(s[0]*p[0]+s[1]*p[1]+s[2]*p[2])*inv;if(u<0||u>1)return false;for(int i=0;i<3;i++)q[i]=s[(i+1)%3]*e1[(i+2)%3]-s[(i+2)%3]*e1[(i+1)%3];float v=(d[0]*q[0]+d[1]*q[1]+d[2]*q[2])*inv;if(v<0||u+v>1)return false;t=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])*inv;return t>=0;}
static float meshHit(const Object &object,const ModelAsset &asset,const ControllerSnapshot &hand,int handIndex,bool accelerated=true){
  if(!hand.valid)return std::numeric_limits<float>::max();
  constexpr float tilt=.6981317008f;
  const float tip[3]={handIndex==0?.012694f:-.012694f,-.02522f,.020687f};
  float origin[3]={hand.x,hand.y,hand.z},direction[3]{};
  for(int row=0;row<3;row++){
    for(int axis=0;axis<3;axis++)origin[row]+=hand.rotation[row*3+axis]*tip[axis];
    direction[row]=-std::sin(tilt)*hand.rotation[row*3+1]-std::cos(tilt)*hand.rotation[row*3+2];
  }
  const float miss=std::numeric_limits<float>::max();
  if(object.scale<=0)return miss;
  // Invert the renderer's F*R transform once per ray, rather than applying it
  // to every triangle. Keep direction unit length; convert hit distance back.
  float rotation[9];objectRotationMatrix(object.pitch,object.yaw,object.roll,rotation);
  const float delta[3]={origin[0]-object.x,origin[1]-object.y,-(origin[2]-object.z)};
  const float reflectedDirection[3]={direction[0],direction[1],-direction[2]};
  float localOrigin[3]{},localDirection[3]{};
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++){
    localOrigin[axis]+=rotation[row*3+axis]*delta[row]/object.scale;
    localDirection[axis]+=rotation[row*3+axis]*reflectedDirection[row];
  }
  float best=miss;
  if(accelerated)best=picking::intersect(asset,localOrigin,localDirection,rayTriangle);
  else{
    float near;
    if(!picking::bounds(localOrigin,localDirection,asset.low,asset.high,miss,near))return miss;
    for(const auto &part:asset.primitives)for(size_t index=0;index+14<part.vertices.size();index+=15){
      const float *v=part.vertices.data()+index;float distance;
      if(rayTriangle(localOrigin,localDirection,v,v+5,v+10,distance))best=std::min(best,distance);
    }
  }
  return best==miss?miss:best*object.scale;
}
int main(int argc,char **argv) {
  // CPU-only workload: no SteamVR session or graphics context required.
  if(argc>2&&std::string(argv[1])=="--benchmark-picking"){
    ModelAsset asset;if(!loadModelFile(argv[2],asset))return 1;
    Object object{1,1,0,0,0,.4f,.5f};
    ControllerSnapshot hand;hand.valid=true;
    constexpr float tilt=.6981317008f;
    const float c=std::cos(tilt),s=std::sin(tilt);
    const float rotation[9]={1,0,0,0,c,-s,0,s,c};
    std::copy(rotation,rotation+9,hand.rotation);
    hand.y=-(-.02522f*c-.020687f*s);
    hand.z=1.f-(-.02522f*s+.020687f*c);
    constexpr int frames=200;
    std::array<float,frames*2> reference{};
    for(bool accelerated:{false,true}){
      int hits=0;double checksum=0;
      const auto start=std::chrono::steady_clock::now();
      for(int frame=0;frame<frames;frame++)for(int h=0;h<2;h++){
        hand.x=(h==0?-.012694f:.012694f)+((frame%21)-10)*.012f;
        const float distance=meshHit(object,asset,hand,h,accelerated);
        if(!accelerated)reference[frame*2+h]=distance;
        else{
          const float expected=reference[frame*2+h];
          const bool hit=distance<std::numeric_limits<float>::max(),expectedHit=expected<std::numeric_limits<float>::max();
          if(hit!=expectedHit||(hit&&std::fabs(distance-expected)>1e-5f)){
            fprintf(stderr,"[benchmark] BVH mismatch frame=%d hand=%d expected=%g actual=%g\n",frame,h,expected,distance);return 2;
          }
        }
        if(distance<std::numeric_limits<float>::max()){hits++;checksum+=distance;}
      }
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      fprintf(stdout,"[benchmark] mode=%s frames=%d hands=2 picking_ms=%.4f hits=%d checksum=%.6f nodes=%zu triangles=%zu\n",accelerated?"bvh":"linear",frames,ms/frames,hits,checksum,asset.pickNodes.size(),asset.pickTriangles.size());
    }
    return 0;
  }
  if(argc>2&&std::string(argv[1])=="--inspect"){
    ModelAsset asset;return loadModelFile(argv[2],asset)?0:1;
  }
  std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
  std::string scriptPath="scripts/avocado.js",calibrationPath;bool hasScript=false;
  for(int i=1;i<argc;i++){
    std::string arg=argv[i];
    if(arg=="--eye-calibration"||arg=="--eye-calibration-output"){
      if(i+1==argc){fprintf(stderr,"%s requires a file path\n",arg.c_str());return 13;}
      (arg=="--eye-calibration"?calibrationPath:calibrationOutput)=argv[++i];
    }else if(!arg.empty()&&arg[0]!='-'&&!hasScript){scriptPath=arg;hasScript=true;}
    else{fprintf(stderr,"Unknown argument: %s\n",arg.c_str());return 13;}
  }
  if(!calibrationPath.empty()){
    std::ifstream file(calibrationPath,std::ios::binary);std::string data((std::istreambuf_iterator<char>(file)),{});
    JSRuntime *runtime=JS_NewRuntime();JSContext *ctx=JS_NewContext(runtime);
    JSValue value=JS_ParseJSON(ctx,data.c_str(),data.size(),calibrationPath.c_str());
    const bool ok=file.good()&&!JS_IsException(value)&&parseCalibration(ctx,value,eyeCalibration);
    JS_FreeValue(ctx,value);JS_FreeContext(ctx);JS_FreeRuntime(runtime);
    if(!ok){fprintf(stderr,"Invalid eye calibration: %s\n",calibrationPath.c_str());return 13;}
    fprintf(stdout,"[eye] calibration loaded: %s\n",calibrationPath.c_str());
  }
  ScriptHost script;if(!loadScript(script,scriptPath))return 13;
  EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
  ScopeExit eglCleanup{[&]{if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}}};
  ScopeExit scriptCleanup{[&]{closeScript(script);currentScene=liveScene=nullptr;}};
  if(!initEgl(display,context,surface))return 1;
  vr::EVRInitError initError;vr::VR_Init(&initError,vr::VRApplication_Overlay);if(initError!=vr::VRInitError_None)return 2;
  ScopeExit vrCleanup{[&]{frameInput.stop();trackingAnchorValid=false;vr::VR_Shutdown();}};
  if(!controllerInput.init(std::filesystem::current_path().string(),argv[0]))fprintf(stderr,"[input] SteamVR actions unavailable; controller poses and overlay pointer events remain active\n");
  if(!frameInput.start())fprintf(stderr,"[input] raw Steam Frame WebSocket unavailable; action input remains active\n");
  auto *system=vr::VRSystem();auto *overlays=vr::VROverlay();
  int drm=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);auto *gbm=drm>=0?gbm_create_device(drm):nullptr;
  ScopeExit gbmCleanup{[&]{if(gbm)gbm_device_destroy(gbm);if(drm>=0)close(drm);}};
  createImage=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  destroyImage=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  imageRenderbuffer=(PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
  if(!gbm||!createImage||!destroyImage||!imageRenderbuffer)return 3;
  GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
  ScopeExit shaderCleanup{[&]{glDeleteProgram(program);glDeleteShader(v);glDeleteShader(f);}};
  glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 5;
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto anchor=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)anchor.m[r][3]+=anchor.m[r][1]*-.2f+anchor.m[r][2]*-1.f;
  trackingAnchor=anchor;trackingAnchorValid=true;
  fprintf(stdout,"anchor world=%f,%f,%f\n",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);fflush(stdout);
  vr::VROverlayHandle_t lasers[2]={vr::k_ulOverlayHandleInvalid,vr::k_ulOverlayHandleInvalid};bool laserShown[2]{};
  std::map<int,std::unique_ptr<Surface>> surfaces;std::map<int,vr::VROverlayHandle_t> panelOverlays;std::map<int,vr::Texture_t> panelTextures;
  ScopeExit overlayCleanup{[&]{
    for(auto laser:lasers)if(laser!=vr::k_ulOverlayHandleInvalid)overlays->DestroyOverlay(laser);
    for(auto &[id,item]:surfaces)destroySurface(*item,display);
    for(auto &[id,handle]:panelOverlays){overlays->HideOverlay(handle);overlays->ClearOverlayTexture(handle);overlays->DestroyOverlay(handle);}
  }};
  if(!createLaser(lasers[0],0)||!createLaser(lasers[1],1))return 9;
  std::array<GrabState,2> grabs{};
  auto syncSurfaces=[&]()->bool{
    std::vector<int> desired;
    for(const auto &object:script.scene.objects)if(object.visible)desired.push_back(object.id);
    for(auto it=surfaces.begin();it!=surfaces.end();){
      if(std::find(desired.begin(),desired.end(),it->first)==desired.end()){
        forgetObjectInteraction(it->first);destroySurface(*it->second,display);it=surfaces.erase(it);
      }else ++it;
    }
    for(int id:desired){
      const Object *object=nullptr;for(const auto &entry:script.scene.objects)if(entry.id==id){object=&entry;break;}
      if(!object)continue;
      const float width=std::max(.55f,object->scale*2.1f*objectRadius(*object));
      const int eyePixels=script.scene.assets[(size_t)object->asset-1].eyePixels;
      const bool interactive=!object->laserPassThrough;
      auto found=surfaces.find(id);
      if(found!=surfaces.end()&&(found->second->eyePixels!=eyePixels||found->second->interactive!=interactive)){
        pointerStates.erase(id);destroySurface(*found->second,display);surfaces.erase(found);found=surfaces.end();
      }
      if(found==surfaces.end()){
        auto created=std::make_unique<Surface>();
        if(!createSurface(*created,id,width,eyePixels,interactive,gbm,display,anchor)){destroySurface(*created,display);return false;}
        surfaces.emplace(id,std::move(created));
      }else if(std::fabs(found->second->width-width)>.0001f){
        found->second->width=width;
        overlays->SetOverlayWidthInMeters(found->second->overlay,width);
      }
    }
    return true;
  };
  if(!syncSurfaces())return 8;
  int panelHovered[2]={0,0};float panelCursorX[2]={0,0},panelCursorY[2]={0,0};
  auto syncPanels=[&](){
    for(auto it=panelOverlays.begin();it!=panelOverlays.end();){
      const int id=it->first;
      const bool exists=std::any_of(script.scene.panels.begin(),script.scene.panels.end(),[id](const Panel &p){return p.id==id;});
      if(exists){++it;continue;}
      overlays->HideOverlay(it->second);overlays->ClearOverlayTexture(it->second);overlays->DestroyOverlay(it->second);
      panelTextures.erase(id);
      for(int hand=0;hand<2;hand++)if(panelHovered[hand]==id){frameInput.addOverlayEvent("panel.hoverEnd",id,hand);panelHovered[hand]=0;}
      it=panelOverlays.erase(it);
    }
    for(auto &retired:script.scene.retiredPanels)panelDestroy(retired);
    script.scene.retiredPanels.clear();
    for(auto &panel:script.scene.panels){
      if(!panel.visible){if(panelOverlays.count(panel.id))overlays->HideOverlay(panelOverlays.at(panel.id));for(int h=0;h<2;h++)if(panelHovered[h]==panel.id){frameInput.addOverlayEvent("panel.hoverEnd",panel.id,h);panelHovered[h]=0;}}
      if(panel.dirty)panelUpload(panel);
      if(panelOverlays.count(panel.id))continue;
      vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
      std::string key="sf_overlay_engine.panel."+std::to_string(panel.id);
      if(overlays->CreateOverlay(key.c_str(),key.c_str(),&handle)!=vr::VROverlayError_None)continue;
      overlays->SetOverlayWidthInMeters(handle,panel.widthMeters);
      vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(handle,&bounds);
      if(panel.interactive){overlays->SetOverlayInputMethod(handle,vr::VROverlayInputMethod_Mouse);overlays->SetOverlayFlag(handle,vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,true);vr::HmdVector2_t scale{{(float)panel.width,(float)panel.height}};overlays->SetOverlayMouseScale(handle,&scale);}
      panelOverlays.emplace(panel.id,handle);
    }
  };
  syncPanels();
  glUseProgram(program);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glUniform1i(glGetUniformLocation(program,"material"),0);glEnable(GL_DEPTH_TEST);
  const GLint centerLocation=glGetUniformLocation(program,"center");
  const GLint rightLocation=glGetUniformLocation(program,"right");
  const GLint upLocation=glGetUniformLocation(program,"up");
  const GLint forwardLocation=glGetUniformLocation(program,"forward");
  const GLint panelRightLocation=glGetUniformLocation(program,"panelRight");
  const GLint panelUpLocation=glGetUniformLocation(program,"panelUp");
  const GLint panelForwardLocation=glGetUniformLocation(program,"panelForward");
  const GLint eyeLocation=glGetUniformLocation(program,"eye");
  const GLint panelHalfWidthLocation=glGetUniformLocation(program,"panelHalfWidth");
  setVec3(centerLocation,anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);
  setVec3(rightLocation,anchor.m[0][0],anchor.m[1][0],anchor.m[2][0]);
  setVec3(upLocation,anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]);
  setVec3(forwardLocation,-anchor.m[0][2],-anchor.m[1][2],-anchor.m[2][2]);
  GLint yawLocation=glGetUniformLocation(program,"yaw"),pitchLocation=glGetUniformLocation(program,"pitch"),rollLocation=glGetUniformLocation(program,"roll"),scaleLocation=glGetUniformLocation(program,"modelScale"),offsetLocation=glGetUniformLocation(program,"offset");
  GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff");
  GLint sheenLocation=glGetUniformLocation(program,"sheen");
  std::vector<Surface*> ready;
  double pickingMs=0,renderMs=0,finishMs=0,physicsMs=0;
  auto next=std::chrono::steady_clock::now();auto reportStart=next;int frame=0;uint64_t diagnosticFrames=0;engineStats.prime();
  auto lastAnimationTick=std::chrono::steady_clock::now();
  auto reconnectAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
  while(running){
    vr::VREvent_t runtimeEvent{};
    while(system->PollNextEvent(&runtimeEvent,sizeof(runtimeEvent)))if(runtimeEvent.eventType==vr::VREvent_Quit){system->AcknowledgeQuit_Exiting();fprintf(stdout,"[lifecycle] SteamVR quit; exiting\n");running=false;}
    if(!running)break;
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,.011f,poses,vr::k_unMaxTrackedDeviceCount);
    for(int hand=0;hand<2;hand++){
      auto &snapshot=controllers[hand];snapshot={};
      auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      if(device==vr::k_unTrackedDeviceIndexInvalid){if(frame%900==0)fprintf(stdout,"[input] hand=%d device=none\n",hand);continue;}
      const auto &pose=poses[device];
      if(frame%900==0){fprintf(stdout,"[input] hand=%d device=%u pose=%d\n",hand,device,pose.bPoseIsValid);fflush(stdout);}
      snapshot.connected=pose.bDeviceIsConnected;
      if(!snapshot.connected||!pose.bPoseIsValid)continue;
      snapshot.valid=true;
      float relative[3];for(int i=0;i<3;i++)relative[i]=pose.mDeviceToAbsoluteTracking.m[i][3]-anchor.m[i][3];
      for(int axis=0;axis<3;axis++){
        float value=0;for(int i=0;i<3;i++)value+=relative[i]*anchor.m[i][axis];
        if(axis==0)snapshot.x=value;else if(axis==1)snapshot.y=value;else snapshot.z=-value;
      }
      for(int row=0;row<3;row++)for(int col=0;col<3;col++){
        float value=0;for(int i=0;i<3;i++)value+=anchor.m[i][row]*pose.mDeviceToAbsoluteTracking.m[i][col];
        snapshot.rotation[row*3+col]=row==2?-value:value;
      }
    }
    if(frame%30==0){
      std::error_code ec;auto modified=std::filesystem::last_write_time(scriptPath,ec);
      if(!ec&&modified!=script.mtime){
        const bool loaded=loadScript(script,scriptPath,[&]{
          for(auto &[id,surface]:surfaces)destroySurface(*surface,display);
          surfaces.clear();pointerStates.clear();grabs={};
          for(auto &[id,handle]:panelOverlays){overlays->HideOverlay(handle);overlays->ClearOverlayTexture(handle);overlays->DestroyOverlay(handle);}
          panelOverlays.clear();panelTextures.clear();panelHovered[0]=panelHovered[1]=0;
          frameInput.resetScene();
          for(int h=0;h<2;h++){hoveredObject[h]=meshHoveredObject[h]=meshCurrentObject[h]=0;meshHitDistance[h]=2;meshLastHit[h]={};grabNeedsRelease[h]=true;panelCursorX[h]=panelCursorY[h]=0;}
          lastAnimationTick=std::chrono::steady_clock::now();
          fprintf(stdout,"[lifecycle] scene state reset\n");
        });
        if(!loaded)script.mtime=modified;
      }
    }
    const auto animationTick=std::chrono::steady_clock::now();
    const double animationDt=std::chrono::duration<double>(animationTick-lastAnimationTick).count();lastAnimationTick=animationTick;
    for(auto &object:script.scene.objects)if(object.animation.clip>=0){
      const auto &asset=script.scene.assets[size_t(object.asset)-1];
      if(animation::advance(object.animation,asset.animations[object.animation.clip],animationDt))evaluateModelPose(asset,object.animation,*object.animationPose);
    }
    if(!syncSurfaces()){running=false;break;}
    syncPanels();
    for(auto &[id,handle]:panelOverlays){
      vr::VREvent_t event{};
      while(overlays->PollNextOverlayEvent(handle,&event,sizeof(event))){
        const auto role=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
        const int hand=role==vr::TrackedControllerRole_LeftHand?0:role==vr::TrackedControllerRole_RightHand?1:-1;
        if(event.eventType==vr::VREvent_FocusLeave){for(int h=0;h<2;h++)if(panelHovered[h]==id){frameInput.addOverlayEvent("panel.hoverEnd",id,h);panelHovered[h]=0;}continue;}
        if(hand<0)continue;
        auto *panelData=findPanel(id);if(!panelData||!panelData->visible)continue;
        if(event.eventType==vr::VREvent_MouseMove){panelCursorX[hand]=std::clamp(event.data.mouse.x,0.f,(float)panelData->width);panelCursorY[hand]=std::clamp(event.data.mouse.y,0.f,(float)panelData->height);}
        const float x=panelCursorX[hand],y=panelCursorY[hand];
        if(event.eventType==vr::VREvent_MouseMove){if(panelHovered[hand]!=id){if(panelHovered[hand])frameInput.addOverlayEvent("panel.hoverEnd",panelHovered[hand],hand);panelHovered[hand]=id;frameInput.addPanelEvent("panel.hoverStart",id,hand,x,y);}frameInput.addPanelEvent("panel.hover",id,hand,x,y);}
        const std::string button=event.data.mouse.button==vr::VRMouseButton_Left?"left":event.data.mouse.button==vr::VRMouseButton_Right?"right":"other";
        if(event.eventType==vr::VREvent_MouseButtonDown)frameInput.addPanelEvent("panel.pointerDown",id,hand,x,y,button,true);
        if(event.eventType==vr::VREvent_MouseButtonUp)frameInput.addPanelEvent("panel.pointerUp",id,hand,x,y,button,false);
      }
    }
    for(auto &[id,surface]:surfaces){
      if(id==0)continue;
      vr::VREvent_t event{};
      auto &state=pointerStates[id];
      state.scrollX=state.scrollY=0;
      while(overlays->PollNextOverlayEvent(surface->overlay,&event,sizeof(event))){
        if(event.eventType==vr::VREvent_FocusLeave){state.valid=false;state.down=false;state.secondaryDown=false;if(state.hand>=0&&hoveredObject[state.hand]==id){frameInput.addOverlayEvent("overlay.surfaceHoverEnd",id,state.hand);hoveredObject[state.hand]=0;frameInput.setHoveredObject(state.hand,0);}state.hand=-1;continue;}
        if(event.eventType==vr::VREvent_ScrollSmooth){
          state.scrollX+=event.data.scroll.xdelta;
          state.scrollY+=event.data.scroll.ydelta;
          continue;
        }
        if(event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonDown||event.eventType==vr::VREvent_MouseButtonUp){
          state.valid=true;
          const auto eventRole=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
          const int eventHand=eventRole==vr::TrackedControllerRole_LeftHand?0:eventRole==vr::TrackedControllerRole_RightHand?1:-1;
          if(eventHand<0)continue;
          if(eventHand>=0&&event.eventType==vr::VREvent_MouseMove&&hoveredObject[eventHand]!=id){if(hoveredObject[eventHand])frameInput.addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[eventHand],eventHand);hoveredObject[eventHand]=id;frameInput.setHoveredObject(eventHand,id);frameInput.addOverlayEvent("overlay.surfaceHoverStart",id,eventHand);}
          frameInput.addOverlayEvent(event.eventType==vr::VREvent_MouseMove?"overlay.surfaceHover":"overlay.pointer",id,eventHand);
          state.x=(event.data.mouse.x-.5f)*surface->width;
          state.y=(.5f-event.data.mouse.y)*surface->width;
          if(event.eventType==vr::VREvent_MouseButtonDown){
            if(event.data.mouse.button==vr::VRMouseButton_Left)state.down=true;
            if(event.data.mouse.button==vr::VRMouseButton_Right)state.secondaryDown=true;
            auto role=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
            state.hand=role==vr::TrackedControllerRole_LeftHand?0:role==vr::TrackedControllerRole_RightHand?1:-1;
          }
          if(event.eventType==vr::VREvent_MouseButtonUp){
            if(event.data.mouse.button==vr::VRMouseButton_Left)state.down=false;
            if(event.data.mouse.button==vr::VRMouseButton_Right)state.secondaryDown=false;
          }
          if(event.eventType!=vr::VREvent_MouseMove){fprintf(stdout,"[input] object=%d event=%u button=%u device=%u hand=%d down=%d\n",id,event.eventType,event.data.mouse.button,event.trackedDeviceIndex,state.hand,state.down);fflush(stdout);}
        }
      }
    }
    if(script.scene.eyeTrackingRequested){
      vr::TrackedDevicePose_t headPoses[vr::k_unMaxTrackedDeviceCount]{};
      system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,headPoses,vr::k_unMaxTrackedDeviceCount);
      const auto &head=headPoses[vr::k_unTrackedDeviceIndex_Hmd];headBasisValid=head.bPoseIsValid&&head.bDeviceIsConnected;
      if(headBasisValid)for(int r=0;r<3;r++)for(int c=0;c<3;c++){
        float value=0;for(int k=0;k<3;k++)value+=anchor.m[k][r]*head.mDeviceToAbsoluteTracking.m[k][c];
        headBasis[r][c]=value*(r==2?-1:1)*(c==2?-1:1);
      }
    }else headBasisValid=false;
    controllerInput.poll(script.scene.eyeTrackingRequested,script.scene.foveationRequested);
    const bool streamWasStarted=frameInput.started();
    frameInput.poll();
    if(streamWasStarted&&!frameInput.started()){
      for(int h=0;h<2;h++){grabNeedsRelease[h]=true;if(panelHovered[h])frameInput.addOverlayEvent("panel.hoverEnd",panelHovered[h],h);panelHovered[h]=0;}
      for(const auto &object:script.scene.objects){forgetObjectInteraction(object.id);if(script.scene.physics)script.scene.physics->cancelThrow(object.id);}
    }
    for(int h=0;h<2;h++)if(!controllers[h].connected){
      frameInput.invalidateHand(h);
      for(auto &[id,pointer]:pointerStates)if(pointer.hand==h)pointer={};
      if(hoveredObject[h]){frameInput.addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[h],h);hoveredObject[h]=0;}
      if(panelHovered[h]){frameInput.addOverlayEvent("panel.hoverEnd",panelHovered[h],h);panelHovered[h]=0;}
    }
    const auto pickingStart=std::chrono::steady_clock::now();
    for(int hand=0;hand<2;hand++){
      int hit=0;float nearest=std::numeric_limits<float>::max();
      for(const auto &object:script.scene.objects){
        if(!object.visible)continue;
        const auto &asset=script.scene.assets[(size_t)object.asset-1];
        const float distance=meshHit(object,object.animationPose?object.animationPose->geometry:asset,controllers[hand],hand);
        if(distance<nearest){nearest=distance;hit=object.id;}
      }
      meshCurrentObject[hand]=hit;
      const auto now=std::chrono::steady_clock::now();
      if(hit)meshLastHit[hand]=now;
      else if(controllers[hand].connected&&meshHoveredObject[hand]&&std::any_of(script.scene.objects.begin(),script.scene.objects.end(),[&](const Object &o){return o.id==meshHoveredObject[hand]&&o.visible;})&&now-meshLastHit[hand]<std::chrono::milliseconds(100)){
        hit=meshHoveredObject[hand];nearest=meshHitDistance[hand];
      }
      if(hit!=meshHoveredObject[hand]){
        if(meshHoveredObject[hand])frameInput.addOverlayEvent("overlay.meshHoverEnd",meshHoveredObject[hand],hand);
        if(hit)frameInput.addOverlayEvent("overlay.meshHoverStart",hit,hand);
        meshHoveredObject[hand]=hit;
      }
      meshHitDistance[hand]=std::min(nearest,2.f);
    }
    pickingMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
    const auto reconnectNow=std::chrono::steady_clock::now();
    if(!frameInput.started()&&reconnectNow>=reconnectAt){frameInput.reconnect();reconnectAt=reconnectNow+std::chrono::seconds(1);}
    if(inputCallbackCtx){auto events=frameInput.takeCallbackEvents();for(const auto &event:events){JSValue value=inputEventValue(inputCallbackCtx,event);JSValue result=JS_Call(inputCallbackCtx,inputCallback,JS_UNDEFINED,1,&value);JS_FreeValue(inputCallbackCtx,value);if(JS_IsException(result))JS_FreeValue(inputCallbackCtx,JS_GetException(inputCallbackCtx));else JS_FreeValue(inputCallbackCtx,result);}}
    updateManipulation(script.scene,grabs,1.f/90.f);
    for(int hand=0;hand<2;hand++)if(grabs[hand].objectId)meshHitDistance[hand]=grabs[hand].laserDistance;
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);JS_FreeValue(script.ctx,dt);
      if(JS_IsException(result)){JSValue exception=JS_GetException(script.ctx);const char *message=JS_ToCString(script.ctx,exception);fprintf(stderr,"[js] update error: %s\n",message?message:"unknown");if(message)JS_FreeCString(script.ctx,message);JS_FreeValue(script.ctx,exception);JS_FreeValue(script.ctx,script.update);script.update=JS_UNDEFINED;}
      JS_FreeValue(script.ctx,result);
    }
    const auto physicsStart=std::chrono::steady_clock::now();
    try{updateScenePhysics(script.scene,animationDt);}
    catch(const std::exception &error){fprintf(stderr,"[physics] %s\n",error.what());running=false;break;}
    physicsMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-physicsStart).count();
    for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);
    for(auto &object:script.scene.objects)if(object.animationPose)uploadModelPose(*object.animationPose);
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
      for(int hand=0;hand<2;hand++){
        auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
        auto device=system->GetTrackedDeviceIndexForControllerRole(role);
        vr::TrackedDevicePose_t pose{};
        if(device!=vr::k_unTrackedDeviceIndexInvalid)pose=poses[device];
        updateLaser(lasers[hand],laserShown[hand],script.scene.laserVisible[hand],hand,pose,head,meshHitDistance[hand]);
      }
      const auto renderStart=std::chrono::steady_clock::now();
      float eyes[2][3];
      for(int i=0;i<2;i++){
        const auto eyeTransform=system->GetEyeToHeadTransform((vr::EVREye)i);
        for(int r=0;r<3;r++)eyes[i][r]=head.m[r][3]+head.m[r][0]*eyeTransform.m[0][3]+head.m[r][1]*eyeTransform.m[1][3]+head.m[r][2]*eyeTransform.m[2][3];
      }
      ready.clear();
      ready.reserve(surfaces.size());
      for(auto &panelData:script.scene.panels){auto h=panelOverlays.find(panelData.id);if(h==panelOverlays.end())continue;if(!panelData.texture||!panelData.visible){overlays->HideOverlay(h->second);continue;}auto &texture=panelTextures[panelData.id];texture={reinterpret_cast<void*>(static_cast<uintptr_t>(panelData.texture)),vr::TextureType_OpenGL,vr::ColorSpace_Gamma};auto panelError=overlays->SetOverlayTexture(h->second,&texture);if(panelError!=vr::VROverlayError_None)fprintf(stderr,"[panel] submit id=%d error=%d\n",panelData.id,(int)panelError);auto transform=anchor;transform.m[0][3]+=anchor.m[0][0]*panelData.x+anchor.m[0][1]*panelData.y-anchor.m[0][2]*panelData.z;transform.m[1][3]+=anchor.m[1][0]*panelData.x+anchor.m[1][1]*panelData.y-anchor.m[1][2]*panelData.z;transform.m[2][3]+=anchor.m[2][0]*panelData.x+anchor.m[2][1]*panelData.y-anchor.m[2][2]*panelData.z;if(panelData.mode=="arbitrary"){for(int row=0;row<3;row++)for(int col=0;col<3;col++){float value=0;for(int axis=0;axis<3;axis++)value+=anchor.m[row][axis]*panelData.rotation[axis*3+col];transform.m[row][col]=value;}}if(panelData.mode=="headLocked"){transform=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;transform.m[0][3]+=transform.m[0][0]*panelData.x+transform.m[0][1]*panelData.y-transform.m[0][2]*panelData.z;transform.m[1][3]+=transform.m[1][0]*panelData.x+transform.m[1][1]*panelData.y-transform.m[1][2]*panelData.z;transform.m[2][3]+=transform.m[2][0]*panelData.x+transform.m[2][1]*panelData.y-transform.m[2][2]*panelData.z;}if(panelData.mode=="viewerFacing"){float pr[3],pu[3],pf[3];faceViewer(transform,anchor,head,pr,pu,pf);for(int i=0;i<3;i++){transform.m[i][0]=pr[i];transform.m[i][1]=pu[i];transform.m[i][2]=-pf[i];}}overlays->SetOverlayTransformAbsolute(h->second,vr::TrackingUniverseStanding,&transform);overlays->ShowOverlay(h->second);}
      for(auto &[id,surface]:surfaces){
        const Object *target=nullptr;
        for(const auto &object:script.scene.objects)if(object.id==id){target=&object;break;}
        if(!target)continue;
        const float animatedWidth=std::max(.55f,target->scale*2.1f*objectRadius(*target));
        if(std::fabs(surface->width-animatedWidth)>.0001f){surface->width=animatedWidth;overlays->SetOverlayWidthInMeters(surface->overlay,animatedWidth);}
        float surfaceX=target->x,surfaceY=target->y,surfaceZ=target->z;
        auto panel=anchor;
        for(int r=0;r<3;r++)panel.m[r][3]+=anchor.m[r][0]*surfaceX+anchor.m[r][1]*surfaceY-anchor.m[r][2]*surfaceZ;
        float distanceSquared=0,facing=0;
        for(int r=0;r<3;r++){float delta=panel.m[r][3]-head.m[r][3];distanceSquared+=delta*delta;facing-=delta*head.m[r][2];}
        if(facing<.25f*std::sqrt(distanceSquared)-target->scale*(target->animationPose?objectRadius(*target):.6f)){
          if(surface->shown){overlays->HideOverlay(surface->overlay);surface->shown=false;}
          continue;
        }
        float panelRight[3],panelUp[3],panelForward[3];
        faceViewer(panel,anchor,head,panelRight,panelUp,panelForward);
        overlays->SetOverlayTransformAbsolute(surface->overlay,vr::TrackingUniverseStanding,&panel);
        setVec3(centerLocation,panel.m[0][3],panel.m[1][3],panel.m[2][3]);
        setVec3(panelRightLocation,panelRight[0],panelRight[1],panelRight[2]);
        setVec3(panelUpLocation,panelUp[0],panelUp[1],panelUp[2]);
        setVec3(panelForwardLocation,panelForward[0],panelForward[1],panelForward[2]);
        glUniform1f(panelHalfWidthLocation,surface->width*.5f);
        Buffer &buffer=surface->buffers[frame%3];
        glBindFramebuffer(GL_FRAMEBUFFER,buffer.fbo);glViewport(0,0,2*surface->eyePixels,surface->eyePixels);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        for(int i=0;i<2;i++){
          setVec3(eyeLocation,eyes[i][0],eyes[i][1],eyes[i][2]);glViewport(i*surface->eyePixels,0,surface->eyePixels,surface->eyePixels);
          const auto &targetGeometry=target->animationPose?target->animationPose->geometry:script.scene.assets[size_t(target->asset)-1];
          const auto targetBounds=projectedBounds(*target,targetGeometry,anchor,eyes[i],panelRight,panelUp,panelForward);
          const float surfaceCenter[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]};
          auto region=projectedScissor(targetBounds,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels);
          region.x+=i*surface->eyePixels;
          renderObjectLayers(id,[&](int targetId,int pass){
            for(const auto &object:script.scene.objects){
              if(!object.visible||(pass<0?object.id==targetId:object.id!=targetId))continue;
              const auto &geometry=object.animationPose?object.animationPose->geometry:script.scene.assets[size_t(object.asset)-1];
              if(pass<0&&!mayOcclude(projectedBounds(object,geometry,anchor,eyes[i],panelRight,panelUp,panelForward),targetBounds))continue;
              const auto &asset=script.scene.assets[(size_t)object.asset-1];
              glUniform1f(yawLocation,object.yaw);glUniform1f(pitchLocation,object.pitch);glUniform1f(rollLocation,object.roll);glUniform1f(scaleLocation,object.scale);
              glUniform3f(offsetLocation,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
              for(size_t primitive=0;primitive<asset.primitives.size();primitive++){
                const auto &part=asset.primitives[primitive];
                if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
                glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);
                glUniform3fv(sheenLocation,1,part.sheen);
                glBindBuffer(GL_ARRAY_BUFFER,object.animationPose?object.animationPose->geometry.primitives[primitive].vbo:part.vbo);
                glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
                glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
                glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
              }
            }
          },&region);
        }
        ready.push_back(surface.get());
      }
      const auto finishStart=std::chrono::steady_clock::now();
      renderMs+=std::chrono::duration<double,std::milli>(finishStart-renderStart).count();
      glFinish();
      finishMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-finishStart).count();
      glBindFramebuffer(GL_FRAMEBUFFER,0);
      for(auto *surface:ready){
        auto &buffer=surface->buffers[frame%3];
        vr::Texture_t texture{&buffer.handle,vr::TextureType_SharedTextureHandle,vr::ColorSpace_Gamma};
        auto error=overlays->SetOverlayTexture(surface->overlay,&texture);
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] submit id=%d error=%d\n",surface->objectId,(int)error);
        if(!surface->shown){overlays->ShowOverlay(surface->overlay);surface->shown=true;}
      }
      ++frame;
      if(frame%900==0){fprintf(stdout,"[profile] picking_ms=%.3f render_submit_ms=%.3f gpu_wait_ms=%.3f physics_ms=%.3f\n",pickingMs/900,renderMs/900,finishMs/900,physicsMs/900);pickingMs=renderMs=finishMs=physicsMs=0;}
      if(frame%900==0){auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-reportStart).count();reportStart=now;fprintf(stdout,"frames=%d fps=%.1f surfaces=%zu ready=%zu gl_error=%x laser_left=%.2f laser_right=%.2f held_left=%d held_right=%d\n",frame,900.0/seconds,surfaces.size(),ready.size(),(unsigned)glGetError(),meshHitDistance[0],meshHitDistance[1],grabs[0].objectId,grabs[1].objectId);fflush(stdout);}
    }
    if(engineStats.update(++diagnosticFrames)){
      uint64_t bytes=0;std::set<GLuint> countedDepth;
      for(const auto &[id,surface]:surfaces)for(const auto &buffer:surface->buffers){
        const uint64_t height=surface->eyePixels,width=2*height;
        if(buffer.bo)bytes+=uint64_t(gbm_bo_get_stride(buffer.bo))*height;
        if(buffer.depth&&countedDepth.insert(buffer.depth).second)bytes+=width*height*4; // Depth24 storage commonly occupies 32 bits.
      }
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.vbo)bytes+=part.vertices.size()*sizeof(float);if(part.texture)bytes+=uint64_t(part.width)*part.height*4;}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.vbo)bytes+=part.vertices.size()*sizeof(float);
      for(const auto &panel:script.scene.panels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      for(const auto &panel:script.scene.retiredPanels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      engineStats.gpuAllocatedEstimateBytes=bytes;
    }
    // Drop missed deadlines rather than rendering bursts to repay old frame debt.
    next=std::max(next+std::chrono::microseconds(11111),std::chrono::steady_clock::now());
    std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  return 0;
}
