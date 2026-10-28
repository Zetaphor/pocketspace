#pragma once
// Test-only GPU geometry reduction. CPU geometry and allocation capacities stay
// unchanged, so native comparisons isolate triangle work from memory changes.
#include "../src/mesh_index.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
namespace lodExperiment {
struct Result {size_t original=0,count=0;float error=0;};
inline std::unordered_map<const PrimitiveAsset*,Result> results;
inline float ratio(){const char *value=std::getenv("SF_GPU_MODEL_LOD");if(!value)return 1;return std::string(value)=="0.5"?.5f:std::string(value)=="0.25"?.25f:1.f;}
inline void simplify(const ModelAsset &asset,const PrimitiveAsset &part,mesh::Indexed &indexed){
  results.erase(&part);const float target=ratio();
  if(target==1||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode!=0)return;
  const auto start=std::chrono::steady_clock::now();const size_t original=indexed.indices.size(),vertices=indexed.vertices.size()/5;
  std::vector<uint32_t> reduced(original);const float weights[2]={.1f,.1f};float error=0;
  const auto count=meshopt_simplifyWithAttributes(reduced.data(),indexed.indices.data(),original,indexed.vertices.data(),vertices,5*sizeof(float),indexed.vertices.data()+3,5*sizeof(float),weights,2,nullptr,std::max(size_t(3),size_t(original*target)/3*3),.001f,meshopt_SimplifyLockBorder,&error);
  if(count>0&&count<=original&&count%3==0&&std::isfinite(error)&&error<=.001001f){reduced.resize(count);indexed.indices=std::move(reduced);results[&part]={original,count,error};}
  const auto found=results.find(&part);const size_t actual=found==results.end()?original:found->second.count;
  printf("LOD_UPLOAD {\"path\":\"%s\",\"ratio\":%.2f,\"originalTriangles\":%zu,\"triangles\":%zu,\"error\":%.8f,\"buildMs\":%.3f}\n",asset.path.c_str(),target,original/3,actual/3,error,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());fflush(stdout);
}
inline GLsizei count(const PrimitiveAsset &part){auto found=results.find(&part);return found==results.end()?part.vertexCount:GLsizei(found->second.count);}
}
