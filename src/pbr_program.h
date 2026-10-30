#pragma once
#include "model_shaders.h"
#include "spot_shadow_settings.h"
#include <cmath>
#include <map>
#include <string>
#ifdef SF_LIGHT_WORK_EXPERIMENT
#include "../tests/light-work-reference.h"
#endif
#ifdef SF_DIRECTIONAL_LIGHT_VALIDATION
#include "../tests/directional-light-reference.h"
#endif
// A separate program keeps legacy shaders, storage and their fast paths intact.
class PbrProgram {
 GLuint program=0;
 bool depthSupport=false;
 mutable bool sheenTexturesBound=false,shadowBound=false;
#ifdef SF_DIRECTIONAL_LIGHT_VALIDATION
 bool originalLightLoop=false;
#endif
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
 bool minimalDepth=false;
#endif
 mutable int pointCount=-1;
 mutable std::array<float,104> pointPayload{};
 mutable std::map<std::string,GLint> locations;
 GLint location(const char *name)const{
  const auto found=locations.find(name);if(found!=locations.end())return found->second;
  const GLint value=glGetUniformLocation(program,name);locations.emplace(name,value);return value;
 }
 void scalar(const char *name,float v)const{glUniform1f(location(name),v);}
 void vector(const char *name,const float *v)const{glUniform3fv(location(name),1,v);}
 void integer(const char *name,int v)const{glUniform1i(location(name),v);}
public:
#ifdef SF_DIRECTIONAL_LIGHT_VALIDATION
 bool initLightReference(bool depth=false){originalLightLoop=true;return init(depth);}
#endif
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
 bool initMinimalDepth(){minimalDepth=true;return init(true);}
#endif
 bool initVariant(int key){return init(false
#ifdef SF_LIGHT_WORK_EXPERIMENT
 ,false
#endif
#ifdef SF_PBR_TRANSFORM_EXPERIMENT
 ,false
#endif
#ifdef SF_PBR_LIGHT_INVARIANT_EXPERIMENT
 ,false
#endif
 ,key);}
 GLuint id()const{return program;}
#ifdef SF_PBR_VARIANT_EXPERIMENT
 GLuint experimentId()const{return program;}
#endif
 ~PbrProgram(){if(program)glDeleteProgram(program);}
 bool init(bool fastDepth=false
#ifdef SF_LIGHT_WORK_EXPERIMENT
 ,bool optimizeLightWork=false
#endif
#ifdef SF_PBR_TRANSFORM_EXPERIMENT
 ,bool prepareRotation=false
#endif
#ifdef SF_PBR_LIGHT_INVARIANT_EXPERIMENT
 ,bool reuseLightInvariants=false
#endif
 ,int variantKey=-1
 ){
  if(program)return true;
  std::string vertex=modelVertexShader;
  vertex.insert(vertex.find("layout(location=6)"),R"(
layout(location=8) in vec3 surfaceNormal;
layout(location=9) in vec4 surfaceTangent;
layout(location=10) in vec2 surfaceUV;
out highp vec3 litNormal,litTangent,litView,litPosition;
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
  litPosition=offset*vec3(1,1,-1)+modelScale*rotated;
  litNormal=rotateSurface(surfaceNormal);litTangent=rotateSurface(surfaceTangent.xyz);tangentSign=surfaceTangent.w;mapUV=surfaceUV;
  vec3 v=eye-world;litView=vec3(dot(v,right),dot(v,up),-dot(v,forward));
)");
#ifdef SF_PBR_TRANSFORM_EXPERIMENT
  if(prepareRotation){
   const std::string signature="vec3 rotateSurface(vec3 v){";
   vertex.replace(vertex.find(signature),signature.size(),"vec3 rotateSurface(vec3 v,float cr,float sr,float cp,float sp,float cy,float sy){");
   const std::string trig="float cr=cos(roll),sr=sin(roll),cp=cos(pitch),sp=sin(pitch),cy=cos(yaw),sy=sin(yaw);";
   vertex.erase(vertex.find(trig),trig.size()); // Keep the identical computation in main.
   for(const char *input:{"surfaceNormal","surfaceTangent.xyz"}){
    const std::string from=std::string("rotateSurface(")+input+")";
    vertex.replace(vertex.find(from),from.size(),std::string("rotateSurface(")+input+",cr,sr,cp,sp,cy,sy)");
   }
  }
#endif
  const char *fragment=R"(#version 300 es
precision highp float;
in vec2 t;in highp vec2 mapUV;in highp vec3 litNormal,litTangent,litView,litPosition;in highp float tangentSign;
uniform sampler2D material,normalMap,metalRoughMap,occlusionMap,emissiveMap;
uniform sampler2D sheenColorMap,sheenRoughnessMap,sheenAlbedo;
uniform bool sheenEnabled,hasSheenColor,hasSheenRoughness;
uniform mat3 sheenColorUV,sheenRoughnessUV;
uniform vec3 sheenColor;uniform float sheenRoughness;
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
uniform int pointLightCount;
uniform highp sampler2DShadow spotShadowMap;
uniform mat4 spotShadowMatrix;uniform vec3 spotShadowOrigin;
uniform int shadowLightIndex;uniform float shadowBias,shadowNormalBias,shadowTexel;
uniform vec4 pointPositionRange[8],pointColorIntensity[8],spotDirectionOuter[8];
uniform float spotInner[8];
out vec4 color;
const float PI=3.141592653589793;
vec3 safeUnit(vec3 v,vec3 fallback){float d=dot(v,v);return d>1e-16?v*inversesqrt(d):fallback;}
vec3 encodeSRGB(vec3 c){return mix(12.92*c,1.055*pow(max(c,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),c));}
vec3 environmentDirection(vec3 d){float c=environmentRotationCS.x,s=environmentRotationCS.y;return vec3(c*d.x-s*d.z,d.y,s*d.x+c*d.z);}
vec3 diffuseEnvironment(vec3 n){
 return max(vec3(0),environmentDiffuse[0]*.2820947918+environmentDiffuse[1]*(.4886025119*n.y)+environmentDiffuse[2]*(.4886025119*n.z)+environmentDiffuse[3]*(.4886025119*n.x)+environmentDiffuse[4]*(1.0925484306*n.x*n.y)+environmentDiffuse[5]*(1.0925484306*n.y*n.z)+environmentDiffuse[6]*(.3153915653*(3.*n.z*n.z-1.))+environmentDiffuse[7]*(1.0925484306*n.x*n.z)+environmentDiffuse[8]*(.5462742153*(n.x*n.x-n.y*n.y)));
}
float sheenLambda(float cosine,float a){
 cosine=clamp(cosine,0.,1.);
 float q=(1.-a)*(1.-a);
 float A=mix(21.5473,25.3245,q),B=mix(3.82987,3.32435,q),C=mix(.19823,.16801,q);
 float D=mix(-1.97760,-1.27393,q),E=mix(-4.32054,-4.85967,q);
 float x=cosine<.5?cosine:1.-cosine;
 float fitted=A/(1.+B*pow(x,C))+D*x+E;
 float middle=A/(1.+B*pow(.5,C))+.5*D+E;
 return exp(cosine<.5?fitted:2.*middle-fitted);
}
float sheenBrdf(float nv,float nl,float nh,float r){
 if(nv<=0.||nl<=0.)return 0.;
 float a=r*r,inv=1./a;
 float distribution=(2.+inv)*pow(max(0.,1.-nh*nh),inv*.5)/(2.*PI);
 return distribution*min(1.,1./((1.+sheenLambda(nv,a)+sheenLambda(nl,a))*4.*nv*nl));
}
vec3 layerSheen(vec3 base,vec3 tint,float r,float nv,float nl,float nh,float ev,float normalization){
 if(!sheenEnabled)return base;
 float el=texture(sheenAlbedo,vec2(nl,r)).r;
 float strength=max(tint.r,max(tint.g,tint.b));
 return base*max(0.,1.-strength*max(ev,el))+tint*sheenBrdf(nv,nl,nh,r)*normalization;
}
// Compute before alpha discard and per-light branches: derivatives are undefined
// in divergent control flow. Use geometry rather than normal-map perturbations.
vec2 shadowReceiverSlope(){
 vec4 clip=spotShadowMatrix*vec4(litPosition+spotShadowOrigin,1);
 vec3 q=clip.xyz/(abs(clip.w)>1e-8?clip.w:1e-8)*.5+.5;
 vec3 dx=dFdx(q),dy=dFdy(q);
 float determinant=dx.x*dy.y-dx.y*dy.x;
 if(abs(determinant)<1e-12)return vec2(0);
 return vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant;
}
float spotlightVisibility(vec3 n,vec2 slope){
 vec4 clip=spotShadowMatrix*vec4(litPosition+spotShadowOrigin+n*shadowNormalBias,1);
 if(clip.w<=0.)return 1.;
 vec3 q=clip.xyz/clip.w*.5+.5;
 if(any(lessThan(q,vec3(0)))||any(greaterThan(q,vec3(1))))return 1.;
 // Four bilinear comparison taps reach up to 1.5 texels from the receiver.
 // Cover that plane-depth variation so coarse maps do not shadow the plane itself.
 float receiverBias=shadowBias+1.5*shadowTexel*(abs(slope.x)+abs(slope.y));
 float visibility=0.;
 for(int y=0;y<2;y++)for(int x=0;x<2;x++)visibility+=texture(spotShadowMap,vec3(q.xy+(vec2(x,y)-.5)*shadowTexel,q.z-receiverBias));
 return visibility*.25;
}
void main(){
 vec2 shadowSlope=shadowLightIndex>=0?shadowReceiverSlope():vec2(0);
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
  vec3 sheenTint=sheenEnabled?sheenColor*(hasSheenColor?texture(sheenColorMap,(sheenColorUV*vec3(mapUV,1)).xy).rgb:vec3(1)):vec3(0);
  float sr=clamp(sheenRoughness*(hasSheenRoughness?texture(sheenRoughnessMap,(sheenRoughnessUV*vec3(mapUV,1)).xy).a:1.),.07,1.);
  vec2 sheenEnergy=sheenEnabled?texture(sheenAlbedo,vec2(nv,sr)).rg:vec2(0,1);
  float ev=sheenEnergy.r,normalization=sheenEnergy.g;
  float viewScale=1.-max(sheenTint.r,max(sheenTint.g,sheenTint.b))*ev;
  float ao=hasOcclusion?mix(1.,texture(occlusionMap,(occlusionUV*vec3(mapUV,1)).xy).r,occlusionStrength):1.;
  vec3 emission=emissive*(hasEmissive?texture(emissiveMap,(emissiveUV*vec3(mapUV,1)).xy).rgb:vec3(1));
  result=layerSheen(diffuse+distribution*visibility*f,sheenTint,sr,nv,nl,nh,ev,normalization)*lightColor*intensity*nl+ambient*base.rgb*(1.-metal)*ao*viewScale+emission;
  for(int i=0;i<8;i++){
   if(i>=pointLightCount)break;
   vec3 delta=pointPositionRange[i].xyz-litPosition;
   float distance2=dot(delta,delta),range=pointPositionRange[i].w;
   bool directional=range<0.;
   if(!directional&&distance2>=range*range)continue;
   vec3 pl=directional?-spotDirectionOuter[i].xyz:safeUnit(delta,n),ph=safeUnit(pl+v,n);
   float cone=1.;
   if(!directional&&spotDirectionOuter[i].w>=-1.){
    float cosine=dot(-pl,spotDirectionOuter[i].xyz),outer=spotDirectionOuter[i].w,inner=spotInner[i];
    if(cosine<outer)continue;
    cone=inner>outer?clamp((cosine-outer)/(inner-outer),0.,1.):step(outer,cosine);
    cone*=cone;
   }
   float pnl=max(dot(n,pl),0.),pnh=max(dot(n,ph),0.),pvh=max(dot(v,ph),0.);
   float pd=pnh*pnh*(a2-1.)+1.,distributionP=a2/(PI*pd*pd);
   float visibilityP=.5/max(pnl*sqrt(nv*nv*(1.-a2)+a2)+nv*sqrt(pnl*pnl*(1.-a2)+a2),1e-6);
   vec3 pf=f0+(1.-f0)*pow(1.-pvh,5.);
   float attenuation=1.;
   if(!directional){
    float window=max(1.-pow(distance2/(range*range),2.),0.);
    attenuation=cone*window*window/max(distance2,.0001);
   }
   result+=layerSheen((1.-pf)*(1.-metal)*base.rgb/PI+distributionP*visibilityP*pf,sheenTint,sr,nv,pnl,pnh,ev,normalization)*pointColorIntensity[i].rgb*pointColorIntensity[i].w*pnl*attenuation*(i==shadowLightIndex?spotlightVisibility(n,shadowSlope):1.);
  }
  if(environmentEnabled){
   vec3 en=environmentDirection(n),reflection=environmentDirection(reflect(-v,n));
   vec3 filtered=textureLod(environmentMap,reflection,r*environmentMaxLod).rgb;
   vec2 brdf=texture(environmentBrdf,vec2(nv,r)).rg;
   vec3 fresnel=f0+(max(vec3(1.-r),f0)-f0)*pow(1.-nv,5.);
   vec3 indirectDiffuse=(1.-fresnel)*(1.-metal)*base.rgb*diffuseEnvironment(en);
   vec3 indirectSpecular=filtered*(f0*brdf.x+brdf.y);
   // Indirect sheen uses low-frequency SH irradiance, not GGX-filtered specular.
   vec3 indirectSheen=sheenTint*diffuseEnvironment(en)*ev;
   result+=((indirectDiffuse+indirectSpecular)*viewScale+indirectSheen)*environmentIntensity*ao;
  }
  result=vec3(1)-exp(-max(result,vec3(0))*exposure);
 }
 color=vec4(clamp(encodeSRGB(result),0.,1.),alpha);if(alphaMode==1)color.rgb*=alpha;
})";
  auto compileShader=[](GLenum kind,const char *text){GLuint shader=glCreateShader(kind);glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);std::fprintf(stderr,"[pbr] %s\n",log);glDeleteShader(shader);return GLuint(0);}return shader;};
  std::string fragmentSource=fragment;
