#pragma once
// Local control socket: line-delimited JSON over a private Unix-domain socket.
// Polled from the host loop, so every request runs on the host thread between frames.
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace control {
constexpr size_t maxLineBytes=1024*1024,maxClients=16,maxPendingOutput=4*1024*1024,maxRequestsPerPoll=64;
inline std::filesystem::path defaultSocketPath(){
  const char *runtime=std::getenv("XDG_RUNTIME_DIR");
  const std::filesystem::path base=runtime&&*runtime?std::filesystem::path(runtime):std::filesystem::path("/tmp")/("pocketspace-"+std::to_string(getuid()));
  return base/"pocketspace/control.sock";
}
// eof: the peer finished sending; its pending requests are still answered.
struct Client {uint64_t id=0;int fd=-1;std::string input,output;bool watching=false,closing=false,eof=false;};

class Server {
  int listener_=-1;std::filesystem::path path_;
  std::vector<Client> clients_;uint64_t nextClient_=1;
  static bool nonblocking(int fd){const int flags=fcntl(fd,F_GETFL);return flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0;}
  void flush(Client &client){
    while(!client.output.empty()){
      const auto n=::send(client.fd,client.output.data(),client.output.size(),MSG_NOSIGNAL|MSG_DONTWAIT);
      if(n>0){client.output.erase(0,size_t(n));continue;}
      if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
      client.closing=true;client.output.clear();break;
    }
    // A client that stops reading cannot hold unbounded engine memory.
    if(client.output.size()>maxPendingOutput){client.closing=true;client.output.clear();}
  }
public:
  Server()=default;
  Server(const Server&)=delete;Server &operator=(const Server&)=delete;
  ~Server(){stop();}
  const std::filesystem::path &path()const{return path_;}
  size_t clientCount()const{return clients_.size();}
  // Creates the private directory and socket. Fails if another engine answers on it;
  // a stale socket from a crashed engine is replaced.
  bool start(const std::filesystem::path &path,std::string &error){
    stop();
    const auto directory=path.parent_path();
    if(path.string().size()>=sizeof(sockaddr_un::sun_path)){error="control socket path is too long";return false;}
    std::error_code ec;std::filesystem::create_directories(directory,ec);
    struct stat info{};
    if(::lstat(directory.c_str(),&info)!=0||!S_ISDIR(info.st_mode)||info.st_uid!=getuid()){error="control socket directory is not a private directory owned by this user";return false;}
    if((info.st_mode&077)&&::chmod(directory.c_str(),0700)!=0){error="control socket directory cannot be made private";return false;}
    sockaddr_un address{};address.sun_family=AF_UNIX;std::strncpy(address.sun_path,path.c_str(),sizeof(address.sun_path)-1);
    if(::lstat(path.c_str(),&info)==0){
      if(!S_ISSOCK(info.st_mode)){error="control socket path exists and is not a socket";return false;}
      const int probe=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
      const bool live=probe>=0&&::connect(probe,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0;
      if(probe>=0)::close(probe);
      if(live){error="another engine is already running on "+path.string();return false;}
      ::unlink(path.c_str());
    }
    listener_=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(listener_<0){error="control socket cannot be created";return false;}
    const mode_t previous=::umask(077);
    const bool bound=::bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0;
    ::umask(previous);
    if(!bound||::chmod(path.c_str(),0600)!=0||::listen(listener_,8)!=0||!nonblocking(listener_)){
      error=std::string("control socket cannot listen: ")+std::strerror(errno);::close(listener_);listener_=-1;::unlink(path.c_str());return false;
    }
    path_=path;error.clear();return true;
  }
  void stop(){
    for(auto &client:clients_)::close(client.fd);
    clients_.clear();
    if(listener_>=0){::close(listener_);listener_=-1;::unlink(path_.c_str());}
  }
  // Accepts clients, reads complete lines and answers each with handle(client, line).
  // At most maxRequestsPerPoll requests run per call; the rest wait for the next one.
  template<class Handle> void poll(Handle &&handle){
    if(listener_<0)return;
    for(;;){
      const int fd=::accept4(listener_,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);
      if(fd<0)break;
      if(clients_.size()>=maxClients){static const char busy[]="{\"v\":1,\"id\":null,\"ok\":false,\"error\":{\"code\":\"busy\",\"message\":\"too many control clients\"}}\n";(void)!::send(fd,busy,sizeof(busy)-1,MSG_NOSIGNAL|MSG_DONTWAIT);::close(fd);continue;}
      clients_.push_back({nextClient_++,fd});
    }
    size_t budget=maxRequestsPerPoll;
    for(auto &client:clients_){
      char buffer[65536];
      for(;!client.eof&&!client.closing;){
        const auto n=::recv(client.fd,buffer,sizeof(buffer),MSG_DONTWAIT);
        if(n>0){client.input.append(buffer,size_t(n));if(client.input.size()>maxLineBytes+65536)break;continue;}
        if(n==0)client.eof=true;
        else if(errno!=EAGAIN&&errno!=EWOULDBLOCK)client.closing=true;
        break;
      }
      for(size_t end;budget&&!client.closing&&(end=client.input.find('\n'))!=std::string::npos;--budget){
        std::string line=client.input.substr(0,end);client.input.erase(0,end+1);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        client.output+=handle(client,line);client.output+='\n';
      }
      if(client.input.size()>maxLineBytes&&client.input.find('\n')==std::string::npos){
        client.output+="{\"v\":1,\"id\":null,\"ok\":false,\"error\":{\"code\":\"too_large\",\"message\":\"request exceeds 1 MiB\"}}\n";
        client.input.clear();client.closing=true;
      }
      flush(client);
    }
    std::erase_if(clients_,[](const Client &client){if((client.closing||(client.eof&&client.input.find('\n')==std::string::npos))&&client.output.empty()){::close(client.fd);return true;}return false;});
  }
  // Sends one event line to every client that asked to watch.
  void broadcast(const std::string &line){
    for(auto &client:clients_)if(client.watching&&!client.closing){client.output+=line;client.output+='\n';flush(client);}
  }
};
}
