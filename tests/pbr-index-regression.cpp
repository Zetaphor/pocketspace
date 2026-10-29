// Test-only full-input static PBR indexing: exact pixels and native ABBA cost.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static uint64_t geometryBytes(const ModelAsset &asset){
 uint64_t bytes=0;
 for(const auto &part:asset.primitives)for(GLuint buffer:{part.vbo,part.surfaceVbo,part.indexBuffer})if(buffer){glBindBuffer(GL_ARRAY_BUFFER,buffer);GLint size=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);bytes+=size;}
 return bytes;
}
static bool verifyStream(const PrimitiveAsset &p,bool reordered=false){
 if(!p.indexBuffer)return false;
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p.indexBuffer);
 size_t bytes=size_t(p.vertexCount)*(p.indexType==GL_UNSIGNED_SHORT?2:4);
 const void *indices=glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,0,bytes,GL_MAP_READ_BIT);assert(indices);
 std::vector<uint32_t> order(p.vertexCount);for(size_t v=0;v<order.size();v++)order[v]=p.indexType==GL_UNSIGNED_SHORT?static_cast<const uint16_t*>(indices)[v]:static_cast<const uint32_t*>(indices)[v];
 assert(glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
 std::vector<std::array<uint32_t,42>> actual((order.size()+2)/3),expected(actual.size());
 for(bool surface:{false,true}){
  const size_t width=surface?9:5;const auto &expanded=surface?p.surface:p.vertices;
  glBindBuffer(GL_ARRAY_BUFFER,surface?p.surfaceVbo:p.vbo);
  const auto *data=static_cast<const float*>(glMapBufferRange(GL_ARRAY_BUFFER,0,size_t(p.gpuVertexCount)*width*sizeof(float),GL_MAP_READ_BIT));assert(data);
  for(size_t v=0;v<order.size();v++){
   assert(order[v]<uint32_t(p.gpuVertexCount));
   if(!reordered)assert(std::memcmp(data+order[v]*width,expanded.data()+v*width,width*sizeof(float))==0);
   for(size_t c=0;c<width;c++){
    size_t offset=(v%3)*14+(surface?5:0)+c;
    actual[v/3][offset]=std::bit_cast<uint32_t>(data[order[v]*width+c]);
    expected[v/3][offset]=std::bit_cast<uint32_t>(expanded[v*width+c]);
   }
  }
  assert(glUnmapBuffer(GL_ARRAY_BUFFER));
 }
 const bool changed=actual!=expected;
 std::sort(actual.begin(),actual.end());std::sort(expected.begin(),expected.end());assert(actual==expected);return changed;
}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 // Boundary coverage for both index widths and exact shader-input identity.
 for(size_t unique:{65536u,65538u}){
  ModelAsset a;a.pbr=true;a.lod=false;a.primitives.emplace_back();auto &p=a.primitives[0];p.vertexCount=GLsizei(unique*3);p.rgba={255,255,255,255};
  for(int repeat=0;repeat<3;repeat++)for(size_t v=0;v<unique;v++){
   p.vertices.insert(p.vertices.end(),{float(v),0,0,0,0});
   p.surface.insert(p.surface.end(),{0,0,1,1,0,0,1,0,0});
  }
  setenv("SF_PBR_INDEXED","1",1);uploadModel(a);assert(p.indexBuffer&&p.gpuVertexCount==GLsizei(unique));assert(p.indexType==(unique==65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT));verifyStream(p);deleteModel(a);
 }
 {
  std::vector<float> tuples(15*14,0);
  for(size_t row=1;row<15;row++)tuples[row*14+row-1]=1;
  auto distinct=mesh::index<14>(tuples);assert(distinct.vertices.size()==tuples.size());
 }
 size_t views=0,indexed=0;std::vector<GLuint> buffers;
 {
 Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();
 for(const char *name:{"Box","ToyCar","Avocado","NormalTangentTest","NormalTangentMirrorTest","NegativeScaleTest","TextureEncodingTest","UnlitTest","EmissiveStrengthTest","AlphaBlendModeTest","Fox","AnimatedMorphCube"}){
  const std::string path=std::string(name)=="Avocado"?"assets/Avocado.glb":std::string("assets/samples/")+name+"/"+name+".glb";
  for(int alpha=0;alpha<3;alpha++){
  int ids[3];bool reordered=false;
  for(int mode=0;mode<3;mode++){
   setenv("SF_PBR_INDEXED",std::to_string(mode).c_str(),1);r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.lod=false;a.optimizeVertexCache=mode==2;assert(loadModelFile(path,a));for(auto &p:a.primitives){p.alphaMode=alpha;p.factor[3]=alpha==2?.6f:1;}uploadModel(a);ids[mode]=int(r.scene.assets.size());
   a.environment=loadLightingEnvironment("assets/environments/studio.sfenv");a.lighting.environment="assets/environments/studio.sfenv";assert(a.environment);
   for(const auto &p:a.primitives){if(!mode||a.hasAnimation||a.hasSkin||a.hasMorph)assert(!p.indexBuffer);if(p.indexBuffer){++indexed;reordered|=verifyStream(p,mode==2&&alpha!=2);}for(GLuint b:{p.vbo,p.surfaceVbo,p.indexBuffer})if(b)buffers.push_back(b);assert(!p.lod[0].indexBuffer&&!p.lod[1].indexBuffer);}
  }
  assert(geometryBytes(r.scene.assets[ids[1]-1])<=geometryBytes(r.scene.assets[ids[0]-1]));
  if(std::string(name)=="ToyCar")assert(reordered==(alpha!=2));
  for(int phase=0;phase<2;phase++)for(int eye=0;eye<2;eye++)for(const auto &members:std::vector<std::vector<int>>{{1,2},{1},{2}}){
   std::vector<unsigned char> reference;
   for(int mode=0;mode<3;mode++){
    auto &a=r.scene.assets[ids[mode]-1];for(auto &p:a.primitives){p.alphaMode=alpha;p.factor[3]=alpha==2?.6f:1;}
    a.lighting.environmentRotation=phase*.8f;
    Object o{};o.id=1;o.asset=ids[mode];o.scale=.55f;o.pitch=.15f;o.yaw=.25f+phase*.3f;
    if(a.hasAnimation){o.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=0;state.time=.35+phase*.2;evaluateModelPose(a,state,*o.animationPose);uploadModelPose(*o.animationPose);}
    Object other=o;other.id=2;other.x=.12f;other.y=.09f;other.z=-.1f;other.scale=.35f;r.scene.objects={o,other};
    auto image=r.renderSet(members,{phase*.15f,.1f,-1.2f},eye);
    if(!mode)reference=std::move(image);else{if(image!=reference)std::fprintf(stderr,"PBR index mismatch model=%s alpha=%d phase=%d eye=%d mode=%d\n",name,alpha,phase,eye,mode);assert(image==reference);++views;}
   }
  }
 }
 }
 assert(indexed>0);std::printf("PASS PBR index: %zu exact stereo/alpha/overlap views, %zu indexed primitives, exact reconstructed streams and animation fallback\n",views,indexed);
 }
 for(GLuint buffer:buffers)assert(!glIsBuffer(buffer));
 if(argc>1&&std::string(argv[1])=="--benchmark"){
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  for(int count:{1,16}){
   Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();
   int ids[3];
   for(int mode=0;mode<3;mode++){
    setenv("SF_PBR_INDEXED",std::to_string(mode).c_str(),1);r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=false;asset.optimizeVertexCache=mode==2;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);asset.environment=loadLightingEnvironment("assets/environments/studio.sfenv");assert(asset.environment&&asset.environment->upload());ids[mode]=int(r.scene.assets.size());
   }
   std::vector<int> members;
   for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=ids[0];o.scale=count==1?.7f:.25f;o.x=count==1?0:(i%4-1.5f)*.25f;o.y=count==1?0:(i/4-1.5f)*.25f;o.pitch=.2f;o.yaw=.3f;r.scene.objects.push_back(o);members.push_back(o.id);}

   assert(r.pbrProgram.init());
   for(int variant:{1,2})for(int stage=0;stage<4;stage++){
    const int mode=stage==1||stage==2?variant:0;for(auto &o:r.scene.objects)o.asset=ids[mode];double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<150;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);assert(r.pbrDepthDraws==0&&r.pbrDepthTriangles==0&&r.pbrColorDraws==uint64_t(count)*3&&r.pbrColorTriangles==uint64_t(count)*108936);const auto enqueued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(enqueued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("PBR_INDEX_SAMPLE {\"objects\":%d,\"stage\":%d,\"mode\":%d,\"frames\":120,\"eyePixels\":768,\"optimizeVertexCache\":%s,\"colorDraws\":%llu,\"colorTriangles\":%llu,\"geometryBytes\":%llu,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",count,stage,mode,r.scene.assets[ids[mode]-1].optimizeVertexCache?"true":"false",(unsigned long long)r.pbrColorDraws,(unsigned long long)r.pbrColorTriangles,(unsigned long long)geometryBytes(r.scene.assets[ids[mode]-1]),gpu/120,cpu/120,wall/120);std::fflush(stdout);
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