#ifdef SF_DIRECTIONAL_LIGHT_VALIDATION
  if(originalLightLoop){const auto first=fragmentSource.find("  for(int i=0;i<8;i++){");const auto last=fragmentSource.find("  if(environmentEnabled){",first);fragmentSource.replace(first,last-first,directionalLightReferenceLoop);}
#endif
  if(variantKey>=0){
   const std::string flags="uniform bool hasNormal,hasMetalRough,hasOcclusion,hasEmissive,materialUnlit,doubleSided;";
   const char *names[]={"hasNormal","hasMetalRough","hasOcclusion","hasEmissive","materialUnlit","doubleSided"};
   std::string constants;for(int i=0;i<6;i++)constants+=std::string("const bool ")+names[i]+"="+((variantKey&(1<<i))?"true":"false")+";\n";
   fragmentSource.replace(fragmentSource.find(flags),flags.size(),constants);
   auto specialize=[&](const std::string &from,const std::string &to){fragmentSource.replace(fragmentSource.find(from),from.size(),to);};
   specialize("uniform bool environmentEnabled;",std::string("const bool environmentEnabled=")+((variantKey&64)?"true;":"false;"));
   specialize("uniform int alphaMode;","const int alphaMode="+std::to_string((variantKey>>7)&3)+";");
   specialize("uniform int pointLightCount;","const int pointLightCount="+std::to_string((variantKey>>9)&15)+";");
  }
