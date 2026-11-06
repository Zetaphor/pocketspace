// pocketspace-cli: manage a running engine over its control socket, and a live REPL.
// One-shot commands are noninteractive; --json prints one versioned JSON object.
// See docs/cli.md for commands, output and exit codes.
#include "control_client.h"
#include <editline/readline.h>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <optional>
#include <poll.h>
#include <sstream>
#include <sys/wait.h>

namespace {
using control::Clock;
// The protocol's request ID limit, from control_service.h, which needs the engine.
constexpr size_t maxRequestIdBytes=128;

// Stable exit codes; docs/cli.md lists them.
enum Exit {
  Success=0,Failure=1,Usage=2,Offline=3,NoResponse=4,NotFound=5,Conflict=6,Stale=7,Rejected=8,LoadFailed=9,
  EvalException=10,EvalTimeout=11,Unavailable=12,StartFailed=13,
};
int exitFor(const std::string &code){
  static const std::map<std::string,int> codes{
    {"not_found",NotFound},{"conflict",Conflict},{"stale_generation",Stale},{"bad_request",Rejected},{"invalid",Rejected},
    {"unknown_op",Rejected},{"unsupported_version",Rejected},{"too_large",Rejected},{"load_failed",LoadFailed},
    {"reload_failed",LoadFailed},{"timeout",EvalTimeout},{"busy",Unavailable},{"unavailable",Unavailable},
    {"offline",Offline},{"no_response",NoResponse},{"unknown_outcome",NoResponse},{"usage",Usage},{"start_failed",StartFailed}};
  const auto found=codes.find(code);return found==codes.end()?Failure:found->second;
}

using control::quote;using control::Json;using control::Link;

struct Options {
  bool json=false;
  std::filesystem::path socket=control::defaultSocketPath();
  std::optional<double> timeoutSeconds;
  std::string requestId;
  double timeout(double fallback=5)const{return timeoutSeconds.value_or(fallback);}
};
Options options;

std::string newRequestId(){return control::newRequestId("cli");}

// A request's outcome as the CLI sees it: the engine's reply, or a client-side failure.
struct Reply {
  std::string line;Json json;
  bool ok()const{return json["ok"].truthy();}
  std::string code()const{return json["error"]["code"].str();}
};
Reply clientFailure(const std::string &id,const std::string &code,const std::string &message,const std::string &extra=""){
  Reply reply;
  reply.line="{\"v\":1,\"id\":"+(id.empty()?std::string("null"):quote(id))+",\"ok\":false,\"error\":{\"code\":\""+code+"\",\"message\":"+quote(message)+extra+"}}";
  reply.json=*Json::parse(reply.line);return reply;
}
Reply success(const std::string &result){Reply reply;reply.line="{\"v\":1,\"id\":null,\"ok\":true,\"result\":"+result+"}";reply.json=*Json::parse(reply.line);return reply;}

const std::set<std::string> changing{"start","stop","reload","scratch.reset","data.reset","settings.set","publish","eval"};

// Sends one request and waits for its reply. A changing request whose reply never
// arrives has an unknown outcome; it is never sent again automatically.
Reply call(Link &link,const std::string &op,const std::string &fields,double seconds,const std::string &chosenId={}){
  const std::string id=chosenId.empty()?newRequestId():chosenId;
  if(!link.open()&&!link.connect(options.socket))return clientFailure(id,"offline","engine is not running (no control socket at "+options.socket.string()+")");
  const std::string request="{\"v\":1,\"id\":"+quote(id)+",\"op\":"+quote(op)+fields+"}";
  if(request.size()>control::maxLineBytes)return clientFailure(id,"usage","request exceeds 1 MiB");
  const bool mutates=changing.count(op);
  auto lostReply=[&](const char *what){
    if(!mutates)return clientFailure(id,"no_response",std::string(what)+"; the request can be retried");
    return clientFailure(id,"unknown_outcome",std::string(what)+"; outcome unknown. Check it with: pocketspace-cli outcome "+id,",\"requestId\":"+quote(id));
  };
  if(!link.send(request)){link.close();return lostReply("connection lost while sending");}
  const auto deadline=Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
  for(;;){
    bool lost=false;const auto line=link.line(deadline,lost);
    if(!line)return lostReply(lost?"connection lost before the reply":"no reply before the timeout");
    auto json=Json::parse(*line);
    if(!json)continue;
    if(!(*json)["event"].isNull())continue;
    if((*json)["id"].str()!=id&&!(*json)["id"].isNull())continue;
    return Reply{*line,*json};
  }
}

// Prints a reply for one-shot commands and returns its exit code.
int finish(const Reply &reply,const std::function<void(const Json&)> &human){
  if(options.json)std::printf("%s\n",reply.line.c_str());
  if(!reply.ok()){
    if(!options.json)std::fprintf(stderr,"error: %s (%s)\n",reply.json["error"]["message"].str().c_str(),reply.code().c_str());
    return exitFor(reply.code());
  }
  if(!options.json&&human)human(reply.json["result"]);
  return Success;
}

std::string uptime(double seconds){
  char buffer[32];const long s=long(seconds);
  if(s>=3600)std::snprintf(buffer,sizeof(buffer),"%ldh%02ldm",s/3600,s/60%60);
  else if(s>=60)std::snprintf(buffer,sizeof(buffer),"%ldm%02lds",s/60,s%60);
  else std::snprintf(buffer,sizeof(buffer),"%lds",s);
  return buffer;
}
std::string bytes(const Json &value){
  if(value.isNull())return "-";
  char text[32];const double b=value.number();
  if(b>=1048576)std::snprintf(text,sizeof(text),"%.1f MiB",b/1048576);else if(b>=1024)std::snprintf(text,sizeof(text),"%.1f KiB",b/1024);else std::snprintf(text,sizeof(text),"%.0f B",b);
  return text;
}
std::string decimal(const Json &value,const char *format){
  if(value.isNull())return "-";
  char text[32];std::snprintf(text,sizeof(text),format,value.number());return text;
}
void printInstance(const Json &instance){
  std::printf("instance    %lld\nname        %s\npath        %s\nstate       %s\ngeneration  %lld\nuptime      %s\n",
    (long long)instance["instanceId"].number(),instance["name"].str().c_str(),instance["path"].str().c_str(),instance["state"].str().c_str(),
    (long long)instance["generation"].number(),uptime(instance["uptimeSeconds"].number()).c_str());
  std::printf("last error  %s\nlast reload %s\nrestarting  %s\n",instance["lastError"].isNull()?"-":instance["lastError"].str().c_str(),
    instance["lastReload"].isNull()?"-":instance["lastReload"].str().c_str(),instance["restartPending"].truthy()?"yes":"no");
}
void printInstanceLine(const Json &instance){
  const auto error=instance["lastError"].str();
  std::printf("%-4lld %-28s %-8s %-4lld %-8s %s\n",(long long)instance["instanceId"].number(),instance["name"].str().c_str(),instance["state"].str().c_str(),
    (long long)instance["generation"].number(),instance["state"].str()=="running"?uptime(instance["uptimeSeconds"].number()).c_str():"-",error.empty()?"":error.c_str());
}

bool parseInteger(const std::string &text,long long &out,long long minimum=0){
  if(text.empty()||text.size()>15)return false;
  for(char c:text)if(c<'0'||c>'9')return false;
  out=std::stoll(text);return out>=minimum;
}

int usage(const std::string &message){
  if(options.json)std::printf("%s\n",clientFailure("","usage",message).line.c_str());
  std::fprintf(stderr,"error: %s\nRun pocketspace-cli --help for usage.\n",message.c_str());
  return Usage;
}

const char *helpText=R"(Usage: pocketspace-cli [options] <command> [arguments]

Commands:
  start-engine                 Start the engine's user service if it is not running
  list                         All instances this session, including scratch (0)
  catalog                      Scripts available to start
  status <id>                  One instance
  start <name>                 Start a catalog script
  stop <id> [--generation N]   Stop a script (generation looked up if omitted)
  reload <id> [--generation N] Reload a script from its file
  stats                        Engine totals and each script's resources
  watch [--limit N] [--duration S]
                               Stream status, error, bus-drop and resource events
  publish <topic> [json]       Publish a space.* event as scratch
  scratch reset                Clear and recreate the scratch scene
  data reset <name>            Delete a catalog script's saves; a running script reloads
  autostart list|add <name>|remove <name>
                               Edit the scripts started with the engine
  eval (--code TEXT | --file PATH) [--script ID [--generation N]] [--eval-timeout MS]
                               Run JavaScript in scratch or a live script
  outcome <request-id>         The remembered reply of a recent changing request
  repl [--eval-timeout MS]     Interactive JavaScript, in scratch by default

Options:
  --json             One JSON object on stdout (watch: one per line); no prompts
  --socket PATH      Control socket (default $XDG_RUNTIME_DIR/pocketspace/control.sock)
  --timeout SECONDS  Wait for a reply (default 5; start-engine 30), added to eval's limit
  --request-id ID    Use this request ID for a changing command
  -h, --help         Show this help
)";

