// Control socket and operations with real Unix-socket clients and QuickJS scenes, no VR.
#include "../src/render_host.h"
#include <cassert>
#include <sys/stat.h>

namespace {
std::filesystem::path root;
control::Server *server=nullptr;control::Service *service=nullptr;
void pump(){server->poll([](control::Client &client,const std::string &line){return service->handle(client,line);});for(const auto &line:service->takeEvents())server->broadcast(line);}
struct Peer {
  int fd=-1;std::string input;
  explicit Peer(const std::filesystem::path &path){
    fd=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);sockaddr_un address{};address.sun_family=AF_UNIX;
    std::strncpy(address.sun_path,path.c_str(),sizeof(address.sun_path)-1);
    assert(fd>=0&&::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
  }
  ~Peer(){if(fd>=0)::close(fd);}
  void send(const std::string &line){const std::string text=line+"\n";size_t sent=0;while(sent<text.size()){auto n=::send(fd,text.data()+sent,text.size()-sent,MSG_NOSIGNAL);assert(n>0);sent+=size_t(n);}}
  // Pumps the engine side until one full line arrives; empty on EOF or timeout.
  std::string line(){
    for(int i=0;i<2000;i++){
      if(const auto end=input.find('\n');end!=std::string::npos){auto out=input.substr(0,end);input.erase(0,end+1);return out;}
      pump();char buffer[65536];const auto n=::recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
      if(n>0)input.append(buffer,size_t(n));else if(n==0)return {};else usleep(500);
    }
    return {};
  }
  std::string call(const std::string &request){send(request);return line();}
};
bool has(const std::string &text,const std::string &needle){
  if(text.find(needle)!=std::string::npos)return true;
  std::fprintf(stderr,"missing '%s' in: %s\n",needle.c_str(),text.c_str());return false;
}
std::string generationOf(const std::string &view){
  const auto at=view.find("\"generation\":");assert(at!=std::string::npos);
  return view.substr(at+13,view.find_first_of(",}",at+13)-at-13);
}
}

