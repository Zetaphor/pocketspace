#pragma once
#include <cmath>
namespace tracking {
// Head coordinates use +X right, +Y up, +Z forward; angles are radians.
struct EyeCalibration {
  bool loaded=false;
  double yaw[3]{},pitch[3]{},bounds[4]{};
  bool correct(const float input[3],const float basis[3][3],float output[3])const{
    double local[3]{};
    for(int c=0;c<3;c++)for(int r=0;r<3;r++)local[c]+=basis[r][c]*input[r];
    const double x=std::atan2(local[0],local[2]),y=std::atan2(local[1],std::hypot(local[0],local[2]));
    if(!loaded||local[2]<=0||x<bounds[0]||x>bounds[1]||y<bounds[2]||y>bounds[3])return false;
    const double a=yaw[0]+yaw[1]*x+yaw[2]*y,b=pitch[0]+pitch[1]*x+pitch[2]*y;
    if(!std::isfinite(a+b)||std::abs(a)>1.4||std::abs(b)>1.4)return false;
    const double direction[]={std::sin(a)*std::cos(b),std::sin(b),std::cos(a)*std::cos(b)};
    for(int r=0;r<3;r++){output[r]=0;for(int c=0;c<3;c++)output[r]+=basis[r][c]*direction[c];}
    return true;
  }
};
}
