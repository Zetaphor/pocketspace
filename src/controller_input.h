#pragma once
#include <array>
#include <string>
#include <openvr.h>

namespace controller_input {
enum class Control : unsigned {
  GripClick, GripTouch, GripValue, TriggerClick, TriggerTouch, TriggerValue,
  Thumbstick, ThumbstickClick, ThumbstickTouch, BumperClick, BumperTouch,
  SystemClick, SystemTouch, MenuClick, ViewClick, AClick, BClick, XClick,
  YClick, DpadUpClick, DpadDownClick, DpadLeftClick, DpadRightClick, Count
};
struct Value {bool active=false, down=false;float x=0,y=0;};
struct Hand {bool valid=false;std::array<Value,static_cast<unsigned>(Control::Count)> values{};};
class Input {
public:
  bool init(const std::string &root,const std::string &executable);
  void poll();
  const Hand &hand(int index) const {return hands_[index];}
  bool pulse(int hand,float seconds,float amplitude);
private:
  vr::VRActionSetHandle_t set_=vr::k_ulInvalidActionSetHandle;
  vr::VRActionHandle_t haptic_=vr::k_ulInvalidActionHandle;
  std::array<vr::VRActionHandle_t,static_cast<unsigned>(Control::Count)> actions_{};
  vr::VRInputValueHandle_t sources_[2]={vr::k_ulInvalidInputValueHandle,vr::k_ulInvalidInputValueHandle};
  Hand hands_[2]{};
  bool ready_=false;
};
constexpr unsigned index(Control c){return static_cast<unsigned>(c);}
}
