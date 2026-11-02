// Frame loop for the VR host.
#include "render_host.h"
int main(int argc,char **argv) {
#ifdef SF_RIGID_BATCH_EXPERIMENT
  rigidBatchEnabled=std::getenv("SF_RIGID_BATCH")&&std::string(std::getenv("SF_RIGID_BATCH"))=="1";
  std::fprintf(stdout,"RIGID_BATCH enabled=%d\n",rigidBatchEnabled);
#endif
#ifdef SF_VERTEX_ARRAY_EXPERIMENT
  vertexArrayCachingEnabled=std::getenv("SF_VERTEX_ARRAY_CACHE")&&std::string(std::getenv("SF_VERTEX_ARRAY_CACHE"))=="1";
  std::fprintf(stdout,"VERTEX_ARRAY_CACHE enabled=%d\n",vertexArrayCachingEnabled);
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
  drawUniformCachingEnabled=std::getenv("SF_DRAW_UNIFORM_CACHE")&&std::string(std::getenv("SF_DRAW_UNIFORM_CACHE"))=="1";
  std::fprintf(stdout,"DRAW_UNIFORM_CACHE enabled=%d\n",drawUniformCachingEnabled);
#endif
#ifdef SF_RIGID_POSE_EXPERIMENT
  rigidPoseCachingEnabled=!(std::getenv("SF_RIGID_POSE_CACHE")&&std::string(std::getenv("SF_RIGID_POSE_CACHE"))=="0");
  std::fprintf(stdout,"RIGID_POSE_CACHE enabled=%d\n",rigidPoseCachingEnabled);
#endif
#ifdef SF_GPU_SKIN_EXPERIMENT
  gpuSkinningEnabled=std::getenv("SF_GPU_SKINNING")&&std::string(std::getenv("SF_GPU_SKINNING"))=="1";
  std::fprintf(stdout,"GPU_SKINNING mode=%s (eligible single-set skins; matched conservative bounds)\n",gpuSkinningEnabled?"gpu":"cpu");
#endif
  // CPU-only workload: no SteamVR session or graphics context required.
  if(argc>2&&std::string(argv[1])=="--benchmark-picking"){
    ModelAsset asset;if(!loadModelFile(argv[2],asset))return 1;
    Object object{1,1,0,0,0,.4f,.5f};
    ControllerSnapshot hand;hand.valid=true;
    constexpr float tilt=.6981317008f;
    const float c=std::cos(tilt),s=std::sin(tilt);
    const float rotation[9]={1,0,0,0,c,-s,0,s,c};
    std::copy(rotation,rotation+9,hand.rotation);
    hand.y=-(-.02522f*c-.020687f*s);
    hand.z=1.f-(-.02522f*s+.020687f*c);
    constexpr int frames=200;
    std::array<float,frames*2> reference{};
    for(bool accelerated:{false,true}){
      int hits=0;double checksum=0;
      const auto start=std::chrono::steady_clock::now();
      for(int frame=0;frame<frames;frame++)for(int h=0;h<2;h++){
        hand.x=(h==0?-.012694f:.012694f)+((frame%21)-10)*.012f;
        const float distance=meshHit(object,asset,hand,h,accelerated);
        if(!accelerated)reference[frame*2+h]=distance;
        else{
          const float expected=reference[frame*2+h];
          const bool hit=distance<std::numeric_limits<float>::max(),expectedHit=expected<std::numeric_limits<float>::max();
          if(hit!=expectedHit||(hit&&std::fabs(distance-expected)>1e-5f)){
            fprintf(stderr,"[benchmark] BVH mismatch frame=%d hand=%d expected=%g actual=%g\n",frame,h,expected,distance);return 2;
          }
        }
        if(distance<std::numeric_limits<float>::max()){hits++;checksum+=distance;}
      }
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
      fprintf(stdout,"[benchmark] mode=%s frames=%d hands=2 picking_ms=%.4f hits=%d checksum=%.6f nodes=%zu triangles=%zu\n",accelerated?"bvh":"linear",frames,ms/frames,hits,checksum,asset.pickNodes.size(),asset.pickTriangles.size());
    }
    return 0;
  }
  if(argc>2&&std::string(argv[1])=="--inspect"){
    ModelAsset asset;return loadModelFile(argv[2],asset)?0:1;
  }
  std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
  std::string calibrationPath;
  const char *userHome=std::getenv("HOME");
  std::filesystem::path scriptsDirectory=userHome?std::filesystem::path(userHome)/".local/share/pocketspace/scripts":"user-scripts";
  std::vector<std::filesystem::path> launchScripts;
  for(int i=1;i<argc;i++){
    std::string arg=argv[i];
    if(arg=="--eye-calibration"||arg=="--eye-calibration-output"){
      if(i+1==argc){fprintf(stderr,"%s requires a file path\n",arg.c_str());return 13;}
      (arg=="--eye-calibration"?calibrationPath:calibrationOutput)=argv[++i];
    }else if(arg=="--scene-state-dir"){
      if(i+1==argc){fprintf(stderr,"--scene-state-dir requires a directory\n");return 13;}
      scenePersistence::directory=argv[++i];
    }else if(arg=="--scripts-dir"){
      if(i+1==argc){fprintf(stderr,"--scripts-dir requires a directory\n");return 13;}
      scriptsDirectory=argv[++i];
    }else if(arg=="--launch"){
      if(i+1==argc){fprintf(stderr,"--launch requires a catalog-relative script\n");return 13;}
      launchScripts.emplace_back(argv[++i]);
    }else{fprintf(stderr,"Unknown argument: %s (use --scripts-dir and --launch for scripts)\n",arg.c_str());return 13;}
  }
  if(!calibrationPath.empty()){
    std::ifstream file(calibrationPath,std::ios::binary);std::string data((std::istreambuf_iterator<char>(file)),{});
    JSRuntime *runtime=JS_NewRuntime();JSContext *ctx=JS_NewContext(runtime);
    JSValue value=JS_ParseJSON(ctx,data.c_str(),data.size(),calibrationPath.c_str());
    const bool ok=file.good()&&!JS_IsException(value)&&parseCalibration(ctx,value,eyeCalibration);
    JS_FreeValue(ctx,value);JS_FreeContext(ctx);JS_FreeRuntime(runtime);
    if(!ok){fprintf(stderr,"Invalid eye calibration: %s\n",calibrationPath.c_str());return 13;}
    fprintf(stdout,"[eye] calibration loaded: %s\n",calibrationPath.c_str());
  }
  scenePersistence::catalogRoot=std::filesystem::absolute(scriptsDirectory).string();
  ScriptRegistry scriptRegistry;
  std::string launchError;
  for(const auto &name:launchScripts){
    std::filesystem::path resolved;
    if(!resolveCatalogScript(scriptsDirectory,name,resolved,launchError)){
      fprintf(stderr,"[lifecycle] launch %s failed: %s\n",name.c_str(),launchError.c_str());return 13;
    }
    int id=0;
    if(!scriptRegistry.launch(resolved.string(),id,launchError)){
      fprintf(stderr,"[lifecycle] launch %s failed: %s\n",name.c_str(),launchError.c_str());return 13;
    }
  }
  EGLDisplay display=EGL_NO_DISPLAY;EGLContext context=EGL_NO_CONTEXT;EGLSurface surface=EGL_NO_SURFACE;
  ScopeExit eglCleanup{[&]{if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}}};
  ScopeExit scriptCleanup{[&]{
    for(int id:scriptRegistry.allIds())if(auto *entry=scriptRegistry.find(id);entry&&entry->host.ctx){
      ScriptScope scope(entry->host);closeScript(entry->host);resetActiveInteraction();
    }
  }};
  if(!initEgl(display,context,surface))return 1;
  vr::EVRInitError initError;vr::VR_Init(&initError,vr::VRApplication_Overlay);if(initError!=vr::VRInitError_None)return 2;
  ScopeExit vrCleanup{[&]{frameInput.stop();trackingAnchorValid=false;vr::VR_Shutdown();}};
  if(!controllerInput.init(std::filesystem::current_path().string(),argv[0]))fprintf(stderr,"[input] SteamVR actions unavailable; controller poses and overlay pointer events remain active\n");
  if(!frameInput.start())fprintf(stderr,"[input] raw Steam Frame WebSocket unavailable; action input remains active\n");
  auto *system=vr::VRSystem();auto *overlays=vr::VROverlay();
  int drm=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);auto *gbm=drm>=0?gbm_create_device(drm):nullptr;
  ScopeExit gbmCleanup{[&]{if(gbm)gbm_device_destroy(gbm);if(drm>=0)close(drm);}};
  createImage=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  destroyImage=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  imageRenderbuffer=(PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
  if(!gbm||!createImage||!destroyImage||!imageRenderbuffer)return 3;
#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *ordinaryMode=std::getenv("SF_GPU_ORDINARY_VERTEX");
  if(ordinaryMode)ordinaryVertexEnabled=std::strcmp(ordinaryMode,"1")==0;
#endif
  const char *vertexSource=modelVertexShader;
#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *staticMode=std::getenv("SF_GPU_STATIC_VERTEX");
  const bool staticVertexOnly=staticMode&&std::strcmp(staticMode,"1")==0;
  const auto staticSource=staticVertexOnly?staticVertexExperiment():std::string{};
  if(staticVertexOnly)vertexSource=staticSource.c_str();
#endif
  GLuint generalProgram=glCreateProgram(),v=compile(GL_VERTEX_SHADER,vertexSource),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
  ScopeExit shaderCleanup{[&]{glDeleteProgram(generalProgram);glDeleteShader(v);glDeleteShader(f);}};
  glAttachShader(generalProgram,v);glAttachShader(generalProgram,f);glLinkProgram(generalProgram);GLint linked=0;glGetProgramiv(generalProgram,GL_LINK_STATUS,&linked);if(!linked)return 5;
  // Validate color and depth material programs before publishing a live scene.
  std::map<int,std::unique_ptr<SceneRenderState>> renderStates;
  std::array<panelPointer::Pointer,2> panelPointers;
  PbrProgram pbrProgram;if(!pbrProgram.init())return 5;
  PbrProgram depthPbrProgram;if(!depthPbrProgram.init(true))return 5;
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
  PbrProgram minimalDepthProgram;
  const bool minimalPbrDepth=std::getenv("SF_PBR_MINIMAL_DEPTH")&&std::strcmp(std::getenv("SF_PBR_MINIMAL_DEPTH"),"1")==0;
  if(minimalPbrDepth&&!minimalDepthProgram.initMinimalDepth())return 5;
  PbrProgram &selectedDepthProgram=minimalPbrDepth?minimalDepthProgram:depthPbrProgram;
  printf("PBR_MINIMAL_DEPTH enabled=%d\n",int(minimalPbrDepth));
#else
  PbrProgram &selectedDepthProgram=depthPbrProgram;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
  const bool pbrVariantsEnabled=std::getenv("SF_GPU_PBR_VARIANTS")&&std::strcmp(std::getenv("SF_GPU_PBR_VARIANTS"),"1")==0;
#else
  constexpr bool pbrVariantsEnabled=true;
#endif
#ifdef SF_PBR_VARIANT_EXPERIMENT
  ScopeExit variantReport{[&]{
    size_t programs=0,fallbackDraws=0;uint64_t binaryBytes=0;double compileMs=0;
    for(const auto &[id,owner]:renderStates){programs+=owner->pbrVariants.programs.size();binaryBytes+=owner->pbrVariants.binaryBytes();compileMs+=owner->pbrVariants.compileMs;fallbackDraws+=owner->pbrVariants.fallbackDraws;}
    printf("PBR_VARIANTS_CACHE programs=%zu binary_bytes=%llu compile_ms=%.4f fallback_draws=%zu\n",programs,(unsigned long long)binaryBytes,compileMs,fallbackDraws);
  }};
#endif
#ifdef SF_PBR_DEPTH_EXPERIMENT
  const bool fastPbrDepth=std::getenv("SF_PBR_DEPTH")&&std::string(std::getenv("SF_PBR_DEPTH"))=="1";
#else
  constexpr bool fastPbrDepth=true;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
  OrdinaryModelProgram ordinaryProgram,skinProgram;
  const GLuint program=ordinaryProgram.select(false)?ordinaryProgram.id():generalProgram;
  skinProgram.borrow(generalProgram);
#else
  const GLuint program=generalProgram;
#endif
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto anchor=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)anchor.m[r][3]+=anchor.m[r][1]*-.2f+anchor.m[r][2]*-1.f;
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
  // Profiling only: a canonical scene basis removes cross-run tracking noise.
  const bool fixedDepthView=std::getenv("SF_PBR_DEPTH_FIXED_VIEW")&&std::strcmp(std::getenv("SF_PBR_DEPTH_FIXED_VIEW"),"1")==0;
  if(fixedDepthView){anchor={};for(int axis=0;axis<3;axis++)anchor.m[axis][axis]=1;}
  printf("PBR_DEPTH_FIXED_VIEW enabled=%d\n",int(fixedDepthView));
