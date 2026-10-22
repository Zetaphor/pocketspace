#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
int main(){
  ScriptHost host;ScopeExit cleanup{[&]{closeScript(host);}};
  assert(loadScript(host,"tests/physics-api-smoke.js"));
  for(int i=0;i<45;i++)updateScenePhysics(host.scene,1./90);
  auto eval=[&](const char *source){auto value=JS_Eval(host.ctx,source,strlen(source),"physics-regression",JS_EVAL_TYPE_GLOBAL);const bool ok=!JS_IsException(value);if(!ok){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"%s\n",message);JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}JS_FreeValue(host.ctx,value);return ok;};
  assert(eval("verifyAdvance()"));
  auto &w=*host.scene.physics;
  w.hold(1);assert(eval("check(getObjectPhysics(box).held);rejects(()=>applyObjectImpulse(box,1,0,0))"));
  w.release(1,false);
  assert(eval("setObjectPosition(box,0,0,0);setObjectVelocity(box,0,0,0);enableObjectInteraction(box,{grabButton:'both'})"));
  controllers[0].valid=controllers[0].connected=true;
  for(int i=0;i<3;i++)controllers[0].rotation[i*3+i]=1;
  pointerStates[1].valid=true;pointerStates[1].down=true;pointerStates[1].hand=0;
  std::array<GrabState,2> grabs{};
  updateManipulation(host.scene,grabs,1.f/90);assert(w.get(1).held);
  for(int i=0;i<15;i++){
    controllers[0].x+=.01f;updateManipulation(host.scene,grabs,1.f/90);updateScenePhysics(host.scene,1./90);
  }
  pointerStates[1].down=false;updateManipulation(host.scene,grabs,1.f/90);
  assert(!w.get(1).held&&w.get(1).velocity.x>.8f);
  pointerStates[1].down=true;updateManipulation(host.scene,grabs,1.f/90);assert(w.get(1).held);
  controllers[0].connected=false;updateManipulation(host.scene,grabs,1.f/90);
  assert(!w.get(1).held&&w.get(1).velocity.x==0);

  assert(eval("removeObjectPhysics(box);check(getObjectPhysics(box)===null);setObjectPhysics(box);removeObject(box)"));
  assert(!w.contains(1));
  // A rejected candidate must not replace or mutate the active physics world.
  const auto path=std::filesystem::temp_directory_path()/("sf-physics-rollback-"+std::to_string(getpid())+".js");
  ScopeExit unlink{[&]{std::filesystem::remove(path);}};
  std::ofstream(path)<<"const a=loadModel('assets/samples/Box/Box.glb');const b=createObject(a,0,1,0,.2);setObjectPhysics(b);throw Error('EXPECTED_ROLLBACK');";
  auto *active=host.scene.physics.get();assert(!loadScript(host,path.string()));assert(host.scene.physics.get()==active);
  // Reload releases the old world and every body.
  assert(loadScript(host,"scripts/avocado.js"));assert(!host.scene.physics);
  puts("PASS physics API: configuration, argument validation, scaling, velocity/impulse, transforms, advancement, held rules, removal and reload isolation");
}
