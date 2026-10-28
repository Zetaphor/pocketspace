#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)
int main(){
  ScriptHost host;ScopeExit cleanup{[&]{closeScript(host);currentScene=liveScene=nullptr;reserveObjectOverlay={};releaseObjectOverlay={};}};
  assert(loadScript(host,"tests/scene-container-api-smoke.js"));
  assert(host.scene.objects.size()==2&&host.scene.panels.size()==1&&host.scene.retiredPanels.size()==1);
  assert(host.scene.retiredPanels[0].interactive);
  assert(host.scene.interactiveIds.empty());assert(inputCallbackCtx==host.ctx&&inputCallbackContainer>0);
  frameInput.addOverlayEvent("overlay.grabStart",1,0);frameInput.addOverlayEvent("overlay.grabEnd",1,0);dispatchInputCallbacks();
  JSValue global=JS_GetGlobalObject(host.ctx),calls=JS_GetPropertyStr(host.ctx,global,"calls");int count=0;assert(JS_ToInt32(host.ctx,&count,calls)==0&&count==1);JS_FreeValue(host.ctx,calls);JS_FreeValue(host.ctx,global);
  assert(!inputCallbackCtx&&inputCallbackContainer==0);
  auto eval=[&](const char *code){JSValue value=JS_Eval(host.ctx,code,strlen(code),"container-test",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(value)){JSValue e=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,e);fprintf(stderr,"%s\n",message?message:"exception");if(message)JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,e);std::exit(1);}JS_FreeValue(host.ctx,value);};
  int released=0;releaseObjectOverlay=[&](int key){assert(key==-4);released++;};
  eval("globalThis.owner=createSceneContainer();globalThis.survivor=createObject(1,0,0,0,.1,{renderGroup:4});globalThis.member=createObject(1,.2,0,0,.1,{renderGroup:4,container:owner});enableObjectInteraction(member);onInputEvent(()=>{}, {container:owner});");
  int owner=inputCallbackContainer;JSContext *ctx=host.ctx;
  const auto path=std::filesystem::temp_directory_path()/("sf-container-rollback-"+std::to_string(getpid())+".js");ScopeExit remove{[&]{std::filesystem::remove(path);}};
  std::ofstream(path)<<"const c=createSceneContainer();onInputEvent(()=>{}, {container:c});disposeSceneContainer(c);throw Error('EXPECTED_CONTAINER_ROLLBACK');";
  assert(!loadScript(host,path.string()));assert(host.ctx==ctx&&inputCallbackCtx==ctx&&inputCallbackContainer==owner);
  int member=host.scene.objects.back().id;pointerStates[member].valid=true;meshHoveredObject[0]=member;meshCurrentObject[0]=member;
  eval("disposeSceneContainer(owner)");assert(!released&&!pointerStates.count(member)&&meshHoveredObject[0]==0&&meshCurrentObject[0]==0);
  eval("removeObject(survivor)");assert(released==1);
  releaseObjectOverlay={};
  for(int n=0;n<30;n++)eval("{const c=createSceneContainer();const o=createObject(1,0,0,0,.1,{container:c});const p=createPanel({widthPixels:8,heightPixels:8,widthMeters:.1,container:c,interactive:true});if(!getSceneContainerState(c).panelIds.length)throw Error('panel ownership');disposeSceneContainer(c);}");
  std::ofstream(path)<<"function update(){}";assert(loadScript(host,path.string()));assert(host.scene.containers.empty()&&!inputCallbackCtx&&!inputCallbackContainer);
  puts("PASS scene containers: objects/panels/physics/joints, ownership transfer, callback self-disposal, reentrant options, rollback, shared surfaces, interaction cleanup and repeated disposal/reload");
}
