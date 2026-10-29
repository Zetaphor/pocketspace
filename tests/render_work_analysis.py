"""Validate experiment-only work samples before comparing renderer throughput."""
import json
import math
import statistics


def summarize_work(log, expected_objects, warmup=6):
    rows = [json.loads(line.removeprefix('RENDER_WORK_SAMPLE '))
            for line in log.splitlines() if line.startswith('RENDER_WORK_SAMPLE ')]
    rows = [row for row in rows if row['seconds'] >= warmup]
    if len(rows) < 3:
        raise ValueError('too few render-work windows')
    for row in rows:
        if row['frames'] <= 0 or row['sceneFrames'] != row['frames']:
            raise ValueError('scene/frame counter mismatch')
        for key in ('drawsPerFrame', 'trianglesPerFrame', 'lodObjectsPerFrame'):
            if len(row[key]) != 3 or any(not math.isfinite(v) or v < 0 for v in row[key]):
                raise ValueError('invalid render-work array')
        triangles = row['sceneTrianglesPerFrame']
        if not math.isfinite(triangles) or triangles <= 0:
            raise ValueError('invalid scene triangle count')
        if abs(sum(row['lodObjectsPerFrame']) - expected_objects) > .0001:
            raise ValueError('wrong visible LOD object count')
    first = rows[0]
    stable = all(abs(row['sceneTrianglesPerFrame'] - first['sceneTrianglesPerFrame']) < .0001
                 and all(abs(a - b) < .0001 for a, b in zip(row['lodObjectsPerFrame'], first['lodObjectsPerFrame']))
                 for row in rows)
    complete = all(abs(sum(row['trianglesPerFrame'][1:]) - 2*row['sceneTrianglesPerFrame']) < .0001 for row in rows)
    return dict(workSamples=len(rows), stableSceneGeometry=stable, completeMemberGeometry=complete,
                sceneTriangles=statistics.median(r['sceneTrianglesPerFrame'] for r in rows),
                lodObjects=[statistics.median(r['lodObjectsPerFrame'][i] for r in rows) for i in range(3)],
                draws=[statistics.median(r['drawsPerFrame'][i] for r in rows) for i in range(3)],
                triangles=[statistics.median(r['trianglesPerFrame'][i] for r in rows) for i in range(3)])


def matching_geometry(a, b):
    return (a['stableSceneGeometry'] and b['stableSceneGeometry']
            and a['completeMemberGeometry'] and b['completeMemberGeometry']
            and abs(a['sceneTriangles'] - b['sceneTriangles']) < .0001
            and all(abs(x - y) < .0001 for x, y in zip(a['lodObjects'], b['lodObjects']))
            and abs(a['triangles'][1] - b['triangles'][1]) < .0001
            and abs(a['triangles'][2] - b['triangles'][2]) < .0001)
