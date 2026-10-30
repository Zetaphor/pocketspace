#pragma once
#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
#include "model_asset.h"
#include "model_shaders.h"
#include "scene_lights.h"
#include "scope_exit.h"

// One light-space map is generated before all surface/eye passes. It never owns
// or retains scene pointers, model buffers, animation poses or compositor images.
class SpotShadows:public SpotShadowFrame {
 GLuint program=0,fbo=0,vao=0,query=0;
 bool pending=false,blocked=false;int warmFrames=0;
 uint64_t revision=0;SpotShadowSettings settings;SpotShadowGpuBudget gpuBudget;
 PFNGLGENQUERIESEXTPROC gen=nullptr;PFNGLBEGINQUERYEXTPROC begin=nullptr;
 PFNGLENDQUERYEXTPROC end=nullptr;PFNGLGETQUERYOBJECTUIVEXTPROC available=nullptr;
 PFNGLGETQUERYOBJECTUI64VEXTPROC result=nullptr;PFNGLDELETEQUERIESEXTPROC destroy=nullptr;
 std::map<std::string,GLint> locations;
 GLint location(const char *name){auto [it,fresh]=locations.emplace(name,-1);if(fresh)it->second=glGetUniformLocation(program,name);return it->second;}
 struct State {
  GLint draw=0,read=0,viewport[4]{},program=0,vao=0,array=0,active=0,textures[2]{},depthFunc=0,front=0,cullMode=0;
  GLboolean depth=false,scissor=false,blend=false,cull=false,depthMask=false,colorMask[4]{};GLfloat clear=1;
  State(){
   glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
   for(int i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,textures+i);}glActiveTexture(GL_TEXTURE0);
   depth=glIsEnabled(GL_DEPTH_TEST);scissor=glIsEnabled(GL_SCISSOR_TEST);blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);
   glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);glGetIntegerv(GL_FRONT_FACE,&front);glGetIntegerv(GL_CULL_FACE_MODE,&cullMode);glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&clear);
  }
  ~State(){
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));glBindBuffer(GL_ARRAY_BUFFER,GLuint(array));
   for(int i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,GLuint(textures[i]));}glActiveTexture(GLenum(active));
   for(auto [cap,enabled]:{std::pair{GL_DEPTH_TEST,depth},std::pair{GL_SCISSOR_TEST,scissor},std::pair{GL_BLEND,blend},std::pair{GL_CULL_FACE,cull}}){if(enabled)glEnable(cap);else glDisable(cap);}
   glDepthFunc(GLenum(depthFunc));glFrontFace(GLenum(front));glCullFace(GLenum(cullMode));glDepthMask(depthMask);glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);glClearDepthf(clear);
  }
 };
 bool init(){
  if(program)return true;
  std::string vertex=modelVertexShader;
  vertex.insert(vertex.find("void main(){"),"uniform mat4 lightMatrix;\n");
  const auto first=vertex.find("  vec3 world="),last=vertex.find("  t=uv;",first);
  vertex.replace(first,last-first,"  vec3 world=offset*vec3(1,1,-1)+modelScale*rotated;\n  gl_Position=lightMatrix*vec4(world,1);\n");
  const char *fragment=R"(#version 300 es
precision highp float;in vec2 t;uniform sampler2D material;uniform int alphaMode;uniform float alpha,alphaCutoff;
void main(){if(alphaMode==1&&texture(material,t).a*alpha<alphaCutoff)discard;}
)";
  auto shader=[](GLenum type,const char *source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),nullptr,log);fprintf(stderr,"[shadow] %s\n",log);glDeleteShader(s);return GLuint(0);}return s;};
  GLuint v=shader(GL_VERTEX_SHADER,vertex.c_str()),f=shader(GL_FRAGMENT_SHADER,fragment);if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
  program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(program);program=0;return false;}
  bool timers=false;GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);for(int i=0;i<count;i++)if(std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,i)),"GL_EXT_disjoint_timer_query")==0)timers=true;
  if(timers){
   gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));available=reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC>(eglGetProcAddress("glGetQueryObjectuivEXT"));result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));
  }
  return true;
 }
 void releaseMap(){if(query&&destroy)destroy(1,&query);query=0;pending=false;if(fbo)glDeleteFramebuffers(1,&fbo);fbo=0;if(texture)glDeleteTextures(1,&texture);texture=0;resolution=0;active=false;}
 bool allocate(int size){
  if(texture&&resolution==size)return true;
  releaseMap();glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH_COMPONENT24,size,size);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,texture,0);const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError()!=GL_NO_ERROR){releaseMap();return false;}resolution=size;gen(1,&query);return true;
 }
