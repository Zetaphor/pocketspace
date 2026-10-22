#pragma once
#include <array>
#include <string>
#include <openvr.h>
#include "tracking_state.h"

namespace controller_input {
enum class Control : unsigned {
  GripClick, GripTouch, GripValue, TriggerClick, TriggerTouch, TriggerValue,
  Thumbstick, ThumbstickClick, ThumbstickTouch, BumperClick, BumperTouch,
  SystemClick, SystemTouch, MenuClick, ViewClick, AClick, BClick, XClick,
  YClick, DpadUpClick, DpadDownClick, DpadLeftClick, DpadRightClick, Count
};
struct Value {bool active=false, down=false;float x=0,y=0;};
struct Hand {bool valid=false;std::array<Value,static_cast<unsigned>(Control::Count)> values{};};
struct EyeState {
  bool configured=false,active=false,valid=false,tracked=false,suppressed=false;
  uint32_t scenePid=0;int error=0;const char *status="notRequested";
  float origin[3]{},target[3]{};
  tracking::Clock::time_point polledAt{};
};
struct FoveationState {
  bool available=false,valid=false;float left[2]{},right[2]{};
  tracking::Clock::time_point polledAt{};
};
class Input {
public:
  bool init(const std::string &root,const std::string &executable);
  void poll(bool requestEye=false,bool requestFoveation=false);
  const EyeState &eye() const {return eye_;}
  const FoveationState &foveation() const {return foveation_;}
  const Hand &hand(int index) const {return hands_[index];}
  bool pulse(int hand,float seconds,float amplitude);
private:
  vr::VRActionSetHandle_t set_=vr::k_ulInvalidActionSetHandle;
  vr::VRActionHandle_t haptic_=vr::k_ulInvalidActionHandle;
  std::array<vr::VRActionHandle_t,static_cast<unsigned>(Control::Count)> actions_{};
  vr::VRInputValueHandle_t sources_[2]={vr::k_ulInvalidInputValueHandle,vr::k_ulInvalidInputValueHandle};
  Hand hands_[2]{};
  vr::VRActionSetHandle_t eyeSet_=vr::k_ulInvalidActionSetHandle;
  vr::VRActionHandle_t gaze_=vr::k_ulInvalidActionHandle;
  EyeState eye_;FoveationState foveation_;
  bool ready_=false;
};
constexpr unsigned index(Control c){return static_cast<unsigned>(c);}
}