// --name value pairs after a command's positional arguments.
struct Arguments {
  std::vector<std::string> positional;std::map<std::string,std::string> named;std::string error;
  Arguments(const std::vector<std::string> &args,const std::set<std::string> &allowed){
    for(size_t i=0;i<args.size();i++){
      if(args[i].rfind("--",0)!=0||args[i]=="--"){positional.push_back(args[i]);continue;}
      if(!allowed.count(args[i])){error="unknown option "+args[i];return;}
      if(i+1==args.size()){error=args[i]+" requires a value";return;}
      named[args[i]]=args[i+1];i++;
    }
  }
  std::optional<std::string> get(const std::string &name)const{auto f=named.find(name);return f==named.end()?std::nullopt:std::optional(f->second);}
};

// Looks up an instance's current generation for commands that target one.
std::optional<Reply> lookupGeneration(Link &link,long long id,long long &generation){
  const auto reply=call(link,"status",",\"instanceId\":"+std::to_string(id),options.timeout());
  if(!reply.ok())return reply;
  generation=(long long)reply.json["result"]["generation"].number();return std::nullopt;
}

int targeted(const std::string &op,const std::vector<std::string> &args){
  Arguments a(args,{"--generation"});
  long long id=0,generation=0;
  if(!a.error.empty())return usage(a.error);
  if(a.positional.size()!=1||!parseInteger(a.positional[0],id))return usage(op+" needs one instance ID");
  Link link;
  if(const auto given=a.get("--generation")){if(!parseInteger(*given,generation,1))return usage("--generation must be a positive integer");}
  else if(auto failed=lookupGeneration(link,id,generation))return finish(*failed,{});
  const auto reply=call(link,op,",\"instanceId\":"+std::to_string(id)+",\"generation\":"+std::to_string(generation),options.timeout(),options.requestId);
  return finish(reply,[&](const Json &r){
    std::printf("%s %lld %s (generation %lld)\n",op=="stop"?"stopped":"reloaded",(long long)r["instanceId"].number(),r["name"].str().c_str(),(long long)r["generation"].number());
  });
}

