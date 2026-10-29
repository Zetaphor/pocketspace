#pragma once
#include "model_asset.h"
#include "mesh_index.h"
#include <meshoptimizer.h>
#include <map>
#include <new>
// Surface-aware static PBR simplification; upload reduced expanded streams to
// avoid the indexed-draw regression. Full CPU and full-detail GPU data survive.
#include "scope_exit.h"
namespace pbrLod {
#ifdef SF_PBR_LOD_EXPERIMENT
// Failure injection for the excluded regression, never a public runtime control.
inline bool failSurfaceUpload=false,failCpuStaging=false;
inline size_t rejectedPairs=0;
inline bool rejectedPairsClean=true;
#endif
inline bool uploadBuffer(GLuint &buffer,const std::vector<float> &values){
 glGenBuffers(1,&buffer);if(!buffer)return false;
 glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,values.size()*sizeof(float),values.data(),GL_STATIC_DRAW);
 GLint bytes=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);
 return bytes>=0&&uint64_t(bytes)==uint64_t(values.size())*sizeof(float);
}
inline void upload(const ModelAsset &asset,PrimitiveAsset &part) try {
#ifdef SF_PBR_LOD_EXPERIMENT
 if(const char *enabled=std::getenv("SF_PBR_LOD");enabled&&std::string(enabled)=="0")return;
#endif
 if(!asset.pbr||!asset.lod||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode!=0||part.vertexCount<300||pbrLodBudget.used>=PbrLodBudget::limit)return;
 // Do not build large temporary remaps when even the quarter target cannot
 // fit. Actual pairs still require an exact reservation after simplification.
 if(uint64_t(part.vertexCount/4/3*3)*14*sizeof(float)>PbrLodBudget::limit-pbrLodBudget.used)return;
 std::vector<float> combined;combined.reserve(size_t(part.vertexCount)*14);
 for(size_t v=0;v<size_t(part.vertexCount);v++){
  combined.insert(combined.end(),part.vertices.begin()+v*5,part.vertices.begin()+(v+1)*5);
  combined.insert(combined.end(),part.surface.begin()+v*9,part.surface.begin()+(v+1)*9);
 }
 auto indexed=mesh::index<14>(combined);const size_t unique=indexed.vertices.size()/14;
 // Lock coincident positions with distinct surface/UV inputs. All members of a
 // seam position receive the same lock, as required by the simplifier.
 std::map<std::array<uint32_t,3>,std::vector<size_t>> positions;
 for(size_t v=0;v<unique;v++){std::array<uint32_t,3> key;for(int c=0;c<3;c++)key[c]=indexed.vertices[v*14+c]==0?0:std::bit_cast<uint32_t>(indexed.vertices[v*14+c]);positions[key].push_back(v);}
 std::vector<unsigned char> locked(unique,0);
 for(const auto &[key,vertices]:positions)if(vertices.size()>1)for(size_t v:vertices)locked[v]=1;
 const float weights[11]={.1f,.1f,1,1,1,.5f,.5f,.5f,1,.1f,.1f};
 for(int level=0;level<2;level++){
  std::vector<uint32_t> reduced(indexed.indices.size());float error=0;
  const size_t target=indexed.indices.size()/(level==0?2:4)/3*3;
  const size_t count=meshopt_simplifyWithAttributes(reduced.data(),indexed.indices.data(),indexed.indices.size(),indexed.vertices.data(),unique,14*sizeof(float),indexed.vertices.data()+3,14*sizeof(float),weights,11,locked.data(),target,.001f,meshopt_SimplifyLockBorder,&error);
  if(!count||count%3||count>indexed.indices.size()*4/5||!std::isfinite(error)||error>.001001f)continue;
  if(level&&part.lod[0].vbo&&count>size_t(part.lod[0].count)*4/5)continue;
  const uint64_t bytes=uint64_t(count)*14*sizeof(float);
  if(!pbrLodBudget.reserve(bytes))continue;
  ModelLodLevel staged;bool published=false;
  ScopeExit rollback{[&]{if(!published){
    if(staged.vbo)glDeleteBuffers(1,&staged.vbo);if(staged.surfaceVbo)glDeleteBuffers(1,&staged.surfaceVbo);
#ifdef SF_PBR_LOD_EXPERIMENT
    if(staged.vbo||staged.surfaceVbo){++rejectedPairs;rejectedPairsClean&=(!staged.vbo||!glIsBuffer(staged.vbo))&&(!staged.surfaceVbo||!glIsBuffer(staged.surfaceVbo));}
#endif
    pbrLodBudget.release(bytes);
  }}};
#ifdef SF_PBR_LOD_EXPERIMENT
  if(failCpuStaging)throw std::bad_alloc();
#endif
  std::vector<float> vertices,surface;vertices.reserve(count*5);surface.reserve(count*9);
  for(size_t i=0;i<count;i++){
   const auto first=indexed.vertices.begin()+size_t(reduced[i])*14;
   vertices.insert(vertices.end(),first,first+5);surface.insert(surface.end(),first+5,first+14);
  }
  if(!uploadBuffer(staged.vbo,vertices))continue;
#ifdef SF_PBR_LOD_EXPERIMENT
  if(failSurfaceUpload)continue;
#endif
  if(!uploadBuffer(staged.surfaceVbo,surface))continue;
  staged.count=GLsizei(count);staged.error=error;staged.budgetBytes=bytes;
  part.lod[level]=staged;published=true;

 }
} catch(const std::bad_alloc&){
 // Optional detail must not invalidate an already uploaded full mesh. Any
 // unpublished reservation/buffers have been released by the staging guard.
}
}
