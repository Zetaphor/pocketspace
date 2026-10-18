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
#include <thread>
#include <vector>
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
#include <string>
#include <utility>
#include <quickjs.h>
#include "model_asset.h"

constexpr int kEyeSize=2048,kWidth=2*kEyeSize,kHeight=kEyeSize;
struct Buffer {gbm_bo *bo=nullptr;int fd=-1;vr::SharedTextureHandle_t handle=0;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint color=0,depth=0,fbo=0;};
struct Surface {int objectId=0;float width=.7f;vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid;Buffer buffers[3];bool shown=false;};
static PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC imageRenderbuffer=nullptr;
static std::atomic<bool> running{true};
struct Object {int id,asset;float x,y,z,scale,yaw;bool visible=true;};
struct Scene {std::vector<Object> objects;std::vector<ModelAsset> assets;std::vector<int> interactiveIds;int nextId=1;};
static Scene *currentScene=nullptr;
struct ControllerSnapshot {bool valid=false;float x=0,y=0,z=0;float rotation[9]{};};
static ControllerSnapshot controllers[2];
struct PointerSnapshot {bool valid=false,down=false;float x=0,y=0;int hand=-1;};
static std::map<int,PointerSnapshot> pointerStates;
static void stop(int){running=false;}
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
  JS_SetPropertyStr(ctx,object,"x",JS_NewFloat64(ctx,c.x));
  JS_SetPropertyStr(ctx,object,"y",JS_NewFloat64(ctx,c.y));
  JS_SetPropertyStr(ctx,object,"z",JS_NewFloat64(ctx,c.z));
  JSValue rotation=JS_NewArray(ctx);
  for(int i=0;i<9;i++)JS_SetPropertyUint32(ctx,rotation,i,JS_NewFloat64(ctx,c.rotation[i]));
  JS_SetPropertyStr(ctx,object,"rotation",rotation);
  return object;
}
static JSValue jsGetPointer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc>0&&JS_ToInt32(ctx,&id,argv[0])<0)return JS_EXCEPTION;
  if(argc==0&&currentScene&&currentScene->interactiveIds.size()==1)id=currentScene->interactiveIds.front();
  const auto found=pointerStates.find(id);
  const PointerSnapshot state=found==pointerStates.end()?PointerSnapshot{}:found->second;
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));
  JS_SetPropertyStr(ctx,object,"down",JS_NewBool(ctx,state.down));
  JS_SetPropertyStr(ctx,object,"x",JS_NewFloat64(ctx,state.x));
  JS_SetPropertyStr(ctx,object,"y",JS_NewFloat64(ctx,state.y));
  JS_SetPropertyStr(ctx,object,"hand",state.hand==0?JS_NewString(ctx,"left"):state.hand==1?JS_NewString(ctx,"right"):JS_NULL);
  return object;
}
static Object *findObject(int id){
  if(!currentScene)return nullptr;
  for(auto &object:currentScene->objects)if(object.id==id)return &object;
  return nullptr;
}
static JSValue jsEnableObjectInteraction(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||!findObject(id))return JS_ThrowRangeError(ctx,"enableObjectInteraction(objectId) requires a live object");
  auto &ids=currentScene->interactiveIds;
  if(std::find(ids.begin(),ids.end(),id)==ids.end()){
    if(ids.size()>=4)return JS_ThrowRangeError(ctx,"interactive surface limit is four objects");
    ids.push_back(id);
  }
  return JS_UNDEFINED;
}
static bool number(JSContext *ctx,JSValueConst value,float &out){
  double n=0;if(JS_ToFloat64(ctx,&n,value)<0||!std::isfinite(n))return false;out=(float)n;return std::isfinite(out);
}
static JSValue jsLoadModel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<1||currentScene->assets.size()>=32)return JS_ThrowRangeError(ctx,"loadModel(path) requires a path; limit is 32 assets");
  const char *path=JS_ToCString(ctx,argv[0]);if(!path)return JS_EXCEPTION;
  std::string name(path);JS_FreeCString(ctx,path);
  for(size_t i=0;i<currentScene->assets.size();i++)if(currentScene->assets[i].path==name)return JS_NewInt32(ctx,(int)i+1);
  ModelAsset asset;
  if(!loadModelFile(name,asset))return JS_ThrowTypeError(ctx,"could not load model: %s",name.c_str());
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
static JSValue jsSetObjectPosition(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float x,y,z;
  if(argc<4||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],x)||!number(ctx,argv[2],y)||!number(ctx,argv[3],z))return JS_ThrowTypeError(ctx,"setObjectPosition(id, x, y, z)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->x=x;object->y=y;object->z=z;return JS_UNDEFINED;
}
static JSValue jsSetObjectYaw(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float yaw;
  if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],yaw))return JS_ThrowTypeError(ctx,"setObjectYaw(id, radians)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->yaw=yaw;return JS_UNDEFINED;
}
static JSValue jsSetObjectScale(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float scale;
  if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],scale))return JS_ThrowTypeError(ctx,"setObjectScale(id, meters)");
  if(scale<=0||scale>2)return JS_ThrowRangeError(ctx,"scale must be greater than 0 and at most 2 meters");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->scale=scale;return JS_UNDEFINED;
}
static JSValue jsSetObjectVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"setObjectVisible(id, visible)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;
}
static JSValue jsRemoveObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"removeObject(id)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  auto &ids=currentScene->interactiveIds;
  ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
  pointerStates.erase(id);
  currentScene->objects.erase(std::remove_if(currentScene->objects.begin(),currentScene->objects.end(),[id](const Object &entry){return entry.id==id;}),currentScene->objects.end());
  return JS_UNDEFINED;
}
struct ScriptHost {JSRuntime *rt=nullptr;JSContext *ctx=nullptr;JSValue update=JS_UNDEFINED;std::filesystem::file_time_type mtime{};Scene scene;};
static void bind(JSContext *ctx,const char *name,JSCFunction *function,int argc){
  JSValue global=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global,name,JS_NewCFunction(ctx,function,name,argc));JS_FreeValue(ctx,global);
}
static void closeScript(ScriptHost &s){
  for(auto &asset:s.scene.assets)deleteModel(asset);
  if(s.ctx){JS_FreeValue(s.ctx,s.update);JS_FreeContext(s.ctx);}if(s.rt)JS_FreeRuntime(s.rt);
  s={};
}
static bool loadScript(ScriptHost &active,const std::string &path){
  std::ifstream file(path);if(!file){fprintf(stderr,"script unreadable: %s\n",path.c_str());return false;}
  std::ostringstream stream;stream<<file.rdbuf();std::string source=stream.str();
  ScriptHost candidate;candidate.rt=JS_NewRuntime();if(!candidate.rt)return false;
  JS_SetMemoryLimit(candidate.rt,128*1024*1024);
  candidate.ctx=JS_NewContext(candidate.rt);if(!candidate.ctx){closeScript(candidate);return false;}
  currentScene=&candidate.scene;
  bind(candidate.ctx,"print",jsLog,1);
  bind(candidate.ctx,"getController",jsGetController,1);
  bind(candidate.ctx,"enableObjectInteraction",jsEnableObjectInteraction,1);
  bind(candidate.ctx,"getPointer",jsGetPointer,0);
  bind(candidate.ctx,"loadModel",jsLoadModel,1);
  bind(candidate.ctx,"createObject",jsCreateObject,5);
  bind(candidate.ctx,"setObjectPosition",jsSetObjectPosition,4);
  bind(candidate.ctx,"setObjectYaw",jsSetObjectYaw,2);
  bind(candidate.ctx,"setObjectScale",jsSetObjectScale,2);
  bind(candidate.ctx,"setObjectVisible",jsSetObjectVisible,2);
  bind(candidate.ctx,"removeObject",jsRemoveObject,1);
  JSValue result=JS_Eval(candidate.ctx,source.c_str(),source.size(),path.c_str(),JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(candidate.ctx);const char *message=JS_ToCString(candidate.ctx,exception);
    fprintf(stderr,"[js] load error: %s\n",message?message:"unknown");if(message)JS_FreeCString(candidate.ctx,message);
    JS_FreeValue(candidate.ctx,exception);JS_FreeValue(candidate.ctx,result);closeScript(candidate);currentScene=&active.scene;return false;
  }
  JS_FreeValue(candidate.ctx,result);
  JSValue global=JS_GetGlobalObject(candidate.ctx);candidate.update=JS_GetPropertyStr(candidate.ctx,global,"update");JS_FreeValue(candidate.ctx,global);
  std::error_code ec;candidate.mtime=std::filesystem::last_write_time(path,ec);
  closeScript(active);active=std::move(candidate);currentScene=&active.scene;
  fprintf(stdout,"[js] loaded %s objects=%zu assets=%zu\n",path.c_str(),active.scene.objects.size(),active.scene.assets.size());fflush(stdout);return true;
}
static bool initEgl(EGLDisplay &display,EGLContext &context,EGLSurface &surface) {
  display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(display,nullptr,nullptr))return false;
  EGLConfig config;EGLint count=0;
  const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)return false;
  const EGLint ctxAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttrs);
  const EGLint surfAttrs[]={EGL_WIDTH,kEyeSize,EGL_HEIGHT,kEyeSize,EGL_NONE};
  surface=eglCreatePbufferSurface(display,config,surfAttrs);
  return eglMakeCurrent(display,surface,surface,context)==EGL_TRUE;
}
static bool makeBuffer(Buffer &b,gbm_device *gbm,EGLDisplay display) {
  b.bo=gbm_bo_create(gbm,kWidth,kHeight,GBM_FORMAT_ABGR8888,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
  if(!b.bo||(b.fd=gbm_bo_get_fd(b.bo))<0)return false;
  vr::DmabufAttributes_t a{};
  a.unWidth=kWidth;a.unHeight=kHeight;a.unDepth=a.unMipLevels=a.unArrayLayers=a.unSampleCount=1;
  a.unFormat=DRM_FORMAT_ABGR8888;a.ulModifier=DRM_FORMAT_MOD_LINEAR;a.unPlaneCount=1;
  a.plane[0].nFd=b.fd;a.plane[0].unOffset=gbm_bo_get_offset(b.bo,0);a.plane[0].unStride=gbm_bo_get_stride(b.bo);
  if(!vr::VRIPCResourceManager()->ImportDmabuf(vr::VRApplication_Overlay,&a,&b.handle)||!b.handle)return false;
  const EGLint attrs[]={EGL_WIDTH,kWidth,EGL_HEIGHT,kHeight,EGL_LINUX_DRM_FOURCC_EXT,EGLint(DRM_FORMAT_ABGR8888),EGL_DMA_BUF_PLANE0_FD_EXT,b.fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,EGLint(a.plane[0].unOffset),EGL_DMA_BUF_PLANE0_PITCH_EXT,EGLint(a.plane[0].unStride),EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,EGLint(DRM_FORMAT_MOD_LINEAR&0xffffffff),EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,EGLint(DRM_FORMAT_MOD_LINEAR>>32),EGL_NONE};
  b.image=createImage(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
  if(b.image==EGL_NO_IMAGE_KHR){fprintf(stderr,"eglCreateImage error=%x\n",(unsigned)eglGetError());return false;}
  glGenRenderbuffers(1,&b.color);glBindRenderbuffer(GL_RENDERBUFFER,b.color);imageRenderbuffer(GL_RENDERBUFFER,b.image);
  glGenRenderbuffers(1,&b.depth);glBindRenderbuffer(GL_RENDERBUFFER,b.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,kWidth,kHeight);
  glGenFramebuffers(1,&b.fbo);glBindFramebuffer(GL_FRAMEBUFFER,b.fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,b.color);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,b.depth);
  bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;glBindFramebuffer(GL_FRAMEBUFFER,0);return ok;
}
static void destroyBuffer(Buffer &buffer,EGLDisplay display){
  if(buffer.fbo)glDeleteFramebuffers(1,&buffer.fbo);
  if(buffer.depth)glDeleteRenderbuffers(1,&buffer.depth);
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
static bool createSurface(Surface &surface,int id,float width,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &anchor){
  surface.objectId=id;surface.width=width;
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
  if(id>0){
    overlays->SetOverlayInputMethod(surface.overlay,vr::VROverlayInputMethod_Mouse);
    overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,true);
    vr::VROverlayIntersectionMaskPrimitive_t mask{};
    mask.m_nPrimitiveType=vr::OverlayIntersectionPrimitiveType_Circle;
    mask.m_Primitive.m_Circle.m_flCenterX=.5f;
    mask.m_Primitive.m_Circle.m_flCenterY=.5f;
    mask.m_Primitive.m_Circle.m_flRadius=.3f;
    overlays->SetOverlayIntersectionMask(surface.overlay,&mask,1);
    vr::HmdVector2_t scale{{1,1}};overlays->SetOverlayMouseScale(surface.overlay,&scale);
  }
  for(auto &buffer:surface.buffers)if(!makeBuffer(buffer,gbm,display)){fprintf(stderr,"[surface] buffer id=%d failed\n",id);return false;}
  fprintf(stdout,"[surface] created id=%d width=%.3f\n",id,width);fflush(stdout);
  return true;
}
static GLuint compile(GLenum type,const char *source) {
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);fprintf(stderr,"shader error: %s\n",log);}
  return shader;
}
static void setVec3(GLuint program,const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
int main(int argc,char **argv) {
  if(argc>2&&std::string(argv[1])=="--inspect"){
    ModelAsset asset;return loadModelFile(argv[2],asset)?0:1;
  }
  std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
  std::string scriptPath=argc>1?argv[1]:"scripts/avocado.js";
  ScriptHost script;if(!loadScript(script,scriptPath))return 13;
  EGLDisplay display;EGLContext context;EGLSurface surface;if(!initEgl(display,context,surface))return 1;
  vr::EVRInitError initError;vr::VR_Init(&initError,vr::VRApplication_Overlay);if(initError!=vr::VRInitError_None)return 2;
  auto *system=vr::VRSystem();auto *overlays=vr::VROverlay();
  int drm=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);auto *gbm=drm>=0?gbm_create_device(drm):nullptr;
  createImage=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  destroyImage=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  imageRenderbuffer=(PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
  if(!gbm||!createImage||!destroyImage||!imageRenderbuffer)return 3;
  const char *vs=R"(#version 300 es
layout(location=0) in vec3 p;layout(location=1) in vec2 uv;
uniform vec3 center,right,up,forward,eye;
uniform vec3 offset;
uniform float yaw,modelScale,panelHalfWidth;
out vec2 t;
void main(){
  float c=cos(yaw),s=sin(yaw);
  vec3 rotated=vec3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);
  vec3 world=center+right*offset.x+up*offset.y+forward*offset.z+modelScale*(right*rotated.x+up*rotated.y-forward*rotated.z);
  vec3 ray=world-eye;
  float distanceAlong=dot(ray,forward);
  float hitScale=dot(center-eye,forward)/distanceAlong;
  vec3 hit=eye+hitScale*ray;
  float x=dot(hit-center,right)/panelHalfWidth;
  float y=dot(hit-center,up)/panelHalfWidth;
  float z=(distanceAlong-0.05)/4.95*2.0-1.0;
  gl_Position=vec4(x*distanceAlong,y*distanceAlong,z*distanceAlong,distanceAlong);
  t=uv;
})";
  const char *fs=R"(#version 300 es