#ifdef SF_PBR_LIGHT_INVARIANT_EXPERIMENT
  if(reuseLightInvariants){
   const std::string expression="sqrt(nv*nv*(1.-a2)+a2)";
   size_t at=0;while((at=fragmentSource.find(expression,at))!=std::string::npos){fragmentSource.replace(at,expression.size(),"viewVisibilityRoot");at+=18;}
   const std::string marker="  float visibility=";
   fragmentSource.insert(fragmentSource.find(marker),"  float viewVisibilityRoot="+expression+";\n");
   const std::string diffuse="vec3 diffuse=(1.-f)*(1.-metal)*base.rgb/PI;";
   fragmentSource.replace(fragmentSource.find(diffuse),diffuse.size(),"vec3 materialDiffuse=(1.-metal)*base.rgb/PI;vec3 diffuse=(1.-f)*materialDiffuse;");
   const std::string lightDiffuse="(1.-pf)*(1.-metal)*base.rgb/PI";
   fragmentSource.replace(fragmentSource.find(lightDiffuse),lightDiffuse.size(),"(1.-pf)*materialDiffuse");
  }
#endif
#ifdef SF_LIGHT_WORK_EXPERIMENT
  if(optimizeLightWork){const auto first=fragmentSource.find("  for(int i=0;i<8;i++){");const auto last=fragmentSource.find("  if(environmentEnabled){",first);fragmentSource.replace(first,last-first,rejectedLightLoop);}
