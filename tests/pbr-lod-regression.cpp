#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static std::vector<float> readFloats(GLuint buffer,size_t count){
 glBindBuffer(GL_ARRAY_BUFFER,buffer);const auto *data=static_cast<const float*>(glMapBufferRange(GL_ARRAY_BUFFER,0,count*sizeof(float),GL_MAP_READ_BIT));assert(data);std::vector<float> values(data,data+count);assert(glUnmapBuffer(GL_ARRAY_BUFFER));return values;
}
static uint64_t triangleCount(const ModelAsset &asset,int level){uint64_t count=0;for(const auto &p:asset.primitives)count+=modelDrawVertexCount(p,level)/3;return count;}
static void saveImage(const std::string &path,const std::vector<unsigned char> &rgba){
 std::ofstream out(path,std::ios::binary);out<<"P6\n"<<pixels<<" "<<pixels<<"\n255\n";
 for(int y=pixels-1;y>=0;y--)for(int x=0;x<pixels;x++)out.write(reinterpret_cast<const char*>(rgba.data()+size_t(y*pixels+x)*4),3);
}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 std::vector<GLuint> owned;size_t exact=0,quality=0;bool reduced=false;
 {
 Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();
 for(const char *name:{"ToyCar","Avocado","NormalTangentTest","NormalTangentMirrorTest","NegativeScaleTest"}){
  const std::string path=std::string(name)=="Avocado"?"assets/Avocado.glb":std::string("assets/samples/")+name+"/"+name+".glb";
  r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;assert(loadModelFile(path,a));
  auto cpu=a.primitives;auto picks=a.pickTriangles;setenv("SF_PBR_LOD","1",1);uploadModel(a);assert(a.pickTriangles.size()==picks.size());
  for(size_t i=0;i<a.primitives.size();i++){
   const auto &p=a.primitives[i];assert(p.vertices==cpu[i].vertices&&p.surface==cpu[i].surface&&!p.indexBuffer);
   using Key=std::array<uint32_t,14>;std::set<Key> original;
   for(size_t v=0;v<size_t(p.vertexCount);v++){Key k;for(int c=0;c<5;c++)k[c]=std::bit_cast<uint32_t>(p.vertices[v*5+c]);for(int c=0;c<9;c++)k[c+5]=std::bit_cast<uint32_t>(p.surface[v*9+c]);original.insert(k);}
   for(const auto &lod:p.lod)if(lod.vbo){
    reduced=true;assert(lod.count>0&&lod.count%3==0&&lod.count<p.vertexCount&&lod.error<=.001001f&&!lod.indexBuffer&&lod.surfaceVbo);owned.push_back(lod.vbo);owned.push_back(lod.surfaceVbo);
    auto vertices=readFloats(lod.vbo,size_t(lod.count)*5),frames=readFloats(lod.surfaceVbo,size_t(lod.count)*9);
    for(size_t v=0;v<size_t(lod.count);v++){Key k;for(int c=0;c<5;c++)k[c]=std::bit_cast<uint32_t>(vertices[v*5+c]);for(int c=0;c<9;c++)k[c+5]=std::bit_cast<uint32_t>(frames[v*9+c]);assert(original.count(k));}
   }
  }
  a.environment=loadLightingEnvironment("assets/environments/studio.sfenv");assert(a.environment);
  Object o{};o.id=1;o.asset=int(r.scene.assets.size());o.scale=.3f;o.pitch=.2f;o.yaw=.3f;Object other=o;other.id=2;other.asset=3;other.x=.1f;other.z=-.1f;other.scale=.2f;r.scene.objects={o,other};
  // Independently route the reduced streams through the ordinary full-detail
  // draw path, then require identical color/depth/overlap pixels in both eyes.
  for(int level:{1,2})for(int eye=0;eye<2;eye++)for(auto members:{std::vector<int>{1},std::vector<int>{1,2}}){
   r.scene.objects[0].lodLevel=level;auto selected=r.renderSet(members,{.1f,.1f,-1.2f},eye);
   std::vector<std::array<GLuint,4>> saved;
   for(auto &p:a.primitives){saved.push_back({p.vbo,p.surfaceVbo,GLuint(p.vertexCount),GLuint(p.gpuVertexCount)});if(const auto *lod=modelLodLevel(p,level)){p.vbo=lod->vbo;p.surfaceVbo=lod->surfaceVbo;p.vertexCount=p.gpuVertexCount=lod->count;}}
   r.scene.objects[0].lodLevel=0;auto reference=r.renderSet(members,{.1f,.1f,-1.2f},eye);
   for(size_t i=0;i<a.primitives.size();i++){auto &p=a.primitives[i];p.vbo=saved[i][0];p.surfaceVbo=saved[i][1];p.vertexCount=GLsizei(saved[i][2]);p.gpuVertexCount=GLsizei(saved[i][3]);}
   assert(selected==reference);++exact;
  }
  r.scene.objects.resize(1);
  for(float size:{.05f,.16f,.4f})for(int angle=0;angle<3;angle++)for(int eye=0;eye<2;eye++){
   auto &object=r.scene.objects[0];object.scale=size;object.pitch=.15f+angle*.2f;object.yaw=angle*.7f;a.lighting.environmentRotation=angle*.4f;
   object.lodLevel=0;auto full=r.renderSet({1},{0,.1f,-1.2f},eye);
   for(int level:{1,2}){
    object.lodLevel=level;auto image=r.renderSet({1},{0,.1f,-1.2f},eye);size_t mask=0,coverage=0,changed=0;double squared=0;
    for(size_t v=0;v<full.size();v+=4){bool covered=full[v+3]||image[v+3];coverage+=covered;mask+=(full[v+3]!=0)!=(image[v+3]!=0);bool different=false;for(int c=0;c<3;c++){int d=int(full[v+c])-int(image[v+c]);squared+=double(d)*d;different|=d!=0;}changed+=different;}
    std::printf("PBR_LOD_PIXELS {\"model\":\"%s\",\"level\":%d,\"scale\":%.2f,\"angle\":%d,\"eye\":%d,\"coverage\":%zu,\"maskChanged\":%zu,\"changed\":%zu,\"coveredRgbRms\":%.6f}\n",name,level,size,angle,eye,coverage,mask,changed,std::sqrt(squared/std::max(size_t(1),coverage*3)));
    assert(coverage>0);++quality;
    if(eye==0&&size==.4f)if(const char *folder=std::getenv("SF_PBR_LOD_IMAGES")){std::filesystem::create_directories(folder);auto base=std::string(folder)+"/"+name+"-"+std::to_string(angle);saveImage(base+"-full.ppm",full);saveImage(base+"-lod"+std::to_string(level)+".ppm",image);}
   }
  }
 }
 for(int variant=0;variant<7;variant++){
  ModelAsset a;a.pbr=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",a));
  if(variant==0)a.lod=false;if(variant==1)a.hasAnimation=true;if(variant==2)a.hasSkin=true;if(variant==3)a.hasMorph=true;
  if(variant==4||variant==5)for(auto &p:a.primitives)p.alphaMode=variant-3;
  if(variant==6)setenv("SF_PBR_LOD","0",1);else setenv("SF_PBR_LOD","1",1);
  uploadModel(a);for(const auto &p:a.primitives)for(const auto &lod:p.lod)assert(!lod.vbo&&!lod.surfaceVbo);deleteModel(a);
 }
 assert(reduced);
 }
 for(GLuint buffer:owned)assert(!glIsBuffer(buffer));
 std::printf("PASS PBR LOD: %zu exact reduced-stream stereo/depth views, %zu quality views, original shader-input tuples, CPU/picking preservation, eligibility fallbacks and cleanup\n",exact,quality);
 if(argc>1&&std::string(argv[1])=="--benchmark"){
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  for(int count:{1,16}){
   Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();
   setenv("SF_PBR_LOD","1",1);r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);asset.environment=loadLightingEnvironment("assets/environments/studio.sfenv");assert(asset.environment&&asset.environment->upload());const int id=int(r.scene.assets.size());

   std::vector<int> members;
   for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=id;o.scale=count==1?.7f:.25f;o.x=count==1?0:(i%4-1.5f)*.25f;o.y=count==1?0:(i/4-1.5f)*.25f;o.pitch=.2f;o.yaw=.3f;r.scene.objects.push_back(o);members.push_back(o.id);}

   for(int variant:{1,2})for(int stage=0;stage<4;stage++){
    const int mode=stage==1||stage==2?variant:0;for(auto &o:r.scene.objects)o.lodLevel=mode;double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<150;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);const auto enqueued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(enqueued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("PBR_LOD_SAMPLE {\"objects\":%d,\"stage\":%d,\"mode\":%d,\"frames\":120,\"eyePixels\":768,\"triangles\":%llu,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",count,stage,mode,(unsigned long long)triangleCount(asset,mode),gpu/120,cpu/120,wall/120);std::fflush(stdout);
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
