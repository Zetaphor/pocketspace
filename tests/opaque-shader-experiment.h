#pragma once
// Test-only shaders. Opaque specialization is never used for masked/blended parts.
inline constexpr const char *opaqueColorExperiment=R"(#version 300 es
precision mediump float;in vec2 t;uniform sampler2D material;uniform vec4 factor;uniform vec3 sheen;out vec4 color;
void main(){vec4 texel=texture(material,t);color=texel*factor;color.rgb=clamp(color.rgb+texel.rgb*sheen*0.8,0.0,1.0);color.a=1.0;})";
inline constexpr const char *opaqueDepthExperiment=R"(#version 300 es
precision mediump float;out vec4 color;void main(){color=vec4(0.0);})";

struct OpaqueExperimentProgram {
  GLuint id=0;
  GLint yaw,pitch,roll,scale,offset,factor,sheen;
  explicit OpaqueExperimentProgram(const char *fragment){
    auto v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,fragment);
    id=glCreateProgram();glAttachShader(id,v);glAttachShader(id,f);glLinkProgram(id);
    GLint linked=0;glGetProgramiv(id,GL_LINK_STATUS,&linked);glDeleteShader(v);glDeleteShader(f);
    if(!linked){fprintf(stderr,"opaque experiment link failed\n");std::exit(1);}
    yaw=location("yaw");pitch=location("pitch");roll=location("roll");scale=location("modelScale");offset=location("offset");factor=location("factor");sheen=location("sheen");
  }
  ~OpaqueExperimentProgram(){glDeleteProgram(id);}
  GLint location(const char *name)const{return glGetUniformLocation(id,name);}
  void vector(const char *name,const float *value)const{glUniform3fv(location(name),1,value);}
  void view(const vr::HmdMatrix34_t &anchor,const float *center,const float *eye,const float *right,const float *up,const float *forward,float halfWidth)const{
    glUseProgram(id);glUniform1i(location("material"),0);
    vector("center",center);vector("eye",eye);vector("panelRight",right);vector("panelUp",up);vector("panelForward",forward);
    for(int column=0;column<3;column++){float axis[3];for(int row=0;row<3;row++)axis[row]=anchor.m[row][column]*(column==2?-1.f:1.f);vector(column==0?"right":column==1?"up":"forward",axis);}
    glUniform1f(location("panelHalfWidth"),halfWidth);
  }
  void object(float p,float y,float r,float s,float x,float yy,float z)const{
    glUniform1f(pitch,p);glUniform1f(yaw,y);glUniform1f(roll,r);glUniform1f(scale,s);glUniform3f(offset,x,yy,z);
  }
  void material(const PrimitiveAsset &part)const{glUniform4fv(factor,1,part.factor);glUniform3fv(sheen,1,part.sheen);}
};
