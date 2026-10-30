// Transactional shadow settings, single-light capacity and snapshot compatibility.
function check(v) { if (!v) throw Error('shadow assertion'); }
function fails(fn) { let caught=false;try { fn(); } catch(e) { caught=true; }check(caught); }
const owner=createSceneContainer();
const asset=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
const object=createObject(asset,0,0,0,.2,{container:owner});
const spot=createSpotLight({container:owner,objectId:object,range:3});
check(!getSpotLight(spot).shadow.enabled&&getSpotLight(spot).shadow.resolution===512);
setSpotLight(spot,{shadow:{enabled:true,resolution:256,maxTriangles:10000,maxGpuMs:4,near:.02,bias:.001,normalBias:.003}});
const initial=JSON.stringify(getSpotLight(spot));
for(const shadow of [null,true, {resolution:384},{resolution:2048},{resolution:512.5},{resolution:'512'}, {enabled:1}, {near:0},{near:4},{bias:-1},{normalBias:Infinity},{maxTriangles:0},{maxTriangles:1000001},{maxGpuMs:0},{maxGpuMs:17}]) {
  fails(()=>setSpotLight(spot,{shadow}));check(JSON.stringify(getSpotLight(spot))===initial);
}
fails(()=>setSpotLight(spot,{shadow:{get resolution(){throw Error('getter');}}}));
check(JSON.stringify(getSpotLight(spot))===initial);
fails(()=>setSpotLight(spot,{outerConeAngle:Math.PI/2}));
fails(()=>setSpotLight(spot,{range:.01}));
fails(()=>createPointLight({shadow:{enabled:true}}));
fails(()=>createDirectionalLight({shadow:{enabled:true}}));
fails(()=>createSpotLight({shadow:{enabled:true}}));
const second=createSpotLight({enabled:false,shadow:{enabled:true}});
fails(()=>setSpotLight(second,{enabled:true}));check(!getSpotLight(second).enabled);
setSpotLight(spot,{enabled:false});setSpotLight(second,{enabled:true});
fails(()=>setSpotLight(spot,{enabled:true}));setSpotLight(second,{shadow:{enabled:false}});setSpotLight(spot,{enabled:true});removeSpotLight(second);
const snapshot=captureSceneContainer(owner);check(snapshot.version===12&&snapshot.lights[0].shadow.enabled);
const before=JSON.stringify(snapshot),state=JSON.stringify(getSceneContainerState(owner));
fails(()=>restoreSceneContainer(snapshot));check(JSON.stringify(captureSceneContainer(owner))===before);
for(const mutate of [s=>delete s.lights[0].shadow,s=>s.lights[0].shadow.resolution=2048,s=>s.lights[0].shadow.maxTriangles=0,s=>s.lights[0].shadow.maxGpuMs=0,s=>s.lights[0].shadow.near=4,s=>s.lights[0].type='point']) {
  const bad=JSON.parse(before);mutate(bad);fails(()=>restoreSceneContainer(bad));check(JSON.stringify(getSceneContainerState(owner))===state);
}
setSpotLight(spot,{shadow:{enabled:false}});
const restored=restoreSceneContainer(snapshot),copy=getSpotLight(restored.lightMap[spot]);
check(copy.objectId===restored.objectMap[object]&&copy.shadow.resolution===256&&copy.shadow.enabled);
disposeSceneContainer(restored.containerId);
const legacy=JSON.parse(before);legacy.version=11;delete legacy.lights[0].shadow;
const flat=restoreSceneContainer(legacy);check(!getSpotLight(flat.lightMap[spot]).shadow.enabled);disposeSceneContainer(flat.containerId);
setSpotLight(spot,{shadow:{enabled:true}});
const removed=createSpotLight();fails(()=>setSpotLight(removed,{shadow:{get enabled(){removeSpotLight(removed);return false;}}}));
// A getter may reserve the slot, so validation must run again after it returns.
setSpotLight(spot,{shadow:{enabled:false}});
let rival=0;fails(()=>setSpotLight(spot,{shadow:{get enabled(){rival=createSpotLight({shadow:{enabled:true}});return true;}}}));
check(!getSpotLight(spot).shadow.enabled);removeSpotLight(rival);
setSpotLight(spot,{shadow:{enabled:true}});
