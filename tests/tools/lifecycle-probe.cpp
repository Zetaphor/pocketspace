// Read-only overlay inventory; does not bind actions or claim scene focus.
#include <openvr.h>
#include <cstdio>
#include <string>
int main(int argc,char **argv){
 vr::EVRInitError error;vr::VR_Init(&error,vr::VRApplication_Background);
 if(error!=vr::VRInitError_None){fprintf(stderr,"VR_Init: %d\n",int(error));return 2;}
 printf("scenePid=%u\n",vr::VRApplications()->GetCurrentSceneProcessId());
 for(int i=1;i<argc;i++){
  vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
  const auto result=vr::VROverlay()->FindOverlay(argv[i],&handle);
  printf("%s=%s\n",argv[i],result==vr::VROverlayError_None?"present":"absent");
 }
 vr::VR_Shutdown();
}
