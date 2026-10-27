// Actual GLES shaders and render passes, without opening an OpenVR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdlib>
#include "../src/mesh_index.h"
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)

constexpr int pixels=768;
struct Renderer {
  GLuint program=0,fbo=0,color=0,depth=0;
  Scene scene;
  std::map<const PrimitiveAsset*,GLuint> expandedBuffers;
  vr::HmdMatrix34_t anchor{};
  Renderer(){
    for(int i=0;i<3;i++)anchor.m[i][i]=1;
    auto v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
    program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);glDeleteShader(v);glDeleteShader(f);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"material"),0);
    vector("right",1,0,0);vector("up",0,1,0);vector("forward",0,0,-1);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2*pixels,pixels,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,2*pixels,pixels);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    for(const char *path:{"assets/physics/ThinPlatform.glb","assets/physics/ThinWallBlue.glb","assets/samples/Box/Box.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));uploadModel(asset);scene.assets.push_back(std::move(asset));
    }
    scene.objects.push_back({1,1,0,-.43f,0,1.2f,0});
    scene.objects.push_back({2,2,-.6f,-.1f,0,.6f,0,0,float(std::acos(-1)/2)});
    scene.objects.push_back({3,3,0,-.32f,0,.16f,0});
  }
  ~Renderer(){for(auto [part,buffer]:expandedBuffers)glDeleteBuffers(1,&buffer);for(auto &asset:scene.assets)deleteModel(asset);glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);glDeleteProgram(program);}
  void vector(const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
  std::vector<unsigned char> render(int id,physics::Vec3 headScene,int eyeIndex,bool occlusion,bool bounds,int &sample,bool scissor=false,bool crop=false,bool expanded=false){
    auto &target=scene.objects[size_t(id)-1];
    auto panel=anchor,head=anchor;
    panel.m[0][3]=target.x;panel.m[1][3]=target.y;panel.m[2][3]=-target.z;
    head.m[0][3]=headScene.x;head.m[1][3]=headScene.y;head.m[2][3]=-headScene.z;
    float r[3],u[3],f[3];faceViewer(panel,anchor,head,r,u,f);
    float eye[3]={head.m[0][3]+(eyeIndex?.032f:-.032f),head.m[1][3],head.m[2][3]};
    glUseProgram(program);vector("center",panel.m[0][3],panel.m[1][3],panel.m[2][3]);vector("eye",eye[0],eye[1],eye[2]);
    vector("panelRight",r[0],r[1],r[2]);vector("panelUp",u[0],u[1],u[2]);vector("panelForward",f[0],f[1],f[2]);
    const float referenceWidth=std::max(.55f,target.scale*2.1f);
    const float center[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]};
    float eyes[2][3]={{head.m[0][3]-.032f,head.m[1][3],head.m[2][3]},{head.m[0][3]+.032f,head.m[1][3],head.m[2][3]}};
    ProjectedBounds both[2];for(int i=0;i<2;i++)both[i]=projectedBounds(target,scene.assets[target.asset-1],anchor,eyes[i],r,u,f);
    const int size=crop?objectCropPixels(both,center,eyes,r,u,f,referenceWidth,pixels):pixels;
    const float width=referenceWidth*size/pixels;glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),width*.5f);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(eyeIndex*pixels,0,size,size);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const auto targetBounds=projectedBounds(target,scene.assets[size_t(target.asset)-1],anchor,eye,r,u,f);
    auto draw=[&](int targetId,int pass){
      if(pass<0&&!occlusion)return;
      for(const auto &object:scene.objects){
        if(!object.visible||(pass<0?object.id==targetId:object.id!=targetId))continue;
        const auto &asset=scene.assets[size_t(object.asset)-1];
        if(pass<0&&bounds&&!mayOcclude(projectedBounds(object,asset,anchor,eye,r,u,f),targetBounds))continue;
        glUniform1f(glGetUniformLocation(program,"yaw"),object.yaw);glUniform1f(glGetUniformLocation(program,"pitch"),object.pitch);glUniform1f(glGetUniformLocation(program,"roll"),object.roll);
        glUniform1f(glGetUniformLocation(program,"modelScale"),object.scale);vector("offset",object.x-target.x,object.y-target.y,object.z-target.z);
        for(const auto &part:asset.primitives){
          if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
          glUniform4fv(glGetUniformLocation(program,"factor"),1,part.factor);glUniform1i(glGetUniformLocation(program,"alphaMode"),part.alphaMode);glUniform1f(glGetUniformLocation(program,"alphaCutoff"),part.alphaCutoff);
          glUniform3fv(glGetUniformLocation(program,"sheen"),1,part.sheen);
          GLuint reference=0;
          if(expanded){auto &buffer=expandedBuffers[&part];if(!buffer){glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_STATIC_DRAW);}reference=buffer;}
          glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);drawModelPrimitive(part,reference);
        }
      }
    };
    auto region=projectedScissor(targetBounds,center,eye,r,u,f,width*.5f,size);
    region.x+=eyeIndex*pixels;
    renderObjectLayers(id,draw,scissor?&region:nullptr);
    // Pixel along the ray through the red cube's center in this overlay.
    const auto &box=scene.objects[2];float ray[3]={box.x-eye[0],box.y-eye[1],-box.z-eye[2]};
    float distance=0,plane=0;for(int axis=0;axis<3;axis++){distance+=ray[axis]*f[axis];plane+=(panel.m[axis][3]-eye[axis])*f[axis];}
    float x=0,y=0;for(int axis=0;axis<3;axis++){const float delta=eye[axis]+ray[axis]*plane/distance-panel.m[axis][3];x+=delta*r[axis];y+=delta*u[axis];}
    const int px=int((x/referenceWidth+.5f)*pixels),py=int((y/referenceWidth+.5f)*pixels);assert(px>=0&&px<pixels&&py>=0&&py<pixels);sample=(py*pixels+px)*4;
    std::vector<unsigned char> result(pixels*pixels*4),cropped(size*size*4);
    glReadPixels(eyeIndex*pixels,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,cropped.data());
    const int border=(pixels-size)/2;for(int y=0;y<size;y++)std::copy_n(cropped.data()+y*size*4,size*4,result.data()+((y+border)*pixels+border)*4);
    assert(glGetError()==GL_NO_ERROR);return result;
  }
};
static void indexingValidation(){
  // UV seams and distinct float bit patterns must remain separate.
  const std::vector<float> source={0,0,0,0,0, 1,0,0,1,0, 0,1,0,0,1, 0,0,0,0,0, 0,0,0,1,1, -0.f,0,0,0,0};
  auto indexed=mesh::index(source);assert(indexed.vertices.size()==25&&indexed.indices.size()==6);
  for(size_t i=0;i<indexed.indices.size();i++)for(int j=0;j<5;j++)assert(std::bit_cast<uint32_t>(source[i*5+j])==std::bit_cast<uint32_t>(indexed.vertices[indexed.indices[i]*5+j]));
  for(bool repeated:{false,true}){
    ModelAsset asset;asset.primitives.resize(1);auto &part=asset.primitives[0];
    const int count=repeated?65538:3;
    for(int repeat=0;repeat<(repeated?2:1);repeat++)for(int i=0;i<count;i++)part.vertices.insert(part.vertices.end(),{float(i),0,0,0,0});
    part.vertexCount=GLsizei(part.vertices.size()/5);part.rgba={255,255,255,255};
    uploadModel(asset);
    if(repeated){assert(part.indexBuffer&&part.indexType==GL_UNSIGNED_INT&&part.gpuVertexCount==count);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer);GLint bytes;glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);assert(bytes==part.vertexCount*4);}
    else assert(!part.indexBuffer&&part.gpuVertexCount==part.vertexCount);
    auto buffer=part.indexBuffer;deleteModel(asset);assert(!part.vbo&&!part.indexBuffer&&!part.texture);if(buffer)assert(!glIsBuffer(buffer));
  }
}
static void sharedDepthLifetime(EGLDisplay display){
  std::array<Buffer,3> buffers;GLuint shared=0;
  for(int i=0;i<3;i++){
    auto &b=buffers[i];glGenRenderbuffers(1,&b.color);glBindRenderbuffer(GL_RENDERBUFFER,b.color);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,16,16);
    makeDepthStorage(b,16,16,shared);shared=b.depth;
    glGenFramebuffers(1,&b.fbo);glBindFramebuffer(GL_FRAMEBUFFER,b.fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,b.color);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,b.depth);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);assert(b.ownsDepth==(i==0));
    glClearColor(float(i+1)/4,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  }
  assert(buffers[0].depth==buffers[1].depth&&buffers[1].depth==buffers[2].depth);
  for(int i=0;i<3;i++){
    glBindFramebuffer(GL_FRAMEBUFFER,buffers[i].fbo);unsigned char color[4]{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,color);
    assert(std::abs(int(color[0])-int(std::round(float(i+1)*255/4)))<=1);
  }
  glBindFramebuffer(GL_FRAMEBUFFER,0);destroyBuffer(buffers[0],display);
  for(int i=1;i<3;i++){glBindFramebuffer(GL_FRAMEBUFFER,buffers[i].fbo);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);}
  glBindFramebuffer(GL_FRAMEBUFFER,0);for(auto &buffer:buffers)destroyBuffer(buffer,display);
  assert(glGetError()==GL_NO_ERROR);
}
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    indexingValidation();sharedDepthLifetime(display);
    Renderer renderer;int sample=0,ignored=0;int cases=0;
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{0,.1f,-1.2f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      auto old=renderer.render(1,head,eye,false,false,sample);assert(old[sample+3]>250);
      auto fixed=renderer.render(1,head,eye,true,true,sample);assert(fixed[sample+3]==0);
      auto reference=renderer.render(1,head,eye,true,false,ignored);assert(fixed==reference);
      assert(fixed==renderer.render(1,head,eye,true,true,ignored,true,true));
      assert(fixed==renderer.render(1,head,eye,true,true,ignored,true));
      auto box=renderer.render(3,head,eye,true,true,sample);assert(box[sample+3]>250);assert(box==renderer.render(3,head,eye,true,true,ignored,true));cases++;
    }
    renderer.scene.objects[2].y=-.5f;
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      auto floor=renderer.render(1,head,eye,true,true,sample);assert(floor[sample+3]>250);
      auto below=renderer.render(3,head,eye,true,true,sample);assert(below[sample+3]==0);
      assert(below==renderer.render(3,head,eye,true,false,ignored));assert(below==renderer.render(3,head,eye,true,true,ignored,true));cases++;
    }
    renderer.scene.objects[2].y=0;
    for(auto head:{physics::Vec3{1,0,-.4f},physics::Vec3{1.5f,0,-.4f}})for(int eye=0;eye<2;eye++){
      auto old=renderer.render(2,head,eye,false,false,sample);assert(old[sample+3]>250);
      auto fixed=renderer.render(2,head,eye,true,true,sample);assert(fixed[sample+3]==0);
      assert(fixed==renderer.render(2,head,eye,true,false,ignored));assert(fixed==renderer.render(2,head,eye,true,true,ignored,true));assert(fixed==renderer.render(2,head,eye,true,true,ignored,true,true));cases++;
    }
    renderer.scene.objects[2].y=-.32f;auto &part=renderer.scene.assets[2].primitives[0];
    part.alphaMode=1;part.factor[3]=0;
    auto hole=renderer.render(1,{0,.1f,-1.2f},0,true,true,sample);assert(hole[sample+3]>250);assert(hole==renderer.render(1,{0,.1f,-1.2f},0,true,true,ignored,true));
    part.alphaMode=2;part.factor[3]=.5f;
    auto blend=renderer.render(1,{0,.1f,-1.2f},0,true,true,sample);assert(blend[sample+3]>250);assert(blend==renderer.render(1,{0,.1f,-1.2f},0,true,true,ignored,true));
    part.alphaMode=0;part.factor[3]=1;renderer.scene.objects[2].visible=false;
    auto hidden=renderer.render(1,{0,.1f,-1.2f},0,true,true,sample);assert(hidden[sample+3]>250);assert(hidden==renderer.render(1,{0,.1f,-1.2f},0,true,true,ignored,true));
    renderer.scene.objects[2].visible=true;
    for(auto head:{physics::Vec3{0,-.32f,-.08f},physics::Vec3{.7f,.4f,-.8f}})for(int eye=0;eye<2;eye++){
      auto reference=renderer.render(3,head,eye,true,false,sample);
      assert(reference==renderer.render(3,head,eye,true,true,ignored,true));
    }
    for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","assets/Avocado.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));uploadModel(asset);renderer.scene.assets.push_back(std::move(asset));
      const int id=int(renderer.scene.objects.size())+1;renderer.scene.objects.push_back({id,int(renderer.scene.assets.size()),0,-.32f,0,.12f,0});
    }
    for(int id:{3,4,5})for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{0,-.32f,-.08f}})for(int eye=0;eye<2;eye++){
      auto full=renderer.render(id,head,eye,true,true,ignored,true,false,true);
      assert(full==renderer.render(id,head,eye,true,true,ignored,true));
      auto cropped=renderer.render(id,head,eye,true,true,ignored,true,true);
      int differences=0,maxError=0;for(size_t i=0;i<full.size();i++){int error=std::abs(int(full[i])-int(cropped[i]));maxError=std::max(maxError,error);if(error>1)++differences;}
      if(differences)fprintf(stderr,"crop id=%d eye=%d differences=%d max_error=%d\n",id,eye,differences,maxError);
      assert(differences==0);
    }
    bool shortIndices=false;
    for(const auto &asset:renderer.scene.assets)for(const auto &part:asset.primitives){
      auto indexed=mesh::index(part.vertices);for(size_t i=0;i<indexed.indices.size();i++)for(int j=0;j<5;j++)assert(std::bit_cast<uint32_t>(part.vertices[i*5+j])==std::bit_cast<uint32_t>(indexed.vertices[indexed.indices[i]*5+j]));
      shortIndices|=part.indexBuffer&&part.indexType==GL_UNSIGNED_SHORT;
    }assert(shortIndices);
    const float center[3]={0,0,0},eye[3]={0,0,1},r[3]={1,0,0},u[3]={0,1,0},f[3]={0,0,-1};
    ProjectedBounds invalid;invalid.valid=false;
    const auto full=projectedScissor(invalid,center,eye,r,u,f,.275f,pixels);assert(full.x==0&&full.y==0&&full.width==pixels&&full.height==pixels);
    ProjectedBounds outside;outside.x0=10;outside.x1=11;outside.y0=0;outside.y1=1;
    assert(projectedScissor(outside,center,eye,r,u,f,.275f,pixels).width==0);
    printf("PASS GLES occlusion: %d stereo floor/wall lean-back views, foreground preserved, bounded/unbounded equality, alpha-mask holes, blended/hidden models, per-eye scissors, near-plane fallback, shared depth lifetime, cropped model pixels, indexed/expanded equality, UV seams, 16/32-bit indices and cleanup\n",cases);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
