#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static void state(Renderer &r){
  GLint value=0;glGetIntegerv(GL_CURRENT_PROGRAM,&value);assert(GLuint(value)==r.program);
  for(GLuint attribute:{2u,3u}){
    glGetVertexAttribiv(attribute,GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&value);assert(value==0);
    glGetVertexAttribiv(attribute,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&value);assert(!value);
  }
}
static void compare(Renderer &r,const std::vector<int> &ids,physics::Vec3 head,int eye){
  r.instanceExperiment.mode=ModelInstancing::Original;auto original=r.renderSet(ids,head,eye,0,true,1,true);
  r.instanceExperiment.mode=ModelInstancing::Instanced;auto instanced=r.renderSet(ids,head,eye,0,true,1,true);
  if(original!=instanced){size_t pixels=0;for(size_t n=0;n<original.size();n+=4)if(!std::equal(original.begin()+n,original.begin()+n+4,instanced.begin()+n))++pixels;fprintf(stderr,"instancing differing pixels=%zu\n",pixels);std::exit(1);}
  state(r);
}
static void extraChecks(Renderer &r){
  // Nonindexed and 32-bit indexed single-primitive geometry use both GL paths.
  for(bool large:{false,true}){
    ModelAsset asset;asset.gpuInstancing=true;asset.low[0]=asset.low[1]=-.5f;asset.high[0]=asset.high[1]=.5f;asset.primitives.resize(1);auto &p=asset.primitives[0];
    const int count=large?65538:3;
    for(int repeat=0;repeat<(large?2:1);repeat++)for(int n=0;n<count;n++){
      const float x=large?float(n%256)/255-.5f:(n==1?.4f:-.4f),y=large?float(n/256)/256-.5f:(n==2?.4f:-.4f);
      p.vertices.insert(p.vertices.end(),{x,y,0,0,0});
    }
    p.vertexCount=p.vertices.size()/5;p.rgba={190,220,255,255};uploadModel(asset);
    assert(large?(p.indexBuffer&&p.indexType==GL_UNSIGNED_INT):!p.indexBuffer);
    r.scene.assets.push_back(std::move(asset));const int key=r.scene.assets.size();
    r.scene.objects.clear();for(int n=0;n<3;n++)r.scene.objects.push_back({n+1,key,(n-1)*.12f,-.2f,0,.2f,float(n)*.3f,.1f,-.1f});
    compare(r,{1,2,3},{0,.1f,-.7f},0);compare(r,{1,2,3},{0,.1f,-.7f},1);
  }
  auto &asset=r.scene.assets.back();const auto before=r.instanceExperiment.submittedBatches();
  for(bool *flag:{&asset.hasAnimation,&asset.hasSkin,&asset.hasMorph}){*flag=true;compare(r,{1,2,3},{0,.1f,-.7f},0);*flag=false;}
  assert(r.instanceExperiment.submittedBatches()==before);
  for(auto &object:r.scene.objects){
    object.animationPose=std::make_shared<ModelPose>();auto &geometry=object.animationPose->geometry;geometry.primitives.resize(1);auto &part=geometry.primitives[0];part.vertices=asset.primitives[0].vertices;part.vertexCount=asset.primitives[0].vertexCount;
    for(size_t n=0;n<part.vertices.size();n+=5)part.vertices[n]+=.05f;uploadModelPose(*object.animationPose);
  }
  compare(r,{1,2,3},{0,.1f,-.7f},0);assert(r.instanceExperiment.submittedBatches()==before);
  // Capacity boundary and an asset barrier: no global sorting to manufacture runs.
  r.scene.objects.clear();std::vector<int> ids;
  const int small=r.scene.assets.size()-1;
  for(int n=0;n<256;n++){r.scene.objects.push_back({n+1,small,(n%16-7.5f)*.035f,(n/16-7.5f)*.035f,0,.025f,0});ids.push_back(n+1);}
  compare(r,ids,{0,.1f,-.7f},0);compare(r,ids,{0,.1f,-.7f},1);
  r.scene.objects[128].asset=r.scene.assets.size();compare(r,ids,{0,.1f,-.7f},0);
  puts("PASS instancing paths: arrays/32-bit indices, deformation flags and poses, 256-instance capacity, ordered asset barrier");
}
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer r;int views=0;for(auto &asset:r.scene.assets)asset.gpuInstancing=true;
    for(const char *path:{"assets/Avocado.glb","assets/samples/ToyCar/ToyCar.glb"}){ModelAsset asset;assert(loadModelFile(path,asset));asset.optimizeVertexCache=true;asset.gpuInstancing=true;uploadModel(asset);r.scene.assets.push_back(std::move(asset));}
    ModelAsset quad;quad.gpuInstancing=true;quad.low[0]=quad.low[1]=-.5f;quad.high[0]=quad.high[1]=.5f;quad.primitives.resize(1);
    quad.primitives[0].vertices={-.5f,-.5f,0,0,0, .5f,-.5f,0,1,0, .5f,.5f,0,1,1, -.5f,-.5f,0,0,0, .5f,.5f,0,1,1, -.5f,.5f,0,0,1};
    quad.primitives[0].vertexCount=6;quad.primitives[0].rgba={255,255,255,255};uploadModel(quad);r.scene.assets.push_back(std::move(quad));
    for(int n=0;n<7;n++){auto object=r.scene.objects[2];object.id=4+n;object.x=(n%3-1)*.12f;object.y=-.2f+(n/3)*.1f;object.z=(n%2)*.08f;object.yaw=n*.3f;object.pitch=n*.11f;object.roll=-n*.08f;object.scale=.1f+n*.007f;r.scene.objects.push_back(object);}
    // Coplanar instances with rotated UVs must retain input ownership/order.
    r.scene.objects[4]=r.scene.objects[3];r.scene.objects[4].id=5;r.scene.objects[4].roll=float(std::acos(-1));
    for(int asset:{3,4,5,6}){
      for(size_t n=2;n<r.scene.objects.size();n++)r.scene.objects[n].asset=asset;
      auto &parts=r.scene.assets[asset-1].primitives;std::vector<GLuint> textures;for(auto &p:parts)textures.push_back(p.texture);
      GLuint checker;glGenTextures(1,&checker);glBindTexture(GL_TEXTURE_2D,checker);
      const unsigned char texels[]={255,60,120,0,90,240,160,255,255,180,100,255,40,120,255,0};
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      for(int mode:{0,1,2,3}){
        for(size_t n=0;n<parts.size();n++){auto &p=parts[n];p.alphaMode=mode==3?1:mode;p.factor[3]=mode==2?.4f:.8f;p.sheen[0]=.15f;p.sheen[2]=.2f;p.texture=mode==3?checker:textures[n];}
        if(asset==6&&mode==3){
          for(auto &object:r.scene.objects)object.visible=object.id==4||object.id==5;
          const std::vector<int> ids={4,5};r.instanceExperiment.mode=ModelInstancing::Original;
          auto first=r.renderSet(ids,{0,-.2f,-.7f},0,0,true,1,true);std::swap(r.scene.objects[3],r.scene.objects[4]);
          auto reversed=r.renderSet(ids,{0,-.2f,-.7f},0,0,true,1,true);std::swap(r.scene.objects[3],r.scene.objects[4]);assert(first!=reversed);
          compare(r,ids,{0,-.2f,-.7f},0);++views;for(auto &object:r.scene.objects)object.visible=true;
        }
        for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,-.2f,-1.5f},physics::Vec3{0,-.2f,-.025f}})for(int eye=0;eye<2;eye++){
          for(const auto &ids:{std::vector<int>{3,4,5,6,7,8,9,10},std::vector<int>{3},std::vector<int>{1,3,4,5,6,7,8,9,10}}){compare(r,ids,head,eye);++views;}
        }
      }
      for(size_t n=0;n<parts.size();n++)parts[n].texture=textures[n];glDeleteTextures(1,&checker);
    }
    assert(r.instanceExperiment.submittedBatches()>0);
    float rotation[9];objectRotationMatrix(.3f,.6f,-.2f,rotation);for(int row=0;row<3;row++)for(int column=0;column<3;column++)r.anchor.m[row][column]=rotation[row*3+column];
    for(size_t n=2;n<r.scene.objects.size();n++)r.scene.objects[n].asset=4;
    for(int eye=0;eye<2;eye++){compare(r,{3,4,5,6,7,8,9,10},{.3f,.3f,-1.5f},eye);++views;}
    extraChecks(r);
    auto buffer=r.instanceExperiment.instanceBuffer();assert(buffer&&glIsBuffer(buffer));r.instanceExperiment.reset();assert(!glIsBuffer(buffer));
    printf("PASS GPU instancing: %d exact stereo/group/material/near-plane views, ordered overlap, multi-part and blended fallback, attribute state and buffer release\n",views);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
