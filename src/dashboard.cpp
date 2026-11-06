#include "dashboard.h"
#include <algorithm>
#include <cmath>
#include <utility>
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

namespace dashboard {
using control::Json;using control::quote;
namespace {
constexpr std::chrono::milliseconds reconnectInterval{1000},startingReconnect{500},refreshInterval{10000},startTimeout{30000},noticeTime{10000},
  stopConfirmTime{3000},stopTimeout{10000};
constexpr size_t maxAlerts=8;

// Neutral near-black surfaces, with the Pocketspace logo's lavender #b47ff7 and
// cyan #0cf8fd as accents.
struct Color {unsigned char r,g,b;};
constexpr Color railColor{10,10,11},background{16,16,18},card{27,27,30},cardHover{38,38,42},border{48,48,53},
  text{238,238,242},muted{160,160,168},faint{106,106,114},accent{180,127,247},accentHover{198,156,250},
  onAccent{16,16,18},cyan{12,248,253},danger{255,110,140},warn{255,200,110};

// Measures and draws text in one of the dashboard's fonts.
struct Type {
  const std::string &font;int pixels;float tracking=0;
  int width(const std::string &s)const{return std::max(0,canvasTextWidthFont(font,s,pixels,tracking));}
  // Cuts text to fit a width, ending in an ellipsis when shortened.
  std::string fit(std::string s,int room)const{
    for(auto &ch:s)if(ch=='\n'||ch=='\r'||ch=='\t')ch=' ';
    if(width(s)<=room)return s;
    while(!s.empty()){
      do s.pop_back();while(!s.empty()&&((unsigned char)s.back()&0xc0)==0x80);
      if(width(s+"\u2026")<=room)return s+"\u2026";
    }
    return s;
  }
  void draw(Canvas &c,int x,int y,const std::string &s,Color k)const{canvasTextFont(c,font,x,y,s,k.r,k.g,k.b,255,pixels,tracking);}
  void center(Canvas &c,int cx,int y,const std::string &s,Color k)const{draw(c,cx-width(s)/2,y,s,k);}
  void right(Canvas &c,int rx,int y,const std::string &s,Color k)const{draw(c,rx-width(s),y,s,k);}
};

void panel(Canvas &c,int x,int y,int w,int h,int radius,Color fill){
  canvasFillRoundRect(c,x,y,w,h,radius,border.r,border.g,border.b,255);
  canvasFillRoundRect(c,x+1,y+1,w-2,h-2,radius-1,fill.r,fill.g,fill.b,255);
}
std::string clock(double seconds){
  char out[32];const long s=std::max(0L,long(seconds));
  std::snprintf(out,sizeof(out),"%02ld:%02ld:%02ld",s/3600,s/60%60,s%60);
  return out;
}

std::string bytes(std::optional<double> b){
  if(!b)return "-";
  char out[32];
  // Three significant figures at most, so values fit narrow columns.
  const auto scaled=[&](double v,const char *unit){std::snprintf(out,sizeof(out),v>=100?"%.0f %s":v>=10?"%.1f %s":"%.2f %s",v,unit);};
  if(*b>=1073741824)scaled(*b/1073741824,"GiB");
  else if(*b>=1048576)scaled(*b/1048576,"MiB");
  else if(*b>=1024)scaled(*b/1024,"KiB");
  else std::snprintf(out,sizeof(out),"%.0f B",*b);
  return out;
}
std::string decimal(std::optional<double> v,const char *format){if(!v)return "-";char out[32];std::snprintf(out,sizeof(out),format,*v);return out;}
std::string count(std::optional<long long> v){return v?std::to_string(*v):"-";}
std::string uptime(double seconds){
  char out[32];const long s=long(seconds);
  if(s>=3600)std::snprintf(out,sizeof(out),"%ldh%02ldm",s/3600,s/60%60);
  else if(s>=60)std::snprintf(out,sizeof(out),"%ldm%02lds",s/60,s%60);
  else std::snprintf(out,sizeof(out),"%lds",s);
  return out;
}
std::optional<double> optionalNumber(const Json &v){if(v.isNull())return std::nullopt;return v.number();}
std::optional<long long> optionalCount(const Json &v){if(v.isNull())return std::nullopt;return (long long)v.number();}
std::string list(const std::vector<std::string> &names){std::string out="[";for(size_t i=0;i<names.size();i++)out+=(i?",":"")+quote(names[i]);return out+"]";}
}

Dashboard::Dashboard(std::filesystem::path socket,Hooks hooks,Assets assets):socket_(std::move(socket)),hooks_(std::move(hooks)),fonts_(std::move(assets)){}

void Dashboard::setVisible(bool visible,Clock::time_point now){
  if(visible==visible_)return;
  visible_=visible;dirty_=true;
  if(visible){nextConnect_=now;return;}
  // Hidden: drop the connection. Showing again resynchronizes from scratch.
  link_.close();pending_.clear();hovered_.clear();pressed_.clear();
}

void Dashboard::lost(Clock::time_point now){
  link_.close();pending_.clear();
  if(online_){online_=false;dirty_=true;}
  nextConnect_=now+(starting_?startingReconnect:reconnectInterval);
}

void Dashboard::request(const std::string &op,const std::string &fields,const std::string &key,const std::string &label){
  if(!link_.open())return;
  const std::string id=control::newRequestId("dashboard");
  if(!link_.send("{\"v\":1,\"id\":"+quote(id)+",\"op\":"+quote(op)+fields+"}")){lost(now_);return;}
  pending_[id]={op,key,label};
  if(!key.empty())dirty_=true;
}

bool Dashboard::busy(const std::string &key)const{
  for(const auto &[id,pending]:pending_)if(pending.key==key)return true;
  return false;
}

void Dashboard::setNotice(const std::string &text,Clock::time_point now){notice_=text;noticeUntil_=now+noticeTime;dirty_=true;}

void Dashboard::poll(Clock::time_point now){
  now_=now;
  if(!notice_.empty()&&now>=noticeUntil_){notice_.clear();dirty_=true;}
  if(starting_&&!online_&&now>=startDeadline_){
    starting_=false;startError_="The engine did not answer within 30 s. Check journalctl --user -u pocketspace.service";dirty_=true;
  }
  if(stopArmedUntil_!=Clock::time_point{}&&now>=stopArmedUntil_){stopArmedUntil_={};dirty_=true;}
  if(!resetArmed_.empty()&&now>=resetArmedUntil_){resetArmed_.clear();dirty_=true;}
  if(stopping_&&!online_)stopping_=false;
  if(stopping_&&now>=stopDeadline_){
    stopping_=false;setNotice("The engine did not stop. Is it running outside pocketspace.service?",now);
  }
  if(!visible_)return;
  if(!link_.open()&&now>=nextConnect_){
    if(!link_.connect(socket_)){lost(now);return;}
    online_=true;starting_=false;startError_.clear();dirty_=true;
    // Watch first, so no change between the snapshot and the stream is missed.
    request("watch","");request("list","");request("catalog","");request("settings.get","");request("stats","");
    nextRefresh_=now+refreshInterval;
  }
  if(!link_.open())return;
  // The catalog and settings file can change on disk without an event.
  if(now>=nextRefresh_){request("catalog","");request("settings.get","");nextRefresh_=now+refreshInterval;}
  for(int i=0;i<256&&link_.open();i++){
    // Read only what has arrived; the socket wait uses the real clock.
    bool gone=false;const auto line=link_.line(Clock::now(),gone);
    if(gone){lost(now);break;}
    if(!line)break;
    handle(*line,now);
  }
}

void Dashboard::handle(const std::string &line,Clock::time_point now){
  const auto json=Json::parse(line);
  if(!json)return;
  if(!(*json)["event"].isNull()){event(*json,now);return;}
  const auto found=pending_.find((*json)["id"].str());
  if(found==pending_.end())return;
  const Pending pending=found->second;pending_.erase(found);
  if(!pending.key.empty())dirty_=true;
  reply(pending,*json,now);
}

void Dashboard::reply(const Pending &pending,const Json &json,Clock::time_point now){
  const auto result=json["result"];const auto &op=pending.op;
  if(!json["ok"].truthy()){
    const auto error=json["error"];const auto code=error["code"].str();
    if(!error["instance"].isNull())upsert(error["instance"],now);
    if(op=="list"||op=="catalog"||op=="settings.get"||op=="stats"||op=="watch")return;
    setNotice(pending.label+" failed: "+error["message"].str(),now);
    if(code=="stale_generation"||code=="not_found"||code=="conflict")request("list","");
    if(op=="settings.set")request("settings.get","");
    return;
  }
  if(op=="list"){
    std::vector<Instance> previous;previous.swap(instances_);
    const auto all=result["instances"];
    for(uint32_t i=0;i<all.length();i++)upsert(all[i],now);
    // Keep resource values for instances that are still there.
    for(auto &instance:instances_)for(const auto &old:previous)if(old.id==instance.id&&old.generation==instance.generation){
      instance.heapBytes=old.heapBytes;instance.callbackMsPerSecond=old.callbackMsPerSecond;instance.worstCallbackMs=old.worstCallbackMs;
      instance.gpuEstimateBytes=old.gpuEstimateBytes;instance.objects=old.objects;instance.panels=old.panels;instance.assets=old.assets;instance.bodies=old.bodies;
    }
    dirty_=true;
  }else if(op=="catalog"){
    std::vector<std::string> names;const auto scripts=result["scripts"];
    for(uint32_t i=0;i<scripts.length();i++)names.push_back(scripts[i].str());
    if(names!=catalog_){catalog_=std::move(names);dirty_=true;}
  }else if(op=="settings.get"||op=="settings.set"){
    const auto settings=result["settings"];
    std::vector<std::string> names;const auto auto_=settings["autostart"];
    for(uint32_t i=0;i<auto_.length();i++)names.push_back(auto_[i].str());
    const std::string fileError=op=="settings.get"?result["fileError"].str():std::string();
    const bool restart=settings["autoRestart"].truthy(),engine=settings["engineAutostart"].truthy();
    if(!settingsKnown_||names!=autostart_||restart!=autoRestart_||engine!=engineAutostart_||fileError!=settingsError_)dirty_=true;
    autostart_=std::move(names);autoRestart_=restart;engineAutostart_=engine;settingsError_=fileError;settingsKnown_=true;
  }else if(op=="stats")resources(result);
  else if(op=="start"||op=="stop"||op=="reload"||op=="scratch.reset")upsert(result,now);
  else if(op=="data.reset"){
    if(!result["instance"].isNull())upsert(result["instance"],now);
    const long long removed=(long long)result["removed"].number();
    // The key is data:<script>.
    setNotice("Cleared "+std::to_string(removed)+(removed==1?" saved file for ":" saved files for ")+pending.key.substr(5),now);
  }
}

void Dashboard::event(const Json &json,Clock::time_point now){
  const auto type=json["event"].str();
  if(type=="status")upsert(json["instance"],now);
  else if(type=="resources")resources(json);
  else if(type=="error"){
    const long long id=(long long)json["instanceId"].number();
    std::string name="instance "+std::to_string(id);
    for(const auto &instance:instances_)if(instance.id==id)name=instance.name;
    auto found=std::find_if(alerts_.begin(),alerts_.end(),[&](const Alert &a){return a.instanceId==id;});
    if(found!=alerts_.end()){Alert alert=*found;alerts_.erase(found);alert.message=json["message"].str();alert.count++;alerts_.insert(alerts_.begin(),alert);}
    else{alerts_.insert(alerts_.begin(),Alert{id,name,json["message"].str(),1});if(alerts_.size()>maxAlerts)alerts_.pop_back();}
    dirty_=true;
  }
}

void Dashboard::upsert(const Json &json,Clock::time_point now){
  if(json.isNull())return;
  Instance next;
  next.id=(long long)json["instanceId"].number();next.generation=(long long)json["generation"].number();
  next.name=json["name"].str();next.state=json["state"].str();next.lastError=json["lastError"].str();
  next.lastReload=json["lastReload"].str();next.restartPending=json["restartPending"].truthy();
  next.uptimeSeconds=json["uptimeSeconds"].number();next.received=now;
  auto found=std::find_if(instances_.begin(),instances_.end(),[&](const Instance &i){return i.id==next.id;});
  if(found==instances_.end()){
    instances_.insert(std::upper_bound(instances_.begin(),instances_.end(),next,[](const Instance &a,const Instance &b){return a.id<b.id;}),next);
  }else{
    // A new generation starts a fresh sample window; until then values are unknown.
    if(found->generation==next.generation&&found->state==next.state){
      next.heapBytes=found->heapBytes;next.callbackMsPerSecond=found->callbackMsPerSecond;next.worstCallbackMs=found->worstCallbackMs;
      next.gpuEstimateBytes=found->gpuEstimateBytes;next.objects=found->objects;next.panels=found->panels;next.assets=found->assets;next.bodies=found->bodies;
    }
    *found=next;
  }
  dirty_=true;
}

void Dashboard::resources(const Json &sample){
  fps_=optionalNumber(sample["fps"]);cpuPercent_=optionalNumber(sample["cpuPercent"]);ramBytes_=optionalNumber(sample["ramBytes"]);
  vramBytes_=optionalNumber(sample["vramBytes"]);gpuEstimateBytes_=optionalNumber(sample["gpuEstimateBytes"]);
  sharedGpuEstimateBytes_=optionalNumber(sample["sharedGpuEstimateBytes"]);
  const auto all=sample["instances"];
  for(uint32_t i=0;i<all.length();i++){
    const auto x=all[i];const long long id=(long long)x["instanceId"].number();
    for(auto &instance:instances_)if(instance.id==id&&instance.generation==(long long)x["generation"].number()){
      instance.heapBytes=optionalNumber(x["heapBytes"]);instance.callbackMsPerSecond=optionalNumber(x["callbackMsPerSecond"]);
      instance.worstCallbackMs=optionalNumber(x["worstCallbackMs"]);instance.gpuEstimateBytes=optionalNumber(x["gpuEstimateBytes"]);
      instance.objects=optionalCount(x["objects"]);instance.panels=optionalCount(x["panels"]);instance.assets=optionalCount(x["assets"]);instance.bodies=optionalCount(x["bodies"]);
    }
  }
  dirty_=true;
}

void Dashboard::writeSettings(std::vector<std::string> autostart,bool autoRestart,bool engineAutostart){
  request("settings.set",",\"settings\":{\"version\":1,\"autostart\":"+list(autostart)+",\"autoRestart\":"+(autoRestart?"true":"false")
    +",\"engineAutostart\":"+(engineAutostart?"true":"false")+"}","settings","Changing settings");
}

void Dashboard::startEngine(Clock::time_point now){
  startError_.clear();
  const std::string error=hooks_.startEngine?hooks_.startEngine():std::string("no way to start the engine");
  if(!error.empty()){startError_=error;dirty_=true;return;}
  starting_=true;startDeadline_=now+startTimeout;nextConnect_=now;dirty_=true;
}

void Dashboard::stopEngine(Clock::time_point now){
  stopArmedUntil_={};dirty_=true;
  const std::string error=hooks_.stopEngine?hooks_.stopEngine():std::string("no way to stop the engine");
  if(!error.empty()){setNotice(error,now);return;}
  stopping_=true;stopDeadline_=now+stopTimeout;
}

const Button *Dashboard::hit(int x,int y)const{
  for(const auto &button:buttons_)if(button.enabled&&x>=button.x&&y>=button.y&&x<button.x+button.w&&y<button.y+button.h)return &button;
  return nullptr;
}
void Dashboard::pointerMove(int x,int y){
  const auto *button=hit(x,y);const std::string key=button?button->key:std::string();
  if(key!=hovered_){hovered_=key;dirty_=true;}
}
void Dashboard::pointerDown(int x,int y){pointerMove(x,y);pressed_=hovered_;}
void Dashboard::pointerUp(int x,int y){
  pointerMove(x,y);
  const std::string pressed=std::exchange(pressed_,{});
  if(pressed.empty()||pressed!=hovered_)return;
  // Copy the action: running it can redraw and replace the buttons.
  for(const auto &button:buttons_)if(button.key==pressed){auto action=button.action;if(action)action();dirty_=true;return;}
}
void Dashboard::pointerLeave(){if(!hovered_.empty()){hovered_.clear();dirty_=true;}pressed_.clear();}

void Dashboard::button(Canvas &c,int x,int y,int w,int h,const std::string &key,const std::string &label,Style style,bool enabled,std::function<void()> action){
  const bool working=busy(key);enabled=enabled&&!working;
  const bool hot=enabled&&key==hovered_;
  Color fillColor=card,textColor=text;
  if(style==Style::Primary){fillColor=hot?accentHover:accent;textColor=onAccent;}
  else if(style==Style::Danger){fillColor=danger;textColor=onAccent;}
  else if(hot)fillColor=cardHover;
  if(!enabled){fillColor=card;textColor=faint;}
  if(style==Style::Ghost||!enabled)panel(c,x,y,w,h,h/2,fillColor);
  else canvasFillRoundRect(c,x,y,w,h,h/2,fillColor.r,fillColor.g,fillColor.b,255);
  const Type type{fonts_.semibold,h>=56?20:17};
  const std::string shown=type.fit(working?"…":label,w-24);
  type.center(c,x+w/2,y+(h-type.pixels*5/4)/2,shown,textColor);
  buttons_.push_back({x,y,w,h,key,label,enabled,false,std::move(action)});
}

void Dashboard::region(int x,int y,int w,int h,const std::string &key,const std::string &label,bool enabled,bool active,std::function<void()> action){
  buttons_.push_back({x,y,w,h,key,label,enabled,active,std::move(action)});
}

void Dashboard::toggle(Canvas &c,int x,int y,const std::string &key,bool on,bool enabled,std::function<void()> action){
  enabled=enabled&&!busy(key)&&!busy("settings");
  const int w=60,h=34;
  const Color track=!enabled?border:on?(key==hovered_?accentHover:accent):(key==hovered_?faint:border);
  canvasFillRoundRect(c,x,y,w,h,h/2,track.r,track.g,track.b,255);
  const Color knob=on?onAccent:text;
  canvasFillCircleSmooth(c,on?x+w-h/2.f:x+h/2.f,y+h/2.f,h/2.f-5,knob.r,knob.g,knob.b,enabled?255:120);
  buttons_.push_back({x,y,w,h,key,on?"On":"Off",enabled,on,std::move(action)});
}

void Dashboard::drawIcon(Canvas &c,int x,int y,int size){
  if(iconSize_!=size){
    // Box-filter the 512 px icon down once; a missing file draws nothing.
    icon_.clear();iconSize_=size;
    int w=0,h=0,n=0;
    if(unsigned char *source=stbi_load(fonts_.icon.c_str(),&w,&h,&n,4)){
      icon_.assign(size_t(size)*size*4,0);
      for(int oy=0;oy<size;oy++)for(int ox=0;ox<size;ox++){
        const int x0=ox*w/size,x1=std::max(x0+1,(ox+1)*w/size),y0=oy*h/size,y1=std::max(y0+1,(oy+1)*h/size);
        unsigned sum[4]={0,0,0,0},count=0;
        for(int sy=y0;sy<y1;sy++)for(int sx=x0;sx<x1;sx++){for(int k=0;k<4;k++)sum[k]+=source[(size_t(sy)*w+sx)*4+k];count++;}
        for(int k=0;k<4;k++)icon_[(size_t(oy)*size+ox)*4+k]=(unsigned char)(sum[k]/count);
      }
      stbi_image_free(source);
    }
  }
  if(icon_.empty())return;
  // Round the corners like an app icon.
  const float radius=size*.22f;
  for(int oy=0;oy<size;oy++)for(int ox=0;ox<size;ox++){
    const float qx=std::max(std::abs(ox+.5f-size/2.f)-(size/2.f-radius),0.f),qy=std::max(std::abs(oy+.5f-size/2.f)-(size/2.f-radius),0.f);
    const float coverage=std::clamp(radius+.5f-std::sqrt(qx*qx+qy*qy),0.f,1.f);
    const unsigned char *p=&icon_[(size_t(oy)*size+ox)*4];
    unsigned char rgba[4]={p[0],p[1],p[2],(unsigned char)(p[3]*coverage)};
    canvasBlit(c,x+ox,y+oy,1,1,rgba);
  }
}

void Dashboard::rail(Canvas &c){
  constexpr int railWidth=112;
  canvasRect(c,0,0,railWidth,height,railColor.r,railColor.g,railColor.b,255,true);
  canvasRect(c,railWidth-1,0,1,height,border.r,border.g,border.b,255,true);
  drawIcon(c,(railWidth-64)/2,28,64);
  const Type label{fonts_.semibold,14};
  struct Item {Page page;const char *key,*name;};
  const Item items[]={{Page::Space,"nav:space","Space"},{Page::Library,"nav:library","Library"},{Page::Alerts,"nav:alerts","Alerts"},{Page::Settings,"nav:settings","Settings"}};
  for(int i=0;i<4;i++){
    const auto &item=items[i];
    const int y=128+i*96;
    const bool current=page_==item.page,enabled=online_&&(item.page!=Page::Settings||settingsKnown_);
    if(current)canvasFillRoundRect(c,12,y,railWidth-24,84,18,card.r,card.g,card.b,255);
    else if(enabled&&hovered_==item.key)canvasFillRoundRect(c,12,y,railWidth-24,84,18,background.r,background.g,background.b,255);
    if(current)canvasFillRoundRect(c,0,y+22,4,40,2,accent.r,accent.g,accent.b,255);
    const Color ink=!enabled?faint:current?accent:muted;
    const float gx=railWidth/2.f,gy=y+32;
    // Simple drawn glyphs: a portal, a grid, a bell-like alert mark and sliders.
    if(item.page==Page::Space){canvasRing(c,gx,gy,14,3,ink.r,ink.g,ink.b,255);canvasFillCircleSmooth(c,gx,gy,5,ink.r,ink.g,ink.b,255);}
    else if(item.page==Page::Library){for(int k=0;k<4;k++)canvasFillRoundRect(c,gx-13+(k%2)*15,gy-13+(k/2)*15,11,11,3,ink.r,ink.g,ink.b,255);}
    else if(item.page==Page::Alerts){
      canvasRing(c,gx,gy,14,3,ink.r,ink.g,ink.b,255);
      canvasFillRoundRect(c,gx-1.5f,gy-8,3,10,1.5f,ink.r,ink.g,ink.b,255);canvasFillCircleSmooth(c,gx,gy+6.5f,2,ink.r,ink.g,ink.b,255);
      if(!alerts_.empty()){
        canvasFillCircleSmooth(c,gx+15,gy-12,11,danger.r,danger.g,danger.b,255);
        Type{fonts_.bold,13}.center(c,int(gx+15),int(gy-21),std::to_string(alerts_.size()),onAccent);
      }
    }else{
      for(int k=0;k<3;k++){
        const float ly=gy-10+k*10;
        canvasFillRoundRect(c,gx-14,ly-1.5f,28,3,1.5f,ink.r,ink.g,ink.b,255);
        canvasFillCircleSmooth(c,gx+(k==1?6:-6),ly,4.5f,ink.r,ink.g,ink.b,255);
      }
    }
    label.center(c,railWidth/2,y+56,item.name,ink);
    const Page target=item.page;
    region(12,y,railWidth-24,84,item.key,item.name,enabled,current,[this,target]{page_=target;});
  }
  // The engine power control at the foot of the rail. Stopping needs a second press.
  const bool armed=stopArmedUntil_!=Clock::time_point{},engineBusy=starting_||stopping_;
  const std::string key=online_?"stop-engine":"rail-start";
  const float cx=railWidth/2.f,cy=height-92;
  const Color ring=engineBusy?warn:!online_?faint:armed?danger:hovered_==key?cyan:Color{8,180,190};
  canvasRing(c,cx,cy,26,4,ring.r,ring.g,ring.b,255);
  // The power symbol: a ring with a gap, and a bar through it.
  canvasRing(c,cx,cy,13,3,ring.r,ring.g,ring.b,255);
  canvasFillRoundRect(c,cx-5,cy-19,10,14,3,railColor.r,railColor.g,railColor.b,255);
  canvasFillRoundRect(c,cx-1.5f,cy-17,3,13,1.5f,ring.r,ring.g,ring.b,255);
  const std::string caption=engineBusy?(starting_?"Starting":"Stopping"):!online_?"Start":armed?"Press again":"Stop";
  Type{fonts_.semibold,13}.center(c,railWidth/2,int(cy+34),caption,armed?danger:muted);
  region(int(cx-40),int(cy-36),80,92,key,armed?"Press again to stop":caption,!engineBusy,armed,[this,armed]{
    if(!online_){startEngine(now_);return;}
    if(armed)stopEngine(now_);else{stopArmedUntil_=now_+stopConfirmTime;dirty_=true;}
  });
}

void Dashboard::title(Canvas &c,const std::string &heading,const std::string &subtitle){
  Type{fonts_.bold,34}.draw(c,152,40,heading,text);
  if(!subtitle.empty())Type{fonts_.medium,18}.draw(c,152,90,subtitle,muted);
}

void Dashboard::draw(Canvas &c,Clock::time_point now){
  now_=now;buttons_.clear();dirty_=false;
  if(!online_)page_=Page::Space;
  canvasClear(c,background.r,background.g,background.b,255);
  rail(c);
  if(!online_)drawOffline(c);
  else switch(page_){
    case Page::Space:drawSpace(c,now);break;
    case Page::Library:drawLibrary(c);break;
    case Page::Alerts:drawAlerts(c);break;
    case Page::Settings:drawSettings(c);break;
  }
  if(!notice_.empty()){
    const Type type{fonts_.semibold,18};
    const int mid=112+(width-112)/2;
    const std::string shown=type.fit(notice_,width-260);
    const int w=type.width(shown)+56;
    panel(c,mid-w/2,height-62,w,44,22,cardHover);
    type.center(c,mid,height-62+10,shown,warn);
  }
  c.dirty=true;
}

void Dashboard::drawOffline(Canvas &c){
  const int mid=112+(width-112)/2;
  drawIcon(c,mid-64,170,128);
  Type{fonts_.bold,34}.center(c,mid,334,"Pocketspace is not running",text);
  Type{fonts_.medium,19}.center(c,mid,388,"Start the engine to bring your scripts into this space.",muted);
  button(c,mid-150,446,300,64,"start-engine",starting_?"Starting…":"Start engine",Style::Primary,!starting_,[this]{startEngine(now_);});
  if(!startError_.empty()){const Type note{fonts_.medium,17};note.center(c,mid,540,note.fit(startError_,width-260),danger);}
  else if(engineAutostart_)Type{fonts_.medium,17}.center(c,mid,540,"The engine also starts with SteamVR.",faint);
}

void Dashboard::drawSpace(Canvas &c,Clock::time_point now){
  std::vector<const Instance*> live;
  for(const auto &x:instances_)if(x.id!=0&&x.state!="stopped")live.push_back(&x);
  int running=0;for(const auto *x:live)running+=x->state=="running";
  title(c,"Your space",running==0?"Nothing running yet":std::to_string(running)+(running==1?" script running":" scripts running"));
  // Engine health as a row of chips at the top right.
  const Instance *scratch=nullptr;for(const auto &x:instances_)if(x.id==0)scratch=&x;
  const double up=scratch?scratch->uptimeSeconds+std::chrono::duration<double>(now-scratch->received).count():0;
  const std::string chips[]={"Up "+uptime(up),decimal(fps_,"%.0f")+" fps",decimal(cpuPercent_,"%.0f%%")+" CPU",bytes(ramBytes_)+" RAM"};
  const Type chipType{fonts_.semibold,16};
  int cx=width-48;
  for(int i=3;i>=0;i--){
    const int w=chipType.width(chips[i])+(i==0?52:36);
    cx-=w;
    panel(c,cx,44,w,40,20,card);
    if(i==0){canvasFillCircleSmooth(c,cx+20,64,5,cyan.r,cyan.g,cyan.b,255);chipType.draw(c,cx+34,53,chips[i],text);}
    else chipType.draw(c,cx+18,53,chips[i],text);
    cx-=10;
  }
  const Type section{fonts_.bold,13,2.5f},name{fonts_.semibold,24},meta{fonts_.medium,16};
  // Running now: one card per live script.
  section.draw(c,152,150,"RUNNING NOW",faint);
  constexpr int cardsTop=180,cardHeight=236,perPage=3;
  const int cardWidth=(width-152-48-2*24)/3;
  const int pages=std::max(1,int((live.size()+perPage-1)/perPage));
  runningPage_=std::clamp(runningPage_,0,pages-1);
  if(pages>1){
    button(c,width-48-96,140,44,32,"running-prev","‹",Style::Ghost,runningPage_>0,[this]{runningPage_--;});
    button(c,width-48-44,140,44,32,"running-next","›",Style::Ghost,runningPage_<pages-1,[this]{runningPage_++;});
  }
  if(live.empty()){
    panel(c,152,cardsTop,width-152-48,cardHeight,24,card);
    Type{fonts_.semibold,24}.draw(c,192,cardsTop+64,"Nothing is running in your space",text);
    meta.draw(c,192,cardsTop+106,"Launch a script below, or browse everything in your library.",muted);
    button(c,192,cardsTop+150,200,52,"nav:library-empty","Open library",Style::Primary,true,[this]{page_=Page::Library;});
  }
  for(size_t i=size_t(runningPage_*perPage);i<live.size()&&i<size_t((runningPage_+1)*perPage);i++){
    const auto &x=*live[i];
    const int col=int(i)%perPage,x0=152+col*(cardWidth+24);
    const bool isRunning=x.state=="running";
    panel(c,x0,cardsTop,cardWidth,cardHeight,24,card);
    // A thin top band in the instance's state color.
    const Color band=isRunning?cyan:x.restartPending?warn:danger;
    canvasFillRoundRect(c,x0+24,cardsTop+20,40,5,2.5f,band.r,band.g,band.b,255);
    name.draw(c,x0+24,cardsTop+40,name.fit(x.name,cardWidth-48),text);
    std::vector<std::string> lines;
    if(isRunning){
      lines.push_back("Up "+uptime(x.uptimeSeconds+std::chrono::duration<double>(now-x.received).count())+"  ·  generation "+std::to_string(x.generation));
      std::string usage;
      if(x.heapBytes)usage+=bytes(x.heapBytes)+" heap";
      if(x.callbackMsPerSecond)usage+=(usage.empty()?"":"  ·  ")+decimal(x.callbackMsPerSecond,"%.1f")+" ms/s";
      if(!usage.empty())lines.push_back(usage);
      if(x.objects)lines.push_back(count(x.objects)+(*x.objects==1?" object":" objects")+(x.gpuEstimateBytes?"  ·  GPU "+bytes(x.gpuEstimateBytes):""));
      if(x.lastReload=="failedKeptScene")lines.push_back("Last reload failed; scene kept");
    }else{
      lines.push_back(x.restartPending?"Failed, restarting":"Failed");
      if(!x.lastError.empty())lines.push_back(x.lastError);
    }
    for(size_t k=0;k<lines.size()&&k<3;k++)meta.draw(c,x0+24,cardsTop+82+int(k)*26,meta.fit(lines[k],cardWidth-48),!isRunning&&k==1?danger:muted);
    const std::string id=std::to_string(x.id),fields=",\"instanceId\":"+id+",\"generation\":"+std::to_string(x.generation),label=x.name;
    const int by=cardsTop+cardHeight-68,bw=(cardWidth-48-12)/2;
    if(isRunning||x.restartPending){
      button(c,x0+24,by,bw,48,"reload:"+id,"Reload",Style::Ghost,isRunning,[this,fields,id,label]{request("reload",fields,"reload:"+id,"Reloading "+label);});
      button(c,x0+24+bw+12,by,bw,48,"stop:"+id,"Stop",Style::Primary,true,[this,fields,id,label]{request("stop",fields,"stop:"+id,"Stopping "+label);});
    }else button(c,x0+24,by,cardWidth-48,48,"start:"+label,"Start again",Style::Primary,true,[this,label]{request("start",",\"name\":"+quote(label),"start:"+label,"Starting "+label);});
  }
  // Quick launch: autostart scripts first, then the rest of the catalog.
  section.draw(c,152,452,"QUICK LAUNCH",faint);
  std::vector<std::string> quick;
  auto isLive=[&](const std::string &n){for(const auto *x:live)if(x->name==n)return true;return false;};
  for(const auto &n:autostart_)if(std::find(catalog_.begin(),catalog_.end(),n)!=catalog_.end()&&!isLive(n))quick.push_back(n);
  for(const auto &n:catalog_)if(!isLive(n)&&std::find(quick.begin(),quick.end(),n)==quick.end())quick.push_back(n);
  const int pillWidth=(width-152-48-3*16)/4;
  if(quick.empty())meta.draw(c,152,486,catalog_.empty()?"Your library is empty.":"Everything in your library is running.",muted);
  for(size_t i=0;i<quick.size()&&i<8;i++){
    const std::string script=quick[i];
    button(c,152+int(i%4)*(pillWidth+16),482+int(i/4)*72,pillWidth,56,"start:"+script,script,Style::Ghost,true,
      [this,script]{request("start",",\"name\":"+quote(script),"start:"+script,"Starting "+script);});
  }
  const std::string more="See the whole library  →";
  const Type link{fonts_.semibold,17};
  const int rows=quick.empty()?1:int(std::min<size_t>(quick.size(),8)+3)/4,linkY=482+rows*72+8;
  link.draw(c,152,linkY,more,accent);
  region(152,linkY-8,link.width(more),36,"nav:library-more",more,true,false,[this]{page_=Page::Library;});
}

void Dashboard::drawLibrary(Canvas &c){
  title(c,"Library",std::to_string(catalog_.size())+(catalog_.size()==1?" script":" scripts")+" in your catalog");
  const Type name{fonts_.semibold,22},meta{fonts_.medium,16};
  if(catalog_.empty()){meta.draw(c,152,160,"No scripts in the scripts directory.",muted);return;}
  constexpr int top=144,tileHeight=156,perPage=9;
  const int tileWidth=(width-152-48-2*24)/3;
  const int pages=std::max(1,int((catalog_.size()+perPage-1)/perPage));
  libraryPage_=std::clamp(libraryPage_,0,pages-1);
  for(size_t i=size_t(libraryPage_*perPage);i<catalog_.size()&&i<size_t((libraryPage_+1)*perPage);i++){
    const std::string script=catalog_[i];
    const int slot=int(i)%perPage,x=152+(slot%3)*(tileWidth+24),y=top+(slot/3)*(tileHeight+16);
    bool live=false;for(const auto &inst:instances_)live|=inst.id!=0&&inst.name==script&&inst.state!="stopped";
    const bool automatic=std::find(autostart_.begin(),autostart_.end(),script)!=autostart_.end();
    panel(c,x,y,tileWidth,tileHeight,22,card);
    name.draw(c,x+24,y+22,name.fit(script,tileWidth-48-140),text);
    // Reset clears the script's saves; the first press only arms it.
    const bool armed=resetArmed_==script;
    button(c,x+tileWidth-24-128,y+18,128,36,"data:"+script,armed?"Press again":"Reset",armed?Style::Danger:Style::Ghost,true,[this,script,armed]{
      if(!armed){resetArmed_=script;resetArmedUntil_=now_+stopConfirmTime;dirty_=true;return;}
      resetArmed_.clear();request("data.reset",",\"name\":"+quote(script),"data:"+script,"Resetting saved data for "+script);
    });
    if(live){canvasFillCircleSmooth(c,x+30,y+67,5,cyan.r,cyan.g,cyan.b,255);meta.draw(c,x+42,y+58,"Running",cyan);}
    else meta.draw(c,x+24,y+58,automatic?"Starts with the engine":"Not running",muted);
    button(c,x+24,y+tileHeight-64,132,44,"start:"+script,live?"Running":"Launch",Style::Primary,!live,[this,script]{request("start",",\"name\":"+quote(script),"start:"+script,"Starting "+script);});
    meta.right(c,x+tileWidth-24-60-12,y+tileHeight-53,"Autostart",muted);
    toggle(c,x+tileWidth-24-60,y+tileHeight-59,"auto:"+script,automatic,settingsKnown_,[this,script,automatic]{
      auto names=autostart_;
      if(automatic)names.erase(std::find(names.begin(),names.end(),script));else names.push_back(script);
      writeSettings(names,autoRestart_,engineAutostart_);
    });
  }
  if(pages>1){
    meta.draw(c,152,height-108,"Page "+std::to_string(libraryPage_+1)+" of "+std::to_string(pages),muted);
    button(c,width-48-272,height-120,128,48,"library-prev","Previous",Style::Ghost,libraryPage_>0,[this]{libraryPage_--;});
    button(c,width-48-128,height-120,128,48,"library-next","Next",Style::Ghost,libraryPage_<pages-1,[this]{libraryPage_++;});
  }
}

void Dashboard::drawAlerts(Canvas &c){
  title(c,"Alerts",alerts_.empty()?"Nothing to report":"Scripts that stopped on an error");
  const Type name{fonts_.semibold,22},meta{fonts_.medium,16};
  if(alerts_.empty()){meta.draw(c,152,160,"When a script stops on an error, it shows up here.",muted);return;}
  int y=144;
  for(size_t i=0;i<alerts_.size()&&i<5;i++){
    const auto &a=alerts_[i];
    panel(c,152,y,width-152-48,96,20,card);
    canvasFillRoundRect(c,176,y+24,5,48,2.5f,danger.r,danger.g,danger.b,255);
    name.draw(c,200,y+18,name.fit(a.name+(a.count>1?"  ·  failed "+std::to_string(a.count)+" times":""),width-152-48-80),text);
    meta.draw(c,200,y+54,meta.fit(a.message,width-152-48-80),danger);
    y+=108;
  }
  button(c,width-48-200,height-120,200,48,"dismiss","Dismiss all",Style::Ghost,true,[this]{alerts_.clear();});
}

void Dashboard::drawSettings(Canvas &c){
  title(c,"Settings","How the engine starts and recovers");
  const Type name{fonts_.semibold,22},meta{fonts_.medium,16};
  struct Row {const char *key,*title,*detail;bool on;std::function<void()> action;};
  const Row rows[]={
    {"auto-restart","Restart failed scripts","Retry a script that stops on an error, after 1, 2, 4, 8 and 16 seconds.",autoRestart_,[this]{writeSettings(autostart_,!autoRestart_,engineAutostart_);}},
    {"engine-autostart","Start the engine with SteamVR","Launch the engine and the autostart scripts whenever SteamVR starts.",engineAutostart_,[this]{writeSettings(autostart_,autoRestart_,!engineAutostart_);}}};
  int y=144;
  for(const auto &row:rows){
    panel(c,152,y,width-152-48,100,20,card);
    name.draw(c,180,y+20,row.title,text);
    meta.draw(c,180,y+56,meta.fit(row.detail,width-152-48-160),muted);
    toggle(c,width-48-28-60,y+33,row.key,row.on,settingsKnown_,row.action);
    y+=116;
  }
  // The scratch scene is for live JavaScript from the CLI REPL.
  panel(c,152,y,width-152-48,100,20,card);
  name.draw(c,180,y+20,"Scratch scene",text);
  meta.draw(c,180,y+56,meta.fit("The live scene that pocketspace-cli repl runs in. Reset clears its objects and globals.",width-152-48-220),muted);
  button(c,width-48-28-120,y+26,120,48,"reset:0","Reset",Style::Ghost,true,[this]{request("scratch.reset","","reset:0","Resetting scratch");});
  y+=116;
  meta.draw(c,152,y+4,meta.fit(autostart_.empty()?"No scripts start with the engine. Choose them in the library.":
    std::to_string(autostart_.size())+(autostart_.size()==1?" script starts":" scripts start")+" with the engine. Choose them in the library.",width-200),muted);
  if(!settingsError_.empty())meta.draw(c,152,y+36,meta.fit("Settings file: "+settingsError_,width-200),warn);
}
}