#endif
  trackingAnchor=anchor;trackingAnchorValid=true;
  fprintf(stdout,"anchor world=%f,%f,%f\n",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);fflush(stdout);
  vr::VROverlayHandle_t lasers[2]={vr::k_ulOverlayHandleInvalid,vr::k_ulOverlayHandleInvalid};bool laserShown[2]{};
  using SceneResources=NativeSceneResources<Surface,vr::VROverlayHandle_t,PanelSubmission>;
  std::map<int,std::unique_ptr<SceneResources>> resourcesByInstance;
  auto releaseResources=[&](SceneResources &resources){resources.release(
    [&](Surface &surface){destroySurface(surface,display);},
    [&](vr::VROverlayHandle_t handle){overlays->HideOverlay(handle);overlays->ClearOverlayTexture(handle);overlays->DestroyOverlay(handle);});};
  scriptRegistry.setBeforeStop([&](int id){
    for(auto &pointer:panelPointers)pointer.forgetInstance(id);
    if(auto owner=resourcesByInstance.find(id);owner!=resourcesByInstance.end())releaseResources(*owner->second);
    if(auto owner=renderStates.find(id);owner!=renderStates.end())owner->second->resetScene();
  });
  ScopeExit clearStopHook{[&]{scriptRegistry.setBeforeStop({});}};
  ScopeExit overlayCleanup{[&]{
    for(auto laser:lasers)if(laser!=vr::k_ulOverlayHandleInvalid)overlays->DestroyOverlay(laser);
    for(auto &[id,resources]:resourcesByInstance)releaseResources(*resources);
    fprintf(stdout,"[surface] grouped_buffer_bytes=%llu peak=%llu limit=%llu\n",(unsigned long long)groupBufferBudget.used,(unsigned long long)groupBufferBudget.peak,(unsigned long long)GroupBufferBudget::limit);
  }};
  if(!createLaser(lasers[0],0)||!createLaser(lasers[1],1))return 9;
  GLint maxRenderbufferPixels=0;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRenderbufferPixels);
  const int maxGroupPixels=std::min(4096,maxRenderbufferPixels/2);
  using SurfaceMembers=std::map<int,std::vector<const Object*>>;
  auto surfaceMembers=[&](const ScriptHost &host){SurfaceMembers groups;for(const auto &object:host.scene.objects)if(object.visible)groups[objectSurfaceKey(object)].push_back(&object);return groups;};
  auto eraseSurface=[&](int ownerId,int key){
    auto owner=resourcesByInstance.find(ownerId);
    auto render=renderStates.find(ownerId);
    if(owner==resourcesByInstance.end()||render==renderStates.end())return;
    auto &surfaces=owner->second->surfaces;
    auto &pendingTileChange=render->second->pendingTileChange;
    auto found=surfaces.find(key);if(found==surfaces.end())return;
    if(pendingTileChange&&!pendingTileChange->merge&&(key==pendingTileChange->parent||key==pendingTileChange->child)){
      const int otherKey=key==pendingTileChange->parent?pendingTileChange->child:pendingTileChange->parent;
      auto other=surfaces.find(otherKey);
      if(other!=surfaces.end()&&found->second->previous){
        preserveSurfaceFallback(*found->second,*other->second);
      }
    }
    destroySurface(*found->second,display);surfaces.erase(found);
  };
  auto syncSurfaces=[&](ScriptHost &host,SceneResources &resources)->bool{
    auto &surfaces=resources.surfaces;
    const auto groups=surfaceMembers(host);
    for(auto it=surfaces.begin();it!=surfaces.end();){
      if(!groups.count(it->first)){
        const int key=it->first;++it;forgetObjectInteraction(key);eraseSurface(host.instanceId,key);
      }else ++it;
    }
    for(const auto &[id,members]:groups){
      const auto *object=members.front();
      const float width=std::max(.55f,object->scale*2.1f*objectRadius(*object));
      const int eyePixels=host.scene.assets[(size_t)object->asset-1].eyePixels;
      const bool interactive=id>0&&!object->laserPassThrough;
      auto found=surfaces.find(id);
      if(found!=surfaces.end()&&found->second->buffers[0].fbo&&((id>0&&found->second->referenceEyePixels!=eyePixels)||found->second->interactive!=interactive)){
        pointerStates.erase(id);destroySurface(*found->second,display);surfaces.erase(found);found=surfaces.end();
      }
      if(found==surfaces.end()){
        auto created=std::make_unique<Surface>();
        if(!createSurface(*created,host.instanceId,id,width,eyePixels,interactive,gbm,display,anchor)){destroySurface(*created,display);return false;}
        surfaces.emplace(id,std::move(created));
      }else if(!found->second->buffers[0].fbo){
        if(!createSurface(*found->second,host.instanceId,id,width,eyePixels,interactive,gbm,display,anchor))return false;
      }
    }
    return true;
  };
  reserveObjectOverlay=[&](int id)->std::string{
    auto owner=resourcesByInstance.find(activeInstanceId);
    if(owner==resourcesByInstance.end())return "script resource owner unavailable";
    auto &sceneResources=*owner->second;
    auto &surfaces=sceneResources.surfaces;
    if(surfaces.count(id))return {};
    auto reserved=std::make_unique<Surface>();reserved->instanceId=activeInstanceId;reserved->objectId=id;
    const auto key=objectOverlayKey(sceneResources.key(id));
    const auto error=overlays->CreateOverlay(key.c_str(),key.c_str(),&reserved->overlay);
    if(error!=vr::VROverlayError_None)return std::string("SteamVR: ")+overlays->GetOverlayErrorNameFromEnum(error);
    if(id<0){
      const auto stageError=overlays->CreateOverlay((key+".stage").c_str(),(key+".stage").c_str(),&reserved->stagingOverlay);
      if(stageError!=vr::VROverlayError_None){destroySurface(*reserved,display);return std::string("SteamVR: ")+overlays->GetOverlayErrorNameFromEnum(stageError);}
    }
    surfaces.emplace(id,std::move(reserved));return {};
  };
  releaseObjectOverlay=[&](int id){eraseSurface(activeInstanceId,id);};
  ScopeExit objectOverlayHooksCleanup{[]{reserveObjectOverlay={};releaseObjectOverlay={};}};

  reservePanelOverlay=[&](const Panel &panel)->std::string{
    auto owner=resourcesByInstance.find(activeInstanceId);
    if(owner==resourcesByInstance.end())return "script resource owner unavailable";
    auto &sceneResources=*owner->second;
    auto &panelOverlays=sceneResources.panelOverlays;
    if(panelOverlays.count(panel.id))return {};
    vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
    std::string key=panelOverlayKey(sceneResources.key(panel.id));
    const auto error=overlays->CreateOverlay(key.c_str(),key.c_str(),&handle);if(error!=vr::VROverlayError_None)return std::string("SteamVR: ")+overlays->GetOverlayErrorNameFromEnum(error);
    overlays->SetOverlayWidthInMeters(handle,panel.widthMeters);
    vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(handle,&bounds);
    panelOverlays.emplace(panel.id,handle);
    return {};
  };
  releasePanelOverlay=[&](int id){
    auto owner=resourcesByInstance.find(activeInstanceId);if(owner==resourcesByInstance.end())return;
    auto &panels=owner->second->panelOverlays;
    auto it=panels.find(id);if(it==panels.end())return;
    overlays->ClearOverlayTexture(it->second);overlays->DestroyOverlay(it->second);
    panels.erase(it);owner->second->panelSubmissions.erase(id);
  };
  ScopeExit panelOverlayHooksCleanup{[]{reservePanelOverlay={};releasePanelOverlay={};}};
  auto syncPanels=[&](ScriptHost &host,SceneResources &resources,SceneRenderState &render){
    auto &panelOverlays=resources.panelOverlays;
    auto &panelSubmissions=resources.panelSubmissions;
    for(auto it=panelOverlays.begin();it!=panelOverlays.end();){
      const int id=it->first;
      const bool exists=findSceneItem(host.scene.panels,id)!=nullptr;
      if(exists){++it;continue;}
      overlays->HideOverlay(it->second);overlays->ClearOverlayTexture(it->second);overlays->DestroyOverlay(it->second);
      panelSubmissions.erase(id);
      it=panelOverlays.erase(it);
    }
    for(auto &retired:host.scene.retiredPanels)panelDestroy(retired);
    host.scene.retiredPanels.clear();
    for(auto &panel:host.scene.panels){
      if(!panel.visible){if(panelOverlays.count(panel.id))hidePanel(*overlays,panelOverlays.at(panel.id),panelSubmissions[panel.id]);}
      if(panel.dirty)panelUpload(panel);
      if(panelOverlays.count(panel.id))continue;
      reservePanelOverlay(panel);
    }
  };
  glUseProgram(program);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glUniform1i(glGetUniformLocation(program,"material"),0);glEnable(GL_DEPTH_TEST);
  const GLint centerLocation=glGetUniformLocation(program,"center");
  const GLint rightLocation=glGetUniformLocation(program,"right");
  const GLint upLocation=glGetUniformLocation(program,"up");
  const GLint forwardLocation=glGetUniformLocation(program,"forward");
  const GLint panelRightLocation=glGetUniformLocation(program,"panelRight");
  const GLint panelUpLocation=glGetUniformLocation(program,"panelUp");
  const GLint panelForwardLocation=glGetUniformLocation(program,"panelForward");
  const GLint eyeLocation=glGetUniformLocation(program,"eye");
  const GLint panelHalfWidthLocation=glGetUniformLocation(program,"panelHalfWidth");
  setVec3(centerLocation,anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);
  setVec3(rightLocation,anchor.m[0][0],anchor.m[1][0],anchor.m[2][0]);
  setVec3(upLocation,anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]);
  setVec3(forwardLocation,-anchor.m[0][2],-anchor.m[1][2],-anchor.m[2][2]);
  GLint yawLocation=glGetUniformLocation(program,"yaw"),pitchLocation=glGetUniformLocation(program,"pitch"),rollLocation=glGetUniformLocation(program,"roll"),scaleLocation=glGetUniformLocation(program,"modelScale"),offsetLocation=glGetUniformLocation(program,"offset");
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
  const ModelDrawLocations drawLocations(program);
