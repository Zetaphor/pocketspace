#pragma once
// Control operations over the script registry, answered from the host thread.
// Requests and responses are version 1 JSON objects; see docs/control-protocol.md.
#include "control_server.h"
#include <deque>
#include <map>

namespace control {
constexpr size_t maxRequestIdBytes=128,maxRecentOutcomes=256;
constexpr int defaultEvalMs=1000,maxEvalMs=10000;

class Service {
public:
  struct Hooks {
    // Replaces a running script's scene with the host's normal reload cleanup.
    std::function<bool(int)> reload;
    // Applies settings after a successful settings.set or external edit.
    std::function<void(const settings::Settings&)> applySettings;
  };
private:
  ScriptRegistry &registry_;std::filesystem::path catalog_;settings::File &settings_;Hooks hooks_;
  JSRuntime *rt_=nullptr;JSContext *ctx_=nullptr;
  // Mutating requests by ID, oldest first, for duplicate detection and outcome lookup.
  std::deque<std::string> recentOrder_;std::map<std::string,std::string> recent_;
  uint64_t eventSequence_=0,lastDropped_=0;
  struct Seen {ScriptState state;uint64_t generation;std::string lastError;};
  std::map<int,Seen> seen_;
  std::vector<std::string> pendingEvents_;

  static std::string quote(const std::string &s){return settings::jsonString(s);}
  static const char *stateName(ScriptState state){return state==ScriptState::Running?"running":state==ScriptState::Failed?"failed":"stopped";}
  std::string instanceView(const ScriptInstance &instance)const{
    const auto now=std::chrono::steady_clock::now();
    const double uptime=instance.state==ScriptState::Running?std::chrono::duration<double>(now-instance.startedAt).count():0;
    std::string name=instance.id?catalogKey(catalog_,instance.path):"scratch";
    if(name.empty()||name.rfind("..",0)==0)name=instance.path;
    std::string out="{\"instanceId\":"+std::to_string(instance.id)+",\"name\":"+quote(name)+",\"path\":"+quote(instance.path)
      +",\"state\":\""+stateName(instance.state)+"\",\"generation\":"+std::to_string(instance.generation)
      +",\"uptimeSeconds\":"+std::to_string(uptime)+",\"lastError\":"+(instance.lastError.empty()?"null":quote(instance.lastError))
      +",\"restartPending\":"+(instance.restartAt?"true":"false")
      +",\"lastReload\":"+(instance.lastReloadOk?(*instance.lastReloadOk?"\"ok\"":"\"failedKeptScene\""):"null")+"}";
    return out;
  }
  static std::string ok(const std::string &id,const std::string &result){return "{\"v\":1,\"id\":"+id+",\"ok\":true,\"result\":"+result+"}";}
  static std::string fail(const std::string &id,const char *code,const std::string &message,const std::string &extra=""){
    return "{\"v\":1,\"id\":"+id+",\"ok\":false,\"error\":{\"code\":\""+code+"\",\"message\":"+quote(message)+extra+"}}";
  }
  // Field readers over the parsed request; each returns false for a wrong type.
  bool field(JSValueConst request,const char *name,std::string &out,bool required=true){
    JSValue v=JS_GetPropertyStr(ctx_,request,name);
    if(JS_IsUndefined(v)){JS_FreeValue(ctx_,v);return !required;}
    const char *s=JS_IsString(v)?JS_ToCString(ctx_,v):nullptr;
    if(s){out=s;JS_FreeCString(ctx_,s);}
    JS_FreeValue(ctx_,v);return s!=nullptr;
  }
  bool field(JSValueConst request,const char *name,int64_t &out,bool required=true){
    JSValue v=JS_GetPropertyStr(ctx_,request,name);
    if(JS_IsUndefined(v)){JS_FreeValue(ctx_,v);return !required;}
    double d=0;const bool valid=JS_IsNumber(v)&&JS_ToFloat64(ctx_,&d,v)==0&&std::isfinite(d)&&d==std::floor(d)&&d>=0&&d<9007199254740992.;
    if(valid)out=int64_t(d);
    JS_FreeValue(ctx_,v);return valid;
  }
  std::string remember(const std::string &requestId,std::string response){
    if(!recent_.count(requestId)){
      recentOrder_.push_back(requestId);
      if(recentOrder_.size()>maxRecentOutcomes){recent_.erase(recentOrder_.front());recentOrder_.pop_front();}
    }
    recent_[requestId]=response;return response;
  }
  void event(const std::string &type,const std::string &body){
    pendingEvents_.push_back("{\"v\":1,\"event\":\""+type+"\",\"seq\":"+std::to_string(++eventSequence_)+body+"}");
  }
  std::string run(const std::string &op,const std::string &id,JSValueConst request,Client &client){
    if(op=="catalog"){
      std::string error;const auto keys=listCatalogScripts(catalog_,error);
      if(!error.empty())return fail(id,"unavailable",error);
      std::string list="[";for(size_t i=0;i<keys.size();i++)list+=(i?",":"")+quote(keys[i]);
      return ok(id,"{\"scripts\":"+list+"]}");
    }
    if(op=="list"){
      std::string list="[";bool first=true;
      for(int instanceId:registry_.allIds())if(const auto *instance=registry_.find(instanceId)){list+=(first?"":",")+instanceView(*instance);first=false;}
      return ok(id,"{\"instances\":"+list+"]}");
    }
    if(op=="stats"){
      auto number=[](const auto &value){return value?std::to_string(*value):std::string("null");};
      size_t running=0;for(int instanceId:registry_.allIds())running+=registry_.find(instanceId)->state==ScriptState::Running;
      return ok(id,"{\"fps\":"+number(engineStats.fps)+",\"cpuPercent\":"+number(engineStats.cpuPercent)+",\"ramBytes\":"+number(engineStats.ramBytes)
        +",\"vramBytes\":"+number(engineStats.vramBytes)+",\"instances\":{\"running\":"+std::to_string(running)+",\"total\":"+std::to_string(registry_.allIds().size())
        +"},\"bus\":{\"queued\":"+std::to_string(eventBus.queued())+",\"queuedBytes\":"+std::to_string(eventBus.queuedBytes())+",\"dropped\":"+std::to_string(eventBus.dropped())+"}}");
    }
    if(op=="watch"){client.watching=true;return ok(id,"{\"watching\":true,\"seq\":"+std::to_string(eventSequence_)+"}");}
    if(op=="settings.get"){
      const auto &current=settings_.current();
      return ok(id,"{\"path\":"+quote(settings_.path().string())+",\"settings\":"+settings::serialize(current,false)+",\"fileError\":"+(settings_.error().empty()?"null":quote(settings_.error()))+"}");
    }
    if(op=="outcome"){
      std::string requestId;if(!field(request,"requestId",requestId))return fail(id,"bad_request","outcome requires requestId");
      const auto found=recent_.find(requestId);
      if(found==recent_.end())return fail(id,"not_found","no recent outcome for that request ID");
      return ok(id,"{\"response\":"+found->second+"}");
    }
    int64_t instanceId=0;
    if(op=="status"){
      if(!field(request,"instanceId",instanceId))return fail(id,"bad_request","status requires instanceId");
      const auto *instance=instanceId<=INT_MAX?registry_.find(int(instanceId)):nullptr;
      return instance?ok(id,instanceView(*instance)):fail(id,"not_found","unknown instance");
    }
    return fail(id,"unknown_op","unknown operation: "+op);
  }
  // Operations that change engine state. Their responses are remembered by request ID.
  std::string mutate(const std::string &op,const std::string &id,JSValueConst request){
    int64_t instanceId=0,generation=0;
    auto target=[&](bool requireGeneration,ScriptInstance *&instance)->std::string{
      if(!field(request,"instanceId",instanceId)||instanceId>INT_MAX)return fail(id,"bad_request",op+" requires instanceId");
      if(!field(request,"generation",generation,requireGeneration))return fail(id,"bad_request",op+" requires the target's generation");
      instance=registry_.find(int(instanceId));
      if(!instance)return fail(id,"not_found","unknown instance");
      if(generation&&uint64_t(generation)!=instance->generation)
        return fail(id,"stale_generation","instance has a newer generation",",\"generation\":"+std::to_string(instance->generation));
      return {};
    };
    if(op=="start"){
      std::string name;if(!field(request,"name",name))return fail(id,"bad_request","start requires a catalog name");
      std::filesystem::path resolved;std::string error;
      if(!resolveCatalogScript(catalog_,name,resolved,error))return fail(id,"not_found",error);
      int launched=0;
      if(!registry_.launch(resolved.string(),launched,error)){
        if(!launched)return fail(id,"conflict",error);
        return fail(id,"load_failed",error,",\"instance\":"+instanceView(*registry_.find(launched)));
      }
      return ok(id,instanceView(*registry_.find(launched)));
    }
    if(op=="stop"){
      ScriptInstance *instance=nullptr;if(auto error=target(true,instance);!error.empty())return error;
      if(!instanceId)return fail(id,"invalid","scratch cannot be stopped; use scratch.reset");
      if(!registry_.stop(int(instanceId)))return fail(id,"invalid","instance is not running");
      return ok(id,instanceView(*instance));
    }
    if(op=="reload"){
      ScriptInstance *instance=nullptr;if(auto error=target(true,instance);!error.empty())return error;
      if(!instanceId||instance->state!=ScriptState::Running)return fail(id,"invalid","only a running user script can be reloaded");
      const bool reloaded=hooks_.reload?hooks_.reload(int(instanceId)):registry_.reload(int(instanceId));
      if(!reloaded)return fail(id,"reload_failed",instance->lastError.empty()?"reload failed; the previous scene is kept":instance->lastError,",\"instance\":"+instanceView(*instance));
      return ok(id,instanceView(*instance));
    }
    if(op=="scratch.reset"){
      if(!registry_.resetScratch())return fail(id,"unavailable","scratch scene is unavailable");
      return ok(id,instanceView(*registry_.find(0)));
    }
    if(op=="settings.set"){
      JSValue value=JS_GetPropertyStr(ctx_,request,"settings");
      JSValue text=JS_IsObject(value)?JS_JSONStringify(ctx_,value,JS_UNDEFINED,JS_UNDEFINED):JS_UNDEFINED;JS_FreeValue(ctx_,value);
      std::string json;if(JS_IsString(text)){const char *s=JS_ToCString(ctx_,text);json=s?s:"";if(s)JS_FreeCString(ctx_,s);}
      JS_FreeValue(ctx_,text);
      settings::Settings parsed;std::string error;
      if(json.empty())return fail(id,"bad_request","settings.set requires a settings object");
      if(!settings::parse(json,parsed,error))return fail(id,"invalid",error);
      if(!settings::write(settings_.path(),parsed,error))return fail(id,"unavailable",error);
      settings_.refresh();if(hooks_.applySettings)hooks_.applySettings(settings_.current());
      return ok(id,"{\"settings\":"+settings::serialize(settings_.current(),false)+"}");
    }
    if(op=="publish"){
      std::string topic;if(!field(request,"topic",topic))return fail(id,"bad_request","publish requires a topic");
      if(!bus::scriptPublishable(topic))return fail(id,"invalid","publish topics are space.* names of at most 128 bytes without *");
      JSValue payload=JS_GetPropertyStr(ctx_,request,"payload");std::string error;
      const std::string encoded=JS_IsUndefined(payload)?"null":bus::encodePayload(ctx_,payload,error);JS_FreeValue(ctx_,payload);
      if(!error.empty())return fail(id,"invalid",error);
      // The control socket publishes as the scratch scene.
      const auto sequence=eventBus.publish(0,topic,encoded);
      return ok(id,"{\"sequence\":"+(sequence?std::to_string(*sequence):std::string("null"))+"}");
    }
    if(op=="eval"){
      std::string code;if(!field(request,"code",code))return fail(id,"bad_request","eval requires code");
      if(!field(request,"instanceId",instanceId,false)||instanceId>INT_MAX)return fail(id,"bad_request","instanceId must be an instance ID");
      if(!field(request,"generation",generation,instanceId!=0))return fail(id,"bad_request","eval in a user script requires its generation");
      int64_t timeout=defaultEvalMs;if(!field(request,"timeoutMs",timeout,false)||timeout<1||timeout>maxEvalMs)return fail(id,"bad_request","timeoutMs must be 1 to 10000");
      const auto outcome=registry_.evaluate(int(instanceId),uint64_t(generation),code,std::chrono::milliseconds(timeout));
      using Status=ScriptRegistry::Evaluation::Status;
      const std::string tag=",\"instanceId\":"+std::to_string(instanceId)+",\"generation\":"+std::to_string(outcome.generation);
      switch(outcome.status){
        case Status::NotRunning:return fail(id,"not_found","instance is not running");
        case Status::StaleGeneration:return fail(id,"stale_generation","instance has a newer generation",",\"generation\":"+std::to_string(outcome.generation));
        case Status::TooLarge:return fail(id,"too_large","eval source exceeds 256 KiB");
        case Status::Timeout:return fail(id,"timeout","evaluation exceeded "+std::to_string(timeout)+" ms; the instance was stopped",tag+",\"output\":"+quote(outcome.output));
        case Status::Exception:return ok(id,"{\"result\":null,\"exception\":"+quote(outcome.exception)+",\"output\":"+quote(outcome.output)+tag+"}");
        case Status::Ok:break;
      }
      return ok(id,"{\"result\":"+quote(outcome.result)+",\"exception\":null,\"output\":"+quote(outcome.output)+tag+"}");
    }
    return {};
  }
public:
  Service(ScriptRegistry &registry,std::filesystem::path catalog,settings::File &file,Hooks hooks={})
    :registry_(registry),catalog_(std::move(catalog)),settings_(file),hooks_(std::move(hooks)){
    rt_=JS_NewRuntime();ctx_=rt_?JS_NewContext(rt_):nullptr;lastDropped_=eventBus.dropped();
  }
  ~Service(){if(ctx_)JS_FreeContext(ctx_);if(rt_)JS_FreeRuntime(rt_);}
  Service(const Service&)=delete;Service &operator=(const Service&)=delete;
  std::string handle(Client &client,const std::string &line){
    if(!ctx_)return fail("null","unavailable","control parser unavailable");
    JSValue request=JS_ParseJSON(ctx_,line.c_str(),line.size(),"request");
    if(JS_IsException(request)){JSValue e=JS_GetException(ctx_);JS_FreeValue(ctx_,e);return fail("null","bad_request","request is not valid JSON");}
    std::string response;
    std::string requestId,op;int64_t version=0;
    if(!JS_IsObject(request)||JS_IsArray(request))response=fail("null","bad_request","request must be a JSON object");
    else if(!field(request,"id",requestId)||requestId.empty()||requestId.size()>maxRequestIdBytes)response=fail("null","bad_request","id must be a string of 1 to 128 bytes");
    else if(!field(request,"v",version)||version!=1)response=fail(quote(requestId),"unsupported_version","protocol version must be 1");
    else if(!field(request,"op",op))response=fail(quote(requestId),"bad_request","op must be a string");
    else{
      const std::string id=quote(requestId);
      static const std::set<std::string> mutating{"start","stop","reload","scratch.reset","settings.set","publish","eval"};
      if(mutating.count(op)){
        // A repeated mutating request returns its first outcome instead of running again.
        if(const auto found=recent_.find(requestId);found!=recent_.end())response=found->second.substr(0,found->second.size()-1)+",\"duplicate\":true}";
        else response=remember(requestId,mutate(op,id,request));
      }else response=run(op,id,request,client);
    }
    JS_FreeValue(ctx_,request);
    return response;
  }
  // Records status changes, failures and bus drops for watchers. Call once per frame.
  std::vector<std::string> takeEvents(){
    for(int instanceId:registry_.allIds()){
      const auto *instance=registry_.find(instanceId);
      const Seen now{instance->state,instance->generation,instance->lastError};
      const auto previous=seen_.find(instanceId);
      if(previous==seen_.end()||previous->second.state!=now.state||previous->second.generation!=now.generation){
        event("status",",\"instance\":"+instanceView(*instance));
        if(now.state==ScriptState::Failed)event("error",",\"instanceId\":"+std::to_string(instanceId)+",\"message\":"+quote(now.lastError));
      }
      seen_[instanceId]=now;
    }
    if(eventBus.dropped()!=lastDropped_){
      event("busDrop",",\"dropped\":"+std::to_string(eventBus.dropped()-lastDropped_)+",\"total\":"+std::to_string(eventBus.dropped()));
      lastDropped_=eventBus.dropped();
    }
    return std::exchange(pendingEvents_,{});
  }
};
}
