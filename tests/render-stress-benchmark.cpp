// Fixed-view GPU workload: same meshes, resolution and passes as the engine.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdlib>
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)

int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  constexpr int pixels=768,count=35;
  {
    Scene scene;vr::HmdMatrix34_t anchor{},head{};for(int i=0;i<3;i++)anchor.m[i][i]=head.m[i][i]=1;
    head.m[1][3]=.15f;head.m[2][3]=1;
    for(const char *path:{"assets/physics/ThinPlatform.glb","assets/samples/Box/Box.glb"}){ModelAsset a;assert(loadModelFile(path,a));uploadModel(a);scene.assets.push_back(std::move(a));}
    scene.objects.push_back({1,1,0,-.55f,0,1.2f,0});
    for(int i=0;i<count-1;i++)scene.objects.push_back({i+2,2,((i%6)-2.5f)*.13f,-.25f+(i/6)*.13f,0,.1f,0});
    GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
    glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);glDeleteShader(v);glDeleteShader(f);glUseProgram(program);
    auto location=[&](const char *name){return glGetUniformLocation(program,name);};
    const auto centerLoc=location("center"),eyeLoc=location("eye"),rLoc=location("panelRight"),uLoc=location("panelUp"),fLoc=location("panelForward"),halfLoc=location("panelHalfWidth");
    const auto yawLoc=location("yaw"),pitchLoc=location("pitch"),rollLoc=location("roll"),scaleLoc=location("modelScale"),offsetLoc=location("offset"),factorLoc=location("factor"),modeLoc=location("alphaMode"),cutoffLoc=location("alphaCutoff"),sheenLoc=location("sheen");
    glUniform3f(location("right"),1,0,0);glUniform3f(location("up"),0,1,0);glUniform3f(location("forward"),0,0,-1);glUniform1i(location("material"),0);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    struct Target{GLuint fbo,color,depth;int size;};std::vector<Target> targets;
    for(int mode=0;mode<2;mode++)for(int i=0;i<2*count;i++){
      const auto &target=scene.objects[i/2];auto panel=anchor;panel.m[0][3]=target.x;panel.m[1][3]=target.y;panel.m[2][3]=-target.z;
      float r[3],u[3],f[3];faceViewer(panel,anchor,head,r,u,f);
      const float eyes[2][3]={{-.032f,.15f,1},{.032f,.15f,1}},center[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]};
      ProjectedBounds bounds[2];for(int eye=0;eye<2;eye++)bounds[eye]=projectedBounds(target,scene.assets[target.asset-1],anchor,eyes[eye],r,u,f);
      Target t;t.size=mode?objectCropPixels(bounds,center,eyes,r,u,f,std::max(.55f,target.scale*2.1f),pixels):pixels;glGenFramebuffers(1,&t.fbo);glBindFramebuffer(GL_FRAMEBUFFER,t.fbo);
      glGenTextures(1,&t.color);glBindTexture(GL_TEXTURE_2D,t.color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t.size,t.size,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.color,0);
      glGenRenderbuffers(1,&t.depth);glBindRenderbuffer(GL_RENDERBUFFER,t.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,t.size,t.size);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,t.depth);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);targets.push_back(t);
    }
    auto frame=[&](bool scissor,bool crop){
      for(const auto &target:scene.objects){
        auto panel=anchor;panel.m[0][3]=target.x;panel.m[1][3]=target.y;panel.m[2][3]=-target.z;
        float right[3],up[3],forward[3];faceViewer(panel,anchor,head,right,up,forward);
        const auto &buffer=targets[(crop?2*count:0)+2*(target.id-1)];
        const int size=buffer.size;
        const float center[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]},width=std::max(.55f,target.scale*2.1f)*size/pixels;
        glUniform3fv(centerLoc,1,center);glUniform3fv(rLoc,1,right);glUniform3fv(uLoc,1,up);glUniform3fv(fLoc,1,forward);glUniform1f(halfLoc,width*.5f);
        for(int eyeIndex=0;eyeIndex<2;eyeIndex++){
          const float eye[3]={eyeIndex?.032f:-.032f,.15f,1};glUniform3fv(eyeLoc,1,eye);
          glBindFramebuffer(GL_FRAMEBUFFER,targets[(crop?2*count:0)+2*(target.id-1)+eyeIndex].fbo);glViewport(0,0,size,size);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
          const auto bounds=projectedBounds(target,scene.assets[target.asset-1],anchor,eye,right,up,forward);
          auto region=projectedScissor(bounds,center,eye,right,up,forward,width*.5f,size);
          renderObjectLayers(target.id,[&](int id,int pass){
            for(const auto &object:scene.objects){
              if(pass<0?object.id==id:object.id!=id)continue;
              const auto &asset=scene.assets[object.asset-1];if(pass<0&&!mayOcclude(projectedBounds(object,asset,anchor,eye,right,up,forward),bounds))continue;
              glUniform1f(yawLoc,object.yaw);glUniform1f(pitchLoc,object.pitch);glUniform1f(rollLoc,object.roll);glUniform1f(scaleLoc,object.scale);glUniform3f(offsetLoc,object.x-target.x,object.y-target.y,object.z-target.z);
              for(const auto &part:asset.primitives){
                if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
                glUniform4fv(factorLoc,1,part.factor);glUniform1i(modeLoc,part.alphaMode);glUniform1f(cutoffLoc,part.alphaCutoff);glUniform3fv(sheenLoc,1,part.sheen);
                glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));glBindTexture(GL_TEXTURE_2D,part.texture);glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
              }
            }
          },scissor?&region:nullptr);
        }
      }
      glFinish();assert(glGetError()==GL_NO_ERROR);
    };
    for(int round=0;round<3;round++)for(int mode=0;mode<3;mode++){
      const bool scissor=mode>0,crop=mode==2;
      for(int i=0;i<20;i++)frame(scissor,crop);
      const auto start=std::chrono::steady_clock::now();for(int i=0;i<200;i++)frame(scissor,crop);
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
      printf("BENCH round=%d scissor=%d crop=%d objects=%d eyes=2 pixels=%d frame_ms=%.3f\n",round,int(scissor),int(crop),count,pixels,ms);fflush(stdout);
    }
    for(auto t:targets){glDeleteRenderbuffers(1,&t.depth);glDeleteTextures(1,&t.color);glDeleteFramebuffers(1,&t.fbo);}for(auto &a:scene.assets)deleteModel(a);glDeleteProgram(program);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
