#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static size_t covered(const std::vector<unsigned char> &image){size_t n=0;for(size_t i=3;i<image.size();i+=4)n+=image[i]!=0;return n;}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();size_t views=0;
  for(bool pbr:{false,true})for(const char *path:{"tests/fixtures/pbr/MissingNormals.glb","tests/fixtures/pbr/MissingNormalsMirrored.glb"}){
   r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=pbr;assert(loadModelFile(path,asset));uploadModel(asset);
   Object object{};object.id=1;object.asset=int(r.scene.assets.size());object.scale=.6f;r.scene.objects={object};
   for(int alpha=0;alpha<3;alpha++)for(int eye=0;eye<2;eye++){
    auto &part=asset.primitives[0];part.alphaMode=alpha;part.factor[3]=alpha==2?.7f:1;
    for(float headZ:{-1.2f,1.2f}){
     part.doubleSided=false;r.materialCulling=false;auto all=r.renderSet({1},{0,0,headZ},eye);assert(covered(all)>100);
     r.materialCulling=true;auto single=r.renderSet({1},{0,0,headZ},eye);
     assert(headZ<0?covered(single)==covered(all):covered(single)==0);
     part.doubleSided=true;auto both=r.renderSet({1},{0,0,headZ},eye);assert(covered(both)==covered(all));
     assert(!glIsEnabled(GL_CULL_FACE));GLint winding=0;glGetIntegerv(GL_FRONT_FACE,&winding);assert(winding==GL_CCW);++views;
    }
   }
  }
  // Static repeated mirrored meshes must match ordinary draws exactly.
  for(bool mirrored:{false,true}){
   const char *path=mirrored?"tests/fixtures/pbr/MissingNormalsMirrored.glb":"tests/fixtures/pbr/MissingNormals.glb";
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();assert(loadModelFile(path,a));uploadModel(a);
   Object object{};object.id=1;object.asset=int(r.scene.assets.size());object.scale=.4f;object.x=-.12f;Object second=object;second.id=2;second.x=.12f;r.scene.objects={object,second};
   for(bool sided:{false,true})for(int eye=0;eye<2;eye++)for(float z:{-1.2f,1.2f}){
    a.primitives[0].doubleSided=sided;r.instanceExperiment.mode=ModelInstancing::Off;auto expected=r.renderSet({1,2},{0,0,z},eye);
    r.instanceExperiment.mode=ModelInstancing::Instanced;auto actual=r.renderSet({1,2},{0,0,z},eye);assert(actual==expected);assert(!glIsEnabled(GL_CULL_FACE));++views;
   }
  }
  {
   r.scene.assets.emplace_back();auto &a=r.scene.assets.back();assert(loadModelFile("assets/samples/InterpolationTest/InterpolationTest.glb",a));assert(a.rigidBatchEligible);a.nodes[1].scale[0]=-1;uploadModel(a);
   Object o{};o.id=1;o.asset=int(r.scene.assets.size());o.scale=.8f;o.animationPose=std::make_shared<ModelPose>();r.scene.objects={o};
   for(double time:{.1,.5,.9}){
    animation::Playback state;state.clip=0;state.time=time;evaluateModelPose(a,state,*r.scene.objects[0].animationPose);uploadModelPose(*r.scene.objects[0].animationPose);
    assert(modelPrimitiveMirrored(a.primitives[0],r.scene.objects[0].animationPose.get())!=modelPrimitiveMirrored(a.primitives[1],r.scene.objects[0].animationPose.get()));
    for(int eye=0;eye<2;eye++){
     rigidBatchEnabled=false;auto expected=r.renderSet({1},{0,0,-1.2f},eye);rigidBatchEnabled=true;auto actual=r.renderSet({1},{0,0,-1.2f},eye);assert(expected==actual);++views;
    }
   }
  }
  std::printf("Culling regression passed: %zu front/back, mirrored, alpha, PBR/legacy and instanced views\n",views);
 }
 if(argc>1&&std::string(argv[1])=="--benchmark"){
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  for(bool cars:{false,true}){
   Renderer r;r.ordinaryRouting=false;r.instanceExperiment.mode=ModelInstancing::Instanced;r.scene.objects.clear();
   r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.lod=false;assert(loadModelFile(cars?"assets/samples/ToyCar/ToyCar.glb":"assets/samples/Box/Box.glb",asset));uploadModel(asset);const int id=int(r.scene.assets.size()),count=cars?32:240;std::vector<int> members;
   for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=id;o.scale=cars?.16f:.07f;o.x=(i%8-3.5f)*.15f;o.y=(i/8%5-2)*.15f;o.z=(i/40)*.1f;o.pitch=.15f;o.yaw=.35f;r.scene.objects.push_back(o);members.push_back(o.id);}
   size_t triangles=0;for(const auto &p:asset.primitives)triangles+=p.vertexCount/3;
   for(int round=0;round<4;round++){
    r.materialCulling=round==1||round==2;double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<90;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);
     r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false);
     end(GL_TIME_ELAPSED_EXT);const auto enqueued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(enqueued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("CULLING_SAMPLE {\"case\":\"%s\",\"round\":%d,\"culling\":%s,\"frames\":60,\"objects\":%d,\"sceneTriangles\":%zu,\"eyePixels\":768,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",cars?"32-cars":"240-boxes",round,r.materialCulling?"true":"false",count,triangles*count,gpu/60,cpu/60,wall/60);std::fflush(stdout);
   }
  }
 }
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
