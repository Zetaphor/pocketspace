#!/usr/bin/env python3
"""Bounded production-engine motion/reload soak with separate diagnostic logs."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/pocketspace'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--phases', type=int, default=5)
parser.add_argument('--phase-seconds', type=int, default=60)
args = parser.parse_args()
if args.phases < 3 or args.phase_seconds < 30:
    parser.error('require at least three phases and thirty seconds per phase')
for name in {'sf-overlay-engi', 'gpu-sync-experi', args.engine.name[:15]}:
    if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode == 0:
        parser.error('an engine is already running')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/fast-motion.js').read_text()
assert source.count('expectedObjects: count') == 1
source = source.replace('expectedObjects: count', 'expectedObjects: count, phase: globalThis.soakPhase')
hud = '''
const panel=createPanel({widthPixels:1024,heightPixels:220,widthMeters:.9});
panelSetTransform(panel,'world',0,.65,.2);
const motionUpdate=update;let nextHud=0;
update=function(){
 motionUpdate();if(Date.now()<nextHud)return;nextHud=Date.now()+500;
 const s=getEngineStats(),c=panel.canvas;c.clear(12,18,29,255);
 ['Stability '+(soakPhase+1)+'/'+soakPhases+': '+count+' '+soakGeometry,
  'Adaptive / mesh cache ON / FPS: '+(s.fps===null?'--':s.fps.toFixed(1)),
  'GPU estimate: '+(s.gpuAllocatedEstimateBytes/1048576).toFixed(1)+' MiB / automatic stop'
 ].forEach((line,i)=>c.text(24,12+i*65,line,230,240,255,255,26));panel.present();
};
'''
def scene(phase):
    simple = phase % 2 == 0
    config = dict(motionCount=128 if simple else 32, motionModels=True,
                  motionSimpleModels=simple, motionAdaptive=True, motionCache=True,
                  soakPhase=phase, soakPhases=args.phases,
                  soakGeometry='Avocados' if simple else 'cars + Avocados')
    return '\n'.join(f'globalThis.{key}={json.dumps(value)};' for key, value in config.items()) + '\n' + source + hud

def interrupted(signum, frame):
    raise KeyboardInterrupt
signal.signal(signal.SIGTERM, interrupted)
start = time.monotonic()
with tempfile.TemporaryDirectory(prefix='sf-motion-soak-') as directory:
    path = Path(directory) / 'scene.js'
    path.write_text(scene(0))
    with (args.output / 'engine.log').open('w') as output, (args.output / 'stderr.log').open('w') as errors:
        process = subprocess.Popen([str(args.engine.resolve()), str(path)], stdout=output, stderr=errors,
                                   env={**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'})
        (args.output / 'engine.pid').write_text(str(process.pid))
        (args.output / 'runner.pid').write_text(str(os.getpid()))
        try:
            for phase in range(args.phases):
                if phase:
                    replacement = path.with_name('next.js')
                    replacement.write_text(scene(phase)); replacement.replace(path)
                print(f'SOAK_PHASE {phase + 1}/{args.phases}: ' + ('128 Avocados' if phase % 2 == 0 else '32 cars/Avocados'), flush=True)
                deadline = time.monotonic() + args.phase_seconds
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'early engine exit {process.returncode}')
                    time.sleep(min(1, max(0, deadline - time.monotonic())))
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(); raise RuntimeError('shutdown timed out')

log = (args.output / 'engine.log').read_text()
errors = (args.output / 'stderr.log').read_text()
if process.returncode or re.search(r'\[js\].*(?:update|load) error|buffer id=.*failed|\[surface\].*error=|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log + errors):
    raise RuntimeError('native failure; inspect retained logs')
accounting = re.search(r'grouped_buffer_bytes=(\d+) peak=(\d+) limit=(\d+)', log)
if not accounting or int(accounting[1]) or int(accounting[2]) > int(accounting[3]):
    raise RuntimeError('missing accounting, retained grouped buffers or budget exceeded')
if log.count('[lifecycle] scene state reset') != args.phases - 1:
    raise RuntimeError('not all scene reloads committed')
samples = [json.loads(line.split('MOTION_SAMPLE ', 1)[1]) for line in log.splitlines() if 'MOTION_SAMPLE ' in line]
results = []
for phase in range(args.phases):
    expected = 128 if phase % 2 == 0 else 32
    all_samples = [s for s in samples if s['phase'] == phase]
    late = [s for s in all_samples if s['seconds'] >= 10 and s['fps'] is not None]
    if len(late) < args.phase_seconds // 2 or any(s['objectCount'] != expected for s in all_samples):
        raise RuntimeError(f'phase {phase}: insufficient samples or changing object count')
    result = dict(phase=phase, objects=expected, samples=len(late),
                  fpsMedian=statistics.median(s['fps'] for s in late),
                  ramMiBMedian=statistics.median(s['ramBytes'] / 1048576 for s in late),
                  gpuEstimateMiBMedian=statistics.median(s['gpuAllocatedEstimateBytes'] / 1048576 for s in late))
    results.append(result)
summary = dict(elapsedSeconds=time.monotonic() - start, phases=results,
               reloads=args.phases - 1, groupBufferPeakMiB=int(accounting[2]) / 1048576,
               groupedBytesAfterShutdown=int(accounting[1]),
               firstToLastAvocadoRamMiB=results[-1]['ramMiBMedian'] - results[0]['ramMiBMedian'] if args.phases % 2 else None)
(args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print('PASS motion soak ' + json.dumps(summary), flush=True)