int startEngine(){
  const double seconds=options.timeout(30);
  const auto running=[&]{Link probe;return probe.connect(options.socket)&&call(probe,"list","",2).ok();};
  if(running()){
    const auto reply=success("{\"started\":false,\"socket\":"+quote(options.socket.string())+"}");
    return finish(reply,[&](const Json&){std::printf("engine already running\n");});
  }
  // The service manager runs the engine; this never starts SteamVR.
  const pid_t child=fork();
  if(child==0){
    if(options.json){const int null=::open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDOUT_FILENO);::close(null);}}
    execlp("systemctl","systemctl","--user","start","pocketspace.service",nullptr);_exit(127);
  }
  int status=0;
  if(child<0||waitpid(child,&status,0)<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)
    return finish(clientFailure("","start_failed","systemctl --user start pocketspace.service failed; is the service installed (tools/install-user-service.sh)?"),{});
  const auto deadline=Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
  while(Clock::now()<deadline){
    if(running())return finish(success("{\"started\":true,\"socket\":"+quote(options.socket.string())+"}"),[&](const Json&){std::printf("engine started\n");});
    usleep(200000);
  }
  return finish(clientFailure("","start_failed","the service started but the engine did not answer within the timeout; check journalctl --user -u pocketspace.service"),{});
}

volatile sig_atomic_t interrupted=0;
void onInterrupt(int){interrupted=1;}

