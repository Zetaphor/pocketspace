const owner=createSceneContainer();
function check(v,message='panel persistence assertion'){if(!v)throw Error(message);}
function fails(fn){let caught=false;try{fn();}catch(_){caught=true;}check(caught,'expected rejection');}
const object=createObject(loadModel('assets/samples/Box/Box.glb'),0,0,0,.1,{container:owner});
const panels=['world','headLocked','viewerFacing','arbitrary'].map((mode,n)=>{
  const p=createPanel({widthPixels:n===0?1024:32,heightPixels:n===0?512:16,widthMeters:.4,interactive:n===0,container:owner});
  p.canvas.clear(10+n,20,30,n===1?120:255);
  panelSetTransform(p,mode,n*.1,.2,.5);panelSetRotation(p,0,-1,0,1,0,0,0,0,1);
  if(n===0){check(p.canvas.text(12,12,'Saved pixels',255,255,255,255,24));check(p.canvas.image(12,60,'assets/panel-icon.png'));p.canvas.line(0,0,20,20,255,0,0,128);p.canvas.circle(100,100,20,0,255,0,255,true);}
  if(n===1)for(let y=0;y<16;y++)for(let x=0;x<32;x++)p.canvas.rect(x,y,1,1,x*7,y*11,91,128,true);
  if(n===2){panelSetVisible(p,false);panelLoadFont(p,'/missing-font-is-not-needed-to-display-pixels.ttf',18);}
  p.present();return p;
});
const snapshot=captureSceneContainer(owner);check(snapshot.version===10&&snapshot.panels.length===4);
check(snapshot.panels[0].pixels.encoding==='rgba8-rle-base64');check(snapshot.panels[1].pixels.encoding==='rgba8-base64');
saveSceneContainer(owner,'panels');const copy=loadSceneContainer('panels');check(copy.panels.length===4&&copy.panelIds.length===4);
const again=captureSceneContainer(copy.containerId);
for(let n=0;n<4;n++){
  const a=snapshot.panels[n],b=again.panels[n];check(copy.panelMap[a.id]===b.id&&copy.panels[n].id===b.id);
  for(const key of Object.keys(a))if(key!=='id')check(JSON.stringify(a[key])===JSON.stringify(b[key]),key);
}
copy.panels[1].canvas.rect(0,0,4,4,255,0,0,255,true);copy.panels[1].present();
let clicks=0;
onInputEvent(event=>{if(event.type==='panel.pointerDown'&&event.objectId===copy.panelIds[0]){clicks++;copy.panels[0].canvas.rect(0,0,5,5,0,255,0,255,true);copy.panels[0].present();}});
function bad(edit){const s=JSON.parse(JSON.stringify(snapshot));edit(s);fails(()=>restoreSceneContainer(s));}
bad(s=>s.version=11);bad(s=>s.version=3);bad(s=>delete s.panels);bad(s=>s.panels[1].id=s.panels[0].id);
bad(s=>s.panels[0].widthPixels=0);bad(s=>s.panels[0].heightPixels=4097);bad(s=>s.panels[0].widthMeters=0);
bad(s=>s.panels[0].rotation=[1]);bad(s=>s.panels[0].position.x=null);bad(s=>s.panels[0].mode='invalid');
bad(s=>{s.panels[0].widthMeters=1e-100;s.panels[0].heightMeters=5e-101;});bad(s=>s.panels[0].heightMeters=0);bad(s=>s.panels[0].fontPixels=-1);bad(s=>s.panels[0].visible=1);bad(s=>s.panels[0].pixels.encoding='other');
bad(s=>s.panels[0].pixels.data='!AAA');bad(s=>s.panels[0].pixels.data='AAAA====');
bad(s=>s.panels[0].pixels.data='AAAAAAAAAAA='); // zero run
bad(s=>s.panels[0].pixels.data='/////wAAAAA='); // run exceeds dimensions
bad(s=>s.panels[1].pixels.data='AAAA'); // raw length mismatch
bad(s=>s.panels=Array(33).fill(s.panels[0]));
{const extra=[];for(let n=0;n<29;n++)extra.push(createPanel({widthPixels:1,heightPixels:1,widthMeters:.1,container:owner}));fails(()=>captureSceneContainer(owner));for(const p of extra)removePanel(p);}
for(let n=0;n<10;n++){const c=restoreSceneContainer(snapshot);disposeSceneContainer(c.containerId);}
// Handle construction must not call user-defined inherited setters mid-restore.
{const keys=['id','canvas','present','text'];try{
  for(const key of keys)Object.defineProperty(Object.prototype,key,{configurable:true,set(){throw Error('inherited setter');}});
  const c=restoreSceneContainer(snapshot);c.panels[0].canvas.clear(1,2,3,255);c.panels[0].present();disposeSceneContainer(c.containerId);
}finally{for(const key of keys)delete Object.prototype[key];}}
// Legacy v3 saves have no panels and still restore their objects.
{const s=JSON.parse(JSON.stringify(snapshot));s.version=3;delete s.panels;const c=restoreSceneContainer(s);check(c.panels.length===0&&c.objectIds.length===1);disposeSceneContainer(c.containerId);}
globalThis.panelSnapshot=snapshot;globalThis.panelOwner=owner;globalThis.panelCopy=copy;
function update(){}