#endif
  GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff");
  GLint sheenLocation=glGetUniformLocation(program,"sheen");
  double pickingMs=0,renderMs=0,finishMs=0,physicsMs=0;

#ifdef SF_GPU_SYNC_EXPERIMENT
  const char *groupPolicy=std::getenv("SF_GPU_GROUP_POLICY");
  groupOccluderTieBreak=groupPolicy&&std::strcmp(groupPolicy,"occluders")==0;
  GpuSyncExperiment gpuSync;
  const char *instanceMode=std::getenv("SF_GPU_INSTANCING");
  const char *primitiveMode=std::getenv("SF_GPU_PRIMITIVE_VISIBILITY");
  const bool primitiveVisibility=primitiveMode&&std::strcmp(primitiveMode,"1")==0;
  PrimitiveVisibilityExperiment primitiveExperiment;
  const char *clusterMode=std::getenv("SF_GPU_CLUSTER_MODE");
  ClusterVisibilityExperiment clusterExperiment(clusterMode?(std::strcmp(clusterMode,"cull")==0?ClusterVisibilityExperiment::Cull:std::strcmp(clusterMode,"full")==0?ClusterVisibilityExperiment::Full:ClusterVisibilityExperiment::Original):ClusterVisibilityExperiment::Off);
  const char *opaqueMode=std::getenv("SF_GPU_OPAQUE_SHADER");
  const bool specializeOpaque=opaqueMode&&std::strcmp(opaqueMode,"1")==0;
  std::unique_ptr<OpaqueExperimentProgram> opaqueExperiment,depthExperiment;
  if(specializeOpaque){opaqueExperiment=std::make_unique<OpaqueExperimentProgram>(opaqueColorExperiment);depthExperiment=std::make_unique<OpaqueExperimentProgram>(opaqueDepthExperiment);}
#endif
  auto next=std::chrono::steady_clock::now();auto reportStart=next;int frame=0;uint64_t diagnosticFrames=0;engineStats.prime();
  auto reconnectAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
