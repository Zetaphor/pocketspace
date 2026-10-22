#include "animation.h"
#include <cassert>
#include <cstdio>
static bool near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<tolerance;}
int main(){
 using namespace animation;
 Channel channel;channel.times={1,3};channel.values={0,2,4,4,6,8};
 assert(near(sample(channel,2)[0],2));assert(near(sample(channel,-1)[0],0));assert(near(sample(channel,4)[0],4));
 channel.interpolation=Interpolation::Step;assert(near(sample(channel,2.99)[0],0));assert(near(sample(channel,3)[0],4));
 channel.interpolation=Interpolation::CubicSpline;
 // Value 0 -> 4, outgoing tangent 2, incoming tangent 0; dt=2.
 channel.values={0,0,0, 0,0,0, 2,0,0, 0,0,0, 4,0,0, 0,0,0};
 assert(near(sample(channel,2)[0],2.5));
 channel.path=Path::Rotation;channel.interpolation=Interpolation::Linear;
 channel.values={0,0,0,1, 0,0,1,0};
 auto q=sample(channel,2);assert(near(q[2],std::sqrt(.5))&&near(q[3],std::sqrt(.5)));
 channel.values={0,0,0,1, 0,0,0,-1};q=sample(channel,2);assert(near(std::abs(q[3]),1));
 channel.interpolation=Interpolation::CubicSpline;channel.values={0,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0};
 q=sample(channel,2);double length=0;for(float v:q)length+=v*v;assert(near(length,1));
 std::vector<Node> nodes(3);nodes[0].parent=2;nodes[0].translation={1,0,0};nodes[2].parent=1;nodes[2].scale={2,2,2};nodes[1].translation={0,3,0};
 std::vector<int> sorted;assert(order(nodes,sorted));std::vector<Node> evaluated;std::vector<Matrix> world;Clip clip;
 evaluate(nodes,sorted,clip,0,evaluated,world);assert(near(world[0][12],2)&&near(world[0][13],3));
 nodes[1].parent=0;assert(!order(nodes,sorted));
 Playback state;state.clip=0;state.playing=true;clip.duration=2;
 advance(state,clip,.5);assert(near(state.time,.5));state.playing=false;assert(!advance(state,clip,5)&&near(state.time,.5));
 state.playing=true;state.speed=2;advance(state,clip,1);assert(near(state.time,.5));
 state.loop=false;advance(state,clip,2);assert(state.finished&&!state.playing&&state.time==2);
 clip.duration=0;state.playing=true;advance(state,clip,1);assert(state.finished&&state.time==0);
 puts("PASS animation math: clamping, STEP, LINEAR, tangent-scaled CUBICSPLINE, shortest quaternion rotation, hierarchy, cycles, playback time");
}
