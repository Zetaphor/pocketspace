#pragma once
#include <GLES3/gl3.h>
#include <string>
#include <vector>
#include "picking_bvh.h"
#include "animation.h"
#include "animation_profile.h"
#include "gpu_skinning.h"

struct ModelLodLevel {GLuint indexBuffer=0;GLsizei count=0;float error=0;};
struct PrimitiveAsset {
  std::vector<float> vertices; // expanded xyz, uv triangle stream
  std::vector<float> localVertices; // retained for animated or skinned assets
  std::vector<std::vector<float>> morphPositions; // expanded xyz deltas per target
  std::vector<SkinJointBounds> skinBounds;
  std::vector<float> skinVertices;
  std::vector<uint32_t> skinIndices;
  GLuint skinVbo=0,skinIbo=0;
  int node=-1,skin=-1;
  animation::InfluenceSets influences;
  std::vector<unsigned char> rgba;
  int width=1,height=1;
  float factor[4]={1,1,1,1};
  float sheen[3]={0,0,0};
  int alphaMode=0; // 0 opaque, 1 mask, 2 blend
  float alphaCutoff=.5f;
  bool doubleSided=false;
  ModelLodLevel lod[2];
  GLuint vbo=0,indexBuffer=0,texture=0;
  GLenum indexType=GL_UNSIGNED_INT;
  GLsizei vertexCount=0,gpuVertexCount=0; // expanded draw count; uploaded vertex count
};
struct ModelAsset {
  std::string path;
  int eyePixels=768;
  std::vector<PrimitiveAsset> primitives;
  std::vector<picking::Triangle> pickTriangles;
  std::vector<picking::Node> pickNodes;
  std::vector<animation::Skin> skins;
  std::vector<animation::Node> nodes;
  std::vector<int> nodeOrder;
  std::vector<animation::Clip> animations;
  float center[3]{},normalization=1;
  bool skinEligible=false;
  bool uploaded=false;
  float low[3]={0,0,0},high[3]={0,0,0};
  bool hasSkin=false,hasMorph=false,hasAnimation=false;
  bool optimizeVertexCache=false;
  bool gpuInstancing=true;
  bool lod=true;
};
bool loadModelFile(const std::string &path,ModelAsset &out);
void uploadModel(ModelAsset &asset);
void deleteModel(ModelAsset &asset);
// An expanded override is used for per-instance CPU-deformed geometry.
void drawModelPrimitive(const PrimitiveAsset &part,GLuint expandedVbo=0,int lodLevel=0);

inline bool rigidPoseCachingEnabled=false; // Comparison tests may force full CPU updates.
struct PosePartState {
  animation::Matrix world{};
  std::array<float,3> low{},high{};
  float radius=0;
  bool valid=false,uploadDirty=true;
};
// Per-object deformed geometry; material textures remain on the shared asset.
struct ModelPose {
  ModelAsset geometry;
  std::vector<PosePartState> parts;
  std::vector<animation::Node> nodes,sourceNodes;
  std::vector<animation::Matrix> world;
  std::vector<std::vector<animation::Matrix>> palettes;
  bool gpuSkin=false,pickingValid=false;
  std::vector<GLuint> skinTextures;
  float radius=1;bool dirty=true;
  ~ModelPose();
};
void evaluateModelPose(const ModelAsset &asset,const animation::Playback &state,ModelPose &pose);
void uploadModelPose(ModelPose &pose);
void prepareSkinPicking(const ModelAsset &asset,ModelPose &pose);
bool bindSkinPrimitive(const ModelAsset &asset,const PrimitiveAsset &part,const ModelPose &pose,GLuint program);
void unbindSkinPrimitive(GLuint program);

#ifdef SF_MODEL_LOD_EXPERIMENT
#include "../tests/lod-experiment.h"
#endif
inline const ModelLodLevel *modelLodLevel(const PrimitiveAsset &part,int level){
  if(level==2&&part.lod[1].indexBuffer)return &part.lod[1];
  return level>=1&&level<=2&&part.lod[0].indexBuffer?&part.lod[0]:nullptr;
}
inline GLuint modelIndexBuffer(const PrimitiveAsset &part,int level=0){const auto *lod=modelLodLevel(part,level);return lod?lod->indexBuffer:part.indexBuffer;}
inline GLsizei modelDrawVertexCount(const PrimitiveAsset &part,int level=0){
  if(const auto *lod=modelLodLevel(part,level))return lod->count;
#ifdef SF_MODEL_LOD_EXPERIMENT
  return lodExperiment::count(part);
#else
  return part.vertexCount;
#endif
}
