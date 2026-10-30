#pragma once
#include <GLES3/gl3.h>
#include <string>
#include <vector>
#include "picking_bvh.h"
#include "animation.h"
#include "surface_frame.h"
#include "lighting_environment.h"
#include "sheen.h"
#include "pbr_lod_budget.h"
#include "animation_profile.h"
#include "gpu_skinning.h"
#include "rigid_batching.h"
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
#include "../tests/vertex-array-experiment.h"
#endif

struct ModelLodLevel {GLuint indexBuffer=0;GLsizei count=0;float error=0;GLuint vbo=0,surfaceVbo=0;uint64_t budgetBytes=0;GLsizei gpuVertexCount=0;GLenum indexType=GL_UNSIGNED_INT;};
struct MaterialMap {
  std::vector<unsigned char> rgba;
  int width=1,height=1;
  GLuint texture=0;
  float uv[9]={1,0,0,0,1,0,0,0,1};
  GLint wrapS=GL_REPEAT,wrapT=GL_REPEAT,minFilter=GL_LINEAR_MIPMAP_LINEAR,magFilter=GL_LINEAR;
};
struct ModelLighting {
  float direction[3]={-.4f,.8f,.5f},color[3]={1,1,1};
  float intensity=3,ambient=.15f,exposure=1;
  std::string environment;
  float environmentIntensity=1,environmentRotation=0;
  bool operator==(const ModelLighting &)const=default;
};
struct PrimitiveAsset {
  std::vector<float> vertices; // expanded xyz, uv triangle stream
  // Expanded normal.xyz, tangent.xyzw, UV0; separate from legacy xyz/UV.
  std::vector<float> surface,localSurface;
  std::vector<std::vector<float>> morphNormals,morphTangents;
  MaterialMap maps[6]; // normal, metallic/roughness, occlusion, emissive, sheen color/roughness
  GLuint surfaceVbo=0;
  float metallic=1,roughness=1,normalScale=1,occlusionStrength=1,emissive[3]={};
  bool materialUnlit=false,flatNormals=false,surfaceMirrored=false;
  GLint baseWrapS=GL_REPEAT,baseWrapT=GL_REPEAT,baseMinFilter=GL_LINEAR_MIPMAP_NEAREST,baseMagFilter=GL_LINEAR;
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
  float sheenRoughness=0;
  int alphaMode=0; // 0 opaque, 1 mask, 2 blend
  float alphaCutoff=.5f;
  bool doubleSided=false;
  bool untextured=false;
  ModelLodLevel lod[2];
  GLuint vbo=0,indexBuffer=0,texture=0;
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
  mutable GLuint vertexArrays[3]{};
#endif
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
  bool rigidBatchEligible=false;
  GLuint batchVbo=0; // Owned once; geometry.primitives never alias this name.
  size_t batchBytes=0;
  bool uploaded=false;
  float low[3]={0,0,0},high[3]={0,0,0};
  bool hasSkin=false,hasMorph=false,hasAnimation=false;
  bool optimizeVertexCache=false;
  bool gpuInstancing=true;
  bool lod=true;
  bool pbr=false;
  ModelLighting lighting;
  std::shared_ptr<LightingEnvironment> environment;
  std::shared_ptr<SheenLookup> sheenLookup;
};
bool loadModelFile(const std::string &path,ModelAsset &out);
void uploadModel(ModelAsset &asset);
void deleteModel(ModelAsset &asset);
// An expanded override is used for per-instance CPU-deformed geometry.
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
void drawModelPrimitive(const PrimitiveAsset &part,GLuint expandedVbo=0,int lodLevel=0,const PrimitiveAsset *deformed=nullptr);
#else
void drawModelPrimitive(const PrimitiveAsset &part,GLuint expandedVbo=0,int lodLevel=0);
#endif

inline bool rigidPoseCachingEnabled=true; // Comparison tests may force full CPU updates.
struct PosePartState {
  animation::Matrix world{};
  std::array<float,3> low{},high{};
  float radius=0;
  GLint streamFirst=0;
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
  if(level==2&&(part.lod[1].indexBuffer||part.lod[1].vbo))return &part.lod[1];
  return level>=1&&level<=2&&(part.lod[0].indexBuffer||part.lod[0].vbo)?&part.lod[0]:nullptr;
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

bool drawRigidBatch(const ModelAsset &asset,const ModelPose &pose,size_t &primitive);
void drawPosePrimitive(const PrimitiveAsset &source,const ModelPose &pose,size_t primitive);

inline bool modelPrimitiveMirrored(const PrimitiveAsset &part,const ModelPose *pose=nullptr){
  return pose&&part.node>=0&&size_t(part.node)<pose->world.size()?surfaceFrame::determinant(pose->world[part.node])<0:part.surfaceMirrored;
}
// One owner per render pass; avoid redundant driver calls between materials.
class ModelCulling {
  bool policy=true,enabled=false,clockwise=false;
public:
  explicit ModelCulling(bool active=true):policy(active){}
  ModelCulling(const ModelCulling&)=delete;
  ~ModelCulling(){reset();}
  void material(const PrimitiveAsset &part,const ModelPose *pose=nullptr){
    const bool active=policy&&!part.doubleSided,cw=modelPrimitiveMirrored(part,pose);
    if(active!=enabled){if(active)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);enabled=active;}
    if(cw!=clockwise){glFrontFace(cw?GL_CW:GL_CCW);clockwise=cw;}
  }
  void reset(){if(enabled)glDisable(GL_CULL_FACE);if(clockwise)glFrontFace(GL_CCW);enabled=clockwise=false;}
};

#ifdef SF_PBR_INDEX_VALIDATION
namespace pbrIndexValidation {
inline int failStage=-1;
inline bool failCpu=false,rollbackClean=true;
inline size_t rejectedBuffers=0;
}
#endif
