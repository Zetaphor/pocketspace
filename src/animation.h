#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace animation {
using Matrix=std::array<float,16>;
enum class Path { Translation, Rotation, Scale };
enum class Interpolation { Step, Linear, CubicSpline };
struct Node {
 int parent=-1;bool matrixOnly=false;
 std::array<float,3> translation{0,0,0},scale{1,1,1};
 std::array<float,4> rotation{0,0,0,1};
 Matrix matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
};
struct Channel {int node=-1;Path path=Path::Translation;Interpolation interpolation=Interpolation::Linear;std::vector<float> times,values;};
struct Clip {std::string name,unsupportedReason;double duration=0;std::vector<Channel> channels;bool supported()const{return unsupportedReason.empty()&&!channels.empty();}};
struct Playback {int clip=-1;double time=0,speed=1;bool playing=false,loop=true,finished=false;};
inline void normalize(std::array<float,4> &q){double n=0;for(float x:q)n+=double(x)*x;if(n<1e-20){q={0,0,0,1};return;}n=std::sqrt(n);for(float &x:q)x=float(x/n);}
inline std::array<float,4> sample(const Channel &channel,double time){
 const size_t dimensions=channel.path==Path::Rotation?4:3;
 const bool cubic=channel.interpolation==Interpolation::CubicSpline;
 auto key=[&](size_t i,int component){std::array<float,4> out{};for(size_t k=0;k<dimensions;k++)out[k]=channel.values[(i*(cubic?3:1)+(cubic?component:0))*dimensions+k];return out;};
 std::array<float,4> result;
 if(time<=channel.times.front())result=key(0,1);
 else if(time>=channel.times.back())result=key(channel.times.size()-1,1);
 else{
  const size_t next=std::upper_bound(channel.times.begin(),channel.times.end(),time)-channel.times.begin(),previous=next-1;
  const double dt=channel.times[next]-channel.times[previous],t=(time-channel.times[previous])/dt;
  const auto a=key(previous,1),b=key(next,1);result=a;
  if(channel.interpolation==Interpolation::CubicSpline){
   const auto out=key(previous,2),in=key(next,0);
   for(size_t k=0;k<dimensions;k++)result[k]=float((2*t*t*t-3*t*t+1)*a[k]+(t*t*t-2*t*t+t)*dt*out[k]+(-2*t*t*t+3*t*t)*b[k]+(t*t*t-t*t)*dt*in[k]);
  }else if(channel.interpolation==Interpolation::Linear){
   double dot=0;for(size_t k=0;k<dimensions;k++)dot+=double(a[k])*b[k];
   double sign=1,wa=1-t,wb=t;
   if(channel.path==Path::Rotation){
    if(dot<0){sign=-1;dot=-dot;}
    if(dot<.9995){const double angle=std::acos(std::clamp(dot,0.,1.)),sine=std::sin(angle);wa=std::sin((1-t)*angle)/sine;wb=std::sin(t*angle)/sine;}
   }
   for(size_t k=0;k<dimensions;k++)result[k]=float(wa*a[k]+wb*sign*b[k]);
  }
 }
 if(channel.path==Path::Rotation)normalize(result);
 return result;
}
inline Matrix multiply(const Matrix &a,const Matrix &b){Matrix m{};for(int c=0;c<4;c++)for(int r=0;r<4;r++)for(int k=0;k<4;k++)m[c*4+r]+=a[k*4+r]*b[c*4+k];return m;}
inline Matrix local(const Node &node){
 if(node.matrixOnly)return node.matrix;
 auto q=node.rotation;normalize(q);const float x=q[0],y=q[1],z=q[2],w=q[3];
 return {(1-2*y*y-2*z*z)*node.scale[0],(2*x*y+2*w*z)*node.scale[0],(2*x*z-2*w*y)*node.scale[0],0,
 (2*x*y-2*w*z)*node.scale[1],(1-2*x*x-2*z*z)*node.scale[1],(2*y*z+2*w*x)*node.scale[1],0,
 (2*x*z+2*w*y)*node.scale[2],(2*y*z-2*w*x)*node.scale[2],(1-2*x*x-2*y*y)*node.scale[2],0,
 node.translation[0],node.translation[1],node.translation[2],1};
}
inline bool order(const std::vector<Node> &nodes,std::vector<int> &out){
 out.clear();std::vector<unsigned char> state(nodes.size());
 auto visit=[&](auto &&self,int i)->bool{
  if(state[i]==2)return true;
  if(state[i]==1)return false;
  state[i]=1;
  const int parent=nodes[i].parent;if(parent>=0&&(parent>=int(nodes.size())||!self(self,parent)))return false;
  state[i]=2;out.push_back(i);return true;
 };
 for(size_t i=0;i<nodes.size();i++)if(!visit(visit,int(i)))return false;
 return true;
}
inline void evaluate(const std::vector<Node> &base,const std::vector<int> &order,const Clip &clip,double time,std::vector<Node> &nodes,std::vector<Matrix> &world){
 nodes=base;world.resize(nodes.size());
 for(const auto &channel:clip.channels){
  const auto value=sample(channel,time);auto &node=nodes[channel.node];
  if(channel.path==Path::Rotation)node.rotation=value;
  else for(int i=0;i<3;i++)(channel.path==Path::Scale?node.scale:node.translation)[i]=value[i];
 }
 for(int i:order){world[i]=local(nodes[i]);if(nodes[i].parent>=0)world[i]=multiply(world[nodes[i].parent],world[i]);}
}
struct Skin {std::vector<int> joints;std::vector<Matrix> inverseBind;};
struct Influence {std::array<uint32_t,4> joints{};std::array<float,4> weights{};};
using InfluenceSets=std::vector<std::vector<Influence>>;
inline std::array<float,3> transformPoint(const Matrix &matrix,const float *p){
 std::array<float,3> out{};for(int axis=0;axis<3;axis++)out[axis]=matrix[axis]*p[0]+matrix[4+axis]*p[1]+matrix[8+axis]*p[2]+matrix[12+axis];return out;
}
inline void palettes(const std::vector<Skin> &skins,const std::vector<Matrix> &world,std::vector<std::vector<Matrix>> &out){
 out.resize(skins.size());
 for(size_t s=0;s<skins.size();s++){out[s].resize(skins[s].joints.size());for(size_t j=0;j<skins[s].joints.size();j++)out[s][j]=multiply(world[skins[s].joints[j]],skins[s].inverseBind[j]);}
}
inline std::array<float,3> skinPoint(const float *p,const std::vector<Matrix> &palette,const InfluenceSets &sets,size_t vertex){
 std::array<float,3> out{};
 for(const auto &set:sets)for(int lane=0;lane<4;lane++){
  const auto &influence=set[vertex];const float weight=influence.weights[lane];if(weight==0)continue;
  const auto transformed=transformPoint(palette[influence.joints[lane]],p);
  for(int axis=0;axis<3;axis++)out[axis]+=weight*transformed[axis];
 }
 return out;
}
inline bool advance(Playback &state,const Clip &clip,double dt){
 if(!state.playing||dt<=0)return false;
 const double old=state.time,next=old+dt*state.speed;
 if(clip.duration<=0){state.time=0;state.playing=false;state.finished=true;}
 else if(state.loop)state.time=std::fmod(next,clip.duration);
 else{state.time=std::min(next,clip.duration);if(state.time>=clip.duration){state.playing=false;state.finished=true;}}
 return old!=state.time;
}
} // namespace animation
