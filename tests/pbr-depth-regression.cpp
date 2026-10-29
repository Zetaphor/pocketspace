#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static void assertSame(Renderer &r,const std::vector<int> &members,physics::Vec3 head,int eye){
 r.fastPbrDepth=true;auto candidate=r.renderSet(members,head,eye,0,true,1,true);
 r.fastPbrDepth=false;auto reference=r.renderSet(members,head,eye,0,true,1,true);
 assert(candidate==reference);assert(glGetError()==GL_NO_ERROR);
 for(GLuint attribute:{8u,9u,10u}){GLint enabled=1;glGetVertexAttribiv(attribute,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);assert(!enabled);}
}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 size_t views=0;GLuint fastProgram=0;
 {
 Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();
 GLint current=0;glUseProgram(r.program);assert(r.depthPbrProgram.init(true));glGetIntegerv(GL_CURRENT_PROGRAM,&current);assert(GLuint(current)==r.program);
 for(const char *path:{"tests/fixtures/pbr/MissingNormals.glb","tests/fixtures/pbr/MissingNormalsMirrored.glb"}){
  r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;assert(loadModelFile(path,a));uploadModel(a);a.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(a.environment);
  auto &p=a.primitives[0];const unsigned char pixels2x2[]={255,255,255,0,255,255,255,127,255,255,255,128,255,255,255,255};
  glBindTexture(GL_TEXTURE_2D,p.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels2x2);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  Object target{};target.id=1;target.asset=3;target.scale=.9f;target.z=.7f;Object occluder=target;occluder.id=2;occluder.asset=int(r.scene.assets.size());occluder.scale=.7f;occluder.z=-.1f;Object second=occluder;second.id=3;second.x=.18f;second.y=.08f;second.z=.02f;r.scene.objects={target,occluder,second};
  // Ensure external depth materially changes the target, including real alpha
  // cutouts. Equality alone could otherwise pass for an occluder behind it.
  p.factor[3]=1;p.alphaCutoff=.5f;p.doubleSided=true;
  auto coverage=[](const std::vector<unsigned char> &image){size_t count=0;for(size_t i=3;i<image.size();i+=4)count+=image[i]!=0;return count;};
  r.fastPbrDepth=true;p.alphaMode=2;size_t blended=coverage(r.renderSet({1},{.1f,.1f,-1.2f},0));
  p.alphaMode=0;size_t opaque=coverage(r.renderSet({1},{.1f,.1f,-1.2f},0));
  p.alphaMode=1;size_t masked=coverage(r.renderSet({1},{.1f,.1f,-1.2f},0));
  assert(opaque<masked&&masked<blended);
  for(int alpha=0;alpha<3;alpha++)for(float factor:{.5f,1.f})for(float cutoff:{0.f,.5f,1.f})for(bool sided:{false,true})for(float headZ:{-1.2f,1.2f})for(int eye=0;eye<2;eye++)for(auto members:{std::vector<int>{1},std::vector<int>{1,2},std::vector<int>{2,3},std::vector<int>{1,2,3}}){
   p.alphaMode=alpha;p.factor[3]=factor;p.alphaCutoff=cutoff;p.doubleSided=sided;
   assertSame(r,members,{.1f,.1f,headZ},eye);++views;
  }
 }
 for(const char *name:{"ToyCar","NormalTangentTest","NormalTangentMirrorTest","NegativeScaleTest","Fox","RiggedSimple","AnimatedMorphCube","InterpolationTest"}){
  r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",a));uploadModel(a);a.environment=loadLightingEnvironment("assets/environments/studio.psenv");
  Object target{};target.id=1;target.asset=3;target.scale=.8f;target.z=.7f;Object o=target;o.id=2;o.asset=int(r.scene.assets.size());o.scale=.45f;o.z=-.1f;o.pitch=.2f;o.yaw=.3f;r.scene.objects={target,o};
  for(int phase=0;phase<3;phase++)for(int eye=0;eye<2;eye++){
   auto &object=r.scene.objects[1];object.lodLevel=phase;object.yaw=.3f+phase*.4f;
   if(a.hasAnimation){object.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=0;state.time=.2+phase*.3;evaluateModelPose(a,state,*object.animationPose);uploadModelPose(*object.animationPose);}
   assertSame(r,{1},{.15f,.05f,-1.2f},eye);assertSame(r,{2},{.15f,.05f,-1.2f},eye);views+=2;
  }
 }
 float c[3]={0,0,0},e[3]={0,0,1},right[3]={1,0,0},up[3]={0,1,0},forward[3]={0,0,-1};r.depthPbrProgram.view(r.anchor,c,e,right,up,forward,1);glGetIntegerv(GL_CURRENT_PROGRAM,&current);fastProgram=GLuint(current);glUseProgram(r.program);
 }
 assert(fastProgram&&!glIsProgram(fastProgram));assert(pbrLodBudget.used==0);
 std::printf("PASS PBR depth: %zu exact stereo/alpha/overlap/LOD/pose views, preserved program/attribute state and shader cleanup\n",views);std::fflush(stdout);
 if(argc>1&&std::string(argv[1])=="--benchmark"){
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  for(int count:{4,16})for(bool depthWork:{true,false}){
   Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);asset.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(asset.environment&&asset.environment->upload());const int id=int(r.scene.assets.size());

   assert(r.pbrProgram.init(false)&&r.depthPbrProgram.init(true));
   std::vector<int> members;
   Object target{};target.id=1;target.asset=3;target.scale=.95f;target.z=.7f;if(depthWork){r.scene.objects.push_back(target);members.push_back(1);}
   for(int i=0;i<count;i++){Object o{};o.id=i+2;o.asset=id;o.scale=count==4?.32f:.2f;o.x=(i%(count==4?2:4)-(count==4?.5f:1.5f))*.18f;o.y=(i/(count==4?2:4)-(count==4?.5f:1.5f))*.18f;o.z=-.1f;o.pitch=.2f;o.yaw=.3f;r.scene.objects.push_back(o);if(!depthWork)members.push_back(o.id);}
   std::array<uint64_t,4> workload{};
   for(int stage=0;stage<4;stage++){
    const int mode=stage==1||stage==2?1:0;r.fastPbrDepth=mode;double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<150;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false,1,true);end(GL_TIME_ELAPSED_EXT);
     std::array<uint64_t,4> actual={r.pbrDepthDraws,r.pbrColorDraws,r.pbrDepthTriangles,r.pbrColorTriangles};
     if(stage==0&&frame==0)workload=actual;else assert(actual==workload);
     assert(depthWork?(r.pbrDepthDraws>0&&r.pbrColorDraws==0):(r.pbrDepthDraws==0&&r.pbrColorDraws>0));
     const auto enqueued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(enqueued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("PBR_DEPTH_SAMPLE {\"objects\":%d,\"stage\":%d,\"mode\":%d,\"frames\":120,\"eyePixels\":768,\"depthWork\":%s,\"depthDraws\":%llu,\"colorDraws\":%llu,\"depthTriangles\":%llu,\"colorTriangles\":%llu,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",count,stage,mode,depthWork?"true":"false",(unsigned long long)workload[0],(unsigned long long)workload[1],(unsigned long long)workload[2],(unsigned long long)workload[3],gpu/120,cpu/120,wall/120);std::fflush(stdout);
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
