// The dashboard tab's model against a fake engine on a real Unix socket, without VR:
// offline Start Engine, two scripts, stop/reload/start/reset requests with generations,
// stale replies, alerts from a restart loop, resources, settings, engine restart,
// a dropped connection and hiding the tab.
#include "../src/dashboard.h"
#include <cassert>
#include <cstdio>
#include <set>

#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)

using control::Json;using control::quote;using dashboard::Dashboard;

namespace {
struct FakeInstance {long long id,generation;std::string name,state;std::string error;bool restartPending=false;};

// A control service with just enough behavior for the dashboard.
struct FakeEngine {
  control::Server server;std::filesystem::path path;
  std::vector<FakeInstance> instances{{0,1,"scratch","running"},{1,1,"alpha.js","running"},{2,1,"beta.js","running"}};
  std::vector<std::string> catalog{"alpha.js","beta.js","gamma.js"},autostart{"alpha.js"};
  bool autoRestart=true,engineAutostart=false;long long nextId=3;
  std::vector<std::string> ops;std::vector<std::string> requests;
  std::set<std::string> silent; // ops that get no reply
  bool start(){std::string error;return server.start(path,error);}
  std::string view(const FakeInstance &x){
    return "{\"instanceId\":"+std::to_string(x.id)+",\"name\":"+quote(x.name)+",\"path\":\"/s/"+x.name+"\",\"state\":\""+x.state+"\",\"generation\":"+std::to_string(x.generation)
      +",\"uptimeSeconds\":5,\"lastError\":"+(x.error.empty()?"null":quote(x.error))+",\"restartPending\":"+(x.restartPending?"true":"false")+",\"lastReload\":null}";
  }
  std::string settings(){
    std::string list="[";for(size_t i=0;i<autostart.size();i++)list+=(i?",":"")+quote(autostart[i]);
    return "{\"version\":1,\"autostart\":"+list+"],\"autoRestart\":"+(autoRestart?"true":"false")+",\"engineAutostart\":"+(engineAutostart?"true":"false")+"}";
  }
  FakeInstance *find(long long id){for(auto &x:instances)if(x.id==id)return &x;return nullptr;}
  std::string handle(control::Client &client,const std::string &line){
    const auto request=*Json::parse(line);const auto id=quote(request["id"].str());const auto op=request["op"].str();
    ops.push_back(op);requests.push_back(line);
    auto ok=[&](const std::string &result){return "{\"v\":1,\"id\":"+id+",\"ok\":true,\"result\":"+result+"}";};
    auto fail=[&](const std::string &code,const std::string &message){return "{\"v\":1,\"id\":"+id+",\"ok\":false,\"error\":{\"code\":\""+code+"\",\"message\":"+quote(message)+"}}";};
    if(silent.count(op))return "{\"v\":1,\"event\":\"ignored\",\"seq\":0}";
    if(op=="watch"){client.watching=true;return ok("{\"watching\":true,\"seq\":0}");}
    if(op=="list"){std::string out="[";for(size_t i=0;i<instances.size();i++)out+=(i?",":"")+view(instances[i]);return ok("{\"instances\":"+out+"]}");}
    if(op=="catalog"){std::string out="[";for(size_t i=0;i<catalog.size();i++)out+=(i?",":"")+quote(catalog[i]);return ok("{\"scripts\":"+out+"]}");}
    if(op=="settings.get")return ok("{\"path\":\"/s/settings.json\",\"settings\":"+settings()+",\"fileError\":null}");
    if(op=="settings.set"){
      const auto s=request["settings"];autostart.clear();
      for(uint32_t i=0;i<s["autostart"].length();i++)autostart.push_back(s["autostart"][i].str());
      autoRestart=s["autoRestart"].truthy();engineAutostart=s["engineAutostart"].truthy();
      return ok("{\"settings\":"+settings()+"}");
    }
    if(op=="stats")return ok("{\"fps\":90,\"cpuPercent\":8,\"ramBytes\":104857600,\"vramBytes\":null,\"gpuEstimateBytes\":2097152,\"sharedGpuEstimateBytes\":1048576,"
      "\"sampleSeconds\":1,\"sampleId\":3,\"bus\":{\"queued\":0,\"queuedBytes\":0,\"dropped\":0},\"instances\":[{\"instanceId\":1,\"name\":\"alpha.js\",\"state\":\"running\",\"generation\":1,"
      "\"heapBytes\":2048,\"objects\":4,\"panels\":1,\"assets\":2,\"bodies\":0,\"gpuEstimateBytes\":1024,\"callbackMsPerSecond\":0.5,\"worstCallbackMs\":0.2,\"busQueued\":0,\"busDropped\":0}]}");
    if(op=="start"){
      const auto name=request["name"].str();
      for(auto &x:instances)if(x.name==name&&x.state!="stopped")return fail("conflict","script is already running");
      instances.push_back({nextId++,1,name,"running"});
      server.broadcast("{\"v\":1,\"event\":\"status\",\"seq\":1,\"instance\":"+view(instances.back())+"}");
      return ok(view(instances.back()));
    }
    const long long target=(long long)request["instanceId"].number(),generation=(long long)request["generation"].number();
    auto *x=find(target);
    if(op=="scratch.reset"){instances[0].generation++;return ok(view(instances[0]));}
    if(!x)return fail("not_found","unknown instance");
    if(x->generation!=generation)return "{\"v\":1,\"id\":"+id+",\"ok\":false,\"error\":{\"code\":\"stale_generation\",\"message\":\"instance has a newer generation\",\"generation\":"+std::to_string(x->generation)+"}}";
    if(op=="stop"){x->state="stopped";x->restartPending=false;server.broadcast("{\"v\":1,\"event\":\"status\",\"seq\":2,\"instance\":"+view(*x)+"}");return ok(view(*x));}
    if(op=="reload"){x->generation++;server.broadcast("{\"v\":1,\"event\":\"status\",\"seq\":3,\"instance\":"+view(*x)+"}");return ok(view(*x));}
    return fail("unknown_op","unknown operation");
  }
  void poll(){server.poll([this](control::Client &c,const std::string &line){return handle(c,line);});}
};

using Clock=control::Clock;
Clock::time_point now=Clock::now();
Canvas canvas;

// Alternates engine and dashboard work until both are idle.
void pump(FakeEngine *engine,Dashboard &board){
  for(int i=0;i<20;i++){
    if(engine)engine->poll();
    usleep(2000);
    board.poll(now);
  }
  board.draw(canvas,now);
}
const dashboard::Button *button(const Dashboard &board,const std::string &key){
  for(const auto &b:board.buttons())if(b.key==key)return &b;
  return nullptr;
}
void click(Dashboard &board,const std::string &key){
  const auto *b=button(board,key);CHECK(b&&b->enabled);
  const int x=b->x+b->w/2,y=b->y+b->h/2;
  board.pointerMove(x,y);board.pointerDown(x,y);board.pointerUp(x,y);
}
const dashboard::Instance *instance(const Dashboard &board,long long id){
  for(const auto &x:board.instances())if(x.id==id)return &x;
  return nullptr;
}
size_t countOp(const FakeEngine &engine,const std::string &op){return size_t(std::count(engine.ops.begin(),engine.ops.end(),op));}
}

