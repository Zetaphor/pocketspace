#!/usr/bin/env python3
"""Bounded whole-engine comparison of general and specialized PBR programs."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
from render_work_analysis import summarize_work, matching_geometry
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine',type=Path,default=Path('build/gpu-sync-experiment'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--seconds',type=int,default=16)
a=p.parse_args()
if a.seconds<12:p.error('at least twelve seconds per run required')
a.output.mkdir(parents=True,exist_ok=True)
results=[]
for repetition in range(2):
 for lights in ((0,8) if repetition==0 else (8,0)):
  pair=[]
  for enabled in ((False,True) if repetition==0 else (True,False)):
   directory=a.output/f'r{repetition}-lights-{lights}-variants-{int(enabled)}'
   env={k:v for k,v in os.environ.items() if not k.startswith('SF_GPU_')}
   env.update(SF_GPU_PBR_VARIANTS=str(int(enabled)),SF_GPU_SYNC_MODE='finish',SF_GPU_PROFILE_PASSES='0',SF_GPU_STATIC_VERTEX='0',SF_GPU_ORDINARY_VERTEX='0')
   subprocess.run([sys.executable,'tests/fast-motion.py','--engine',str(a.engine.resolve()),'--output',str(directory),'--seconds',str(a.seconds),'--case','32-cars-fixed','--no-lod','--instancing','--vertex-cache','--pbr-lights',str(lights)],env=env,check=True)
   row=json.loads((directory/'summary.json').read_text())[0]
   log=(directory/'32-cars-fixed.log').read_text()
   samples=[json.loads(line[len('GPU_SYNC_SAMPLE '):]) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
   samples=[s for s in samples if s['seconds']>=6]
   if len(samples)<3 or any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] or s['gpuMs'] is None for s in samples):raise RuntimeError('invalid GPU samples')
   cache=re.search(r'PBR_VARIANTS_CACHE programs=(\d+) binary_bytes=(\d+) compile_ms=([\d.]+) fallback_draws=(\d+)',log)
   if not cache or int(cache[1])>16:raise RuntimeError('missing/invalid program accounting')
   row.update(round=repetition,variants=enabled,programs=int(cache[1]),programBinaryBytes=int(cache[2]),compileMs=float(cache[3]),fallbackDraws=int(cache[4]),work=summarize_work(log,32),**{k:statistics.median(s[k] for s in samples) for k in ('gpuMs','enqueueMs','waitMs','submitMs')})
   pair.append(row);results.append(row)
   (a.output/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
   print('PBR_VARIANT_COMPARISON '+json.dumps(row),flush=True)
  matched=abs(pair[0]['gpuEstimateMiBMedian']-pair[1]['gpuEstimateMiBMedian'])<.01 and pair[0]['groupBufferPeakMiB']==pair[1]['groupBufferPeakMiB']
  for row in pair:row.update(matchingStorage=matched,matchingGeometry=matching_geometry(pair[0]['work'],pair[1]['work']))
  (a.output/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
  if not matched or not pair[0]['matchingGeometry']:raise RuntimeError('geometry/storage mismatch; reject comparison')
