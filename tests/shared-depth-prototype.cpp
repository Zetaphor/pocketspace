// Isolated experiment, never enabled in the engine or script API.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"

static size_t differences(const std::vector<unsigned char> &a,const std::vector<unsigned char> &b){
  assert(a.size()==b.size());size_t count=0;
  for(size_t i=0;i<a.size();i+=4)if(!std::equal(a.begin()+i,a.begin()+i+4,b.begin()+i))count++;
  return count;
}
static size_t compare(Renderer &r,physics::Vec3 head,int eye){
  std::vector<std::vector<unsigned char>> reference;
  for(const auto &o:r.scene.objects)reference.push_back(r.renderSet({o.id},head,eye));
  r.renderSet({},head,eye,1,false);
  size_t count=0;
  for(size_t i=0;i<r.scene.objects.size();i++)count+=differences(reference[i],r.renderSet({r.scene.objects[i].id},head,eye,2));
  return count;
}
int main(int argc,char **argv){
  const bool bench=argc>1&&std::string(argv[1])=="--bench";
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer r;size_t differencesTotal=0;int views=0;
    // Matching grid is intentionally a best case; real group grids differ.
    for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
      auto count=compare(r,head,eye);differencesTotal+=count;views++;
      printf("PIXELS case=opaque eye=%d differing_surface_pixels=%zu\n",eye,count);
    }
    auto &box=r.scene.assets[2].primitives[0];const int mode=box.alphaMode;const auto factor=std::array<float,4>{box.factor[0],box.factor[1],box.factor[2],box.factor[3]};
    for(int alphaMode:{1,2}){
      box.alphaMode=alphaMode;box.factor[3]=alphaMode==1?0.f:.5f;
      for(int eye=0;eye<2;eye++){auto count=compare(r,{0,.1f,-1.2f},eye);differencesTotal+=count;views++;printf("PIXELS case=%s eye=%d differing_surface_pixels=%zu\n",alphaMode==1?"masked-hole":"blended",eye,count);}
    }
    box.alphaMode=mode;std::copy(factor.begin(),factor.end(),box.factor);
    GLuint mask=0;glGenTextures(1,&mask);glBindTexture(GL_TEXTURE_2D,mask);
    const unsigned char checker[]={255,255,255,0,255,255,255,255,255,255,255,255,255,255,255,0};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,checker);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const GLuint originalTexture=box.texture;box.texture=mask;box.alphaMode=1;
    for(int eye=0;eye<2;eye++){auto count=compare(r,{0,.1f,-1.2f},eye);differencesTotal+=count;views++;printf("PIXELS case=checker-mask eye=%d differing_surface_pixels=%zu\n",eye,count);}
    box.texture=originalTexture;box.alphaMode=mode;glDeleteTextures(1,&mask);
    r.scene.objects[2].visible=false;
    for(int eye=0;eye<2;eye++){auto count=compare(r,{0,.1f,-1.2f},eye);differencesTotal+=count;views++;printf("PIXELS case=hidden eye=%d differing_surface_pixels=%zu\n",eye,count);}
    r.scene.objects[2].visible=true;
    auto reference=r.renderSet({3},{0,.1f,-1.2f},0);
    r.renderSet({},{0,.1f,-1.2f},0,1,false,1.01f);
    auto shifted=r.renderSet({3},{0,.1f,-1.2f},0,2);
    size_t gridMismatch=differences(reference,shifted);assert(gridMismatch>0);
    printf("REJECT differing_grid_surface_pixels=%zu (1%% projection scale mismatch)\n",gridMismatch);
    // Coincident surfaces reveal a semantic change: LEQUAL admits both owners.
    r.scene.objects.push_back(r.scene.objects[2]);r.scene.objects.back().id=4;
    size_t ties=compare(r,{0,.1f,-1.2f},0);assert(ties>0);
    printf("REJECT coincident_surface_pixels=%zu (baseline strict depth excludes equal-depth nonmembers)\n",ties);
    r.scene.objects.pop_back();
    for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","assets/Avocado.glb"}){ModelAsset asset;assert(loadModelFile(path,asset));uploadModel(asset);r.scene.assets.push_back(std::move(asset));}
    for(int asset:{4,5}){
      r.scene.objects[2].asset=asset;
      for(int eye=0;eye<2;eye++){auto count=compare(r,{0,.1f,-1.2f},eye);differencesTotal+=count;views++;printf("PIXELS case=model-%d eye=%d differing_surface_pixels=%zu\n",asset,eye,count);}
    }
    r.scene.objects[2].asset=3;
    printf("SUMMARY views=%d ordinary_differences=%zu adoption=blocked_by_equal_depth_and_grid_requirements\n",views,differencesTotal);
    if(bench){
      for(bool detailed:{false,true}){
        r.scene.objects.clear();r.scene.objects.push_back({1,1,0,-.55f,0,1.2f,0});
        std::vector<std::vector<int>> groups(5);groups[4].push_back(1);
        for(int n=0;n<48;n++){const int col=n%8,row=(n/8)%8,id=n+2;r.scene.objects.push_back({id,detailed?4+n%2:3,(col-3.5f)*.12f,-.2f,row*.12f,.1f,0});groups[(col/4)+2*(row/4)].push_back(id);}
        auto frame=[&](bool shared){for(int eye=0;eye<2;eye++){
          if(shared)r.renderSet({},{0,.15f,-1.2f},eye,1,false);
          for(const auto &ids:groups)r.renderSet(ids,{0,.15f,-1.2f},eye,shared?2:0,false,1,true);
        }glFinish();assert(glGetError()==GL_NO_ERROR);};
        for(int round=0;round<3;round++)for(int order=0;order<2;order++){
          const bool shared=(round%2?order==0:order==1);
          for(int n=0;n<5;n++)frame(shared);
          auto start=std::chrono::steady_clock::now();for(int n=0;n<30;n++)frame(shared);
          printf("BENCH geometry=%s round=%d shared=%d groups=5 objects=49 grid=%d baseline_culling=1 frame_ms=%.3f\n",detailed?"models":"boxes",round,int(shared),pixels,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30);fflush(stdout);
        }
      }
    }
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
