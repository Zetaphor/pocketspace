// Exercise the real QuickJS loader with two independent scene hosts, no VR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>

int main(){
  assert(currentScene==nullptr&&liveScene==nullptr&&inputCallbackCtx==nullptr&&activeInstanceId==0);
  const auto catalog=std::filesystem::temp_directory_path()/("pocketspace-catalog-"+std::to_string(getpid()));
  std::filesystem::create_directories(catalog/"nested");
  ScopeExit catalogCleanup{[&]{std::filesystem::remove_all(catalog);}};
  {std::ofstream file(catalog/"nested"/"test.js");file<<"function update(){}";}
  std::filesystem::create_symlink(std::filesystem::canonical("tests/scenes/multi-script-left.js"),catalog/"escape.js");
  std::filesystem::path catalogScript;std::string catalogError;
  assert(resolveCatalogScript(catalog,"nested/test.js",catalogScript,catalogError));
  assert(catalogScript==std::filesystem::canonical(catalog/"nested/test.js"));
  assert(!resolveCatalogScript(catalog,"../multi-script-left.js",catalogScript,catalogError));
  assert(!resolveCatalogScript(catalog,"escape.js",catalogScript,catalogError));
  assert(!resolveCatalogScript(catalog,"nested/missing.js",catalogScript,catalogError));
  {
    SceneRenderState leftRender,rightRender;
    leftRender.grabs[0].objectId=1;rightRender.grabs[0].objectId=1;
    leftRender.panelHovered[0]=1;rightRender.panelHovered[1]=1;
    leftRender.ready.push_back(nullptr);rightRender.ready.push_back(nullptr);
    leftRender.pendingTileChange=SceneRenderState::PendingTileChange{-4,-260,false};
    leftRender.resetScene();
    assert(!leftRender.grabs[0].objectId&&!leftRender.panelHovered[0]&&leftRender.ready.empty()&&!leftRender.pendingTileChange);
    assert(rightRender.grabs[0].objectId==1&&rightRender.panelHovered[1]==1&&rightRender.ready.size()==1);
  }
  ScriptRegistry registry;
  assert(registry.find(0)&&registry.find(0)->id==0);
  int left=0,right=0;std::string error;
  assert(registry.launch("tests/scenes/multi-script-left.js",left,error));
  assert(registry.launch("tests/scenes/multi-script-right.js",right,error));
  assert(left>0&&right>left);
  assert(registry.runningIds()==std::vector<int>({left,right}));
  const auto *a=registry.find(left),*b=registry.find(right);
  assert(a&&b&&a->state==ScriptState::Running&&b->state==ScriptState::Running);
  assert(a->host.ctx!=b->host.ctx&&a->host.rt!=b->host.rt);
  assert(a->host.instanceId==left&&b->host.instanceId==right);
  assert(a->host.scene.objects.size()==1&&b->host.scene.objects.size()==1);
  assert(a->host.scene.objects[0].id==1&&b->host.scene.objects[0].id==1);
  assert(a->host.scene.panels.size()==1&&b->host.scene.panels.size()==1);
  assert(a->host.scene.panels[0].id==1&&b->host.scene.panels[0].id==1);
  assert(!JS_IsUndefined(a->host.callback)&&!JS_IsUndefined(b->host.callback));
  frame_input::Event raw{};raw.type="controller.button";raw.hand="left";raw.objectId=1;
  fanOutRawInput(registry,{raw});
  auto leftRaw=registry.find(left)->host.inputQueue->takeEvents();
  auto rightRaw=registry.find(right)->host.inputQueue->takeEvents();
  assert(leftRaw.size()==1&&rightRaw.size()==1);
  assert(leftRaw[0].type=="controller.button"&&rightRaw[0].type=="controller.button");
  assert(leftRaw[0].objectId==0&&rightRaw[0].objectId==0);
  assert(registry.find(left)->host.inputQueue->takeEvents().empty());
  assert(registry.find(right)->host.inputQueue->takeEvents().empty());
  assert(inputCallbackCtx==nullptr&&currentScene==nullptr&&liveScene==nullptr);
  {
    ScriptScope scope(registry.find(left)->host);
    assert(activeInstanceId==left);
    pointerStates[1].valid=true;hoveredObject[0]=1;meshCurrentObject[1]=1;grabNeedsRelease[0]=true;
  }
  assert(activeInstanceId==0&&pointerStates.empty()&&hoveredObject[0]==0&&meshCurrentObject[1]==0&&!grabNeedsRelease[0]);
  assert(registry.find(left)->host.interaction.pointers.at(1).valid);
  {
    ScriptScope scope(registry.find(right)->host);
    assert(activeInstanceId==right);
    assert(pointerStates.empty()&&hoveredObject[0]==0&&meshCurrentObject[1]==0&&!grabNeedsRelease[0]);
    pointerStates[1].hand=1;
  }
  {
    ScriptScope scope(registry.find(left)->host);
    assert(pointerStates.at(1).valid&&hoveredObject[0]==1&&meshCurrentObject[1]==1&&grabNeedsRelease[0]);
    sceneInput().addOverlayEvent("overlay.pointer",1,0);
  }
  {
    ScriptScope scope(registry.find(right)->host);
    assert(sceneInput().takeEvents().empty());
    sceneInput().addOverlayEvent("panel.hover",1,1);
  }
  {
    ScriptScope scope(registry.find(left)->host);
    const auto events=sceneInput().takeEvents();
    assert(events.size()==1&&events[0].type=="overlay.pointer"&&events[0].objectId==1);
  }
  assert(registry.reload(left));
  assert(registry.find(left)->host.instanceId==left&&activeInstanceId==0);
  assert(registry.find(left)->host.interaction.pointers.empty()&&registry.find(right)->host.interaction.pointers.at(1).hand==1);
  {
    ScriptScope scope(registry.find(left)->host);
    assert(sceneInput().takeCallbackEvents().empty());
  }
  {
    ScriptScope scope(registry.find(right)->host);
    const auto events=sceneInput().takeEvents();
    assert(events.size()==1&&events[0].type=="panel.hover"&&events[0].objectId==1);
  }
  bool rightCleaned=false;
  const auto leftGeneration=registry.find(left)->generation;
  assert(registry.reload(right,[&]{
    assert(activeInstanceId==right&&liveScene==&registry.find(right)->host.scene);
    rightCleaned=true;
  }));
  assert(rightCleaned&&registry.find(right)->generation==2);
  assert(registry.find(left)->generation==leftGeneration&&registry.find(left)->state==ScriptState::Running);
  frame_input::Event input{};input.type="controller.button";input.hand="left";
  assert(registry.dispatchInput(left,input)&&registry.dispatchInput(right,input));
  int duplicate=0;assert(!registry.launch("tests/scenes/multi-script-left.js",duplicate,error));
  assert(error=="script is already running"&&duplicate==0);

  const auto temp=std::filesystem::temp_directory_path()/("pocketspace-multi-"+std::to_string(getpid())+".js");
  ScopeExit cleanup{[&]{std::filesystem::remove(temp);}};
  {std::ofstream out(temp);out<<"onInputEvent(()=>{});function update(){}";}
  int third=0;assert(registry.launch(temp.string(),third,error));
  JSContext *original=registry.find(third)->host.ctx;
  const auto initialMtime=registry.find(third)->host.mtime;
  {std::ofstream out(temp);out<<"onInputEvent(()=>{});throw Error('EXPECTED_ROLLBACK')";}
  std::filesystem::last_write_time(temp,initialMtime+std::chrono::seconds(2));
  registry.reloadChanged();
  assert(registry.find(third)->host.ctx==original);
  assert(registry.find(third)->generation==1&&registry.find(third)->lastError=="script reload failed");
  assert(!JS_IsUndefined(registry.find(third)->host.callback));
  assert(registry.find(right)->state==ScriptState::Running);
  assert(!JS_IsUndefined(registry.find(right)->host.callback));
  assert(registry.update(right,1./90));

  assert(registry.stop(left));
  assert(registry.find(left)->state==ScriptState::Stopped&&registry.find(left)->manuallyStopped);
  assert(registry.find(left)->host.ctx==nullptr);
  assert(registry.find(right)->host.ctx!=nullptr&&registry.update(right,1./90));
  {std::ofstream out(temp);out<<"onInputEvent(()=>{throw Error('EXPECTED_CALLBACK_FAILURE')});function update(){}";}
  std::filesystem::last_write_time(temp,initialMtime+std::chrono::seconds(4));
  registry.reloadChanged();
  assert(registry.find(third)->generation==2);
  assert(!registry.dispatchInput(third,input));
  assert(registry.find(third)->state==ScriptState::Failed);
  assert(registry.find(third)->lastError.find("EXPECTED_CALLBACK_FAILURE")!=std::string::npos);
  {std::ofstream out(temp);out<<"function update(){throw Error('EXPECTED_UPDATE_FAILURE')}";}
  int fourth=0;assert(registry.launch(temp.string(),fourth,error));
  assert(!registry.update(fourth,1./90));
  assert(registry.find(fourth)->state==ScriptState::Failed);
  assert(registry.find(fourth)->lastError.find("EXPECTED_UPDATE_FAILURE")!=std::string::npos);
  assert(registry.find(right)->state==ScriptState::Running&&registry.update(right,1./90));
  {std::ofstream out(temp);out<<"function update(){}";}
  int fifth=0;assert(registry.launch(temp.string(),fifth,error));
  {
    ScriptScope scope(registry.find(fifth)->host);
    assert(registry.failActive(fifth,"EXPECTED_PRODUCTION_FAILURE"));
  }
  assert(registry.find(fifth)->state==ScriptState::Failed);
  assert(registry.find(fifth)->lastError=="EXPECTED_PRODUCTION_FAILURE");
  assert(registry.find(right)->state==ScriptState::Running&&registry.update(right,1./90));
  {std::ofstream out(temp);out<<"onInputEvent(()=>{throw Error('EXPECTED_PRODUCTION_CALLBACK')})";}
  int sixth=0;assert(registry.launch(temp.string(),sixth,error));
  {
    ScriptScope scope(registry.find(sixth)->host);
    sceneInput().addOverlayEvent("overlay.pointer",1,0);
    const auto callbackError=dispatchInputCallbacks();
    assert(callbackError.find("EXPECTED_PRODUCTION_CALLBACK")!=std::string::npos);
    assert(registry.failActive(sixth,callbackError));
  }
  assert(registry.find(sixth)->state==ScriptState::Failed);
  assert(registry.find(right)->state==ScriptState::Running&&registry.update(right,1./90));
  const auto frameSnapshot=registry.runningIds();
  assert(frameSnapshot==std::vector<int>{right});
  assert(registry.stop(right));
  assert(frameSnapshot==std::vector<int>{right}&&registry.runningIds().empty());
  assert(inputCallbackCtx==nullptr&&currentScene==nullptr&&liveScene==nullptr);
  {
    ScriptRegistry production;
    int id=0;std::string launchError;
    assert(production.launch("tests/scenes/multi-script-left.js",id,launchError));
    bool committed=false;
    assert(production.reload(id,[&]{
      assert(activeInstanceId==id&&liveScene==&production.find(id)->host.scene);
      committed=true;
    }));
    assert(committed&&production.find(id)->generation==2);
    assert(production.find(id)->host.instanceId==id&&activeInstanceId==0);
    assert(production.stop(id));
    assert(!production.find(id)->host.ctx&&currentScene==nullptr&&liveScene==nullptr&&inputCallbackCtx==nullptr);
  }
  puts("PASS multi-script lifecycle: independent IDs/contexts/callbacks/interactions, duplicate rejection, file-change rollback, scoped stop and callback errors");
}
