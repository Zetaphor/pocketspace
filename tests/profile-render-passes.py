#!/usr/bin/env python3
"""Bounded per-pass GPU attribution, paired with whole-render timing runs."""
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
cases = args.case or ['128-models-fixed', '128-models-adaptive', '128-avocados-fixed']
for round_index in range(args.rounds):
    for case in (cases if round_index % 2 == 0 else reversed(cases)):
        pair = []
        for passes in ((False, True) if round_index % 2 == 0 else (True, False)):
            directory = args.output / f'r{round_index}-{case}-passes-{int(passes)}'
            env = {**os.environ, 'SF_GPU_PROFILE_PASSES': str(int(passes)), 'SF_GPU_SYNC_MODE': 'finish'}
            subprocess.run([sys.executable, 'tests/fast-motion.py', '--engine', str(args.engine.resolve()),
                            '--output', str(directory), '--seconds', str(args.seconds), '--case', case,
                            '--vertex-cache'], env=env, check=True)
            result = json.loads((directory / 'summary.json').read_text())[0]
            log = (directory / f'{case}.log').read_text()
            def samples(prefix):
                return [s for line in log.splitlines() if line.startswith(prefix)
                        for s in [json.loads(line[len(prefix):])] if s['seconds'] >= 6]
            whole = samples('GPU_SYNC_SAMPLE ')
            if len(whole) < 3 or any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] for s in whole):
                raise RuntimeError(f'{directory}: missing or invalid whole-render samples')
            result.update(round=round_index, passProfiling=passes,
                          **{key: statistics.median(s[key] for s in whole)
                             for key in ['enqueueMs', 'waitMs', 'submitMs']})
            if passes:
                measured = samples('RENDER_PASS_SAMPLE ')
                if len(measured) < 3 or any(not s['valid'] or s['queries'] < 6 for s in measured):
                    raise RuntimeError(f'{directory}: missing or invalid per-pass samples')
                result['profileSamples'] = len(measured)
                result['categoryOrder'] = ['occluder', 'opaque', 'blend']
                for key in ['gpuMs', 'threadCpuMs', 'draws', 'triangles']:
                    result[key] = [statistics.median(s[key][i] for s in measured) for i in range(3)]
                result['occluderGpuShareMedian'] = statistics.median(s['gpuMs'][0] / sum(s['gpuMs']) for s in measured)
            else:
                if any(s['gpuMs'] is None for s in whole):
                    raise RuntimeError(f'{directory}: GPU timer unavailable')
                result['wholeRenderGpuMs'] = statistics.median(s['gpuMs'] for s in whole)
            pair.append(result)
            results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print('PASS_PROFILE ' + json.dumps(result), flush=True)
        matched = abs(pair[0]['gpuEstimateMiBMedian'] - pair[1]['gpuEstimateMiBMedian']) < .01 and pair[0]['groupBufferPeakMiB'] == pair[1]['groupBufferPeakMiB']
        for result in pair:
            result['matchingStorage'] = matched
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
