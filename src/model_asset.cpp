#include "model_asset.h"
#include "mesh_index.h"
#include "scope_exit.h"
#include <new>
#include <meshoptimizer.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <unordered_map>
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

// A model's images decode once, in parallel, before its primitives are built;
// each primitive copies the shared pixels. Failed entries decode again on demand.
struct DecodedImage {std::vector<unsigned char> rgba;int width=0,height=0;bool ok=false;};
static thread_local const std::unordered_map<const cgltf_image*,DecodedImage> *decodedImages=nullptr;
static DecodedImage decodeImage(const cgltf_image &image) try {
  DecodedImage result;
  const auto *bytes=image.buffer_view?static_cast<const unsigned char*>(cgltf_buffer_view_data(image.buffer_view)):nullptr;
  if(!bytes)return result;
  int channels=0;auto *pixels=stbi_load_from_memory(bytes,(int)image.buffer_view->size,&result.width,&result.height,&channels,4);
  if(!pixels)return result;
  ScopeExit release{[&]{stbi_image_free(pixels);}};
  result.rgba.assign(pixels,pixels+(size_t)result.width*result.height*4);result.ok=true;
  return result;
} catch(const std::bad_alloc&){return {};}
// Only images the materials use: base color, plus the material maps for PBR.
static std::unordered_map<const cgltf_image*,DecodedImage> decodeImages(const cgltf_data &data,bool pbr){
  std::vector<const cgltf_image*> used;
  auto add=[&](const cgltf_texture_view &view){if(view.texture&&view.texture->image&&std::find(used.begin(),used.end(),view.texture->image)==used.end())used.push_back(view.texture->image);};
  for(cgltf_size i=0;i<data.materials_count;i++){
    const auto &m=data.materials[i];
    if(m.has_pbr_metallic_roughness)add(m.pbr_metallic_roughness.base_color_texture);
    if(!pbr)continue;
    add(m.normal_texture);add(m.pbr_metallic_roughness.metallic_roughness_texture);add(m.occlusion_texture);add(m.emissive_texture);
    if(m.has_sheen){add(m.sheen.sheen_color_texture);add(m.sheen.sheen_roughness_texture);}
  }
  std::vector<DecodedImage> results(used.size());std::atomic<size_t> next{0};
  auto work=[&]{for(size_t i;(i=next++)<results.size();)results[i]=decodeImage(*used[i]);};
  const size_t threads=std::min<size_t>(results.size(),std::max(1u,std::thread::hardware_concurrency()));
  std::vector<std::thread> pool;for(size_t t=1;t<threads;t++)pool.emplace_back(work);
  work();for(auto &thread:pool)thread.join();
  std::unordered_map<const cgltf_image*,DecodedImage> decoded;
  for(size_t i=0;i<results.size();i++)decoded.emplace(used[i],std::move(results[i]));
  return decoded;
}
static bool decodeBaseColor(const cgltf_image *image,PrimitiveAsset &part) {
  if(!image){part.rgba={255,255,255,255};return true;}
  if(decodedImages)if(auto found=decodedImages->find(image);found!=decodedImages->end()&&found->second.ok){
    part.rgba=found->second.rgba;part.width=found->second.width;part.height=found->second.height;return true;
  }
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
#include "pbr_asset.h"

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
  part.untextured=!image;
  if(!decodeBaseColor(image,part)){std::fprintf(stderr,"[model] base-color image decode failed\n");return false;}
  if(out.pbr&&!readPbrMaterial(material,view,part))return false;
  if(out.pbr&&view&&view->texture&&view->texture->sampler){const auto &s=*view->texture->sampler;if(s.wrap_s)part.baseWrapS=s.wrap_s;if(s.wrap_t)part.baseWrapT=s.wrap_t;if(s.min_filter)part.baseMinFilter=s.min_filter;if(s.mag_filter)part.baseMagFilter=s.mag_filter;}
  animation::Matrix transform;cgltf_node_transform_world(&node,transform.data());part.surfaceMirrored=surfaceFrame::determinant(transform)<0;
  cgltf_size count=source.indices?source.indices->count:positions->count;
  part.vertices.reserve(count*5);
  if(out.pbr||out.hasAnimation||out.hasSkin||out.hasMorph)part.localVertices.reserve(count*5);
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
    if(out.pbr||out.hasAnimation||out.hasSkin||out.hasMorph)part.localVertices.insert(part.localVertices.end(),{p[0],p[1],p[2],uv[0],uv[1]});
    for(int axis=0;axis<3;axis++){low[axis]=std::min(low[axis],world[axis]);high[axis]=std::max(high[axis],world[axis]);}
    part.vertices.insert(part.vertices.end(),{world[0],world[1],world[2],uv[0],uv[1]});
  }
  part.vertexCount=(GLsizei)count;
  if(out.pbr&&!readPbrSurface(source,positions,uvs,node,part,transform,bindPalettes))return false;
  if(source.targets_count)out.hasMorph=true;
  if(out.pbr&&!part.materialUnlit&&std::any_of(part.sheen,part.sheen+3,[](float c){return c>0;}))out.sheenLookup=sharedSheenLookup();
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
static bool sameRigidMaterial(const PrimitiveAsset &a,const PrimitiveAsset &b){
 return a.untextured&&b.untextured&&a.alphaMode!=2&&a.alphaMode==b.alphaMode&&a.alphaCutoff==b.alphaCutoff&&a.doubleSided==b.doubleSided&&std::equal(a.factor,a.factor+4,b.factor)&&std::equal(a.sheen,a.sheen+3,b.sheen);
}
bool drawRigidBatch(const ModelAsset &asset,const ModelPose &pose,size_t &primitive){
#ifdef SF_TEST_HOOKS
 ++rigidBatchStats.draws;
#endif
 if(!rigidBatchEnabled||!asset.rigidBatchEligible||pose.gpuSkin||!pose.geometry.batchVbo)return false;
 const auto &part=asset.primitives[primitive];size_t end=primitive+1;
 while(end<asset.primitives.size()&&sameRigidMaterial(part,asset.primitives[end])&&(part.doubleSided||modelPrimitiveMirrored(part,&pose)==modelPrimitiveMirrored(asset.primitives[end],&pose)))++end;
 if(end==primitive+1)return false;
 const GLint first=pose.parts[primitive].streamFirst;GLsizei count=0;
 for(size_t p=primitive;p<end;p++)count+=asset.primitives[p].vertexCount;
 glBindBuffer(GL_ARRAY_BUFFER,pose.geometry.batchVbo);
 glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
 glDrawArrays(GL_TRIANGLES,first,count);
#ifdef SF_TEST_HOOKS
 rigidBatchStats.mergedParts+=end-primitive;
#endif
 primitive=end-1;return true;
}
bool loadModelFile(const std::string &path,ModelAsset &out) {
  cgltf_options options{};cgltf_data *data=nullptr;
  auto result=cgltf_parse_file(&options,path.c_str(),&data);
  if(result!=cgltf_result_success){std::fprintf(stderr,"[model] parse failed %s: %d\n",path.c_str(),(int)result);return false;}
  result=cgltf_load_buffers(&options,data,path.c_str());
  if(result!=cgltf_result_success||cgltf_validate(data)!=cgltf_result_success){std::fprintf(stderr,"[model] invalid glTF %s\n",path.c_str());cgltf_free(data);return false;}
  const auto images=decodeImages(*data,out.pbr);decodedImages=&images;ScopeExit forgetImages{[]{decodedImages=nullptr;}};
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
  if(!out.pbr)prepareSkinAsset(out);
  uint64_t streamVertices=0;for(const auto &part:out.primitives)streamVertices+=uint64_t(part.vertexCount);
  if(!out.pbr&&out.hasAnimation&&!out.hasSkin&&!out.hasMorph&&streamVertices<=uint64_t(std::numeric_limits<GLsizei>::max())&&std::all_of(out.primitives.begin(),out.primitives.end(),[](const auto &p){return p.vertexCount>0&&p.vertexCount%3==0;}))
    for(size_t i=1;i<out.primitives.size();i++)if(sameRigidMaterial(out.primitives[i-1],out.primitives[i]))out.rigidBatchEligible=true;
  const auto pickingStart=std::chrono::steady_clock::now();
  picking::build(out);
  const double pickingBuildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
  const size_t pickingBytes=out.pickNodes.capacity()*sizeof(picking::Node)+out.pickTriangles.capacity()*sizeof(picking::Triangle);
  std::fprintf(stdout,"[picking] nodes=%zu triangles=%zu build_ms=%.3f reserved_bytes=%zu\n",out.pickNodes.size(),out.pickTriangles.size(),pickingBuildMs,pickingBytes);
  std::fprintf(stdout,"[model] loaded %s parts=%zu triangles=%zu skin=%d morph=%d animation=%d\n",path.c_str(),out.primitives.size(),[&]{size_t n=0;for(auto &p:out.primitives)n+=p.vertexCount/3;return n;}(),out.hasSkin,out.hasMorph,out.hasAnimation);
  std::fflush(stdout);
  return true;
}
void buildShapeAsset(const std::vector<shapeMesh::Part> &parts,ModelAsset &out,float &extent,std::array<float,3> &center){
  using shapeMesh::V;
  out.pbr=true;out.generated=true;
  std::vector<std::pair<std::array<float,4>,size_t>> materials;
  float low[3]={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high[3]={-low[0],-low[1],-low[2]};
  for(const auto &part:parts){
    const std::array<float,4> key{part.color[0],part.color[1],part.color[2],float(part.finish)};
    auto found=std::find_if(materials.begin(),materials.end(),[&](const auto &m){return m.first==key;});
    if(found==materials.end()){
      PrimitiveAsset primitive;primitive.rgba={255,255,255,255};primitive.untextured=true;
      const auto finish=shapeMesh::values(part.finish);primitive.metallic=finish.metallic;primitive.roughness=finish.roughness;
      for(int c=0;c<3;c++){primitive.factor[c]=part.color[c];primitive.emissive[c]=finish.emissive?part.color[c]:0;}
      out.primitives.push_back(std::move(primitive));materials.push_back({key,out.primitives.size()-1});found=materials.end()-1;
    }
    auto &primitive=out.primitives[found->second];
    // Object conventions: scale, rotate, then place; model Z is scene Z negated.
    for(const auto &vertex:shapeMesh::generate(part.shape,part.scale)){
      const V p=shapeMesh::rotate(part.rotation,vertex.position),n=shapeMesh::rotate(part.rotation,vertex.normal);
      const V world{p[0]+part.position[0],p[1]+part.position[1],p[2]-part.position[2]};
      // Any unit tangent perpendicular to the normal; shape assets have no normal map.
      const V axis=std::abs(n[0])<.9f?V{1,0,0}:V{0,1,0};
      const auto t=surfaceFrame::unit(surfaceFrame::cross(surfaceFrame::cross(n,axis),n));
      primitive.vertices.insert(primitive.vertices.end(),{world[0],world[1],world[2],0,0});
      primitive.surface.insert(primitive.surface.end(),{n[0],n[1],n[2],t[0],t[1],t[2],1,0,0});
      for(int a=0;a<3;a++){low[a]=std::min(low[a],world[a]);high[a]=std::max(high[a],world[a]);}
    }
  }
  for(auto &primitive:out.primitives)primitive.vertexCount=GLsizei(primitive.vertices.size()/5);
  extent=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]});
  const float scale=1.f/extent;out.normalization=scale;
  for(int a=0;a<3;a++){out.center[a]=(low[a]+high[a])*.5f;out.low[a]=(low[a]-high[a])*.5f*scale;out.high[a]=(high[a]-low[a])*.5f*scale;}
  center={out.center[0],out.center[1],-out.center[2]};
  for(auto &primitive:out.primitives)for(size_t i=0;i<primitive.vertices.size();i+=5)for(int a=0;a<3;a++)primitive.vertices[i+a]=(primitive.vertices[i+a]-out.center[a])*scale;
  picking::build(out);
}
static void uploadLod(const ModelAsset &asset,PrimitiveAsset &part,const mesh::Indexed &indexed){
  if(!asset.lod||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode!=0||part.vertexCount<300)return;
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
std::vector<DepthCluster> buildDepthClusters(const float *positions,size_t stride,const uint32_t *indices,size_t indexCount,std::vector<uint32_t> &ordered){
  const size_t triangles=indexCount/3;
  auto index=[&](size_t i){return indices?indices[i]:uint32_t(i);};
  auto position=[&](size_t i){return positions+size_t(index(i))*stride;};
  std::vector<std::array<float,3>> centers(triangles);
  float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
  for(size_t t=0;t<triangles;t++)for(int axis=0;axis<3;axis++){
    const float center=(position(t*3)[axis]+position(t*3+1)[axis]+position(t*3+2)[axis])/3;
    centers[t][axis]=center;low[axis]=std::min(low[axis],center);high[axis]=std::max(high[axis],center);
  }
  auto spread=[](uint32_t v){v&=1023;v=(v|(v<<16))&0x030000FF;v=(v|(v<<8))&0x0300F00F;v=(v|(v<<4))&0x030C30C3;v=(v|(v<<2))&0x09249249;return v;};
  std::vector<std::pair<uint32_t,uint32_t>> keys(triangles);
  for(size_t t=0;t<triangles;t++){
    uint32_t code=0;
    for(int axis=0;axis<3;axis++){
      const float extent=high[axis]-low[axis];
      const float unit=extent>0?std::clamp((centers[t][axis]-low[axis])/extent,0.f,1.f):0.f;
      code|=spread(uint32_t(unit*1023))<<axis;
    }
    keys[t]={code,uint32_t(t)};
  }
  std::sort(keys.begin(),keys.end());
  ordered.clear();ordered.reserve(triangles*3);
  std::vector<DepthCluster> clusters;clusters.reserve((triangles+kDepthClusterTriangles-1)/kDepthClusterTriangles);
  for(size_t start=0;start<triangles;start+=kDepthClusterTriangles){
    DepthCluster cluster;cluster.first=uint32_t(start*3);
    for(int axis=0;axis<3;axis++){cluster.low[axis]=INFINITY;cluster.high[axis]=-INFINITY;}
    for(size_t k=start;k<std::min(triangles,start+kDepthClusterTriangles);k++)for(int corner=0;corner<3;corner++){
      const size_t source=size_t(keys[k].second)*3+corner;ordered.push_back(index(source));
      for(int axis=0;axis<3;axis++){const float value=position(source)[axis];cluster.low[axis]=std::min(cluster.low[axis],value);cluster.high[axis]=std::max(cluster.high[axis],value);}
    }
    cluster.count=uint32_t(ordered.size())-cluster.first;clusters.push_back(cluster);
  }
  return clusters;
}
// Optional: a primitive without clusters draws whole in external depth passes.
static void uploadDepthClusters(const ModelAsset &asset,PrimitiveAsset &part,const float *positions,size_t stride,const uint32_t *indices,size_t indexCount) try {
  if(asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode==2||indexCount%3||indexCount/3<kDepthClusterMinTriangles)return;
  std::vector<uint32_t> ordered;auto clusters=buildDepthClusters(positions,stride,indices,indexCount,ordered);
  const uint32_t largest=*std::max_element(ordered.begin(),ordered.end());
  const GLenum type=largest<65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
  GLuint buffer=0;glGenBuffers(1,&buffer);if(!buffer)return;
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffer);
  uint64_t bytes=0;
  if(type==GL_UNSIGNED_SHORT){std::vector<uint16_t> packed(ordered.begin(),ordered.end());bytes=packed.size()*sizeof(uint16_t);glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(bytes),packed.data(),GL_STATIC_DRAW);}
  else{bytes=ordered.size()*sizeof(uint32_t);glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(bytes),ordered.data(),GL_STATIC_DRAW);}
  GLint allocated=0;glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&allocated);
  if(allocated<0||uint64_t(allocated)!=bytes){glDeleteBuffers(1,&buffer);return;}
  part.depthClusters={buffer,type,bytes,std::move(clusters)};
} catch(const std::bad_alloc&){}
// Static PBR indexing keeps every shader input in vertex identity and requires
// cache ordering; plain indexing measured slower.
static bool uploadIndexedPbr(const ModelAsset &asset,PrimitiveAsset &part) try {
  if(!asset.pbr||!asset.optimizeVertexCache||asset.hasAnimation||asset.hasSkin||asset.hasMorph||part.alphaMode==2)return false;
  constexpr bool reorder=true;
#ifdef SF_TEST_HOOKS
  if(pbrIndexValidation::failCpu)throw std::bad_alloc();
#endif
  std::vector<float> combined;combined.reserve(size_t(part.vertexCount)*14);
  for(size_t v=0;v<size_t(part.vertexCount);v++){
    combined.insert(combined.end(),part.vertices.begin()+v*5,part.vertices.begin()+(v+1)*5);
    combined.insert(combined.end(),part.surface.begin()+v*9,part.surface.begin()+(v+1)*9);
  }
  auto indexed=mesh::index<14>(combined);const size_t unique=indexed.vertices.size()/14;
  const GLenum type=unique<=65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
  const size_t indexBytes=type==GL_UNSIGNED_SHORT?2:4;
  if(indexed.vertices.size()*sizeof(float)+indexed.indices.size()*indexBytes>=combined.size()*sizeof(float))return false;
  if(reorder){
    meshopt_optimizeVertexCache(indexed.indices.data(),indexed.indices.data(),indexed.indices.size(),unique);
    meshopt_optimizeVertexFetch(indexed.vertices.data(),indexed.indices.data(),indexed.indices.size(),indexed.vertices.data(),unique,14*sizeof(float));
  }
  std::vector<float> positions,surface;positions.reserve(unique*5);surface.reserve(unique*9);
  for(size_t v=0;v<unique;v++){
    positions.insert(positions.end(),indexed.vertices.begin()+v*14,indexed.vertices.begin()+v*14+5);
    surface.insert(surface.end(),indexed.vertices.begin()+v*14+5,indexed.vertices.begin()+(v+1)*14);
  }
  GLuint positionBuffer=0,surfaceBuffer=0,indexBuffer=0;bool published=false;
  ScopeExit rollback{[&]{if(!published){for(GLuint buffer:{positionBuffer,surfaceBuffer,indexBuffer})if(buffer){glDeleteBuffers(1,&buffer);
#ifdef SF_TEST_HOOKS
    ++pbrIndexValidation::rejectedBuffers;pbrIndexValidation::rollbackClean&=!glIsBuffer(buffer);
#endif
  }}}};
  auto upload=[](GLuint &buffer,GLenum target,const void *data,size_t bytes){
    glGenBuffers(1,&buffer);if(!buffer)return false;glBindBuffer(target,buffer);glBufferData(target,bytes,data,GL_STATIC_DRAW);
    GLint allocated=0;glGetBufferParameteriv(target,GL_BUFFER_SIZE,&allocated);return allocated>=0&&uint64_t(allocated)==bytes;
  };
  if(!upload(positionBuffer,GL_ARRAY_BUFFER,positions.data(),positions.size()*sizeof(float)))return false;
#ifdef SF_TEST_HOOKS
  if(pbrIndexValidation::failStage==0)return false;
#endif
  if(!upload(surfaceBuffer,GL_ARRAY_BUFFER,surface.data(),surface.size()*sizeof(float)))return false;
#ifdef SF_TEST_HOOKS
  if(pbrIndexValidation::failStage==1)return false;
#endif
  if(type==GL_UNSIGNED_SHORT){std::vector<uint16_t> packed(indexed.indices.begin(),indexed.indices.end());if(!upload(indexBuffer,GL_ELEMENT_ARRAY_BUFFER,packed.data(),packed.size()*sizeof(uint16_t)))return false;}
  else if(!upload(indexBuffer,GL_ELEMENT_ARRAY_BUFFER,indexed.indices.data(),indexed.indices.size()*sizeof(uint32_t)))return false;
#ifdef SF_TEST_HOOKS
  if(pbrIndexValidation::failStage==2)return false;
#endif
  part.vbo=positionBuffer;part.surfaceVbo=surfaceBuffer;part.indexBuffer=indexBuffer;
  part.gpuVertexCount=GLsizei(unique);part.indexType=type;published=true;
  uploadDepthClusters(asset,part,indexed.vertices.data(),14,indexed.indices.data(),indexed.indices.size());
  return true;
} catch(const std::bad_alloc&){return false;}
#include "pbr_lod.h"
void uploadModel(ModelAsset &asset){
  if(asset.sheenLookup)asset.sheenLookup->upload();
  if(asset.uploaded)return;
  // Simplification is CPU work independent per primitive: prepare every part in
  // parallel, then publish in order below.
  std::vector<pbrLod::Prepared> lods(asset.primitives.size());
  if(asset.pbr&&asset.lod){
    std::atomic<size_t> next{0};auto work=[&]{for(size_t i;(i=next++)<lods.size();)lods[i]=pbrLod::prepare(asset,asset.primitives[i]);};
    std::vector<std::thread> pool;for(size_t t=1;t<std::min<size_t>(lods.size(),std::max(1u,std::thread::hardware_concurrency()));t++)pool.emplace_back(work);
    work();for(auto &thread:pool)thread.join();
  }
  for(auto &part:asset.primitives){
    if(!uploadIndexedPbr(asset,part)){
    glGenBuffers(1,&part.vbo);glBindBuffer(GL_ARRAY_BUFFER,part.vbo);
    auto indexed=asset.pbr?mesh::Indexed{}:mesh::index(part.vertices);
    const size_t unique=indexed.vertices.size()/5;
    part.indexType=unique<=65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
    const size_t indexSize=part.indexType==GL_UNSIGNED_SHORT?sizeof(uint16_t):sizeof(uint32_t);
    if(!asset.pbr&&indexed.vertices.size()*sizeof(float)+size_t(part.vertexCount)*indexSize<part.vertices.size()*sizeof(float)){
      // Reorder complete oriented triangles only. Blend order and CPU-deformed
      // streams keep their original order; all CPU geometry remains unchanged.
      if(asset.optimizeVertexCache&&!asset.hasAnimation&&!asset.hasSkin&&part.alphaMode!=2&&indexed.indices.size()%3==0){
        meshopt_optimizeVertexCache(indexed.indices.data(),indexed.indices.data(),indexed.indices.size(),unique);
        meshopt_optimizeVertexFetch(indexed.vertices.data(),indexed.indices.data(),indexed.indices.size(),indexed.vertices.data(),unique,5*sizeof(float));
      }
      glBufferData(GL_ARRAY_BUFFER,indexed.vertices.size()*sizeof(float),indexed.vertices.data(),GL_STATIC_DRAW);
      part.gpuVertexCount=GLsizei(unique);
      glGenBuffers(1,&part.indexBuffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer);
      if(part.indexType==GL_UNSIGNED_SHORT){std::vector<uint16_t> indices(indexed.indices.begin(),indexed.indices.end());glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint16_t),indices.data(),GL_STATIC_DRAW);}
      else glBufferData(GL_ELEMENT_ARRAY_BUFFER,indexed.indices.size()*sizeof(uint32_t),indexed.indices.data(),GL_STATIC_DRAW);
      uploadLod(asset,part,indexed);
      uploadDepthClusters(asset,part,indexed.vertices.data(),5,indexed.indices.data(),indexed.indices.size());
    }else{
      glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_STATIC_DRAW);part.gpuVertexCount=part.vertexCount;
      uploadDepthClusters(asset,part,part.vertices.data(),5,nullptr,size_t(part.vertexCount));
    }
    }
    if(asset.pbr){
      if(!part.surfaceVbo)uploadSurface(part,GL_STATIC_DRAW);
      pbrLod::publish(asset,part,lods[size_t(&part-asset.primitives.data())]);
      for(size_t i=0;i<6;i++){auto &map=part.maps[i];if(map.rgba.empty())continue;
        glGenTextures(1,&map.texture);glBindTexture(GL_TEXTURE_2D,map.texture);
        glTexImage2D(GL_TEXTURE_2D,0,(i==3||i==4)?GL_SRGB8_ALPHA8:GL_RGBA8,map.width,map.height,0,GL_RGBA,GL_UNSIGNED_BYTE,map.rgba.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,map.minFilter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,map.magFilter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,map.wrapS);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,map.wrapT);glGenerateMipmap(GL_TEXTURE_2D);map.rgba.clear();
      }
    }
    glGenTextures(1,&part.texture);glBindTexture(GL_TEXTURE_2D,part.texture);
    glTexImage2D(GL_TEXTURE_2D,0,asset.pbr?GL_SRGB8_ALPHA8:GL_RGBA,part.width,part.height,0,GL_RGBA,GL_UNSIGNED_BYTE,part.rgba.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,part.baseMinFilter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,part.baseMagFilter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,part.baseWrapS);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,part.baseWrapT);
    glGenerateMipmap(GL_TEXTURE_2D);
    part.rgba.clear();
  }
  uint64_t lodBytes=0;for(const auto &part:asset.primitives)for(const auto &lod:part.lod)if(lod.indexBuffer)lodBytes+=uint64_t(lod.count)*((lod.vbo?lod.indexType:part.indexType)==GL_UNSIGNED_SHORT?2:4);
  if(lodBytes){std::printf("[model] lod path=%s index_bytes=%llu\n",asset.path.c_str(),(unsigned long long)lodBytes);std::fflush(stdout);}
  if(asset.skinEligible)for(auto &part:asset.primitives){
    glGenBuffers(1,&part.skinVbo);glBindBuffer(GL_ARRAY_BUFFER,part.skinVbo);glBufferData(GL_ARRAY_BUFFER,part.skinVertices.size()*sizeof(float),part.skinVertices.data(),GL_STATIC_DRAW);
    glGenBuffers(1,&part.skinIbo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,part.skinIndices.size()*sizeof(uint32_t),part.skinIndices.data(),GL_STATIC_DRAW);
  }
  asset.uploaded=true;
}
void deleteModel(ModelAsset &asset){
  if(asset.batchVbo)glDeleteBuffers(1,&asset.batchVbo);asset.batchVbo=0;asset.batchBytes=0;
  for(auto &part:asset.primitives){
    if(part.surfaceVbo)glDeleteBuffers(1,&part.surfaceVbo);part.surfaceVbo=0;
    for(auto &map:part.maps){if(map.texture)glDeleteTextures(1,&map.texture);map.texture=0;}
    if(part.skinVbo)glDeleteBuffers(1,&part.skinVbo);if(part.skinIbo)glDeleteBuffers(1,&part.skinIbo);part.skinVbo=part.skinIbo=0;
    if(part.depthClusters.indexBuffer)glDeleteBuffers(1,&part.depthClusters.indexBuffer);part.depthClusters={};
    for(auto &lod:part.lod){if(lod.indexBuffer)glDeleteBuffers(1,&lod.indexBuffer);if(lod.vbo)glDeleteBuffers(1,&lod.vbo);if(lod.surfaceVbo)glDeleteBuffers(1,&lod.surfaceVbo);pbrLodBudget.release(lod.budgetBytes);lod={};}
    if(part.vbo)glDeleteBuffers(1,&part.vbo);if(part.indexBuffer)glDeleteBuffers(1,&part.indexBuffer);if(part.texture)glDeleteTextures(1,&part.texture);part.vbo=part.indexBuffer=part.texture=0;part.gpuVertexCount=0;}
  asset.uploaded=false;
}

