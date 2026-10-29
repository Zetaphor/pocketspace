#include "model_asset.h"
#include "mesh_index.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include "gpu_skinning_asset.h"
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
static bool appendPrimitive(const cgltf_node &node,const cgltf_primitive &source,ModelAsset &out,float low[3],float high[3],int skinIndex,const std::vector<std::vector<animation::Matrix>> &bindPalettes) {
  if(source.type!=cgltf_primitive_type_triangles){std::fprintf(stderr,"[model] skipping non-triangle primitive\n");return true;}
  const cgltf_accessor *positions=nullptr,*uvs=nullptr;
  for(cgltf_size i=0;i<source.attributes_count;i++){
    const auto &attribute=source.attributes[i];
    if(attribute.type==cgltf_attribute_type_position)positions=attribute.data;
    if(attribute.type==cgltf_attribute_type_texcoord&&attribute.index==0)uvs=attribute.data;
  }
  if(!positions)return false;
  PrimitiveAsset part;part.skin=skinIndex;
  std::map<int,std::pair<const cgltf_accessor*,const cgltf_accessor*>> sets;
  if(skinIndex>=0){
    for(cgltf_size i=0;i<source.attributes_count;i++){
      const auto &a=source.attributes[i];
      if(a.type==cgltf_attribute_type_joints){if(sets[a.index].first)return false;sets[a.index].first=a.data;}
      if(a.type==cgltf_attribute_type_weights){if(sets[a.index].second)return false;sets[a.index].second=a.data;}
    }
    if(sets.empty())return false;
    int expected=0;
    for(const auto &[index,pair]:sets){
      const auto *j=pair.first,*w=pair.second;
      if(index!=expected++||!j||!w||j->type!=cgltf_type_vec4||w->type!=cgltf_type_vec4||j->count!=positions->count||w->count!=positions->count||j->normalized||(j->component_type!=cgltf_component_type_r_8u&&j->component_type!=cgltf_component_type_r_16u))return false;
      if(w->component_type!=cgltf_component_type_r_32f&&!((w->component_type==cgltf_component_type_r_8u||w->component_type==cgltf_component_type_r_16u)&&w->normalized))return false;
    }
    part.influences.resize(sets.size());
  }
  std::vector<const cgltf_accessor*> targets(source.targets_count,nullptr);
  part.morphPositions.resize(source.targets_count);
  for(size_t t=0;t<targets.size();t++){
    const auto &target=source.targets[t];
    for(size_t a=0;a<target.attributes_count;a++)if(target.attributes[a].type==cgltf_attribute_type_position)targets[t]=target.attributes[a].data;
    if(targets[t]&&(targets[t]->type!=cgltf_type_vec3||targets[t]->component_type!=cgltf_component_type_r_32f||targets[t]->count!=positions->count||targets[t]->is_sparse))return false;
  }
  const auto &defaultWeights=node.weights_count?node.weights:node.mesh->weights;
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
  animation::Matrix transform;cgltf_node_transform_world(&node,transform.data());
  cgltf_size count=source.indices?source.indices->count:positions->count;
  part.vertices.reserve(count*5);
  if(out.hasAnimation||out.hasSkin||out.hasMorph)part.localVertices.reserve(count*5);
  for(auto &set:part.influences)set.reserve(count);
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
    if(skinIndex>=0){
      double total=0;size_t set=0;
      for(const auto &[number,pair]:sets){
        (void)number;animation::Influence influence;
        if(!cgltf_accessor_read_uint(pair.first,index,influence.joints.data(),4)||!cgltf_accessor_read_float(pair.second,index,influence.weights.data(),4))return false;
        for(int lane=0;lane<4;lane++){
          if(influence.joints[lane]>=out.skins[skinIndex].joints.size()||!std::isfinite(influence.weights[lane])||influence.weights[lane]<0)return false;
          total+=influence.weights[lane];
        }
        part.influences[set++].push_back(influence);
      }
      if(!std::isfinite(total)||total<=0)return false;
      for(auto &influences:part.influences)for(float &weight:influences.back().weights)weight=float(weight/total);
    }
    float morphed[3]={p[0],p[1],p[2]};
    for(size_t t=0;t<targets.size();t++){
      float delta[3]={};if(targets[t]&&!cgltf_accessor_read_float(targets[t],index,delta,3))return false;
      for(int axis=0;axis<3;axis++){
        if(!std::isfinite(delta[axis]))return false;
        part.morphPositions[t].push_back(delta[axis]);
        morphed[axis]+=defaultWeights?defaultWeights[t]*delta[axis]:0;
      }
    }
    const auto world=skinIndex>=0?animation::skinPoint(morphed,bindPalettes[skinIndex],part.influences,size_t(i)):animation::transformPoint(transform,morphed);
    if(out.hasAnimation||out.hasSkin||out.hasMorph)part.localVertices.insert(part.localVertices.end(),{p[0],p[1],p[2],uv[0],uv[1]});
    for(int axis=0;axis<3;axis++){low[axis]=std::min(low[axis],world[axis]);high[axis]=std::max(high[axis],world[axis]);}
    part.vertices.insert(part.vertices.end(),{world[0],world[1],world[2],uv[0],uv[1]});
  }
  part.vertexCount=(GLsizei)count;
  if(source.targets_count)out.hasMorph=true;
  out.primitives.push_back(std::move(part));
  return true;
}
static bool loadAnimations(const cgltf_data &data,ModelAsset &out){

  out.nodes.resize(data.nodes_count);
  for(cgltf_size i=0;i<data.nodes_count;i++){
    const auto &source=data.nodes[i];auto &node=out.nodes[i];
    if(source.mesh){
      const size_t count=source.mesh->primitives_count?source.mesh->primitives[0].targets_count:0;
      for(size_t p=0;p<source.mesh->primitives_count;p++)if(source.mesh->primitives[p].targets_count!=count)return false;
      if((source.weights_count&&source.weights_count!=count)||(source.mesh->weights_count&&source.mesh->weights_count!=count))return false;
      node.weights.resize(count);
      const auto *weights=source.weights_count?source.weights:source.mesh->weights;
      if(weights)for(size_t w=0;w<count;w++){if(!std::isfinite(weights[w]))return false;node.weights[w]=weights[w];}
      if(count)out.hasMorph=true;
    }else if(source.weights_count)return false;
    node.parent=source.parent?int(source.parent-data.nodes):-1;node.matrixOnly=source.has_matrix;
    if(source.has_translation)std::copy(source.translation,source.translation+3,node.translation.begin());
    if(source.has_rotation)std::copy(source.rotation,source.rotation+4,node.rotation.begin());
    if(source.has_scale)std::copy(source.scale,source.scale+3,node.scale.begin());
    if(source.has_matrix)std::copy(source.matrix,source.matrix+16,node.matrix.begin());
  }
  if(!animation::order(out.nodes,out.nodeOrder))return false;
  out.skins.resize(data.skins_count);
  for(cgltf_size i=0;i<data.skins_count;i++){
    const auto &source=data.skins[i];auto &skin=out.skins[i];if(!source.joints_count)return false;
    const auto *inverse=source.inverse_bind_matrices;
    if(inverse&&(inverse->type!=cgltf_type_mat4||inverse->component_type!=cgltf_component_type_r_32f||inverse->count<source.joints_count))return false;
    for(cgltf_size j=0;j<source.joints_count;j++){
      if(!source.joints[j])return false;
      skin.joints.push_back(int(source.joints[j]-data.nodes));
      animation::Matrix matrix=animation::local(animation::Node{});
      if(inverse&&!cgltf_accessor_read_float(inverse,j,matrix.data(),16))return false;
      for(float value:matrix)if(!std::isfinite(value))return false;
      if(matrix[3]!=0||matrix[7]!=0||matrix[11]!=0||matrix[15]!=1)return false;
      skin.inverseBind.push_back(matrix);
    }
  }
  for(cgltf_size i=0;i<data.animations_count;i++){
    const auto &source=data.animations[i];animation::Clip clip;clip.name=source.name?source.name:"";
    for(cgltf_size j=0;j<source.channels_count;j++){
      const auto &input=source.channels[j];const auto *sampler=input.sampler;animation::Channel channel;
      float end=0;
      if(!sampler||!sampler->input||!sampler->input->count||!cgltf_accessor_read_float(sampler->input,sampler->input->count-1,&end,1)||!std::isfinite(end)||end<0)return false;
      clip.duration=std::max(clip.duration,double(end));
      if(!input.target_node||!sampler||(input.target_path!=cgltf_animation_path_type_translation&&input.target_path!=cgltf_animation_path_type_rotation&&input.target_path!=cgltf_animation_path_type_scale&&input.target_path!=cgltf_animation_path_type_weights)){clip.unsupportedReason="only node transforms and morph weights are supported";continue;}
      channel.node=int(input.target_node-data.nodes);
      if(out.nodes[channel.node].matrixOnly&&input.target_path!=cgltf_animation_path_type_weights){clip.unsupportedReason="animated matrix nodes are unsupported";continue;}
      channel.path=input.target_path==cgltf_animation_path_type_weights?animation::Path::Weights:input.target_path==cgltf_animation_path_type_rotation?animation::Path::Rotation:input.target_path==cgltf_animation_path_type_scale?animation::Path::Scale:animation::Path::Translation;
      if(sampler->interpolation!=cgltf_interpolation_type_step&&sampler->interpolation!=cgltf_interpolation_type_linear&&sampler->interpolation!=cgltf_interpolation_type_cubic_spline)return false;
      channel.weightCount=out.nodes[channel.node].weights.size();
      const bool weights=channel.path==animation::Path::Weights;
      const size_t dimensions=weights?channel.weightCount:channel.path==animation::Path::Rotation?4:3;
      if(!dimensions)return false;
      channel.interpolation=sampler->interpolation==cgltf_interpolation_type_step?animation::Interpolation::Step:sampler->interpolation==cgltf_interpolation_type_cubic_spline?animation::Interpolation::CubicSpline:animation::Interpolation::Linear;
      const size_t multiplier=channel.interpolation==animation::Interpolation::CubicSpline?3:1;
      if(!sampler->input||!sampler->output||sampler->input->type!=cgltf_type_scalar||sampler->input->count==0||sampler->output->count!=sampler->input->count*multiplier*(weights?dimensions:1)||cgltf_num_components(sampler->output->type)!=(weights?1:dimensions)||sampler->output->is_sparse)return false;
      for(cgltf_size k=0;k<sampler->input->count;k++){
        float time;if(!cgltf_accessor_read_float(sampler->input,k,&time,1)||!std::isfinite(time)||time<0||(!channel.times.empty()&&time<=channel.times.back()))return false;
        channel.times.push_back(time);
      }
      for(cgltf_size k=0;k<sampler->output->count;k++){
        float value[4];const size_t components=weights?1:dimensions;if(!cgltf_accessor_read_float(sampler->output,k,value,components))return false;
        for(size_t c=0;c<components;c++){if(!std::isfinite(value[c]))return false;channel.values.push_back(value[c]);}
      }
      clip.duration=std::max(clip.duration,double(channel.times.back()));clip.channels.push_back(std::move(channel));
    }
    if(clip.channels.empty()&&clip.unsupportedReason.empty())clip.unsupportedReason="clip has no supported channels";
    out.animations.push_back(std::move(clip));
  }
  return true;
}
bool loadModelFile(const std::string &path,ModelAsset &out) {
  cgltf_options options{};cgltf_data *data=nullptr;
  auto result=cgltf_parse_file(&options,path.c_str(),&data);
  if(result!=cgltf_result_success){std::fprintf(stderr,"[model] parse failed %s: %d\n",path.c_str(),(int)result);return false;}
  result=cgltf_load_buffers(&options,data,path.c_str());
  if(result!=cgltf_result_success||cgltf_validate(data)!=cgltf_result_success){std::fprintf(stderr,"[model] invalid glTF %s\n",path.c_str());cgltf_free(data);return false;}
  out.path=path;out.hasAnimation=data->animations_count>0;out.hasSkin=data->skins_count>0;
  if(!loadAnimations(*data,out)){std::fprintf(stderr,"[model] invalid animation data in %s\n",path.c_str());cgltf_free(data);return false;}
  std::vector<animation::Node> bindNodes;std::vector<animation::Matrix> bindWorld;
  std::vector<std::vector<animation::Matrix>> bindPalettes;
  if(out.hasSkin){animation::evaluate(out.nodes,out.nodeOrder,animation::Clip{},0,bindNodes,bindWorld);animation::palettes(out.skins,bindWorld,bindPalettes);}
  float low[3]={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  float high[3]={-low[0],-low[1],-low[2]};
  out.low[0]=out.low[1]=out.low[2]=std::numeric_limits<float>::max();out.high[0]=out.high[1]=out.high[2]=-std::numeric_limits<float>::max();
  for(cgltf_size i=0;i<data->nodes_count;i++){
    const auto &node=data->nodes[i];if(!node.mesh)continue;
    for(cgltf_size j=0;j<node.mesh->primitives_count;j++){
      const size_t before=out.primitives.size();
      if(!appendPrimitive(node,node.mesh->primitives[j],out,low,high,node.skin?int(node.skin-data->skins):-1,bindPalettes)){std::fprintf(stderr,"[model] unsupported primitive in %s\n",path.c_str());cgltf_free(data);return false;}
      if(out.primitives.size()>before)out.primitives.back().node=int(i);
    }
  }
  cgltf_free(data);
  if(out.primitives.empty())return false;
  float extent=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]});
  if(!std::isfinite(extent)||extent<=0)return false;
  float scale=1.f/extent;out.normalization=scale;
  for(int axis=0;axis<3;axis++)out.center[axis]=(low[axis]+high[axis])*.5f;
  for(auto &part:out.primitives)for(size_t i=0;i<part.vertices.size();i+=5)for(int axis=0;axis<3;axis++)part.vertices[i+axis]=(part.vertices[i+axis]-(low[axis]+high[axis])*.5f)*scale;
  // Picking bounds must use the same centered, normalized space as vertices.
  for(int axis=0;axis<3;axis++){
    out.low[axis]=(low[axis]-high[axis])*.5f*scale;
    out.high[axis]=(high[axis]-low[axis])*.5f*scale;
  }
  prepareSkinAsset(out);
  const auto pickingStart=std::chrono::steady_clock::now();
  picking::build(out);
  const double pickingBuildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
  const size_t pickingBytes=out.pickNodes.capacity()*sizeof(picking::Node)+out.pickTriangles.capacity()*sizeof(picking::Triangle);
  std::fprintf(stdout,"[picking] nodes=%zu triangles=%zu build_ms=%.3f reserved_bytes=%zu\n",out.pickNodes.size(),out.pickTriangles.size(),pickingBuildMs,pickingBytes);
  std::fprintf(stdout,"[model] loaded %s parts=%zu triangles=%zu skin=%d morph=%d animation=%d\n",path.c_str(),out.primitives.size(),[&]{size_t n=0;for(auto &p:out.primitives)n+=p.vertexCount/3;return n;}(),out.hasSkin,out.hasMorph,out.hasAnimation);
  std::fflush(stdout);
  return true;
}
static void uploadLod(const ModelAsset &asset,PrimitiveAsset &part,const mesh::Indexed &indexed){
  if(!asset.lod||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode!=0||part.vertexCount<300)return;
#ifdef SF_MODEL_LOD_EXPERIMENT
  if(std::getenv("SF_GPU_MODEL_LOD"))return;
#endif
  const float weights[2]={.1f,.1f};const size_t vertices=indexed.vertices.size()/5;
  for(int level=0;level<2;level++){
    std::vector<uint32_t> reduced(indexed.indices.size());float error=0;const size_t target=indexed.indices.size()/(level==0?2:4)/3*3;
    const size_t count=meshopt_simplifyWithAttributes(reduced.data(),indexed.indices.data(),indexed.indices.size(),indexed.vertices.data(),vertices,5*sizeof(float),indexed.vertices.data()+3,5*sizeof(float),weights,2,nullptr,target,.001f,meshopt_SimplifyLockBorder,&error);
    if(!count||count%3||count>indexed.indices.size()*4/5||!std::isfinite(error)||error>.001001f)continue;
    if(level==1&&count==size_t(part.lod[0].count))continue;
    reduced.resize(count);meshopt_optimizeVertexCache(reduced.data(),reduced.data(),count,vertices);
    auto &lod=part.lod[level];glGenBuffers(1,&lod.indexBuffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,lod.indexBuffer);
    const size_t bytes=count*(part.indexType==GL_UNSIGNED_SHORT?2:4);
    if(part.indexType==GL_UNSIGNED_SHORT){std::vector<uint16_t> packed(reduced.begin(),reduced.end());glBufferData(GL_ELEMENT_ARRAY_BUFFER,bytes,packed.data(),GL_STATIC_DRAW);}else glBufferData(GL_ELEMENT_ARRAY_BUFFER,bytes,reduced.data(),GL_STATIC_DRAW);
    GLint allocated=0;glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&allocated);
    if(size_t(allocated)!=bytes){glDeleteBuffers(1,&lod.indexBuffer);lod={};continue;}
    lod.count=GLsizei(count);lod.error=error;
  }
}
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
static void prepareVertexArrays(const PrimitiveAsset &part){
 if(part.vertexArrays[0]||!part.vbo)return;
 GLint previous=0,arrayBuffer=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previous);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer);
 for(int level=0;level<3;level++){
  if(level&&!part.lod[level-1].indexBuffer)continue;
  glGenVertexArrays(1,&part.vertexArrays[level]);glBindVertexArray(part.vertexArrays[level]);
  glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer?modelIndexBuffer(part,level):0);
 }
 glBindVertexArray(GLuint(previous));glBindBuffer(GL_ARRAY_BUFFER,GLuint(arrayBuffer));
}
#endif
void uploadModel(ModelAsset &asset){
  if(asset.uploaded)return;
  for(auto &part:asset.primitives){
    glGenBuffers(1,&part.vbo);glBindBuffer(GL_ARRAY_BUFFER,part.vbo);
    auto indexed=mesh::index(part.vertices);
    const size_t unique=indexed.vertices.size()/5;
    part.indexType=unique<=65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
    const size_t indexSize=part.indexType==GL_UNSIGNED_SHORT?sizeof(uint16_t):sizeof(uint32_t);
    if(indexed.vertices.size()*sizeof(float)+size_t(part.vertexCount)*indexSize<part.vertices.size()*sizeof(float)){
#ifdef SF_MODEL_LOD_EXPERIMENT
      lodExperiment::simplify(asset,part,indexed);
#endif
      // Reorder complete oriented triangles only. Blend order and CPU-deformed
      // streams keep their original order; all CPU geometry remains unchanged.
      if(asset.optimizeVertexCache&&!asset.hasAnimation&&!asset.hasSkin&&part.alphaMode!=2&&indexed.indices.size()%3==0){
        meshopt_optimizeVertexCache(indexed.indices.data(),indexed.indices.data(),indexed.indices.size(),unique);
        meshopt_optimizeVertexFetch(indexed.vertices.data(),indexed.indices.data(),indexed.indices.size(),indexed.vertices.data(),unique,5*sizeof(float));
      }
#ifdef SF_MODEL_LOD_EXPERIMENT
      // Retain the original index capacity for matched allocation comparisons.
      indexed.indices.resize(size_t(part.vertexCount),0);
#endif
      glBufferData(GL_ARRAY_BUFFER,indexed.vertices.size()*sizeof(float),indexed.vertices.data(),GL_STATIC_DRAW);
      part.gpuVertexCount=GLsizei(unique);
      glGenBuffers(1,&part.indexBuffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer);
      if(part.indexType==GL_UNSIGNED_SHORT){std::vector<uint16_t> indices(indexed.indices.begin(),indexed.indices.end());glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint16_t),indices.data(),GL_STATIC_DRAW);}
      else glBufferData(GL_ELEMENT_ARRAY_BUFFER,indexed.indices.size()*sizeof(uint32_t),indexed.indices.data(),GL_STATIC_DRAW);
      uploadLod(asset,part,indexed);
    }else{
      glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_STATIC_DRAW);part.gpuVertexCount=part.vertexCount;
    }
    glGenTextures(1,&part.texture);glBindTexture(GL_TEXTURE_2D,part.texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,part.width,part.height,0,GL_RGBA,GL_UNSIGNED_BYTE,part.rgba.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    part.rgba.clear();
  }
  uint64_t lodBytes=0;for(const auto &part:asset.primitives)for(const auto &lod:part.lod)if(lod.indexBuffer)lodBytes+=uint64_t(lod.count)*(part.indexType==GL_UNSIGNED_SHORT?2:4);
  if(lodBytes){std::printf("[model] lod path=%s index_bytes=%llu\n",asset.path.c_str(),(unsigned long long)lodBytes);std::fflush(stdout);}
  if(asset.skinEligible)for(auto &part:asset.primitives){
    glGenBuffers(1,&part.skinVbo);glBindBuffer(GL_ARRAY_BUFFER,part.skinVbo);glBufferData(GL_ARRAY_BUFFER,part.skinVertices.size()*sizeof(float),part.skinVertices.data(),GL_STATIC_DRAW);
    glGenBuffers(1,&part.skinIbo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,part.skinIndices.size()*sizeof(uint32_t),part.skinIndices.data(),GL_STATIC_DRAW);
  }
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
  for(const auto &part:asset.primitives)prepareVertexArrays(part);
