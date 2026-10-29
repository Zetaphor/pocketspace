#pragma once
#include "model_shaders.h"
#include <cmath>
#include <map>
#include <string>
// A separate program keeps legacy shaders, storage and their fast paths intact.
class PbrProgram {
 GLuint program=0;
 bool depthSupport=false;
 mutable std::map<std::string,GLint> locations;
 GLint location(const char *name)const{
  const auto found=locations.find(name);if(found!=locations.end())return found->second;
  const GLint value=glGetUniformLocation(program,name);locations.emplace(name,value);return value;
 }
 void scalar(const char *name,float v)const{glUniform1f(location(name),v);}
 void vector(const char *name,const float *v)const{glUniform3fv(location(name),1,v);}
 void integer(const char *name,int v)const{glUniform1i(location(name),v);}
public:
 ~PbrProgram(){if(program)glDeleteProgram(program);}
 bool init(bool fastDepth=false){
  if(program)return true;
  std::string vertex=modelVertexShader;
  vertex.insert(vertex.find("layout(location=6)"),R"(
layout(location=8) in vec3 surfaceNormal;
layout(location=9) in vec4 surfaceTangent;
layout(location=10) in vec2 surfaceUV;
out highp vec3 litNormal,litTangent,litView;
out highp vec2 mapUV;
out highp float tangentSign;
vec3 rotateSurface(vec3 v){
 float cr=cos(roll),sr=sin(roll),cp=cos(pitch),sp=sin(pitch),cy=cos(yaw),sy=sin(yaw);
 vec3 a=vec3(cr*v.x-sr*v.y,sr*v.x+cr*v.y,v.z);
 vec3 b=vec3(a.x,cp*a.y-sp*a.z,sp*a.y+cp*a.z);
 return vec3(cy*b.x+sy*b.z,b.y,-sy*b.x+cy*b.z);
}
)");
  vertex.insert(vertex.find("  vec3 ray=world-eye;"),R"(
  litNormal=rotateSurface(surfaceNormal);litTangent=rotateSurface(surfaceTangent.xyz);tangentSign=surfaceTangent.w;mapUV=surfaceUV;
  vec3 v=eye-world;litView=vec3(dot(v,right),dot(v,up),-dot(v,forward));
)");
  const char *fragment=R"(#version 300 es
precision highp float;
in vec2 t;in highp vec2 mapUV;in highp vec3 litNormal,litTangent,litView;in highp float tangentSign;
uniform sampler2D material,normalMap,metalRoughMap,occlusionMap,emissiveMap;
uniform highp samplerCube environmentMap;
uniform highp sampler2D environmentBrdf;
uniform bool environmentEnabled;
uniform float environmentIntensity,environmentMaxLod;
uniform vec2 environmentRotationCS;
uniform vec3 environmentDiffuse[9];
uniform bool hasNormal,hasMetalRough,hasOcclusion,hasEmissive,materialUnlit,doubleSided;
uniform mat3 normalUV,metalRoughUV,occlusionUV,emissiveUV;
uniform vec4 factor;uniform int alphaMode;uniform float alphaCutoff;
uniform float metallic,roughness,normalScale,occlusionStrength,intensity,ambient,exposure;
uniform vec3 emissive,lightDirection,lightColor;
out vec4 color;
const float PI=3.141592653589793;
vec3 safeUnit(vec3 v,vec3 fallback){float d=dot(v,v);return d>1e-16?v*inversesqrt(d):fallback;}
vec3 encodeSRGB(vec3 c){return mix(12.92*c,1.055*pow(max(c,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),c));}
vec3 environmentDirection(vec3 d){float c=environmentRotationCS.x,s=environmentRotationCS.y;return vec3(c*d.x-s*d.z,d.y,s*d.x+c*d.z);}
vec3 diffuseEnvironment(vec3 n){
 return max(vec3(0),environmentDiffuse[0]*.2820947918+environmentDiffuse[1]*(.4886025119*n.y)+environmentDiffuse[2]*(.4886025119*n.z)+environmentDiffuse[3]*(.4886025119*n.x)+environmentDiffuse[4]*(1.0925484306*n.x*n.y)+environmentDiffuse[5]*(1.0925484306*n.y*n.z)+environmentDiffuse[6]*(.3153915653*(3.*n.z*n.z-1.))+environmentDiffuse[7]*(1.0925484306*n.x*n.z)+environmentDiffuse[8]*(.5462742153*(n.x*n.x-n.y*n.y)));
}
void main(){
 vec4 base=texture(material,t)*factor;
 if(alphaMode==1&&base.a<alphaCutoff)discard;
 float alpha=alphaMode==0?1.:base.a;
 vec3 result=base.rgb;
 if(!materialUnlit){
  vec3 n=safeUnit(litNormal,vec3(0,0,1));
  vec3 tangent=safeUnit(litTangent-n*dot(n,litTangent),vec3(1,0,0));
  vec3 bitangent=cross(n,tangent)*(tangentSign<0.?-1.:1.);
  if(hasNormal){vec3 mapped=texture(normalMap,(normalUV*vec3(mapUV,1)).xy).xyz*2.-1.;mapped.xy*=normalScale;n=safeUnit(mat3(tangent,bitangent,n)*mapped,n);}
  // glFrontFace accounts for mirrored node transforms.
  vec3 v=safeUnit(litView,vec3(0,0,1));if(doubleSided&&!gl_FrontFacing)n=-n;
  vec3 l=safeUnit(lightDirection,vec3(0,1,0)),h=safeUnit(l+v,n);
  vec2 mr=hasMetalRough?texture(metalRoughMap,(metalRoughUV*vec3(mapUV,1)).xy).bg:vec2(1);
  float metal=clamp(metallic*mr.x,0.,1.),r=clamp(roughness*mr.y,.045,1.);
  float nl=max(dot(n,l),0.),nv=max(dot(n,v),0.),nh=max(dot(n,h),0.),vh=max(dot(v,h),0.);
  float a=r*r,a2=a*a,d=nh*nh*(a2-1.)+1.;float distribution=a2/(PI*d*d);
  float visibility=.5/max(nl*sqrt(nv*nv*(1.-a2)+a2)+nv*sqrt(nl*nl*(1.-a2)+a2),1e-6);
  vec3 f0=mix(vec3(.04),base.rgb,metal),f=f0+(1.-f0)*pow(1.-vh,5.);
  vec3 diffuse=(1.-f)*(1.-metal)*base.rgb/PI;
  float ao=hasOcclusion?mix(1.,texture(occlusionMap,(occlusionUV*vec3(mapUV,1)).xy).r,occlusionStrength):1.;
  vec3 emission=emissive*(hasEmissive?texture(emissiveMap,(emissiveUV*vec3(mapUV,1)).xy).rgb:vec3(1));
  result=(diffuse+distribution*visibility*f)*lightColor*intensity*nl+ambient*base.rgb*(1.-metal)*ao+emission;
  if(environmentEnabled){
   vec3 en=environmentDirection(n),reflection=environmentDirection(reflect(-v,n));
   vec3 filtered=textureLod(environmentMap,reflection,r*environmentMaxLod).rgb;
   vec2 brdf=texture(environmentBrdf,vec2(nv,r)).rg;
   vec3 fresnel=f0+(max(vec3(1.-r),f0)-f0)*pow(1.-nv,5.);
   vec3 indirectDiffuse=(1.-fresnel)*(1.-metal)*base.rgb*diffuseEnvironment(en);
   vec3 indirectSpecular=filtered*(f0*brdf.x+brdf.y);
   result+=(indirectDiffuse+indirectSpecular)*environmentIntensity*ao;
  }
  result=vec3(1)-exp(-max(result,vec3(0))*exposure);
 }
 color=vec4(clamp(encodeSRGB(result),0.,1.),alpha);if(alphaMode==1)color.rgb*=alpha;
})";
  auto compileShader=[](GLenum kind,const char *text){GLuint shader=glCreateShader(kind);glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);std::fprintf(stderr,"[pbr] %s\n",log);glDeleteShader(shader);return GLuint(0);}return shader;};
  std::string fragmentSource=fragment;
  if(fastDepth){
    fragmentSource.insert(fragmentSource.find("void main(){"),"uniform bool depthOnly;\n");
    fragmentSource.insert(fragmentSource.find(" vec4 base=")," if(depthOnly&&alphaMode==0){color=vec4(0);return;}\n");
    fragmentSource.insert(fragmentSource.find(" float alpha=")," if(depthOnly){color=vec4(0);return;}\n");
  }
  GLuint v=compileShader(GL_VERTEX_SHADER,vertex.c_str()),f=compileShader(GL_FRAGMENT_SHADER,fragmentSource.c_str());
  if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
  program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
  if(!ok){glDeleteProgram(program);program=0;return false;}
  depthSupport=fastDepth;
  if(depthSupport){
    GLint previous=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);glUseProgram(program);
    integer("material",0);integer("normalMap",2);integer("metalRoughMap",3);integer("occlusionMap",4);integer("emissiveMap",5);integer("environmentMap",6);integer("environmentBrdf",7);
    glUseProgram(GLuint(previous));
  }
  return true;
 }
 void view(const vr::HmdMatrix34_t &anchor,const float *c,const float *e,const float *r,const float *u,const float *f,float width)const{
  glUseProgram(program);vector("center",c);vector("eye",e);vector("panelRight",r);vector("panelUp",u);vector("panelForward",f);scalar("panelHalfWidth",width);
  float axes[3][3];for(int column=0;column<3;column++)for(int row=0;row<3;row++)axes[column][row]=anchor.m[row][column]*(column==2?-1.f:1.f);
  vector("right",axes[0]);vector("up",axes[1]);vector("forward",axes[2]);integer("skinEnabled",0);integer("material",0);
 }
 void object(float pitch,float yaw,float roll,float scale,float x,float y,float z)const{
  scalar("pitch",pitch);scalar("yaw",yaw);scalar("roll",roll);scalar("modelScale",scale);float offset[3]={x,y,z};vector("offset",offset);
 }
 void material(const ModelAsset &asset,const PrimitiveAsset &p,const PrimitiveAsset &geometry,int lodLevel=0,bool depthOnly=false)const{
  glUniform4fv(location("factor"),1,p.factor);integer("alphaMode",p.alphaMode);scalar("alphaCutoff",p.alphaCutoff);
  if(depthSupport){
    integer("depthOnly",depthOnly);
    if(depthOnly){
      glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.texture);
      glDisableVertexAttribArray(8);glDisableVertexAttribArray(9);glDisableVertexAttribArray(10);
      return;
    }
  }
  scalar("metallic",p.metallic);scalar("roughness",p.roughness);scalar("normalScale",p.normalScale);scalar("occlusionStrength",p.occlusionStrength);vector("emissive",p.emissive);
  integer("materialUnlit",p.materialUnlit);integer("doubleSided",p.doubleSided);
  const auto &l=asset.lighting;vector("lightDirection",l.direction);vector("lightColor",l.color);scalar("intensity",l.intensity);scalar("ambient",l.ambient);scalar("exposure",l.exposure);
  const char *samplers[]={"normalMap","metalRoughMap","occlusionMap","emissiveMap"},*present[]={"hasNormal","hasMetalRough","hasOcclusion","hasEmissive"},*uv[]={"normalUV","metalRoughUV","occlusionUV","emissiveUV"};
  for(int i=0;i<4;i++){integer(samplers[i],i+2);integer(present[i],p.maps[i].texture!=0);glUniformMatrix3fv(location(uv[i]),1,GL_FALSE,p.maps[i].uv);glActiveTexture(GL_TEXTURE0+i+2);glBindTexture(GL_TEXTURE_2D,p.maps[i].texture);}
  integer("environmentMap",6);integer("environmentBrdf",7);
  const auto &env=asset.environment;
  const bool enabled=!p.materialUnlit&&env&&l.environmentIntensity>0&&env->upload();integer("environmentEnabled",enabled);
  if(enabled){
   scalar("environmentIntensity",l.environmentIntensity);glUniform2f(location("environmentRotationCS"),std::cos(l.environmentRotation),std::sin(l.environmentRotation));scalar("environmentMaxLod",float(env->levels-1));glUniform3fv(location("environmentDiffuse"),9,env->diffuse.data());
   glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_CUBE_MAP,env->cube);glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,env->lut);
  }
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.texture);
  const auto *lod=modelLodLevel(geometry,lodLevel);
  glBindBuffer(GL_ARRAY_BUFFER,lod&&lod->surfaceVbo?lod->surfaceVbo:geometry.surfaceVbo);
  glEnableVertexAttribArray(8);glEnableVertexAttribArray(9);glEnableVertexAttribArray(10);
  glVertexAttribPointer(8,3,GL_FLOAT,GL_FALSE,9*sizeof(float),nullptr);glVertexAttribPointer(9,4,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)(3*sizeof(float)));glVertexAttribPointer(10,2,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)(7*sizeof(float)));
 }
 void release()const{glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_CUBE_MAP,0);glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,0);for(int i=2;i<6;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,0);}glActiveTexture(GL_TEXTURE0);glDisableVertexAttribArray(8);glDisableVertexAttribArray(9);glDisableVertexAttribArray(10);}
};
