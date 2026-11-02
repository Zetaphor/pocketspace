#include "../src/engine_state.h"
#include "../src/native_resource_key.h"
#include "../src/script_registry.h"
#include "../src/input_arbitration.h"
#include "../src/panel_pointer.h"
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)
// Two real script instances: same-name isolation, reload, explicit import, failed
// import rollback, symlink refusal, restart with different instance IDs, and scratch.
static void namespaceRegression(){
  namespace fs=std::filesystem;
  const auto root=fs::temp_directory_path()/("sf-namespaces-"+std::to_string(getpid()));
  const auto catalog=root/"catalog",state=root/"state";fs::create_directories(catalog/"nested");fs::create_directories(state);
  ScopeExit cleanup{[&]{reserveObjectOverlay={};releaseObjectOverlay={};scenePersistence::catalogRoot.clear();scenePersistence::directory.clear();fs::remove_all(root);}};
  scenePersistence::directory=state.string();scenePersistence::catalogRoot=catalog.string();
  auto scriptWith=[](int objects){return "const model=loadModel('assets/samples/Box/Box.glb',512);const owner=createSceneContainer();for(let n=0;n<"+std::to_string(objects)+";n++)createObject(model,n*.1,0,0,.1,{container:owner});function check(v){if(!v)throw Error('namespace assertion');}function update(){}";};
  auto writeFile=[](const fs::path &path,const std::string &text){std::ofstream(path)<<text;};
  auto readFile=[](const fs::path &path){std::ifstream file(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(file)),{});};
  writeFile(catalog/"left.js",scriptWith(1));writeFile(catalog/"nested"/"right.js",scriptWith(2));writeFile(catalog/"redirected.js",scriptWith(1));
  fs::create_symlink(catalog/"left.js",catalog/"alias.js");
  writeFile(root/"outside.js",scriptWith(1));fs::create_symlink(root/"outside.js",catalog/"escape.js");
  auto run=[](ScriptHost &host,const std::string &code,bool success=true){
    ScriptScope scope(host);
    JSValue value=JS_Eval(host.ctx,code.c_str(),code.size(),"namespace regression",JS_EVAL_TYPE_GLOBAL);const bool ok=!JS_IsException(value);
    if(!ok){JSValue e=JS_GetException(host.ctx);const char *s=JS_ToCString(host.ctx,e);if(success)fprintf(stderr,"%s: %s\n",code.c_str(),s?s:"exception");if(s)JS_FreeCString(host.ctx,s);JS_FreeValue(host.ctx,e);}
    JS_FreeValue(host.ctx,value);assert(ok==success);
  };
  const auto leftKey=state/"left.js",rightKey=state/"nested%2Fright.js";
  auto launch=[&](ScriptRegistry &registry,const char *name){
    fs::path resolved;std::string error;assert(resolveCatalogScript(catalog,name,resolved,error));
    int id=0;assert(registry.launch(resolved.string(),id,error));return id;
  };
  // Keys are stable across a moved catalog, injective, and never share scratch.
  assert(scriptStorageKey("/nonexistent-catalog","/tmp/a b%.js")=="%2Ftmp%2Fa%20b%25.js");
  assert(scriptStorageKey(catalog,fs::canonical(catalog)/"nested"/"right.js")=="nested%2Fright.js");
  assert(scriptStorageKey(catalog,fs::canonical(catalog)/"nested%2Fright.js")=="nested%252Fright.js");
  int firstLeft=0,firstRight=0;
  {
    ScriptRegistry registry;
    firstLeft=launch(registry,"left.js");firstRight=launch(registry,"nested/right.js");
    auto &left=registry.find(firstLeft)->host;auto &right=registry.find(firstRight)->host;
    run(left,"saveSceneContainer(owner,'layout')");run(right,"saveSceneContainer(owner,'layout')");
    assert(fs::is_regular_file(leftKey/"layout.json")&&fs::is_regular_file(rightKey/"layout.json"));
    assert(!fs::exists(state/"layout.json")&&!fs::exists(state/"scratch"));
    run(left,"{const c=loadSceneContainer('layout');check(c.objectIds.length===1);disposeSceneContainer(c.containerId);}");
    run(right,"{const c=loadSceneContainer('layout');check(c.objectIds.length===2);disposeSceneContainer(c.containerId);}");
    // Explicit import copies through restore with fresh IDs, in both directions.
    const auto rightBytes=readFile(rightKey/"layout.json");
    run(left,"{const c=importSceneContainer('nested/right.js','layout');check(c.objectIds.length===2&&!c.objectIds.includes(1));disposeSceneContainer(c.containerId);}");
    run(right,"{const c=importSceneContainer('left.js','layout');check(c.objectIds.length===1);disposeSceneContainer(c.containerId);}");
    run(left,"{const c=importSceneContainer('alias.js','layout');check(c.objectIds.length===1);disposeSceneContainer(c.containerId);}");
    assert(readFile(rightKey/"layout.json")==rightBytes&&right.scene.objects.size()==2);
    // Rejected sources and failed restores leave both scenes and files unchanged.
    writeFile(rightKey/"malformed.json","{not json");writeFile(rightKey/"future.json","{\"version\":99}");
    fs::create_symlink(leftKey/"layout.json",rightKey/"link.json");
    fs::create_symlink(leftKey,state/"redirected.js");
    const size_t leftObjects=left.scene.objects.size(),leftContainers=left.scene.containers.size();const int leftNext=left.scene.nextId;
    for(const char *source:{"'../left.js'","'nested/../left.js'","'/tmp/left.js'","'left'","''","7","'missing.js'","'escape.js'","'redirected.js'"})
      run(left,std::string("importSceneContainer(")+source+",'layout')",false);
    for(const char *name:{"'missing'","'malformed'","'future'","'link'","'../layout'","''"})
      run(left,std::string("importSceneContainer('nested/right.js',")+name+")",false);
    run(left,"importSceneContainer('nested/right.js')",false);
    int calls=0,releases=0;
    reserveObjectOverlay=[&](int){return ++calls==2?"EXPECTED reservation failure":std::string{};};releaseObjectOverlay=[&](int){releases++;};
    run(left,"importSceneContainer('nested/right.js','layout')",false);assert(calls==2&&releases==1);
    reserveObjectOverlay={};releaseObjectOverlay={};
    assert(left.scene.objects.size()==leftObjects&&left.scene.containers.size()==leftContainers&&left.scene.nextId==leftNext);
    assert(right.scene.objects.size()==2&&readFile(rightKey/"layout.json")==rightBytes);
    // A symlinked namespace cannot redirect a script's own saves or loads.
    const int redirected=launch(registry,"redirected.js");auto &link=registry.find(redirected)->host;
    run(link,"saveSceneContainer(owner,'stolen')",false);run(link,"loadSceneContainer('layout')",false);
    assert(!fs::exists(leftKey/"stolen.json"));
    // Reload replaces the host but keeps its namespace; failed reload changes nothing.
    assert(registry.reload(firstLeft));
    run(registry.find(firstLeft)->host,"saveSceneContainer(owner,'reloaded')");assert(fs::is_regular_file(leftKey/"reloaded.json"));
    writeFile(catalog/"left.js","throw Error('EXPECTED candidate failure');");
    assert(!registry.reload(firstLeft));
    run(registry.find(firstLeft)->host,"saveSceneContainer(owner,'kept')");assert(fs::is_regular_file(leftKey/"kept.json"));
    writeFile(catalog/"left.js",scriptWith(1));
    // A save outlives its stopped source.
    assert(registry.stop(firstRight));
    run(registry.find(firstLeft)->host,"{const c=importSceneContainer('nested/right.js','layout');check(c.objectIds.length===2);disposeSceneContainer(c.containerId);}");
  }
  assert(scenePersistence::activeNamespace==&scenePersistence::scratchNamespace);
  // A restarted engine assigns other instance IDs but finds the same saves.
  {
    ScriptRegistry registry;
    const int right=launch(registry,"nested/right.js"),left=launch(registry,"left.js");
    assert(right==firstLeft&&left==firstRight);
    run(registry.find(left)->host,"{const c=loadSceneContainer('layout');check(c.objectIds.length===1);disposeSceneContainer(c.containerId);}");
    run(registry.find(right)->host,"{const c=loadSceneContainer('layout');check(c.objectIds.length===2);disposeSceneContainer(c.containerId);}");
  }
  // Scratch has a separate namespace and uses the same explicit import path.
  {
    writeFile(root/"scratch.js",scriptWith(3));
    ScriptHost scratch;ScopeExit close{[&]{ScriptScope scope(scratch);closeScript(scratch);resetActiveInteraction();}};
    {ScriptScope scope(scratch);assert(loadScript(scratch,(root/"scratch.js").string()));}
    run(scratch,"saveSceneContainer(owner,'layout')");assert(fs::is_regular_file(state/"scratch"/"layout.json"));
    run(scratch,"{const c=loadSceneContainer('layout');check(c.objectIds.length===3);disposeSceneContainer(c.containerId);}");
    run(scratch,"{const c=importSceneContainer('left.js','layout');check(c.objectIds.length===1);disposeSceneContainer(c.containerId);}");
    assert(readFile(leftKey/"layout.json").find("\"objects\"")!=std::string::npos);
  }
}
int main(){
  namespaceRegression();
  const auto dir=std::filesystem::temp_directory_path()/("sf-persistence-"+std::to_string(getpid()));std::filesystem::create_directory(dir);
  scenePersistence::directory=dir.string();auto script=dir/"test.js";
  ScriptHost host;ScopeExit cleanup{[&]{reserveObjectOverlay={};releaseObjectOverlay={};reservePanelOverlay={};releasePanelOverlay={};closeScript(host);std::filesystem::remove_all(dir);}};
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
    for(const key of Object.keys(snap.objects[0]))if(!['id','renderGroup','container'].includes(key))check(JSON.stringify(snap.objects[0][key])===JSON.stringify(again.objects[0][key]));
    check(again.objects[0].container===copy.containerId&&copy.containerMap[owner]===copy.containerId);
    check(again.objects[1].visible===false);disposeSceneContainer(copy.containerId);
    function bad(edit){const s=JSON.parse(JSON.stringify(snap));edit(s);fails(()=>restoreSceneContainer(s));}
    bad(s=>s.objects[0].x=null);bad(s=>delete s.objects[0].visible);
    fails(()=>restoreSceneContainer(null));const cycle={};cycle.self=cycle;fails(()=>restoreSceneContainer(cycle));
    bad(s=>s.note="x".repeat(16*1024*1024));
    bad(s=>s.version=2);bad(s=>s.version=14);bad(s=>s.format='sf-overlay-engine.scene-container');bad(s=>s.format='other');
    for(const key of ['joints','panels','lights','containers','rootContainer'])bad(s=>delete s[key]);
    bad(s=>delete s.objects[0].physics);bad(s=>delete s.objects[0].animation);bad(s=>delete s.objects[0].container);bad(s=>delete s.models[0].pbr);bad(s=>delete s.models[0].lighting);bad(s=>delete s.models[0].lighting.environment);bad(s=>s.objects[0].scale=0);bad(s=>s.objects[0].x=Infinity);
    bad(s=>s.objects[0].visible=1);bad(s=>s.objects[0].grabButton='invalid');bad(s=>s.objects[1].id=o);
    bad(s=>s.objects[0].model=5);bad(s=>s.models[0].eyePixels=513);bad(s=>s.models[0].lod=true);
    bad(s=>s.models[0].path='/missing-model.glb');bad(s=>s.models.push(s.models[0]));
    bad(s=>s.objects=Array(257).fill(s.objects[0]));
    fails(()=>restoreSceneContainer({toJSON(){throw Error('getter');}}));
    fails(()=>captureSceneContainer(0));fails(()=>saveSceneContainer(owner,'../escape'));fails(()=>saveSceneContainer(owner,''));
    const p=createPanel({widthPixels:8,heightPixels:8,widthMeters:.1,container:owner});check(captureSceneContainer(owner).panels.length===1);removePanel(p);
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
  const auto saves=dir/"scratch";
  std::ofstream(saves/"future.json")<<"{\"version\":2}";eval("loadSceneContainer('future')",false);
  std::ofstream(saves/"huge.json")<<std::string(scenePersistence::maxBytes+1,'x');eval("loadSceneContainer('huge')",false);
  std::filesystem::create_symlink(saves/"roundtrip.json",saves/"link.json");eval("loadSceneContainer('link')",false);
  std::filesystem::create_directory(saves/"blocked.json");eval("saveSceneContainer(owner,'blocked')",false);
  for(const auto &entry:std::filesystem::directory_iterator(saves))assert(entry.path().filename().string().find(".tmp-")==std::string::npos);
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
  closeScript(host);assert(loadScript(host,"tests/panel-persistence-api-smoke.js"));ctx=host.ctx;liveScene=currentScene=&host.scene;
  assert(host.scene.panels.size()==8&&host.scene.objects.size()==2);
  for(size_t i=0;i<4;i++){const auto &a=host.scene.panels[i],&b=host.scene.panels[i+4];if(i!=1)assert(a.rgba==b.rgba);assert(a.rgba.data()!=b.rgba.data()&&b.texture==0&&b.dirty&&b.container!=a.container);}
  const int panelNext=host.scene.nextPanelId,objectIdNext=host.scene.nextId,ownerIdNext=host.scene.nextContainerId;int panelCalls=0,panelReleases=0;calls=releases=0;
  reserveObjectOverlay=[&](int){calls++;return std::string{};};releaseObjectOverlay=[&](int){releases++;};
  reservePanelOverlay=[&](const Panel &){return ++panelCalls==2?std::string("EXPECTED panel overlay failure"):std::string{};};releasePanelOverlay=[&](int){panelReleases++;};
  eval("restoreSceneContainer(panelSnapshot)",false);
  assert(panelCalls==2&&panelReleases==1&&calls==1&&releases==1&&host.scene.nextPanelId==panelNext&&host.scene.nextId==objectIdNext&&host.scene.nextContainerId==ownerIdNext&&host.scene.panels.size()==8);
  reserveObjectOverlay={};releaseObjectOverlay={};reservePanelOverlay={};releasePanelOverlay={};
  calls=releases=panelCalls=panelReleases=0;
  reserveObjectOverlay=[&](int){calls++;return std::string{};};releaseObjectOverlay=[&](int){releases++;};
  reservePanelOverlay=[&](const Panel &){panelCalls++;return std::string{};};releasePanelOverlay=[&](int){panelReleases++;};
  eval("setObjectPhysics(object);{const s=captureSceneContainer(panelOwner);s.objects[0].physics.settings.mass=0;fails(()=>restoreSceneContainer(s));}");
  assert(calls==1&&releases==1&&panelCalls==4&&panelReleases==4&&host.scene.nextPanelId==panelNext&&host.scene.nextId==objectIdNext&&host.scene.nextContainerId==ownerIdNext&&host.scene.panels.size()==8&&host.scene.physics->contains(1)&&!host.scene.physics->contains(objectIdNext));
  reserveObjectOverlay={};releaseObjectOverlay={};reservePanelOverlay={};releasePanelOverlay={};
  // Budget rejection happens before allocating decoded pixel storage.
  size_t fullBudget=scenePersistence::maxPanelBytes;bool rejected=false;
  try{JS_FreeValue(ctx,capturePanelSnapshot(ctx,host.scene.panels[0],fullBudget));}catch(const std::exception&){rejected=true;}assert(rejected&&fullBudget==scenePersistence::maxPanelBytes);
  frameInput.addOverlayEvent("panel.pointerDown",5,0);dispatchInputCallbacks();eval("check(clicks===1)");
  assert(host.scene.panels[4].rgba!=host.scene.panels[0].rgba);
  eval("disposeSceneContainer(panelCopy.containerId)");assert(host.scene.panels.size()==4);
  closeScript(host);std::ofstream(script)<<"const c=loadSceneContainer('panels');if(c.panels.length!==4)throw Error('fresh panel restore');c.panels[0].canvas.text(1,1,'New text');c.panels[0].present();function update(){}";
  assert(loadScript(host,script.string()));assert(host.scene.panels.size()==4&&host.scene.panels[0].interactive&&!host.scene.panels[2].visible);
  puts("PASS scene persistence: per-script namespaces, restart, explicit import, JSON/disk roundtrip, transforms/options, fresh IDs/groups, unsupported resources, validation, staged asset and surface rollback, repeated disposal, bounded files, atomic replacement cleanup, physics bodies/joints/motors/frames, independent skin/rigid animation poses, playback states, lossless panel pixels/handles and rollback");
}
