#include "native_resource_key.h"
#include <cassert>
#include <map>

int main(){
  const NativeResourceKey first{1,7},second{2,7},group{1,-7};
  std::map<NativeResourceKey,int> resources;
  resources[first]=11;resources[second]=22;resources[group]=33;
  assert(resources.size()==3);
  assert(resources[first]==11&&resources[second]==22&&resources[group]==33);
  assert(objectOverlayKey(first)!=objectOverlayKey(second));
  assert(objectOverlayKey(first)!=panelOverlayKey(first));
  assert(objectOverlayKey(group)+".stage"==objectOverlayKey(group,".stage"));
  assert(objectOverlayKey(group,".resize.1")!=objectOverlayKey(group,".resize.2"));
  resources.erase(first);
  assert(resources.size()==2&&resources.contains(second));
}