#endif
  asset.uploaded=true;
}
void deleteModel(ModelAsset &asset){
  for(auto &part:asset.primitives){
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
    glDeleteVertexArrays(3,part.vertexArrays);std::fill_n(part.vertexArrays,3,0);
#endif
    if(part.skinVbo)glDeleteBuffers(1,&part.skinVbo);if(part.skinIbo)glDeleteBuffers(1,&part.skinIbo);part.skinVbo=part.skinIbo=0;
#ifdef SF_MODEL_LOD_EXPERIMENT
    lodExperiment::results.erase(&part);
#endif
    for(auto &lod:part.lod){if(lod.indexBuffer)glDeleteBuffers(1,&lod.indexBuffer);lod={};}
    if(part.vbo)glDeleteBuffers(1,&part.vbo);if(part.indexBuffer)glDeleteBuffers(1,&part.indexBuffer);if(part.texture)glDeleteTextures(1,&part.texture);part.vbo=part.indexBuffer=part.texture=0;part.gpuVertexCount=0;}
  asset.uploaded=false;
}

#ifdef SF_VERTEX_ARRAY_EXPERIMENT
void drawModelPrimitive(const PrimitiveAsset &part,GLuint expandedVbo,int lodLevel,const PrimitiveAsset *deformed){
#else
void drawModelPrimitive(const PrimitiveAsset &part,GLuint expandedVbo,int lodLevel){
#endif
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
  const auto *owner=expandedVbo?deformed:&part;
  const int level=!expandedVbo&&modelLodLevel(part,lodLevel)?lodLevel:0;
  if(vertexArrayCachingEnabled&&owner&&owner->vertexArrays[level]&&(!expandedVbo||owner->vbo==expandedVbo)){
    glBindVertexArray(owner->vertexArrays[level]);
    if(!expandedVbo&&part.indexBuffer)glDrawElements(GL_TRIANGLES,modelDrawVertexCount(part,lodLevel),part.indexType,nullptr);
    else glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
    glBindVertexArray(0);return;
  }
#endif
  glBindBuffer(GL_ARRAY_BUFFER,expandedVbo?expandedVbo:part.vbo);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
  if(!expandedVbo&&part.indexBuffer){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,modelIndexBuffer(part,lodLevel));glDrawElements(GL_TRIANGLES,modelDrawVertexCount(part,lodLevel),part.indexType,nullptr);}
  else glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
}