#endif
  if(fastDepth){
    const std::string slopeCondition="shadowLightIndex>=0?shadowReceiverSlope():vec2(0)";
    fragmentSource.replace(fragmentSource.find(slopeCondition),slopeCondition.size(),"(!depthOnly&&shadowLightIndex>=0)?shadowReceiverSlope():vec2(0)");
    fragmentSource.insert(fragmentSource.find("void main(){"),"uniform bool depthOnly;\n");
    fragmentSource.insert(fragmentSource.find(" vec4 base=")," if(depthOnly&&alphaMode==0){color=vec4(0);return;}\n");
    fragmentSource.insert(fragmentSource.find(" float alpha=")," if(depthOnly){color=vec4(0);return;}\n");
  }
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
  if(minimalDepth){
    // Keep projection and alpha cutoff arithmetic identical to the shipped path.
    vertex=modelVertexShader;
    fragmentSource=R"(#version 300 es
precision highp float;
in vec2 t;
uniform sampler2D material;
uniform vec4 factor;
uniform int alphaMode;
uniform float alphaCutoff;
out vec4 color;
void main(){
 if(alphaMode==0){color=vec4(0);return;}
 vec4 base=texture(material,t)*factor;
 if(alphaMode==1&&base.a<alphaCutoff)discard;
 color=vec4(0);
}
)";
  }
