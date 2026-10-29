#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t views=0;
 {
  Renderer renderer;
  {
   const ModelDrawLocations locations(renderer.program);ModelDrawUniforms cache(locations,true);PrimitiveAsset part;
   cache.object(0,0,0,1,0,0,0);cache.material(part);const auto first=cache.issued;
   cache.object(0,0,0,1,0,0,0);cache.material(part);assert(cache.issued==first&&cache.skipped==9);
   part.alphaMode=1;part.alphaCutoff=.4f;part.factor[2]=.2f;cache.material(part);assert(cache.issued==first+3);
  }
  for(const char *name:{"Box","ToyCar","AlphaBlendModeTest","InterpolationTest","Fox","AnimatedMorphCube"}){
   renderer.scene.objects.resize(2);auto &asset=renderer.scene.assets[2];deleteModel(asset);asset={};
   assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));uploadModel(asset);
   for(int i=0;i<6;i++){
    Object object{};object.id=i+3;object.asset=3;object.x=(i%3-1)*.18f;object.y=-.1f+(i/3)*.16f;object.scale=.22f;object.yaw=i*.1f;object.pitch=i*.04f;
    if(asset.hasAnimation){object.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=std::string(name)=="InterpolationTest"?1:0;state.time=.1*i;evaluateModelPose(asset,state,*object.animationPose);uploadModelPose(*object.animationPose);}
    renderer.scene.objects.push_back(std::move(object));
   }
   for(int alpha:{-1,0,1,2}){
    if(alpha>=0)for(auto &part:asset.primitives){part.alphaMode=alpha;part.factor[3]=alpha==2?.55f:1;}
    for(auto mode:{ModelInstancing::Off,ModelInstancing::Instanced}){
     renderer.instanceExperiment.mode=mode;
     for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      drawUniformCachingEnabled=false;auto expected=renderer.renderSet({1,3,4,5,6,7,8},head,eye);
      // External writes between renders must not poison the new pass cache.
      glUniform1f(glGetUniformLocation(renderer.program,"modelScale"),17);
      drawUniformCachingEnabled=true;auto actual=renderer.renderSet({1,3,4,5,6,7,8},head,eye);
      assert(expected==actual);assert(std::any_of(expected.begin(),expected.end(),[](auto x){return x!=0;}));++views;
     }
    }
   }
  }
  assert(glGetError()==GL_NO_ERROR);
 }
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS draw uniform cache: %zu exact stereo views, opaque/masked/blended materials, static/rigid/morph/GPU skin, instancing switches and pass invalidation\n",views);
}
