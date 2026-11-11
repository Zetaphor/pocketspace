#pragma once
// Scene types and engine state shared by the script API, the host and regressions.
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
#include <optional>
#include <iterator>
#include "model_asset.h"
#include "model_shaders.h"
#include "controller_input.h"
#include "frame_input.h"
#include "panel.h"
#include "panel_submission.h"
#include "native_scene_resources.h"
#include "eye_calibration.h"
#include "scope_exit.h"
#include "scene_lookup.h"
#include "physics_world.h"
#include "script_telemetry.h"
#include "runtime_stats.h"
#include "group_buffer_budget.h"
#include "script_catalog.h"
#include "event_bus.h"

constexpr int kDefaultEyePixels=768;
enum class GrabButton { Trigger, Grip, Both };
#include "model_lod.h"
struct Object {
  int id,asset;float x,y,z;
  // Size in meters along the object's local axes, applied before rotation.
  std::array<float,3> scale{1,1,1};
  float yaw,pitch=0,roll=0;
  bool visible=true,laserPassThrough=false,grabbable=false,directGrab=true;
  GrabButton grabButton=GrabButton::Trigger;
  bool laserYaw=false,laserPitchWithStickClick=false,depthAdjust=false;
  bool handPitch=false,handYaw=false,handRoll=false;
  animation::Playback animation;
  std::shared_ptr<ModelPose> animationPose;
  int renderGroup=0,renderTile=0;
  int lodLevel=0,container=0;
  std::optional<MaterialOverride> material; // PBR objects only
};
inline float largestScale(const Object &object){return std::max({object.scale[0],object.scale[1],object.scale[2]});}
inline bool validScale(const std::array<float,3> &scale){for(float s:scale)if(!std::isfinite(s)||s<=0||s>2)return false;return true;}
#include "scene_lights.h"
struct RenderGroupSplitPolicy {int maxSurfaces=4,targetEyePixels=2048;bool merge=false,rebalance=false;};
struct Scene {bool assetsReleased=false;std::vector<PointLight> lights;int nextLightId=1;std::set<int> containers;std::map<int,int> containerParents;int nextContainerId=1;std::unique_ptr<physics::World> physics;std::vector<Object> objects;std::map<int,RenderGroupSplitPolicy> renderGroupSplits;std::vector<ModelAsset> assets;std::vector<Panel> panels,retiredPanels;int nextId=1,nextPanelId=1;bool eyeTrackingRequested=false,foveationRequested=false;
  // Bus callbacks live in this scene's context; a candidate's publications wait for its commit.
  std::vector<bus::Subscription> busSubscriptions;std::vector<bus::Message> stagedPublications;};
inline size_t loadedAssetCount(const Scene &scene){return std::count_if(scene.assets.begin(),scene.assets.end(),[](const ModelAsset &asset){return !asset.path.empty();});}
inline bool loadedAsset(const Scene &scene,int id){return id>0&&size_t(id)<=scene.assets.size()&&!scene.assets[size_t(id)-1].path.empty();}
inline std::set<int> containerSubtree(const Scene &scene,int root){
 std::set<int> result;if(!scene.containers.count(root))return result;result.insert(root);
 bool changed=true;while(changed){changed=false;for(auto [child,parent]:scene.containerParents)if(result.count(parent))changed|=result.insert(child).second;}
 return result;
}
// Set whenever a script's runtime and scene are freed. The host returns free heap
// pages to the system once, after that frame's work.
inline bool heapTrimPending=false;
inline int objectSurfaceKey(const Object &object){return object.renderGroup?-(object.renderGroup+256*object.renderTile):object.id;}
inline int surfaceRenderGroup(int key){return key<0?((-key-1)%256)+1:0;}
inline Scene *currentScene=nullptr,*liveScene=nullptr;
inline int activeInstanceId=0;
// Installed only by the VR host; candidate scripts never reserve live resources.
inline std::function<std::string(int)> reserveObjectOverlay;
inline std::function<void(int)> releaseObjectOverlay;
inline std::function<std::string(const Panel&)> reservePanelOverlay;
inline std::function<void(int)> releasePanelOverlay;
inline diagnostics::Sampler engineStats;
struct ControllerSnapshot {bool valid=false,connected=false;float x=0,y=0,z=0;float rotation[9]{};};
inline ControllerSnapshot controllers[2];
inline vr::HmdMatrix34_t trackingAnchor{};inline bool trackingAnchorValid=false;
inline controller_input::Input controllerInput;
inline frame_input::Monitor frameInput;
inline frame_input::Monitor *activeSceneInput=&frameInput;
inline frame_input::Monitor &sceneInput(){return *activeSceneInput;}
inline bool inputPollingRequested=false;inline int meshHoveredObject[2]={0,0},meshCurrentObject[2]={0,0};inline float meshHitDistance[2]={2.f,2.f};
// Scene-space point on the hovered model, valid while meshHoveredObject is set.
inline std::array<float,3> meshHitPoint[2]{};
inline std::chrono::steady_clock::time_point meshLastHit[2]{};
inline bool grabNeedsRelease[2]{};
// One beam per hand, owned by the engine. Scripts request changes; the latest wins.
inline bool laserVisible[2]{};
struct InteractionState {
  int meshHovered[2]{},meshCurrent[2]{};
  float meshDistance[2]{2.f,2.f};
  std::array<float,3> meshPoint[2]{};
  std::chrono::steady_clock::time_point meshLast[2]{};
  bool grabRelease[2]{};
};
inline void swapActiveInteraction(InteractionState &state){
  std::swap(meshHoveredObject,state.meshHovered);
  std::swap(meshHitPoint,state.meshPoint);
  std::swap(meshCurrentObject,state.meshCurrent);
  std::swap(meshHitDistance,state.meshDistance);
  std::swap(meshLastHit,state.meshLast);
  std::swap(grabNeedsRelease,state.grabRelease);
}
inline void resetActiveInteraction(){InteractionState empty;swapActiveInteraction(empty);}

