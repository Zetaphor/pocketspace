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
#include <climits>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <stdexcept>
#include <utility>
#include <functional>
#include <quickjs.h>
#include <tuple>
#include <iterator>
#include "model_asset.h"
#include "model_shaders.h"
#ifdef SF_GPU_SYNC_EXPERIMENT
#include "../tests/static-vertex-experiment.h"
#include "../tests/ordinary-program-experiment.h"
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
#include "../tests/draw-uniforms-experiment.h"
#endif
#include "controller_input.h"
#include "frame_input.h"
#include "panel.h"
#include "panel_submission.h"
#include "native_scene_resources.h"
#include "eye_calibration.h"
#include "scope_exit.h"
#include "scene_lookup.h"
#include "physics_world.h"
#include "runtime_stats.h"
#include "group_buffer_budget.h"
#ifdef SF_GPU_SYNC_EXPERIMENT
#include "../tests/gpu-sync-experiment.h"
#endif

constexpr int kDefaultEyePixels=768;
struct Buffer {gbm_bo *bo=nullptr;int fd=-1;vr::SharedTextureHandle_t handle=0;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint color=0,depth=0,fbo=0;bool ownsDepth=false;};
struct Surface {int instanceId=0,objectId=0,eyePixels=kDefaultEyePixels,referenceEyePixels=kDefaultEyePixels,shrinkFrames=0;float width=.7f;vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid,stagingOverlay=vr::k_ulOverlayHandleInvalid;Buffer buffers[3];bool stagingShown=false,shown=false,interactive=false,panel=false,resolutionLimited=false;std::unique_ptr<Surface> previous;uint64_t groupBufferBytes=0;std::chrono::steady_clock::time_point resizeAfter{};};
static GroupBufferBudget groupBufferBudget;
static float surfaceCoverageWidth(float width,int allocated,int requested,bool grouped){
  return width*(grouped?std::max(1.f,float(allocated)/requested):float(allocated)/requested);
}
// Transfer retained snapshots before an in-flight output tile is removed.
// The surviving output must keep the complete old cohort until presentation.
static void preserveSurfaceFallback(Surface &removed,Surface &survivor){
  if(!removed.previous)return;auto *tail=&survivor;while(tail->previous)tail=tail->previous.get();tail->previous=std::move(removed.previous);
}
static PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC imageRenderbuffer=nullptr;
static std::atomic<bool> running{true};
enum class GrabButton { Trigger, Grip, Both };
#include "model_lod.h"
struct Object {
  int id,asset;float x,y,z,scale,yaw,pitch=0,roll=0;
  bool visible=true,laserPassThrough=true,grabbable=false,directGrab=true,autoManipulate=false;
  GrabButton grabButton=GrabButton::Trigger;
  bool laserYaw=false,laserPitchWithStickClick=false,depthAdjust=false;
  bool handPitch=false,handYaw=false,handRoll=false;
  animation::Playback animation;
  std::shared_ptr<ModelPose> animationPose;
  int renderGroup=0,renderTile=0;
  int lodLevel=0,container=0;
};
#include "scene_lights.h"
struct RenderGroupSplitPolicy {int maxSurfaces=4,targetEyePixels=2048;bool merge=false,rebalance=false;};
struct Scene {bool assetsReleased=false;std::vector<PointLight> lights;int nextLightId=1;std::set<int> containers;std::map<int,int> containerParents;int nextContainerId=1;std::unique_ptr<physics::World> physics;std::vector<Object> objects;std::map<int,RenderGroupSplitPolicy> renderGroupSplits;std::vector<ModelAsset> assets;std::vector<Panel> panels,retiredPanels;std::vector<int> interactiveIds;int nextId=1,nextPanelId=1;bool laserVisible[2]{};bool eyeTrackingRequested=false,foveationRequested=false;};
static size_t loadedAssetCount(const Scene &scene){return std::count_if(scene.assets.begin(),scene.assets.end(),[](const ModelAsset &asset){return !asset.path.empty();});}
static bool loadedAsset(const Scene &scene,int id){return id>0&&size_t(id)<=scene.assets.size()&&!scene.assets[size_t(id)-1].path.empty();}
static std::set<int> containerSubtree(const Scene &scene,int root){
 std::set<int> result;if(!scene.containers.count(root))return result;result.insert(root);
 bool changed=true;while(changed){changed=false;for(auto [child,parent]:scene.containerParents)if(result.count(parent))changed|=result.insert(child).second;}
 return result;
}
#include "model_instancing.h"
#include "spot_shadows.h"
#include "pbr_program.h"
#include "pbr_variants.h"
static int objectSurfaceKey(const Object &object){return object.renderGroup?-(object.renderGroup+256*object.renderTile):object.id;}
static int surfaceRenderGroup(int key){return key<0?((-key-1)%256)+1:0;}
static Scene *currentScene=nullptr,*liveScene=nullptr;
static int activeInstanceId=0;
// Installed only by the VR host; candidate scripts never reserve live resources.
static std::function<std::string(int)> reserveObjectOverlay;
static std::function<void(int)> releaseObjectOverlay;
static std::function<std::string(const Panel&)> reservePanelOverlay;
static std::function<void(int)> releasePanelOverlay;
static diagnostics::Sampler engineStats;
struct ControllerSnapshot {bool valid=false,connected=false;float x=0,y=0,z=0;float rotation[9]{};};
static ControllerSnapshot controllers[2];
static vr::HmdMatrix34_t trackingAnchor{};static bool trackingAnchorValid=false;
static controller_input::Input controllerInput;
static frame_input::Monitor frameInput;
static frame_input::Monitor *activeSceneInput=&frameInput;
static frame_input::Monitor &sceneInput(){return *activeSceneInput;}
static bool inputPollingRequested=false;static int hoveredObject[2]={0,0},meshHoveredObject[2]={0,0},meshCurrentObject[2]={0,0};static float meshHitDistance[2]={2.f,2.f};
static std::chrono::steady_clock::time_point meshLastHit[2]{};
struct PointerSnapshot {bool valid=false,down=false,secondaryDown=false;float x=0,y=0,scrollX=0,scrollY=0;int hand=-1;};
static std::map<int,PointerSnapshot> pointerStates;
static bool grabNeedsRelease[2]{};
struct InteractionState {
  std::map<int,PointerSnapshot> pointers;
  int hovered[2]{},meshHovered[2]{},meshCurrent[2]{};
  float meshDistance[2]{2.f,2.f};
  std::chrono::steady_clock::time_point meshLast[2]{};
  bool grabRelease[2]{};
};
static void swapActiveInteraction(InteractionState &state){
  pointerStates.swap(state.pointers);
  std::swap(hoveredObject,state.hovered);
  std::swap(meshHoveredObject,state.meshHovered);
  std::swap(meshCurrentObject,state.meshCurrent);
  std::swap(meshHitDistance,state.meshDistance);
  std::swap(meshLastHit,state.meshLast);
  std::swap(grabNeedsRelease,state.grabRelease);
}
static void resetActiveInteraction(){InteractionState empty;swapActiveInteraction(empty);}

