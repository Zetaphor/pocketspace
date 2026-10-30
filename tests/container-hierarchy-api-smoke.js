// Nested cleanup, reparenting and subtree snapshots through real API bindings.
function check(v) { if (!v) throw Error('hierarchy assertion'); }
function fails(fn) { let caught=false;try{fn();}catch(e){caught=true;}check(caught); }
const model=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
const parent=createSceneContainer(),child=createSceneContainer({parent}),grand=createSceneContainer({parent:child});
check(getSceneContainerState(parent).childIds[0]===child&&getSceneContainerState(grand).parentId===child);
const rootObject=createObject(model,0,0,0,.1,{container:parent});
const object=createObject(model,.2,0,0,.1,{container:grand});
setObjectPhysics(rootObject);setObjectPhysics(object);createPhysicsJoint(rootObject,object);
const panel=createPanel({widthPixels:8,heightPixels:8,container:child});
const light=createDirectionalLight({container:child,objectId:object});
onInputEvent(()=>{}, {container:grand});
for(const call of [()=>setSceneContainerParent(parent,grand),()=>setSceneContainerParent(child,child),()=>setSceneContainerParent(0,parent),()=>createSceneContainer({parent:999}),()=>setSceneContainerParent(child,999)])fails(call);
check(getSceneContainerState(child).parentId===parent);
const snapshot=captureSceneContainer(parent);check(snapshot.version===12&&snapshot.containers.length===3&&snapshot.objects.length===2&&snapshot.panels.length===1);
fails(()=>captureSceneContainer(child)); // The joint crosses this smaller subtree.
const copy=restoreSceneContainer(snapshot);
check(copy.containerIds.length===3&&copy.containerMap[parent]===copy.containerId);
check(getSceneContainerState(copy.containerMap[grand]).parentId===copy.containerMap[child]);
check(getSceneContainerState(copy.containerMap[grand]).objectIds[0]===copy.objectMap[object]);
check(getSceneContainerState(copy.containerMap[child]).panelIds[0]===copy.panelMap[panel.id]);
check(getDirectionalLight(copy.lightMap[light]).objectId===copy.objectMap[object]);
disposeSceneContainer(copy.containerId);check(copy.containerIds.every(id=>getSceneContainerState(id).disposed));
for(const mutate of [s=>s.containers[1].parent=s.containers[1].id,s=>s.containers[1].parent=999,s=>s.containers.push(s.containers[0]),s=>s.rootContainer=999,s=>s.objects[0].container=999,s=>s.panels[0].container=999,s=>s.lights[0].container=999,s=>delete s.containers]){
 const bad=JSON.parse(JSON.stringify(snapshot));mutate(bad);fails(()=>restoreSceneContainer(bad));check(JSON.stringify(captureSceneContainer(parent))===JSON.stringify(snapshot));
}
saveSceneContainer(parent,'hierarchy');const saved=loadSceneContainer('hierarchy');check(saved.containerIds.length===3);disposeSceneContainer(saved.containerId);
const legacy=JSON.parse(JSON.stringify(snapshot));legacy.version=10;delete legacy.containers;delete legacy.rootContainer;
const flat=restoreSceneContainer(legacy);check(flat.containerIds.length===1&&getSceneContainerState(flat.containerId).objectIds.length===2);disposeSceneContainer(flat.containerId);
const elsewhere=createSceneContainer();setSceneContainerParent(child,elsewhere);disposeSceneContainer(parent);
check(!getSceneContainerState(child).disposed&&getSceneContainerState(parent).disposed);
setSceneContainerParent(child,0);disposeSceneContainer(elsewhere);check(!getSceneContainerState(child).disposed);
disposeSceneContainer(child);check(getSceneContainerState(grand).disposed);
fails(()=>getDirectionalLight(light));fails(()=>setObjectPosition(object,0,0,0));
check(!getSceneContainerState(grand).inputCallback);
const doomed=createSceneContainer();fails(()=>createSceneContainer({get parent(){disposeSceneContainer(doomed);return doomed;}}));
const before=createSceneContainer();disposeSceneContainer(before);
fails(()=>restoreSceneContainer({...snapshot,containers:[]}));
check(createSceneContainer()===before+1);

// A deep hierarchy also exercises subtree capacity checks and bounded traversal.
const chain=[];while(chain.length<250){chain.push(createSceneContainer({parent:chain.length?chain[chain.length-1]:0}));}
fails(()=>restoreSceneContainer(captureSceneContainer(chain[0])));
// Dispose a deep hierarchy without recursive stack traversal.
disposeSceneContainer(chain[0]);check(chain.every(id=>getSceneContainerState(id).disposed));
