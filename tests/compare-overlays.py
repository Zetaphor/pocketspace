#!/usr/bin/env python3
"""Bounded native comparison; run from the repository root with VR available."""
from engine_launch import catalog_arguments
import argparse
import json
import os
import re
from pathlib import Path
import signal
import statistics
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/pocketspace'))
parser.add_argument('--counts', type=int, nargs='+', default=[48, 96])
parser.add_argument('--geometries', choices=['boxes', 'models'], nargs='+', default=['boxes', 'models'])
parser.add_argument('--reverse-order', action='store_true')
parser.add_argument('--layout', choices=['compact', 'wide'], default='compact')
parser.add_argument('--seconds', type=float, default=12)
parser.add_argument('--warmup', type=float, default=5)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.seconds <= args.warmup + 2 or not all(1 <= count <= 240 for count in args.counts):
    parser.error('require counts 1..240 and duration more than warmup + 2 seconds')
if any(subprocess.run(['pgrep', '-x', name], capture_output=True).returncode == 0 for name in {'pocketspace', args.engine.name[:15]}):
    parser.error('an engine is already running; stop the known test scene first')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/overlay-comparison.js').read_text()
env = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'}
results = []
for count in args.counts:
    for geometry in args.geometries:
        # Alternate pair order to reduce a consistent first/second-run bias.
        modes = ('single', 'grouped') if geometry == 'boxes' else ('grouped', 'single')
        if args.reverse_order:
            modes = tuple(reversed(modes))
        for renderer in modes:
            config = dict(renderer=renderer, geometry=geometry, count=count, dynamic=False, lasers=False, interactive=False, layout=args.layout)
            name = f'{geometry}-{count}-{renderer}'
            with tempfile.TemporaryDirectory(prefix='sf-overlay-compare-') as temp:
                scene = Path(temp) / 'scene.js'
                scene.write_text('globalThis.overlayComparison=' + json.dumps(config) + ';\n' + source)
                process = subprocess.Popen([str(args.engine.resolve()), *catalog_arguments(scene)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
                try:
                    log, _ = process.communicate(timeout=args.seconds)
                    raise RuntimeError(f'{name}: engine exited early ({process.returncode})\n{log[-2000:]}')
                except subprocess.TimeoutExpired:
                    process.send_signal(signal.SIGTERM)
                    try:
                        log, _ = process.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill(); log, _ = process.communicate()
                        raise RuntimeError(f'{name}: engine did not stop after SIGTERM')
            (args.output / f'{name}.log').write_text(log)
            if process.returncode != 0 or '[js] update error:' in log or '[surface] submit' in log or '[surface] placement' in log or re.search(r'gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log):
                raise RuntimeError(f'{name}: engine failed; inspect its log')
            samples = [json.loads(line.split('OVERLAY_SAMPLE ', 1)[1]) for line in log.splitlines() if 'OVERLAY_SAMPLE ' in line]
            samples = [s for s in samples if s['variantSeconds'] >= args.warmup and s['fps'] is not None]
            if len(samples) < 3 or any(s['bodies'] != count or s['objectCount'] != count + 1 for s in samples):
                raise RuntimeError(f'{name}: insufficient samples or mismatched object counts')
            result = {**config, 'samples': len(samples),
                      'resolutionLimitedGroups': sorted({int(group) for group in re.findall(r'group=(\d+) reaches \d+ pixels per eye', log)}),
                      'maxAllocatedEyePixels': max(map(int, re.findall(r'eye_px=(\d+)', log)), default=0)}
            for key in ('fps', 'cpuPercent', 'ramBytes', 'gpuAllocatedEstimateBytes'):
                result[key] = statistics.median(s[key] for s in samples)
            results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print(json.dumps(result), flush=True)
