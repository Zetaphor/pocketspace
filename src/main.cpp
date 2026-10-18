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
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <quickjs.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#include "../avocado_model.h"

constexpr int kEyeSize=1024,kWidth=2048,kHeight=1024;
struct Buffer {gbm_bo *bo=nullptr;int fd=-1;vr::SharedTextureHandle_t handle=0;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint color=0,depth=0,fbo=0;};
static PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC imageRenderbuffer=nullptr;
static std::atomic<bool> running{true};
static float modelYaw=0.f;
static bool modelVisible=true;
static void stop(int){running=false;}
static JSValue jsLog(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc){const char *s=JS_ToCString(ctx,argv[0]);if(s){fprintf(stdout,"[js] %s\n",s);JS_FreeCString(ctx,s);}}
  return JS_UNDEFINED;
}
static JSValue jsSetYaw(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  double value=0;if(argc&&JS_ToFloat64(ctx,&value,argv[0])==0)modelYaw=(float)value;
  return JS_UNDEFINED;
}
static JSValue jsSetVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc)modelVisible=JS_ToBool(ctx,argv[0])>0;
  return JS_UNDEFINED;
}
struct ScriptHost {JSRuntime *rt=nullptr;JSContext *ctx=nullptr;JSValue update=JS_UNDEFINED;std::filesystem::file_time_type mtime{};};
static void closeScript(ScriptHost &s){
  if(s.ctx){JS_FreeValue(s.ctx,s.update);JS_FreeContext(s.ctx);}if(s.rt)JS_FreeRuntime(s.rt);
  s={};
}
static bool loadScript(ScriptHost &active,const std::string &path){
  std::ifstream file(path);if(!file){fprintf(stderr,"script unreadable: %s\n",path.c_str());return false;}
  std::ostringstream stream;stream<<file.rdbuf();std::string source=stream.str();
  ScriptHost candidate;candidate.rt=JS_NewRuntime();if(!candidate.rt)return false;
  JS_SetMemoryLimit(candidate.rt,128*1024*1024);
  candidate.ctx=JS_NewContext(candidate.rt);if(!candidate.ctx){closeScript(candidate);return false;}
  JSValue global=JS_GetGlobalObject(candidate.ctx);
  JS_SetPropertyStr(candidate.ctx,global,"print",JS_NewCFunction(candidate.ctx,jsLog,"print",1));
  JS_SetPropertyStr(candidate.ctx,global,"setModelYaw",JS_NewCFunction(candidate.ctx,jsSetYaw,"setModelYaw",1));
  JS_SetPropertyStr(candidate.ctx,global,"setModelVisible",JS_NewCFunction(candidate.ctx,jsSetVisible,"setModelVisible",1));
  JS_FreeValue(candidate.ctx,global);
  JSValue result=JS_Eval(candidate.ctx,source.c_str(),source.size(),path.c_str(),JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(candidate.ctx);const char *message=JS_ToCString(candidate.ctx,exception);
    fprintf(stderr,"[js] load error: %s\n",message?message:"unknown");if(message)JS_FreeCString(candidate.ctx,message);
    JS_FreeValue(candidate.ctx,exception);JS_FreeValue(candidate.ctx,result);closeScript(candidate);return false;
  }
  JS_FreeValue(candidate.ctx,result);
  global=JS_GetGlobalObject(candidate.ctx);candidate.update=JS_GetPropertyStr(candidate.ctx,global,"update");JS_FreeValue(candidate.ctx,global);
  std::error_code ec;candidate.mtime=std::filesystem::last_write_time(path,ec);
  closeScript(active);active=candidate;fprintf(stdout,"[js] loaded %s\n",path.c_str());fflush(stdout);return true;
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
static GLuint compile(GLenum type,const char *source) {
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);fprintf(stderr,"shader error: %s\n",log);}
  return shader;
}
static void setVec3(GLuint program,const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
int main(int argc,char **argv) {
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
  Buffer buffers[3];for(auto &buffer:buffers)if(!makeBuffer(buffer,gbm,display)){fprintf(stderr,"shared buffer failed\n");return 4;}
  const char *vs=R"(#version 300 es
layout(location=0) in vec3 p;layout(location=1) in vec2 uv;
uniform vec3 center,right,up,forward,eye;
uniform float yaw;
out vec2 t;
void main(){
  float c=cos(yaw),s=sin(yaw);
  vec3 rotated=vec3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);
  vec3 world=center+0.55*(right*rotated.x+up*rotated.y-forward*rotated.z);
  vec3 ray=world-eye;
  float distanceAlong=dot(ray,forward);
  float hitScale=dot(center-eye,forward)/distanceAlong;
  vec3 hit=eye+hitScale*ray;
  float x=dot(hit-center,right)/0.35;
  float y=dot(hit-center,up)/0.35;
  float z=(distanceAlong-0.05)/4.95*2.0-1.0;
  gl_Position=vec4(x*distanceAlong,y*distanceAlong,z*distanceAlong,distanceAlong);
  t=uv;
})";
  const char *fs=R"(#version 300 es
