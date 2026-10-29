#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <random>
static bool approximately(float a,float b){return std::abs(a-b)<2e-5;}
static void testSkins(const char *fixtures){
 ModelAsset man;assert(loadModelFile("assets/samples/CesiumMan/CesiumMan.glb",man));
 assert(man.skins.size()==1&&man.skins[0].joints.size()==19&&man.animations[0].supported());
 animation::Playback state;state.clip=0;ModelPose pose;std::vector<float> first;bool moved=false;int hits=0;
 Object object{};object.scale=.5;
 std::mt19937 random(9);std::uniform_real_distribution<float> position(-.7,.7);
 for(double time:{0.,.3,.7,1.2}){
  state.time=time;evaluateModelPose(man,state,pose);
  const auto &vertices=pose.geometry.primitives[0].vertices;
  if(first.empty())first=vertices;else moved|=first!=vertices;
  for(size_t v=0;v<vertices.size();v+=5)for(int axis=0;axis<3;axis++)assert(std::isfinite(vertices[v+axis])&&vertices[v+axis]>=pose.geometry.low[axis]-1e-5&&vertices[v+axis]<=pose.geometry.high[axis]+1e-5);
  for(int ray=0;ray<400;ray++){
   ControllerSnapshot hand;hand.valid=true;hand.x=position(random);hand.y=position(random);hand.z=position(random);for(int axis=0;axis<3;axis++)hand.rotation[axis*3+axis]=1;
   const float fast=meshHit(object,pose.geometry,hand,ray%2),slow=meshHit(object,pose.geometry,hand,ray%2,false);
   assert((fast==std::numeric_limits<float>::max()&&slow==fast)||approximately(fast,slow));hits+=fast!=std::numeric_limits<float>::max();
  }
 }
 assert(moved&&hits>0);
 state.time=.2;ModelPose independent;evaluateModelPose(man,state,independent);auto snapshot=independent.geometry.primitives[0].vertices;
 state.time=.8;evaluateModelPose(man,state,pose);assert(independent.geometry.primitives[0].vertices==snapshot);
 if(!fixtures)return;
 for(const char *name:{"two-sets","identity","no-animation"}){
  ModelAsset asset;assert(loadModelFile(std::string(fixtures)+"/"+name+".glb",asset));
  const float weight=191.f/255;
  assert(asset.skins.size()==1&&asset.primitives[0].influences.size()==2);
  // The +100 mesh-node translation is intentionally ignored. Packed weights
  // are normalized together across both sets, not separately within each set.
  const bool identity=std::string(name)=="identity";
  assert(approximately(asset.center[0],identity?.5f+weight:.5f));
  assert(approximately(asset.center[1],identity?1.5f:.5f));
  asset.animations.push_back({});state.clip=int(asset.animations.size()-1);state.time=0;
  ModelPose synthetic;
  evaluateModelPose(asset,state,synthetic);
  assert(approximately(synthetic.geometry.primitives[0].vertices[0],-.5));
  if(std::string(name)!="no-animation"){
   state.clip=0;state.time=1;evaluateModelPose(asset,state,synthetic);
   assert(approximately(synthetic.geometry.primitives[0].vertices[0],-.5f+2*weight));
  }else assert(asset.animations.size()==1); // only our synthetic bind-pose clip
 }
 for(const char *name:{"missing-pair","bad-joint","float-joint","zero-weights","negative-weight","bad-inverse"}){
  ModelAsset invalid;assert(!loadModelFile(std::string(fixtures)+"/"+name+".glb",invalid));
 }
 puts("PASS skin integration: CesiumMan deformation, 1600 animated picking comparisons, independent poses, multi-set packed weights, inverse binds, ignored mesh transform, static skins and malformed rigs");
}
int main(int argc,char **argv){
 rigidPoseCachingEnabled=true;

 gpuSkinningEnabled=false; // Retained CPU reference path.

 testSkins(argc>1?argv[1]:nullptr);

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
