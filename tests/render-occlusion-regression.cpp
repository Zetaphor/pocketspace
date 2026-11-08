// Actual GLES shaders and render passes, without opening an OpenVR session.
#include "render_harness.h"
#include <set>
static void indexingValidation(){
  // UV seams and distinct float bit patterns must remain separate.
  const std::vector<float> source={0,0,0,0,0, 1,0,0,1,0, 0,1,0,0,1, 0,0,0,0,0, 0,0,0,1,1, -0.f,0,0,0,0};
  auto indexed=mesh::index(source);assert(indexed.vertices.size()==25&&indexed.indices.size()==6);
  for(size_t i=0;i<indexed.indices.size();i++)for(int j=0;j<5;j++)assert(std::bit_cast<uint32_t>(source[i*5+j])==std::bit_cast<uint32_t>(indexed.vertices[indexed.indices[i]*5+j]));
  for(bool repeated:{false,true}){
    ModelAsset asset;asset.optimizeVertexCache=true;asset.primitives.resize(1);auto &part=asset.primitives[0];
    const int count=repeated?65538:3;
    for(int repeat=0;repeat<(repeated?2:1);repeat++)for(int i=0;i<count;i++)part.vertices.insert(part.vertices.end(),{float(i),0,0,0,0});
    part.vertexCount=GLsizei(part.vertices.size()/5);part.rgba={255,255,255,255};
    uploadModel(asset);
    if(repeated){assert(part.indexBuffer&&part.indexType==GL_UNSIGNED_INT&&part.gpuVertexCount==count);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer);GLint bytes;glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);assert(bytes==part.vertexCount*4);}
    else assert(!part.indexBuffer&&part.gpuVertexCount==part.vertexCount);
    auto buffer=part.indexBuffer;deleteModel(asset);assert(!part.vbo&&!part.indexBuffer&&!part.texture);if(buffer)assert(!glIsBuffer(buffer));
  }
}
// External depth clusters: every triangle once with its winding, conservative
// bounds, upload eligibility and cleanup, and exact depth inside a culled region.
static void depthClusters(){
  constexpr int grid=80;
  auto mesh=[&](bool distinct){
    std::vector<float> vertices;
    auto corner=[&](int x,int y,int k){const float px=x*2.f/grid-1,py=y*2.f/grid-1;vertices.insert(vertices.end(),{px,py,.3f*std::sin(3*px)*std::cos(2*py),distinct?float(k):0,0});};
    int k=0;
    for(int y=0;y<grid;y++)for(int x=0;x<grid;x++){corner(x,y,k++);corner(x+1,y,k++);corner(x+1,y+1,k++);corner(x,y,k++);corner(x+1,y+1,k++);corner(x,y+1,k++);}
    return vertices;
  };
  {
    const auto vertices=mesh(false);const size_t count=vertices.size()/5;
    std::vector<uint32_t> ordered;auto clusters=buildDepthClusters(vertices.data(),5,nullptr,count,ordered);
    assert(ordered.size()==count&&clusters.size()==(count/3+kDepthClusterTriangles-1)/kDepthClusterTriangles);
    std::multiset<std::array<uint32_t,3>> before,after;
    for(size_t t=0;t<count/3;t++){before.insert({uint32_t(t*3),uint32_t(t*3+1),uint32_t(t*3+2)});after.insert({ordered[t*3],ordered[t*3+1],ordered[t*3+2]});}
    assert(before==after); // Same triangles, same winding.
    uint32_t next=0;
    for(const auto &cluster:clusters){
      assert(cluster.first==next&&cluster.count%3==0&&cluster.count<=kDepthClusterTriangles*3);next+=cluster.count;
      for(uint32_t i=cluster.first;i<cluster.first+cluster.count;i++)for(int axis=0;axis<3;axis++){const float value=vertices[ordered[i]*5+axis];assert(value>=cluster.low[axis]&&value<=cluster.high[axis]);}
    }
    assert(next==count);
    std::vector<uint32_t> again;buildDepthClusters(vertices.data(),5,nullptr,count,again);assert(again==ordered);
  }
  const char *vertexSource="attribute vec3 p;void main(){gl_Position=vec4(p,1.);}";
  const char *fragmentSource="precision mediump float;uniform vec4 color;void main(){gl_FragColor=color;}";
  auto compile=[](GLenum type,const char *source){GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);assert(ok);return shader;};
  GLuint program=glCreateProgram();glAttachShader(program,compile(GL_VERTEX_SHADER,vertexSource));glAttachShader(program,compile(GL_FRAGMENT_SHADER,fragmentSource));
  glBindAttribLocation(program,0,"p");glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);
  constexpr int size=128;GLuint color,depth,fbo;
  glGenRenderbuffers(1,&color);glBindRenderbuffer(GL_RENDERBUFFER,color);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,size,size);
  glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,size,size);
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
  assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glUseProgram(program);glEnableVertexAttribArray(0);glViewport(0,0,size,size);
  const GLint colorLocation=glGetUniformLocation(program,"color");
  GLuint quad;glGenBuffers(1,&quad);const float plane[]={-1,-1,0,0,0, 1,-1,0,0,0, 1,1,0,0,0, -1,-1,0,0,0, 1,1,0,0,0, -1,1,0,0,0};
  glBindBuffer(GL_ARRAY_BUFFER,quad);glBufferData(GL_ARRAY_BUFFER,sizeof(plane),plane,GL_STATIC_DRAW);
  const int region[4]={30,50,40,40}; // x, y, width, height in pixels
  const float x0=region[0]*2.f/size-1,x1=(region[0]+region[2])*2.f/size-1,y0=region[1]*2.f/size-1,y1=(region[1]+region[3])*2.f/size-1;
  for(bool distinct:{false,true}){
    ModelAsset asset;asset.primitives.resize(1);auto &part=asset.primitives[0];
    part.vertices=mesh(distinct);part.vertexCount=GLsizei(part.vertices.size()/5);part.rgba={255,255,255,255};
    uploadModel(asset);
    assert(bool(part.indexBuffer)==!distinct); // Indexed and unindexed streams both get clusters.
    const auto &set=part.depthClusters;assert(set.indexBuffer&&!set.clusters.empty());
    assert(set.bytes==uint64_t(part.vertexCount)*(set.indexType==GL_UNSIGNED_SHORT?2:4));
    assert(set.indexType==GL_UNSIGNED_SHORT); // Under 65,536 vertices either way.
    auto image=[&](bool clustered,size_t &drawn){
      glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      glEnable(GL_SCISSOR_TEST);glScissor(region[0],region[1],region[2],region[3]);
      glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
      drawn=0;
      if(!clustered)drawModelPrimitive(part);
      else assert(drawDepthClusters(part,[&](const DepthCluster &c){const bool keep=c.high[0]>=x0&&c.low[0]<=x1&&c.high[1]>=y0&&c.low[1]<=y1;drawn+=keep;return keep;}));
      glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUniform4f(colorLocation,1,1,1,1);
      glBindBuffer(GL_ARRAY_BUFFER,quad);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glDrawArrays(GL_TRIANGLES,0,6);
      std::vector<unsigned char> pixels(size_t(region[2])*region[3]*4);
      glReadPixels(region[0],region[1],region[2],region[3],GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
      glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);return pixels;
    };
    size_t all=0,kept=0;const auto full=image(false,all),culled=image(true,kept);
    assert(full==culled&&kept>0&&kept<set.clusters.size()/2);
    size_t covered=0;for(size_t i=0;i<full.size();i+=4)covered+=full[i]==0;
    assert(covered>0&&covered<full.size()/4); // The mesh both hides and reveals the plane here.
    const GLuint buffer=set.indexBuffer;deleteModel(asset);assert(!part.depthClusters.indexBuffer&&!glIsBuffer(buffer));
  }
  for(int variant=0;variant<3;variant++){
    ModelAsset asset;asset.primitives.resize(1);auto &part=asset.primitives[0];
    part.vertices=mesh(false);
    if(variant==0)part.vertices.resize(size_t(kDepthClusterMinTriangles-1)*15); // Too small to benefit.
    if(variant==1)part.alphaMode=2; // Blended primitives never occlude.
    if(variant==2)asset.hasAnimation=true; // Deformed geometry uses its pose.
    part.vertexCount=GLsizei(part.vertices.size()/5);part.rgba={255,255,255,255};
    uploadModel(asset);assert(!part.depthClusters.indexBuffer&&part.depthClusters.clusters.empty());deleteModel(asset);
  }
  glDisableVertexAttribArray(0);glDeleteBuffers(1,&quad);glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&color);glDeleteRenderbuffers(1,&depth);glDeleteProgram(program);
  glBindFramebuffer(GL_FRAMEBUFFER,0);glUseProgram(0);
  puts("PASS depth clusters: every triangle once with winding, conservative bounds, exact region depth for indexed and unindexed streams, eligibility and cleanup");
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
static std::vector<uint32_t> uploadedIndices(const PrimitiveAsset &part){
  assert(part.indexBuffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer);
  const size_t size=part.indexType==GL_UNSIGNED_SHORT?2:4;
  const void *data=glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,0,part.vertexCount*size,GL_MAP_READ_BIT);assert(data);
  std::vector<uint32_t> result(part.vertexCount);
  for(size_t i=0;i<result.size();i++)result[i]=size==2?static_cast<const uint16_t*>(data)[i]:static_cast<const uint32_t*>(data)[i];
  assert(glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER));
  // Compare oriented triangles using original exact vertex IDs even when the
  // GPU vertex stream is repacked into optimized first-use order.
  const auto source=mesh::index(part.vertices);
  std::map<std::array<uint32_t,5>,uint32_t> original;
  for(size_t i=0;i<source.vertices.size();i+=5){std::array<uint32_t,5> key;for(int j=0;j<5;j++)key[j]=std::bit_cast<uint32_t>(source.vertices[i+j]);original.emplace(key,i/5);}
  glBindBuffer(GL_ARRAY_BUFFER,part.vbo);
  const float *vertices=static_cast<const float*>(glMapBufferRange(GL_ARRAY_BUFFER,0,part.gpuVertexCount*5*sizeof(float),GL_MAP_READ_BIT));assert(vertices);
  std::vector<uint32_t> remap(part.gpuVertexCount);
  for(size_t i=0;i<remap.size();i++){
    std::array<uint32_t,5> key;for(int j=0;j<5;j++)key[j]=std::bit_cast<uint32_t>(vertices[i*5+j]);
    auto found=original.find(key);assert(found!=original.end());remap[i]=found->second;
  }
  assert(glUnmapBuffer(GL_ARRAY_BUFFER));
  for(auto &index:result){assert(index<remap.size());index=remap[index];}
  return result;
}
static size_t cacheMisses(const std::vector<uint32_t> &indices){
  std::vector<uint32_t> cache;size_t misses=0;
  for(auto index:indices){auto found=std::find(cache.begin(),cache.end(),index);if(found==cache.end())++misses;else cache.erase(found);cache.insert(cache.begin(),index);if(cache.size()>32)cache.pop_back();}
  return misses;
}
static void vertexCachePixels(){
  {
    // Do not pass an incomplete triangle tail to a library that requires triples.
    ModelAsset asset;asset.optimizeVertexCache=true;asset.primitives.resize(1);auto &part=asset.primitives[0];
    for(int i=0;i<4;i++)part.vertices.insert(part.vertices.end(),{0,0,0,0,0});
    part.vertexCount=4;part.rgba={255,255,255,255};uploadModel(asset);
    assert(uploadedIndices(part)==std::vector<uint32_t>({0,0,0,0}));deleteModel(asset);
  }
  Renderer reference,optimized;size_t views=0;
  for(const char *path:{"assets/samples/Box/Box.glb","assets/Avocado.glb","assets/samples/ToyCar/ToyCar.glb"})for(int mode=0;mode<3;mode++){
    for(auto *renderer:{&reference,&optimized}){
      auto &asset=renderer->scene.assets[2];deleteModel(asset);asset={};assert(loadModelFile(path,asset));asset.optimizeVertexCache=renderer==&optimized;
      for(auto &part:asset.primitives){part.alphaMode=mode;part.factor[3]=mode==2?.6f:1;}
      uploadModel(asset);
      for(auto &part:asset.primitives)if(part.indexBuffer){
        const auto source=mesh::index(part.vertices).indices,actual=uploadedIndices(part);
        if(mode==2||renderer==&reference)assert(source==actual);
        else{
          std::vector<std::array<uint32_t,3>> before,after;
          for(size_t i=0;i<source.size();i+=3){before.push_back({source[i],source[i+1],source[i+2]});after.push_back({actual[i],actual[i+1],actual[i+2]});}
          std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());assert(before==after);
          if(mode==0)printf("CACHE_REUSE model=%s triangles=%zu misses_before=%zu misses_after=%zu\n",path,source.size()/3,cacheMisses(source),cacheMisses(actual));
        }
        if(mode==1){const unsigned char holes[16]={255,255,255,255,255,255,255,0,255,255,255,0,255,255,255,255};glBindTexture(GL_TEXTURE_2D,part.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,holes);glGenerateMipmap(GL_TEXTURE_2D);}
      }
    }
    for(int pose=0;pose<3;pose++){
      for(auto *renderer:{&reference,&optimized}){auto &object=renderer->scene.objects[2];object.pitch=.13f*pose;object.yaw=.37f*pose;object.roll=-.17f*pose;}
      for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
        const auto before=reference.renderSet({1,2,3},head,eye),after=optimized.renderSet({1,2,3},head,eye);
        assert(before==after);++views;
      }
    }
  }
  for(int deforming=0;deforming<2;deforming++){
    ModelAsset asset;assert(loadModelFile("assets/Avocado.glb",asset));asset.optimizeVertexCache=true;asset.hasSkin=deforming;asset.hasAnimation=!deforming;uploadModel(asset);
    for(const auto &part:asset.primitives)if(part.indexBuffer)assert(uploadedIndices(part)==mesh::index(part.vertices).indices);
    deleteModel(asset);
  }
  printf("PASS vertex cache: %zu exact stereo model/material/rotation views, oriented triangle preservation, blend/deformation order and CPU geometry\n",views);
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
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  vertexCachePixels();
  {
    coherentGroupPlanes();indexingValidation();depthClusters();sharedDepthLifetime(display);
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
      // Refused growth must preserve the complete image on the existing grid.
      assert(grouped==renderer.renderSet({1,2,3},head,eye,0,true,1,false,0,4096));
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
      // Display density caps the reference density: 1.25 * 400 px per tangent at 1 m.
      for(auto &object:groupScene.objects)object.renderGroup=1;
      FrameBoundsCache cache;cache.update(groupScene,renderer.anchor,head,eyes);
      const auto uncapped=groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,1,4096,&cache);
      cache.displayFocal=400;
      const auto capped=groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,1,4096,&cache);
      assert(!capped.limited&&capped.pixels<uncapped.pixels&&capped.pixels/capped.width>=500.f&&capped.pixels/capped.width<500.f*1.2f);
      assert(capped.width>=uncapped.width&&capped.width<uncapped.width+32/500.f); // Same bounds; a wider 32-pixel border.
      cache.displayFocal=5000; // A sharper display than the reference keeps the reference.
      assert(groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,1,4096,&cache).pixels==uncapped.pixels);
      for(auto &object:groupScene.objects)object.renderGroup=0;
      // Independent buffers allocate 25% headroom up to the reference; a crop that
      // swings between 576 and 640 pixels then fits one 768-pixel allocation.
      assert(surfaceBufferPixels(576,768)==768&&surfaceBufferPixels(640,768)==768&&surfaceBufferPixels(256,768)==320&&surfaceBufferPixels(128,256)==192);
      for(auto &object:groupScene.objects)object.y=-2;
      const auto tile=groupPlane(members,groupScene,renderer.anchor,head,eyes);
      const float tileCenter[3]={tile.transform.m[0][3],tile.transform.m[1][3],tile.transform.m[2][3]};
      const auto offAxis=groupLayout(members,groupScene,renderer.anchor,head,eyes,tileCenter,tile.right,tile.up,tile.forward,1);
      assert(!offAxis.limited&&offAxis.pixels/offAxis.width>=768/.55f&&offAxis.pixels/offAxis.width<768/.55f*1.1f);
      for(auto &object:groupScene.objects)object.y=0;
      groupScene.objects[0].z=-.98f;
      const auto near=groupLayout(members,groupScene,renderer.anchor,head,eyes,center,r,u,f,2);assert(!near.bounds[0].valid&&near.width==2);
    }
    puts("PASS grouped GLES: stereo shared/independent pixels, external floor occlusion, bounds, density, display density cap, resolution ceiling and near-plane fallback");
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
