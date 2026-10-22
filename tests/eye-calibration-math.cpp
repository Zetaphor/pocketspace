#include "eye_calibration.h"
#include <cassert>
int main(){
 tracking::EyeCalibration c;c.loaded=true;c.yaw[0]=.1;c.yaw[1]=1;c.pitch[2]=1;
 c.bounds[0]=c.bounds[2]=-.5;c.bounds[1]=c.bounds[3]=.5;
 float identity[3][3]={{1,0,0},{0,1,0},{0,0,1}},input[]={0,0,1},out[3];
 assert(c.correct(input,identity,out));assert(std::abs(out[0]-std::sin(.1))<1e-6);
 // A 90-degree head turn must rotate the correction along with the head.
 float rotated[3][3]={{0,0,1},{0,1,0},{-1,0,0}},turned[]={1,0,0};
 assert(c.correct(turned,rotated,out));assert(std::abs(out[0]-std::cos(.1))<1e-6);assert(std::abs(out[2]+std::sin(.1))<1e-6);
 float outside[]={1,0,1};assert(!c.correct(outside,identity,out));
 float behind[]={0,0,-1};assert(!c.correct(behind,identity,out));
 c.loaded=false;assert(!c.correct(input,identity,out));
}
