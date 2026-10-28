#!/usr/bin/env python3
"""Compare ordered GPU instancing with original draws on repeated models."""
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
parser.add_argument('--multipart', action='store_true', help='Test combined multi-material streams in both matched-storage paths')
args = parser.parse_args()
if args.rounds < 1 or args.seconds < 12:
    parser.error('require positive rounds and at least twelve seconds per run')
args.output.mkdir(parents=True, exist_ok=True)
results = []
cases = args.case or ['128-avocados-fixed', '128-avocados-two-groups', '128-models-fixed']
for round_index in range(args.rounds):
    for case_index, case in enumerate(cases if round_index % 2 == 0 else reversed(cases)):
        pair = []
        for enabled in ((False, True) if (round_index + case_index) % 2 == 0 else (True, False)):
            directory = args.output / f'r{round_index}-{case}-instancing-{int(enabled)}'
            env = {**os.environ, 'SF_GPU_INSTANCING': str(int(enabled)), 'SF_GPU_PRIMITIVE_VISIBILITY': '0', 'SF_GPU_OPAQUE_SHADER': '0',
                   'SF_GPU_PROFILE_PASSES': '0', 'SF_GPU_SYNC_MODE': 'finish'}
            env.pop('SF_GPU_CLUSTER_MODE', None)
            env.pop('SF_GPU_MULTIPART_INSTANCING', None)
            if args.multipart:
                env['SF_GPU_MULTIPART_INSTANCING'] = '1'
            subprocess.run([sys.executable, 'tests/fast-motion.py', '--engine', str(args.engine.resolve()),
                            '--output', str(directory), '--seconds', str(args.seconds), '--case', case,
                            '--vertex-cache', '--instancing', '--no-lod'], env=env, check=True)
            result = json.loads((directory / 'summary.json').read_text())[0]
            log = (directory / f'{case}.log').read_text()
            samples = [json.loads(line.split('GPU_SYNC_SAMPLE ', 1)[1]) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
            if any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] for s in samples):
                raise RuntimeError(f'{directory}: GPU error, including warmup')
            samples = [s for s in samples if s['seconds'] >= 6]
            if len(samples) < 3 or any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] or s['gpuMs'] is None for s in samples):
                raise RuntimeError(f'{directory}: missing or invalid GPU timing')
            result.update(round=round_index, instancing=enabled, multipart=args.multipart, profileSamples=len(samples),
                          **{key: statistics.median(s[key] for s in samples)
                             for key in ['gpuMs', 'enqueueMs', 'waitMs', 'submitMs']})
            culls = [json.loads(line.split('INSTANCING_SAMPLE ', 1)[1]) for line in log.splitlines() if line.startswith('INSTANCING_SAMPLE ')]
            culls = [s for s in culls if s['seconds'] >= 6 and s['frames'] > 0]
            if len(culls) < 3:
                raise RuntimeError(f'{directory}: missing instancing statistics')
            result.update(batchesPerFrame=statistics.median(s['batches']/s['frames'] for s in culls),
                          instancesPerFrame=statistics.median(s['instances']/s['frames'] for s in culls),
                          savedDrawsPerFrame=statistics.median(s['savedDraws']/s['frames'] for s in culls),
                          instanceBytes=max(s['instanceBytes'] for s in culls))
            if enabled and ('avocados' in case or args.multipart) and result['savedDrawsPerFrame'] < 1:
                raise RuntimeError(f'{directory}: eligible scene did not produce GPU batches')
            pair.append(result);results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print('INSTANCING_COMPARISON ' + json.dumps(result), flush=True)
        matched = abs(pair[0]['gpuEstimateMiBMedian'] - pair[1]['gpuEstimateMiBMedian']) < .01 and pair[0]['groupBufferPeakMiB'] == pair[1]['groupBufferPeakMiB'] and pair[0]['instanceBytes'] == pair[1]['instanceBytes']
        for result in pair:
            result['matchingStorage'] = matched
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        if not matched:
            raise RuntimeError(f'{case}: differing allocations invalidate throughput comparison')
