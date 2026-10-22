// Gaze is suppressed while any primary scene app runs. Raw fingers remain global.
const panel=createPanel({widthPixels:1024,heightPixels:768,widthMeters:1.1,interactive:false});
panelSetTransform(panel,'world',0,0,0);
panelSetVisible(panel,true);
let elapsed=0,lastDraw=-1,lastLog=-1;
const counts={gaze:0,foveation:0,left:0,right:0};
function require(condition,message){if(!condition)throw new Error(message);}
for(const bad of ['other',17]){
  let rejected=false;try{getFingerTracking(bad);}catch(error){rejected=true;}
  require(rejected,'getFingerTracking must reject invalid hands');
}
const initialEye=getEyeTracking();
require(initialEye.source==='openvr'&&(initialEye.valid||initialEye.origin===null),'invalid gaze must be unavailable');
const initialFoveation=getEyeFoveationCenters();
require(initialFoveation.valid||initialFoveation.left===null,'invalid foveation must be unavailable');
const initialFinger=getFingerTracking('left');
require(initialFinger.source==='vrserver'&&(initialFinger.fresh||initialFinger.sensors.ring.value===null),'stale fingers must be unavailable');
print('TRACKING_API unavailable values and invalid-hand checks passed');
function format(field){return field.value===null?'--':typeof field.value==='boolean'?String(field.value):field.value.toFixed(3);}
function update(dt){
  elapsed+=dt;
  const gaze=getEyeTracking(),foveation=getEyeFoveationCenters();
  const left=getFingerTracking('left'),right=getFingerTracking('right');
  pollInputEvents();
  counts.gaze+=+gaze.valid;counts.foveation+=+foveation.valid;counts.left+=+left.valid;counts.right+=+right.valid;
  if(gaze.valid){
    const d=gaze.direction;
    require(Number.isFinite(d.x+d.y+d.z)&&Math.abs(Math.hypot(d.x,d.y,d.z)-1)<.001,'gaze direction must be normalized');
    require(gaze.sampleAgeMs===null&&gaze.pollAgeMs!==null,'poll time must not pretend to be sample time');
    require(!gaze.suppressed&&gaze.sceneAppPid===0,'gaze must be suppressed during a scene app');
  }else require(gaze.origin===null&&gaze.target===null&&gaze.direction===null,'invalid gaze must not leak coordinates');
  for(const hand of [left,right]){
    require(hand.skeleton===null,'raw fingers must not claim skeleton data');
    for(const field of [...Object.values(hand.sensors),...Object.values(hand.touches)])
      if(!field.available||!hand.fresh)require(field.value===null,'missing/stale finger values must be null');
  }
  const logTick=Math.floor(elapsed/2);
  if(logTick!==lastLog){
    lastLog=logTick;
    print(`TRACKING_API counts=${JSON.stringify(counts)} gaze=${gaze.status} tracked=${gaze.tracked} leftRing=${format(left.sensors.ring)} leftPinky=${format(left.sensors.pinky)} rightRing=${format(right.sensors.ring)} rightPinky=${format(right.sensors.pinky)}`);
  }
  const drawTick=Math.floor(elapsed*10);if(drawTick===lastDraw)return;lastDraw=drawTick;
  const c=panel.canvas;c.clear(12,18,29,255);
  c.text(24,16,'Eye gaze and raw finger sensors',240,245,255,255,30);
  c.text(24,65,`Gaze: ${gaze.status} tracked=${gaze.tracked} / foveation=${foveation.valid}`,180,220,255,255,23);
  c.text(24,105,'Look at each circle; keep your head still.',220,230,240,255,23);
  c.text(24,140,'Green dot = gaze ray on this panel. Raw sensors are not curls.',180,200,220,255,20);
  for(const x of [205,512,819]){c.circle(x,220,28,220,180,90,255,false);c.circle(x,220,3,220,180,90,255,true);}
  if(gaze.valid&&gaze.direction.z>1e-5){
    const distance=-gaze.origin.z/gaze.direction.z;
    const x=(gaze.origin.x+distance*gaze.direction.x)/1.1*1024+512;
    const y=384-(gaze.origin.y+distance*gaze.direction.y)/(.825)*768;
    if(distance>0&&x>=0&&x<1024&&y>=0&&y<768)c.circle(x,y,10,80,255,130,255,true);
  }
  let y=300;
  for(const hand of [left,right]){
    c.text(24,y,`${hand.hand}: valid=${hand.valid} fresh=${hand.fresh} age=${hand.ageMs===null?'--':hand.ageMs.toFixed(0)}ms`,230,240,255,255,24);
    c.text(24,y+42,`ring ${format(hand.sensors.ring)} / pinky ${format(hand.sensors.pinky)} / grip ${format(hand.sensors.grip)}`,200,220,255,255,23);
    c.text(24,y+80,`trigger ${format(hand.sensors.trigger)} / bumper ${format(hand.sensors.bumper)} / thumbrest ${format(hand.sensors.thumbrest)}`,200,220,255,255,21);
    c.text(24,y+116,`handgrip touch: ${format(hand.touches.handgrip1)} / ${format(hand.touches.handgrip2)}`,200,220,255,255,21);
    y+=180;
  }
  c.text(24,710,'Lift and curl ring/pinky separately; missing values show --.',220,200,160,255,22);
  c.present();
}
