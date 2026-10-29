"""Summarize complete steady animation windows; does not infer GPU execution time."""
import argparse
import json
from pathlib import Path


def summarize(lines, warmup=5):
    stages = []
    current = None
    complete = False
    for line in lines:
        if line.startswith('ANIMATION_PROFILE_DONE'):
            complete = True
            current = None
            continue
        if line.startswith('ANIMATION_PROFILE_ABORT'):
            raise ValueError('profiling was aborted; incomplete run')
        if line.startswith('ANIMATION_STAGE '):
            current = dict(json.loads(line.split(' ', 1)[1]), elapsed=0, windows=[])
            stages.append(current)
        elif line.startswith('ANIMATION_PROFILE {') and current is not None:
            row = json.loads(line.split(' ', 1)[1])
            if row['objects'] != current['count'] or row['playing'] != (current['count'] if current['playing'] else 0):
                raise ValueError('profile window crosses a workload boundary')
            if current['elapsed'] >= warmup:
                current['windows'].append(row)
            current['elapsed'] += row['seconds']
    if not stages or not complete:
        raise ValueError('missing completed run marker')
    output = []
    fields = ['poseMs','deformBoundsMs','bvhMs','uploadSubmitMs','renderSubmitMs',
              'gpuWaitMs','workMs','evaluations','vertices','uploads','uploadBytes']
    for stage in stages:
        rows = stage['windows']
        if not rows:
            raise ValueError(f"no steady windows for stage {stage['stage']}")
        frames = sum(row['frames'] for row in rows)
        result = {key: stage[key] for key in ['stage','count','playing','model','eyePixels','renderGroup']}
        result.update(windows=len(rows), frames=frames, fps=frames/sum(row['seconds'] for row in rows),
                      maxWindowP95Ms=max(row['workP95Ms'] for row in rows))
        result.update({field:sum(row[field]*row['frames'] for row in rows)/frames for field in fields})
        if not result['playing'] and (result['evaluations'] or result['uploads'] or result['uploadBytes']):
            raise ValueError('paused stage unexpectedly evaluated or uploaded animation')
        output.append(result)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--warmup', type=float, default=5)
    args = parser.parse_args()
    if args.warmup < 0:
        parser.error('warmup must be nonnegative')
    try:
        print(json.dumps(summarize(args.log.read_text().splitlines(),args.warmup),indent=2))
    except (ValueError,KeyError) as error:
        parser.exit(1,f'Invalid profile: {error}\n')
