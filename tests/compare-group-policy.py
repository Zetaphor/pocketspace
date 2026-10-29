#!/usr/bin/env python3
"""Compare existing/equal-allocation occluder-aware split tie-breaks with work counters."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
from render_work_analysis import summarize_work, matching_geometry

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/gpu-sync-experiment'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--rounds', type=int, default=2)
parser.add_argument('--seconds', type=int, default=16)
parser.add_argument('--case', action='append')
parser.add_argument('--no-lod', action='store_true', help='Retain full geometry to isolate grouping from moving LOD thresholds')
parser.add_argument('--reverse', action='store_true', help='Reverse the initial pair order')
args = parser.parse_args()
if args.rounds < 1 or args.seconds < 12:
    parser.error('require positive rounds and at least twelve seconds per run')
args.output.mkdir(parents=True, exist_ok=True)
results = []
cases = args.case or ['128-models-adaptive', '128-avocados-adaptive']
for round_index in range(args.rounds):
    for case in (cases if round_index % 2 == 0 else reversed(cases)):
        pair = []
        for enabled in ((False, True) if (round_index + cases.index(case) + args.reverse) % 2 == 0 else (True, False)):
            directory = args.output / f'r{round_index}-{case}-occluders-{int(enabled)}'
            env = {k: v for k, v in os.environ.items() if not k.startswith('SF_GPU_')}
            env.update(SF_GPU_STATIC_VERTEX='0', SF_GPU_ORDINARY_VERTEX='0', SF_GPU_PROFILE_PASSES='0', SF_GPU_SYNC_MODE='finish')
            env['SF_GPU_GROUP_POLICY']='occluders' if enabled else 'pixels'
            command = [sys.executable, 'tests/fast-motion.py', '--engine', str(args.engine.resolve()),
                       '--output', str(directory), '--seconds', str(args.seconds), '--case', case,
                       '--vertex-cache', '--instancing']
            if args.no_lod:
                command.append('--no-lod')
            subprocess.run(command, env=env, check=True)
            result = json.loads((directory / 'summary.json').read_text())[0]
            log = (directory / f'{case}.log').read_text()
            samples = [json.loads(line.split('GPU_SYNC_SAMPLE ', 1)[1]) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
            samples = [s for s in samples if s['seconds'] >= 6]
            if len(samples) < 3 or any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] or s['gpuMs'] is None for s in samples):
                raise RuntimeError(f'{directory}: missing or invalid GPU timing')
            result.update(summarize_work(log, int(case.split('-', 1)[0])))
            result.update(round=round_index, occluderTieBreak=enabled, profileSamples=len(samples),
                          **{key: statistics.median(s[key] for s in samples)
                             for key in ['gpuMs', 'enqueueMs', 'waitMs', 'submitMs']})
            pair.append(result);results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print('GROUP_POLICY_COMPARISON ' + json.dumps(result), flush=True)
        matched = abs(pair[0]['gpuEstimateMiBMedian'] - pair[1]['gpuEstimateMiBMedian']) < .01 and pair[0]['groupBufferPeakMiB'] == pair[1]['groupBufferPeakMiB']
        for result in pair:
            result['matchingStorage'] = matched
            result['matchingGeometry'] = matching_geometry(pair[0], pair[1])
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        if not matched or not matching_geometry(pair[0], pair[1]):
            raise RuntimeError(f'{case}: differing allocation/geometry invalidates throughput comparison')
