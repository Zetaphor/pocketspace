#!/usr/bin/env python3
"""Bounded prepared-program restore, unknown-state, failed/successful reload check."""
from engine_launch import catalog_arguments
import argparse, json, os, re, signal, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine',type=Path,default=Path('build/gpu-sync-experiment'))
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
for name in ('pocketspace','gpu-sync-experi'):
 if subprocess.run(['pgrep','-x',name],capture_output=True).returncode==0:p.error('an engine is already running')
source='''
const asset=loadModel('assets/samples/ToyCar/ToyCar.glb',1536,{pbr:true,optimizeVertexCache:true});
let tool=createSceneContainer();
const object=createObject(asset,0,0,0,.3,{container:tool,renderGroup:1});
for(let i=0;i<4;i++)createPointLight({container:tool,position:[(i-1.5)*.2,.2,-.4],intensity:.1,range:2});
setLaserVisible('left',false);setLaserVisible('right',false);
let phase=-1;const started=Date.now();
function update(){const next=Math.min(2,Math.floor((Date.now()-started)/2000));if(next===phase)return;phase=next;
 if(phase===1){const snapshot=captureSceneContainer(tool);const restored=restoreSceneContainer(snapshot);disposeSceneContainer(tool);tool=restored.containerId;print('VARIANT_RESTORED');}
 if(phase===2){setModelLighting(asset,{environment:'assets/environments/studio.psenv',environmentIntensity:1});const more=loadModel('assets/samples/NormalTangentTest/NormalTangentTest.glb',1536,{pbr:true});createObject(more,.3,0,0,.2,{container:tool,renderGroup:1});print('VARIANT_UNKNOWN_STATE');}
}
'''
env={k:v for k,v in os.environ.items() if not k.startswith('SF_GPU_')}
env.update(DISPLAY=os.environ.get('DISPLAY',':0'),EGL_PLATFORM='x11',SF_GPU_PBR_VARIANTS='1',SF_GPU_SYNC_MODE='finish')
with tempfile.TemporaryDirectory(prefix='pbr-variant-lifecycle-') as folder:
 path=Path(folder)/'scene.js';path.write_text(source)
 proc=subprocess.Popen([str(a.engine.resolve()),*catalog_arguments(path)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 try:
  for seconds,content in ((6,"throw Error('expected variant reload failure');"),(3,source),(6,None)):
   try:
    log,_=proc.communicate(timeout=seconds);raise RuntimeError(f'early exit {proc.returncode}: {log[-1500:]}')
   except subprocess.TimeoutExpired:
    if content is not None:path.write_text(content)
  proc.send_signal(signal.SIGTERM)
  try:log,_=proc.communicate(timeout=10)
  except subprocess.TimeoutExpired:proc.kill();proc.communicate();raise RuntimeError('shutdown failed')
 finally:
  if proc.poll() is None:proc.terminate();proc.communicate(timeout=10)
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(log)
assert proc.returncode==0 and 'grouped_buffer_bytes=0 ' in log
assert log.count('VARIANT_RESTORED')==2 and log.count('VARIANT_UNKNOWN_STATE')==2
assert 'expected variant reload failure' in log and log.count('[lifecycle] scene state reset')==1
prepares=re.findall(r'PBR_VARIANTS_PREPARE generation=(\d+) programs=(\d+) compile_ms=([\d.]+)',log)
assert len(prepares)==2 and [int(x[0]) for x in prepares]==[1,2] and all(0<int(x[1])<=16 for x in prepares)
assert not re.search(r'\[js\] update error|\[surface\].*error=|\[physics\]',log)
samples=[json.loads(line[len('GPU_SYNC_SAMPLE '):]) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
assert samples and all(not s['glError'] and not s['disjointSamples'] and not s['fenceFailures'] for s in samples)
cache=re.search(r'PBR_VARIANTS_CACHE programs=(\d+) binary_bytes=(\d+) compile_ms=([\d.]+) fallback_draws=(\d+)',log)
assert cache and int(cache[4])>0 and float(cache[3])==float(prepares[-1][2])
print('PASS prepared variants: restore, unknown-state fallback, failed reload retains cache, successful reload prepares, no subsequent compilation, clean shutdown')
