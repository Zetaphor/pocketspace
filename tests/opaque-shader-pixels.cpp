// Exact GLES comparison; includes the actual experimental programs and passes.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer renderer(true);int compared=0;
    for(const char *path:{"assets/samples/Box/Box.glb","assets/Avocado.glb","assets/samples/ToyCar/ToyCar.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));asset.optimizeVertexCache=true;uploadModel(asset);
      renderer.scene.assets.push_back(std::move(asset));renderer.scene.objects[2].asset=renderer.scene.assets.size();
      auto &parts=renderer.scene.assets.back().primitives;
      GLuint checker=0;glGenTextures(1,&checker);glBindTexture(GL_TEXTURE_2D,checker);
      const unsigned char pixels[]={255,100,180,0,90,240,160,255,255,180,100,255,40,120,255,0};
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      std::vector<GLuint> textures;for(auto &part:parts)textures.push_back(part.texture);
      for(int mode=0;mode<4;mode++){
        for(size_t n=0;n<parts.size();n++){
          auto &p=parts[n];p.alphaMode=mode==3?1:mode;p.factor[3]=mode==2?.4f:.8f;p.sheen[0]=.15f;p.sheen[2]=.2f;
          p.texture=mode==3?checker:textures[n];
        }
        for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f},physics::Vec3{-.3f,-.2f,-1.5f}})for(int eye=0;eye<2;eye++){
          renderer.scene.objects[2].yaw=head.x*2;renderer.scene.objects[2].pitch=head.y;renderer.scene.objects[2].roll=.17f;
          for(const auto &ids:{std::vector<int>{1},std::vector<int>{3},std::vector<int>{1,2,3}}){
            renderer.specialize=false;auto baseline=renderer.renderSet(ids,head,eye,0,true,1,true);
            renderer.specialize=true;auto specialized=renderer.renderSet(ids,head,eye,0,true,1,true);
            assert(baseline==specialized);++compared;
          }
        }
      }
      for(size_t n=0;n<parts.size();n++)parts[n].texture=textures[n];glDeleteTextures(1,&checker);
    }
    printf("PASS opaque shader specialization: %d exact stereo/group/material views, masked holes and blended fallback preserved\n",compared);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
