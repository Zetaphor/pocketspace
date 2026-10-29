#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <random>
static bool near(float a,float b){return std::abs(a-b)<3e-5;}
// Deterministic CPU animation + real meshHit workload; deliberately excludes rendering.
static void profilePicking(){
 Scene scene;currentScene=liveScene=&scene;scene.assets.emplace_back();
 auto &asset=scene.assets[0];assert(loadModelFile("assets/samples/CesiumMan/CesiumMan.glb",asset));
 uint64_t vertices=0;for(const auto &part:asset.primitives)vertices+=part.vertexCount;
 ControllerSnapshot hands[2]{};const float angle=.6981317008f,c=std::cos(angle),s=std::sin(angle);
 for(int h=0;h<2;h++){
  auto &hand=hands[h];hand.valid=true;hand.rotation[0]=1;hand.rotation[4]=c;hand.rotation[5]=-s;hand.rotation[7]=s;hand.rotation[8]=c;
  hand.x=h==0?-.012694f:.012694f;hand.y=c*.02522f+s*.020687f;hand.z=1+s*.02522f-c*.020687f;
 }
 // Reverse mode order for the single-candidate case to vary ordering.
 for(int candidates:{0,1,4,32})for(int order=0;order<2;order++){
  gpuSkinningEnabled=(order+(candidates%2))%2;
  scene.objects.clear();
  for(int i=0;i<32;i++){Object object{};object.id=i+1;object.asset=1;object.scale=.5;object.animationPose=std::make_shared<ModelPose>();scene.objects.push_back(std::move(object));}
  for(bool playing:{true,false}){
   AnimationProfileFrame total;std::vector<double> elapsed;uint64_t hits=0;
   for(int frame=0;frame<140;frame++){
    AnimationProfileFrame profile;animationProfileFrame=&profile;const auto start=AnimationProfileClock::now();
    for(size_t i=0;i<scene.objects.size();i++){
     auto &object=scene.objects[i];auto &pose=*object.animationPose;
     if(playing||frame==0){animation::Playback state;state.clip=0;state.time=std::fmod(frame/90.+double(i)/32.,asset.animations[0].duration);evaluateModelPose(asset,state,pose);}
     object.x=i<size_t(candidates)?-(pose.geometry.low[0]+pose.geometry.high[0])*.25f:100;
     object.y=-(pose.geometry.low[1]+pose.geometry.high[1])*.25f;
    }
    for(int h=0;h<2;h++)for(auto &object:scene.objects)hits+=meshHit(object,object.animationPose->geometry,hands[h],h)!=std::numeric_limits<float>::max();
    const double ms=animationProfileMs(start);animationProfileFrame=nullptr;
    const uint64_t expected=(playing||frame==0)?vertices*(gpuSkinningEnabled?candidates:32):0;
    assert(profile.vertices==expected); // Both hands share exactly one candidate deformation.
    if(frame>=20){elapsed.push_back(ms);total.add(profile);}
   }
   std::sort(elapsed.begin(),elapsed.end());double mean=0;for(double ms:elapsed)mean+=ms;mean/=elapsed.size();
   printf("SKIN_PICK_PROFILE {\"gpu\":%s,\"playing\":%s,\"objects\":32,\"candidates\":%d,\"hands\":2,\"frames\":120,\"cpuWorkMs\":%.6f,\"p95Ms\":%.6f,\"deformBoundsMs\":%.6f,\"bvhMs\":%.6f,\"vertices\":%.0f,\"hits\":%llu}\n",gpuSkinningEnabled?"true":"false",playing?"true":"false",candidates,mean,elapsed[113],total.deformBoundsMs/120,total.bvhMs/120,double(total.vertices)/120,(unsigned long long)hits);fflush(stdout);
  }
 }
 scene.objects.clear();currentScene=liveScene=nullptr;
}
int main(int argc,char **argv){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 std::vector<std::string> paths={"assets/samples/CesiumMan/CesiumMan.glb"};
 if(argc>1)paths.push_back(std::string(argv[1])+"/gpu-multipart.glb");
 for(const auto &path:paths){
  Scene scene;currentScene=liveScene=&scene;scene.assets.emplace_back();auto &asset=scene.assets[0];
  assert(loadModelFile(path,asset)&&asset.skinEligible);
  if(paths.size()>1&&path==paths[1])for(auto &part:asset.primitives)if(part.alphaMode==1){
   part.width=part.height=2;part.rgba={255,255,255,255,255,255,255,0,255,255,255,0,255,255,255,255};
  }
  uploadModel(asset);
  uint64_t vertexCount=0,paletteBytes=0;
  for(const auto &part:asset.primitives)vertexCount+=part.vertexCount;
  for(const auto &skin:asset.skins)paletteBytes+=skin.joints.size()*64;
  GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
  glAttachShader(program,v);glAttachShader(program,f);const char *varying="gpuSkinPosition";glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);glDeleteShader(v);glDeleteShader(f);
  glUseProgram(program);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  auto vec=[&](const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);};
  vec("right",1,0,0);vec("up",0,1,0);vec("forward",0,0,-1);vec("panelRight",1,0,0);vec("panelUp",0,1,0);vec("panelForward",0,0,-1);vec("center",0,0,0);vec("offset",0,0,0);
  glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),.65);glUniform1f(glGetUniformLocation(program,"modelScale"),.7);glUniform1f(glGetUniformLocation(program,"yaw"),.23);glUniform1f(glGetUniformLocation(program,"roll"),-.1);glUniform1i(glGetUniformLocation(program,"material"),0);
  GLuint fbo,color,depth;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,256,256);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  auto image=[&](ModelPose &pose,float eye,int selected=-1){
   vec("eye",eye,0,1);glViewport(0,0,256,256);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
   for(int pass=0;pass<2;pass++){
   if(pass){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}else{glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
   for(size_t p=0;p<asset.primitives.size();p++){
    const auto &part=asset.primitives[p];if((selected>=0&&int(p)!=selected)||(part.alphaMode==2)!=bool(pass))continue;glUniform4fv(glGetUniformLocation(program,"factor"),1,part.factor);glUniform3fv(glGetUniformLocation(program,"sheen"),1,part.sheen);glUniform1i(glGetUniformLocation(program,"alphaMode"),part.alphaMode);glUniform1f(glGetUniformLocation(program,"alphaCutoff"),part.alphaCutoff);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
    if(bindSkinPrimitive(asset,part,pose,program)){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glDrawElements(GL_TRIANGLES,GLsizei(part.skinIndices.size()),GL_UNSIGNED_INT,nullptr);unbindSkinPrimitive(program);}
    else drawModelPrimitive(part,pose.geometry.primitives[p].vbo);
   }
   }
   glDepthMask(GL_TRUE);glDisable(GL_BLEND);
   std::vector<unsigned char> pixels(256*256*4);glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());assert(glGetError()==GL_NO_ERROR);return pixels;
  };
  Object object{};object.id=1;object.asset=1;object.scale=.5;object.animationPose=std::make_shared<ModelPose>();
  std::mt19937 random(22);std::uniform_real_distribution<float> position(-.8,.8);int hits=0;
  for(double time:{0.,.2,.7,1.2,1.8}){
   animation::Playback state;state.clip=0;state.time=time;
   if(time==.7){state.transition.duration=1;state.transition.elapsed=.5;state.transition.source.clip=0;state.transition.source.time=.2;}
   if(time==1.2){state.transition.duration=1;state.transition.elapsed=.25;state.transition.frozen=object.animationPose->nodes;}
   ModelPose cpu;gpuSkinningEnabled=false;evaluateModelPose(asset,state,cpu);uploadModelPose(cpu);
   AnimationProfileFrame profile;animationProfileFrame=&profile;gpuSkinningEnabled=true;auto &gpu=*object.animationPose;evaluateModelPose(asset,state,gpu);uploadModelPose(gpu);
   assert(gpu.gpuSkin&&!gpu.pickingValid&&profile.vertices==0&&profile.uploadBytes==paletteBytes);
   assert(cpu.geometry.low[0]==gpu.geometry.low[0]&&cpu.geometry.high[2]==gpu.geometry.high[2]);
   for(size_t p=0;p<asset.primitives.size();p++){
    const auto &part=asset.primitives[p];const size_t unique=part.skinVertices.size()/13;
    GLuint feedback;glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,unique*3*sizeof(float),nullptr,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    assert(bindSkinPrimitive(asset,part,gpu,program));glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,GLsizei(unique));glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);unbindSkinPrimitive(program);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);const auto *result=static_cast<const float*>(glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,unique*3*sizeof(float),GL_MAP_READ_BIT));assert(result);
    for(size_t i=0;i<part.skinIndices.size();i++)for(int a=0;a<3;a++){
     const float actual=result[part.skinIndices[i]*3+a],expected=cpu.geometry.primitives[p].vertices[i*5+a];assert(near(actual,expected));
     assert(actual>=gpu.geometry.low[a]&&actual<=gpu.geometry.high[a]);
    }
    glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);glDeleteBuffers(1,&feedback);
   }
   if(paths.size()>1&&path==paths[1])for(size_t p=0;p<asset.primitives.size();p++){
    auto expected=image(cpu,-.032f,int(p)),actual=image(gpu,-.032f,int(p));size_t visible=0;
    for(size_t i=0;i<actual.size();i++){assert(std::abs(int(actual[i])-int(expected[i]))<=2);if(i%4==3)visible+=expected[i]>0;}
    assert(visible>10);
   }
   for(float eye:{-.032f,.032f}){
    auto expected=image(cpu,eye),actual=image(gpu,eye);size_t different=0,visible=0;
    for(size_t i=0;i<actual.size();i+=4){bool differs=false;visible+=expected[i+3]>0;for(int c=0;c<4;c++)differs|=std::abs(int(actual[i+c])-int(expected[i+c]))>2;different+=differs;}
    printf("SKIN_PIXELS time=%.2f eye=%.3f differing_pixels=%zu visible=%zu\n",time,eye,different,visible);fflush(stdout);assert(visible>100&&different<=32);
   }
   ControllerSnapshot miss{};miss.valid=true;miss.x=100;for(int a=0;a<3;a++)miss.rotation[a*3+a]=1;
   assert(meshHit(object,gpu.geometry,miss,0)==std::numeric_limits<float>::max()&&!gpu.pickingValid);
   for(int r=0;r<600;r++){
    ControllerSnapshot hand{};hand.valid=true;hand.x=position(random);hand.y=position(random);hand.z=position(random);for(int a=0;a<3;a++)hand.rotation[a*3+a]=1;
    const float fast=meshHit(object,gpu.geometry,hand,r%2),expected=meshHit(object,cpu.geometry,hand,r%2,false);
    assert((fast==expected&&fast==std::numeric_limits<float>::max())||near(fast,expected));hits+=fast!=std::numeric_limits<float>::max();
   }
   assert(gpu.pickingValid&&profile.vertices==vertexCount); // one exact cache for both hands
   const auto uploads=profile.uploads;uploadModelPose(gpu);assert(profile.uploads==uploads);animationProfileFrame=nullptr;
  }
  assert(hits>0);
  if(argc>1){ModelAsset multi;assert(loadModelFile(std::string(argv[1])+"/two-sets.glb",multi)&&!multi.skinEligible);animation::Playback state;state.clip=0;ModelPose pose;evaluateModelPose(multi,state,pose);assert(!pose.gpuSkin);}
  ModelAsset morph;assert(loadModelFile("assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb",morph)&&!morph.skinEligible);
  object.animationPose.reset();
  for(int cycle=0;cycle<24;cycle++){
   auto pose=std::make_unique<ModelPose>();animation::Playback state;state.clip=0;state.time=.2;
   evaluateModelPose(asset,state,*pose);uploadModelPose(*pose);prepareSkinPicking(asset,*pose);
   const auto textures=pose->skinTextures;pose.reset();for(auto texture:textures)assert(!glIsTexture(texture));
   assert(glGetError()==GL_NO_ERROR);
  }
  std::vector<GLuint> buffers;for(const auto &part:asset.primitives){buffers.push_back(part.skinVbo);buffers.push_back(part.skinIbo);}
  deleteModel(asset);for(auto buffer:buffers)assert(!glIsBuffer(buffer));glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);glDeleteProgram(program);currentScene=liveScene=nullptr;
 }
 {
  ScriptHost host;gpuSkinningEnabled=true;assert(loadScript(host,"tests/animation-blend-api-smoke.js"));
  auto run=[&](const char *source){JSValue result=JS_Eval(host.ctx,source,strlen(source),"gpu-skin-save",JS_EVAL_TYPE_GLOBAL);assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);};
  for(int round=0;round<2;round++){
   run("snapshotBlend()");const auto &a=*host.scene.objects[1].animationPose,&b=*host.scene.objects[4].animationPose;
   assert(a.gpuSkin&&b.gpuSkin&&a.world==b.world&&a.palettes==b.palettes&&a.geometry.low[0]==b.geometry.low[0]);
   run("disposeSceneContainer(blendCopy.containerId);interruptBlends()");
  }
  closeScript(host);currentScene=liveScene=nullptr;
 }
 if(argc>2&&std::string(argv[2])=="--profile-picking")profilePicking();
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 printf("PASS GPU skinning: %zu assets, %zu exact picking comparisons, transform-feedback vertices, conservative bounds, stereo/material pixels, pose cache, palette uploads, resource cleanup and CPU fallback\n",paths.size(),paths.size()*3000);
}
