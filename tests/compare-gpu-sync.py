#!/usr/bin/env python3
"""Bounded native producer-completion comparison; never submits unfinished work."""
from engine_launch import catalog_arguments
import argparse
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/gpu-sync-experiment'))
parser.add_argument('--renderer', choices=['single', 'grouped'], default='grouped')
parser.add_argument('--count', type=int, default=48)
parser.add_argument('--fence-wait-ms', type=int, choices=[1, 100], default=100)
parser.add_argument('--rounds', type=int, default=1)
parser.add_argument('--seconds', type=float, default=12)
parser.add_argument('--warmup', type=float, default=5)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if not 1 <= args.count <= 240 or args.rounds < 1 or args.seconds <= args.warmup + 4:
    parser.error('require count 1..240, positive rounds, and duration > warmup + 4')
for process_name in {'pocketspace', 'gpu-sync-experi', args.engine.name[:15]}:
    if subprocess.run(['pgrep', '-x', process_name], capture_output=True).returncode == 0:
        parser.error(f'{process_name} is already running')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/overlay-comparison.js').read_text()
results = []
for round_index in range(args.rounds):
    for geometry in ('boxes', 'models'):
        modes = ('finish', 'fence') if (round_index + (geometry == 'models')) % 2 == 0 else ('fence', 'finish')
        for mode in modes:
            name = f'{args.renderer}-{geometry}-{args.count}-r{round_index}-{mode}'
            config = dict(renderer=args.renderer, geometry=geometry, count=args.count, dynamic=False, lasers=False, interactive=False, layout='compact')
            env = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11', 'SF_GPU_SYNC_MODE': mode, 'SF_GPU_SYNC_WAIT_MS': str(args.fence_wait_ms)}
            with tempfile.TemporaryDirectory(prefix='sf-gpu-sync-') as temp:
                path = Path(temp) / 'scene.js'
                path.write_text('globalThis.overlayComparison=' + json.dumps(config) + ';\n' + source)
                process = subprocess.Popen([str(args.engine.resolve()), *catalog_arguments(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
                try:
                    log, _ = process.communicate(timeout=args.seconds)
                    raise RuntimeError(f'{name}: early exit {process.returncode}\n{log[-2000:]}')
                except subprocess.TimeoutExpired:
                    process.send_signal(signal.SIGTERM)
                    try:
                        log, _ = process.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate()
                        raise RuntimeError(f'{name}: failed to stop')
            (args.output / f'{name}.log').write_text(log)
            if process.returncode or re.search(r'\[js\] (?:update|load) error|\[surface\].*(?:unavailable|error=)|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log):
                raise RuntimeError(f'{name}: inspect log')
            profiles = [json.loads(line.split('GPU_SYNC_SAMPLE ', 1)[1]) for line in log.splitlines() if 'GPU_SYNC_SAMPLE ' in line]
            if not profiles or any(s['fenceFailures'] or s['glError'] or s['mode'] != mode or s['timeoutMs'] != args.fence_wait_ms for s in profiles):
                raise RuntimeError(f'{name}: missing/invalid profiling samples')
            profiles = [s for s in profiles if s['seconds'] >= args.warmup + 1]
            stats = [json.loads(line.split('OVERLAY_SAMPLE ', 1)[1]) for line in log.splitlines() if 'OVERLAY_SAMPLE ' in line]
            stats = [s for s in stats if s['variantSeconds'] >= args.warmup and s['fps'] is not None]
            if len(profiles) < 3 or len(stats) < 3 or any(s['bodies'] != args.count for s in stats):
                raise RuntimeError(f'{name}: too few samples or wrong body count')
            def median(samples, key):
                values = [s[key] for s in samples if s[key] is not None]
                return statistics.median(values) if values else None
            summary = dict(name=name, renderer=args.renderer, geometry=geometry, count=args.count, round=round_index, mode=mode, fenceWaitMs=args.fence_wait_ms,
                           profileSamples=len(profiles), statsSamples=len(stats),
                           resolutionLimitedGroups=sorted(set(re.findall(r'group=(\d+) reaches', log))),
                           **{key: median(profiles, key) for key in ('gpuMs', 'enqueueMs', 'waitMs', 'waitThreadCpuMs', 'submitMs')},
                           **{key: median(stats, key) for key in ('fps', 'cpuPercent', 'ramBytes', 'gpuAllocatedEstimateBytes')})
            summary['disjointWindows'] = sum(s['disjointSamples'] > 0 for s in profiles)
            results.append(summary)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
