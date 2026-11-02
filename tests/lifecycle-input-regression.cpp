#include "frame_input.h"
#include "scope_exit.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using namespace std::chrono_literals;
static void sendAll(int fd,const std::string &text){size_t p=0;while(p<text.size()){auto n=send(fd,text.data()+p,text.size()-p,MSG_NOSIGNAL);assert(n>0);p+=n;}}
static std::string frame(const std::string &text){std::string out(1,char(0x81));if(text.size()<126)out+=char(text.size());else{out+=char(126);out+=char(text.size()>>8);out+=char(text.size());}return out+text;}
static void request(int fd){std::string data;char c;while(data.find("\r\n\r\n")==std::string::npos){assert(recv(fd,&c,1,0)==1);data+=c;}}
// A live socket alone does not prove that input_server still subscribes to the
// controller currently assigned to a hand. Simulate lost subscriptions without
// closing the old socket; recovery must be initiated by the client.
static void testSubscriptions(){
 const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(listener,4)==0);
 socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
 std::atomic<int> phase=0;
 auto until=[&](auto predicate){const auto end=tracking::Clock::now()+2s;while(!predicate()){assert(tracking::Clock::now()<end);std::this_thread::sleep_for(1ms);}};
 std::thread server([&]{
  for(int session=0;session<5;session++){
   const int fd=accept(listener,nullptr,nullptr);assert(fd>=0);request(fd);
   sendAll(fd,"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
   if(session==0||session==2||session==4)sendAll(fd,frame(session==2?R"({"device":"right","components":{"/input/a/click":false}})":R"({"device":"right","components":{"/input/a/click":true}})"));
   until([&]{return phase>session;});close(fd);
  }
 });
 frame_input::Monitor monitor(ntohs(address.sin_port));monitor.setTrackedDevice(1,1);assert(monitor.start());
 auto received=[&]{until([&]{monitor.poll();return monitor.hand(1).valid;});};received();
 monitor.takeEvents();monitor.takeCallbackEvents();
 // Quiet-but-valid delta streams must not trigger a timeout or duplicate input.
 monitor.poll(tracking::Clock::now()+60s);assert(monitor.started());assert(monitor.takeEvents().empty());
 frame_input::Event physics;physics.type="physics.jointBreak";monitor.addEvent(physics);
 monitor.setTrackedDevice(1,2);monitor.poll();assert(!monitor.started()&&!monitor.hand(1).valid);
 auto queued=monitor.takeCallbackEvents();assert(queued.size()==1&&queued[0].type=="physics.jointBreak");
 phase=1;assert(monitor.reconnect());
 // Replacement controller's subscription is accepted, but never sends state.
 monitor.poll(tracking::Clock::now()+6s);assert(!monitor.started());
 phase=2;assert(monitor.reconnect());received();
 assert(!monitor.hand(1).raw.at("/input/a/click").boolean);
 auto events=monitor.takeCallbackEvents();assert(events.size()==1&&!events[0].component.boolean);
 monitor.setTrackedDevice(1,2);monitor.poll();assert(monitor.started()); // stable identity
 monitor.setTrackedDevice(1,-1);monitor.poll();assert(!monitor.started());
 phase=3;assert(monitor.reconnect());monitor.poll(tracking::Clock::now()+60s);assert(monitor.started()); // no controller
 monitor.setTrackedDevice(1,2);monitor.poll();assert(!monitor.started());
 phase=4;assert(monitor.reconnect());received();
 assert(monitor.hand(1).raw.at("/input/a/click").boolean);
 events=monitor.takeCallbackEvents();assert(events.size()==1&&events[0].component.boolean);
 monitor.resetScene();monitor.poll(tracking::Clock::now()+60s);assert(monitor.started()&&monitor.hand(1).valid);
 phase=5;monitor.stop();server.join();close(listener);
 puts("PASS raw subscriptions: live-socket device replacement, missing initial state retry, reconnect callbacks, idle delta stream, absent/reconnected controller, stable identity, physics preservation and scene reload");
}

