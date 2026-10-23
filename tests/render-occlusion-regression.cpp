// Actual GLES shaders and render passes, without opening an OpenVR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>

constexpr int pixels=192;
struct Renderer {
  GLuint program=0,fbo=0,color=0,depth=0;
  Scene scene;
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
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,pixels,pixels,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,pixels,pixels);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    for(const char *path:{"assets/physics/ThinPlatform.glb","assets/physics/ThinWallBlue.glb","assets/samples/Box/Box.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));uploadModel(asset);scene.assets.push_back(std::move(asset));
    }
    scene.objects.push_back({1,1,0,-.43f,0,1.2f,0});
    scene.objects.push_back({2,2,-.6f,-.1f,0,.6f,0,0,float(std::acos(-1)/2)});
    scene.objects.push_back({3,3,0,-.32f,0,.16f,0});
  }
  ~Renderer(){for(auto &asset:scene.assets)deleteModel(asset);glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);glDeleteProgram(program);}
  void vector(const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
  std::vector<unsigned char> render(int id,physics::Vec3 headScene,int eyeIndex,bool occlusion,bool bounds,int &sample){
    auto &target=scene.objects[size_t(id)-1];
    auto panel=anchor,head=anchor;
    panel.m[0][3]=target.x;panel.m[1][3]=target.y;panel.m[2][3]=-target.z;
    head.m[0][3]=headScene.x;head.m[1][3]=headScene.y;head.m[2][3]=-headScene.z;
    float r[3],u[3],f[3];faceViewer(panel,anchor,head,r,u,f);
    float eye[3]={head.m[0][3]+(eyeIndex?.032f:-.032f),head.m[1][3],head.m[2][3]};
    glUseProgram(program);vector("center",panel.m[0][3],panel.m[1][3],panel.m[2][3]);vector("eye",eye[0],eye[1],eye[2]);
    vector("panelRight",r[0],r[1],r[2]);vector("panelUp",u[0],u[1],u[2]);vector("panelForward",f[0],f[1],f[2]);
    const float width=std::max(.55f,target.scale*2.1f);glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),width*.5f);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,pixels,pixels);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
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
          glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
          glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
        }
      }
    };
    renderObjectLayers(id,draw);
    // Pixel along the ray through the red cube's center in this overlay.
    const auto &box=scene.objects[2];float ray[3]={box.x-eye[0],box.y-eye[1],-box.z-eye[2]};
    float distance=0,plane=0;for(int axis=0;axis<3;axis++){distance+=ray[axis]*f[axis];plane+=(panel.m[axis][3]-eye[axis])*f[axis];}
    float x=0,y=0;for(int axis=0;axis<3;axis++){const float delta=eye[axis]+ray[axis]*plane/distance-panel.m[axis][3];x+=delta*r[axis];y+=delta*u[axis];}
    const int px=int((x/width+.5f)*pixels),py=int((y/width+.5f)*pixels);assert(px>=0&&px<pixels&&py>=0&&py<pixels);sample=(py*pixels+px)*4;
    std::vector<unsigned char> result(pixels*pixels*4);glReadPixels(0,0,pixels,pixels,GL_RGBA,GL_UNSIGNED_BYTE,result.data());assert(glGetError()==GL_NO_ERROR);return result;
  }
};
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer renderer;int sample=0,ignored=0;int cases=0;
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{0,.1f,-1.2f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      auto old=renderer.render(1,head,eye,false,false,sample);assert(old[sample+3]>250);
      auto fixed=renderer.render(1,head,eye,true,true,sample);assert(fixed[sample+3]==0);
      auto reference=renderer.render(1,head,eye,true,false,ignored);assert(fixed==reference);
      auto box=renderer.render(3,head,eye,true,true,sample);assert(box[sample+3]>250);cases++;
    }
    renderer.scene.objects[2].y=-.5f;
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      auto floor=renderer.render(1,head,eye,true,true,sample);assert(floor[sample+3]>250);
      auto below=renderer.render(3,head,eye,true,true,sample);assert(below[sample+3]==0);
      assert(below==renderer.render(3,head,eye,true,false,ignored));cases++;
    }
    renderer.scene.objects[2].y=0;
    for(auto head:{physics::Vec3{1,0,-.4f},physics::Vec3{1.5f,0,-.4f}})for(int eye=0;eye<2;eye++){
      auto old=renderer.render(2,head,eye,false,false,sample);assert(old[sample+3]>250);
      auto fixed=renderer.render(2,head,eye,true,true,sample);assert(fixed[sample+3]==0);
      assert(fixed==renderer.render(2,head,eye,true,false,ignored));cases++;
    }
    renderer.scene.objects[2].y=-.32f;auto &part=renderer.scene.assets[2].primitives[0];
    part.alphaMode=1;part.factor[3]=0;
    auto hole=renderer.render(1,{0,.1f,-1.2f},0,true,true,sample);assert(hole[sample+3]>250);
    part.alphaMode=2;part.factor[3]=.5f;
    auto blend=renderer.render(1,{0,.1f,-1.2f},0,true,true,sample);assert(blend[sample+3]>250);
    part.alphaMode=0;part.factor[3]=1;renderer.scene.objects[2].visible=false;
    auto hidden=renderer.render(1,{0,.1f,-1.2f},0,true,true,sample);assert(hidden[sample+3]>250);
    printf("PASS GLES occlusion: %d stereo floor/wall lean-back views, foreground preserved, bounded/unbounded equality, alpha-mask holes, blended and hidden models\n",cases);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
