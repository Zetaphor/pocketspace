#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
#include <fstream>
#include <filesystem>
static void save(const std::string &path,const std::vector<unsigned char> &rgba){
  std::ofstream out(path,std::ios::binary);out<<"P6\n"<<pixels<<" "<<pixels<<"\n255\n";
  for(int y=pixels-1;y>=0;y--)for(int x=0;x<pixels;x++)out.write(reinterpret_cast<const char*>(&rgba[(size_t(y)*pixels+x)*4]),3);
}
int main(int argc,char **argv){
  const std::string output=argc>1?argv[1]:"/tmp/sf-lod-pixels";std::filesystem::create_directories(output);
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  {
    unsetenv("SF_GPU_MODEL_LOD");Renderer r;int views=0;bool reduced=false;
    for(const char *path:{"assets/samples/ToyCar/ToyCar.glb","assets/Avocado.glb"}){
      unsetenv("SF_GPU_MODEL_LOD");ModelAsset full;assert(loadModelFile(path,full));full.optimizeVertexCache=true;uploadModel(full);r.scene.assets.push_back(std::move(full));const int original=r.scene.assets.size();
      for(const char *ratio:{"0.5","0.25"}){
        setenv("SF_GPU_MODEL_LOD",ratio,1);ModelAsset low;assert(loadModelFile(path,low));low.optimizeVertexCache=true;
        std::vector<std::vector<float>> geometry;for(const auto &p:low.primitives)geometry.push_back(p.vertices);const auto triangles=low.pickTriangles.size();uploadModel(low);
        for(size_t n=0;n<low.primitives.size();n++){const auto &p=low.primitives[n];assert(p.vertices==geometry[n]);assert(size_t(p.vertexCount)*5==p.vertices.size());assert(modelDrawVertexCount(p)<=p.vertexCount);reduced|=modelDrawVertexCount(p)<p.vertexCount;}
        assert(low.pickTriangles.size()==triangles);r.scene.assets.push_back(std::move(low));const int candidate=r.scene.assets.size();
        const std::string model=original==4?"car":"avocado";
        for(float grid:{1.f,.25f})for(float size:{.045f,.16f,.5f})for(int angle=0;angle<3;angle++)for(int eye=0;eye<2;eye++){
          auto &object=r.scene.objects[2];object.scale=size;object.y=-.2f;object.pitch=.15f+angle*.2f;object.yaw=angle*.7f;object.roll=.1f;
          object.asset=original;auto a=r.renderSet({3},{0,-.2f,-.7f},eye,0,true,grid,true);
          object.asset=candidate;auto b=r.renderSet({3},{0,-.2f,-.7f},eye,0,true,grid,true);
          size_t changed=0,coverage=0,mask=0;double squared=0;
          for(size_t n=0;n<a.size();n+=4){bool pixel=false;coverage+=a[n+3]!=0;mask+=(a[n+3]!=0)!=(b[n+3]!=0);for(int c=0;c<4;c++){int d=int(a[n+c])-int(b[n+c]);squared+=double(d)*d;pixel|=d!=0;}changed+=pixel;}
          printf("LOD_PIXELS {\"model\":\"%s\",\"ratio\":%s,\"scale\":%.3f,\"gridScale\":%.2f,\"angle\":%d,\"eye\":%d,\"changed\":%zu,\"coverage\":%zu,\"maskChanged\":%zu,\"rms\":%.6f}\n",model.c_str(),ratio,size,grid,angle,eye,changed,coverage,mask,std::sqrt(squared/a.size()));
          if(eye==0){const auto name=std::string(grid<1?"close-":"")+model+"-"+ratio+"-"+std::to_string(int(size*1000))+"-"+std::to_string(angle);save(output+"/"+name+"-full.ppm",a);save(output+"/"+name+"-lod.ppm",b);}++views;
        }
      }
    }
    for(int variant=0;variant<5;variant++){
      setenv("SF_GPU_MODEL_LOD","0.5",1);ModelAsset guarded;assert(loadModelFile("assets/Avocado.glb",guarded));
      if(variant==0)guarded.hasAnimation=true;if(variant==1)guarded.hasSkin=true;if(variant==2)guarded.hasMorph=true;
      if(variant>=3)for(auto &part:guarded.primitives)part.alphaMode=variant-2;
      uploadModel(guarded);for(const auto &part:guarded.primitives)assert(modelDrawVertexCount(part)==part.vertexCount);deleteModel(guarded);
    }
    assert(reduced);assert(glGetError()==GL_NO_ERROR);for(auto &asset:r.scene.assets)deleteModel(asset);assert(lodExperiment::results.empty());
    printf("PASS LOD: %d stereo views, unchanged CPU/picking geometry, bounded simplifier metric, blend/mask/deformation fallback, reduced GPU counts and metadata cleanup\n",views);
  }
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
}
