#!/usr/bin/env python3
"""Bounded comparison of split-only and opt-in merge/rebalance on moving IDs."""
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
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if any(subprocess.run(['pgrep', '-x', name], capture_output=True).returncode == 0 for name in {'pocketspace', args.engine.name[:15]}):
    parser.error('an engine is already running')
args.output.mkdir(parents=True, exist_ok=True)
source = Path('tests/scenes/auto-split.js').read_text()
exercise = '''
const originalRegroupUpdate=update;let regroupPhase=-1;
update=function(){
  const phase=Math.min(3,Math.floor((Date.now()-started)/8000));
  if(phase!==regroupPhase){regroupPhase=phase;
    globalThis.regroupStage=['wide','mixed','compact','wide-again'][phase];
    globalThis.regroupPhaseStarted=Date.now();print('REGROUP_PHASE '+globalThis.regroupStage);
    if(phase===1){wide=true;moveBodies(true);}
    if(phase===2){wide=false;moveBodies();}
    if(phase===3){wide=true;moveBodies();}
  }
  originalRegroupUpdate();
};
'''
results = []
for models in (False, True):
    for enabled in ((False, True) if not models else (True, False)):
        name = ('models' if models else 'boxes') + ('-adaptive' if enabled else '-split-only')
        config = dict(splitModels=models, splitKinematic=True, splitLimit=2, splitMerge=enabled, splitRebalance=enabled)
        with tempfile.TemporaryDirectory(prefix='sf-regroup-') as temp:
            path = Path(temp) / 'scene.js'
            path.write_text('\n'.join(f'globalThis.{key}={json.dumps(value)};' for key, value in config.items()) + '\n' + source + exercise)
            env = {**os.environ, 'DISPLAY': os.environ.get('DISPLAY', ':0'), 'EGL_PLATFORM': 'x11'}
            process = subprocess.Popen([str(args.engine.resolve()), *catalog_arguments(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
            try:
                log, _ = process.communicate(timeout=34)
                raise RuntimeError(f'{name}: early exit {process.returncode}\n{log[-2000:]}')
            except subprocess.TimeoutExpired:
                process.send_signal(signal.SIGTERM)
                try:
                    log, _ = process.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.communicate()
                    raise RuntimeError(f'{name}: failed to stop')
        (args.output / f'{name}.log').write_text(log)
        if process.returncode or re.search(r'\[js\].*(?:update|load) error|\[surface\].*(?:unavailable|error=)|gl_error=(?!0(?:\s|$))[0-9a-fA-F]+', log):
            raise RuntimeError(f'{name}: inspect log')
        samples = []; events = []; body_tiles = 1
        for line in log.splitlines():
            event = re.search(r'group=1 (split|merge|rebalance) tiles=(\d+)', line)
            if event:
                body_tiles = int(event[2]); events.append(dict(operation=event[1], bodyTiles=body_tiles))
            if 'SPLIT_SAMPLE ' in line:
                sample = json.loads(line.split('SPLIT_SAMPLE ', 1)[1]); sample['bodyTiles'] = body_tiles; samples.append(sample)
        expected_ids = samples[0]['bodyIds'] if samples else []
        if len(expected_ids) != 16 or any(s['objectCount'] != 17 or s['bodyIds'] != expected_ids for s in samples):
            raise RuntimeError(f'{name}: objects changed across regrouping')
        if enabled and not all(any(e['operation'] == op for e in events) for op in ('merge', 'rebalance')):
            raise RuntimeError(f'{name}: expected merge and rebalance')
        if not enabled and any(e['operation'] != 'split' for e in events):
            raise RuntimeError(f'{name}: split-only policy regrouped')
        stages = []
        for stage in ('wide', 'mixed', 'compact', 'wide-again'):
            selected = [s for s in samples if s['stage'] == stage and s['stageAge'] >= 4 and s['fps'] is not None]
            if len(selected) < 3:
                raise RuntimeError(f'{name}: missing settled samples for {stage}')
            expected_tiles = 1 if enabled and stage == 'compact' else 2
            if any(s['bodyTiles'] != expected_tiles for s in selected):
                raise RuntimeError(f'{name}: wrong settled tile count for {stage}')
            stages.append(dict(stage=stage, bodyTiles=expected_tiles, samples=len(selected),
                               **{key: statistics.median(s[key] for s in selected if s[key] is not None)
                                  for key in ('fps', 'cpuPercent', 'ramBytes', 'gpuAllocatedEstimateBytes')}))
        summary = dict(name=name, unchangedObjectIds=expected_ids, events=events, stages=stages,
                       resolutionLimitedGroups=sorted(set(re.findall(r'group=(\d+) reaches', log))))
        results.append(summary)
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        print(json.dumps(summary), flush=True)
