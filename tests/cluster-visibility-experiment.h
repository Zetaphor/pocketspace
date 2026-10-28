#pragma once
#include <numeric>
// Test-only depth-occluder clusters. Color draws and source buffers never change.
class ClusterVisibilityExperiment {
public:
  enum Mode {Off,Original,Full,Cull};
  Mode mode=Off;
  using Bounds=PrimitiveVisibilityExperiment::Bounds;
private:
  struct Cluster {Bounds bounds;unsigned first=0,count=0;};
  struct Part {GLuint index=0;GLenum type=GL_UNSIGNED_INT;unsigned count=0;std::vector<Cluster> clusters;};
  struct Projection {WorldBounds world;ProjectedBounds common[2];};
  std::vector<std::vector<Part>> assets;
  std::vector<std::vector<std::vector<Projection>>> entries;
  const Scene *scene=nullptr;
  using Clock=std::chrono::steady_clock;
  Clock::time_point epoch=Clock::now(),window=epoch;
  uint64_t frames=0,eligible=0,rejected=0,rangeDraws=0,indexBytes=0,totalRejected=0;
  double prepareMs=0;
  unsigned leafTriangles=4096;
  template<class T> static bool read(GLenum target,GLuint buffer,std::vector<T> &out,size_t size){
    glBindBuffer(target,buffer);const void *data=glMapBufferRange(target,0,size*sizeof(T),GL_MAP_READ_BIT);
    if(!data)return false;out.resize(size);std::memcpy(out.data(),data,size*sizeof(T));return glUnmapBuffer(target)==GL_TRUE;
  }
  Part build(const PrimitiveAsset &source){
    Part result;if(!source.vbo||source.vertexCount<3||source.vertexCount%3||source.alphaMode==2)return result;
    std::vector<float> vertices;if(!read(GL_ARRAY_BUFFER,source.vbo,vertices,size_t(source.gpuVertexCount)*5))return result;
    std::vector<unsigned> indices;
    if(source.indexBuffer){
      if(source.indexType==GL_UNSIGNED_SHORT){std::vector<uint16_t> data;if(!read(GL_ELEMENT_ARRAY_BUFFER,source.indexBuffer,data,source.vertexCount))return result;indices.assign(data.begin(),data.end());}
      else if(!read(GL_ELEMENT_ARRAY_BUFFER,source.indexBuffer,indices,source.vertexCount))return result;
    }else{indices.resize(source.vertexCount);std::iota(indices.begin(),indices.end(),0u);}
    std::vector<std::array<float,3>> centers(indices.size()/3);
    for(size_t t=0;t<centers.size();t++)for(int lane=0;lane<3;lane++){
      const auto index=indices[t*3+lane];if(index>=vertices.size()/5)return result;
      for(int axis=0;axis<3;axis++){const float v=vertices[index*5+axis];if(!std::isfinite(v))return result;centers[t][axis]+=v/3;}
    }
    std::vector<unsigned> triangles(centers.size()),ordered;std::iota(triangles.begin(),triangles.end(),0u);ordered.reserve(indices.size());
    auto partition=[&](auto &&self,size_t begin,size_t end)->void{
      Bounds bounds,centroid;
      for(size_t n=begin;n<end;n++){
        const auto t=triangles[n];for(int axis=0;axis<3;axis++){
          centroid.low[axis]=std::min(centroid.low[axis],centers[t][axis]);centroid.high[axis]=std::max(centroid.high[axis],centers[t][axis]);
          for(int lane=0;lane<3;lane++){const float v=vertices[indices[t*3+lane]*5+axis];bounds.low[axis]=std::min(bounds.low[axis],v);bounds.high[axis]=std::max(bounds.high[axis],v);}
        }
      }
      if(end-begin<=leafTriangles){
        Cluster c;c.bounds=bounds;c.first=unsigned(ordered.size());
        for(size_t n=begin;n<end;n++)for(int lane=0;lane<3;lane++)ordered.push_back(indices[triangles[n]*3+lane]);
        c.count=unsigned(ordered.size())-c.first;result.clusters.push_back(c);return;
      }
      int axis=0;for(int i=1;i<3;i++)if(centroid.high[i]-centroid.low[i]>centroid.high[axis]-centroid.low[axis])axis=i;
      const size_t middle=(begin+end)/2;
      std::nth_element(triangles.begin()+begin,triangles.begin()+middle,triangles.begin()+end,[&](unsigned a,unsigned b){return centers[a][axis]<centers[b][axis];});
      self(self,begin,middle);self(self,middle,end);
    };
    partition(partition,0,triangles.size());result.count=unsigned(ordered.size());
    result.type=source.indexBuffer?source.indexType:(source.gpuVertexCount<=65536?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT);
    glGenBuffers(1,&result.index);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,result.index);
    if(result.type==GL_UNSIGNED_SHORT){std::vector<uint16_t> packed(ordered.begin(),ordered.end());glBufferData(GL_ELEMENT_ARRAY_BUFFER,packed.size()*2,packed.data(),GL_STATIC_DRAW);indexBytes+=packed.size()*2;}
    else{glBufferData(GL_ELEMENT_ARRAY_BUFFER,ordered.size()*4,ordered.data(),GL_STATIC_DRAW);indexBytes+=ordered.size()*4;}
    // Check the uploaded stream, including triangle orientation and duplicates.
    std::vector<unsigned> uploaded;
    if(result.type==GL_UNSIGNED_SHORT){std::vector<uint16_t> packed;if(!read(GL_ELEMENT_ARRAY_BUFFER,result.index,packed,ordered.size()))std::exit(1);uploaded.assign(packed.begin(),packed.end());}
    else if(!read(GL_ELEMENT_ARRAY_BUFFER,result.index,uploaded,ordered.size()))std::exit(1);
    auto canonical=[](const std::vector<unsigned> &data){std::vector<std::array<unsigned,3>> out;for(size_t n=0;n<data.size();n+=3)out.push_back({data[n],data[n+1],data[n+2]});std::sort(out.begin(),out.end());return out;};
    if(canonical(indices)!=canonical(uploaded)){fprintf(stderr,"cluster triangle preservation failed\n");std::exit(1);}
    printf("CLUSTER_BUILD triangles=%zu clusters=%zu index_bytes=%zu verified=1\n",indices.size()/3,result.clusters.size(),ordered.size()*(result.type==GL_UNSIGNED_SHORT?2:4));
    return result;
  }
