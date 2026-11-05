// Control host for tests/cli-regression.py: the real control service, registry and
// scratch scene served on a socket without VR, until SIGTERM or SIGINT.
// Usage: cli-regression <catalog> <socket> <settings>
#include "../src/render_host.h"
#include <cassert>
#include <csignal>

namespace {volatile sig_atomic_t stopping=0;void onSignal(int){stopping=1;}}

int main(int argc,char **argv){
  if(argc!=4){std::fprintf(stderr,"usage: cli-regression <catalog> <socket> <settings>\n");return 2;}
  std::signal(SIGTERM,onSignal);std::signal(SIGINT,onSignal);
  settings::File file(argv[3]);file.refresh();
  ScriptRegistry registry;assert(registry.enableScratch());
  registry.setAutoRestart(file.current().autoRestart);
  control::Service service(registry,argv[1],file,{{},[&](const settings::Settings &s){registry.setAutoRestart(s.autoRestart);}});
  scratchControl=[&](const std::string &request){return service.handleScratch(request);};
  control::Server server;std::string error;
  if(!server.start(argv[2],error)){std::fprintf(stderr,"[control] %s\n",error.c_str());return 14;}
  std::printf("[control] listening on %s\n",argv[2]);std::fflush(stdout);
  // A frame: requests and queued scratch calls between frames, then updates and bus delivery.
  while(!stopping){
    server.poll([&](control::Client &client,const std::string &line){return service.handle(client,line);});
    service.applyScratchQueue();
    for(const auto &line:service.takeEvents())server.broadcast(line);
    registry.tickRestarts();
    for(int id:registry.runningIds())registry.update(id,1/90.);
    registry.dispatchBus();
    usleep(5000);
  }
  scratchControl={};server.stop();
  std::printf("[control] stopped\n");
}
