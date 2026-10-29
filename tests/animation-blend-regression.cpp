#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <random>
static bool near(float a,float b){return std::abs(a-b)<2e-5;}
int main(){
 // Analytic local-space interpolation: translation, scale, shortest-path
 // quaternion rotation, unbounded morph weights, and morphs on matrix nodes.
 std::vector<animation::Node> from(2),to(2);from[0].weights={-1,2};to[0].weights={3,0};
 to[0].translation={4,2,-2};to[0].scale={3,5,1};to[0].rotation={0,0,1,0};
 from[1].matrixOnly=to[1].matrixOnly=true;from[1].weights={0};to[1].weights={2};
 animation::blendNodes(from,to,.5);
 assert(near(to[0].translation[0],2)&&near(to[0].scale[1],3));
 assert(near(to[0].rotation[2],std::sqrt(.5f))&&near(to[0].rotation[3],std::sqrt(.5f)));
 assert(near(to[0].weights[0],1)&&near(to[1].weights[0],1));
 to[0].rotation={0,0,0,-1};animation::blendNodes(from,to,.5);assert(near(std::abs(to[0].rotation[3]),1));
 ScriptHost host;ScopeExit cleanup{[&]{closeScript(host);currentScene=liveScene=nullptr;}};
 assert(loadScript(host,"tests/animation-blend-api-smoke.js"));
 auto js=[&](const char *source){JSValue value=JS_Eval(host.ctx,source,strlen(source),"blend-test",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(value)){JSValue error=JS_GetException(host.ctx);const char *text=JS_ToCString(host.ctx,error);fprintf(stderr,"%s\n",text?text:"error");assert(false);}JS_FreeValue(host.ctx,value);};
 auto step=[&](double dt){for(int id=1;id<=3;id++){auto &o=*findObject(id);const auto &a=host.scene.assets[o.asset-1];if(animation::advancePlayback(o.animation,a.animations,dt))evaluateModelPose(a,o.animation,*o.animationPose);}};
 auto geometry=[&](const Object &a,const Object &b){assert(a.animationPose&&b.animationPose&&a.animationPose!=b.animationPose);for(size_t p=0;p<a.animationPose->geometry.primitives.size();p++)assert(a.animationPose->geometry.primitives[p].vertices==b.animationPose->geometry.primitives[p].vertices);};
 auto roundtrip=[&]{js("snapshotBlend()");for(int i=0;i<3;i++)geometry(host.scene.objects[i],host.scene.objects[i+3]);};
 // At the start the displayed pose is exactly the source, despite a new target time.
 for(int id=1;id<=3;id++){
  const auto &o=*findObject(id);animation::Playback source;static_cast<animation::Cursor&>(source)=o.animation.transition.source;ModelPose reference;evaluateModelPose(host.scene.assets[o.asset-1],source,reference);
  assert(o.animationPose->geometry.primitives[0].vertices==reference.geometry.primitives[0].vertices);
 }
 step(.25);for(int id=1;id<=3;id++){
  const auto &o=*findObject(id);assert(near(o.animation.transition.elapsed,.25)&&near(o.animation.transition.source.time,.45)&&near(o.animation.time,1.05));
  const auto &g=o.animationPose->geometry;
  for(const auto &part:g.primitives)for(size_t v=0;v<part.vertices.size();v+=5)for(int axis=0;axis<3;axis++)assert(std::isfinite(part.vertices[v+axis])&&part.vertices[v+axis]>=g.low[axis]-1e-5&&part.vertices[v+axis]<=g.high[axis]+1e-5);
 }
 std::mt19937 random(14);std::uniform_real_distribution<float> offset(-.7,.7);int hits=0;
 for(int id=1;id<=3;id++)for(int ray=0;ray<300;ray++){
  const auto &o=*findObject(id);ControllerSnapshot hand{};hand.valid=true;hand.x=offset(random);hand.y=offset(random);hand.z=offset(random);for(int axis=0;axis<3;axis++)hand.rotation[axis*3+axis]=1;
  const auto &g=o.animationPose->geometry;const float fast=meshHit(o,g,hand,ray%2),slow=meshHit(o,g,hand,ray%2,false);
  assert((fast==std::numeric_limits<float>::max()&&slow==fast)||near(fast,slow));hits+=fast!=std::numeric_limits<float>::max();
 }
 assert(hits>0);
 roundtrip();js("invalidBlendSnapshots();disposeSceneContainer(blendCopy.containerId)");
 std::vector<std::vector<float>> before;for(int id=1;id<=3;id++)before.push_back(findObject(id)->animationPose->geometry.primitives[0].vertices);
 js("interruptBlends()");
 for(int id=1;id<=3;id++){const auto &o=*findObject(id);assert(o.animation.transition.source.clip==-1&&!o.animation.transition.frozen.empty());assert(o.animationPose->geometry.primitives[0].vertices==before[id-1]);}
 step(.3);for(int id=1;id<=3;id++)assert(findObject(id)->animation.transition.elapsed==0);
 roundtrip();js("invalidFrozenSnapshots();disposeSceneContainer(blendCopy.containerId);for(const id of objects)resumeAnimation(id)");
 step(.8);for(int id=1;id<=3;id++){
  const auto &o=*findObject(id);assert(!o.animation.transition.active());ModelPose reference;evaluateModelPose(host.scene.assets[o.asset-1],o.animation,reference);assert(o.animationPose->geometry.primitives[0].vertices==reference.geometry.primitives[0].vertices);
 }
 js("playAnimation(objects[0],0,{time:1.99,loop:false,blendDuration:.5})");step(.1);assert(findObject(1)->animation.playing&&!findObject(1)->animation.finished);step(.4);assert(!findObject(1)->animation.playing&&findObject(1)->animation.finished);
 js("playAnimation(objects[0],0,{blendDuration:.5});seekAnimation(objects[0],.4);check(getAnimationState(objects[0]).transition===null,'seek cancels fade');playAnimation(objects[0],0,{blendDuration:0});check(getAnimationState(objects[0]).transition===null,'zero fade');stopAnimation(objects[0]);check(getAnimationState(objects[0]).transition===null,'stop cancels fade')");
 // Old v4 snapshots restore with no transition. Their new optional null field is harmless.
 js("const legacy=captureSceneContainer(owner);legacy.version=4;for(const o of legacy.objects)if(o.animation)delete o.animation.transition;const old=restoreSceneContainer(legacy);disposeSceneContainer(old.containerId)");
 js("for(const id of objects){stopAnimation(id);playAnimation(id,0,{blendDuration:.5});check(getAnimationState(id).transition.sourceClip===null,'default source')}");
 for(int id=1;id<=3;id++){
  const auto &o=*findObject(id);const auto &a=host.scene.assets[o.asset-1];
  for(size_t p=0;p<a.primitives.size();p++)for(size_t v=0;v<a.primitives[p].vertices.size();v++)assert(near(a.primitives[p].vertices[v],o.animationPose->geometry.primitives[p].vertices[v]));
 }
 closeScript(host);assert(loadScript(host,"scripts/blending.js"));
 auto button=[&](const char *control,bool down){frame_input::Event e{};e.type="controller.button";e.hand="right";e.control=control;e.component.boolean=down;JSValue v=inputEventValue(host.ctx,e),result=JS_Call(host.ctx,inputCallback,JS_UNDEFINED,1,&v);assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);JS_FreeValue(host.ctx,v);};
 button("/input/bumper/click",true);for(const auto &o:host.scene.objects)assert(o.animation.transition.duration==1);
 button("/input/a/click",true);button("/input/a/click",true);for(const auto &o:host.scene.objects)assert(!o.animation.playing);
 button("/input/a/click",false);button("/input/a/click",true);for(const auto &o:host.scene.objects)assert(o.animation.playing);
 puts("PASS animation transitions: analytic TRS/morph blending, live source clocks, independent rigid/skin/morph poses, interruptions, pause/resume, endpoint completion, validation, v5 roundtrip and v4 compatibility");
}
