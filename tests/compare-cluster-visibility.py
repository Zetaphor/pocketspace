#!/usr/bin/env python3
"""Compare original, full-cluster, and culled-cluster depth draws."""
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
parser.add_argument('--case', action='append')
args = parser.parse_args()
if args.rounds < 1 or args.seconds < 12:
    parser.error('require positive rounds and at least twelve seconds per run')
args.output.mkdir(parents=True, exist_ok=True)
results = []
cases = args.case or ['128-models-fixed', '128-models-two-groups', '128-avocados-fixed']
for round_index in range(args.rounds):
    for case_index, case in enumerate(cases if round_index % 2 == 0 else reversed(cases)):
        pair = []
        for mode in (('original', 'full', 'cull') if (round_index + case_index) % 2 == 0 else ('cull', 'full', 'original')):
            directory = args.output / f'r{round_index}-{case}-cluster-{mode}'
            env = {**os.environ, 'SF_GPU_CLUSTER_MODE': mode, 'SF_GPU_PRIMITIVE_VISIBILITY': '0', 'SF_GPU_OPAQUE_SHADER': '0',
                   'SF_GPU_PROFILE_PASSES': '0', 'SF_GPU_SYNC_MODE': 'finish'}
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
                raise RuntimeError(f'{directory}: missing or invalid GPU timing')
            result.update(round=round_index, clusterMode=mode, profileSamples=len(samples),
                          **{key: statistics.median(s[key] for s in samples)
                             for key in ['gpuMs', 'enqueueMs', 'waitMs', 'submitMs']})
            culls = [json.loads(line.split('CLUSTER_VISIBILITY_SAMPLE ', 1)[1]) for line in log.splitlines() if line.startswith('CLUSTER_VISIBILITY_SAMPLE ')]
            culls = [s for s in culls if s['seconds'] >= 6 and s['frames'] > 0]
            if len(culls) < 3:
                raise RuntimeError(f'{directory}: missing culling statistics')
            result.update(skippedTrianglesPerFrame=statistics.median(s['skippedTriangles']/s['frames'] for s in culls),
                          eligibleTrianglesPerFrame=statistics.median(s['eligibleTriangles']/s['frames'] for s in culls),
                          rangeDrawsPerFrame=statistics.median(s['rangeDraws']/s['frames'] for s in culls),
                          indexBytes=max(s['indexBytes'] for s in culls),
                          prepareMsPerFrame=statistics.median(s['prepareMsPerFrame'] for s in culls))
            pair.append(result);results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print('CLUSTER_COMPARISON ' + json.dumps(result), flush=True)
        matched = all(abs(result['gpuEstimateMiBMedian']-pair[0]['gpuEstimateMiBMedian']) < .01
                      and result['groupBufferPeakMiB'] == pair[0]['groupBufferPeakMiB']
                      and result['indexBytes'] == pair[0]['indexBytes'] for result in pair)
        for result in pair:
            result['matchingStorage'] = matched
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        if not matched:
            raise RuntimeError(f'{case}: differing allocations invalidate throughput comparison')
