#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include "../src/model_lod.h"
static std::vector<unsigned char> read(GLenum target,GLuint buffer,size_t bytes){
  glBindBuffer(target,buffer);void *p=glMapBufferRange(target,0,bytes,GL_MAP_READ_BIT);assert(p);std::vector<unsigned char> result(bytes);std::memcpy(result.data(),p,bytes);assert(glUnmapBuffer(target));return result;
}
static std::shared_ptr<ModelPose> expanded(const ModelAsset &asset,int level){
  auto pose=std::make_shared<ModelPose>();pose->geometry.primitives.resize(asset.primitives.size());
  for(size_t n=0;n<asset.primitives.size();n++){
    const auto &p=asset.primitives[n];auto vertices=read(GL_ARRAY_BUFFER,p.vbo,size_t(p.gpuVertexCount)*5*sizeof(float));
    auto indices=read(GL_ELEMENT_ARRAY_BUFFER,modelIndexBuffer(p,level),size_t(modelDrawVertexCount(p,level))*(p.indexType==GL_UNSIGNED_SHORT?2:4));
    std::vector<float> output(size_t(p.vertexCount)*5);
    for(int i=0;i<modelDrawVertexCount(p,level);i++){uint32_t key=0;if(p.indexType==GL_UNSIGNED_SHORT){uint16_t small;std::memcpy(&small,indices.data()+i*2,2);key=small;}else std::memcpy(&key,indices.data()+i*4,4);assert(key<uint32_t(p.gpuVertexCount));std::memcpy(output.data()+size_t(i)*5,vertices.data()+size_t(key)*5*sizeof(float),5*sizeof(float));}
    auto &part=pose->geometry.primitives[n];part.vertexCount=p.vertexCount;glGenBuffers(1,&part.vbo);glBindBuffer(GL_ARRAY_BUFFER,part.vbo);glBufferData(GL_ARRAY_BUFFER,output.size()*sizeof(float),output.data(),GL_STATIC_DRAW);
  }
  return pose;
}
int main(){
  assert(modelLod::projectedDiameter(.1f,1000,2,1)>modelLod::projectedDiameter(.1f,1000,1,1));
  assert(!std::isfinite(modelLod::projectedDiameter(.1f,1000,1,.05f)));
  assert(modelLod::select(90,1,0)==2);assert(modelLod::select(120,1,2)==2);assert(modelLod::select(139,1,2)==1);
  assert(modelLod::select(260,1,0)==0);assert(modelLod::select(260,1,1)==1);assert(modelLod::select(346,1,1)==0);
  assert(modelLod::select(90,.36f,0)==0);assert(modelLod::select(90,.36f,2)==2);assert(modelLod::select(90,.34f,2)==0);assert(modelLod::select(INFINITY,1,2)==0);
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    Renderer r;ModelAsset car;assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",car));car.optimizeVertexCache=true;
    std::vector<std::vector<float>> cpu;for(auto &p:car.primitives)cpu.push_back(p.vertices);uploadModel(car);
    std::vector<GLuint> buffers;for(size_t n=0;n<car.primitives.size();n++){const auto &p=car.primitives[n];assert(p.vertices==cpu[n]);for(const auto &lod:p.lod){assert(lod.indexBuffer&&lod.count<p.vertexCount&&lod.error<=.001001f);buffers.push_back(lod.indexBuffer);}}
    r.scene.assets.push_back(std::move(car));const int key=r.scene.assets.size();auto &object=r.scene.objects[2];object.asset=key;object.y=-.2f;object.scale=.4f;int compared=0;
    for(int level:{1,2}){
      auto reference=expanded(r.scene.assets[key-1],level);
      for(int angle=0;angle<3;angle++)for(int eye=0;eye<2;eye++){
        object.lodLevel=level;object.yaw=angle*.6f;object.pitch=.2f;object.animationPose.reset();auto actual=r.renderSet({3},{0,.1f,-.7f},eye,0,true,1,true);
        object.animationPose=reference;auto expected=r.renderSet({3},{0,.1f,-.7f},eye,0,true,1,true);assert(actual==expected);++compared;
      }
      object.animationPose.reset();
    }
    for(int gridSize:{40,260}){
    ModelAsset grid;grid.low[0]=grid.low[1]=-.5f;grid.high[0]=grid.high[1]=.5f;grid.primitives.resize(1);auto &p=grid.primitives[0];p.rgba={100,220,160,255};
    for(int y=0;y<gridSize;y++)for(int x=0;x<gridSize;x++)for(auto corner:{std::pair{0,0},std::pair{1,0},std::pair{1,1},std::pair{0,0},std::pair{1,1},std::pair{0,1}}){float u=float(x+corner.first)/gridSize,v=float(y+corner.second)/gridSize;p.vertices.insert(p.vertices.end(),{u-.5f,v-.5f,0,u,v});}
    p.vertexCount=p.vertices.size()/5;ModelAsset cpuGrid=grid;uploadModel(grid);assert(p.lod[0].indexBuffer&&p.lod[1].indexBuffer);assert(p.indexType==(gridSize==40?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT));
    if(gridSize==40)for(int variant=0;variant<6;variant++){
      ModelAsset guarded=cpuGrid;if(variant==0)guarded.lod=false;if(variant==1)guarded.hasAnimation=true;if(variant==2)guarded.hasSkin=true;if(variant==3)guarded.hasMorph=true;if(variant>=4)guarded.primitives[0].alphaMode=variant-3;
      uploadModel(guarded);for(const auto &part:guarded.primitives)assert(!part.lod[0].indexBuffer&&!part.lod[1].indexBuffer);deleteModel(guarded);
    }r.scene.assets.push_back(std::move(grid));
    r.scene.objects.clear();for(int n=0;n<8;n++)r.scene.objects.push_back({n+1,int(r.scene.assets.size()),(n%4-1.5f)*.12f,-.2f+(n/4)*.13f,0,.1f,n*.1f});
    for(int level:{0,1,2})for(int eye=0;eye<2;eye++){
      for(auto &o:r.scene.objects)o.lodLevel=level;
      r.instanceExperiment.mode=ModelInstancing::Original;auto a=r.renderSet({1,2,3,4,5,6,7,8},{0,.1f,-.7f},eye,0,true,1,true);
      const auto batchesBefore=r.instanceExperiment.submittedBatches();
      r.instanceExperiment.mode=ModelInstancing::Instanced;auto b=r.renderSet({1,2,3,4,5,6,7,8},{0,.1f,-.7f},eye,0,true,1,true);assert(a==b);assert(r.instanceExperiment.submittedBatches()>batchesBefore);++compared;
    }
    const auto mixedBefore=r.instanceExperiment.submittedBatches();
    for(auto &o:r.scene.objects)o.lodLevel=o.id%3;
    r.instanceExperiment.mode=ModelInstancing::Original;auto a=r.renderSet({1,2,3,4,5,6,7,8},{0,.1f,-.7f},0,0,true,1,true);
    r.instanceExperiment.mode=ModelInstancing::Instanced;auto b=r.renderSet({1,2,3,4,5,6,7,8},{0,.1f,-.7f},0,0,true,1,true);assert(a==b);assert(r.instanceExperiment.submittedBatches()==mixedBefore);++compared;
    }
    deleteModel(r.scene.assets[key-1]);for(auto buffer:buffers)assert(!glIsBuffer(buffer));assert(glGetError()==GL_NO_ERROR);
    printf("PASS automatic LOD: policy/hysteresis/near restoration, %d exact indexed/expanded and instanced views, mixed-level barriers, 16/32-bit LOD indices, opt-out/material/deformation guards, full CPU meshes and buffer release\n",compared);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
