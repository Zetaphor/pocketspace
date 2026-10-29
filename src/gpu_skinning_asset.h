#pragma once
// Experiment implementation included only by model_asset.cpp.
static void prepareSkinAsset(ModelAsset &asset){
 asset.skinEligible=asset.hasSkin&&!asset.hasMorph&&!asset.primitives.empty();
 for(const auto &p:asset.primitives)if(p.skin<0||p.influences.size()!=1||asset.skins[p.skin].joints.size()>1024)asset.skinEligible=false;
 for(const auto &skin:asset.skins)if(skin.joints.size()>1024)asset.skinEligible=false;
 if(!asset.skinEligible)return;
 for(auto &part:asset.primitives){
  part.skinBounds.resize(asset.skins[part.skin].joints.size());
  using Key=std::array<uint32_t,13>;struct Hash{size_t operator()(const Key &k)const{size_t h=1469598103934665603ull;for(auto x:k)h=(h^x)*1099511628211ull;return h;}};
  std::unordered_map<Key,uint32_t,Hash> seen;
  for(size_t v=0;v<part.localVertices.size()/5;v++){
   const auto &influence=part.influences[0][v];const float *p=part.localVertices.data()+v*5;
   std::array<float,13> vertex;std::copy_n(p,5,vertex.begin());
   for(int lane=0;lane<4;lane++){
    vertex[5+lane]=float(influence.joints[lane]);vertex[9+lane]=influence.weights[lane];
    if(influence.weights[lane]<=0)continue;
    auto &b=part.skinBounds[influence.joints[lane]];
    if(!b.used){std::copy_n(p,3,b.low.begin());b.high=b.low;b.used=true;}
    else for(int axis=0;axis<3;axis++){b.low[axis]=std::min(b.low[axis],p[axis]);b.high[axis]=std::max(b.high[axis],p[axis]);}
   }
   Key key;for(size_t k=0;k<13;k++)key[k]=std::bit_cast<uint32_t>(vertex[k]);
   auto [it,added]=seen.emplace(key,uint32_t(part.skinVertices.size()/13));
   if(added)part.skinVertices.insert(part.skinVertices.end(),vertex.begin(),vertex.end());part.skinIndices.push_back(it->second);
  }
 }
}
static void skinPoseBounds(const ModelAsset &asset,ModelPose &pose){
#ifdef SF_ANIMATION_PROFILE
 const auto start=AnimationProfileClock::now();
#endif
 for(int a=0;a<3;a++){pose.geometry.low[a]=std::numeric_limits<float>::max();pose.geometry.high[a]=-std::numeric_limits<float>::max();}
 for(const auto &part:asset.primitives)for(size_t joint=0;joint<part.skinBounds.size();joint++){
  const auto &b=part.skinBounds[joint];if(!b.used)continue;
  for(int corner=0;corner<8;corner++){
   float point[3];for(int a=0;a<3;a++)point[a]=(corner&(1<<a))?b.high[a]:b.low[a];
   const auto world=animation::transformPoint(pose.palettes[part.skin][joint],point);
   for(int a=0;a<3;a++){const float p=(world[a]-asset.center[a])*asset.normalization;pose.geometry.low[a]=std::min(pose.geometry.low[a],p);pose.geometry.high[a]=std::max(pose.geometry.high[a],p);}
  }
 }
 pose.radius=0;
 for(int a=0;a<3;a++){
  // Weight normalization and CPU/GPU arithmetic introduce small rounding errors.
  const float pad=1e-5f*(1+std::max(std::abs(pose.geometry.low[a]),std::abs(pose.geometry.high[a])));
  pose.geometry.low[a]-=pad;pose.geometry.high[a]+=pad;
  const float p=std::max(std::abs(pose.geometry.low[a]),std::abs(pose.geometry.high[a]));pose.radius+=p*p;
 }
 pose.radius=std::sqrt(pose.radius);
#ifdef SF_ANIMATION_PROFILE
 if(animationProfileFrame)animationProfileFrame->deformBoundsMs+=animationProfileMs(start);
#endif
}
static void uploadSkinPose(ModelPose &pose){
#ifdef SF_ANIMATION_PROFILE
 const auto start=AnimationProfileClock::now();if(animationProfileFrame)animationProfileFrame->uploads++;
#endif
 pose.skinTextures.resize(pose.palettes.size());glActiveTexture(GL_TEXTURE1);
 for(size_t s=0;s<pose.palettes.size();s++){
  const auto &palette=pose.palettes[s];auto &texture=pose.skinTextures[s];
  if(!texture){glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,4,GLsizei(palette.size()),0,GL_RGBA,GL_FLOAT,palette.data());}
  else{glBindTexture(GL_TEXTURE_2D,texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,GLsizei(palette.size()),GL_RGBA,GL_FLOAT,palette.data());}
#ifdef SF_ANIMATION_PROFILE
  if(animationProfileFrame)animationProfileFrame->uploadBytes+=palette.size()*sizeof(animation::Matrix);
#endif
 }
 glActiveTexture(GL_TEXTURE0);pose.dirty=false;
#ifdef SF_ANIMATION_PROFILE
 if(animationProfileFrame)animationProfileFrame->uploadMs+=animationProfileMs(start);
#endif
}
bool bindSkinPrimitive(const ModelAsset &asset,const PrimitiveAsset &part,const ModelPose &pose,GLuint program){
 if(!pose.gpuSkin)return false;
 glUniform1i(glGetUniformLocation(program,"skinEnabled"),1);glUniform1i(glGetUniformLocation(program,"skinPalette"),1);
 glUniform4f(glGetUniformLocation(program,"skinNormalize"),asset.center[0],asset.center[1],asset.center[2],asset.normalization);
 glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,pose.skinTextures[part.skin]);glActiveTexture(GL_TEXTURE0);
 glBindBuffer(GL_ARRAY_BUFFER,part.skinVbo);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,13*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,13*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
 for(int a=6;a<=7;a++){glEnableVertexAttribArray(a);glVertexAttribDivisor(a,0);glVertexAttribPointer(a,4,GL_FLOAT,GL_FALSE,13*sizeof(float),reinterpret_cast<void*>((a==6?5:9)*sizeof(float)));}
 return true;
}
void unbindSkinPrimitive(GLuint program){glDisableVertexAttribArray(6);glDisableVertexAttribArray(7);glUniform1i(glGetUniformLocation(program,"skinEnabled"),0);}
