const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
function fixture(file='tests/scenes/animation-profile.js'){
  let now=0,next=1,callback;
  const actors=new Map(),logs=[];
  const context=vm.createContext({Date:{now:()=>now},
    loadModel:()=>1,getAnimations:()=>[{duration:2}],
    createObject:()=>{const id=next++;actors.set(id,{});return id;},
    removeObject:id=>assert(actors.delete(id)),
    playAnimation:id=>{actors.get(id).playing=true;},pauseAnimation:id=>{actors.get(id).playing=false;},
    createPanel:()=>({canvas:{clear(){},text(){}},present(){}}),panelSetTransform(){},
    getEngineStats:()=>({fps:90,cpuPercent:100,ramBytes:1048576,sampleId:Math.floor(now/1000)}),
    onInputEvent:fn=>callback=fn,print:text=>logs.push(text)});
  vm.runInContext(fs.readFileSync(file,'utf8'),context);
  return {actors,logs,advance(){now+=20000;vm.runInContext('update()',context);},abort(){callback({type:'controller.button',control:'/input/b/click',down:true});}};
}
const run=fixture();
for(const [i,[count,playing]] of [[8,true],[8,false],[16,false],[16,true],[32,true],[32,false]].entries()){
  assert.equal(run.actors.size,count);
  assert([...run.actors.values()].every(actor=>actor.playing===playing));
  const stage=JSON.parse(run.logs.findLast(line=>line.startsWith('ANIMATION_STAGE ')).slice('ANIMATION_STAGE '.length));
  assert.equal(stage.stage,i);assert.equal(stage.count,count);assert.equal(stage.playing,playing);
  run.advance();
}
assert.equal(run.actors.size,0);assert(run.logs.includes('ANIMATION_PROFILE_DONE'));
run.advance();assert.equal(run.actors.size,0);
const abort=fixture();abort.abort();assert.equal(abort.actors.size,0);abort.advance();assert.equal(abort.actors.size,0);assert(abort.logs.includes('ANIMATION_PROFILE_ABORT'));
console.log('PASS animation profiling scene: bounded stages, exact counts/playback, complete cleanup and abort');

const gpu=fixture('tests/scenes/gpu-skinning-profile.js');assert.equal(gpu.actors.size,32);gpu.advance();assert.equal(gpu.actors.size,32);assert([...gpu.actors.values()].every(actor=>!actor.playing));gpu.advance();assert.equal(gpu.actors.size,0);
