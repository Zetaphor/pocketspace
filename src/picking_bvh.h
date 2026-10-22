#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace picking {
struct Triangle {uint32_t primitive=0,offset=0;};
struct Node {
  float low[3]{},high[3]{};
  uint32_t first=0,count=0,left=0,right=0;
};

// Immutable normalized geometry: shared by every instance of a loaded asset.
template<class Asset> void build(Asset &asset){
  asset.pickTriangles.clear();asset.pickNodes.clear();
  for(size_t part=0;part<asset.primitives.size();part++)
    for(size_t offset=0;offset+14<asset.primitives[part].vertices.size();offset+=15)
      asset.pickTriangles.push_back({static_cast<uint32_t>(part),static_cast<uint32_t>(offset)});
  if(asset.pickTriangles.empty())return;
  asset.pickNodes.reserve(asset.pickTriangles.size()/2+1);
  auto vertex=[&](const Triangle &triangle){return asset.primitives[triangle.primitive].vertices.data()+triangle.offset;};
  auto centroid=[&](const Triangle &triangle,int axis){const float *v=vertex(triangle);return (v[axis]+v[5+axis]+v[10+axis])/3.f;};
  auto split=[&](auto &&self,uint32_t first,uint32_t count)->uint32_t{
    const uint32_t id=static_cast<uint32_t>(asset.pickNodes.size());
    asset.pickNodes.emplace_back();
    Node node;node.first=first;node.count=count;
    float centroidLow[3],centroidHigh[3];
    for(int axis=0;axis<3;axis++){
      node.low[axis]=centroidLow[axis]=std::numeric_limits<float>::max();
      node.high[axis]=centroidHigh[axis]=-std::numeric_limits<float>::max();
    }
    for(uint32_t i=first;i<first+count;i++){
      const auto &triangle=asset.pickTriangles[i];const float *v=vertex(triangle);
      for(int axis=0;axis<3;axis++){
        for(int point=0;point<3;point++){
          node.low[axis]=std::min(node.low[axis],v[point*5+axis]);
          node.high[axis]=std::max(node.high[axis],v[point*5+axis]);
        }
        const float center=centroid(triangle,axis);
        centroidLow[axis]=std::min(centroidLow[axis],center);
        centroidHigh[axis]=std::max(centroidHigh[axis],center);
      }
    }
    // Conservative bounds preserve edge/vertex hits despite floating-point rounding.
    for(int axis=0;axis<3;axis++){node.low[axis]-=1e-6f;node.high[axis]+=1e-6f;}
    if(count>8){
      int axis=0;
      for(int i=1;i<3;i++)if(centroidHigh[i]-centroidLow[i]>centroidHigh[axis]-centroidLow[axis])axis=i;
      const uint32_t half=count/2;
      auto begin=asset.pickTriangles.begin()+first;
      std::nth_element(begin,begin+half,begin+count,[&](const Triangle &a,const Triangle &b){return centroid(a,axis)<centroid(b,axis);});
      node.count=0;
      node.left=self(self,first,half);node.right=self(self,first+half,count-half);
    }
    asset.pickNodes[id]=node;return id;
  };
  split(split,0,static_cast<uint32_t>(asset.pickTriangles.size()));
}

// Reuse the split topology for animated instances whose vertex positions change.
template<class Asset> void refit(Asset &asset){
  if(asset.pickNodes.empty()){build(asset);return;}
  for(size_t i=asset.pickNodes.size();i-->0;){
    auto &node=asset.pickNodes[i];
    for(int axis=0;axis<3;axis++){node.low[axis]=std::numeric_limits<float>::max();node.high[axis]=-std::numeric_limits<float>::max();}
    if(node.count){
      for(uint32_t j=node.first;j<node.first+node.count;j++){
        const auto &triangle=asset.pickTriangles[j];const float *v=asset.primitives[triangle.primitive].vertices.data()+triangle.offset;
        for(int axis=0;axis<3;axis++)for(int p=0;p<3;p++){node.low[axis]=std::min(node.low[axis],v[p*5+axis]);node.high[axis]=std::max(node.high[axis],v[p*5+axis]);}
      }
      for(int axis=0;axis<3;axis++){node.low[axis]-=1e-6f;node.high[axis]+=1e-6f;}
    }else for(int axis=0;axis<3;axis++){node.low[axis]=std::min(asset.pickNodes[node.left].low[axis],asset.pickNodes[node.right].low[axis]);node.high[axis]=std::max(asset.pickNodes[node.left].high[axis],asset.pickNodes[node.right].high[axis]);}
  }
}

inline bool bounds(const float origin[3],const float direction[3],const float low[3],const float high[3],float limit,float &near){
  near=0;float far=limit;
  for(int axis=0;axis<3;axis++){
    if(direction[axis]==0){if(origin[axis]<low[axis]||origin[axis]>high[axis])return false;}
    else{
      float a=(low[axis]-origin[axis])/direction[axis],b=(high[axis]-origin[axis])/direction[axis];
      if(a>b)std::swap(a,b);
      near=std::max(near,a);far=std::min(far,b);
      if(near>far)return false;
    }
  }
  return true;
}

template<class Asset,class TriangleHit> float intersect(const Asset &asset,const float origin[3],const float direction[3],TriangleHit triangleHit){
  float best=std::numeric_limits<float>::max();
  if(asset.pickNodes.empty())return best;
  struct Entry {uint32_t node;float near;};
  // Median splits halve triangle counts: uint32_t counts imply depth <=32.
  std::array<Entry,64> stack;size_t size=0;float near;
  const auto &root=asset.pickNodes[0];
  if(!bounds(origin,direction,root.low,root.high,best,near))return best;
  stack[size++]={0,near};
  while(size){
    const auto entry=stack[--size];if(entry.near>best)continue;
    const auto &node=asset.pickNodes[entry.node];
    if(node.count){
      for(uint32_t i=node.first;i<node.first+node.count;i++){
        const auto &triangle=asset.pickTriangles[i];
        const float *v=asset.primitives[triangle.primitive].vertices.data()+triangle.offset;
        float distance;if(triangleHit(origin,direction,v,v+5,v+10,distance))best=std::min(best,distance);
      }
    }else{
      Entry a{node.left,0},b{node.right,0};
      const auto &left=asset.pickNodes[a.node];const auto &right=asset.pickNodes[b.node];
      const bool hitA=bounds(origin,direction,left.low,left.high,best,a.near);
      const bool hitB=bounds(origin,direction,right.low,right.high,best,b.near);
      if(hitA&&hitB){if(a.near>b.near)std::swap(a,b);stack[size++]=b;stack[size++]=a;}
      else if(hitA)stack[size++]=a;else if(hitB)stack[size++]=b;
    }
  }
  return best;
}
} // namespace picking
