// Transactional shadow settings, single-light capacity and snapshot compatibility.
function check(v) { if (!v) throw Error('shadow assertion'); }
function fails(fn) { let caught=false;try { fn(); } catch(e) { caught=true; }check(caught); }
const owner=createSceneContainer();
const asset=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
const object=createObject(asset,0,0,0,.2,{container:owner});
const spot=createPointLight({container:owner,objectId:object,range:3});
check(!getPointLight(spot).shadow.enabled&&getPointLight(spot).shadow.resolution===512);
setPointLight(spot,{shadow:{enabled:true,resolution:256,maxTriangles:10000,maxGpuMs:4,near:.02,bias:.001,normalBias:.003}});
const initial=JSON.stringify(getPointLight(spot));
for(const shadow of [null,true, {resolution:384},{resolution:2048},{resolution:512.5},{resolution:'512'}, {enabled:1}, {near:0},{near:4},{bias:-1},{normalBias:Infinity},{maxTriangles:0},{maxTriangles:1000001},{maxGpuMs:0},{maxGpuMs:17}]) {
  fails(()=>setPointLight(spot,{shadow}));check(JSON.stringify(getPointLight(spot))===initial);
}
fails(()=>setPointLight(spot,{shadow:{get resolution(){throw Error('getter');}}}));
check(JSON.stringify(getPointLight(spot))===initial);
fails(()=>setPointLight(spot,{shadow:{resolution:1024}}));
fails(()=>setPointLight(spot,{range:.01}));
fails(()=>createPointLight({shadow:{enabled:true}}));
fails(()=>createDirectionalLight({shadow:{enabled:true}})); // Existing spotlight owns the shared slot.
fails(()=>createPointLight({shadow:{enabled:true}}));
const second=createPointLight({enabled:false,shadow:{enabled:true}});
fails(()=>setPointLight(second,{enabled:true}));check(!getPointLight(second).enabled);
setPointLight(spot,{enabled:false});setPointLight(second,{enabled:true});
fails(()=>setPointLight(spot,{enabled:true}));setPointLight(second,{shadow:{enabled:false}});setPointLight(spot,{enabled:true});removePointLight(second);
const snapshot=captureSceneContainer(owner);check(snapshot.version===14&&snapshot.lights[0].shadow.enabled);
const before=JSON.stringify(snapshot),state=JSON.stringify(getSceneContainerState(owner));
fails(()=>restoreSceneContainer(snapshot));check(JSON.stringify(captureSceneContainer(owner))===before);
for(const mutate of [s=>delete s.lights[0].shadow,s=>s.lights[0].shadow.resolution=2048,s=>s.lights[0].shadow.maxTriangles=0,s=>s.lights[0].shadow.maxGpuMs=0,s=>s.lights[0].shadow.near=4,s=>delete s.lights[0].shadow.near]) {
  const bad=JSON.parse(before);mutate(bad);fails(()=>restoreSceneContainer(bad));check(JSON.stringify(getSceneContainerState(owner))===state);
}
setPointLight(spot,{shadow:{enabled:false}});
const restored=restoreSceneContainer(snapshot),copy=getPointLight(restored.lightMap[spot]);
check(copy.objectId===restored.objectMap[object]&&copy.shadow.resolution===256&&copy.shadow.enabled);
disposeSceneContainer(restored.containerId);
const legacy=JSON.parse(before);legacy.version=13;delete legacy.lights[0].shadow;
const flat=restoreSceneContainer(legacy);check(!getPointLight(flat.lightMap[spot]).shadow.enabled);disposeSceneContainer(flat.containerId);
setPointLight(spot,{shadow:{enabled:true}});
const removed=createPointLight();fails(()=>setPointLight(removed,{shadow:{get enabled(){removePointLight(removed);return false;}}}));
// A getter may reserve the slot, so validation must run again after it returns.
setPointLight(spot,{shadow:{enabled:false}});
let rival=0;fails(()=>setPointLight(spot,{shadow:{get enabled(){rival=createPointLight({shadow:{enabled:true}});return true;}}}));
check(!getPointLight(spot).shadow.enabled);removePointLight(rival);
setPointLight(spot,{shadow:{enabled:true}});
