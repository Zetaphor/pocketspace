#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)
int main(){
  const auto dir=std::filesystem::temp_directory_path()/("sf-persistence-"+std::to_string(getpid()));std::filesystem::create_directory(dir);
  scenePersistence::directory=dir.string();auto script=dir/"test.js";
  ScriptHost host;ScopeExit cleanup{[&]{reserveObjectOverlay={};releaseObjectOverlay={};closeScript(host);std::filesystem::remove_all(dir);}};
  std::ofstream(script)<<R"JS(
    const a=loadModel('assets/samples/Box/Box.glb',512,{lod:false,gpuInstancing:false,optimizeVertexCache:true});
    const owner=createSceneContainer();
    const o=createObject(a,.1,.2,-.3,.15,{container:owner,renderGroup:4});
    enableObjectInteraction(o,{grabButton:'both'});
    setObjectRotation(o,.2,.3,.4);setObjectInteraction(o,{laserYaw:true,depthAdjust:true,handRotationAxes:{pitch:true,yaw:true,roll:true}});
    const other=createObject(a,-.2,.1,0,.1,{container:owner});setObjectVisible(other,false);
    const root=createObject(a,0,0,0,.1,{renderGroup:1});
    const snap=captureSceneContainer(owner);const copy=restoreSceneContainer(JSON.parse(JSON.stringify(snap)));
    function check(v){if(!v)throw Error('persistence assertion');}
    function fails(fn){let caught=false;try{fn();}catch(e){caught=true;}check(caught);}
    check(copy.objectIds.length===2&&copy.objectMap[o]===copy.objectIds[0]&&copy.renderGroups[4]!==4&&copy.renderGroups[4]!==1);
    const again=captureSceneContainer(copy.containerId);check(again.models[0].lod===false&&again.models[0].optimizeVertexCache===true);
    for(const key of Object.keys(snap.objects[0]))if(!['id','renderGroup'].includes(key))check(JSON.stringify(snap.objects[0][key])===JSON.stringify(again.objects[0][key]));
    check(again.objects[1].visible===false);disposeSceneContainer(copy.containerId);
    function bad(edit){const s=JSON.parse(JSON.stringify(snap));edit(s);fails(()=>restoreSceneContainer(s));}
    bad(s=>s.objects[0].x=null);bad(s=>delete s.objects[0].visible);
    fails(()=>restoreSceneContainer(null));const cycle={};cycle.self=cycle;fails(()=>restoreSceneContainer(cycle));
    bad(s=>s.note="x".repeat(1024*1024));
    bad(s=>s.version=4);bad(s=>s.format='other');bad(s=>s.objects[0].scale=0);bad(s=>s.objects[0].x=Infinity);
    bad(s=>s.objects[0].visible=1);bad(s=>s.objects[0].grabButton='invalid');bad(s=>s.objects[1].id=o);
    bad(s=>s.objects[0].model=5);bad(s=>s.models[0].eyePixels=513);bad(s=>s.models[0].lod=true);
    bad(s=>s.models[0].path='/missing-model.glb');bad(s=>s.models.push(s.models[0]));
    bad(s=>s.objects=Array(257).fill(s.objects[0]));
    fails(()=>restoreSceneContainer({toJSON(){throw Error('getter');}}));
    fails(()=>captureSceneContainer(0));fails(()=>saveSceneContainer(owner,'../escape'));fails(()=>saveSceneContainer(owner,''));
    const p=createPanel({widthPixels:8,heightPixels:8,widthMeters:.1,container:owner});fails(()=>captureSceneContainer(owner));removePanel(p);
    setObjectPhysics(o);check(captureSceneContainer(owner).objects[0].physics.settings.type==='dynamic');removeObjectPhysics(o);
    saveSceneContainer(owner,'roundtrip');const disk=loadSceneContainer('roundtrip');check(disk.objectIds.length===2);disposeSceneContainer(disk.containerId);
    fails(()=>loadSceneContainer('missing'));
    onInputEvent(()=>{}, {container:owner});
    function update(){}
  )JS";
  assert(loadScript(host,script.string()));
  assert(host.scene.objects.size()==3&&host.scene.assets.size()==1&&host.scene.containers.size()==1);
  const int next=host.scene.nextId,ownerNext=host.scene.nextContainerId;auto *ctx=host.ctx;
  auto eval=[&](const char *code,bool success=true){JSValue v=JS_Eval(ctx,code,strlen(code),"persistence regression",JS_EVAL_TYPE_GLOBAL);bool ok=!JS_IsException(v);if(!ok){JSValue e=JS_GetException(ctx);const char *s=JS_ToCString(ctx,e);if(success)fprintf(stderr,"%s\n",s?s:"exception");if(s)JS_FreeCString(ctx,s);JS_FreeValue(ctx,e);}JS_FreeValue(ctx,v);assert(ok==success);};
  // A second surface failure must release the first reservation, leave all IDs,
  // cache contents, root objects and callback ownership unchanged.
  liveScene=currentScene=&host.scene;int calls=0,releases=0;
  reserveObjectOverlay=[&](int){return ++calls==2?"EXPECTED reservation failure":std::string{};};releaseObjectOverlay=[&](int){releases++;};
  eval("restoreSceneContainer(snap)",false);assert(calls==2&&releases==1&&host.scene.nextId==next&&host.scene.nextContainerId==ownerNext&&host.scene.objects.size()==3&&host.scene.assets.size()==1);
  assert(inputCallbackCtx==ctx&&inputCallbackContainer==1);
  reserveObjectOverlay={};releaseObjectOverlay={};
  // Missing second asset must not retain a successfully staged first asset.
  eval("{const s=JSON.parse(JSON.stringify(snap));s.models[0].path='assets/Avocado.glb';s.models.push({...s.models[0],path:'/missing-last.glb'});fails(()=>restoreSceneContainer(s));}");
  assert(host.scene.assets.size()==1&&host.scene.nextId==next);
  // Exercise options on uncached assets and repeated restore/dispose.
  eval("{const s=JSON.parse(JSON.stringify(snap));s.models[0].path='assets/Avocado.glb';const c=restoreSceneContainer(s);const a=captureSceneContainer(c.containerId).models[0];check(a.eyePixels===512&&!a.lod&&!a.gpuInstancing&&a.optimizeVertexCache);disposeSceneContainer(c.containerId);}");
  eval("for(let i=0;i<40;i++){const c=loadSceneContainer('roundtrip');disposeSceneContainer(c.containerId);}");
  assert(host.scene.objects.size()==3&&host.scene.containers.size()==1);
  eval("{const a=loadModel('assets/samples/CesiumMan/CesiumMan.glb');const c=createSceneContainer();const id=createObject(a,0,0,0,.1,{container:c});check(captureSceneContainer(c).objects.length===1);playAnimation(id,0);check(captureSceneContainer(c).objects[0].animation.clip===0);disposeSceneContainer(c);}");
  // File validation, symlink refusal, failed replacement cleanup, original intact.
  std::ofstream(dir/"future.json")<<"{\"version\":2}";eval("loadSceneContainer('future')",false);
  std::ofstream(dir/"huge.json")<<std::string(scenePersistence::maxBytes+1,'x');eval("loadSceneContainer('huge')",false);
  std::filesystem::create_symlink(dir/"roundtrip.json",dir/"link.json");eval("loadSceneContainer('link')",false);
  std::filesystem::create_directory(dir/"blocked.json");eval("saveSceneContainer(owner,'blocked')",false);
  for(const auto &entry:std::filesystem::directory_iterator(dir))assert(entry.path().filename().string().find(".tmp-")==std::string::npos);
  eval("const last=loadSceneContainer('roundtrip');disposeSceneContainer(last.containerId);");
  // A fresh QuickJS runtime and empty asset cache can load the on-disk save.
  closeScript(host);std::ofstream(script)<<"const restored=loadSceneContainer('roundtrip');if(restored.objectIds.length!==2)throw Error('fresh runtime restore');function update(){}";
  assert(loadScript(host,script.string()));assert(host.scene.assets.size()==1&&host.scene.objects.size()==2);
  assert(host.scene.assets[0].eyePixels==512&&!host.scene.assets[0].lod&&!host.scene.assets[0].gpuInstancing&&host.scene.assets[0].optimizeVertexCache);
  closeScript(host);assert(loadScript(host,"tests/physics-persistence-api-smoke.js"));ctx=host.ctx;
  liveScene=currentScene=&host.scene;
  const auto objectCount=host.scene.objects.size(),assetCount=host.scene.assets.size();const int objectNext=host.scene.nextId,containerNext=host.scene.nextContainerId,jointNext=host.scene.physics->nextJointId();
  calls=releases=0;reserveObjectOverlay=[&](int){calls++;return std::string{};};releaseObjectOverlay=[&](int){releases++;};
  eval("physicsBad(s=>{s.models[0].path='assets/Avocado.glb';s.joints[2].settings.spring.frequency=0;})");
  assert(calls==1&&releases==1&&host.scene.objects.size()==objectCount&&host.scene.assets.size()==assetCount&&host.scene.nextId==objectNext&&host.scene.nextContainerId==containerNext&&host.scene.physics->nextJointId()==jointNext);
  assert(host.scene.physics->jointSnapshots().size()==3&&!host.scene.physics->contains(objectNext)&&!host.scene.physics->hasConvexHull(2)&&!host.scene.physics->hasMesh(2));
  reserveObjectOverlay={};releaseObjectOverlay={};
  host.scene.physics->hold(2);eval("{const c=restoreSceneContainer(captureSceneContainer(physicsOwner));for(const id of c.objectIds)check(!getObjectPhysics(id).held);disposeSceneContainer(c.containerId);}");host.scene.physics->release(2,false);
  // Old v1 documents still restore; new captures always use v3.
  eval("{const s=captureSceneContainer(physicsOwner);s.version=1;delete s.joints;for(const o of s.objects)delete o.physics;const c=restoreSceneContainer(s);check(c.jointIds.length===0&&c.objectIds.every(id=>getObjectPhysics(id)===null));disposeSceneContainer(c.containerId);}");
  closeScript(host);std::ofstream(script)<<"const c=loadSceneContainer('physics');if(c.objectIds.length!==6||c.jointIds.length!==3)throw Error('fresh physics restore');function update(){}";
  assert(loadScript(host,script.string()));assert(host.scene.physics&&host.scene.physics->jointSnapshots().size()==3);
  for(const auto &o:host.scene.objects)assert(host.scene.physics->contains(o.id));
  host.scene.physics->advance(1.0/90);assert(host.scene.physics->jointSnapshots().size()==3);
  closeScript(host);assert(loadScript(host,"tests/animation-persistence-api-smoke.js"));ctx=host.ctx;
  auto *finished=findObject(3);const auto &clip=host.scene.assets[finished->asset-1].animations[finished->animation.clip];animation::advance(finished->animation,clip,1.0/90);assert(finished->animation.finished);
  eval("saveAndCheck()");
  assert(host.scene.objects.size()==8&&host.scene.assets.size()==2);
  for(size_t i=0;i<4;i++){
    auto &a=host.scene.objects[i],&b=host.scene.objects[i+4];assert(bool(a.animationPose)==bool(b.animationPose));
    if(!a.animationPose)continue;assert(a.animationPose!=b.animationPose);
    assert(a.animationPose->radius==b.animationPose->radius);
    for(size_t p=0;p<a.animationPose->geometry.primitives.size();p++)assert(a.animationPose->geometry.primitives[p].vertices==b.animationPose->geometry.primitives[p].vertices);
    for(int axis=0;axis<3;axis++){assert(a.animationPose->geometry.low[axis]==b.animationPose->geometry.low[axis]);assert(a.animationPose->geometry.high[axis]==b.animationPose->geometry.high[axis]);}
  }
  liveScene=currentScene=&host.scene;calls=releases=0;const int beforeObjects=host.scene.nextId,beforeOwner=host.scene.nextContainerId;
  reserveObjectOverlay=[&](int){calls++;return std::string("EXPECTED animation reservation failure");};releaseObjectOverlay=[&](int){releases++;};
  eval("restoreSceneContainer(animationSnapshot)",false);assert(calls==1&&releases==0&&host.scene.nextId==beforeObjects&&host.scene.nextContainerId==beforeOwner&&host.scene.objects.size()==8);
  reserveObjectOverlay={};releaseObjectOverlay={};
  eval("for(let i=0;i<20;i++){const c=restoreSceneContainer(animationSnapshot);disposeSceneContainer(c.containerId);}");
  assert(host.scene.objects.size()==8&&host.scene.containers.size()==2);
  closeScript(host);std::ofstream(script)<<"const c=loadSceneContainer('animations');if(c.objectIds.length!==4||getAnimationState(c.objectIds[0]).speed!==.75||getAnimationState(c.objectIds[1]).status!=='paused'||getAnimationState(c.objectIds[2]).status!=='finished'||getAnimationState(c.objectIds[3]).status!=='stopped')throw Error('fresh animation restore');function update(){}";
  assert(loadScript(host,script.string()));assert(host.scene.objects[0].animationPose&&host.scene.objects[1].animationPose&&host.scene.objects[2].animationPose&&!host.scene.objects[3].animationPose);
  puts("PASS scene persistence: JSON/disk roundtrip, transforms/options, fresh IDs/groups, unsupported resources, validation, staged asset and surface rollback, repeated disposal, bounded files, atomic replacement cleanup, physics bodies/joints/motors/frames, independent skin/rigid animation poses, playback states and rollback");
}