precision mediump float;in vec2 t;uniform sampler2D material;out vec4 color;
void main(){color=texture(material,t);if(color.a<0.1)discard;})";
  GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
  glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 5;
  int tw=0,th=0,tc=0;auto *pixels=stbi_load("assets/avocado_0.png",&tw,&th,&tc,4);if(!pixels)return 6;
  GLuint material;glGenTextures(1,&material);glBindTexture(GL_TEXTURE_2D,material);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,tw,th,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glGenerateMipmap(GL_TEXTURE_2D);stbi_image_free(pixels);
  std::vector<float> vertices;vertices.reserve(kAvocadoIndexCount*5);
  for(uint32_t i=0;i<kAvocadoIndexCount;i++){auto &a=kAvocadoVertices[kAvocadoIndices[i]];vertices.insert(vertices.end(),{a.x,a.y,a.z,a.u,1.f-a.v});}
  GLuint vbo;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto panel=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)panel.m[r][3]+=panel.m[r][1]*-.2f+panel.m[r][2]*-1.f;
  vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid;
  auto err=overlays->CreateOverlay("sf_stereo_overlay_engine","SF Stereo Overlay Engine",&overlay);if(err!=vr::VROverlayError_None)return 8;
  err=overlays->SetOverlayFlag(overlay,vr::VROverlayFlags_SideBySide_Parallel,true);if(err!=vr::VROverlayError_None)return 9;
  err=overlays->SetOverlayWidthInMeters(overlay,.7f);if(err!=vr::VROverlayError_None)return 10;
  vr::VRTextureBounds_t bounds{0,1,1,0};err=overlays->SetOverlayTextureBounds(overlay,&bounds);if(err!=vr::VROverlayError_None)return 11;
  err=overlays->SetOverlayTransformAbsolute(overlay,vr::TrackingUniverseStanding,&panel);if(err!=vr::VROverlayError_None)return 12;
  fprintf(stdout,"panel world=%f,%f,%f\n",panel.m[0][3],panel.m[1][3],panel.m[2][3]);fflush(stdout);
  glUseProgram(program);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,material);glUniform1i(glGetUniformLocation(program,"material"),0);glEnable(GL_DEPTH_TEST);
  setVec3(program,"center",panel.m[0][3],panel.m[1][3],panel.m[2][3]);
  setVec3(program,"right",panel.m[0][0],panel.m[1][0],panel.m[2][0]);
  setVec3(program,"up",panel.m[0][1],panel.m[1][1],panel.m[2][1]);
  setVec3(program,"forward",-panel.m[0][2],-panel.m[1][2],-panel.m[2][2]);
  auto next=std::chrono::steady_clock::now();int frame=0;
  while(running){
    if(frame%30==0){std::error_code ec;auto modified=std::filesystem::last_write_time(scriptPath,ec);if(!ec&&modified!=script.mtime){if(!loadScript(script,scriptPath))script.mtime=modified;}}
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);JS_FreeValue(script.ctx,dt);
      if(JS_IsException(result)){JSValue exception=JS_GetException(script.ctx);const char *message=JS_ToCString(script.ctx,exception);fprintf(stderr,"[js] update error: %s\n",message?message:"unknown");if(message)JS_FreeCString(script.ctx,message);JS_FreeValue(script.ctx,exception);JS_FreeValue(script.ctx,script.update);script.update=JS_UNDEFINED;}
      JS_FreeValue(script.ctx,result);
    }
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
      Buffer &buffer=buffers[frame%3];glBindFramebuffer(GL_FRAMEBUFFER,buffer.fbo);glViewport(0,0,kWidth,kHeight);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      for(int i=0;i<2;i++){
        auto eyeTransform=system->GetEyeToHeadTransform((vr::EVREye)i);
        float eye[3];for(int r=0;r<3;r++)eye[r]=head.m[r][3]+head.m[r][0]*eyeTransform.m[0][3]+head.m[r][1]*eyeTransform.m[1][3]+head.m[r][2]*eyeTransform.m[2][3];
        setVec3(program,"eye",eye[0],eye[1],eye[2]);glViewport(i*kEyeSize,0,kEyeSize,kEyeSize);
        glUniform1f(glGetUniformLocation(program,"yaw"),modelYaw);
        if(modelVisible)glDrawArrays(GL_TRIANGLES,0,kAvocadoIndexCount);
      }
      if(frame==0){std::vector<unsigned char> rgba(kWidth*kHeight*4);glReadPixels(0,0,kWidth,kHeight,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());size_t opaque[2]={0,0};for(int y=0;y<kHeight;y++)for(int x=0;x<kWidth;x++)opaque[x/kEyeSize]+=rgba[(size_t(y)*kWidth+x)*4+3]!=0;fprintf(stdout,"first frame opaque=%zu,%zu\n",opaque[0],opaque[1]);}
      glFinish();glBindFramebuffer(GL_FRAMEBUFFER,0);
      vr::Texture_t texture{&buffer.handle,vr::TextureType_SharedTextureHandle,vr::ColorSpace_Gamma};
      err=overlays->SetOverlayTexture(overlay,&texture);if(err!=vr::VROverlayError_None)fprintf(stderr,"submit err=%d\n",(int)err);
      if(frame==0)overlays->ShowOverlay(overlay);
      if(frame++%900==0){fprintf(stdout,"frames=%d visible=%d gl_error=%x\n",frame,(int)overlays->IsOverlayVisible(overlay),(unsigned)glGetError());fflush(stdout);}
    }
    next+=std::chrono::microseconds(11111);std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);overlays->DestroyOverlay(overlay);
  for(auto &buffer:buffers){if(buffer.fbo)glDeleteFramebuffers(1,&buffer.fbo);if(buffer.depth)glDeleteRenderbuffers(1,&buffer.depth);if(buffer.color)glDeleteRenderbuffers(1,&buffer.color);if(buffer.image!=EGL_NO_IMAGE_KHR)destroyImage(display,buffer.image);if(buffer.handle)vr::VRIPCResourceManager()->UnrefResource(buffer.handle);if(buffer.fd>=0)close(buffer.fd);if(buffer.bo)gbm_bo_destroy(buffer.bo);}
  closeScript(script);gbm_device_destroy(gbm);close(drm);vr::VR_Shutdown();
}
