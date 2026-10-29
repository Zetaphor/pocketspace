#pragma once
// Test-only, surface-aware simplification; upload reduced expanded streams to
// avoid the indexed-draw regression. Full CPU and full-detail GPU data survive.
namespace pbrLodExperiment {
inline void upload(const ModelAsset &asset,PrimitiveAsset &part){
 const char *enabled=std::getenv("SF_PBR_LOD");
 if(!enabled||std::string(enabled)!="1"||!asset.lod||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode!=0||part.vertexCount<300)return;
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
  std::vector<float> vertices,surface;vertices.reserve(count*5);surface.reserve(count*9);
  for(size_t i=0;i<count;i++){
   const auto first=indexed.vertices.begin()+size_t(reduced[i])*14;
   vertices.insert(vertices.end(),first,first+5);surface.insert(surface.end(),first+5,first+14);
  }
  auto &lod=part.lod[level];
  glGenBuffers(1,&lod.vbo);glBindBuffer(GL_ARRAY_BUFFER,lod.vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
  glGenBuffers(1,&lod.surfaceVbo);glBindBuffer(GL_ARRAY_BUFFER,lod.surfaceVbo);glBufferData(GL_ARRAY_BUFFER,surface.size()*sizeof(float),surface.data(),GL_STATIC_DRAW);
  lod.count=GLsizei(count);lod.error=error;
  std::printf("PBR_LOD_UPLOAD {\"path\":\"%s\",\"level\":%d,\"originalTriangles\":%d,\"triangles\":%zu,\"error\":%.8f}\n",asset.path.c_str(),level+1,part.vertexCount/3,count/3,error);
 }
}
}