#ifdef SF_ANIMATION_PROFILE
  AnimationProfileFrame animationFrame;AnimationProfileWindow animationWindow;unsigned animationEpoch=0;
  animationProfileFrame=&animationFrame;ScopeExit clearAnimationProfiler{[]{animationProfileFrame=nullptr;}};
  std::fprintf(stdout,"ANIMATION_PROFILE_BEGIN gpu=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
#endif
  while(running){
#ifdef SF_ANIMATION_PROFILE
    animationFrame={};const auto animationFrameStart=AnimationProfileClock::now();
#endif
    vr::VREvent_t runtimeEvent{};
    while(system->PollNextEvent(&runtimeEvent,sizeof(runtimeEvent)))if(runtimeEvent.eventType==vr::VREvent_Quit){system->AcknowledgeQuit_Exiting();fprintf(stdout,"[lifecycle] SteamVR quit; exiting\n");running=false;}
    if(!running)break;
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,.011f,poses,vr::k_unMaxTrackedDeviceCount);
    for(int hand=0;hand<2;hand++){
      auto &snapshot=controllers[hand];snapshot={};
      auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      if(device==vr::k_unTrackedDeviceIndexInvalid){frameInput.setTrackedDevice(hand,-1);if(frame%900==0)fprintf(stdout,"[input] hand=%d device=none\n",hand);continue;}
      const auto &pose=poses[device];
      if(frame%900==0){fprintf(stdout,"[input] hand=%d device=%u pose=%d\n",hand,device,pose.bPoseIsValid);fflush(stdout);}
      snapshot.connected=pose.bDeviceIsConnected;
      frameInput.setTrackedDevice(hand,snapshot.connected?int(device):-1);
      if(!snapshot.connected||!pose.bPoseIsValid)continue;
      snapshot.valid=true;
      float relative[3];for(int i=0;i<3;i++)relative[i]=pose.mDeviceToAbsoluteTracking.m[i][3]-anchor.m[i][3];
      for(int axis=0;axis<3;axis++){
        float value=0;for(int i=0;i<3;i++)value+=relative[i]*anchor.m[i][axis];
        if(axis==0)snapshot.x=value;else if(axis==1)snapshot.y=value;else snapshot.z=-value;
      }
      for(int row=0;row<3;row++)for(int col=0;col<3;col++){
        float value=0;for(int i=0;i<3;i++)value+=anchor.m[i][row]*pose.mDeviceToAbsoluteTracking.m[i][col];
        snapshot.rotation[row*3+col]=row==2?-value:value;
      }
    }
    const bool streamWasStarted=frameInput.started();
    frameInput.poll();
    fanOutRawInput(scriptRegistry,frameInput.takeEvents());
    frameInput.takeCallbackEvents();
    bool eyeTrackingRequested=false,foveationRequested=false;
    for(int id:scriptRegistry.runningIds())if(const auto *instance=scriptRegistry.find(id)){
      eyeTrackingRequested|=instance->host.scene.eyeTrackingRequested;
      foveationRequested|=instance->host.scene.foveationRequested;
    }
    controllerInput.poll(eyeTrackingRequested,foveationRequested);
    if(frame%30==0)for(int id:scriptRegistry.runningIds()){
      auto *entry=scriptRegistry.find(id);
      if(!entry||entry->state!=ScriptState::Running)continue;
      auto &host=entry->host;
      std::error_code ec;const auto modified=std::filesystem::last_write_time(entry->path,ec);
      if(ec||modified==host.mtime)continue;
      const bool loaded=scriptRegistry.reload(id,[&]{
        for(auto &pointer:panelPointers)pointer.forgetInstance(id);
        if(auto owner=resourcesByInstance.find(id);owner!=resourcesByInstance.end())releaseResources(*owner->second);
        if(auto owner=renderStates.find(id);owner!=renderStates.end())owner->second->resetScene();
        pointerStates.clear();
        sceneInput().resetScene();
        for(int hand=0;hand<2;hand++){
          hoveredObject[hand]=meshHoveredObject[hand]=meshCurrentObject[hand]=0;
          meshHitDistance[hand]=2;meshLastHit[hand]={};grabNeedsRelease[hand]=true;
        }
#ifdef SF_ANIMATION_PROFILE
        ++animationEpoch;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
        primitiveExperiment.reset();clusterExperiment.reset();
#endif
        fprintf(stdout,"[lifecycle] instance=%d scene state reset\n",id);
      });
      if(loaded&&pbrVariantsEnabled&&renderStates.count(id)){
        for(auto &asset:host.scene.assets)if(!asset.uploaded)uploadModel(asset);
        renderStates.at(id)->pbrVariants.prepare(host.scene);
      }
      if(!loaded)host.mtime=modified;
    }
    std::array<std::optional<inputArbitration::Target>,2> rayTargets{};
    std::array<int,2> rayOwners{};
    std::array<int,2> grabOwners{};
    const auto runningSceneIds=scriptRegistry.runningIds();
    for(int hand=0;hand<2;hand++){
      if(!controllers[hand].valid||!controllers[hand].connected)continue;
      // A held object keeps its owner until release or that instance stops.
      for(int id:runningSceneIds)if(auto render=renderStates.find(id);render!=renderStates.end()&&render->second->grabs[hand].objectId){
        rayTargets[hand]=inputArbitration::Target{id,render->second->grabs[hand].objectId,render->second->grabs[hand].laserDistance};
        grabOwners[hand]=id;
        break;
      }
      if(rayTargets[hand]){rayOwners[hand]=rayTargets[hand]->instanceId;continue;}
      for(int id:runningSceneIds)if(auto *entry=scriptRegistry.find(id)){
        ScriptScope pickScope(entry->host);
        for(const auto &object:entry->host.scene.objects){
          if(!object.visible)continue;
          const auto &asset=entry->host.scene.assets[size_t(object.asset)-1];
          const float distance=meshHit(object,object.animationPose?object.animationPose->geometry:asset,controllers[hand],hand);
          inputArbitration::consider(rayTargets[hand],{id,object.id,distance});
        }
      }
      if(rayTargets[hand])rayOwners[hand]=rayTargets[hand]->instanceId;
    }
    // One ray per hand: the closest of the model hit and every interactive panel wins.
    std::array<float,2> panelHitDistance{-1.f,-1.f};
    {
      vr::TrackedDevicePose_t rayPoses[vr::k_unMaxTrackedDeviceCount]{};
      system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,rayPoses,vr::k_unMaxTrackedDeviceCount);
      const auto &rayHead=rayPoses[vr::k_unTrackedDeviceIndex_Hmd];
      for(int hand=0;hand<2;hand++){
        auto &pointer=panelPointers[hand];
        std::vector<panelPointer::Event> pointerEvents;
        const auto *owner=scriptRegistry.find(pointer.current().instanceId);
        const auto *ownerPanel=owner&&pointer.current().panelId&&owner->state==ScriptState::Running?findSceneItem(owner->host.scene.panels,pointer.current().panelId):nullptr;
        if(pointer.current().panelId&&(!ownerPanel||!ownerPanel->visible||!ownerPanel->interactive))pointer.drop(pointerEvents);
        std::optional<panelPointer::Target> target;float targetDistance=-1.f;
        const auto device=system->GetTrackedDeviceIndexForControllerRole(hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand);
        const bool tracked=controllers[hand].valid&&controllers[hand].connected&&device!=vr::k_unTrackedDeviceIndexInvalid&&rayPoses[device].bPoseIsValid&&rayHead.bPoseIsValid;
        if(tracked&&!grabOwners[hand]){
          float origin[3],direction[3];laserRay(rayPoses[device].mDeviceToAbsoluteTracking,hand,origin,direction);
          for(int id:runningSceneIds)if(const auto *entry=scriptRegistry.find(id))for(const auto &panel:entry->host.scene.panels){
            if(!panel.visible||!panel.interactive)continue;
            const auto transform=panelTransform(panel,anchor,rayHead.mDeviceToAbsoluteTracking);
            const auto hit=panelPointer::intersect(origin,direction,transform.m,panel.widthMeters,panel.heightMeters,panel.width,panel.height);
            if(hit&&(targetDistance<0||hit->distance<targetDistance||(hit->distance==targetDistance&&(id<target->instanceId||(id==target->instanceId&&panel.id<target->panelId))))){
              target=panelPointer::Target{id,panel.id,hit->x,hit->y};targetDistance=hit->distance;
            }
          }
        }
        // A press owns the hand until release, so it cannot also start a model grab.
        if(target&&(pointer.pressed()||inputArbitration::panelWins(targetDistance,rayTargets[hand]))){
          rayTargets[hand].reset();rayOwners[hand]=0;panelHitDistance[hand]=targetDistance;
        }else{
          if(pointer.pressed()){rayTargets[hand].reset();rayOwners[hand]=0;}
          target.reset();
        }
        pointer.step(target,tracked&&frameTriggerDown(hand),pointerEvents);
        for(const auto &event:pointerEvents){
          auto *recipient=scriptRegistry.find(event.instanceId);
          if(!recipient||recipient->state!=ScriptState::Running)continue;
          auto &queue=*recipient->host.inputQueue;
          switch(event.type){
            case panelPointer::EventType::HoverStart:queue.addPanelEvent("panel.hoverStart",event.panelId,hand,event.x,event.y);break;
            case panelPointer::EventType::Hover:queue.addPanelEvent("panel.hover",event.panelId,hand,event.x,event.y);break;
            case panelPointer::EventType::HoverEnd:queue.addOverlayEvent("panel.hoverEnd",event.panelId,hand);break;
            case panelPointer::EventType::Down:queue.addPanelEvent("panel.pointerDown",event.panelId,hand,event.x,event.y,"left",true);break;
            case panelPointer::EventType::Up:queue.addPanelEvent("panel.pointerUp",event.panelId,hand,event.x,event.y,"left",false);break;
          }
        }
      }
    }
    auto renderActiveScene=[&](ScriptInstance &instance)->int{
    auto &script=instance.host;
    const int scriptInstanceId=instance.id;
    auto &sceneRender=*renderStates.at(scriptInstanceId);
    auto &sceneResources=*resourcesByInstance.at(scriptInstanceId);
    auto &surfaces=sceneResources.surfaces;
    auto &panelOverlays=sceneResources.panelOverlays;
    auto &panelSubmissions=sceneResources.panelSubmissions;
    auto &spotShadows=sceneRender.spotShadows;
    auto &pbrVariants=sceneRender.pbrVariants;
    auto &grabs=sceneRender.grabs;
    auto &pendingTileChange=sceneRender.pendingTileChange;
    auto &stableAdjustment=sceneRender.stableAdjustment;
    auto &adjustmentCursor=sceneRender.adjustmentCursor;
    auto &regroupAfter=sceneRender.regroupAfter;
    auto &nextAdaptationCheck=sceneRender.nextAdaptationCheck;
    auto &nextGroupResize=sceneRender.nextGroupResize;
    auto &ready=sceneRender.ready;
    auto &frameBounds=sceneRender.frameBounds;
    auto &frameObjectLights=sceneRender.frameObjectLights;
    auto &instanceExperiment=sceneRender.instanceExperiment;
    auto &lastAnimationTick=sceneRender.lastAnimationTick;
    ScriptScope sceneScope(script);
    const auto animationTick=std::chrono::steady_clock::now();
    const double animationDt=std::chrono::duration<double>(animationTick-lastAnimationTick).count();lastAnimationTick=animationTick;
    for(auto &object:script.scene.objects)if(object.animation.clip>=0){
      const auto &asset=script.scene.assets[size_t(object.asset)-1];
      if(animation::advancePlayback(object.animation,asset.animations,animationDt))evaluateModelPose(asset,object.animation,*object.animationPose);
    }
    if(!syncSurfaces(script,sceneResources)){running=false;return -1;}
    syncPanels(script,sceneResources,sceneRender);
    for(auto &[id,surface]:surfaces){
      if(id<=0)continue;
      vr::VREvent_t event{};
      auto &state=pointerStates[id];
      state.scrollX=state.scrollY=0;
      while(overlays->PollNextOverlayEvent(surface->overlay,&event,sizeof(event))){
        if(event.eventType==vr::VREvent_FocusLeave){state.valid=false;state.down=false;state.secondaryDown=false;if(state.hand>=0&&hoveredObject[state.hand]==id){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",id,state.hand);hoveredObject[state.hand]=0;}state.hand=-1;continue;}
        if(event.eventType==vr::VREvent_ScrollSmooth){
          state.scrollX+=event.data.scroll.xdelta;
          state.scrollY+=event.data.scroll.ydelta;
          continue;
        }
        if(event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonDown||event.eventType==vr::VREvent_MouseButtonUp){
          state.valid=true;
          const auto eventRole=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
          const int eventHand=eventRole==vr::TrackedControllerRole_LeftHand?0:eventRole==vr::TrackedControllerRole_RightHand?1:-1;
          if(eventHand<0)continue;
          if(eventHand>=0&&event.eventType==vr::VREvent_MouseMove&&hoveredObject[eventHand]!=id){if(hoveredObject[eventHand])sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[eventHand],eventHand);hoveredObject[eventHand]=id;sceneInput().addOverlayEvent("overlay.surfaceHoverStart",id,eventHand);}
          sceneInput().addOverlayEvent(event.eventType==vr::VREvent_MouseMove?"overlay.surfaceHover":"overlay.pointer",id,eventHand);
          state.x=(event.data.mouse.x-.5f)*surface->width;
          state.y=(.5f-event.data.mouse.y)*surface->width;
          if(event.eventType==vr::VREvent_MouseButtonDown){
            if(event.data.mouse.button==vr::VRMouseButton_Left)state.down=true;
            if(event.data.mouse.button==vr::VRMouseButton_Right)state.secondaryDown=true;
            auto role=system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex);
            state.hand=role==vr::TrackedControllerRole_LeftHand?0:role==vr::TrackedControllerRole_RightHand?1:-1;
          }
          if(event.eventType==vr::VREvent_MouseButtonUp){
            if(event.data.mouse.button==vr::VRMouseButton_Left)state.down=false;
            if(event.data.mouse.button==vr::VRMouseButton_Right)state.secondaryDown=false;
          }
          if(event.eventType!=vr::VREvent_MouseMove){fprintf(stdout,"[input] object=%d event=%u button=%u device=%u hand=%d down=%d\n",id,event.eventType,event.data.mouse.button,event.trackedDeviceIndex,state.hand,state.down);fflush(stdout);}
        }
      }
    }
    if(script.scene.eyeTrackingRequested){
      vr::TrackedDevicePose_t headPoses[vr::k_unMaxTrackedDeviceCount]{};
      system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,headPoses,vr::k_unMaxTrackedDeviceCount);
      const auto &head=headPoses[vr::k_unTrackedDeviceIndex_Hmd];headBasisValid=head.bPoseIsValid&&head.bDeviceIsConnected;
      if(headBasisValid)for(int r=0;r<3;r++)for(int c=0;c<3;c++){
        float value=0;for(int k=0;k<3;k++)value+=anchor.m[k][r]*head.mDeviceToAbsoluteTracking.m[k][c];
        headBasis[r][c]=value*(r==2?-1:1)*(c==2?-1:1);
      }
    }else headBasisValid=false;
    if(streamWasStarted&&!frameInput.started()){
      for(int h=0;h<2;h++)grabNeedsRelease[h]=true;
      for(const auto &object:script.scene.objects){forgetObjectInteraction(object.id);if(script.scene.physics)script.scene.physics->cancelThrow(object.id);}
    }
    for(int h=0;h<2;h++)if(!controllers[h].connected){
      frameInput.invalidateHand(h);
      sceneInput().invalidateHand(h);
      for(auto &[id,pointer]:pointerStates)if(pointer.hand==h)pointer={};
      if(hoveredObject[h]){sceneInput().addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[h],h);hoveredObject[h]=0;}
    }
    const auto pickingStart=std::chrono::steady_clock::now();
    for(int hand=0;hand<2;hand++){
      const bool ownsRay=rayOwners[hand]==instance.id;
      int hit=ownsRay&&rayTargets[hand]?rayTargets[hand]->localId:0;
      float nearest=ownsRay&&rayTargets[hand]?rayTargets[hand]->distance:std::numeric_limits<float>::max();
      meshCurrentObject[hand]=hit;
      const auto now=std::chrono::steady_clock::now();
      const auto *previousHit=findSceneItem(script.scene.objects,meshHoveredObject[hand]);
      if(hit)meshLastHit[hand]=now;
      else if(ownsRay&&controllers[hand].connected&&previousHit&&previousHit->visible&&now-meshLastHit[hand]<std::chrono::milliseconds(100)){
        hit=meshHoveredObject[hand];nearest=meshHitDistance[hand];
      }
      if(hit!=meshHoveredObject[hand]){
        if(meshHoveredObject[hand])sceneInput().addOverlayEvent("overlay.meshHoverEnd",meshHoveredObject[hand],hand);
        if(hit)sceneInput().addOverlayEvent("overlay.meshHoverStart",hit,hand);
        meshHoveredObject[hand]=hit;
      }
      meshHitDistance[hand]=std::min(nearest,2.f);
    }
    pickingMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
    const auto reconnectNow=std::chrono::steady_clock::now();
    if(!frameInput.started()&&reconnectNow>=reconnectAt){frameInput.reconnect();reconnectAt=reconnectNow+std::chrono::seconds(1);}
    auto failScene=[&](const std::string &error){
      fprintf(stderr,"[lifecycle] instance=%d stopped after error: %s\n",instance.id,error.c_str());
      scriptRegistry.failActive(instance.id,error);
      return 0;
    };
    if(const auto error=dispatchInputCallbacks();!error.empty())return failScene(error);
    updateManipulation(script.scene,grabs,1.f/90.f,instance.id,rayOwners,grabOwners);
    for(int hand=0;hand<2;hand++)if(grabs[hand].objectId)meshHitDistance[hand]=grabs[hand].laserDistance;
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);JS_FreeValue(script.ctx,dt);
      if(JS_IsException(result)){
        JSValue exception=JS_GetException(script.ctx);
        const char *message=JS_ToCString(script.ctx,exception);
        std::string error=message?message:"update failed";
        if(message)JS_FreeCString(script.ctx,message);
        JS_FreeValue(script.ctx,exception);JS_FreeValue(script.ctx,result);
        return failScene(error);
      }
      JS_FreeValue(script.ctx,result);
    }
    const auto physicsStart=std::chrono::steady_clock::now();
    try{updateScenePhysics(script.scene,animationDt);}
    catch(const std::exception &error){return failScene(error.what());}
    physicsMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-physicsStart).count();
    if(script.scene.assetsReleased){
      instanceExperiment.reset();
#ifdef SF_GPU_SYNC_EXPERIMENT
      primitiveExperiment.reset();clusterExperiment.reset();
#endif
      script.scene.assetsReleased=false;
    }
    for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);
    for(auto &object:script.scene.objects)if(object.animationPose)uploadModelPose(*object.animationPose);
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
#ifdef SF_PBR_MINIMAL_DEPTH_EXPERIMENT
      if(fixedDepthView){head=anchor;head.m[1][3]=.2f;head.m[2][3]=1.f;}
