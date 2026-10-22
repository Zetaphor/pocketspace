"""Summarize completed gaze grids; evaluate an affine fit on separate grids only."""
import argparse
import json
import math
from pathlib import Path
import statistics


def stable(points, limit):
    return [p for p in points if p['spread95Pixels'] <= limit]


def solve(matrix, vector):
    rows = [list(row) + [value] for row, value in zip(matrix, vector)]
    for i in range(3):
        pivot = max(range(i, 3), key=lambda j: abs(rows[j][i]))
        rows[i], rows[pivot] = rows[pivot], rows[i]
        if abs(rows[i][i]) < 1e-10:
            raise ValueError('Insufficient independent points for an affine fit')
        divisor = rows[i][i]
        rows[i] = [value / divisor for value in rows[i]]
        for j in range(3):
            if i != j:
                factor = rows[j][i]
                rows[j] = [a - factor*b for a, b in zip(rows[j], rows[i])]
    return [row[3] for row in rows]


def features(point):
    return [point['observedPixels']['x']/1024, point['observedPixels']['y']/768, 1]


def fit(points):
    x = [features(p) for p in points]
    normal = [[sum(row[i]*row[j] for row in x) for j in range(3)] for i in range(3)]
    return [solve(normal, [sum(row[i]*p['targetPixels'][axis] for row, p in zip(x, points))
                           for i in range(3)]) for axis in ('x', 'y')]


def residuals(coefficients, points):
    return [math.hypot(*(sum(a*b for a, b in zip(row, features(p))) - p['targetPixels'][axis]
                         for row, axis in zip(coefficients, ('x', 'y')))) for p in points]


def analyze(runs, limit=50):
    output = {'maxSpread95Pixels': limit, 'runs': [], 'crossValidation': []}
    for index, points in enumerate(runs):
        accepted = stable(points, limit)
        output['runs'].append({
            'run': index+1, 'points': len(points), 'stablePoints': len(accepted),
            'unstableTargets': [p['target'] for p in points if p['spread95Pixels'] > limit],
            'allErrorDegreesRange': [min(p['medianErrorDegrees'] for p in points), max(p['medianErrorDegrees'] for p in points)],
            'stableMedianErrorDegrees': statistics.median(p['medianErrorDegrees'] for p in accepted) if accepted else None,
            'stableMedianErrorPixels': {axis: statistics.median(p['errorPixels'][axis] for p in accepted) if accepted else None for axis in ('x', 'y')},
        })
    for train_index, train in enumerate(runs):
        accepted = stable(train, limit)
        if len(accepted) < 3:
            continue
        try:
            coefficients = fit(accepted)
        except ValueError:
            continue
        for test_index, test in enumerate(runs):
            heldout = stable(test, limit)
            if train_index == test_index or not heldout:
                continue
            errors = residuals(coefficients, heldout)
            output['crossValidation'].append({'trainRun': train_index+1, 'testRun': test_index+1,
                'medianResidualPixels': statistics.median(errors), 'maxResidualPixels': max(errors)})
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--max-spread', type=float, default=50)
    args = parser.parse_args()
    marker = 'GAZE_ACCURACY_COMPLETE '
    runs = [json.loads(line.split(marker, 1)[1]) for line in args.log.read_text().splitlines() if marker in line]
    if not runs:
        parser.error('No completed gaze grids found')
    print(json.dumps(analyze(runs, args.max_spread), indent=2))