static JSContext *inputCallbackCtx=nullptr;static JSValue inputCallback=JS_UNDEFINED;static int inputCallbackContainer=0;
static void clearInputCallback(JSContext *ctx){if(inputCallbackCtx==ctx){JS_FreeValue(ctx,inputCallback);inputCallback=JS_UNDEFINED;inputCallbackCtx=nullptr;inputCallbackContainer=0;}}
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
static void dispatchInputCallbacks(){
  if(inputCallbackCtx){auto events=sceneInput().takeCallbackEvents();for(const auto &event:events){
      if(!inputCallbackCtx)break;
      JSContext *ctx=inputCallbackCtx;JSValue callback=JS_DupValue(ctx,inputCallback),value=inputEventValue(ctx,event);
      JSValue result=JS_Call(ctx,callback,JS_UNDEFINED,1,&value);JS_FreeValue(ctx,value);JS_FreeValue(ctx,callback);
      if(JS_IsException(result))JS_FreeValue(ctx,JS_GetException(ctx));else JS_FreeValue(ctx,result);
    }}
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
static Panel *findPanel(int id){return currentScene?findSceneItem(currentScene->panels,id):nullptr;}
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
static JSValue jsPanelSetFont(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"panelLoadFont(panel,path,pixels)");id=panelArgumentId(ctx,argv[0]);auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*path=JS_ToCString(ctx,argv[1]);if(!path)return JS_EXCEPTION;p->fontPath=path;if(argc>2)JS_ToInt32(ctx,&p->fontPixels,argv[2]);JS_FreeCString(ctx,path);return JS_UNDEFINED;}
static JSValue jsPanelText(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED),x=0,y=0;if(argc<4||JS_ToInt32(ctx,&x,argv[1])<0||JS_ToInt32(ctx,&y,argv[2])<0)return JS_ThrowTypeError(ctx,"panel.text(id,x,y,text,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*t=JS_ToCString(ctx,argv[3]);if(!t)return JS_EXCEPTION;int r=argc>4?colorPart(ctx,argv[4],255):255,g=argc>5?colorPart(ctx,argv[5],255):255,b=argc>6?colorPart(ctx,argv[6],255):255,a=argc>7?colorPart(ctx,argv[7],255):255;bool ok=panelText(*p,x,y,t,r,g,b,a,argc>8?colorPart(ctx,argv[8],p->fontPixels):p->fontPixels);JS_FreeCString(ctx,t);return JS_NewBool(ctx,ok);}
static JSValue jsPanelSetTransform(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<5)return JS_ThrowTypeError(ctx,"panel.setTransform(id,mode,x,y,z)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*mode=JS_ToCString(ctx,argv[1]);if(mode){p->mode=mode;JS_FreeCString(ctx,mode);}double x=0,y=0,z=0;if(JS_ToFloat64(ctx,&x,argv[2])<0||JS_ToFloat64(ctx,&y,argv[3])<0||JS_ToFloat64(ctx,&z,argv[4])<0)return JS_EXCEPTION;p->x=(float)x;p->y=(float)y;p->z=(float)z;return JS_UNDEFINED;}
static JSValue jsPanelSetRotation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<10)return JS_ThrowTypeError(ctx,"panel.setRotation(panel,pitch,yaw,roll,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");for(int i=0;i<9;i++){double value=0;if(JS_ToFloat64(ctx,&value,argv[i+1])<0)return JS_EXCEPTION;p->rotation[i]=(float)value;}return JS_UNDEFINED;}
static JSValue jsPanelRemove(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(argc<1)return JS_ThrowTypeError(ctx,"removePanel(panel)");int id=panelArgumentId(ctx,argv[0]);auto &panels=currentScene->panels;auto it=std::find_if(panels.begin(),panels.end(),[id](const Panel &p){return p.id==id;});if(it==panels.end())return JS_ThrowRangeError(ctx,"unknown panel");currentScene->retiredPanels.push_back(std::move(*it));panels.erase(it);return JS_UNDEFINED;}
static JSValue jsPanelVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"panelSetVisible(panel,visible)");id=panelArgumentId(ctx,argv[0]);auto*p=findPanel(id);if(p)p->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;}
static JSValue jsPanelPresent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=panelArgumentId(ctx,argc?argv[0]:JS_UNDEFINED);if(argc<1)return JS_EXCEPTION;auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");p->dirty=true;return JS_UNDEFINED;}
static Object *findObject(int id){
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
  // Compatibility no-op: rendering allocates independent or grouped surfaces.
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
static void updateScenePhysics(Scene &scene,double elapsed){
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
static void forgetObjectInteraction(int id){
  pointerStates.erase(id);
  for(int h=0;h<2;h++){
    if(hoveredObject[h]==id){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",id,h);hoveredObject[h]=0;frameInput.setHoveredObject(h,0);}
    if(meshHoveredObject[h]==id){sceneInput().addOverlayEvent("overlay.meshHoverEnd",id,h);meshHoveredObject[h]=0;}
    if(meshCurrentObject[h]==id){meshCurrentObject[h]=0;meshHitDistance[h]=2;meshLastHit[h]={};}
  }
}
static void removeSceneObject(int id){
  auto *object=findObject(id);if(!object)return;
  std::erase_if(currentScene->lights,[&](const PointLight &light){return light.objectId==id;});
  if(currentScene->physics)currentScene->physics->remove(id);
  auto &ids=currentScene->interactiveIds;
  ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
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

struct ScriptHost {int instanceId=0;JSRuntime *rt=nullptr;JSContext *ctx=nullptr;JSValue update=JS_UNDEFINED,callback=JS_UNDEFINED;int callbackContainer=0;std::filesystem::file_time_type mtime{};Scene scene;InteractionState interaction;std::unique_ptr<frame_input::Monitor> inputQueue=std::make_unique<frame_input::Monitor>(0);};
static void bind(JSContext *ctx,const char *name,JSCFunction *function,int argc){
  JSValue global=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global,name,JS_NewCFunction(ctx,function,name,argc));JS_FreeValue(ctx,global);
}
static void closeScript(ScriptHost &s){
  for(auto &asset:s.scene.assets)deleteModel(asset);
  for(auto &panel:s.scene.panels)panelDestroy(panel);
  for(auto &panel:s.scene.retiredPanels)panelDestroy(panel);
  if(currentScene==&s.scene)currentScene=nullptr;
  if(liveScene==&s.scene)liveScene=nullptr;
  if(s.ctx){clearInputCallback(s.ctx);JS_FreeValue(s.ctx,s.callback);JS_FreeValue(s.ctx,s.update);JS_FreeContext(s.ctx);}if(s.rt)JS_FreeRuntime(s.rt);
  if(activeSceneInput==s.inputQueue.get())activeSceneInput=&frameInput;
  s={};
}
static bool loadScript(ScriptHost &active,const std::string &path,const std::function<void()> &beforeCommit={}){
  std::ifstream file(path);if(!file){fprintf(stderr,"script unreadable: %s\n",path.c_str());return false;}
  std::ostringstream stream;stream<<file.rdbuf();std::string source=stream.str();
  ScriptHost candidate;candidate.rt=JS_NewRuntime();if(!candidate.rt)return false;
  JS_SetMemoryLimit(candidate.rt,128*1024*1024);
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
  bind(candidate.ctx,"enableObjectSurface",jsEnableObjectSurface,1);
  bind(candidate.ctx,"setObjectInteraction",jsSetObjectInteraction,2);
  bind(candidate.ctx,"getPointer",jsGetPointer,0);
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
  bind(candidate.ctx,"createSceneContainer",jsCreateSceneContainer,1);
  bind(candidate.ctx,"setSceneContainerParent",jsSetSceneContainerParent,2);
  bind(candidate.ctx,"unloadModel",jsUnloadModel,1);
  bind(candidate.ctx,"disposeSceneContainer",jsDisposeSceneContainer,1);
  bind(candidate.ctx,"getSceneContainerState",jsGetSceneContainerState,1);
  bind(candidate.ctx,"setObjectContainer",jsSetObjectContainer,2);
  bind(candidate.ctx,"setPanelContainer",jsSetPanelContainer,2);
  JSValue result=JS_Eval(candidate.ctx,source.c_str(),source.size(),path.c_str(),JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(candidate.ctx);const char *message=JS_ToCString(candidate.ctx,exception);
    fprintf(stderr,"[js] load error: %s\n",message?message:"unknown");if(message)JS_FreeCString(candidate.ctx,message);
    JS_FreeValue(candidate.ctx,exception);JS_FreeValue(candidate.ctx,result);closeScript(candidate);inputCallbackCtx=previousCallbackCtx;inputCallback=previousCallback;inputCallbackContainer=previousCallbackContainer;currentScene=&active.scene;return false;
  }
  JS_FreeValue(candidate.ctx,result);
  JSValue global=JS_GetGlobalObject(candidate.ctx);candidate.update=JS_GetPropertyStr(candidate.ctx,global,"update");JS_FreeValue(candidate.ctx,global);
  std::error_code ec;candidate.mtime=std::filesystem::last_write_time(path,ec);
  if(previousCallbackCtx)JS_FreeValue(previousCallbackCtx,previousCallback);
  if(beforeCommit)beforeCommit();
  const int instanceId=active.instanceId;
  const bool queueActive=activeSceneInput==active.inputQueue.get();
  closeScript(active);active=std::move(candidate);active.instanceId=instanceId;
  if(queueActive)activeSceneInput=active.inputQueue.get();
  currentScene=liveScene=&active.scene;
  fprintf(stdout,"[js] loaded %s objects=%zu assets=%zu\n",path.c_str(),active.scene.objects.size(),active.scene.assets.size());fflush(stdout);return true;
}
#include "native_resource_key.h"
#include "script_registry.h"
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
  if(surface.previous){destroySurface(*surface.previous,display);surface.previous.reset();}
  if(surface.stagingOverlay!=vr::k_ulOverlayHandleInvalid){
    vr::VROverlay()->ClearOverlayTexture(surface.stagingOverlay);
    vr::VROverlay()->DestroyOverlay(surface.stagingOverlay);surface.stagingOverlay=vr::k_ulOverlayHandleInvalid;
  }
  if(surface.overlay!=vr::k_ulOverlayHandleInvalid){
    vr::VROverlay()->ClearOverlayTexture(surface.overlay);
    vr::VROverlay()->DestroyOverlay(surface.overlay);
  }
  for(auto &buffer:surface.buffers)destroyBuffer(buffer,display);
  groupBufferBudget.release(surface.groupBufferBytes);surface.groupBufferBytes=0;
  surface.overlay=vr::k_ulOverlayHandleInvalid;
}
static bool createSurface(Surface &surface,int instanceId,int id,float width,int eyePixels,bool interactive,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &anchor,const std::string &suffix={}){
  if(id<0&&!surface.groupBufferBytes){
    const auto bytes=GroupBufferBudget::bytes(eyePixels);
    if(!groupBufferBudget.reserve(bytes))return false;
    surface.groupBufferBytes=bytes;
  }
  surface.instanceId=instanceId;surface.objectId=id;surface.width=width;surface.eyePixels=surface.referenceEyePixels=eyePixels;surface.interactive=interactive;
  const std::string key=objectOverlayKey({instanceId,id},suffix);
  auto *overlays=vr::VROverlay();
  auto error=vr::VROverlayError_None;
  if(surface.overlay==vr::k_ulOverlayHandleInvalid){
    error=overlays->CreateOverlay(key.c_str(),key.c_str(),&surface.overlay);
    if(error!=vr::VROverlayError_None){fprintf(stderr,"[surface] create id=%d error=%d\n",id,(int)error);return false;}
  }
  error=overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_SideBySide_Parallel,true);
  if(error!=vr::VROverlayError_None)return false;
  // GL blending stores premultiplied colors; preserve that convention when
  // the compositor filters transparent silhouette/cutout borders.
  error=overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_IsPremultiplied,true);
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
  if(id<0){
    if(surface.stagingOverlay==vr::k_ulOverlayHandleInvalid){
      error=overlays->CreateOverlay((key+".stage").c_str(),(key+".stage").c_str(),&surface.stagingOverlay);
      if(error!=vr::VROverlayError_None)return false;
    }
    if(overlays->SetOverlayFlag(surface.stagingOverlay,vr::VROverlayFlags_SideBySide_Parallel,true)!=vr::VROverlayError_None)return false;
    if(overlays->SetOverlayFlag(surface.stagingOverlay,vr::VROverlayFlags_IsPremultiplied,true)!=vr::VROverlayError_None)return false;
    if(overlays->SetOverlayWidthInMeters(surface.stagingOverlay,width)!=vr::VROverlayError_None)return false;
    overlays->SetOverlayTextureBounds(surface.stagingOverlay,&bounds);
    overlays->SetOverlayTransformAbsolute(surface.stagingOverlay,vr::TrackingUniverseStanding,&anchor);
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
// Keep the overlay handle stable while replacing its three color buffers.
// Called between frames for this surface, after the previous frame's glFinish.
static bool resizeSurfaceBuffers(Surface &surface,int pixels,gbm_device *gbm,EGLDisplay display){
  std::array<Buffer,3> next{};GLuint depth=0;
  for(auto &buffer:next){
    if(!makeBuffer(buffer,gbm,display,pixels,depth)){for(auto &item:next)destroyBuffer(item,display);return false;}
    depth=buffer.depth;
  }
  vr::VROverlay()->ClearOverlayTexture(surface.overlay);
  for(int i=0;i<3;i++){destroyBuffer(surface.buffers[i],display);surface.buffers[i]=next[i];}
  surface.eyePixels=pixels;
  if(surface.interactive){
    vr::VROverlayIntersectionMaskPrimitive_t mask{};mask.m_nPrimitiveType=vr::OverlayIntersectionPrimitiveType_Circle;
    mask.m_Primitive.m_Circle.m_flCenterX=mask.m_Primitive.m_Circle.m_flCenterY=.5f;
    mask.m_Primitive.m_Circle.m_flRadius=std::min(.707107f,.3f*surface.referenceEyePixels/pixels);
    vr::VROverlay()->SetOverlayIntersectionMask(surface.overlay,&mask,1);
  }
  return true;
}
// Shared groups resize as their projected bounds move. Keep the old overlay
// visible until the replacement has a finished texture and has been shown.
static bool replaceGroupedSurface(Surface &surface,int pixels,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &transform){
  if(surface.previous)return true;
  static uint64_t generation=0;
  Surface replacement;
  if(!createSurface(replacement,surface.instanceId,surface.objectId,surface.width,pixels,false,gbm,display,transform,".resize."+std::to_string(++generation))){destroySurface(replacement,display);return false;}
  replacement.referenceEyePixels=surface.referenceEyePixels;
  replacement.resolutionLimited=surface.resolutionLimited;
  replacement.previous=std::make_unique<Surface>(std::move(surface));
  surface=std::move(replacement);
  return true;
}
template<class Overlays>
static vr::EVROverlayError hideSurfaceFront(Overlays &overlays,Surface &surface){
  if(surface.shown){const auto error=overlays.HideOverlay(surface.overlay);if(error!=vr::VROverlayError_None)return error;surface.shown=false;}
  if(surface.stagingShown){const auto error=overlays.HideOverlay(surface.stagingOverlay);if(error!=vr::VROverlayError_None)return error;surface.stagingShown=false;}
  return vr::VROverlayError_None;
}
template<class Overlays>
static vr::EVROverlayError hideSurface(Overlays &overlays,Surface &surface){
  const auto error=hideSurfaceFront(overlays,surface);if(error!=vr::VROverlayError_None)return error;
  return surface.previous?hideSurface(overlays,*surface.previous):vr::VROverlayError_None;
}
template<class Overlays>
static vr::EVROverlayError prepareSurface(Overlays &overlays,Surface &surface,float width,const vr::HmdMatrix34_t &transform){
  const bool staged=surface.stagingOverlay!=vr::k_ulOverlayHandleInvalid;
  const auto handle=staged?surface.stagingOverlay:surface.overlay;
  if(surface.stagingShown){
    const auto error=overlays.HideOverlay(handle);if(error!=vr::VROverlayError_None)return error;
    surface.stagingShown=false;
  }
  if(staged||std::fabs(surface.width-width)>.000001f){
    const auto error=overlays.SetOverlayWidthInMeters(handle,width);if(error!=vr::VROverlayError_None)return error;
  }
  const auto error=overlays.SetOverlayTransformAbsolute(handle,vr::TrackingUniverseStanding,&transform);
  if(error==vr::VROverlayError_None)surface.width=width;
  return error;
}
template<class Overlays,class Retire>
static vr::EVROverlayError submitSurface(Overlays &overlays,Surface &surface,int frame,Retire retire){
  const bool staged=surface.stagingOverlay!=vr::k_ulOverlayHandleInvalid;
  const auto handle=staged?surface.stagingOverlay:surface.overlay;
  auto &buffer=surface.buffers[frame%3];
  vr::Texture_t texture{&buffer.handle,vr::TextureType_SharedTextureHandle,vr::ColorSpace_Gamma};
  auto error=overlays.SetOverlayTexture(handle,&texture);
  if(error!=vr::VROverlayError_None)return error;
  if(staged||!surface.shown){
    error=overlays.ShowOverlay(handle);if(error!=vr::VROverlayError_None)return error;
    if(staged){
      surface.stagingShown=true;
      if(surface.shown){error=overlays.HideOverlay(surface.overlay);if(error!=vr::VROverlayError_None)return error;}
      std::swap(surface.overlay,surface.stagingOverlay);surface.stagingShown=false;
    }
    surface.shown=true;
  }
  if(surface.previous){retire(*surface.previous);surface.previous.reset();}
  return vr::VROverlayError_None;
}
static int groupBufferPixels(int required,int maximum){
  return int(std::min(double(maximum),std::ceil(required*1.25/256)*256));
}
static GLuint compile(GLenum type,const char *source) {
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);fprintf(stderr,"shader error: %s\n",log);}
  return shader;
}
#ifdef SF_GPU_SYNC_EXPERIMENT
#include "../tests/opaque-shader-experiment.h"
#endif
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
static vr::HmdMatrix34_t panelTransform(const Panel &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head){
  auto transform=panel.mode=="headLocked"?head:anchor;
  for(int row=0;row<3;row++)transform.m[row][3]+=transform.m[row][0]*panel.x+transform.m[row][1]*panel.y-transform.m[row][2]*panel.z;
  if(panel.mode=="arbitrary")for(int row=0;row<3;row++)for(int col=0;col<3;col++){
    float value=0;for(int axis=0;axis<3;axis++)value+=anchor.m[row][axis]*panel.rotation[axis*3+col];transform.m[row][col]=value;
  }
  if(panel.mode=="viewerFacing"){float right[3],up[3],forward[3];faceViewer(transform,anchor,head,right,up,forward);}
  return transform;
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
    Object *object=findSceneItem(scene.objects,grab.objectId);
    if(previousGrab&&(!object||!hand.connected||!object->visible||!object->autoManipulate||!object->grabbable)){if(scene.physics)scene.physics->release(previousGrab,false);sceneInput().addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;grabNeedsRelease[h]=true;}
    if(object){
      auto state=pointerStates.find(object->id);
      const bool buttonDown=frameGrabDown(h,object->grabButton);
      const bool nativeDown=object->grabButton!=GrabButton::Grip&&state!=pointerStates.end()&&state->second.hand==h&&state->second.down;
      if(grab.rawGrab)grab.releaseFrames=buttonDown?0:grab.releaseFrames+1;
      if((grab.rawGrab&&grab.releaseFrames>=5)||(!grab.rawGrab&&!nativeDown&&!buttonDown)){
        if(scene.physics)scene.physics->release(previousGrab,true);
        sceneInput().addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;
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
          sceneInput().addOverlayEvent("overlay.grabStart",candidate.id,h);object=&candidate;break;
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
      sceneInput().addOverlayEvent("overlay.grabMove",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
      if(std::fabs(object->x-oldX)+std::fabs(object->y-oldY)+std::fabs(object->z-oldZ)>.00001f)sceneInput().addOverlayEvent("overlay.move",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
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
      if(std::fabs(object->pitch-oldPitch)+std::fabs(object->yaw-oldYaw)+std::fabs(object->roll-oldRoll)>.00001f)sceneInput().addOverlayEvent("overlay.rotate",object->id,h,0,0,0,object->pitch-oldPitch,object->yaw-oldYaw,object->roll-oldRoll);
    }
  }
}
struct ProjectedBounds {float x0=INFINITY,x1=-INFINITY,y0=INFINITY,y1=-INFINITY,near=INFINITY,far=-INFINITY;bool valid=true;};
using WorldBounds=std::array<std::array<float,3>,8>;
template<class Geometry> static WorldBounds worldBounds(const Object &object,const Geometry &geometry,const vr::HmdMatrix34_t &anchor){
  WorldBounds result;float rotation[9];objectRotationMatrix(object.pitch,object.yaw,object.roll,rotation);
  for(int corner=0;corner<8;corner++){
    float local[3],rotated[3]{},position[3]={object.x,object.y,object.z};
    for(int axis=0;axis<3;axis++)local[axis]=(corner&(1<<axis))?geometry.high[axis]:geometry.low[axis];
    for(int row=0;row<3;row++)for(int axis=0;axis<3;axis++)rotated[row]+=rotation[row*3+axis]*local[axis];
    for(int axis=0;axis<3;axis++)position[axis]+=object.scale*rotated[axis]*(axis==2?-1.f:1.f);
    for(int row=0;row<3;row++)result[corner][row]=anchor.m[row][3]+anchor.m[row][0]*position[0]+anchor.m[row][1]*position[1]-anchor.m[row][2]*position[2];
  }
  return result;
}
static ProjectedBounds projectWorldBounds(const WorldBounds &world,const float eye[3],const float right[3],const float up[3],const float forward[3]){
  ProjectedBounds result;
  for(const auto &corner:world){
    float ray[3];for(int row=0;row<3;row++)ray[row]=corner[row]-eye[row];
    float x=0,y=0,d=0;for(int axis=0;axis<3;axis++){x+=ray[axis]*right[axis];y+=ray[axis]*up[axis];d+=ray[axis]*forward[axis];}
    if(d<=.05f){result.valid=false;return result;}
    result.x0=std::min(result.x0,x/d);result.x1=std::max(result.x1,x/d);result.y0=std::min(result.y0,y/d);result.y1=std::max(result.y1,y/d);
    result.near=std::min(result.near,d);result.far=std::max(result.far,d);
  }
  return result;
}
static ProjectedBounds projectedBounds(const Object &object,const ModelAsset &geometry,const vr::HmdMatrix34_t &anchor,const float eye[3],const float right[3],const float up[3],const float forward[3]){
  return projectWorldBounds(worldBounds(object,geometry,anchor),eye,right,up,forward);
}
struct FrameBoundsCache {
  struct Entry {WorldBounds world;ProjectedBounds common[2];};
  const Scene *scene=nullptr;std::vector<Entry> entries;
  // Refresh only after callbacks, manipulation, animation and the physics barrier.
  // Object references and entries remain stable for the following render phase.
  void update(const Scene &source,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3]){
    scene=&source;entries.resize(source.objects.size());
    const bool grouped=std::any_of(source.objects.begin(),source.objects.end(),[](const Object &object){return object.visible&&object.renderGroup;});
    auto plane=anchor;float right[3],up[3],forward[3];if(grouped)faceViewer(plane,anchor,head,right,up,forward);
    for(size_t i=0;i<source.objects.size();i++){
      const auto &object=source.objects[i];if(!object.visible)continue;
      const auto &geometry=object.animationPose?object.animationPose->geometry:source.assets[size_t(object.asset)-1];
      auto &entry=entries[i];entry.world=worldBounds(object,geometry,anchor);
      if(grouped)for(int eye=0;eye<2;eye++)entry.common[eye]=projectWorldBounds(entry.world,eyes[eye],right,up,forward);
    }
  }
  const Entry &entry(const Object &object)const{return entries[size_t(&object-scene->objects.data())];}
  const ProjectedBounds &common(const Object &object,int eye)const{return entry(object).common[eye];}
  ProjectedBounds project(const Object &object,const float eye[3],const float right[3],const float up[3],const float forward[3])const{
    return projectWorldBounds(entry(object).world,eye,right,up,forward);
  }
};
static bool mayOcclude(const ProjectedBounds &occluder,const ProjectedBounds &target){
  if(!occluder.valid||!target.valid)return true;
  constexpr float margin=1e-4f;
  return occluder.near<=target.far+margin&&occluder.x1+margin>=target.x0&&occluder.x0-margin<=target.x1&&occluder.y1+margin>=target.y0&&occluder.y0-margin<=target.y1;
}
struct ScissorRegion {int x=0,y=0,width=0,height=0;};
#ifdef SF_GPU_SYNC_EXPERIMENT
#include "../tests/primitive-visibility-experiment.h"
#include "../tests/cluster-visibility-experiment.h"

#endif
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
// Crop only transparent margins: physical width and pixel count scale together,
// leaving the original sample spacing and centered pixel grid unchanged.
static int objectCropPixels(const ProjectedBounds bounds[2],const float center[3],const float eyes[2][3],const float right[3],const float up[3],const float forward[3],float referenceWidth,int referencePixels){
  int extent=0;
  for(int eye=0;eye<2;eye++){
    if(!bounds[eye].valid)return referencePixels;
    auto r=projectedScissor(bounds[eye],center,eyes[eye],right,up,forward,referenceWidth*.5f,referencePixels);
    if(!r.width||!r.height)return referencePixels;
    const int mid=referencePixels/2;
    extent=std::max({extent,mid-r.x,r.x+r.width-mid,mid-r.y,r.y+r.height-mid});
  }
  // Extra transparent border and 64-pixel buckets limit resize churn.
  return std::min(referencePixels,std::max(128,((2*extent+32+63)/64)*64));
}
struct GroupLayout {float width=.55f;int pixels=128;ProjectedBounds bounds[2];bool limited=false;};
// Project all members onto one plane for both eyes. Match the most demanding
// member's reference angular density, rather than stretching a 768px texture.
static GroupLayout groupLayout(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const float center[3],const float right[3],const float up[3],const float forward[3],float fallbackWidth,int maxPixels=4096,const FrameBoundsCache *cache=nullptr){
  GroupLayout result;float extent=0,density=0;
  for(int eye=0;eye<2;eye++){
    float plane=0,cx=0,cy=0;for(int axis=0;axis<3;axis++){const float delta=center[axis]-eyes[eye][axis];plane+=delta*forward[axis];cx+=delta*right[axis];cy+=delta*up[axis];}
    for(const auto *object:members){
      const auto &asset=scene.assets[size_t(object->asset)-1];const auto &geometry=object->animationPose?object->animationPose->geometry:asset;
      const auto bounds=cache?cache->common(*object,eye):projectedBounds(*object,geometry,anchor,eyes[eye],right,up,forward);
      auto &combined=result.bounds[eye];combined.valid=combined.valid&&bounds.valid;
      combined.x0=std::min(combined.x0,bounds.x0);combined.x1=std::max(combined.x1,bounds.x1);combined.y0=std::min(combined.y0,bounds.y0);combined.y1=std::max(combined.y1,bounds.y1);combined.near=std::min(combined.near,bounds.near);combined.far=std::max(combined.far,bounds.far);
      if(bounds.valid&&plane>.05f)extent=std::max({extent,std::abs(plane*bounds.x0-cx),std::abs(plane*bounds.x1-cx),std::abs(plane*bounds.y0-cy),std::abs(plane*bounds.y1-cy)});
      // Account for the common plane's obliquity. Its minimum angular
      // magnification is plane/cos(theta), so matching an object's own facing
      // plane requires referenceDensity * axialDepth / plane, not radial range.
      float axialDepth=0;for(int axis=0;axis<3;axis++){const float delta=anchor.m[axis][3]+anchor.m[axis][0]*object->x+anchor.m[axis][1]*object->y-anchor.m[axis][2]*object->z-head.m[axis][3];axialDepth+=delta*forward[axis];}
      const float referenceWidth=std::max(.55f,object->scale*2.1f*objectRadius(*object));
      density=std::max(density,asset.eyePixels/referenceWidth*std::max(.05f,axialDepth)/std::max(.05f,plane));
    }
  }
  density=std::max(1.f,density);
  result.width=(!result.bounds[0].valid||!result.bounds[1].valid)?fallbackWidth:std::max(.01f,2*extent+32/density);
  const double wanted=std::ceil(double(result.width)*density/64)*64;
  result.limited=wanted>maxPixels;
  result.pixels=int(std::clamp(wanted,128.0,double(maxPixels)));
  return result;
}
struct GroupPlane {
  vr::HmdMatrix34_t transform{};
  float sceneOrigin[3]{},right[3]{},up[3]{},forward[3]{};
};
// All grouped tiles lie on one plane. Otherwise late head-pose reprojection
// moves an occluder's color and its cutout in another tile by different amounts.
static GroupPlane groupPlane(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache *cache=nullptr){
  GroupPlane result;result.transform=anchor;
  faceViewer(result.transform,anchor,head,result.right,result.up,result.forward);
  float lowX=INFINITY,highX=-INFINITY,lowY=INFINITY,highY=-INFINITY;bool valid=true;
  for(int eye=0;eye<2;eye++){
    float distance=0,eyeX=0,eyeY=0;
    for(int axis=0;axis<3;axis++){const float delta=eyes[eye][axis]-anchor.m[axis][3];distance-=delta*result.forward[axis];eyeX+=delta*result.right[axis];eyeY+=delta*result.up[axis];}
    if(distance<=.05f){valid=false;break;}
    for(const auto *object:members){
      const auto &asset=scene.assets[size_t(object->asset)-1];const auto &geometry=object->animationPose?object->animationPose->geometry:asset;
      const auto bounds=cache?cache->common(*object,eye):projectedBounds(*object,geometry,anchor,eyes[eye],result.right,result.up,result.forward);
      if(!bounds.valid){valid=false;break;}
      lowX=std::min(lowX,eyeX+distance*bounds.x0);highX=std::max(highX,eyeX+distance*bounds.x1);
      lowY=std::min(lowY,eyeY+distance*bounds.y0);highY=std::max(highY,eyeY+distance*bounds.y1);
    }
    if(!valid)break;
  }
  if(valid&&std::isfinite(lowX)&&std::isfinite(highX)&&std::isfinite(lowY)&&std::isfinite(highY)){
    const float x=(lowX+highX)*.5f,y=(lowY+highY)*.5f;
    for(int row=0;row<3;row++)result.transform.m[row][3]+=result.right[row]*x+result.up[row]*y;
  }
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++)result.sceneOrigin[axis]+=(result.transform.m[row][3]-anchor.m[row][3])*anchor.m[row][axis]*(axis==2?-1.f:1.f);
  return result;
}
struct GroupSplitPlan {
  std::vector<const Object*> members[2];int pixels[2]{};float widths[2]{};
  uint64_t parentPixelsSquared=0,childPixelsSquared=0;
  explicit operator bool()const{return !members[0].empty()&&!members[1].empty();}
};
static bool finiteGroupBounds(const std::vector<const Object*> &members,const FrameBoundsCache &cache){
  for(const auto *object:members)for(int eye=0;eye<2;eye++){
    const auto &b=cache.common(*object,eye);
    if(!object->visible||!b.valid||!std::isfinite(b.x0)||!std::isfinite(b.x1)||!std::isfinite(b.y0)||!std::isfinite(b.y1)||!std::isfinite(b.near)||!std::isfinite(b.far))return false;
  }
  return !members.empty();
}
static GroupLayout tileLayout(const std::vector<const Object*> &items,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum){
  const auto plane=groupPlane(items,scene,anchor,head,eyes,&cache);
  const float center[3]={plane.transform.m[0][3],plane.transform.m[1][3],plane.transform.m[2][3]};
  return groupLayout(items,scene,anchor,head,eyes,center,plane.right,plane.up,plane.forward,1,maximum,&cache);
}
#if defined(SF_GPU_SYNC_EXPERIMENT) || defined(SF_GROUP_OCCLUDER_EXPERIMENT)
#include "../tests/group-occluder-experiment.h"
#endif
// Bounded quartile cuts, with deterministic tie-breaking and preserved density.
static GroupSplitPlan partitionGroup(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,uint64_t baseline){
  GroupSplitPlan best;if(members.size()<2||!finiteGroupBounds(members,cache))return best;
  auto layout=[&](const std::vector<const Object*> &items){return tileLayout(items,scene,anchor,head,eyes,cache,maximum);};
  uint64_t bestCost=baseline*4/5; // Require at least 20% fewer allocated texels.
#if defined(SF_GPU_SYNC_EXPERIMENT) || defined(SF_GROUP_OCCLUDER_EXPERIMENT)
  uint64_t bestOccluders=std::numeric_limits<uint64_t>::max();
#endif
  for(int axis=0;axis<2;axis++){
    auto ordered=members;
    auto coordinate=[&](const Object *object){const auto &a=cache.common(*object,0),&b=cache.common(*object,1);return axis?(double(a.y0)+a.y1+b.y0+b.y1):(double(a.x0)+a.x1+b.x0+b.x1);};
    std::sort(ordered.begin(),ordered.end(),[&](const Object *a,const Object *b){const double x=coordinate(a),y=coordinate(b);return x==y?a->id<b->id:x<y;});
    for(int quarter=1;quarter<=3;quarter++){
      const size_t cut=ordered.size()*quarter/4;if(!cut||cut==ordered.size())continue;
      GroupSplitPlan candidate;candidate.members[0].assign(ordered.begin(),ordered.begin()+cut);candidate.members[1].assign(ordered.begin()+cut,ordered.end());
      uint64_t cost=0;bool limited=false;
#if defined(SF_GPU_SYNC_EXPERIMENT) || defined(SF_GROUP_OCCLUDER_EXPERIMENT)
      GroupLayout childLayouts[2];
#endif
      for(int tile=0;tile<2;tile++){
        const auto child=layout(candidate.members[tile]);limited|=child.limited;
#if defined(SF_GPU_SYNC_EXPERIMENT) || defined(SF_GROUP_OCCLUDER_EXPERIMENT)
        childLayouts[tile]=child;
#endif
        candidate.pixels[tile]=groupBufferPixels(child.pixels,maximum);candidate.widths[tile]=child.width*float(candidate.pixels[tile])/child.pixels;
        cost+=uint64_t(candidate.pixels[tile])*candidate.pixels[tile];
      }
#if defined(SF_GPU_SYNC_EXPERIMENT) || defined(SF_GROUP_OCCLUDER_EXPERIMENT)
      if(limited||cost>bestCost||(cost==bestCost&&(!groupOccluderTieBreak||!best)))continue;
      uint64_t occluders=0;
      if(groupOccluderTieBreak){
        occluders=groupOccluderPredictedTriangles(candidate,childLayouts,scene,cache);
        if(cost==bestCost&&occluders>=bestOccluders)continue;
      }
      bestOccluders=occluders;
#else
      if(limited||cost>=bestCost)continue;
#endif
      candidate.parentPixelsSquared=baseline;candidate.childPixelsSquared=cost;bestCost=cost;best=std::move(candidate);
    }
  }
  return best;
}
static GroupSplitPlan planGroupSplit(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,int allocated,const RenderGroupSplitPolicy &policy){
  if(members.size()<2||!finiteGroupBounds(members,cache))return {};
  const auto parent=tileLayout(members,scene,anchor,head,eyes,cache,maximum);
  if(!parent.limited&&parent.pixels<=policy.targetEyePixels)return {};
  const uint64_t baseline=std::max(allocated,groupBufferPixels(parent.pixels,maximum));
  return partitionGroup(members,scene,anchor,head,eyes,cache,maximum,baseline*baseline);
}
struct GroupAdjustmentPlan {
  GroupSplitPlan partition;bool merge=false;
  explicit operator bool()const{return merge||bool(partition);}
};
static GroupAdjustmentPlan planGroupAdjustment(const std::vector<const Object*> &first,const std::vector<const Object*> &second,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,const RenderGroupSplitPolicy &policy){
  if((!policy.merge&&!policy.rebalance)||!finiteGroupBounds(first,cache)||!finiteGroupBounds(second,cache))return {};
  auto members=first;members.insert(members.end(),second.begin(),second.end());
  // Moving blended objects across overlay surfaces can change compositor
  // ordering even when their pixel density and common plane are preserved.
  for(const auto *object:members)for(const auto &part:scene.assets[size_t(object->asset)-1].primitives)if(part.alphaMode==2)return {};

  auto layout=[&](const std::vector<const Object*> &items){return tileLayout(items,scene,anchor,head,eyes,cache,maximum);};
  // Compare steady requested allocations, not old oversized buffers awaiting
  // their normal shrink delay. Otherwise shrinking can masquerade as regrouping.
  const uint64_t a=groupBufferPixels(layout(first).pixels,maximum),b=groupBufferPixels(layout(second).pixels,maximum),baseline=a*a+b*b;
  if(policy.merge){
    const auto merged=layout(members);const uint64_t pixels=groupBufferPixels(merged.pixels,maximum);
    if(!merged.limited&&merged.pixels<=policy.targetEyePixels&&pixels*pixels<baseline*4/5){
      GroupAdjustmentPlan result;result.merge=true;result.partition.members[0]=std::move(members);result.partition.pixels[0]=int(pixels);
      result.partition.widths[0]=merged.width*float(pixels)/merged.pixels;result.partition.parentPixelsSquared=baseline;result.partition.childPixelsSquared=pixels*pixels;return result;
    }
  }
  if(!policy.rebalance)return {};
  GroupAdjustmentPlan result;result.partition=partitionGroup(members,scene,anchor,head,eyes,cache,maximum,baseline);
  if(result){
    const int key=objectSurfaceKey(*first.front());int unchanged=0,swapped=0;
    for(int side=0;side<2;side++)for(const auto *object:result.partition.members[side]){
      const bool wasFirst=objectSurfaceKey(*object)==key;unchanged+=wasFirst==(side==0);swapped+=wasFirst!=(side==0);
    }
    if(swapped>unchanged){std::swap(result.partition.members[0],result.partition.members[1]);std::swap(result.partition.pixels[0],result.partition.pixels[1]);std::swap(result.partition.widths[0],result.partition.widths[1]);}
  }
  return result;
}
struct StableGroupAdjustment {
  int first=0,second=0,observations=0;bool merge=false;
  std::vector<std::pair<int,int>> membership;
  std::chrono::steady_clock::time_point since{};
  void reset(){*this={};}
  bool observe(int a,int b,const GroupAdjustmentPlan &plan,std::chrono::steady_clock::time_point now){
    std::vector<std::pair<int,int>> signature;
    for(int side=0;side<2;side++)for(const auto *object:plan.partition.members[side])signature.emplace_back(object->id,side);
    std::sort(signature.begin(),signature.end());
    if(a!=first||b!=second||merge!=plan.merge||signature!=membership){first=a;second=b;merge=plan.merge;membership=std::move(signature);observations=1;since=now;return false;}
    ++observations;return observations>=3&&now-since>=std::chrono::seconds(1);
  }
};
// GPU programs, light maps, bounds, and group-adaptation history belong to a
// script instance. Shared shader programs and the EGL context remain host-owned.
struct SceneRenderState {
  struct PendingTileChange {int parent=0,child=0;bool merge=false;};
  SpotShadows spotShadows;
  PbrVariantPrograms pbrVariants;
  ModelInstancing instanceExperiment{ModelInstancing::Instanced};
  FrameBoundsCache frameBounds;
  std::vector<ObjectLightSet> frameObjectLights;
  std::array<GrabState,2> grabs{};
  std::array<int,2> panelHovered{};
  std::array<float,2> panelCursorX{},panelCursorY{};
  std::vector<Surface*> ready;
  std::chrono::steady_clock::time_point lastAnimationTick=std::chrono::steady_clock::now();
  std::optional<PendingTileChange> pendingTileChange;
  StableGroupAdjustment stableAdjustment;
  size_t adjustmentCursor=0;
  std::map<int,std::chrono::steady_clock::time_point> regroupAfter;
  std::chrono::steady_clock::time_point nextAdaptationCheck=std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point nextGroupResize=std::chrono::steady_clock::now();
  void resetScene(){
    spotShadows.reset();pbrVariants.clear();instanceExperiment.reset();
    frameBounds={};frameObjectLights.clear();ready.clear();grabs={};
    panelHovered.fill(0);panelCursorX.fill(0);panelCursorY.fill(0);
    lastAnimationTick=std::chrono::steady_clock::now();
    pendingTileChange.reset();stableAdjustment.reset();
    adjustmentCursor=0;regroupAfter.clear();
    nextAdaptationCheck=nextGroupResize=std::chrono::steady_clock::now();
  }
};
// Split/rebalance outputs retain the complete old cohort in previous chains.
// Show the child first and retire all fallbacks only after both outputs succeed.
template<class Overlays,class Retire>
static vr::EVROverlayError submitGroupSplit(Overlays &overlays,Surface &parent,Surface &child,int frame,Retire retire,bool parentVisible=true,bool childVisible=true){
  // Detach both fallbacks so neither is retired by an individual submission.
  // A rebalance needs both old tiles until both new tiles have been presented.
  auto oldParent=std::move(parent.previous),oldChild=std::move(child.previous);
  auto error=childVisible?submitSurface(overlays,child,frame,retire):vr::VROverlayError_None;
  if(error==vr::VROverlayError_None&&parentVisible)error=submitSurface(overlays,parent,frame,retire);
  if(error==vr::VROverlayError_None){if(oldParent)retire(*oldParent);if(oldChild)retire(*oldChild);}
  else{hideSurfaceFront(overlays,parent);hideSurfaceFront(overlays,child);parent.previous=std::move(oldParent);child.previous=std::move(oldChild);}
  return error;
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
  if(object.animationPose&&object.animationPose->gpuSkin && &asset==&object.animationPose->geometry){
    float near;if(!picking::bounds(localOrigin,localDirection,asset.low,asset.high,miss,near))return miss;
    prepareSkinPicking(currentScene->assets[size_t(object.asset)-1],*object.animationPose);
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
#ifdef SF_RIGID_BATCH_EXPERIMENT
  rigidBatchEnabled=std::getenv("SF_RIGID_BATCH")&&std::string(std::getenv("SF_RIGID_BATCH"))=="1";
  std::fprintf(stdout,"RIGID_BATCH enabled=%d\n",rigidBatchEnabled);
#endif
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
  vertexArrayCachingEnabled=std::getenv("SF_VERTEX_ARRAY_CACHE")&&std::string(std::getenv("SF_VERTEX_ARRAY_CACHE"))=="1";
  std::fprintf(stdout,"VERTEX_ARRAY_CACHE enabled=%d\n",vertexArrayCachingEnabled);
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
  drawUniformCachingEnabled=std::getenv("SF_DRAW_UNIFORM_CACHE")&&std::string(std::getenv("SF_DRAW_UNIFORM_CACHE"))=="1";
  std::fprintf(stdout,"DRAW_UNIFORM_CACHE enabled=%d\n",drawUniformCachingEnabled);
#endif
#ifdef SF_RIGID_POSE_EXPERIMENT
  rigidPoseCachingEnabled=!(std::getenv("SF_RIGID_POSE_CACHE")&&std::string(std::getenv("SF_RIGID_POSE_CACHE"))=="0");
  std::fprintf(stdout,"RIGID_POSE_CACHE enabled=%d\n",rigidPoseCachingEnabled);
#endif
#ifdef SF_GPU_SKIN_EXPERIMENT
  gpuSkinningEnabled=std::getenv("SF_GPU_SKINNING")&&std::string(std::getenv("SF_GPU_SKINNING"))=="1";
  std::fprintf(stdout,"GPU_SKINNING mode=%s (eligible single-set skins; matched conservative bounds)\n",gpuSkinningEnabled?"gpu":"cpu");
#endif
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
    }else if(arg=="--scene-state-dir"){
      if(i+1==argc){fprintf(stderr,"--scene-state-dir requires a directory\n");return 13;}
      scenePersistence::directory=argv[++i];
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
  ScriptRegistry scriptRegistry;
  int scriptInstanceId=0;std::string launchError;
  if(!scriptRegistry.launch(scriptPath,scriptInstanceId,launchError)){
    fprintf(stderr,"[lifecycle] launch failed: %s\n",launchError.c_str());return 13;
  }
  ScriptHost &script=scriptRegistry.find(scriptInstanceId)->host;
  ScriptScope activeScript(script);
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
#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *ordinaryMode=std::getenv("SF_GPU_ORDINARY_VERTEX");
  if(ordinaryMode)ordinaryVertexEnabled=std::strcmp(ordinaryMode,"1")==0;
#endif
  const char *vertexSource=modelVertexShader;
#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *staticMode=std::getenv("SF_GPU_STATIC_VERTEX");
  const bool staticVertexOnly=staticMode&&std::strcmp(staticMode,"1")==0;
  const auto staticSource=staticVertexOnly?staticVertexExperiment():std::string{};
  if(staticVertexOnly)vertexSource=staticSource.c_str();
#endif
  GLuint generalProgram=glCreateProgram(),v=compile(GL_VERTEX_SHADER,vertexSource),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
  ScopeExit shaderCleanup{[&]{glDeleteProgram(generalProgram);glDeleteShader(v);glDeleteShader(f);}};
  glAttachShader(generalProgram,v);glAttachShader(generalProgram,f);glLinkProgram(generalProgram);GLint linked=0;glGetProgramiv(generalProgram,GL_LINK_STATUS,&linked);if(!linked)return 5;
  // Validate color and depth material programs before publishing a live scene.
  std::map<int,std::unique_ptr<SceneRenderState>> renderStates;
  auto &sceneRender=*renderStates.emplace(scriptInstanceId,std::make_unique<SceneRenderState>()).first->second;
  auto &spotShadows=sceneRender.spotShadows;
  auto &pbrVariants=sceneRender.pbrVariants;
  if(!spotShadows.prepare())return 5;
  PbrProgram pbrProgram;if(!pbrProgram.init())return 5;
  PbrProgram depthPbrProgram;if(!depthPbrProgram.init(true))return 5;
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
  PbrProgram minimalDepthProgram;
  const bool minimalPbrDepth=std::getenv("SF_PBR_MINIMAL_DEPTH")&&std::strcmp(std::getenv("SF_PBR_MINIMAL_DEPTH"),"1")==0;
  if(minimalPbrDepth&&!minimalDepthProgram.initMinimalDepth())return 5;
  PbrProgram &selectedDepthProgram=minimalPbrDepth?minimalDepthProgram:depthPbrProgram;
  printf("PBR_MINIMAL_DEPTH enabled=%d\n",int(minimalPbrDepth));
#else
  PbrProgram &selectedDepthProgram=depthPbrProgram;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
  const bool pbrVariantsEnabled=std::getenv("SF_GPU_PBR_VARIANTS")&&std::strcmp(std::getenv("SF_GPU_PBR_VARIANTS"),"1")==0;
#else
  constexpr bool pbrVariantsEnabled=true;
#endif
  if(pbrVariantsEnabled){for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);pbrVariants.prepare(script.scene);}
#ifdef SF_PBR_VARIANT_EXPERIMENT
  ScopeExit variantReport{[&]{printf("PBR_VARIANTS_CACHE programs=%zu binary_bytes=%llu compile_ms=%.4f fallback_draws=%zu\n",pbrVariants.programs.size(),(unsigned long long)pbrVariants.binaryBytes(),pbrVariants.compileMs,pbrVariants.fallbackDraws);}};
#endif
#ifdef SF_PBR_DEPTH_EXPERIMENT
  const bool fastPbrDepth=std::getenv("SF_PBR_DEPTH")&&std::string(std::getenv("SF_PBR_DEPTH"))=="1";
#else
  constexpr bool fastPbrDepth=true;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
  OrdinaryModelProgram ordinaryProgram,skinProgram;
  const GLuint program=ordinaryProgram.select(false)?ordinaryProgram.id():generalProgram;
  skinProgram.borrow(generalProgram);
#else
  const GLuint program=generalProgram;
#endif
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto anchor=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)anchor.m[r][3]+=anchor.m[r][1]*-.2f+anchor.m[r][2]*-1.f;
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
  // Profiling only: a canonical scene basis removes cross-run tracking noise.
  const bool fixedDepthView=std::getenv("SF_PBR_DEPTH_FIXED_VIEW")&&std::strcmp(std::getenv("SF_PBR_DEPTH_FIXED_VIEW"),"1")==0;
  if(fixedDepthView){anchor={};for(int axis=0;axis<3;axis++)anchor.m[axis][axis]=1;}
  printf("PBR_DEPTH_FIXED_VIEW enabled=%d\n",int(fixedDepthView));
