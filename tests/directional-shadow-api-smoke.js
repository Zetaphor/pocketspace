// Directional coverage, shared shadow capacity and transactional persistence.
function check(v){if(!v)throw Error('directional shadow assertion');}
function fails(fn){let caught=false;try{fn();}catch(error){caught=true;}check(caught);}
const owner=createSceneContainer();
const asset=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
const object=createObject(asset,0,0,0,.2,{container:owner});
const sun=createDirectionalLight({container:owner,objectId:object,shadow:{enabled:true,center:[1,2,3],size:3,depth:5}});
check(getDirectionalLight(sun).shadow.size===3&&getDirectionalLight(sun).shadow.near===undefined);
const before=JSON.stringify(getDirectionalLight(sun));
for(const shadow of [null,{center:[1,2]},{center:[0,Infinity,0]},{center:[0,0,10001]},{size:0},{size:65},{depth:NaN},{depth:'4'},{near:.1},{enabled:1},{resolution:384}]){
 fails(()=>setDirectionalLight(sun,{shadow}));check(JSON.stringify(getDirectionalLight(sun))===before);
}
fails(()=>setDirectionalLight(sun,{shadow:{get center(){throw Error('getter');}}}));check(JSON.stringify(getDirectionalLight(sun))===before);
fails(()=>createSpotLight({shadow:{enabled:true}}));
fails(()=>createDirectionalLight({shadow:{enabled:true}}));
fails(()=>createSpotLight({shadow:{center:[0,0,0]}}));
const spot=createSpotLight({enabled:false,shadow:{enabled:true}});
fails(()=>setSpotLight(spot,{enabled:true}));
setDirectionalLight(sun,{enabled:false});setSpotLight(spot,{enabled:true});
fails(()=>setDirectionalLight(sun,{enabled:true}));setSpotLight(spot,{enabled:false});setDirectionalLight(sun,{enabled:true});removeSpotLight(spot);
const saved=captureSceneContainer(owner);check(saved.version===14&&saved.lights[0].shadow.depth===5);
fails(()=>restoreSceneContainer(saved));
setDirectionalLight(sun,{shadow:{enabled:false}});
const state=JSON.stringify(getSceneContainerState(owner));
for(const mutate of [s=>delete s.lights[0].shadow,s=>delete s.lights[0].shadow.center,s=>s.lights[0].shadow.depth=0,s=>s.lights[0].shadow.size=65,s=>s.lights[0].shadow.center=[0,0,Infinity],s=>s.lights[0].shadow.near=.1,s=>s.version=12]){
 const bad=JSON.parse(JSON.stringify(saved));mutate(bad);fails(()=>restoreSceneContainer(bad));check(JSON.stringify(getSceneContainerState(owner))===state);
}
const restored=restoreSceneContainer(saved),copy=getDirectionalLight(restored.lightMap[sun]);
check(copy.objectId===restored.objectMap[object]&&copy.shadow.enabled&&copy.shadow.center.join()=== '1,2,3'&&copy.shadow.size===3&&copy.shadow.depth===5);
disposeSceneContainer(restored.containerId);
const legacy=JSON.parse(JSON.stringify(saved));legacy.version=12;delete legacy.lights[0].shadow;
const old=restoreSceneContainer(legacy);check(!getDirectionalLight(old.lightMap[sun]).shadow.enabled);disposeSceneContainer(old.containerId);
// Getter mutations are revalidated before either source or slot is published.
let rival=0;fails(()=>setDirectionalLight(sun,{shadow:{get enabled(){rival=createSpotLight({shadow:{enabled:true}});return true;}}}));
check(!getDirectionalLight(sun).shadow.enabled);removeSpotLight(rival);
const removed=createDirectionalLight();fails(()=>setDirectionalLight(removed,{shadow:{get enabled(){removeDirectionalLight(removed);return false;}}}));
setDirectionalLight(sun,{shadow:{enabled:true}});
