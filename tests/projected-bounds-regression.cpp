// Actual scene projection code, checked against its pre-cache implementation.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cstdlib>
#include <random>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static ProjectedBounds referenceBounds(const Object &object,const ModelAsset &geometry,const vr::HmdMatrix34_t &anchor,const float eye[3],const float right[3],const float up[3],const float forward[3]){
  ProjectedBounds result;float rotation[9];objectRotationMatrix(object.pitch,object.yaw,object.roll,rotation);
  for(int corner=0;corner<8;corner++){
    float local[3],rotated[3]{},position[3]={object.x,object.y,object.z};
    for(int axis=0;axis<3;axis++)local[axis]=(corner&(1<<axis))?geometry.high[axis]:geometry.low[axis];
    for(int row=0;row<3;row++)for(int axis=0;axis<3;axis++)rotated[row]+=rotation[row*3+axis]*local[axis];
    for(int axis=0;axis<3;axis++)position[axis]+=object.scale*rotated[axis]*(axis==2?-1.f:1.f);
    float ray[3];for(int row=0;row<3;row++)ray[row]=anchor.m[row][3]+anchor.m[row][0]*position[0]+anchor.m[row][1]*position[1]-anchor.m[row][2]*position[2]-eye[row];
    float x=0,y=0,d=0;for(int axis=0;axis<3;axis++){x+=ray[axis]*right[axis];y+=ray[axis]*up[axis];d+=ray[axis]*forward[axis];}
    if(d<=.05f){result.valid=false;return result;}
    result.x0=std::min(result.x0,x/d);result.x1=std::max(result.x1,x/d);result.y0=std::min(result.y0,y/d);result.y1=std::max(result.y1,y/d);
    result.near=std::min(result.near,d);result.far=std::max(result.far,d);
  }
  return result;
}

