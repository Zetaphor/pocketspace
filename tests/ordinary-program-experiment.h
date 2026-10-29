#pragma once
#include <string>
#include <stdexcept>

inline bool ordinaryVertexEnabled=false; // Test-only comparison switch; not enabled in normal builds.
inline std::string ordinaryModelVertexSource(){
  std::string source=modelVertexShader;
  const std::string expression="skinEnabled?skinPoint():p";
  const auto offset=source.find(expression);
  if(offset==std::string::npos)throw std::runtime_error("ordinary vertex shader: skin expression changed");
  source.replace(offset,expression.size(),"p");
  return source;
}

// Host-owned, lazily linked ordinary program; never stores scene/pose pointers.
class OrdinaryModelProgram {
  GLuint program=0;bool attempted=false,owned=true;
  GLint center,eye,right,up,forward,panelRight,panelUp,panelForward,halfWidth;
  GLint yaw,pitch,roll,scale,offset,factor,sheen,mode,cutoff,texture;
  GLint location(const char *name)const{return glGetUniformLocation(program,name);}
  static GLuint shader(GLenum type,const char *source){
    GLuint value=glCreateShader(type);glShaderSource(value,1,&source,nullptr);glCompileShader(value);
    GLint ok=0;glGetShaderiv(value,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(value);return 0;}return value;
  }
  void bindLocations(){
    center=location("center");eye=location("eye");right=location("right");up=location("up");forward=location("forward");
    panelRight=location("panelRight");panelUp=location("panelUp");panelForward=location("panelForward");halfWidth=location("panelHalfWidth");
    yaw=location("yaw");pitch=location("pitch");roll=location("roll");scale=location("modelScale");offset=location("offset");
    factor=location("factor");sheen=location("sheen");mode=location("alphaMode");cutoff=location("alphaCutoff");texture=location("material");
  }

public:
  OrdinaryModelProgram()=default;
  OrdinaryModelProgram(const OrdinaryModelProgram&)=delete;
  OrdinaryModelProgram &operator=(const OrdinaryModelProgram&)=delete;
  ~OrdinaryModelProgram(){reset();}
  void reset(){if(program&&owned)glDeleteProgram(program);program=0;attempted=false;owned=true;}
  void borrow(GLuint value){reset();program=value;owned=false;attempted=true;bindLocations();}
  GLuint id()const{return program;}
  bool select(bool gpuSkin){
    if(!ordinaryVertexEnabled||gpuSkin)return false;
    if(attempted)return program!=0;
    attempted=true;
    const auto source=ordinaryModelVertexSource();
    const GLuint v=shader(GL_VERTEX_SHADER,source.c_str()),f=shader(GL_FRAGMENT_SHADER,modelFragmentShader);
    if(v&&f){program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);}
    if(v)glDeleteShader(v);if(f)glDeleteShader(f);
    GLint linked=0;if(program)glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){if(program)glDeleteProgram(program);program=0;fprintf(stderr,"[model] ordinary shader unavailable; using general program\n");return false;}
    bindLocations();
    return true;
  }
  void view(const vr::HmdMatrix34_t &anchor,const float *c,const float *e,const float *r,const float *u,const float *f,float width)const{
    glUseProgram(program);glUniform1i(texture,0);glUniform3fv(center,1,c);glUniform3fv(eye,1,e);
    glUniform3fv(panelRight,1,r);glUniform3fv(panelUp,1,u);glUniform3fv(panelForward,1,f);glUniform1f(halfWidth,width);
    float axes[3][3];for(int col=0;col<3;col++)for(int row=0;row<3;row++)axes[col][row]=anchor.m[row][col]*(col==2?-1.f:1.f);
    glUniform3fv(right,1,axes[0]);glUniform3fv(up,1,axes[1]);glUniform3fv(forward,1,axes[2]);
  }
  void object(float p,float y,float r,float s,float x,float yy,float z)const{
    glUniform1f(pitch,p);glUniform1f(yaw,y);glUniform1f(roll,r);glUniform1f(scale,s);glUniform3f(offset,x,yy,z);
  }
  void material(const PrimitiveAsset &part)const{
    glUniform4fv(factor,1,part.factor);glUniform3fv(sheen,1,part.sheen);glUniform1i(mode,part.alphaMode);glUniform1f(cutoff,part.alphaCutoff);
  }
};
