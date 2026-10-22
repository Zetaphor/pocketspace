"""Compile the actual picker against known geometry and a world-space oracle.
Requires Python 3 and a C++20 compiler; no OpenVR, GL, or headset needed.
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.cpp').read_text()
blob = (root / 'assets/tests/HollowFrame.glb').read_bytes()
length = struct.unpack_from('<I', blob, 12)[0]
doc = json.loads(blob[20:20+length])
binary = blob[28+length:]
pv, iv = doc['bufferViews']
positions = struct.unpack_from('<'+'f'*(pv['byteLength']//4), binary, pv['byteOffset'])
indices = struct.unpack_from('<'+'H'*(iv['byteLength']//2), binary, iv['byteOffset'])
vertices = []
for index in indices:
    vertices.extend([*positions[index*3:index*3+3], 0, 0])

header = r"""#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <cassert>
#include <cstdio>
#include <random>
#include "picking_bvh.h"
struct Object {float x=0,y=0,z=0,scale=.5,pitch=0,yaw=0,roll=0;};
struct Part {std::vector<float> vertices;};
struct ModelAsset {float low[3]={-.5,-.5,-.08},high[3]={.5,.5,.08};std::vector<Part> primitives;std::vector<picking::Triangle> pickTriangles;std::vector<picking::Node> pickNodes;};
struct ControllerSnapshot {bool valid=true;float x=0,y=0,z=0;float rotation[9];};
"""

# Frozen pre-optimization world-space implementation, after correctness fixes.
reference = r"""static float meshHitReference(const Object &object,const ModelAsset &asset,const ControllerSnapshot &hand,int handIndex){
  if(!hand.valid)return std::numeric_limits<float>::max();
  constexpr float tilt=.6981317008f;
  const float tip[3]={handIndex==0?.012694f:-.012694f,-.02522f,.020687f};
  float origin[3]={hand.x,hand.y,hand.z},direction[3]{};
  for(int row=0;row<3;row++){
    for(int axis=0;axis<3;axis++)origin[row]+=hand.rotation[row*3+axis]*tip[axis];
    direction[row]=-std::sin(tilt)*hand.rotation[row*3+1]-std::cos(tilt)*hand.rotation[row*3+2];
  }
  float best=std::numeric_limits<float>::max();
  const float cp=std::cos(object.pitch),sp=std::sin(object.pitch),cy=std::cos(object.yaw),sy=std::sin(object.yaw),cr=std::cos(object.roll),sr=std::sin(object.roll);
  const float centerX=(asset.low[0]+asset.high[0])*.5f*object.scale,centerY=(asset.low[1]+asset.high[1])*.5f*object.scale,centerZ=(asset.low[2]+asset.high[2])*.5f*object.scale;
  const float rotatedX=cr*centerX-sr*centerY,rotatedY=sr*centerX+cr*centerY;
  const float pitchedY=cp*rotatedY-sp*centerZ,pitchedZ=sp*rotatedY+cp*centerZ;
  const float center[3]={object.x+cy*rotatedX+sy*pitchedZ,object.y+pitchedY,object.z+sy*rotatedX-cy*pitchedZ};
  const float halfX=(asset.high[0]-asset.low[0])*.5f*object.scale,halfY=(asset.high[1]-asset.low[1])*.5f*object.scale,halfZ=(asset.high[2]-asset.low[2])*.5f*object.scale;
  const float outerRadiusSquared=halfX*halfX+halfY*halfY+halfZ*halfZ;
  float along=0,centerDistanceSquared=0;
  for(int axis=0;axis<3;axis++){const float delta=center[axis]-origin[axis];along+=delta*direction[axis];centerDistanceSquared+=delta*delta;}
  if(along<0&&centerDistanceSquared>outerRadiusSquared)return best;
  if(centerDistanceSquared-along*along>outerRadiusSquared)return best;
  for(const auto &part:asset.primitives)for(size_t index=0;index+14<part.vertices.size();index+=15){
    float triangle[3][3];
    for(int vertex=0;vertex<3;vertex++){
      const float x=part.vertices[index+vertex*5]*object.scale,y=part.vertices[index+vertex*5+1]*object.scale,z=part.vertices[index+vertex*5+2]*object.scale;
      const float rotatedX=cr*x-sr*y,rotatedY=sr*x+cr*y;
      const float pitchedY=cp*rotatedY-sp*z,pitchedZ=sp*rotatedY+cp*z;
      triangle[vertex][0]=object.x+cy*rotatedX+sy*pitchedZ;
      triangle[vertex][1]=object.y+pitchedY;
      triangle[vertex][2]=object.z+sy*rotatedX-cy*pitchedZ;
    }
    float distance;
    if(rayTriangle(origin,direction,triangle[0],triangle[1],triangle[2],distance))best=std::min(best,distance);
  }
  // Bounds only reject candidates. A triangle miss must stay a miss so holes
  // and concave openings do not occlude another object's actual surface.
  return best;
}"""

checks = r"""
int main(){
float aa[3]={-.08f,-.08f,0},bb[3]={.08f,-.08f,0},cc[3]={0,.08f,0};
float origin[3]={0,0,1},direction[3]={0,0,-1},distance=0;
assert(rayTriangle(origin,direction,aa,bb,cc,distance));assert(std::fabs(distance-1)<1e-5f);
origin[0]=.2f;assert(!rayTriangle(origin,direction,aa,bb,cc,distance));
ModelAsset a; a.primitives.push_back(Part{{VERTICES}}); picking::build(a); Object o;
for(int hand=0;hand<2;hand++){ControllerSnapshot h;float t=.6981317008f,c=std::cos(t),s=std::sin(t);float r[]={1,0,0,0,c,-s,0,s,c};std::copy(r,r+9,h.rotation);h.x=hand==0?-.012694f:.012694f;h.y=-(-.02522f*c-.020687f*s);h.z=1-(-.02522f*s+.020687f*c);
assert(meshHit(o,a,h,hand)==std::numeric_limits<float>::max());
h.x+=.21f;assert(meshHit(o,a,h,hand)<1.f);
o.yaw=.9f;h.x-=.21f;assert(meshHit(o,a,h,hand)==std::numeric_limits<float>::max());o.yaw=0;
for(int n=0;n<2000;n++){
  o.pitch=(n%13)*.17f;o.yaw=(n%17)*.19f;o.roll=(n%11)*.23f;o.scale=.1f+(n%9)*.08f;
  h.x=(hand==0?-.012694f:.012694f)+((n%41)-20)*.012f;
  h.y=-(-.02522f*c-.020687f*s)+((n%37)-18)*.013f;
  const float actual=meshHit(o,a,h,hand),reference=meshHitReference(o,a,h,hand);
  assert((actual==std::numeric_limits<float>::max())==(reference==std::numeric_limits<float>::max()));
  if(actual!=std::numeric_limits<float>::max())assert(std::fabs(actual-reference)<1e-4f);
}
o.pitch=o.yaw=o.roll=0;o.scale=.5f;
h.valid=false;assert(meshHit(o,a,h,hand)==std::numeric_limits<float>::max());}
// Exercise tree traversal with sparse layers, overlapping surfaces, and rays
// both outside and inside the geometry. Compare nearest hits with a full scan.
ModelAsset grid;grid.primitives.resize(3);
for(int layer=0;layer<3;layer++)for(int y=0;y<20;y++)for(int x=0;x<20;x++){
  auto &v=grid.primitives[layer].vertices;
  const float cx=(x-10)*.1f,cy=(y-10)*.1f,z=layer*.2f;
  for(const auto &point:std::array<std::array<float,3>,3>{{{cx-.04f,cy-.04f,z},{cx+.04f,cy-.04f,z},{cx,cy+.04f,z}}})
    v.insert(v.end(),{point[0],point[1],point[2],0,0});
}
picking::build(grid);
assert(grid.pickTriangles.size()==1200);assert(grid.pickNodes.size()>1);
std::mt19937 random(12345);std::uniform_real_distribution<float> sample(-1.2f,1.2f);
for(int n=0;n<8000;n++){
  float origin[3]={sample(random),sample(random),sample(random)};
  float direction[3]={sample(random),sample(random),sample(random)};
  if(n%4==0){direction[0]=direction[1]=0;direction[2]=n%8==0?1.f:-1.f;}
  if(n%11==0)direction[0]=1e-9f;
  const float actual=picking::intersect(grid,origin,direction,rayTriangle);
  float expected=std::numeric_limits<float>::max();
  for(const auto &part:grid.primitives)for(size_t i=0;i+14<part.vertices.size();i+=15){
    const float *v=part.vertices.data()+i;float distance;
    if(rayTriangle(origin,direction,v,v+5,v+10,distance))expected=std::min(expected,distance);
  }
  assert((actual==std::numeric_limits<float>::max())==(expected==std::numeric_limits<float>::max()));
  if(actual!=std::numeric_limits<float>::max())assert(std::fabs(actual-expected)<1e-5f);
}
// Rebuilding must replace old references; empty assets have no candidates.
grid.primitives.clear();picking::build(grid);
assert(grid.pickTriangles.empty()&&grid.pickNodes.empty());
origin[0]=origin[1]=0;origin[2]=1;
assert(picking::intersect(grid,origin,direction,rayTriangle)==std::numeric_limits<float>::max());
puts("PASS BVH: 8000 rays, nearest hits, inside origins, parallel rays, sparse layers, rebuild");
puts("PASS both hands: hollow center miss, rail hit, rotated opening miss, invalid tracking miss");}
"""

rotation_start = source.index('static void objectRotationMatrix(')
rotation_end = source.index('static bool frameButtonDown', rotation_start)
picker_start = source.index('static bool rayTriangle(')
picker_end = source.index('\nint main(', picker_start)
program = (header + source[rotation_start:rotation_end] + source[picker_start:picker_end]
           + reference + checks.replace('VERTICES', ','.join(map(str, vertices))))
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'picking.cpp'
    executable = Path(directory) / 'picking'
    cpp.write_text(program)
    checked = ['-D_GLIBCXX_ASSERTIONS', '-D_GLIBCXX_DEBUG'] if '--checked' in sys.argv else []
    sanitizer = ['-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(root / 'src'), '-Wall', '-Wextra', str(cpp), '-o', str(executable)] + checked + sanitizer, check=True)
    subprocess.run([str(executable)], check=True)
print('PASS 4000 transformed-ray comparisons with world-space oracle')
