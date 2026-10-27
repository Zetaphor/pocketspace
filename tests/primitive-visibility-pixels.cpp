#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
static void fallbackChecks(){
  Scene scene;scene.assets.resize(1);auto &asset=scene.assets[0];asset.primitives.resize(2);
  for(auto &part:asset.primitives){part.vertices={10,0,0,0,0,11,0,0,0,0,10,1,0,0,0};part.vertexCount=3;}
  scene.objects.push_back({1,1,0,0,0,1,0});auto anchor=vr::HmdMatrix34_t{};for(int i=0;i<3;i++)anchor.m[i][i]=1;
  auto head=anchor;head.m[2][3]=1;const float eyes[2][3]={{0,0,1},{0,0,1}},r[]={1,0,0},u[]={0,1,0},f[]={0,0,-1};
  ProjectedBounds target;target.x0=target.y0=-.1f;target.x1=target.y1=.1f;target.near=.5f;target.far=2;
  PrimitiveVisibilityExperiment cache;
  auto draw=[&]{return cache.mayDraw(scene.objects[0],0,target,0,true,eyes[0],r,u,f,1,768);};
  cache.update(scene,anchor,head,eyes);assert(!draw());
  for(bool *flag:{&asset.hasAnimation,&asset.hasSkin,&asset.hasMorph}){*flag=true;cache.update(scene,anchor,head,eyes);assert(draw());*flag=false;}
  scene.objects[0].z=-1;cache.update(scene,anchor,head,eyes);assert(draw());scene.objects[0].z=0;
  asset.primitives[0].vertices[0]=NAN;cache.reset();cache.update(scene,anchor,head,eyes);assert(draw());
  asset.primitives[0].vertices[0]=10;cache.reset();cache.update(scene,anchor,head,eyes);assert(!draw());
  target.valid=false;assert(draw());target.valid=true;target.x0=NAN;assert(draw());
  scene.objects.clear();cache.reset();cache.update(scene,anchor,head,eyes);
}
int main(){
  fallbackChecks();
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer r;int views=0;
    for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","assets/Avocado.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));asset.optimizeVertexCache=true;uploadModel(asset);r.scene.assets.push_back(std::move(asset));
    }
    // Multiple material primitives with deliberately disjoint geometry make
    // primitive rejection observable rather than merely exercising fallback.
    ModelAsset separated;assert(loadModelFile("assets/samples/Box/Box.glb",separated));
    auto part=separated.primitives[0];for(float shift:{-2.f,2.f}){
      auto copy=part;for(size_t n=0;n<copy.vertices.size();n+=5)copy.vertices[n]+=shift;separated.primitives.push_back(std::move(copy));
    }
    separated.low[0]=-2.5f;separated.high[0]=2.5f;uploadModel(separated);r.scene.assets.push_back(std::move(separated));
    for(int asset:{4,5,6})for(int mode:{0,1,2}){
      r.scene.objects[1].asset=asset;
      for(auto &p:r.scene.assets[asset-1].primitives){p.alphaMode=mode;p.factor[3]=mode==1?0.f:.5f;}
      for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,-.2f,-1.5f},physics::Vec3{0,-.2f,-.025f}})for(int eye=0;eye<2;eye++){
        r.scene.objects[1].yaw=head.x;r.scene.objects[1].pitch=head.y;
        for(const auto &ids:{std::vector<int>{3},std::vector<int>{1,3},std::vector<int>{1,2,3}}){
          r.primitiveVisibility=false;auto baseline=r.renderSet(ids,head,eye,0,true,1,true);
          r.primitiveVisibility=true;auto actual=r.renderSet(ids,head,eye,0,true,1,true);assert(baseline==actual);++views;
        }
      }
    }
    assert(r.primitiveBounds.rejected()>0);
    // Invalid geometry, near-plane crossing, and animated assets must retain draws.
    PrimitiveAsset invalid;invalid.vertices={NAN,0,0,0,0};assert(!PrimitiveVisibilityExperiment::bounds(invalid).valid);
    auto &scene=r.scene;scene.objects[1].asset=6;scene.assets[5].hasAnimation=true;r.primitiveBounds.reset();
    r.primitiveVisibility=true;auto fallback=r.renderSet({3},{0,.1f,-.7f},0,0,true,1,true);
    r.primitiveVisibility=false;assert(fallback==r.renderSet({3},{0,.1f,-.7f},0,0,true,1,true));
    printf("PASS primitive visibility: %d exact stereo/group/material/near-plane views plus animated and invalid-data fallback\n",views);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