ModelPose::~ModelPose(){
 for(auto texture:skinTextures)if(texture)glDeleteTextures(1,&texture);
 deleteModel(geometry);
}
static void deformPose(const ModelAsset &asset,ModelPose &pose){
  if(pose.geometry.primitives.empty()){
    pose.geometry.primitives.resize(asset.primitives.size());
    for(size_t i=0;i<asset.primitives.size();i++){pose.geometry.primitives[i].vertices=asset.primitives[i].vertices;pose.geometry.primitives[i].vertexCount=asset.primitives[i].vertexCount;}
    pose.geometry.pickTriangles=asset.pickTriangles;pose.geometry.pickNodes=asset.pickNodes;
  }
#ifdef SF_ANIMATION_PROFILE
  const auto profileDeformStart=AnimationProfileClock::now();
#endif
  pose.parts.resize(asset.primitives.size());bool changed=false;
  pose.radius=0;
  for(int axis=0;axis<3;axis++){pose.geometry.low[axis]=std::numeric_limits<float>::max();pose.geometry.high[axis]=-std::numeric_limits<float>::max();}
  for(size_t i=0;i<asset.primitives.size();i++){
    const auto &source=asset.primitives[i];auto &target=pose.geometry.primitives[i];const auto &matrix=pose.world[source.node];
    auto &cache=pose.parts[i];
    const bool update=!rigidPoseCachingEnabled||source.skin>=0||!source.morphPositions.empty()||!cache.valid||cache.world!=matrix;
    if(update){
      changed=true;cache.uploadDirty=true;cache.radius=0;
      for(int axis=0;axis<3;axis++){cache.low[axis]=std::numeric_limits<float>::max();cache.high[axis]=-std::numeric_limits<float>::max();}
    for(size_t v=0;v<source.localVertices.size();v+=5){
      const float *base=source.localVertices.data()+v;float p[3]={base[0],base[1],base[2]};float radiusSquared=0;
      for(size_t t=0;t<source.morphPositions.size();t++)for(int axis=0;axis<3;axis++)p[axis]+=pose.nodes[source.node].weights[t]*source.morphPositions[t][v/5*3+axis];
      const auto world=source.skin>=0?animation::skinPoint(p,pose.palettes[source.skin],source.influences,v/5):animation::transformPoint(matrix,p);
      for(int axis=0;axis<3;axis++){
        const float value=(world[axis]-asset.center[axis])*asset.normalization;
        target.vertices[v+axis]=value;radiusSquared+=value*value;
        cache.low[axis]=std::min(cache.low[axis],value);cache.high[axis]=std::max(cache.high[axis],value);
      }
      cache.radius=std::max(cache.radius,std::sqrt(radiusSquared));
    }
      cache.world=matrix;cache.valid=true;
#ifdef SF_ANIMATION_PROFILE
      if(animationProfileFrame)animationProfileFrame->vertices+=source.localVertices.size()/5;
#endif
    }
    pose.radius=std::max(pose.radius,cache.radius);
    for(int axis=0;axis<3;axis++){pose.geometry.low[axis]=std::min(pose.geometry.low[axis],cache.low[axis]);pose.geometry.high[axis]=std::max(pose.geometry.high[axis],cache.high[axis]);}
  }
#ifdef SF_ANIMATION_PROFILE
  if(animationProfileFrame)animationProfileFrame->deformBoundsMs+=animationProfileMs(profileDeformStart);
  const auto profileBvhStart=AnimationProfileClock::now();
#endif
  if(changed)picking::refit(pose.geometry);
  pose.dirty=std::any_of(pose.parts.begin(),pose.parts.end(),[](const auto &part){return part.uploadDirty;});
#ifdef SF_ANIMATION_PROFILE
  if(animationProfileFrame)animationProfileFrame->bvhMs+=animationProfileMs(profileBvhStart);
#endif
}
void evaluateModelPose(const ModelAsset &asset,const animation::Playback &state,ModelPose &pose){
#ifdef SF_ANIMATION_PROFILE
  const auto profilePoseStart=AnimationProfileClock::now();
#endif
  animation::evaluateNodes(asset.nodes,asset.animations[state.clip],state.time,pose.nodes);
  const auto &blend=state.transition;
  if(blend.active()){
    if(blend.source.clip>=0)animation::evaluateNodes(asset.nodes,asset.animations[blend.source.clip],blend.source.time,pose.sourceNodes);
    animation::blendNodes(blend.source.clip>=0?pose.sourceNodes:blend.frozen,pose.nodes,blend.elapsed/blend.duration);
  }
  animation::worldMatrices(pose.nodes,asset.nodeOrder,pose.world);
  animation::palettes(asset.skins,pose.world,pose.palettes);
#ifdef SF_ANIMATION_PROFILE
  if(animationProfileFrame){animationProfileFrame->poseMs+=animationProfileMs(profilePoseStart);animationProfileFrame->evaluations++;}

#endif
  pose.gpuSkin=asset.skinEligible&&gpuSkinningEnabled;pose.pickingValid=false;
  if(pose.gpuSkin){skinPoseBounds(asset,pose);pose.dirty=true;return;}
  deformPose(asset,pose);
#ifdef SF_GPU_SKIN_EXPERIMENT
  if(asset.skinEligible)skinPoseBounds(asset,pose); // matched comparison layout
#endif
}

