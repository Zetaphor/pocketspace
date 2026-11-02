#!/usr/bin/env python3
"""Exercise live container disposal/recreation with shared surfaces and panels."""
from engine_launch import catalog_arguments
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine',type=Path,default=Path('build/pocketspace'))
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
for name in {'pocketspace','gpu-sync-experi',a.engine.name[:15]}:
    if subprocess.run(['pgrep','-x',name],capture_output=True).returncode==0:
        p.error('an engine is already running')
a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'stderr.log').open('w') as errors:
    process=subprocess.Popen([str(a.engine.resolve()),*catalog_arguments('tests/scenes/container-lifecycle.js')],stdout=subprocess.PIPE,stderr=errors,text=True,env={**os.environ,'DISPLAY':':0','EGL_PLATFORM':'x11'})
    try:
        log,_=process.communicate(timeout=32)
        raise RuntimeError(f'early engine exit {process.returncode}')
    except subprocess.TimeoutExpired:
        process.send_signal(signal.SIGTERM)
        try:
            log,_=process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill();process.communicate();raise RuntimeError('shutdown timed out')
log+='\n'+(a.output/'stderr.log').read_text();(a.output/'engine.log').write_text(log)
if process.returncode or re.search(r'\[js\].*(?:load|update) error|\[(?:surface|panel)\].*error=|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+',log):
    raise RuntimeError('native error; inspect engine log')
samples=[json.loads(line.split('CONTAINER_SAMPLE ',1)[1]) for line in log.splitlines() if 'CONTAINER_SAMPLE ' in line]
summary=[]
for phase in range(6):
    rows=[r for r in samples if r['phase']==phase and r['age']>=1]
    active=phase%2==0
    if len(rows)<3 or any(r['objectCount']!=(9 if active else 1) or r['panelCount']!=(2 if active else 1) or r['container']['disposed']==active or len(r['container']['objectIds'])!=(8 if active else 0) or len(r['container']['panelIds'])!=(1 if active else 0) or r['container']['inputCallback']!=active for r in rows):
        raise RuntimeError(f'phase {phase}: resource/owner counts failed')
    summary.append(dict(phase=phase,active=active,samples=len(rows),objects=rows[-1]['objectCount'],panels=rows[-1]['panelCount'],gpuBytes=rows[-1]['gpuAllocatedEstimateBytes']))
memory=re.search(r'grouped_buffer_bytes=(\d+) peak=(\d+) limit=(\d+)',log)
if not memory or int(memory[1]) or int(memory[2])>int(memory[3]):
    raise RuntimeError('grouped budget release failed')
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('PASS live container disposal/recreation '+json.dumps(summary))