int watch(const std::vector<std::string> &args){
  Arguments a(args,{"--limit","--duration"});
  if(!a.error.empty())return usage(a.error);
  if(!a.positional.empty())return usage("watch takes no arguments");
  long long limit=0;double duration=0;
  if(const auto v=a.get("--limit");v&&!parseInteger(*v,limit,1))return usage("--limit must be a positive integer");
  if(const auto v=a.get("--duration")){char *end=nullptr;duration=std::strtod(v->c_str(),&end);if(*end||!(duration>0))return usage("--duration must be a positive number of seconds");}
  std::signal(SIGINT,onInterrupt);std::signal(SIGTERM,onInterrupt);
  const auto deadline=duration>0?Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(duration)):Clock::time_point::max();
  Link link;long long received=0;bool first=true;
  auto marker=[&](const std::string &event,const std::string &extra,const std::string &human){
    if(options.json)std::printf("{\"v\":1,\"event\":\"%s\"%s}\n",event.c_str(),extra.c_str());else std::printf("%s\n",human.c_str());
    std::fflush(stdout);
  };
  while(!interrupted&&Clock::now()<deadline){
    if(!link.open()){
      const auto reply=call(link,"watch","",options.timeout());
      if(!reply.ok()){
        if(first)return finish(reply,{});
        link.close();
        for(int i=0;i<10&&!interrupted&&Clock::now()<deadline;i++)usleep(100000);
        continue;
      }
      const auto seq=(long long)reply.json["result"]["seq"].number();
      // Events between the loss and this point were not seen; list again to resynchronize.
      if(!first)marker("reconnected",",\"seq\":"+std::to_string(seq),"-- reconnected; events while disconnected were missed");
      first=false;
    }
    bool lost=false;
    const auto line=link.line(std::min(deadline,Clock::now()+std::chrono::milliseconds(250)),lost);
    if(lost){marker("connectionLost","","-- connection lost; reconnecting");continue;}
    if(!line)continue;
    const auto event=Json::parse(*line);
    if(!event||(*event)["event"].isNull())continue;
    if(options.json)std::printf("%s\n",line->c_str());
    else{
      const auto type=(*event)["event"].str();const auto seq=(long long)(*event)["seq"].number();
      if(type=="status"){const auto i=(*event)["instance"];std::printf("#%lld status %lld %s %s generation %lld\n",seq,(long long)i["instanceId"].number(),i["name"].str().c_str(),i["state"].str().c_str(),(long long)i["generation"].number());}
      else if(type=="error")std::printf("#%lld error %lld %s\n",seq,(long long)(*event)["instanceId"].number(),(*event)["message"].str().c_str());
      else if(type=="resources"){
        std::printf("#%lld resources %s fps, %s CPU, %s RAM\n",seq,decimal((*event)["fps"],"%.1f").c_str(),decimal((*event)["cpuPercent"],"%.1f%%").c_str(),bytes((*event)["ramBytes"]).c_str());
        const auto list=(*event)["instances"];
        for(uint32_t i=0;i<list.length();i++){const auto x=list[i];if(x["state"].str()!="running")continue;
          std::printf("    %lld %s heap %s, %s ms/s callbacks (worst %s ms), GPU est. %s\n",(long long)x["instanceId"].number(),x["name"].str().c_str(),bytes(x["heapBytes"]).c_str(),
            decimal(x["callbackMsPerSecond"],"%.2f").c_str(),decimal(x["worstCallbackMs"],"%.2f").c_str(),bytes(x["gpuEstimateBytes"]).c_str());}
      }
      else if(type=="busDrop")std::printf("#%lld busDrop dropped %lld total %lld\n",seq,(long long)(*event)["dropped"].number(),(long long)(*event)["total"].number());
      else std::printf("#%lld %s\n",seq,line->c_str());
    }
    std::fflush(stdout);
    if(limit&&++received>=limit)break;
  }
  return Success;
}

int autostart(const std::vector<std::string> &args){
  if(args.empty())return usage("autostart needs list, add <name> or remove <name>");
  const auto &action=args[0];
  if(action=="list"?args.size()!=1:(action!="add"&&action!="remove")||args.size()!=2)return usage("autostart needs list, add <name> or remove <name>");
  Link link;
  const auto current=call(link,"settings.get","",options.timeout());
  if(!current.ok())return finish(current,{});
  const auto settings=current.json["result"]["settings"];const auto list=settings["autostart"];
  std::vector<std::string> names;for(uint32_t i=0;i<list.length();i++)names.push_back(list[i].str());
  if(action=="list"){
    std::string result="[";for(size_t i=0;i<names.size();i++)result+=(i?",":"")+quote(names[i]);
    return finish(success("{\"autostart\":"+result+"]}"),[&](const Json&){for(const auto &name:names)std::printf("%s\n",name.c_str());});
  }
  const auto &name=args[1];const bool present=std::find(names.begin(),names.end(),name)!=names.end();
  if(action=="add"){
    if(present)return finish(clientFailure("","conflict",name+" is already in the autostart list"),{});
    const auto catalog=call(link,"catalog","",options.timeout());
    if(!catalog.ok())return finish(catalog,{});
    bool known=false;const auto scripts=catalog.json["result"]["scripts"];
    for(uint32_t i=0;i<scripts.length();i++)known|=scripts[i].str()==name;
    if(!known)return finish(clientFailure("","not_found",name+" is not in the script catalog"),{});
    names.push_back(name);
  }else{
    if(!present)return finish(clientFailure("","not_found",name+" is not in the autostart list"),{});
    names.erase(std::find(names.begin(),names.end(),name));
  }
  std::string updated="[";for(size_t i=0;i<names.size();i++)updated+=(i?",":"")+quote(names[i]);updated+="]";
  const std::string body="{\"version\":1,\"autostart\":"+updated+",\"autoRestart\":"+(settings["autoRestart"].truthy()?"true":"false")
    +",\"engineAutostart\":"+(settings["engineAutostart"].truthy()?"true":"false")+"}";
  const auto reply=call(link,"settings.set",",\"settings\":"+body,options.timeout(),options.requestId);
  return finish(reply,[&](const Json&){std::printf("%s %s\n",action=="add"?"added":"removed",name.c_str());});
}

