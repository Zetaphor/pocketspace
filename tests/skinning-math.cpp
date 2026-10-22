#include "animation.h"
#include <cassert>
#include <cstdio>
static bool near(float a,float b){return std::abs(a-b)<1e-5;}
int main(){
 using namespace animation;
 // Joint order deliberately differs from node order. A translated bind pose
 // and inverse bind cancel; then only the second joint moves two units.
 std::vector<Node> nodes(3);nodes[0].translation={0,1,0};nodes[1].parent=0;nodes[1].translation={1,0,0};nodes[2].parent=0;
 std::vector<int> sorted;assert(order(nodes,sorted));std::vector<Node> evaluated;std::vector<Matrix> world;
 evaluate(nodes,sorted,Clip{},0,evaluated,world);
 Skin skin;skin.joints={2,1};Node inverseA,inverseB;inverseA.translation={0,-1,0};inverseB.translation={-1,-1,0};skin.inverseBind={local(inverseA),local(inverseB)};
 std::vector<std::vector<Matrix>> palette;palettes({skin},world,palette);
 InfluenceSets sets(2);Influence a,b;a.joints={1,0,0,0};a.weights={.75,0,0,0};b.joints={0,0,0,0};b.weights={.25,0,0,0};sets[0].push_back(a);sets[1].push_back(b);
 float p[3]={1,2,3};auto result=skinPoint(p,palette[0],sets,0);assert(near(result[0],1)&&near(result[1],2)&&near(result[2],3));
 nodes[1].translation[0]=3;evaluate(nodes,sorted,Clip{},0,evaluated,world);palettes({skin},world,palette);result=skinPoint(p,palette[0],sets,0);assert(near(result[0],2.5)&&near(result[1],2));
 // Inverse bind precedes rotation (noncommuting transforms).
 Node joint;joint.rotation={0,0,float(std::sqrt(.5)),float(std::sqrt(.5))};Node inverse;inverse.translation={-1,0,0};
 Skin rotated;rotated.joints={0};rotated.inverseBind={local(inverse)};palettes({rotated},{local(joint)},palette);
 InfluenceSets rigid(1);Influence only;only.weights={1,0,0,0};rigid[0].push_back(only);
 float x[3]={2,0,0};result=skinPoint(x,palette[0],rigid,0);assert(near(result[0],0)&&near(result[1],1));
 puts("PASS skinning math: joint remapping, inverse bind cancellation/order, ancestors, weighted multi-set blend, rotation");
}
