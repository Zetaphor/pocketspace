#include "scene_lookup.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(condition) do{if(!(condition)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);std::exit(1);}}while(0)
struct Item {int id;};
int main(){
  std::vector<Item> items;
  CHECK(!findSceneItem(items,1));
  for(int id=1;id<=512;id+=2)items.push_back({id});
  for(int id=-1;id<=514;id++){
    const bool exists=id>0&&id<=511&&id%2;
    auto *item=findSceneItem(items,id);CHECK(bool(item)==exists);if(item)CHECK(item->id==id);
  }
  items.erase(items.begin()+128);CHECK(!findSceneItem(items,257));CHECK(findSceneItem(items,259));
  items.erase(items.begin());items.erase(items.end()-1);
  CHECK(!findSceneItem(items,1)&&!findSceneItem(items,511));
  for(int id=513;id<2000;id+=2)items.push_back({id});
  const auto &view=items;CHECK(findSceneItem(view,1999)->id==1999);CHECK(findSceneItem(items,259)->id==259);
  auto replacement=std::move(items);items={{1},{3}};
  CHECK(!findSceneItem(items,259));CHECK(findSceneItem(replacement,259));CHECK(findSceneItem(items,1));
  puts("PASS scene lookup: missing IDs, gaps, erasure, growth, const access, and scene replacement");
}
