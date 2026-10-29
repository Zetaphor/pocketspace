#include "model_asset.h"
#include <EGL/egl.h>
#include <cassert>
#include <cstdio>
int main(){
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,nullptr,nullptr));
 const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};EGLConfig config;EGLint count=0;assert(eglChooseConfig(display,attributes,&config,1,&count)&&count);
 const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surfaceAttributes[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttributes);assert(eglMakeCurrent(display,surface,surface,context));
 for(const char *name:{"CesiumMan","InterpolationTest","AnimatedMorphCube"}){
  ModelAsset asset;assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));
  {
   AnimationProfileFrame profile;animationProfileFrame=&profile;ModelPose pose;animation::Playback state;state.clip=0;
   uint64_t vertices=0,bytes=0;for(const auto &part:asset.primitives){vertices+=part.localVertices.size()/5;bytes+=part.vertices.size()*sizeof(float);}
   evaluateModelPose(asset,state,pose);uploadModelPose(pose);glFinish();
   assert(profile.evaluations==1&&profile.vertices==vertices&&profile.uploads==1&&profile.uploadBytes==bytes);
   uploadModelPose(pose);assert(profile.uploads==1&&profile.uploadBytes==bytes); // clean poses do no work
   state.time=.5;evaluateModelPose(asset,state,pose);uploadModelPose(pose);glFinish();
   assert(profile.evaluations==2&&profile.vertices==vertices*2&&profile.uploads==2&&profile.uploadBytes==bytes*2);
   assert(profile.poseMs>=0&&profile.deformBoundsMs>=0&&profile.bvhMs>=0&&profile.uploadMs>=0&&glGetError()==GL_NO_ERROR);
   AnimationProfileFrame sum;sum.add(profile);sum.add(profile);assert(sum.uploadBytes==bytes*4&&sum.evaluations==4);
   AnimationProfileWindow window;window.record(profile,1,1,0);assert(window.frames==0);window.record(profile,1,1,0);assert(window.frames==1);
   window.record(profile,1,0,0);assert(window.frames==0);window.record(profile,1,0,1);assert(window.frames==0);
   animationProfileFrame=nullptr;
  }
 }
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 puts("PASS animation profiling: real rigid/skin/morph evaluation and uploads, exact counters, clean-pose skip, aggregation and workload/epoch boundaries");
}