#endif
  trackingAnchor=anchor;trackingAnchorValid=true;
  fprintf(stdout,"anchor world=%f,%f,%f\n",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);fflush(stdout);
  vr::VROverlayHandle_t lasers[2]={vr::k_ulOverlayHandleInvalid,vr::k_ulOverlayHandleInvalid};bool laserShown[2]{};
  using SceneResources=NativeSceneResources<Surface,vr::VROverlayHandle_t,PanelSubmission>;
  std::map<int,std::unique_ptr<SceneResources>> resourcesByInstance;
  auto &sceneResources=*resourcesByInstance.emplace(scriptInstanceId,std::make_unique<SceneResources>(scriptInstanceId)).first->second;
  auto &surfaces=sceneResources.surfaces;
  auto &panelOverlays=sceneResources.panelOverlays;
  auto &panelSubmissions=sceneResources.panelSubmissions;
  auto releaseResources=[&](SceneResources &resources){resources.release(
    [&](Surface &surface){destroySurface(surface,display);},
    [&](vr::VROverlayHandle_t handle){overlays->HideOverlay(handle);overlays->ClearOverlayTexture(handle);overlays->DestroyOverlay(handle);});};
  ScopeExit overlayCleanup{[&]{
    for(auto laser:lasers)if(laser!=vr::k_ulOverlayHandleInvalid)overlays->DestroyOverlay(laser);
    for(auto &[id,resources]:resourcesByInstance)releaseResources(*resources);
    fprintf(stdout,"[surface] grouped_buffer_bytes=%llu peak=%llu limit=%llu\n",(unsigned long long)groupBufferBudget.used,(unsigned long long)groupBufferBudget.peak,(unsigned long long)GroupBufferBudget::limit);
  }};
  if(!createLaser(lasers[0],0)||!createLaser(lasers[1],1))return 9;
  auto &grabs=sceneRender.grabs;
  GLint maxRenderbufferPixels=0;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRenderbufferPixels);
  const int maxGroupPixels=std::min(4096,maxRenderbufferPixels/2);
  auto &pendingTileChange=sceneRender.pendingTileChange;
  auto &stableAdjustment=sceneRender.stableAdjustment;
  auto &adjustmentCursor=sceneRender.adjustmentCursor;
  auto &regroupAfter=sceneRender.regroupAfter;
  auto &nextAdaptationCheck=sceneRender.nextAdaptationCheck;
  auto &nextGroupResize=sceneRender.nextGroupResize;
  using SurfaceMembers=std::map<int,std::vector<const Object*>>;
  auto surfaceMembers=[&](const ScriptHost &host){SurfaceMembers groups;for(const auto &object:host.scene.objects)if(object.visible)groups[objectSurfaceKey(object)].push_back(&object);return groups;};
  auto eraseSurface=[&](int ownerId,int key){
    auto owner=resourcesByInstance.find(ownerId);
    auto render=renderStates.find(ownerId);
    if(owner==resourcesByInstance.end()||render==renderStates.end())return;
    auto &surfaces=owner->second->surfaces;
    auto &pendingTileChange=render->second->pendingTileChange;
    auto found=surfaces.find(key);if(found==surfaces.end())return;
    if(pendingTileChange&&!pendingTileChange->merge&&(key==pendingTileChange->parent||key==pendingTileChange->child)){
      const int otherKey=key==pendingTileChange->parent?pendingTileChange->child:pendingTileChange->parent;
      auto other=surfaces.find(otherKey);
      if(other!=surfaces.end()&&found->second->previous){
        preserveSurfaceFallback(*found->second,*other->second);
      }
    }
    destroySurface(*found->second,display);surfaces.erase(found);
  };
  auto syncSurfaces=[&](ScriptHost &host,SceneResources &resources)->bool{
    auto &surfaces=resources.surfaces;
    const auto groups=surfaceMembers(host);
    for(auto it=surfaces.begin();it!=surfaces.end();){
      if(!groups.count(it->first)){
        const int key=it->first;++it;forgetObjectInteraction(key);eraseSurface(host.instanceId,key);
      }else ++it;
    }
    for(const auto &[id,members]:groups){
      const auto *object=members.front();
      const float width=std::max(.55f,object->scale*2.1f*objectRadius(*object));
      const int eyePixels=host.scene.assets[(size_t)object->asset-1].eyePixels;
      const bool interactive=id>0&&!object->laserPassThrough;
      auto found=surfaces.find(id);
      if(found!=surfaces.end()&&found->second->buffers[0].fbo&&((id>0&&found->second->referenceEyePixels!=eyePixels)||found->second->interactive!=interactive)){
        pointerStates.erase(id);destroySurface(*found->second,display);surfaces.erase(found);found=surfaces.end();
      }
      if(found==surfaces.end()){
        auto created=std::make_unique<Surface>();
        if(!createSurface(*created,host.instanceId,id,width,eyePixels,interactive,gbm,display,anchor)){destroySurface(*created,display);return false;}
        surfaces.emplace(id,std::move(created));
      }else if(!found->second->buffers[0].fbo){
        if(!createSurface(*found->second,host.instanceId,id,width,eyePixels,interactive,gbm,display,anchor))return false;
      }
    }
    return true;
  };
  if(!syncSurfaces(script,sceneResources))return 8;
  reserveObjectOverlay=[&](int id)->std::string{
    auto owner=resourcesByInstance.find(activeInstanceId);
    if(owner==resourcesByInstance.end())return "script resource owner unavailable";
    auto &sceneResources=*owner->second;
    auto &surfaces=sceneResources.surfaces;
    if(surfaces.count(id))return {};
    auto reserved=std::make_unique<Surface>();reserved->instanceId=activeInstanceId;reserved->objectId=id;
    const auto key=objectOverlayKey(sceneResources.key(id));
    const auto error=overlays->CreateOverlay(key.c_str(),key.c_str(),&reserved->overlay);
    if(error!=vr::VROverlayError_None)return std::string("SteamVR: ")+overlays->GetOverlayErrorNameFromEnum(error);
    if(id<0){
      const auto stageError=overlays->CreateOverlay((key+".stage").c_str(),(key+".stage").c_str(),&reserved->stagingOverlay);
      if(stageError!=vr::VROverlayError_None){destroySurface(*reserved,display);return std::string("SteamVR: ")+overlays->GetOverlayErrorNameFromEnum(stageError);}
    }
    surfaces.emplace(id,std::move(reserved));return {};
  };
  releaseObjectOverlay=[&](int id){eraseSurface(activeInstanceId,id);};
  ScopeExit objectOverlayHooksCleanup{[]{reserveObjectOverlay={};releaseObjectOverlay={};}};

  auto &panelHovered=sceneRender.panelHovered;
  auto &panelCursorX=sceneRender.panelCursorX;
  auto &panelCursorY=sceneRender.panelCursorY;
  reservePanelOverlay=[&](const Panel &panel)->std::string{
    auto owner=resourcesByInstance.find(activeInstanceId);
    if(owner==resourcesByInstance.end())return "script resource owner unavailable";
    auto &sceneResources=*owner->second;
    auto &panelOverlays=sceneResources.panelOverlays;
    if(panelOverlays.count(panel.id))return {};
    vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
    std::string key=panelOverlayKey(sceneResources.key(panel.id));
    const auto error=overlays->CreateOverlay(key.c_str(),key.c_str(),&handle);if(error!=vr::VROverlayError_None)return std::string("SteamVR: ")+overlays->GetOverlayErrorNameFromEnum(error);
    overlays->SetOverlayWidthInMeters(handle,panel.widthMeters);
    vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(handle,&bounds);
    if(panel.interactive){overlays->SetOverlayInputMethod(handle,vr::VROverlayInputMethod_Mouse);overlays->SetOverlayFlag(handle,vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,true);vr::HmdVector2_t scale{{(float)panel.width,(float)panel.height}};overlays->SetOverlayMouseScale(handle,&scale);}
    panelOverlays.emplace(panel.id,handle);
    return {};
  };
  releasePanelOverlay=[&](int id){
    auto owner=resourcesByInstance.find(activeInstanceId);if(owner==resourcesByInstance.end())return;
    auto &panels=owner->second->panelOverlays;
    auto it=panels.find(id);if(it==panels.end())return;
    overlays->ClearOverlayTexture(it->second);overlays->DestroyOverlay(it->second);
    panels.erase(it);owner->second->panelSubmissions.erase(id);
  };
  ScopeExit panelOverlayHooksCleanup{[]{reservePanelOverlay={};releasePanelOverlay={};}};
  auto syncPanels=[&](ScriptHost &host,SceneResources &resources,SceneRenderState &render){
    auto &panelOverlays=resources.panelOverlays;
    auto &panelSubmissions=resources.panelSubmissions;
    auto &panelHovered=render.panelHovered;
    for(auto it=panelOverlays.begin();it!=panelOverlays.end();){
      const int id=it->first;
      const bool exists=findSceneItem(host.scene.panels,id)!=nullptr;
      if(exists){++it;continue;}
      overlays->HideOverlay(it->second);overlays->ClearOverlayTexture(it->second);overlays->DestroyOverlay(it->second);
      panelSubmissions.erase(id);
      for(int hand=0;hand<2;hand++)if(panelHovered[hand]==id){sceneInput().addOverlayEvent("panel.hoverEnd",id,hand);panelHovered[hand]=0;}
      it=panelOverlays.erase(it);
    }
    for(auto &retired:host.scene.retiredPanels)panelDestroy(retired);
    host.scene.retiredPanels.clear();
    for(auto &panel:host.scene.panels){
      if(!panel.visible){if(panelOverlays.count(panel.id))hidePanel(*overlays,panelOverlays.at(panel.id),panelSubmissions[panel.id]);for(int h=0;h<2;h++)if(panelHovered[h]==panel.id){sceneInput().addOverlayEvent("panel.hoverEnd",panel.id,h);panelHovered[h]=0;}}
      if(panel.dirty)panelUpload(panel);
      if(panelOverlays.count(panel.id))continue;
      reservePanelOverlay(panel);
    }
  };
  syncPanels(script,sceneResources,sceneRender);
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
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
  const ModelDrawLocations drawLocations(program);
