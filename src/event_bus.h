#pragma once
// Inter-script publish/subscribe. Payloads cross QuickJS runtimes as validated JSON
// text, so no value is shared between contexts. Delivery is deferred to the host loop.
#include <quickjs.h>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bus {
constexpr size_t maxTopicBytes=128,maxPayloadBytes=64*1024,maxDepth=32;
constexpr size_t maxQueuedPerSource=256,maxQueuedBytes=4*1024*1024,maxDeliveriesPerFrame=1024;

struct Message {
  uint64_t sequence=0;
  std::string topic,payload;
  // Null for engine-originated events; scratch publishes as instance 0.
  std::optional<int> source;
  size_t bytes()const{return topic.size()+payload.size();}
};
// Owned by one scene; the callback belongs to that scene's QuickJS context.
struct Subscription {uint64_t id=0;std::string topic;JSValue callback=JS_UNDEFINED;};
struct Recipient {int instanceId=0;uint64_t subscriptionId=0;};

inline bool validTopicName(const std::string &topic){
  if(topic.empty()||topic.size()>maxTopicBytes)return false;
  const auto dot=topic.find('.');
  if(dot==std::string::npos||dot+1==topic.size())return false;
  for(unsigned char c:topic)if(c<0x20||c==0x7f||c=='*')return false;
  return true;
}
// Scripts publish only space.* topics. engine.* is reserved for the engine.
inline bool scriptPublishable(const std::string &topic){return topic.rfind("space.",0)==0&&validTopicName(topic);}
inline bool subscribable(const std::string &topic){
  return topic=="*"||((topic.rfind("space.",0)==0||topic.rfind("engine.",0)==0)&&validTopicName(topic));
}
// Catch-all sees other publishers and the engine; an exact topic also sees its own.
inline bool matches(const Subscription &subscription,const Message &message,int subscriber){
  if(subscription.topic=="*")return !message.source||*message.source!=subscriber;
  return subscription.topic==message.topic;
}

