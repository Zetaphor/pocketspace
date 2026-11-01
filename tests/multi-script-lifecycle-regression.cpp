// Exercise the real QuickJS loader with two independent scene hosts, no VR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>

int main(){
  assert(currentScene==nullptr&&liveScene==nullptr&&inputCallbackCtx==nullptr);
  ScriptRegistry registry;
  assert(registry.find(0)&&registry.find(0)->id==0);
  int left=0,right=0;std::string error;
  assert(registry.launch("tests/scenes/multi-script-left.js",left,error));
  assert(registry.launch("tests/scenes/multi-script-right.js",right,error));
  assert(left>0&&right>left);
  const auto *a=registry.find(left),*b=registry.find(right);
  assert(a&&b&&a->state==ScriptState::Running&&b->state==ScriptState::Running);
  assert(a->host.ctx!=b->host.ctx&&a->host.rt!=b->host.rt);
  assert(a->host.scene.objects.size()==1&&b->host.scene.objects.size()==1);
  assert(a->host.scene.objects[0].id==1&&b->host.scene.objects[0].id==1);
  assert(a->host.scene.panels.size()==1&&b->host.scene.panels.size()==1);
  assert(a->host.scene.panels[0].id==1&&b->host.scene.panels[0].id==1);
  assert(!JS_IsUndefined(a->host.callback)&&!JS_IsUndefined(b->host.callback));
  assert(inputCallbackCtx==nullptr&&currentScene==nullptr&&liveScene==nullptr);
  {
    ScriptScope scope(registry.find(left)->host);
    pointerStates[1].valid=true;hoveredObject[0]=1;meshCurrentObject[1]=1;grabNeedsRelease[0]=true;
  }
  assert(pointerStates.empty()&&hoveredObject[0]==0&&meshCurrentObject[1]==0&&!grabNeedsRelease[0]);
  assert(registry.find(left)->host.interaction.pointers.at(1).valid);
  {
    ScriptScope scope(registry.find(right)->host);
    assert(pointerStates.empty()&&hoveredObject[0]==0&&meshCurrentObject[1]==0&&!grabNeedsRelease[0]);
    pointerStates[1].hand=1;
  }
  {
    ScriptScope scope(registry.find(left)->host);
    assert(pointerStates.at(1).valid&&hoveredObject[0]==1&&meshCurrentObject[1]==1&&grabNeedsRelease[0]);
  }
  assert(registry.reload(left));
  assert(registry.find(left)->host.interaction.pointers.empty()&&registry.find(right)->host.interaction.pointers.at(1).hand==1);
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
  assert(inputCallbackCtx==nullptr&&currentScene==nullptr&&liveScene==nullptr);
  puts("PASS multi-script lifecycle: independent IDs/contexts/callbacks/interactions, duplicate rejection, file-change rollback, scoped stop and callback errors");
}