// Engine totals, then one row per instance. Estimates are labelled as such.
void printResources(const Json &r){
  std::printf("engine     %s fps, %s CPU, %s RAM, %s GPU memory (driver)\n",decimal(r["fps"],"%.1f").c_str(),decimal(r["cpuPercent"],"%.1f%%").c_str(),
    bytes(r["ramBytes"]).c_str(),r["vramBytes"].isNull()?"unavailable":bytes(r["vramBytes"]).c_str());
  std::printf("estimates  %s GPU allocated, of which %s shared between scripts\n",bytes(r["gpuEstimateBytes"]).c_str(),bytes(r["sharedGpuEstimateBytes"]).c_str());
  std::printf("bus        %lld queued (%s), %lld dropped\n",(long long)r["bus"]["queued"].number(),bytes(r["bus"]["queuedBytes"]).c_str(),(long long)r["bus"]["dropped"].number());
  std::printf("%-4s %-24s %-8s %10s %7s %6s %6s %6s %12s %8s %8s\n","ID","NAME","STATE","JS HEAP","OBJECTS","PANELS","ASSETS","BODIES","GPU (EST.)","CB MS/S","WORST MS");
  const auto list=r["instances"];
  for(uint32_t i=0;i<list.length();i++){
    const auto x=list[i];
    auto count=[](const Json &v){return v.isNull()?std::string("-"):std::to_string((long long)v.number());};
    std::printf("%-4lld %-24s %-8s %10s %7s %6s %6s %6s %12s %8s %8s\n",(long long)x["instanceId"].number(),x["name"].str().c_str(),x["state"].str().c_str(),
      bytes(x["heapBytes"]).c_str(),count(x["objects"]).c_str(),count(x["panels"]).c_str(),count(x["assets"]).c_str(),count(x["bodies"]).c_str(),
      bytes(x["gpuEstimateBytes"]).c_str(),decimal(x["callbackMsPerSecond"],"%.2f").c_str(),decimal(x["worstCallbackMs"],"%.2f").c_str());
  }
}

// Prints an eval reply's output and result for people. Returns true if it threw.
bool showEval(const Json &result){
  const auto output=result["output"].str();
  if(!output.empty())std::fputs(output.c_str(),stdout);
  if(!result["exception"].isNull()){std::fprintf(stderr,"Uncaught %s\n",result["exception"].str().c_str());return true;}
  std::printf("%s\n",result["result"].str().c_str());return false;
}

int evalCommand(const std::vector<std::string> &args){
  Arguments a(args,{"--code","--file","--script","--generation","--eval-timeout"});
  if(!a.error.empty())return usage(a.error);
  if(!a.positional.empty())return usage("eval takes --code or --file");
  const auto code=a.get("--code"),file=a.get("--file");
  if(bool(code)==bool(file))return usage("eval needs exactly one of --code or --file");
  std::string source;
  if(code)source=*code;
  else{std::ifstream in(*file,std::ios::binary);if(!in)return usage("cannot read "+*file);std::ostringstream s;s<<in.rdbuf();source=s.str();}
  long long instance=0,generation=0,limit=1000;
  if(const auto v=a.get("--eval-timeout");v&&(!parseInteger(*v,limit,1)||limit>10000))return usage("--eval-timeout must be 1 to 10000 ms");
  if(a.get("--generation")&&!a.get("--script"))return usage("--generation needs --script");
  Link link;
  if(const auto v=a.get("--script")){
    if(!parseInteger(*v,instance,1))return usage("--script needs a user script instance ID");
    if(const auto g=a.get("--generation")){if(!parseInteger(*g,generation,1))return usage("--generation must be a positive integer");}
    else if(auto failed=lookupGeneration(link,instance,generation))return finish(*failed,{});
  }
  std::string fields=",\"code\":"+quote(source)+",\"timeoutMs\":"+std::to_string(limit);
  if(instance)fields+=",\"instanceId\":"+std::to_string(instance)+",\"generation\":"+std::to_string(generation);
  const auto reply=call(link,"eval",fields,options.timeout()+limit/1000.,options.requestId);
  if(options.json){std::printf("%s\n",reply.line.c_str());return reply.ok()?(reply.json["result"]["exception"].isNull()?Success:EvalException):exitFor(reply.code());}
  if(!reply.ok()){
    const auto output=reply.json["error"]["output"].str();if(!output.empty())std::fputs(output.c_str(),stdout);
    std::fprintf(stderr,"error: %s (%s)\n",reply.json["error"]["message"].str().c_str(),reply.code().c_str());return exitFor(reply.code());
  }
  return showEval(reply.json["result"])?EvalException:Success;
}

