#include "tracking_state.h"
#include "frame_input.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using namespace std::chrono_literals;
static void readBytes(int fd,void *data,size_t size){
  auto *p=static_cast<char*>(data);
  while(size){const auto n=recv(fd,p,size,0);assert(n>0);p+=n;size-=n;}
}
static void consumeFrame(int fd){
  unsigned char head[2];readBytes(fd,head,2);size_t size=head[1]&127;
  if(size==126){unsigned char length[2];readBytes(fd,length,2);size=(length[0]<<8)|length[1];}
  assert(size<65536);std::string payload(size+(head[1]&128?4:0),'\0');readBytes(fd,payload.data(),payload.size());
}
static void sendFrame(int fd,const std::string &text){
  std::string frame(1,char(0x81));
  if(text.size()<126)frame+=char(text.size());
  else{frame+=char(126);frame+=char(text.size()>>8);frame+=char(text.size());}
  frame+=text;assert(send(fd,frame.data(),frame.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(frame.size()));
}
int main(){
  const auto now=tracking::Clock::now();
  assert(tracking::ageMs({})==-1);
  assert(tracking::fresh(now,now+250ms));assert(!tracking::fresh(now,now+251ms));assert(!tracking::fresh(now,now-1ms));
  assert(tracking::canPollGaze(true,true,0));
  assert(!tracking::canPollGaze(false,true,0));assert(!tracking::canPollGaze(true,false,0));assert(!tracking::canPollGaze(true,true,42));
  const float identity[3][4]={{1,0,0,1},{0,1,0,2},{0,0,1,3}};
  float origin[3]={1,2,3},target[3]={1,2,1};
  auto ray=tracking::sceneRay(origin,target,identity);
  assert(ray.valid&&ray.origin[0]==0&&ray.origin[1]==0&&ray.origin[2]==0);
  assert(ray.target[2]==2&&ray.direction[2]==1);
  const float rotated[3][4]={{0,0,1,1},{0,1,0,2},{-1,0,0,3}};
  ray=tracking::sceneRay(origin,target,rotated);assert(ray.valid&&ray.target[0]==2&&ray.direction[0]==1);
  assert(!tracking::sceneRay(origin,origin,identity).valid);
  target[0]=std::numeric_limits<float>::quiet_NaN();assert(!tracking::sceneRay(origin,target,identity).valid);

  const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(listener,1)==0);
  socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
  std::mutex mutex;std::condition_variable condition;int phase=0;
  std::thread server([&]{
    const int fd=accept(listener,nullptr,nullptr);assert(fd>=0);
    std::string request;char c;
    while(request.find("\r\n\r\n")==std::string::npos){readBytes(fd,&c,1);request+=c;}
    const char *reply="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
    assert(send(fd,reply,strlen(reply),MSG_NOSIGNAL)>0);
    for(int i=0;i<3;i++)consumeFrame(fd);
    sendFrame(fd,R"({"device":"/user/hand/left","components":{"/input/capsense/ring":0.5,"/input/handgrip1/touch":false,"/input/capsense/pinky":null,"/input/capsense/thumbrest":"unknown","/input/capsense/bumper":1e400,"/input/skeleton/left":{"nested":123,"data":[0,1]}}})");
    {std::unique_lock lock(mutex);condition.wait(lock,[&]{return phase>=1;});}
    sendFrame(fd,R"({"device":"/user/hand/left","components":{"/input/capsense/ring":0.75}})");
    {std::unique_lock lock(mutex);condition.wait(lock,[&]{return phase>=2;});}
    sendFrame(fd,R"({"device":"/user/hand/left","components":{"/input/capsense/ring":null,"/input/capsense/grip":0}})");
    {std::unique_lock lock(mutex);condition.wait(lock,[&]{return phase>=3;});}
    shutdown(fd,SHUT_RDWR);close(fd);
  });
  frame_input::Monitor monitor(ntohs(address.sin_port));assert(monitor.start());
  auto wait=[&](auto predicate){
    const auto end=tracking::Clock::now()+2s;
    do{monitor.poll();if(predicate())return;std::this_thread::sleep_for(1ms);}while(tracking::Clock::now()<end);
    assert(false&&"stream test timed out");
  };
  wait([&]{return monitor.hand(0).raw.size()==2;});
  assert(monitor.hand(0).valid&&tracking::fresh(monitor.hand(0).updatedAt));
  assert(!monitor.hand(1).valid&&monitor.hand(1).raw.empty());
  assert(monitor.hand(0).raw.at("/input/capsense/ring").value==.5f);
  const auto touchTime=monitor.hand(0).raw.at("/input/handgrip1/touch").updatedAt;
  {std::lock_guard lock(mutex);phase=1;}condition.notify_one();
  wait([&]{return monitor.hand(0).raw.at("/input/capsense/ring").value==.75f;});
  assert(monitor.hand(0).raw.at("/input/handgrip1/touch").updatedAt==touchTime);
  assert(monitor.hand(0).raw.at("/input/handgrip1/touch").isBoolean);
  assert(!monitor.hand(0).raw.at("/input/handgrip1/touch").boolean);
  auto events=monitor.takeEvents();assert(events.size()==3);assert(monitor.takeEvents().empty());
  {std::lock_guard lock(mutex);phase=2;}condition.notify_one();
  wait([&]{return !monitor.hand(0).raw.count("/input/capsense/ring")&&monitor.hand(0).raw.count("/input/capsense/grip");});
  assert(monitor.hand(0).raw.at("/input/capsense/grip").value==0);
  {std::lock_guard lock(mutex);phase=3;}condition.notify_one();
  wait([&]{return !monitor.started();});
  assert(!monitor.hand(0).valid&&monitor.hand(0).raw.empty()&&monitor.hand(0).updatedAt==tracking::Clock::time_point{});
  server.join();close(listener);
  puts("PASS tracking: scene transforms, invalid rays, freshness, gaze policy, delta fields, event queue, disconnect invalidation");
}
