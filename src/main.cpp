// VR host: rendering, overlay submission and the frame loop.
#include "engine_state.h"
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
#include "model_instancing.h"
#include "spot_shadows.h"
#include "pbr_program.h"
#include "pbr_variants.h"
static void stop(int){running=false;}
#include "native_resource_key.h"
#include "script_registry.h"
#include "script_catalog.h"
#include "input_arbitration.h"
#include "panel_pointer.h"
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
  auto *overlays=vr::VROverlay();const std::string key="pocketspace.laser."+std::to_string(hand);
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
// Frame controller /pose/tip and /pose/openxr_aim: 40 degrees about local X.
static void laserRay(const vr::HmdMatrix34_t &pose,int hand,float origin[3],float direction[3]){
  constexpr float tilt=.6981317008f;
  const float localOrigin[3]={hand==0?.012694f:-.012694f,-.02522f,.020687f};
  for(int i=0;i<3;i++){
    origin[i]=pose.m[i][3];
    for(int axis=0;axis<3;axis++)origin[i]+=pose.m[i][axis]*localOrigin[axis];
    direction[i]=-std::sin(tilt)*pose.m[i][1]-std::cos(tilt)*pose.m[i][2];
  }
}
static void updateLaser(vr::VROverlayHandle_t handle,bool &shown,bool visible,int hand,const vr::TrackedDevicePose_t &controller,const vr::HmdMatrix34_t &head,float hitDistance){
  auto *overlays=vr::VROverlay();
  if(!visible||!controller.bPoseIsValid){if(shown){overlays->HideOverlay(handle);shown=false;}return;}
  const auto &pose=controller.mDeviceToAbsoluteTracking;
  const float length=std::clamp(hitDistance,.02f,2.f);
  float direction[3],origin[3],center[3],normal[3],up[3],viewLength=0;
  laserRay(pose,hand,origin,direction);
  for(int i=0;i<3;i++){
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
static void updateManipulation(Scene &scene,std::array<GrabState,2> &grabs,float dt,int instanceId,const std::array<int,2> &rayOwners,std::array<int,2> &grabOwners){
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
    if(!object&&hand.valid&&hand.connected&&!grabNeedsRelease[h]&&(grabOwners[h]==0||grabOwners[h]==instanceId)){
      for(auto &candidate:scene.objects){
        if(!candidate.visible||!candidate.autoManipulate||!candidate.grabbable||ownedByOther(candidate.id))continue;
        auto pointer=pointerStates.find(candidate.id);
        // Native surfaces are rectangles: custom lasers must acquire only a current mesh hit.
        const bool nativeDown=candidate.grabButton!=GrabButton::Grip&&!scene.laserVisible[h]&&pointer!=pointerStates.end()&&pointer->second.down&&pointer->second.hand==h;
        const bool laserDown=rayOwners[h]==instanceId&&meshCurrentObject[h]==candidate.id&&frameGrabDown(h,candidate.grabButton);
        if(nativeDown||laserDown){
          const float dx=candidate.x-hand.x,dy=candidate.y-hand.y,dz=candidate.z-hand.z;
          const float reach=.10f+candidate.scale*.5f;
          const bool direct=candidate.directGrab&&dx*dx+dy*dy+dz*dz<reach*reach;
          beginGrab(grab,candidate,hand,direct);
          grabOwners[h]=instanceId;
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
  std::string calibrationPath;
  const char *userHome=std::getenv("HOME");
  std::filesystem::path scriptsDirectory=userHome?std::filesystem::path(userHome)/".local/share/pocketspace/scripts":"user-scripts";
  std::vector<std::filesystem::path> launchScripts;
  for(int i=1;i<argc;i++){
    std::string arg=argv[i];
    if(arg=="--eye-calibration"||arg=="--eye-calibration-output"){
      if(i+1==argc){fprintf(stderr,"%s requires a file path\n",arg.c_str());return 13;}
      (arg=="--eye-calibration"?calibrationPath:calibrationOutput)=argv[++i];
    }else if(arg=="--scene-state-dir"){
      if(i+1==argc){fprintf(stderr,"--scene-state-dir requires a directory\n");return 13;}
      scenePersistence::directory=argv[++i];
    }else if(arg=="--scripts-dir"){
      if(i+1==argc){fprintf(stderr,"--scripts-dir requires a directory\n");return 13;}
      scriptsDirectory=argv[++i];
    }else if(arg=="--launch"){
      if(i+1==argc){fprintf(stderr,"--launch requires a catalog-relative script\n");return 13;}
      launchScripts.emplace_back(argv[++i]);
    }else{fprintf(stderr,"Unknown argument: %s (use --scripts-dir and --launch for scripts)\n",arg.c_str());return 13;}
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
  scenePersistence::catalogRoot=std::filesystem::absolute(scriptsDirectory).string();
  ScriptRegistry scriptRegistry;
  std::string launchError;
  for(const auto &name:launchScripts){
    std::filesystem::path resolved;
    if(!resolveCatalogScript(scriptsDirectory,name,resolved,launchError)){
      fprintf(stderr,"[lifecycle] launch %s failed: %s\n",name.c_str(),launchError.c_str());return 13;
    }
    int id=0;
    if(!scriptRegistry.launch(resolved.string(),id,launchError)){
      fprintf(stderr,"[lifecycle] launch %s failed: %s\n",name.c_str(),launchError.c_str());return 13;
    }
  }
  EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
  ScopeExit eglCleanup{[&]{if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}}};
  ScopeExit scriptCleanup{[&]{
    for(int id:scriptRegistry.allIds())if(auto *entry=scriptRegistry.find(id);entry&&entry->host.ctx){
      ScriptScope scope(entry->host);closeScript(entry->host);resetActiveInteraction();
    }
  }};
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
  std::array<panelPointer::Pointer,2> panelPointers;
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
#ifdef SF_PBR_VARIANT_EXPERIMENT
  ScopeExit variantReport{[&]{
    size_t programs=0,fallbackDraws=0;uint64_t binaryBytes=0;double compileMs=0;
    for(const auto &[id,owner]:renderStates){programs+=owner->pbrVariants.programs.size();binaryBytes+=owner->pbrVariants.binaryBytes();compileMs+=owner->pbrVariants.compileMs;fallbackDraws+=owner->pbrVariants.fallbackDraws;}
    printf("PBR_VARIANTS_CACHE programs=%zu binary_bytes=%llu compile_ms=%.4f fallback_draws=%zu\n",programs,(unsigned long long)binaryBytes,compileMs,fallbackDraws);
  }};
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
  auto releaseResources=[&](SceneResources &resources){resources.release(
    [&](Surface &surface){destroySurface(surface,display);},
    [&](vr::VROverlayHandle_t handle){overlays->HideOverlay(handle);overlays->ClearOverlayTexture(handle);overlays->DestroyOverlay(handle);});};
  scriptRegistry.setBeforeStop([&](int id){
    for(auto &pointer:panelPointers)pointer.forgetInstance(id);
    if(auto owner=resourcesByInstance.find(id);owner!=resourcesByInstance.end())releaseResources(*owner->second);
    if(auto owner=renderStates.find(id);owner!=renderStates.end())owner->second->resetScene();
  });
  ScopeExit clearStopHook{[&]{scriptRegistry.setBeforeStop({});}};
  ScopeExit overlayCleanup{[&]{
    for(auto laser:lasers)if(laser!=vr::k_ulOverlayHandleInvalid)overlays->DestroyOverlay(laser);
    for(auto &[id,resources]:resourcesByInstance)releaseResources(*resources);
    fprintf(stdout,"[surface] grouped_buffer_bytes=%llu peak=%llu limit=%llu\n",(unsigned long long)groupBufferBudget.used,(unsigned long long)groupBufferBudget.peak,(unsigned long long)GroupBufferBudget::limit);
  }};
  if(!createLaser(lasers[0],0)||!createLaser(lasers[1],1))return 9;
  GLint maxRenderbufferPixels=0;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRenderbufferPixels);
  const int maxGroupPixels=std::min(4096,maxRenderbufferPixels/2);
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
    for(auto it=panelOverlays.begin();it!=panelOverlays.end();){
      const int id=it->first;
      const bool exists=findSceneItem(host.scene.panels,id)!=nullptr;
      if(exists){++it;continue;}
      overlays->HideOverlay(it->second);overlays->ClearOverlayTexture(it->second);overlays->DestroyOverlay(it->second);
      panelSubmissions.erase(id);
      it=panelOverlays.erase(it);
    }
    for(auto &retired:host.scene.retiredPanels)panelDestroy(retired);
    host.scene.retiredPanels.clear();
    for(auto &panel:host.scene.panels){
      if(!panel.visible){if(panelOverlays.count(panel.id))hidePanel(*overlays,panelOverlays.at(panel.id),panelSubmissions[panel.id]);}
      if(panel.dirty)panelUpload(panel);
      if(panelOverlays.count(panel.id))continue;
      reservePanelOverlay(panel);
    }
  };
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
  double pickingMs=0,renderMs=0,finishMs=0,physicsMs=0;

#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *groupPolicy=std::getenv("SF_GPU_GROUP_POLICY");
  groupOccluderTieBreak=groupPolicy&&std::strcmp(groupPolicy,"occluders")==0;
  GpuSyncExperiment gpuSync;
  const char *instanceMode=std::getenv("SF_GPU_INSTANCING");
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
    bool eyeTrackingRequested=false,foveationRequested=false;
    for(int id:scriptRegistry.runningIds())if(const auto *instance=scriptRegistry.find(id)){
      eyeTrackingRequested|=instance->host.scene.eyeTrackingRequested;
      foveationRequested|=instance->host.scene.foveationRequested;
    }
    controllerInput.poll(eyeTrackingRequested,foveationRequested);
    if(frame%30==0)for(int id:scriptRegistry.runningIds()){
      auto *entry=scriptRegistry.find(id);
      if(!entry||entry->state!=ScriptState::Running)continue;
      auto &host=entry->host;
      std::error_code ec;const auto modified=std::filesystem::last_write_time(entry->path,ec);
      if(ec||modified==host.mtime)continue;
      const bool loaded=scriptRegistry.reload(id,[&]{
        for(auto &pointer:panelPointers)pointer.forgetInstance(id);
        if(auto owner=resourcesByInstance.find(id);owner!=resourcesByInstance.end())releaseResources(*owner->second);
        if(auto owner=renderStates.find(id);owner!=renderStates.end())owner->second->resetScene();
        pointerStates.clear();
        sceneInput().resetScene();
        for(int hand=0;hand<2;hand++){
          hoveredObject[hand]=meshHoveredObject[hand]=meshCurrentObject[hand]=0;
          meshHitDistance[hand]=2;meshLastHit[hand]={};grabNeedsRelease[hand]=true;
        }
#ifdef SF_ANIMATION_PROFILE
        ++animationEpoch;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
        primitiveExperiment.reset();clusterExperiment.reset();
#endif
        fprintf(stdout,"[lifecycle] instance=%d scene state reset\n",id);
      });
      if(loaded&&pbrVariantsEnabled&&renderStates.count(id)){
        for(auto &asset:host.scene.assets)if(!asset.uploaded)uploadModel(asset);
        renderStates.at(id)->pbrVariants.prepare(host.scene);
      }
      if(!loaded)host.mtime=modified;
    }
    std::array<std::optional<inputArbitration::Target>,2> rayTargets{};
    std::array<int,2> rayOwners{};
    std::array<int,2> grabOwners{};
    const auto runningSceneIds=scriptRegistry.runningIds();
    for(int hand=0;hand<2;hand++){
      if(!controllers[hand].valid||!controllers[hand].connected)continue;
      // A held object keeps its owner until release or that instance stops.
      for(int id:runningSceneIds)if(auto render=renderStates.find(id);render!=renderStates.end()&&render->second->grabs[hand].objectId){
        rayTargets[hand]=inputArbitration::Target{id,render->second->grabs[hand].objectId,render->second->grabs[hand].laserDistance};
        grabOwners[hand]=id;
        break;
      }
      if(rayTargets[hand]){rayOwners[hand]=rayTargets[hand]->instanceId;continue;}
      for(int id:runningSceneIds)if(auto *entry=scriptRegistry.find(id)){
        ScriptScope pickScope(entry->host);
        for(const auto &object:entry->host.scene.objects){
          if(!object.visible)continue;
          const auto &asset=entry->host.scene.assets[size_t(object.asset)-1];
          const float distance=meshHit(object,object.animationPose?object.animationPose->geometry:asset,controllers[hand],hand);
          inputArbitration::consider(rayTargets[hand],{id,object.id,distance});
        }
      }
      if(rayTargets[hand])rayOwners[hand]=rayTargets[hand]->instanceId;
    }
    // One ray per hand: the closest of the model hit and every interactive panel wins.
    std::array<float,2> panelHitDistance{-1.f,-1.f};
    {
      vr::TrackedDevicePose_t rayPoses[vr::k_unMaxTrackedDeviceCount]{};
      system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,rayPoses,vr::k_unMaxTrackedDeviceCount);
      const auto &rayHead=rayPoses[vr::k_unTrackedDeviceIndex_Hmd];
      for(int hand=0;hand<2;hand++){
        auto &pointer=panelPointers[hand];
        std::vector<panelPointer::Event> pointerEvents;
        const auto *owner=scriptRegistry.find(pointer.current().instanceId);
        const auto *ownerPanel=owner&&pointer.current().panelId&&owner->state==ScriptState::Running?findSceneItem(owner->host.scene.panels,pointer.current().panelId):nullptr;
        if(pointer.current().panelId&&(!ownerPanel||!ownerPanel->visible||!ownerPanel->interactive))pointer.drop(pointerEvents);
        std::optional<panelPointer::Target> target;float targetDistance=-1.f;
        const auto device=system->GetTrackedDeviceIndexForControllerRole(hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand);
        const bool tracked=controllers[hand].valid&&controllers[hand].connected&&device!=vr::k_unTrackedDeviceIndexInvalid&&rayPoses[device].bPoseIsValid&&rayHead.bPoseIsValid;
        if(tracked&&!grabOwners[hand]){
          float origin[3],direction[3];laserRay(rayPoses[device].mDeviceToAbsoluteTracking,hand,origin,direction);
          for(int id:runningSceneIds)if(const auto *entry=scriptRegistry.find(id))for(const auto &panel:entry->host.scene.panels){
            if(!panel.visible||!panel.interactive)continue;
            const auto transform=panelTransform(panel,anchor,rayHead.mDeviceToAbsoluteTracking);
            const auto hit=panelPointer::intersect(origin,direction,transform.m,panel.widthMeters,panel.heightMeters,panel.width,panel.height);
            if(hit&&(targetDistance<0||hit->distance<targetDistance||(hit->distance==targetDistance&&(id<target->instanceId||(id==target->instanceId&&panel.id<target->panelId))))){
              target=panelPointer::Target{id,panel.id,hit->x,hit->y};targetDistance=hit->distance;
            }
          }
        }
        // A press owns the hand until release, so it cannot also start a model grab.
        if(target&&(pointer.pressed()||inputArbitration::panelWins(targetDistance,rayTargets[hand]))){
          rayTargets[hand].reset();rayOwners[hand]=0;panelHitDistance[hand]=targetDistance;
        }else{
          if(pointer.pressed()){rayTargets[hand].reset();rayOwners[hand]=0;}
          target.reset();
        }
        pointer.step(target,tracked&&frameTriggerDown(hand),pointerEvents);
        for(const auto &event:pointerEvents){
          auto *recipient=scriptRegistry.find(event.instanceId);
          if(!recipient||recipient->state!=ScriptState::Running)continue;
          auto &queue=*recipient->host.inputQueue;
          switch(event.type){
            case panelPointer::EventType::HoverStart:queue.addPanelEvent("panel.hoverStart",event.panelId,hand,event.x,event.y);break;
            case panelPointer::EventType::Hover:queue.addPanelEvent("panel.hover",event.panelId,hand,event.x,event.y);break;
            case panelPointer::EventType::HoverEnd:queue.addOverlayEvent("panel.hoverEnd",event.panelId,hand);break;
            case panelPointer::EventType::Down:queue.addPanelEvent("panel.pointerDown",event.panelId,hand,event.x,event.y,"left",true);break;
            case panelPointer::EventType::Up:queue.addPanelEvent("panel.pointerUp",event.panelId,hand,event.x,event.y,"left",false);break;
          }
        }
      }
    }
    auto renderActiveScene=[&](ScriptInstance &instance)->int{
    auto &script=instance.host;
    const int scriptInstanceId=instance.id;
    auto &sceneRender=*renderStates.at(scriptInstanceId);
    auto &sceneResources=*resourcesByInstance.at(scriptInstanceId);
    auto &surfaces=sceneResources.surfaces;
    auto &panelOverlays=sceneResources.panelOverlays;
    auto &panelSubmissions=sceneResources.panelSubmissions;
    auto &spotShadows=sceneRender.spotShadows;
    auto &pbrVariants=sceneRender.pbrVariants;
    auto &grabs=sceneRender.grabs;
    auto &pendingTileChange=sceneRender.pendingTileChange;
    auto &stableAdjustment=sceneRender.stableAdjustment;
    auto &adjustmentCursor=sceneRender.adjustmentCursor;
    auto &regroupAfter=sceneRender.regroupAfter;
    auto &nextAdaptationCheck=sceneRender.nextAdaptationCheck;
    auto &nextGroupResize=sceneRender.nextGroupResize;
    auto &ready=sceneRender.ready;
    auto &frameBounds=sceneRender.frameBounds;
    auto &frameObjectLights=sceneRender.frameObjectLights;
    auto &instanceExperiment=sceneRender.instanceExperiment;
    auto &lastAnimationTick=sceneRender.lastAnimationTick;
    ScriptScope sceneScope(script);
    const auto animationTick=std::chrono::steady_clock::now();
    const double animationDt=std::chrono::duration<double>(animationTick-lastAnimationTick).count();lastAnimationTick=animationTick;
    for(auto &object:script.scene.objects)if(object.animation.clip>=0){
      const auto &asset=script.scene.assets[size_t(object.asset)-1];
      if(animation::advancePlayback(object.animation,asset.animations,animationDt))evaluateModelPose(asset,object.animation,*object.animationPose);
    }
    if(!syncSurfaces(script,sceneResources)){running=false;return -1;}
    syncPanels(script,sceneResources,sceneRender);
    for(auto &[id,surface]:surfaces){
      if(id<=0)continue;
      vr::VREvent_t event{};
      auto &state=pointerStates[id];
      state.scrollX=state.scrollY=0;
      while(overlays->PollNextOverlayEvent(surface->overlay,&event,sizeof(event))){
        if(event.eventType==vr::VREvent_FocusLeave){state.valid=false;state.down=false;state.secondaryDown=false;if(state.hand>=0&&hoveredObject[state.hand]==id){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",id,state.hand);hoveredObject[state.hand]=0;}state.hand=-1;continue;}
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
          if(eventHand>=0&&event.eventType==vr::VREvent_MouseMove&&hoveredObject[eventHand]!=id){if(hoveredObject[eventHand])sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[eventHand],eventHand);hoveredObject[eventHand]=id;sceneInput().addOverlayEvent("overlay.surfaceHoverStart",id,eventHand);}
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
    if(streamWasStarted&&!frameInput.started()){
      for(int h=0;h<2;h++)grabNeedsRelease[h]=true;
      for(const auto &object:script.scene.objects){forgetObjectInteraction(object.id);if(script.scene.physics)script.scene.physics->cancelThrow(object.id);}
    }
    for(int h=0;h<2;h++)if(!controllers[h].connected){
      frameInput.invalidateHand(h);
      sceneInput().invalidateHand(h);
      for(auto &[id,pointer]:pointerStates)if(pointer.hand==h)pointer={};
      if(hoveredObject[h]){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[h],h);hoveredObject[h]=0;}
    }
    const auto pickingStart=std::chrono::steady_clock::now();
    for(int hand=0;hand<2;hand++){
      const bool ownsRay=rayOwners[hand]==instance.id;
      int hit=ownsRay&&rayTargets[hand]?rayTargets[hand]->localId:0;
      float nearest=ownsRay&&rayTargets[hand]?rayTargets[hand]->distance:std::numeric_limits<float>::max();
      meshCurrentObject[hand]=hit;
      const auto now=std::chrono::steady_clock::now();
      const auto *previousHit=findSceneItem(script.scene.objects,meshHoveredObject[hand]);
      if(hit)meshLastHit[hand]=now;
      else if(ownsRay&&controllers[hand].connected&&previousHit&&previousHit->visible&&now-meshLastHit[hand]<std::chrono::milliseconds(100)){
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
    auto failScene=[&](const std::string &error){
      fprintf(stderr,"[lifecycle] instance=%d stopped after error: %s\n",instance.id,error.c_str());
      scriptRegistry.failActive(instance.id,error);
      return 0;
    };
    if(const auto error=dispatchInputCallbacks();!error.empty())return failScene(error);
    updateManipulation(script.scene,grabs,1.f/90.f,instance.id,rayOwners,grabOwners);
    for(int hand=0;hand<2;hand++)if(grabs[hand].objectId)meshHitDistance[hand]=grabs[hand].laserDistance;
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);JS_FreeValue(script.ctx,dt);
      if(JS_IsException(result)){
        JSValue exception=JS_GetException(script.ctx);
        const char *message=JS_ToCString(script.ctx,exception);
        std::string error=message?message:"update failed";
        if(message)JS_FreeCString(script.ctx,message);
        JS_FreeValue(script.ctx,exception);JS_FreeValue(script.ctx,result);
        return failScene(error);
      }
      JS_FreeValue(script.ctx,result);
    }
    const auto physicsStart=std::chrono::steady_clock::now();
    try{updateScenePhysics(script.scene,animationDt);}
    catch(const std::exception &error){return failScene(error.what());}
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
      const int completedFrame=frame+1;
      if(completedFrame%900==0&&pbrVariantsEnabled)fprintf(stdout,"[pbr] prepared=%zu ready_draws=%zu fallback_draws=%zu\n",pbrVariants.programs.size(),pbrVariants.readyDraws,pbrVariants.fallbackDraws);
    }
      return 0;
    };
    int sceneResult=0;
    for(int id:scriptRegistry.runningIds()){
      auto *instance=scriptRegistry.find(id);
      if(!instance||instance->state!=ScriptState::Running)continue;
      if(!renderStates.count(id)){
        auto owner=std::make_unique<SceneRenderState>();
        if(!owner->spotShadows.prepare())return 5;
#ifdef SF_GPU_SYNC_EXPERIMENT
        if(std::getenv("SF_GPU_MULTIPART_INSTANCING"))owner->instanceExperiment.enableMultipartExperiment();
        if(instanceMode){owner->instanceExperiment.mode=std::strcmp(instanceMode,"1")==0?ModelInstancing::Instanced:ModelInstancing::Original;owner->instanceExperiment.prewarm();}
#endif
        if(pbrVariantsEnabled){
          for(auto &asset:instance->host.scene.assets)if(!asset.uploaded)uploadModel(asset);
          owner->pbrVariants.prepare(instance->host.scene);
        }
        renderStates.emplace(id,std::move(owner));
      }
      if(!resourcesByInstance.count(id))resourcesByInstance.emplace(id,std::make_unique<SceneResources>(id));
      sceneResult=renderActiveScene(*instance);
      if(sceneResult!=0)break;
    }
    if(sceneResult>0)return sceneResult;
    if(sceneResult<0)break;
    // Lasers are shared overlays: apply visibility and the selected hit once after all scenes update.
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    const bool headValid=poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid;
    for(int hand=0;hand<2;hand++){
      // Interactive panels need a visible pointer even when no script requests a beam.
      bool visible=panelHitDistance[hand]>=0;
      for(int id:scriptRegistry.runningIds()){
        const auto *entry=scriptRegistry.find(id);
        if(entry&&entry->host.scene.laserVisible[hand]){visible=true;break;}
      }
      const auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      const auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      vr::TrackedDevicePose_t pose{};
      if(device!=vr::k_unTrackedDeviceIndexInvalid)pose=poses[device];
      const float distance=panelHitDistance[hand]>=0?panelHitDistance[hand]:rayTargets[hand]?rayTargets[hand]->distance:2.f;
      updateLaser(lasers[hand],laserShown[hand],visible&&headValid,hand,pose,
                  poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking,distance);
    }
    ++frame;
    if(frame%900==0){
      const auto now=std::chrono::steady_clock::now();
      const double seconds=std::chrono::duration<double>(now-reportStart).count();reportStart=now;
      size_t surfaceCount=0,readyCount=0;
      for(const auto &[id,owner]:resourcesByInstance)surfaceCount+=owner->surfaces.size();
      for(const auto &[id,owner]:renderStates)readyCount+=owner->ready.size();
      fprintf(stdout,"frames=%d fps=%.1f scenes=%zu surfaces=%zu ready=%zu gl_error=%x\n",frame,900.0/seconds,scriptRegistry.runningIds().size(),surfaceCount,readyCount,(unsigned)glGetError());fflush(stdout);
      fprintf(stdout,"[profile] picking_ms=%.3f render_submit_ms=%.3f gpu_wait_ms=%.3f physics_ms=%.3f\n",pickingMs/900,renderMs/900,finishMs/900,physicsMs/900);
      pickingMs=renderMs=finishMs=physicsMs=0;
    }
    if(engineStats.update(++diagnosticFrames)){
      uint64_t bytes=0;std::set<GLuint> countedDepth;
      for(const auto &[ownerId,owner]:resourcesByInstance)for(const auto &[id,surface]:owner->surfaces)for(const Surface *generation=surface.get();generation;generation=generation->previous.get())for(const auto &buffer:generation->buffers){
        const uint64_t height=generation->eyePixels,width=2*height;
        if(buffer.bo)bytes+=uint64_t(gbm_bo_get_stride(buffer.bo))*height;
        if(buffer.depth&&countedDepth.insert(buffer.depth).second)bytes+=width*height*4; // Depth24 storage commonly occupies 32 bits.
      }
      std::set<const LightingEnvironment*> countedEnvironments;
      std::set<const SheenLookup*> countedSheen;
      for(int id:scriptRegistry.runningIds()){
        const auto *entry=scriptRegistry.find(id);
        if(!entry)continue;
        const auto &script=entry->host;
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);if(part.indexBuffer)bytes+=uint64_t(part.vertexCount)*(part.indexType==GL_UNSIGNED_SHORT?2:4);for(const auto &lod:part.lod){if(lod.budgetBytes)bytes+=lod.budgetBytes;else if(lod.indexBuffer)bytes+=uint64_t(lod.count)*(part.indexType==GL_UNSIGNED_SHORT?2:4);}if(part.texture)bytes+=uint64_t(part.width)*part.height*4;}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);
      for(const auto &panel:script.scene.panels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      for(const auto &panel:script.scene.retiredPanels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      if(auto render=renderStates.find(id);render!=renderStates.end())bytes+=render->second->instanceExperiment.allocatedBytes();
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.skinVbo)bytes+=part.skinVertices.size()*sizeof(float);if(part.skinIbo)bytes+=part.skinIndices.size()*sizeof(uint32_t);}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(size_t s=0;s<object.animationPose->skinTextures.size();s++)if(object.animationPose->skinTextures[s])bytes+=object.animationPose->palettes[s].size()*sizeof(animation::Matrix);
      for(const auto &object:script.scene.objects)if(object.animationPose&&object.animationPose->geometry.batchVbo)bytes+=object.animationPose->geometry.batchBytes;
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){
        if(part.surfaceVbo)bytes+=uint64_t(part.gpuVertexCount)*9*sizeof(float);
        for(const auto &map:part.maps)if(map.texture){uint64_t w=map.width,h=map.height;for(;;){bytes+=w*h*4;if(w==1&&h==1)break;w=std::max(uint64_t(1),w/2);h=std::max(uint64_t(1),h/2);}}
      }
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.surfaceVbo)bytes+=part.surface.size()*sizeof(float);
      for(const auto &asset:script.scene.assets)if(asset.environment&&countedEnvironments.insert(asset.environment.get()).second)bytes+=asset.environment->gpuBytes();
      for(const auto &asset:script.scene.assets)if(asset.sheenLookup&&asset.sheenLookup->texture&&countedSheen.insert(asset.sheenLookup.get()).second)bytes+=SheenLookup::size*SheenLookup::size*4;
      }
      bytes+=spotShadowDiagnostics.bytes;
      engineStats.gpuAllocatedEstimateBytes=bytes;
    }
#ifdef SF_ANIMATION_PROFILE
    animationFrame.workMs=animationProfileMs(animationFrameStart);
    size_t profiledObjects=0,playingObjects=0;
    for(int id:scriptRegistry.runningIds())if(const auto *entry=scriptRegistry.find(id))
      for(const auto &object:entry->host.scene.objects)if(object.animationPose){profiledObjects++;playingObjects+=object.animation.playing;}
    animationWindow.record(animationFrame,profiledObjects,playingObjects,animationEpoch);
#endif
    // Drop missed deadlines rather than rendering bursts to repay old frame debt.
    next=std::max(next+std::chrono::microseconds(11111),std::chrono::steady_clock::now());
    std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  return 0;
}
