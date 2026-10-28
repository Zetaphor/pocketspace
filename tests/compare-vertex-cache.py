#!/usr/bin/env python3
"""Alternate one instrumented executable with GPU mesh reordering off/on."""
import argparse
import json
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
if args.rounds < 1 or args.seconds < 10:
    parser.error('require positive rounds and at least ten seconds per run')
cases = args.case or ['128-models-adaptive', '128-models-fixed']
args.output.mkdir(parents=True, exist_ok=True)
results = []
for round_index in range(args.rounds):
    for case_index, case in enumerate(cases if round_index % 2 == 0 else reversed(cases)):
        pair = []
        for enabled in ((False, True) if (round_index + case_index) % 2 == 0 else (True, False)):
            directory = args.output / f'r{round_index}-{case}-cache-{int(enabled)}'
            command = [sys.executable, 'tests/fast-motion.py', '--engine', str(args.engine.resolve()), '--output', str(directory), '--seconds', str(args.seconds), '--case', case, '--no-lod']
            if enabled:
                command.append('--vertex-cache')
            subprocess.run(command, check=True)
            result = json.loads((directory / 'summary.json').read_text())[0]
            log = (directory / f'{case}.log').read_text()
            profiles = [json.loads(line.split('GPU_SYNC_SAMPLE ', 1)[1]) for line in log.splitlines() if line.startswith('GPU_SYNC_SAMPLE ')]
            profiles = [s for s in profiles if s['seconds'] >= 6]
            if len(profiles) < 3 or any(s['glError'] or s['fenceFailures'] or s['disjointSamples'] or s['gpuMs'] is None or s['mode'] != 'finish' for s in profiles):
                raise RuntimeError(f'{directory}: missing/invalid GPU profiling samples')
            result.update(round=round_index, profileSamples=len(profiles), **{key: statistics.median(s[key] for s in profiles) for key in ['gpuMs', 'enqueueMs', 'waitMs', 'submitMs']})
            results.append(result)
            pair.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print('CACHE_COMPARISON ' + json.dumps(result), flush=True)
        comparable = abs(pair[0]['gpuEstimateMiBMedian'] - pair[1]['gpuEstimateMiBMedian']) < .01 and pair[0]['groupBufferPeakMiB'] == pair[1]['groupBufferPeakMiB']
        for result in pair:
            result['matchingStorage'] = comparable
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        if not comparable:
            raise RuntimeError(f'{case}: allocation sizes differ; retain logs but reject this throughput comparison')
