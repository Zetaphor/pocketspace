// Real GLES spotlight cone/attachment checks and transactional API/persistence behavior.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static uint64_t brightness(const std::vector<unsigned char> &image){uint64_t sum=0;for(size_t i=0;i<image.size();i+=4)sum+=image[i]+image[i+1]+image[i+2];return sum;}
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=false;
  assert(loadModelFile("assets/samples/Box/Box.glb",asset));asset.lighting.intensity=asset.lighting.ambient=0;uploadModel(asset);
  for(auto &p:asset.primitives){p.factor[0]=p.factor[1]=p.factor[2]=1;p.metallic=0;p.roughness=.5;}
  Object object{};object.id=1;object.asset=int(r.scene.assets.size());object.scale=.45;r.scene.objects={object};
  PointLight light;light.id=1;light.spot=true;light.position={0,0,-.6f};light.direction={0,0,1};light.range=2;light.intensity=.15f;light.innerConeAngle=.12f;light.outerConeAngle=.25f;
  for(int eye=0;eye<2;eye++){
   r.scene.lights.clear();auto dark=r.renderSet({1},{0,0,-1.2f},eye);
   r.scene.lights={light};auto narrow=r.renderSet({1},{0,0,-1.2f},eye);assert(brightness(narrow)>brightness(dark)+1000);
   r.scene.lights[0].innerConeAngle=.6;r.scene.lights[0].outerConeAngle=.8;auto wide=r.renderSet({1},{0,0,-1.2f},eye);assert(brightness(wide)>brightness(narrow)+10000);
   for(size_t i=3;i<wide.size();i+=4)assert(wide[i]==narrow[i]&&wide[i]==dark[i]);
   r.scene.lights[0].direction={0,0,-1};assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=light;r.scene.lights[0].direction={0,0,8};assert(narrow==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0].enabled=false;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=light;r.scene.lights[0].range=.01;assert(dark==r.renderSet({1},{0,0,-1.2f},eye));
   // A fully illuminated hemisphere is pixel-identical to the point-light BRDF.
   r.scene.lights[0]=light;r.scene.lights[0].innerConeAngle=r.scene.lights[0].outerConeAngle=float(std::acos(-1)/2);
   auto hemisphere=r.renderSet({1},{0,0,-1.2f},eye);r.scene.lights[0].spot=false;assert(hemisphere==r.renderSet({1},{0,0,-1.2f},eye));
   r.scene.lights[0]=light;r.scene.lights[0].innerConeAngle=r.scene.lights[0].outerConeAngle=.3;auto hard=r.renderSet({1},{0,0,-1.2f},eye);assert(brightness(hard)>brightness(narrow));
   for(auto &p:asset.primitives)p.materialUnlit=true;auto unlit=r.renderSet({1},{0,0,-1.2f},eye);r.scene.lights.clear();assert(unlit==r.renderSet({1},{0,0,-1.2f},eye));for(auto &p:asset.primitives)p.materialUnlit=false;
   Object target{};target.id=2;target.asset=3;target.z=.3;target.scale=.8;r.scene.objects.push_back(target);auto depth=r.renderSet({2},{0,0,-1.2f},eye);r.scene.lights={light};assert(depth==r.renderSet({2},{0,0,-1.2f},eye));r.scene.objects.pop_back();
  }
  light.objectId=1;light.position={0,0,.1};r.scene.lights={light};auto &o=r.scene.objects[0];o.x=.2;o.y=.3;o.z=.4;o.yaw=float(std::acos(-1)/2);o.scale=.9;
  auto resolved=resolvePointLights(r.scene);assert(resolved.size()==1&&std::abs(resolved[0].position[0]-.1)<1e-5&&std::abs(resolved[0].position[2]-.4)<1e-5&&std::abs(resolved[0].direction[0]+1)<1e-5&&std::abs(resolved[0].direction[2])<1e-5);
  o.yaw=0;o.pitch=float(std::acos(-1)/2);resolved=resolvePointLights(r.scene);assert(std::abs(resolved[0].direction[1]-1)<1e-5&&std::abs(resolved[0].position[1]-.4)<1e-5);
  o.pitch=0;o.roll=float(std::acos(-1)/2);r.scene.lights[0].direction={1,0,0};resolved=resolvePointLights(r.scene);assert(std::abs(resolved[0].direction[1]-1)<1e-5);
 }
 {
  const auto dir=std::filesystem::temp_directory_path()/("ps-spot-"+std::to_string(getpid()));std::filesystem::create_directory(dir);const auto script=dir/"test.js";ScopeExit cleanup{[&]{std::filesystem::remove_all(dir);}};scenePersistence::directory=dir.string();
  std::ofstream(script)<<R"JS(
   function check(v){if(!v)throw Error('spotlight assertion');}
   function fails(fn){let caught=false;try{fn()}catch(e){caught=true}check(caught);}
   const owner=createSceneContainer(),asset=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
   const object=createObject(asset,0,0,0,.2,{container:owner});
   const spot=createSpotLight({container:owner,objectId:object,direction:[0,0,2],innerConeAngle:.2,outerConeAngle:.4});
   const point=createPointLight({container:owner});check(getSpotLight(spot).type==='spot'&&getPointLight(point).type==='point');
   for(const call of [()=>getPointLight(spot),()=>setPointLight(spot,{}),()=>removePointLight(spot),()=>getSpotLight(point),()=>setSpotLight(point,{}),()=>removeSpotLight(point)])fails(call);
   const initial=JSON.stringify(getSpotLight(spot));
   for(const options of [{direction:[0,0,0]},{direction:[1,2]},{direction:[0,Infinity,1]},{direction:null},{innerConeAngle:-1},{outerConeAngle:0},{outerConeAngle:2},{innerConeAngle:.6},{outerConeAngle:.1},{innerConeAngle:'0'},{range:0},{enabled:1}]){fails(()=>setSpotLight(spot,options));check(JSON.stringify(getSpotLight(spot))===initial);}
   fails(()=>setSpotLight(spot,{get direction(){throw Error('expected getter')}}));check(JSON.stringify(getSpotLight(spot))===initial);
   setSpotLight(spot,{innerConeAngle:Math.PI/2,outerConeAngle:Math.PI/2});const edge=captureSceneContainer(owner),edgeCopy=restoreSceneContainer(edge);check(getSpotLight(edgeCopy.lightMap[spot]).outerConeAngle>1.57);disposeSceneContainer(edgeCopy.containerId);
   setSpotLight(spot,{innerConeAngle:.2,outerConeAngle:.4});const snapshot=captureSceneContainer(owner);check(snapshot.version===14&&snapshot.lights.length===2&&snapshot.lights[0].type==='spot');
   saveSceneContainer(owner,'spot');const disk=loadSceneContainer('spot');check(getSpotLight(disk.lightMap[spot]).objectId===disk.objectMap[object]);disposeSceneContainer(disk.containerId);
   for(const mutate of [s=>s.lights[0].type='area',s=>s.lights[0].direction=[0,0,0],s=>s.lights[0].innerConeAngle=.9,s=>delete s.lights[0].direction,s=>s.version=8]){
    const bad=JSON.parse(JSON.stringify(snapshot));mutate(bad);const before=JSON.stringify(getSceneContainerState(owner));fails(()=>restoreSceneContainer(bad));check(JSON.stringify(getSceneContainerState(owner))===before&&JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snapshot));
   }
   const legacy=JSON.parse(JSON.stringify(snapshot));legacy.version=8;legacy.lights=legacy.lights.filter(l=>l.type==='point');delete legacy.lights[0].type;const restored=restoreSceneContainer(legacy);check(getPointLight(restored.lightIds[0]).type==='point');disposeSceneContainer(restored.containerId);
   const extras=Array.from({length:6},(_,i)=>i%2?createSpotLight({enabled:false}):createPointLight());fails(()=>createPointLight());fails(()=>createSpotLight());extras.forEach((id,i)=>i%2?removeSpotLight(id):removePointLight(id));
   const removed=createSpotLight();fails(()=>setSpotLight(removed,{get direction(){removeSpotLight(removed);return [0,0,1]}}));
   const doomed=createSceneContainer(),gone=createSpotLight({container:doomed});fails(()=>setSpotLight(gone,{get outerConeAngle(){disposeSceneContainer(doomed);return .5}}));
   removeObject(object);fails(()=>getSpotLight(spot));disposeSceneContainer(owner);fails(()=>getPointLight(point));
   const last=createSpotLight({direction:[1,0,0]});check(getSpotLight(last).direction[0]===1);
  )JS";
  ScriptHost host;assert(loadScript(host,script.string()));auto *active=host.ctx;auto saved=host.scene.lights;
  std::ofstream(script)<<"createSpotLight();throw Error('EXPECTED spotlight reload rejection');";assert(!loadScript(host,script.string())&&host.ctx==active&&host.scene.lights.size()==saved.size()&&host.scene.lights[0].direction==saved[0].direction);closeScript(host);
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS spotlights: stereo cones, direction/normalization, hard edges, point equivalence, unlit/depth invariance, attachments, typed APIs, rollback, mixed capacity, v9/v8 persistence and cleanup\n");
}
