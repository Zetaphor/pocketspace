// Conservative sphere/cone selection, stereo pixels and a range-only comparison.
#define SF_LIGHT_WORK_EXPERIMENT
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static void populate(Renderer &r,const char *path,int count){
 r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=false;asset.optimizeVertexCache=true;assert(loadModelFile(path,asset));uploadModel(asset);asset.lighting.intensity=.3;asset.lighting.ambient=.04;
 for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=count==1?.6f:.2f;o.x=count==1?0:(i%4-1.5f)*.22f;o.y=count==1?0:(i/4-1.5f)*.22f;o.yaw=.2;o.pitch=.1;r.scene.objects.push_back(o);}
}
static void setLights(Renderer &r,int count,int kind){
 r.scene.lights.clear();for(int i=0;i<count;i++){
  PointLight l;l.id=i+1;l.position={float(i%4-1.5f)*.22f,.15f,-.5f};l.intensity=.15;l.range=2;l.color={1,.8f,.6f};l.spot=kind!=2||i%2;
  l.direction=kind==3?std::array<float,3>{0,0,-1}:std::array<float,3>{0,0,1};
  l.innerConeAngle=kind==0?1.2f:.15f;l.outerConeAngle=kind==0?1.4f:.25f;r.scene.lights.push_back(l);
 }
}
static size_t retained(const Renderer &r){const auto lights=resolvePointLights(r.scene);std::vector<ObjectLightSet> sets;updateObjectLights(r.scene,lights,sets,r.selectConeLights);size_t n=0;for(const auto &set:sets)n+=set.count;return n;}
static void equalPixels(Renderer &a,Renderer &b,const std::vector<int> &ids,physics::Vec3 head,int eye){
 auto x=a.renderSet(ids,head,eye),y=b.renderSet(ids,head,eye);assert(x.size()==y.size());for(size_t i=0;i<x.size();i++){if(i%4==3)assert(x[i]==y[i]);else assert(std::abs(int(x[i])-int(y[i]))<=1);}
}
static void sphereTests(){
 ResolvedPointLight l{{0,0,0},{1,1,1},1,8,{0,0,1},.98f,float(std::cos(.35))};
 assert(!sphereOutsideSpotlight(l,0,0,2,.1));assert(sphereOutsideSpotlight(l,2,0,0,.1));assert(sphereOutsideSpotlight(l,0,0,-2,.1));assert(!sphereOutsideSpotlight(l,0,0,0,.1));assert(!sphereOutsideSpotlight(l,0,0,-.05,.1));
 const double edge=.35+std::asin(.1/2);assert(!sphereOutsideSpotlight(l,2*std::sin(edge),0,2*std::cos(edge),.1002));assert(sphereOutsideSpotlight(l,2*std::sin(edge+.02),0,2*std::cos(edge+.02),.1002));
 auto wide=l;wide.outerCos=0;assert(!sphereOutsideSpotlight(wide,0,0,-2,.1));auto invalid=l;invalid.direction={NAN,0,1};assert(!sphereOutsideSpotlight(invalid,2,0,0,.1));
 // Deterministic samples on/in spheres: no rejected sphere may contain a lit point.
 uint32_t seed=0x12345678;auto random=[&]{seed=1664525*seed+1013904223;return double(seed)/double(UINT32_MAX);};size_t rejected=0,lit=0;
 for(int i=0;i<6000;i++){
  const double angle=.001+random()*1.55,theta=random()*6.28318530718,z=random()*2-1,s=std::sqrt(1-z*z);
  l.direction={float(s*std::cos(theta)),float(s*std::sin(theta)),float(z)};l.outerCos=float(std::cos(angle));
  const double x=random()*4-2,y=random()*4-2,c=random()*4-2,r=.001+random()*.4;
  const bool outside=sphereOutsideSpotlight(l,x,y,c,r+1e-4+1e-5*(r+2));rejected+=outside;
  for(int p=0;p<48;p++){
   const double t=random()*6.28318530718,v=random()*2-1,k=std::sqrt(1-v*v),length=r*std::cbrt(random());
   const double px=x+length*k*std::cos(t),py=y+length*k*std::sin(t),pz=c+length*v,d=std::sqrt(px*px+py*py+pz*pz);
   if(d<1e-8)continue;
   if((px*l.direction[0]+py*l.direction[1]+pz*l.direction[2])/d>l.outerCos+1e-6){lit++;assert(!outside);}
  }
 }
 assert(rejected>1000&&lit>1000);
}
int main(int argc,char **argv){
 sphereTests();EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));size_t images=0;
 {
  Scene scene;scene.assets.emplace_back();scene.assets[0].pbr=true;
  for(int c=0;c<3;c++){scene.assets[0].low[c]=-.01;scene.assets[0].high[c]=.01;}
  Object o{};o.id=1;o.asset=1;o.scale=1;o.x=.6;o.z=1;scene.objects.push_back(o);
  ResolvedPointLight l{{0,0,0},{1,1,1},1,8,{0,0,1},.98f,float(std::cos(.25))};std::vector<ResolvedPointLight> lights(4,l);std::vector<ObjectLightSet> sets;
  updateObjectLights(scene,lights,sets,false);assert(sets[0].count==4);updateObjectLights(scene,lights,sets,true);assert(sets[0].count==0);
  scene.objects[0].animationPose=std::make_shared<ModelPose>();for(int c=0;c<3;c++){scene.objects[0].animationPose->geometry.low[c]=-1;scene.objects[0].animationPose->geometry.high[c]=1;}
  updateObjectLights(scene,lights,sets,true);assert(sets[0].count==4);
 }
 {
  for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","tests/fixtures/pbr/GeneratedTangents.glb","tests/fixtures/pbr/SurfaceFrames.glb","assets/samples/AlphaBlendModeTest/AlphaBlendModeTest.glb"}){
   Renderer reference,candidate;reference.selectConeLights=false;candidate.selectConeLights=true;
   const bool detailed=std::string(path).find("ToyCar")!=std::string::npos;populate(reference,path,detailed?16:1);populate(candidate,path,detailed?16:1);std::vector<int> ids;for(const auto &o:reference.scene.objects)ids.push_back(o.id);
   for(int kind:{0,1,2,3})for(int count:{0,1,4,8}){
    setLights(reference,count,kind);setLights(candidate,count,kind);
    for(auto head:{physics::Vec3{0,0,-1.2f},physics::Vec3{.3f,.15f,-1.f}})for(int eye=0;eye<2;eye++){equalPixels(reference,candidate,ids,head,eye);images++;}
   }
   // A moving attached spotlight must follow rotations without clipping its illumination.
   if(!detailed){
    for(auto *r:{&reference,&candidate}){setLights(*r,4,1);r->scene.lights[0].objectId=1;r->scene.lights[0].position={0,0,-.6};r->scene.lights[0].direction={0,0,1};}
    for(float angle:{-.5f,0.f,.5f}){for(auto *r:{&reference,&candidate})r->scene.objects[0].yaw=angle;equalPixels(reference,candidate,ids,{0,0,-1.2f},0);images++;}
    for(auto *r:{&reference,&candidate})for(auto &p:r->scene.assets.back().primitives)p.materialUnlit=true;
    equalPixels(reference,candidate,ids,{0,0,-1.2f},0);images++;
   }
   if(reference.scene.assets.back().hasAnimation){
    for(float time:{0.f,.5f,1.f}){
     for(auto *r:{&reference,&candidate}){
      setLights(*r,4,1);for(auto &p:r->scene.assets.back().primitives)p.materialUnlit=false;
      auto &o=r->scene.objects[0];o.animation.clip=0;o.animation.time=time;o.animationPose=std::make_shared<ModelPose>();evaluateModelPose(r->scene.assets.back(),o.animation,*o.animationPose);uploadModelPose(*o.animationPose);
     }
     for(int eye=0;eye<2;eye++){equalPixels(reference,candidate,ids,{0,0,-1.2f},eye);images++;}
    }
   }
   if(detailed&&argc>1&&std::string(argv[1])=="--profile"){
    auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
    for(int kind:{0,1,2,3})for(int count:{0,1,4,8}){
     setLights(candidate,count,kind);candidate.selectConeLights=false;const auto before=retained(candidate);candidate.selectConeLights=true;const auto after=retained(candidate);
     std::array<double,4> wall{},gpu{};std::array<GLuint,4> queries{};gen(4,queries.data());
     for(int frame=0;frame<120;frame++)for(int stage=0;stage<4;stage++){
      candidate.selectConeLights=stage==1||stage==2;auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,queries[stage]);candidate.renderSet(ids,{0,0,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);glFinish();GLuint64 elapsed=0;result(queries[stage],GL_QUERY_RESULT_EXT,&elapsed);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint);
      if(frame>=30){wall[stage]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();gpu[stage]+=double(elapsed)/1e6;}
     }
     destroy(4,queries.data());for(int stage=0;stage<4;stage++)printf("CONE_WORK kind=%d lights=%d optimized=%d stage=%d gpu_ms=%.5f wall_ms=%.5f pairs_before=%zu pairs_after=%zu frames=90\n",kind,count,int(stage==1||stage==2),stage,gpu[stage]/90,wall[stage]/90,before,after);fflush(stdout);
    }
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS spotlight cones: %zu stereo/material/attachment images and 288000 sphere samples, alpha exact, RGB difference <=1 byte\n",images);
}