static void equalBounds(const ProjectedBounds &a,const ProjectedBounds &b){
  CHECK(a.valid==b.valid);if(!a.valid)return;
  const float av[]={a.x0,a.x1,a.y0,a.y1,a.near,a.far},bv[]={b.x0,b.x1,b.y0,b.y1,b.near,b.far};
  for(int i=0;i<6;i++)CHECK(std::abs(av[i]-bv[i])<=2e-5f*std::max(1.f,std::abs(bv[i])));
}
static vr::HmdMatrix34_t identity(){vr::HmdMatrix34_t m{};for(int axis=0;axis<3;axis++)m.m[axis][axis]=1;return m;}
static const ModelAsset &geometry(const Scene &scene,const Object &object){return object.animationPose?object.animationPose->geometry:scene.assets[object.asset-1];}
static void correctness(Scene &scene){
  std::mt19937 random(9327);std::uniform_real_distribution<float> unit(-1,1);
  FrameBoundsCache cache;int cases=0;
  for(int frame=0;frame<20;frame++){
    if(frame==2){scene.objects.erase(scene.objects.begin()+5);scene.objects.push_back({1000,1,0,0,0,.1f,0});}
    if(frame==3)for(int i=0;i<80;i++)scene.objects.push_back({1001+i,1,0,0,0,.1f,0});
    if(frame==4)scene.objects[0].animationPose=std::make_shared<ModelPose>();
    for(auto &object:scene.objects){
      object.x=unit(random)*.7f;object.y=unit(random)*.7f;object.z=unit(random)*.8f;
      object.pitch=unit(random)*3;object.yaw=unit(random)*3;object.roll=unit(random)*3;object.scale=.05f+.2f*std::abs(unit(random));
      object.visible=(object.id+frame)%7!=0;object.renderGroup=frame==19?0:1+object.id%4;
      if(object.animationPose)for(int axis=0;axis<3;axis++){object.animationPose->geometry.low[axis]=-.3f-axis*.1f;object.animationPose->geometry.high[axis]=.4f+frame*.01f;}
    }
    auto anchor=identity(),head=identity();float rotation[9];objectRotationMatrix(.1f,.3f,-.1f,rotation);
    for(int row=0;row<3;row++){for(int col=0;col<3;col++)anchor.m[row][col]=rotation[row*3+col];anchor.m[row][3]=float(row-1);head.m[row][3]=anchor.m[row][3]+anchor.m[row][2]*(1.2f+.1f*unit(random));}
    float eyes[2][3];for(int eye=0;eye<2;eye++)for(int axis=0;axis<3;axis++)eyes[eye][axis]=head.m[axis][3]+anchor.m[axis][0]*(eye?.032f:-.032f);
    cache.update(scene,anchor,head,eyes);
    auto common=anchor;float r[3],u[3],f[3];faceViewer(common,anchor,head,r,u,f);
    auto other=anchor;other.m[0][3]+=.5f;float r2[3],u2[3],f2[3];faceViewer(other,anchor,head,r2,u2,f2);
    std::vector<const Object*> members;
    for(const auto &object:scene.objects)if(object.visible){
      if(object.renderGroup==1)members.push_back(&object);
      for(int eye=0;eye<2;eye++){
        const auto expected=referenceBounds(object,geometry(scene,object),anchor,eyes[eye],r,u,f);
        const auto actual=cache.project(object,eyes[eye],r,u,f);equalBounds(actual,expected);
        if(frame!=19)equalBounds(cache.common(object,eye),expected);
        equalBounds(cache.project(object,eyes[eye],r2,u2,f2),referenceBounds(object,geometry(scene,object),anchor,eyes[eye],r2,u2,f2));
        const float center[3]={common.m[0][3],common.m[1][3],common.m[2][3]};
        auto a=projectedScissor(actual,center,eyes[eye],r,u,f,.8f,768),b=projectedScissor(expected,center,eyes[eye],r,u,f,.8f,768);
        CHECK(a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height);cases++;
      }
    }
    if(!members.empty()){
      const auto a=groupPlane(members,scene,anchor,head,eyes),b=groupPlane(members,scene,anchor,head,eyes,&cache);
      for(int row=0;row<3;row++)for(int col=0;col<4;col++)CHECK(std::abs(a.transform.m[row][col]-b.transform.m[row][col])<1e-5f);
      const float center[3]={a.transform.m[0][3],a.transform.m[1][3],a.transform.m[2][3]};
      auto old=groupLayout(members,scene,anchor,head,eyes,center,a.right,a.up,a.forward,2);
      auto cached=groupLayout(members,scene,anchor,head,eyes,center,b.right,b.up,b.forward,2,4096,&cache);
      CHECK(old.pixels==cached.pixels&&old.limited==cached.limited&&std::abs(old.width-cached.width)<1e-5f);
      for(int eye=0;eye<2;eye++)equalBounds(old.bounds[eye],cached.bounds[eye]);
    }
  }
  // Near-plane crossing, scene replacement, and return to a grouped snapshot.
  Scene replacement;replacement.assets.resize(1);for(int axis=0;axis<3;axis++){replacement.assets[0].low[axis]=-.5f;replacement.assets[0].high[axis]=.5f;}
  replacement.objects.push_back({1,1,0,0,-.98f,.1f,0});replacement.objects[0].renderGroup=1;
  auto anchor=identity(),head=identity();head.m[2][3]=1;float eyes[2][3]={{-.032f,0,1},{.032f,0,1}};
  cache.update(replacement,anchor,head,eyes);CHECK(!cache.common(replacement.objects[0],0).valid);
  replacement.objects.clear();cache.update(replacement,anchor,head,eyes);CHECK(cache.entries.empty());
  printf("PASS frame bounds: %d stereo comparisons, independent planes, rotated anchors, animation bounds, visibility, erasure/growth, scene replacement and near-plane fallback\n",cases);
}
static volatile double sink;
static void benchmark(Scene &scene){
  scene.objects.clear();for(int i=0;i<240;i++){scene.objects.push_back({i+1,1+i%3,((i%8)-3.5f)*.12f,-.25f+(i/64)*.15f,(i/8%8)*.12f,.1f,.2f});scene.objects.back().renderGroup=1+i%4;}
  auto anchor=identity(),head=identity();head.m[1][3]=.15f;head.m[2][3]=1;
  const float eyes[2][3]={{-.032f,.15f,1},{.032f,.15f,1}};auto common=anchor;float r[3],u[3],f[3];faceViewer(common,anchor,head,r,u,f);
  FrameBoundsCache cache;
  for(int round=0;round<3;round++)for(int mode=0;mode<2;mode++){
    const bool cached=round%2?mode==0:mode==1;
    auto work=[&]{
      double sum=0;if(cached)cache.update(scene,anchor,head,eyes);
      for(int group=0;group<5;group++)for(int eye=0;eye<2;eye++)for(const auto &object:scene.objects){
        const auto bounds=cached?cache.common(object,eye):referenceBounds(object,geometry(scene,object),anchor,eyes[eye],r,u,f);
        if(bounds.valid)sum+=bounds.x0+bounds.y0+bounds.near;
      }
      sink=sum;
    };
    for(int i=0;i<20;i++)work();
    auto start=std::chrono::steady_clock::now();for(int i=0;i<200;i++)work();
    double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/200;
    printf("BOUNDS_BENCH round=%d cached=%d objects=240 groups=5 stereo=1 frame_us=%.3f\n",round,int(cached),us);
  }
}
int main(){
  Scene scene;for(const char *path:{"assets/samples/Box/Box.glb","assets/samples/ToyCar/ToyCar.glb","assets/Avocado.glb"}){ModelAsset asset;CHECK(loadModelFile(path,asset));scene.assets.push_back(std::move(asset));}
  for(int i=0;i<32;i++)scene.objects.push_back({i+1,1+i%3,0,0,0,.1f,0});
  correctness(scene);benchmark(scene);
}
