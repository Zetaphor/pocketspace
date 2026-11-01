#pragma once

#include <map>
#include <memory>
#include "native_resource_key.h"

// Each instance owns a separate local-ID namespace and releases only its own
// OpenVR/GPU resources. The caller supplies the platform-specific destructors.
template<class Surface,class PanelHandle,class PanelSubmission>
struct NativeSceneResources {
  explicit NativeSceneResources(int id):instanceId(id){}
  int instanceId;
  std::map<int,std::unique_ptr<Surface>> surfaces;
  std::map<int,PanelHandle> panelOverlays;
  std::map<int,PanelSubmission> panelSubmissions;
  NativeResourceKey key(int localId)const{return {instanceId,localId};}
  template<class DestroySurface,class DestroyPanel>
  void release(DestroySurface destroySurface,DestroyPanel destroyPanel){
    for(auto &[id,surface]:surfaces)destroySurface(*surface);
    surfaces.clear();
    for(auto &[id,handle]:panelOverlays)destroyPanel(handle);
    panelOverlays.clear();panelSubmissions.clear();
  }
};
