// Compile on Deckard against its installed OpenVR SDK. No graphics context needed.
#include <openvr.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
int main(int argc,char **argv){
  const bool requestGaze=argc>1&&std::string(argv[1])=="--gaze-action";
  vr::EVRInitError error;vr::VR_Init(&error,vr::VRApplication_Overlay);
  if(error!=vr::VRInitError_None){fprintf(stderr,"init=%d\n",int(error));return 1;}
  auto *input=vr::VRInput();auto *system=vr::VRSystem();
  const auto manifest=std::filesystem::absolute("cfg/tracking-probe/actions.json").string();
  const auto manifestError=input->SetActionManifestPath(manifest.c_str());
  printf("manifest=%d SDK=%s system=%s scene_pid=%u\n",int(manifestError),vr::IVRInput_Version,vr::IVRSystem_Version,vr::VRApplications()->GetCurrentSceneProcessId());
  if(manifestError!=vr::VRInputError_None){vr::VR_Shutdown();return 2;}
  vr::VRActionSetHandle_t fingerSet=0,eyeSet=0;vr::VRActionHandle_t hands[2]{},gaze=0;
  input->GetActionSetHandle("/actions/fingers",&fingerSet);input->GetActionSetHandle("/actions/eyes",&eyeSet);
  input->GetActionHandle("/actions/fingers/in/left",&hands[0]);input->GetActionHandle("/actions/fingers/in/right",&hands[1]);
  input->GetActionHandle("/actions/eyes/in/gaze",&gaze);
  int foveationValid=0,gazeValid=0,skeletonActive[2]{},summaryValid[2]{},bonesValid[2]{};
  const auto start=std::chrono::steady_clock::now();
  for(int frame=0;frame<450;frame++){
    vr::HmdVector2_t left{},right{};
    const bool foveation=system->GetEyeTrackedFoveationCenter(&left,&right);foveationValid+=foveation;
    // Avoid the action path during any scene app: installed Frametop reports focus side effects.
    const bool readGaze=requestGaze&&vr::VRApplications()->GetCurrentSceneProcessId()==0;
    std::array<vr::VRActiveActionSet_t,2> active{};
    active[0].ulActionSet=fingerSet;active[1].ulActionSet=eyeSet;
    for(auto &set:active)set.nPriority=vr::k_nActionSetOverlayGlobalPriorityMin;
    const auto update=input->UpdateActionState(active.data(),sizeof(active[0]),readGaze?2:1);
    vr::VREyeTrackingData_t eye{};vr::EVRInputError eyeError=vr::VRInputError_NoData;
    if(readGaze)eyeError=input->GetEyeTrackingDataRelativeToNow(gaze,vr::TrackingUniverseStanding,0,&eye,sizeof(eye));
    gazeValid+=readGaze&&eyeError==vr::VRInputError_None&&eye.bActive&&eye.bValid;
    if(frame%90==0)printf("frame=%d update=%d foveation=%d left_ndc=%.3f,%.3f right_ndc=%.3f,%.3f gaze_requested=%d gaze_polled=%d gaze_error=%d active=%d valid=%d tracked=%d\n",frame,int(update),foveation,left.v[0],left.v[1],right.v[0],right.v[1],requestGaze,readGaze,int(eyeError),eye.bActive,eye.bValid,eye.bTracked);
    for(int h=0;h<2;h++){
      vr::InputSkeletalActionData_t data{};vr::VRSkeletalSummaryData_t summary{};uint32_t count=0;
      vr::EVRSkeletalTrackingLevel level=vr::VRSkeletalTracking_Estimated;
      const auto stateError=input->GetSkeletalActionData(hands[h],&data,sizeof(data));
      const auto summaryError=input->GetSkeletalSummaryData(hands[h],vr::VRSummaryType_FromDevice,&summary);
      const auto countError=input->GetBoneCount(hands[h],&count);
      const auto levelError=input->GetSkeletalTrackingLevel(hands[h],&level);
      vr::EVRInputError boneError=vr::VRInputError_NoData;
      if(countError==vr::VRInputError_None&&count>0&&count<=128){
        std::vector<vr::VRBoneTransform_t> bones(count);
        boneError=input->GetSkeletalBoneData(hands[h],vr::VRSkeletalTransformSpace_Model,vr::VRSkeletalMotionRange_WithController,bones.data(),count);
      }
      vr::VRSkeletalSummaryData_t animation{};
      const auto animationError=input->GetSkeletalSummaryData(hands[h],vr::VRSummaryType_FromAnimation,&animation);
      if(frame==90){
        std::array<vr::InputBindingInfo_t,8> bindings{};uint32_t returned=0;
        const auto bindingError=input->GetActionBindingInfo(hands[h],bindings.data(),sizeof(bindings[0]),bindings.size(),&returned);
        printf("hand=%s animation_error=%d binding_error=%d bindings=%u\n",h?"right":"left",int(animationError),int(bindingError),returned);
        for(uint32_t i=0;i<returned&&i<bindings.size();i++)printf("binding device=%s input=%s mode=%s\n",bindings[i].rchDevicePathName,bindings[i].rchInputPathName,bindings[i].rchModeName);
      }
      skeletonActive[h]+=stateError==vr::VRInputError_None&&data.bActive;
      summaryValid[h]+=data.bActive&&summaryError==vr::VRInputError_None;
      bonesValid[h]+=data.bActive&&boneError==vr::VRInputError_None;
      if(frame%90==0)printf("hand=%s state=%d active=%d summary=%d level_error=%d level=%d count_error=%d bones=%u bone_error=%d curl=%.3f,%.3f,%.3f,%.3f,%.3f\n",h?"right":"left",int(stateError),data.bActive,int(summaryError),int(levelError),int(level),int(countError),count,int(boneError),summary.flFingerCurl[0],summary.flFingerCurl[1],summary.flFingerCurl[2],summary.flFingerCurl[3],summary.flFingerCurl[4]);
    }
    std::this_thread::sleep_until(start+std::chrono::microseconds((frame+1)*11111));
  }
  printf("RESULT frames=450 foveation_valid=%d gaze_valid=%d skeleton_active=%d,%d summary_valid=%d,%d bones_valid=%d,%d\n",foveationValid,gazeValid,skeletonActive[0],skeletonActive[1],summaryValid[0],summaryValid[1],bonesValid[0],bonesValid[1]);
  vr::VR_Shutdown();return 0;
}
