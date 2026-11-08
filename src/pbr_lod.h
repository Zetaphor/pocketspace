#pragma once
#include "model_asset.h"
#include "mesh_index.h"
#include <meshoptimizer.h>
#include <exception>
#include <map>
#include <new>
#include <thread>
// Surface-aware PBR simplification with independently owned reduced streams.
// Cache-enabled assets can compact them; full CPU and full GPU data survive.
#include "scope_exit.h"
namespace pbrLod {
#ifdef SF_TEST_HOOKS
// Failure injection for the excluded regression, never a public runtime control.
inline bool failSurfaceUpload=false,failCpuStaging=false,failIndexUpload=false;
inline size_t rejectedPairs=0;
inline bool rejectedPairsClean=true;
#endif
inline bool uploadBuffer(GLuint &buffer,const std::vector<float> &values){
 glGenBuffers(1,&buffer);if(!buffer)return false;
 glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,values.size()*sizeof(float),values.data(),GL_STATIC_DRAW);
 GLint bytes=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);
 return bytes>=0&&uint64_t(bytes)==uint64_t(values.size())*sizeof(float);
}
// CPU half: index, lock seams and simplify both levels. It only reads the asset
// and budget, so primitives prepare concurrently; publish() then applies the
// budget and uploads in order, giving the same levels as a sequential upload.
struct PreparedLevel {bool valid=false,compact=false;size_t count=0,uploaded=0;float error=0;GLenum indexType=GL_UNSIGNED_INT;std::vector<uint32_t> reduced;std::vector<float> packed;};
struct Prepared {bool eligible=false;mesh::Indexed indexed;PreparedLevel levels[2];};
inline bool eligible(const ModelAsset &asset,const PrimitiveAsset &part){
 if(!asset.pbr||!asset.lod||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode!=0||part.vertexCount<300||pbrLodBudget.used>=PbrLodBudget::limit)return false;
 // Do not build large temporary remaps when even the quarter target cannot
 // fit. Actual pairs still require an exact reservation after simplification.
 return asset.optimizeVertexCache||uint64_t(part.vertexCount/4/3*3)*14*sizeof(float)<=PbrLodBudget::limit-pbrLodBudget.used;
}
inline Prepared prepare(const ModelAsset &asset,const PrimitiveAsset &part) try {
 Prepared result;
#ifdef SF_TEST_HOOKS
 if(const char *enabled=std::getenv("SF_PBR_LOD");enabled&&std::string(enabled)=="0")return result;
#endif
 if(!eligible(asset,part))return result;
 std::vector<float> combined;combined.reserve(size_t(part.vertexCount)*14);
 for(size_t v=0;v<size_t(part.vertexCount);v++){
  combined.insert(combined.end(),part.vertices.begin()+v*5,part.vertices.begin()+(v+1)*5);
  combined.insert(combined.end(),part.surface.begin()+v*9,part.surface.begin()+(v+1)*9);
 }
 result.indexed=mesh::index<14>(combined);auto &indexed=result.indexed;const size_t unique=indexed.vertices.size()/14;
 // Lock coincident positions with distinct surface/UV inputs. All members of a
 // seam position receive the same lock, as required by the simplifier.
 std::map<std::array<uint32_t,3>,std::vector<size_t>> positions;
 for(size_t v=0;v<unique;v++){std::array<uint32_t,3> key;for(int c=0;c<3;c++)key[c]=indexed.vertices[v*14+c]==0?0:std::bit_cast<uint32_t>(indexed.vertices[v*14+c]);positions[key].push_back(v);}
 std::vector<unsigned char> locked(unique,0);
 for(const auto &[key,vertices]:positions)if(vertices.size()>1)for(size_t v:vertices)locked[v]=1;
 const float weights[11]={.1f,.1f,1,1,1,.5f,.5f,.5f,1,.1f,.1f};
 bool compactAllowed=asset.optimizeVertexCache;
#ifdef SF_TEST_HOOKS
 if(const char *enabled=std::getenv("SF_PBR_LOD_INDEXED");enabled&&std::string(enabled)=="0")compactAllowed=false;
#endif
 auto simplify=[&](int level){
  auto &out=result.levels[level];
  out.reduced.resize(indexed.indices.size());
  const size_t target=indexed.indices.size()/(level==0?2:4)/3*3;
  out.count=meshopt_simplifyWithAttributes(out.reduced.data(),indexed.indices.data(),indexed.indices.size(),indexed.vertices.data(),unique,14*sizeof(float),indexed.vertices.data()+3,14*sizeof(float),weights,11,locked.data(),target,.001f,meshopt_SimplifyLockBorder,&out.error);
  if(!out.count||out.count%3||out.count>indexed.indices.size()*4/5||!std::isfinite(out.error)||out.error>.001001f){out.reduced.clear();return;}
  out.reduced.resize(out.count);out.uploaded=out.count;bool compact=compactAllowed;
  if(compact){
    meshopt_optimizeVertexCache(out.reduced.data(),out.reduced.data(),out.count,unique);
    out.packed.resize(indexed.vertices.size());
    out.uploaded=meshopt_optimizeVertexFetch(out.packed.data(),out.reduced.data(),out.count,indexed.vertices.data(),unique,14*sizeof(float));
    out.packed.resize(out.uploaded*14);out.indexType=out.uploaded<=65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
    compact=out.uploaded*14*sizeof(float)+out.count*(out.indexType==GL_UNSIGNED_SHORT?2:4)<out.count*14*sizeof(float);
  }
  if(!compact)out.uploaded=out.count;
  out.compact=compact;out.valid=true;
 };
 // The two levels simplify the same input independently.
 std::exception_ptr failure;
 std::thread second([&]{try{simplify(1);}catch(...){failure=std::current_exception();}});
 try{simplify(0);}catch(...){second.join();throw;}
 second.join();if(failure)std::rethrow_exception(failure);
 result.eligible=true;return result;
} catch(const std::bad_alloc&){return {};}
// GL half, in upload order: the budget reservation and level rules as before.
inline void publish(const ModelAsset &asset,PrimitiveAsset &part,Prepared &prepared) try {
 if(!prepared.eligible||!eligible(asset,part))return;
 const auto &indexed=prepared.indexed;
 for(int level=0;level<2;level++){
  auto &prep=prepared.levels[level];if(!prep.valid)continue;
  const size_t count=prep.count;const float error=prep.error;
  if(level&&part.lod[0].vbo&&count>size_t(part.lod[0].count)*4/5)continue;
  const auto &reduced=prep.reduced;const auto &packed=prep.packed;const bool compact=prep.compact;const size_t uploaded=prep.uploaded;const GLenum indexType=prep.indexType;
  const uint64_t bytes=uint64_t(uploaded)*14*sizeof(float)+(compact?uint64_t(count)*(indexType==GL_UNSIGNED_SHORT?2:4):0);
  if(!pbrLodBudget.reserve(bytes))continue;
  ModelLodLevel staged;bool published=false;
  ScopeExit rollback{[&]{if(!published){
    if(staged.vbo)glDeleteBuffers(1,&staged.vbo);if(staged.surfaceVbo)glDeleteBuffers(1,&staged.surfaceVbo);if(staged.indexBuffer)glDeleteBuffers(1,&staged.indexBuffer);
#ifdef SF_TEST_HOOKS
    if(staged.vbo||staged.surfaceVbo||staged.indexBuffer){++rejectedPairs;rejectedPairsClean&=(!staged.vbo||!glIsBuffer(staged.vbo))&&(!staged.surfaceVbo||!glIsBuffer(staged.surfaceVbo))&&(!staged.indexBuffer||!glIsBuffer(staged.indexBuffer));}
#endif
    pbrLodBudget.release(bytes);
  }}};
#ifdef SF_TEST_HOOKS
  if(failCpuStaging)throw std::bad_alloc();
#endif
  std::vector<float> vertices,surface;vertices.reserve(uploaded*5);surface.reserve(uploaded*9);
  for(size_t i=0;i<uploaded;i++){
   const auto first=compact?packed.begin()+i*14:(packed.empty()?indexed.vertices.begin():packed.begin())+size_t(reduced[i])*14;
   vertices.insert(vertices.end(),first,first+5);surface.insert(surface.end(),first+5,first+14);
  }
  if(!uploadBuffer(staged.vbo,vertices))continue;
#ifdef SF_TEST_HOOKS
  if(failSurfaceUpload)continue;
#endif
  if(!uploadBuffer(staged.surfaceVbo,surface))continue;
  if(compact){
    glGenBuffers(1,&staged.indexBuffer);if(!staged.indexBuffer)continue;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,staged.indexBuffer);
    if(indexType==GL_UNSIGNED_SHORT){std::vector<uint16_t> indices(reduced.begin(),reduced.end());glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint16_t),indices.data(),GL_STATIC_DRAW);}
    else glBufferData(GL_ELEMENT_ARRAY_BUFFER,reduced.size()*sizeof(uint32_t),reduced.data(),GL_STATIC_DRAW);
    GLint allocated=0;glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&allocated);
    if(allocated<0||uint64_t(allocated)!=uint64_t(count)*(indexType==GL_UNSIGNED_SHORT?2:4))continue;
#ifdef SF_TEST_HOOKS
    if(failIndexUpload)continue;
#endif
  }
  staged.gpuVertexCount=GLsizei(uploaded);staged.indexType=indexType;
  staged.count=GLsizei(count);staged.error=error;staged.budgetBytes=bytes;
  part.lod[level]=staged;published=true;
 }
} catch(const std::bad_alloc&){
 // Optional detail must not invalidate an already uploaded full mesh. Any
 // unpublished reservation/buffers have been released by the staging guard.
}
}