// True while brackets, a template string or a block comment are still open.
bool incomplete(const std::string &source){
  int depth=0;char quoted=0;bool escaped=false,lineComment=false,blockComment=false;
  for(size_t i=0;i<source.size();i++){
    const char c=source[i],next=i+1<source.size()?source[i+1]:0;
    if(lineComment){if(c=='\n')lineComment=false;continue;}
    if(blockComment){if(c=='*'&&next=='/'){blockComment=false;i++;}continue;}
    if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c==quoted)quoted=0;continue;}
    if(c=='/'&&next=='/'){lineComment=true;i++;}
    else if(c=='/'&&next=='*'){blockComment=true;i++;}
    else if(c=='"'||c=='\''||c=='`')quoted=c;
    else if(c=='('||c=='['||c=='{')depth++;
    else if(c==')'||c==']'||c=='}')depth--;
  }
  return depth>0||quoted=='`'||blockComment;
}

const char *replHelp=R"(JavaScript runs in the scratch scene (instance 0) unless you enter a script.
Scratch objects and globals stay after you exit; `pocketspace-cli scratch reset` clears them.
Code runs in the live scene immediately; it is not a transactional reload.
  :enter <id>  Run following input in that running script's context
  :leave       Return to scratch
  :where       Show the current target and its generation
  :help        Show this help
Unbalanced brackets continue on the next line; an empty line submits anyway.
Arrow keys recall earlier input; Ctrl-R searches it.
Ctrl-D exits. In scratch, scripts.list(), scripts.start(name) and publish(topic, payload) manage other scripts.
)";

int repl(const std::vector<std::string> &args){
  Arguments a(args,{"--eval-timeout"});
  if(!a.error.empty())return usage(a.error);
  if(!a.positional.empty())return usage("repl takes no arguments");
  if(options.json)return usage("repl is interactive; use eval --json for scripted evaluation");
  long long limit=1000;
  if(const auto v=a.get("--eval-timeout");v&&(!parseInteger(*v,limit,1)||limit>10000))return usage("--eval-timeout must be 1 to 10000 ms");
  const bool tty=isatty(STDIN_FILENO);
  Link link,watcher;
  {const auto first=call(link,"list","",options.timeout());if(!first.ok())return finish(first,{});}
  // A second connection reports reloads and stops of the entered script between inputs.
  if(!call(watcher,"watch","",options.timeout()).ok())watcher.close();
  struct Target {long long id=0,generation=0;std::string name="scratch";} target;
  auto leave=[&](const std::string &why){std::printf("%s; back in scratch. Use :enter again to continue.\n",why.c_str());target={};};
  auto checkEvents=[&]{
    while(watcher.open()){
      bool lost=false;const auto line=watcher.line(Clock::now(),lost);
      if(!line)break;
      const auto event=Json::parse(*line);
      if(!event||(*event)["event"].str()!="status"||!target.id)continue;
      const auto instance=(*event)["instance"];
      if((long long)instance["instanceId"].number()!=target.id)continue;
      if(instance["state"].str()!="running")leave("instance "+std::to_string(target.id)+" "+instance["state"].str());
      else if((long long)instance["generation"].number()!=target.generation)leave("instance "+std::to_string(target.id)+" reloaded (generation "+std::to_string((long long)instance["generation"].number())+")");
    }
  };
  auto read=[&](const std::string &prompt)->std::optional<std::string>{
    if(tty){char *raw=readline(prompt.c_str());if(!raw)return std::nullopt;std::string line=raw;std::free(raw);return line;}
    std::string line;if(!std::getline(std::cin,line))return std::nullopt;return line;
  };
  if(tty)std::printf("Pocketspace REPL in scratch. :help for commands, Ctrl-D to exit.\n");
  for(;;){
    checkEvents();
    const std::string prompt=target.id?std::to_string(target.id)+":"+target.name+"@"+std::to_string(target.generation)+"> ":"scratch> ";
    auto line=read(prompt);
    if(!line){if(tty)std::printf("\n");break;}
    if(line->empty())continue;
    if((*line)[0]==':'){
      if(tty)add_history(line->c_str());
      std::istringstream words(line->substr(1));std::string command,argument,extra;words>>command>>argument>>extra;
      if(command=="help")std::fputs(replHelp,stdout);
      else if(command=="where"){
        if(target.id)std::printf("instance %lld %s, generation %lld\n",target.id,target.name.c_str(),target.generation);
        else std::printf("scratch (instance 0)\n");
      }else if(command=="leave"){if(target.id)target={};std::printf("in scratch\n");}
      else if(command=="enter"){
        long long id=0;
        if(!parseInteger(argument,id,1)||!extra.empty()){std::printf(":enter needs one user script instance ID\n");continue;}
        const auto reply=call(link,"status",",\"instanceId\":"+std::to_string(id),options.timeout());
        if(!reply.ok()){std::printf("cannot enter %lld: %s\n",id,reply.json["error"]["message"].str().c_str());continue;}
        const auto r=reply.json["result"];
        if(r["state"].str()!="running"){std::printf("cannot enter %lld: it is %s\n",id,r["state"].str().c_str());continue;}
        // Drop status events from before entering so they do not end this session.
        checkEvents();
        target={id,(long long)r["generation"].number(),r["name"].str()};
        std::printf("entered %lld %s, generation %lld. Input now changes that live scene.\n",id,target.name.c_str(),target.generation);
      }else std::printf("unknown command :%s (:help lists commands)\n",command.c_str());
      continue;
    }
    std::string source=*line;
    while(incomplete(source)){
      auto more=read("... ");
      if(!more||more->empty())break;
      source+="\n"+*more;
    }
    if(tty)add_history(source.c_str());
    std::string fields=",\"code\":"+quote(source)+",\"timeoutMs\":"+std::to_string(limit);
    if(target.id)fields+=",\"instanceId\":"+std::to_string(target.id)+",\"generation\":"+std::to_string(target.generation);
    const auto reply=call(link,"eval",fields,options.timeout()+limit/1000.);
    if(reply.ok()){showEval(reply.json["result"]);std::fflush(stdout);continue;}
    const auto code=reply.code();
    const auto output=reply.json["error"]["output"].str();if(!output.empty())std::fputs(output.c_str(),stdout);
    if(code=="unknown_outcome"){std::printf("%s\nThe input was not sent again.\n",reply.json["error"]["message"].str().c_str());link.close();}
    else if(code=="offline"){std::printf("%s\n",reply.json["error"]["message"].str().c_str());}
    else if(target.id&&(code=="stale_generation"||code=="not_found"))leave("instance "+std::to_string(target.id)+" was reloaded or stopped");
    else if(code=="timeout"){
      std::printf("%s\n",reply.json["error"]["message"].str().c_str());
      if(target.id)leave("instance "+std::to_string(target.id)+" stopped");
      else std::printf("scratch was recreated empty.\n");
    }else std::printf("error: %s (%s)\n",reply.json["error"]["message"].str().c_str(),code.c_str());
    std::fflush(stdout);
  }
  return Success;
}

