#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Backend {
  enum Operation{None,Texture,Transform,Show,Hide} failure=None;
  vr::VROverlayHandle_t failureHandle=0;
  int textures=0,transforms=0,shows=0,hides=0,widths=0;bool shared=false,hiddenWrites=false;std::set<vr::VROverlayHandle_t> visible;
  vr::EVROverlayError result(Operation op,vr::VROverlayHandle_t handle=0){if(failure==op&&(!failureHandle||failureHandle==handle)){failure=None;return vr::VROverlayError_InvalidHandle;}return vr::VROverlayError_None;}
  vr::EVROverlayError SetOverlayTexture(vr::VROverlayHandle_t handle,const vr::Texture_t *texture){if(hiddenWrites)CHECK(!visible.count(handle));++textures;CHECK(texture->eType==(shared?vr::TextureType_SharedTextureHandle:vr::TextureType_OpenGL));return result(Texture,handle);}
  vr::EVROverlayError SetOverlayTransformAbsolute(vr::VROverlayHandle_t handle,vr::ETrackingUniverseOrigin origin,const vr::HmdMatrix34_t*){if(hiddenWrites)CHECK(!visible.count(handle));++transforms;CHECK(origin==vr::TrackingUniverseStanding);return result(Transform,handle);}
  vr::EVROverlayError SetOverlayWidthInMeters(vr::VROverlayHandle_t handle,float){if(hiddenWrites)CHECK(!visible.count(handle));++widths;return vr::VROverlayError_None;}
  vr::EVROverlayError ShowOverlay(vr::VROverlayHandle_t handle){++shows;auto error=result(Show,handle);if(error==vr::VROverlayError_None)visible.insert(handle);return error;}
  vr::EVROverlayError HideOverlay(vr::VROverlayHandle_t handle){++hides;auto error=result(Hide,handle);if(error==vr::VROverlayError_None)visible.erase(handle);return error;}
};
static vr::HmdMatrix34_t identity(){vr::HmdMatrix34_t m{};for(int i=0;i<3;i++)m.m[i][i]=1;return m;}
static void placement(){
  auto anchor=identity(),head=identity();for(int row=0;row<3;row++){anchor.m[row][3]=float(row+1);head.m[row][3]=float(row+4);}
  Panel panel;panel.x=.1f;panel.y=.2f;panel.z=.3f;
  const auto world=panelTransform(panel,anchor,head);CHECK(world.m[0][3]==1.1f&&world.m[1][3]==2.2f&&world.m[2][3]==2.7f);
  auto moved=head;moved.m[0][3]+=.4f;auto same=panelTransform(panel,anchor,moved);CHECK(!std::memcmp(world.m,same.m,sizeof(world.m)));
  panel.mode="headLocked";auto locked=panelTransform(panel,anchor,head),changed=panelTransform(panel,anchor,moved);CHECK(locked.m[0][3]==4.1f&&locked.m[2][3]==5.7f);CHECK(changed.m[0][3]!=locked.m[0][3]);
  panel.mode="viewerFacing";auto facing=panelTransform(panel,anchor,head),turned=panelTransform(panel,anchor,moved);CHECK(facing.m[0][3]==world.m[0][3]&&facing.m[2][3]==world.m[2][3]);CHECK(std::memcmp(facing.m,turned.m,sizeof(facing.m)));
  panel.mode="arbitrary";const float rotation[9]={0,0,1,0,1,0,-1,0,0};std::copy_n(rotation,9,panel.rotation);auto arbitrary=panelTransform(panel,anchor,head);for(int row=0;row<3;row++)for(int col=0;col<3;col++)CHECK(arbitrary.m[row][col]==rotation[row*3+col]);
}
static void caching(){
  Backend backend;Panel panel;panel.texture=42;panel.textureRevision=1;PanelSubmission cache;auto transform=identity();
  for(int frame=0;frame<900;frame++)CHECK(submitPanel(backend,1,panel,transform,cache)==vr::VROverlayError_None);
  CHECK(backend.textures==1&&backend.transforms==1&&backend.shows==1);
  ++panel.textureRevision;CHECK(submitPanel(backend,1,panel,transform,cache)==vr::VROverlayError_None);CHECK(backend.textures==2&&backend.transforms==1&&backend.shows==1);
  transform.m[0][3]=.5f;submitPanel(backend,1,panel,transform,cache);CHECK(backend.transforms==2&&backend.textures==2);
  panel.visible=false;for(int i=0;i<10;i++)submitPanel(backend,1,panel,transform,cache);CHECK(backend.hides==1&&!cache.shown);
  ++panel.textureRevision;submitPanel(backend,1,panel,transform,cache);CHECK(backend.textures==2);
  panel.visible=true;submitPanel(backend,1,panel,transform,cache);CHECK(backend.textures==3&&backend.shows==2&&backend.transforms==2);
  panel.texture=99;submitPanel(backend,1,panel,transform,cache);CHECK(backend.textures==4);
  panel.texture=0;submitPanel(backend,1,panel,transform,cache);CHECK(!cache.shown&&backend.hides==2);
  panel.texture=99;submitPanel(backend,2,panel,transform,cache);CHECK(backend.textures==5&&backend.transforms==3&&backend.shows==3);
  Backend hud;PanelSubmission hudCache;panel.textureRevision=0;
  for(int frame=0;frame<900;frame++){if(frame%45==0)++panel.textureRevision;submitPanel(hud,3,panel,transform,hudCache);}
  CHECK(hud.textures==20&&hud.transforms==1&&hud.shows==1);
  printf("PASS panel calls over 900 frames: static 3 versus 2700; 2-Hz HUD %d versus 2700\n",hud.textures+hud.transforms+hud.shows);
}
static void retries(){
  for(auto operation:{Backend::Texture,Backend::Transform,Backend::Show}){
    Backend backend;backend.failure=operation;PanelSubmission cache;Panel panel;panel.texture=1;auto transform=identity();
    CHECK(submitPanel(backend,1,panel,transform,cache)!=vr::VROverlayError_None);
    CHECK(submitPanel(backend,1,panel,transform,cache)==vr::VROverlayError_None&&cache.shown);
    CHECK(backend.textures==(operation==Backend::Texture?2:1));CHECK(backend.transforms==(operation==Backend::Transform?2:1));CHECK(backend.shows==(operation==Backend::Show?2:1));
    panel.visible=false;backend.failure=Backend::Hide;
    CHECK(submitPanel(backend,1,panel,transform,cache)!=vr::VROverlayError_None&&cache.shown);
    CHECK(submitPanel(backend,1,panel,transform,cache)==vr::VROverlayError_None&&!cache.shown);CHECK(backend.hides==2);
  }
}
static void surfaceReplacement(){
  Backend backend;backend.shared=true;Surface surface;surface.overlay=2;
  surface.previous=std::make_unique<Surface>();surface.previous->overlay=1;surface.previous->shown=true;
  int retired=0;auto retire=[&](Surface &old){CHECK(surface.shown&&backend.shows==2&&old.overlay==1&&old.shown);retired++;};
  backend.failure=Backend::Texture;
  CHECK(submitSurface(backend,surface,0,retire)!=vr::VROverlayError_None&&surface.previous&&retired==0&&backend.shows==0);
  backend.failure=Backend::Show;
  CHECK(submitSurface(backend,surface,1,retire)!=vr::VROverlayError_None&&surface.previous&&retired==0&&!surface.shown);
  CHECK(submitSurface(backend,surface,2,retire)==vr::VROverlayError_None&&!surface.previous&&retired==1);
  CHECK(submitSurface(backend,surface,3,retire)==vr::VROverlayError_None&&retired==1&&backend.shows==2);
  CHECK(groupBufferPixels(768,4096)==1024&&groupBufferPixels(1000,4096)==1280&&groupBufferPixels(4000,4096)==4096);
  puts("PASS shared surface replacement: old overlay retained on texture/show failures, retired only after successful show, bounded allocation headroom");
}
static void stagedPresentation(){
  Backend backend;backend.shared=backend.hiddenWrites=true;backend.visible.insert(1);
  Surface surface;surface.overlay=1;surface.stagingOverlay=2;surface.shown=true;
  auto transform=identity();auto retire=[](Surface &){CHECK(false);};
  CHECK(prepareSurface(backend,surface,1,transform)==vr::VROverlayError_None);
  CHECK(backend.visible==std::set<vr::VROverlayHandle_t>{1});
  backend.failure=Backend::Texture;
  CHECK(submitSurface(backend,surface,0,retire)!=vr::VROverlayError_None&&surface.overlay==1&&backend.visible.size()==1);
  backend.failure=Backend::Show;
  CHECK(submitSurface(backend,surface,1,retire)!=vr::VROverlayError_None&&surface.overlay==1&&backend.visible.size()==1);
  CHECK(submitSurface(backend,surface,2,retire)==vr::VROverlayError_None&&surface.overlay==2&&surface.stagingOverlay==1);
  CHECK(backend.visible==std::set<vr::VROverlayHandle_t>{2});
  transform.m[0][3]=.2f;
  CHECK(prepareSurface(backend,surface,1,transform)==vr::VROverlayError_None&&backend.widths==2);
  backend.failure=Backend::Hide;
  CHECK(submitSurface(backend,surface,3,retire)!=vr::VROverlayError_None&&surface.overlay==2&&surface.stagingShown&&backend.visible.size()==2);
  backend.failure=Backend::Hide;const int transforms=backend.transforms;
  CHECK(prepareSurface(backend,surface,1,transform)!=vr::VROverlayError_None&&backend.transforms==transforms);
  CHECK(prepareSurface(backend,surface,1,transform)==vr::VROverlayError_None&&!surface.stagingShown&&backend.visible.size()==1);
  CHECK(submitSurface(backend,surface,4,retire)==vr::VROverlayError_None&&surface.overlay==1&&backend.visible.size()==1);
  backend.failure=Backend::Hide;
  CHECK(hideSurface(backend,surface)!=vr::VROverlayError_None&&surface.shown&&backend.visible.size()==1);
  CHECK(hideSurface(backend,surface)==vr::VROverlayError_None&&!surface.shown&&backend.visible.empty());
  puts("PASS staged grouped presentation: visible pose/width/texture never mutated; submit/show/hide failures retain a coherent visible frame and recover");
}
static void splitPresentation(){
  for(auto operation:{Backend::Texture,Backend::Show})for(auto failingHandle:{4,6}){
    Backend backend;backend.shared=backend.hiddenWrites=true;backend.visible.insert(1);
    Surface parent,child;parent.overlay=3;parent.stagingOverlay=4;parent.previous=std::make_unique<Surface>();parent.previous->overlay=1;parent.previous->shown=true;
    child.overlay=5;child.stagingOverlay=6;
    int retired=0;auto retire=[&](Surface &old){CHECK(parent.shown&&child.shown&&old.overlay==1&&backend.visible.count(child.overlay));backend.visible.erase(old.overlay);retired++;};
    auto transform=identity();CHECK(prepareSurface(backend,parent,1,transform)==vr::VROverlayError_None);CHECK(prepareSurface(backend,child,1,transform)==vr::VROverlayError_None);
    backend.failure=operation;backend.failureHandle=failingHandle;
    CHECK(submitGroupSplit(backend,parent,child,0,retire)!=vr::VROverlayError_None);
    CHECK(parent.previous&&retired==0&&!parent.shown&&!child.shown&&backend.visible==std::set<vr::VROverlayHandle_t>{1});
    CHECK(prepareSurface(backend,parent,1,transform)==vr::VROverlayError_None);CHECK(prepareSurface(backend,child,1,transform)==vr::VROverlayError_None);
    CHECK(submitGroupSplit(backend,parent,child,1,retire)==vr::VROverlayError_None);
    CHECK(!parent.previous&&parent.shown&&child.shown&&retired==1&&backend.visible.size()==2&&!backend.visible.count(1));
  }
  for(bool parentVisible:{false,true})for(bool childVisible:{false,true}){
    Backend backend;backend.shared=true;
    Surface parent,child;parent.overlay=3;parent.stagingOverlay=4;parent.previous=std::make_unique<Surface>();parent.previous->overlay=1;parent.previous->shown=parentVisible;
    child.overlay=5;child.stagingOverlay=6;if(parentVisible)backend.visible.insert(1);
    int retired=0;auto retire=[&](Surface &old){CHECK(!parentVisible||parent.shown);CHECK(!childVisible||child.shown);backend.visible.erase(old.overlay);retired++;};
    CHECK(submitGroupSplit(backend,parent,child,0,retire,parentVisible,childVisible)==vr::VROverlayError_None);
    CHECK(!parent.previous&&retired==1&&parent.shown==parentVisible&&child.shown==childVisible);
    CHECK(backend.visible.size()==size_t(parentVisible)+size_t(childVisible));
  }
  struct StickyChild:Backend {
    bool sticky=true;
    vr::EVROverlayError HideOverlay(vr::VROverlayHandle_t handle){if(sticky&&handle==6)return vr::VROverlayError_InvalidHandle;return Backend::HideOverlay(handle);}
  } backend;
  backend.shared=backend.hiddenWrites=true;backend.visible.insert(1);
  Surface parent,child;parent.overlay=3;parent.stagingOverlay=4;parent.previous=std::make_unique<Surface>();parent.previous->overlay=1;parent.previous->shown=true;child.overlay=5;child.stagingOverlay=6;
  auto retire=[&](Surface &old){CHECK(parent.shown&&child.shown);backend.visible.erase(old.overlay);};
  backend.failure=Backend::Show;backend.failureHandle=4;
  CHECK(submitGroupSplit(backend,parent,child,0,retire)!=vr::VROverlayError_None&&parent.previous&&child.shown);
  CHECK(backend.visible==std::set<vr::VROverlayHandle_t>({1,6}));
  // The host gates all pending-child writes on this hide succeeding.
  CHECK(hideSurface(backend,child)!=vr::VROverlayError_None&&child.shown&&parent.previous);
  backend.sticky=false;CHECK(hideSurface(backend,child)==vr::VROverlayError_None&&!child.shown);
  CHECK(submitGroupSplit(backend,parent,child,1,retire)==vr::VROverlayError_None&&!parent.previous);
  puts("PASS split presentation: complete parent retained on either tile texture/show failure, child rollback, hidden writes, retry and two-tile retirement");
}
static void pixels(){
  EGLDisplay display;EGLContext context;EGLSurface surface;CHECK(initEgl(display,context,surface));
  Panel panel;CHECK(panelInit(panel,1,8,8,.1f,.1f,""));panelClear(panel,25,50,75,255);panelUpload(panel);CHECK(panel.textureRevision==1&&!panel.dirty);auto texture=panel.texture;
  GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,panel.texture,0);CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  unsigned char rgba[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);CHECK(rgba[0]==25&&rgba[1]==50&&rgba[2]==75&&rgba[3]==255);
  panelClear(panel,90,70,50,255);panelUpload(panel);CHECK(panel.texture==texture&&panel.textureRevision==2&&!panel.dirty);
  glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);CHECK(rgba[0]==90&&rgba[1]==70&&rgba[2]==50&&rgba[3]==255);CHECK(glGetError()==GL_NO_ERROR);
  glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);panelDestroy(panel);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
int main(){splitPresentation();stagedPresentation();surfaceReplacement();placement();caching();retries();pixels();puts("PASS panel submissions: placement modes, changed content, visibility, handle/texture reuse, error retries and uploaded pixels");}
