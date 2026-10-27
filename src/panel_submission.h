#pragma once
#include "panel.h"
#include <openvr.h>
#include <cstring>

struct PanelSubmission {
  vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
  vr::Texture_t descriptor{};
  GLuint texture=0;
  uint64_t revision=0;
  vr::HmdMatrix34_t transform{};
  bool hasTexture=false,hasTransform=false,shown=false;
  void forHandle(vr::VROverlayHandle_t next){if(handle!=next){*this={};handle=next;}}
};

template<class Overlays>
vr::EVROverlayError hidePanel(Overlays &overlays,vr::VROverlayHandle_t handle,PanelSubmission &cache){
  cache.forHandle(handle);
  if(!cache.shown)return vr::VROverlayError_None;
  const auto error=overlays.HideOverlay(handle);
  if(error==vr::VROverlayError_None)cache.shown=false;
  return error;
}

template<class Overlays>
vr::EVROverlayError submitPanel(Overlays &overlays,vr::VROverlayHandle_t handle,const Panel &panel,const vr::HmdMatrix34_t &transform,PanelSubmission &cache){
  cache.forHandle(handle);
  if(!panel.visible||!panel.texture)return hidePanel(overlays,handle,cache);
  if(!cache.hasTexture||cache.texture!=panel.texture||cache.revision!=panel.textureRevision){
    cache.descriptor={reinterpret_cast<void*>(static_cast<uintptr_t>(panel.texture)),vr::TextureType_OpenGL,vr::ColorSpace_Gamma};
    const auto error=overlays.SetOverlayTexture(handle,&cache.descriptor);
    if(error!=vr::VROverlayError_None)return error;
    cache.hasTexture=true;cache.texture=panel.texture;cache.revision=panel.textureRevision;
  }
  if(!cache.hasTransform||std::memcmp(cache.transform.m,transform.m,sizeof(transform.m))!=0){
    const auto error=overlays.SetOverlayTransformAbsolute(handle,vr::TrackingUniverseStanding,&transform);
    if(error!=vr::VROverlayError_None)return error;
    cache.hasTransform=true;cache.transform=transform;
  }
  if(!cache.shown){
    const auto error=overlays.ShowOverlay(handle);
    if(error!=vr::VROverlayError_None)return error;
    cache.shown=true;
  }
  return vr::VROverlayError_None;
}