int run(const std::string &command,const std::vector<std::string> &args){
  Link link;
  auto none=[&](const char *name)->bool{return args.empty()||(usage(std::string(name)+" takes no arguments"),false);};
  if(command=="start-engine"){if(!args.empty())return usage("start-engine takes no arguments");return startEngine();}
  if(command=="list"){
    if(!none("list"))return Usage;
    return finish(call(link,"list","",options.timeout()),[](const Json &r){
      std::printf("%-4s %-28s %-8s %-4s %-8s %s\n","ID","NAME","STATE","GEN","UPTIME","LAST ERROR");
      const auto list=r["instances"];for(uint32_t i=0;i<list.length();i++)printInstanceLine(list[i]);
    });
  }
  if(command=="catalog"){
    if(!none("catalog"))return Usage;
    return finish(call(link,"catalog","",options.timeout()),[](const Json &r){const auto s=r["scripts"];for(uint32_t i=0;i<s.length();i++)std::printf("%s\n",s[i].str().c_str());});
  }
  if(command=="status"){
    long long id=0;if(args.size()!=1||!parseInteger(args[0],id))return usage("status needs one instance ID");
    return finish(call(link,"status",",\"instanceId\":"+std::to_string(id),options.timeout()),printInstance);
  }
  if(command=="start"){
    if(args.size()!=1||args[0].rfind("--",0)==0)return usage("start needs one catalog name");
    return finish(call(link,"start",",\"name\":"+quote(args[0]),options.timeout(),options.requestId),[](const Json &r){
      std::printf("started %lld %s (generation %lld)\n",(long long)r["instanceId"].number(),r["name"].str().c_str(),(long long)r["generation"].number());});
  }
  if(command=="stop"||command=="reload")return targeted(command,args);
  if(command=="stats"){
    if(!none("stats"))return Usage;
    return finish(call(link,"stats","",options.timeout()),printResources);
  }
  if(command=="watch")return watch(args);
  if(command=="publish"){
    if(args.empty()||args.size()>2)return usage("publish needs a topic and optional JSON payload");
    std::string fields=",\"topic\":"+quote(args[0]);
    if(args.size()==2){const auto payload=Json::parse(args[1]);if(!payload)return usage("payload is not valid JSON");fields+=",\"payload\":"+payload->text();}
    return finish(call(link,"publish",fields,options.timeout(),options.requestId),[](const Json &r){
      if(r["sequence"].isNull())std::printf("dropped: the event bus is full\n");else std::printf("published sequence %lld\n",(long long)r["sequence"].number());});
  }
  if(command=="scratch"){
    if(args.size()!=1||args[0]!="reset")return usage("scratch needs reset");
    return finish(call(link,"scratch.reset","",options.timeout(),options.requestId),[](const Json &r){std::printf("scratch reset (generation %lld)\n",(long long)r["generation"].number());});
  }
  if(command=="data"){
    if(args.size()!=2||args[0]!="reset")return usage("data needs reset <name>");
    const std::string name=args[1];
    return finish(call(link,"data.reset",",\"name\":"+quote(name),options.timeout(),options.requestId),[&](const Json &r){
      const long long removed=(long long)r["removed"].number();
      std::printf("removed %lld saved file%s for %s",removed,removed==1?"":"s",name.c_str());
      if(r["instance"].isNull())std::printf("\n");else std::printf("; reloaded (generation %lld)\n",(long long)r["instance"]["generation"].number());
    });
  }
  if(command=="autostart")return autostart(args);
  if(command=="eval")return evalCommand(args);
  if(command=="outcome"){
    if(args.size()!=1)return usage("outcome needs one request ID");
    return finish(call(link,"outcome",",\"requestId\":"+quote(args[0]),options.timeout()),[](const Json &r){std::printf("%s\n",r["response"].text().c_str());});
  }
  if(command=="repl")return repl(args);
  return usage("unknown command "+command);
}
}

