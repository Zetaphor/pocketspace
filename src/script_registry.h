#pragma once

// Included after ScriptHost and loadScript in main.cpp. Each scope selects one
// instance's bindings and interaction state for engine and QuickJS calls.
struct ScriptScope {
  ScriptHost &host;
  Scene *previousCurrent=currentScene,*previousLive=liveScene;
  int previousInstanceId=activeInstanceId;
  const std::string *previousNamespace=scenePersistence::activeNamespace;
  frame_input::Monitor *previousInput=activeSceneInput;
  explicit ScriptScope(ScriptHost &value):host(value){
    if(inputCallbackCtx)throw std::logic_error("script scope entered with another callback active");
    swapActiveInteraction(host.interaction);
    activeInstanceId=host.instanceId;
    scenePersistence::activeNamespace=&host.saveNamespace;
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
    scenePersistence::activeNamespace=previousNamespace;
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
  // Automatic restart after callback errors; attempts reset after a healthy minute.
  int restartAttempts=0;
  std::optional<std::chrono::steady_clock::time_point> restartAt;
  std::chrono::steady_clock::time_point startedAt{};
  // The last reload's result in this session; unset before any reload.
  std::optional<bool> lastReloadOk;
};
namespace restart {
constexpr int maxAttempts=5;
constexpr auto firstDelay=std::chrono::seconds(1),healthyRun=std::chrono::seconds(60);
}

class ScriptRegistry {
  std::map<int,std::unique_ptr<ScriptInstance>> instances_;
  int nextId_=1;
  std::function<void(int)> beforeStop_;
  std::function<void(const ScriptInstance&)> onFailure_;
  std::function<std::chrono::steady_clock::time_point()> clock_=[]{return std::chrono::steady_clock::now();};
  bool autoRestart_=false,scratchEnabled_=false;
  bool startScratch(ScriptInstance &scratch){
    bool loaded=false;
    {ScriptScope scope(scratch.host);loaded=loadScriptSource(scratch.host,"scratch","");}
    scratch.state=loaded?ScriptState::Running:ScriptState::Failed;
    if(loaded){++scratch.generation;scratch.startedAt=clock_();}
    return loaded;
  }
  // Called after a callback error stops a script. Scratch is recreated empty instead.
  void failed(ScriptInstance &instance){
    instance.restartAt.reset();
    if(!instance.id&&scratchEnabled_){if(onFailure_)onFailure_(instance);startScratch(instance);return;}
    if(instance.id&&autoRestart_&&!instance.manuallyStopped&&instance.restartAttempts<restart::maxAttempts)
      instance.restartAt=clock_()+restart::firstDelay*(1<<instance.restartAttempts++);
    if(onFailure_)onFailure_(instance);
  }
  void publishLifecycle(const char *topic,const ScriptInstance &instance,const char *reason=nullptr){
    std::string payload="{\"instanceId\":"+std::to_string(instance.id)+",\"path\":"+bus::jsonText(instance.path);
    if(reason)payload+=std::string(",\"reason\":")+bus::jsonText(reason)+",\"error\":"+bus::jsonText(instance.lastError);
    eventBus.publish(std::nullopt,topic,payload+"}");
  }
  // Records a callback exception and stops only this instance. The caller owns its scope.
  void failScoped(ScriptInstance &instance,const char *fallback){
    JSValue exception=JS_GetException(instance.host.ctx);
    const char *message=JS_ToCString(instance.host.ctx,exception);
    instance.lastError=message?message:fallback;
    if(message)JS_FreeCString(instance.host.ctx,message);
    JS_FreeValue(instance.host.ctx,exception);
    if(beforeStop_)beforeStop_(instance.id);
    closeScript(instance.host);resetActiveInteraction();instance.state=ScriptState::Failed;
    publishLifecycle("engine.scriptStopped",instance,"failed");
    failed(instance);
  }
public:
  ScriptRegistry(){auto scratch=std::make_unique<ScriptInstance>();scratch->id=0;instances_.emplace(0,std::move(scratch));}
  ~ScriptRegistry(){for(auto &[id,instance]:instances_)if(instance->host.ctx){ScriptScope scope(instance->host);closeScript(instance->host);resetActiveInteraction();}eventBus.clear();}
  ScriptRegistry(const ScriptRegistry&)=delete;
  ScriptRegistry& operator=(const ScriptRegistry&)=delete;
  void setBeforeStop(std::function<void(int)> callback){beforeStop_=std::move(callback);}
  void setOnFailure(std::function<void(const ScriptInstance&)> callback){onFailure_=std::move(callback);}
  void setClock(std::function<std::chrono::steady_clock::time_point()> clock){clock_=std::move(clock);}
  // Off by default. Turning it off cancels pending restarts.
  void setAutoRestart(bool enabled){
    autoRestart_=enabled;
    if(!enabled)for(auto &[id,instance]:instances_)instance->restartAt.reset();
  }
  bool autoRestart()const{return autoRestart_;}
  // The engine-owned scratch scene: instance 0 with an empty, persistent context.
  bool enableScratch(){scratchEnabled_=true;auto &scratch=*instances_.at(0);return scratch.state==ScriptState::Running||startScratch(scratch);}
  // Clears and recreates only instance 0.
  bool resetScratch(){
    auto &scratch=*instances_.at(0);if(!scratchEnabled_)return false;
    if(scratch.host.ctx){ScriptScope scope(scratch.host);if(beforeStop_)beforeStop_(0);closeScript(scratch.host);resetActiveInteraction();}
    return startScratch(scratch);
  }
  struct Evaluation {
    enum class Status {Ok,Exception,Timeout,NotRunning,StaleGeneration,TooLarge} status=Status::Ok;
    std::string result,exception,output;uint64_t generation=0;
  };
  // Runs source in a live scene's existing context. A nonzero generation must match.
  // A timeout leaves the scene in an unknown state, so it stops that script (or resets
  // scratch) like a callback error; an exception is only reported.
  Evaluation evaluate(int id,uint64_t generation,const std::string &source,std::chrono::milliseconds timeout){
    Evaluation outcome;auto *instance=find(id);
    if(!instance||instance->state!=ScriptState::Running||!instance->host.ctx){outcome.status=Evaluation::Status::NotRunning;return outcome;}
    if(generation&&generation!=instance->generation){outcome.status=Evaluation::Status::StaleGeneration;outcome.generation=instance->generation;return outcome;}
    if(source.size()>evaluation::maxSourceBytes){outcome.status=Evaluation::Status::TooLarge;return outcome;}
    outcome.generation=instance->generation;
    ScriptScope scope(instance->host);JSContext *ctx=instance->host.ctx;
    evaluation::capture=&outcome.output;evaluation::interrupted=false;evaluation::deadline=std::chrono::steady_clock::now()+timeout;
    JSValue value=JS_Eval(ctx,source.c_str(),source.size(),"<eval>",JS_EVAL_TYPE_GLOBAL);
    evaluation::deadline.reset();evaluation::capture=nullptr;
    auto text=[&](JSValueConst v){const char *s=JS_ToCString(ctx,v);std::string r=s?s:"";if(s)JS_FreeCString(ctx,s);return r;};
    if(JS_IsException(value)){
      JSValue error=JS_GetException(ctx);
      const bool timedOut=evaluation::interrupted;evaluation::interrupted=false;
      outcome.exception=text(error);JS_FreeValue(ctx,error);
      if(timedOut){
        outcome.status=Evaluation::Status::Timeout;
        instance->lastError="evaluation timed out after "+std::to_string(timeout.count())+" ms";
        if(beforeStop_)beforeStop_(id);
        closeScript(instance->host);resetActiveInteraction();instance->state=ScriptState::Failed;
        publishLifecycle("engine.scriptStopped",*instance,"failed");
        failed(*instance);
      }else outcome.status=Evaluation::Status::Exception;
      return outcome;
    }
    // Results are reported as JSON when possible, otherwise as their string form.
    JSValue json=JS_JSONStringify(ctx,value,JS_UNDEFINED,JS_UNDEFINED);
    if(JS_IsException(json)){JSValue e=JS_GetException(ctx);JS_FreeValue(ctx,e);outcome.result=JS_IsUndefined(value)?"undefined":text(value);}
    else outcome.result=JS_IsUndefined(json)?(JS_IsUndefined(value)?"undefined":text(value)):text(json);
    JS_FreeValue(ctx,json);JS_FreeValue(ctx,value);
    if(outcome.result.size()>evaluation::maxOutputBytes)outcome.result.resize(evaluation::maxOutputBytes);
    return outcome;
  }
  // Retries due restarts and clears the attempt count of scripts that stayed up.
  // Returns the instances that started again.
  std::vector<int> tickRestarts(){
    const auto now=clock_();std::vector<int> restarted;
    for(auto &[id,instance]:instances_){
      if(!id)continue;
      if(instance->state==ScriptState::Running&&instance->restartAttempts&&now-instance->startedAt>=restart::healthyRun)instance->restartAttempts=0;
      if(instance->state!=ScriptState::Failed||!instance->restartAt||now<*instance->restartAt)continue;
      instance->restartAt.reset();
      bool duplicate=false;
      for(const auto &[otherId,other]:instances_)duplicate|=otherId!=id&&other->path==instance->path&&other->state==ScriptState::Running;
      if(duplicate)continue;
      bool loaded=false;
      {ScriptScope scope(instance->host);loaded=loadScript(instance->host,instance->path);}
      if(loaded){
        instance->state=ScriptState::Running;++instance->generation;instance->startedAt=now;instance->lastError.clear();
        publishLifecycle("engine.scriptStarted",*instance);restarted.push_back(id);
      }else{instance->lastError="script restart failed";failed(*instance);}
    }
    return restarted;
  }
  const ScriptInstance *find(int id)const{auto it=instances_.find(id);return it==instances_.end()?nullptr:it->second.get();}
  ScriptInstance *find(int id){auto it=instances_.find(id);return it==instances_.end()?nullptr:it->second.get();}
  std::vector<int> allIds()const{
    std::vector<int> ids;ids.reserve(instances_.size());
    for(const auto &[id,instance]:instances_)ids.push_back(id);
    return ids;
  }
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
    auto instance=std::make_unique<ScriptInstance>();instance->id=nextId_++;instance->host.instanceId=instance->id;instance->host.saveNamespace=scriptStorageKey(scenePersistence::catalogRoot,canonical);instance->path=canonical;id=instance->id;
    {ScriptScope scope(instance->host);if(loadScript(instance->host,canonical)){instance->state=ScriptState::Running;instance->generation=1;instance->startedAt=clock_();}else{instance->state=ScriptState::Failed;instance->lastError="script load failed";}}
    const bool loaded=instance->state==ScriptState::Running;error=instance->lastError;
    publishLifecycle(loaded?"engine.scriptStarted":"engine.scriptStopped",*instance,loaded?nullptr:"failed");
    instances_.emplace(id,std::move(instance));return loaded;
  }
  bool reload(int id,const std::function<void()> &beforeCommit={}){
    auto *instance=find(id);if(!instance||!id||instance->state!=ScriptState::Running)return false;
    ScriptScope scope(instance->host);
    if(!loadScript(instance->host,instance->path,beforeCommit)){
      instance->lastError="script reload failed";instance->lastReloadOk=false;
      eventBus.publish(std::nullopt,"engine.scriptReloadFailed","{\"instanceId\":"+std::to_string(id)+",\"path\":"+bus::jsonText(instance->path)+",\"error\":"+bus::jsonText(instance->lastError)+"}");
      return false;
    }
    instance->lastReloadOk=true;
    resetActiveInteraction();
    ++instance->generation;instance->lastError.clear();
    publishLifecycle("engine.scriptReloaded",*instance);return true;
  }
  void reloadChanged(){
    for(auto &[id,entry]:instances_)if(id&&entry->state==ScriptState::Running){
      std::error_code ec;const auto modified=std::filesystem::last_write_time(entry->path,ec);
      if(!ec&&modified!=entry->host.mtime&&!reload(id))entry->host.mtime=modified;
    }
  }
  bool stop(int id,bool manual=true){
    auto *instance=find(id);if(!instance||!id)return false;
    // A manual stop also cancels a pending automatic restart.
    if(instance->state==ScriptState::Failed&&manual&&instance->restartAt){instance->restartAt.reset();instance->state=ScriptState::Stopped;instance->manuallyStopped=true;return true;}
    if(instance->state!=ScriptState::Running)return false;
    {ScriptScope scope(instance->host);if(beforeStop_)beforeStop_(id);closeScript(instance->host);resetActiveInteraction();}
    instance->state=ScriptState::Stopped;instance->manuallyStopped=manual;
    publishLifecycle("engine.scriptStopped",*instance,"stopped");return true;
  }
  // Called from a running scene pass, which already owns this host's scope.
  bool failActive(int id,const std::string &error){
    auto *instance=find(id);
    if(!instance||instance->state!=ScriptState::Running||currentScene!=&instance->host.scene)return false;
    instance->lastError=error;
    if(beforeStop_)beforeStop_(id);
    closeScript(instance->host);resetActiveInteraction();
    instance->state=ScriptState::Failed;
    publishLifecycle("engine.scriptStopped",*instance,"failed");
    failed(*instance);return true;
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
    failScoped(*instance,"script update failed");return false;
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
    failScoped(*instance,"script input callback failed");return false;
  }
  // One delivery turn of the event bus. Recipients are fixed when a message's
  // delivery begins; stopped or reloaded subscribers are skipped when their turn comes.
  size_t dispatchBus(){
    return eventBus.dispatch([&](const bus::Message &message){
      std::vector<bus::Recipient> recipients;
      for(const auto &[id,instance]:instances_)if(instance->state==ScriptState::Running)
        for(const auto &subscription:instance->host.scene.busSubscriptions)
          if(bus::matches(subscription,message,id))recipients.push_back({id,subscription.id});
      std::sort(recipients.begin(),recipients.end(),[](const bus::Recipient &a,const bus::Recipient &b){return a.subscriptionId<b.subscriptionId;});
      return recipients;
    },[&](const bus::Recipient &recipient,const bus::Message &message){
      auto *instance=find(recipient.instanceId);
      if(!instance||instance->state!=ScriptState::Running)return;
      const auto &subscriptions=instance->host.scene.busSubscriptions;
      const auto found=std::find_if(subscriptions.begin(),subscriptions.end(),[&](const bus::Subscription &s){return s.id==recipient.subscriptionId;});
      if(found==subscriptions.end())return;
      ScriptScope scope(instance->host);
      JSContext *ctx=instance->host.ctx;
      JSValue envelope=JS_NewObject(ctx);
      JS_SetPropertyStr(ctx,envelope,"topic",JS_NewStringLen(ctx,message.topic.data(),message.topic.size()));
      JS_SetPropertyStr(ctx,envelope,"payload",JS_ParseJSON(ctx,message.payload.c_str(),message.payload.size(),"bus"));
      JS_SetPropertyStr(ctx,envelope,"sourceInstanceId",message.source?JS_NewInt32(ctx,*message.source):JS_NULL);
      JS_SetPropertyStr(ctx,envelope,"sequence",JS_NewFloat64(ctx,double(message.sequence)));
      // The subscription may be removed by its own callback.
      JSValue callback=JS_DupValue(ctx,found->callback);
      JSValue result=JS_Call(ctx,callback,JS_UNDEFINED,1,&envelope);
      JS_FreeValue(ctx,callback);JS_FreeValue(ctx,envelope);
      if(!JS_IsException(result)){JS_FreeValue(ctx,result);return;}
      JS_FreeValue(ctx,result);
      failScoped(*instance,"script bus callback failed");
    });
  }
};

inline void fanOutRawInput(ScriptRegistry &registry,std::vector<frame_input::Event> events){
  const auto recipients=registry.runningIds();
  for(auto &event:events){
    // Raw controller input carries no scene-local object identity.
    event.objectId=0;
    for(int id:recipients)if(auto *instance=registry.find(id))
      instance->host.inputQueue->addEvent(event);
  }
}

// Launches each autostart entry independently, in order. Returns one message per
// entry that could not start; the others still run.
inline std::vector<std::string> launchAutostart(ScriptRegistry &registry,const std::filesystem::path &catalog,const std::vector<std::string> &names){
  std::vector<std::string> errors;
  for(const auto &name:names){
    std::filesystem::path resolved;std::string error;int id=0;
    if(!resolveCatalogScript(catalog,name,resolved,error)||!registry.launch(resolved.string(),id,error))errors.push_back(name+": "+error);
  }
  return errors;
}