#endif
  GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff");
  GLint sheenLocation=glGetUniformLocation(program,"sheen");
  auto &ready=sceneRender.ready;
  auto &frameBounds=sceneRender.frameBounds;
  auto &frameObjectLights=sceneRender.frameObjectLights;
  auto &instanceExperiment=sceneRender.instanceExperiment;
  double pickingMs=0,renderMs=0,finishMs=0,physicsMs=0;

#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *groupPolicy=std::getenv("SF_GPU_GROUP_POLICY");
  groupOccluderTieBreak=groupPolicy&&std::strcmp(groupPolicy,"occluders")==0;
  GpuSyncExperiment gpuSync;
  if(std::getenv("SF_GPU_MULTIPART_INSTANCING"))instanceExperiment.enableMultipartExperiment();
  const char *instanceMode=std::getenv("SF_GPU_INSTANCING");
  if(instanceMode){instanceExperiment.mode=std::strcmp(instanceMode,"1")==0?ModelInstancing::Instanced:ModelInstancing::Original;instanceExperiment.prewarm();}
  const char *primitiveMode=std::getenv("SF_GPU_PRIMITIVE_VISIBILITY");
  const bool primitiveVisibility=primitiveMode&&std::strcmp(primitiveMode,"1")==0;
  PrimitiveVisibilityExperiment primitiveExperiment;
  const char *clusterMode=std::getenv("SF_GPU_CLUSTER_MODE");
  ClusterVisibilityExperiment clusterExperiment(clusterMode?(std::strcmp(clusterMode,"cull")==0?ClusterVisibilityExperiment::Cull:std::strcmp(clusterMode,"full")==0?ClusterVisibilityExperiment::Full:ClusterVisibilityExperiment::Original):ClusterVisibilityExperiment::Off);
  const char *opaqueMode=std::getenv("SF_GPU_OPAQUE_SHADER");
  const bool specializeOpaque=opaqueMode&&std::strcmp(opaqueMode,"1")==0;
  std::unique_ptr<OpaqueExperimentProgram> opaqueExperiment,depthExperiment;
  if(specializeOpaque){opaqueExperiment=std::make_unique<OpaqueExperimentProgram>(opaqueColorExperiment);depthExperiment=std::make_unique<OpaqueExperimentProgram>(opaqueDepthExperiment);}
