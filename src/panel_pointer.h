#pragma once
#include <cmath>
#include <optional>
#include <vector>

// The engine laser's panel input: ray intersection and one hand's hover/press
// state. Free of OpenVR and scene types so it can be tested without a headset.
namespace panelPointer {
struct Hit {float distance=0,x=0,y=0;};

// transform is an overlay's rigid 3x4 pose: X right, Y up, centered on the panel.
// Returned coordinates are canvas pixels with the origin at the top left.
inline std::optional<Hit> intersect(const float origin[3],const float direction[3],const float transform[3][4],
                                    float widthMeters,float heightMeters,int widthPixels,int heightPixels){
  if(!(widthMeters>0)||!(heightMeters>0))return std::nullopt;
  float facing=0,toPanel=0;
  for(int i=0;i<3;i++){facing+=direction[i]*transform[i][2];toPanel+=(transform[i][3]-origin[i])*transform[i][2];}
  if(std::fabs(facing)<1e-6f)return std::nullopt;
  const float distance=toPanel/facing;
  if(!(distance>=0))return std::nullopt;
  float localX=0,localY=0;
  for(int i=0;i<3;i++){
    const float offset=origin[i]+direction[i]*distance-transform[i][3];
    localX+=offset*transform[i][0];localY+=offset*transform[i][1];
  }
  if(std::fabs(localX)>widthMeters*.5f||std::fabs(localY)>heightMeters*.5f)return std::nullopt;
  return Hit{distance,(localX/widthMeters+.5f)*float(widthPixels),(.5f-localY/heightMeters)*float(heightPixels)};
}

struct Target {int instanceId=0,panelId=0;float x=0,y=0;};
enum class EventType {HoverStart,Hover,HoverEnd,Down,Up};
struct Event {EventType type=EventType::Hover;int instanceId=0,panelId=0;float x=0,y=0;};

class Pointer {
  Target current_{};
  bool pressed_=false,triggerWas_=false;
  void leave(std::vector<Event> &out){
    if(!current_.panelId)return;
    if(pressed_)out.push_back({EventType::Up,current_.instanceId,current_.panelId,current_.x,current_.y});
    out.push_back({EventType::HoverEnd,current_.instanceId,current_.panelId,current_.x,current_.y});
    current_={};pressed_=false;
  }
public:
  bool pressed()const{return pressed_;}
  const Target &current()const{return current_;}
  // A press stays on its panel until release, even if the ray leaves that panel.
  void step(const std::optional<Target> &target,bool trigger,std::vector<Event> &out){
    if(pressed_&&!trigger){
      out.push_back({EventType::Up,current_.instanceId,current_.panelId,current_.x,current_.y});
      pressed_=false;
    }
    const bool sameTarget=target&&current_.panelId&&target->instanceId==current_.instanceId&&target->panelId==current_.panelId;
    if(sameTarget){
      if(std::fabs(target->x-current_.x)>=.5f||std::fabs(target->y-current_.y)>=.5f){
        current_=*target;
        out.push_back({EventType::Hover,current_.instanceId,current_.panelId,current_.x,current_.y});
      }
    }else if(!pressed_){
      leave(out);
      if(target){
        current_=*target;
        out.push_back({EventType::HoverStart,current_.instanceId,current_.panelId,current_.x,current_.y});
        out.push_back({EventType::Hover,current_.instanceId,current_.panelId,current_.x,current_.y});
      }
    }
    // A trigger already held when the ray arrives is not a click.
    if(trigger&&!triggerWas_&&current_.panelId&&!pressed_){
      pressed_=true;
      out.push_back({EventType::Down,current_.instanceId,current_.panelId,current_.x,current_.y});
    }
    triggerWas_=trigger;
  }
  // The panel was removed, hidden or made noninteractive while its script keeps running.
  void drop(std::vector<Event> &out){leave(out);}
  // The owner stopped or reloaded, so nothing is left to receive events.
  void forgetInstance(int instanceId){if(current_.instanceId==instanceId){current_={};pressed_=false;}}
};
}