int main(){
  root=std::filesystem::temp_directory_path()/("pocketspace-control-"+std::to_string(getpid()));
  ScopeExit cleanup{[&]{std::filesystem::remove_all(root);}};
  const auto catalog=root/"catalog",socketPath=root/"run/pocketspace/control.sock";
  std::filesystem::create_directories(catalog);
  std::ofstream(catalog/"a.js")<<"globalThis.seen=[];subscribe('*',e=>seen.push(`${e.topic}|${e.sourceInstanceId}`));function update(){}";
  std::ofstream(catalog/"b.js")<<"globalThis.value=1;function update(){}";
  settings::File file(root/"settings.json");file.refresh();
  ScriptRegistry registry;assert(registry.enableScratch());
  int applied=0;
  control::Service operations(registry,catalog,file,{{},[&](const settings::Settings &s){applied++;registry.setAutoRestart(s.autoRestart);}});
  control::Server listener;service=&operations;server=&listener;
  std::string error;

  // Private directory and socket; a live engine is not replaced, a stale socket is.
  assert(listener.start(socketPath,error));
  struct stat info{};
  assert(::stat(socketPath.parent_path().c_str(),&info)==0&&(info.st_mode&0777)==0700);
  assert(::stat(socketPath.c_str(),&info)==0&&S_ISSOCK(info.st_mode)&&(info.st_mode&0777)==0600);
  {control::Server second;assert(!second.start(socketPath,error)&&error.find("already running")!=std::string::npos);}
  {
    const auto stale=root/"stale/control.sock";std::filesystem::create_directories(stale.parent_path());
    const int fd=::socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un address{};address.sun_family=AF_UNIX;std::strncpy(address.sun_path,stale.c_str(),sizeof(address.sun_path)-1);
    assert(::bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);::close(fd);
    control::Server replacement;assert(replacement.start(stale,error));replacement.stop();assert(!std::filesystem::exists(stale));
  }

  Peer one(socketPath),two(socketPath);
  // Malformed requests get structured errors and leave the connection usable.
  assert(has(one.call("not json"),"\"code\":\"bad_request\""));
  assert(has(one.call("[1]"),"request must be a JSON object"));
  assert(has(one.call(R"({"v":1,"op":"list"})"),"id must be a string"));
  assert(has(one.call(R"({"v":1,"id":")"+std::string(129,'x')+R"(","op":"list"})"),"id must be a string"));
  assert(has(one.call(R"({"v":2,"id":"a","op":"list"})"),"unsupported_version"));
  assert(has(one.call(R"({"v":1,"id":"b","op":"fly"})"),"unknown_op"));
  assert(has(one.call(R"({"v":1,"id":"c","op":"start"})"),"start requires a catalog name"));
  assert(has(one.call(R"({"v":1,"id":"d","op":"stop","instanceId":1})"),"requires the target's generation"));

  // Responsive with no user script: the catalog, scratch and stats.
  assert(has(one.call(R"({"v":1,"id":"cat","op":"catalog"})"),R"("scripts":["a.js","b.js"])"));
  const auto list=one.call(R"({"v":1,"id":"list","op":"list"})");
  assert(has(list,R"("instanceId":0,"name":"scratch")")&&has(list,R"("state":"running")"));
  assert(has(one.call(R"({"v":1,"id":"stats","op":"stats"})"),R"("instances":{"running":1)"));

  // Watch from the second client, then manage two scripts from the first.
  assert(has(two.call(R"({"v":1,"id":"w","op":"watch"})"),R"("watching":true)"));
  const auto a=one.call(R"({"v":1,"id":"start-a","op":"start","name":"a.js"})");
  assert(has(a,R"("ok":true)")&&has(a,R"("instanceId":1)")&&has(a,R"("generation":1)"));
  assert(has(two.line(),R"("event":"status")"));
  assert(has(one.call(R"({"v":1,"id":"start-b","op":"start","name":"b.js"})"),R"("instanceId":2)"));
  assert(has(two.line(),R"("instanceId":2)"));
  assert(has(one.call(R"({"v":1,"id":"start-b2","op":"start","name":"b.js"})"),R"("code":"conflict")"));
  assert(has(one.call(R"({"v":1,"id":"start-x","op":"start","name":"missing.js"})"),R"("code":"not_found")"));
  assert(has(one.call(R"({"v":1,"id":"start-esc","op":"start","name":"../x.js"})"),R"("code":"not_found")"));
  // Concurrent clients each get their own answers, matched by ID.
  one.send(R"({"v":1,"id":"s1","op":"status","instanceId":1})");two.send(R"({"v":1,"id":"s2","op":"status","instanceId":2})");
  const auto r1=one.line();std::string r2;
  for(std::string line;(line=two.line()).find("\"id\":\"s2\"")==std::string::npos;)assert(!line.empty());
  assert(has(r1,R"("id":"s1")")&&has(r1,R"("instanceId":1)"));

  // Reload checks the generation; a failed reload keeps the scene and says so.
  assert(has(one.call(R"({"v":1,"id":"r0","op":"reload","instanceId":2,"generation":7})"),R"("code":"stale_generation","message":"instance has a newer generation","generation":1)"));
  assert(has(one.call(R"({"v":1,"id":"r1","op":"reload","instanceId":2,"generation":1})"),R"("generation":2)"));
  std::ofstream(catalog/"b.js")<<"throw Error('EXPECTED_CONTROL_RELOAD')";
  const auto failed=one.call(R"({"v":1,"id":"r2","op":"reload","instanceId":2,"generation":2})");
  assert(has(failed,R"("code":"reload_failed")")&&has(failed,R"("lastReload":"failedKeptScene")")&&has(failed,R"("state":"running")"));
  std::ofstream(catalog/"b.js")<<"globalThis.value=1;function update(){}";

  // Eval: results, print output and exceptions; user scripts need a current generation.
  const auto sum=one.call(R"({"v":1,"id":"e1","op":"eval","code":"print('hi');globalThis.made=createObject(loadModel('assets/samples/Box/Box.glb'),0,0,0,.1);1+1"})");
  assert(has(sum,R"("result":"2")")&&has(sum,R"("output":"hi\n")")&&has(sum,R"("instanceId":0)"));
  assert(registry.find(0)->host.scene.objects.size()==1);
  assert(has(one.call(R"({"v":1,"id":"e2","op":"eval","code":"null.x"})"),R"("exception":"TypeError)"));
  assert(has(one.call(R"({"v":1,"id":"e3","op":"eval","instanceId":2,"code":"value"})"),"requires its generation"));
  assert(has(one.call(R"({"v":1,"id":"e4","op":"eval","instanceId":2,"generation":1,"code":"value"})"),"stale_generation"));
  assert(has(one.call(R"({"v":1,"id":"e5","op":"eval","instanceId":2,"generation":2,"code":"value+=1"})"),R"("result":"2")"));
  // A repeated request ID returns the first outcome without running again.
  const auto repeat=one.call(R"({"v":1,"id":"e5","op":"eval","instanceId":2,"generation":2,"code":"value+=1"})");
  assert(has(repeat,R"("result":"2")")&&has(repeat,R"("duplicate":true)"));
  assert(has(one.call(R"({"v":1,"id":"e6","op":"eval","instanceId":2,"generation":2,"code":"value"})"),R"("result":"2")"));
  // A tight loop is interrupted; that scene stops, others keep running.
  const auto spin=one.call(R"({"v":1,"id":"spin","op":"eval","instanceId":2,"generation":2,"code":"print('before');for(;;){}","timeoutMs":50})");
  assert(has(spin,R"("code":"timeout")")&&has(spin,R"("output":"before\n")"));
  assert(registry.find(2)->state==ScriptState::Failed&&registry.find(2)->lastError.find("timed out")!=std::string::npos);
  assert(registry.find(1)->state==ScriptState::Running);
  const auto scratchGeneration=registry.find(0)->generation;
  assert(has(one.call(R"({"v":1,"id":"spin0","op":"eval","code":"for(;;){}","timeoutMs":20})"),R"("code":"timeout")"));
  assert(registry.find(0)->state==ScriptState::Running&&registry.find(0)->generation==scratchGeneration+1&&registry.find(0)->host.scene.objects.empty());

  // Disconnect during a command: the outcome stays available by request ID.
  {Peer gone(socketPath);gone.send(R"({"v":1,"id":"lost","op":"start","name":"b.js"})");}
  for(int i=0;i<20;i++)pump();
  const auto lookup=one.call(R"({"v":1,"id":"look","op":"outcome","requestId":"lost"})");
  assert(has(lookup,R"("response":{"v":1,"id":"lost","ok":true)"));
  assert(has(one.call(R"({"v":1,"id":"look2","op":"outcome","requestId":"never"})"),R"("code":"not_found")"));
  // Stop needs the current generation.
  const auto b=std::to_string(registry.runningIds().back());
  const auto view=one.call(R"({"v":1,"id":"st","op":"status","instanceId":)"+b+"}");
  assert(has(one.call(R"({"v":1,"id":"stop","op":"stop","instanceId":)"+b+R"(,"generation":)"+generationOf(view)+"}"),R"("state":"stopped")"));
  assert(has(one.call(R"({"v":1,"id":"stop0","op":"stop","instanceId":0,"generation":0})"),"scratch cannot be stopped"));

  // Publish acts as scratch; scratch.reset clears only scratch.
  assert(has(one.call(R"({"v":1,"id":"p1","op":"publish","topic":"space.fromCli","payload":{"n":1}})"),R"("sequence":)"));
  assert(has(one.call(R"({"v":1,"id":"p2","op":"publish","topic":"engine.fake"})"),R"("code":"invalid")"));
  while(eventBus.queued())registry.dispatchBus();
  assert(has(one.call(R"j({"v":1,"id":"seen","op":"eval","instanceId":1,"generation":1,"code":"seen.join(',')"})j"),"space.fromCli|0"));
  one.call(R"({"v":1,"id":"g","op":"eval","code":"globalThis.keep=1"})");
  assert(has(one.call(R"({"v":1,"id":"reset","op":"scratch.reset"})"),R"("instanceId":0)"));
  assert(has(one.call(R"({"v":1,"id":"g2","op":"eval","code":"typeof keep"})"),R"("result":"\"undefined\"")"));
  assert(registry.find(1)->state==ScriptState::Running);

  // Settings: validated, written atomically and applied.
  assert(has(one.call(R"({"v":1,"id":"set0","op":"settings.set","settings":{"version":1,"autoRestart":"yes"}})"),R"("code":"invalid")"));
  assert(!std::filesystem::exists(root/"settings.json")&&applied==0);
  assert(has(one.call(R"({"v":1,"id":"set1","op":"settings.set","settings":{"version":1,"autostart":["a.js"],"autoRestart":true}})"),R"("autoRestart":true)"));
  assert(applied==1&&registry.autoRestart()&&std::filesystem::exists(root/"settings.json"));
  assert(has(one.call(R"({"v":1,"id":"get","op":"settings.get"})"),R"("autostart":["a.js"])"));

  // Scratch's scripts object: queries answer at once; start, stop and reload are checked
  // at once, applied between frames, and report through engine.* topics.
  scratchControl=[&](const std::string &request){return operations.handleScratch(request);};
  int evals=0;
  auto inScratch=[&](const std::string &code){
    const auto reply=one.call(R"({"v":1,"id":"scr)"+std::to_string(++evals)+R"(","op":"eval","code":)"+settings::jsonString(code)+"}");
    assert(has(reply,R"("ok":true)"));return reply;
  };
  assert(has(inScratch("scripts.catalog().join()"),R"("result":"\"a.js,b.js\"")"));
  assert(has(inScratch("scripts.list().map(i=>i.instanceId+':'+i.state).join()"),R"(0:running,1:running)"));
  assert(has(inScratch("scripts.status(1).name"),R"("result":"\"a.js\"")"));
  inScratch("globalThis.seen=[];for(const t of ['engine.scriptStarted','engine.scriptStopped','engine.scriptReloaded','engine.scriptReloadFailed'])subscribe(t,e=>seen.push(t.slice(13)+':'+e.payload.instanceId))");
  const auto runningBefore=registry.runningIds().size();
  assert(has(inScratch("scripts.start('b.js')"),R"("result":"undefined")"));
  assert(registry.runningIds().size()==runningBefore);
  operations.applyScratchQueue();
  const int started=registry.runningIds().back();
  assert(registry.runningIds().size()==runningBefore+1&&registry.find(started)->path.ends_with("b.js"));
  const auto codeOf=[&](const std::string &call){return inScratch("try{"+call+";'none'}catch(e){e.code??e.name}");};
  assert(has(codeOf("scripts.start('b.js')"),R"(\"conflict\")"));
  assert(has(codeOf("scripts.start('../x.js')"),R"(\"not_found\")"));
  assert(has(codeOf("scripts.stop(0,scripts.status(0).generation)"),R"(\"invalid\")"));
  assert(has(codeOf("scripts.stop(1,99)"),R"(\"stale_generation\")"));
  assert(has(codeOf("scripts.reload(99,1)"),R"(\"not_found\")"));
  assert(has(codeOf("scripts.stop('1')"),R"(\"TypeError\")"));
  assert(has(codeOf("scripts.status(-1)"),R"(\"TypeError\")"));
  // A failed reload keeps the scene and is reported on the bus.
  std::ofstream(catalog/"b.js")<<"throw Error('EXPECTED_SCRATCH_RELOAD')";
  inScratch("scripts.reload("+std::to_string(started)+",1)");operations.applyScratchQueue();
  assert(registry.find(started)->state==ScriptState::Running&&registry.find(started)->lastReloadOk==false);
  std::ofstream(catalog/"b.js")<<"globalThis.value=1;function update(){}";
  inScratch("scripts.reload("+std::to_string(started)+",1)");operations.applyScratchQueue();
  assert(registry.find(started)->generation==2);
  // A call from a bus callback, inside the frame, waits for the next boundary.
  inScratch("subscribe('space.stopB',()=>scripts.stop("+std::to_string(started)+",2))");
  assert(has(one.call(R"({"v":1,"id":"pb","op":"publish","topic":"space.stopB"})"),R"("sequence":)"));
  while(eventBus.queued())registry.dispatchBus();
  assert(registry.find(started)->state==ScriptState::Running);
  operations.applyScratchQueue();
  assert(registry.find(started)->state==ScriptState::Stopped);
  while(eventBus.queued())registry.dispatchBus();
  const auto id=std::to_string(started);
  assert(has(inScratch("seen.join()"),"Started:"+id+",ReloadFailed:"+id+",Reloaded:"+id+",Stopped:"+id));
  // A queued request that is stale by the boundary does nothing.
  inScratch("scripts.reload(1,1)");
  assert(has(one.call(R"({"v":1,"id":"r-a","op":"reload","instanceId":1,"generation":1})"),R"("generation":2)"));
  operations.applyScratchQueue();
  assert(registry.find(1)->generation==2);
  // Only scratch has the scripts object.
  assert(has(one.call(R"({"v":1,"id":"noscripts","op":"eval","instanceId":1,"generation":2,"code":"typeof scripts"})"),R"("result":"\"undefined\"")"));
  scratchControl={};
  assert(has(codeOf("scripts.list()"),R"(\"unavailable\")"));

  // Watchers saw the failure and the bus remains bounded; oversized requests close the link.
  std::string events;for(std::string line;!(line=two.line()).empty()&&events.find("\"event\":\"error\"")==std::string::npos;)events+=line;
  assert(has(events,R"("event":"error")")&&has(events,"timed out"));
  {
    Peer big(socketPath);const std::string huge(control::maxLineBytes+70000,'x');
    // The engine reads only while pumped, so send without blocking.
    for(size_t sent=0;sent<huge.size();){
      const auto n=::send(big.fd,huge.data()+sent,huge.size()-sent,MSG_NOSIGNAL|MSG_DONTWAIT);
      if(n>0)sent+=size_t(n);else if(errno==EAGAIN)pump();else break;
    }
    assert(has(big.line(),R"("code":"too_large")"));assert(big.line().empty());
  }
  // Shutdown removes the socket and ends every connection.
  listener.stop();
  assert(!std::filesystem::exists(socketPath));
  char byte;assert(::recv(one.fd,&byte,1,0)==0);
  std::puts("PASS control: scratch scripts object (queries, checked and queued start/stop/reload, bus outcomes, calls from callbacks, stale queued requests, scratch only), private socket and single instance, stale socket replacement, malformed requests, catalog/list/stats with no user script, start/stop/reload with generations, concurrent clients, watch events, eval results/output/exceptions/timeouts, duplicate IDs, outcome lookup after disconnect, publish as scratch, scratch reset, settings, oversized requests and shutdown");
}