#endif
  auto next=std::chrono::steady_clock::now();auto reportStart=next;int frame=0;uint64_t diagnosticFrames=0;engineStats.prime();
  auto &lastAnimationTick=sceneRender.lastAnimationTick;
  auto reconnectAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
#ifdef SF_ANIMATION_PROFILE
  AnimationProfileFrame animationFrame;AnimationProfileWindow animationWindow;unsigned animationEpoch=0;
  animationProfileFrame=&animationFrame;ScopeExit clearAnimationProfiler{[]{animationProfileFrame=nullptr;}};
  std::fprintf(stdout,"ANIMATION_PROFILE_BEGIN gpu=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
#endif
  while(running){
#ifdef SF_ANIMATION_PROFILE
    animationFrame={};const auto animationFrameStart=AnimationProfileClock::now();
#endif
    vr::VREvent_t runtimeEvent{};
    while(system->PollNextEvent(&runtimeEvent,sizeof(runtimeEvent)))if(runtimeEvent.eventType==vr::VREvent_Quit){system->AcknowledgeQuit_Exiting();fprintf(stdout,"[lifecycle] SteamVR quit; exiting\n");running=false;}
    if(!running)break;
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,.011f,poses,vr::k_unMaxTrackedDeviceCount);
    for(int hand=0;hand<2;hand++){
      auto &snapshot=controllers[hand];snapshot={};
      auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      if(device==vr::k_unTrackedDeviceIndexInvalid){frameInput.setTrackedDevice(hand,-1);if(frame%900==0)fprintf(stdout,"[input] hand=%d device=none\n",hand);continue;}
      const auto &pose=poses[device];
      if(frame%900==0){fprintf(stdout,"[input] hand=%d device=%u pose=%d\n",hand,device,pose.bPoseIsValid);fflush(stdout);}
      snapshot.connected=pose.bDeviceIsConnected;
      frameInput.setTrackedDevice(hand,snapshot.connected?int(device):-1);
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
    const bool streamWasStarted=frameInput.started();
    frameInput.poll();
    fanOutRawInput(scriptRegistry,frameInput.takeEvents());
    frameInput.takeCallbackEvents();
    if(frame%30==0){
      std::error_code ec;auto modified=std::filesystem::last_write_time(scriptPath,ec);
      if(!ec&&modified!=script.mtime){
        const bool loaded=scriptRegistry.reloadActive(scriptInstanceId,[&]{
          releaseResources(sceneResources);
          sceneRender.resetScene();pointerStates.clear();
          sceneInput().resetScene();
          frameInput.resetScene(); // Legacy physical hover state; arbitration moves to Step 3.
          for(int h=0;h<2;h++){hoveredObject[h]=meshHoveredObject[h]=meshCurrentObject[h]=0;meshHitDistance[h]=2;meshLastHit[h]={};grabNeedsRelease[h]=true;}
#ifdef SF_ANIMATION_PROFILE
          ++animationEpoch;
#endif
          fprintf(stdout,"[lifecycle] scene state reset\n");
#ifdef SF_GPU_SYNC_EXPERIMENT
          primitiveExperiment.reset();
          clusterExperiment.reset();
#endif
        });
        if(loaded&&pbrVariantsEnabled){for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);pbrVariants.prepare(script.scene);}
        if(!loaded)script.mtime=modified;
      }
    }
    auto renderActiveScene=[&]()->int{
    const auto animationTick=std::chrono::steady_clock::now();
    const double animationDt=std::chrono::duration<double>(animationTick-lastAnimationTick).count();lastAnimationTick=animationTick;
    for(auto &object:script.scene.objects)if(object.animation.clip>=0){
      const auto &asset=script.scene.assets[size_t(object.asset)-1];
      if(animation::advancePlayback(object.animation,asset.animations,animationDt))evaluateModelPose(asset,object.animation,*object.animationPose);
    }
    if(!syncSurfaces(script,sceneResources)){running=false;return -1;}
    syncPanels(script,sceneResources,sceneRender);
    for(auto &[id,handle]:panelOverlays){
      vr::VREvent_t event{};
      while(overlays->PollNextOverlayEvent(handle,&event,sizeof(event))){
        const auto role=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
        const int hand=role==vr::TrackedControllerRole_LeftHand?0:role==vr::TrackedControllerRole_RightHand?1:-1;
        if(event.eventType==vr::VREvent_FocusLeave){for(int h=0;h<2;h++)if(panelHovered[h]==id){sceneInput().addOverlayEvent("panel.hoverEnd",id,h);panelHovered[h]=0;}continue;}
        if(hand<0)continue;
        auto *panelData=findPanel(id);if(!panelData||!panelData->visible)continue;
        if(event.eventType==vr::VREvent_MouseMove){panelCursorX[hand]=std::clamp(event.data.mouse.x,0.f,(float)panelData->width);panelCursorY[hand]=std::clamp(event.data.mouse.y,0.f,(float)panelData->height);}
        const float x=panelCursorX[hand],y=panelCursorY[hand];
        if(event.eventType==vr::VREvent_MouseMove){if(panelHovered[hand]!=id){if(panelHovered[hand])sceneInput().addOverlayEvent("panel.hoverEnd",panelHovered[hand],hand);panelHovered[hand]=id;sceneInput().addPanelEvent("panel.hoverStart",id,hand,x,y);}sceneInput().addPanelEvent("panel.hover",id,hand,x,y);}
        const std::string button=event.data.mouse.button==vr::VRMouseButton_Left?"left":event.data.mouse.button==vr::VRMouseButton_Right?"right":"other";
        if(event.eventType==vr::VREvent_MouseButtonDown)sceneInput().addPanelEvent("panel.pointerDown",id,hand,x,y,button,true);
        if(event.eventType==vr::VREvent_MouseButtonUp)sceneInput().addPanelEvent("panel.pointerUp",id,hand,x,y,button,false);
      }
    }
    for(auto &[id,surface]:surfaces){
      if(id<=0)continue;
      vr::VREvent_t event{};
      auto &state=pointerStates[id];
      state.scrollX=state.scrollY=0;
      while(overlays->PollNextOverlayEvent(surface->overlay,&event,sizeof(event))){
        if(event.eventType==vr::VREvent_FocusLeave){state.valid=false;state.down=false;state.secondaryDown=false;if(state.hand>=0&&hoveredObject[state.hand]==id){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",id,state.hand);hoveredObject[state.hand]=0;frameInput.setHoveredObject(state.hand,0);}state.hand=-1;continue;}
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
          if(eventHand>=0&&event.eventType==vr::VREvent_MouseMove&&hoveredObject[eventHand]!=id){if(hoveredObject[eventHand])sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[eventHand],eventHand);hoveredObject[eventHand]=id;frameInput.setHoveredObject(eventHand,id);sceneInput().addOverlayEvent("overlay.surfaceHoverStart",id,eventHand);}
          sceneInput().addOverlayEvent(event.eventType==vr::VREvent_MouseMove?"overlay.surfaceHover":"overlay.pointer",id,eventHand);
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
    if(streamWasStarted&&!frameInput.started()){
      for(int h=0;h<2;h++){grabNeedsRelease[h]=true;if(panelHovered[h])sceneInput().addOverlayEvent("panel.hoverEnd",panelHovered[h],h);panelHovered[h]=0;}
      for(const auto &object:script.scene.objects){forgetObjectInteraction(object.id);if(script.scene.physics)script.scene.physics->cancelThrow(object.id);}
    }
    for(int h=0;h<2;h++)if(!controllers[h].connected){
      frameInput.invalidateHand(h);
      for(auto &[id,pointer]:pointerStates)if(pointer.hand==h)pointer={};
      if(hoveredObject[h]){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[h],h);hoveredObject[h]=0;}
      if(panelHovered[h]){sceneInput().addOverlayEvent("panel.hoverEnd",panelHovered[h],h);panelHovered[h]=0;}
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
      const auto *previousHit=findSceneItem(script.scene.objects,meshHoveredObject[hand]);
      if(hit)meshLastHit[hand]=now;
      else if(controllers[hand].connected&&previousHit&&previousHit->visible&&now-meshLastHit[hand]<std::chrono::milliseconds(100)){
        hit=meshHoveredObject[hand];nearest=meshHitDistance[hand];
      }
      if(hit!=meshHoveredObject[hand]){
        if(meshHoveredObject[hand])sceneInput().addOverlayEvent("overlay.meshHoverEnd",meshHoveredObject[hand],hand);
        if(hit)sceneInput().addOverlayEvent("overlay.meshHoverStart",hit,hand);
        meshHoveredObject[hand]=hit;
      }
      meshHitDistance[hand]=std::min(nearest,2.f);
    }
    pickingMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
    const auto reconnectNow=std::chrono::steady_clock::now();
    if(!frameInput.started()&&reconnectNow>=reconnectAt){frameInput.reconnect();reconnectAt=reconnectNow+std::chrono::seconds(1);}
    dispatchInputCallbacks();
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
    catch(const std::exception &error){fprintf(stderr,"[physics] %s\n",error.what());running=false;return -1;}
    physicsMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-physicsStart).count();
    if(script.scene.assetsReleased){
      instanceExperiment.reset();
#ifdef SF_GPU_SYNC_EXPERIMENT
      primitiveExperiment.reset();clusterExperiment.reset();
#endif
      script.scene.assetsReleased=false;
    }
    for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);
    for(auto &object:script.scene.objects)if(object.animationPose)uploadModelPose(*object.animationPose);
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
      if(fixedDepthView){head=anchor;head.m[1][3]=.2f;head.m[2][3]=1.f;}
#endif
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
      static float focal=0;
      if(focal==0||frame%90==0){uint32_t viewWidth=0,viewHeight=0;system->GetRecommendedRenderTargetSize(&viewWidth,&viewHeight);focal=0;
        for(int eye=0;eye<2;eye++){float l,r,t,b;system->GetProjectionRaw(vr::EVREye(eye),&l,&r,&t,&b);focal=std::max(focal,std::max(float(viewWidth)/std::abs(r-l),float(viewHeight)/std::abs(b-t)));}
      }
      for(auto &object:script.scene.objects){
        const auto &asset=script.scene.assets[size_t(object.asset)-1];
        auto selectDetail=[&](int level){
#ifdef SF_PBR_LOD_EXPERIMENT
          if(asset.pbr&&object.lodLevel!=level){size_t triangles=0;for(const auto &part:asset.primitives)triangles+=modelDrawVertexCount(part,level)/3;fprintf(stdout,"[pbr-lod] object=%d level=%d triangles=%zu held=%d\n",object.id,level,triangles,grabs[0].objectId==object.id||grabs[1].objectId==object.id);fflush(stdout);}
#endif
          object.lodLevel=level;
        };
        const bool available=asset.lod&&!object.animationPose&&std::any_of(asset.primitives.begin(),asset.primitives.end(),[](const auto &p){return p.lod[0].indexBuffer||p.lod[1].indexBuffer||p.lod[0].vbo||p.lod[1].vbo;});
        if(!available||grabs[0].objectId==object.id||grabs[1].objectId==object.id){selectDetail(0);continue;}
        float center[3];for(int row=0;row<3;row++)center[row]=anchor.m[row][3]+anchor.m[row][0]*object.x+anchor.m[row][1]*object.y-anchor.m[row][2]*object.z;
        float extent=0;for(int axis=0;axis<3;axis++){const float e=asset.high[axis]-asset.low[axis];extent+=e*e;}
        const float radius=.5f*std::abs(object.scale)*std::sqrt(extent);float nearest=std::numeric_limits<float>::infinity(),pixels=0;
        for(const auto &eye:eyes){float squared=0,depth=0;for(int axis=0;axis<3;axis++){const float d=center[axis]-eye[axis];squared+=d*d;depth-=d*head.m[axis][2];}nearest=std::min(nearest,std::sqrt(squared)-radius);pixels=std::max(pixels,modelLod::projectedDiameter(radius,focal,squared,depth));}
        selectDetail(modelLod::select(pixels,nearest,object.lodLevel));
      }
      spotShadows.update(script.scene);
      const auto frameLights=resolvePointLights(script.scene);
      updateObjectLights(script.scene,frameLights,frameObjectLights);
      frameBounds.update(script.scene,anchor,head,eyes);
      const bool useInstancing=std::any_of(script.scene.assets.begin(),script.scene.assets.end(),[](const ModelAsset &asset){return asset.gpuInstancing;});
#ifdef SF_GPU_SYNC_EXPERIMENT
#ifdef SF_MODEL_LOD_EXPERIMENT
      if(lodExperiment::ratio()!=1&&(useInstancing||primitiveVisibility||clusterExperiment.mode!=ClusterVisibilityExperiment::Off||specializeOpaque||std::getenv("SF_GPU_MULTIPART_INSTANCING"))){fprintf(stderr,"LOD comparison requires original object draws without other geometry/shader experiments\n");return 13;}
#endif
      if(program!=generalProgram&&(staticVertexOnly||specializeOpaque||primitiveVisibility||clusterExperiment.mode!=ClusterVisibilityExperiment::Off)){fprintf(stderr,"ordinary routing comparison cannot combine with other shader/visibility experiments\n");return 13;}
      if(staticVertexOnly&&std::any_of(script.scene.assets.begin(),script.scene.assets.end(),[](const auto &asset){return asset.hasSkin;})){fprintf(stderr,"static vertex experiment cannot render skinned assets\n");return 13;}
      if(useInstancing&&(primitiveVisibility||clusterExperiment.mode!=ClusterVisibilityExperiment::Off||specializeOpaque)){fprintf(stderr,"instancing cannot combine with other geometry/shader experiments\n");return 13;}
      if(primitiveVisibility)primitiveExperiment.update(script.scene,anchor,head,eyes);
      if(clusterExperiment.mode!=ClusterVisibilityExperiment::Off)clusterExperiment.update(script.scene,anchor,head,eyes);
#endif
      if(!pendingTileChange&&std::chrono::steady_clock::now()>=nextAdaptationCheck){
        const auto now=std::chrono::steady_clock::now();nextAdaptationCheck=now+std::chrono::milliseconds(500);
        const auto candidates=surfaceMembers(script);
        bool allocationFailed=false;
        auto startChange=[&](int key,int childKey,const GroupSplitPlan &plan,bool merge,bool rebalance,size_t tileCount){
          const int group=surfaceRenderGroup(key);auto child=std::make_unique<Surface>();Surface parent;
          const auto extra=GroupBufferBudget::bytes(plan.pixels[0])+(merge?0:GroupBufferBudget::bytes(plan.pixels[1]));
          if(!groupBufferBudget.fits(extra)){
            allocationFailed=true;nextAdaptationCheck=now+std::chrono::seconds(5);
            fprintf(stderr,"[surface] group=%d adaptation budget deferred; retaining existing tiles\n",group);return;
          }
          static uint64_t generation=0;const auto suffix=".adapt."+std::to_string(++generation);
          if(!createSurface(parent,scriptInstanceId,key,plan.widths[0],plan.pixels[0],false,gbm,display,anchor,suffix)||
             (!merge&&!createSurface(*child,scriptInstanceId,childKey,plan.widths[1],plan.pixels[1],false,gbm,display,anchor,suffix))){
            destroySurface(parent,display);destroySurface(*child,display);allocationFailed=true;nextAdaptationCheck=now+std::chrono::seconds(5);
            fprintf(stderr,"[surface] group=%d adaptation allocation unavailable; retaining existing tiles\n",group);return;
          }
          parent.previous=std::move(surfaces.at(key));
          if(merge){parent.previous->previous=std::move(surfaces.at(childKey));surfaces.erase(childKey);}
          else{
            if(rebalance)child->previous=std::move(surfaces.at(childKey));
            surfaces.insert_or_assign(childKey,std::move(child));
          }
          surfaces.at(key)=std::make_unique<Surface>(std::move(parent));
          for(int side=0;side<(merge?1:2);side++)for(const auto *object:plan.members[side]){
            const int tileKey=side?childKey:key;script.scene.objects[size_t(object-script.scene.objects.data())].renderTile=(-tileKey-group)/256;
          }
          pendingTileChange=SceneRenderState::PendingTileChange{key,childKey,merge};stableAdjustment.reset();regroupAfter[group]=now+std::chrono::seconds(2);
          fprintf(stdout,"[surface] group=%d %s tiles=%zu parent_pixels_squared=%llu child_pixels_squared=%llu\n",group,merge?"merge":rebalance?"rebalance":"split",tileCount,(unsigned long long)plan.parentPixelsSquared,(unsigned long long)plan.childPixelsSquared);fflush(stdout);
        };
        std::set<int> hiddenTiles;for(const auto &object:script.scene.objects)if(object.renderGroup&&!object.visible)hiddenTiles.insert(objectSurfaceKey(object));
        std::vector<std::pair<int,int>> pairs;
        auto eligible=[&](int key){auto found=surfaces.find(key);return found!=surfaces.end()&&found->second->buffers[0].fbo&&!found->second->previous&&!hiddenTiles.count(key);};
        for(auto a=candidates.begin();a!=candidates.end();++a){
          const int group=surfaceRenderGroup(a->first);auto policy=script.scene.renderGroupSplits.find(group);
          if(!group||policy==script.scene.renderGroupSplits.end()||(!policy->second.merge&&!policy->second.rebalance)||now<regroupAfter[group]||!eligible(a->first))continue;
          for(auto b=std::next(a);b!=candidates.end();++b)if(surfaceRenderGroup(b->first)==group&&eligible(b->first))pairs.emplace_back(a->first,b->first);
        }
        auto sticky=std::find(pairs.begin(),pairs.end(),std::pair{stableAdjustment.first,stableAdjustment.second});
        if(sticky==pairs.end())stableAdjustment.reset();
        bool proposed=false;
        for(size_t attempt=0;attempt<std::min(size_t(4),pairs.size());attempt++){
          const auto pair=attempt==0&&sticky!=pairs.end()?*sticky:pairs[adjustmentCursor++%pairs.size()];
          const auto [a,b]=pair;const int group=surfaceRenderGroup(a);
          const auto plan=planGroupAdjustment(candidates.at(a),candidates.at(b),script.scene,anchor,head,eyes,frameBounds,maxGroupPixels,script.scene.renderGroupSplits.at(group));
          if(!plan){if(a==stableAdjustment.first&&b==stableAdjustment.second)stableAdjustment.reset();continue;}
          proposed=true;
          if(stableAdjustment.observe(a,b,plan,now)){
            std::set<int> tiles;for(const auto &object:script.scene.objects)if(object.renderGroup==group)tiles.insert(object.renderTile);
            startChange(a,b,plan.partition,plan.merge,!plan.merge,tiles.size()-(plan.merge?1:0));
          }
          break;
        }
        if(!proposed)stableAdjustment.reset();
        if(!pendingTileChange&&!allocationFailed)for(const auto &[key,members]:candidates){
          const int group=surfaceRenderGroup(key);auto policy=script.scene.renderGroupSplits.find(group);auto found=surfaces.find(key);
          if(!group||policy==script.scene.renderGroupSplits.end()||found==surfaces.end()||!found->second->buffers[0].fbo||found->second->previous||surfaceRenderGroup(stableAdjustment.first)==group)continue;
          std::set<int> tiles;int nextTile=0;for(const auto &object:script.scene.objects)if(object.renderGroup==group)tiles.insert(object.renderTile);
          if(tiles.size()>=size_t(policy->second.maxSurfaces))continue;while(tiles.count(nextTile))++nextTile;
          const auto plan=planGroupSplit(members,script.scene,anchor,head,eyes,frameBounds,maxGroupPixels,found->second->eyePixels,policy->second);
          if(!plan)continue;startChange(key,-(group+256*nextTile),plan,false,false,tiles.size()+1);break;
        }
      }
      ready.clear();
      ready.reserve(surfaces.size());
      for(const auto &panelData:script.scene.panels){
        auto overlay=panelOverlays.find(panelData.id);if(overlay==panelOverlays.end())continue;
        const auto transform=panelData.visible&&panelData.texture?panelTransform(panelData,anchor,head):vr::HmdMatrix34_t{};
        const auto error=submitPanel(*overlays,overlay->second,panelData,transform,panelSubmissions[panelData.id]);
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[panel] submit id=%d error=%d\n",panelData.id,int(error));
      }
      const auto groups=surfaceMembers(script);
      int shrinkBudget=2;std::set<int> culledSurfaces;
#ifdef SF_GPU_SYNC_EXPERIMENT
      gpuSync.scene(script.scene);gpuSync.begin();
      if(specializeOpaque)for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives)if(part.alphaMode!=0){fprintf(stderr,"opaque shader experiment requires opaque-only assets\n");return 13;}
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
      uint64_t uniformIssued=0,uniformSkipped=0;
#endif
#ifdef SF_RIGID_BATCH_EXPERIMENT
      rigidBatchStats={};
#endif
      for(auto &[id,surface]:surfaces){
        if(!surface->buffers[0].fbo)continue; // A handle reserved during this update initializes next frame.
        auto group=groups.find(id);if(group==groups.end())continue;
        const auto &members=group->second;const auto *target=members.front();
        // Hide only replacement fronts before reusing their buffers. Retained
        // cohort snapshots stay visible until the complete handoff succeeds.
        const bool changing=pendingTileChange&&(id==pendingTileChange->parent||(!pendingTileChange->merge&&id==pendingTileChange->child));
        if(changing&&hideSurfaceFront(*overlays,*surface)!=vr::VROverlayError_None)continue;
        float animatedWidth=std::max(.55f,target->scale*2.1f*objectRadius(*target));
        float surfaceX=target->x,surfaceY=target->y,surfaceZ=target->z;
        auto panel=anchor;
        float panelRight[3],panelUp[3],panelForward[3];
        if(id<0){
          const auto projection=groupPlane(members,script.scene,anchor,head,eyes,&frameBounds);
          panel=projection.transform;surfaceX=projection.sceneOrigin[0];surfaceY=projection.sceneOrigin[1];surfaceZ=projection.sceneOrigin[2];
          std::copy_n(projection.right,3,panelRight);std::copy_n(projection.up,3,panelUp);std::copy_n(projection.forward,3,panelForward);
          for(const auto *member:members){const float dx=member->x-surfaceX,dy=member->y-surfaceY,dz=member->z-surfaceZ;animatedWidth=std::max(animatedWidth,2.1f*(std::sqrt(dx*dx+dy*dy+dz*dz)+member->scale*objectRadius(*member)));}
        }else{
          for(int r=0;r<3;r++)panel.m[r][3]+=anchor.m[r][0]*surfaceX+anchor.m[r][1]*surfaceY-anchor.m[r][2]*surfaceZ;
          faceViewer(panel,anchor,head,panelRight,panelUp,panelForward);
        }
        float distanceSquared=0,facing=0;
        for(int r=0;r<3;r++){float delta=panel.m[r][3]-head.m[r][3];distanceSquared+=delta*delta;facing-=delta*head.m[r][2];}
        if(facing<.25f*std::sqrt(distanceSquared)-(id<0?animatedWidth*.5f:target->scale*(target->animationPose?objectRadius(*target):.6f))){
          const auto error=changing?hideSurfaceFront(*overlays,*surface):hideSurface(*overlays,*surface);
          if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] hide id=%d error=%d\n",id,int(error));
          else culledSurfaces.insert(id);
          continue;
        }
        const float surfaceCenter[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]};
        ProjectedBounds targetBounds[2];if(id>0)for(int eye=0;eye<2;eye++)targetBounds[eye]=frameBounds.project(*target,eyes[eye],panelRight,panelUp,panelForward);
        int crop=id>0?objectCropPixels(targetBounds,surfaceCenter,eyes,panelRight,panelUp,panelForward,animatedWidth,surface->referenceEyePixels):surface->referenceEyePixels;
        if(id<0){
          const auto layout=groupLayout(members,script.scene,anchor,head,eyes,surfaceCenter,panelRight,panelUp,panelForward,animatedWidth,maxGroupPixels,&frameBounds);
          if(layout.limited&&!surface->resolutionLimited)fprintf(stderr,"[surface] group=%d reaches %d pixels per eye; use smaller groups to retain density\n",surfaceRenderGroup(id),maxGroupPixels);
          surface->resolutionLimited=layout.limited;
          animatedWidth=layout.width;crop=layout.pixels;
          targetBounds[0]=layout.bounds[0];targetBounds[1]=layout.bounds[1];
          surface->referenceEyePixels=crop;
        }
        // Grow immediately when needed; shrink only after 45 consecutive frames.
        if(crop<(id<0?surface->eyePixels*.6f:surface->eyePixels))++surface->shrinkFrames;else surface->shrinkFrames=0;
        if(crop>surface->eyePixels||(surface->shrinkFrames>=45&&shrinkBudget>0)){
          if(crop<surface->eyePixels)--shrinkBudget;
          if(id<0){
            const auto now=std::chrono::steady_clock::now();
            if(!surface->previous&&!pendingTileChange&&now>=surface->resizeAfter&&now>=nextGroupResize){
              // At most one group resize per 100 ms, including allocation failure.
              // Preflight the complete generation before creating any buffers.
              const int requested=groupBufferPixels(crop,maxGroupPixels);
              if(requested!=surface->eyePixels){
                const int pixels=groupBufferBudget.resizePixels(requested,surface->eyePixels);
                nextGroupResize=now+std::chrono::milliseconds(100);
                if(!pixels||!replaceGroupedSurface(*surface,pixels,gbm,display,panel)){
                  surface->resizeAfter=now+std::chrono::seconds(5);
                  fprintf(stderr,"[surface] replacement id=%d deferred; retaining buffers, retry in 5s\n",id);
                }else if(pixels<requested){
                  surface->resizeAfter=now+std::chrono::seconds(5);
                  fprintf(stderr,"[surface] replacement id=%d budget limited eye_px=%d requested=%d; retry in 5s\n",id,pixels,requested);
                }
              }
            }
          }else if(!resizeSurfaceBuffers(*surface,crop,gbm,display)){fprintf(stderr,"[surface] resize id=%d failed\n",id);running=false;break;}
          surface->shrinkFrames=0;
        }
        // Moving bounds still fit when growth is throttled or cannot be admitted.
        // Render the complete group at temporarily reduced density, never crop it.
        const float width=surfaceCoverageWidth(animatedWidth,surface->eyePixels,surface->referenceEyePixels,id<0);
        const auto placementError=prepareSurface(*overlays,*surface,width,panel);
        if(placementError!=vr::VROverlayError_None){fprintf(stderr,"[surface] placement id=%d error=%d\n",id,int(placementError));continue;}
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
          auto region=projectedScissor(targetBounds[i],surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels);
          region.x+=i*surface->eyePixels;
          renderObjectLayers(id,[&](int targetId,int pass){
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
            ModelDrawUniforms drawUniforms(drawLocations,drawUniformCachingEnabled);
            ScopeExit countUniforms{[&]{uniformIssued+=drawUniforms.issued;uniformSkipped+=drawUniforms.skipped;}};
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
            gpuSync.beginPass(pass);ScopeExit finishPass{[&]{gpuSync.endPass();}};
            auto *special=specializeOpaque?(pass<0?depthExperiment.get():opaqueExperiment.get()):nullptr;
            ScopeExit restoreProgram{[&]{if(special)glUseProgram(program);}};
            if(special)special->view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);
#endif
            ModelCulling culling;
            auto selected=[&](const Object &object){
              if(!object.visible||(pass<0?objectSurfaceKey(object)==targetId:objectSurfaceKey(object)!=targetId))return false;
              if(pass<0&&!mayOcclude(id<0?frameBounds.common(object,i):frameBounds.project(object,eyes[i],panelRight,panelUp,panelForward),targetBounds[i]))return false;
              return true;
            };
#ifdef SF_GPU_SYNC_EXPERIMENT
            bool skinBound=false;
            ScopeExit restoreSkinProgram{[&]{if(skinBound)glUseProgram(program);}};
            auto restoredProgram=[&]{skinBound=false;};
#else
            auto restoredProgram=[]{};
#endif
            auto drawObject=[&](const Object &object){
              const auto &asset=script.scene.assets[(size_t)object.asset-1];
#ifdef SF_GPU_SYNC_EXPERIMENT
              const bool useSkinProgram=program!=generalProgram&&object.animationPose&&object.animationPose->gpuSkin;
              if(useSkinProgram){
                if(!skinBound){skinProgram.view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);skinBound=true;}
                skinProgram.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
              }else if(skinBound){glUseProgram(program);skinBound=false;}
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
              if(drawUniforms.enabled&&std::none_of(asset.primitives.begin(),asset.primitives.end(),[&](const auto &p){return pass<0?p.alphaMode!=2:(p.alphaMode==2)==bool(pass);}))return;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
              if(special)special->object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);else
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
              if(!useSkinProgram)