precision mediump float;in vec2 t;uniform sampler2D material;uniform vec4 factor;uniform vec3 sheen;uniform int alphaMode;uniform float alphaCutoff;out vec4 color;
void main(){vec4 texel=texture(material,t);color=texel*factor;color.rgb=clamp(color.rgb+texel.rgb*sheen*0.8,0.0,1.0);if(alphaMode==1&&color.a<alphaCutoff)discard;if(alphaMode==0)color.a=1.0;})";
  GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
  glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 5;
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto anchor=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)anchor.m[r][3]+=anchor.m[r][1]*-.2f+anchor.m[r][2]*-1.f;
  fprintf(stdout,"anchor world=%f,%f,%f\n",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);fflush(stdout);
  std::map<int,std::unique_ptr<Surface>> surfaces;
  auto syncSurfaces=[&]()->bool{
    std::vector<int> desired=script.scene.interactiveIds;
    bool stage=false;
    for(const auto &object:script.scene.objects)if(std::find(desired.begin(),desired.end(),object.id)==desired.end())stage=true;
    if(stage)desired.push_back(0);
    for(auto it=surfaces.begin();it!=surfaces.end();){
      if(std::find(desired.begin(),desired.end(),it->first)==desired.end()){
        pointerStates.erase(it->first);destroySurface(*it->second,display);it=surfaces.erase(it);
      }else ++it;
    }
    for(int id:desired){
      float width=.7f;
      if(id>0){for(const auto &object:script.scene.objects)if(object.id==id){width=std::max(.55f,object.scale*2.1f);break;}}
      auto found=surfaces.find(id);
      if(found==surfaces.end()){
        auto created=std::make_unique<Surface>();
        if(!createSurface(*created,id,width,gbm,display,anchor)){destroySurface(*created,display);return false;}
        surfaces.emplace(id,std::move(created));
      }else if(std::fabs(found->second->width-width)>.0001f){
        found->second->width=width;
        overlays->SetOverlayWidthInMeters(found->second->overlay,width);
      }
    }
    return true;
  };
  if(!syncSurfaces())return 8;
  glUseProgram(program);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glUniform1i(glGetUniformLocation(program,"material"),0);glEnable(GL_DEPTH_TEST);
  setVec3(program,"center",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);
  setVec3(program,"right",anchor.m[0][0],anchor.m[1][0],anchor.m[2][0]);
  setVec3(program,"up",anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]);
  setVec3(program,"forward",-anchor.m[0][2],-anchor.m[1][2],-anchor.m[2][2]);
  GLint yawLocation=glGetUniformLocation(program,"yaw"),scaleLocation=glGetUniformLocation(program,"modelScale"),offsetLocation=glGetUniformLocation(program,"offset");
  GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff");
  GLint sheenLocation=glGetUniformLocation(program,"sheen");
  auto next=std::chrono::steady_clock::now();int frame=0;
  while(running){
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,.011f,poses,vr::k_unMaxTrackedDeviceCount);
    for(int hand=0;hand<2;hand++){
      auto &snapshot=controllers[hand];snapshot={};
      auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      if(device==vr::k_unTrackedDeviceIndexInvalid){if(frame%900==0)fprintf(stdout,"[input] hand=%d device=none\n",hand);continue;}
      const auto &pose=poses[device];
      if(frame%900==0){fprintf(stdout,"[input] hand=%d device=%u pose=%d\n",hand,device,pose.bPoseIsValid);fflush(stdout);}
      if(!pose.bPoseIsValid)continue;
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
        if(loadScript(script,scriptPath)){
          for(auto &[id,surface]:surfaces)destroySurface(*surface,display);
          surfaces.clear();pointerStates.clear();
        }else script.mtime=modified;
      }
    }
    if(!syncSurfaces()){running=false;break;}
    for(auto &[id,surface]:surfaces){
      if(id==0)continue;
      vr::VREvent_t event{};
      auto &state=pointerStates[id];
      while(overlays->PollNextOverlayEvent(surface->overlay,&event,sizeof(event))){
        if(event.eventType==vr::VREvent_FocusLeave){state.valid=false;state.down=false;state.hand=-1;continue;}
        if(event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonDown||event.eventType==vr::VREvent_MouseButtonUp){
          state.valid=true;
          state.x=(event.data.mouse.x-.5f)*surface->width;
          state.y=(.5f-event.data.mouse.y)*surface->width;
          if(event.eventType==vr::VREvent_MouseButtonDown){
            state.down=true;
            auto role=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
            state.hand=role==vr::TrackedControllerRole_LeftHand?0:role==vr::TrackedControllerRole_RightHand?1:-1;
          }
          if(event.eventType==vr::VREvent_MouseButtonUp)state.down=false;
          if(event.eventType!=vr::VREvent_MouseMove){fprintf(stdout,"[input] object=%d event=%u device=%u hand=%d down=%d\n",id,event.eventType,event.trackedDeviceIndex,state.hand,state.down);fflush(stdout);}
        }
      }
    }
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);JS_FreeValue(script.ctx,dt);
      if(JS_IsException(result)){JSValue exception=JS_GetException(script.ctx);const char *message=JS_ToCString(script.ctx,exception);fprintf(stderr,"[js] update error: %s\n",message?message:"unknown");if(message)JS_FreeCString(script.ctx,message);JS_FreeValue(script.ctx,exception);JS_FreeValue(script.ctx,script.update);script.update=JS_UNDEFINED;}
      JS_FreeValue(script.ctx,result);
    }
    for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
      std::vector<Surface*> ready;
      for(auto &[id,surface]:surfaces){
        const Object *target=nullptr;
        if(id>0){for(const auto &object:script.scene.objects)if(object.id==id){target=&object;break;}}
        bool hasVisible=false;
        for(const auto &object:script.scene.objects){
          if(!object.visible)continue;
          const bool interactive=std::find(script.scene.interactiveIds.begin(),script.scene.interactiveIds.end(),object.id)!=script.scene.interactiveIds.end();
          if(id==object.id||id==0&&!interactive){hasVisible=true;break;}
        }
        if(!hasVisible){if(surface->shown){overlays->HideOverlay(surface->overlay);surface->shown=false;}continue;}
        float surfaceX=target?target->x:0,surfaceY=target?target->y:0,surfaceZ=target?target->z:0;
        auto panel=anchor;
        for(int r=0;r<3;r++)panel.m[r][3]+=anchor.m[r][0]*surfaceX+anchor.m[r][1]*surfaceY-anchor.m[r][2]*surfaceZ;
        overlays->SetOverlayTransformAbsolute(surface->overlay,vr::TrackingUniverseStanding,&panel);
        setVec3(program,"center",panel.m[0][3],panel.m[1][3],panel.m[2][3]);
        glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),surface->width*.5f);
        Buffer &buffer=surface->buffers[frame%3];
        glBindFramebuffer(GL_FRAMEBUFFER,buffer.fbo);glViewport(0,0,kWidth,kHeight);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        for(int i=0;i<2;i++){
          auto eyeTransform=system->GetEyeToHeadTransform((vr::EVREye)i);
          float eye[3];for(int r=0;r<3;r++)eye[r]=head.m[r][3]+head.m[r][0]*eyeTransform.m[0][3]+head.m[r][1]*eyeTransform.m[1][3]+head.m[r][2]*eyeTransform.m[2][3];
          setVec3(program,"eye",eye[0],eye[1],eye[2]);glViewport(i*kEyeSize,0,kEyeSize,kEyeSize);
          for(int pass=0;pass<2;pass++){
            if(pass){glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}
            else {glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
            for(const auto &object:script.scene.objects){
              if(!object.visible)continue;
              const bool interactive=std::find(script.scene.interactiveIds.begin(),script.scene.interactiveIds.end(),object.id)!=script.scene.interactiveIds.end();
              if(id!=object.id&&(id!=0||interactive))continue;
              const auto &asset=script.scene.assets[(size_t)object.asset-1];
              glUniform1f(yawLocation,object.yaw);glUniform1f(scaleLocation,object.scale);
              glUniform3f(offsetLocation,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
              for(const auto &part:asset.primitives){
                if((part.alphaMode==2)!=bool(pass))continue;
                glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);
                glUniform3fv(sheenLocation,1,part.sheen);
                glBindBuffer(GL_ARRAY_BUFFER,part.vbo);
                glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
                glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
                glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
              }
            }
          }
          glDepthMask(GL_TRUE);glDisable(GL_BLEND);
        }
        ready.push_back(surface.get());
      }
      glFinish();glBindFramebuffer(GL_FRAMEBUFFER,0);
      for(auto *surface:ready){
        auto &buffer=surface->buffers[frame%3];
        vr::Texture_t texture{&buffer.handle,vr::TextureType_SharedTextureHandle,vr::ColorSpace_Gamma};
        auto error=overlays->SetOverlayTexture(surface->overlay,&texture);
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] submit id=%d error=%d\n",surface->objectId,(int)error);
        if(!surface->shown){overlays->ShowOverlay(surface->overlay);surface->shown=true;}
      }
      if(frame++%900==0){fprintf(stdout,"frames=%d surfaces=%zu ready=%zu gl_error=%x\n",frame,surfaces.size(),ready.size(),(unsigned)glGetError());fflush(stdout);}
    }
    next+=std::chrono::microseconds(11111);std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  for(auto &[id,surface]:surfaces)destroySurface(*surface,display);
  closeScript(script);gbm_device_destroy(gbm);close(drm);vr::VR_Shutdown();
}
