#pragma once
#include "surface_frame.h"
#include "../third_party/mikktspace.h"

static bool readMaterialMap(const cgltf_texture_view &view,MaterialMap &map){
 if(!view.texture)return true;
 if(!view.texture->image)return false;
 if((view.has_transform&&view.transform.has_texcoord?view.transform.texcoord:view.texcoord)!=0)return false;
 PrimitiveAsset image;if(!decodeBaseColor(view.texture->image,image))return false;
 map.rgba=std::move(image.rgba);map.width=image.width;map.height=image.height;
 if(auto *sampler=view.texture->sampler){if(sampler->wrap_s)map.wrapS=sampler->wrap_s;if(sampler->wrap_t)map.wrapT=sampler->wrap_t;if(sampler->min_filter)map.minFilter=sampler->min_filter;if(sampler->mag_filter)map.magFilter=sampler->mag_filter;}
 if(view.has_transform){const auto &t=view.transform;float c=std::cos(t.rotation),s=std::sin(t.rotation);map.uv[0]=c*t.scale[0];map.uv[1]=s*t.scale[0];map.uv[3]=-s*t.scale[1];map.uv[4]=c*t.scale[1];map.uv[6]=t.offset[0];map.uv[7]=t.offset[1];}
 return true;
}
static bool readPbrMaterial(const cgltf_material *m,const cgltf_texture_view *base,PrimitiveAsset &p){
 if(base&&base->texture&&!base->texture->image)return false;
 if(base&&base->texture&&(base->has_transform&&base->transform.has_texcoord?base->transform.texcoord:base->texcoord)!=0)return false;
 if(!m)return true;
 p.materialUnlit=m->unlit;p.metallic=m->pbr_metallic_roughness.metallic_factor;p.roughness=m->pbr_metallic_roughness.roughness_factor;
 p.normalScale=m->normal_texture.scale;p.occlusionStrength=m->occlusion_texture.scale;
 for(int i=0;i<3;i++)p.emissive[i]=m->emissive_factor[i]*(m->has_emissive_strength?m->emissive_strength.emissive_strength:1.f);
 // Normal-map tangent frames must use its independently transformed UV0.
 if(m->normal_texture.texture&&m->normal_texture.has_transform){const auto &t=m->normal_texture.transform;if(t.scale[0]==0||t.scale[1]==0)return false;}
 return readMaterialMap(m->normal_texture,p.maps[0])&&readMaterialMap(m->pbr_metallic_roughness.metallic_roughness_texture,p.maps[1])&&readMaterialMap(m->occlusion_texture,p.maps[2])&&readMaterialMap(m->emissive_texture,p.maps[3]);
}
static animation::Matrix surfaceMatrix(const PrimitiveAsset &p,size_t vertex,const animation::Matrix &world,const std::vector<animation::Matrix> *palette){
 if(!palette)return world;
 animation::Matrix result{};
 for(const auto &set:p.influences)for(int lane=0;lane<4;lane++)for(int c=0;c<16;c++)result[c]+=set[vertex].weights[lane]*(*palette)[set[vertex].joints[lane]][c];
 return result;
}
static void deformSurface(const PrimitiveAsset &source,PrimitiveAsset &target,const animation::Matrix &world,const std::vector<animation::Matrix> *palette,const std::vector<float> &weights){
 using namespace surfaceFrame;
 target.surface=source.localSurface;target.surfaceMirrored=determinant(world)<0;
 for(size_t v=0;v<target.surface.size()/9;v++){
  float *out=target.surface.data()+v*9;V n{out[0],out[1],out[2]},t{out[3],out[4],out[5]};
  for(size_t w=0;w<weights.size();w++)for(int a=0;a<3;a++){if(w<source.morphNormals.size()&&!source.morphNormals[w].empty())n[a]+=weights[w]*source.morphNormals[w][v*3+a];if(w<source.morphTangents.size()&&!source.morphTangents[w].empty())t[a]+=weights[w]*source.morphTangents[w][v*3+a];}
  auto matrix=surfaceMatrix(source,v,world,palette);n=normal(matrix,n);t=tangent(n,direction(matrix,t));
  for(int a=0;a<3;a++){out[a]=n[a];out[3+a]=t[a];}out[6]*=determinant(matrix)<0?-1.f:1.f;
 }
 if(source.flatNormals)for(size_t v=0;v+2<target.vertices.size()/5;v+=3){
  V a{},b{};for(int c=0;c<3;c++){a[c]=target.vertices[(v+1)*5+c]-target.vertices[v*5+c];b[c]=target.vertices[(v+2)*5+c]-target.vertices[v*5+c];}auto n=unit(cross(a,b));if(target.surfaceMirrored)for(auto &c:n)c=-c;
  for(size_t k=0;k<3;k++){float *out=target.surface.data()+(v+k)*9;auto t=tangent(n,{out[3],out[4],out[5]});for(int c=0;c<3;c++){out[c]=n[c];out[c+3]=t[c];}}
 }
}
static bool generateTangents(PrimitiveAsset &p){
 SMikkTSpaceInterface api{};
 api.m_getNumFaces=[](const SMikkTSpaceContext *c){return int(static_cast<PrimitiveAsset*>(c->m_pUserData)->localSurface.size()/27);};
 api.m_getNumVerticesOfFace=[](const SMikkTSpaceContext*,int){return 3;};
 api.m_getPosition=[](const SMikkTSpaceContext *c,float *o,int f,int v){const auto &p=*static_cast<PrimitiveAsset*>(c->m_pUserData);std::copy_n(p.localVertices.data()+(f*3+v)*5,3,o);};
 api.m_getNormal=[](const SMikkTSpaceContext *c,float *o,int f,int v){const auto &p=*static_cast<PrimitiveAsset*>(c->m_pUserData);std::copy_n(p.localSurface.data()+(f*3+v)*9,3,o);};
 api.m_getTexCoord=[](const SMikkTSpaceContext *c,float *o,int f,int v){const auto &p=*static_cast<PrimitiveAsset*>(c->m_pUserData);const float *s=p.localSurface.data()+(f*3+v)*9+7;const float *m=p.maps[0].uv;o[0]=m[0]*s[0]+m[3]*s[1]+m[6];o[1]=m[1]*s[0]+m[4]*s[1]+m[7];};
 api.m_setTSpaceBasic=[](const SMikkTSpaceContext *c,const float *t,float sign,int f,int v){auto &p=*static_cast<PrimitiveAsset*>(c->m_pUserData);float *o=p.localSurface.data()+(f*3+v)*9+3;std::copy_n(t,3,o);o[3]=-sign; /* Convert MikkTSpace handedness to glTF. */};
 SMikkTSpaceContext context{&api,&p};return genTangSpaceDefault(&context)!=0;
}
static bool readPbrSurface(const cgltf_primitive &src,const cgltf_accessor *positions,const cgltf_accessor *uv,const cgltf_node &node,PrimitiveAsset &p,const animation::Matrix &world,const std::vector<std::vector<animation::Matrix>> &palettes){
 const cgltf_accessor *normals=nullptr,*tangents=nullptr;
 for(size_t i=0;i<src.attributes_count;i++){const auto &a=src.attributes[i];if(a.type==cgltf_attribute_type_normal)normals=a.data;if(a.type==cgltf_attribute_type_tangent)tangents=a.data;}
 if((normals&&(normals->type!=cgltf_type_vec3||normals->count!=positions->count))||(tangents&&(tangents->type!=cgltf_type_vec4||tangents->count!=positions->count)))return false;
 p.flatNormals=!normals;p.localSurface.resize(size_t(p.vertexCount)*9);
 p.morphNormals.resize(src.targets_count);p.morphTangents.resize(src.targets_count);
 for(size_t v=0;v<size_t(p.vertexCount);v++){
  size_t index=src.indices?cgltf_accessor_read_index(src.indices,v):v;float *s=p.localSurface.data()+v*9;
  s[2]=1;s[3]=1;s[6]=1;
  if(normals&&!cgltf_accessor_read_float(normals,index,s,3))return false;
  if(normals&&tangents&&!cgltf_accessor_read_float(tangents,index,s+3,4))return false;
  if(uv&&!cgltf_accessor_read_float(uv,index,s+7,2))return false;
  for(int c=0;c<9;c++)if(!std::isfinite(s[c]))return false;
  if(tangents&&normals&&s[6]!=1&&s[6]!=-1)return false;
  auto n=surfaceFrame::unit({s[0],s[1],s[2]});std::copy(n.begin(),n.end(),s);
  for(size_t w=0;w<src.targets_count;w++)for(size_t a=0;a<src.targets[w].attributes_count;a++){
   const auto &attr=src.targets[w].attributes[a];auto *dest=attr.type==cgltf_attribute_type_normal?&p.morphNormals[w]:attr.type==cgltf_attribute_type_tangent?&p.morphTangents[w]:nullptr;
   if(!dest)continue;const auto *acc=attr.data;if(acc->type!=cgltf_type_vec3||acc->count!=positions->count)return false;
   float d[3];if(!cgltf_accessor_read_float(acc,index,d,3))return false;for(float x:d)if(!std::isfinite(x))return false;dest->insert(dest->end(),d,d+3);
  }
 }
 for(const auto *targets:{&p.morphNormals,&p.morphTangents})for(const auto &d:*targets)if(!d.empty()&&d.size()!=size_t(p.vertexCount)*3)return false;
 if(!normals)for(size_t v=0;v+2<size_t(p.vertexCount);v+=3){surfaceFrame::V a{},b{};for(int c=0;c<3;c++){a[c]=p.localVertices[(v+1)*5+c]-p.localVertices[v*5+c];b[c]=p.localVertices[(v+2)*5+c]-p.localVertices[v*5+c];}auto n=surfaceFrame::unit(surfaceFrame::cross(a,b));for(int k=0;k<3;k++)std::copy(n.begin(),n.end(),p.localSurface.data()+(v+k)*9);}
 if((!normals||!tangents)&&!generateTangents(p))return false;
 if(normals&&tangents){
  const float *m=p.maps[0].uv;float det=m[0]*m[4]-m[1]*m[3];if(std::abs(det)<1e-12f)return false;
  for(size_t v=0;v<size_t(p.vertexCount);v++){float *s=p.localSurface.data()+v*9;surfaceFrame::V n{s[0],s[1],s[2]},t{s[3],s[4],s[5]};auto b=surfaceFrame::cross(n,t);for(int c=0;c<3;c++)t[c]=(t[c]*m[4]-b[c]*s[6]*m[1])/det;t=surfaceFrame::tangent(n,t);std::copy(t.begin(),t.end(),s+3);s[6]*=det<0?-1.f:1.f;}
 }
 std::vector<float> weights(src.targets_count);const auto *defaults=node.weights_count?node.weights:node.mesh->weights;if(defaults)std::copy_n(defaults,weights.size(),weights.begin());
 deformSurface(p,p,world,p.skin>=0?&palettes[p.skin]:nullptr,weights);
 return true;
}
static void uploadSurface(PrimitiveAsset &p,GLenum usage){
 if(p.surface.empty())return;bool fresh=!p.surfaceVbo;if(fresh)glGenBuffers(1,&p.surfaceVbo);glBindBuffer(GL_ARRAY_BUFFER,p.surfaceVbo);
 if(fresh)glBufferData(GL_ARRAY_BUFFER,p.surface.size()*sizeof(float),p.surface.data(),usage);else glBufferSubData(GL_ARRAY_BUFFER,0,p.surface.size()*sizeof(float),p.surface.data());
}
