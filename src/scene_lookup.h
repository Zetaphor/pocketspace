#pragma once
#include <algorithm>
#include <memory>

// IDs increase on append; stable erasure preserves their order. Resolve afresh
// rather than retaining pointers across vector growth, erasure, or scene reload.
template<class Items>
auto findSceneItem(Items &items,int id) -> decltype(std::addressof(items.front())) {
  const auto it=std::lower_bound(items.begin(),items.end(),id,[](const auto &item,int key){return item.id<key;});
  return it!=items.end()&&it->id==id?std::addressof(*it):nullptr;
}
