"""Known affine mapping and quality filtering for the offline gaze analyzer."""
import copy
import importlib.util
import sys
sys.dont_write_bytecode = True
from pathlib import Path
spec = importlib.util.spec_from_file_location('analysis', Path(__file__).resolve().parents[1] / 'scripts/analyze-gaze-accuracy.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
points = []
for y in (220, 384, 548):
    for x in (205, 512, 819):
        points.append({'target': f'{x},{y}', 'targetPixels': {'x': x, 'y': y},
                       'observedPixels': {'x': 2*x+100, 'y': 3*y-40},
                       'errorPixels': {'x': x+100, 'y': 2*y-40},
                       'medianErrorDegrees': 10, 'spread95Pixels': 2})
second = copy.deepcopy(points)
second[0]['spread95Pixels'] = 100
result = analysis.analyze([points, second])
assert result['runs'][0]['stablePoints'] == 9
assert result['runs'][1]['stablePoints'] == 8
assert result['runs'][1]['unstableTargets'] == ['205,220']
assert len(result['crossValidation']) == 2
for item in result['crossValidation']:
    assert item['maxResidualPixels'] < 1e-8
assert analysis.analyze([[dict(points[0], spread95Pixels=100)]])['runs'][0]['stableMedianErrorDegrees'] is None
print('PASS gaze analyzer: known affine recovery, held-out predictions, unstable rejection, empty stable set')
