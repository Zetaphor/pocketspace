#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t views=0;
 {
  Renderer renderer;
  for(const char *name:{"Box","ToyCar","AlphaBlendModeTest","InterpolationTest","Fox","AnimatedMorphCube"}){
   std::vector<GLuint> retired;
   for(size_t i=2;i<renderer.scene.objects.size();i++)if(auto pose=renderer.scene.objects[i].animationPose)for(const auto &part:pose->geometry.primitives)for(auto vao:part.vertexArrays)if(vao)retired.push_back(vao);
   renderer.scene.objects.resize(2);for(auto vao:retired)assert(!glIsVertexArray(vao));
   auto &asset=renderer.scene.assets[2];retired.clear();for(const auto &part:asset.primitives)for(auto vao:part.vertexArrays)if(vao)retired.push_back(vao);
   deleteModel(asset);for(auto vao:retired)assert(!glIsVertexArray(vao));asset={};
   assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));uploadModel(asset);
   for(int i=0;i<6;i++){
    Object object{};object.id=i+3;object.asset=3;object.x=(i%3-1)*.18f;object.y=-.1f+(i/3)*.16f;object.scale=.22f;object.yaw=i*.1f;object.pitch=i*.04f;
    if(asset.hasAnimation){object.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=std::string(name)=="InterpolationTest"?1:0;state.time=.1*i;evaluateModelPose(asset,state,*object.animationPose);uploadModelPose(*object.animationPose);}
    renderer.scene.objects.push_back(std::move(object));
   }
   for(int alpha:{-1,0,1,2}){
    if(alpha>=0)for(auto &part:asset.primitives){part.alphaMode=alpha;part.factor[3]=alpha==2?.55f:1;}
    for(int lod=0;lod<3;lod++)for(auto mode:{ModelInstancing::Off,ModelInstancing::Instanced}){
     for(auto &object:renderer.scene.objects)object.lodLevel=lod;
     renderer.instanceExperiment.mode=mode;
     for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      vertexArrayCachingEnabled=false;auto expected=renderer.renderSet({1,3,4,5,6,7,8},head,eye);
      // External writes between renders must not poison the new pass cache.
      glUniform1f(glGetUniformLocation(renderer.program,"modelScale"),17);
      vertexArrayCachingEnabled=true;auto actual=renderer.renderSet({1,3,4,5,6,7,8},head,eye);
      GLint vao=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);assert(vao==0);assert(expected==actual);assert(std::any_of(expected.begin(),expected.end(),[](auto x){return x!=0;}));++views;
     }
    }
   }
  }
  assert(glGetError()==GL_NO_ERROR);
 }
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS vertex arrays: %zu exact stereo views, opaque/masked/blended materials, static/rigid/morph/GPU skin, instancing switches and LOD, default VAO restoration and resource deletion\n",views);
}
