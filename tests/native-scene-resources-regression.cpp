#include "native_scene_resources.h"
#include <cassert>
#include <set>

struct Surface {int instanceId;int localId;};
int main(){
  NativeSceneResources<Surface,int,int> left(1),right(2);
  left.surfaces.emplace(7,std::make_unique<Surface>(Surface{1,7}));
  right.surfaces.emplace(7,std::make_unique<Surface>(Surface{2,7}));
  left.panelOverlays[7]=107;right.panelOverlays[7]=207;
  left.panelSubmissions[7]=1;right.panelSubmissions[7]=2;
  assert(objectOverlayKey(left.key(7))!=objectOverlayKey(right.key(7)));
  assert(panelOverlayKey(left.key(7))!=panelOverlayKey(right.key(7)));
  std::set<int> releasedSurfaces,releasedPanels;
  left.release([&](const Surface &surface){releasedSurfaces.insert(surface.instanceId);},[&](int handle){releasedPanels.insert(handle);});
  assert(releasedSurfaces==std::set<int>{1}&&releasedPanels==std::set<int>{107});
  assert(left.surfaces.empty()&&left.panelOverlays.empty()&&left.panelSubmissions.empty());
  assert(right.surfaces.size()==1&&right.panelOverlays.at(7)==207&&right.panelSubmissions.at(7)==2);
  right.release([&](const Surface &surface){releasedSurfaces.insert(surface.instanceId);},[&](int handle){releasedPanels.insert(handle);});
  assert(releasedSurfaces==std::set<int>({1,2})&&releasedPanels==std::set<int>({107,207}));
}
