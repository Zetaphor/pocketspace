#define SF_GROUP_OCCLUDER_EXPERIMENT
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <random>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
 std::mt19937 random(916);std::uniform_real_distribution<float> xy(-.5f,.5f),depth(-.2f,.2f);
 size_t compared=0,improved=0;
 for(int trial=0;trial<256;trial++){
  Scene scene;
  for(int asset=0;asset<2;asset++){
   scene.assets.emplace_back();auto &model=scene.assets.back();model.eyePixels=768;
   for(int axis=0;axis<3;axis++){model.low[axis]=-.5f;model.high[axis]=.5f;}
   model.primitives.resize(1);model.primitives[0].vertexCount=asset?300000:36;
  }
  for(int n=0;n<24;n++){
   Object object{};object.id=n+1;object.asset=n%3?1:2;object.x=xy(random);object.y=xy(random);object.z=depth(random);object.scale=.06f;object.renderGroup=n<16?1:2;
   scene.objects.push_back(object);
  }
  vr::HmdMatrix34_t anchor{},head{};for(int axis=0;axis<3;axis++)anchor.m[axis][axis]=head.m[axis][axis]=1;head.m[2][3]=1.2f;
  const float eyes[2][3]={{-.032f,0,1.2f},{.032f,0,1.2f}};FrameBoundsCache cache;cache.update(scene,anchor,head,eyes);
  std::vector<const Object*> members;for(int n=0;n<16;n++)members.push_back(&scene.objects[n]);
  groupOccluderTieBreak=false;const auto baseline=partitionGroup(members,scene,anchor,head,eyes,cache,4096,4096*4096);
  groupOccluderTieBreak=true;const auto candidate=partitionGroup(members,scene,anchor,head,eyes,cache,4096,4096*4096);
  CHECK(bool(candidate)==bool(baseline));if(!baseline)continue;
  CHECK(candidate.childPixelsSquared==baseline.childPixelsSquared&&candidate.parentPixelsSquared==baseline.parentPixelsSquared);
  auto score=[&](const GroupSplitPlan &plan){
   GroupLayout layouts[2];for(int tile=0;tile<2;tile++){layouts[tile]=tileLayout(plan.members[tile],scene,anchor,head,eyes,cache,4096);CHECK(!layouts[tile].limited);}
   return groupOccluderPredictedTriangles(plan,layouts,scene,cache);
  };
  CHECK(score(candidate)<=score(baseline));improved+=score(candidate)<score(baseline);++compared;
  std::set<int> ids;for(const auto &tile:candidate.members)for(const auto *object:tile)CHECK(ids.insert(object->id).second);CHECK(ids.size()==16);
  for(int repeat=0;repeat<3;repeat++){
   const auto again=partitionGroup(members,scene,anchor,head,eyes,cache,4096,4096*4096);
   for(int tile=0;tile<2;tile++)CHECK(candidate.members[tile]==again.members[tile]);
  }
  scene.objects[0].visible=false;cache.update(scene,anchor,head,eyes);CHECK(!partitionGroup(members,scene,anchor,head,eyes,cache,4096,4096*4096));
 }
 CHECK(compared&&improved);
 printf("PASS equal-allocation occluder tie-break: %zu deterministic plans, %zu strictly lower predicted work, equal minimum allocated area, uncapped density, complete IDs and hidden-member fallback\n",compared,improved);
}
