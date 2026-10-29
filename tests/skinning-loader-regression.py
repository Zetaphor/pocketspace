"""Generate tiny skin GLBs and run the real loader/pose regression on them.
On Deckard: LD_LIBRARY_PATH=/opt/steamvr/bin/linuxarm64 python3 tests/skinning-loader-regression.py ./build/animation-regression
"""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

def fixture(mode):
    binary=bytearray();views=[];accessors=[]
    def accessor(values,fmt,component,kind,count,normalized=False,**extra):
        while len(binary)%4:binary.append(0)
        start=len(binary);binary.extend(struct.pack('<'+fmt*len(values),*values))
        views.append({'buffer':0,'byteOffset':start,'byteLength':len(binary)-start})
        accessors.append(dict(bufferView=len(views)-1,componentType=component,type=kind,count=count,normalized=normalized,**extra))
        return len(accessors)-1
    positions=accessor([0,0,0,1,0,0,0,1,0],'f',5126,'VEC3',3,min=[0,0,0],max=[1,1,0])
    j0=accessor(([2 if mode=='bad-joint' else 1,0,0,0])*3,'B',5121,'VEC4',3)
    if mode=='float-joint':j0=accessor([1,0,0,0]*3,'f',5126,'VEC4',3)
    j1=accessor([0,0,0,0]*3,'H',5123,'VEC4',3)
    w0=accessor([0 if mode=='zero-weights' else 191,0,0,0]*3,'B',5121,'VEC4',3,True)
    w1=accessor([0 if mode=='zero-weights' else 64,0,0,0]*3,'B',5121,'VEC4',3,True)
    if mode=='negative-weight':w0=accessor([-.5,0,0,0]*3,'f',5126,'VEC4',3)
    def matrix(x,y):return [1,0,0,0,0,1,0,0,0,0,1,0,x,y,0,1]
    inverse=accessor(matrix(0,-1)+matrix(-1,-1),'f',5126,'MAT4',2)
    if mode=='bad-inverse':inverse=accessor(matrix(0,-1),'f',5126,'MAT4',1)
    times=accessor([0,1],'f',5126,'SCALAR',2,min=[0],max=[1])
    translation=accessor([1,0,0,1.2 if mode=='gpu-multipart' else 3,0,0],'f',5126,'VEC3',2)
    attrs={'POSITION':positions,'JOINTS_0':j0,'WEIGHTS_0':w0,'JOINTS_1':j1,'WEIGHTS_1':w1}
    if mode=='missing-pair':del attrs['WEIGHTS_1']
    skin={'joints':[3,2],'skeleton':1}
    if mode!='identity':skin['inverseBindMatrices']=inverse
    doc={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0,1]}],
         'nodes':[{'mesh':0,'skin':0,'translation':[100,0,0]},
                  {'translation':[0,1,0],'children':[2,3]}, {'translation':[1,0,0]},{}],
         'meshes':[{'primitives':[{'attributes':attrs,'mode':4}]}],'skins':[skin],
         'animations':[{'samplers':[{'input':times,'output':translation,'interpolation':'LINEAR'}],
                        'channels':[{'sampler':0,'target':{'node':2,'path':'translation'}}]}],
         'buffers':[{'byteLength':len(binary)}],'bufferViews':views,'accessors':accessors}
    if mode=='gpu-multipart':
        # Three material paths, two independent skin palettes, nonidentity parents.
        del attrs['JOINTS_1'];del attrs['WEIGHTS_1']
        doc['materials']=[{'pbrMetallicRoughness':{'baseColorFactor':color},'alphaMode':alpha,
                           'doubleSided':True} for color,alpha in
                          [([1,.1,.1,1],'OPAQUE'),([.1,1,.1,1],'MASK'),([.1,.1,1,.6],'BLEND')]]
        doc['meshes'][0]['primitives']=[]
        for i in range(3):
            part=dict(attrs)
            part['POSITION']=accessor([i*.4,0,0,i*.4+.3,0,0,i*.4,.6,0],
                                      'f',5126,'VEC3',3,min=[i*.4,0,0],max=[i*.4+.3,.6,0])
            doc['meshes'][0]['primitives'].append({'attributes':part,'mode':4,'material':i})
        doc['skins'].append({'joints':[2,3],'skeleton':1})
        doc['nodes'].append({'mesh':0,'skin':1,'translation':[-100,0,0]})
        doc['scenes'][0]['nodes'].append(4)
    if mode=='no-animation':del doc['animations']
    doc['buffers'][0]['byteLength']=len(binary)
    encoded=json.dumps(doc,separators=(',',':')).encode()
    encoded+=b' '*((-len(encoded))%4);binary+=b'\0'*((-len(binary))%4)
    return struct.pack('<III',0x46546C67,2,12+8+len(encoded)+8+len(binary))+struct.pack('<II',len(encoded),0x4E4F534A)+encoded+struct.pack('<II',len(binary),0x004E4942)+binary

with tempfile.TemporaryDirectory(prefix='sf-skins-') as directory:
    for mode in ['gpu-multipart','two-sets','identity','no-animation','missing-pair','bad-joint','float-joint','zero-weights','negative-weight','bad-inverse']:
        (Path(directory)/(mode+'.glb')).write_bytes(fixture(mode))
    subprocess.run([str(Path(sys.argv[1]).resolve()),directory,*sys.argv[2:]],check=True)