void drawModelPrimitive(const PrimitiveAsset &part,GLuint expandedVbo,int lodLevel){
  if(!expandedVbo)if(const auto *lod=modelLodLevel(part,lodLevel);lod&&lod->vbo){
    glBindBuffer(GL_ARRAY_BUFFER,lod->vbo);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    if(lod->indexBuffer){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,lod->indexBuffer);glDrawElements(GL_TRIANGLES,lod->count,lod->indexType,nullptr);}
    else glDrawArrays(GL_TRIANGLES,0,lod->count);return;
  }
  glBindBuffer(GL_ARRAY_BUFFER,expandedVbo?expandedVbo:part.vbo);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
  if(!expandedVbo&&part.indexBuffer){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,modelIndexBuffer(part,lodLevel));glDrawElements(GL_TRIANGLES,modelDrawVertexCount(part,lodLevel),part.indexType,nullptr);}
  else glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
}

void drawPosePrimitive(const PrimitiveAsset &source,const ModelPose &pose,size_t primitive){
 if(pose.geometry.batchVbo){
  glBindBuffer(GL_ARRAY_BUFFER,pose.geometry.batchVbo);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
  glDrawArrays(GL_TRIANGLES,pose.parts[primitive].streamFirst,source.vertexCount);return;
 }
 drawModelPrimitive(source,pose.geometry.primitives[primitive].vbo,0
 );
}

