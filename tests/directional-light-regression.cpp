// Scene directional pixels, typed APIs, ownership and staged v10 persistence.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static uint64_t brightness(const std::vector<unsigned char> &image){uint64_t sum=0;for(size_t i=0;i<image.size();i+=4)sum+=image[i]+image[i+1]+image[i+2];return sum;}
static void nearPixels(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){assert(a.size()==b.size());for(size_t i=0;i<a.size();i++)assert(std::abs(int(a[i])-int(b[i]))<=(i%4==3?0:1));}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));size_t images=0;
 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=false;
  assert(loadModelFile("assets/samples/Box/Box.glb",asset));uploadModel(asset);asset.lighting.intensity=asset.lighting.ambient=0;
  for(auto &p:asset.primitives){p.factor[0]=p.factor[1]=p.factor[2]=1;p.metallic=0;p.roughness=.5;}
  Object object{};object.id=1;object.asset=int(r.scene.assets.size());object.scale=.45;r.scene.objects={object};
  PointLight sun;sun.id=1;sun.directional=true;sun.direction={0,0,1};sun.intensity=1;
  for(int eye=0;eye<2;eye++){
   r.scene.lights.clear();auto dark=r.renderSet({1},{0,0,-1.2f},eye);
   r.scene.lights={sun};auto lit=r.renderSet({1},{0,0,-1.2f},eye);assert(brightness(lit)>brightness(dark)+10000);
   asset.lighting.intensity=1;asset.lighting.direction[0]=asset.lighting.direction[1]=0;asset.lighting.direction[2]=1;r.scene.lights.clear();nearPixels(lit,r.renderSet({1},{0,0,-1.2f},eye));asset.lighting.intensity=0;
   r.scene.lights={sun};r.scene.lights[0].direction={0,0,-1};assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=sun;r.scene.lights[0].direction={0,0,8};assert(lit==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0].enabled=false;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=sun;r.scene.lights[0].intensity=0;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   Object handle=object;handle.id=2;handle.visible=false;r.scene.objects.push_back(handle);r.scene.lights[0]=sun;r.scene.lights[0].objectId=2;
   r.scene.objects[1].x=1000;r.scene.objects[1].y=-1000;r.scene.objects[1].scale=10;assert(lit==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.objects[1].yaw=float(std::acos(-1));assert(dark==r.renderSet({1},{0,0,-1.2f},eye));r.scene.objects.pop_back();
   for(auto &p:asset.primitives)p.materialUnlit=true;r.scene.lights={sun};auto unlit=r.renderSet({1},{0,0,-1.2f},eye);r.scene.lights.clear();assert(unlit==r.renderSet({1},{0,0,-1.2f},eye));for(auto &p:asset.primitives)p.materialUnlit=false;
   // Adding lighting cannot change the external-depth cutout on an unlit target.
   Object target{};target.id=2;target.asset=3;target.z=.3;target.scale=.8;r.scene.objects.push_back(target);auto depth=r.renderSet({2},{0,0,-1.2f},eye);r.scene.lights={sun};assert(depth==r.renderSet({2},{0,0,-1.2f},eye));r.scene.objects.pop_back();images+=9;
  }
  r.scene.lights.assign(4,sun);for(auto &l:r.scene.lights)l.position={10000,10000,10000};auto resolved=resolvePointLights(r.scene);std::vector<ObjectLightSet> sets;updateObjectLights(r.scene,resolved,sets);assert(sets[0].count==4);
  // Normalization and attachment rotations use the same axes as spotlights.
  r.scene.lights={sun};r.scene.lights[0].objectId=1;r.scene.objects[0].yaw=float(std::acos(-1)/2);resolved=resolvePointLights(r.scene);assert(std::abs(resolved[0].direction[0]+1)<1e-5&&std::abs(resolved[0].direction[2])<1e-5);
 }
 {
  Renderer r(false,modelVertexShader,1024);r.ordinaryRouting=false;r.scene.objects.clear();
  for(const char *name:{"ToyCar","NormalTangentTest","AlphaBlendModeTest","Fox","AnimatedMorphCube"}){
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",a));uploadModel(a);
   Object o{};o.id=1;o.asset=int(r.scene.assets.size());o.scale=.4;o.pitch=.2;o.yaw=.3;r.scene.objects={o};
   if(a.hasAnimation){auto &object=r.scene.objects[0];object.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=0;state.time=.4;evaluateModelPose(a,state,*object.animationPose);uploadModelPose(*object.animationPose);}
   std::vector<std::array<float,3>> sheens;for(const auto &part:a.primitives)sheens.push_back({part.sheen[0],part.sheen[1],part.sheen[2]});
   r.directionalVariants.clear();
   for(int count:{0,1,4,8})for(bool spot:{false,true})for(int eye=0;eye<2;eye++){
    r.scene.lights.clear();for(int i=0;i<count;i++){PointLight l;l.id=i+1;l.position={float(i%2)*.2f,.15f,-.5f};l.range=2;l.intensity=.1;l.spot=spot;l.direction={0,0,1};l.innerConeAngle=.2;l.outerConeAngle=.5;r.scene.lights.push_back(l);}
    // The frozen loop predates physical sheen. Compare the unchanged core
    // light equations without that layer, then restore sheen for mixed/variant checks.
    for(auto &part:a.primitives)std::fill_n(part.sheen,3,0.f);
    r.referenceLightLoop=true;auto reference=r.renderSet({1},{.1f,.1f,-1.2f},eye);r.referenceLightLoop=false;assert(reference==r.renderSet({1},{.1f,.1f,-1.2f},eye));images++;
    for(size_t p=0;p<a.primitives.size();p++)std::copy(sheens[p].begin(),sheens[p].end(),a.primitives[p].sheen);
    if(count){r.scene.lights.back().spot=false;r.scene.lights.back().directional=true;auto mixed=r.renderSet({1},{.1f,.1f,-1.2f},eye);
     if(count>=4){if(r.directionalVariants.programs.empty())r.directionalVariants.prepare(r.scene);r.preparedDirectional=true;nearPixels(mixed,r.renderSet({1},{.1f,.1f,-1.2f},eye));r.preparedDirectional=false;images++;}
     r.scene.lights.back().enabled=false;auto without=r.renderSet({1},{.1f,.1f,-1.2f},eye);for(size_t p=3;p<mixed.size();p+=4)assert(mixed[p]==without[p]);images++;}
   }
  }
  assert(r.directionalVariants.readyDraws>0);
 }
 {
  const auto dir=std::filesystem::temp_directory_path()/("ps-directional-"+std::to_string(getpid()));std::filesystem::create_directory(dir);const auto script=dir/"test.js";ScopeExit cleanup{[&]{std::filesystem::remove_all(dir);}};scenePersistence::directory=dir.string();
  std::ofstream(script)<<R"JS(
   function check(v){if(!v)throw Error('directional assertion');}
   function fails(fn){let caught=false;try{fn()}catch(e){caught=true}check(caught);}
   const owner=createSceneContainer(),asset=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
   const object=createObject(asset,0,0,0,.2,{container:owner});
   const sun=createDirectionalLight({container:owner,objectId:object,direction:[1,-2,3],color:[1,.5,.2],intensity:2});
   const point=createPointLight({container:owner}),spot=createSpotLight({container:owner});
   const initial=JSON.stringify(getDirectionalLight(sun));check(getDirectionalLight(sun).type==='directional'&&!('range' in getDirectionalLight(sun))&&!('position' in getDirectionalLight(sun)));
   for(const call of [()=>getPointLight(sun),()=>setSpotLight(sun,{}),()=>removePointLight(sun),()=>getDirectionalLight(point),()=>setDirectionalLight(spot,{}),()=>removeDirectionalLight(point)])fails(call);
   for(const options of [{direction:[0,0,0]},{direction:[1,2]},{direction:[0,Infinity,1]},{direction:[65,0,0]},{direction:null},{intensity:-1},{intensity:65},{color:[-1,0,0]},{range:1},{position:[0,0,0]},{innerConeAngle:0},{outerConeAngle:1},{enabled:1}]){fails(()=>setDirectionalLight(sun,options));check(JSON.stringify(getDirectionalLight(sun))===initial);}
   fails(()=>setDirectionalLight(sun,{get direction(){throw Error('expected getter')}}));check(JSON.stringify(getDirectionalLight(sun))===initial);
   let reads=0;setDirectionalLight(sun,{get position(){reads++;return undefined}});check(reads===1&&JSON.stringify(getDirectionalLight(sun))===initial);
   const detached=getDirectionalLight(sun);detached.direction[0]=33;check(JSON.stringify(getDirectionalLight(sun))===initial);
   const outside=createObject(asset,0,0,0,.1),outgoing=createDirectionalLight({container:owner,objectId:outside});fails(()=>captureSceneContainer(owner));removeDirectionalLight(outgoing);removeObject(outside);
   const incoming=createDirectionalLight({objectId:object});fails(()=>captureSceneContainer(owner));removeDirectionalLight(incoming);
   const snapshot=captureSceneContainer(owner);check(snapshot.version===12&&snapshot.lights.length===3);
   saveSceneContainer(owner,'directional');const disk=loadSceneContainer('directional');check(getDirectionalLight(disk.lightMap[sun]).objectId===disk.objectMap[object]);disposeSceneContainer(disk.containerId);
   const beforeInvalid=createDirectionalLight();removeDirectionalLight(beforeInvalid);
   for(const mutate of [s=>s.lights[0].direction=[0,0,0],s=>delete s.lights[0].direction,s=>s.lights[0].range=1,s=>s.lights[0].position=[0,0,0],s=>s.lights[0].type='area',s=>s.version=9]){
    const bad=JSON.parse(JSON.stringify(snapshot));mutate(bad);fails(()=>restoreSceneContainer(bad));check(JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snapshot));
   }
   const afterInvalid=createDirectionalLight();check(afterInvalid===beforeInvalid+1);removeDirectionalLight(afterInvalid);
   const legacy=JSON.parse(JSON.stringify(snapshot));legacy.version=9;legacy.lights=legacy.lights.filter(l=>l.type!=='directional');const restored=restoreSceneContainer(legacy);check(getSpotLight(restored.lightMap[spot]).type==='spot');disposeSceneContainer(restored.containerId);
   const extras=Array.from({length:5},()=>createDirectionalLight({enabled:false}));fails(()=>createDirectionalLight());fails(()=>createPointLight());fails(()=>createSpotLight());extras.forEach(removeDirectionalLight);
   const removed=createDirectionalLight();fails(()=>setDirectionalLight(removed,{get direction(){removeDirectionalLight(removed);return [0,0,1]}}));
   const doomed=createSceneContainer(),gone=createDirectionalLight({container:doomed});fails(()=>setDirectionalLight(gone,{get intensity(){disposeSceneContainer(doomed);return 2}}));
   setDirectionalLight(sun,{objectId:null,enabled:false,direction:[0,-1,0]});check(getDirectionalLight(sun).objectId===null&&!getDirectionalLight(sun).enabled);
   setDirectionalLight(sun,{objectId:object});removeObject(object);fails(()=>getDirectionalLight(sun));disposeSceneContainer(owner);fails(()=>getPointLight(point));fails(()=>getSpotLight(spot));
   createDirectionalLight({direction:[0,-1,1]});
  )JS";
  ScriptHost host;assert(loadScript(host,script.string()));auto *active=host.ctx;const auto saved=host.scene.lights;
  std::ofstream(script)<<"createDirectionalLight();throw Error('EXPECTED directional reload rejection');";assert(!loadScript(host,script.string())&&host.ctx==active&&host.scene.lights.size()==saved.size()&&host.scene.lights[0].direction==saved[0].direction);closeScript(host);
 }
 if(argc>1&&std::string(argv[1])=="--profile"){
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));uploadModel(a);a.lighting.intensity=0;a.lighting.ambient=.04;
  std::vector<int> ids;for(int i=0;i<16;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=.2;o.x=(i%4-1.5f)*.22f;o.y=(i/4-1.5f)*.22f;r.scene.objects.push_back(o);ids.push_back(o.id);}
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);GLuint q=0;gen(1,&q);
  for(int kind=0;kind<3;kind++)for(int count:{0,1,4,8}){
   r.scene.lights.clear();for(int i=0;i<count;i++){PointLight l;l.id=i+1;l.spot=kind==1;l.directional=kind==2;l.position={float(i%2)*.2f,.15f,-.5f};l.range=2;l.intensity=l.directional?.2f:.1f;l.direction={0,0,1};l.innerConeAngle=.3;l.outerConeAngle=.7;r.scene.lights.push_back(l);}
   std::array<double,4> gpu{},wall{};const int stages=kind==2?2:4;
   for(int frame=0;frame<80;frame++){
    for(auto &o:r.scene.objects){o.yaw=.3f+frame*.01f;o.pitch=.2f;}
    for(int stage=0;stage<stages;stage++){
     r.referenceLightLoop=kind<2&&(bool(stage==0||stage==3)^(argc>2&&std::string(argv[2])=="--reverse"));auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,q);r.renderSet(ids,{0,.1f,-1.2f},frame%2,0,false,1,true);end(GL_TIME_ELAPSED_EXT);glFinish();GLuint64 ns=0;result(q,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=20){gpu[stage]+=double(ns)/1e6;wall[stage]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    }
   }
   for(int stage=0;stage<stages;stage++)printf("DIRECTIONAL_LIGHT_SAMPLE {\"kind\":%d,\"lights\":%d,\"stage\":%d,\"reference\":%s,\"frames\":60,\"eyePixels\":768,\"triangles\":1742976,\"draws\":48,\"gpuMs\":%.6f,\"wallMs\":%.6f}\n",kind,count,stage,kind<2&&(bool(stage==0||stage==3)^(argc>2&&std::string(argv[2])=="--reverse"))?"true":"false",gpu[stage]/60,wall[stage]/60);fflush(stdout);
  }
  destroy(1,&q);
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS directionals: %zu stereo/reference/mixed views, attachments, no distance filtering, typed API rollback, shared capacity, v10/v9 snapshots and cleanup\n",images);
}
