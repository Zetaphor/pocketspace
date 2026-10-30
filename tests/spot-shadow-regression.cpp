// Native light-space depth, alpha silhouettes, animation and budget/GL lifetimes.
#define SF_PBR_VARIANT_EXPERIMENT
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static size_t darker(const std::vector<unsigned char> &lit,const std::vector<unsigned char> &shadow){size_t count=0;assert(lit.size()==shadow.size());for(size_t i=0;i<lit.size();i+=4){if(lit[i+3]!=shadow[i+3]){fprintf(stderr,"alpha mismatch pixel=%zu lit=%d shadow=%d error=%x status=%s\n",i/4,int(lit[i+3]),int(shadow[i+3]),glGetError(),spotShadowDiagnostics.status);assert(false);}if(lit[i+3]&&int(lit[i])+lit[i+1]+lit[i+2]>int(shadow[i])+shadow[i+1]+shadow[i+2]+12)++count;}return count;}
int main(int argc,char **argv){
 const std::string mode=argc>1?argv[1]:"";
 const bool directional=mode=="--directional"||mode=="--directional-benchmark";

 {SpotShadowGpuBudget budget;
  assert(!budget.add(4,2));
  for(unsigned i=1;i<SpotShadowGpuBudget::window*3;i++)assert(!budget.add(.02,2));
  budget.reset();
  for(unsigned i=1;i<SpotShadowGpuBudget::window;i++)assert(!budget.add(3,2));
  assert(budget.add(3,2));budget.reset();assert(!budget.add(.02,2));
 }

 const auto dir=std::filesystem::temp_directory_path()/("pocketspace-shadow-"+std::to_string(getpid()));std::filesystem::create_directory(dir);scenePersistence::directory=dir.string();
 {ScriptHost host;assert(loadScript(host,"tests/spot-shadow-api-smoke.js"));assert(host.scene.lights.size()==1&&host.scene.lights[0].shadow.enabled);closeScript(host);currentScene=liveScene=nullptr;}
 {ScriptHost host;assert(loadScript(host,"tests/directional-shadow-api-smoke.js"));assert(host.scene.lights.size()==1&&host.scene.lights[0].directional&&host.scene.lights[0].shadow.enabled);closeScript(host);currentScene=liveScene=nullptr;}
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();for(auto &a:r.scene.assets)deleteModel(a);r.scene.assets.clear();
  for(int i=0;i<2;i++){
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=i==0;
   assert(loadModelFile("tests/fixtures/pbr/MissingNormals.glb",a));uploadModel(a);
   a.lighting.intensity=0;a.lighting.ambient=.03;for(auto &p:a.primitives){p.metallic=0;p.roughness=.8f;p.factor[0]=p.factor[1]=p.factor[2]=.7f;}
  }
  Object receiver{},caster{};receiver.id=1;receiver.asset=1;receiver.scale=1.5;
  caster.id=2;caster.asset=2;caster.scale=.25f;caster.z=-.3f;r.scene.objects={receiver,caster};
  PointLight light;light.id=1;light.spot=true;light.position={-.4f,.5f,-1.f};light.direction={.4f,-.5f,1.f};light.range=3;light.intensity=2;light.innerConeAngle=.6;light.outerConeAngle=.9;light.shadow.resolution=256;light.shadow.maxGpuMs=16;if(directional){light.spot=false;light.directional=true;light.shadow.size=2;light.shadow.depth=4;}r.scene.lights={light};
  auto baseline=r.renderSet({1},{0,0,-1.2f},0);assert(!r.spotShadows.texture);
  r.scene.lights[0].shadow.enabled=true;auto shaded=r.renderSet({1},{0,0,-1.2f},0);assert(r.spotShadows.active);const size_t solid=darker(baseline,shaded);printf("SHADOW opaque pixels=%zu\n",solid);assert(solid>100);
  for(int eye=0;eye<2;eye++){printf("STEREO eye=%d\n",eye);fflush(stdout);
   r.scene.lights[0].shadow.enabled=false;auto lit=r.renderSet({1},{.1f,.1f,-1.2f},eye);r.scene.lights[0].shadow.enabled=true;assert(darker(lit,r.renderSet({1},{.1f,.1f,-1.2f},eye))>100);
  }
  auto reference=[&]{r.scene.lights[0].shadow.enabled=false;auto image=r.renderSet({1},{0,0,-1.2f},0);r.scene.lights[0].shadow.enabled=true;return image;};
  // Default bias must keep the bare sloped receiver clean at every supported
  // resolution, while an actual caster must still produce a visible shadow.
  for(int resolution:{256,512,1024}){
   r.scene.lights[0].shadow.resolution=resolution;
   r.scene.objects[1].visible=false;baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))==0);
   r.scene.objects[1].visible=true;baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))>100);
  }
  r.scene.lights[0].shadow.resolution=256;
  auto &part=r.scene.assets[1].primitives[0];part.alphaMode=2;baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))==0);
  part.alphaMode=1;part.factor[3]=0;baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))==0);part.factor[3]=1;
  unsigned char checker[]={255,255,255,255,255,255,255,0,255,255,255,0,255,255,255,255};glBindTexture(GL_TEXTURE_2D,part.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,checker);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  baseline=reference();const size_t holes=darker(baseline,r.renderSet({1},{0,0,-1.2f},0));printf("SHADOW cutout pixels=%zu\n",holes);assert(holes>10&&holes<solid);
  part.alphaMode=0;r.scene.objects[1].x=1.5;baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))==0);r.scene.objects[1].x=0;
  // Surface grouping does not limit the set of shadow casters.
  r.scene.objects[0].renderGroup=9;r.scene.objects[1].renderGroup=10;baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))>100);
  // Filtered light sets and prepared shader routing retain the caster identity.
  for(int i=0;i<3;i++){auto extra=light;extra.id=i+2;extra.shadow.enabled=false;extra.intensity=.1f;r.scene.lights.push_back(extra);}
  r.specializePbr=false;auto general=r.renderSet({1},{0,0,-1.2f},0);r.specializePbr=true;auto specialized=r.renderSet({1},{0,0,-1.2f},0);assert(darker(general,specialized)==0&&darker(specialized,general)==0);r.specializePbr=false;r.scene.lights.resize(1);
  if(directional){
   // World-space coverage remains fixed under attachment translation. Direction
   // rotates with the attachment, while moving the center moves only coverage.
   auto &sun=r.scene.lights[0];sun.objectId=2;r.spotShadows.update(r.scene);const auto fixed=r.spotShadows.matrix;
   r.scene.objects[1].x=.2f;r.spotShadows.update(r.scene);assert(r.spotShadows.matrix==fixed);
   r.scene.objects[1].yaw=.3f;r.spotShadows.update(r.scene);assert(r.spotShadows.matrix!=fixed);
   r.scene.objects[1].x=0;r.scene.objects[1].yaw=0;sun.objectId=0;
   sun.shadow.center={10,0,0};baseline=reference();assert(darker(baseline,r.renderSet({1},{0,0,-1.2f},0))==0);
   sun.shadow.center={0,0,0};
   for(auto direction:{std::array<float,3>{0,-1,0},std::array<float,3>{0,1,0}}){sun.direction=direction;r.spotShadows.update(r.scene);for(float v:r.spotShadows.matrix)assert(std::isfinite(v));}
   sun.direction=light.direction;
  }
  // Animated casters use the current pose. Compare GPU skinning to independent
  // CPU deformation on the same receiver; permit only sparse raster edge changes.
  for(const char *name:{"Fox","AnimatedMorphCube","InterpolationTest"}){
   r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();
   assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));uploadModel(asset);
   r.scene.objects[1].asset=int(r.scene.assets.size());r.scene.objects[1].scale=.4f;
   // Isolate the shadow image from the caster's separate overlay depth mask.
   auto poseShadow=[&]{r.spotShadows.update(r.scene);assert(r.spotShadows.active);r.updateShadows=false;r.scene.objects[1].visible=false;auto image=r.renderSet({1},{0,0,-1.2f},0);r.scene.objects[1].visible=true;r.updateShadows=true;return image;};
   std::vector<unsigned char> previous;
   for(double time:{0.,.6}){
    animation::Playback state;state.clip=0;state.time=time;
    auto cpu=std::make_shared<ModelPose>();gpuSkinningEnabled=false;evaluateModelPose(asset,state,*cpu);uploadModelPose(*cpu);r.scene.objects[1].animationPose=cpu;
    auto expected=poseShadow();
    auto gpu=std::make_shared<ModelPose>();gpuSkinningEnabled=true;evaluateModelPose(asset,state,*gpu);uploadModelPose(*gpu);r.scene.objects[1].animationPose=gpu;
    if(std::string(name)=="Fox")assert(gpu->gpuSkin&&!cpu->gpuSkin);
    auto actual=poseShadow();size_t changed=0;for(size_t i=0;i<actual.size();i+=4){int difference=0;for(int c=0;c<4;c++)difference=std::max(difference,std::abs(int(actual[i+c])-int(expected[i+c])));changed+=difference>2;}
    assert(changed<size_t(pixels)*pixels/200);if(!previous.empty())assert(actual!=previous);previous=actual;
   }
  }
  r.scene.objects[1].animationPose.reset();r.scene.objects[1].asset=2;r.scene.objects[1].scale=.25f;
  // Restore caller GL state, using a private VAO for all caster attributes.
  GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(7,11,31,29);glEnable(GL_SCISSOR_TEST);glEnable(GL_BLEND);glEnable(GL_CULL_FACE);glCullFace(GL_FRONT);glFrontFace(GL_CW);glDepthMask(GL_FALSE);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);glClearDepthf(.3f);glActiveTexture(GL_TEXTURE4);
  r.spotShadows.update(r.scene);GLint saved=0,viewport[4]{};GLboolean mask[4]{};glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&saved);assert(GLuint(saved)==vao);glGetIntegerv(GL_ACTIVE_TEXTURE,&saved);assert(saved==GL_TEXTURE4);glGetIntegerv(GL_VIEWPORT,viewport);assert(viewport[0]==7&&viewport[3]==29);glGetBooleanv(GL_COLOR_WRITEMASK,mask);assert(!mask[0]&&mask[1]&&!mask[2]&&mask[3]);assert(glIsEnabled(GL_SCISSOR_TEST)&&glIsEnabled(GL_BLEND)&&glIsEnabled(GL_CULL_FACE));
  glBindVertexArray(0);glDeleteVertexArrays(1,&vao);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearDepthf(1);glActiveTexture(GL_TEXTURE0);
  // Work exhaustion never samples the last valid map.
  r.scene.lights[0].shadow.maxTriangles=1;r.spotShadows.update(r.scene);assert(!r.spotShadows.active&&std::string(spotShadowDiagnostics.status)=="geometry-budget");r.scene.lights[0].shadow.maxTriangles=1000000;
  for(int size:{256,512,1024}){r.scene.lights[0].shadow.resolution=size;r.spotShadows.update(r.scene);assert(r.spotShadows.active&&spotShadowDiagnostics.bytes==uint64_t(size)*size*4);}
  r.scene.lights[0].shadow.resolution=256;r.scene.lights[0].shadow.maxGpuMs=.001f;
  for(int i=0;i<96&&!std::string(spotShadowDiagnostics.status).starts_with("gpu-budget");i++){r.spotShadows.update(r.scene);glFinish();}
  assert(!r.spotShadows.active&&std::string(spotShadowDiagnostics.status)=="gpu-budget");
  r.scene.lights[0].shadow.maxGpuMs=16;r.spotShadows.update(r.scene);assert(r.spotShadows.active);
  GLuint texture=r.spotShadows.texture;r.scene.lights.clear();r.spotShadows.update(r.scene);assert(!r.spotShadows.active&&!r.spotShadows.texture&&!glIsTexture(texture)&&spotShadowDiagnostics.bytes==0);
 }
 glUseProgram(0);
 {
  const auto file=dir/"reload.js";
  const std::string valid="const a=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});createObject(a,0,0,0,.2);createSpotLight({position:[0,0,-1],shadow:{enabled:true,maxGpuMs:16}});";
  std::ofstream(file)<<valid;ScriptHost host;assert(loadScript(host,file.string()));for(auto &a:host.scene.assets)uploadModel(a);SpotShadows map;map.update(host.scene);assert(map.active);const GLuint previous=map.texture;auto *contextBefore=host.ctx;
  std::ofstream(file)<<valid<<"createSpotLight({shadow:{enabled:true}});";
  bool committed=false;assert(!loadScript(host,file.string(),[&]{committed=true;map.reset();}));assert(!committed&&host.ctx==contextBefore&&map.active&&map.texture==previous&&glIsTexture(previous));
  std::ofstream(file)<<valid;assert(loadScript(host,file.string(),[&]{committed=true;map.reset();}));assert(committed&&!map.active&&!map.texture&&!glIsTexture(previous));for(auto &a:host.scene.assets)uploadModel(a);map.update(host.scene);assert(map.active);
  closeScript(host);currentScene=liveScene=nullptr;map.update(host.scene);assert(!map.texture);
 }
 if(mode=="--benchmark"||mode=="--directional-benchmark"){
  Renderer r;r.ordinaryRouting=false;r.updateShadows=false;r.scene.objects.clear();
  r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.optimizeVertexCache=true;
  assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);asset.lighting.intensity=0;asset.lighting.ambient=.02;
  PointLight light;light.id=1;light.spot=true;light.position={-.4f,.7f,-1.f};light.direction={.4f,-.7f,1.f};light.range=3;light.intensity=2;light.innerConeAngle=.5;light.outerConeAngle=1;light.shadow.maxGpuMs=16;light.shadow.maxTriangles=1000000;if(directional){light.spot=false;light.directional=true;light.shadow.size=2;light.shadow.depth=4;}
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  for(int count:{1,4})for(int resolution:{256,512,1024}){
   r.scene.objects.clear();std::vector<int> members;
   for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=count==1?.6f:.3f;o.x=count==1?0:(i%2-.5f)*.5f;o.y=count==1?0:(i/2-.5f)*.5f;o.yaw=.3f;r.scene.objects.push_back(o);members.push_back(o.id);}
   light.shadow.resolution=resolution;r.scene.lights={light};
   for(int stage=0;stage<4;stage++){
    r.scene.lights[0].shadow.enabled=stage==1||stage==2;
    GLuint query=0;gen(1,&query);double gpu=0,shadow=0,cpu=0,wall=0;
    for(int frame=0;frame<48;frame++){
     auto start=std::chrono::steady_clock::now();r.spotShadows.update(r.scene);begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{.1f,.1f,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);auto queued=std::chrono::steady_clock::now();glFinish();auto finished=std::chrono::steady_clock::now();
     GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);assert(!r.scene.lights[0].shadow.enabled||r.spotShadows.active);
     if(frame>=12){gpu+=double(ns)/1e6;shadow+=spotShadowDiagnostics.gpuMs.value_or(0);cpu+=std::chrono::duration<double,std::milli>(queued-start).count();wall+=std::chrono::duration<double,std::milli>(finished-start).count();}
    }
    destroy(1,&query);printf("SHADOW_SAMPLE directional=%d objects=%d resolution=%d stage=%d enabled=%d eye_pixels=768 frames=36 shadow_gpu_ms=%.6f color_gpu_ms=%.6f enqueue_ms=%.6f wall_ms=%.6f map_bytes=%llu\n",int(directional),count,resolution,stage,int(r.scene.lights[0].shadow.enabled),shadow/36,gpu/36,cpu/36,wall/36,(unsigned long long)spotShadowDiagnostics.bytes);fflush(stdout);
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);std::filesystem::remove_all(dir);
 puts(directional?"PASS directional shadows: orthographic coverage, attachment basis, stereo, alpha, animation, budgets and cleanup":"PASS spotlight shadows: API rollback, capacity/snapshots, stereo, alpha cutouts, groups, shader variants, GL state, work/time budgets and cleanup");
}
