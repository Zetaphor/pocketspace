#!/usr/bin/env python3
"""Compare opt-in subdivision with the same fixed two-island native scene."""
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
parser.add_argument('--engine', type=Path, default=Path('build/pocketspace'))
parser.add_argument('--seconds', type=float, default=12)
parser.add_argument('--warmup', type=float, default=5)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.seconds <= args.warmup + 2:
    parser.error('duration must exceed warmup + 2 seconds')
if any(subprocess.run(['pgrep', '-x', name], capture_output=True).returncode == 0 for name in {'pocketspace', args.engine.name[:15]}):
    parser.error('an engine is already running')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/auto-split.js').read_text()
env = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'}
results = []
for models in (False, True):
    for enabled in ((False, True) if not models else (True, False)):
        name = ('models' if models else 'boxes') + ('-adaptive' if enabled else '-single')
        with tempfile.TemporaryDirectory(prefix='sf-split-') as temp:
            path = Path(temp) / 'scene.js'
            config = {'splitModels': models, 'splitEnabled': enabled, 'splitKinematic': True}
            path.write_text('\n'.join(f'globalThis.{key}={json.dumps(value)};' for key, value in config.items()) + '\n' + source)
            process = subprocess.Popen([str(args.engine.resolve()), *catalog_arguments(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
            try:
                log, _ = process.communicate(timeout=args.seconds)
                raise RuntimeError(f'{name}: exited early ({process.returncode})\n{log[-2000:]}')
            except subprocess.TimeoutExpired:
                process.send_signal(signal.SIGTERM)
                try:
                    log, _ = process.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
                    raise RuntimeError(f'{name}: did not stop after SIGTERM')
        (args.output / f'{name}.log').write_text(log)
        if process.returncode or re.search(r'\[js\] (?:update|load) error|\[surface\].*(?:unavailable|error=)|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log):
            raise RuntimeError(f'{name}: inspect its log')
        samples = [json.loads(line.split('SPLIT_SAMPLE ', 1)[1]) for line in log.splitlines() if 'SPLIT_SAMPLE ' in line]
        samples = [s for s in samples if s['seconds'] >= args.warmup and s['fps'] is not None]
        if len(samples) < 3 or any(s['objectCount'] != 17 for s in samples):
            raise RuntimeError(f'{name}: missing samples or wrong object count')
        split_tiles = [int(n) for n in re.findall(r'group=1 split tiles=(\d+)', log)]
        if enabled != bool(split_tiles):
            raise RuntimeError(f'{name}: unexpected split decisions')
        summary = dict(name=name, samples=len(samples), bodyTiles=max(split_tiles, default=1),
                       **{key: statistics.median(s[key] for s in samples if s[key] is not None)
                          for key in ('fps', 'cpuPercent', 'ramBytes', 'gpuAllocatedEstimateBytes')})
        results.append(summary)
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps(summary), flush=True)
