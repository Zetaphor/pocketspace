#pragma once
// Client side of the control socket, shared by pocketspace-cli and the dashboard:
// JSON text helpers, parsed JSON read through one QuickJS context, and a connection.
#include "control_server.h"
#include <quickjs.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <poll.h>

namespace control {
using Clock=std::chrono::steady_clock;

inline std::string quote(const std::string &text){
  std::string out="\"";
  for(unsigned char c:text){
    if(c=='"'||c=='\\')out+={'\\',char(c)};
    else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";
    else if(c<0x20){char buffer[8];std::snprintf(buffer,sizeof(buffer),"\\u%04x",c);out+=buffer;}
    else out+=char(c);
  }
  return out+"\"";
}

// The QuickJS context every Json value belongs to. Create it once with startJson.
inline JSRuntime *jsonRuntime=nullptr;inline JSContext *jsonContext=nullptr;
inline bool startJson(){
  if(jsonContext)return true;
  jsonRuntime=JS_NewRuntime();jsonContext=jsonRuntime?JS_NewContext(jsonRuntime):nullptr;
  return jsonContext!=nullptr;
}
inline void stopJson(){
  if(jsonContext)JS_FreeContext(jsonContext);
  if(jsonRuntime)JS_FreeRuntime(jsonRuntime);
  jsonContext=nullptr;jsonRuntime=nullptr;
}

class Json {
  JSValue value_=JS_UNDEFINED;
public:
  Json()=default;
  explicit Json(JSValue value):value_(value){}
  static std::optional<Json> parse(const std::string &text){
    JSValue value=JS_ParseJSON(jsonContext,text.c_str(),text.size(),"json");
    if(JS_IsException(value)){JS_FreeValue(jsonContext,JS_GetException(jsonContext));return std::nullopt;}
    return Json(value);
  }
  Json(const Json &other):value_(JS_DupValue(jsonContext,other.value_)){}
  Json &operator=(const Json &other){if(this!=&other){JS_FreeValue(jsonContext,value_);value_=JS_DupValue(jsonContext,other.value_);}return *this;}
  ~Json(){if(jsonContext)JS_FreeValue(jsonContext,value_);}
  Json operator[](const char *key)const{return Json(JS_IsObject(value_)?JS_GetPropertyStr(jsonContext,value_,key):JS_UNDEFINED);}
  Json operator[](uint32_t index)const{return Json(JS_IsArray(value_)?JS_GetPropertyUint32(jsonContext,value_,index):JS_UNDEFINED);}
  bool isNull()const{return JS_IsNull(value_)||JS_IsUndefined(value_);}
  bool isString()const{return JS_IsString(value_);}
  bool isArray()const{return JS_IsArray(value_);}
  bool truthy()const{return JS_ToBool(jsonContext,value_)>0;}
  uint32_t length()const{int64_t n=0;if(isArray())JS_GetLength(jsonContext,value_,&n);return uint32_t(n);}
  double number(double fallback=0)const{double d=fallback;if(JS_IsNumber(value_))JS_ToFloat64(jsonContext,&d,value_);return d;}
  std::string str()const{
    if(isNull())return {};
    const char *s=JS_ToCString(jsonContext,value_);std::string out=s?s:"";if(s)JS_FreeCString(jsonContext,s);return out;
  }
  // Compact JSON text on one line.
  std::string text()const{
    JSValue json=JS_JSONStringify(jsonContext,value_,JS_UNDEFINED,JS_UNDEFINED);
    std::string out=JS_IsString(json)?Json(JS_DupValue(jsonContext,json)).str():"null";JS_FreeValue(jsonContext,json);return out;
  }
};

// One connection to the engine.
class Link {
  int fd_=-1;std::string input_;
public:
  Link()=default;
  Link(const Link&)=delete;Link &operator=(const Link&)=delete;
  ~Link(){close();}
  void close(){if(fd_>=0)::close(fd_);fd_=-1;input_.clear();}
  bool open()const{return fd_>=0;}
  int fd()const{return fd_;}
  bool connect(const std::filesystem::path &path){
    close();
    fd_=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd_<0)return false;
    sockaddr_un address{};address.sun_family=AF_UNIX;
    if(path.string().size()>=sizeof(address.sun_path)){close();return false;}
    std::strncpy(address.sun_path,path.c_str(),sizeof(address.sun_path)-1);
    if(::connect(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0){close();return false;}
    return true;
  }
  bool send(const std::string &line){
    const std::string text=line+"\n";
    for(size_t sent=0;sent<text.size();){
      const auto n=::send(fd_,text.data()+sent,text.size()-sent,MSG_NOSIGNAL);
      if(n<=0){if(n<0&&errno==EINTR)continue;return false;}
      sent+=size_t(n);
    }
    return true;
  }
  // One full line, or nothing on timeout (lost=false) or a closed connection (lost=true).
  std::optional<std::string> line(Clock::time_point deadline,bool &lost){
    lost=false;
    for(;;){
      if(const auto end=input_.find('\n');end!=std::string::npos){auto out=input_.substr(0,end);input_.erase(0,end+1);return out;}
      if(fd_<0){lost=true;return std::nullopt;}
      // A deadline already passed still reads what has arrived.
      const auto left=std::max<long long>(0,std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count());
      pollfd p{fd_,POLLIN,0};
      const int ready=::poll(&p,1,int(std::min<long long>(left,1000)));
      if(ready<0&&errno!=EINTR){close();lost=true;return std::nullopt;}
      if(ready<=0){if(Clock::now()>=deadline)return std::nullopt;continue;}
      char buffer[65536];const auto n=::recv(fd_,buffer,sizeof(buffer),0);
      if(n>0){input_.append(buffer,size_t(n));if(input_.size()>maxLineBytes*2){close();lost=true;return std::nullopt;}}
      else if(n==0||errno!=EINTR){close();lost=true;return std::nullopt;}
    }
  }
};

// A request ID unique to this process: prefix-pid-milliseconds-counter.
inline std::string newRequestId(const char *prefix){
  static unsigned counter=0;
  const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  return std::string(prefix)+"-"+std::to_string(getpid())+"-"+std::to_string(ms)+"-"+std::to_string(++counter);
}
}
