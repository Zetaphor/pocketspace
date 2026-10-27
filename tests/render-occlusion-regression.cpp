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
  std::vector<unsigned char> renderSet(const std::vector<int> &ids,physics::Vec3 headScene,int eyeIndex,int sharedDepth=0,bool readback=true,float gridScale=1,bool cull=false,int tileKey=0){
    auto panel=anchor,head=anchor;panel.m[1][3]=-.2f;
    head.m[0][3]=headScene.x;head.m[1][3]=headScene.y;head.m[2][3]=-headScene.z;
    float r[3],u[3],f[3];faceViewer(panel,anchor,head,r,u,f);
    glUseProgram(program);vector("center",0,-.2f,0);vector("eye",head.m[0][3]+(eyeIndex?.032f:-.032f),head.m[1][3],head.m[2][3]);
    vector("panelRight",r[0],r[1],r[2]);vector("panelUp",u[0],u[1],u[2]);vector("panelForward",f[0],f[1],f[2]);
    glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),1.2f*gridScale);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,pixels,pixels);glClearColor(0,0,0,0);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glClear(sharedDepth==2?GL_COLOR_BUFFER_BIT:GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const int targetKey=tileKey?tileKey:-1;
    if(!tileKey)for(auto &object:scene.objects)object.renderGroup=std::find(ids.begin(),ids.end(),object.id)!=ids.end()?1:0;
    std::vector<ProjectedBounds> projected;ProjectedBounds combined;
    const float eyePosition[3]={head.m[0][3]+(eyeIndex?.032f:-.032f),head.m[1][3],head.m[2][3]},planeCenter[3]={0,-.2f,0};
    if(cull){
      for(const auto &object:scene.objects){
        projected.push_back(projectedBounds(object,scene.assets[object.asset-1],anchor,eyePosition,r,u,f));
        if(object.visible&&objectSurfaceKey(object)==targetKey){const auto &b=projected.back();combined.valid&=b.valid;combined.x0=std::min(combined.x0,b.x0);combined.x1=std::max(combined.x1,b.x1);combined.y0=std::min(combined.y0,b.y0);combined.y1=std::max(combined.y1,b.y1);combined.near=std::min(combined.near,b.near);combined.far=std::max(combined.far,b.far);}
      }
    }
    ScissorRegion region{};if(cull)region=projectedScissor(combined,planeCenter,eyePosition,r,u,f,1.2f*gridScale,pixels);
    auto draw=[&](int key,int pass){
      for(const auto &object:scene.objects){
        if(!object.visible||(pass<0?objectSurfaceKey(object)==key:objectSurfaceKey(object)!=key))continue;
        if(cull&&pass<0&&!mayOcclude(projected[size_t(&object-scene.objects.data())],combined))continue;
        const auto &asset=scene.assets[object.asset-1];
        vector("offset",object.x,object.y+.2f,object.z);
        glUniform1f(glGetUniformLocation(program,"yaw"),object.yaw);glUniform1f(glGetUniformLocation(program,"pitch"),object.pitch);glUniform1f(glGetUniformLocation(program,"roll"),object.roll);glUniform1f(glGetUniformLocation(program,"modelScale"),object.scale);
        for(const auto &part:asset.primitives){
          if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
          glUniform4fv(glGetUniformLocation(program,"factor"),1,part.factor);glUniform1i(glGetUniformLocation(program,"alphaMode"),part.alphaMode);glUniform1f(glGetUniformLocation(program,"alphaCutoff"),part.alphaCutoff);glUniform3fv(glGetUniformLocation(program,"sheen"),1,part.sheen);
          glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);drawModelPrimitive(part);
        }
      }
    };
    if(sharedDepth==1){
      glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);draw(-999,-1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }else if(sharedDepth==2){
      glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);draw(targetKey,0);
      glDepthFunc(GL_LESS);glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);draw(targetKey,1);
      glDisable(GL_BLEND);glDepthMask(GL_TRUE);
    }else renderObjectLayers(targetKey,draw,cull?&region:nullptr);
    if(!tileKey)for(auto &object:scene.objects)object.renderGroup=0;
    if(!readback)return {};
    std::vector<unsigned char> image(pixels*pixels*4);glReadPixels(0,0,pixels,pixels,GL_RGBA,GL_UNSIGNED_BYTE,image.data());assert(glGetError()==GL_NO_ERROR);return image;
  }
  ~Renderer(){for(auto [part,buffer]:expandedBuffers)glDeleteBuffers(1,&buffer);for(auto &asset:scene.assets)deleteModel(asset);glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);glDeleteFramebuffers(1,&fbo);glDeleteProgram(program);}
  void vector(const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
  std::vector<unsigned char> render(int id,physics::Vec3 headScene,int eyeIndex,bool occlusion,bool bounds,int &sample,bool scissor=false,bool crop=false,bool expanded=false,bool cached=false){
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
    FrameBoundsCache frameBounds;if(cached)frameBounds.update(scene,anchor,head,eyes);
    ProjectedBounds both[2];for(int i=0;i<2;i++)both[i]=cached?frameBounds.project(target,eyes[i],r,u,f):projectedBounds(target,scene.assets[target.asset-1],anchor,eyes[i],r,u,f);
    const int size=crop?objectCropPixels(both,center,eyes,r,u,f,referenceWidth,pixels):pixels;
    const float width=referenceWidth*size/pixels;glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),width*.5f);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(eyeIndex*pixels,0,size,size);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const auto targetBounds=cached?frameBounds.project(target,eye,r,u,f):projectedBounds(target,scene.assets[size_t(target.asset)-1],anchor,eye,r,u,f);
    auto draw=[&](int targetId,int pass){
      if(pass<0&&!occlusion)return;
      for(const auto &object:scene.objects){
        if(!object.visible||(pass<0?object.id==targetId:object.id!=targetId))continue;
        const auto &asset=scene.assets[size_t(object.asset)-1];
        if(pass<0&&bounds&&!mayOcclude(cached?frameBounds.project(object,eye,r,u,f):projectedBounds(object,asset,anchor,eye,r,u,f),targetBounds))continue;
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
static void coherentGroupPlanes(){
  Scene scene;ModelAsset asset;for(int axis=0;axis<3;axis++){asset.low[axis]=-.5f;asset.high[axis]=.5f;}scene.assets.push_back(std::move(asset));
  scene.objects.push_back({1,1,-.3f,-.2f,-.35f,.15f,0});scene.objects.push_back({2,1,.3f,.1f,.35f,.15f,0});
  vr::HmdMatrix34_t anchor{};for(int axis=0;axis<3;axis++)anchor.m[axis][axis]=1;
  for(const auto position:{physics::Vec3{0,.2f,1.2f},physics::Vec3{.4f,.3f,1},physics::Vec3{-.4f,.3f,1.5f}}){
    auto head=anchor;head.m[0][3]=position.x;head.m[1][3]=position.y;head.m[2][3]=position.z;
    float eyes[2][3]={{position.x-.032f,position.y,position.z},{position.x+.032f,position.y,position.z}};
    auto a=groupPlane({&scene.objects[0]},scene,anchor,head,eyes),b=groupPlane({&scene.objects[1]},scene,anchor,head,eyes);
    float offset=0;for(int axis=0;axis<3;axis++){assert(a.forward[axis]==b.forward[axis]);offset+=(a.transform.m[axis][3]-b.transform.m[axis][3])*a.forward[axis];}
    assert(std::abs(offset)<1e-5f);
    for(int eye=0;eye<2;eye++){
      // Color and an external depth cutout for the same world point must land
      // at the same physical point on both tiles before late-pose reprojection.
      const float point[3]={-.3f,-.2f,.35f};float hits[2][3];int tile=0;
      for(const auto *plane:{&a,&b}){
        float distance=0,along=0;for(int axis=0;axis<3;axis++){distance+=(plane->transform.m[axis][3]-eyes[eye][axis])*plane->forward[axis];along+=(point[axis]-eyes[eye][axis])*plane->forward[axis];}
        for(int axis=0;axis<3;axis++)hits[tile][axis]=eyes[eye][axis]+distance/along*(point[axis]-eyes[eye][axis]);tile++;
      }
      for(int axis=0;axis<3;axis++)assert(std::abs(hits[0][axis]-hits[1][axis])<1e-5f);
      for(float shift:{-.12f,.12f}){
        const float displayX=eyes[eye][0]+shift;
        const float u0=(hits[0][0]-displayX)/(position.z-hits[0][2]),u1=(hits[1][0]-displayX)/(position.z-hits[1][2]);assert(std::abs(u0-u1)<1e-5f);
      }
    }
    const float origin[3]={a.sceneOrigin[0],a.sceneOrigin[1],-a.sceneOrigin[2]};for(int axis=0;axis<3;axis++)assert(std::abs(origin[axis]-a.transform.m[axis][3])<1e-5f);
  }
  // The previous independent planes produce a visible gap after translation.
  assert(std::abs(.12f/.8f-.12f/1.2f)>.04f);
  puts("PASS common group planes: stereo color/cutout alignment through lateral head motion, different object depths, and tile scene transforms");
}
#ifndef SF_OCCLUSION_HELPERS_ONLY
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    coherentGroupPlanes();indexingValidation();sharedDepthLifetime(display);
    Renderer renderer;int sample=0,ignored=0;int cases=0;
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{0,.1f,-1.2f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      auto old=renderer.render(1,head,eye,false,false,sample);assert(old[sample+3]>250);
      auto fixed=renderer.render(1,head,eye,true,true,sample);assert(fixed[sample+3]==0);
      auto reference=renderer.render(1,head,eye,true,false,ignored);assert(fixed==reference);assert(fixed==renderer.render(1,head,eye,true,true,ignored,true,false,false,true));
      assert(fixed==renderer.render(1,head,eye,true,true,ignored,true,true));
      assert(fixed==renderer.render(1,head,eye,true,true,ignored,true));
      auto box=renderer.render(3,head,eye,true,true,sample);assert(box[sample+3]>250);assert(box==renderer.render(3,head,eye,true,true,ignored,true));cases++;
    }
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{1,0,-.4f}})for(int eye=0;eye<2;eye++){
      // Render on the same projection plane: independent depth-clipped images
      // must composite to exactly the shared opaque/masked group image.
      auto grouped=renderer.renderSet({1,2,3},head,eye);std::vector<unsigned char> composed(grouped.size());
      for(int id:{1,2,3}){auto image=renderer.renderSet({id},head,eye);for(size_t i=0;i<image.size();i+=4)if(image[i+3])std::copy_n(image.data()+i,4,composed.data()+i);}
      assert(grouped==composed);
      auto bodies=renderer.renderSet({2,3},head,eye);auto floor=renderer.renderSet({1},head,eye);
      for(size_t i=0;i<bodies.size();i+=4)if(bodies[i+3])std::copy_n(bodies.data()+i,4,floor.data()+i);
      assert(grouped==floor);
    }
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      const auto floor=renderer.renderSet({1},head,eye),bodies=renderer.renderSet({2,3},head,eye);
      for(auto &object:renderer.scene.objects){object.renderGroup=7;object.renderTile=object.id==1?0:1;}
      assert(floor==renderer.renderSet({},head,eye,0,true,1,true,-7));
      assert(bodies==renderer.renderSet({},head,eye,0,true,1,true,-263));
      auto whole=renderer.renderSet({},head,eye,0,true,1,true,-7);
      auto other=renderer.renderSet({},head,eye,0,true,1,true,-263);
      for(size_t i=0;i<whole.size();i+=4)if(other[i+3])std::copy_n(other.data()+i,4,whole.data()+i);
      renderer.scene.objects[2].renderTile=0; // Rebalance one member between the tiles.
      auto rebalanced=renderer.renderSet({},head,eye,0,true,1,true,-7);
      other=renderer.renderSet({},head,eye,0,true,1,true,-263);
      for(size_t i=0;i<rebalanced.size();i+=4)if(other[i+3])std::copy_n(other.data()+i,4,rebalanced.data()+i);
      assert(whole==rebalanced);
      for(auto &object:renderer.scene.objects)object.renderTile=0; // Merge both tiles.
      assert(whole==renderer.renderSet({},head,eye,0,true,1,true,-7));
      for(auto &object:renderer.scene.objects){object.renderGroup=0;object.renderTile=0;}
    }
    puts("PASS subdivided GLES: runtime tile keys preserve cross-tile floor/body occlusion and exact stereo pixels on a matching grid");
    {
      Scene groupScene;ModelAsset asset;asset.eyePixels=768;for(int axis=0;axis<3;axis++){asset.low[axis]=-.5f;asset.high[axis]=.5f;}groupScene.assets.push_back(std::move(asset));
      groupScene.objects.push_back({1,1,-.25f,0,0,.1f,0});groupScene.objects.push_back({2,1,.25f,0,0,.1f,0});
      std::vector<const Object*> members{&groupScene.objects[0],&groupScene.objects[1]};
      auto head=renderer.anchor;head.m[2][3]=1;
      const float center[3]={0,0,0},eyes[2][3]={{-.032f,0,1},{.032f,0,1}},r[3]={1,0,0},u[3]={0,1,0},f[3]={0,0,-1};
      const auto layout=groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,1);
      assert(!layout.limited&&layout.pixels%64==0&&layout.pixels/layout.width>=768/.55f);
      for(int eye=0;eye<2;eye++){
        const auto &bounds=layout.bounds[eye];assert(bounds.valid);
        assert(std::abs(bounds.x0-eyes[eye][0]*-1)<layout.width*.5f);assert(std::abs(bounds.x1-eyes[eye][0]*-1)<layout.width*.5f);
      }
      const auto limited=groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,1,128);assert(limited.limited&&limited.pixels==128);
      for(auto &object:groupScene.objects)object.y=-2;
      const auto tile=groupPlane(members,groupScene,renderer.anchor,head,eyes);
      const float tileCenter[3]={tile.transform.m[0][3],tile.transform.m[1][3],tile.transform.m[2][3]};
      const auto offAxis=groupLayout(members,groupScene,renderer.anchor,head,eyes,tileCenter,tile.right,tile.up,tile.forward,1);
      assert(!offAxis.limited&&offAxis.pixels/offAxis.width>=768/.55f&&offAxis.pixels/offAxis.width<768/.55f*1.1f);
      for(auto &object:groupScene.objects)object.y=0;
      groupScene.objects[0].z=-.98f;
      const auto near=groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,2);assert(!near.bounds[0].valid&&near.width==2);
    }
    puts("PASS grouped GLES: stereo shared/independent pixels, external floor occlusion, bounds, density, resolution ceiling and near-plane fallback");
    {
      auto &material=renderer.scene.assets[2].primitives[0];const auto originalMode=material.alphaMode;const float originalAlpha=material.factor[3];
      material.alphaMode=0;material.factor[3]=1;auto opaque=renderer.renderSet({3},{0,.1f,-1.2f},0);
      material.alphaMode=2;material.factor[3]=.5f;auto blended=renderer.renderSet({3},{0,.1f,-1.2f},0);
      material.alphaMode=1;material.factor[3]=.75f;auto masked=renderer.renderSet({3},{0,.1f,-1.2f},0);
      int checked=0;
      for(size_t i=0;i<opaque.size();i+=4)if(opaque[i+3]==255){
        assert(blended[i+3]>=127&&blended[i+3]<=192&&std::abs(int(masked[i+3])-191)<=1);
        const float coverage=blended[i+3]/255.f;
        for(int channel=0;channel<3;channel++){assert(std::abs(int(blended[i+channel])-opaque[i+channel]*coverage)<=2);assert(std::abs(int(masked[i+channel])-opaque[i+channel]*.75f)<=1);}
        checked++;
      }
      assert(checked>100);material.alphaMode=originalMode;material.factor[3]=originalAlpha;
      puts("PASS object alpha: blended and masked framebuffer colors match premultiplied compositor input");
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
      assert(full==renderer.render(id,head,eye,true,true,ignored,true,false,false,true));
      auto cropped=renderer.render(id,head,eye,true,true,ignored,true,true);
      assert(cropped==renderer.render(id,head,eye,true,true,ignored,true,true,false,true));
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

#endif
