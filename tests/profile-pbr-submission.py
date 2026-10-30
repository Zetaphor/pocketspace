#!/usr/bin/env python3
"""Measure PBR render-thread submission cost with fixed poses and view."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import tempfile
from render_work_analysis import summarize_work

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, default=Path('build/gpu-sync-experiment'))
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=int, default=16)
parser.add_argument('--reverse', action='store_true')
parser.add_argument('--workload', choices=['both', 'grouped', 'color'], default='both')
args = parser.parse_args()
if args.seconds < 12:
    parser.error('at least twelve seconds per stage required')
for name in {'pocketspace', args.engine.name[:15]}:
    if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode == 0:
        parser.error('an engine is already running')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/pbr-depth-controlled.js').read_text()
rows = []
for control in ((False, True) if args.workload == 'both' else (args.workload == 'color',)):
    for stage in range(4):
        mode = int(stage in (1, 2)) ^ args.reverse
        env = {k: v for k, v in os.environ.items() if not k.startswith('SF_GPU_')}
        env.update(DISPLAY=env.get('DISPLAY', ':0'), EGL_PLATFORM='x11',
                   SF_GPU_SYNC_MODE='finish', SF_GPU_PBR_VARIANTS='1',
                   SF_GPU_PROFILE_PASSES='0', SF_PBR_MINIMAL_DEPTH='0',
                   SF_GPU_PROFILE_PBR_SUBMIT=str(mode),
                   SF_PBR_DEPTH_FIXED_VIEW='1')
        label = f'{"color" if control else "grouped"}-stage-{stage}'
        with tempfile.TemporaryDirectory(prefix='pbr-depth-controlled-') as temporary:
            scene = Path(temporary) / 'scene.js'
            scene.write_text(f'globalThis.depthColorControl={json.dumps(control)};\n' + source)
            # File-backed logs avoid a full pipe stalling the renderer.
            with (args.output / f'{label}.log').open('w') as log_file:
                process = subprocess.Popen([str(args.engine.resolve()), str(scene)], env=env,
                                           stdout=log_file, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=args.seconds)
                    raise RuntimeError(f'{label}: early exit {process.returncode}')
                except subprocess.TimeoutExpired:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()
                        raise RuntimeError(f'{label}: shutdown timed out')
        log = (args.output / f'{label}.log').read_text()
        def samples(prefix):
            return [json.loads(line[len(prefix):]) for line in log.splitlines() if line.startswith(prefix)]
        whole = [s for s in samples('GPU_SYNC_SAMPLE ') if s['seconds'] >= 6]
        stats = samples('[js] DEPTH_CONTROL_STATS ')
        budget = re.search(r'grouped_buffer_bytes=(\d+) peak=(\d+) limit=(\d+)', log)
        assert process.returncode == 0 and 'PBR_MINIMAL_DEPTH enabled=0' in log
        assert 'PBR_DEPTH_FIXED_VIEW enabled=1' in log
        assert len(whole) >= 3 and stats and budget and int(budget[1]) == 0
        assert int(budget[2]) <= int(budget[3])
        assert not re.search(r'\[js\].*error|\[surface\].*error=|failed', log)
        assert all(s['gpuMs'] is not None and s['gpuSamples'] > 0 and not s['glError']
                   and not s['disjointSamples'] and not s['fenceFailures'] for s in whole)
        work = summarize_work(log, 32)
        work_windows = [s for s in samples('RENDER_WORK_SAMPLE ') if s['seconds'] >= 6]
        assert all(all(s[key] == work_windows[0][key] for key in
                   ('drawsPerFrame', 'trianglesPerFrame', 'lodObjectsPerFrame',
                    'sceneTrianglesPerFrame')) for s in work_windows)
        assert work['stableSceneGeometry'] and work['completeMemberGeometry']
        assert (work['draws'][0] == 0 if control else work['draws'][0] > 0)
        surface_generations = re.findall(r'\[surface\] created id=(-?\d+) width=([\d.]+) eye_px=(\d+)', log)
        assert surface_generations
        row = dict(control=control, stage=stage, instrumented=bool(mode), work=work,
                   surfaceGenerations=surface_generations,
                   groupBufferPeakBytes=int(budget[2]),
                   gpuAllocatedEstimateBytes=stats[-1]['gpuAllocatedEstimateBytes'],
                   samples=len(whole), **{key: statistics.median(s[key] for s in whole)
                       for key in ('gpuMs', 'enqueueMs', 'enqueueThreadCpuMs', 'waitMs',
                                   'waitThreadCpuMs', 'submitMs')})
        pbr = [s for s in samples('PBR_SUBMIT_SAMPLE ') if s['seconds'] >= 6]
        if mode:
            assert len(pbr) >= 3 and all(s['draws'] == work['draws'] for s in pbr)
            row['pbrSamples'] = len(pbr)
            row['pbrThreadCpuMs'] = [statistics.median(s['threadCpuMs'][i] for s in pbr)
                                     for i in range(3)]
            row['pbrTotalThreadCpuMs'] = statistics.median(sum(s['threadCpuMs']) for s in pbr)
            row['pbrDraws'] = pbr[0]['draws']
        else:
            assert not pbr
        # Static geometry/view must match every category, including external depth.
        reference = next((r for r in rows if r['control'] == control), row)
        row['matchingWorkAndStorage'] = all(row[key] == reference[key] for key in
            ('groupBufferPeakBytes', 'gpuAllocatedEstimateBytes', 'surfaceGenerations')) and all(
            work[key] == reference['work'][key] for key in
            ('draws', 'triangles', 'lodObjects', 'sceneTriangles'))
        rows.append(row)
        (args.output / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(json.dumps(row), flush=True)
        if not row['matchingWorkAndStorage']:
            raise RuntimeError(f'{label}: work/storage changed; reject comparison')
