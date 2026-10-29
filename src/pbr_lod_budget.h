#pragma once
#include <cassert>
#include <cstdint>

// Optional reduced PBR geometry only. Retained full meshes, textures and overlay
// surfaces have separate accounting. Charge both halves of an unpublished pair.
struct PbrLodBudget {
  static constexpr uint64_t limit=32ull*1024*1024;
  uint64_t used=0;
  bool reserve(uint64_t bytes){if(used>limit||bytes>limit-used)return false;used+=bytes;return true;}
  void release(uint64_t bytes){assert(bytes<=used);used-=bytes;}
};
inline PbrLodBudget pbrLodBudget;
