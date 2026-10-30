// Offscreen geometry/pixel sensitivity, not a production quality comparison.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 {
  Renderer r(false,modelVertexShader,2048);r.ordinaryRouting=false;r.scene.objects.clear();r.scene.assets.emplace_back();
  auto &asset=r.scene.assets.back();asset.pbr=true;asset.lod=true;asset.optimizeVertexCache=true;
  assert(loadModelFile("assets/samples/ToyCar/ToyCar.glb",asset));uploadModel(asset);
  asset.lighting.intensity=.3;asset.lighting.ambient=.04;
  std::vector<int> ids;
  for(int i=0;i<32;i++){Object o{};o.id=i+1;o.asset=int(r.scene.assets.size());o.scale=.16f;o.x=(i%8-3.5f)*.22f;o.y=(i/8-1.5f)*.22f;r.scene.objects.push_back(o);ids.push_back(o.id);}
  auto pose=[&](int frame){for(auto &o:r.scene.objects){o.yaw=.2f+frame*.013f;o.pitch=.1f+std::sin(frame*.02f)*.15f;}};
  struct Case{int lights,level,pixels;double gpu=0,wall=0;uint64_t triangles=0;};
  std::vector<Case> cases;for(int lights:{0,8})for(int level:{0,2})for(int size:{768,384,2048})cases.push_back({lights,level,size});
  auto configure=[&](Case &c){
   r.samplePixels=c.pixels;for(auto &o:r.scene.objects)o.lodLevel=c.level;
   r.scene.lights.clear();for(int i=0;i<c.lights;i++){PointLight l;l.id=i+1;l.position={float(i%4-1.5f)*.4f,i<4?.3f:-.3f,-.6f};l.color={1,.8f,.6f};l.intensity=.15;l.range=2;l.spot=i%2;l.direction={0,0,1};l.innerConeAngle=.3;l.outerConeAngle=.6;r.scene.lights.push_back(l);}
   c.triangles=0;for(const auto &p:asset.primitives)c.triangles+=uint64_t(modelDrawVertexCount(p,c.level))/3*ids.size();
  };
  pose(0);configure(cases[0]);const auto before=r.renderSet(ids,{0,0,-1.2f},0);
  auto gen=reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));auto begin=reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));auto end=reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));auto result=reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));auto destroy=reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));assert(gen&&begin&&end&&result&&destroy);
  GLuint query=0;gen(1,&query);
  // Each case sees exactly the same moving pose/eye sequence; alternate order.
  for(int frame=0;frame<120;frame++){
   pose(frame);
   for(size_t step=0;step<cases.size();step++){
    auto &c=cases[frame%2?cases.size()-1-step:step];configure(c);
    const auto start=std::chrono::steady_clock::now();begin(GL_TIME_ELAPSED_EXT,query);
    r.renderSet(ids,{0,0,-1.2f},frame%2,0,false);end(GL_TIME_ELAPSED_EXT);glFinish();
    GLuint64 elapsed=0;result(query,GL_QUERY_RESULT_EXT,&elapsed);GLint disjoint=0;glGetIntegerv(GL_GPU_DISJOINT_EXT,&disjoint);assert(!disjoint&&glGetError()==GL_NO_ERROR);
    if(frame>=30){c.gpu+=double(elapsed)/1e6;c.wall+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
   }
  }
  destroy(1,&query);pose(0);configure(cases[0]);assert(before==r.renderSet(ids,{0,0,-1.2f},0));
  assert(cases[0].triangles>cases[3].triangles);
  for(const auto &c:cases)printf("PBR_COST {\"lights\":%d,\"level\":%d,\"pixels\":%d,\"triangles\":%llu,\"frames\":90,\"gpuMs\":%.6f,\"wallMs\":%.6f}\n",c.lights,c.level,c.pixels,(unsigned long long)c.triangles,c.gpu/90,c.wall/90);
 }
 assert(glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);
 puts("PASS PBR cost diagnostics: fixed storage, identical moving poses, both eyes, exact baseline after switches, clean GL/disjoint status");
}