#endif
              {
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
                drawUniforms.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
#else
                glUniform1f(yawLocation,object.yaw);glUniform1f(pitchLocation,object.pitch);glUniform1f(rollLocation,object.roll);glUniform1f(scaleLocation,object.scale);
                glUniform3f(offsetLocation,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
#endif
              }
              for(size_t primitive=0;primitive<asset.primitives.size();primitive++){
                const auto &part=asset.primitives[primitive];
                if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(primitiveVisibility&&pass<0&&!primitiveExperiment.mayDraw(object,primitive,targetBounds[i],i,id<0,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels))continue;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(special)special->material(part);else
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(useSkinProgram)skinProgram.material(part);else
#endif
                {
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
                  drawUniforms.material(part);
#else
                  glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);glUniform3fv(sheenLocation,1,part.sheen);
#endif
                }
                culling.material(part,object.animationPose.get());
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(pass<0&&clusterExperiment.draw(object,primitive,targetBounds[i],i,id<0,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels,[&](unsigned count){gpuSync.draw(count);}))continue;
#endif
                if(asset.pbr){
#ifdef SF_GPU_SYNC_EXPERIMENT
                  gpuSync.beginPbr();ScopeExit finishPbrTiming{[&]{gpuSync.endPbr();}};
#endif
                  auto *variant=pbrVariantsEnabled&&pass>=0?pbrVariants.ready(asset,part,frameObjectLights[size_t(&object-script.scene.objects.data())].count,surface->eyePixels):nullptr;
                  auto &activePbr=variant?*variant:(fastPbrDepth&&pass<0?selectedDepthProgram:pbrProgram);
                  activePbr.view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);
                  activePbr.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
                  if(pass>=0)activePbr.lights(frameObjectLights[size_t(&object-script.scene.objects.data())].values(),surfaceX,surfaceY,surfaceZ);
                  if(pass>=0)activePbr.shadows(spotShadows,frameObjectLights[size_t(&object-script.scene.objects.data())].values(),surfaceX,surfaceY,surfaceZ);
                  activePbr.material(asset,part,object.animationPose?object.animationPose->geometry.primitives[primitive]:part,object.animationPose?0:object.lodLevel,pass<0);
                  ScopeExit restorePbr{[&]{activePbr.release();glUseProgram(program);}};
                  if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);else drawModelPrimitive(part,0,object.lodLevel);
