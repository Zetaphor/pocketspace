#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static vr::HmdMatrix34_t identity(){vr::HmdMatrix34_t m{};for(int axis=0;axis<3;axis++)m.m[axis][axis]=1;return m;}
static void planner(){
  Scene scene;ModelAsset asset;asset.eyePixels=768;for(int axis=0;axis<3;axis++){asset.low[axis]=-.5f;asset.high[axis]=.5f;}scene.assets.push_back(std::move(asset));
  for(int n=0;n<16;n++){scene.objects.push_back({n+1,1,(n<8?-.65f:.65f)+(n%2)*.025f,(n%4)*.025f,0,.1f,0});scene.objects.back().renderGroup=1;}
  auto anchor=identity(),head=identity();head.m[2][3]=1.2f;
  float eyes[2][3]={{-.032f,0,1.2f},{.032f,0,1.2f}};FrameBoundsCache cache;cache.update(scene,anchor,head,eyes);
  std::vector<const Object*> members;for(const auto &object:scene.objects)members.push_back(&object);
  RenderGroupSplitPolicy policy;policy.targetEyePixels=512;
  auto plan=planGroupSplit(members,scene,anchor,head,eyes,cache,4096,2048,policy);CHECK(plan);
  CHECK(plan.members[0].size()==8&&plan.members[1].size()==8&&plan.childPixelsSquared<plan.parentPixelsSquared*.8);
  std::set<int> ids;for(const auto &tile:plan.members)for(const auto *object:tile)CHECK(ids.insert(object->id).second);CHECK(ids.size()==scene.objects.size());
  const auto original=ids;
  auto a=groupPlane(plan.members[0],scene,anchor,head,eyes,&cache),b=groupPlane(plan.members[1],scene,anchor,head,eyes,&cache);
  float distance=0;for(int axis=0;axis<3;axis++){CHECK(a.forward[axis]==b.forward[axis]);distance+=(a.transform.m[axis][3]-b.transform.m[axis][3])*a.forward[axis];}CHECK(std::abs(distance)<1e-5f);
  for(const auto *object:plan.members[1])scene.objects[object-scene.objects.data()].renderTile=1;
  for(const auto &object:scene.objects)CHECK(surfaceRenderGroup(objectSurfaceKey(object))==1);
  CHECK(objectSurfaceKey(scene.objects[0])!=objectSurfaceKey(scene.objects[15]));
  // Repeated observations do not change the plan; stable tie-breaking uses IDs.
  for(int n=0;n<10;n++){auto again=planGroupSplit(members,scene,anchor,head,eyes,cache,4096,2048,policy);CHECK(again);for(size_t i=0;i<8;i++)CHECK(plan.members[0][i]->id==again.members[0][i]->id);}
  CHECK(!planGroupSplit({members[0]},scene,anchor,head,eyes,cache,4096,2048,policy));
  policy.targetEyePixels=4096;CHECK(!planGroupSplit(members,scene,anchor,head,eyes,cache,4096,2048,policy));policy.targetEyePixels=512;
  for(auto &object:scene.objects)object.x=0;cache.update(scene,anchor,head,eyes);CHECK(!planGroupSplit(members,scene,anchor,head,eyes,cache,4096,2048,policy));
  for(auto &object:scene.objects)object.z=-1.2f;cache.update(scene,anchor,head,eyes);CHECK(!planGroupSplit(members,scene,anchor,head,eyes,cache,4096,2048,policy));
  for(int group:{1,256})for(int tile:{0,1,15,255}){Object object{123,1,0,0,0,1,0};object.renderGroup=group;object.renderTile=tile;CHECK(surfaceRenderGroup(objectSurfaceKey(object))==group);}
  printf("PASS split planner: stable complete partitions, common plane, >=20%% allocated-texel savings, threshold, compact/single-member and near-plane fallbacks\n");
}
static void bindings(){
  ScriptHost host;CHECK(loadScript(host,"scripts/grouped-stress.js"));
  auto eval=[&](const char *source){auto result=JS_Eval(host.ctx,source,std::strlen(source),"split-bindings",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"%s\n",message?message:"exception");if(message)JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}CHECK(!JS_IsException(result));JS_FreeValue(host.ctx,result);};
  eval(R"(
    for (const args of [[0], [257], [1.5], ['1'], [NaN], [1,true], [1,null], [1,{maxSurfaces:0}], [1,{maxSurfaces:17}], [1,{maxSurfaces:1.5}], [1,{targetEyePixels:255}], [1,{targetEyePixels:4097}], [1,{targetEyePixels:300}], [1,{targetEyePixels:NaN}]]) {
      let failed=false;try { setRenderGroupAutoSplit(...args); } catch(e) {failed=true;}if(!failed)throw Error('invalid options accepted');
    }
    setRenderGroupAutoSplit(256);setRenderGroupAutoSplit(2,{maxSurfaces:1,targetEyePixels:256});
  )");
  CHECK(host.scene.renderGroupSplits.at(256).maxSurfaces==4&&host.scene.renderGroupSplits.at(256).targetEyePixels==2048);
  CHECK(host.scene.renderGroupSplits.at(2).maxSurfaces==1);
  eval("setRenderGroupAutoSplit(256,false)");CHECK(!host.scene.renderGroupSplits.count(256));
  // Newly spawned members join the nearest existing tile and reserve its key.
  for(auto &object:host.scene.objects)if(object.renderGroup==1)object.renderTile=3;
  int reserved=0,released=0;reserveObjectOverlay=[&](int key){reserved=key;return std::string{};};releaseObjectOverlay=[&](int key){released=key;};
  eval("var spawned=createObject(assets[0],-.4,0,0,.1,{renderGroup:1});setObjectPhysics(spawned,{mass:.2});enableObjectInteraction(spawned,{grabButton:'both'});");
  auto &spawned=host.scene.objects.back();CHECK(spawned.renderTile==3&&reserved==-(1+256*3));CHECK(host.scene.physics->contains(spawned.id));
  const int count=host.scene.objects.size();eval("setRenderGroupAutoSplit(1,false)");CHECK(host.scene.objects.size()==size_t(count)&&host.scene.objects.back().renderTile==3);
  eval("removeObject(spawned)");CHECK(!released&&host.scene.objects.size()==size_t(count-1));
  eval("var farMember=createObject(assets[0],1e30,1e30,1e30,.1,{renderGroup:1});");CHECK(host.scene.objects.back().renderTile==3);eval("removeObject(farMember)");
  reserveObjectOverlay={};releaseObjectOverlay={};closeScript(host);currentScene=liveScene=nullptr;
  puts("PASS split API: strict validation, defaults, disable without merging, nearest-tile reservation, stable physics/grab IDs and removal");
}
int main(){planner();bindings();}
