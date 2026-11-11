// Two independent scenes with the same local object ID render offscreen.
#include "render_harness.h"

int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;
  assert(initEgl(display,context,surface));
  for(int cycle=0;cycle<3;cycle++){
    auto left=std::make_unique<Renderer>();
    auto right=std::make_unique<Renderer>();
    Object leftObject{},rightObject{};
    leftObject.id=rightObject.id=1;
    leftObject.asset=rightObject.asset=3; // Each renderer owns its own Box asset.
    leftObject.scale.fill(.4f);rightObject.scale.fill(.4f);
    leftObject.x=-.25f;rightObject.x=.25f;
    left->scene.objects={leftObject};right->scene.objects={rightObject};
    const auto leftImage=left->renderSet({1},{0,0,-1.2f},0);
    const auto rightImage=right->renderSet({1},{0,0,-1.2f},0);
    assert(leftImage!=rightImage);
    assert(std::any_of(leftImage.begin(),leftImage.end(),[](unsigned char pixel){return pixel!=0;}));
    assert(std::any_of(rightImage.begin(),rightImage.end(),[](unsigned char pixel){return pixel!=0;}));
    const GLuint leftColor=left->color,rightColor=right->color;
    assert(leftColor!=rightColor&&glIsTexture(leftColor)&&glIsTexture(rightColor));
    left.reset();
    assert(!glIsTexture(leftColor)&&glIsTexture(rightColor));
    assert(right->renderSet({1},{0,0,-1.2f},0)==rightImage);
    right.reset();
    assert(!glIsTexture(rightColor)&&glGetError()==GL_NO_ERROR);
  }
  // Per-script GPU estimates: different workloads differ, nothing is counted for
  // another scene, and an environment both scenes load is counted once as shared.
  {
    auto left=std::make_unique<Renderer>();auto right=std::make_unique<Renderer>();
    const std::map<int,std::unique_ptr<Surface>> none;
    const uint64_t base=ownedGpuEstimate(left->scene,none,nullptr);
    assert(base>0&&ownedGpuEstimate(right->scene,none,nullptr)==base);
    auto car=[](Renderer &renderer){
      renderer.scene.assets.emplace_back();auto &asset=renderer.scene.assets.back();asset.pbr=true;
      assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);
      asset.environment=loadLightingEnvironment("assets/environments/studio.psenv");assert(asset.environment&&asset.environment->upload());
    };
    car(*right);
    const uint64_t heavier=ownedGpuEstimate(right->scene,none,nullptr);
    assert(heavier>base&&ownedGpuEstimate(left->scene,none,nullptr)==base);
    // The car also uses the process-wide sheen table, another shared item.
    const auto &carAsset=right->scene.assets.back();
    const uint64_t sheen=carAsset.sheenLookup&&carAsset.sheenLookup->texture?uint64_t(SheenLookup::size)*SheenLookup::size*4:0;
    const uint64_t shared=carAsset.environment->gpuBytes()+sheen;
    assert(carAsset.environment->gpuBytes()>0&&sharedGpuEstimate({&right->scene})==shared&&sharedGpuEstimate({&left->scene})==0);
    car(*left);
    assert(left->scene.assets.back().environment==right->scene.assets.back().environment);
    assert(sharedGpuEstimate({&left->scene,&right->scene})==shared);
    assert(ownedGpuEstimate(left->scene,none,nullptr)==heavier);
    // A surface's three buffers share one depth attachment; a retained previous
    // generation still counts. Only identifiers are read, so none are allocated.
    std::map<int,std::unique_ptr<Surface>> surfaces;
    auto surface=std::make_unique<Surface>();surface->eyePixels=512;
    for(auto &buffer:surface->buffers)buffer.depth=901;
    surface->previous=std::make_unique<Surface>();surface->previous->eyePixels=256;surface->previous->buffers[0].depth=902;
    surfaces.emplace(1,std::move(surface));
    assert(ownedGpuEstimate(left->scene,surfaces,nullptr)==heavier+1024ull*512*4+512ull*256*4);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
  puts("PASS multi-script offscreen render: overlapping local IDs, independent pixels and GPU cleanup; per-script GPU estimates with shared environments counted once");
}
