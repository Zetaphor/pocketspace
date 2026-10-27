// Verify that both producer completion methods expose the same finished pixels.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include "gpu-sync-experiment.h"
int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer renderer;
    std::vector<std::vector<unsigned char>> baseline;int compared=0;
    for(const char *mode:{"finish","fence"}){
      setenv("SF_GPU_SYNC_MODE",mode,1);GpuSyncExperiment completion;size_t frame=0;
      for(int alphaMode:{0,1,2})for(auto head:{physics::Vec3{0,.1f,-.7f},physics::Vec3{.3f,.3f,-1.5f}})for(int eye=0;eye<2;eye++){
        auto &part=renderer.scene.assets[2].primitives[0];part.alphaMode=alphaMode;part.factor[3]=alphaMode==1?0.f:alphaMode==2?.5f:1.f;
        completion.begin();renderer.renderSet({2,3},head,eye,0,false);completion.complete();
        std::vector<unsigned char> image(pixels*pixels*4);glReadPixels(0,0,pixels,pixels,GL_RGBA,GL_UNSIGNED_BYTE,image.data());assert(glGetError()==GL_NO_ERROR);
        if(std::strcmp(mode,"finish")==0)baseline.push_back(image);else{assert(image==baseline.at(frame));++compared;}
        ++frame;completion.submitted();
      }
    }
    printf("PASS GPU completion: %d exact stereo floor/body pixel comparisons, opaque/masked/blended geometry, lateral viewpoints; finish and fence complete before readback\n",compared);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
