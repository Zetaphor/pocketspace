// Bounded material/light-count PBR variants, strict pixels and matched timings.
#define SF_PBR_VARIANT_EXPERIMENT
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static void lights(Renderer &r,int count){
 r.scene.lights.clear();for(int i=0;i<count;i++){PointLight l;l.id=i+1;l.position={float(i%4-1.5f)*.4f,.3f,-.6f};l.color={1,.8f,.6f};l.intensity=.15;l.range=2;l.spot=i%2;l.direction={0,0,1};l.innerConeAngle=.3;l.outerConeAngle=.6;r.scene.lights.push_back(l);}
}
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t images=0,alphaChanged=0,rgbChanged=0;int maxRgb=0;
 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();
  for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","assets/samples/NormalTangentTest/NormalTangentTest.glb","assets/samples/NormalTangentMirrorTest/NormalTangentMirrorTest.glb","assets/samples/NegativeScaleTest/NegativeScaleTest.glb","assets/samples/EmissiveStrengthTest/EmissiveStrengthTest.glb","assets/samples/MetalRoughSpheresNoTextures/MetalRoughSpheresNoTextures.glb","tests/fixtures/pbr/SurfaceFrames.glb","assets/samples/AlphaBlendModeTest/AlphaBlendModeTest.glb"}){
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.optimizeVertexCache=true;assert(loadModelFile(path,a));uploadModel(a);a.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(a.environment);
   std::vector<bool> originalUnlit;for(const auto &part:a.primitives)originalUnlit.push_back(part.materialUnlit);
   Object o{};o.id=1;o.asset=int(r.scene.assets.size());o.scale=.6;auto other=o;other.id=2;other.x=.12;other.z=-.12;r.scene.objects={o,other};lights(r,4);r.scene.lights[1].objectId=2;
   for(int lightCount:{0,1,4,8})for(float angle:{0.f,.3f,2.1f,1000.f}){
    r.pbrVariants.clear();const auto priorFallbacks=r.pbrVariants.fallbackDraws;a.lighting.environmentIntensity=angle==.3f?1.f:0.f;
    for(bool unlit:{false,true})for(int level:{0,2})for(int eye=0;eye<2;eye++)for(auto members:{std::vector<int>{1},std::vector<int>{1,2}}){
    for(size_t i=0;i<a.primitives.size();i++)a.primitives[i].materialUnlit=unlit||originalUnlit[i];
    lights(r,lightCount);if(lightCount>1)r.scene.lights[1].objectId=2;
    for(auto &object:r.scene.objects){object.pitch=angle*.3f;object.yaw=angle;object.roll=-angle*.2f;object.lodLevel=level;
     if(a.hasAnimation){object.animation.clip=0;object.animation.time=.75;object.animationPose=std::make_shared<ModelPose>();evaluateModelPose(a,object.animation,*object.animationPose);uploadModelPose(*object.animationPose);}}
    r.specializePbr=false;auto reference=r.renderSet(members,{.15f,.1f,-1.2f},eye);
    r.specializePbr=true;auto candidate=r.renderSet(members,{.15f,.1f,-1.2f},eye);assert(reference.size()==candidate.size());
    for(size_t p=0;p<reference.size();p++){int delta=std::abs(int(reference[p])-int(candidate[p]));if(p%4==3)alphaChanged+=delta!=0;else{rgbChanged+=delta!=0;maxRgb=std::max(maxRgb,delta);}}images++;
   }
   assert(r.pbrVariants.fallbackDraws==priorFallbacks);
   }
  }
  r.pbrVariants.clear();
  auto &asset=r.scene.assets.back();auto &part=asset.primitives[0];const int originalAlpha=part.alphaMode;const bool originalSided=part.doubleSided;
  for(int alpha:{0,1})for(bool sided:{false,true})for(int count=0;count<=8;count++){part.alphaMode=alpha;part.doubleSided=sided;r.pbrVariants.get(asset,part,count);}
  part.alphaMode=originalAlpha;part.doubleSided=originalSided;
  assert(r.pbrVariants.programs.size()<=PbrVariantPrograms::limit&&r.pbrVariants.fallbackDraws>0);
  std::vector<GLuint> owned;for(const auto &[key,p]:r.pbrVariants.programs)owned.push_back(p->experimentId());
  r.pbrVariants.clear();for(auto id:owned)assert(!glIsProgram(id));
  r.pbrVariants.rejectNewPrograms=true;assert(!r.pbrVariants.get(asset,part,0));assert(!r.pbrVariants.get(asset,part,0));assert(r.pbrVariants.failed.size()==1&&r.pbrVariants.programs.empty());
  r.specializePbr=false;auto fallbackReference=r.renderSet({1,2},{.15f,.1f,-1.2f},0);
  r.specializePbr=true;assert(fallbackReference==r.renderSet({1,2},{.15f,.1f,-1.2f},0));
  r.pbrVariants.rejectNewPrograms=false;r.pbrVariants.clear();
 }
 printf("PBR_VARIANTS_PIXELS images=%zu alpha_changed=%zu rgb_changed=%zu max_rgb_delta=%d\n",images,alphaChanged,rgbChanged,maxRgb);fflush(stdout);
 // Preserve exact coverage/occlusion and the existing one-byte shading tolerance.
 assert(alphaChanged==0&&maxRgb<=1);
 {
  Renderer r(false,modelVertexShader,2048);r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));uploadModel(a);a.lighting.intensity=.3;a.lighting.ambient=.04;
  std::vector<int> ids;for(int i=0;i<32;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=.16;o.x=(i%8-3.5f)*.22f;o.y=(i/8-1.5f)*.22f;r.scene.objects.push_back(o);ids.push_back(o.id);}
  lights(r,8);r.pbrVariants.prepare(r.scene);r.prewarmedPbr=true;const auto preparedMs=r.pbrVariants.compileMs;
  const auto &part=a.primitives[0];assert(!r.pbrVariants.ready(a,part,8,768));assert(!r.pbrVariants.ready(a,part,0,2048));
  assert(r.pbrVariants.ready(a,part,8,2048));
  a.lighting.environmentIntensity=1;a.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(a.environment);assert(!r.pbrVariants.ready(a,part,8,2048));
  a.lighting.environmentIntensity=0;assert(r.pbrVariants.compileMs==preparedMs);
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);GLuint q=0;gen(1,&q);
  for(int count:{0,8})for(int level:{0,2})for(int size:{768,2048}){
   lights(r,count);r.samplePixels=size;std::array<double,4> gpu{},wall{};
   for(int frame=0;frame<120;frame++){
    for(auto &o:r.scene.objects){o.lodLevel=level;o.yaw=.2f+frame*.013f;o.pitch=.1f+std::sin(frame*.02f)*.15f;o.roll=.1;}
    for(int stage=0;stage<4;stage++){
     r.specializePbr=stage==1||stage==2;auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,q);r.renderSet(ids,{0,0,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);glFinish();GLuint64 elapsed=0;result(q,GL_QUERY_RESULT_EXT,&elapsed);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=30){gpu[stage]+=double(elapsed)/1e6;wall[stage]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    }
   }
   for(int stage=0;stage<4;stage++)printf("PBR_VARIANTS {\"lights\":%d,\"level\":%d,\"pixels\":%d,\"stage\":%d,\"prepared\":%s,\"gpuMs\":%.6f,\"wallMs\":%.6f,\"frames\":90}\n",count,level,size,stage,stage==1||stage==2?"true":"false",gpu[stage]/90,wall[stage]/90);fflush(stdout);
  }
  assert(r.pbrVariants.compileMs==preparedMs);
  printf("PBR_VARIANTS_CACHE programs=%zu binary_bytes=%llu compile_ms=%.4f fallback_draws=%zu\n",r.pbrVariants.programs.size(),(unsigned long long)r.pbrVariants.binaryBytes(),r.pbrVariants.compileMs,r.pbrVariants.fallbackDraws);
  destroy(1,&q);
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 puts("PASS PBR variants pixels and matched ABBA profiling");
}
