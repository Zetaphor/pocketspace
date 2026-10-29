#!/usr/bin/env python3
"""Generate bounded original surface-frame fixtures; no third-party asset data."""
import copy
import json
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parent
binary = bytearray()
views = []
accessors = []

def view(data):
    while len(binary) % 4:
        binary.append(0)
    result = len(views)
    views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)})
    binary.extend(data)
    return result

def floats(data, kind, count):
    result = len(accessors)
    accessors.append({'bufferView': view(struct.pack('<' + 'f' * len(data), *data)),
                      'componentType': 5126, 'count': count, 'type': kind})
    return result

position = floats([-.5, -.5, 0, .5, -.5, 0, 0, .5, 0], 'VEC3', 3)
accessors[position].update(min=[-.5, -.5, 0], max=[.5, .5, 0])
normal = floats([1 / 3**.5] * 9, 'VEC3', 3)
tangent = floats([1, 0, 0, 1] * 3, 'VEC4', 3)
uv = floats([0, 0, 1, 0, .5, 1], 'VEC2', 3)
dp = floats([0, 0, .2] * 3, 'VEC3', 3)
accessors[dp].update(min=[0, 0, .2], max=[0, 0, .2])
dn = floats([-.1, .2, .1] * 3, 'VEC3', 3)
dt = floats([.1, .2, -.1] * 3, 'VEC3', 3)
time = floats([0, 1], 'SCALAR', 2)
accessors[time].update(min=[0], max=[1])
weights = floats([0, 1], 'SCALAR', 2)
flat_normal = floats([0, 0, 1] * 3, 'VEC3', 3)
gltf_tangent = floats([1, 0, 0, -1] * 3, 'VEC4', 3)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(b'\0\x80\x80\xff\xff')) + chunk(b'IEND', b''))
image = view(png)
normal_png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))
              + chunk(b'IDAT', zlib.compress(b'\0\x80\xc0\xee\xff')) + chunk(b'IEND', b''))
strong_image = view(normal_png)
primitive = {'attributes': {'POSITION': position, 'NORMAL': normal, 'TANGENT': tangent,
                             'TEXCOORD_0': uv}, 'material': 0,
             'targets': [{'POSITION': dp, 'NORMAL': dn, 'TANGENT': dt}]}
doc = {'asset': {'version': '2.0', 'generator': 'sf-overlay-engine PBR fixtures'},
       'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors,
       'images': [{'bufferView': image, 'mimeType': 'image/png'}], 'textures': [{'source': 0}],
       'materials': [{'pbrMetallicRoughness': {'metallicFactor': 0, 'roughnessFactor': .5},
                      'normalTexture': {'index': 0}}],
       'meshes': [{'primitives': [primitive], 'weights': [.5]}],
       'nodes': [{'mesh': 0, 'scale': [2, 3, -4]}], 'scenes': [{'nodes': [0]}], 'scene': 0,
       'animations': [{'samplers': [{'input': time, 'output': weights}],
                       'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'weights'}}]}]}

def save(name, value):
    encoded = json.dumps(value, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    data = bytes(binary) + b'\0' * (-len(binary) % 4)
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(encoded) + 8 + len(data))
    out += struct.pack('<I4s', len(encoded), b'JSON') + encoded
    out += struct.pack('<I4s', len(data), b'BIN\0') + data
    (root / name).write_bytes(out)

save('SurfaceFrames.glb', doc)
missing = copy.deepcopy(doc)
missing.pop('animations')
missing['nodes'][0].pop('scale')
missing['meshes'][0].pop('weights')
missing['meshes'][0]['primitives'][0].pop('targets')
missing['meshes'][0]['primitives'][0]['attributes'].pop('NORMAL')
save('MissingNormals.glb', missing)
mirrored = copy.deepcopy(missing)
mirrored['nodes'][0]['scale'] = [-1, 1, 1]
save('MissingNormalsMirrored.glb', mirrored)
unsupported = copy.deepcopy(doc)
unsupported['materials'][0]['normalTexture']['texCoord'] = 1
unsupported['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_1'] = uv
save('UnsupportedUV.glb', unsupported)
zero = copy.deepcopy(doc)
zero['extensionsUsed'] = ['KHR_texture_transform']
zero['materials'][0]['normalTexture']['extensions'] = {'KHR_texture_transform': {'scale': [0, 1]}}
save('ZeroNormalUVScale.glb', zero)

generated = copy.deepcopy(missing)
generated['images'].append({'bufferView': strong_image, 'mimeType': 'image/png'})
generated['textures'].append({'source': 1})
generated['materials'][0]['normalTexture']['index'] = 1
attrs = generated['meshes'][0]['primitives'][0]['attributes']
attrs['NORMAL'] = flat_normal
attrs.pop('TANGENT')
save('GeneratedTangents.glb', generated)
supplied = copy.deepcopy(generated)
supplied['meshes'][0]['primitives'][0]['attributes']['TANGENT'] = gltf_tangent
save('SuppliedTangents.glb', supplied)
