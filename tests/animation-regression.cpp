#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <random>
static bool approximately(float a,float b){return std::abs(a-b)<2e-5;}
int main(){
 ModelAsset asset;assert(loadModelFile("assets/samples/InterpolationTest/InterpolationTest.glb",asset));
 assert(asset.animations.size()==9);for(const auto &clip:asset.animations)assert(clip.supported()&&clip.duration==2);
 // No animation channels must reproduce the old cgltf-baked bind geometry.
 asset.animations.push_back({});animation::Playback state;state.clip=int(asset.animations.size()-1);ModelPose bind;
 evaluateModelPose(asset,state,bind);
 for(size_t p=0;p<asset.primitives.size();p++)for(size_t v=0;v<asset.primitives[p].vertices.size();v++)assert(approximately(bind.geometry.primitives[p].vertices[v],asset.primitives[p].vertices[v]));
 asset.animations.pop_back();
 Object object{};object.id=1;object.scale=.4;object.yaw=.2;object.pitch=.1;
 std::mt19937 random(7);std::uniform_real_distribution<float> position(-1.5,1.5);
 ModelPose pose;int hits=0;
 for(int clip=0;clip<9;clip++)for(double time:{0.,.25,.75,1.,1.5,2.}){
  state.clip=clip;state.time=time;evaluateModelPose(asset,state,pose);
  assert(std::isfinite(pose.radius)&&pose.radius>0);
  for(const auto &part:pose.geometry.primitives)for(size_t v=0;v<part.vertices.size();v+=5){
   float radius=0;for(int axis=0;axis<3;axis++){assert(std::isfinite(part.vertices[v+axis]));assert(part.vertices[v+axis]>=pose.geometry.low[axis]-1e-6&&part.vertices[v+axis]<=pose.geometry.high[axis]+1e-6);radius+=part.vertices[v+axis]*part.vertices[v+axis];}
   assert(std::sqrt(radius)<=pose.radius+1e-5);
  }
  // BVH refit must agree with the actual transformed-ray triangle scan.
  for(int ray=0;ray<100;ray++){
   ControllerSnapshot hand;hand.valid=true;hand.x=position(random);hand.y=position(random);hand.z=position(random);for(int i=0;i<3;i++)hand.rotation[i*3+i]=1;
   const float fast=meshHit(object,pose.geometry,hand,ray%2),slow=meshHit(object,pose.geometry,hand,ray%2,false);
   assert((fast==std::numeric_limits<float>::max()&&slow==fast)||approximately(fast,slow));if(fast!=std::numeric_limits<float>::max())hits++;
  }
 }
 assert(hits>0);
 // Different object poses must not change the asset or each other's geometry.
 state.clip=8;state.time=0;ModelPose one;evaluateModelPose(asset,state,one);auto snapshot=one.geometry.primitives[0].vertices;
 state.time=.5;ModelPose two;evaluateModelPose(asset,state,two);assert(one.geometry.primitives[0].vertices==snapshot);
 bool moved=false;for(size_t p=0;p<one.geometry.primitives.size();p++)moved|=one.geometry.primitives[p].vertices!=two.geometry.primitives[p].vertices;assert(moved);
 ScriptHost host;ScopeExit cleanup{[&]{closeScript(host);currentScene=liveScene=nullptr;}};
 assert(loadScript(host,"tests/animation-api-smoke.js"));
 for(auto &entry:host.scene.objects)if(entry.animationPose)assert(entry.animationPose->geometry.primitives[0].vbo==0);
 closeScript(host);
 assert(inputCallbackCtx==nullptr);
 puts("PASS animation integration: bind pose, nine clips and endpoints, finite bounds, 5400 BVH/triangle comparisons, independent geometry, actual QuickJS API and removal cleanup");
}