ModelPose::~ModelPose(){
 for(auto texture:skinTextures)if(texture)glDeleteTextures(1,&texture);
 deleteModel(geometry);
}
static void deformPose(const ModelAsset &asset,ModelPose &pose){
  if(pose.geometry.primitives.empty()){
    pose.geometry.primitives.resize(asset.primitives.size());
    for(size_t i=0;i<asset.primitives.size();i++){pose.geometry.primitives[i].vertices=asset.primitives[i].vertices;pose.geometry.primitives[i].vertexCount=asset.primitives[i].vertexCount;}
    pose.geometry.rigidBatchEligible=asset.rigidBatchEligible;pose.geometry.pbr=asset.pbr;
    pose.geometry.pickTriangles=asset.pickTriangles;pose.geometry.pickNodes=asset.pickNodes;
  }
#ifdef SF_TEST_HOOKS
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
      if(asset.pbr)deformSurface(source,target,matrix,source.skin>=0?&pose.palettes[source.skin]:nullptr,pose.nodes[source.node].weights);
      cache.world=matrix;cache.valid=true;
#ifdef SF_TEST_HOOKS
      if(animationProfileFrame)animationProfileFrame->vertices+=source.localVertices.size()/5;
#endif
    }
    pose.radius=std::max(pose.radius,cache.radius);
    for(int axis=0;axis<3;axis++){pose.geometry.low[axis]=std::min(pose.geometry.low[axis],cache.low[axis]);pose.geometry.high[axis]=std::max(pose.geometry.high[axis],cache.high[axis]);}
  }
