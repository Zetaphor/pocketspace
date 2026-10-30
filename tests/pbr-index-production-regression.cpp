// Actual production upload policy; no SF_PBR_INDEX_EXPERIMENT routing controls.
#define SF_PBR_INDEX_HELPERS_ONLY
#include "pbr-index-regression.cpp"
#include "../src/pbr_lod.h"
static uint64_t allGeometryBytes(const ModelAsset &asset){
 uint64_t bytes=geometryBytes(asset);for(const auto &p:asset.primitives)for(const auto &lod:p.lod)bytes+=lod.budgetBytes;return bytes;
}
static uint64_t triangles(const ModelAsset &asset,int level){uint64_t n=0;for(const auto &p:asset.primitives)n+=modelDrawVertexCount(p,level)/3;return n;}
static void benchmarkMaterials(){
 auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
 for(const auto &[name,count]:std::vector<std::pair<std::string,int>>{{"Avocado",16},{"Lantern",16},{"MetalRoughSpheres",1},{"MetalRoughSpheresNoTextures",1}}){
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();int ids[2];
  for(int mode=0;mode<2;mode++){
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.lod=false;a.optimizeVertexCache=mode;
   assert(loadModelFile(name=="Avocado"?"assets/Avocado.glb":"assets/samples/"+name+"/"+name+".glb",a));uploadModel(a);
   for(const auto &p:a.primitives){assert(p.alphaMode==0);if(p.indexBuffer)verifyStream(p,true);}
   a.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(a.environment&&a.environment->upload());ids[mode]=int(r.scene.assets.size());
  }
  assert(r.pbrProgram.init()&&r.depthPbrProgram.init(true));
  const uint64_t work=triangles(r.scene.assets[ids[0]-1],0)*count,draws=r.scene.assets[ids[0]-1].primitives.size()*count;
  assert(work==triangles(r.scene.assets[ids[1]-1],0)*count);
  for(bool depthOnly:{false,true}){
   r.scene.objects.clear();std::vector<int> members;
   for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=ids[0];o.scale=count==1?.7f:.2f;o.x=count==1?0:(i%4-1.5f)*.23f;o.y=count==1?0:(i/4-1.5f)*.23f;o.pitch=.2f;o.yaw=.3f;r.scene.objects.push_back(o);members.push_back(o.id);}
   if(depthOnly){Object target{};target.id=100;target.asset=3;target.z=.7f;target.scale=.95f;r.scene.objects.push_back(target);members={100};}
   for(int stage=0;stage<4;stage++){
    const bool cache=stage==1||stage==2;for(auto &o:r.scene.objects)if(o.id!=100)o.asset=ids[cache];double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<150;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);const auto queued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     assert((depthOnly?r.pbrDepthTriangles:r.pbrColorTriangles)==work&&(depthOnly?r.pbrColorTriangles:r.pbrDepthTriangles)==0);assert((depthOnly?r.pbrDepthDraws:r.pbrColorDraws)==draws);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(queued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("PBR_MATERIAL_CACHE_SAMPLE {\"model\":\"%s\",\"objects\":%d,\"stage\":%d,\"cache\":%s,\"lod\":false,\"depthOnly\":%s,\"frames\":120,\"eyePixels\":768,\"triangles\":%llu,\"draws\":%llu,\"geometryBytes\":%llu,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",name.c_str(),count,stage,cache?"true":"false",depthOnly?"true":"false",(unsigned long long)work,(unsigned long long)draws,(unsigned long long)geometryBytes(r.scene.assets[ids[cache]-1]),gpu/120,cpu/120,wall/120);std::fflush(stdout);
   }
  }
 }
}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t views=0,indexed=0;bool reduced=false;uint64_t compactBudget=0;
 // Production ignores the diagnostic override, even when it asks for expansion.
 setenv("SF_PBR_INDEXED","0",1);
 for(const char *name:{"ToyCar","NormalTangentTest","NormalTangentMirrorTest","NegativeScaleTest","AlphaBlendModeTest","TextureEncodingTest","Fox","AnimatedMorphCube","Lantern","MetalRoughSpheres","MetalRoughSpheresNoTextures","UnlitTest","EmissiveStrengthTest","Box","RiggedSimple","RiggedFigure","CesiumMan","InterpolationTest","Avocado"}){
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();int ids[2];std::vector<GLuint> owned;
  for(int mode=0;mode<2;mode++){
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.optimizeVertexCache=mode;a.lod=std::string(name).find("MetalRoughSpheres")==std::string::npos;assert(loadModelFile(std::string(name)=="Avocado"?"assets/Avocado.glb":std::string("assets/samples/")+name+"/"+name+".glb",a));
   auto cpu=a.primitives;auto picks=a.pickTriangles;uploadModel(a);ids[mode]=int(r.scene.assets.size());assert(a.pickTriangles.size()==picks.size());
   bool reordered=false;
   for(size_t i=0;i<a.primitives.size();i++){
    const auto &p=a.primitives[i];assert(p.vertices==cpu[i].vertices&&p.surface==cpu[i].surface);
    if(!mode||p.alphaMode==2||a.hasAnimation||a.hasSkin||a.hasMorph)assert(!p.indexBuffer);
    if(p.indexBuffer){++indexed;reordered|=verifyStream(p,true);}
    for(GLuint b:{p.vbo,p.surfaceVbo,p.indexBuffer})if(b)owned.push_back(b);
    for(const auto &lod:p.lod)if(lod.vbo){reduced=true;owned.push_back(lod.vbo);owned.push_back(lod.surfaceVbo);if(!mode)assert(!lod.indexBuffer);else if(lod.indexBuffer)owned.push_back(lod.indexBuffer);
     uint64_t allocated=0;for(GLuint buffer:{lod.vbo,lod.surfaceVbo,lod.indexBuffer})if(buffer){glBindBuffer(GL_ARRAY_BUFFER,buffer);GLint bytes=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);assert(bytes>0);allocated+=uint64_t(bytes);}assert(allocated==lod.budgetBytes);
     if(mode&&std::string(name)=="ToyCar")compactBudget+=lod.budgetBytes;
     if(lod.indexBuffer)assert(lod.indexType==(lod.gpuVertexCount<=65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT));}
   }
   if(mode&&std::string(name)=="ToyCar")assert(reordered);
   a.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(a.environment);
  }
  for(size_t i=0;i<r.scene.assets[ids[1]-1].primitives.size();i++)for(int level:{1,2}){
   const auto &source=r.scene.assets[ids[0]-1].primitives[i];const auto &part=r.scene.assets[ids[1]-1].primitives[i];
   const auto *control=modelLodLevel(source,level),*lod=modelLodLevel(part,level);assert(bool(control)==bool(lod));
   if(!lod)continue;assert(control->count==lod->count&&control->error==lod->error);
   if(lod->indexBuffer){
    PrimitiveAsset check;check.vertexCount=lod->count;check.gpuVertexCount=lod->gpuVertexCount;check.vbo=lod->vbo;check.surfaceVbo=lod->surfaceVbo;check.indexBuffer=lod->indexBuffer;check.indexType=lod->indexType;
    for(bool surface:{false,true}){const size_t count=size_t(control->count)*(surface?9:5);glBindBuffer(GL_ARRAY_BUFFER,surface?control->surfaceVbo:control->vbo);const auto *data=static_cast<const float*>(glMapBufferRange(GL_ARRAY_BUFFER,0,count*sizeof(float),GL_MAP_READ_BIT));assert(data);(surface?check.surface:check.vertices).assign(data,data+count);assert(glUnmapBuffer(GL_ARRAY_BUFFER));}
    verifyStream(check,true);
   }
  }
  assert(geometryBytes(r.scene.assets[ids[1]-1])<=geometryBytes(r.scene.assets[ids[0]-1]));
  for(int level:{0,1,2})for(int phase:{0,1})for(int eye:{0,1})for(const auto &members:std::vector<std::vector<int>>{{1,2},{1},{2}}){
   std::vector<unsigned char> reference;
   for(int mode=0;mode<2;mode++){
    auto &a=r.scene.assets[ids[mode]-1];Object o{};o.id=1;o.asset=ids[mode];o.scale=.35f;o.pitch=.15f;o.yaw=.25f+phase*.3f;o.lodLevel=level;
    if(a.hasAnimation){o.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=0;state.time=.35+phase*.2;evaluateModelPose(a,state,*o.animationPose);uploadModelPose(*o.animationPose);}
    Object other=o;other.id=2;other.x=.12f;other.z=-.1f;other.scale=.25f;r.scene.objects={o,other};
    auto image=r.renderSet(members,{phase*.15f,.1f,-1.2f},eye);
    if(!mode)reference=std::move(image);else{if(image!=reference)std::fprintf(stderr,"production index mismatch %s level=%d phase=%d eye=%d\n",name,level,phase,eye);assert(image==reference);++views;}
   }
  }
  r.scene.objects.clear();for(auto &a:r.scene.assets)deleteModel(a);for(GLuint b:owned)assert(!glIsBuffer(b));assert(pbrLodBudget.used==0);
 }
 size_t coplanarChanges=0;
 for(int alpha:{0,1}){
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();int ids[2];
  for(int mode=0;mode<2;mode++){
   ModelAsset a;a.pbr=true;a.lod=false;a.optimizeVertexCache=mode;a.primitives.emplace_back();auto &p=a.primitives[0];
   p.materialUnlit=true;p.doubleSided=true;p.alphaMode=alpha;p.width=2;p.height=1;p.rgba={255,0,0,255,0,0,255,255};p.baseMinFilter=p.baseMagFilter=GL_NEAREST;
   for(int y=0;y<8;y++)for(int x=0;x<8;x++)for(int tri=0;tri<2;tri++)for(int layer=0;layer<2;layer++){
    const int order=((x+y+tri)%2)?1-layer:layer;const float uv=order?.75f:.25f;
    const int corners[2][3][2]={{{0,0},{1,0},{1,1}},{{0,0},{1,1},{0,1}}};
    for(int v=0;v<3;v++){const float px=float(x+corners[tri][v][0])/8-.5f,py=float(y+corners[tri][v][1])/8-.5f;p.vertices.insert(p.vertices.end(),{px,py,0,uv,.5f});p.surface.insert(p.surface.end(),{0,0,1,1,0,0,1,uv,.5f});}
   }
   p.vertexCount=GLsizei(p.vertices.size()/5);uploadModel(a);if(mode)assert(p.indexBuffer&&verifyStream(p,true));r.scene.assets.push_back(std::move(a));ids[mode]=int(r.scene.assets.size());
  }
  for(int eye:{0,1}){
   Object o{};o.id=1;o.asset=ids[0];o.scale=.7f;r.scene.objects={o};auto original=r.renderSet({1},{0,.1f,-1.2f},eye);
   r.scene.objects[0].asset=ids[1];auto optimized=r.renderSet({1},{0,.1f,-1.2f},eye);size_t changed=0;
   for(size_t i=0;i<original.size();i+=4){assert(original[i+3]==optimized[i+3]);changed+=!std::equal(original.begin()+i,original.begin()+i+3,optimized.begin()+i);}
   assert(changed>0);coplanarChanges+=changed;std::printf("PBR_COPLANAR_SAMPLE {\"alphaMode\":%d,\"eye\":%d,\"changedPixels\":%zu}\n",alpha,eye,changed);
  }
 }
 assert(coplanarChanges>0);std::printf("PASS coplanar order sensitivity: unchanged geometry/coverage with changed equal-depth color ownership; retain opt-in default\n");
 assert(indexed>0&&reduced&&compactBudget>0);
 // Admit the complete compact payload even when expanded levels would not fit.
 assert(pbrLodBudget.reserve(PbrLodBudget::limit-compactBudget));
 {
  ModelAsset a;a.pbr=true;a.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));uploadModel(a);
  uint64_t charged=0;for(const auto &p:a.primitives)for(const auto &lod:p.lod)charged+=lod.budgetBytes;
  assert(charged==compactBudget&&pbrLodBudget.used==PbrLodBudget::limit);deleteModel(a);assert(pbrLodBudget.used==PbrLodBudget::limit-compactBudget);
 }
 pbrLodBudget.release(PbrLodBudget::limit-compactBudget);
 // Full-detail indexing is independent of the optional reduced-pair budget.
 assert(pbrLodBudget.reserve(PbrLodBudget::limit));
 {
  ModelAsset a;a.pbr=true;a.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));uploadModel(a);
  assert(a.primitives[0].indexBuffer&&geometryBytes(a)==5550662);
  for(const auto &p:a.primitives)for(const auto &lod:p.lod)assert(!lod.vbo&&!lod.surfaceVbo&&!lod.budgetBytes);
  deleteModel(a);assert(pbrLodBudget.used==PbrLodBudget::limit);
 }
 pbrLodBudget.release(PbrLodBudget::limit);
 // Reject each partial upload and CPU staging failure without publishing it.
 for(int failure:{-2,0,1,2}){
  ModelAsset a;a.pbr=true;a.lod=false;a.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));
  const auto before=pbrIndexValidation::rejectedBuffers;pbrIndexValidation::failCpu=failure==-2;pbrIndexValidation::failStage=failure;
  uploadModel(a);pbrIndexValidation::failCpu=false;pbrIndexValidation::failStage=-1;
  for(const auto &p:a.primitives)assert(!p.indexBuffer&&p.gpuVertexCount==p.vertexCount&&glIsBuffer(p.vbo)&&glIsBuffer(p.surfaceVbo));
  assert(pbrIndexValidation::rollbackClean);if(failure>=0)assert(pbrIndexValidation::rejectedBuffers>before);deleteModel(a);
 }
 {
  ModelAsset a;a.pbr=true;a.optimizeVertexCache=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));
  pbrLod::failIndexUpload=true;uploadModel(a);pbrLod::failIndexUpload=false;
  assert(a.primitives[0].indexBuffer&&pbrLodBudget.used==0&&pbrLod::rejectedPairsClean);
  for(const auto &p:a.primitives)for(const auto &lod:p.lod)assert(!lod.vbo&&!lod.surfaceVbo&&!lod.indexBuffer&&!lod.budgetBytes);
  deleteModel(a);
 }
 {
  const auto folder=std::filesystem::temp_directory_path()/("sf-pbr-index-"+std::to_string(getpid()));std::filesystem::create_directory(folder);const auto script=folder/"scene.js";
  std::ofstream(script)<<R"JS(
   function check(v){if(!v)throw Error('index assertion');}
   const asset=loadModel('assets/samples/ToyCar/ToyCar.glb',768,{pbr:true,optimizeVertexCache:true});
   const owner=createSceneContainer();createObject(asset,0,0,0,.3,{container:owner});
   const saved=captureSceneContainer(owner);check(saved.version===11&&saved.models[0].optimizeVertexCache&&saved.models[0].lod);
   const restored=restoreSceneContainer(saved);check(captureSceneContainer(restored.containerId).models[0].optimizeVertexCache);
   let failed=false;try{loadModel('assets/samples/ToyCar/ToyCar.glb',768,{pbr:true,optimizeVertexCache:false});}catch(e){failed=true;}check(failed);
   function update(){}
  )JS";
  ScriptHost host;assert(loadScript(host,script.string()));assert(host.scene.assets.size()==1);uploadModel(host.scene.assets[0]);const GLuint buffer=host.scene.assets[0].primitives[0].indexBuffer;assert(buffer);const auto budget=pbrLodBudget.used;auto *ctx=host.ctx;
  std::ofstream(script)<<"loadModel('assets/Avocado.glb',768,{pbr:true,optimizeVertexCache:true});throw Error('EXPECTED rejected reload');function update(){}";
  assert(!loadScript(host,script.string()));assert(host.ctx==ctx&&glIsBuffer(buffer)&&pbrLodBudget.used==budget);closeScript(host);assert(!glIsBuffer(buffer)&&pbrLodBudget.used==0);std::filesystem::remove_all(folder);
 }
 std::printf("PASS production PBR indexing: %zu exact stereo/LOD/depth/pose views, %zu indexed primitives, original CPU inputs, blend/animation fallback, allocation rollback, shared snapshots/reload and cleanup\n",views,indexed);
 if(argc>1&&std::string(argv[1])=="--benchmark"){
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();int ids[3];
  for(int mode=0;mode<3;mode++){setenv("SF_PBR_LOD_INDEXED",mode==2?"0":"1",1);r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;a.optimizeVertexCache=mode;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));uploadModel(a);a.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(a.environment&&a.environment->upload());ids[mode]=int(r.scene.assets.size());}
  unsetenv("SF_PBR_LOD_INDEXED");
  assert(r.pbrProgram.init()&&r.depthPbrProgram.init(true));
  for(bool depthOnly:{false,true})for(int level:{0,1,2}){
   r.scene.objects.clear();std::vector<int> members;
   for(int i=0;i<16;i++){Object o{};o.id=i+1;o.asset=ids[0];o.scale=.2f;o.x=(i%4-1.5f)*.23f;o.y=(i/4-1.5f)*.23f;o.pitch=.2f;o.yaw=.3f;o.lodLevel=level;r.scene.objects.push_back(o);members.push_back(o.id);}
   if(depthOnly){Object target{};target.id=17;target.asset=3;target.z=.7f;target.scale=.95f;r.scene.objects.push_back(target);members={17};}
   const uint64_t work=triangles(r.scene.assets[ids[0]-1],level)*16;assert(work==triangles(r.scene.assets[ids[1]-1],level)*16);
   for(int stage=0;stage<4;stage++){
    const int mode=stage==1||stage==2;for(auto &o:r.scene.objects)if(o.id!=17)o.asset=ids[!mode&&level?2:mode];double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<150;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);const auto queued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     assert((depthOnly?r.pbrDepthTriangles:r.pbrColorTriangles)==work&&(depthOnly?r.pbrColorTriangles:r.pbrDepthTriangles)==0);assert((depthOnly?r.pbrDepthDraws:r.pbrColorDraws)==48);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(queued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("PBR_INDEX_PRODUCTION_SAMPLE {\"objects\":16,\"stage\":%d,\"cache\":%s,\"indexedLod\":%s,\"lod\":%d,\"depthOnly\":%s,\"frames\":120,\"eyePixels\":768,\"triangles\":%llu,\"draws\":48,\"geometryBytes\":%llu,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",stage,r.scene.assets[ids[!mode&&level?2:mode]-1].optimizeVertexCache?"true":"false",mode?"true":"false",level,depthOnly?"true":"false",(unsigned long long)work,(unsigned long long)allGeometryBytes(r.scene.assets[ids[!mode&&level?2:mode]-1]),gpu/120,cpu/120,wall/120);std::fflush(stdout);
   }
  }
 }
 if(argc>1&&std::string(argv[1])=="--benchmark-models")benchmarkMaterials();
 assert(pbrLodBudget.used==0&&glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
