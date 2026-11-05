// pocketspace-dashboard: the Pocketspace tab in the SteamVR dashboard.
// It attaches only to a SteamVR session that is already running, talks to the engine
// over its control socket while the tab is visible, and sleeps while it is hidden.
// See docs/dashboard.md.
#include "dashboard.h"
#include <openvr.h>
#include <csignal>
#include <sys/wait.h>
#include <thread>

namespace {
volatile sig_atomic_t stopping=0;
void onSignal(int){stopping=1;}

// Asks systemd to start the engine's user service without waiting for it.
std::string startEngineService(){
  const pid_t child=fork();
  if(child==0){
    const int null=::open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDOUT_FILENO);dup2(null,STDERR_FILENO);::close(null);}
    execlp("systemctl","systemctl","--user","start","--no-block","pocketspace.service",nullptr);_exit(127);
  }
  int status=0;
  if(child<0||waitpid(child,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)
    return "systemctl --user start pocketspace.service failed; is the service installed (tools/install-user-service.sh)?";
  return {};
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
  // Background never starts SteamVR; without a running session this fails and exits 3.
  vr::EVRInitError initError=vr::VRInitError_None;
  vr::VR_Init(&initError,vr::VRApplication_Background);
  if(initError!=vr::VRInitError_None){
    std::fprintf(stderr,"[dashboard] SteamVR is not running (%s); exiting\n",vr::VR_GetVRInitErrorAsEnglishDescription(initError));
    return 3;
  }
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
  Canvas icon;canvasInit(icon,256,256,"");drawThumbnail(icon);
  overlays->SetOverlayRaw(thumbnail,icon.rgba.data(),uint32_t(icon.width),uint32_t(icon.height),4);

  dashboard::Dashboard board(socket,{startEngineService});
  Canvas canvas;canvasInit(canvas,dashboard::width,dashboard::height,"");
  auto lastUpload=control::Clock::time_point{};
  board.setVisible(overlays->IsOverlayVisible(tab),control::Clock::now());
  std::fprintf(stdout,"[dashboard] tab ready\n");std::fflush(stdout);
  bool running=true;
  while(running&&!stopping){
    vr::VREvent_t event{};
    while(vr::VRSystem()->PollNextEvent(&event,sizeof(event)))
      if(event.eventType==vr::VREvent_Quit){vr::VRSystem()->AcknowledgeQuit_Exiting();running=false;}
    while(overlays->PollNextOverlayEvent(tab,&event,sizeof(event))){
      const auto now=control::Clock::now();
      // Mouse coordinates have their origin at the bottom left.
      const int x=int(event.data.mouse.x),y=dashboard::height-1-int(event.data.mouse.y);
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
    board.poll(now);
    if(!board.visible()){std::this_thread::sleep_for(std::chrono::milliseconds(200));continue;}
    // Redraw on change, at most ten times a second.
    if(board.dirty()&&now-lastUpload>=std::chrono::milliseconds(100)){
      board.draw(canvas,now);
      overlays->SetOverlayRaw(tab,canvas.rgba.data(),uint32_t(canvas.width),uint32_t(canvas.height),4);
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