#endif
      const auto renderStart=std::chrono::steady_clock::now();
      float eyes[2][3];
      for(int i=0;i<2;i++){
        const auto eyeTransform=system->GetEyeToHeadTransform((vr::EVREye)i);
        for(int r=0;r<3;r++)eyes[i][r]=head.m[r][3]+head.m[r][0]*eyeTransform.m[0][3]+head.m[r][1]*eyeTransform.m[1][3]+head.m[r][2]*eyeTransform.m[2][3];
      }
      static float focal=0;
      if(focal==0||frame%90==0){uint32_t viewWidth=0,viewHeight=0;system->GetRecommendedRenderTargetSize(&viewWidth,&viewHeight);focal=0;
        for(int eye=0;eye<2;eye++){float l,r,t,b;system->GetProjectionRaw(vr::EVREye(eye),&l,&r,&t,&b);focal=std::max(focal,std::max(float(viewWidth)/std::abs(r-l),float(viewHeight)/std::abs(b-t)));}
      }
      for(auto &object:script.scene.objects){
        const auto &asset=script.scene.assets[size_t(object.asset)-1];
        auto selectDetail=[&](int level){
#ifdef SF_PBR_LOD_EXPERIMENT
          if(asset.pbr&&object.lodLevel!=level){size_t triangles=0;for(const auto &part:asset.primitives)triangles+=modelDrawVertexCount(part,level)/3;fprintf(stdout,"[pbr-lod] object=%d level=%d triangles=%zu held=%d\n",object.id,level,triangles,grabs[0].objectId==object.id||grabs[1].objectId==object.id);fflush(stdout);}
#endif
          object.lodLevel=level;
        };
        const bool available=asset.lod&&!object.animationPose&&std::any_of(asset.primitives.begin(),asset.primitives.end(),[](const auto &p){return p.lod[0].indexBuffer||p.lod[1].indexBuffer||p.lod[0].vbo||p.lod[1].vbo;});
        if(!available||grabs[0].objectId==object.id||grabs[1].objectId==object.id){selectDetail(0);continue;}
        float center[3];for(int row=0;row<3;row++)center[row]=anchor.m[row][3]+anchor.m[row][0]*object.x+anchor.m[row][1]*object.y-anchor.m[row][2]*object.z;
        float extent=0;for(int axis=0;axis<3;axis++){const float e=asset.high[axis]-asset.low[axis];extent+=e*e;}
        const float radius=.5f*std::abs(object.scale)*std::sqrt(extent);float nearest=std::numeric_limits<float>::infinity(),pixels=0;
        for(const auto &eye:eyes){float squared=0,depth=0;for(int axis=0;axis<3;axis++){const float d=center[axis]-eye[axis];squared+=d*d;depth-=d*head.m[axis][2];}nearest=std::min(nearest,std::sqrt(squared)-radius);pixels=std::max(pixels,modelLod::projectedDiameter(radius,focal,squared,depth));}
        selectDetail(modelLod::select(pixels,nearest,object.lodLevel));
      }
      spotShadows.update(script.scene);
      const auto frameLights=resolvePointLights(script.scene);
      updateObjectLights(script.scene,frameLights,frameObjectLights);
      frameBounds.update(script.scene,anchor,head,eyes);
      const bool useInstancing=std::any_of(script.scene.assets.begin(),script.scene.assets.end(),[](const ModelAsset &asset){return asset.gpuInstancing;});
#ifdef SF_GPU_SYNC_EXPERIMENT
#ifdef SF_MODEL_LOD_EXPERIMENT
      if(lodExperiment::ratio()!=1&&(useInstancing||primitiveVisibility||clusterExperiment.mode!=ClusterVisibilityExperiment::Off||specializeOpaque||std::getenv("SF_GPU_MULTIPART_INSTANCING"))){fprintf(stderr,"LOD comparison requires original object draws without other geometry/shader experiments\n");return 13;}
#endif
      if(program!=generalProgram&&(staticVertexOnly||specializeOpaque||primitiveVisibility||clusterExperiment.mode!=ClusterVisibilityExperiment::Off)){fprintf(stderr,"ordinary routing comparison cannot combine with other shader/visibility experiments\n");return 13;}
      if(staticVertexOnly&&std::any_of(script.scene.assets.begin(),script.scene.assets.end(),[](const auto &asset){return asset.hasSkin;})){fprintf(stderr,"static vertex experiment cannot render skinned assets\n");return 13;}
      if(useInstancing&&(primitiveVisibility||clusterExperiment.mode!=ClusterVisibilityExperiment::Off||specializeOpaque)){fprintf(stderr,"instancing cannot combine with other geometry/shader experiments\n");return 13;}
      if(primitiveVisibility)primitiveExperiment.update(script.scene,anchor,head,eyes);
      if(clusterExperiment.mode!=ClusterVisibilityExperiment::Off)clusterExperiment.update(script.scene,anchor,head,eyes);
