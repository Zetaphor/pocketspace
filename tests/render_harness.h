#pragma once
// Offscreen stereo renderer shared by the GLES regressions; built once, linked by each.
#include "../src/render_host.h"
#include <cassert>
#include <cstdlib>
#include "../src/mesh_index.h"

#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)

constexpr int pixels=768;
struct Renderer {
  int samplePixels=::pixels,backingPixels=::pixels; // Test-only viewport sensitivity; backing storage stays fixed.
  GLuint program=0,fbo=0,color=0,depth=0;
  Scene scene;
  bool selectLocalLights=true,selectConeLights=true;
  std::vector<ObjectLightSet> objectLightSets;
  bool specializePbr=false,prewarmedPbr=false;
  PbrVariantPrograms pbrVariants;
  SpotShadows spotShadows;bool updateShadows=true;
  PbrProgram pbrProgram;
  PbrProgram referenceLightProgram,referenceLightDepthProgram;bool referenceLightLoop=false,preparedDirectional=false;
  PbrVariantPrograms directionalVariants;
  PbrProgram depthPbrProgram;bool fastPbrDepth=true;
  uint64_t pbrDepthDraws=0,pbrColorDraws=0,pbrDepthTriangles=0,pbrColorTriangles=0;
  std::map<const PrimitiveAsset*,GLuint> expandedBuffers;
  vr::HmdMatrix34_t anchor{};
  bool materialCulling=true;
  ModelInstancing instancing;
  explicit Renderer(int storagePixels=::pixels);
  std::vector<unsigned char> renderSet(const std::vector<int> &ids,physics::Vec3 headScene,int eyeIndex,int sharedDepth=0,bool readback=true,float gridScale=1,bool cull=false,int tileKey=0,int requestedPixels=0);
  ~Renderer(){for(auto [part,buffer]:expandedBuffers)glDeleteBuffers(1,&buffer);for(auto &asset:scene.assets)deleteModel(asset);glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);glDeleteProgram(program);}
  void vector(const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
  std::vector<unsigned char> render(int id,physics::Vec3 headScene,int eyeIndex,bool occlusion,bool bounds,int &sample,bool scissor=false,bool crop=false,bool expanded=false,bool cached=false);
};
