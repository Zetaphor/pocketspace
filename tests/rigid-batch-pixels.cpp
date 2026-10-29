#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t views=0;
 {
  Renderer renderer;
  for(const char *name:{"Box","ToyCar","AlphaBlendModeTest","InterpolationTest","Fox","AnimatedMorphCube"}){
   std::vector<GLuint> retired;
   for(size_t i=2;i<renderer.scene.objects.size();i++)if(auto pose=renderer.scene.objects[i].animationPose)if(pose->geometry.batchVbo)retired.push_back(pose->geometry.batchVbo);
   renderer.scene.objects.resize(2);for(auto buffer:retired)assert(!glIsBuffer(buffer));auto &asset=renderer.scene.assets[2];deleteModel(asset);asset={};
   assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));uploadModel(asset);
   for(int i=0;i<6;i++){
    Object object{};object.id=i+3;object.asset=3;object.x=(i%3-1)*.18f;object.y=-.1f+(i/3)*.16f;object.scale=.22f;object.yaw=i*.1f;object.pitch=i*.04f;
    if(asset.hasAnimation){object.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=std::string(name)=="InterpolationTest"?1:0;state.time=.1*i;evaluateModelPose(asset,state,*object.animationPose);uploadModelPose(*object.animationPose);}
    renderer.scene.objects.push_back(std::move(object));
   }
   for(int round=0;round<3;round++){
    if(asset.hasAnimation)for(size_t i=2;i<renderer.scene.objects.size();i++){
     auto &pose=*renderer.scene.objects[i].animationPose;animation::Playback state;
     state.clip=std::string(name)=="InterpolationTest"?(round==0?1:round==1?5:8):0;state.time=.2*round+.1*i;
     evaluateModelPose(asset,state,pose);uploadModelPose(pose);
     if(pose.geometry.batchVbo){
      glBindBuffer(GL_ARRAY_BUFFER,pose.geometry.batchVbo);const auto *data=static_cast<const float*>(glMapBufferRange(GL_ARRAY_BUFFER,0,pose.geometry.batchBytes,GL_MAP_READ_BIT));assert(data);
      size_t offset=0;for(const auto &part:pose.geometry.primitives){assert(std::equal(part.vertices.begin(),part.vertices.end(),data+offset));offset+=part.vertices.size();}glUnmapBuffer(GL_ARRAY_BUFFER);
     }
    }
    if(round==2&&asset.rigidBatchEligible)asset.primitives[4].factor[0]=.2f; // Material barrier inside the nine-part run.
   for(int alpha:{-1,0,1,2}){
    if(alpha>=0)for(auto &part:asset.primitives){part.alphaMode=alpha;part.factor[3]=alpha==2?.55f:1;}
    for(auto mode:{ModelInstancing::Off,ModelInstancing::Instanced}){
     renderer.instanceExperiment.mode=mode;
     for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      rigidBatchEnabled=false;auto expected=renderer.renderSet({1,3,4,5,6,7,8},head,eye);
      // External writes between renders must not poison the new pass cache.
      glUniform1f(glGetUniformLocation(renderer.program,"modelScale"),17);
      rigidBatchEnabled=true;rigidBatchStats={};auto actual=renderer.renderSet({1,3,4,5,6,7,8},head,eye);
      if(asset.rigidBatchEligible&&alpha==0)assert(rigidBatchStats.mergedParts>0);assert(expected==actual);assert(std::any_of(expected.begin(),expected.end(),[](auto x){return x!=0;}));++views;
     }
    }
   }
  }
  }
  assert(glGetError()==GL_NO_ERROR);
 }
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS rigid adjacent batches: %zu exact stereo views, opaque/masked/blended materials, static/rigid/morph/GPU skin, instancing switches and material barriers, stream ranges, clip changes and cleanup\n",views);
}
