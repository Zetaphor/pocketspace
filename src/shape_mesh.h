#pragma once
// Generated primitive shapes for shape assets: triangles in a part's own frame,
// filling the box from -size/2 to size/2, with outward normals and
// counter-clockwise front faces.
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

namespace shapeMesh {
enum class Shape {Cube,Sphere,Cylinder,Cone,Wedge,Pyramid,Torus,Arch};
enum class Finish {Matte,Glossy,Metal,Glow};
using V=std::array<float,3>;
struct Vertex {V position,normal;};
struct Part {Shape shape=Shape::Cube;V position{},rotation{},scale{1,1,1},color{1,1,1};Finish finish=Finish::Matte;};
struct FinishValues {float metallic,roughness;bool emissive;};

// Segments around curved shapes and rings across a sphere or torus tube.
inline constexpr int kSegments=24,kRings=12;
inline constexpr size_t kMaxParts=256,kMaxMaterials=64;

inline constexpr std::array<std::pair<std::string_view,Shape>,8> shapeNames{{
 {"cube",Shape::Cube},{"sphere",Shape::Sphere},{"cylinder",Shape::Cylinder},{"cone",Shape::Cone},
 {"wedge",Shape::Wedge},{"pyramid",Shape::Pyramid},{"torus",Shape::Torus},{"arch",Shape::Arch}}};
inline constexpr std::array<std::pair<std::string_view,Finish>,4> finishNames{{
 {"matte",Finish::Matte},{"glossy",Finish::Glossy},{"metal",Finish::Metal},{"glow",Finish::Glow}}};
inline std::optional<Shape> shape(std::string_view name){for(auto [key,value]:shapeNames)if(key==name)return value;return std::nullopt;}
inline std::optional<Finish> finish(std::string_view name){for(auto [key,value]:finishNames)if(key==name)return value;return std::nullopt;}
inline FinishValues values(Finish finish){
 switch(finish){
  case Finish::Glossy:return {0,.25f,false};
  case Finish::Metal:return {1,.3f,false};
  case Finish::Glow:return {0,.9f,true};
  default:return {0,.9f,false};
 }
}
// The torus tube and the arch's square cross-section take their thickness from
// one axis, which must leave room for the opening.
inline const char *sizeError(Shape shape,const V &size){
 if(shape==Shape::Torus&&!(size[1]<std::min(size[0],size[2])))return "torus scale y (tube thickness) must be less than x and z";
 if(shape==Shape::Arch&&!(size[2]<size[0]*.5f&&size[2]<size[1]))return "arch scale z (cross-section) must be less than half of x and less than y";
 return nullptr;
}

namespace detail {
inline V sub(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline float dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline V unit(V v){const float l=std::sqrt(dot(v,v));return l>0?V{v[0]/l,v[1]/l,v[2]/l}:V{0,1,0};}
struct Builder {
 std::vector<Vertex> out;
 // Orders each triangle so its winding faces the same way as its normals.
 void tri(Vertex a,Vertex b,Vertex c){
  const V face=cross(sub(b.position,a.position),sub(c.position,a.position));
  const V normal{a.normal[0]+b.normal[0]+c.normal[0],a.normal[1]+b.normal[1]+c.normal[1],a.normal[2]+b.normal[2]+c.normal[2]};
  // Repeated corners (sphere poles, a cone apex) make no triangle; fused
  // multiply-add can leave their cross product slightly off zero.
  if(a.position==b.position||b.position==c.position||a.position==c.position)return;
  if(dot(face,normal)<0)std::swap(b,c);
  out.insert(out.end(),{a,b,c});
 }
 void quad(Vertex a,Vertex b,Vertex c,Vertex d){tri(a,b,c);tri(a,c,d);}
 // A flat face of a convex unit shape centered at the origin.
 void flat(std::initializer_list<V> corners){
  const V *p=corners.begin();const size_t n=corners.size();
  V normal=unit(cross(sub(p[1],p[0]),sub(p[2],p[0])));V centroid{};
  for(size_t i=0;i<n;i++)for(int k=0;k<3;k++)centroid[k]+=p[i][k]/float(n);
  if(dot(normal,centroid)<0)normal={-normal[0],-normal[1],-normal[2]};
  for(size_t i=1;i+1<n;i++)tri({p[0],normal},{p[i],normal},{p[i+1],normal});
 }
};
inline float angle(int i,int count,float span=6.28318530718f){return span*float(i)/float(count);}
// Unit shapes fill -0.5..0.5 and are stretched to size afterwards.
inline void unitShape(Shape shape,Builder &b){
 constexpr float h=.5f;
 switch(shape){
  case Shape::Cube:
   for(int axis=0;axis<3;axis++)for(float side:{-h,h}){
    const int u=(axis+1)%3,v=(axis+2)%3;V c[4];
    for(int k=0;k<4;k++){c[k][axis]=side;c[k][u]=(k==1||k==2)?h:-h;c[k][v]=k>=2?h:-h;}
    b.flat({c[0],c[1],c[2],c[3]});
   }
   break;
  case Shape::Sphere:
   for(int ring=0;ring<kRings;ring++)for(int segment=0;segment<kSegments;segment++){
    auto point=[&](int r,int s){
     // Exact poles, so their zero-area triangles are dropped.
     if(r==0||r==kRings){const V n{0,r==0?1.f:-1.f,0};return Vertex{{0,h*n[1],0},n};}
     const float t=angle(r,kRings,3.14159265359f),p=angle(s,kSegments);const V n{std::sin(t)*std::cos(p),std::cos(t),std::sin(t)*std::sin(p)};return Vertex{{h*n[0],h*n[1],h*n[2]},n};};
    b.quad(point(ring,segment),point(ring+1,segment),point(ring+1,segment+1),point(ring,segment+1));
   }
   break;
  case Shape::Cylinder:case Shape::Cone:{
   const bool cone=shape==Shape::Cone;
   for(int segment=0;segment<kSegments;segment++){
    const float a=angle(segment,kSegments),c=angle(segment+1,kSegments),m=(a+c)*.5f;
    // A cone's side normal leans up by its radius-to-height slope.
    auto side=[&](float t){return unit(cone?V{std::cos(t),h,std::sin(t)}:V{std::cos(t),0,std::sin(t)});};
    const V p0{h*std::cos(a),-h,h*std::sin(a)},p1{h*std::cos(c),-h,h*std::sin(c)};
    if(cone)b.tri({p0,side(a)},{p1,side(c)},{{0,h,0},side(m)});
    else b.quad({p0,side(a)},{p1,side(c)},{{p1[0],h,p1[2]},side(c)},{{p0[0],h,p0[2]},side(a)});
    b.tri({{0,-h,0},{0,-1,0}},{p0,{0,-1,0}},{p1,{0,-1,0}});
    if(!cone)b.tri({{0,h,0},{0,1,0}},{{p0[0],h,p0[2]},{0,1,0}},{{p1[0],h,p1[2]},{0,1,0}});
   }
   break;
  }
  case Shape::Wedge:{
   // Full bottom and back (-Z) faces; the slope faces up and toward +Z.
   const V a{-h,-h,-h},b2{h,-h,-h},c{h,-h,h},d{-h,-h,h},e{-h,h,-h},f{h,h,-h};
   b.flat({a,b2,c,d});b.flat({a,b2,f,e});b.flat({d,c,f,e});b.flat({a,d,e});b.flat({b2,c,f});
   break;
  }
  case Shape::Pyramid:{
   const V a{-h,-h,-h},b2{h,-h,-h},c{h,-h,h},d{-h,-h,h},top{0,h,0};
   b.flat({a,b2,c,d});b.flat({a,b2,top});b.flat({b2,c,top});b.flat({c,d,top});b.flat({d,a,top});
   break;
  }
  default:break;
 }
}
// Torus at its real size: X and Z set the ring, Y the round tube's thickness.
inline void torus(const V &size,Builder &b){
 const float r=size[1]*.5f,cx=size[0]*.5f-r,cz=size[2]*.5f-r;
 auto point=[&](int s,int t){
  const float theta=angle(s,kSegments),phi=angle(t,kRings);
  const V ring=unit({cz*std::cos(theta),0,cx*std::sin(theta)});
  const V n{std::cos(phi)*ring[0],std::sin(phi),std::cos(phi)*ring[2]};
  return Vertex{{cx*std::cos(theta)+r*n[0],r*n[1],cz*std::sin(theta)+r*n[2]},n};
 };
 for(int s=0;s<kSegments;s++)for(int t=0;t<kRings;t++)b.quad(point(s,t),point(s+1,t),point(s+1,t+1),point(s,t+1));
}
// Arch at its real size: X is the span, Y the rise from the base, Z the side of
// its square cross-section.
inline void arch(const V &size,Builder &b){
 const float t=size[2],a=size[0]*.5f,height=size[1],ai=a-t,bi=height-t,base=-height*.5f,z=t*.5f;
 auto outer=[&](int s){const float q=angle(s,kSegments,3.14159265359f);return std::pair<V,V>{{a*std::cos(q),base+height*std::sin(q),0},unit({height*std::cos(q),a*std::sin(q),0})};};
 auto inner=[&](int s){const float q=angle(s,kSegments,3.14159265359f);return std::pair<V,V>{{ai*std::cos(q),base+bi*std::sin(q),0},unit({-bi*std::cos(q),-ai*std::sin(q),0})};};
 auto at=[](V p,float depth){p[2]=depth;return p;};
 for(int s=0;s<kSegments;s++){
  const auto [o0,on0]=outer(s);const auto [o1,on1]=outer(s+1);const auto [i0,in0]=inner(s);const auto [i1,in1]=inner(s+1);
  b.quad({at(o0,-z),on0},{at(o1,-z),on1},{at(o1,z),on1},{at(o0,z),on0});
  b.quad({at(i0,-z),in0},{at(i1,-z),in1},{at(i1,z),in1},{at(i0,z),in0});
  for(float side:{-z,z}){const V n{0,0,side>0?1.f:-1.f};b.quad({at(o0,side),n},{at(o1,side),n},{at(i1,side),n},{at(i0,side),n});}
 }
 for(int s:{0,kSegments}){
  const V o=outer(s).first,i=inner(s).first,n{0,-1,0};
  b.quad({at(o,-z),n},{at(i,-z),n},{at(i,z),n},{at(o,z),n});
 }
}
}

// Triangles for one shape filling -size/2..size/2 in its own frame.
inline std::vector<Vertex> generate(Shape shape,const V &size){
 detail::Builder b;
 if(shape==Shape::Torus)detail::torus(size,b);
 else if(shape==Shape::Arch)detail::arch(size,b);
 else{
  detail::unitShape(shape,b);
  // Stretch positions; normals use the inverse stretch.
  for(auto &v:b.out){
   for(int axis=0;axis<3;axis++){v.position[axis]*=size[axis];v.normal[axis]/=size[axis];}
   v.normal=detail::unit(v.normal);
  }
 }
 return std::move(b.out);
}
// The object rotation convention: roll about Z, then pitch about X, then yaw about Y.
inline V rotate(const V &rotation,const V &v){
 const float cp=std::cos(rotation[0]),sp=std::sin(rotation[0]),cy=std::cos(rotation[1]),sy=std::sin(rotation[1]),cr=std::cos(rotation[2]),sr=std::sin(rotation[2]);
 const V a{cr*v[0]-sr*v[1],sr*v[0]+cr*v[1],v[2]};
 const V b{a[0],cp*a[1]-sp*a[2],sp*a[1]+cp*a[2]};
 return {cy*b[0]+sy*b[2],b[1],-sy*b[0]+cy*b[2]};
}
}
