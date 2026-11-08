#pragma once
// Surfaces, layout and render passes shared by the frame loop and regressions.
#include "engine_state.h"
struct Buffer {gbm_bo *bo=nullptr;int fd=-1;vr::SharedTextureHandle_t handle=0;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint color=0,depth=0,fbo=0;bool ownsDepth=false;};
struct Surface {int instanceId=0,objectId=0,eyePixels=kDefaultEyePixels,referenceEyePixels=kDefaultEyePixels,shrinkFrames=0;float width=.7f;vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid,stagingOverlay=vr::k_ulOverlayHandleInvalid;Buffer buffers[3];bool stagingShown=false,shown=false,panel=false,resolutionLimited=false;std::unique_ptr<Surface> previous;uint64_t groupBufferBytes=0;std::chrono::steady_clock::time_point resizeAfter{};};
inline GroupBufferBudget groupBufferBudget;
float surfaceCoverageWidth(float width,int allocated,int requested,bool grouped);
// Transfer retained snapshots before an in-flight output tile is removed.
// The surviving output must keep the complete old cohort until presentation.
void preserveSurfaceFallback(Surface &removed,Surface &survivor);
inline PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;
inline PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
inline PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC imageRenderbuffer=nullptr;
inline std::atomic<bool> running{true};
#include "model_instancing.h"
#include "spot_shadows.h"
#include "pbr_program.h"
#include "pbr_variants.h"
inline void stop(int){running=false;}
#include "native_resource_key.h"
#include "script_registry.h"
#include "engine_settings.h"
#include "control_service.h"
#include "script_catalog.h"
#include "input_arbitration.h"
#include "panel_pointer.h"
bool initEgl(EGLDisplay &display,EGLContext &context,EGLSurface &surface);
void makeDepthStorage(Buffer &buffer,int width,int height,GLuint sharedDepth=0);
bool makeBuffer(Buffer &b,gbm_device *gbm,EGLDisplay display,int eyePixels,GLuint sharedDepth=0);
void destroyBuffer(Buffer &buffer,EGLDisplay display);
void destroySurface(Surface &surface,EGLDisplay display);
bool createSurface(Surface &surface,int instanceId,int id,float width,int eyePixels,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &anchor,const std::string &suffix={});
// Keep the overlay handle stable while replacing its three color buffers.
// Called between frames for this surface, after the previous frame's glFinish.
bool resizeSurfaceBuffers(Surface &surface,int pixels,gbm_device *gbm,EGLDisplay display);
// Shared groups resize as their projected bounds move. Keep the old overlay
// visible until the replacement has a finished texture and has been shown.
bool replaceGroupedSurface(Surface &surface,int pixels,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &transform);
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
int groupBufferPixels(int required,int maximum);
// Independent buffers: 25% headroom in 64-pixel steps, at most the reference. With
// the 60% shrink threshold, a rotating object's changing crop does not reallocate.
int surfaceBufferPixels(int required,int reference);
GLuint compile(GLenum type,const char *source);
inline void setVec3(GLint location,float x,float y,float z){glUniform3f(location,x,y,z);}
void faceViewer(vr::HmdMatrix34_t &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,float right[3],float up[3],float forward[3]);
// object is the followed object for object placement; without it such a panel is not shown.
vr::HmdMatrix34_t panelTransform(const Panel &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const Object *object=nullptr);
inline const Object *panelObject(const Panel &panel,const Scene &scene){return panel.mode=="object"&&panel.objectId?findSceneItem(scene.objects,panel.objectId):nullptr;}
// A panel following an object shows only while that object exists and is visible.
inline bool panelShown(const Panel &panel,const Scene &scene){
  if(!panel.visible)return false;
  if(panel.mode!="object")return true;
  const auto *object=panelObject(panel,scene);return object&&object->visible;
}
bool createLaser(vr::VROverlayHandle_t &handle,int hand);
// Frame controller /pose/tip and /pose/openxr_aim: 40 degrees about local X.
void laserRay(const vr::HmdMatrix34_t &pose,int hand,float origin[3],float direction[3]);
void updateLaser(vr::VROverlayHandle_t handle,bool &shown,bool visible,int hand,const vr::TrackedDevicePose_t &controller,const vr::HmdMatrix34_t &head,float hitDistance);
struct GrabState {
  int objectId=0;bool direct=false;int releaseFrames=0;
  float offset[3]{},startHand[9]{},startPitch=0,startYaw=0,startRoll=0,laserDistance=2.f;
};
// Distant-grab thumbstick control: dead zone, radians per second and meters per second.
constexpr float kStickDeadZone=.15f,kStickTurnRate=3.f,kStickDepthRate=1.5f;
void beginGrab(GrabState &grab,const Object &object,const ControllerSnapshot &hand,bool direct);
void objectRotationMatrix(float pitch,float yaw,float roll,float m[9]);
inline bool frameButtonDown(int hand,const char *path){auto it=frameInput.hand(hand).raw.find(path);return it!=frameInput.hand(hand).raw.end()&&it->second.isBoolean&&it->second.boolean;}
inline float frameAxis(int hand,const char *path){auto it=frameInput.hand(hand).raw.find(path);return it==frameInput.hand(hand).raw.end()?0.f:it->second.value;}
inline bool frameTriggerDown(int hand){return frameButtonDown(hand,"/input/trigger/click")||frameAxis(hand,"/input/trigger/value")>.2f;}
bool frameGrabDown(int hand,GrabButton button);
void updateManipulation(Scene &scene,std::array<GrabState,2> &grabs,float dt,int instanceId,const std::array<int,2> &rayOwners,std::array<int,2> &grabOwners);
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
ProjectedBounds projectWorldBounds(const WorldBounds &world,const float eye[3],const float right[3],const float up[3],const float forward[3]);
ProjectedBounds projectedBounds(const Object &object,const ModelAsset &geometry,const vr::HmdMatrix34_t &anchor,const float eye[3],const float right[3],const float up[3],const float forward[3]);
struct FrameBoundsCache {
  struct Entry {WorldBounds world;ProjectedBounds common[2];};
  const Scene *scene=nullptr;std::vector<Entry> entries;
  // Headset pixels per unit view tangent for this frame; zero when SteamVR has not
  // reported a projection, which leaves surfaces at their reference density.
  float displayFocal=0;
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
bool mayOcclude(const ProjectedBounds &occluder,const ProjectedBounds &target);
struct ScissorRegion {int x=0,y=0,width=0,height=0;};
// Convert the target's conservative angular bounds to its overlay pixels.
// Invalid/near-plane bounds retain the full viewport. Padding covers raster edges.
ScissorRegion projectedScissor(const ProjectedBounds &bounds,const float center[3],const float eye[3],const float right[3],const float up[3],const float forward[3],float halfWidth,int pixels);
// Crop only transparent margins: physical width and pixel count scale together,
// leaving the original sample spacing and centered pixel grid unchanged.
int objectCropPixels(const ProjectedBounds bounds[2],const float center[3],const float eyes[2][3],const float right[3],const float up[3],const float forward[3],float referenceWidth,int referencePixels);
// Grouped surfaces never sample more densely than the headset can show at their
// depth, with kDisplayOversample headroom for resampling. Independent surfaces keep
// their reference density: their in-place resize clears the overlay for a frame.
constexpr float kDisplayOversample=1.25f;
struct GroupLayout {float width=.55f;int pixels=128;ProjectedBounds bounds[2];bool limited=false;};
// Project all members onto one plane for both eyes. Match the most demanding
// member's reference angular density, rather than stretching a 768px texture,
// capped at the cache's display density.
GroupLayout groupLayout(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const float center[3],const float right[3],const float up[3],const float forward[3],float fallbackWidth,int maxPixels=4096,const FrameBoundsCache *cache=nullptr);
struct GroupPlane {
  vr::HmdMatrix34_t transform{};
  float sceneOrigin[3]{},right[3]{},up[3]{},forward[3]{};
};
// All grouped tiles lie on one plane. Otherwise late head-pose reprojection
// moves an occluder's color and its cutout in another tile by different amounts.
GroupPlane groupPlane(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache *cache=nullptr);
struct GroupSplitPlan {
  std::vector<const Object*> members[2];int pixels[2]{};float widths[2]{};
  uint64_t parentPixelsSquared=0,childPixelsSquared=0;
  explicit operator bool()const{return !members[0].empty()&&!members[1].empty();}
};
bool finiteGroupBounds(const std::vector<const Object*> &members,const FrameBoundsCache &cache);
GroupLayout tileLayout(const std::vector<const Object*> &items,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum);
// Bounded quartile cuts, with deterministic tie-breaking and preserved density.
GroupSplitPlan partitionGroup(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,uint64_t baseline);
GroupSplitPlan planGroupSplit(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,int allocated,const RenderGroupSplitPolicy &policy);
struct GroupAdjustmentPlan {
  GroupSplitPlan partition;bool merge=false;
  explicit operator bool()const{return merge||bool(partition);}
};
GroupAdjustmentPlan planGroupAdjustment(const std::vector<const Object*> &first,const std::vector<const Object*> &second,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,const RenderGroupSplitPolicy &policy);
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
  ModelInstancing instancing{ModelInstancing::Instanced};
  FrameBoundsCache frameBounds;
  std::vector<ObjectLightSet> frameObjectLights;
  std::array<GrabState,2> grabs{};
  std::vector<Surface*> ready;
  std::chrono::steady_clock::time_point lastAnimationTick=std::chrono::steady_clock::now();
  std::optional<PendingTileChange> pendingTileChange;
  StableGroupAdjustment stableAdjustment;
  size_t adjustmentCursor=0;
  std::map<int,std::chrono::steady_clock::time_point> regroupAfter;
  std::chrono::steady_clock::time_point nextAdaptationCheck=std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point nextGroupResize=std::chrono::steady_clock::now();
  void resetScene(){
    spotShadows.reset();pbrVariants.clear();instancing.reset();
    frameBounds={};frameObjectLights.clear();ready.clear();grabs={};
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
inline bool rayTriangle(const float o[3],const float d[3],const float a[3],const float b[3],const float c[3],float &t){float e1[3],e2[3],p[3],q[3],s[3];for(int i=0;i<3;i++){e1[i]=b[i]-a[i];e2[i]=c[i]-a[i];}for(int i=0;i<3;i++){p[i]=d[(i+1)%3]*e2[(i+2)%3]-d[(i+2)%3]*e2[(i+1)%3];}float det=e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2];if(std::fabs(det)<1e-6f)return false;float inv=1.f/det;for(int i=0;i<3;i++)s[i]=o[i]-a[i];float u=(s[0]*p[0]+s[1]*p[1]+s[2]*p[2])*inv;if(u<0||u>1)return false;for(int i=0;i<3;i++)q[i]=s[(i+1)%3]*e1[(i+2)%3]-s[(i+2)%3]*e1[(i+1)%3];float v=(d[0]*q[0]+d[1]*q[1]+d[2]*q[2])*inv;if(v<0||u+v>1)return false;t=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])*inv;return t>=0;}
// The engine laser in scene coordinates; direction is unit length.
void laserSceneRay(const ControllerSnapshot &hand,int handIndex,float origin[3],float direction[3]);
float meshHit(const Object &object,const ModelAsset &asset,const ControllerSnapshot &hand,int handIndex,bool accelerated=true);