int main(){
  CHECK(control::startJson()&&canvasInitLibrary());
  canvasInit(canvas,dashboard::width,dashboard::height,"");
  CHECK(!canvas.fontPath.empty());
  char directory[]="/tmp/pocketspace-dashboard-XXXXXX";CHECK(mkdtemp(directory));
  FakeEngine engine;engine.path=std::filesystem::path(directory)/"control.sock";

  // Offline: Start Engine asks the service manager and connects once the engine answers.
  int startCalls=0;std::string startResult;
  Dashboard board(engine.path,{[&]{startCalls++;return startResult;}});
  board.setVisible(true,now);pump(nullptr,board);
  CHECK(!board.online()&&button(board,"start-engine")&&!button(board,"stop:1"));
  startResult="systemctl failed";click(board,"start-engine");
  CHECK(startCalls==1&&!board.starting());
  pump(nullptr,board);
  startResult.clear();click(board,"start-engine");
  CHECK(startCalls==2&&board.starting());
  // An engine that never answers ends the attempt with an error.
  now+=std::chrono::seconds(31);pump(nullptr,board);
  CHECK(!board.starting()&&!board.online()&&button(board,"start-engine")->enabled);
  click(board,"start-engine");CHECK(board.starting());
  CHECK(engine.start());
  now+=std::chrono::seconds(1);pump(&engine,board);
  CHECK(board.online()&&!board.starting());
  CHECK(countOp(engine,"watch")==1&&countOp(engine,"list")==1&&countOp(engine,"catalog")==1&&countOp(engine,"settings.get")==1);

  // Two scripts plus scratch, with per-instance resources and the catalog.
  CHECK(board.instances().size()==3&&board.catalog().size()==3);
  CHECK(button(board,"reload:1")&&button(board,"stop:1")&&button(board,"reload:2")&&button(board,"stop:2")&&button(board,"reset:0"));
  CHECK(!button(board,"stop:0"));
  CHECK(!button(board,"start:alpha.js")->enabled&&button(board,"start:gamma.js")->enabled);
  CHECK(instance(board,1)->heapBytes==2048.&&instance(board,1)->objects==4&&!instance(board,2)->heapBytes);
  CHECK(button(board,"auto:alpha.js")->active&&!button(board,"auto:beta.js")->active);

  // Stop sends the shown generation; the status event removes the row and frees the launch.
  click(board,"stop:1");board.draw(canvas,now);
  CHECK(button(board,"stop:1")&&!button(board,"stop:1")->enabled);
  pump(&engine,board);
  CHECK(engine.requests.back().find("\"generation\":1")!=std::string::npos&&engine.requests.back().find("\"op\":\"stop\"")!=std::string::npos);
  CHECK(instance(board,1)->state=="stopped"&&!button(board,"stop:1")&&button(board,"start:alpha.js")->enabled);
  click(board,"start:alpha.js");pump(&engine,board);
  CHECK(instance(board,3)&&instance(board,3)->name=="alpha.js"&&button(board,"stop:3"));

  // A reload elsewhere makes the shown generation stale: the failure is reported and the list refreshed.
  engine.find(2)->generation=5;
  const size_t lists=countOp(engine,"list");
  click(board,"reload:2");pump(&engine,board);
  CHECK(board.notice().find("Reloading beta.js failed")!=std::string::npos);
  CHECK(countOp(engine,"list")==lists+1&&instance(board,2)->generation==5);
  click(board,"reload:2");pump(&engine,board);
  CHECK(instance(board,2)->generation==6);
  click(board,"reset:0");pump(&engine,board);
  CHECK(instance(board,0)->generation==2);

  // A restart loop produces one alert per instance, counting repeats.
  engine.find(2)->state="failed";engine.find(2)->restartPending=true;engine.find(2)->error="Error: boom";
  for(int i=0;i<3;i++){
    engine.server.broadcast("{\"v\":1,\"event\":\"status\",\"seq\":9,\"instance\":"+engine.view(*engine.find(2))+"}");
    engine.server.broadcast("{\"v\":1,\"event\":\"error\",\"seq\":10,\"instanceId\":2,\"message\":\"Error: boom\"}");
  }
  pump(&engine,board);
  CHECK(board.alerts().size()==1&&board.alerts()[0].count==3&&board.alerts()[0].name=="beta.js");
  // A failed script waiting to restart can be stopped but not reloaded.
  CHECK(button(board,"stop:2")->enabled&&!button(board,"reload:2")->enabled);
  click(board,"dismiss");pump(&engine,board);
  CHECK(board.alerts().empty()&&!button(board,"dismiss"));

  // Resources events update totals and running instances only for their generation.
  engine.server.broadcast("{\"v\":1,\"event\":\"resources\",\"seq\":11,\"fps\":88,\"cpuPercent\":9,\"ramBytes\":1,\"vramBytes\":null,\"gpuEstimateBytes\":1,\"sharedGpuEstimateBytes\":0,"
    "\"instances\":[{\"instanceId\":3,\"generation\":1,\"state\":\"running\",\"heapBytes\":4096,\"objects\":1,\"panels\":0,\"assets\":0,\"bodies\":0,\"gpuEstimateBytes\":0,\"callbackMsPerSecond\":null,\"worstCallbackMs\":null},"
    "{\"instanceId\":2,\"generation\":1,\"state\":\"running\",\"heapBytes\":9999}]}");
  pump(&engine,board);
  CHECK(instance(board,3)->heapBytes==4096.&&!instance(board,3)->callbackMsPerSecond&&!instance(board,2)->heapBytes);

  // Settings: autostart and both toggles write the whole settings object.
  click(board,"auto:gamma.js");pump(&engine,board);
  CHECK(engine.autostart==std::vector<std::string>({"alpha.js","gamma.js"})&&board.autostart()==engine.autostart);
  click(board,"auto:alpha.js");pump(&engine,board);
  CHECK(engine.autostart==std::vector<std::string>({"gamma.js"}));
  click(board,"auto-restart");pump(&engine,board);
  CHECK(!engine.autoRestart&&!board.autoRestart()&&engine.autostart.size()==1);
  click(board,"engine-autostart");pump(&engine,board);
  CHECK(engine.engineAutostart&&board.engineAutostart());
  // An edit made outside the dashboard appears at the next refresh.
  engine.autoRestart=true;now+=std::chrono::seconds(11);pump(&engine,board);
  CHECK(board.autoRestart());

  // A request whose reply never comes keeps its button busy, then clears on reconnect.
  engine.silent.insert("stop");click(board,"stop:3");pump(&engine,board);
  CHECK(!button(board,"stop:3")->enabled);
  engine.silent.clear();

  // Engine restart: offline, then reconnected and resynchronized.
  const size_t watches=countOp(engine,"watch");
  engine.server.stop();pump(nullptr,board);
  CHECK(!board.online()&&button(board,"start-engine"));
  CHECK(engine.start());
  pump(&engine,board);CHECK(!board.online());
  now+=std::chrono::seconds(2);pump(&engine,board);
  CHECK(board.online()&&countOp(engine,"watch")==watches+1&&button(board,"stop:3")->enabled);

  // Hidden: the connection closes and nothing is requested until the tab shows again.
  board.setVisible(false,now);CHECK(board.fd()<0);
  const size_t requests=engine.requests.size();
  now+=std::chrono::seconds(30);pump(&engine,board);
  CHECK(engine.requests.size()==requests&&engine.server.clientCount()==0);
  board.setVisible(true,now);pump(&engine,board);
  CHECK(board.online()&&countOp(engine,"watch")==watches+2);

  engine.server.stop();
  std::filesystem::remove_all(directory);
  control::stopJson();
  std::printf("dashboard-regression passed\n");
  return 0;
}
