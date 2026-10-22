#include "model_asset.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#define CGLTF_IMPLEMENTATION
#include "../third_party/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

static bool decodeBaseColor(const cgltf_image *image,PrimitiveAsset &part) {
  if(!image){part.rgba={255,255,255,255};return true;}
  if(!image->buffer_view){std::fprintf(stderr,"[model] external image unsupported\n");return false;}
  const auto *bytes=cgltf_buffer_view_data(image->buffer_view);
  if(!bytes)return false;
  int channels=0;
  auto *pixels=stbi_load_from_memory(bytes,(int)image->buffer_view->size,&part.width,&part.height,&channels,4);
  if(!pixels)return false;
  part.rgba.assign(pixels,pixels+(size_t)part.width*part.height*4);
  stbi_image_free(pixels);
  return true;
}
static bool appendPrimitive(const cgltf_node &node,const cgltf_primitive &source,ModelAsset &out,float low[3],float high[3]) {
  if(source.type!=cgltf_primitive_type_triangles){std::fprintf(stderr,"[model] skipping non-triangle primitive\n");return true;}
  const cgltf_accessor *positions=nullptr,*uvs=nullptr;
  for(cgltf_size i=0;i<source.attributes_count;i++){
    const auto &attribute=source.attributes[i];
    if(attribute.type==cgltf_attribute_type_position)positions=attribute.data;
    if(attribute.type==cgltf_attribute_type_texcoord&&attribute.index==0)uvs=attribute.data;
  }
  if(!positions)return false;
  PrimitiveAsset part;
  const cgltf_material *material=source.material;
  const cgltf_texture_view *view=nullptr;
  if(material){
    part.alphaMode=(int)material->alpha_mode;
    part.alphaCutoff=material->alpha_cutoff;
    part.doubleSided=material->double_sided;
    if(material->has_sheen)for(int i=0;i<3;i++)part.sheen[i]=material->sheen.sheen_color_factor[i];
    if(material->has_pbr_metallic_roughness){
      for(int i=0;i<4;i++)part.factor[i]=material->pbr_metallic_roughness.base_color_factor[i];
      view=&material->pbr_metallic_roughness.base_color_texture;
    }
  }
  const cgltf_image *image=view&&view->texture?view->texture->image:nullptr;
  if(!decodeBaseColor(image,part)){std::fprintf(stderr,"[model] base-color image decode failed\n");return false;}
  float matrix[16];cgltf_node_transform_world(&node,matrix);
  cgltf_size count=source.indices?source.indices->count:positions->count;
  part.vertices.reserve(count*5);
  for(cgltf_size i=0;i<count;i++){
    cgltf_size index=source.indices?cgltf_accessor_read_index(source.indices,i):i;
    if(index>=positions->count||uvs&&index>=uvs->count)return false;
    float p[3],uv[2]={0,0};
    if(!cgltf_accessor_read_float(positions,index,p,3))return false;
    if(uvs&&!cgltf_accessor_read_float(uvs,index,uv,2))return false;
    if(view&&view->has_transform){
      const auto &t=view->transform;
      float u=uv[0]*t.scale[0],v=uv[1]*t.scale[1],c=std::cos(t.rotation),s=std::sin(t.rotation);
      uv[0]=t.offset[0]+c*u-s*v;uv[1]=t.offset[1]+s*u+c*v;
    }
    float world[3]={matrix[0]*p[0]+matrix[4]*p[1]+matrix[8]*p[2]+matrix[12],matrix[1]*p[0]+matrix[5]*p[1]+matrix[9]*p[2]+matrix[13],matrix[2]*p[0]+matrix[6]*p[1]+matrix[10]*p[2]+matrix[14]};
    for(int axis=0;axis<3;axis++){low[axis]=std::min(low[axis],world[axis]);high[axis]=std::max(high[axis],world[axis]);}
    part.vertices.insert(part.vertices.end(),{world[0],world[1],world[2],uv[0],uv[1]});
  }
  part.vertexCount=(GLsizei)count;
  if(source.targets_count)out.hasMorph=true;
  out.primitives.push_back(std::move(part));
  return true;
}
bool loadModelFile(const std::string &path,ModelAsset &out) {
  cgltf_options options{};cgltf_data *data=nullptr;
  auto result=cgltf_parse_file(&options,path.c_str(),&data);
  if(result!=cgltf_result_success){std::fprintf(stderr,"[model] parse failed %s: %d\n",path.c_str(),(int)result);return false;}
  result=cgltf_load_buffers(&options,data,path.c_str());
  if(result!=cgltf_result_success||cgltf_validate(data)!=cgltf_result_success){std::fprintf(stderr,"[model] invalid glTF %s\n",path.c_str());cgltf_free(data);return false;}
  out.path=path;out.hasAnimation=data->animations_count>0;out.hasSkin=data->skins_count>0;
  float low[3]={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  float high[3]={-low[0],-low[1],-low[2]};
  out.low[0]=out.low[1]=out.low[2]=std::numeric_limits<float>::max();out.high[0]=out.high[1]=out.high[2]=-std::numeric_limits<float>::max();
  for(cgltf_size i=0;i<data->nodes_count;i++){
    const auto &node=data->nodes[i];if(!node.mesh)continue;
    for(cgltf_size j=0;j<node.mesh->primitives_count;j++){
      if(!appendPrimitive(node,node.mesh->primitives[j],out,low,high)){std::fprintf(stderr,"[model] unsupported primitive in %s\n",path.c_str());cgltf_free(data);return false;}
    }
  }
  cgltf_free(data);
  if(out.primitives.empty())return false;
  float extent=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]});
  if(!std::isfinite(extent)||extent<=0)return false;
  float scale=1.f/extent;
  for(auto &part:out.primitives)for(size_t i=0;i<part.vertices.size();i+=5)for(int axis=0;axis<3;axis++)part.vertices[i+axis]=(part.vertices[i+axis]-(low[axis]+high[axis])*.5f)*scale;
  // Picking bounds must use the same centered, normalized space as vertices.
  for(int axis=0;axis<3;axis++){
    out.low[axis]=(low[axis]-high[axis])*.5f*scale;
    out.high[axis]=(high[axis]-low[axis])*.5f*scale;
  }
  const auto pickingStart=std::chrono::steady_clock::now();
  picking::build(out);
  const double pickingBuildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
  const size_t pickingBytes=out.pickNodes.capacity()*sizeof(picking::Node)+out.pickTriangles.capacity()*sizeof(picking::Triangle);
  std::fprintf(stdout,"[picking] nodes=%zu triangles=%zu build_ms=%.3f reserved_bytes=%zu\n",out.pickNodes.size(),out.pickTriangles.size(),pickingBuildMs,pickingBytes);
  std::fprintf(stdout,"[model] loaded %s parts=%zu triangles=%zu skin=%d morph=%d animation=%d\n",path.c_str(),out.primitives.size(),[&]{size_t n=0;for(auto &p:out.primitives)n+=p.vertexCount/3;return n;}(),out.hasSkin,out.hasMorph,out.hasAnimation);
  std::fflush(stdout);
  return true;
}
void uploadModel(ModelAsset &asset){
  if(asset.uploaded)return;
  for(auto &part:asset.primitives){
    glGenBuffers(1,&part.vbo);glBindBuffer(GL_ARRAY_BUFFER,part.vbo);
    glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_STATIC_DRAW);
    glGenTextures(1,&part.texture);glBindTexture(GL_TEXTURE_2D,part.texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,part.width,part.height,0,GL_RGBA,GL_UNSIGNED_BYTE,part.rgba.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    part.rgba.clear();
  }
  asset.uploaded=true;
}
void deleteModel(ModelAsset &asset){
  for(auto &part:asset.primitives){if(part.vbo)glDeleteBuffers(1,&part.vbo);if(part.texture)glDeleteTextures(1,&part.texture);part.vbo=part.texture=0;}
  asset.uploaded=false;
}