#ifdef SF_GPU_SYNC_EXPERIMENT
                  gpuSync.draw(modelDrawVertexCount(part,object.animationPose?0:object.lodLevel));
#endif
                  continue;
                }
                GLuint activeProgram=program;
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(useSkinProgram)activeProgram=generalProgram;
#endif
                if(object.animationPose&&bindSkinPrimitive(asset,part,*object.animationPose,activeProgram)){
                  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glDrawElements(GL_TRIANGLES,GLsizei(part.skinIndices.size()),GL_UNSIGNED_INT,nullptr);unbindSkinPrimitive(activeProgram);
#ifdef SF_GPU_SYNC_EXPERIMENT
                  gpuSync.draw(unsigned(part.skinIndices.size()));
#endif
                  continue;
                }
#ifdef SF_GPU_SYNC_EXPERIMENT
                const size_t firstPrimitive=primitive;
#endif
                if(object.animationPose&&drawRigidBatch(asset,*object.animationPose,primitive)){
#ifdef SF_GPU_SYNC_EXPERIMENT
                  unsigned count=0;for(size_t p=firstPrimitive;p<=primitive;p++)count+=asset.primitives[p].vertexCount;gpuSync.draw(count);
#endif
                  continue;
                }
#ifdef SF_GPU_SYNC_EXPERIMENT
                gpuSync.draw(modelDrawVertexCount(part,object.animationPose?0:object.lodLevel));
