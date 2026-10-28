function check(value) { if (!value) throw Error('container API assertion'); }
function rejects(fn) { let failed=false;try{fn();}catch(_){failed=true;}check(failed); }
const model=loadModel('assets/samples/Box/Box.glb');
const first=createSceneContainer(),second=createSceneContainer();
const owned=createObject(model,0,0,0,.1,{container:first,renderGroup:1});
const shared=createObject(model,.2,0,0,.1,{container:second,renderGroup:1});
const root=createObject(model,.4,0,0,.1);
enableObjectInteraction(owned,{grabButton:'both'});
setObjectPhysics(owned);setObjectPhysics(shared);
const joint=createPhysicsJoint(owned,shared);
const panel=createPanel({widthPixels:8,heightPixels:8,widthMeters:.1,container:first,interactive:true});
const kept=createPanel({widthPixels:8,heightPixels:8,widthMeters:.1,container:first});
check(getSceneContainerState(first).objectIds[0]===owned);
check(getSceneContainerState(first).panelIds.length===2);
setPanelContainer(kept,second);
onInputEvent(()=>{}, {container:first});check(getSceneContainerState(first).inputCallback);
for(const bad of [-1,1.5,'1',null,{},Infinity,999]) {
  rejects(()=>createObject(model,0,0,0,.1,{container:bad}));
  rejects(()=>createPanel({container:bad}));
  rejects(()=>setObjectContainer(root,bad));rejects(()=>setPanelContainer(kept,bad));
  rejects(()=>onInputEvent(()=>{}, {container:bad}));
}
check(getSceneContainerState(first).inputCallback);
rejects(()=>createObject(model,0,0,0,.1,{get container(){throw Error('getter');}}));
rejects(()=>createPanel({get container(){throw Error('getter');}}));
disposeSceneContainer(first);disposeSceneContainer(first);
check(getSceneContainerState(first).disposed);
check(getSceneContainerState(first).objectIds.length===0&&!getSceneContainerState(first).inputCallback);
check(getPhysicsJoint(joint)===null);check(getObjectPhysics(shared)!==null);
rejects(()=>setObjectPosition(owned,0,0,0));panelSetVisible(panel,true);
// panelSetVisible preserves its legacy unknown-ID no-op; other panel APIs reject.
rejects(()=>panelSetTransform(panel,'world',0,0,0));
rejects(()=>createObject(model,0,0,0,.1,{container:first}));
setObjectContainer(shared,0);setPanelContainer(kept,0);disposeSceneContainer(second);
setObjectPosition(shared,.3,0,0);panelSetTransform(kept,'world',0,0,0);
check(loadModel('assets/samples/Box/Box.glb')===model);
const removed=createSceneContainer();const temp=createObject(model,0,0,0,.1,{container:removed});removeObject(temp);
check(getSceneContainerState(removed).objectIds.length===0);disposeSceneContainer(removed);
for(const id of [0,999,-1,'1']) {rejects(()=>disposeSceneContainer(id));rejects(()=>getSceneContainerState(id));}
const invalidated=createSceneContainer();
rejects(()=>createObject(model,0,0,0,.1,{container:invalidated,get renderGroup(){disposeSceneContainer(invalidated);return 0;}}));
const invalidPanel=createSceneContainer();
rejects(()=>createPanel({container:invalidPanel,get widthPixels(){disposeSceneContainer(invalidPanel);return 8;}}));
rejects(()=>setPanelContainer({get id(){throw Error('panel getter');}},0));
const limit=Array.from({length:256},()=>createSceneContainer());
rejects(()=>createSceneContainer());for(const id of limit)disposeSceneContainer(id);
// Keep one live owner/callback for native dispatch and rollback checks.
const callbackOwner=createSceneContainer();globalThis.calls=0;
onInputEvent(()=>{calls++;disposeSceneContainer(callbackOwner);},{container:callbackOwner});
function update() {}
