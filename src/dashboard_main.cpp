// pocketspace-dashboard: the Pocketspace tab in the SteamVR dashboard.
// It attaches only to a SteamVR session that is already running, talks to the engine
// over its control socket while the tab is visible, and sleeps while it is hidden.
// See docs/dashboard.md.
#include "dashboard.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openvr.h>
#include <csignal>
#include <fstream>
#include <sys/wait.h>
#include <thread>

namespace {
volatile sig_atomic_t stopping=0;
void onSignal(int){stopping=1;}

constexpr const char *appKey="com.pocketspace.dashboard";

// Asks systemd to start or stop the engine's user service without waiting for it.
std::string engineService(const char *action){
  const pid_t child=fork();
  if(child==0){
    const int null=::open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDOUT_FILENO);dup2(null,STDERR_FILENO);::close(null);}
    execlp("systemctl","systemctl","--user",action,"--no-block","pocketspace.service",nullptr);_exit(127);
  }
  int status=0;
  if(child<0||waitpid(child,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)
    return std::string("systemctl --user ")+action+" pocketspace.service failed; is the service installed (tools/install-user-service.sh)?";
  return {};
}

// A GLES context with no window, as the engine uses, for the tab's texture.
struct Gl {
  EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;GLuint texture=0;
  bool start(){
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(display==EGL_NO_DISPLAY||!eglInitialize(display,nullptr,nullptr))return false;
    const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;
    if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)return false;
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surfaceAttrs[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
    if(context==EGL_NO_CONTEXT||surface==EGL_NO_SURFACE||!eglMakeCurrent(display,surface,surface,context))return false;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return texture!=0;
  }
  // Replaces the texture's pixels and waits, so SteamVR copies a finished image.
  void upload(const Canvas &canvas){
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,canvas.width,canvas.height,0,GL_RGBA,GL_UNSIGNED_BYTE,canvas.rgba.data());
    glFinish();
  }
  ~Gl(){
    if(display==EGL_NO_DISPLAY)return;
    if(texture)glDeleteTextures(1,&texture);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
    if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
    eglTerminate(display);
  }
};

// Registers the dashboard with SteamVR so it is listed as an application, with
// auto-launch off: the systemd unit is the only thing that starts it.
void registerApplication(){
  const char *home=std::getenv("HOME");
  std::error_code ec;const auto executable=std::filesystem::read_symlink("/proc/self/exe",ec);
  if(!home||ec){std::fprintf(stderr,"[dashboard] cannot register with SteamVR: no HOME or executable path\n");return;}
  const auto directory=std::filesystem::path(home)/".local/share/pocketspace";
  std::filesystem::create_directories(directory,ec);
  const auto manifest=directory/"pocketspace-dashboard.vrmanifest";
  {
    std::ofstream out(manifest,std::ios::trunc);
    out<<"{\"source\":\"user\",\"applications\":[{\"app_key\":"<<control::quote(appKey)<<",\"launch_type\":\"binary\","
       <<"\"binary_path_linux\":"<<control::quote(executable.string())<<",\"binary_path_linux_arm\":"<<control::quote(executable.string())<<","
       <<"\"is_dashboard_overlay\":true,\"strings\":{\"en_us\":{\"name\":\"Pocketspace\",\"description\":\"Start the engine and manage scripts\"}}}]}\n";
    if(!out){std::fprintf(stderr,"[dashboard] cannot write %s\n",manifest.c_str());return;}
  }
  auto *apps=vr::VRApplications();
  const auto added=apps->AddApplicationManifest(manifest.c_str(),false);
  const auto launch=apps->SetApplicationAutoLaunch(appKey,false);
  const auto identified=apps->IdentifyApplication(getpid(),appKey);
  if(added!=vr::VRApplicationError_None||launch!=vr::VRApplicationError_None||identified!=vr::VRApplicationError_None)
    std::fprintf(stderr,"[dashboard] SteamVR registration incomplete: manifest=%d autolaunch=%d identify=%d\n",int(added),int(launch),int(identified));
}

// The tab's icon: the product initials on the accent color.
void drawThumbnail(Canvas &c){
  canvasClear(c,86,156,255,255);
  const int w=canvasTextWidth(c,"PS",120);
  canvasText(c,(c.width-std::max(0,w))/2,(c.height-150)/2,"PS",22,24,29,255,120);
}
}