#endif
      if(!pendingTileChange&&std::chrono::steady_clock::now()>=nextAdaptationCheck){
        const auto now=std::chrono::steady_clock::now();nextAdaptationCheck=now+std::chrono::milliseconds(500);
        const auto candidates=surfaceMembers(script);
        bool allocationFailed=false;
        auto startChange=[&](int key,int childKey,const GroupSplitPlan &plan,bool merge,bool rebalance,size_t tileCount){
          const int group=surfaceRenderGroup(key);auto child=std::make_unique<Surface>();Surface parent;
          const auto extra=GroupBufferBudget::bytes(plan.pixels[0])+(merge?0:GroupBufferBudget::bytes(plan.pixels[1]));
          if(!groupBufferBudget.fits(extra)){
            allocationFailed=true;nextAdaptationCheck=now+std::chrono::seconds(5);
            fprintf(stderr,"[surface] group=%d adaptation budget deferred; retaining existing tiles\n",group);return;
          }
          static uint64_t generation=0;const auto suffix=".adapt."+std::to_string(++generation);
          if(!createSurface(parent,scriptInstanceId,key,plan.widths[0],plan.pixels[0],false,gbm,display,anchor,suffix)||
             (!merge&&!createSurface(*child,scriptInstanceId,childKey,plan.widths[1],plan.pixels[1],false,gbm,display,anchor,suffix))){
            destroySurface(parent,display);destroySurface(*child,display);allocationFailed=true;nextAdaptationCheck=now+std::chrono::seconds(5);
            fprintf(stderr,"[surface] group=%d adaptation allocation unavailable; retaining existing tiles\n",group);return;
          }
          parent.previous=std::move(surfaces.at(key));
          if(merge){parent.previous->previous=std::move(surfaces.at(childKey));surfaces.erase(childKey);}
          else{
            if(rebalance)child->previous=std::move(surfaces.at(childKey));
            surfaces.insert_or_assign(childKey,std::move(child));
          }
          surfaces.at(key)=std::make_unique<Surface>(std::move(parent));
          for(int side=0;side<(merge?1:2);side++)for(const auto *object:plan.members[side]){
            const int tileKey=side?childKey:key;script.scene.objects[size_t(object-script.scene.objects.data())].renderTile=(-tileKey-group)/256;
          }
          pendingTileChange=SceneRenderState::PendingTileChange{key,childKey,merge};stableAdjustment.reset();regroupAfter[group]=now+std::chrono::seconds(2);
          fprintf(stdout,"[surface] group=%d %s tiles=%zu parent_pixels_squared=%llu child_pixels_squared=%llu\n",group,merge?"merge":rebalance?"rebalance":"split",tileCount,(unsigned long long)plan.parentPixelsSquared,(unsigned long long)plan.childPixelsSquared);fflush(stdout);
        };
        std::set<int> hiddenTiles;for(const auto &object:script.scene.objects)if(object.renderGroup&&!object.visible)hiddenTiles.insert(objectSurfaceKey(object));
        std::vector<std::pair<int,int>> pairs;
        auto eligible=[&](int key){auto found=surfaces.find(key);return found!=surfaces.end()&&found->second->buffers[0].fbo&&!found->second->previous&&!hiddenTiles.count(key);};
        for(auto a=candidates.begin();a!=candidates.end();++a){
          const int group=surfaceRenderGroup(a->first);auto policy=script.scene.renderGroupSplits.find(group);
          if(!group||policy==script.scene.renderGroupSplits.end()||(!policy->second.merge&&!policy->second.rebalance)||now<regroupAfter[group]||!eligible(a->first))continue;
          for(auto b=std::next(a);b!=candidates.end();++b)if(surfaceRenderGroup(b->first)==group&&eligible(b->first))pairs.emplace_back(a->first,b->first);
        }
        auto sticky=std::find(pairs.begin(),pairs.end(),std::pair{stableAdjustment.first,stableAdjustment.second});
        if(sticky==pairs.end())stableAdjustment.reset();
        bool proposed=false;
        for(size_t attempt=0;attempt<std::min(size_t(4),pairs.size());attempt++){
          const auto pair=attempt==0&&sticky!=pairs.end()?*sticky:pairs[adjustmentCursor++%pairs.size()];
          const auto [a,b]=pair;const int group=surfaceRenderGroup(a);
          const auto plan=planGroupAdjustment(candidates.at(a),candidates.at(b),script.scene,anchor,head,eyes,frameBounds,maxGroupPixels,script.scene.renderGroupSplits.at(group));
          if(!plan){if(a==stableAdjustment.first&&b==stableAdjustment.second)stableAdjustment.reset();continue;}
          proposed=true;
          if(stableAdjustment.observe(a,b,plan,now)){
            std::set<int> tiles;for(const auto &object:script.scene.objects)if(object.renderGroup==group)tiles.insert(object.renderTile);
            startChange(a,b,plan.partition,plan.merge,!plan.merge,tiles.size()-(plan.merge?1:0));
          }
          break;
        }
        if(!proposed)stableAdjustment.reset();
        if(!pendingTileChange&&!allocationFailed)for(const auto &[key,members]:candidates){
          const int group=surfaceRenderGroup(key);auto policy=script.scene.renderGroupSplits.find(group);auto found=surfaces.find(key);
          if(!group||policy==script.scene.renderGroupSplits.end()||found==surfaces.end()||!found->second->buffers[0].fbo||found->second->previous||surfaceRenderGroup(stableAdjustment.first)==group)continue;
          std::set<int> tiles;int nextTile=0;for(const auto &object:script.scene.objects)if(object.renderGroup==group)tiles.insert(object.renderTile);
          if(tiles.size()>=size_t(policy->second.maxSurfaces))continue;while(tiles.count(nextTile))++nextTile;
          const auto plan=planGroupSplit(members,script.scene,anchor,head,eyes,frameBounds,maxGroupPixels,found->second->eyePixels,policy->second);
          if(!plan)continue;startChange(key,-(group+256*nextTile),plan,false,false,tiles.size()+1);break;
        }
      }
      ready.clear();
      ready.reserve(surfaces.size());
      for(const auto &panelData:script.scene.panels){
        auto overlay=panelOverlays.find(panelData.id);if(overlay==panelOverlays.end())continue;
        const auto transform=panelData.visible&&panelData.texture?panelTransform(panelData,anchor,head):vr::HmdMatrix34_t{};
        const auto error=submitPanel(*overlays,overlay->second,panelData,transform,panelSubmissions[panelData.id]);
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[panel] submit id=%d error=%d\n",panelData.id,int(error));
      }
      const auto groups=surfaceMembers(script);
      int shrinkBudget=2;std::set<int> culledSurfaces;
#ifdef SF_GPU_SYNC_EXPERIMENT
      gpuSync.scene(script.scene);gpuSync.begin();
      if(specializeOpaque)for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives)if(part.alphaMode!=0){fprintf(stderr,"opaque shader experiment requires opaque-only assets\n");return 13;}
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
      uint64_t uniformIssued=0,uniformSkipped=0;
#endif
#ifdef SF_RIGID_BATCH_EXPERIMENT
      rigidBatchStats={};
