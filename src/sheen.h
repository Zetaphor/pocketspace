#pragma once
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

// Charlie microfibers use perceptual roughness squared. Keep CPU integration and
// GLSL evaluation in agreement; the lower bound avoids singular zero roughness.
namespace sheen {
inline constexpr double pi=3.141592653589793;
inline double lambda(double cosine,double alpha){
 cosine=std::clamp(cosine,0.,1.);
 const double q=(1-alpha)*(1-alpha);
 const double a=21.5473+(25.3245-21.5473)*q,b=3.82987+(3.32435-3.82987)*q;
 const double c=.19823+(.16801-.19823)*q,d=-1.97760+(-1.27393+1.97760)*q,e=-4.32054+(-4.85967+4.32054)*q;
 auto fitted=[&](double x){return a/(1+b*std::pow(x,c))+d*x+e;};
 return std::exp(cosine<.5?fitted(cosine):2*fitted(.5)-fitted(1-cosine));
}
inline double brdf(double nv,double nl,double nh,double roughness){
 if(nv<=0||nl<=0)return 0;
 const double alpha=std::pow(std::clamp(roughness,.07,1.),2),inverse=1/alpha;
 const double distribution=(2+inverse)*std::pow(std::max(0.,1-nh*nh),inverse*.5)/(2*pi);
 return distribution*std::min(1.,1/((1+lambda(nv,alpha)+lambda(nl,alpha))*4*nv*nl));
}
// Midpoint integration over incident solid angle supplies directional albedo.
inline double albedo(double nv,double roughness,int elevation=32,int azimuth=64){
 double sum=0;const double vx=std::sqrt(std::max(0.,1-nv*nv));
 for(int y=0;y<elevation;y++){
  const double nl=(y+.5)/elevation,lx=std::sqrt(1-nl*nl);
  for(int x=0;x<azimuth;x++){
   const double vl=vx*lx*std::cos(2*pi*(x+.5)/azimuth)+nv*nl;
   const double nh=(nv+nl)/std::sqrt(std::max(1e-12,2+2*vl));
   sum+=brdf(nv,nl,nh,roughness)*nl;
  }
 }
 return sum*2*pi/(elevation*azimuth);
}
}
struct SheenLookup {
 static constexpr int size=32;
 std::array<float,size*size*2> values{};
 GLuint texture=0;
 SheenLookup(){
  for(int y=0;y<size;y++){
   const double roughness=(y+.5)/size;
   double maximum=std::max(1.,sheen::albedo(1e-6,roughness));
   for(int x=0;x<size;x++){const float e=float(sheen::albedo((x+.5)/size,roughness));values[(y*size+x)*2]=e;maximum=std::max(maximum,double(e));}
   // A roughness-only correction keeps the BRDF reciprocal while bounding the
   // fitted visibility model's excess grazing energy. Include quadrature margin.
   const float normalization=float(1/(maximum*1.01));
   for(int x=0;x<size;x++){values[(y*size+x)*2]*=normalization;values[(y*size+x)*2+1]=normalization;}
  }
 }
 ~SheenLookup(){if(texture)glDeleteTextures(1,&texture);}
 bool upload(){
  if(texture)return true;
  GLint binding=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
  glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,size,size,0,GL_RG,GL_FLOAT,values.data());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D,GLuint(binding));return texture!=0;
 }
};
inline std::shared_ptr<SheenLookup> sharedSheenLookup(){
 static std::weak_ptr<SheenLookup> cache;
 if(auto existing=cache.lock())return existing;
 auto result=std::make_shared<SheenLookup>();cache=result;return result;
}
