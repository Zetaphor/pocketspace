// Generated shape geometry, createShapeAsset validation and lifetime, and real GLES drawing.
#include "render_harness.h"
#include <map>
#include <tuple>
using shapeMesh::Shape;using shapeMesh::V;
static float length(V v){return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
static V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
static V sub(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
static void geometry(){
  struct Case {Shape shape;V size;size_t triangles;};
  const Case cases[]={
    {Shape::Cube,{.3f,.2f,.4f},12},{Shape::Sphere,{.3f,.2f,.4f},528},{Shape::Cylinder,{.3f,.2f,.4f},96},
    {Shape::Cone,{.3f,.2f,.4f},48},{Shape::Wedge,{.3f,.2f,.4f},8},{Shape::Pyramid,{.3f,.2f,.4f},6},
    {Shape::Torus,{.3f,.08f,.4f},576},{Shape::Arch,{.6f,.3f,.08f},196}};
  for(const auto &c:cases){
    const auto vertices=shapeMesh::generate(c.shape,c.size);
    assert(vertices.size()==c.triangles*3);
    V low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};
    for(const auto &v:vertices){assert(std::abs(length(v.normal)-1)<1e-5f);for(int a=0;a<3;a++){low[a]=std::min(low[a],v.position[a]);high[a]=std::max(high[a],v.position[a]);}}
    // Each shape fills its box exactly.
    for(int a=0;a<3;a++)assert(std::abs(low[a]+c.size[a]*.5f)<1e-5f&&std::abs(high[a]-c.size[a]*.5f)<1e-5f);
    // Closed and consistently wound: every directed edge appears once and its reverse once.
    std::map<std::tuple<long,long,long,long,long,long>,int> edges;
    auto key=[](V p){return std::tuple<long,long,long>{std::lround(p[0]*1e4f),std::lround(p[1]*1e4f),std::lround(p[2]*1e4f)};};
    for(size_t t=0;t<vertices.size();t+=3){
      const V face=cross(sub(vertices[t+1].position,vertices[t].position),sub(vertices[t+2].position,vertices[t].position));
      for(int k=0;k<3;k++)assert(face[0]*vertices[t+k].normal[0]+face[1]*vertices[t+k].normal[1]+face[2]*vertices[t+k].normal[2]>0);
      for(int k=0;k<3;k++){auto a=key(vertices[t+k].position),b=key(vertices[t+(k+1)%3].position);++edges[std::tuple_cat(a,b)];}
    }
    for(const auto &[edge,count]:edges){
      assert(count==1);
      const auto reverse=std::make_tuple(std::get<3>(edge),std::get<4>(edge),std::get<5>(edge),std::get<0>(edge),std::get<1>(edge),std::get<2>(edge));
      assert(edges.count(reverse)&&edges.at(reverse)==1);
    }
  }
  assert(shapeMesh::sizeError(Shape::Torus,{.3f,.3f,.4f})&&!shapeMesh::sizeError(Shape::Torus,{.3f,.29f,.4f}));
  assert(shapeMesh::sizeError(Shape::Arch,{.6f,.3f,.3f})&&shapeMesh::sizeError(Shape::Arch,{.6f,.05f,.06f})&&!shapeMesh::sizeError(Shape::Arch,{.6f,.3f,.29f}));
  puts("PASS shape geometry: eight shapes fill their boxes, unit normals, closed consistent winding, triangle counts and thickness limits");
}
int main(){
  geometry();
  EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
  const auto dir=std::filesystem::temp_directory_path()/("ps-shapes-"+std::to_string(getpid()));std::filesystem::create_directory(dir);
  scenePersistence::directory=dir.string();ScopeExit cleanup{[&]{std::filesystem::remove_all(dir);}};
  const auto script=dir/"shapes.js";
  std::ofstream(script)<<R"JS(
    function check(v,message='shape asset assertion'){if(!v)throw Error(message);}
    function fails(fn){let caught=false;try{fn();}catch(_){caught=true;}check(caught,'expected rejection');}
    function near(a,b){return Math.abs(a-b)<1e-5;}
    // A rotated, stretched cube: extent and center are in meters in the parts' frame.
    const cube=createShapeAsset([{shape:'cube',position:[1,2,3],rotation:[0,Math.PI/2,0],scale:[.2,.4,.6],color:[1,0,0],finish:'glossy'}]);
    check(Object.keys(cube).join()==='assetId,extent,center'&&near(cube.extent,.6));
    check(near(cube.center.x,1)&&near(cube.center.y,2)&&near(cube.center.z,3));
    // Defaults: origin, no rotation, 0.1 m, white matte.
    const plain=createShapeAsset([{shape:'sphere'}]);check(near(plain.extent,.1)&&near(plain.center.x,0));
    // Parts with the same color and finish share a primitive.
    globalThis.thing=createShapeAsset([
      {shape:'cube',position:[0,0,0],scale:[.4,.05,.4],color:[.8,.6,.3]},
      {shape:'cylinder',position:[-.15,-.2,-.15],scale:[.04,.35,.04],color:[.8,.6,.3]},
      {shape:'cylinder',position:[.15,-.2,.15],scale:[.04,.35,.04],color:[.8,.6,.3]},
      {shape:'sphere',position:[0,.1,0],scale:[.12,.12,.12],color:[.1,.4,.9],finish:'metal'},
      {shape:'torus',position:[0,.2,0],rotation:[Math.PI/2,0,0],scale:[.2,.02,.2],color:[1,1,0],finish:'glow'},
      {shape:'arch',position:[.1,0,-.1],scale:[.2,.1,.03]},{shape:'cone',position:[-.1,.1,.1]},
      {shape:'wedge',position:[.1,.1,.1]},{shape:'pyramid',position:[-.1,.1,-.1]}]);
    check(near(thing.extent,.675)&&near(thing.center.y,-.0375)&&near(thing.center.x,0));
    globalThis.thingObject=createObject(thing.assetId,thing.center.x,thing.center.y,thing.center.z,thing.extent);
    // Validation leaves the scene unchanged.
    const before=loadModel('assets/samples/Box/Box.glb');
    for(const bad of [undefined,{},[],'cube',Array(257).fill({shape:'cube'}),
      [{shape:'ball'}],[{shape:'cube',finish:'shiny'}],[{shape:1}],[{}],[null],[[1,2,3]],
      [{shape:'cube',scale:[0,1,1]}],[{shape:'cube',scale:[.1,2.1,.1]}],[{shape:'cube',scale:[.1,.1]}],[{shape:'cube',scale:{x:1,y:1,z:1}}],
      [{shape:'cube',position:[0,10.5,0]}],[{shape:'cube',position:[0,NaN,0]}],[{shape:'cube',rotation:[0,Infinity,0]}],
      [{shape:'cube',color:[1.1,0,0]}],[{shape:'cube',color:[-.1,0,0]}],[{shape:'cube',color:['1',0,0]}],
      [{shape:'torus',scale:[.2,.2,.3]}],[{shape:'arch',scale:[.2,.3,.1]}],[{shape:'arch',scale:[.4,.05,.06]}],
      Array.from({length:65},(_,i)=>({shape:'cube',color:[i/64,0,0]}))])fails(()=>createShapeAsset(bad));
    for(const options of [null,1,{eyePixels:300},{eyePixels:4096},{lod:1},{gpuInstancing:'yes'},{optimizeVertexCache:null}])
      fails(()=>createShapeAsset([{shape:'cube'}],options));
    fails(()=>createShapeAsset([{get shape(){throw Error('getter');}}]));
    const after=loadModel('assets/samples/Box/Box.glb');check(after===before);
    check(createShapeAsset(Array.from({length:64},(_,i)=>({shape:'cube',color:[i/63,0,0]})),{eyePixels:512,lod:false}).extent>0);
    // A shape asset's internal name is not a file path, and loadModel never returns it.
    fails(()=>loadModel(`shape:${cube.assetId}`));
    // Saving waits for shape-asset snapshots.
    const owner=createSceneContainer();createObject(cube.assetId,0,0,0,.2,{container:owner});
    fails(()=>captureSceneContainer(owner));disposeSceneContainer(owner);
    // Shape assets count toward the asset limit and are released with unloadModel.
    unloadModel(plain.assetId);fails(()=>createObject(plain.assetId,0,0,0,.1));
    globalThis.extra=[];while(true){try{extra.push(createShapeAsset([{shape:'cube'}]).assetId);}catch(_){break;}}
    check(extra.length>0);fails(()=>loadModel('assets/samples/ToyCar/ToyCar.glb'));
    unloadModel(extra.pop());check(createShapeAsset([{shape:'cube'}]).assetId>0);
  )JS";
  {
    ScriptHost host;assert(loadScript(host,script.string()));
    assert(loadedAssetCount(host.scene)==32);
    const auto &thing=host.scene.assets[2];
    assert(thing.generated&&thing.pbr&&thing.primitives.size()==4);
    size_t triangles=0;for(const auto &p:thing.primitives){triangles+=size_t(p.vertexCount)/3;assert(p.surface.size()==size_t(p.vertexCount)*9&&p.untextured);}
    assert(thing.pickTriangles.size()==triangles&&!thing.pickNodes.empty());
    float extent=0;for(int a=0;a<3;a++)extent=std::max(extent,thing.high[a]-thing.low[a]);assert(std::abs(extent-1)<1e-5f);
    const auto &metal=thing.primitives[1],&glow=thing.primitives[2];
    assert(metal.metallic==1&&std::abs(metal.roughness-.3f)<1e-6f&&std::abs(metal.factor[2]-.9f)<1e-6f);
    assert(glow.emissive[0]==1&&glow.emissive[1]==1&&glow.emissive[2]==0);
    // The object placed at center with scale extent covers the same meters as the parts.
    const auto &object=host.scene.objects.front();
    assert(std::abs(largestScale(object)-.675f)<1e-5f&&std::abs(object.y+.0375f)<1e-5f);
    puts("PASS createShapeAsset: extent/center, defaults, material sharing, PBR finishes, normalization, picking BVH, validation, options, internal names, save refusal, limit and unload");
    closeScript(host);
  }
  {
    // Material overrides: API, snapshots and rollback on a PBR model.
    const auto materials=dir/"materials.js";
    std::ofstream(materials)<<R"JS(
      function check(v,message='material assertion'){if(!v)throw Error(message);}
      function fails(fn){let caught=false;try{fn();}catch(_){caught=true;}check(caught,'expected rejection');}
      const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
      const lit=loadModel('assets/samples/Box/Box.glb',512,{pbr:true}),flat=loadModel('assets/Avocado.glb');
      const owner=createSceneContainer();
      globalThis.box=createObject(lit,0,0,0,.2,{container:owner});
      check(getObjectMaterial(box)===null);
      setObjectMaterial(box,{color:[1,.5,0],finish:'metal'});check(same(getObjectMaterial(box),{color:[1,.5,0],finish:'metal'}));
      setObjectMaterial(box,{finish:'glow'});check(same(getObjectMaterial(box),{finish:'glow'}));
      setObjectMaterial(box,{color:[0,0,1]});check(same(getObjectMaterial(box),{color:[0,0,1]}));
      const copy=getObjectMaterial(box);copy.color[0]=1;check(same(getObjectMaterial(box),{color:[0,0,1]}));
      for(const bad of [{},[],1,'red',undefined,{color:[1,0]},{color:[1,0,2]},{color:[-.1,0,0]},{color:['1',0,0]},{color:{r:1}},{finish:'shiny'},{finish:1}])
        fails(()=>setObjectMaterial(box,bad));
      check(same(getObjectMaterial(box),{color:[0,0,1]}));
      fails(()=>setObjectMaterial(box,{get color(){throw Error('getter');}}));check(same(getObjectMaterial(box),{color:[0,0,1]}));
      const unlit=createObject(flat,0,0,0,.2);fails(()=>setObjectMaterial(unlit,{finish:'glow'}));setObjectMaterial(unlit,null);check(getObjectMaterial(unlit)===null);
      fails(()=>setObjectMaterial(9999,null));fails(()=>getObjectMaterial(9999));fails(()=>setObjectMaterial(box));
      setObjectMaterial(box,{color:[.25,.5,.75],finish:'glossy'});
      const plain=createObject(lit,.3,0,0,.2,{container:owner});
      // Snapshots keep each object's override or null.
      const snap=captureSceneContainer(owner);check(snap.version===3);
      check(same(snap.objects[0].material,{color:[.25,.5,.75],finish:'glossy'})&&snap.objects[1].material===null);
      saveSceneContainer(owner,'materials');const loaded=loadSceneContainer('materials');
      check(same(getObjectMaterial(loaded.objectMap[box]),{color:[.25,.5,.75],finish:'glossy'})&&getObjectMaterial(loaded.objectMap[plain])===null);
      disposeSceneContainer(loaded.containerId);
      function bad(edit){const s=JSON.parse(JSON.stringify(snap));edit(s);fails(()=>restoreSceneContainer(s));}
      bad(s=>delete s.objects[0].material);bad(s=>s.objects[0].material={});bad(s=>s.objects[0].material=[]);
      bad(s=>s.objects[0].material.color=[2,0,0]);bad(s=>s.objects[0].material.color=[1,0]);bad(s=>s.objects[0].material.finish='shiny');
      bad(s=>s.version=2);
      setObjectMaterial(box,null);check(getObjectMaterial(box)===null&&captureSceneContainer(owner).objects[0].material===null);
    )JS";
    ScriptHost host;assert(loadScript(host,materials.string()));
    // Rejected restores leave no objects behind.
    size_t live=host.scene.objects.size();assert(live==3);
    closeScript(host);
    puts("PASS material overrides: set, partial, replace, copies, validation, getters, non-PBR rejection, removal, snapshot save/load, invalid snapshots and version");
  }
  {
    // An override renders exactly like a shape asset built with that color and finish,
    // and changes an imported PBR model.
    Renderer renderer;renderer.scene.objects.clear();
    auto shapeAsset=[&](V color,shapeMesh::Finish finish){
      std::vector<shapeMesh::Part> parts(1);parts[0].shape=Shape::Sphere;parts[0].scale={.3f,.3f,.3f};parts[0].color=color;parts[0].finish=finish;
      renderer.scene.assets.emplace_back();auto &asset=renderer.scene.assets.back();float extent;std::array<float,3> center;buildShapeAsset(parts,asset,extent,center);uploadModel(asset);
      return int(renderer.scene.assets.size());
    };
    const int white=shapeAsset({1,1,1},shapeMesh::Finish::Matte);
    for(auto finish:{shapeMesh::Finish::Matte,shapeMesh::Finish::Glossy,shapeMesh::Finish::Metal,shapeMesh::Finish::Glow}){
      const int expectedAsset=shapeAsset({.8f,.3f,.1f},finish);
      Object o{};o.id=1;o.asset=expectedAsset;o.scale.fill(.5f);o.yaw=.4f;renderer.scene.objects={o};
      const auto expected=renderer.renderSet({1},{0,0,-1.2f},0);
      renderer.scene.objects[0].asset=white;renderer.scene.objects[0].material=MaterialOverride{std::array<float,3>{.8f,.3f,.1f},finish};
      const auto actual=renderer.renderSet({1},{0,0,-1.2f},0);
      renderer.scene.objects[0].material.reset();const auto plain=renderer.renderSet({1},{0,0,-1.2f},0);
      assert(actual==expected&&plain!=actual);
    }
    renderer.scene.assets.emplace_back();auto &box=renderer.scene.assets.back();box.pbr=true;assert(loadModelFile("assets/samples/Box/Box.glb",box));uploadModel(box);
    Object o{};o.id=1;o.asset=int(renderer.scene.assets.size());o.scale.fill(.5f);o.yaw=.4f;o.pitch=.3f;renderer.scene.objects={o};
    const auto original=renderer.renderSet({1},{0,0,-1.2f},0);
    renderer.scene.objects[0].material=MaterialOverride{std::array<float,3>{0,1,0},std::nullopt};const auto green=renderer.renderSet({1},{0,0,-1.2f},0);
    size_t greener=0;for(size_t p=0;p<green.size();p+=4)if(green[p+3]==255&&green[p+1]>green[p]+40&&green[p+1]>green[p+2]+40)++greener;
    assert(green!=original&&greener>1000);
    renderer.scene.objects[0].material.reset();assert(renderer.renderSet({1},{0,0,-1.2f},0)==original);
    puts("PASS material override rendering: matches baked shape materials for every finish, recolors an imported PBR model, removal restores it");
  }
  {
    // Each shape draws lit, and the glow finish emits without light.
    Renderer renderer;renderer.scene.objects.clear();
    const char *names[]={"cube","sphere","cylinder","cone","wedge","pyramid","torus","arch"};
    const V sizes[]={{.3f,.3f,.3f},{.3f,.3f,.3f},{.3f,.3f,.3f},{.3f,.3f,.3f},{.3f,.3f,.3f},{.3f,.3f,.3f},{.3f,.08f,.3f},{.3f,.15f,.05f}};
    for(int i=0;i<8;i++)for(auto finish:{shapeMesh::Finish::Matte,shapeMesh::Finish::Glow}){
      std::vector<shapeMesh::Part> parts(1);parts[0].shape=*shapeMesh::shape(names[i]);parts[0].scale=sizes[i];parts[0].color={.2f,.6f,.9f};parts[0].finish=finish;
      parts[0].rotation={.4f,.6f,0};
      renderer.scene.assets.emplace_back();auto &asset=renderer.scene.assets.back();float extent;std::array<float,3> center;
      buildShapeAsset(parts,asset,extent,center);asset.lighting.intensity=0;asset.lighting.ambient=.02f;uploadModel(asset);
      Object o{};o.id=1;o.asset=int(renderer.scene.assets.size());o.scale.fill(.5f);renderer.scene.objects={o};
      const auto image=renderer.renderSet({1},{0,0,-1.2f},0);size_t covered=0;double brightness=0;
      for(size_t p=0;p<image.size();p+=4)if(image[p+3]==255){++covered;brightness+=image[p]+image[p+1]+image[p+2];}
      brightness/=std::max<size_t>(covered,1);
      std::printf("SHAPE %s %s covered=%zu brightness=%.1f\n",names[i],finish==shapeMesh::Finish::Glow?"glow":"matte",covered,brightness);
      assert(covered>2000&&(finish==shapeMesh::Finish::Glow?brightness>200:brightness<120));
      if(finish==shapeMesh::Finish::Matte){
        // Lit from one side, normals must shade the surface across a wide range.
        asset.lighting.intensity=3;const auto lit=renderer.renderSet({1},{0,0,-1.2f},0);int low=765,high=0;
        for(size_t p=0;p<lit.size();p+=4)if(lit[p+3]==255){const int b=lit[p]+lit[p+1]+lit[p+2];low=std::min(low,b);high=std::max(high,b);}
        std::printf("SHAPE %s lit range=%d..%d\n",names[i],low,high);assert(high-low>30);
      }
    }
    puts("PASS shape rendering: every shape draws through the PBR path; glow emits without light");
  }
  {
    // Build cost at the 256-part limit, using the most detailed shape.
    std::vector<shapeMesh::Part> parts(256);
    for(size_t i=0;i<parts.size();i++){parts[i].shape=Shape::Torus;parts[i].position={float(i%16)*.1f,float(i/16)*.1f,0};parts[i].scale={.09f,.02f,.09f};parts[i].color={float(i%64)/63.f,0,0};}
    ModelAsset asset;float extent;std::array<float,3> center;
    const auto start=std::chrono::steady_clock::now();buildShapeAsset(parts,asset,extent,center);
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    size_t triangles=0;for(const auto &p:asset.primitives)triangles+=size_t(p.vertexCount)/3;
    // Upload adds indexing, automatic LOD and depth clusters; finish so GPU work is counted.
    const auto uploadStart=std::chrono::steady_clock::now();uploadModel(asset);glFinish();
    const double uploadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
    std::printf("SHAPE build parts=256 materials=%zu triangles=%zu build_ms=%.1f upload_ms=%.1f\n",asset.primitives.size(),triangles,ms,uploadMs);
    assert(asset.primitives.size()==64&&triangles==256*576);deleteModel(asset);
  }
}
