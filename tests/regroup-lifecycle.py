#!/usr/bin/env python3
"""Exercise live regrouping, visibility, removal, and script replacement on Frame."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/pocketspace'))
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if subprocess.run(['pgrep', '-x', 'sf-overlay-engi'], capture_output=True).returncode == 0:
    parser.error('an engine is already running')
source = Path('tests/scenes/auto-split.js').read_text()
exercise = '''
const originalLifecycleUpdate=update;let lifecyclePhase=-1;
update=function(){
  const age=(Date.now()-started)/1000;
  const phase=age<4?0:age<8?1:age<12?2:age<13?3:age<16?4:age<18?5:6;
  if(phase!==lifecyclePhase){lifecyclePhase=phase;print('REGROUP_LIFECYCLE '+phase);
    if(phase===1){wide=true;moveBodies(true);}
    if(phase===2){wide=false;moveBodies();}
    if(phase===3)bodies.forEach(id=>setObjectVisible(id,false));
    if(phase===4){bodies.forEach(id=>setObjectVisible(id,true));wide=true;moveBodies();}
    if(phase===5)while(bodies.length>8)removeObject(bodies.pop());
    if(phase===6)while(bodies.length)removeObject(bodies.pop());
  }
  originalLifecycleUpdate();
};
'''
def scene(models):
    return 'globalThis.splitKinematic=true;globalThis.splitModels=' + json.dumps(models) + ';\n' + source + exercise

with tempfile.TemporaryDirectory(prefix='sf-regroup-lifecycle-') as temp:
    path = Path(temp) / 'scene.js'; path.write_text(scene(False))
    env = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'}
    process = subprocess.Popen([str(args.engine.resolve()), str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    try:
        process.communicate(timeout=20)
        raise RuntimeError(f'early exit {process.returncode}')
    except subprocess.TimeoutExpired:
        path.write_text(scene(True))
        try:
            process.communicate(timeout=6)
            raise RuntimeError(f'early exit after reload {process.returncode}')
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGTERM)
            try:
                log, _ = process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.communicate(); raise RuntimeError('failed to stop')
args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(log)
if process.returncode or re.search(r'\[js\].*(?:update|load) error|\[surface\].*(?:unavailable|error=)|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log):
    raise RuntimeError('native lifecycle failed; inspect output')
if not all(f'REGROUP_LIFECYCLE {phase}' in log for phase in range(7)):
    raise RuntimeError('not all lifecycle phases ran')
if '[lifecycle] scene state reset' not in log or log.count('[js] loaded') < 2:
    raise RuntimeError('scene reload did not commit')
if not all(re.search(r'group=1 ' + op + r' tiles=', log) for op in ('split', 'merge', 'rebalance')):
    raise RuntimeError('missing structural operations')
samples = [json.loads(s.split('SPLIT_SAMPLE ', 1)[1]) for s in log.splitlines() if 'SPLIT_SAMPLE ' in s]
if not any(s['objectCount'] == 1 and s['bodyIds'] == [] for s in samples):
    raise RuntimeError('body removal did not leave only the floor')
print('PASS native regroup lifecycle: split/rebalance/merge, hide/show, half/full removal, boxes-to-models reload, normal SIGTERM cleanup')
