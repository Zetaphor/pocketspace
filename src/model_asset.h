#pragma once
#include <GLES3/gl3.h>
#include <string>
#include <vector>

struct PrimitiveAsset {
  std::vector<float> vertices; // expanded xyz, uv triangle stream
  std::vector<unsigned char> rgba;
  int width=1,height=1;
  float factor[4]={1,1,1,1};
  float sheen[3]={0,0,0};
  int alphaMode=0; // 0 opaque, 1 mask, 2 blend
  float alphaCutoff=.5f;
  bool doubleSided=false;
  GLuint vbo=0,texture=0;
  GLsizei vertexCount=0;
};
struct ModelAsset {
  std::string path;
  int eyePixels=768;
  std::vector<PrimitiveAsset> primitives;
  bool uploaded=false;
  float low[3]={0,0,0},high[3]={0,0,0};
  bool hasSkin=false,hasMorph=false,hasAnimation=false;
};
bool loadModelFile(const std::string &path,ModelAsset &out);
void uploadModel(ModelAsset &asset);
void deleteModel(ModelAsset &asset);
