// Sheen import, independently integrated BRDF energy and actual GLES pixels.
#define SF_PBR_VARIANT_EXPERIMENT
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static double linear(double x){x/=255;return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
static int delta(const std::vector<unsigned char> &a,const std::vector<unsigned char> &b){int d=0;assert(a.size()==b.size());for(size_t i=0;i<a.size();i++)d=std::max(d,std::abs(int(a[i])-int(b[i])));return d;}
int main(int argc,char **argv){
 for(double r:{0.,.07,.2,.5,1.})for(double nv:{.02,.1,.5,1.}){
  double integrated=sheen::albedo(nv,r,64,128);
  assert(std::isfinite(integrated)&&integrated>=0&&integrated<=std::max(1.,sheen::albedo(1e-6,r,64,128))+1e-3);
  assert(std::abs(integrated-sheen::albedo(nv,r))<.035);
  for(double nl:{.001,.1,.5,1.})for(double nh:{0.,.5,.99,1.}){
   const double value=sheen::brdf(nv,nl,nh,r);
   assert(std::isfinite(value)&&value>=0&&std::abs(value-sheen::brdf(nl,nv,nh,r))<1e-9);
  }
 }
 // Validate the shipped table's normalization against a finer independent grid.
 {
  auto table=sharedSheenLookup();
  for(int y=0;y<SheenLookup::size;y++)for(double nv:{.000001,.02,.1,.5,1.}){
   const double r=(y+.5)/SheenLookup::size,norm=table->values[y*SheenLookup::size*2+1];
   assert(sheen::albedo(nv,r,64,128)*norm<=1.01);
  }
 }
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));GLuint lookup=0;
 {
  Renderer renderer;renderer.ordinaryRouting=false;renderer.scene.objects.clear();
  for(auto &asset:renderer.scene.assets)deleteModel(asset);renderer.scene.assets.clear();
  for(const char *name:{"SheenFactors","SheenTextures"}){
   renderer.scene.assets.emplace_back();auto &asset=renderer.scene.assets.back();asset.pbr=true;
   assert(loadModelFile(std::string("tests/fixtures/pbr/")+name+".glb",asset));
   assert(asset.sheenLookup);uploadModel(asset);lookup=asset.sheenLookup->texture;assert(lookup);
   asset.lighting.ambient=0;asset.lighting.direction[0]=.98f;asset.lighting.direction[1]=0;asset.lighting.direction[2]=.2f;
   assert(std::abs(asset.primitives[0].sheenRoughness-.4f)<1e-6);
  }
  assert(renderer.scene.assets[0].sheenLookup==renderer.scene.assets[1].sheenLookup);
  ModelAsset invalid;invalid.pbr=true;assert(!loadModelFile("tests/fixtures/pbr/SheenUnsupportedUV.glb",invalid));
  Object object{};object.id=1;object.asset=1;object.scale=.6f;renderer.scene.objects={object};
  auto &factors=renderer.scene.assets[0];auto &textured=renderer.scene.assets[1];
  auto &fp=factors.primitives[0];auto &tp=textured.primitives[0];
  assert(tp.maps[4].texture&&tp.maps[5].texture);
  fp.sheen[0]*=linear(128);fp.sheen[1]*=linear(64);fp.sheen[2]*=linear(192);fp.sheenRoughness*=128.f/255;
  for(int eye=0;eye<2;eye++)for(float yaw:{0.f,.4f}){
   renderer.scene.objects[0].yaw=yaw;renderer.scene.objects[0].asset=1;auto expected=renderer.renderSet({1},{.3f,.1f,-1.2f},eye);
   renderer.scene.objects[0].asset=2;auto actual=renderer.renderSet({1},{.3f,.1f,-1.2f},eye);assert(delta(expected,actual)<=2);
  }
  fp.sheenRoughness=.8f;
  renderer.scene.objects[0].asset=1;auto lit=renderer.renderSet({1},{.3f,.1f,-1.2f},0);
  const std::array<float,3> tint={fp.sheen[0],fp.sheen[1],fp.sheen[2]};std::fill_n(fp.sheen,3,0);
  auto noSheen=renderer.renderSet({1},{.3f,.1f,-1.2f},0);assert(lit!=noSheen);
  std::copy(tint.begin(),tint.end(),fp.sheen);fp.materialUnlit=true;auto unlit=renderer.renderSet({1},{.3f,.1f,-1.2f},0);
  std::fill_n(fp.sheen,3,0);assert(unlit==renderer.renderSet({1},{.3f,.1f,-1.2f},0));std::copy(tint.begin(),tint.end(),fp.sheen);fp.materialUnlit=false;
  factors.environment=loadLightingEnvironment("assets/environments/studio.psenv");factors.lighting.intensity=0;
  auto indirect=renderer.renderSet({1},{.3f,.1f,-1.2f},0);factors.lighting.environmentIntensity=0;
  assert(indirect!=renderer.renderSet({1},{.3f,.1f,-1.2f},0));factors.lighting.environmentIntensity=1;
  // General and specialized shaders must preserve sheen, including its textures.
  for(int i=0;i<4;i++){PointLight l;l.id=i+1;l.position={.2f*i,0,-.5f};l.range=3;l.intensity=.4;renderer.scene.lights.push_back(l);}
  for(int asset:{1,2})for(int eye=0;eye<2;eye++){
   renderer.scene.objects[0].asset=asset;renderer.specializePbr=false;auto general=renderer.renderSet({1},{.3f,.1f,-1.2f},eye);
   renderer.specializePbr=true;auto specialized=renderer.renderSet({1},{.3f,.1f,-1.2f},eye);assert(delta(general,specialized)<=1);
  }
  renderer.specializePbr=false;
  renderer.scene.assets.emplace_back();auto &sample=renderer.scene.assets.back();sample.pbr=true;
  assert(loadModelFile("assets/samples/CompareSheen/CompareSheen.glb",sample));assert(sample.sheenLookup);uploadModel(sample);
  renderer.scene.objects[0].asset=int(renderer.scene.assets.size());
  for(int eye=0;eye<2;eye++)for(float yaw:{0.f,.7f}){renderer.scene.objects[0].yaw=yaw;auto image=renderer.renderSet({1},{.1f,.1f,-1.2f},eye);size_t solid=0;for(size_t i=3;i<image.size();i+=4)solid+=image[i]>0;assert(solid>100);}
  if(argc>1&&std::string(argv[1])=="--benchmark"){
   auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
   auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
   auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
   auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
   auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
   std::vector<std::array<float,3>> colors;for(const auto &p:sample.primitives)colors.push_back({p.sheen[0],p.sheen[1],p.sheen[2]});
   renderer.scene.lights.clear();sample.lighting.intensity=3;sample.lighting.ambient=0;
   for(int count:{1,16}){
    renderer.scene.objects.clear();std::vector<int> members;
    for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=int(renderer.scene.assets.size());o.scale=count==1?.6f:.18f;o.x=count==1?0:(i%4-1.5f)*.25f;o.y=count==1?0:(i/4-1.5f)*.25f;o.yaw=.3f;renderer.scene.objects.push_back(o);members.push_back(o.id);}
    for(int stage=0;stage<4;stage++){
     const bool enabled=stage==1||stage==2;
     for(size_t i=0;i<sample.primitives.size();i++)for(int c=0;c<3;c++)sample.primitives[i].sheen[c]=enabled?colors[i][c]:0;
     double gpu=0,cpu=0,wall=0;GLuint query=0;gen(1,&query);
     for(int frame=0;frame<48;frame++){
      const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);
      renderer.renderSet(members,{.1f,.1f,-1.2f},frame%2,0,false);
      end(GL_TIME_ELAPSED_EXT);const auto queued=std::chrono::steady_clock::now();glFinish();const auto finished=std::chrono::steady_clock::now();
      GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
      if(frame>=12){gpu+=double(ns)/1e6;cpu+=std::chrono::duration<double,std::milli>(queued-start).count();wall+=std::chrono::duration<double,std::milli>(finished-start).count();}
     }
     destroy(1,&query);printf("SHEEN_SAMPLE objects=%d stage=%d enabled=%d eye_pixels=768 frames=36 gpu_ms=%.6f enqueue_ms=%.6f wall_ms=%.6f\n",count,stage,int(enabled),gpu/36,cpu/36,wall/36);fflush(stdout);
    }
   }
  }
  for(int unit:{8,9,10}){glActiveTexture(GL_TEXTURE0+unit);GLint binding=1;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);assert(binding==0);}glActiveTexture(GL_TEXTURE0);
 }
 assert(!glIsTexture(lookup)&&glGetError()==GL_NO_ERROR);
 {
  const auto file=std::filesystem::temp_directory_path()/("pocketspace-sheen-"+std::to_string(getpid())+".js");
  std::ofstream(file)<<"const model=loadModel('assets/samples/CompareSheen/CompareSheen.glb',768,{pbr:true});";
  ScriptHost host;assert(loadScript(host,file.string()));uploadModel(host.scene.assets[0]);
  GLuint owned=host.scene.assets[0].sheenLookup->texture;assert(owned);
  JSValue result=JS_Eval(host.ctx,"unloadModel(model)",18,"sheen-unload",JS_EVAL_TYPE_GLOBAL);
  assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
  assert(!host.scene.assets[0].sheenLookup&&!glIsTexture(owned));closeScript(host);
  currentScene=liveScene=nullptr;std::filesystem::remove(file);
 }

 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 puts("PASS sheen: BRDF reciprocity/energy, factors, sRGB color/alpha roughness, UV rejection, stereo, unlit, environment, variants, Khronos sample and resource cleanup");
}
