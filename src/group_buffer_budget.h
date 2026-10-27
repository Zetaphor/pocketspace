#pragma once
#include <cstdint>

// Three stereo RGBA8 colors plus one shared stereo depth24 attachment,
// conservatively charged as four bytes per depth texel. Retained generations
// remain charged until destroyed, including both sides of a regroup transaction.
struct GroupBufferBudget {
  static constexpr uint64_t limit=512ull*1024*1024;
  uint64_t used=0,peak=0;
  static constexpr uint64_t bytes(int pixels){return uint64_t(pixels)*pixels*32;}
  bool fits(uint64_t extra)const{return used<=limit&&extra<=limit-used;}
  int fittingPixels(int requested)const{
    for(int pixels=requested;pixels>=256;pixels-=256)if(fits(bytes(pixels)))return pixels;
    return 0;
  }
  int resizePixels(int requested,int current)const{
    // Never trade away extra density just to fit a shrink replacement beside
    // its retained full-quality snapshot. Keep the existing allocation instead.
    if(requested<=current)return fits(bytes(requested))?requested:0;
    const int pixels=fittingPixels(requested);return pixels>current?pixels:0;
  }
  bool reserve(uint64_t extra){if(!fits(extra))return false;used+=extra;if(used>peak)peak=used;return true;}
  void release(uint64_t bytes){used-=bytes;}
};
