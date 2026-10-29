// Non-interactive analytic submission counts for the native profiler.
const assets = ['Box','Fox','InterpolationTest'].map(name =>
  loadModel(`assets/samples/${name}/${name}.glb`,768,{lod:false}));
let stage=-1,actors=[],started=Date.now();
function select(next){
  for(const id of actors)removeObject(id);actors=[];stage=next;
  if(stage===3){print('RENDER_WORK_DONE');return;}
  for(let n=0;n<3;n++){
    const id=createObject(assets[stage],(n-1)*.2,0,0,.15,{renderGroup:1});
    setObjectInteraction(id,{grabbable:false,laserPassThrough:true});
    if(stage)playAnimation(id,stage===2?1:0,{time:.1*n});actors.push(id);
  }
}
setLaserVisible('left',false);setLaserVisible('right',false);
function update(){const next=Math.min(3,Math.floor((Date.now()-started)/4000));if(next!==stage)select(next);}
select(0);
