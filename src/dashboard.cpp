#include "dashboard.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace dashboard {
using control::Json;using control::quote;
namespace {
constexpr std::chrono::milliseconds reconnectInterval{1000},startingReconnect{500},refreshInterval{10000},startTimeout{30000},noticeTime{10000},
  stopConfirmTime{3000},stopTimeout{10000};
constexpr size_t maxAlerts=8;

struct Color {unsigned char r,g,b;};
constexpr Color background{22,24,29},surface{34,37,44},raised{52,57,67},hover{72,80,94},line{60,64,74},
  text{232,234,238},dim{150,156,168},accent{86,156,255},good{82,198,122},bad{236,92,92},warn{240,182,64};

void fill(Canvas &c,int x,int y,int w,int h,Color k){canvasRect(c,x,y,w,h,k.r,k.g,k.b,255,true);}
void write(Canvas &c,int x,int y,const std::string &s,Color k,int pixels){canvasText(c,x,y,s,k.r,k.g,k.b,255,pixels);}

// The default fonts are monospace, so one measured glyph gives every width.
int glyphWidth(const Canvas &c,int pixels){
  static std::map<std::pair<std::string,int>,int> widths;
  const auto key=std::make_pair(c.fontPath,pixels);
  auto found=widths.find(key);
  if(found==widths.end())found=widths.emplace(key,std::max(1,canvasTextWidth(c,"M",pixels))).first;
  return found->second;
}
// Cuts text to fit a width, ending in "..." when shortened.
std::string fit(const Canvas &c,std::string s,int width,int pixels){
  for(auto &ch:s)if(ch=='\n'||ch=='\r'||ch=='\t')ch=' ';
  const size_t chars=size_t(std::max(0,width/glyphWidth(c,pixels)));
  if(s.size()<=chars)return s;
  return chars<=3?s.substr(0,chars):s.substr(0,chars-3)+"...";
}

std::string bytes(std::optional<double> b){
  if(!b)return "-";
  char out[32];
  if(*b>=1073741824)std::snprintf(out,sizeof(out),"%.2f GiB",*b/1073741824);
  else if(*b>=1048576)std::snprintf(out,sizeof(out),"%.1f MiB",*b/1048576);
  else if(*b>=1024)std::snprintf(out,sizeof(out),"%.1f KiB",*b/1024);
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

Dashboard::Dashboard(std::filesystem::path socket,Hooks hooks):socket_(std::move(socket)),hooks_(std::move(hooks)){}

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

void Dashboard::draw(Canvas &c,Clock::time_point now){
  now_=now;buttons_.clear();dirty_=false;
  canvasClear(c,background.r,background.g,background.b,255);
  auto button=[&](int x,int y,int w,int h,const std::string &key,const std::string &label,bool enabled,std::function<void()> action,bool active=false,Color activeColor=accent){
    const bool working=busy(key);enabled=enabled&&!working;
    const Color base=!enabled?surface:active?activeColor:key==hovered_?hover:raised;
    fill(c,x,y,w,h,base);
    const std::string shown=fit(c,working?"...":label,w-12,20);
    const int tw=int(shown.size())*glyphWidth(c,20);
    write(c,x+(w-tw)/2,y+(h-24)/2,shown,enabled?(active?background:text):dim,20);
    buttons_.push_back({x,y,w,h,key,label,enabled,active,std::move(action)});
  };

  // Header: engine state and process totals, which are never split among scripts.
  fill(c,0,0,width,96,surface);
  write(c,24,14,"Pocketspace",text,32);
  const Color stateColor=online_?good:starting_?warn:bad;
  canvasCircle(c,300,34,9,stateColor.r,stateColor.g,stateColor.b,255,true);
  write(c,318,20,online_?"Engine running":starting_?"Starting engine...":"Engine offline",text,22);
  if(online_){
    std::string totals=decimal(fps_,"%.0f")+" fps   "+decimal(cpuPercent_,"%.1f%%")+" CPU   "+bytes(ramBytes_)+" RAM   GPU "+(vramBytes_?bytes(vramBytes_):"n/a")
      +"   GPU est. "+bytes(gpuEstimateBytes_)+" ("+bytes(sharedGpuEstimateBytes_)+" shared)";
    write(c,24,60,fit(c,totals,width-48,20),dim,20);
    // Stopping ends every script, so it takes a second press within a few seconds.
    const bool armed=stopArmedUntil_!=Clock::time_point{};
    button(width-24-260,12,260,40,"stop-engine",stopping_?"Stopping...":armed?"Press again to stop":"Stop Engine",!stopping_,[this,armed]{
      if(armed)stopEngine(now_);else{stopArmedUntil_=now_+stopConfirmTime;dirty_=true;}
    },armed,bad);
  }

  int y=108;
  // Alerts: one line per failed instance, newest first.
  if(!alerts_.empty()){
    const size_t shown=std::min<size_t>(alerts_.size(),2);
    for(size_t i=0;i<shown;i++){
      const auto &a=alerts_[i];
      fill(c,24,y,width-48,40,Color{90,34,38});
      std::string message=a.name+" stopped: "+a.message+(a.count>1?" (x"+std::to_string(a.count)+")":"");
      write(c,36,y+8,fit(c,message,width-48-24-(i==0?160:0),20),text,20);
      if(i==0)button(width-24-150,y+4,140,32,"dismiss","Dismiss",true,[this]{alerts_.clear();});
      y+=46;
    }
    if(alerts_.size()>shown){write(c,36,y,std::to_string(alerts_.size()-shown)+" more",dim,18);y+=26;}
    y+=6;
  }

  if(!online_){
    write(c,320,y+120,"The engine is not running.",text,30);
    write(c,320,y+164,"Scripts, the catalog and settings appear once it is.",dim,20);
    button(320,y+214,280,64,"start-engine",starting_?"Starting...":"Start Engine",!starting_,[this]{startEngine(now_);});
    if(!startError_.empty())write(c,320,y+300,fit(c,startError_,width-344,20),bad,20);
  }else{
    // Scripts: scratch first, then live user scripts. Stopped instances are left out;
    // the catalog starts them again.
    const int left=24,columnWidth=776,bottom=height-48;
    write(c,left,y,"Scripts",text,26);
    std::vector<const Instance*> shown;
    for(const auto &instance:instances_)if(instance.id==0||instance.state!="stopped")shown.push_back(&instance);
    constexpr int rowHeight=118;
    const int rowsTop=y+40,perPage=std::max(1,(bottom-rowsTop-44)/rowHeight);
    const int pages=std::max(1,int((shown.size()+size_t(perPage)-1)/size_t(perPage)));
    scriptPage_=std::clamp(scriptPage_,0,pages-1);
    int rowY=rowsTop;
    for(size_t i=size_t(scriptPage_*perPage);i<shown.size()&&i<size_t((scriptPage_+1)*perPage);i++){
      const auto &x=*shown[i];
      fill(c,left,rowY,columnWidth,rowHeight-8,surface);
      const bool running=x.state=="running";
      const Color dot=running?good:x.restartPending?warn:bad;
      canvasCircle(c,left+20,rowY+22,7,dot.r,dot.g,dot.b,255,true);
      const std::string title=x.id==0?"Scratch":x.name;
      std::string detail=x.id==0?"instance 0":"#"+std::to_string(x.id)+"  gen "+std::to_string(x.generation);
      if(running)detail+="  up "+uptime(x.uptimeSeconds+std::chrono::duration<double>(now-x.received).count());
      else detail+=x.restartPending?"  restarting":"  "+x.state;
      if(x.lastReload=="failedKeptScene")detail+="  reload failed, scene kept";
      const int buttonsWidth=x.id==0?110:226;
      const int textWidth=columnWidth-44-buttonsWidth,fullWidth=columnWidth-48;
      write(c,left+36,rowY+8,fit(c,title,textWidth,22),text,22);
      write(c,left+36,rowY+36,fit(c,detail,textWidth,18),dim,18);
      if(running){
        // The buttons take only the top of the row, so these lines use its full width.
        write(c,left+36,rowY+60,fit(c,"heap "+bytes(x.heapBytes)+"   callbacks "+decimal(x.callbackMsPerSecond,"%.2f")+" ms/s, worst "+decimal(x.worstCallbackMs,"%.2f")+" ms",fullWidth,18),dim,18);
        write(c,left+36,rowY+84,fit(c,"GPU est. "+bytes(x.gpuEstimateBytes)+"   "+count(x.objects)+" objects, "+count(x.panels)+" panels, "+count(x.assets)+" assets, "+count(x.bodies)+" bodies",fullWidth,18),dim,18);
      }else if(!x.lastError.empty()){
        write(c,left+36,rowY+60,fit(c,x.lastError,fullWidth,18),bad,18);
      }
      const int bx=left+columnWidth-buttonsWidth,by=rowY+12;
      const std::string id=std::to_string(x.id),fields=",\"instanceId\":"+id+",\"generation\":"+std::to_string(x.generation);
      if(x.id==0)button(bx,by,100,40,"reset:0","Reset",true,[this]{request("scratch.reset","","reset:0","Resetting scratch");});
      else if(running||x.restartPending){
        const std::string name=x.name;
        button(bx,by,104,40,"reload:"+id,"Reload",running,[this,fields,id,name]{request("reload",fields,"reload:"+id,"Reloading "+name);});
        button(bx+112,by,104,40,"stop:"+id,"Stop",true,[this,fields,id,name]{request("stop",fields,"stop:"+id,"Stopping "+name);});
      }else{
        const std::string name=x.name;
        button(bx+112,by,104,40,"start:"+name,"Start",true,[this,name]{request("start",",\"name\":"+quote(name),"start:"+name,"Starting "+name);});
      }
      rowY+=rowHeight;
    }
    if(pages>1){
      write(c,left,bottom-36,"Page "+std::to_string(scriptPage_+1)+" of "+std::to_string(pages),dim,18);
      button(left+columnWidth-220,bottom-40,104,36,"scripts-prev","Prev",scriptPage_>0,[this]{scriptPage_--;});
      button(left+columnWidth-108,bottom-40,104,36,"scripts-next","Next",scriptPage_<pages-1,[this]{scriptPage_++;});
    }

    // Catalog with launch and autostart, then settings.
    const int right=824,rightWidth=width-24-right;
    const int settingsTop=bottom-150;
    write(c,right,y,"Catalog",text,26);
    constexpr int catalogRow=50;
    const int catalogTop=y+40,perCatalog=std::max(1,(settingsTop-catalogTop-48)/catalogRow);
    const int catalogPages=std::max(1,int((catalog_.size()+size_t(perCatalog)-1)/size_t(perCatalog)));
    catalogPage_=std::clamp(catalogPage_,0,catalogPages-1);
    if(catalog_.empty())write(c,right,catalogTop+8,"No scripts in the scripts directory.",dim,18);
    int catalogY=catalogTop;
    for(size_t i=size_t(catalogPage_*perCatalog);i<catalog_.size()&&i<size_t((catalogPage_+1)*perCatalog);i++){
      const std::string name=catalog_[i];
      bool live=false;for(const auto &x:instances_)live|=x.id!=0&&x.name==name&&x.state!="stopped";
      const bool automatic=std::find(autostart_.begin(),autostart_.end(),name)!=autostart_.end();
      fill(c,right,catalogY,rightWidth,catalogRow-6,surface);
      write(c,right+12,catalogY+10,fit(c,name,rightWidth-12-200,20),live?text:dim,20);
      button(right+rightWidth-196,catalogY+4,96,36,"start:"+name,live?"Running":"Launch",!live,[this,name]{request("start",",\"name\":"+quote(name),"start:"+name,"Starting "+name);});
      button(right+rightWidth-92,catalogY+4,88,36,"auto:"+name,"Auto",settingsKnown_&&!busy("settings"),[this,name,automatic]{
        auto names=autostart_;
        if(automatic)names.erase(std::find(names.begin(),names.end(),name));else names.push_back(name);
        writeSettings(names,autoRestart_,engineAutostart_);
      },automatic);
      catalogY+=catalogRow;
    }
    if(catalogPages>1){
      write(c,right,settingsTop-40,std::to_string(catalogPage_+1)+"/"+std::to_string(catalogPages),dim,18);
      button(right+rightWidth-196,settingsTop-44,96,36,"catalog-prev","Prev",catalogPage_>0,[this]{catalogPage_--;});
      button(right+rightWidth-92,settingsTop-44,88,36,"catalog-next","Next",catalogPage_<catalogPages-1,[this]{catalogPage_++;});
    }
    write(c,right,settingsTop,"Settings",text,26);
    auto toggle=[&](int rowTop,const std::string &key,const std::string &label,bool on,std::function<void()> action){
      write(c,right,rowTop+8,fit(c,label,rightWidth-100,20),text,20);
      button(right+rightWidth-88,rowTop,88,36,key,on?"On":"Off",settingsKnown_&&!busy("settings"),std::move(action),on);
    };
    toggle(settingsTop+40,"auto-restart","Restart failed scripts",autoRestart_,[this]{writeSettings(autostart_,!autoRestart_,engineAutostart_);});
    toggle(settingsTop+84,"engine-autostart","Start engine with SteamVR",engineAutostart_,[this]{writeSettings(autostart_,autoRestart_,!engineAutostart_);});
    if(!settingsError_.empty())write(c,right,settingsTop+126,fit(c,"Settings file: "+settingsError_,rightWidth,16),warn,16);
  }
  if(!notice_.empty())write(c,24,height-36,fit(c,notice_,width-48,20),warn,20);
  c.dirty=true;
}
}