int main(int argc,char **argv){
  if(!control::startJson()){std::fprintf(stderr,"error: cannot create the JSON parser\n");return Failure;}
  std::vector<std::string> args(argv+1,argv+argc);
  size_t i=0;
  for(;i<args.size()&&args[i].rfind("-",0)==0;i++){
    const auto &arg=args[i];
    if(arg=="-h"||arg=="--help"){std::fputs(helpText,stdout);return Success;}
    if(arg=="--json"){options.json=true;continue;}
    if(i+1==args.size())return usage(arg+" requires a value");
    if(arg=="--socket")options.socket=args[++i];
    else if(arg=="--timeout"){
      char *end=nullptr;const double seconds=std::strtod(args[++i].c_str(),&end);
      if(*end||!(seconds>0)||seconds>3600)return usage("--timeout must be 0 to 3600 seconds");
      options.timeoutSeconds=seconds;
    }else if(arg=="--request-id"){
      options.requestId=args[++i];
      if(options.requestId.empty()||options.requestId.size()>maxRequestIdBytes)return usage("--request-id must be 1 to 128 bytes");
    }else return usage("unknown option "+arg);
  }
  if(i==args.size()){std::fputs(helpText,stderr);return Usage;}
  const std::string command=args[i];
  const std::vector<std::string> rest(args.begin()+long(i)+1,args.end());
  // Options may also follow the command, before its own arguments.
  std::vector<std::string> commandArgs;
  for(size_t j=0;j<rest.size();j++){
    if(rest[j]=="--json"){options.json=true;continue;}
    if((rest[j]=="--socket"||rest[j]=="--timeout"||rest[j]=="--request-id")&&j+1<rest.size()){
      const std::vector<std::string> pair{rest[j],rest[j+1]};j++;
      if(pair[0]=="--socket")options.socket=pair[1];
      else if(pair[0]=="--request-id"){options.requestId=pair[1];if(options.requestId.empty()||options.requestId.size()>maxRequestIdBytes)return usage("--request-id must be 1 to 128 bytes");}
      else{char *end=nullptr;const double s=std::strtod(pair[1].c_str(),&end);if(*end||!(s>0)||s>3600)return usage("--timeout must be 0 to 3600 seconds");options.timeoutSeconds=s;}
      continue;
    }
    commandArgs.push_back(rest[j]);
  }
  if(!options.requestId.empty()&&command!="start"&&command!="stop"&&command!="reload"&&command!="publish"&&command!="scratch"&&command!="data"&&command!="eval"&&command!="autostart")
    return usage("--request-id applies only to changing commands");
  const int code=run(command,commandArgs);
  std::fflush(stdout);
  control::stopJson();
  return code;
}
