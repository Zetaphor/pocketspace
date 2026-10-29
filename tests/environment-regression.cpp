#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <GLES2/gl2ext.h>
static size_t lightPixels(const std::vector<unsigned char> &image){size_t n=0;for(size_t i=0;i<image.size();i+=4)n+=image[i]+image[i+1]+image[i+2]>30&&image[i+3]>0;return n;}
int main(int argc,char **argv){
 const std::string path="assets/environments/studio.psenv";
 auto env=loadLightingEnvironment(path);assert(env&&env->size==128&&env->levels==8&&env->lutSize==128&&env->gpuBytes()==0);
 assert(loadLightingEnvironment(path)==env);assert(!loadLightingEnvironment("/missing.psenv"));
 const auto dir=std::filesystem::temp_directory_path()/("sf-environment-"+std::to_string(getpid()));std::filesystem::create_directory(dir);
 std::ifstream input(path,std::ios::binary);std::string data((std::istreambuf_iterator<char>(input)),{});
 assert(data.substr(0,8)=="PSENV001");
 {
  std::string legacy=data;legacy.replace(0,8,"SFENV001");const auto oldFile=dir/"legacy.sfenv";
  std::ofstream(oldFile,std::ios::binary).write(legacy.data(),legacy.size());
  auto old=loadLightingEnvironment(oldFile.string());assert(old&&old->diffuse==env->diffuse&&old->specular==env->specular&&old->brdf==env->brdf);
  auto alias=loadLightingEnvironment("assets/environments/studio.sfenv");assert(alias&&alias->diffuse==env->diffuse&&alias->specular==env->specular&&alias->brdf==env->brdf);
 }
 for(int variant=0;variant<5;variant++){
  std::string bad=data;if(variant==0)bad[0]='?';if(variant==1)bad.resize(127);if(variant==2)bad[8]=3;if(variant==3){bad[128]=0;bad[129]=char(0x7c);}if(variant==4)bad.push_back('x');
  auto file=dir/("bad"+std::to_string(variant)+".psenv");std::ofstream(file,std::ios::binary).write(bad.data(),bad.size());assert(!loadLightingEnvironment(file.string()));
 }
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));GLuint cube=0,lut=0;
 {
  GLuint textures[2];glGenTextures(2,textures);glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_CUBE_MAP,textures[0]);glBindTexture(GL_TEXTURE_2D,textures[1]);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
  assert(env->upload());GLint value=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&value);assert(value==GL_TEXTURE4);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&value);assert(GLuint(value)==textures[0]);glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);assert(GLuint(value)==textures[1]);glGetIntegerv(GL_UNPACK_ALIGNMENT,&value);assert(value==8);
  glBindTexture(GL_TEXTURE_CUBE_MAP,0);glBindTexture(GL_TEXTURE_2D,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);glActiveTexture(GL_TEXTURE0);glDeleteTextures(2,textures);
 }

 {
  Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &a=r.scene.assets.back();a.pbr=true;
  assert(loadModelFile("assets/samples/Box/Box.glb",a));uploadModel(a);a.lighting.intensity=0;a.lighting.ambient=0;
  auto &p=a.primitives[0];p.metallic=1;p.roughness=.045f;p.factor[0]=p.factor[1]=p.factor[2]=.8f;
  Object o{};o.id=1;o.asset=int(r.scene.assets.size());o.scale=.6f;o.yaw=.3f;o.pitch=.2f;r.scene.objects={o};
  auto dark=r.renderSet({1},{0,.1f,-1.2f},0);assert(lightPixels(dark)==0);
  a.environment=env;a.lighting.environment=path;auto lit=r.renderSet({1},{0,.1f,-1.2f},0);assert(lightPixels(lit)>100&&lit!=dark);cube=env->cube;lut=env->lut;assert(cube&&lut&&env->gpuBytes()>0);
  a.lighting.environmentIntensity=0;assert(r.renderSet({1},{0,.1f,-1.2f},0)==dark);a.lighting.environmentIntensity=1;
  a.lighting.environmentRotation=1;assert(r.renderSet({1},{0,.1f,-1.2f},0)!=lit);a.lighting.environmentRotation=0;
  p.materialUnlit=true;auto unlit=r.renderSet({1},{0,.1f,-1.2f},0);a.environment.reset();assert(r.renderSet({1},{0,.1f,-1.2f},0)==unlit);
  {
   std::vector<unsigned char> reference;std::vector<float> referenceSurface;
   for(const char *name:{"GeneratedTangents","SuppliedTangents"}){
    r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;assert(loadModelFile(std::string("tests/fixtures/pbr/")+name+".glb",asset));uploadModel(asset);asset.environment=env;asset.lighting.intensity=asset.lighting.ambient=0;
    const auto &surface=asset.primitives[0].surface;for(size_t v=0;v<surface.size();v+=9)assert(surface[v+6]==-1);
    o.asset=int(r.scene.assets.size());o.yaw=o.pitch=0;r.scene.objects={o};auto image=r.renderSet({1},{0,.1f,-1.2f},0);
    if(reference.empty()){reference=image;referenceSurface=surface;}else{assert(image==reference&&surface==referenceSurface);}
   }
   o.yaw=.3f;o.pitch=.2f;
  }
  for(const char *name:{"NormalTangentTest","NormalTangentMirrorTest","NegativeScaleTest","TextureEncodingTest"}){
   r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;assert(loadModelFile(std::string("assets/samples/")+name+"/"+name+".glb",asset));uploadModel(asset);asset.environment=env;asset.lighting.environment=path;asset.lighting.intensity=0;asset.lighting.ambient=0;
   o.asset=int(r.scene.assets.size());o.scale=.7f;r.scene.objects={o};
   for(int eye=0;eye<2;eye++)for(float angle:{0.f,1.f}){asset.lighting.environmentRotation=angle;auto image=r.renderSet({1},{0,.1f,-1.2f},eye);assert(lightPixels(image)>100);
    if(!eye&&angle==0)if(const char *directory=std::getenv("SF_ENV_IMAGES")){
     std::filesystem::create_directories(directory);std::ofstream out(std::filesystem::path(directory)/(std::string(name)+".ppm"),std::ios::binary);out<<"P6\n"<<pixels<<" "<<pixels<<"\n255\n";
     for(int y=pixels-1;y>=0;y--)for(int x=0;x<pixels;x++)out.write(reinterpret_cast<const char*>(image.data()+size_t(y*pixels+x)*4),3);
    }
   }
  }
  // PBR draws must release their extra texture units and restore active unit zero.
  GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);assert(active==GL_TEXTURE0);glActiveTexture(GL_TEXTURE6);GLint bound=1;glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&bound);assert(bound==0);glActiveTexture(GL_TEXTURE7);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);assert(bound==0);glActiveTexture(GL_TEXTURE0);
 }
 env.reset();assert(!glIsTexture(cube)&&!glIsTexture(lut));
 {
  const auto script=dir/"test.js";std::ofstream(script)<<R"JS(
   function check(v){if(!v)throw Error('environment assertion');}function fails(fn){let caught=false;try{fn();}catch(e){caught=true;}check(caught);}
   const model=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
   setModelLighting(model,{environment:'assets/environments/studio.psenv',environmentIntensity:.8,environmentRotation:.4});
   const owner=createSceneContainer();createObject(model,0,0,0,.2,{container:owner});const snap=captureSceneContainer(owner);check(snap.version===7&&snap.models[0].lighting.environmentIntensity>.79);
   fails(()=>setModelLighting(model,{environment:'/missing.psenv',intensity:9}));fails(()=>setModelLighting(model,{environment:'x\0y'}));fails(()=>setModelLighting(model,{environment:1}));fails(()=>setModelLighting(model,{environmentIntensity:-1}));fails(()=>setModelLighting(model,{environmentRotation:Infinity}));
   check(JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snap));
   const copy=restoreSceneContainer(snap);disposeSceneContainer(copy.containerId);
   const bad=JSON.parse(JSON.stringify(snap));bad.models[0].lighting.environment='/missing.psenv';fails(()=>restoreSceneContainer(bad));check(JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snap));
   bad.models[0].path='assets/samples/Lantern/Lantern.glb';fails(()=>restoreSceneContainer(bad));check(JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snap));
   setModelLighting(model,{environment:null,environmentIntensity:1,environmentRotation:0});check(captureSceneContainer(owner).models[0].lighting.environment==='');
   const old=JSON.parse(JSON.stringify(captureSceneContainer(owner)));old.version=6;delete old.models[0].lighting.environment;delete old.models[0].lighting.environmentIntensity;delete old.models[0].lighting.environmentRotation;const c=restoreSceneContainer(old);check(captureSceneContainer(c.containerId).models[0].lighting.environment==='');disposeSceneContainer(c.containerId);
   function update(){}
  )JS";
  ScriptHost host;assert(loadScript(host,script.string()));assert(host.scene.assets.size()==1);closeScript(host);
 }
 if(argc>1&&std::string(argv[1])=="--benchmark"){
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  for(int count:{1,16}){
   Renderer r;r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();auto &asset=r.scene.assets.back();asset.pbr=true;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);asset.environment=loadLightingEnvironment(path);asset.lighting.environment=path;assert(asset.environment&&asset.environment->upload());std::vector<int> members;
   for(int i=0;i<count;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=count==1?.7f:.25f;o.x=count==1?0:(i%4-1.5f)*.25f;o.y=count==1?0:(i/4-1.5f)*.25f;o.pitch=.2f;o.yaw=.3f;r.scene.objects.push_back(o);members.push_back(o.id);}
   for(int stage=0;stage<4;stage++){
    asset.lighting.environmentIntensity=stage==1||stage==2?1:0;double cpu=0,wall=0,gpu=0;GLuint query=0;gen(1,&query);
    for(int frame=0;frame<150;frame++){
     const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);r.renderSet(members,{0,.1f,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);const auto enqueued=std::chrono::steady_clock::now();glFinish();const auto finish=std::chrono::steady_clock::now();GLuint64 ns=0;result(query,GL_QUERY_RESULT_EXT,&ns);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
     if(frame>=30){cpu+=std::chrono::duration<double,std::milli>(enqueued-start).count();wall+=std::chrono::duration<double,std::milli>(finish-start).count();gpu+=double(ns)/1e6;}
    }
    destroy(1,&query);std::printf("ENVIRONMENT_SAMPLE {\"objects\":%d,\"stage\":%d,\"environment\":%s,\"frames\":120,\"eyePixels\":768,\"gpuBytes\":%llu,\"gpuMs\":%.6f,\"enqueueMs\":%.6f,\"frameMs\":%.6f}\n",count,stage,asset.lighting.environmentIntensity>0?"true":"false",(unsigned long long)asset.environment->gpuBytes(),gpu/120,cpu/120,wall/120);std::fflush(stdout);
   }
  }
 }
 assert(glGetError()==GL_NO_ERROR);std::filesystem::remove_all(dir);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 std::printf("PASS environment: dark metal illumination, intensity/rotation, unlit invariance, Khronos stereo views, shared textures, malformed files, API rollback, v6/v7 snapshots and resource cleanup\n");
}
