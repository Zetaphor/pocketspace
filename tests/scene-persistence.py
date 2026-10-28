#!/usr/bin/env python3
"""Run bounded live save/restore cycles using an isolated state directory."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine',type=Path,default=Path('build/sf-overlay-engine'))
p.add_argument('--output',type=Path,required=True)
mode=p.add_mutually_exclusive_group()
mode.add_argument('--physics',action='store_true',help='exercise physics bodies, joints, motors and moving simulation')
mode.add_argument('--animation',action='store_true',help='exercise skin/rigid playback and pose resource lifetime')
a=p.parse_args()
owned=3 if a.animation else 5 if a.physics else 4
scene='animation-persistence' if a.animation else 'physics-persistence' if a.physics else 'scene-persistence'
if subprocess.run(['pgrep','-x',a.engine.name[:15]],capture_output=True).returncode==0:
    p.error('an engine is already running')
a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sf-scene-state-') as state, (a.output/'stderr.log').open('w') as errors:
    process=subprocess.Popen([str(a.engine.resolve()),f'tests/scenes/{scene}.js','--scene-state-dir',state],stdout=subprocess.PIPE,stderr=errors,text=True,env={**os.environ,'DISPLAY':':0','EGL_PLATFORM':'x11'})
    try:
        log,_=process.communicate(timeout=19)
        raise RuntimeError(f'early engine exit {process.returncode}')
    except subprocess.TimeoutExpired:
        process.send_signal(signal.SIGTERM)
        try:
            log,_=process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill();process.communicate();raise RuntimeError('shutdown timed out')
    saved=json.loads((Path(state)/'live.json').read_text())
    assert saved['version']==3 and len(saved['objects'])==owned
    assert len(saved['joints'])==(2 if a.physics else 0)
log+='\n'+(a.output/'stderr.log').read_text();(a.output/'engine.log').write_text(log)
if process.returncode or re.search(r'\[js\].*(?:load|update) error|\[(?:surface|panel)\].*error=|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+',log):
    raise RuntimeError('native error; inspect engine log')
samples=[json.loads(line.split('PERSISTENCE_SAMPLE ',1)[1]) for line in log.splitlines() if 'PERSISTENCE_SAMPLE ' in line]
summary=[]
for phase in range(6):
    rows=[r for r in samples if r['phase']==phase and r['age']>=.75]
    active=phase%2==0
    if len(rows)<2 or any(r['objectCount']!=(owned+1 if active else 1) or r['panelCount']!=1 or r['owner']['disposed']==active or len(r['owner']['objectIds'])!=(owned if active else 0) for r in rows):
        raise RuntimeError(f'phase {phase}: resource/owner counts failed')
    if a.physics and any(r['jointCount']!=(2 if active else 0) for r in rows):
        raise RuntimeError(f'phase {phase}: joint count failed')
    summary.append(dict(phase=phase,objects=rows[-1]['objectCount'],gpuBytes=rows[-1]['gpuAllocatedEstimateBytes']))
memory=re.search(r'grouped_buffer_bytes=(\d+) peak=(\d+) limit=(\d+)',log)
if not memory or int(memory[1]) or int(memory[2])>int(memory[3]):
    raise RuntimeError('grouped budget release failed')
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('PASS live scene persistence '+json.dumps(summary))
