#!/usr/bin/env python3
"""Generate the slab derivatives and procedural sphere used by colliders.js."""
import copy
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'assets/physics'

def write_glb(path, document, binary):
    metadata = json.dumps(document, separators=(',', ':')).encode()
    metadata += b' ' * (-len(metadata) % 4)
    binary += b'\0' * (-len(binary) % 4)
    chunks = (struct.pack('<II', len(metadata), 0x4E4F534A) + metadata +
              struct.pack('<II', len(binary), 0x004E4942) + binary)
    path.write_bytes(struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks)

source = (OUTPUT / 'Platform.glb').read_bytes()
size = struct.unpack_from('<I', source, 12)[0]
original = json.loads(source[20:20 + size])
binary = source[28 + size:]
for name, color in [('ThinPlatform', [.26, .30, .36, 1]),
                    ('ThinWallBlue', [.12, .48, 1, 1]),
                    ('ThinWallGreen', [.12, .9, .38, 1])]:
    document = copy.deepcopy(original)
    # Flatten the already transformed model along world Y (column-major matrix).
    matrix = document['nodes'][0]['matrix']
    for column in range(4):
        matrix[column * 4 + 1] *= .05
    material = document['materials'][0]
    material['name'] = name
    material['pbrMetallicRoughness']['baseColorFactor'] = color
    write_glb(OUTPUT / f'{name}.glb', document, binary)

latitudes, longitudes = 12, 24
positions, normals, indices = [], [], []
for row in range(latitudes + 1):
    theta = math.pi * row / latitudes
    for column in range(longitudes + 1):
        phi = 2 * math.pi * column / longitudes
        normal = (math.sin(theta) * math.cos(phi), math.cos(theta),
                  math.sin(theta) * math.sin(phi))
        positions.extend(value * .5 for value in normal)
        normals.extend(normal)
for row in range(latitudes):
    for column in range(longitudes):
        a = row * (longitudes + 1) + column
        b = a + longitudes + 1
        if row:
            indices.extend((a, a + 1, b))
        if row < latitudes - 1:
            indices.extend((a + 1, b + 1, b))
positions_bytes = struct.pack('<' + 'f' * len(positions), *positions)
normals_bytes = struct.pack('<' + 'f' * len(normals), *normals)
indices_bytes = struct.pack('<' + 'H' * len(indices), *indices)
binary = positions_bytes + normals_bytes + indices_bytes
count = len(positions) // 3
document = {
    'asset': {'version': '2.0', 'generator': 'sf-overlay-engine physics shapes'},
    'buffers': [{'byteLength': len(binary)}],
    'bufferViews': [
        {'buffer': 0, 'byteOffset': 0, 'byteLength': len(positions_bytes)},
        {'buffer': 0, 'byteOffset': len(positions_bytes), 'byteLength': len(normals_bytes)},
        {'buffer': 0, 'byteOffset': len(positions_bytes) + len(normals_bytes),
         'byteLength': len(indices_bytes)}],
    'accessors': [
        {'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3',
         'min': [-.5, -.5, -.5], 'max': [.5, .5, .5]},
        {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
        {'bufferView': 2, 'componentType': 5123, 'count': len(indices), 'type': 'SCALAR'}],
    'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [1, .8, .08, 1],
                                        'metallicFactor': 0}}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1},
                              'indices': 2, 'material': 0}]}],
    'nodes': [{'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0,
}
write_glb(OUTPUT / 'YellowSphere.glb', document, binary)
