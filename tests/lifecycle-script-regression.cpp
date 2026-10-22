// Include the real loader/bindings; no VR session or graphics context is created.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
int main(){
  const auto path=std::filesystem::temp_directory_path()/("sf-script-lifecycle-"+std::to_string(getpid())+".js");
  ScopeExit remove{[&]{std::filesystem::remove(path);}};
  auto write=[&](const char *source){std::ofstream(path)<<source;};
  ScriptHost host;ScopeExit cleanup{[&]{closeScript(host);currentScene=liveScene=nullptr;}};
  write("globalThis.received=0;onInputEvent(()=>received++);function update(){}");
  assert(loadScript(host,path.string()));
  JSContext *original=host.ctx;
  pointerStates[1].valid=true;pointerStates[1].down=true;pointerStates[1].hand=0;
  frameInput.addOverlayEvent("overlay.grabStart",1,0);
  write("const asset=loadModel('assets/samples/Box/Box.glb',512);const id=createObject(asset,0,0,0,.1);removeObject(id);if(pollInputEvents().length)throw Error('candidate saw active queue');onInputEvent(()=>{throw Error('candidate callback leaked')});throw Error('EXPECTED_ROLLBACK');");
  bool committed=false;
  assert(!loadScript(host,path.string(),[&]{committed=true;}));
  assert(!committed&&host.ctx==original&&inputCallbackCtx==original&&currentScene==&host.scene&&liveScene==&host.scene);
  assert(pointerStates[1].valid&&pointerStates[1].down);
  auto events=frameInput.takeEvents();assert(events.size()==1&&events[0].type=="overlay.grabStart");
  auto callbackEvents=frameInput.takeCallbackEvents();assert(callbackEvents.size()==1);
  JSValue event=inputEventValue(host.ctx,callbackEvents[0]);JSValue result=JS_Call(host.ctx,inputCallback,JS_UNDEFINED,1,&event);assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);JS_FreeValue(host.ctx,event);
  JSValue global=JS_GetGlobalObject(host.ctx),received=JS_GetPropertyStr(host.ctx,global,"received");int32_t count=0;assert(JS_ToInt32(host.ctx,&count,received)==0&&count==1);JS_FreeValue(host.ctx,received);JS_FreeValue(host.ctx,global);
  // Deferred panel storage remains owned until the host detaches the overlay.
  write("const panel=createPanel({widthPixels:8,heightPixels:8,widthMeters:.1});removePanel(panel);function update(){}");
  frameInput.addOverlayEvent("overlay.grabEnd",1,0);
  assert(loadScript(host,path.string(),[&]{committed=true;pointerStates.clear();frameInput.resetScene();}));
  assert(committed&&inputCallbackCtx==nullptr&&pointerStates.empty());
  assert(frameInput.takeEvents().empty()&&frameInput.takeCallbackEvents().empty());
  assert(host.scene.panels.empty()&&host.scene.retiredPanels.size()==1);
  for(int i=0;i<30;i++){
    write("onInputEvent(()=>{});function update(){}");assert(loadScript(host,path.string()));
    assert(inputCallbackCtx==host.ctx&&currentScene==&host.scene&&liveScene==&host.scene);
    write("onInputEvent(()=>{});throw Error('EXPECTED_ROLLBACK');");original=host.ctx;
    assert(!loadScript(host,path.string()));assert(inputCallbackCtx==original&&host.ctx==original);
  }
  puts("PASS script lifecycle: failed candidate isolation, live pointer/queue/callback rollback, commit hook, deferred panels, repeated context teardown");
}
