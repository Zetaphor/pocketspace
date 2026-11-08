#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>

namespace diagnostics {
inline std::optional<uint64_t> cpuTicks(const std::string &line){
  const auto end=line.rfind(')');if(end==std::string::npos)return {};
  std::istringstream input(line.substr(end+1));std::string field;uint64_t ticks=0;
  for(int index=0;index<=12;index++){
    if(!(input>>field))return {};
    if(index==11||index==12){try{ticks+=std::stoull(field);}catch(...){return {};}}
  }
  return ticks;
}
inline std::optional<uint64_t> residentBytes(){
  std::ifstream input("/proc/self/status");std::string line;
  while(std::getline(input,line))if(line.starts_with("VmRSS:")){std::istringstream value(line.substr(6));uint64_t kb;if(value>>kb)return kb*1024;}
  return {};
}
// One DRM fdinfo memory value: an integer with an optional KiB or MiB unit.
inline std::optional<uint64_t> drmMemoryBytes(const std::string &text){
  std::istringstream value(text);uint64_t bytes;std::string unit;
  if(!(value>>bytes))return std::nullopt;
  if(!(value>>unit))return bytes;
  if(unit=="KiB")return bytes*1024;
  if(unit=="MiB")return bytes*1024*1024;
  return std::nullopt;
}
// The driver's total for one DRM client file, from `drm-total-memory`, the key
// for drivers with a single memory region such as msm. Region-specific counters
// may overlap; do not invent a total by adding them together.
struct DrmClientMemory {std::string client;std::optional<uint64_t> bytes;};
inline DrmClientMemory drmClientMemory(std::istream &input){
  DrmClientMemory result;std::string line,device;
  while(std::getline(input,line)){
    if(line.starts_with("drm-client-id:"))result.client=line.substr(14);
    if(line.starts_with("drm-pdev:"))device=line.substr(9);
    if(line.starts_with("drm-total-memory:"))result.bytes=drmMemoryBytes(line.substr(17));
  }
  if(!result.client.empty())result.client=device+":"+result.client;
  return result;
}
// Count each DRM client once, however many descriptors share it.
inline std::optional<uint64_t> gpuBytes(){
  uint64_t total=0;bool available=false;std::set<std::string> clients;std::error_code error;
  for(const auto &entry:std::filesystem::directory_iterator("/proc/self/fdinfo",error)){
    std::ifstream input(entry.path());const auto memory=drmClientMemory(input);
    if(memory.bytes&&!memory.client.empty()&&clients.insert(memory.client).second){total+=*memory.bytes;available=true;}
  }
  return available?std::optional<uint64_t>(total):std::nullopt;
}
struct Sampler {
  using Clock=std::chrono::steady_clock;
  Clock::time_point previous=Clock::now();uint64_t previousFrames=0;std::optional<uint64_t> previousCpu;
  std::optional<double> fps,cpuPercent;std::optional<uint64_t> ramBytes,vramBytes;
  uint64_t gpuAllocatedEstimateBytes=0,sharedGpuEstimateBytes=0,sampleId=0;double sampleSeconds=0;
  bool update(uint64_t frames,Clock::time_point now=Clock::now()){
    const double seconds=std::chrono::duration<double>(now-previous).count();if(seconds<1)return false;
    std::ifstream stat("/proc/self/stat");std::string line;std::getline(stat,line);const auto ticks=cpuTicks(line);const long rate=sysconf(_SC_CLK_TCK);
    if(ticks&&previousCpu&&rate>0&&*ticks>=*previousCpu)cpuPercent=100.*double(*ticks-*previousCpu)/double(rate)/seconds;
    else cpuPercent.reset();
    fps=double(frames-previousFrames)/seconds;ramBytes=residentBytes();vramBytes=gpuBytes();
    previous=now;previousFrames=frames;previousCpu=ticks;sampleSeconds=seconds;++sampleId;return true;
  }
  void prime(){std::ifstream stat("/proc/self/stat");std::string line;std::getline(stat,line);previousCpu=cpuTicks(line);previous=Clock::now();previousFrames=0;}
};
}
