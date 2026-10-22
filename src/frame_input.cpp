#include "frame_input.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <random>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace frame_input {
namespace {
std::string jsonString(const std::string &s,const std::string &key,size_t from=0){
  auto p=s.find("\""+key+"\"",from); if(p==std::string::npos)return {};
  p=s.find('"',s.find(':',p)+1); if(p==std::string::npos)return {};
  auto e=s.find('"',p+1); return e==std::string::npos?std::string{}:s.substr(p+1,e-p-1);
}
std::string randomKey(){std::random_device source;std::string mask(4,'\0');for(char &byte:mask)byte=static_cast<char>(source());return mask;}
}
Monitor::~Monitor(){if(fd_>=0)close(fd_);}
void Monitor::disconnect(){if(fd_>=0)close(fd_);fd_=-1;started_=false;buffer_.clear();fragment_.clear();for(auto &hand:hands_)hand={};}
bool Monitor::reconnect(){disconnect();return start();}
bool Monitor::sendFrame(unsigned char opcode,const std::string &payload){
 if(fd_<0)return false;
 std::string out;out.push_back(char(0x80|opcode)); size_t n=payload.size();
 if(n<126)out.push_back(char(0x80|n));else if(n<65536){out.push_back(char(0x80|126));out.push_back(char(n>>8));out.push_back(char(n));}else return false;
 std::string mask=randomKey();out+=mask;for(size_t i=0;i<n;i++)out.push_back(char(payload[i]^mask[i%4]));return send(fd_,out.data(),out.size(),MSG_NOSIGNAL)==(ssize_t)out.size();
}
bool Monitor::start(){
 disconnect();
 fd_=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);if(fd_<0)return false; sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port_);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);connect(fd_,(sockaddr*)&a,sizeof(a));
 fd_=fd_; int flags=fcntl(fd_,F_GETFL);fcntl(fd_,F_SETFL,flags&~O_NONBLOCK);
 const std::string origin="127.0.0.1:"+std::to_string(port_);
 std::string key="sf_overlay_engine";std::string req="GET / HTTP/1.1\r\nHost: "+origin+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\nOrigin: http://"+origin+"\r\nReferer: http://"+origin+"/dashboard/controllerbinding.html\r\n\r\n";if(send(fd_,req.data(),req.size(),MSG_NOSIGNAL)<0)return false;
 char buf[4096];std::string reply;while(reply.find("\r\n\r\n")==std::string::npos){ssize_t n=recv(fd_,buf,sizeof(buf),0);if(n<=0)return false;reply.append(buf,n);}sendFrame(1,"mailbox_open sf_overlay_engine");
 for(const char *side:{"left","right"}){std::string msg="mailbox_send input_server {\"type\":\"request_input_state_updates\",\"device_path\":\"/user/hand/"+std::string(side)+"\",\"returnAddress\":\"sf_overlay_engine\"}";sendFrame(1,msg);}
 fcntl(fd_,F_SETFL,flags);started_=true;return true;
}
void Monitor::processMessage(const std::string &message){
 const auto device=jsonString(message,"device");
 const int hand=device.find("left")!=std::string::npos?0:device.find("right")!=std::string::npos?1:-1;
 if(hand<0)return;
 auto start=message.find("\"components\"");if(start==std::string::npos)return;
 start=message.find('{',start);if(start==std::string::npos)return;
 const auto now=tracking::Clock::now();
 size_t p=start+1;
 while(p<message.size()){
   p=message.find_first_not_of(" \t\r\n,",p);
   if(p==std::string::npos||message[p]=='}'||message[p]!='"')break;
   const auto keyEnd=message.find('"',p+1);if(keyEnd==std::string::npos)break;
   const std::string name=message.substr(p+1,keyEnd-p-1);
   const auto colon=message.find_first_not_of(" \t\r\n",keyEnd+1);
   if(colon==std::string::npos||message[colon]!=':')break;
   const auto valueStart=message.find_first_not_of(" \t\r\n",colon+1);if(valueStart==std::string::npos)break;
   size_t end=valueStart;int depth=0;bool string=false,escaped=false;
   // Structured/unknown values are skipped whole, never mistaken for scalar zero
   // or for additional component names inside a skeleton/pose object.
   for(;end<message.size();end++){
     const char ch=message[end];
     if(string){if(escaped)escaped=false;else if(ch=='\\')escaped=true;else if(ch=='"')string=false;continue;}
     if(ch=='"'){string=true;continue;}
     if(ch=='{'||ch=='[')depth++;
     else if(ch=='}'||ch==']'){if(depth==0)break;depth--;}
     else if(ch==','&&depth==0)break;
   }
   std::string token=message.substr(valueStart,end-valueStart);
   const auto last=token.find_last_not_of(" \t\r\n");if(last!=std::string::npos)token.resize(last+1);
   Component component;
   if(token=="true"||token=="false"){component.isBoolean=true;component.boolean=token=="true";}
   else{
     char *valueEnd=nullptr;component.value=std::strtof(token.c_str(),&valueEnd);
     if(token.empty()||valueEnd==token.c_str()||*valueEnd!='\0'||!std::isfinite(component.value)){hands_[hand].raw.erase(name);p=end;continue;}
   }
   component.updatedAt=now;
   hands_[hand].valid=true;hands_[hand].updatedAt=now;hands_[hand].raw[name]=component;
   Event event;event.type=component.isBoolean?"controller.button":"controller.value";
   event.hand=hand?"right":"left";event.control=name;event.component=component;event.objectId=hovered_[hand];
   events_.push_back(event);callbackEvents_.push_back(std::move(event));
   if(events_.size()>4096)events_.erase(events_.begin(),events_.begin()+1024);
   if(callbackEvents_.size()>4096)callbackEvents_.erase(callbackEvents_.begin(),callbackEvents_.begin()+1024);
   p=end;
 }
}
void Monitor::poll(){if(!started_)return;char buf[65536];for(;;){ssize_t n=recv(fd_,buf,sizeof(buf),MSG_DONTWAIT);if(n==0){disconnect();break;}if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK)break;disconnect();break;}buffer_.append(buf,n);}while(buffer_.size()>=2){unsigned char b0=buffer_[0],b1=buffer_[1];size_t n=b1&127,h=2;if(n==126){if(buffer_.size()<4)break;n=(static_cast<unsigned char>(buffer_[2])<<8)|static_cast<unsigned char>(buffer_[3]);h=4;}if(buffer_.size()<h+n)break;auto payload=buffer_.substr(h,n);buffer_.erase(0,h+n);if((b0&15)==9){sendFrame(10,payload);continue;}if((b0&15)==1){fragment_+=payload;if(b0&128){processMessage(fragment_);fragment_.clear();}}}}
std::vector<Event> Monitor::takeEvents(){std::vector<Event> result;result.swap(events_);return result;}
std::vector<Event> Monitor::takeCallbackEvents(){auto result=std::move(callbackEvents_);callbackEvents_.clear();return result;}
void Monitor::setHoveredObject(int hand,int objectId){if(hand>=0&&hand<2)hovered_[hand]=objectId;}
void Monitor::addPanelEvent(const std::string &type,int panelId,int hand,float x,float y,const std::string &button,bool down){Event event;event.type=type;event.objectId=panelId;event.x=x;event.y=y;event.control=button;event.component.isBoolean=!button.empty();event.component.boolean=down;if(hand>=0)event.hand=hand?"right":"left";events_.push_back(event);callbackEvents_.push_back(std::move(event));if(events_.size()>4096)events_.erase(events_.begin(),events_.begin()+1024);if(callbackEvents_.size()>4096)callbackEvents_.erase(callbackEvents_.begin(),callbackEvents_.begin()+1024);}
void Monitor::addOverlayEvent(const std::string &type,int objectId,int hand,float dx,float dy,float dz,float dpitch,float dyaw,float droll){Event event;event.type=type;event.objectId=objectId;event.dx=dx;event.dy=dy;event.dz=dz;event.dpitch=dpitch;event.dyaw=dyaw;event.droll=droll;if(hand>=0)event.hand=hand?"right":"left";events_.push_back(event);callbackEvents_.push_back(std::move(event));if(events_.size()>4096)events_.erase(events_.begin(),events_.begin()+1024);if(callbackEvents_.size()>4096)callbackEvents_.erase(callbackEvents_.begin(),callbackEvents_.begin()+1024);}
}
