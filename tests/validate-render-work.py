#!/usr/bin/env python3
"""Verify actual native submissions against known static/skin/rigid geometry."""
from engine_launch import catalog_arguments
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine',type=Path,default=Path('build/gpu-sync-experiment'))
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
for name in ('pocketspace',a.engine.name[:15]):
    if subprocess.run(['pgrep','-x',name],capture_output=True).returncode==0:
        p.error('an engine is already running')
env={k:v for k,v in os.environ.items() if not k.startswith('SF_GPU_')}
env.update(DISPLAY=os.environ.get('DISPLAY',':0'),EGL_PLATFORM='x11',SF_GPU_SYNC_MODE='finish')
process=subprocess.Popen([str(a.engine.resolve()),*catalog_arguments('tests/scenes/render-work-validation.js')],env=env,
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    log,_=process.communicate(timeout=16)
    raise RuntimeError('unexpected early exit')
except subprocess.TimeoutExpired:
    process.send_signal(signal.SIGTERM)
    try:
        log,_=process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill();process.communicate();raise RuntimeError('shutdown failed')
a.output.write_text(log)
assert process.returncode==0 and 'RENDER_WORK_DONE' in log
assert 'grouped_buffer_bytes=0 ' in log
assert not re.search(r'\[js\].*(?:load|update) error|\[surface\].*error=',log)
windows=[json.loads(line.removeprefix('RENDER_WORK_SAMPLE ')) for line in log.splitlines() if line.startswith('RENDER_WORK_SAMPLE ')]
# Three copies: Box=12 triangles, Fox=576, InterpolationTest=110.
# Stereo draw counts: one instanced Box draw per eye; three Fox draws per eye;
# each rigid actor has one merged nine-part range plus one textured range per eye.
for triangles,draws in ((36,2),(1728,6),(330,12)):
    matching=[w for w in windows if abs(w['sceneTrianglesPerFrame']-triangles)<.0001]
    assert matching, f'missing stable stage {triangles}'
    for w in matching:
        assert w['frames']==w['sceneFrames'] and w['lodObjectsPerFrame']==[3,0,0]
        assert w['drawsPerFrame']==[0,draws,0]
        assert w['trianglesPerFrame']==[0,2*triangles,0]
profiles=[json.loads(line.removeprefix('GPU_SYNC_SAMPLE ')) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
assert profiles and all(not r['glError'] and not r['fenceFailures'] for r in profiles)
print('PASS native render-work counters: exact instanced Box, GPU-skinned Fox and merged rigid ranges, stereo totals, LOD/frame coherence, non-interactive lifecycle and budget cleanup')