#endif
      for(auto &[id,surface]:surfaces){
        if(!surface->buffers[0].fbo)continue; // A handle reserved during this update initializes next frame.
        auto group=groups.find(id);if(group==groups.end())continue;
        const auto &members=group->second;const auto *target=members.front();
        // Hide only replacement fronts before reusing their buffers. Retained
        // cohort snapshots stay visible until the complete handoff succeeds.
        const bool changing=pendingTileChange&&(id==pendingTileChange->parent||(!pendingTileChange->merge&&id==pendingTileChange->child));
        if(changing&&hideSurfaceFront(*overlays,*surface)!=vr::VROverlayError_None)continue;
        float animatedWidth=std::max(.55f,target->scale*2.1f*objectRadius(*target));
        float surfaceX=target->x,surfaceY=target->y,surfaceZ=target->z;
        auto panel=anchor;
        float panelRight[3],panelUp[3],panelForward[3];
        if(id<0){
          const auto projection=groupPlane(members,script.scene,anchor,head,eyes,&frameBounds);
          panel=projection.transform;surfaceX=projection.sceneOrigin[0];surfaceY=projection.sceneOrigin[1];surfaceZ=projection.sceneOrigin[2];
          std::copy_n(projection.right,3,panelRight);std::copy_n(projection.up,3,panelUp);std::copy_n(projection.forward,3,panelForward);
          for(const auto *member:members){const float dx=member->x-surfaceX,dy=member->y-surfaceY,dz=member->z-surfaceZ;animatedWidth=std::max(animatedWidth,2.1f*(std::sqrt(dx*dx+dy*dy+dz*dz)+member->scale*objectRadius(*member)));}
        }else{
          for(int r=0;r<3;r++)panel.m[r][3]+=anchor.m[r][0]*surfaceX+anchor.m[r][1]*surfaceY-anchor.m[r][2]*surfaceZ;
          faceViewer(panel,anchor,head,panelRight,panelUp,panelForward);
        }
        float distanceSquared=0,facing=0;
        for(int r=0;r<3;r++){float delta=panel.m[r][3]-head.m[r][3];distanceSquared+=delta*delta;facing-=delta*head.m[r][2];}
        if(facing<.25f*std::sqrt(distanceSquared)-(id<0?animatedWidth*.5f:target->scale*(target->animationPose?objectRadius(*target):.6f))){
          const auto error=changing?hideSurfaceFront(*overlays,*surface):hideSurface(*overlays,*surface);
          if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] hide id=%d error=%d\n",id,int(error));
          else culledSurfaces.insert(id);
          continue;
        }
        const float surfaceCenter[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]};
        ProjectedBounds targetBounds[2];if(id>0)for(int eye=0;eye<2;eye++)targetBounds[eye]=frameBounds.project(*target,eyes[eye],panelRight,panelUp,panelForward);
        int crop=id>0?objectCropPixels(targetBounds,surfaceCenter,eyes,panelRight,panelUp,panelForward,animatedWidth,surface->referenceEyePixels):surface->referenceEyePixels;
        if(id<0){
          const auto layout=groupLayout(members,script.scene,anchor,head,eyes,surfaceCenter,panelRight,panelUp,panelForward,animatedWidth,maxGroupPixels,&frameBounds);
          if(layout.limited&&!surface->resolutionLimited)fprintf(stderr,"[surface] group=%d reaches %d pixels per eye; use smaller groups to retain density\n",surfaceRenderGroup(id),maxGroupPixels);
          surface->resolutionLimited=layout.limited;
          animatedWidth=layout.width;crop=layout.pixels;
          targetBounds[0]=layout.bounds[0];targetBounds[1]=layout.bounds[1];
          surface->referenceEyePixels=crop;
        }
        // Grow immediately when needed; shrink only after 45 consecutive frames.
        if(crop<(id<0?surface->eyePixels*.6f:surface->eyePixels))++surface->shrinkFrames;else surface->shrinkFrames=0;
        if(crop>surface->eyePixels||(surface->shrinkFrames>=45&&shrinkBudget>0)){
          if(crop<surface->eyePixels)--shrinkBudget;
          if(id<0){
            const auto now=std::chrono::steady_clock::now();
            if(!surface->previous&&!pendingTileChange&&now>=surface->resizeAfter&&now>=nextGroupResize){
              // At most one group resize per 100 ms, including allocation failure.
              // Preflight the complete generation before creating any buffers.
              const int requested=groupBufferPixels(crop,maxGroupPixels);
              if(requested!=surface->eyePixels){
                const int pixels=groupBufferBudget.resizePixels(requested,surface->eyePixels);
                nextGroupResize=now+std::chrono::milliseconds(100);
                if(!pixels||!replaceGroupedSurface(*surface,pixels,gbm,display,panel)){
                  surface->resizeAfter=now+std::chrono::seconds(5);
                  fprintf(stderr,"[surface] replacement id=%d deferred; retaining buffers, retry in 5s\n",id);
                }else if(pixels<requested){
                  surface->resizeAfter=now+std::chrono::seconds(5);
                  fprintf(stderr,"[surface] replacement id=%d budget limited eye_px=%d requested=%d; retry in 5s\n",id,pixels,requested);
                }
              }
            }
          }else if(!resizeSurfaceBuffers(*surface,crop,gbm,display)){fprintf(stderr,"[surface] resize id=%d failed\n",id);running=false;break;}
          surface->shrinkFrames=0;
        }
        // Moving bounds still fit when growth is throttled or cannot be admitted.
        // Render the complete group at temporarily reduced density, never crop it.
        const float width=surfaceCoverageWidth(animatedWidth,surface->eyePixels,surface->referenceEyePixels,id<0);
        const auto placementError=prepareSurface(*overlays,*surface,width,panel);
        if(placementError!=vr::VROverlayError_None){fprintf(stderr,"[surface] placement id=%d error=%d\n",id,int(placementError));continue;}
        setVec3(centerLocation,panel.m[0][3],panel.m[1][3],panel.m[2][3]);
        setVec3(panelRightLocation,panelRight[0],panelRight[1],panelRight[2]);
        setVec3(panelUpLocation,panelUp[0],panelUp[1],panelUp[2]);
        setVec3(panelForwardLocation,panelForward[0],panelForward[1],panelForward[2]);
        glUniform1f(panelHalfWidthLocation,surface->width*.5f);
        Buffer &buffer=surface->buffers[frame%3];
        glBindFramebuffer(GL_FRAMEBUFFER,buffer.fbo);glViewport(0,0,2*surface->eyePixels,surface->eyePixels);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        for(int i=0;i<2;i++){
          setVec3(eyeLocation,eyes[i][0],eyes[i][1],eyes[i][2]);glViewport(i*surface->eyePixels,0,surface->eyePixels,surface->eyePixels);
          auto region=projectedScissor(targetBounds[i],surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels);
          region.x+=i*surface->eyePixels;
          renderObjectLayers(id,[&](int targetId,int pass){
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
            ModelDrawUniforms drawUniforms(drawLocations,drawUniformCachingEnabled);
            ScopeExit countUniforms{[&]{uniformIssued+=drawUniforms.issued;uniformSkipped+=drawUniforms.skipped;}};
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
            gpuSync.beginPass(pass);ScopeExit finishPass{[&]{gpuSync.endPass();}};
            auto *special=specializeOpaque?(pass<0?depthExperiment.get():opaqueExperiment.get()):nullptr;
            ScopeExit restoreProgram{[&]{if(special)glUseProgram(program);}};
            if(special)special->view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);
#endif
            ModelCulling culling;
            auto selected=[&](const Object &object){
              if(!object.visible||(pass<0?objectSurfaceKey(object)==targetId:objectSurfaceKey(object)!=targetId))return false;
              if(pass<0&&!mayOcclude(id<0?frameBounds.common(object,i):frameBounds.project(object,eyes[i],panelRight,panelUp,panelForward),targetBounds[i]))return false;
              return true;
            };
#ifdef SF_GPU_SYNC_EXPERIMENT
            bool skinBound=false;
            ScopeExit restoreSkinProgram{[&]{if(skinBound)glUseProgram(program);}};
            auto restoredProgram=[&]{skinBound=false;};
#else
            auto restoredProgram=[]{};
#endif
            auto drawObject=[&](const Object &object){
              const auto &asset=script.scene.assets[(size_t)object.asset-1];
#ifdef SF_GPU_SYNC_EXPERIMENT
              const bool useSkinProgram=program!=generalProgram&&object.animationPose&&object.animationPose->gpuSkin;
              if(useSkinProgram){
                if(!skinBound){skinProgram.view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);skinBound=true;}
                skinProgram.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
              }else if(skinBound){glUseProgram(program);skinBound=false;}
#endif
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
              if(drawUniforms.enabled&&std::none_of(asset.primitives.begin(),asset.primitives.end(),[&](const auto &p){return pass<0?p.alphaMode!=2:(p.alphaMode==2)==bool(pass);}))return;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
              if(special)special->object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);else
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
              if(!useSkinProgram)
#endif
              {
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
                drawUniforms.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
#else
                glUniform1f(yawLocation,object.yaw);glUniform1f(pitchLocation,object.pitch);glUniform1f(rollLocation,object.roll);glUniform1f(scaleLocation,object.scale);
                glUniform3f(offsetLocation,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
#endif
              }
              for(size_t primitive=0;primitive<asset.primitives.size();primitive++){
                const auto &part=asset.primitives[primitive];
                if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(primitiveVisibility&&pass<0&&!primitiveExperiment.mayDraw(object,primitive,targetBounds[i],i,id<0,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels))continue;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(special)special->material(part);else
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(useSkinProgram)skinProgram.material(part);else
#endif
                {
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
                  drawUniforms.material(part);
#else
                  glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);glUniform3fv(sheenLocation,1,part.sheen);
#endif
                }
                culling.material(part,object.animationPose.get());
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(pass<0&&clusterExperiment.draw(object,primitive,targetBounds[i],i,id<0,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,surface->eyePixels,[&](unsigned count){gpuSync.draw(count);}))continue;
#endif
                if(asset.pbr){
#ifdef SF_GPU_SYNC_EXPERIMENT
                  gpuSync.beginPbr();ScopeExit finishPbrTiming{[&]{gpuSync.endPbr();}};
#endif
                  auto *variant=pbrVariantsEnabled&&pass>=0?pbrVariants.ready(asset,part,frameObjectLights[size_t(&object-script.scene.objects.data())].count,surface->eyePixels):nullptr;
                  auto &activePbr=variant?*variant:(fastPbrDepth&&pass<0?selectedDepthProgram:pbrProgram);
                  activePbr.view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);
                  activePbr.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
                  if(pass>=0)activePbr.lights(frameObjectLights[size_t(&object-script.scene.objects.data())].values(),surfaceX,surfaceY,surfaceZ);
                  if(pass>=0)activePbr.shadows(spotShadows,frameObjectLights[size_t(&object-script.scene.objects.data())].values(),surfaceX,surfaceY,surfaceZ);
                  activePbr.material(asset,part,object.animationPose?object.animationPose->geometry.primitives[primitive]:part,object.animationPose?0:object.lodLevel,pass<0);
                  ScopeExit restorePbr{[&]{activePbr.release();glUseProgram(program);}};
                  if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);else drawModelPrimitive(part,0,object.lodLevel);
#ifdef SF_GPU_SYNC_EXPERIMENT
                  gpuSync.draw(modelDrawVertexCount(part,object.animationPose?0:object.lodLevel));
#endif
                  continue;
                }
                GLuint activeProgram=program;
#ifdef SF_GPU_SYNC_EXPERIMENT
                if(useSkinProgram)activeProgram=generalProgram;
#endif
                if(object.animationPose&&bindSkinPrimitive(asset,part,*object.animationPose,activeProgram)){
                  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glDrawElements(GL_TRIANGLES,GLsizei(part.skinIndices.size()),GL_UNSIGNED_INT,nullptr);unbindSkinPrimitive(activeProgram);
#ifdef SF_GPU_SYNC_EXPERIMENT
                  gpuSync.draw(unsigned(part.skinIndices.size()));
#endif
                  continue;
                }
#ifdef SF_GPU_SYNC_EXPERIMENT
                const size_t firstPrimitive=primitive;
#endif
                if(object.animationPose&&drawRigidBatch(asset,*object.animationPose,primitive)){
#ifdef SF_GPU_SYNC_EXPERIMENT
                  unsigned count=0;for(size_t p=firstPrimitive;p<=primitive;p++)count+=asset.primitives[p].vertexCount;gpuSync.draw(count);
#endif
                  continue;
                }
#ifdef SF_GPU_SYNC_EXPERIMENT
                gpuSync.draw(modelDrawVertexCount(part,object.animationPose?0:object.lodLevel));
#endif
                if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);
                else drawModelPrimitive(part,0,object.lodLevel);
              }
            };
            if(useInstancing){
              std::vector<const Object*> objects;objects.reserve(script.scene.objects.size());for(const auto &object:script.scene.objects)if(selected(object))objects.push_back(&object);
              const float origin[3]={surfaceX,surfaceY,surfaceZ};
              instanceExperiment.draw(script.scene,culling,objects,pass,program,anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,origin,drawObject,[&](unsigned count){
#ifdef SF_GPU_SYNC_EXPERIMENT
                gpuSync.draw(count);
#else
                (void)count;
#endif
              },restoredProgram);return;
            }
            for(const auto &object:script.scene.objects)if(selected(object))drawObject(object);
          },&region);
        }
        ready.push_back(surface.get());
      }
#ifdef SF_DRAW_SUBMIT_EXPERIMENT
      if(frame%120==0)std::fprintf(stdout,"DRAW_UNIFORMS {\"frame\":%d,\"issued\":%llu,\"skipped\":%llu}\n",frame,(unsigned long long)uniformIssued,(unsigned long long)uniformSkipped);
#endif
#ifdef SF_RIGID_BATCH_EXPERIMENT
      if(frame%120==0)std::fprintf(stdout,"RIGID_BATCH_DRAWS {\"draws\":%llu,\"mergedParts\":%llu}\n",(unsigned long long)rigidBatchStats.draws,(unsigned long long)rigidBatchStats.mergedParts);
#endif
      const auto finishStart=std::chrono::steady_clock::now();
      const double frameRenderMs=std::chrono::duration<double,std::milli>(finishStart-renderStart).count();renderMs+=frameRenderMs;
#ifdef SF_ANIMATION_PROFILE
      animationFrame.renderMs+=frameRenderMs;
#endif
#ifdef SF_GPU_SYNC_EXPERIMENT
      gpuSync.complete();
      if(useInstancing)instanceExperiment.report();
      if(primitiveVisibility)primitiveExperiment.report();
      if(clusterExperiment.mode!=ClusterVisibilityExperiment::Off)clusterExperiment.report();
#else
      glFinish();