#ifdef SF_TEST_HOOKS
  if(animationProfileFrame)animationProfileFrame->deformBoundsMs+=animationProfileMs(profileDeformStart);
  const auto profileBvhStart=AnimationProfileClock::now();
#endif
  if(changed)picking::refit(pose.geometry);
  pose.dirty=std::any_of(pose.parts.begin(),pose.parts.end(),[](const auto &part){return part.uploadDirty;});
#ifdef SF_TEST_HOOKS
  if(animationProfileFrame)animationProfileFrame->bvhMs+=animationProfileMs(profileBvhStart);
#endif
}
void evaluateModelPose(const ModelAsset &asset,const animation::Playback &state,ModelPose &pose){
#ifdef SF_TEST_HOOKS
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
#ifdef SF_TEST_HOOKS
  if(animationProfileFrame){animationProfileFrame->poseMs+=animationProfileMs(profilePoseStart);animationProfileFrame->evaluations++;}

#endif
  pose.gpuSkin=asset.skinEligible&&gpuSkinningEnabled;pose.pickingValid=false;
  if(pose.gpuSkin){skinPoseBounds(asset,pose);pose.dirty=true;return;}
  deformPose(asset,pose);
}

void uploadModelPose(ModelPose &pose){
  if(!pose.dirty)return;
  if(pose.gpuSkin){uploadSkinPose(pose);return;}
#ifdef SF_TEST_HOOKS
  const auto profileUploadStart=AnimationProfileClock::now();
  if(animationProfileFrame)animationProfileFrame->uploads++;
#endif
  pose.parts.resize(pose.geometry.primitives.size());
  if(pose.geometry.rigidBatchEligible&&!pose.geometry.batchVbo){
    for(const auto &part:pose.geometry.primitives)pose.geometry.batchBytes+=part.vertices.size()*sizeof(float);
    glGenBuffers(1,&pose.geometry.batchVbo);glBindBuffer(GL_ARRAY_BUFFER,pose.geometry.batchVbo);glBufferData(GL_ARRAY_BUFFER,pose.geometry.batchBytes,nullptr,GL_DYNAMIC_DRAW);
  }
  size_t byteOffset=0;
  for(size_t i=0;i<pose.geometry.primitives.size();i++){
    auto &part=pose.geometry.primitives[i];auto &cache=pose.parts[i];
    const auto offset=byteOffset;byteOffset+=part.vertices.size()*sizeof(float);cache.streamFirst=GLint(offset/(5*sizeof(float)));
    if(cache.valid&&!cache.uploadDirty&&(pose.geometry.batchVbo||part.vbo))continue;
#ifdef SF_TEST_HOOKS
    if(animationProfileFrame)animationProfileFrame->uploadBytes+=part.vertices.size()*sizeof(float);
#endif
    if(pose.geometry.batchVbo){
      glBindBuffer(GL_ARRAY_BUFFER,pose.geometry.batchVbo);glBufferSubData(GL_ARRAY_BUFFER,offset,part.vertices.size()*sizeof(float),part.vertices.data());
    }else{
      part.gpuVertexCount=part.vertexCount;
      if(!part.vbo){glGenBuffers(1,&part.vbo);glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_DYNAMIC_DRAW);}
      else{glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glBufferSubData(GL_ARRAY_BUFFER,0,part.vertices.size()*sizeof(float),part.vertices.data());}
    }
    if(pose.geometry.pbr)uploadSurface(part,GL_DYNAMIC_DRAW);
    cache.uploadDirty=false;
  }
  pose.dirty=false;
#ifdef SF_TEST_HOOKS
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
