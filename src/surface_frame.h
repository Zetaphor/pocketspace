#pragma once
#include "animation.h"
#include <cmath>
#include <array>
namespace surfaceFrame {
using V=std::array<float,3>;
inline V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline float dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline V unit(V v,V fallback={0,0,1}){float d=dot(v,v);if(!std::isfinite(d)||d<1e-20f)return fallback;for(auto &x:v)x/=std::sqrt(d);return v;}
inline V direction(const animation::Matrix &m,V v){return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2],m[1]*v[0]+m[5]*v[1]+m[9]*v[2],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]};}
inline float determinant(const animation::Matrix &m){return dot({m[0],m[1],m[2]},cross({m[4],m[5],m[6]},{m[8],m[9],m[10]}));}
inline V normal(const animation::Matrix &m,V v){
 auto a=cross({m[4],m[5],m[6]},{m[8],m[9],m[10]}),b=cross({m[8],m[9],m[10]},{m[0],m[1],m[2]}),c=cross({m[0],m[1],m[2]},{m[4],m[5],m[6]});
 float sign=determinant(m)<0?-1.f:1.f;
 return unit({sign*(a[0]*v[0]+b[0]*v[1]+c[0]*v[2]),sign*(a[1]*v[0]+b[1]*v[1]+c[1]*v[2]),sign*(a[2]*v[0]+b[2]*v[1]+c[2]*v[2])});
}
inline V tangent(V n,V t){float d=dot(n,t);for(int i=0;i<3;i++)t[i]-=n[i]*d;return unit(t,unit(cross(std::abs(n[0])<.9f?V{1,0,0}:V{0,1,0},n),{1,0,0}));}
}
