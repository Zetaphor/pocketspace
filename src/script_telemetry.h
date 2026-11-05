#pragma once
// Per-script measurements: QuickJS heap bytes and callback time.
#include <quickjs.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <optional>

namespace telemetry {
// Bytes currently allocated by one QuickJS runtime, kept by its allocator so reading
// it costs nothing. JS_ComputeMemoryUsage walks the whole heap instead: about 1 ms
// per 100,000 objects on Steam Frame. Runtimes run only on the host thread.
struct HeapCounter {uint64_t bytes=0;};
inline void count(void *opaque,void *pointer,int64_t sign){
  if(pointer)static_cast<HeapCounter*>(opaque)->bytes+=uint64_t(sign*int64_t(malloc_usable_size(pointer)));
}
inline void *heapCalloc(void *opaque,size_t n,size_t size){void *p=calloc(n,size);count(opaque,p,1);return p;}
inline void *heapMalloc(void *opaque,size_t size){void *p=malloc(size);count(opaque,p,1);return p;}
inline void heapFree(void *opaque,void *pointer){count(opaque,pointer,-1);free(pointer);}
inline void *heapRealloc(void *opaque,void *pointer,size_t size){
  if(!size){heapFree(opaque,pointer);return nullptr;}
  const size_t before=pointer?malloc_usable_size(pointer):0;
  void *moved=realloc(pointer,size);
  if(moved)static_cast<HeapCounter*>(opaque)->bytes+=malloc_usable_size(moved)-before;
  return moved;
}
inline size_t heapUsable(const void *pointer){return malloc_usable_size(const_cast<void*>(pointer));}
// The counter must outlive the runtime.
inline JSRuntime *newCountedRuntime(HeapCounter &counter){
  static const JSMallocFunctions functions{heapCalloc,heapMalloc,heapFree,heapRealloc,heapUsable};
  return JS_NewRuntime2(&functions,&counter);
}

// Callback time of one script: update, input and bus callbacks. Each frame's total
// joins a window that a sample closes, giving milliseconds per second of wall time
// and the slowest frame. Values stay unset until the first sample after a start.
struct CallbackTimer {
  using Clock=std::chrono::steady_clock;
  double frameSeconds=0,windowSeconds=0,worstSeconds=0;
  Clock::time_point windowStart=Clock::now();
  std::optional<double> msPerSecond,worstFrameMs;
  void add(double seconds){frameSeconds+=seconds;}
  void endFrame(){windowSeconds+=frameSeconds;worstSeconds=std::max(worstSeconds,frameSeconds);frameSeconds=0;}
  void sample(Clock::time_point now=Clock::now()){
    const double elapsed=std::chrono::duration<double>(now-windowStart).count();
    if(elapsed>0){msPerSecond=windowSeconds*1000/elapsed;worstFrameMs=worstSeconds*1000;}
    windowSeconds=worstSeconds=0;windowStart=now;
  }
  // A start or restart begins a new window with no values.
  void clear(Clock::time_point now=Clock::now()){*this={};windowStart=now;}
};
// Adds the time until it goes out of scope.
class Timed {
  CallbackTimer &timer_;CallbackTimer::Clock::time_point start_=CallbackTimer::Clock::now();
public:
  explicit Timed(CallbackTimer &timer):timer_(timer){}
  Timed(const Timed&)=delete;Timed &operator=(const Timed&)=delete;
  ~Timed(){timer_.add(std::chrono::duration<double>(CallbackTimer::Clock::now()-start_).count());}
};
}
