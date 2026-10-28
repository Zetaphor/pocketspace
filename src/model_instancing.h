#pragma once
// Ordered, opt-in batching. Never reorder objects/materials to create a batch.
class ModelInstancing {
public:
  enum Mode {Off,Original,Instanced};
  Mode mode=Off;
private:
  struct Instance {float offsetScale[4],angles[3];};
  static_assert(sizeof(Instance)==7*sizeof(float));
  static constexpr size_t capacity=256;
  GLuint buffer=0;
  struct Program {
    GLuint id=0;
    GLint center,eye,right,up,forward,panelRight,panelUp,panelForward,halfWidth,material,factor,sheen;
    static GLuint shader(GLenum type,const char *source){
      GLuint id=glCreateShader(type);glShaderSource(id,1,&source,nullptr);glCompileShader(id);GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
      if(!ok){glDeleteShader(id);return 0;}return id;
    }
    bool init(const char *vertex){
      const auto v=shader(GL_VERTEX_SHADER,vertex),f=shader(GL_FRAGMENT_SHADER,modelFragmentShader);
      if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return false;}
      id=glCreateProgram();glAttachShader(id,v);glAttachShader(id,f);glLinkProgram(id);glDeleteShader(v);glDeleteShader(f);
      GLint ok=0;glGetProgramiv(id,GL_LINK_STATUS,&ok);if(!ok)return false;
      center=location("center");eye=location("eye");right=location("right");up=location("up");forward=location("forward");panelRight=location("panelRight");panelUp=location("panelUp");panelForward=location("panelForward");halfWidth=location("panelHalfWidth");material=location("material");factor=location("factor");sheen=location("sheen");return true;
    }
    ~Program(){if(id)glDeleteProgram(id);}
    GLint location(const char *name)const{return glGetUniformLocation(id,name);}
    void view(const vr::HmdMatrix34_t &anchor,const float *c,const float *e,const float *r,const float *u,const float *f,float width)const{
      glUseProgram(id);glUniform1i(material,0);glUniform3fv(center,1,c);glUniform3fv(eye,1,e);glUniform3fv(panelRight,1,r);glUniform3fv(panelUp,1,u);glUniform3fv(panelForward,1,f);glUniform1f(halfWidth,width);
      float axes[3][3];for(int column=0;column<3;column++)for(int row=0;row<3;row++)axes[column][row]=anchor.m[row][column]*(column==2?-1.f:1.f);
      glUniform3fv(right,1,axes[0]);glUniform3fv(up,1,axes[1]);glUniform3fv(forward,1,axes[2]);
    }
    void setMaterial(const PrimitiveAsset &p)const{glUniform4fv(factor,1,p.factor);glUniform3fv(sheen,1,p.sheen);}
  };
  std::unique_ptr<Program> program;
  bool attempted=false;
  GLint alphaMode=-1,alphaCutoff=-1;
  std::vector<Instance> staging;
  using Clock=std::chrono::steady_clock;
  Clock::time_point epoch=Clock::now(),window=epoch;
  uint64_t frames=0,batches=0,instances=0,fallbacks=0,totalBatches=0;
  bool ensure(){
    if(attempted)return program&&buffer;
    attempted=true;
    std::string vertex=modelVertexShader;
    const auto begin=vertex.find("uniform vec3 offset;"),end=vertex.find("out vec2 t;");
    if(begin==std::string::npos||end==std::string::npos)return false;
    vertex.replace(begin,end-begin,R"(layout(location=2) in vec4 instanceOffsetScale;
layout(location=3) in vec3 instanceAngles;
#define offset instanceOffsetScale.xyz
#define modelScale instanceOffsetScale.w
#define pitch instanceAngles.x
#define yaw instanceAngles.y
#define roll instanceAngles.z
uniform float panelHalfWidth;
)");
    program=std::make_unique<Program>();
    if(!program->init(vertex.c_str())){fprintf(stderr,"[model] instancing shader unavailable; using original draws\n");program.reset();return false;}
    alphaMode=program->location("alphaMode");alphaCutoff=program->location("alphaCutoff");
    GLint previous=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);
    glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,capacity*sizeof(Instance),nullptr,GL_STREAM_DRAW);glBindBuffer(GL_ARRAY_BUFFER,previous);
    GLint allocated=0;glBindBuffer(GL_ARRAY_BUFFER,buffer);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&allocated);glBindBuffer(GL_ARRAY_BUFFER,previous);
    if(allocated!=GLint(capacity*sizeof(Instance))){if(buffer)glDeleteBuffers(1,&buffer);buffer=0;program.reset();fprintf(stderr,"[model] instancing buffer unavailable; using original draws\n");return false;}
    staging.reserve(capacity);return true;
  }
  static bool eligible(const Scene &scene,const Object &object,int pass){
    const auto &asset=scene.assets[size_t(object.asset)-1];
    return asset.gpuInstancing&&pass<=0&&!object.animationPose&&!asset.hasAnimation&&!asset.hasSkin&&!asset.hasMorph&&asset.primitives.size()==1&&asset.primitives[0].alphaMode!=2&&asset.primitives[0].vbo;
  }
