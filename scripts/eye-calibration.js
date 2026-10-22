// Fit head-relative angular correction; validation targets never enter the fit.
if(!getEyeTrackingCalibration().outputPath)throw Error("Start with --eye-calibration-output FILE; its parent directory must exist.");
const panel=createPanel({widthPixels:1024,heightPixels:768,widthMeters:1.1,interactive:false});
panelSetTransform(panel,'world',0,0,0);panelSetVisible(panel,true);
const training=[[512,384],[819,384],[205,384],[819,220],[512,220],[205,220],[819,548],[512,548],[205,548]];
const validation=[[358,302],[666,466],[666,302],[358,466],[512,425]];
let index=0,capture=null,fit=null,done=false,lastDraw=0,message='Look at the gold circle, then tap either trigger.';
const points=[],checks=[],started=Date.now(),degrees=180/Math.PI;
getEyeTracking();
function median(a){a=a.slice().sort((x,y)=>x-y);const m=Math.floor(a.length/2);return a.length%2?a[m]:(a[m-1]+a[m])/2;}
function angles(v,b){const h=[0,0,0];for(let c=0;c<3;c++)for(let r=0;r<3;r++)h[c]+=b[r*3+c]*v[r];if(h[2]<=0)return null;return [Math.atan2(h[0],h[2]),Math.atan2(h[1],Math.hypot(h[0],h[2]))];}
function sample(g){
  if(!g.valid||!g.tracked||!g.headBasis)return null;
  const t=(index<9?training:validation)[index<9?index:index-9];
  const expected=[(t[0]/1024-.5)*1.1-g.origin.x,(.5-t[1]/768)*.825-g.origin.y,-g.origin.z];
  const raw=angles([g.direction.x,g.direction.y,g.direction.z],g.headBasis),target=angles(expected,g.headBasis);
  return raw&&target?{raw,target}:null;
}
function solve(rows,axis){
  const m=Array.from({length:3},()=>[0,0,0,0]);
  for(const p of rows){const a=[1,...p.raw];for(let i=0;i<3;i++){for(let j=0;j<3;j++)m[i][j]+=a[i]*a[j];m[i][3]+=a[i]*p.target[axis];}}
  for(let i=0;i<3;i++){let pivot=i;for(let j=i+1;j<3;j++)if(Math.abs(m[j][i])>Math.abs(m[pivot][i]))pivot=j;[m[i],m[pivot]]=[m[pivot],m[i]];if(Math.abs(m[i][i])<1e-8)throw Error('Degenerate fit. Bumper restarts.');const scale=m[i][i];for(let k=i;k<4;k++)m[i][k]/=scale;for(let j=0;j<3;j++)if(j!==i){const factor=m[j][i];for(let k=i;k<4;k++)m[j][k]-=factor*m[i][k];}}
  return m.map(row=>row[3]);
}
function corrected(raw){return [fit.yaw,fit.pitch].map(c=>c[0]+c[1]*raw[0]+c[2]*raw[1]);}
function error(a,b){const dot=Math.sin(a[1])*Math.sin(b[1])+Math.cos(a[1])*Math.cos(b[1])*Math.cos(a[0]-b[0]);return Math.acos(Math.max(-1,Math.min(1,dot)))*degrees;}
function finish(){
  const samples=capture.samples;capture=null;
  if(samples.length<30){message='Too few tracked samples. Look steadily and retry trigger.';return;}
  const p={raw:[0,1].map(i=>median(samples.map(s=>s.raw[i]))),target:[0,1].map(i=>median(samples.map(s=>s.target[i])))};
  const spread=samples.map(s=>Math.max(error(s.raw,p.raw),error(s.target,p.target))).sort((a,b)=>a-b);
  if(spread[Math.ceil(spread.length*.95)-1]>1.5){message='Gaze or head moved too much. Keep still and retry trigger.';return;}
  if(index<9)points.push(p);else checks.push({raw:error(p.raw,p.target),corrected:error(corrected(p.raw),p.target),inRange:p.raw[0]>=fit.bounds[0]&&p.raw[0]<=fit.bounds[1]&&p.raw[1]>=fit.bounds[2]&&p.raw[1]<=fit.bounds[3]});
  index++;
  if(index===9){
    try{fit={version:1,source:'openvr',model:'head-angular-affine',units:'radians',yaw:solve(points,0),pitch:solve(points,1),bounds:[Math.min(...points.map(p=>p.raw[0]))-.05,Math.max(...points.map(p=>p.raw[0]))+.05,Math.min(...points.map(p=>p.raw[1]))-.05,Math.max(...points.map(p=>p.raw[1]))+.05]};}
    catch(e){done=true;message=String(e);return;}
  }
  message='Captured. Look at the next circle; tap trigger when ready.';
  if(index===14){
    const raw=median(checks.map(p=>p.raw)),corrected=median(checks.map(p=>p.corrected)),max=Math.max(...checks.map(p=>p.corrected));
    fit.validation={targets:5,rawMedianDegrees:raw,correctedMedianDegrees:corrected,maxDegrees:max};
    print('EYE_CALIBRATION_VALIDATION '+JSON.stringify(fit.validation));done=true;
    if(!checks.every(p=>p.inRange)||corrected>2||max>5||corrected>raw+.25){message=`Validation failed: median ${corrected.toFixed(1)}, max ${max.toFixed(1)} deg. Bumper retries.`;return;}
    fit.createdAt=new Date().toISOString();
    try{const path=saveEyeTrackingCalibration(fit);message='Saved calibration. Bumper restarts.';print('EYE_CALIBRATION_SAVED '+path);}
    catch(e){message='Save failed. Check --eye-calibration-output path and runtime log.';print('EYE_CALIBRATION_SAVE_FAILED '+String(e));}
  }
}
onInputEvent(e=>{
  if(e.type!=='controller.button'||!e.down||Date.now()-started<1000)return;
  if(e.control==='/input/bumper/click'){index=0;capture=null;fit=null;done=false;points.length=0;checks.length=0;message='Restarted. Look at gold circle, then tap trigger.';}
  else if(e.control==='/input/trigger/click'&&!capture&&!done){capture={start:Date.now()+350,end:Date.now()+1350,samples:[]};message='Keep head and gaze steady. Sampling for one second.';}
});
function update(){
  const gaze=getEyeTracking(),now=Date.now();pollInputEvents();
  if(capture){if(now>=capture.start&&now<capture.end){const s=sample(gaze);if(s)capture.samples.push(s);}if(now>=capture.end)finish();}
  if(now-lastDraw<200)return;lastDraw=now;
  const c=panel.canvas;c.clear(12,18,29,255);c.text(24,20,'Eye calibration - head-relative correction',240,245,255,255,28);
  c.text(24,70,`${index<9?'Training':'Validation'} ${Math.min(index+1,14)}/14 | gaze ${gaze.status}`,180,220,255,255,22);
  c.text(24,110,'Keep head still. Look at circle. Tap either trigger.',220,230,240,255,22);
  if(!done){const t=(index<9?training:validation)[index<9?index:index-9];c.circle(t[0],t[1],25,240,195,80,255,false);c.circle(t[0],t[1],3,240,195,80,255,true);}
  c.text(24,650,message,220,220,180,255,18);c.text(24,700,'Bumper restarts the full calibration.',180,200,220,255,18);c.present();
}
print('EYE_CALIBRATION_READY 9 training + 5 validation targets; trigger captures; bumper restarts');