void uploadModelPose(ModelPose &pose){
  if(!pose.dirty)return;
  if(pose.gpuSkin){uploadSkinPose(pose);return;}
#ifdef SF_ANIMATION_PROFILE
  const auto profileUploadStart=AnimationProfileClock::now();
  if(animationProfileFrame)animationProfileFrame->uploads++;
#endif
  pose.parts.resize(pose.geometry.primitives.size());
  for(size_t i=0;i<pose.geometry.primitives.size();i++){
    auto &part=pose.geometry.primitives[i];auto &cache=pose.parts[i];
    if(part.vbo&&cache.valid&&!cache.uploadDirty)continue;
#ifdef SF_ANIMATION_PROFILE
    if(animationProfileFrame)animationProfileFrame->uploadBytes+=part.vertices.size()*sizeof(float);
#endif
    part.gpuVertexCount=part.vertexCount;
    if(!part.vbo){glGenBuffers(1,&part.vbo);glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_DYNAMIC_DRAW);}
    else{glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glBufferSubData(GL_ARRAY_BUFFER,0,part.vertices.size()*sizeof(float),part.vertices.data());}
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
    prepareVertexArrays(part);
#endif
    cache.uploadDirty=false;
  }
  pose.dirty=false;
#ifdef SF_ANIMATION_PROFILE
  if(animationProfileFrame)animationProfileFrame->uploadMs+=animationProfileMs(profileUploadStart);
#endif
}

void prepareSkinPicking(const ModelAsset &asset,ModelPose &pose){
 if(!pose.gpuSkin||pose.pickingValid)return;
 const bool dirty=pose.dirty;const float radius=pose.radius;
 std::array<float,3> low,high;std::copy_n(pose.geometry.low,3,low.begin());std::copy_n(pose.geometry.high,3,high.begin());
 deformPose(asset,pose);pose.dirty=dirty;pose.radius=radius;
 std::copy(low.begin(),low.end(),pose.geometry.low);std::copy(high.begin(),high.end(),pose.geometry.high);pose.pickingValid=true;
}