#endif
      const double frameFinishMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-finishStart).count();finishMs+=frameFinishMs;
#ifdef SF_ANIMATION_PROFILE
      animationFrame.finishMs+=frameFinishMs;
#endif
      glBindFramebuffer(GL_FRAMEBUFFER,0);
      std::set<int> changingSubmissions;
      if(pendingTileChange){
        const auto parent=surfaces.find(pendingTileChange->parent),child=surfaces.find(pendingTileChange->child);
        const int group=surfaceRenderGroup(pendingTileChange->parent);
        if(parent==surfaces.end()||(!pendingTileChange->merge&&child==surfaces.end()))pendingTileChange.reset();
        else{
          changingSubmissions.insert(pendingTileChange->parent);if(!pendingTileChange->merge)changingSubmissions.insert(pendingTileChange->child);
          const bool parentVisible=std::find(ready.begin(),ready.end(),parent->second.get())!=ready.end();
          const bool childVisible=!pendingTileChange->merge&&std::find(ready.begin(),ready.end(),child->second.get())!=ready.end();
          if((parentVisible||culledSurfaces.count(pendingTileChange->parent))&&(pendingTileChange->merge||childVisible||culledSurfaces.count(pendingTileChange->child))){
            auto retire=[&](Surface &retired){destroySurface(retired,display);};vr::EVROverlayError error=vr::VROverlayError_None;
            if(pendingTileChange->merge){
              if(parentVisible)error=submitSurface(*overlays,*parent->second,frame,retire);
              else if(parent->second->previous){retire(*parent->second->previous);parent->second->previous.reset();}
            }else error=submitGroupSplit(*overlays,*parent->second,*child->second,frame,retire,parentVisible,childVisible);
            if(error==vr::VROverlayError_None){pendingTileChange.reset();regroupAfter[group]=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
            else fprintf(stderr,"[surface] adaptation presentation error=%d; retaining complete fallbacks\n",int(error));
          }
        }
      }
      for(auto *surface:ready){
        if(changingSubmissions.count(surface->objectId))continue;
        const auto error=submitSurface(*overlays,*surface,frame,[&](Surface &retired){destroySurface(retired,display);});
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] submit id=%d error=%d\n",surface->objectId,int(error));
      }
#ifdef SF_GPU_SYNC_EXPERIMENT
      gpuSync.submitted();
#endif
      const int completedFrame=frame+1;
      if(completedFrame%900==0&&pbrVariantsEnabled)fprintf(stdout,"[pbr] prepared=%zu ready_draws=%zu fallback_draws=%zu\n",pbrVariants.programs.size(),pbrVariants.readyDraws,pbrVariants.fallbackDraws);
    }
      return 0;
    };
    int sceneResult=0;
    for(int id:scriptRegistry.runningIds()){
      auto *instance=scriptRegistry.find(id);
      if(!instance||instance->state!=ScriptState::Running)continue;
      if(!renderStates.count(id)){
        auto owner=std::make_unique<SceneRenderState>();
        if(!owner->spotShadows.prepare())return 5;
#ifdef SF_GPU_SYNC_EXPERIMENT
        if(std::getenv("SF_GPU_MULTIPART_INSTANCING"))owner->instanceExperiment.enableMultipartExperiment();
        if(instanceMode){owner->instanceExperiment.mode=std::strcmp(instanceMode,"1")==0?ModelInstancing::Instanced:ModelInstancing::Original;owner->instanceExperiment.prewarm();}
#endif
        if(pbrVariantsEnabled){
          for(auto &asset:instance->host.scene.assets)if(!asset.uploaded)uploadModel(asset);
          owner->pbrVariants.prepare(instance->host.scene);
        }
        renderStates.emplace(id,std::move(owner));
      }
      if(!resourcesByInstance.count(id))resourcesByInstance.emplace(id,std::make_unique<SceneResources>(id));
      sceneResult=renderActiveScene(*instance);
      if(sceneResult!=0)break;
    }
    if(sceneResult>0)return sceneResult;
    if(sceneResult<0)break;
    // Lasers are shared overlays: apply visibility and the selected hit once after all scenes update.
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    const bool headValid=poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid;
    for(int hand=0;hand<2;hand++){
      // Interactive panels need a visible pointer even when no script requests a beam.
      bool visible=panelHitDistance[hand]>=0;
      for(int id:scriptRegistry.runningIds()){
        const auto *entry=scriptRegistry.find(id);
        if(entry&&entry->host.scene.laserVisible[hand]){visible=true;break;}
      }
      const auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      const auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      vr::TrackedDevicePose_t pose{};
      if(device!=vr::k_unTrackedDeviceIndexInvalid)pose=poses[device];
      const float distance=panelHitDistance[hand]>=0?panelHitDistance[hand]:rayTargets[hand]?rayTargets[hand]->distance:2.f;
      updateLaser(lasers[hand],laserShown[hand],visible&&headValid,hand,pose,
                  poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking,distance);
    }
    ++frame;
    if(frame%900==0){
      const auto now=std::chrono::steady_clock::now();
      const double seconds=std::chrono::duration<double>(now-reportStart).count();reportStart=now;
      size_t surfaceCount=0,readyCount=0;
      for(const auto &[id,owner]:resourcesByInstance)surfaceCount+=owner->surfaces.size();
      for(const auto &[id,owner]:renderStates)readyCount+=owner->ready.size();
      fprintf(stdout,"frames=%d fps=%.1f scenes=%zu surfaces=%zu ready=%zu gl_error=%x\n",frame,900.0/seconds,scriptRegistry.runningIds().size(),surfaceCount,readyCount,(unsigned)glGetError());fflush(stdout);
      fprintf(stdout,"[profile] picking_ms=%.3f render_submit_ms=%.3f gpu_wait_ms=%.3f physics_ms=%.3f\n",pickingMs/900,renderMs/900,finishMs/900,physicsMs/900);
      pickingMs=renderMs=finishMs=physicsMs=0;
    }
    if(engineStats.update(++diagnosticFrames)){
      uint64_t bytes=0;std::set<GLuint> countedDepth;
      for(const auto &[ownerId,owner]:resourcesByInstance)for(const auto &[id,surface]:owner->surfaces)for(const Surface *generation=surface.get();generation;generation=generation->previous.get())for(const auto &buffer:generation->buffers){
        const uint64_t height=generation->eyePixels,width=2*height;
        if(buffer.bo)bytes+=uint64_t(gbm_bo_get_stride(buffer.bo))*height;
        if(buffer.depth&&countedDepth.insert(buffer.depth).second)bytes+=width*height*4; // Depth24 storage commonly occupies 32 bits.
      }
      std::set<const LightingEnvironment*> countedEnvironments;
      std::set<const SheenLookup*> countedSheen;
      for(int id:scriptRegistry.runningIds()){
        const auto *entry=scriptRegistry.find(id);
        if(!entry)continue;
        const auto &script=entry->host;
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);if(part.indexBuffer)bytes+=uint64_t(part.vertexCount)*(part.indexType==GL_UNSIGNED_SHORT?2:4);for(const auto &lod:part.lod){if(lod.budgetBytes)bytes+=lod.budgetBytes;else if(lod.indexBuffer)bytes+=uint64_t(lod.count)*(part.indexType==GL_UNSIGNED_SHORT?2:4);}if(part.texture)bytes+=uint64_t(part.width)*part.height*4;}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.vbo)bytes+=uint64_t(part.gpuVertexCount)*5*sizeof(float);
      for(const auto &panel:script.scene.panels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      for(const auto &panel:script.scene.retiredPanels)if(panel.texture)bytes+=uint64_t(panel.width)*panel.height*4;
      if(auto render=renderStates.find(id);render!=renderStates.end())bytes+=render->second->instanceExperiment.allocatedBytes();
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){if(part.skinVbo)bytes+=part.skinVertices.size()*sizeof(float);if(part.skinIbo)bytes+=part.skinIndices.size()*sizeof(uint32_t);}
      for(const auto &object:script.scene.objects)if(object.animationPose)for(size_t s=0;s<object.animationPose->skinTextures.size();s++)if(object.animationPose->skinTextures[s])bytes+=object.animationPose->palettes[s].size()*sizeof(animation::Matrix);
      for(const auto &object:script.scene.objects)if(object.animationPose&&object.animationPose->geometry.batchVbo)bytes+=object.animationPose->geometry.batchBytes;
      for(const auto &asset:script.scene.assets)for(const auto &part:asset.primitives){
        if(part.surfaceVbo)bytes+=uint64_t(part.gpuVertexCount)*9*sizeof(float);
        for(const auto &map:part.maps)if(map.texture){uint64_t w=map.width,h=map.height;for(;;){bytes+=w*h*4;if(w==1&&h==1)break;w=std::max(uint64_t(1),w/2);h=std::max(uint64_t(1),h/2);}}
      }
      for(const auto &object:script.scene.objects)if(object.animationPose)for(const auto &part:object.animationPose->geometry.primitives)if(part.surfaceVbo)bytes+=part.surface.size()*sizeof(float);
      for(const auto &asset:script.scene.assets)if(asset.environment&&countedEnvironments.insert(asset.environment.get()).second)bytes+=asset.environment->gpuBytes();
      for(const auto &asset:script.scene.assets)if(asset.sheenLookup&&asset.sheenLookup->texture&&countedSheen.insert(asset.sheenLookup.get()).second)bytes+=SheenLookup::size*SheenLookup::size*4;
      }
      bytes+=spotShadowDiagnostics.bytes;
      engineStats.gpuAllocatedEstimateBytes=bytes;
    }
#ifdef SF_ANIMATION_PROFILE
    animationFrame.workMs=animationProfileMs(animationFrameStart);
    size_t profiledObjects=0,playingObjects=0;
    for(int id:scriptRegistry.runningIds())if(const auto *entry=scriptRegistry.find(id))
      for(const auto &object:entry->host.scene.objects)if(object.animationPose){profiledObjects++;playingObjects+=object.animation.playing;}
    animationWindow.record(animationFrame,profiledObjects,playingObjects,animationEpoch);
#endif
    // Drop missed deadlines rather than rendering bursts to repay old frame debt.
    next=std::max(next+std::chrono::microseconds(11111),std::chrono::steady_clock::now());
    std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  return 0;
}
