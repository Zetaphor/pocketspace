#!/usr/bin/env python3
"""Reject invalid work windows and unmatched geometry even when storage matches."""
import copy
import json
from render_work_analysis import summarize_work, matching_geometry

sample = dict(seconds=7, frames=60, sceneFrames=60,
              drawsPerFrame=[10, 4, 0], trianglesPerFrame=[120, 960, 0],
              lodObjectsPerFrame=[0, 0, 8], sceneTrianglesPerFrame=480)
def log(rows):
    return '\n'.join('RENDER_WORK_SAMPLE '+json.dumps(r) for r in rows)
def fails(rows):
    try:
        summarize_work(log(rows), 8)
    except (ValueError, KeyError):
        return
    raise AssertionError('accepted invalid samples')
a = summarize_work(log([sample]*3), 8)
changed = copy.deepcopy(sample);changed['trianglesPerFrame'][0] = 60
b = summarize_work(log([changed]*3), 8)
assert matching_geometry(a, b)  # Reducing occluder duplication is the intended change.
changed['sceneTrianglesPerFrame'] = 240
assert not matching_geometry(a, summarize_work(log([changed]*3), 8))
changed = copy.deepcopy(sample);changed['lodObjectsPerFrame'] = [0, 4, 4]
assert not matching_geometry(a, summarize_work(log([changed]*3), 8))
assert not summarize_work(log([sample, changed, sample]), 8)['stableSceneGeometry']
changed = copy.deepcopy(sample);changed['trianglesPerFrame'][1] = 480
assert not matching_geometry(a, summarize_work(log([changed]*3), 8))
for key, value in [('frames', 0), ('sceneFrames', 59), ('lodObjectsPerFrame', [0, 0, 7]),
                   ('drawsPerFrame', [1, 2]), ('trianglesPerFrame', [float('nan'), 960, 0])]:
    changed = copy.deepcopy(sample);changed[key] = value;fails([changed]*3)
fails([sample]*2)
print('PASS render-work analysis: invalid/missing windows, frame coherence, visible counts, LOD/geometry drift and intentional occluder savings')