int main(int argc,char **argv){
  std::filesystem::path socket=control::defaultSocketPath();
  for(int i=1;i<argc;i++){
    const std::string arg=argv[i];
    if(arg=="--control-socket"&&i+1<argc)socket=argv[++i];
    else if(arg=="-h"||arg=="--help"){std::printf("Usage: pocketspace-dashboard [--control-socket PATH]\n");return 0;}
    else{std::fprintf(stderr,"unknown argument %s\n",arg.c_str());return 2;}
  }
  std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);std::signal(SIGPIPE,SIG_IGN);
  if(!control::startJson()||!canvasInitLibrary()){std::fprintf(stderr,"[dashboard] cannot start the JSON parser or font library\n");return 1;}
  // The same display defaults as run-pocketspace.sh gives the engine.
  setenv("DISPLAY",":0",0);setenv("EGL_PLATFORM","x11",0);
  Gl gl;
  if(!gl.start()){std::fprintf(stderr,"[dashboard] cannot create a GLES context\n");return 1;}
  // Background never starts SteamVR; without a running session this fails and exits 3.
  vr::EVRInitError initError=vr::VRInitError_None;
  vr::VR_Init(&initError,vr::VRApplication_Background);
  if(initError!=vr::VRInitError_None){
    std::fprintf(stderr,"[dashboard] SteamVR is not running (%s); exiting\n",vr::VR_GetVRInitErrorAsEnglishDescription(initError));
    return 3;
  }
  registerApplication();
  auto *overlays=vr::VROverlay();
  vr::VROverlayHandle_t tab=vr::k_ulOverlayHandleInvalid,thumbnail=vr::k_ulOverlayHandleInvalid;
  const auto created=overlays->CreateDashboardOverlay("pocketspace.dashboard","Pocketspace",&tab,&thumbnail);
  if(created!=vr::VROverlayError_None){
    std::fprintf(stderr,"[dashboard] cannot create the dashboard tab (%s); another dashboard may be running\n",overlays->GetOverlayErrorNameFromEnum(created));
    vr::VR_Shutdown();return 4;
  }
  overlays->SetOverlayWidthInMeters(tab,2.4f);
  overlays->SetOverlayInputMethod(tab,vr::VROverlayInputMethod_Mouse);
  const vr::HmdVector2_t scale{float(dashboard::width),float(dashboard::height)};
  overlays->SetOverlayMouseScale(tab,&scale);
  // GL textures start at the bottom row; the canvas starts at the top.
  vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(tab,&bounds);
  Canvas icon;canvasInit(icon,256,256,"");drawThumbnail(icon);
  overlays->SetOverlayRaw(thumbnail,icon.rgba.data(),uint32_t(icon.width),uint32_t(icon.height),4);

  dashboard::Dashboard board(socket,{[]{return engineService("start");},[]{return engineService("stop");}});
  Canvas canvas;canvasInit(canvas,dashboard::width,dashboard::height,"");
  auto lastUpload=control::Clock::time_point{};
  board.setVisible(overlays->IsOverlayVisible(tab),control::Clock::now());
  // Draw once now, so the tab never shows an image left by an earlier process.
  board.draw(canvas,control::Clock::now());
  gl.upload(canvas);
  {vr::Texture_t texture{reinterpret_cast<void*>(static_cast<uintptr_t>(gl.texture)),vr::TextureType_OpenGL,vr::ColorSpace_Gamma};overlays->SetOverlayTexture(tab,&texture);}
  std::fprintf(stdout,"[dashboard] tab ready\n");std::fflush(stdout);
  bool running=true;
  while(running&&!stopping){
    vr::VREvent_t event{};
    while(vr::VRSystem()->PollNextEvent(&event,sizeof(event)))
      if(event.eventType==vr::VREvent_Quit){vr::VRSystem()->AcknowledgeQuit_Exiting();running=false;}
    while(overlays->PollNextOverlayEvent(tab,&event,sizeof(event))){
      const auto now=control::Clock::now();
      // Mouse coordinates start at the texture's bottom left. The flipped texture
      // bounds put the canvas's top row there, so y already counts from the top.
      const int x=int(event.data.mouse.x),y=int(event.data.mouse.y);
      switch(event.eventType){
        case vr::VREvent_OverlayShown:board.setVisible(true,now);break;
        case vr::VREvent_OverlayHidden:board.setVisible(false,now);break;
        case vr::VREvent_MouseMove:board.pointerMove(x,y);break;
        case vr::VREvent_MouseButtonDown:if(event.data.mouse.button==vr::VRMouseButton_Left)board.pointerDown(x,y);break;
        case vr::VREvent_MouseButtonUp:if(event.data.mouse.button==vr::VRMouseButton_Left)board.pointerUp(x,y);break;
        case vr::VREvent_FocusLeave:board.pointerLeave();break;
        default:break;
      }
    }
    if(!running)break;
    const auto now=control::Clock::now();
    // Events alone miss a tab that was already open when this overlay was created.
    board.setVisible(overlays->IsOverlayVisible(tab),now);
    board.poll(now);
    if(!board.visible()){std::this_thread::sleep_for(std::chrono::milliseconds(200));continue;}
    // Redraw on change, at most ten times a second.
    if(board.dirty()&&now-lastUpload>=std::chrono::milliseconds(100)){
      board.draw(canvas,now);
      // SetOverlayRaw flickers as SteamVR swaps images; a GL texture is copied whole.
      gl.upload(canvas);
      vr::Texture_t texture{reinterpret_cast<void*>(static_cast<uintptr_t>(gl.texture)),vr::TextureType_OpenGL,vr::ColorSpace_Gamma};
      overlays->SetOverlayTexture(tab,&texture);
      lastUpload=now;
    }
    // Wait for engine output or the next pointer check.
    pollfd p{board.fd(),POLLIN,0};
    if(p.fd>=0)::poll(&p,1,30);else std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  overlays->DestroyOverlay(tab);
  vr::VR_Shutdown();
  control::stopJson();
  std::fprintf(stdout,"[dashboard] exiting\n");
  return 0;
}