public:
  explicit ModelInstancing(Mode initial=Off):mode(initial){}
  ~ModelInstancing(){reset();}
  void reset(){if(buffer)glDeleteBuffers(1,&buffer);buffer=0;program.reset();staging.clear();attempted=false;}
  bool prewarm(){return ensure();}
  uint64_t allocatedBytes()const{return buffer?capacity*sizeof(Instance):0;}
  GLuint instanceBuffer()const{return buffer;}
  uint64_t submittedBatches()const{return totalBatches;}
  template<class Fallback,class Count> void draw(const Scene &scene,const std::vector<const Object*> &objects,int pass,GLuint baseline,const vr::HmdMatrix34_t &anchor,const float center[3],const float eye[3],const float r[3],const float u[3],const float f[3],float halfWidth,const float origin[3],Fallback fallback,Count count){
    if(mode==Original)ensure();
    for(size_t begin=0;begin<objects.size();){
      size_t end=begin+1;
      if(mode==Instanced&&eligible(scene,*objects[begin],pass))while(end<objects.size()&&end-begin<capacity&&objects[end]->asset==objects[begin]->asset&&eligible(scene,*objects[end],pass))++end;
      if(end-begin<2||!ensure()){fallback(*objects[begin]);++fallbacks;++begin;continue;}
      const auto &part=scene.assets[size_t(objects[begin]->asset)-1].primitives[0];staging.clear();
      for(size_t n=begin;n<end;n++){
        const auto &o=*objects[n];staging.push_back({{o.x-origin[0],o.y-origin[1],o.z-origin[2],o.scale},{o.pitch,o.yaw,o.roll}});
      }
      program->view(anchor,center,eye,r,u,f,halfWidth);program->setMaterial(part);glUniform1i(alphaMode,part.alphaMode);glUniform1f(alphaCutoff,part.alphaCutoff);
      glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
      glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,capacity*sizeof(Instance),nullptr,GL_STREAM_DRAW);glBufferSubData(GL_ARRAY_BUFFER,0,staging.size()*sizeof(Instance),staging.data());
      glEnableVertexAttribArray(2);glEnableVertexAttribArray(3);
      glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Instance),nullptr);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(Instance),reinterpret_cast<void*>(4*sizeof(float)));
      glVertexAttribDivisor(2,1);glVertexAttribDivisor(3,1);
      glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
      if(part.indexBuffer){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.indexBuffer);glDrawElementsInstanced(GL_TRIANGLES,part.vertexCount,part.indexType,nullptr,end-begin);}
      else glDrawArraysInstanced(GL_TRIANGLES,0,part.vertexCount,end-begin);
      glVertexAttribDivisor(2,0);glVertexAttribDivisor(3,0);glDisableVertexAttribArray(2);glDisableVertexAttribArray(3);glUseProgram(baseline);
      count(unsigned(part.vertexCount)*unsigned(end-begin));++batches;++totalBatches;instances+=end-begin;begin=end;
    }
  }
#ifdef SF_GPU_SYNC_EXPERIMENT
  void report(){
    ++frames;const auto now=Clock::now();if(now-window<std::chrono::seconds(1))return;
    printf("INSTANCING_SAMPLE {\"seconds\":%.6f,\"frames\":%llu,\"batches\":%llu,\"instances\":%llu,\"savedDraws\":%llu,\"fallbackObjects\":%llu,\"instanceBytes\":%zu}\n",std::chrono::duration<double>(now-epoch).count(),(unsigned long long)frames,(unsigned long long)batches,(unsigned long long)instances,(unsigned long long)(instances-batches),(unsigned long long)fallbacks,buffer?capacity*sizeof(Instance):0);fflush(stdout);
    window=now;frames=batches=instances=fallbacks=0;
  }
#endif
};
