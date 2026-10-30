// Compare per-object light range selection; retain a rejected shader-work diagnostic.
#define SF_LIGHT_WORK_EXPERIMENT
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static void populate(Renderer &r,const char *path,int count){
 r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=false;asset.optimizeVertexCache=true;assert(loadModelFile(path,asset));uploadModel(asset);
 asset.lighting.intensity=.3;asset.lighting.ambient=.04;
 for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=count==1?.6f:.2f;o.x=count==1?0:(i%4-1.5f)*.22f;o.y=count==1?0:(i/4-1.5f)*.22f;o.yaw=.2f;o.pitch=.1f;r.scene.objects.push_back(o);}
}
static bool localRange=false;
static void lights(Renderer &r,int count,int kind){
 r.scene.lights.clear();for(int i=0;i<count;i++){
  PointLight l;l.id=i+1;l.position={float(i%2)*.2f,.15f,i%2?.5f:-.5f};l.intensity=.15;l.range=2;l.color={1,.8f,.6f};
  if(localRange){l.position={float(i%4-1.5f)*.22f,float(i/4-.5f)*.4f,-.12f};l.range=.18f;}
  l.spot=kind==1||(kind==2&&i%2);l.direction={0,0,i%2?-1.f:1.f};l.innerConeAngle=.2;l.outerConeAngle=.35;
  r.scene.lights.push_back(l);
 }
}
static void equalPixels(Renderer &a,Renderer &b,const std::vector<int> &ids,physics::Vec3 head,int eye){
 auto reference=a.renderSet(ids,head,eye),candidate=b.renderSet(ids,head,eye);assert(reference.size()==candidate.size());
 int delta=0;for(size_t i=0;i<reference.size();i++){if(i%4==3)assert(reference[i]==candidate[i]);delta=std::max(delta,std::abs(int(reference[i])-int(candidate[i])));}assert(delta<=1);
}
int main(int argc,char **argv){
 const bool shader=argc>1&&std::string(argv[1])=="--profile-shader";
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));size_t images=0;
 {
  Object o{};o.scale=1;ModelAsset geometry;for(int c=0;c<3;c++){geometry.low[c]=-.5;geometry.high[c]=.5;}
  ResolvedPointLight light{{1.0660254f,0,0},{1,1,1},1,.2f};std::vector<ResolvedPointLight> lights{light};
  assert(selectObjectLights(o,geometry,lights).count==1);lights[0].position[0]=1.1;assert(selectObjectLights(o,geometry,lights).count==0);
  o.pitch=.7;o.yaw=1.1;assert(selectObjectLights(o,geometry,lights).count==0);
  geometry.high[1]=NAN;assert(selectObjectLights(o,geometry,lights).count==1);
  Scene scene;scene.assets.emplace_back();scene.assets[0].pbr=true;for(int c=0;c<3;c++){scene.assets[0].low[c]=-.01;scene.assets[0].high[c]=.01;}
  o.id=1;o.asset=1;scene.objects.push_back(o);std::vector<ObjectLightSet> sets;updateObjectLights(scene,lights,sets);assert(sets[0].count==1);
  lights.resize(4,lights[0]);updateObjectLights(scene,lights,sets);assert(sets[0].count==0);
  scene.objects[0].animationPose=std::make_shared<ModelPose>();for(int c=0;c<3;c++){scene.objects[0].animationPose->geometry.low[c]=-2;scene.objects[0].animationPose->geometry.high[c]=2;}
  updateObjectLights(scene,lights,sets);assert(sets[0].count==4);
 }

 {
  for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","tests/fixtures/pbr/GeneratedTangents.glb","tests/fixtures/pbr/SurfaceFrames.glb","assets/samples/AlphaBlendModeTest/AlphaBlendModeTest.glb"}){
   Renderer reference,candidate;reference.optimizeLightWork=false;candidate.optimizeLightWork=shader;reference.selectLocalLights=false;candidate.selectLocalLights=!shader;reference.selectConeLights=candidate.selectConeLights=false;
   const bool detailed=std::string(path).find("ToyCar")!=std::string::npos;populate(reference,path,detailed?16:1);populate(candidate,path,detailed?16:1);
   std::vector<int> ids;for(const auto &o:reference.scene.objects)ids.push_back(o.id);
   for(bool local:{false,true}){if(shader&&local)continue;localRange=local;
   for(int kind:{0,1,2})for(int count:{0,1,4,8}){
    lights(reference,count,kind);lights(candidate,count,kind);
    for(auto head:{physics::Vec3{0,0,-1.2f},physics::Vec3{.3f,.15f,-1.f}})for(int eye=0;eye<2;eye++){equalPixels(reference,candidate,ids,head,eye);images++;}
    // Exercise actual opaque/masked/blended and unlit material branches too.
    if(!detailed){
     for(int mode:{1,2}){
      for(auto *r:{&reference,&candidate})for(auto &p:r->scene.assets.back().primitives){p.alphaMode=mode;p.factor[3]=.7;}
      equalPixels(reference,candidate,ids,{0,0,-1.2f},0);images++;
     }
     for(auto *r:{&reference,&candidate})for(auto &p:r->scene.assets.back().primitives){p.alphaMode=0;p.factor[3]=1;p.materialUnlit=true;}
     equalPixels(reference,candidate,ids,{0,0,-1.2f},0);images++;
     for(auto *r:{&reference,&candidate})for(auto &p:r->scene.assets.back().primitives)p.materialUnlit=false;
    }
   }
   }
   if(detailed&&argc>1&&(std::string(argv[1])=="--profile"||shader)){
    auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
    for(bool local:{false,true}){if(shader&&local)continue;localRange=local;
    for(int kind:{0,1,2})for(int count:{0,1,4,8}){
     lights(reference,count,kind);lights(candidate,count,kind);candidate.selectLocalLights=!shader;equalPixels(reference,candidate,ids,{0,0,-1.2f},0);
     std::array<double,4> wall{},gpu{};std::array<GLuint,4> queries{};gen(4,queries.data());
     // Interleave ABBA within each measured frame to reduce clock/power drift.
     for(int frame=0;frame<120;frame++)for(int stage=0;stage<4;stage++){
      const bool optimized=stage==1||stage==2;auto &r=shader?(optimized?candidate:reference):candidate;
      r.selectLocalLights=!shader&&optimized;
      auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,queries[stage]);r.renderSet(ids,{0,0,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);glFinish();GLuint64 elapsed=0;result(queries[stage],GL_QUERY_RESULT_EXT,&elapsed);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint);
      if(frame>=30){wall[stage]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();gpu[stage]+=double(elapsed)/1e6;}
     }
     destroy(4,queries.data());
     for(int stage=0;stage<4;stage++)printf("LIGHT_WORK local=%d kind=%d lights=%d optimized=%d stage=%d gpu_ms=%.5f wall_ms=%.5f frames=90\n",int(local),kind,count,int(stage==1||stage==2),stage,gpu[stage]/90,wall[stage]/90);fflush(stdout);
    }
    }
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS light work: %zu matched stereo/material images, alpha exact and RGB difference <=1 byte\n",images);
}
