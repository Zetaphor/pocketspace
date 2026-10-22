"""Generate an untextured hollow frame with a known empty center (standard library only)."""
import json
from pathlib import Path
import struct

vertices = []
indices = []
for cx, cy, sx, sy in [(-.42, 0, .16, 1), (.42, 0, .16, 1),
                       (0, -.42, .68, .16), (0, .42, .68, .16)]:
    base = len(vertices) // 3
    for x, y, z in [(-1,-1,-1), (1,-1,-1), (1,1,-1), (-1,1,-1),
                    (-1,-1,1), (1,-1,1), (1,1,1), (-1,1,1)]:
        vertices.extend([cx + x*sx/2, cy + y*sy/2, z*.08])
    for face in [(0,3,2,1), (4,5,6,7), (0,1,5,4), (3,7,6,2), (0,4,7,3), (1,2,6,5)]:
        indices.extend(base+i for i in [face[0], face[1], face[2], face[0], face[2], face[3]])
positions = struct.pack('<'+'f'*len(vertices), *vertices)
binary = positions + struct.pack('<'+'H'*len(indices), *indices)
document = {
    'asset': {'version': '2.0', 'generator': 'sf-overlay selection fixture'},
    'buffers': [{'byteLength': len(binary)}],
    'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': len(positions)},
                    {'buffer': 0, 'byteOffset': len(positions), 'byteLength': len(binary)-len(positions)}],
    'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': len(vertices)//3,
                   'type': 'VEC3', 'min': [-.5,-.5,-.08], 'max': [.5,.5,.08]},
                  {'bufferView': 1, 'componentType': 5123, 'count': len(indices), 'type': 'SCALAR'}],
    'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [.1,.65,1,1], 'metallicFactor': 0,
                                         'roughnessFactor': 1}, 'doubleSided': True}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1, 'material': 0}]}],
    'nodes': [{'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0,
}
metadata = json.dumps(document, separators=(',', ':')).encode()
metadata += b' ' * (-len(metadata) % 4)
binary += b'\0' * (-len(binary) % 4)
output = Path(__file__).resolve().parents[1] / 'assets/tests/HollowFrame.glb'
output.write_bytes(struct.pack('<III', 0x46546c67, 2, 28+len(metadata)+len(binary))
                   + struct.pack('<II', len(metadata), 0x4e4f534a) + metadata
                   + struct.pack('<II', len(binary), 0x004e4942) + binary)
print(output)
