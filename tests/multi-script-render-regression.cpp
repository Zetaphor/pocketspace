// Two independent scenes with the same local object ID render offscreen.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"

int main(){
  EGLDisplay display;EGLContext context;EGLSurface surface;
  assert(initEgl(display,context,surface));
  for(int cycle=0;cycle<3;cycle++){
    auto left=std::make_unique<Renderer>();
    auto right=std::make_unique<Renderer>();
    Object leftObject{},rightObject{};
    leftObject.id=rightObject.id=1;
    leftObject.asset=rightObject.asset=3; // Each renderer owns its own Box asset.
    leftObject.scale=rightObject.scale=.4f;
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
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
  puts("PASS multi-script offscreen render: overlapping local IDs, independent pixels and GPU cleanup");
}
