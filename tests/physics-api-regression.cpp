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
  frameInput.resetScene();
  assert(loadScript(host,"tests/collision-api-smoke.js"));
  auto dispatch=[&]{for(const auto &event:frameInput.takeCallbackEvents()){
    JSValue value=inputEventValue(host.ctx,event);JSValue result=JS_Call(host.ctx,inputCallback,JS_UNDEFINED,1,&value);
    assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);JS_FreeValue(host.ctx,value);
  }};
  for(int i=0;i<360;i++){updateScenePhysics(host.scene,1./90);dispatch();}
  assert(eval("verifyContacts()"));
  assert(eval("removeObject(box)"));updateScenePhysics(host.scene,0);dispatch();assert(eval("verifyEnd()"));
  // Failed candidate event configuration cannot flush the live scene's queues.
  frame_input::Event queued;queued.type="physics.collisionStart";queued.objectId=1;queued.otherObjectId=2;
  frameInput.addEvent(queued);
  std::ofstream(path)<<"setPhysicsCollisionEvents(false);throw Error('EXPECTED_COLLISION_ROLLBACK');";
  assert(!loadScript(host,path.string()));assert(frameInput.takeEvents().size()==1&&frameInput.takeCallbackEvents().size()==1);
  frameInput.addEvent(queued);assert(eval("setPhysicsCollisionEvents(false);check(pollInputEvents().length===0)"));
  assert(frameInput.takeCallbackEvents().empty());
  puts("PASS collision API: callback/poll schema equality, contact continuity, end on removal, candidate isolation, option validation and disable clearing");
  assert(loadScript(host,"tests/custom-collider-api-smoke.js"));
  puts("PASS custom collider API: dimensions, radius, offset, scale independence, resolved state, shape switching, null defaults, malformed options, rollback and imported slab bounds");
  assert(loadScript(host,"tests/convex-api-smoke.js"));
  puts("PASS convex API: imported hulls, shared scaled instances, offsets, shape changes and invalid-size rollback");
  assert(loadScript(host,"tests/mesh-collider-api-smoke.js"));
  for(int i=0;i<90;i++)updateScenePhysics(host.scene,1./90);
  assert(eval("check(getObjectPhysics(sphere).position.y < -1)"));
  puts("PASS mesh API: imported concave geometry, shared scaling, offset, type restriction, state and empty gap");
  assert(loadScript(host,"tests/joint-api-smoke.js"));
  for(int i=0;i<180;i++)updateScenePhysics(host.scene,1./90);
  assert(eval("verifyJoint()"));
  auto *jointWorld=host.scene.physics.get();jointWorld->hold(2);
  auto *heldObject=findObject(2);assert(heldObject);
  for(int i=0;i<90;i++){
    heldObject->x=2;heldObject->y=2;heldObject->z=1;updateScenePhysics(host.scene,1./90);
    const auto physical=jointWorld->get(2);
    assert(std::abs(heldObject->x-physical.pose.position.x)<1e-6&&std::abs(heldObject->y-physical.pose.position.y)<1e-6);
  }
  assert(eval("check(getPhysicsJoint(tether).distance < .33)"));jointWorld->release(2,false);
  std::ofstream(path)<<"const a=loadModel('assets/samples/Box/Box.glb');const b=createObject(a,0,1,0,.1);const c=createObject(a,.2,1,0,.1);setObjectPhysics(b);setObjectPhysics(c);createPhysicsJoint(b,c);throw Error('EXPECTED_JOINT_ROLLBACK');";
  assert(!loadScript(host,path.string()));assert(host.scene.physics.get()==jointWorld);
  assert(eval("check(getPhysicsJoint(tether)!==null)"));
  puts("PASS joint API: creation, state, limits, invalid options, scale rollback, removal and tether simulation");
  assert(loadScript(host,"tests/motor-api-smoke.js"));
  for(int i=0;i<270;i++)updateScenePhysics(host.scene,1./90);
  assert(eval("verifyMotor();reverseMotor()"));
  for(int i=0;i<270;i++)updateScenePhysics(host.scene,1./90);
  assert(eval("verifyReverse()"));
  puts("PASS motor API: state, partial updates, position/velocity drive and invalid-update rollback");
  assert(loadScript(host,"tests/spring-api-smoke.js"));
  for(int i=0;i<360;i++)updateScenePhysics(host.scene,1./90);
  assert(eval("verifySpring()"));
  puts("PASS spring API: optional spring, defaults, validation, resolved state and elastic equilibrium");
  assert(loadScript(host,"tests/breakable-joint-api-smoke.js"));
  for(int i=0;i<30;i++){updateScenePhysics(host.scene,1./90);dispatch();}
  assert(eval("check(getPhysicsJoint(joint)!==null);pull()"));
  for(int i=0;i<10;i++){updateScenePhysics(host.scene,1./90);dispatch();}
  assert(eval("verifyBreak()"));
  puts("PASS breakable API: threshold validation, automatic removal, one-shot callback/poll equality and collision-toggle isolation");
  assert(loadScript(host,"tests/joint-update-api-smoke.js"));
  for(int i=0;i<180;i++)updateScenePhysics(host.scene,1./90);
  assert(eval("verifyLength();configureBreak()"));updateScenePhysics(host.scene,1./90);assert(eval("verifyUpdatedBreak()"));
  puts("PASS joint update API: partial options, same ID, nested spring merge/null reset, rollback, new limits and threshold breaking");
  puts("PASS physics API: configuration, argument validation, scaling, velocity/impulse, transforms, advancement, held rules, removal and reload isolation");
}
