#!/usr/bin/env python3
"""Bounded production reload check: instanced scene, empty scene, repeat."""
from engine_launch import catalog_arguments
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/pocketspace'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--lod', action='store_true', help='Exercise generated LOD buffer lifetime with ToyCar')
args = parser.parse_args()
for name in {'pocketspace', 'gpu-sync-experi', args.engine.name[:15]}:
    if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode == 0:
        parser.error('an engine is already running')
args.output.mkdir(parents=True, exist_ok=True)
def scene(phase):
    content = '' if phase % 2 else """
const asset=loadModel('assets/Avocado.glb',768,{gpuInstancing:true});
for(let n=0;n<16;n++)createObject(asset,(n%4-1.5)*.16,(Math.floor(n/4)-1.5)*.16,0,.12,{renderGroup:1});
"""
    if args.lod:
        content = content.replace('assets/Avocado.glb', 'assets/samples/ToyCar/ToyCar.glb')
    return content + f"""
const phase={phase},started=Date.now();let next=0;
function update(){{if(Date.now()>=next){{next=Date.now()+1000;print('INSTANCE_LIFECYCLE '+JSON.stringify({{...getEngineStats(),phase,seconds:(Date.now()-started)/1000}}));}}}}
"""
def interrupted(signum, frame):
    raise KeyboardInterrupt
signal.signal(signal.SIGTERM, interrupted)
with tempfile.TemporaryDirectory(prefix='sf-instancing-lifecycle-') as directory:
    path = Path(directory) / 'scene.js'
    path.write_text(scene(0))
    with (args.output / 'engine.log').open('w') as output, (args.output / 'stderr.log').open('w') as errors:
        process = subprocess.Popen([str(args.engine.resolve()), *catalog_arguments(path)], stdout=output, stderr=errors,
                                   env={**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'})
        try:
            for phase in range(4):
                if phase:
                    replacement=path.with_name('next.js');replacement.write_text(scene(phase));replacement.replace(path)
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'early engine exit {process.returncode}')
                    time.sleep(min(1,max(0,deadline-time.monotonic())))
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait();raise RuntimeError('shutdown timed out')
log=(args.output/'engine.log').read_text()+'\n'+(args.output/'stderr.log').read_text()
if process.returncode or re.search(r'\[js\].*(?:update|load) error|\[(?:surface|panel)\].*error=|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+|instancing .* unavailable',log):
    raise RuntimeError('native error; inspect logs')
if log.count('[lifecycle] scene state reset')!=3:
    raise RuntimeError('not all scene reloads committed')
samples=[json.loads(line.split('INSTANCE_LIFECYCLE ',1)[1]) for line in log.splitlines() if 'INSTANCE_LIFECYCLE ' in line]
summary=[]
for phase in range(4):
    late=[s for s in samples if s['phase']==phase and s['seconds']>=4 and s['gpuAllocatedEstimateBytes'] is not None]
    expected=0 if phase%2 else 16
    if len(late)<3 or any(s['objectCount']!=expected or (s['gpuAllocatedEstimateBytes']!=0 if phase%2 else s['gpuAllocatedEstimateBytes']<=7168) for s in late):
        raise RuntimeError(f'phase {phase}: counts, samples, or resource release failed')
    summary.append(dict(phase=phase,objects=expected,samples=len(late),gpuBytesMin=min(s['gpuAllocatedEstimateBytes'] for s in late),gpuBytesMax=max(s['gpuAllocatedEstimateBytes'] for s in late)))
accounting=re.search(r'grouped_buffer_bytes=(\d+) peak=(\d+) limit=(\d+)',log)
if not accounting or int(accounting[1]) or int(accounting[2])>int(accounting[3]):
    raise RuntimeError('buffer accounting failed')
(args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('PASS instancing reload and empty-scene release '+json.dumps(summary))
