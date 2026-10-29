#pragma once
// Equal-allocation tie-break only; no API/default or layout density change.
inline bool groupOccluderTieBreak=false;
inline uint64_t groupOccluderPredictedTriangles(const GroupSplitPlan &plan,const GroupLayout (&layouts)[2],const Scene &scene,const FrameBoundsCache &cache){
  uint64_t total=0;
  for(int tile=0;tile<2;tile++){
    std::set<int> members;for(const auto *object:plan.members[tile])members.insert(object->id);
    for(const auto &object:scene.objects)if(object.visible&&!members.count(object.id)){
      uint64_t triangles=0;
      for(const auto &part:scene.assets[size_t(object.asset)-1].primitives)if(part.alphaMode!=2)
        triangles+=uint64_t(modelDrawVertexCount(part,object.animationPose?0:object.lodLevel))/3;
      if(!triangles)continue;
      for(int eye=0;eye<2;eye++)if(mayOcclude(cache.common(object,eye),layouts[tile].bounds[eye]))total+=triangles;
    }
  }
  return total;
}
