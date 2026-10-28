#!/usr/bin/env python3
"""Matched original versus bounded-error GPU model simplification on Frame."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/gpu-sync-experiment'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--rounds', type=int, default=2)
parser.add_argument('--seconds', type=int, default=16)
parser.add_argument('--ratio', choices=['0.5', '0.25'], default='0.5')
parser.add_argument('--case', action='append')
args = parser.parse_args()
if args.rounds < 1 or args.seconds < 12:
    parser.error('require positive rounds and at least twelve seconds per run')
args.output.mkdir(parents=True, exist_ok=True)
results = []
cases = args.case or ['32-cars-fixed', '128-models-fixed']
for round_index in range(args.rounds):
    for case_index, case in enumerate(cases if round_index % 2 == 0 else reversed(cases)):
        pair = []
        for enabled in ((False, True) if (round_index + case_index) % 2 == 0 else (True, False)):
            ratio = args.ratio if enabled else '1'
            directory = args.output / f'r{round_index}-{case}-ratio-{ratio}'
            env = {**os.environ, 'SF_GPU_MODEL_LOD': ratio, 'SF_GPU_PRIMITIVE_VISIBILITY': '0',
                   'SF_GPU_OPAQUE_SHADER': '0', 'SF_GPU_PROFILE_PASSES': '0', 'SF_GPU_SYNC_MODE': 'finish'}
            for name in ['SF_GPU_CLUSTER_MODE', 'SF_GPU_INSTANCING', 'SF_GPU_MULTIPART_INSTANCING']:
                env.pop(name, None)
            subprocess.run([sys.executable, 'tests/fast-motion.py', '--engine', str(args.engine.resolve()),
                            '--output', str(directory), '--seconds', str(args.seconds), '--case', case,
                            '--vertex-cache'], env=env, check=True)
            result = json.loads((directory / 'summary.json').read_text())[0]
            log = (directory / f'{case}.log').read_text()
            samples = [json.loads(line.split('GPU_SYNC_SAMPLE ', 1)[1]) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
            if any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] for s in samples):
                raise RuntimeError(f'{directory}: GPU error, including warmup')
            samples = [s for s in samples if s['seconds'] >= 6]
            if len(samples) < 3 or any(s['gpuMs'] is None for s in samples):
                raise RuntimeError(f'{directory}: missing GPU timing')
            uploads = [json.loads(line.split('LOD_UPLOAD ', 1)[1]) for line in log.splitlines() if line.startswith('LOD_UPLOAD ')]
            if enabled and (not uploads or sum(s['triangles'] for s in uploads) >= sum(s['originalTriangles'] for s in uploads) or any(s['error'] > .001001 for s in uploads)):
                raise RuntimeError(f'{directory}: missing geometry reduction or excessive simplifier metric')
            result.update(round=round_index, lodRatio=float(ratio), profileSamples=len(samples), uploads=uploads,
                          **{key: statistics.median(s[key] for s in samples)
                             for key in ['gpuMs', 'enqueueMs', 'waitMs', 'submitMs']})
            pair.append(result)
            results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print('LOD_COMPARISON ' + json.dumps(result), flush=True)
        matched = abs(pair[0]['gpuEstimateMiBMedian'] - pair[1]['gpuEstimateMiBMedian']) < .01 and pair[0]['groupBufferPeakMiB'] == pair[1]['groupBufferPeakMiB']
        for result in pair:
            result['matchingStorage'] = matched
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        if not matched:
            raise RuntimeError(f'{case}: differing allocations invalidate throughput comparison')
