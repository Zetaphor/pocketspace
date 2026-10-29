#!/usr/bin/env python3
"""Bounded fast-motion workloads; retain logs and report actual throughput."""
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
parser.add_argument('--engine', type=Path, default=Path('build/pocketspace'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=int, default=20)
parser.add_argument('--case', action='append', help='Run only this named case; repeat to select several')
parser.add_argument('--vertex-cache', action='store_true', help='Opt loaded assets into GPU triangle reordering')
parser.add_argument('--no-lod', action='store_true', help='Disable automatic model LOD')
parser.add_argument('--instancing', action='store_true', help='Opt loaded assets into ordered GPU instancing')
args = parser.parse_args()
for process_name in {'sf-overlay-engi', args.engine.name[:15], 'gpu-sync-experi'}:
    if subprocess.run(['pgrep', '-x', process_name], capture_output=True).returncode == 0:
        parser.error('an engine is already running')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/fast-motion.js').read_text()
results = []
configurations = [(128, False, True, False), (240, False, True, False), (128, True, True, False), (128, True, False, False), (240, False, False, False), (128, True, True, True), (128, True, False, True)]
if args.case and '128-models-two-groups' in args.case:
    configurations.append((128, True, False, False, 2))
if args.case and '128-avocados-two-groups' in args.case:
    configurations.append((128, True, False, True, 2))
if args.case and '32-cars-fixed' in args.case:
    configurations.append((32, True, False, False, 0, True))
for configuration in configurations:
    count, models, adaptive, simple = configuration[:4]
    cars = len(configuration) > 5 and configuration[5]
    groups = configuration[4] if len(configuration) > 4 else 0
    name = f'{count}-' + ('avocados' if simple else 'models' if models else 'boxes') + ('-adaptive' if adaptive else '-fixed')
    if groups:
        name = f'{count}-' + ('avocados' if simple else 'models') + '-two-groups'
    if cars:
        name = f'{count}-cars-fixed'
    if args.case and name not in args.case:
        continue
    with tempfile.TemporaryDirectory(prefix='sf-motion-') as temp:
        path = Path(temp) / 'scene.js'
        path.write_text(f'globalThis.motionLOD={json.dumps(not args.no_lod)};globalThis.motionCarsOnly={json.dumps(cars)};globalThis.motionCount={count};globalThis.motionModels={json.dumps(models)};globalThis.motionAdaptive={json.dumps(adaptive)};globalThis.motionSimpleModels={json.dumps(simple)};globalThis.motionCache={json.dumps(args.vertex_cache)};globalThis.motionGroups={groups};globalThis.motionInstancing={json.dumps(args.instancing)};\n' + source)
        env = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'}
        error_path = args.output / f'{name}.stderr.log'
        with error_path.open('w') as errors:
            process = subprocess.Popen([str(args.engine.resolve()), str(path)], stdout=subprocess.PIPE, stderr=errors, text=True, env=env)
            try:
                log, _ = process.communicate(timeout=args.seconds)
                raise RuntimeError(f'{name}: early exit {process.returncode}\n{log[-2000:]}')
            except subprocess.TimeoutExpired:
                process.send_signal(signal.SIGTERM)
                try:
                    log, _ = process.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.communicate(); raise RuntimeError(f'{name}: shutdown timed out')
        log += '\n' + error_path.read_text()
    (args.output / f'{name}.log').write_text(log)
    if process.returncode or re.search(r'\[js\].*(?:update|load) error|buffer id=.*failed|\[(?:surface|panel)\].*error=|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log):
        raise RuntimeError(f'{name}: native failure, inspect log')
    memory = re.search(r'grouped_buffer_bytes=(\d+) peak=(\d+) limit=(\d+)', log)
    if not memory or int(memory[1]) or int(memory[2]) > int(memory[3]):
        raise RuntimeError(f'{name}: missing budget accounting, leak or over-budget allocation')
    samples = [json.loads(line.split('MOTION_SAMPLE ', 1)[1]) for line in log.splitlines() if 'MOTION_SAMPLE ' in line]
    late = [s for s in samples if s['seconds'] >= 5 and s['fps'] is not None]
    if len(late) < 3 or any(s['objectCount'] != count for s in samples):
        raise RuntimeError(f'{name}: missing samples or changing object count')
    result = dict(name=name, lod=not args.no_lod, lodIndexBytes=sum(int(n) for n in re.findall(r"\[model\] lod .*index_bytes=(\d+)", log)), vertexCache=args.vertex_cache, gpuInstancing=args.instancing, samples=len(late), fpsMedian=statistics.median(s['fps'] for s in late),
                  gpuEstimateMiBMedian=statistics.median(s['gpuAllocatedEstimateBytes'] / 1048576 for s in late),
                  groupBufferPeakMiB=int(memory[2]) / 1048576,
                  deferredAllocations=log.count('deferred;'),
                  surfaceCreations=len(re.findall(r'\[surface\] created', log)))
    results.append(result)
    print(json.dumps(result), flush=True)
if not results:
    parser.error('no matching cases')
(args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