public:
  explicit ClusterVisibilityExperiment(Mode initial=Off,unsigned leaf=4096):mode(initial),leafTriangles(std::max(1u,leaf)){}
  ~ClusterVisibilityExperiment(){reset();}
  void reset(){for(auto &asset:assets)for(auto &part:asset)if(part.index)glDeleteBuffers(1,&part.index);assets.clear();entries.clear();scene=nullptr;indexBytes=0;}
  uint64_t skipped()const{return totalRejected;}
  std::vector<GLuint> buffers()const{std::vector<GLuint> result;for(const auto &asset:assets)for(const auto &part:asset)if(part.index)result.push_back(part.index);return result;}
  void update(const Scene &source,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3]){
    const auto start=Clock::now();scene=&source;++frames;
    GLint oldArray=0,oldElement=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&oldElement);
    while(assets.size()<source.assets.size()){
      const auto &asset=source.assets[assets.size()];auto &parts=assets.emplace_back();
      for(const auto &part:asset.primitives)parts.push_back(asset.hasAnimation||asset.hasSkin||asset.hasMorph?Part{}:build(part));
    }
    glBindBuffer(GL_ARRAY_BUFFER,oldArray);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,oldElement);
    if(mode==Cull){
      entries.resize(source.objects.size());auto plane=anchor;float r[3],u[3],f[3];faceViewer(plane,anchor,head,r,u,f);
      for(size_t n=0;n<source.objects.size();n++){
        const auto &object=source.objects[n];auto &out=entries[n];out.clear();if(!object.visible||object.animationPose)continue;
        for(const auto &part:assets[size_t(object.asset)-1]){
          auto &projections=out.emplace_back();for(const auto &cluster:part.clusters){
            auto &entry=projections.emplace_back();entry.world=worldBounds(object,cluster.bounds,anchor);
            for(int eye=0;eye<2;eye++)entry.common[eye]=projectWorldBounds(entry.world,eyes[eye],r,u,f);
          }
        }
      }
    }
    prepareMs+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  }
  template<class Count> bool draw(const Object &object,size_t primitive,const ProjectedBounds &target,int eyeIndex,bool common,const float eye[3],const float r[3],const float u[3],const float f[3],float halfWidth,int pixels,Count count){
    if(mode!=Full&&mode!=Cull)return false;
    const auto &part=assets[size_t(object.asset)-1][primitive];if(!part.index||object.animationPose)return false;
    const auto &source=scene->assets[size_t(object.asset)-1].primitives[primitive];eligible+=part.count/3;
    glBindBuffer(GL_ARRAY_BUFFER,source.vbo);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.index);
    auto range=[&](unsigned first,unsigned size){if(size){glDrawElements(GL_TRIANGLES,size,part.type,reinterpret_cast<void*>(uintptr_t(first)*(part.type==GL_UNSIGNED_SHORT?2:4)));count(size);++rangeDraws;}};
    if(mode==Full){range(0,part.count);return true;}
    unsigned first=0,size=0;
    const auto &projections=entries[size_t(&object-scene->objects.data())][primitive];
    auto finite=[](const ProjectedBounds &b){return b.valid&&std::isfinite(b.x0)&&std::isfinite(b.x1)&&std::isfinite(b.y0)&&std::isfinite(b.y1)&&std::isfinite(b.near)&&std::isfinite(b.far);};
    for(size_t n=0;n<part.clusters.size();n++){
      const auto &cluster=part.clusters[n];auto b=common?projections[n].common[eyeIndex]:projectWorldBounds(projections[n].world,eye,r,u,f);
      bool visible=true;
      if(finite(b)&&finite(target)&&pixels>0&&std::isfinite(halfWidth)){
        const float margin=std::max(1e-4f,4*halfWidth/(pixels*std::min(b.near,target.near)));b.x0-=margin;b.x1+=margin;b.y0-=margin;b.y1+=margin;visible=mayOcclude(b,target);
      }
      if(visible){if(!size)first=cluster.first;size+=cluster.count;}
      else{range(first,size);size=0;rejected+=cluster.count/3;totalRejected+=cluster.count/3;}
    }
    range(first,size);return true;
  }
  void report(){
    const auto now=Clock::now();if(now-window<std::chrono::seconds(1))return;
    printf("CLUSTER_VISIBILITY_SAMPLE {\"seconds\":%.6f,\"mode\":%d,\"frames\":%llu,\"eligibleTriangles\":%llu,\"skippedTriangles\":%llu,\"rangeDraws\":%llu,\"indexBytes\":%llu,\"prepareMsPerFrame\":%.6f}\n",std::chrono::duration<double>(now-epoch).count(),int(mode),(unsigned long long)frames,(unsigned long long)eligible,(unsigned long long)rejected,(unsigned long long)rangeDraws,(unsigned long long)indexBytes,frames?prepareMs/frames:0);fflush(stdout);
    window=now;frames=eligible=rejected=rangeDraws=0;prepareMs=0;
  }
};
