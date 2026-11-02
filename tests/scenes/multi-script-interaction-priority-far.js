setInputPriority(5);
// Headset fixture: far model and right panel share local IDs with the near scene.
const asset=loadModel('assets/samples/Box/Box.glb');
const box=createObject(asset,.04,0,.12,.24);
setObjectInteraction(box,{grabbable:true,directGrab:true});
setLaserVisible('left',true);setLaserVisible('right',true);
const panel=createPanel({widthPixels:512,heightPixels:192,widthMeters:.42,interactive:true});
panelSetTransform(panel,'viewerFacing',.12,-.32,.1);
let clicks=0,last='none',elapsed=0;
onInputEvent(e=>{
  if(e.type==='panel.pointerDown'&&e.objectId===panel.id){clicks++;last=`click ${e.hand}`;print(`[far] panel click hand=${e.hand} count=${clicks}`);}
  if(e.type==='overlay.grabStart'&&e.objectId===box){last=`grab ${e.hand}`;print(`[far] grab start hand=${e.hand}`);}
  if(e.type==='overlay.grabEnd'&&e.objectId===box)print(`[far] grab end hand=${e.hand}`);
});
function update(dt){elapsed+=dt;if(elapsed<.2)return;elapsed=0;const c=panel.canvas;c.clear(170,75,40,245);c.text(20,22,'FAR / ORANGE',255,255,255,255,34);c.text(20,78,`clicks ${clicks}  last ${last}`,255,255,255,255,22);c.present();}
