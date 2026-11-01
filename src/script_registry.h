#pragma once

// Included after ScriptHost and loadScript in main.cpp. This isolates the legacy
// binding globals while the host is converted to a multi-instance frame loop.
struct ScriptScope {
  ScriptHost &host;
  Scene *previousCurrent=currentScene,*previousLive=liveScene;
  int previousInstanceId=activeInstanceId;
  frame_input::Monitor *previousInput=activeSceneInput;
  explicit ScriptScope(ScriptHost &value):host(value){
    if(inputCallbackCtx)throw std::logic_error("script scope entered with another callback active");
    swapActiveInteraction(host.interaction);
    activeInstanceId=host.instanceId;
    activeSceneInput=host.inputQueue.get();
    currentScene=liveScene=&host.scene;
    if(host.ctx&&!JS_IsUndefined(host.callback)){
      inputCallbackCtx=host.ctx;inputCallback=host.callback;inputCallbackContainer=host.callbackContainer;
      host.callback=JS_UNDEFINED;host.callbackContainer=0;
    }
  }
  ~ScriptScope(){
    if(inputCallbackCtx){
      // closeScript clears the globals before freeing a stopped host's context.
      assert(inputCallbackCtx==host.ctx);
      host.callback=inputCallback;host.callbackContainer=inputCallbackContainer;
      inputCallbackCtx=nullptr;inputCallback=JS_UNDEFINED;inputCallbackContainer=0;
    }
    swapActiveInteraction(host.interaction);
    activeInstanceId=previousInstanceId;
    activeSceneInput=previousInput;
    currentScene=previousCurrent;liveScene=previousLive;
  }
  ScriptScope(const ScriptScope&)=delete;
  ScriptScope& operator=(const ScriptScope&)=delete;
};

enum class ScriptState { Running, Stopped, Failed };
struct ScriptInstance {
  int id=0;
  std::string path;
  ScriptHost host;
  ScriptState state=ScriptState::Stopped;
  uint64_t generation=0;
  bool manuallyStopped=false;
  std::string lastError;
};

