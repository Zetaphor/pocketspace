// Inter-script event bus with real QuickJS contexts, scratch instance 0 and no VR session.
#include "../src/render_host.h"
#include <cassert>

namespace {
std::filesystem::path directory;
std::filesystem::path script(const std::string &name,const std::string &source){
  const auto path=directory/name;std::ofstream(path)<<source;return path;
}
// Every test script records deliveries as "topic|source|payload".
const std::string recorder="globalThis.log=[];globalThis.record=e=>log.push(`${e.topic}|${e.sourceInstanceId}|${JSON.stringify(e.payload)}`);";
std::string run(ScriptRegistry &registry,int id,const std::string &code){
  auto *instance=registry.find(id);assert(instance&&instance->host.ctx);
  ScriptScope scope(instance->host);JSContext *ctx=instance->host.ctx;
  JSValue value=JS_Eval(ctx,code.c_str(),code.size(),"bus-test",JS_EVAL_TYPE_GLOBAL);
  std::string result;
  if(JS_IsException(value)){
    JSValue error=JS_GetException(ctx);const char *message=JS_ToCString(ctx,error);
    result=std::string("THROW:")+(message?message:"");if(message)JS_FreeCString(ctx,message);JS_FreeValue(ctx,error);
  }else{
    JSValue text=JS_JSONStringify(ctx,value,JS_UNDEFINED,JS_UNDEFINED);
    const char *data=JS_IsString(text)?JS_ToCString(ctx,text):nullptr;result=data?data:"undefined";
    if(data)JS_FreeCString(ctx,data);JS_FreeValue(ctx,text);
  }
  JS_FreeValue(ctx,value);return result;
}
std::string take(ScriptRegistry &registry,int id){return run(registry,id,"log.splice(0)");}
int launch(ScriptRegistry &registry,const std::filesystem::path &path){int id=0;std::string error;assert(registry.launch(path.string(),id,error));return id;}
void drain(ScriptRegistry &registry){while(eventBus.queued())registry.dispatchBus();}
bool throws(ScriptRegistry &registry,int id,const std::string &code){return run(registry,id,code).rfind("THROW:",0)==0;}
}

