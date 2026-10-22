// Measure raw gaze accuracy without changing calibration or displaying a moving dot.
const width=1024,height=768,widthMeters=1.1,heightMeters=.825;
const panel=createPanel({widthPixels:width,heightPixels:height,widthMeters,interactive:false});
panelSetTransform(panel,'world',0,0,0);panelSetVisible(panel,true);
const targets=[
  ['center',512,384],['right',819,384],['left',205,384],
  ['top-right',819,220],['top-center',512,220],['top-left',205,220],
  ['bottom-right',819,548],['bottom-center',512,548],['bottom-left',205,548]
];
const results=[];
let index=0,capture=null,message='Look at the gold circle, then tap either trigger.',lastDraw=0;
const started=Date.now();
getEyeTracking();
function median(values){const sorted=values.slice().sort((a,b)=>a-b);const n=sorted.length;return n%2?sorted[(n-1)/2]:(sorted[n/2-1]+sorted[n/2])/2;}
function record(gaze){
  if(!gaze.valid||!gaze.tracked||gaze.direction.z<=1e-5)return null;
  const distance=-gaze.origin.z/gaze.direction.z;if(distance<=0)return null;
  const x=(gaze.origin.x+distance*gaze.direction.x)/widthMeters*width+width/2;
  const y=height/2-(gaze.origin.y+distance*gaze.direction.y)/heightMeters*height;
  const target=targets[index];
  const expected={x:(target[1]/width-.5)*widthMeters-gaze.origin.x,
    y:(.5-target[2]/height)*heightMeters-gaze.origin.y,z:-gaze.origin.z};
  const length=Math.hypot(expected.x,expected.y,expected.z);
  if(length<1e-6||!Number.isFinite(x+y+length))return null;
  const cosine=(expected.x*gaze.direction.x+expected.y*gaze.direction.y+expected.z*gaze.direction.z)/length;
  return {x,y,errorDegrees:Math.acos(Math.max(-1,Math.min(1,cosine)))*180/Math.PI};
}
function finish(){
  const samples=capture.samples;capture=null;
  if(samples.length<30){message=`Only ${samples.length} valid samples. Look steadily and retry trigger.`;print(`GAZE_ACCURACY_RETRY target=${targets[index][0]} samples=${samples.length}`);return;}
  const target=targets[index],x=median(samples.map(s=>s.x)),y=median(samples.map(s=>s.y));
  const spread=samples.map(s=>Math.hypot(s.x-x,s.y-y)).sort((a,b)=>a-b);
  const result={target:target[0],targetPixels:{x:target[1],y:target[2]},samples:samples.length,
    observedPixels:{x,y},errorPixels:{x:x-target[1],y:y-target[2]},
    medianErrorDegrees:median(samples.map(s=>s.errorDegrees)),spread95Pixels:spread[Math.ceil(spread.length*.95)-1]};
  if(result.spread95Pixels>50){
    message=`Unstable gaze (${result.spread95Pixels.toFixed(0)}px spread). Look steadily; retry trigger.`;
    print(`GAZE_ACCURACY_REJECTED ${JSON.stringify(result)}`);return;
  }
  results.push(result);print(`GAZE_ACCURACY_POINT ${JSON.stringify(result)}`);index++;
  message=index<targets.length?'Saved. Look at the next gold circle; tap trigger when ready.':'Complete. Results saved in the runtime log. Bumper restarts.';
  if(index===targets.length)print(`GAZE_ACCURACY_COMPLETE ${JSON.stringify(results)}`);
}
onInputEvent(event=>{
  if(event.type!=='controller.button'||!event.down||Date.now()-started<1000)return;
  if(event.control==='/input/bumper/click'){
    capture=null;index=0;results.length=0;message='Restarted. Look at gold circle, then tap trigger.';print('GAZE_ACCURACY_RESTART');
  }else if(event.control==='/input/trigger/click'&&!capture&&index<targets.length){
    capture={start:Date.now()+350,end:Date.now()+1350,samples:[]};
    message='Keep looking at the gold circle. Sampling for one second.';
  }
});
function update(){
  const gaze=getEyeTracking(),now=Date.now();pollInputEvents();
  if(capture){
    if(now>=capture.start&&now<capture.end){const sample=record(gaze);if(sample)capture.samples.push(sample);}
    if(now>=capture.end)finish();
  }
  if(now-lastDraw<200)return;lastDraw=now;
  const c=panel.canvas;c.clear(12,18,29,255);
  c.text(24,16,'Raw gaze accuracy - no correction applied',240,245,255,255,28);
  c.text(24,60,`Point ${Math.min(index+1,targets.length)}/${targets.length} - gaze ${gaze.status}`,180,220,255,255,22);
  c.text(24,100,'Keep head still. Look at gold circle. Tap either trigger.',220,230,240,255,22);
  if(index<targets.length){const t=targets[index];c.circle(t[1],t[2],25,240,195,80,255,false);c.circle(t[1],t[2],3,240,195,80,255,true);}
  else{
    for(let i=0;i<results.length;i++)c.text(120,180+i*43,`${results[i].target}: ${results[i].medianErrorDegrees.toFixed(2)} deg`,220,230,240,255,24);
  }
  c.text(24,650,message,220,220,180,255,19);
  c.text(24,695,`Saved ${results.length}/9. Bumper restarts. No live dot to chase.`,180,200,220,255,20);
  c.present();
}
print('GAZE_ACCURACY_READY nine targets; trigger captures after 350 ms settle for 1 second; bumper restarts');
