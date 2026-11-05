#pragma once
// The dashboard tab without SteamVR: engine connection, state, drawing and clicks.
// pocketspace-dashboard feeds it pointer and visibility changes and submits the canvas.
#include "canvas.h"
#include "control_client.h"
#include <functional>
#include <map>

namespace dashboard {
using control::Clock;
constexpr int width=1280,height=800;

struct Instance {
  long long id=0,generation=0;std::string name,state,lastError,lastReload;bool restartPending=false;
  double uptimeSeconds=0;Clock::time_point received{};
  // From the latest resources sample; nullopt when the engine reports null.
  std::optional<double> heapBytes,callbackMsPerSecond,worstCallbackMs,gpuEstimateBytes;
  std::optional<long long> objects,panels,assets,bodies;
};
// One alert per instance; repeated errors from a restart loop update it.
struct Alert {long long instanceId=0;std::string name,message;int count=1;};
struct Button {int x=0,y=0,w=0,h=0;std::string key,label;bool enabled=true,active=false;std::function<void()> action;};

struct Hooks {
  // Ask the service manager to start or stop the engine; return an error message, or empty.
  std::function<std::string()> startEngine,stopEngine;
};

class Dashboard {
public:
  Dashboard(std::filesystem::path socket,Hooks hooks);
  // While hidden the dashboard holds no connection and does no work.
  void setVisible(bool visible,Clock::time_point now);
  bool visible()const{return visible_;}
  // Connects, reads available replies and events, and refreshes slow-changing state.
  void poll(Clock::time_point now);
  // The socket to wait on while visible, or -1.
  int fd()const{return link_.fd();}
  void pointerMove(int x,int y);
  void pointerDown(int x,int y);
  void pointerUp(int x,int y);
  void pointerLeave();
  // Redraws the whole canvas and the buttons that clicks are tested against.
  void draw(Canvas &canvas,Clock::time_point now);
  bool dirty()const{return dirty_;}
  // State, read by the regression.
  bool online()const{return online_;}
  bool starting()const{return starting_;}
  bool stopping()const{return stopping_;}
  const std::vector<Instance> &instances()const{return instances_;}
  const std::vector<std::string> &catalog()const{return catalog_;}
  const std::vector<Alert> &alerts()const{return alerts_;}
  const std::vector<Button> &buttons()const{return buttons_;}
  const std::string &notice()const{return notice_;}
  bool autoRestart()const{return autoRestart_;}
  bool engineAutostart()const{return engineAutostart_;}
  const std::vector<std::string> &autostart()const{return autostart_;}
private:
  struct Pending {std::string op,key,label;};
  std::filesystem::path socket_;Hooks hooks_;control::Link link_;
  bool visible_=false,online_=false,starting_=false,stopping_=false,dirty_=true,settingsKnown_=false;
  // Stop Engine acts on a second press while armed.
  Clock::time_point nextConnect_{},nextRefresh_{},startDeadline_{},stopDeadline_{},stopArmedUntil_{},noticeUntil_{};
  std::map<std::string,Pending> pending_;
  std::vector<Instance> instances_;std::vector<std::string> catalog_,autostart_;std::vector<Alert> alerts_;
  bool autoRestart_=true,engineAutostart_=false;std::string settingsError_,startError_,notice_;
  // Process totals from the latest resources sample.
  std::optional<double> fps_,cpuPercent_,ramBytes_,vramBytes_,gpuEstimateBytes_,sharedGpuEstimateBytes_;
  std::vector<Button> buttons_;std::string hovered_,pressed_;
  int scriptPage_=0,catalogPage_=0;
  void lost(Clock::time_point now);
  // key marks the button that shows the request as busy; label names it in a failure notice.
  void request(const std::string &op,const std::string &fields,const std::string &key={},const std::string &label={});
  void handle(const std::string &line,Clock::time_point now);
  void reply(const Pending &pending,const control::Json &json,Clock::time_point now);
  void event(const control::Json &json,Clock::time_point now);
  void upsert(const control::Json &instance,Clock::time_point now);
  void resources(const control::Json &sample);
  void setNotice(const std::string &text,Clock::time_point now);
  void writeSettings(std::vector<std::string> autostart,bool autoRestart,bool engineAutostart);
  void startEngine(Clock::time_point now);
  void stopEngine(Clock::time_point now);
  const Button *hit(int x,int y)const;
  bool busy(const std::string &key)const;
  Clock::time_point now_{};
};
}