int main(){
  directory=std::filesystem::temp_directory_path()/("pocketspace-bus-"+std::to_string(getpid()));
  std::filesystem::create_directories(directory);ScopeExit cleanup{[&]{std::filesystem::remove_all(directory);}};
  ScriptRegistry registry;
  const int a=launch(registry,script("a.js",recorder+"subscribe('space.ping',record);subscribe('*',record);"));
  const int b=launch(registry,script("b.js",recorder+"subscribe('space.ping',record);"));
  drain(registry);
  // Engine events have a null source and reach catch-alls, including the started script's own.
  const auto started=take(registry,a);
  assert(started.find("engine.scriptStarted|null|{\\\"instanceId\\\":"+std::to_string(a)+",")!=std::string::npos);
  assert(started.find("engine.scriptStarted|null|{\\\"instanceId\\\":"+std::to_string(b)+",\\\"path\\\":")!=std::string::npos);
  assert(take(registry,b)=="[]");

  // Exact subscriptions see their own publication; the catch-all never does. Delivery is deferred.
  assert(run(registry,a,"typeof publish('space.ping',{n:1,list:[true,null,'x']})")=="\"number\"");
  assert(take(registry,a)=="[]"&&take(registry,b)=="[]");
  registry.dispatchBus();
  assert(take(registry,a)=="[\"space.ping|"+std::to_string(a)+"|{\\\"n\\\":1,\\\"list\\\":[true,null,\\\"x\\\"]}\"]");
  assert(take(registry,b)=="[\"space.ping|"+std::to_string(a)+"|{\\\"n\\\":1,\\\"list\\\":[true,null,\\\"x\\\"]}\"]");
  // Topics with no exact subscriber reach other instances' catch-alls only; publish order holds.
  run(registry,b,"publish('space.first',1);publish('space.second');");registry.dispatchBus();
  assert(take(registry,a)=="[\"space.first|"+std::to_string(b)+"|1\",\"space.second|"+std::to_string(b)+"|null\"]");
  assert(take(registry,b)=="[]");
  // Payloads are copied at publish time.
  run(registry,a,"const p={v:1};publish('space.ping',p);p.v=2;");registry.dispatchBus();
  assert(take(registry,b).find("{\\\"v\\\":1}")!=std::string::npos);take(registry,a);

  // Invalid topics, callbacks and payloads are rejected at the call.
  for(const char *code:{"publish('engine.scriptStarted',{})","publish('*')","publish('space.')","publish('other.x')","publish('space.a*b')",
                        "publish('space.'+'x'.repeat(123))","publish('space.x',()=>1)","publish('space.x',{a:undefined})","publish('space.x',NaN)",
                        "publish('space.x',new Date())","publish('space.x',Symbol('s'))","publish('space.x',10n)",
                        "const c={};c.self=c;publish('space.x',c)","let d=[];for(let i=0;i<32;i++)d=[d];publish('space.x',d)",
                        "publish('space.x','x'.repeat(65536))","subscribe('ping',()=>{})","subscribe('space.x',5)","subscribe('engine.',()=>{})"})
    assert(throws(registry,a,code));
  assert(!throws(registry,a,"publish('space.'+'x'.repeat(122))"));
  assert(!throws(registry,a,"let e=[];for(let i=0;i<31;i++)e=[e];publish('space.deep',e)"));
  drain(registry);take(registry,a);take(registry,b);

  // A subscriber's publication waits for the next delivery turn.
  const int c=launch(registry,script("c.js",recorder+"subscribe('space.ping',e=>{record(e);publish('space.pong',e.payload+1);});subscribe('space.pong',record);"));
  drain(registry);take(registry,a);
  run(registry,b,"publish('space.ping',1)");registry.dispatchBus();
  assert(take(registry,c)=="[\"space.ping|"+std::to_string(b)+"|1\"]");
  registry.dispatchBus();assert(take(registry,c)=="[\"space.pong|"+std::to_string(c)+"|2\"]");
  drain(registry);take(registry,a);take(registry,b);

  // A self-sustaining loop is bounded per frame and never stalls a dispatch call.
  run(registry,c,"subscribe('space.loop',()=>{publish('space.loop');publish('space.loop');});publish('space.loop');");
  for(int frame=0;frame<20;frame++)assert(registry.dispatchBus()<=bus::maxDeliveriesPerFrame);
  // The per-publisher queue fills and reports drops to the publisher as null.
  const auto dropped=eventBus.dropped();
  assert(run(registry,c,"let nulls=0;for(let i=0;i<300;i++)if(publish('space.flood')===null)nulls++;nulls")!="0");
  assert(eventBus.dropped()>dropped);
  assert(registry.stop(c));
  drain(registry);
  assert(eventBus.queued()==0);take(registry,a);take(registry,b);
  // Overflow from a quiet publisher: exactly the per-source limit is accepted.
  assert(run(registry,b,"let ok=0;for(let i=0;i<300;i++)if(publish('space.burst',i)!==null)ok++;ok")==std::to_string(bus::maxQueuedPerSource));
  drain(registry);take(registry,a);

  // Failed reload keeps the old subscriptions and drops the candidate's publications.
  script("b.js",recorder+"subscribe('space.other',record);publish('space.fromCandidate');throw Error('EXPECTED_RELOAD_FAILURE');");
  assert(!registry.reload(b));
  run(registry,a,"publish('space.ping',7);publish('space.other',8)");drain(registry);
  assert(take(registry,b)=="[\"space.ping|"+std::to_string(a)+"|7\"]");
  assert(take(registry,a).find("fromCandidate")==std::string::npos);
  // Successful reload replaces them atomically and publishes the candidate's messages after commit.
  script("b.js",recorder+"subscribe('space.other',record);publish('space.fromCandidate',3);");
  assert(registry.reload(b));
  run(registry,a,"publish('space.ping',9);publish('space.other',10)");drain(registry);
  assert(take(registry,b)=="[\"space.other|"+std::to_string(a)+"|10\"]");
  const auto aLog=take(registry,a);
  assert(aLog.find("engine.scriptReloaded")!=std::string::npos&&aLog.find("space.fromCandidate|"+std::to_string(b)+"|3")!=std::string::npos);
  // The candidate's publication keeps the sequence it received while loading.
  assert(aLog.find("space.fromCandidate")<aLog.find("engine.scriptReloaded"));

  // Recipients are fixed when delivery starts; a subscriber stopped mid-message is skipped,
  // and later-started scripts get no replay.
  const int many=launch(registry,script("many.js",recorder+"for(let i=0;i<1100;i++)subscribe('space.many',()=>{globalThis.count=(globalThis.count||0)+1;});"));
  const int late=launch(registry,script("late.js",recorder+"subscribe('space.many',record);subscribe('engine.scriptStopped',record);"));
  drain(registry);take(registry,a);
  run(registry,a,"publish('space.many')");
  assert(registry.dispatchBus()==bus::maxDeliveriesPerFrame);
  assert(run(registry,many,"count")==std::to_string(bus::maxDeliveriesPerFrame));
  assert(registry.stop(late));
  drain(registry);
  assert(run(registry,many,"count")=="1100");
  const int replay=launch(registry,script("replay.js",recorder+"subscribe('space.many',record);"));
  drain(registry);assert(take(registry,replay)=="[]");
  assert(take(registry,a).find("engine.scriptStopped|null|{\\\"instanceId\\\":"+std::to_string(late))!=std::string::npos);

  // A throwing subscriber stops only its own script; later subscribers still receive.
  const int thrower=launch(registry,script("thrower.js","subscribe('space.ping',()=>{throw Error('EXPECTED_BUS_CALLBACK')});"));
  const int after=launch(registry,script("after.js",recorder+"subscribe('space.ping',record);subscribe('engine.scriptStopped',record);"));
  drain(registry);take(registry,a);take(registry,b);
  run(registry,b,"publish('space.ping',11)");registry.dispatchBus();
  assert(registry.find(thrower)->state==ScriptState::Failed);
  assert(registry.find(thrower)->lastError.find("EXPECTED_BUS_CALLBACK")!=std::string::npos);
  assert(take(registry,after)=="[\"space.ping|"+std::to_string(b)+"|11\"]");
  registry.dispatchBus();
  const auto stopped=take(registry,after);
  assert(stopped.find("engine.scriptStopped|null")!=std::string::npos&&stopped.find("\\\"reason\\\":\\\"failed\\\"")!=std::string::npos&&stopped.find("EXPECTED_BUS_CALLBACK")!=std::string::npos);

  // Scratch instance 0 publishes with source 0 and receives like any instance.
  auto *scratch=registry.find(0);
  {ScriptScope scope(scratch->host);assert(loadScript(scratch->host,script("scratch.js",recorder+"subscribe('space.toScratch',record);").string()));}
  scratch->state=ScriptState::Running;
  drain(registry);take(registry,a);take(registry,b);
  run(registry,0,"publish('space.fromScratch',{s:true})");run(registry,a,"publish('space.toScratch',12)");drain(registry);
  assert(take(registry,a).find("space.fromScratch|0|{\\\"s\\\":true}")!=std::string::npos);
  assert(take(registry,0)=="[\"space.toScratch|"+std::to_string(a)+"|12\"]");

  // Unsubscribe removes only the caller's own subscriptions.
  const auto id=run(registry,b,"globalThis.extra=subscribe('space.extra',record);extra");
  assert(run(registry,a,"unsubscribe("+id+")")=="false");
  run(registry,a,"publish('space.extra',1)");drain(registry);assert(take(registry,b)=="[\"space.extra|"+std::to_string(a)+"|1\"]");
  assert(run(registry,b,"unsubscribe(extra)")=="true"&&run(registry,b,"unsubscribe(extra)")=="false");
  run(registry,a,"publish('space.extra',2)");drain(registry);assert(take(registry,b)=="[]");

  // Stopping removes subscriptions; nothing is delivered to a stopped script.
  assert(registry.stop(b));run(registry,a,"publish('space.ping',13)");drain(registry);
  assert(registry.find(b)->host.scene.busSubscriptions.empty());
  assert(eventBus.queued()==0&&eventBus.queuedBytes()==0);
  std::puts("PASS event bus: exact/catch-all/self rules, engine lifecycle topics, copies and validation, deferred and chained delivery, bounded loops and overflow, transactional reload, stop during delivery, no replay, failure isolation, scratch and unsubscribe");
}