inline JSContext *inputCallbackCtx=nullptr;inline JSValue inputCallback=JS_UNDEFINED;inline int inputCallbackContainer=0;
inline void clearInputCallback(JSContext *ctx){if(inputCallbackCtx==ctx){JS_FreeValue(ctx,inputCallback);inputCallback=JS_UNDEFINED;inputCallbackCtx=nullptr;inputCallbackContainer=0;}}
inline tracking::EyeCalibration eyeCalibration;
inline std::string calibrationOutput;
inline float headBasis[3][3]{};inline bool headBasisValid=false;
namespace scenePersistence {
constexpr size_t maxBytes=16*1024*1024;
constexpr size_t maxPanelBytes=64*1024*1024;
inline std::string directory;
// Script keys always end in .js, so they cannot collide with this namespace.
inline const std::string scratchNamespace="scratch";
inline std::string catalogRoot;
// Selected with the active scene; the pointed-to string belongs to its ScriptHost.
inline const std::string *activeNamespace=&scratchNamespace;
// The directory holding one script's saves; it need not exist yet. Throws for a
// key that is too long or a namespace that is not a plain directory.
inline std::filesystem::path namespaceDirectory(const std::string &storageKey){
  std::filesystem::path base=directory;
  if(base.empty()){
    const char *state=getenv("XDG_STATE_HOME");
    if(state&&std::filesystem::path(state).is_absolute())base=state;
    else{const char *home=getenv("HOME");if(!home||!*home)throw std::runtime_error("HOME or --scene-state-dir required");base=std::filesystem::path(home)/".local/state";}
    base/="pocketspace/scenes";
  }
  if(storageKey.size()>255)throw std::runtime_error("script path is too long for a scene namespace");
  const auto owner=base/storageKey;std::error_code ec;const auto type=std::filesystem::symlink_status(owner,ec).type();
  if(type!=std::filesystem::file_type::not_found&&(ec||type!=std::filesystem::file_type::directory))throw std::runtime_error("scene namespace must be a directory, not a symlink");
  return owner;
}
}
// heap counts the runtime's allocations and outlives it; closeScript frees both.
struct ScriptHost {int instanceId=0;std::string saveNamespace=scenePersistence::scratchNamespace;std::unique_ptr<telemetry::HeapCounter> heap;JSRuntime *rt=nullptr;JSContext *ctx=nullptr;JSValue update=JS_UNDEFINED,callback=JS_UNDEFINED;int callbackContainer=0;std::filesystem::file_time_type mtime{};Scene scene;InteractionState interaction;std::unique_ptr<frame_input::Monitor> inputQueue=std::make_unique<frame_input::Monitor>(0);};
bool loadScript(ScriptHost &active,const std::string &path,const std::function<void()> &beforeCommit={});
// Same transaction for source that is not a file, such as the scratch scene's empty start.
bool loadScriptSource(ScriptHost &active,const std::string &name,const std::string &source,const std::function<void()> &beforeCommit={});
// Evaluation bounds: while set, print() output is captured and running code is
// interrupted after the deadline.
namespace evaluation {
inline std::string *capture=nullptr;
constexpr size_t maxSourceBytes=256*1024,maxOutputBytes=64*1024;
inline std::optional<std::chrono::steady_clock::time_point> deadline;
inline bool interrupted=false;
}
// Installed by the host for scratch's scripts object: takes a control request line
// and returns its response line. Unset outside the VR host and its regressions.
inline std::function<std::string(const std::string&)> scratchControl;
void closeScript(ScriptHost &s);
// Called by the host loop and regressions.
JSValue capturePanelSnapshot(JSContext *ctx,const Panel &panel,size_t &budgetUsed);
Object *findObject(int id);
Panel *findPanel(int id);
JSValue inputEventValue(JSContext *ctx,const frame_input::Event &event);
std::string dispatchInputCallbacks();
void updateScenePhysics(Scene &scene,double elapsed);
void forgetObjectInteraction(int id);
float objectRadius(const Object &object);
bool parseCalibration(JSContext *ctx,JSValueConst value,tracking::EyeCalibration &result);
