const vm=require('node:vm'),fs=require('node:fs'),assert=require('node:assert/strict');
const source=fs.readFileSync('scripts/eye-calibration.js','utf8');
function run({jitter=false,badValidation=false,invalid=false}={}){
 let now=0,callback,saved=null,frame=0;
 class Clock extends Date{constructor(...args){super(...(args.length?args:[now]));}static now(){return now;}}
 const context=vm.createContext({Date:Clock,Math,JSON,Error,Number,String,getEyeTrackingCalibration:()=>({outputPath:"/tmp/example.json"}),createPanel:()=>({canvas:new Proxy({},{get:()=>()=>{}})}),panelSetTransform(){},panelSetVisible(){},print(){},pollInputEvents(){},onInputEvent(f){callback=f;},saveEyeTrackingCalibration(f){saved=JSON.parse(JSON.stringify(f));return '/tmp/example.json';},getEyeTracking(){
   const i=vm.runInContext('index',context),t=i<9?vm.runInContext('training[index]',context):vm.runInContext('validation[index-9]',context);
   if(!t)return {valid:false,status:'done'};
   const target=[(t[0]/1024-.5)*1.1,(.5-t[1]/768)*.825,1];
   let yaw=Math.atan2(target[0],1),pitch=Math.atan2(target[1],Math.hypot(target[0],1));
   yaw=(yaw-.06)/1.08;pitch=(pitch+.25)/.9;
   if(jitter)yaw+=(frame++%2?1:-1)*.1;
   if(badValidation&&i>=9)yaw+=.2;
   return {valid:!invalid,tracked:true,status:'ok',origin:{x:0,y:0,z:-1},direction:{x:Math.sin(yaw)*Math.cos(pitch),y:Math.sin(pitch),z:Math.cos(yaw)*Math.cos(pitch)},headBasis:[1,0,0,0,1,0,0,0,1]};
 }});
 vm.runInContext(source,context);now=2000;
 for(let i=0;i<14;i++){
  callback({type:'controller.button',down:true,control:'/input/trigger/click'});
  for(let f=0;f<130;f++){now+=12;vm.runInContext('update()',context);}
  if(jitter||invalid){assert.equal(vm.runInContext('index',context),0);assert.equal(saved,null);return;}
 }
 if(badValidation){assert.equal(saved,null);return;}
 assert(saved);assert.equal(saved.validation.targets,5);assert(saved.validation.maxDegrees<.001);
 assert(Math.abs(saved.yaw[0]-.06)<1e-8);assert(Math.abs(saved.pitch[2]-.9)<1e-8);
}
run();run({jitter:true});run({invalid:true});run({badValidation:true});
console.log('eye calibration generator regression passed');