int main(){
 testSubscriptions();
 std::string order;
 {ScopeExit first{[&]{order+='a';}};{ScopeExit second{[&]{order+='b';}};}}assert(order=="ba");
 auto early=[&]{ScopeExit cleanup{[&]{order+='c';}};return;};early();assert(order=="bac");
 const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(listener,4)==0);
 socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
 std::atomic<int> phase=0;
 auto until=[&](auto condition){const auto end=tracking::Clock::now()+2s;while(!condition()){assert(tracking::Clock::now()<end);std::this_thread::sleep_for(1ms);}};
 const std::string upgrade="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
 std::thread server([&]{
  int fd=accept(listener,nullptr,nullptr);assert(fd>=0);request(fd);
  sendAll(fd,upgrade+frame(R"({"device":"left","components":{"/input/grip/click":true}})")+frame(R"({"device":"right","components":{"/input/trigger/click":true}})"));
  until([&]{return phase>=1;});sendAll(fd,frame(R"({"device":"right","components":{"/input/trigger/click":false}})"));
  until([&]{return phase>=2;});shutdown(fd,SHUT_RDWR);close(fd);
  fd=accept(listener,nullptr,nullptr);assert(fd>=0);request(fd);
  sendAll(fd,upgrade+frame(R"({"device":"right","components":{"/input/grip/value":0.4}})"));
  until([&]{return phase>=3;});sendAll(fd,std::string("\x88\x00",2));
  until([&]{return phase>=4;});close(fd);
  fd=accept(listener,nullptr,nullptr);assert(fd>=0);request(fd);sendAll(fd,"HTTP/1.1 403 Forbidden\r\n\r\n");close(fd);
  fd=accept(listener,nullptr,nullptr);assert(fd>=0);request(fd);std::this_thread::sleep_for(350ms);close(fd);
 });
 frame_input::Monitor monitor(ntohs(address.sin_port));assert(monitor.start());
 auto pollUntil=[&](auto predicate){until([&]{monitor.poll();return predicate();});};
 pollUntil([&]{return monitor.hand(0).valid&&monitor.hand(1).valid;});
 monitor.addOverlayEvent("overlay.grabStart",99,0);
 monitor.resetScene();assert(monitor.takeEvents().empty()&&monitor.takeCallbackEvents().empty());assert(monitor.hand(0).raw.size()==1);
 monitor.invalidateHand(0);assert(!monitor.hand(0).valid&&monitor.hand(0).raw.empty());assert(monitor.hand(1).valid);
 phase=1;pollUntil([&]{auto it=monitor.hand(1).raw.find("/input/trigger/click");return it!=monitor.hand(1).raw.end()&&!it->second.boolean;});
 auto events=monitor.takeEvents();assert(events.size()==1&&events[0].objectId==0);assert(monitor.takeCallbackEvents().size()==1);
 phase=2;pollUntil([&]{return !monitor.started();});assert(!monitor.hand(1).valid);assert(monitor.takeEvents().empty()&&monitor.takeCallbackEvents().empty());
 assert(monitor.reconnect());pollUntil([&]{return monitor.hand(1).valid;});assert(monitor.hand(0).raw.empty());assert(monitor.hand(1).raw.size()==1);
 phase=3;pollUntil([&]{return !monitor.started();});assert(monitor.hand(1).raw.empty());phase=4;
 assert(!monitor.reconnect());assert(!monitor.started());
 const auto before=tracking::Clock::now();assert(!monitor.reconnect());assert(tracking::Clock::now()-before<600ms);
 server.join();close(listener);
 const auto fdCount=[](){size_t count=0;for(const auto &item:std::filesystem::directory_iterator("/proc/self/fd")){(void)item;count++;}return count;};
 const auto baseline=fdCount();for(int i=0;i<30;i++)assert(!monitor.reconnect());assert(fdCount()==baseline);
 monitor.stop();assert(!monitor.started());
 puts("PASS lifecycle input: coalesced upgrade, scene queues, per-hand invalidation, reconnect, close frame, rejected/stalled upgrade, fd cleanup, scope cleanup");
}
