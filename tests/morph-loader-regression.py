"""Generate morph GLBs and exercise the native loader, pose and JavaScript API."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def fixture(mode):
    binary = bytearray()
    views, accessors = [], []

    def accessor(values, kind, count, fmt='f', component=5126, **extra):
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        binary.extend(struct.pack('<' + fmt * len(values), *values))
        views.append(dict(buffer=0, byteOffset=start, byteLength=len(binary)-start))
        accessors.append(dict(bufferView=len(views)-1, componentType=component,
                              type=kind, count=count, **extra))
        return len(accessors)-1

    positions = accessor([0,0,0, 1,0,0, 0,1,0], 'VEC3', 3, min=[0,0,0], max=[1,1,0])
    targets = []
    for i in range(5):
        delta = accessor([0,0,1 if i == 4 else 0]*3, 'VEC3', 3)
        targets.append({'POSITION': delta})
    if mode == 'bad-count':
        accessors[targets[0]['POSITION']]['count'] = 2
    attrs = {'POSITION': positions}
    node = dict(mesh=0, weights=[0,0,0,0,0.5])
    if mode == 'mesh-default':
        del node['weights']
    if mode == 'zero-default':
        del node['weights']
    if mode == 'bad-default':
        node['weights'] = [0]
    doc = dict(asset={'version':'2.0'}, scene=0, scenes=[{'nodes':[0]}], nodes=[node],
               meshes=[dict(primitives=[dict(attributes=attrs, targets=targets)], weights=[0,0,0,0,0.25])])
    if mode == 'zero-default':
        del doc['meshes'][0]['weights']
    times = accessor([0,2], 'SCALAR', 2, min=[0], max=[2])
    values = [0,0,0,0,0, 0,0,0,0,2]
    interpolation = 'STEP' if mode == 'step' else 'LINEAR'
    if mode == 'cubic':
        interpolation = 'CUBICSPLINE'
        values = [0]*5 + [0]*5 + [0,0,0,0,2] + [0]*5 + [0,0,0,0,2] + [0]*5
    output = accessor(values, 'SCALAR', len(values))
    doc['animations'] = [dict(samplers=[dict(input=times, output=output, interpolation=interpolation)],
                              channels=[dict(sampler=0, target=dict(node=0,path='weights'))])]
    if mode == 'bad-output':
        accessors[output]['count'] -= 1
    if mode == 'skin':
        attrs['JOINTS_0'] = accessor([0,0,0,0]*3, 'VEC4', 3, 'B', 5121)
        attrs['WEIGHTS_0'] = accessor([1,0,0,0]*3, 'VEC4', 3)
        doc['nodes'].append(dict(scale=[1,1,2]))
        node['skin'] = 0
        doc['skins'] = [dict(joints=[1])]
        doc['scenes'][0]['nodes'].append(1)
    doc.update(buffers=[dict(byteLength=len(binary))], bufferViews=views, accessors=accessors)
    encoded = json.dumps(doc).encode()
    encoded += b' ' * (-len(encoded) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return (struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary)) +
            struct.pack('<II',len(encoded),0x4e4f534a) + encoded +
            struct.pack('<II',len(binary),0x004e4942) + binary)


with tempfile.TemporaryDirectory(prefix='sf-morph-') as directory:
    for mode in ['linear','step','cubic','skin','mesh-default','zero-default','bad-count','bad-default','bad-output']:
        (Path(directory)/(mode+'.glb')).write_bytes(fixture(mode))
    subprocess.run([str(Path(sys.argv[1]).resolve()), directory], check=True)
