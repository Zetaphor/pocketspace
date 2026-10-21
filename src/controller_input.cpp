#include "controller_input.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <unistd.h>

namespace controller_input {
namespace {
struct Definition {const char *name;int type;}; // 0 digital, 1 scalar, 2 vector2
constexpr Definition definitions[]={
  {"grip_click",0},{"grip_touch",0},{"grip_value",1},
  {"trigger_click",0},{"trigger_touch",0},{"trigger_value",1},
  {"stick",2},{"thumbstick_click",0},{"thumbstick_touch",0},
  {"bumper_click",0},{"bumper_touch",0},{"system_click",0},{"system_touch",0},
  {"menu_click",0},{"view_click",0},{"a_click",0},{"b_click",0},
  {"x_click",0},{"y_click",0},{"dpad_up_click",0},{"dpad_down_click",0},
  {"dpad_left_click",0},{"dpad_right_click",0}
};
static_assert(sizeof(definitions)/sizeof(definitions[0])==index(Control::Count));
}
bool Input::init(const std::string &root,const std::string &executable){
  namespace fs=std::filesystem;
  const auto actionPath=fs::absolute(fs::path(root)/"cfg/actions.json").string();
  const auto appPath=(fs::temp_directory_path()/"sf-overlay-engine.vrmanifest").string();
  std::ofstream app(appPath,std::ios::trunc);
  if(!app)return false;
  app<<"{\"source\":\"user\",\"applications\":[{\"app_key\":\"com.sf_overlay_engine.app\",\"launch_type\":\"binary\",\"binary_path_linux_arm\":\""
     <<fs::absolute(executable).string()<<"\",\"action_manifest_path\":\""<<actionPath
     <<"\",\"is_dashboard_overlay\":true,\"strings\":{\"en_us\":{\"name\":\"Steam Frame Overlay Engine\"}}}]}\n";
  app.close();
  auto *apps=vr::VRApplications();auto *input=vr::VRInput();
  if(!apps||!input)return false;
  auto ae=apps->AddApplicationManifest(appPath.c_str(),false);
  auto id=apps->IdentifyApplication(getpid(),"com.sf_overlay_engine.app");
  auto me=input->SetActionManifestPath(actionPath.c_str());
  if(ae!=vr::VRApplicationError_None||id!=vr::VRApplicationError_None||me!=vr::VRInputError_None){
    fprintf(stderr,"[input] registration failed: app=%d identify=%d actions=%d\n",int(ae),int(id),int(me));return false;
  }
  if(input->GetActionSetHandle("/actions/hands",&set_)!=vr::VRInputError_None)return false;
  for(unsigned i=0;i<index(Control::Count);i++){
    const std::string path=std::string("/actions/hands/in/")+definitions[i].name;
    if(input->GetActionHandle(path.c_str(),&actions_[i])!=vr::VRInputError_None)actions_[i]=vr::k_ulInvalidActionHandle;
  }
  input->GetActionHandle("/actions/hands/out/haptic",&haptic_);
  for(int h=0;h<2;h++)if(input->GetInputSourceHandle(h?"/user/hand/right":"/user/hand/left",&sources_[h])!=vr::VRInputError_None)return false;
  ready_=true;fprintf(stdout,"[input] SteamVR Frame actions ready\n");return true;
}
void Input::poll(){
  for(auto &h:hands_)h=Hand{};
  if(!ready_)return;
  auto *input=vr::VRInput();
  vr::VRActiveActionSet_t active{};active.ulActionSet=set_;active.ulRestrictedToDevice=vr::k_ulInvalidInputValueHandle;
  active.nPriority=0;
  auto updateError=input->UpdateActionState(&active,sizeof(active),1);
  if(updateError!=vr::VRInputError_None){
    static vr::EVRInputError lastError=vr::VRInputError_None;
    if(updateError!=lastError){fprintf(stderr,"[input] UpdateActionState error=%d\n",int(updateError));lastError=updateError;}
    return;
  }
  for(int h=0;h<2;h++){
    for(unsigned i=0;i<index(Control::Count);i++){
      auto &v=hands_[h].values[i];
      if(actions_[i]==vr::k_ulInvalidActionHandle)continue;
      if(definitions[i].type==0){vr::InputDigitalActionData_t data{};
        const auto error=input->GetDigitalActionData(actions_[i],&data,sizeof(data),sources_[h]);
        if(error==vr::VRInputError_None){v.active=data.bActive;v.down=data.bState;}
        else if(h==0&&i==0)fprintf(stderr,"[input] GetDigitalActionData error=%d\n",int(error));
      }else {vr::InputAnalogActionData_t data{};
        const auto error=input->GetAnalogActionData(actions_[i],&data,sizeof(data),sources_[h]);
        if(error==vr::VRInputError_None){v.active=data.bActive;v.x=data.x;v.y=data.y;}
        else if(h==0&&i==0)fprintf(stderr,"[input] GetAnalogActionData error=%d\n",int(error));
      }
      hands_[h].valid|=v.active;
    }
  }
}
bool Input::pulse(int hand,float seconds,float amplitude){
  if(!ready_||hand<0||hand>1||haptic_==vr::k_ulInvalidActionHandle)return false;
  return vr::VRInput()->TriggerHapticVibrationAction(haptic_,0,seconds,150,amplitude,sources_[hand])==vr::VRInputError_None;
}
}