public:
 SpotShadows()=default;SpotShadows(const SpotShadows&)=delete;
 bool prepare(){return init();}
 ~SpotShadows(){reset();if(vao)glDeleteVertexArrays(1,&vao);if(program)glDeleteProgram(program);}
 void reset(){releaseMap();lightId=0;blocked=false;warmFrames=0;gpuBudget.reset();spotShadowDiagnostics={};}
 template<class SceneType>void update(const SceneType &scene){
  active=false;spotShadowDiagnostics.active=false;
  const PointLight *source=nullptr;
  for(const auto &light:scene.lights)if(light.spot&&light.enabled&&light.intensity>0&&light.shadow.enabled){source=&light;break;}
  if(!source){reset();return;}
  auto &stats=spotShadowDiagnostics;stats.lightId=source->id;stats.status="preparing";
  if(source->id!=lightId||source->shadowRevision!=revision||!(source->shadow==settings)){releaseMap();settings=source->shadow;revision=source->shadowRevision;lightId=source->id;warmFrames=0;blocked=false;gpuBudget.reset();stats.gpuMs.reset();stats.bytes=0;stats.resolution=0;}
  if(!init()){stats.status="shader-unavailable";return;}
  if(!(gen&&begin&&end&&available&&result&&destroy)){stats.status="timer-unavailable";return;}
  if(pending){GLuint ready=0;available(query,GL_QUERY_RESULT_AVAILABLE_EXT,&ready);if(ready){GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);pending=false;if(!disjoint){stats.gpuMs=double(ns)/1e6;if(gpuBudget.add(*stats.gpuMs,settings.maxGpuMs))blocked=true;}}}
  if(blocked){releaseMap();stats.bytes=0;stats.resolution=0;stats.status="gpu-budget";return;}
  const auto lights=resolvePointLights(scene);auto found=std::find_if(lights.begin(),lights.end(),[&](const auto &light){return light.id==lightId;});if(found==lights.end()){stats.status="unresolved";return;}
  uint64_t triangles=0;size_t casters=0;
  for(const auto &object:scene.objects)if(object.visible){const auto &asset=scene.assets[size_t(object.asset)-1];bool caster=false;for(const auto &part:asset.primitives)if(part.alphaMode!=2){triangles+=uint64_t(part.vertexCount)/3;caster=true;}casters+=caster;}
  stats.triangles=triangles;
  if(triangles>uint64_t(settings.maxTriangles)||casters>128){releaseMap();stats.bytes=0;stats.resolution=0;stats.status="geometry-budget";return;}
  State state;
  if(!allocate(settings.resolution)){stats.status="allocation-failed";return;}
  stats.resolution=resolution;stats.bytes=uint64_t(resolution)*resolution*4;
  // Scene-space +Z points away from the viewer; shader space uses its negation.
  auto d=found->direction;d[2]=-d[2];std::array<float,3> up=std::abs(d[1])>.99f?std::array<float,3>{1,0,0}:std::array<float,3>{0,1,0};
  auto cross=[](const auto &a,const auto &b){return std::array<float,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
  auto right=cross(d,up);float length=std::sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);for(auto &c:right)c/=length;up=cross(right,d);
  const float tangent=std::tan(source->outerConeAngle),a=(source->range+settings.nearPlane)/(source->range-settings.nearPlane),b=-2*source->range*settings.nearPlane/(source->range-settings.nearPlane);
  const std::array<float,3> position={found->position[0],found->position[1],-found->position[2]};matrix={};
  for(int c=0;c<3;c++){matrix[c*4]=right[c]/tangent;matrix[c*4+1]=up[c]/tangent;matrix[c*4+2]=d[c]*a;matrix[c*4+3]=d[c];matrix[12]-=matrix[c*4]*position[c];matrix[13]-=matrix[c*4+1]*position[c];matrix[14]-=matrix[c*4+2]*position[c];matrix[15]-=matrix[c*4+3]*position[c];}matrix[14]+=b;
  bias=settings.bias;normalBias=settings.normalBias;
  const bool timed=warmFrames++>=3&&!pending;if(timed)begin(GL_TIME_ELAPSED_EXT,query);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,resolution,resolution);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glFrontFace(GL_CCW);glCullFace(GL_BACK);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);glUseProgram(program);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glUniformMatrix4fv(location("lightMatrix"),1,GL_FALSE,matrix.data());glUniform1i(location("material"),0);glUniform1i(location("skinEnabled"),0);glUniform1i(location("skinPalette"),1);
  {
   ModelCulling culling;
   for(const auto &object:scene.objects)if(object.visible){
    const auto &asset=scene.assets[size_t(object.asset)-1];
    glUniform3f(location("offset"),object.x,object.y,object.z);glUniform1f(location("modelScale"),object.scale);glUniform1f(location("pitch"),object.pitch);glUniform1f(location("yaw"),object.yaw);glUniform1f(location("roll"),object.roll);
    for(size_t i=0;i<asset.primitives.size();i++){const auto &part=asset.primitives[i];if(part.alphaMode==2)continue;
     culling.material(part,object.animationPose.get());glUniform1i(location("alphaMode"),part.alphaMode);glUniform1f(location("alpha"),part.factor[3]);glUniform1f(location("alphaCutoff"),part.alphaCutoff);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
     if(object.animationPose&&bindSkinPrimitive(asset,part,*object.animationPose,program)){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glDrawElements(GL_TRIANGLES,GLsizei(part.skinIndices.size()),GL_UNSIGNED_INT,nullptr);unbindSkinPrimitive(program);}
     else if(object.animationPose)drawPosePrimitive(part,*object.animationPose,i);else drawModelPrimitive(part,0,0);
    }
   }
  }
  // A VAO retains its attribute/index buffers even after glDeleteBuffers in
  // another VAO. Drop these references now so removed poses/unloaded assets
  // cannot remain GPU-owned by the shadow pass between frames.
  glBindVertexArray(0);glDeleteVertexArrays(1,&vao);vao=0;
  if(timed){end(GL_TIME_ELAPSED_EXT);pending=true;}
  active=true;stats.active=true;stats.status="active";
 }
};
