"""Deckard integration: repeated reload rollback, callbacks, removal and shutdown.
Run after compiling tests/tools/lifecycle-probe.cpp as /tmp/sf-lifecycle-probe.
No SteamVR restart or controller power cycling is performed by this script.
"""
from pathlib import Path
import os
import signal
import subprocess
import tempfile
import time

root=Path(__file__).resolve().parents[1]
log=Path('/tmp/lifecycle-runtime.log')
keys=['sf_overlay_engine.1','sf_overlay_engine.2','sf_overlay_engine.panel.1','sf_overlay_engine.panel.2']
def probe(extra=()):
 return subprocess.check_output(['/tmp/sf-lifecycle-probe',*keys,*extra],text=True)
def scene(generation):
 return f'''const generation={generation};
const asset=loadModel('assets/samples/Box/Box.glb',512);
let object=createObject(asset,0,0,0,.15);
enableObjectInteraction(object,{{grabButton:'both'}});
let panel=createPanel({{widthPixels:128,heightPixels:64,widthMeters:.3,interactive:true}});
panel.canvas.clear(50,80,100,255);panel.canvas.present();
const started=Date.now();let phase=0,last=-1;
onInputEvent(e=>{{}});
function update(){{
 const elapsed=Date.now()-started;
 if(phase===0&&elapsed>150){{removeObject(object);removePanel(panel);object=createObject(asset,0,0,0,.15);panel=createPanel({{widthPixels:128,heightPixels:64,widthMeters:.3}});panel.canvas.present();phase=1;}}
 if(phase===1&&elapsed>350){{removeObject(object);removePanel(panel);phase=2;print('LIFECYCLE_DRAINED '+generation);}}
 for(const e of pollInputEvents()){{if(phase===2&&e.type==='overlay.grabStart')throw Error('stale grab in drained scene');}}
 const second=Math.floor(elapsed/500);if(second!==last){{last=second;print('LIFECYCLE_ALIVE '+generation+' '+phase);}}
}}
'''
def wait_for(text,after=0,timeout=12):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  data=log.read_text() if log.exists() else ''
  if text in data[after:]:return len(data)
  assert process.poll() is None, data[-3000:]
  time.sleep(.025)
 raise AssertionError(f'missing {text}: '+data[-3000:])
def resources(pid):
 status=Path(f'/proc/{pid}/status').read_text()
 rss=next(line for line in status.splitlines() if line.startswith('VmRSS:'))
 return len(list(Path(f'/proc/{pid}/fd').iterdir())),rss

assert all(f'{key}=absent' in probe() for key in keys),'another engine may be running'
with tempfile.TemporaryDirectory(prefix='sf-lifecycle-') as tmp,log.open('w') as output:
 path=Path(tmp)/'scene.js';path.write_text(scene(0))
 process=subprocess.Popen(['stdbuf','-oL','-eL',str(root/'run-overlay-engine.sh'),str(path)],cwd=root,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
 samples=[]
 try:
  wait_for('LIFECYCLE_DRAINED 0');time.sleep(.1)
  samples.append(resources(process.pid))
  for generation in range(1,11):
   # Candidate mutates its own IDs, registers a callback, then fails. The active
   # scene must retain its callback/queue/resources and continue updating.
   start=log.stat().st_size
   path.write_text("const asset=loadModel('assets/samples/Box/Box.glb',512);const id=createObject(asset,0,0,0,.1);removeObject(id);onInputEvent(e=>print('LEAKED_CALLBACK'));pollInputEvents();throw Error('EXPECTED_ROLLBACK');")
   wait_for('EXPECTED_ROLLBACK',start)
   wait_for(f'LIFECYCLE_ALIVE {generation-1} 2',start)
   start=log.stat().st_size;path.write_text(scene(generation))
   wait_for(f'LIFECYCLE_DRAINED {generation}',start);time.sleep(.1)
   inventory=probe();assert all(f'{key}=absent' in inventory for key in keys),inventory
   samples.append(resources(process.pid))
  assert max(n for n,_ in samples)<=samples[0][0]+2,samples
 finally:
  os.killpg(process.pid,signal.SIGTERM)
  try:code=process.wait(timeout=8)
  except subprocess.TimeoutExpired:
   os.killpg(process.pid,signal.SIGKILL);process.wait();raise
 assert code==0,code
 data=log.read_text();assert 'LEAKED_CALLBACK' not in data;assert '[js] update error' not in data
 inventory=probe(['sf_overlay_engine.laser.0','sf_overlay_engine.laser.1'])
 assert '=present' not in inventory,inventory
 print('PASS lifecycle runtime: 10 failed/successful reload pairs, object/panel removal, no leaked callbacks, bounded FD count, clean SIGTERM and absent overlay keys')
 print('Resource samples:',samples)
 print('Log:',log)
