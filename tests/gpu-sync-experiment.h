#pragma once
// Test executable only. Both modes complete producer work before submission;
// neither mode establishes any additional compositor-consumer ownership rule.
#include <cstdlib>
#include <ctime>
class GpuSyncExperiment {
  using Clock=std::chrono::steady_clock;
  bool fence=false,timer=false,active=false,pending=false;
  GLuint query=0;GLuint64 fenceWaitNs=100000000;
  PFNGLGETQUERYOBJECTUI64VEXTPROC getResult=nullptr;
  Clock::time_point epoch=Clock::now(),window=epoch,start{},waitEnd{};
  unsigned frames=0,gpuSamples=0,disjointSamples=0,fenceFailures=0;
  double enqueueMs=0,waitMs=0,waitCpuMs=0,submitMs=0,gpuMs=0;
  static double milliseconds(Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();}
  static double threadMilliseconds(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return double(t.tv_sec)*1000+t.tv_nsec/1e6;}
  void poll(){
    if(!timer)return;
    GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);
    if(disjoint){pending=false;++disjointSamples;return;}
    if(!pending)return;
    GLuint available=0;glGetQueryObjectuiv(query,GL_QUERY_RESULT_AVAILABLE,&available);
    if(available){GLuint64 result=0;getResult(query,GL_QUERY_RESULT,&result);gpuMs+=double(result)/1e6;++gpuSamples;pending=false;}
  }
public:
  GpuSyncExperiment(){
    const char *mode=std::getenv("SF_GPU_SYNC_MODE");fence=mode&&std::strcmp(mode,"fence")==0;
    const char *timeout=std::getenv("SF_GPU_SYNC_WAIT_MS");if(timeout&&std::strcmp(timeout,"1")==0)fenceWaitNs=1000000;
    GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
    for(int i=0;i<count;i++)if(std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,i)),"GL_EXT_disjoint_timer_query")==0)timer=true;
    getResult=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));timer=timer&&getResult;
    if(timer){GLint bits=0;glGetQueryiv(GL_TIME_ELAPSED_EXT,GL_QUERY_COUNTER_BITS_EXT,&bits);timer=bits>0;}
    if(timer)glGenQueries(1,&query);
    printf("[gpu-sync] mode=%s timer=%d wait_timeout_ms=%llu producer_completion=required consumer_ownership=unchanged\n",fence?"fence":"finish",int(timer),(unsigned long long)(fenceWaitNs/1000000));
  }
  ~GpuSyncExperiment(){if(active)glEndQuery(GL_TIME_ELAPSED_EXT);if(query)glDeleteQueries(1,&query);}
  void begin(){
    poll();start=Clock::now();
    if(timer&&!pending){glBeginQuery(GL_TIME_ELAPSED_EXT,query);active=true;}
  }
  void complete(){
    if(active){glEndQuery(GL_TIME_ELAPSED_EXT);active=false;pending=true;}
    const auto waitStart=Clock::now();enqueueMs+=milliseconds(waitStart-start);
    const double cpuStart=threadMilliseconds();
    if(fence){
      GLsync sync=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
      GLenum result=GL_WAIT_FAILED;
      if(sync){
        GLbitfield flags=GL_SYNC_FLUSH_COMMANDS_BIT;
        do{result=glClientWaitSync(sync,flags,fenceWaitNs);flags=0;}while(result==GL_TIMEOUT_EXPIRED);
        glDeleteSync(sync);
      }
      if(result!=GL_ALREADY_SIGNALED&&result!=GL_CONDITION_SATISFIED){++fenceFailures;glFinish();}
    }else glFinish();
    waitEnd=Clock::now();waitMs+=milliseconds(waitEnd-waitStart);waitCpuMs+=threadMilliseconds()-cpuStart;
    poll();
  }
  void submitted(){
    const auto now=Clock::now();submitMs+=milliseconds(now-waitEnd);++frames;
    if(now-window<std::chrono::seconds(1))return;
    char gpu[40]="null";if(gpuSamples)std::snprintf(gpu,sizeof(gpu),"%.6f",gpuMs/gpuSamples);
    const auto error=glGetError();
    printf("GPU_SYNC_SAMPLE {\"seconds\":%.6f,\"timeoutMs\":%llu,\"glError\":%u,\"mode\":\"%s\",\"frames\":%u,\"gpuSamples\":%u,\"gpuMs\":%s,\"enqueueMs\":%.6f,\"waitMs\":%.6f,\"waitThreadCpuMs\":%.6f,\"submitMs\":%.6f,\"disjointSamples\":%u,\"fenceFailures\":%u}\n",std::chrono::duration<double>(now-epoch).count(),(unsigned long long)(fenceWaitNs/1000000),unsigned(error),fence?"fence":"finish",frames,gpuSamples,gpu,enqueueMs/frames,waitMs/frames,waitCpuMs/frames,submitMs/frames,disjointSamples,fenceFailures);fflush(stdout);
    window=now;frames=gpuSamples=disjointSamples=fenceFailures=0;enqueueMs=waitMs=waitCpuMs=submitMs=gpuMs=0;
  }
};