// Estimated GPU allocations owned by one running script: its surfaces (including a
// retained previous generation), model buffers and textures, deformed and skinned
// buffers, panels, instancing buffer and shadow map. Shared items are left to
// sharedGpuEstimate. Estimates from sizes, not driver measurements.
inline uint64_t ownedGpuEstimate(const Scene &scene,const std::map<int,std::unique_ptr<Surface>> &surfaces,const SceneRenderState *render){
  uint64_t bytes=0;std::set<GLuint> countedDepth;
  for(const auto &[id,surface]:surfaces)for(const Surface *generation=surface.get();generation;generation=generation->previous.get())for(const auto &buffer:generation->buffers){
    const uint64_t height=generation->eyePixels,width=2*height;
    if(buffer.bo)bytes+=uint64_t(gbm_bo_get_stride(buffer.bo))*height;
    if(buffer.depth&&countedDepth.insert(buffer.depth).second)bytes+=width*height*4; // Depth24 storage commonly occupies 32 bits.
  }
  for(const auto &asset:scene.assets)for(const auto &part:asset.primitives){
    if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);
    if(part.indexBuffer)bytes+=uint64_t(part.vertexCount)*(part.indexType==GL_UNSIGNED_SHORT?2:4);
    for(const auto &lod:part.lod){if(lod.budgetBytes)bytes+=lod.budgetBytes;else if(lod.indexBuffer)bytes+=uint64_t(lod.count)*(part.indexType==GL_UNSIGNED_SHORT?2:4);}
    if(part.texture)bytes+=uint64_t(part.width)*part.height*4;
    if(part.skinVbo)bytes+=part.skinVertices.size()*sizeof(float);
    if(part.skinIbo)bytes+=part.skinIndices.size()*sizeof(uint32_t);
    if(part.surfaceVbo)bytes+=uint64_t(part.gpuVertexCount)*9*sizeof(float);
    for(const auto &map:part.maps)if(map.texture){uint64_t w=map.width,h=map.height;for(;;){bytes+=w*h*4;if(w==1&&h==1)break;w=std::max(uint64_t(1),w/2);h=std::max(uint64_t(1),h/2);}}
  }
  for(const auto &object:scene.objects)if(const auto &pose=object.animationPose){
    for(const auto &part:pose->geometry.primitives){if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);if(part.surfaceVbo)bytes+=part.surface.size()*sizeof(float);}
    for(size_t s=0;s<pose->skinTextures.size();s++)if(pose->skinTextures[s])bytes+=pose->palettes[s].size()*sizeof(animation::Matrix);
    if(pose->geometry.batchVbo)bytes+=pose->geometry.batchBytes;
  }
  for(const auto &panel:scene.panels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
  for(const auto &panel:scene.retiredPanels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
  if(render)bytes+=render->instancing.allocatedBytes()+render->spotShadows.allocatedBytes();
  return bytes;
}
// Lighting environments and the sheen table, which scripts loading the same file
// or using sheen share. Each is counted once, however many scripts use it.
inline uint64_t sharedGpuEstimate(const std::vector<const Scene*> &scenes){
  uint64_t bytes=0;std::set<const LightingEnvironment*> environments;std::set<const SheenLookup*> sheen;
  for(const auto *scene:scenes)for(const auto &asset:scene->assets){
    if(asset.environment&&environments.insert(asset.environment.get()).second)bytes+=asset.environment->gpuBytes();
    if(asset.sheenLookup&&asset.sheenLookup->texture&&sheen.insert(asset.sheenLookup.get()).second)bytes+=SheenLookup::size*SheenLookup::size*4;
  }
  return bytes;
}
