// Exercise the production ordinary/general routing through real GLES passes.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t views=0;
 {
  Renderer renderer;ordinaryVertexEnabled=true;
  assert(!renderer.ordinaryProgram.select(true)&&renderer.ordinaryProgram.id()==0);
  const char *names[]={"Fox","Box","RiggedFigure","InterpolationTest","RiggedSimple","AnimatedMorphCube","CesiumMan","ToyCar","Avocado","AlphaBlendModeTest","Fox"};
  renderer.scene.objects.clear();
  for(size_t i=0;i<std::size(names);i++){
   renderer.scene.assets.emplace_back();auto &asset=renderer.scene.assets.back();
   const auto path=std::string(names[i])=="Avocado"?std::string("assets/Avocado.glb"):std::string("assets/samples/")+names[i]+"/"+names[i]+".glb";
   assert(loadModelFile(path,asset));uploadModel(asset);
   Object object{};object.id=int(i)+1;object.asset=int(renderer.scene.assets.size());
   object.x=(int(i%4)-1.5f)*.24f;object.y=(1-int(i/4))*.22f;object.scale=.2f;object.pitch=.07f*i;object.yaw=.13f*i;
   if(asset.hasAnimation){
    gpuSkinningEnabled=i!=10;object.animationPose=std::make_shared<ModelPose>();
    animation::Playback state;state.clip=std::string(names[i])=="InterpolationTest"?1:0;state.time=.1*i;
    evaluateModelPose(asset,state,*object.animationPose);uploadModelPose(*object.animationPose);
   }
   renderer.scene.objects.push_back(std::move(object));
  }
  for(int i=0;i<2;i++){
   auto object=renderer.scene.objects[8];object.id=12+i;object.x=-.12f+i*.24f;object.y=-.5f;
   renderer.scene.objects.push_back(std::move(object));
  }
  gpuSkinningEnabled=true;
  assert(renderer.scene.objects[0].animationPose->gpuSkin&&!renderer.scene.objects[10].animationPose->gpuSkin);
  for(int phase=0;phase<3;phase++){
   if(phase){
    for(auto &object:renderer.scene.objects)if(object.animationPose){
     auto &asset=renderer.scene.assets[object.asset-1];gpuSkinningEnabled=object.id!=11;
     animation::Playback state;state.clip=asset.rigidBatchEligible?1:0;state.time=.1*phase+.02*object.id;
     state.transition.duration=1;state.transition.elapsed=.3;state.transition.frozen=object.animationPose->nodes;
     evaluateModelPose(asset,state,*object.animationPose);uploadModelPose(*object.animationPose);
    }
    gpuSkinningEnabled=true;
   }
   for(auto &asset:renderer.scene.assets)for(auto &part:asset.primitives){part.alphaMode=phase;part.factor[3]=phase==2?.55f:1;}
   for(auto &object:renderer.scene.objects)if(!object.animationPose)object.lodLevel=phase;
   for(auto mode:{ModelInstancing::Off,ModelInstancing::Instanced}){
    renderer.instanceExperiment.mode=mode;
    const auto batches=renderer.instanceExperiment.submittedBatches();
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,-.2f,-1.5f}})for(int eye=0;eye<2;eye++){
     for(const auto &members:{std::vector<int>{1,2,3,4,5,6},std::vector<int>{7,8,9,10,11,12,13},std::vector<int>{1,2,3,4,5,6,7,8,9,10,11,12,13}}){
      ordinaryVertexEnabled=false;auto expected=renderer.renderSet(members,head,eye,0,true,1,true);
      ordinaryVertexEnabled=true;auto actual=renderer.renderSet(members,head,eye,0,true,1,true);
      assert(expected==actual&&std::any_of(actual.begin(),actual.end(),[](auto v){return v!=0;}));++views;
      GLint active=0;glGetIntegerv(GL_CURRENT_PROGRAM,&active);assert(GLuint(active)==renderer.program);
      for(GLuint attribute:{2u,3u,6u,7u}){GLint enabled=1;glGetVertexAttribiv(attribute,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);assert(!enabled);}
     }
    }
    if(mode==ModelInstancing::Instanced&&phase<2)assert(renderer.instanceExperiment.submittedBatches()>batches);
   }
   const GLuint previous=renderer.ordinaryProgram.id();assert(previous&&glIsProgram(previous));
   renderer.ordinaryProgram.reset();assert(!glIsProgram(previous)&&!renderer.ordinaryProgram.id());
  }
 }
 {
  ScriptHost host;Renderer renderer;auto &ordinary=renderer.ordinaryProgram;ordinaryVertexEnabled=true;
  {
   OrdinaryModelProgram borrowed;borrowed.borrow(renderer.program);borrowed.reset();assert(glIsProgram(renderer.program));
  }
  const auto invalid=std::filesystem::temp_directory_path()/("sf-ordinary-invalid-"+std::to_string(getpid())+".js");
  ScopeExit remove{[&]{std::filesystem::remove(invalid);}};std::ofstream(invalid)<<"throw Error('EXPECTED_ORDINARY_ROLLBACK')";
  auto js=[&](const char *code){auto result=JS_Eval(host.ctx,code,strlen(code),"ordinary-lifecycle",JS_EVAL_TYPE_GLOBAL);assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);};
  GLuint old=0;
  for(int cycle=0;cycle<10;cycle++){
   assert(loadScript(host,"tests/gpu-skinning-mixed-api.js"));
   if(old)assert(ordinary.id()==old&&glIsProgram(old));
   assert(ordinary.select(false));old=ordinary.id();
   auto *active=host.ctx;assert(!loadScript(host,invalid.string()));assert(host.ctx==active&&ordinary.id()==old&&glIsProgram(old));
   for(auto &asset:host.scene.assets)uploadModel(asset);
   for(int phase=0;phase<2;phase++){
    js("copyMixed()");assert(host.scene.objects.size()==12);
    for(auto &object:host.scene.objects){
     if(object.animationPose)uploadModelPose(*object.animationPose);
     const bool skin=object.animationPose&&object.animationPose->gpuSkin;
     assert(ordinary.select(skin)==!skin&&ordinary.id()==old);
    }
    std::swap(renderer.scene,host.scene);
    std::vector<int> ids;for(const auto &object:renderer.scene.objects)ids.push_back(object.id);
    for(int eye=0;eye<2;eye++){
     ordinaryVertexEnabled=false;auto expected=renderer.renderSet(ids,{.17f,.1f,-1.2f},eye);
     ordinaryVertexEnabled=true;auto actual=renderer.renderSet(ids,{.17f,.1f,-1.2f},eye);
     assert(expected==actual);++views;
    }
    std::swap(renderer.scene,host.scene);
    js("discardMixed();interruptMixed()");assert(host.scene.objects.size()==6);
   }
  }
  closeScript(host);ordinary.reset();assert(!glIsProgram(old));currentScene=liveScene=nullptr;
 }
 assert(glGetError()==GL_NO_ERROR);
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS ordinary program routing: %zu exact mixed stereo/material/occlusion views, GPU/CPU skin, rigid/morph/static paths, restored bindings, program reset, 10 transactional reload/restore cycles\n",views);
}
