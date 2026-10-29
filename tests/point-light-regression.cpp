// Real GLES point-light pixels, script mutation/cleanup and transactional snapshots.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static uint64_t brightness(const std::vector<unsigned char> &image){uint64_t sum=0;for(size_t i=0;i<image.size();i+=4)sum+=image[i]+image[i+1]+image[i+2];return sum;}
static void sameAlpha(const std::vector<unsigned char> &a,const std::vector<unsigned char> &b){assert(a.size()==b.size());for(size_t i=3;i<a.size();i+=4)assert(a[i]==b[i]);}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=false;
  assert(loadModelFile("assets/samples/Box/Box.glb",asset));asset.lighting.intensity=asset.lighting.ambient=0;uploadModel(asset);
  for(auto &p:asset.primitives){p.factor[0]=p.factor[1]=p.factor[2]=1;p.metallic=0;p.roughness=.5;}
  Object object{};object.id=1;object.asset=int(r.scene.assets.size());object.scale=.45;r.scene.objects={object};
  PointLight light;light.id=1;light.position={0,.15f,-.5f};light.range=2;light.intensity=.15f;
  for(int eye=0;eye<2;eye++){
   r.scene.lights.clear();auto dark=r.renderSet({1},{0,0,-1.2f},eye);
   r.scene.lights={light};auto lit=r.renderSet({1},{0,0,-1.2f},eye);assert(brightness(lit)>brightness(dark)+10000);sameAlpha(dark,lit);
   r.scene.lights[0].intensity=.3;auto brighter=r.renderSet({1},{0,0,-1.2f},eye);assert(brightness(brighter)>brightness(lit));
   r.scene.lights[0].enabled=false;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=light;r.scene.lights[0].range=.01;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=light;r.scene.lights[0].intensity=0;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=light;r.scene.lights[0].color={1,0,0};auto red=r.renderSet({1},{0,0,-1.2f},eye);uint64_t redSum=0,blueSum=0;for(size_t i=0;i<red.size();i+=4){redSum+=red[i];blueSum+=red[i+2];}assert(redSum>blueSum+10000);
   for(auto &p:asset.primitives)p.materialUnlit=true;auto unlit=r.renderSet({1},{0,0,-1.2f},eye);r.scene.lights.clear();assert(unlit==r.renderSet({1},{0,0,-1.2f},eye));for(auto &p:asset.primitives)p.materialUnlit=false;
   // Lights outside a receiving surface cannot change its depth cutout silhouette.
   Object target{};target.id=2;target.asset=3;target.z=.3;target.scale=.8;r.scene.objects.push_back(target);
   auto depth=r.renderSet({2},{0,0,-1.2f},eye);r.scene.lights={light};assert(depth==r.renderSet({2},{0,0,-1.2f},eye));r.scene.objects.pop_back();
  }
  r.scene.objects[0].x=.2;r.scene.objects[0].y=.3;r.scene.objects[0].z=.4;r.scene.objects[0].yaw=float(std::acos(-1)/2);
  light.objectId=1;light.position={0,0,.1};r.scene.lights={light};auto resolved=resolvePointLights(r.scene);assert(resolved.size()==1&&std::abs(resolved[0].position[0]-.1)<1e-5&&std::abs(resolved[0].position[1]-.3)<1e-5&&std::abs(resolved[0].position[2]-.4)<1e-5);
  if(argc>1&&std::string(argv[1])=="--profile"){
   r.scene.objects[0]=object;
   auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
   for(bool detailed:{false,true}){
   std::vector<int> members;
   if(detailed){
    r.scene.assets.emplace_back();auto &car=r.scene.assets.back();car.pbr=true;car.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",car));uploadModel(car);car.lighting.intensity=0;car.lighting.ambient=.04;
    r.scene.objects.clear();for(int i=0;i<16;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=.2f;o.x=(i%4-1.5f)*.22f;o.y=(i/4-1.5f)*.22f;r.scene.objects.push_back(o);members.push_back(o.id);}
   }else members={1};
   for(int count:{0,1,4,8,8,4,1,0}){
    r.scene.lights.clear();for(int i=0;i<count;i++){auto l=light;l.id=i+1;l.objectId=0;l.position={float(i%2)*.2f,.15f,-.5f};l.intensity=.15f;r.scene.lights.push_back(l);}
    GLuint query;gen(1,&query);double wall=0,gpu=0;
    for(int frame=0;frame<240;frame++){
     auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,0,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);glFinish();GLuint64 elapsed=0;result(query,GL_QUERY_RESULT_EXT,&elapsed);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint);
     if(frame>=60){wall+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();gpu+=double(elapsed)/1e6;}
    }
    destroy(1,&query);printf("POINT_PROFILE lights=%d wall_ms=%.4f gpu_ms=%.4f frames=180 pixels=768 geometry=%s\n",count,wall/180,gpu/180,detailed?"16-ToyCars":"Box");
   }
   }
  }
 }
 {
  const auto dir=std::filesystem::temp_directory_path()/("ps-light-"+std::to_string(getpid()));std::filesystem::create_directory(dir);const auto script=dir/"test.js";ScopeExit cleanup{[&]{std::filesystem::remove_all(dir);}};
  std::ofstream(script)<<R"JS(
   function check(v){if(!v)throw Error('point light assertion');}
   function fails(fn){let caught=false;try{fn()}catch(e){caught=true}check(caught);}
   const owner=createSceneContainer(),asset=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
   const object=createObject(asset,0,0,0,.2,{container:owner});
   const light=createPointLight({container:owner,objectId:object,position:[0,.1,0],intensity:.3,range:2,color:[1,.5,.2]});
   const initial=JSON.stringify(getPointLight(light));
   for(const options of [{position:null},{position:[1,2]},{position:[1,2,NaN]},{color:[-1,1,1]},{range:0},{intensity:Infinity},{enabled:1},{objectId:999},{container:999}]){fails(()=>setPointLight(light,options));check(JSON.stringify(getPointLight(light))===initial);}
   fails(()=>setPointLight(light,{get color(){throw Error('expected getter')}}));check(JSON.stringify(getPointLight(light))===initial);
   setPointLight(light,{get intensity(){for(let i=0;i<7;i++)createPointLight();return .4}});fails(()=>createPointLight());
   for(let id=2;id<=8;id++)removePointLight(id);
   const snap=captureSceneContainer(owner);check(snap.version===8&&snap.lights.length===1);
   saveSceneContainer(owner,'light');const disk=loadSceneContainer('light');check(getPointLight(disk.lightIds[0]).objectId===disk.objectMap[object]);disposeSceneContainer(disk.containerId);
   const restored=restoreSceneContainer(snap),copy=restored.lightIds[0];check(copy!==light&&restored.lightMap[light]===copy&&getPointLight(copy).objectId===restored.objectMap[object]);disposeSceneContainer(restored.containerId);fails(()=>getPointLight(copy));
   for(const mutate of [s=>s.lights[0].range=0,s=>s.lights[0].objectId=999,s=>s.lights[0].color=[1,2],s=>s.lights.push(s.lights[0])]){
    const bad=JSON.parse(JSON.stringify(snap));mutate(bad);const before=JSON.stringify(getSceneContainerState(owner));fails(()=>restoreSceneContainer(bad));check(JSON.stringify(getSceneContainerState(owner))===before&&JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snap));
   }
   const old=JSON.parse(JSON.stringify(snap));old.version=7;delete old.lights;const legacy=restoreSceneContainer(old);check(legacy.lightIds.length===0);disposeSceneContainer(legacy.containerId);
   const tooMany=Array.from({length:7},()=>createPointLight());fails(()=>restoreSceneContainer(snap));tooMany.forEach(removePointLight);
   const other=createSceneContainer();setPointLight(light,{container:other});fails(()=>captureSceneContainer(owner));fails(()=>captureSceneContainer(other));setPointLight(light,{container:owner});
   const removed=createPointLight();fails(()=>setPointLight(removed,{get intensity(){removePointLight(removed);return 1}}));
   const doomed=createSceneContainer(),gone=createPointLight({container:doomed});fails(()=>setPointLight(gone,{get color(){disposeSceneContainer(doomed);return [1,1,1]}}));
   const detached=createPointLight({objectId:object});removeObject(object);fails(()=>getPointLight(detached));fails(()=>getPointLight(light));
   const owned=createPointLight({container:owner});disposeSceneContainer(owner);fails(()=>getPointLight(owned));
   const last=createPointLight({position:[1,2,3]});check(getPointLight(last).position[2]===3);
  )JS";
  scenePersistence::directory=dir.string();
  ScriptHost host;assert(loadScript(host,script.string()));auto *active=host.ctx;auto saved=host.scene.lights;
  std::ofstream(script)<<"createPointLight({intensity:9});throw Error('EXPECTED reload rejection');";assert(!loadScript(host,script.string())&&host.ctx==active&&host.scene.lights.size()==saved.size()&&host.scene.lights[0].position==saved[0].position);
  closeScript(host);assert(host.scene.lights.empty());
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS point lights: stereo pixels, intensity/color/range, disabled/unlit/depth invariance, attachment transforms, limits, getter mutations, cleanup, reload and v8/legacy snapshots\n");
}
