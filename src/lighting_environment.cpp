#include "lighting_environment.h"
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>

namespace {
std::map<std::string,std::weak_ptr<LightingEnvironment>> cache;
bool dimension(uint32_t n){return n>=16&&n<=256&&(n&(n-1))==0;}
bool halfValid(uint16_t n){return (n&0x7c00)!=0x7c00&&(!(n&0x8000)||(n&0x7fff)==0);}
}
std::shared_ptr<LightingEnvironment> loadLightingEnvironment(const std::string &path){
  static_assert(std::endian::native==std::endian::little,"PSENV001 data requires a little-endian host");
  for(auto it=cache.begin();it!=cache.end();)if(it->second.expired())it=cache.erase(it);else ++it;
  if(auto it=cache.find(path);it!=cache.end())if(auto asset=it->second.lock())return asset;
  if(path.empty()||path.size()>4096||path.find('\0')!=std::string::npos)return {};
  std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};
  const auto bytes=file.tellg();if(bytes<128||bytes>8*1024*1024)return {};file.seekg(0);
  auto asset=std::make_shared<LightingEnvironment>();asset->path=path;
  char magic[8];file.read(magic,8);if(std::memcmp(magic,"PSENV001",8)&&std::memcmp(magic,"SFENV001",8))return {};
  file.read(reinterpret_cast<char*>(&asset->size),4);file.read(reinterpret_cast<char*>(&asset->levels),4);file.read(reinterpret_cast<char*>(&asset->lutSize),4);
  if(!file||!dimension(asset->size)||!dimension(asset->lutSize)||asset->levels!=std::bit_width(asset->size))return {};
  size_t elements=0;for(uint32_t s=asset->size;s;s/=2)elements+=size_t(s)*s*6*3;
  const size_t lutElements=size_t(asset->lutSize)*asset->lutSize*2;
  if(size_t(bytes)!=20+sizeof(asset->diffuse)+(elements+lutElements)*2)return {};
  file.read(reinterpret_cast<char*>(asset->diffuse.data()),sizeof(asset->diffuse));
  asset->specular.resize(elements);asset->brdf.resize(lutElements);
  file.read(reinterpret_cast<char*>(asset->specular.data()),elements*2);file.read(reinterpret_cast<char*>(asset->brdf.data()),lutElements*2);
  if(!file)return {};
  for(float v:asset->diffuse)if(!std::isfinite(v)||std::abs(v)>1e6f)return {};
  for(uint16_t v:asset->specular)if(!halfValid(v))return {};
  for(uint16_t v:asset->brdf)if(!halfValid(v))return {};
  cache[path]=asset;return asset;
}
LightingEnvironment::~LightingEnvironment(){if(cube)glDeleteTextures(1,&cube);if(lut)glDeleteTextures(1,&lut);}
bool LightingEnvironment::upload(){
  if(cube&&lut)return true;
  GLint previousCube=0,previous2d=0,alignment=0;glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&previousCube);glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous2d);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glGenTextures(1,&cube);glBindTexture(GL_TEXTURE_CUBE_MAP,cube);glTexStorage2D(GL_TEXTURE_CUBE_MAP,levels,GL_RGB16F,size,size);
  size_t offset=0;
  for(uint32_t level=0;level<levels;level++){
    const auto width=std::max(1u,size>>level);
    for(int face=0;face<6;face++){glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,0,0,width,width,GL_RGB,GL_HALF_FLOAT,specular.data()+offset);offset+=size_t(width)*width*3;}
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
  glGenTextures(1,&lut);glBindTexture(GL_TEXTURE_2D,lut);glTexStorage2D(GL_TEXTURE_2D,1,GL_RG16F,lutSize,lutSize);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,lutSize,lutSize,GL_RG,GL_HALF_FLOAT,brdf.data());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  const bool ok=cube&&lut&&glGetError()==GL_NO_ERROR;
  glBindTexture(GL_TEXTURE_CUBE_MAP,GLuint(previousCube));glBindTexture(GL_TEXTURE_2D,GLuint(previous2d));glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
  if(!ok){if(cube)glDeleteTextures(1,&cube);if(lut)glDeleteTextures(1,&lut);cube=lut=0;}return ok;
}
uint64_t LightingEnvironment::gpuBytes()const{
  if(!cube||!lut)return 0;uint64_t bytes=uint64_t(lutSize)*lutSize*4;
  // RGB16F is commonly padded to RGBA16F internally; this is an estimate.
  for(uint32_t s=size;s;s/=2)bytes+=uint64_t(s)*s*6*8;return bytes;
}
