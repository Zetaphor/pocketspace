#include "model_asset.h"
#include <EGL/egl.h>
#include <cassert>
#include <cstdio>
int main(){
 rigidPoseCachingEnabled=true;

 gpuSkinningEnabled=false; // Retained CPU reference path.

 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,nullptr,nullptr));
 const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};EGLConfig config;EGLint count=0;assert(eglChooseConfig(display,attributes,&config,1,&count)&&count);
 const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surfaceAttributes[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttributes);EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttributes);assert(eglMakeCurrent(display,surface,surface,context));
 for(const char *name:{"CesiumMan","InterpolationTest","AnimatedMorphCube"}){
  ModelAsset asset;assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));
  {
   AnimationProfileFrame profile;animationProfileFrame=&profile;ModelPose pose;animation::Playback state;state.clip=std::string(name)=="InterpolationTest"?1:0;
   uint64_t vertices=0,bytes=0;for(const auto &part:asset.primitives){vertices+=part.localVertices.size()/5;bytes+=part.vertices.size()*sizeof(float);}
   evaluateModelPose(asset,state,pose);uploadModelPose(pose);glFinish();
   assert(profile.evaluations==1&&profile.vertices==vertices&&profile.uploads==1&&profile.uploadBytes==bytes);
   uploadModelPose(pose);assert(profile.uploads==1&&profile.uploadBytes==bytes); // clean poses do no work
   state.time=.5;evaluateModelPose(asset,state,pose);uploadModelPose(pose);glFinish();
   assert(profile.evaluations==2&&profile.uploads==2);
   if(std::string(name)=="InterpolationTest")assert(profile.vertices<vertices*2&&profile.uploadBytes<bytes*2);
   else assert(profile.vertices==vertices*2&&profile.uploadBytes==bytes*2);
   assert(profile.poseMs>=0&&profile.deformBoundsMs>=0&&profile.bvhMs>=0&&profile.uploadMs>=0&&glGetError()==GL_NO_ERROR);
   AnimationProfileFrame sum;sum.add(profile);sum.add(profile);assert(sum.uploadBytes==profile.uploadBytes*2&&sum.evaluations==4);
   AnimationProfileWindow window;window.record(profile,1,1,0);assert(window.frames==0);window.record(profile,1,1,0);assert(window.frames==1);
   window.record(profile,1,0,0);assert(window.frames==0);window.record(profile,1,0,1);assert(window.frames==0);
   animationProfileFrame=nullptr;
  }
 }
 {
  ModelAsset asset;assert(loadModelFile("assets/samples/InterpolationTest/InterpolationTest.glb",asset));
  ModelPose cached,reference;size_t saved=0;
  for(size_t clip=0;clip<asset.animations.size();clip++)for(double fraction:{0.,.1,.4,.4,.8,1.}){
   animation::Playback state;state.clip=int(clip);state.time=fraction*asset.animations[clip].duration;
   if(fraction==.8){state.transition.duration=1;state.transition.elapsed=.5;state.transition.frozen=cached.nodes;}
   rigidPoseCachingEnabled=false;evaluateModelPose(asset,state,reference);uploadModelPose(reference);
   rigidPoseCachingEnabled=true;evaluateModelPose(asset,state,cached);
   // Multiple evaluations before upload must preserve pending changes.
   evaluateModelPose(asset,state,cached);
   saved+=std::count_if(cached.parts.begin(),cached.parts.end(),[](const auto &p){return !p.uploadDirty;});
   uploadModelPose(cached);assert(!cached.dirty&&cached.radius==reference.radius);
   for(int axis=0;axis<3;axis++)assert(cached.geometry.low[axis]==reference.geometry.low[axis]&&cached.geometry.high[axis]==reference.geometry.high[axis]);
   for(size_t part=0;part<asset.primitives.size();part++){
    const auto &a=cached.geometry.primitives[part],&b=reference.geometry.primitives[part];assert(a.vertices==b.vertices);
    glBindBuffer(GL_ARRAY_BUFFER,a.vbo);const auto *data=static_cast<const float*>(glMapBufferRange(GL_ARRAY_BUFFER,0,a.vertices.size()*sizeof(float),GL_MAP_READ_BIT));assert(data);
    assert(std::equal(a.vertices.begin(),a.vertices.end(),data));glUnmapBuffer(GL_ARRAY_BUFFER);
   }
   assert(cached.geometry.pickNodes.size()==reference.geometry.pickNodes.size());
   for(size_t i=0;i<cached.geometry.pickNodes.size();i++)for(int axis=0;axis<3;axis++){
    assert(cached.geometry.pickNodes[i].low[axis]==reference.geometry.pickNodes[i].low[axis]);assert(cached.geometry.pickNodes[i].high[axis]==reference.geometry.pickNodes[i].high[axis]);
   }
  }
  assert(saved>0&&glGetError()==GL_NO_ERROR);
  puts("PASS rigid primitive cache: all clips, frozen transitions, repeated/pre-upload evaluations, exact vertices/bounds/BVH and GPU buffer contents");
 }
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 puts("PASS animation profiling: real rigid/skin/morph evaluation and uploads, exact counters, clean-pose skip, aggregation and workload/epoch boundaries");
}