#endif
  GLuint v=compileShader(GL_VERTEX_SHADER,vertex.c_str()),f=compileShader(GL_FRAGMENT_SHADER,fragmentSource.c_str());
  if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
  program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
  if(!ok){glDeleteProgram(program);program=0;return false;}
  depthSupport=fastDepth;
  {
    GLint previous=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous);glUseProgram(program);
    integer("material",0);integer("normalMap",2);integer("metalRoughMap",3);integer("occlusionMap",4);integer("emissiveMap",5);integer("environmentMap",6);integer("environmentBrdf",7);integer("sheenColorMap",8);integer("sheenRoughnessMap",9);integer("sheenAlbedo",10);integer("spotShadowMap",11);integer("shadowLightIndex",-1);
    glUseProgram(GLuint(previous));
  }
  return true;
 }
 void view(const vr::HmdMatrix34_t &anchor,const float *c,const float *e,const float *r,const float *u,const float *f,float width)const{
  glUseProgram(program);vector("center",c);vector("eye",e);vector("panelRight",r);vector("panelUp",u);vector("panelForward",f);scalar("panelHalfWidth",width);
  float axes[3][3];for(int column=0;column<3;column++)for(int row=0;row<3;row++)axes[column][row]=anchor.m[row][column]*(column==2?-1.f:1.f);
  vector("right",axes[0]);vector("up",axes[1]);vector("forward",axes[2]);integer("skinEnabled",0);integer("material",0);
 }
 void lights(std::span<const ResolvedPointLight> lights,float x=0,float y=0,float z=0)const{
  std::array<float,104> payload{};int count=0;
  for(const auto &light:lights){if(count==8)break;const int i=count++*4;
   payload[i]=light.range<0?0:light.position[0]-x;payload[i+1]=light.range<0?0:light.position[1]-y;payload[i+2]=light.range<0?0:-(light.position[2]-z);payload[i+3]=light.range;
   for(int c=0;c<3;c++)payload[32+i+c]=light.color[c];payload[32+i+3]=light.intensity;
   payload[64+i]=light.direction[0];payload[64+i+1]=light.direction[1];payload[64+i+2]=-light.direction[2];payload[64+i+3]=light.outerCos;payload[96+i/4]=light.innerCos;
  }
  if(pointCount==count&&pointPayload==payload)return;
  integer("pointLightCount",count);
  if(count){glUniform4fv(location("pointPositionRange"),count,payload.data());glUniform4fv(location("pointColorIntensity"),count,payload.data()+32);glUniform4fv(location("spotDirectionOuter"),count,payload.data()+64);glUniform1fv(location("spotInner"),count,payload.data()+96);}
  pointCount=count;pointPayload=payload;
 }
 void shadows(const SpotShadowFrame &frame,std::span<const ResolvedPointLight> lights,float x=0,float y=0,float z=0)const{
  int index=-1;if(frame.active)for(size_t i=0;i<lights.size();i++)if(lights[i].id==frame.lightId){index=int(i);break;}
  integer("shadowLightIndex",index);
  if(index<0)return;
  integer("spotShadowMap",11);glUniformMatrix4fv(location("spotShadowMatrix"),1,GL_FALSE,frame.matrix.data());glUniform3f(location("spotShadowOrigin"),x,y,-z);scalar("shadowBias",frame.bias);scalar("shadowNormalBias",frame.normalBias);scalar("shadowTexel",1.f/frame.resolution);
  glActiveTexture(GL_TEXTURE11);glBindTexture(GL_TEXTURE_2D,frame.texture);glActiveTexture(GL_TEXTURE0);shadowBound=true;
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
  vector("sheenColor",p.sheen);scalar("sheenRoughness",p.sheenRoughness);
  const bool hasSheen=!p.materialUnlit&&asset.sheenLookup&&std::any_of(p.sheen,p.sheen+3,[](float c){return c>0;});
  integer("sheenEnabled",hasSheen);
  if(hasSheen){sheenTexturesBound=true;asset.sheenLookup->upload();glActiveTexture(GL_TEXTURE10);glBindTexture(GL_TEXTURE_2D,asset.sheenLookup->texture);}
  integer("materialUnlit",p.materialUnlit);integer("doubleSided",p.doubleSided);
  const auto &l=asset.lighting;vector("lightDirection",l.direction);vector("lightColor",l.color);scalar("intensity",l.intensity);scalar("ambient",l.ambient);scalar("exposure",l.exposure);
  const char *samplers[]={"normalMap","metalRoughMap","occlusionMap","emissiveMap","sheenColorMap","sheenRoughnessMap"},*present[]={"hasNormal","hasMetalRough","hasOcclusion","hasEmissive","hasSheenColor","hasSheenRoughness"},*uv[]={"normalUV","metalRoughUV","occlusionUV","emissiveUV","sheenColorUV","sheenRoughnessUV"};
  for(int i=0;i<6;i++){const int unit=i<4?i+2:i+4;integer(samplers[i],unit);integer(present[i],p.maps[i].texture!=0&&(i<4||hasSheen));if(i>=4&&!hasSheen)continue;glUniformMatrix3fv(location(uv[i]),1,GL_FALSE,p.maps[i].uv);glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,p.maps[i].texture);}
  integer("environmentMap",6);integer("environmentBrdf",7);integer("sheenColorMap",8);integer("sheenRoughnessMap",9);integer("sheenAlbedo",10);
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
 void release()const{if(shadowBound){glActiveTexture(GL_TEXTURE11);glBindTexture(GL_TEXTURE_2D,0);shadowBound=false;}glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_CUBE_MAP,0);glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,0);for(int i=2;i<6;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,0);}if(sheenTexturesBound){for(int i=8;i<=10;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,0);}sheenTexturesBound=false;}glActiveTexture(GL_TEXTURE0);glDisableVertexAttribArray(8);glDisableVertexAttribArray(9);glDisableVertexAttribArray(10);}
};
