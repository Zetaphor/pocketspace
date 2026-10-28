#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static void bufferChecks(){
  for(int variant=0;variant<3;variant++){
    Scene scene;scene.assets.resize(1);auto &asset=scene.assets[0];asset.primitives.resize(1);auto &part=asset.primitives[0];
    const int unique=variant?65538:3;
    for(int repeat=0;repeat<(variant==2?2:1);repeat++)for(int n=0;n<unique;n++)part.vertices.insert(part.vertices.end(),{float(n)/unique,float(n%3)*.1f,0,0,0});
    part.vertexCount=part.vertices.size()/5;part.rgba={255,255,255,255};uploadModel(asset);
    assert(bool(part.indexBuffer)==(variant==2));if(variant==2)assert(part.indexType==GL_UNSIGNED_INT);
    scene.objects.push_back({1,1,0,0,0,1,0});auto anchor=vr::HmdMatrix34_t{};for(int n=0;n<3;n++)anchor.m[n][n]=1;
    auto head=anchor;head.m[2][3]=1;const float eyes[2][3]={{0,0,1},{0,0,1}},r[]={1,0,0},u[]={0,1,0},f[]={0,0,-1};
    ClusterVisibilityExperiment cache(ClusterVisibilityExperiment::Full);cache.update(scene,anchor,head,eyes);
    auto buffers=cache.buffers();assert(buffers.size()==1&&glIsBuffer(buffers[0]));cache.reset();assert(!glIsBuffer(buffers[0]));
    for(bool *flag:{&asset.hasAnimation,&asset.hasSkin,&asset.hasMorph}){*flag=true;cache.update(scene,anchor,head,eyes);assert(cache.buffers().empty());*flag=false;cache.reset();}
    cache.update(scene,anchor,head,eyes);scene.objects[0].animationPose=std::make_shared<ModelPose>();ProjectedBounds target;
    assert(!cache.draw(scene.objects[0],0,target,0,true,eyes[0],r,u,f,1,768,[](unsigned){assert(false);}));scene.objects[0].animationPose.reset();
    cache.reset();glBindBuffer(GL_ARRAY_BUFFER,part.vbo);float invalid=NAN;glBufferSubData(GL_ARRAY_BUFFER,0,sizeof invalid,&invalid);
    cache.update(scene,anchor,head,eyes);assert(cache.buffers().empty());cache.reset();
    deleteModel(asset);assert(glGetError()==GL_NO_ERROR);
  }
  puts("PASS cluster buffers: indexed/nonindexed 16/32-bit streams, deformed/invalid fallback, reset and GPU resource release");
}
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  bufferChecks();
  {
    Renderer r;int views=0;
    for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","assets/Avocado.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));asset.optimizeVertexCache=true;uploadModel(asset);r.scene.assets.push_back(std::move(asset));
    }
    // One material with separated dense islands requires rejection within a
    // primitive, rather than merely repeating material-primitive rejection.
    ModelAsset separated;assert(loadModelFile("assets/samples/Box/Box.glb",separated));
    auto &part=separated.primitives[0];const auto base=part.vertices;part.vertices.clear();
    for(float shift:{-2.f,0.f,2.f})for(int repeat=0;repeat<400;repeat++)for(size_t n=0;n<base.size();n+=5)
      part.vertices.insert(part.vertices.end(),{base[n]+shift,base[n+1],base[n+2],base[n+3],base[n+4]});
    part.vertexCount=part.vertices.size()/5;
    separated.low[0]=-2.5f;separated.high[0]=2.5f;uploadModel(separated);r.scene.assets.push_back(std::move(separated));
    for(int asset:{4,5,6})for(int mode:{0,1,2}){
      r.scene.objects[1].asset=asset;
      for(auto &p:r.scene.assets[asset-1].primitives){p.alphaMode=mode;p.factor[3]=mode==1?0.f:.5f;}
      for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,-.2f,-1.5f},physics::Vec3{0,-.2f,-.025f}})for(int eye=0;eye<2;eye++){
        r.scene.objects[1].yaw=head.x;r.scene.objects[1].pitch=head.y;
        for(const auto &ids:{std::vector<int>{3},std::vector<int>{1,3},std::vector<int>{1,2,3}}){
          r.clusterBounds.mode=ClusterVisibilityExperiment::Original;auto baseline=r.renderSet(ids,head,eye,0,true,1,true);
          r.clusterBounds.mode=ClusterVisibilityExperiment::Cull;auto actual=r.renderSet(ids,head,eye,0,true,1,true);assert(baseline==actual);
          r.clusterBounds.mode=ClusterVisibilityExperiment::Full;assert(baseline==r.renderSet(ids,head,eye,0,true,1,true));++views;
        }
      }
    }
    assert(r.clusterBounds.skipped()>0);
    // Deforming assets must retain the original draw path.
    PrimitiveAsset invalid;invalid.vertices={NAN,0,0,0,0};assert(!PrimitiveVisibilityExperiment::bounds(invalid).valid);
    auto &scene=r.scene;scene.objects[1].asset=6;scene.assets[5].hasAnimation=true;r.clusterBounds.reset();
    r.clusterBounds.mode=ClusterVisibilityExperiment::Cull;auto fallback=r.renderSet({3},{0,.1f,-.7f},0,0,true,1,true);
    r.clusterBounds.mode=ClusterVisibilityExperiment::Original;assert(fallback==r.renderSet({3},{0,.1f,-.7f},0,0,true,1,true));
    printf("PASS cluster visibility: %d exact stereo/group/material/near-plane views in full and culled modes plus animated fallback\n",views);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
