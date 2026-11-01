#pragma once

#include <compare>
#include <string>

// JavaScript IDs remain local to a scene. Native resources use both IDs.
struct NativeResourceKey {
  int instanceId;
  int localId;
  auto operator<=>(const NativeResourceKey &) const = default;
};

inline std::string objectOverlayKey(NativeResourceKey key,const std::string &suffix={}){
  return "sf_overlay_engine.instance."+std::to_string(key.instanceId)+".object."+std::to_string(key.localId)+suffix;
}

inline std::string panelOverlayKey(NativeResourceKey key){
  return "sf_overlay_engine.instance."+std::to_string(key.instanceId)+".panel."+std::to_string(key.localId);
}
