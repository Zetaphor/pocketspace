// Import, real GLES rendering, API failures and transactional persistence; no VR session.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include "../src/surface_frame.h"
int main(){
 using namespace surfaceFrame;
 animation::Matrix scale=animation::local(animation::Node{});scale[0]=2;scale[5]=3;scale[10]=-4;
 auto n=normal(scale,{1,1,1});assert(std::abs(n[0]/n[1]-1.5f)<1e-5f&&n[2]<0);
 auto t=tangent(n,direction(scale,{1,-1,0}));assert(std::abs(dot(n,t))<1e-6f&&std::abs(dot(t,t)-1)<1e-6f);
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 {
  ModelAsset a;a.pbr=true;assert(loadModelFile("tests/fixtures/pbr/SurfaceFrames.glb",a));assert(a.hasAnimation&&a.hasMorph);
  ModelPose pose;animation::Playback state;state.clip=0;state.time=.75;evaluateModelPose(a,state,pose);
  const auto &source=a.primitives[0];const auto &surface=pose.geometry.primitives[0].surface;
  float expected[3]={(1/std::sqrt(3.f)-.075f)/2,(1/std::sqrt(3.f)+.15f)/3,-(1/std::sqrt(3.f)+.075f)/4};float length=std::sqrt(expected[0]*expected[0]+expected[1]*expected[1]+expected[2]*expected[2]);
  for(int c=0;c<3;c++)assert(std::abs(surface[c]-expected[c]/length)<1e-5f);assert(surface[6]==-1&&source.surfaceMirrored);
  ModelAsset flat;flat.pbr=true;assert(loadModelFile("tests/fixtures/pbr/MissingNormals.glb",flat));assert(flat.primitives[0].flatNormals);for(size_t v=0;v<flat.primitives[0].surface.size();v+=9)assert(flat.primitives[0].surface[v+2]==1);
  for(const char *path:{"tests/fixtures/pbr/UnsupportedUV.glb","tests/fixtures/pbr/ZeroNormalUVScale.glb"}){ModelAsset invalid;invalid.pbr=true;assert(!loadModelFile(path,invalid));}
 }
 size_t images=0;std::vector<GLuint> ownedBuffers,ownedTextures;
 {
  Renderer renderer;renderer.ordinaryRouting=false;renderer.scene.objects.clear();
  // Independent transfer-function and packed-data channel checks on one fixed mesh.
  {
   renderer.scene.assets.emplace_back();auto &a=renderer.scene.assets.back();a.pbr=true;
   assert(loadModelFile("assets/samples/Box/Box.glb",a));uploadModel(a);
   Object o{};o.id=1;o.asset=int(renderer.scene.assets.size());o.scale=.6f;renderer.scene.objects={o};auto &p=a.primitives[0];
   p.materialUnlit=true;p.factor[0]=.25f;p.factor[1]=.5f;p.factor[2]=.75f;
   auto image=renderer.renderSet({1},{0,0,-1.2f},0);size_t solid=0;
   for(size_t i=0;i<image.size();i+=4)if(image[i+3]==255){assert(std::abs(int(image[i])-137)<=1&&std::abs(int(image[i+1])-188)<=1&&std::abs(int(image[i+2])-225)<=1);++solid;}assert(solid>100);
   p.materialUnlit=false;a.lighting.ambient=0;a.lighting.direction[0]=0;a.lighting.direction[1]=0;a.lighting.direction[2]=1;
   p.metallic=192.f/255;p.roughness=128.f/255;auto factors=renderer.renderSet({1},{0,0,-1.2f},0);
   p.metallic=p.roughness=1;auto &map=p.maps[1];glGenTextures(1,&map.texture);glBindTexture(GL_TEXTURE_2D,map.texture);unsigned char data[]={255,128,192,255};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,data);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
   auto packed=renderer.renderSet({1},{0,0,-1.2f},0);int maxDelta=0;size_t changed=0;for(size_t i=0;i<factors.size();i++){int d=std::abs(int(factors[i])-int(packed[i]));maxDelta=std::max(maxDelta,d);changed+=d!=0;}std::printf("PBR packed-channel comparison: max_delta=%d changed_channels=%zu\n",maxDelta,changed);assert(maxDelta<=1);
   glDeleteTextures(1,&map.texture);map.texture=0;
   p.materialUnlit=true;p.factor[0]=p.factor[1]=p.factor[2]=1;
   glBindTexture(GL_TEXTURE_2D,p.texture);unsigned char rgb[]={137,188,225,255};glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgb);glGenerateMipmap(GL_TEXTURE_2D);
   auto srgb=renderer.renderSet({1},{0,0,-1.2f},0);for(size_t i=0;i<srgb.size();i+=4)if(srgb[i+3]==255)for(int c=0;c<3;c++)assert(std::abs(int(srgb[i+c])-rgb[c])<=1);
   images+=4;
  }
  // A white KHR_materials_unlit PBR mesh must compose exactly like legacy white.
  {
   const int first=int(renderer.scene.assets.size())+1;
   for(bool lit:{false,true}){renderer.scene.assets.emplace_back();auto &a=renderer.scene.assets.back();a.pbr=lit;assert(loadModelFile("assets/samples/Box/Box.glb",a));uploadModel(a);for(auto &p:a.primitives){std::fill_n(p.factor,4,1.f);p.materialUnlit=true;}}
   Object a{},b{};a.id=1;a.asset=first;a.scale=.4f;a.x=-.1f;b=a;b.id=2;b.x=.1f;b.z=-.12f;b.y=.1f;renderer.scene.objects={a,b};
   for(int alpha=0;alpha<3;alpha++){
    for(int asset:{first,first+1})for(auto &p:renderer.scene.assets[asset-1].primitives){p.alphaMode=alpha;p.factor[3]=alpha==2?.6f:1.f;p.doubleSided=alpha==2;}
    for(auto mode:{ModelInstancing::Off,ModelInstancing::Instanced}){
     renderer.instanceExperiment.mode=mode;
     for(auto members:{std::vector<int>{1},std::vector<int>{2},std::vector<int>{1,2}})for(int eye=0;eye<2;eye++){
      renderer.scene.objects[1].asset=first;auto expected=renderer.renderSet(members,{.15f,.1f,-1.2f},eye);
      renderer.scene.objects[1].asset=first+1;auto actual=renderer.renderSet(members,{.15f,.1f,-1.2f},eye);if(actual!=expected){int delta=0;size_t changed=0;for(size_t p=0;p<actual.size();p++){int d=std::abs(int(actual[p])-int(expected[p]));delta=std::max(delta,d);changed+=d!=0;}std::fprintf(stderr,"PBR mixed alpha=%d mode=%d members=%zu eye=%d delta=%d changed=%zu\n",alpha,int(mode),members.size(),eye,delta,changed);assert(delta==0);}++images;
     }
    }
   }
   renderer.instanceExperiment.mode=ModelInstancing::Off;
  }
  for(const char *name:{"Box","ToyCar","Avocado","Fox","RiggedSimple","AnimatedMorphCube","InterpolationTest","NormalTangentTest","NormalTangentMirrorTest","NegativeScaleTest","MetalRoughSpheres","MetalRoughSpheresNoTextures","TextureEncodingTest","UnlitTest","EmissiveStrengthTest"}){
   const std::string path=std::string(name)=="Avocado"?"assets/Avocado.glb":std::string("assets/samples/")+name+"/"+name+".glb";
   assert(std::filesystem::exists(path));
   renderer.scene.assets.emplace_back();auto &a=renderer.scene.assets.back();a.pbr=true;
   assert(loadModelFile(path,a));assert(!a.skinEligible&&!a.rigidBatchEligible);uploadModel(a);
   if(std::string(name)=="ToyCar")assert(std::any_of(a.primitives.begin(),a.primitives.end(),[](const auto &p){return p.lod[0].vbo&&p.lod[0].surfaceVbo&&p.lod[0].budgetBytes>0;}));
   assert(pbrLodBudget.used<=PbrLodBudget::limit);
   for(const auto &p:a.primitives){assert(p.surface.size()==size_t(p.vertexCount)*9&&p.surfaceVbo&&!p.indexBuffer&&!p.lod[0].indexBuffer);for(float v:p.surface)assert(std::isfinite(v));for(size_t v=0;v<p.surface.size();v+=9){V n{p.surface[v],p.surface[v+1],p.surface[v+2]},t{p.surface[v+3],p.surface[v+4],p.surface[v+5]};assert(std::abs(dot(n,n)-1)<1e-4f&&std::abs(dot(t,t)-1)<1e-4f&&std::abs(dot(n,t))<1e-4f);assert(std::abs(p.surface[v+6])==1);}}
   Object object{};object.id=1;object.asset=int(renderer.scene.assets.size());object.scale=.65f;object.pitch=.15f;object.yaw=.25f;renderer.scene.objects={object};
   std::vector<unsigned char> first;
   for(int phase=0;phase<3;phase++){
    auto &o=renderer.scene.objects[0];
    if(a.hasAnimation){o.animationPose=std::make_shared<ModelPose>();animation::Playback state;state.clip=0;state.time=.25*phase;evaluateModelPose(a,state,*o.animationPose);assert(!o.animationPose->gpuSkin);uploadModelPose(*o.animationPose);for(const auto &p:o.animationPose->geometry.primitives)assert(p.surfaceVbo);}
    a.lighting.direction[0]=phase==1?1:-1;a.lighting.direction[1]=.2;a.lighting.direction[2]=phase==2?-1:1;
    for(int eye=0;eye<2;eye++){
     auto image=renderer.renderSet({1},{0,.1f,-1.2f},eye);assert(std::any_of(image.begin(),image.end(),[](auto v){return v!=0;}));
     if(!phase&&!eye)if(const char *directory=std::getenv("SF_PBR_IMAGES")){
      std::filesystem::create_directories(directory);std::ofstream output(std::filesystem::path(directory)/(std::string(name)+".ppm"),std::ios::binary);output<<"P6\n"<<pixels<<" "<<pixels<<"\n255\n";
      for(int y=pixels-1;y>=0;y--)for(int x=0;x<pixels;x++)output.write(reinterpret_cast<const char*>(image.data()+size_t(y*pixels+x)*4),3);
     }
     if(!phase&&!eye)first=image;if(phase==2&&!eye){if(std::string(name)=="UnlitTest")assert(image==first);else if(std::string(name)!="EmissiveStrengthTest")assert(image!=first);}++images;
     GLint current=0;glGetIntegerv(GL_CURRENT_PROGRAM,&current);assert(GLuint(current)==renderer.program);
     for(GLuint attribute:{8u,9u,10u}){GLint enabled=1;glGetVertexAttribiv(attribute,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);assert(!enabled);}
    }
   }
  }
  for(const auto &a:renderer.scene.assets)for(const auto &p:a.primitives){if(p.surfaceVbo)ownedBuffers.push_back(p.surfaceVbo);for(const auto &map:p.maps)if(map.texture)ownedTextures.push_back(map.texture);}
  for(const auto &o:renderer.scene.objects)if(o.animationPose)for(const auto &p:o.animationPose->geometry.primitives)if(p.surfaceVbo)ownedBuffers.push_back(p.surfaceVbo);
 }
 for(GLuint buffer:ownedBuffers)assert(!glIsBuffer(buffer));for(GLuint texture:ownedTextures)assert(!glIsTexture(texture));
 {
  const auto dir=std::filesystem::temp_directory_path()/("sf-pbr-"+std::to_string(getpid()));std::filesystem::create_directory(dir);const auto script=dir/"test.js";
  std::ofstream(script)<<R"JS(
   function check(v){if(!v)throw Error('PBR assertion');}
   function fails(fn){let caught=false;try{fn();}catch(e){caught=true;}check(caught);}
   const a=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});
   check(loadModel('assets/samples/Box/Box.glb')===a);
   fails(()=>loadModel('assets/samples/Box/Box.glb',512,{pbr:false}));
   fails(()=>loadModel('assets/Avocado.glb',512,{pbr:1}));
   fails(()=>loadModel('tests/fixtures/pbr/UnsupportedUV.glb',512,{pbr:true}));
   setModelLighting(a,{get ambient(){loadModel('assets/samples/Lantern/Lantern.glb');return .15;}});
   setModelLighting(a,{direction:[1,2,3],color:[1,.8,.6],intensity:4,ambient:.1,exposure:1.2});
   const owner=createSceneContainer();createObject(a,0,0,0,.2,{container:owner});
   const snap=captureSceneContainer(owner);check(snap.version===7&&snap.models[0].pbr&&snap.models[0].lighting.intensity===4);
   for(const options of [{direction:[0,0,0]},{direction:[1,2]},{color:[-1,0,0]},{intensity:-1},{exposure:0},{ambient:Infinity}])fails(()=>setModelLighting(a,options));
   fails(()=>setModelLighting(a,{get ambient(){throw Error('getter');}}));
   check(JSON.stringify(captureSceneContainer(owner))===JSON.stringify(snap));
   const restored=restoreSceneContainer(snap);check(captureSceneContainer(restored.containerId).models[0].pbr);disposeSceneContainer(restored.containerId);
   const bad=JSON.parse(JSON.stringify(snap));bad.models[0].lighting.intensity=7;fails(()=>restoreSceneContainer(bad));
   const unlit=loadModel('assets/Avocado.glb');fails(()=>setModelLighting(unlit,{}));
   const old=JSON.parse(JSON.stringify(snap));old.models[0].path='assets/Avocado.glb';old.version=5;old.models[0].eyePixels=768;delete old.models[0].pbr;delete old.models[0].lighting;const c=restoreSceneContainer(old);check(!captureSceneContainer(c.containerId).models[0].pbr);disposeSceneContainer(c.containerId);
   function update(){}
  )JS";
  ScriptHost host;assert(loadScript(host,script.string()));auto *active=host.ctx;auto saved=host.scene.assets[0].lighting;
  std::ofstream(script)<<"const a=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});setModelLighting(a,{intensity:9});throw Error('EXPECTED reload rejection');function update(){}";
  assert(!loadScript(host,script.string())&&host.ctx==active&&host.scene.assets[0].lighting==saved);
  closeScript(host);std::filesystem::remove_all(dir);
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 std::printf("PBR regression passed: %zu stereo/material/pose images, API and persistence checks\n",images);
}
