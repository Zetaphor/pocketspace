#!/usr/bin/env python3
"""Compare production automatic LOD against explicit opt-out on moving models."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine', type=Path, default=Path('build/sf-overlay-engine'))
p.add_argument('--output', type=Path, required=True)
p.add_argument('--rounds', type=int, default=2)
p.add_argument('--seconds', type=int, default=16)
a = p.parse_args()
if a.rounds < 1 or a.seconds < 12:
    p.error('require positive rounds and at least twelve seconds per run')
a.output.mkdir(parents=True, exist_ok=True)
rows = []
for rnd in range(a.rounds):
    cases = ['32-cars-fixed', '128-models-fixed']
    if rnd % 2:
        cases.reverse()
    for case_index, case in enumerate(cases):
        pair = []
        for enabled in ((False, True) if (rnd + case_index) % 2 == 0 else (True, False)):
            directory = a.output / f'r{rnd}-{case}-lod-{int(enabled)}'
            env = {k: v for k, v in os.environ.items() if not k.startswith('SF_GPU_')}
            command = [sys.executable, 'tests/fast-motion.py', '--engine', str(a.engine.resolve()),
                       '--output', str(directory), '--seconds', str(a.seconds), '--case', case, '--vertex-cache']
            if not enabled:
                command.append('--no-lod')
            subprocess.run(command, env=env, check=True)
            row = json.loads((directory / 'summary.json').read_text())[0]
            row['round'] = rnd
            if enabled and row['lodIndexBytes'] <= 0:
                raise RuntimeError('automatic scene did not generate LOD buffers')
            pair.append(row)
            rows.append(row)
            (a.output / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
        full = next(r for r in pair if not r['lod'])
        auto = next(r for r in pair if r['lod'])
        extra = (auto['lodIndexBytes'] - full['lodIndexBytes']) / 1048576
        matched = auto['groupBufferPeakMiB'] == full['groupBufferPeakMiB'] and abs(auto['gpuEstimateMiBMedian'] - full['gpuEstimateMiBMedian'] - extra) < .01
        for row in pair:
            row['matchingStoragePlusLodIndices'] = matched
        (a.output / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
        if not matched:
            raise RuntimeError(f'{case}: storage differs beyond measured LOD index buffers')
        print('AUTO_LOD_PAIR ' + json.dumps(pair), flush=True)