#endif
                if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);
                else drawModelPrimitive(part,0,object.lodLevel);
              }
            };
            if(useInstancing){
              std::vector<const Object*> objects;objects.reserve(script.scene.objects.size());for(const auto &object:script.scene.objects)if(selected(object))objects.push_back(&object);
              const float origin[3]={surfaceX,surfaceY,surfaceZ};
              instanceExperiment.draw(script.scene,culling,objects,pass,program,anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,origin,drawObject,[&](unsigned count){
#ifdef SF_GPU_SYNC_EXPERIMENT
                gpuSync.draw(count);
#else
                (void)count;
#endif
              },restoredProgram);return;
            }
            for(const auto &object:script.scene.objects)if(selected(object))drawObject(object);
          },&region);
        }
        ready.push_back(surface.get());
      }
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
      if(frame%120==0)std::fprintf(stdout,"DRAW_UNIFORMS {\"frame\":%d,\"issued\":%llu,\"skipped\":%llu}\n",frame,(unsigned long long)uniformIssued,(unsigned long long)uniformSkipped);
#endif
#ifdef SF_RIGID_BATCH_EXPERIMENT
      if(frame%120==0)std::fprintf(stdout,"RIGID_BATCH_DRAWS {\"draws\":%llu,\"mergedParts\":%llu}\n",(unsigned long long)rigidBatchStats.draws,(unsigned long long)rigidBatchStats.mergedParts);
#endif
      const auto finishStart=std::chrono::steady_clock::now();
      const double frameRenderMs=std::chrono::duration<double,std::milli>(finishStart-renderStart).count();renderMs+=frameRenderMs;
#ifdef SF_ANIMATION_PROFILE
      animationFrame.renderMs+=frameRenderMs;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
      gpuSync.complete();
      if(useInstancing)instanceExperiment.report();
      if(primitiveVisibility)primitiveExperiment.report();
      if(clusterExperiment.mode!=ClusterVisibilityExperiment::Off)clusterExperiment.report();
#else
      glFinish();
#endif
      const double frameFinishMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-finishStart).count();finishMs+=frameFinishMs;
#ifdef SF_ANIMATION_PROFILE
      animationFrame.finishMs+=frameFinishMs;
#endif
      glBindFramebuffer(GL_FRAMEBUFFER,0);
      std::set<int> changingSubmissions;
      if(pendingTileChange){
        const auto parent=surfaces.find(pendingTileChange->parent),child=surfaces.find(pendingTileChange->child);
        const int group=surfaceRenderGroup(pendingTileChange->parent);
        if(parent==surfaces.end()||(!pendingTileChange->merge&&child==surfaces.end()))pendingTileChange.reset();
        else{
          changingSubmissions.insert(pendingTileChange->parent);if(!pendingTileChange->merge)changingSubmissions.insert(pendingTileChange->child);
          const bool parentVisible=std::find(ready.begin(),ready.end(),parent->second.get())!=ready.end();
          const bool childVisible=!pendingTileChange->merge&&std::find(ready.begin(),ready.end(),child->second.get())!=ready.end();
          if((parentVisible||culledSurfaces.count(pendingTileChange->parent))&&(pendingTileChange->merge||childVisible||culledSurfaces.count(pendingTileChange->child))){
            auto retire=[&](Surface &retired){destroySurface(retired,display);};vr::EVROverlayError error=vr::VROverlayError_None;
            if(pendingTileChange->merge){
              if(parentVisible)error=submitSurface(*overlays,*parent->second,frame,retire);
              else if(parent->second->previous){retire(*parent->second->previous);parent->second->previous.reset();}
            }else error=submitGroupSplit(*overlays,*parent->second,*child->second,frame,retire,parentVisible,childVisible);
            if(error==vr::VROverlayError_None){pendingTileChange.reset();regroupAfter[group]=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
            else fprintf(stderr,"[surface] adaptation presentation error=%d; retaining complete fallbacks\n",int(error));
          }
        }
      }
      for(auto *surface:ready){
        if(changingSubmissions.count(surface->objectId))continue;
        const auto error=submitSurface(*overlays,*surface,frame,[&](Surface &retired){destroySurface(retired,display);});
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] submit id=%d error=%d\n",surface->objectId,int(error));
      }
#ifdef SF_GPU_SYNC_EXPERIMENT
      gpuSync.submitted();
#endif
      ++frame;
      if(frame%900==0&&pbrVariantsEnabled)fprintf(stdout,"[pbr] prepared=%zu ready_draws=%zu fallback_draws=%zu\n",pbrVariants.programs.size(),pbrVariants.readyDraws,pbrVariants.fallbackDraws);
      if(frame%900==0){fprintf(stdout,"[profile] picking_ms=%.3f render_submit_ms=%.3f gpu_wait_ms=%.3f physics_ms=%.3f\n",pickingMs/900,renderMs/900,finishMs/900,physicsMs/900);pickingMs=renderMs=finishMs=physicsMs=0;}
      if(frame%900==0){auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-reportStart).count();reportStart=now;fprintf(stdout,"frames=%d fps=%.1f surfaces=%zu ready=%zu gl_error=%x laser_left=%.2f laser_right=%.2f held_left=%d held_right=%d\n",frame,900.0/seconds,surfaces.size(),ready.size(),(unsigned)glGetError(),meshHitDistance[0],meshHitDistance[1],grabs[0].objectId,grabs[1].objectId);fflush(stdout);}
    }
      return 0;
    };
    const int sceneResult=renderActiveScene();
    if(sceneResult>0)return sceneResult;
    if(sceneResult<0)break;
    if(engineStats.update(++diagnosticFrames)){
      uint64_t bytes=0;std::set<GLuint> countedDepth;
      for(const auto &[id,surface]:surfaces)for(const Surface *generation=surface.get();generation;generation=generation->previous.get())for(const auto &buffer:generation->buffers){
        const uint64_t height=generation->eyePixels,width=2*height;
        if(buffer.bo)bytes+=uint64_t(gbm_bo_get_stride(buffer.bo))*height;
        if(buffer.depth&&countedDepth.insert(buffer.depth).second)bytes+=width*height*4; // Depth24 storage commonly occupies 32 bits.
      }
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);if(part.indexBuffer)bytes+=uint64_t(part.vertexCount)*(part.indexType==GL_UNSIGNED_SHORT?2:4);for(const auto &lod:part.lod){if(lod.budgetBytes)bytes+=lod.budgetBytes;else if(lod.indexBuffer)bytes+=uint64_t(lod.count)*(part.indexType==GL_UNSIGNED_SHORT?2:4);}if(part.texture)bytes+=uint64_t(part.width)*part.height*4;}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);
      for(const auto &panel:script.scene.panels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      for(const auto &panel:script.scene.retiredPanels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      bytes+=instanceExperiment.allocatedBytes();
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.skinVbo)bytes+=part.skinVertices.size()*sizeof(float);if(part.skinIbo)bytes+=part.skinIndices.size()*sizeof(uint32_t);}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(size_t s=0;s<object.animationPose->skinTextures.size();s++)if(object.animationPose->skinTextures[s])bytes+=object.animationPose->palettes[s].size()*sizeof(animation::Matrix);
      for(const auto &object:script.scene.objects)if(object.animationPose&&object.animationPose->geometry.batchVbo)bytes+=object.animationPose->geometry.batchBytes;
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){
        if(part.surfaceVbo)bytes+=uint64_t(part.gpuVertexCount)*9*sizeof(float);
        for(const auto &map:part.maps)if(map.texture){uint64_t w=map.width,h=map.height;for(;;){bytes+=w*h*4;if(w==1&&h==1)break;w=std::max(uint64_t(1),w/2);h=std::max(uint64_t(1),h/2);}}
      }
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.surfaceVbo)bytes+=part.surface.size()*sizeof(float);
      std::set<const LightingEnvironment*> countedEnvironments;
      for(const auto &asset:script.scene.assets)if(asset.environment&&countedEnvironments.insert(asset.environment.get()).second)bytes+=asset.environment->gpuBytes();
      std::set<const SheenLookup*> countedSheen;
      for(const auto &asset:script.scene.assets)if(asset.sheenLookup&&asset.sheenLookup->texture&&countedSheen.insert(asset.sheenLookup.get()).second)bytes+=SheenLookup::size*SheenLookup::size*4;
      bytes+=spotShadowDiagnostics.bytes;
      engineStats.gpuAllocatedEstimateBytes=bytes;
    }
#ifdef SF_ANIMATION_PROFILE
    animationFrame.workMs=animationProfileMs(animationFrameStart);
    size_t profiledObjects=0,playingObjects=0;
    for(const auto &object:script.scene.objects)if(object.animationPose){profiledObjects++;playingObjects+=object.animation.playing;}
    animationWindow.record(animationFrame,profiledObjects,playingObjects,animationEpoch);
#endif
    // Drop missed deadlines rather than rendering bursts to repay old frame debt.
    next=std::max(next+std::chrono::microseconds(11111),std::chrono::steady_clock::now());
    std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  return 0;
}
