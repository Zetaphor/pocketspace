#pragma once
#include "tracking_state.h"
#include <map>
#include <array>
#include <string>
#include <vector>

namespace frame_input {
struct Component { bool boolean=false; float value=0; bool isBoolean=false; tracking::Clock::time_point updatedAt{}; };
struct Event { std::string type, hand, control; Component component; int objectId=0; float x=0,y=0,dx=0,dy=0,dz=0,dpitch=0,dyaw=0,droll=0; int otherObjectId=0; std::array<float,3> point{},normal{}; float penetration=0,relativeSpeed=0;int jointId=0;float force=0,torque=0; };
struct HandState { bool valid=false; std::map<std::string,Component> raw; tracking::Clock::time_point updatedAt{}; };
class Monitor {
public:
  explicit Monitor(uint16_t port=27062):port_(port){}
  ~Monitor();
  bool start();
  bool started() const { return started_; }
  void poll(tracking::Clock::time_point now=tracking::Clock::now());
  // OpenVR device index, or -1 for an absent/disconnected controller.
  void setTrackedDevice(int hand,int device);
  const HandState &hand(int index) const { return hands_[index]; }
  std::vector<Event> takeEvents();
  std::vector<Event> takeCallbackEvents();
  const std::vector<Event> &events() const { return events_; }
  void addEvent(Event event);
  void clearPhysicsEvents();
  void addOverlayEvent(const std::string &type,int objectId,int hand=-1,float dx=0,float dy=0,float dz=0,float dpitch=0,float dyaw=0,float droll=0);
  void addPanelEvent(const std::string &type,int panelId,int hand,float x,float y,const std::string &button="",bool down=false);
  bool reconnect();
  void stop();
  void resetScene();
  void invalidateHand(int hand);
private:
  int devices_[2]={-1,-1};
  bool refreshRequested_=false;
  tracking::Clock::time_point subscribedAt_{};
  uint16_t port_;
  int fd_=-1; std::string buffer_,fragment_; bool started_=false;
  HandState hands_[2]; std::vector<Event> events_,callbackEvents_;
  void disconnect();
  bool sendFrame(unsigned char opcode,const std::string &payload);
  void processMessage(const std::string &message);
};
}
