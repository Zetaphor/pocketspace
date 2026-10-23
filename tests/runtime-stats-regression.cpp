#include "runtime_stats.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok){if(!ok){std::fputs("FAIL runtime stats\n",stderr);std::exit(1);}}
int main(){
  std::string stat="123 (engine ) worker) R";
  for(int field=4;field<=52;field++)stat+=" "+std::to_string(field==14?123:field==15?45:0);
  check(diagnostics::cpuTicks(stat)==168);check(!diagnostics::cpuTicks("invalid"));check(!diagnostics::cpuTicks("1 (a) R 0"));
  diagnostics::Sampler sampler;sampler.prime();check(!sampler.fps&&sampler.sampleId==0);
  check(!sampler.update(90,sampler.previous+std::chrono::milliseconds(500)));
  check(sampler.update(180,sampler.previous+std::chrono::seconds(2)));
  check(sampler.fps&&*sampler.fps==90&&sampler.cpuPercent&&*sampler.cpuPercent>=0&&sampler.ramBytes&&*sampler.ramBytes>0);
  check(sampler.sampleId==1&&sampler.sampleSeconds==2);
  std::puts("PASS runtime stats: process stat parsing, sampling cadence, elapsed FPS, CPU and resident memory");
}
