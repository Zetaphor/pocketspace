#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
static bool near(float a,float b){return std::abs(a-b)<1e-5;}
int main(int argc,char **argv){
 assert(argc==2);
 for(const char *mode:{"linear","step","cubic","skin","mesh-default","zero-default"}){
  ModelAsset asset;assert(loadModelFile(std::string(argv[1])+"/"+mode+".glb",asset));
  assert(asset.hasMorph&&asset.animations[0].supported());
  const std::string name=mode;
  const float initial=name=="mesh-default"?.25f:name=="zero-default"?0.f:name=="skin"?1.f:.5f;
  assert(near(asset.center[2],initial));
  animation::Playback state;state.clip=0;state.time=1;ModelPose pose;
  evaluateModelPose(asset,state,pose);
  const float expected=name=="step"?0:name=="cubic"?1.5f:name=="skin"?2:1;
  for(size_t v=0;v<pose.geometry.primitives[0].vertices.size();v+=5)assert(near(pose.geometry.primitives[0].vertices[v+2],expected-initial));
  ModelPose independent;state.time=0;evaluateModelPose(asset,state,independent);
  auto unchanged=independent.geometry.primitives[0].vertices;
  state.time=2;evaluateModelPose(asset,state,pose);assert(independent.geometry.primitives[0].vertices==unchanged);
  assert(near(pose.geometry.high[2],(name=="skin"?4.f:2.f)-initial));
  Object object{};object.scale=1;ControllerSnapshot hand;hand.valid=true;hand.x=-.25;hand.y=-.25;hand.z=5;const float rotation[9]={1,0,0,0,std::cos(.6981317008f),-std::sin(.6981317008f),0,std::sin(.6981317008f),std::cos(.6981317008f)};std::copy_n(rotation,9,hand.rotation);
  const float hit=meshHit(object,pose.geometry,hand,0);
  assert(hit!=std::numeric_limits<float>::max()&&near(hit,meshHit(object,pose.geometry,hand,0,false)));
 }
 for(const char *mode:{"bad-count","bad-default","bad-output"}){ModelAsset asset;assert(!loadModelFile(std::string(argv[1])+"/"+mode+".glb",asset));}
 ModelAsset cube;assert(loadModelFile("assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb",cube));
 assert(cube.hasMorph&&!cube.animations.empty()&&cube.animations[0].supported());
 ScriptHost host;ScopeExit cleanup{[&]{closeScript(host);currentScene=liveScene=nullptr;}};
 assert(loadScript(host,"tests/morph-api-smoke.js"));
 auto id=[&](const char *name){JSValue global=JS_GetGlobalObject(host.ctx),value=JS_GetPropertyStr(host.ctx,global,name);int result=0;assert(JS_ToInt32(host.ctx,&result,value)==0);JS_FreeValue(host.ctx,value);JS_FreeValue(host.ctx,global);return result;};
 const auto *source=findObject(id("morphSource")),*copy=findObject(id("morphCopy"));
 assert(source&&copy&&source->animationPose&&copy->animationPose&&source->animationPose!=copy->animationPose);
 assert(source->animationPose->geometry.primitives[0].vertices==copy->animationPose->geometry.primitives[0].vertices);
 closeScript(host);
 assert(loadScript(host,"scripts/morph.js"));
 auto button=[&](const char *hand,bool down){frame_input::Event event{};event.type="controller.button";event.hand=hand;event.control="/input/a/click";event.component.boolean=down;JSValue value=inputEventValue(host.ctx,event),result=JS_Call(host.ctx,inputCallback,JS_UNDEFINED,1,&value);assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);JS_FreeValue(host.ctx,value);};
 auto playing=[&]{return host.scene.objects[0].animation.playing;};
 assert(playing());button("right",true);assert(!playing());
 button("right",true);button("right",true);assert(!playing());
 button("right",false);button("right",true);assert(playing());
 button("left",true);assert(!playing());
 puts("PASS morph defaults, five-target STEP/LINEAR/CUBICSPLINE, morph-before-skin, bounds, picking, independent instances, malformed inputs and API");
}
