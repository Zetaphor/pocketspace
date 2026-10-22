// Headless API validation: no panels, overlays, or input capture.
let frame=0,logged=0;
const valid={gaze:0,foveation:0,left:0,right:0};
function require(condition,message){if(!condition)throw new Error(message);}
for(const bad of ['invalid',false,0]){
  let rejected=false;try{getFingerTracking(bad);}catch(error){rejected=true;}
  require(rejected,'invalid hand accepted');
}
getEyeTracking();getEyeFoveationCenters();
function update(){
  const eye=getEyeTracking(),foveation=getEyeFoveationCenters();
  require(eye.source==='openvr'&&eye.space==='scene','gaze source/space');
  require(eye.sampleAgeMs===null&&foveation.sampleAgeMs===null,'fabricated sample time');
  if(eye.valid){
    const d=eye.direction;require(Number.isFinite(d.x+d.y+d.z)&&Math.abs(Math.hypot(d.x,d.y,d.z)-1)<.001,'gaze direction');
    require(eye.active&&eye.fresh&&!eye.suppressed&&eye.sceneAppPid===0,'gaze validity');
    valid.gaze++;
  }else require(eye.origin===null&&eye.target===null&&eye.direction===null,'invalid gaze coordinates');
  if(foveation.valid){
    require(Number.isFinite(foveation.left.x+foveation.left.y+foveation.right.x+foveation.right.y),'foveation coordinates');valid.foveation++;
  }else require(foveation.left===null&&foveation.right===null,'invalid foveation coordinates');
  for(const hand of ['left','right']){
    const fingers=getFingerTracking(hand);
    require(fingers.source==='vrserver'&&fingers.hand===hand&&fingers.skeleton===null,'finger identity');
    if(fingers.valid){require(fingers.fresh&&fingers.connected&&fingers.available,'finger validity');valid[hand]++;}
    for(const field of [...Object.values(fingers.sensors),...Object.values(fingers.touches)]){
      if(!field.available||!fingers.fresh)require(field.value===null,'stale/missing field');
      if(typeof field.value==='number')require(Number.isFinite(field.value),'nonfinite sensor');
    }
    const legacy=getControllerInput(hand);
    for(const name of ['ring','pinky','thumbrest','dpad'])require(typeof legacy.capacitive[name]==='number','capacitive projection');
  }
  pollInputEvents();frame++;
  if(frame%180===0){logged++;print(`TRACKING_SMOKE frames=${frame} valid=${JSON.stringify(valid)} gaze=${eye.status} checks=passed`);}
}
print('TRACKING_SMOKE loaded; unavailable states and invalid hands checked');
