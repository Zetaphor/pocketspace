const fs=require('node:fs');
const vm=require('node:vm');
const assert=require('node:assert/strict');
let now=0,callback,logs=[],valid=true,offset=0;
const targets=[[512,384],[819,384],[205,384],[819,220],[512,220],[205,220],[819,548],[512,548],[205,548]];
let targetIndex=0;
const canvas=new Proxy({},{get:()=>()=>{}});
const context=vm.createContext({Math,JSON,Number,Date:{now:()=>now},
  createPanel:()=>({canvas}),panelSetTransform(){},panelSetVisible(){},pollInputEvents(){},
  onInputEvent:fn=>callback=fn,print:s=>logs.push(s),
  getEyeTracking:()=>{
    const [px,py]=targets[Math.min(targetIndex,8)];
    const v=[((px+offset)/1024-.5)*1.1,(.5-py/768)*.825,1];const length=Math.hypot(...v);
    return {valid,tracked:true,status:valid?'valid':'invalid',origin:{x:0,y:0,z:-1},direction:{x:v[0]/length,y:v[1]/length,z:v[2]/length}};
  }});
vm.runInContext(fs.readFileSync('scripts/gaze-accuracy-test.js','utf8'),context);
function capture(){
  callback({type:'controller.button',down:true,control:'/input/trigger/click'});
  for(let i=0;i<140;i++){now+=10;vm.runInContext('update()',context);}
}
now=1100;valid=false;capture();
assert(logs.some(s=>s.includes('GAZE_ACCURACY_RETRY')));assert(!logs.some(s=>s.startsWith('GAZE_ACCURACY_POINT')));
valid=true;
for(targetIndex=0;targetIndex<9;targetIndex++)capture();
const points=logs.filter(s=>s.startsWith('GAZE_ACCURACY_POINT')).map(s=>JSON.parse(s.slice('GAZE_ACCURACY_POINT '.length)));
assert.equal(points.length,9);assert(logs.some(s=>s.startsWith('GAZE_ACCURACY_COMPLETE')));
for(const point of points){assert(point.samples>=30);assert(Math.abs(point.errorPixels.x)<1e-8);assert(Math.abs(point.errorPixels.y)<1e-8);assert(point.medianErrorDegrees<1e-5);}
callback({type:'controller.button',down:true,control:'/input/bumper/click'});
targetIndex=0;offset=100;capture();
const shifted=JSON.parse(logs.filter(s=>s.startsWith('GAZE_ACCURACY_POINT')).at(-1).slice('GAZE_ACCURACY_POINT '.length));
assert(Math.abs(shifted.errorPixels.x-100)<1e-8);assert(shifted.medianErrorDegrees>1);
console.log('PASS gaze accuracy: exact targets, 100px bias, invalid sample retry, nine-point completion, restart');
