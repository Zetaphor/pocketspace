#pragma once
// Test-only conservative rejection of nonmember primitive occluders.
class PrimitiveVisibilityExperiment {
public:
  struct Bounds {float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};bool valid=true;};
  static Bounds bounds(const PrimitiveAsset &part){
    Bounds b;if(part.vertices.empty()||part.vertices.size()%5)b.valid=false;
    for(size_t n=0;n+4<part.vertices.size();n+=5)for(int axis=0;axis<3;axis++){
      const float v=part.vertices[n+axis];if(!std::isfinite(v))b.valid=false;
      b.low[axis]=std::min(b.low[axis],v);b.high[axis]=std::max(b.high[axis],v);
    }
    return b;
  }
private:
  struct Entry {WorldBounds world;ProjectedBounds common[2];bool valid=false;};
  std::vector<std::vector<Bounds>> assets;
  std::vector<std::vector<Entry>> entries;
  const Scene *scene=nullptr;
  uint64_t frames=0,candidates=0,skipped=0,triangles=0;
  double prepareMs=0;
  using Clock=std::chrono::steady_clock;
  Clock::time_point epoch=Clock::now(),window=epoch;
public:
  void reset(){assets.clear();entries.clear();scene=nullptr;}
  void update(const Scene &source,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3]){
    const auto start=Clock::now();
    scene=&source;entries.resize(source.objects.size());++frames;
    while(assets.size()<source.assets.size()){
      auto &parts=assets.emplace_back();for(const auto &p:source.assets[assets.size()-1].primitives)parts.push_back(bounds(p));
    }
    auto plane=anchor;float r[3],u[3],f[3];faceViewer(plane,anchor,head,r,u,f);
    for(size_t n=0;n<source.objects.size();n++){
      auto &out=entries[n];out.clear();const auto &object=source.objects[n];
      const auto &asset=source.assets[size_t(object.asset)-1];
      // Deforming geometry keeps the baseline path rather than stale bind bounds.
      if(!object.visible||object.animationPose||asset.hasAnimation||asset.hasSkin||asset.hasMorph||asset.primitives.size()<2)continue;
      for(const auto &b:assets[size_t(object.asset)-1]){
        auto &entry=out.emplace_back();entry.valid=b.valid;if(!b.valid)continue;
        entry.world=worldBounds(object,b,anchor);
        for(int eye=0;eye<2;eye++)entry.common[eye]=projectWorldBounds(entry.world,eyes[eye],r,u,f);
      }
    }
    prepareMs+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  }
  uint64_t rejected()const{return skipped;}
  bool mayDraw(const Object &object,size_t primitive,const ProjectedBounds &target,int eyeIndex,bool common,const float eye[3],const float r[3],const float u[3],const float f[3],float halfWidth,int pixels){
    ++candidates;const auto &parts=entries[size_t(&object-scene->objects.data())];
    if(primitive>=parts.size()||!parts[primitive].valid||!target.valid)return true;
    auto b=common?parts[primitive].common[eyeIndex]:projectWorldBounds(parts[primitive].world,eye,r,u,f);
    // Guard near-plane and invalid data. Expand by >= two texels in angular
    // space to retain raster edges beyond the exact mathematical bounds.
    auto finite=[](const ProjectedBounds &v){return std::isfinite(v.x0)&&std::isfinite(v.x1)&&std::isfinite(v.y0)&&std::isfinite(v.y1)&&std::isfinite(v.near)&&std::isfinite(v.far);};
    if(!b.valid||!finite(b)||!finite(target)||!std::isfinite(halfWidth)||pixels<=0)return true;
    const float margin=std::max(1e-4f,4*halfWidth/(pixels*std::min(b.near,target.near)));
    b.x0-=margin;b.x1+=margin;b.y0-=margin;b.y1+=margin;
    if(mayOcclude(b,target))return true;
    ++skipped;triangles+=scene->assets[size_t(object.asset)-1].primitives[primitive].vertexCount/3;return false;
  }
  void report(){
    const auto now=Clock::now();if(now-window<std::chrono::seconds(1))return;
    printf("PRIMITIVE_VISIBILITY_SAMPLE {\"seconds\":%.6f,\"frames\":%llu,\"candidates\":%llu,\"skipped\":%llu,\"trianglesSkipped\":%llu,\"prepareMsPerFrame\":%.6f}\n",
      std::chrono::duration<double>(now-epoch).count(),(unsigned long long)frames,(unsigned long long)candidates,(unsigned long long)skipped,(unsigned long long)triangles,frames?prepareMs/frames:0);fflush(stdout);
    window=now;frames=candidates=skipped=triangles=0;prepareMs=0;
  }
};
