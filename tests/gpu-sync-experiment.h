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
  bool passes=false,capturePasses=false;
  struct PassQuery {GLuint id=0;int category=0;uint64_t draws=0,triangles=0;double cpuMs=0;};
  std::vector<PassQuery> passQueries;
  size_t passCount=0;
  double passCpuStart=0;
  Clock::time_point nextPassFrame=epoch;
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
    const char *profile=std::getenv("SF_GPU_PROFILE_PASSES");passes=profile&&std::strcmp(profile,"1")==0;
    const char *timeout=std::getenv("SF_GPU_SYNC_WAIT_MS");if(timeout&&std::strcmp(timeout,"1")==0)fenceWaitNs=1000000;
    GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
    for(int i=0;i<count;i++)if(std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS,i)),"GL_EXT_disjoint_timer_query")==0)timer=true;
    getResult=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));timer=timer&&getResult;
    if(timer){GLint bits=0;glGetQueryiv(GL_TIME_ELAPSED_EXT,GL_QUERY_COUNTER_BITS_EXT,&bits);timer=bits>0;}
    if(timer)glGenQueries(1,&query);
    printf("[gpu-sync] mode=%s timer=%d wait_timeout_ms=%llu producer_completion=required consumer_ownership=unchanged\n",fence?"fence":"finish",int(timer),(unsigned long long)(fenceWaitNs/1000000));
  }
  ~GpuSyncExperiment(){if(active)glEndQuery(GL_TIME_ELAPSED_EXT);if(query)glDeleteQueries(1,&query);for(auto &p:passQueries)glDeleteQueries(1,&p.id);}
  void begin(){
    poll();start=Clock::now();
    capturePasses=passes&&timer&&start>=nextPassFrame;passCount=0;
    if(capturePasses)nextPassFrame=start+std::chrono::seconds(1);
    if(timer&&!pending&&!passes){glBeginQuery(GL_TIME_ELAPSED_EXT,query);active=true;}
  }
  void beginPass(int pass){
    if(!capturePasses)return;
    if(passCount==passQueries.size()){PassQuery q;glGenQueries(1,&q.id);passQueries.push_back(q);}
    auto &q=passQueries[passCount];q.category=pass+1;q.draws=q.triangles=0;
    glBeginQuery(GL_TIME_ELAPSED_EXT,q.id);passCpuStart=threadMilliseconds();
  }
  void draw(unsigned vertices){if(capturePasses){auto &q=passQueries[passCount];++q.draws;q.triangles+=vertices/3;}}
  void endPass(){if(capturePasses){passQueries[passCount].cpuMs=threadMilliseconds()-passCpuStart;glEndQuery(GL_TIME_ELAPSED_EXT);++passCount;}}
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
    if(capturePasses){
      GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);
      double gpu[3]{},cpu[3]{};uint64_t draws[3]{},triangles[3]{};bool valid=!disjoint;
      for(size_t n=0;n<passCount;n++){
        auto &q=passQueries[n];GLuint available=0;glGetQueryObjectuiv(q.id,GL_QUERY_RESULT_AVAILABLE,&available);
        if(!available){valid=false;continue;}
        GLuint64 ns=0;getResult(q.id,GL_QUERY_RESULT,&ns);gpu[q.category]+=double(ns)/1e6;
        cpu[q.category]+=q.cpuMs;draws[q.category]+=q.draws;triangles[q.category]+=q.triangles;
      }
      printf("RENDER_PASS_SAMPLE {\"seconds\":%.6f,\"valid\":%s,\"queries\":%zu,\"gpuMs\":[%.6f,%.6f,%.6f],\"threadCpuMs\":[%.6f,%.6f,%.6f],\"draws\":[%llu,%llu,%llu],\"triangles\":[%llu,%llu,%llu]}\n",
        std::chrono::duration<double>(waitEnd-epoch).count(),valid?"true":"false",passCount,gpu[0],gpu[1],gpu[2],cpu[0],cpu[1],cpu[2],
        (unsigned long long)draws[0],(unsigned long long)draws[1],(unsigned long long)draws[2],(unsigned long long)triangles[0],(unsigned long long)triangles[1],(unsigned long long)triangles[2]);fflush(stdout);
    }
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