class ScriptRegistry {
  std::map<int,std::unique_ptr<ScriptInstance>> instances_;
  int nextId_=1;
public:
  ScriptRegistry(){auto scratch=std::make_unique<ScriptInstance>();scratch->id=0;instances_.emplace(0,std::move(scratch));}
  ~ScriptRegistry(){for(auto &[id,instance]:instances_)if(instance->host.ctx){ScriptScope scope(instance->host);closeScript(instance->host);resetActiveInteraction();}}
  ScriptRegistry(const ScriptRegistry&)=delete;
  ScriptRegistry& operator=(const ScriptRegistry&)=delete;
  const ScriptInstance *find(int id)const{auto it=instances_.find(id);return it==instances_.end()?nullptr:it->second.get();}
  ScriptInstance *find(int id){auto it=instances_.find(id);return it==instances_.end()?nullptr:it->second.get();}
  std::vector<int> runningIds()const{
    std::vector<int> ids;
    for(const auto &[id,instance]:instances_)if(instance->state==ScriptState::Running)ids.push_back(id);
    return ids;
  }
  bool launch(const std::string &path,int &id,std::string &error){
    std::error_code ec;const auto resolved=std::filesystem::canonical(path,ec);
    if(ec||!std::filesystem::is_regular_file(resolved,ec)||ec){error="script file is not readable";return false;}
    const std::string canonical=resolved.string();
    for(const auto &[otherId,entry]:instances_)if(otherId&&entry->path==canonical&&entry->state==ScriptState::Running){error="script is already running";return false;}
    if(nextId_==INT_MAX){error="script instance ID limit reached";return false;}
    auto instance=std::make_unique<ScriptInstance>();instance->id=nextId_++;instance->host.instanceId=instance->id;instance->path=canonical;id=instance->id;
    {ScriptScope scope(instance->host);if(loadScript(instance->host,canonical)){instance->state=ScriptState::Running;instance->generation=1;}else{instance->state=ScriptState::Failed;instance->lastError="script load failed";}}
    const bool loaded=instance->state==ScriptState::Running;error=instance->lastError;
    instances_.emplace(id,std::move(instance));return loaded;
  }
  bool reload(int id){
    auto *instance=find(id);if(!instance||!id||instance->state!=ScriptState::Running)return false;
    ScriptScope scope(instance->host);
    if(!loadScript(instance->host,instance->path)){instance->lastError="script reload failed";return false;}
    resetActiveInteraction();
    ++instance->generation;instance->lastError.clear();return true;
  }
  // The legacy VR loop holds this host's ScriptScope across the frame.
  bool reloadActive(int id,const std::function<void()> &beforeCommit){
    auto *instance=find(id);
    if(!instance||!id||instance->state!=ScriptState::Running||currentScene!=&instance->host.scene||liveScene!=&instance->host.scene)return false;
    if(!loadScript(instance->host,instance->path,beforeCommit)){instance->lastError="script reload failed";return false;}
    ++instance->generation;instance->lastError.clear();return true;
  }
  void reloadChanged(){
    for(auto &[id,entry]:instances_)if(id&&entry->state==ScriptState::Running){
      std::error_code ec;const auto modified=std::filesystem::last_write_time(entry->path,ec);
      if(!ec&&modified!=entry->host.mtime&&!reload(id))entry->host.mtime=modified;
    }
  }
  bool stop(int id,bool manual=true){
    auto *instance=find(id);if(!instance||!id||instance->state!=ScriptState::Running)return false;
    {ScriptScope scope(instance->host);closeScript(instance->host);resetActiveInteraction();}
    instance->state=ScriptState::Stopped;instance->manuallyStopped=manual;return true;
  }
  bool update(int id,double dt){
    auto *instance=find(id);if(!instance||instance->state!=ScriptState::Running)return false;
    ScriptScope scope(instance->host);
    if(!JS_IsFunction(instance->host.ctx,instance->host.update))return true;
    JSValue arg=JS_NewFloat64(instance->host.ctx,dt);
    JSValue result=JS_Call(instance->host.ctx,instance->host.update,JS_UNDEFINED,1,&arg);
    JS_FreeValue(instance->host.ctx,arg);
    if(!JS_IsException(result)){JS_FreeValue(instance->host.ctx,result);return true;}
    JS_FreeValue(instance->host.ctx,result);
    JSValue exception=JS_GetException(instance->host.ctx);
    const char *message=JS_ToCString(instance->host.ctx,exception);
    instance->lastError=message?message:"script update failed";
    if(message)JS_FreeCString(instance->host.ctx,message);
    JS_FreeValue(instance->host.ctx,exception);
    closeScript(instance->host);resetActiveInteraction();instance->state=ScriptState::Failed;return false;
  }
  bool dispatchInput(int id,const frame_input::Event &event){
    auto *instance=find(id);if(!instance||instance->state!=ScriptState::Running)return false;
    ScriptScope scope(instance->host);
    if(!inputCallbackCtx)return true;
    JSValue value=inputEventValue(instance->host.ctx,event);
    JSValue callback=JS_DupValue(instance->host.ctx,inputCallback);
    JSValue result=JS_Call(instance->host.ctx,callback,JS_UNDEFINED,1,&value);
    JS_FreeValue(instance->host.ctx,callback);JS_FreeValue(instance->host.ctx,value);
    if(!JS_IsException(result)){JS_FreeValue(instance->host.ctx,result);return true;}
    JS_FreeValue(instance->host.ctx,result);
    JSValue exception=JS_GetException(instance->host.ctx);
    const char *message=JS_ToCString(instance->host.ctx,exception);
    instance->lastError=message?message:"script input callback failed";
    if(message)JS_FreeCString(instance->host.ctx,message);
    JS_FreeValue(instance->host.ctx,exception);
    closeScript(instance->host);resetActiveInteraction();instance->state=ScriptState::Failed;return false;
  }
};
