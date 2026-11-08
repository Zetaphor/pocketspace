#include "runtime_stats.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
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
  // msm fdinfo: the total may have no unit, and other drm-* keys must not count.
  check(diagnostics::drmMemoryBytes(" 0")==0&&diagnostics::drmMemoryBytes("\t12 KiB")==12288&&diagnostics::drmMemoryBytes(" 3 MiB")==3145728);
  check(!diagnostics::drmMemoryBytes(" 3 GiB")&&!diagnostics::drmMemoryBytes(" n/a"));
  {std::istringstream fdinfo("pos:\t0\ndrm-driver:\tmsm\ndrm-client-id:\t271\ndrm-engine-gpu:\t5 ns\ndrm-total-memory:\t2048 KiB\ndrm-shared-memory:\t1024 KiB\ndrm-resident-memory:\t2048 KiB\n");
   const auto memory=diagnostics::drmClientMemory(fdinfo);check(memory.client==":\t271"&&memory.bytes==2097152);}
  {std::istringstream legacy("drm-client-id:\t3\ndrm-memory-total:\t9 KiB\n");check(!diagnostics::drmClientMemory(legacy).bytes);}
  std::puts("PASS runtime stats: process stat parsing, sampling cadence, elapsed FPS, CPU, resident memory and DRM client memory");
}
