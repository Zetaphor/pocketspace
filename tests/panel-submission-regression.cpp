#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Backend {
  enum Operation{None,Texture,Transform,Show,Hide} failure=None;
  int textures=0,transforms=0,shows=0,hides=0;
  vr::EVROverlayError result(Operation op){if(failure==op){failure=None;return vr::VROverlayError_InvalidHandle;}return vr::VROverlayError_None;}
  vr::EVROverlayError SetOverlayTexture(vr::VROverlayHandle_t,const vr::Texture_t *texture){++textures;CHECK(texture->eType==vr::TextureType_OpenGL);return result(Texture);}
  vr::EVROverlayError SetOverlayTransformAbsolute(vr::VROverlayHandle_t,vr::ETrackingUniverseOrigin origin,const vr::HmdMatrix34_t*){++transforms;CHECK(origin==vr::TrackingUniverseStanding);return result(Transform);}
  vr::EVROverlayError ShowOverlay(vr::VROverlayHandle_t){++shows;return result(Show);}
  vr::EVROverlayError HideOverlay(vr::VROverlayHandle_t){++hides;return result(Hide);}
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
int main(){placement();caching();retries();pixels();puts("PASS panel submissions: placement modes, changed content, visibility, handle/texture reuse, error retries and uploaded pixels");}
