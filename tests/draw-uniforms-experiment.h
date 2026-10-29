#pragma once
#include <array>
#include <cstddef>
#include <GLES3/gl3.h>
// Reset for every pass: never reuse cached values across external program writes.
inline bool drawUniformCachingEnabled=false;
struct ModelDrawLocations {
 GLint yaw,pitch,roll,scale,offset,factor,mode,cutoff,sheen;
 explicit ModelDrawLocations(GLuint program){
  yaw=glGetUniformLocation(program,"yaw");pitch=glGetUniformLocation(program,"pitch");roll=glGetUniformLocation(program,"roll");scale=glGetUniformLocation(program,"modelScale");offset=glGetUniformLocation(program,"offset");factor=glGetUniformLocation(program,"factor");mode=glGetUniformLocation(program,"alphaMode");cutoff=glGetUniformLocation(program,"alphaCutoff");sheen=glGetUniformLocation(program,"sheen");
 }
};
class ModelDrawUniforms {
 template<class T> struct Value {T value{};bool valid=false;};
 Value<float> yaw,pitch,roll,scale,cutoff;Value<int> mode;
 Value<std::array<float,3>> offset,sheen;Value<std::array<float,4>> factor;
 const ModelDrawLocations &locations;
 template<class T,class Apply> void set(Value<T> &old,const T &value,Apply apply){
  if(!enabled){apply();++issued;return;}
  if(old.valid&&old.value==value){++skipped;return;}
  apply();old.value=value;old.valid=true;++issued;
 }
public:
 bool enabled;size_t issued=0,skipped=0;
 ModelDrawUniforms(const ModelDrawLocations &locations,bool enabled):locations(locations),enabled(enabled){}
 void object(float p,float y,float r,float s,float x,float yy,float z){
  set(yaw,y,[&]{glUniform1f(locations.yaw,y);});set(pitch,p,[&]{glUniform1f(locations.pitch,p);});set(roll,r,[&]{glUniform1f(locations.roll,r);});set(scale,s,[&]{glUniform1f(locations.scale,s);});
  set(offset,std::array<float,3>{x,yy,z},[&]{glUniform3f(locations.offset,x,yy,z);});
 }
 template<class Part> void material(const Part &p){
  set(factor,std::array<float,4>{p.factor[0],p.factor[1],p.factor[2],p.factor[3]},[&]{glUniform4fv(locations.factor,1,p.factor);});
  set(mode,p.alphaMode,[&]{glUniform1i(locations.mode,p.alphaMode);});set(cutoff,p.alphaCutoff,[&]{glUniform1f(locations.cutoff,p.alphaCutoff);});
  set(sheen,std::array<float,3>{p.sheen[0],p.sheen[1],p.sheen[2]},[&]{glUniform3fv(locations.sheen,1,p.sheen);});
 }
};
