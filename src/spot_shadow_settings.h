#pragma once
#include <array>
struct SpotShadowSettings {
 bool enabled=false;
 int resolution=512,maxTriangles=500000;
 float nearPlane=.05f,bias=.0005f,normalBias=.002f,maxGpuMs=2;
 // Directional coverage is a fixed world-space box in the resolved light basis.
 std::array<float,3> center{0,0,0};float size=2,depth=4;
 bool operator==(const SpotShadowSettings &)const=default;
};

#include <array>
#include <cstdint>
#include <optional>
struct SpotShadowFrame {
 uint32_t texture=0;int lightId=0,resolution=0;bool active=false;
 float bias=0,normalBias=0;bool point=false;
 std::array<float,3> position{};std::array<std::array<float,16>,6> faces{};
 std::array<float,16> matrix{};
};
struct SpotShadowDiagnostics {
 bool active=false;int lightId=0,resolution=0;uint64_t triangles=0,bytes=0;
 std::optional<double> gpuMs;const char *status="disabled";
};
inline SpotShadowDiagnostics spotShadowDiagnostics;

// Average a full window of valid GPU results; a scheduling spike alone must not
// permanently remove shadows. Disjoint results never enter this accumulator.
struct SpotShadowGpuBudget {
 static constexpr unsigned window=24;
 std::array<double,window> samples{};
 unsigned count=0,next=0;double sum=0;
 void reset(){*this={};}
 bool add(double ms,double limit){
  sum-=samples[next];samples[next]=ms;sum+=ms;next=(next+1)%window;
  if(count<window)++count;
  return count==window&&sum/window>limit;
 }
};