// Accepts null, booleans, finite numbers, strings, arrays and plain objects.
// Returns an empty string with an error message for anything else.
inline std::string encodePayload(JSContext *ctx,JSValueConst value,std::string &error){
  JSValue global=JS_GetGlobalObject(ctx),object=JS_GetPropertyStr(ctx,global,"Object");
  JSValue objectPrototype=JS_GetPropertyStr(ctx,object,"prototype");JS_FreeValue(ctx,object);JS_FreeValue(ctx,global);
  std::vector<JSValueConst> path;
  auto check=[&](auto &&self,JSValueConst v,size_t depth)->bool{
    if(JS_IsNull(v)||JS_IsBool(v)||JS_IsString(v))return true;
    if(JS_IsNumber(v)){double n=0;JS_ToFloat64(ctx,&n,v);if(std::isfinite(n))return true;error="payload numbers must be finite";return false;}
    if(!JS_IsObject(v)||JS_IsFunction(ctx,v)){error="payload must contain only null, booleans, numbers, strings, arrays and plain objects";return false;}
    if(depth>=maxDepth){error="payload nesting exceeds 32 levels";return false;}
    for(JSValueConst ancestor:path)if(JS_VALUE_GET_PTR(ancestor)==JS_VALUE_GET_PTR(v)){error="payload contains a cycle";return false;}
    const bool array=JS_IsArray(v);
    if(!array){
      JSValue prototype=JS_GetPrototype(ctx,v);
      const bool plain=JS_IsNull(prototype)||(JS_IsObject(prototype)&&JS_VALUE_GET_PTR(prototype)==JS_VALUE_GET_PTR(objectPrototype));
      JS_FreeValue(ctx,prototype);
      if(!plain){error="payload objects must be plain objects or arrays";return false;}
    }
    path.push_back(v);
    JSPropertyEnum *properties=nullptr;uint32_t count=0;bool ok=true;
    if(JS_GetOwnPropertyNames(ctx,&properties,&count,v,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)<0){error="payload properties are unreadable";ok=false;count=0;}
    for(uint32_t i=0;ok&&i<count;i++){
      JSValue child=JS_GetProperty(ctx,v,properties[i].atom);
      if(JS_IsException(child)){error="payload property getter failed";ok=false;}
      else ok=self(self,child,depth+1);
      JS_FreeValue(ctx,child);
    }
    if(properties)JS_FreePropertyEnum(ctx,properties,count);
    path.pop_back();
    return ok;
  };
  const bool valid=check(check,value,0);JS_FreeValue(ctx,objectPrototype);
  if(!valid){JSValue pending=JS_GetException(ctx);JS_FreeValue(ctx,pending);return {};}
  JSValue text=JS_JSONStringify(ctx,value,JS_UNDEFINED,JS_UNDEFINED);
  if(JS_IsException(text)){JSValue pending=JS_GetException(ctx);JS_FreeValue(ctx,pending);error="payload cannot be encoded as JSON";return {};}
  size_t size=0;const char *data=JS_ToCStringLen(ctx,&size,text);
  std::string result=data?std::string(data,size):std::string();if(data)JS_FreeCString(ctx,data);JS_FreeValue(ctx,text);
  if(result.size()>maxPayloadBytes){error="payload exceeds 64 KiB";return {};}
  return result;
}
inline std::string jsonText(const std::string &value){
  std::string out="\"";
  for(unsigned char c:value){
    if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
    else if(c<0x20){static const char *hex="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=char(c);
  }
  return out+'"';
}

class Bus {
  std::deque<Message> queue_;
  std::map<int,size_t> queuedBySource_;
  size_t queuedBytes_=0;
  uint64_t nextSequence_=1,nextSubscription_=1,dropped_=0;
  // Recipients of the front message, fixed when its delivery begins.
  std::vector<Recipient> recipients_;size_t nextRecipient_=0;bool frontStarted_=false;
public:
  uint64_t newSubscriptionId(){return nextSubscription_++;}
  bool hasRoom(std::optional<int> source,size_t bytes,size_t staged=0)const{
    if(queuedBytes_+bytes>maxQueuedBytes)return false;
    if(!source)return true;
    const auto found=queuedBySource_.find(*source);
    return (found==queuedBySource_.end()?0:found->second)+staged<maxQueuedPerSource;
  }
  uint64_t reserveSequence(){return nextSequence_++;}
  void countDrop(){++dropped_;}
  // Queues a message that already has a sequence number; false and a drop when full.
  bool enqueue(Message message){
    if(!hasRoom(message.source,message.bytes())){++dropped_;return false;}
    queuedBytes_+=message.bytes();
    if(message.source)++queuedBySource_[*message.source];
    queue_.push_back(std::move(message));return true;
  }
  std::optional<uint64_t> publish(std::optional<int> source,std::string topic,std::string payload){
    Message message{0,std::move(topic),std::move(payload),source};
    if(!hasRoom(source,message.bytes())){++dropped_;return std::nullopt;}
    message.sequence=reserveSequence();const auto sequence=message.sequence;enqueue(std::move(message));return sequence;
  }
  size_t queued()const{return queue_.size();}
  size_t queuedBytes()const{return queuedBytes_;}
  uint64_t dropped()const{return dropped_;}
  void clear(){queue_.clear();queuedBySource_.clear();queuedBytes_=0;recipients_.clear();nextRecipient_=0;frontStarted_=false;}

  // Delivers up to the per-frame budget, oldest message first. Messages published by
  // these callbacks wait for the next call. `collect` lists a message's recipients in
  // subscription order; `deliver` invokes one and reports nothing back.
  template<class Collect,class Deliver>
  size_t dispatch(Collect &&collect,Deliver &&deliver){
    size_t delivered=0,messages=queue_.size();
    while(messages&&!queue_.empty()&&delivered<maxDeliveriesPerFrame){
      if(!frontStarted_){recipients_=collect(queue_.front());nextRecipient_=0;frontStarted_=true;}
      while(nextRecipient_<recipients_.size()&&delivered<maxDeliveriesPerFrame){
        const auto recipient=recipients_[nextRecipient_++];
        // A copy: callbacks may publish, which can reallocate the queue.
        const Message message=queue_.front();
        deliver(recipient,message);++delivered;
      }
      if(nextRecipient_<recipients_.size())break;
      const Message &done=queue_.front();
      queuedBytes_-=done.bytes();
      if(done.source&&--queuedBySource_[*done.source]==0)queuedBySource_.erase(*done.source);
      queue_.pop_front();frontStarted_=false;recipients_.clear();--messages;
    }
    return delivered;
  }
};
}
inline bus::Bus eventBus;
