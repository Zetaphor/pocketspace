// Frame loop for the VR host.
#include "render_host.h"
#include <malloc.h>
int main(int argc,char **argv) {
  // CPU-only workload: no SteamVR session or graphics context required.
  if(argc>2&&std::string(argv[1])=="--benchmark-picking"){
    ModelAsset asset;if(!loadModelFile(argv[2],asset))return 1;
    Object object{1,1,0,0,0,{.4f,.4f,.4f},.5f};
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
  std::filesystem::path settingsPath=settings::defaultPath();
  std::filesystem::path controlSocketPath=control::defaultSocketPath();
  bool engineAutostartQuery=false;
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
    }else if(arg=="--settings"){
      if(i+1==argc){fprintf(stderr,"--settings requires a file path\n");return 13;}
      settingsPath=argv[++i];
    }else if(arg=="--control-socket"){
      if(i+1==argc){fprintf(stderr,"--control-socket requires a path\n");return 13;}
      controlSocketPath=argv[++i];
    }else if(arg=="--engine-autostart-enabled"){
      engineAutostartQuery=true;
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
  // For the autostart service: exit 0 only when valid settings ask to start with SteamVR.
  if(engineAutostartQuery){settings::File file(settingsPath);file.refresh();return file.error().empty()&&file.current().engineAutostart?0:1;}
  // One engine per user: the control socket doubles as the single-instance lock.
  control::Server controlServer;
  {std::string error;if(!controlServer.start(controlSocketPath,error)){fprintf(stderr,"[control] %s\n",error.c_str());return 14;}}
  fprintf(stdout,"[control] listening on %s\n",controlSocketPath.c_str());
  ScriptRegistry scriptRegistry;
  if(!scriptRegistry.enableScratch())fprintf(stderr,"[lifecycle] scratch scene unavailable\n");
  settings::File engineSettings(settingsPath);engineSettings.refresh();
  if(!engineSettings.error().empty())fprintf(stderr,"[settings] %s: %s; using defaults\n",settingsPath.c_str(),engineSettings.error().c_str());
  scriptRegistry.setAutoRestart(engineSettings.current().autoRestart);
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
  // Autostart entries start independently after explicit launches; failures are reported, not fatal.
  for(const auto &failure:launchAutostart(scriptRegistry,scriptsDirectory,engineSettings.current().autostart))
    fprintf(stderr,"[lifecycle] autostart %s\n",failure.c_str());
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
  GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
  ScopeExit shaderCleanup{[&]{glDeleteProgram(program);glDeleteShader(v);glDeleteShader(f);}};
  glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 5;
  // Validate color and depth material programs before publishing a live scene.
  std::map<int,std::unique_ptr<SceneRenderState>> renderStates;
  std::array<panelPointer::Pointer,2> panelPointers;
  PbrProgram pbrProgram;if(!pbrProgram.init())return 5;
  PbrProgram depthPbrProgram;if(!depthPbrProgram.init(true))return 5;
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto anchor=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)anchor.m[r][3]+=anchor.m[r][1]*-.2f+anchor.m[r][2]*-1.f;
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
  // Notifications need a source overlay; this one is never shown.
  vr::VROverlayHandle_t notificationSource=vr::k_ulOverlayHandleInvalid;
  if(overlays->CreateOverlay("pocketspace.notifications","Pocketspace",&notificationSource)!=vr::VROverlayError_None)notificationSource=vr::k_ulOverlayHandleInvalid;
  ScopeExit notificationCleanup{[&]{if(notificationSource!=vr::k_ulOverlayHandleInvalid)overlays->DestroyOverlay(notificationSource);}};
  scriptRegistry.setOnFailure([&](const ScriptInstance &instance){
    const std::string retry=instance.restartAt?" (restarting)":"";
    std::string text="Pocketspace: "+std::filesystem::path(instance.path).filename().string()+" stopped"+retry+"\n"+instance.lastError;
    if(text.size()>=vr::k_unNotificationTextMaxSize)text.resize(vr::k_unNotificationTextMaxSize-1);
    fprintf(stderr,"[lifecycle] instance=%d stopped after error%s: %s\n",instance.id,retry.c_str(),instance.lastError.c_str());
    vr::VRNotificationId notification=0;
    if(notificationSource==vr::k_ulOverlayHandleInvalid||!vr::VRNotifications()||
       vr::VRNotifications()->CreateNotification(notificationSource,0,vr::EVRNotificationType_Transient,text.c_str(),vr::EVRNotificationStyle_Application,nullptr,&notification)!=vr::VRNotificationError_OK)
      fprintf(stderr,"[lifecycle] SteamVR notification unavailable\n");
  });
  ScopeExit clearFailureHook{[&]{scriptRegistry.setOnFailure({});}};
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
    // Groups share one buffer budget. A new group starts at the largest size that
    // fits and grows through the resize path; with no room it stays unallocated,
    // and unrendered, until another group releases buffers.
    auto create=[&](Surface &surface,int id,float width,int eyePixels){
      int pixels=eyePixels;
      if(id<0){
        pixels=groupBufferBudget.fittingPixels(eyePixels);
        if(!pixels){
          if(!surface.budgetDeferred)fprintf(stderr,"[surface] group id=%d waits for grouped buffer budget\n",id);
          surface.instanceId=host.instanceId;surface.objectId=id;surface.budgetDeferred=true;return true;
        }
        if(pixels<eyePixels||surface.budgetDeferred)fprintf(stderr,"[surface] group id=%d budget limited eye_px=%d requested=%d\n",id,pixels,eyePixels);
        surface.budgetDeferred=false;
      }
      return createSurface(surface,host.instanceId,id,width,pixels,gbm,display,anchor);
    };
    for(const auto &[id,members]:groups){
      const auto *object=members.front();
      const float width=std::max(.55f,largestScale(*object)*2.1f*objectRadius(*object));
      const int eyePixels=host.scene.assets[(size_t)object->asset-1].eyePixels;
      auto found=surfaces.find(id);
      if(found!=surfaces.end()&&found->second->buffers[0].fbo&&id>0&&found->second->referenceEyePixels!=eyePixels){
        destroySurface(*found->second,display);surfaces.erase(found);found=surfaces.end();
      }
      if(found==surfaces.end()){
        auto created=std::make_unique<Surface>();
        if(!create(*created,id,width,eyePixels)){destroySurface(*created,display);return false;}
        surfaces.emplace(id,std::move(created));
      }else if(!found->second->buffers[0].fbo){
        if(!create(*found->second,id,width,eyePixels))return false;
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
  const auto skinUniforms=SkinUniforms::of(program);
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
  GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff");
  GLint sheenLocation=glGetUniformLocation(program,"sheen");
  double pickingMs=0,renderMs=0,finishMs=0,physicsMs=0;

  auto next=std::chrono::steady_clock::now();auto reportStart=next;int frame=0;uint64_t diagnosticFrames=0;engineStats.prime();
  auto reconnectAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
  // File-change and control-socket reloads share the host's cleanup of native state.
  auto reloadInstance=[&](int id)->bool{
    const bool loaded=scriptRegistry.reload(id,[&]{
      for(auto &pointer:panelPointers)pointer.forgetInstance(id);
      if(auto owner=resourcesByInstance.find(id);owner!=resourcesByInstance.end())releaseResources(*owner->second);
      if(auto owner=renderStates.find(id);owner!=renderStates.end())owner->second->resetScene();
      sceneInput().resetScene();
      for(int hand=0;hand<2;hand++){
        meshHoveredObject[hand]=meshCurrentObject[hand]=0;
        meshHitDistance[hand]=2;meshHitPoint[hand]={};meshLastHit[hand]={};grabNeedsRelease[hand]=true;
      }
      fprintf(stdout,"[lifecycle] instance=%d scene state reset\n",id);
    });
    if(auto *entry=scriptRegistry.find(id);loaded&&entry&&renderStates.count(id)){
      for(auto &asset:entry->host.scene.assets)if(!asset.uploaded)uploadModel(asset);
      renderStates.at(id)->pbrVariants.prepare(entry->host.scene);
    }
    return loaded;
  };
  control::Service controlService(scriptRegistry,scriptsDirectory,engineSettings,{reloadInstance,[&](const settings::Settings &current){scriptRegistry.setAutoRestart(current.autoRestart);}});
  scratchControl=[&](const std::string &request){return controlService.handleScratch(request);};
  ScopeExit clearScratchControl{[&]{scratchControl={};}};
  while(running){
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
    if(frame%90==0&&engineSettings.refresh()){
      // Autostart edits apply at the next engine start; the restart flag applies now.
      if(!engineSettings.error().empty())fprintf(stderr,"[settings] %s; keeping the last valid settings\n",engineSettings.error().c_str());
      else{scriptRegistry.setAutoRestart(engineSettings.current().autoRestart);fprintf(stdout,"[settings] reloaded autoRestart=%d\n",int(engineSettings.current().autoRestart));}
    }
    for(int id:scriptRegistry.tickRestarts())if(auto *entry=scriptRegistry.find(id);entry&&renderStates.count(id)){
      for(auto &asset:entry->host.scene.assets)if(!asset.uploaded)uploadModel(asset);
      renderStates.at(id)->pbrVariants.prepare(entry->host.scene);
    }
    if(frame%30==0)for(int id:scriptRegistry.runningIds()){
      auto *entry=scriptRegistry.find(id);
      if(!entry||entry->state!=ScriptState::Running||!id)continue;
      std::error_code ec;const auto modified=std::filesystem::last_write_time(entry->path,ec);
      if(ec||modified==entry->host.mtime)continue;
      if(!reloadInstance(id))entry->host.mtime=modified;
    }
    // Control requests run here, between frames, outside every scene scope.
    controlServer.poll([&](control::Client &client,const std::string &line){return controlService.handle(client,line);});
    // scripts.start/stop/reload from scratch, including calls made inside the last frame.
    controlService.applyScratchQueue();
    for(const auto &line:controlService.takeEvents())controlServer.broadcast(line);
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
          // Grabbable objects always stop the ray; others only unless marked pass-through.
          if(!object.visible||(object.laserPassThrough&&!object.grabbable))continue;
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
        if(pointer.current().panelId&&(!ownerPanel||!panelShown(*ownerPanel,owner->host.scene)||!ownerPanel->interactive))pointer.drop(pointerEvents);
        std::optional<panelPointer::Target> target;float targetDistance=-1.f;
        const auto device=system->GetTrackedDeviceIndexForControllerRole(hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand);
        const bool tracked=controllers[hand].valid&&controllers[hand].connected&&device!=vr::k_unTrackedDeviceIndexInvalid&&rayPoses[device].bPoseIsValid&&rayHead.bPoseIsValid;
        if(tracked&&!grabOwners[hand]){
          float origin[3],direction[3];laserRay(rayPoses[device].mDeviceToAbsoluteTracking,hand,origin,direction);
          for(int id:runningSceneIds)if(const auto *entry=scriptRegistry.find(id))for(const auto &panel:entry->host.scene.panels){
            if(!panel.interactive||!panelShown(panel,entry->host.scene))continue;
            const auto transform=panelTransform(panel,anchor,rayHead.mDeviceToAbsoluteTracking,panelObject(panel,entry->host.scene));
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
    auto &instancing=sceneRender.instancing;
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
    }
    const auto pickingStart=std::chrono::steady_clock::now();
    for(int hand=0;hand<2;hand++){
      const bool ownsRay=rayOwners[hand]==instance.id;
      int hit=ownsRay&&rayTargets[hand]?rayTargets[hand]->localId:0;
      float nearest=ownsRay&&rayTargets[hand]?rayTargets[hand]->distance:std::numeric_limits<float>::max();
      meshCurrentObject[hand]=hit;
      const auto now=std::chrono::steady_clock::now();
      const auto *previousHit=findSceneItem(script.scene.objects,meshHoveredObject[hand]);
      std::array<float,3> point=meshHitPoint[hand];
      const bool current=hit!=0;
      if(current){
        meshLastHit[hand]=now;
        float origin[3],direction[3];laserSceneRay(controllers[hand],hand,origin,direction);
        for(int axis=0;axis<3;axis++)point[axis]=origin[axis]+direction[axis]*nearest;
      }else if(ownsRay&&controllers[hand].connected&&previousHit&&previousHit->visible&&now-meshLastHit[hand]<std::chrono::milliseconds(100)){
        // Bridge single-frame misses at triangle edges; the last hit stays reported.
        hit=meshHoveredObject[hand];nearest=meshHitDistance[hand];
      }
      if(hit!=meshHoveredObject[hand]){
        if(meshHoveredObject[hand])sceneInput().addMeshEvent("overlay.meshHoverEnd",meshHoveredObject[hand],hand,meshHitDistance[hand],meshHitPoint[hand]);
        if(hit)sceneInput().addMeshEvent("overlay.meshHoverStart",hit,hand,nearest,point);
        meshHoveredObject[hand]=hit;
      }else if(current){
        float moved=0;for(int axis=0;axis<3;axis++)moved=std::max(moved,std::fabs(point[axis]-meshHitPoint[hand][axis]));
        if(moved>.001f)sceneInput().addMeshEvent("overlay.meshHover",hit,hand,nearest,point);
      }
      if(hit){meshHitPoint[hand]=point;meshHitDistance[hand]=nearest;}
      else{meshHitPoint[hand]={};meshHitDistance[hand]=2.f;}
    }
    pickingMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pickingStart).count();
    const auto reconnectNow=std::chrono::steady_clock::now();
    if(!frameInput.started()&&reconnectNow>=reconnectAt){frameInput.reconnect();reconnectAt=reconnectNow+std::chrono::seconds(1);}
    auto failScene=[&](const std::string &error){
      // The registry's failure hook logs and notifies.
      scriptRegistry.failActive(instance.id,error);
      return 0;
    };
    std::string inputError;{telemetry::Timed timed(instance.callbacks);inputError=dispatchInputCallbacks();}
    if(!inputError.empty())return failScene(inputError);
    updateManipulation(script.scene,grabs,1.f/90.f,instance.id,rayOwners,grabOwners);
    for(int hand=0;hand<2;hand++)if(grabs[hand].objectId)meshHitDistance[hand]=grabs[hand].laserDistance;
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result;{telemetry::Timed timed(instance.callbacks);result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);}JS_FreeValue(script.ctx,dt);
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
      instancing.reset();
      script.scene.assetsReleased=false;
    }
    for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);
    for(auto &object:script.scene.objects)if(object.animationPose)uploadModelPose(*object.animationPose);
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
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
          object.lodLevel=level;
        };
        const bool available=asset.lod&&!object.animationPose&&std::any_of(asset.primitives.begin(),asset.primitives.end(),[](const auto &p){return p.lod[0].indexBuffer||p.lod[1].indexBuffer||p.lod[0].vbo||p.lod[1].vbo;});
        if(!available||grabs[0].objectId==object.id||grabs[1].objectId==object.id){selectDetail(0);continue;}
        float center[3];for(int row=0;row<3;row++)center[row]=anchor.m[row][3]+anchor.m[row][0]*object.x+anchor.m[row][1]*object.y-anchor.m[row][2]*object.z;
        float extent=0;for(int axis=0;axis<3;axis++){const float e=(asset.high[axis]-asset.low[axis])*object.scale[axis];extent+=e*e;}
        const float radius=.5f*std::sqrt(extent);float nearest=std::numeric_limits<float>::infinity(),pixels=0;
        for(const auto &eye:eyes){float squared=0,depth=0;for(int axis=0;axis<3;axis++){const float d=center[axis]-eye[axis];squared+=d*d;depth-=d*head.m[axis][2];}nearest=std::min(nearest,std::sqrt(squared)-radius);pixels=std::max(pixels,modelLod::projectedDiameter(radius,focal,squared,depth));}
        selectDetail(modelLod::select(pixels,nearest,object.lodLevel));
      }
      spotShadows.update(script.scene);
      const auto frameLights=resolvePointLights(script.scene);
      updateObjectLights(script.scene,frameLights,frameObjectLights);
      frameBounds.update(script.scene,anchor,head,eyes);frameBounds.displayFocal=focal;
      const bool useInstancing=std::any_of(script.scene.assets.begin(),script.scene.assets.end(),[](const ModelAsset &asset){return asset.gpuInstancing;});
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
          if(!createSurface(parent,scriptInstanceId,key,plan.widths[0],plan.pixels[0],gbm,display,anchor,suffix)||
             (!merge&&!createSurface(*child,scriptInstanceId,childKey,plan.widths[1],plan.pixels[1],gbm,display,anchor,suffix))){
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
        const bool shown=panelShown(panelData,script.scene);
        const auto transform=shown&&panelData.texture?panelTransform(panelData,anchor,head,panelObject(panelData,script.scene)):vr::HmdMatrix34_t{};
        const auto error=shown?submitPanel(*overlays,overlay->second,panelData,transform,panelSubmissions[panelData.id]):hidePanel(*overlays,overlay->second,panelSubmissions[panelData.id]);
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[panel] submit id=%d error=%d\n",panelData.id,int(error));
      }
      const auto groups=surfaceMembers(script);
      int shrinkBudget=2;auto &culledSurfaces=sceneRender.culledSurfaces;culledSurfaces.clear();
      for(auto &[id,surface]:surfaces){
        if(!surface->buffers[0].fbo)continue; // A handle reserved during this update initializes next frame.
        auto group=groups.find(id);if(group==groups.end())continue;
        const auto &members=group->second;const auto *target=members.front();
        // Hide only replacement fronts before reusing their buffers. Retained
        // cohort snapshots stay visible until the complete handoff succeeds.
        const bool changing=pendingTileChange&&(id==pendingTileChange->parent||(!pendingTileChange->merge&&id==pendingTileChange->child));
        if(changing&&hideSurfaceFront(*overlays,*surface)!=vr::VROverlayError_None)continue;
        float animatedWidth=std::max(.55f,largestScale(*target)*2.1f*objectRadius(*target));
        float surfaceX=target->x,surfaceY=target->y,surfaceZ=target->z;
        auto panel=anchor;
        float panelRight[3],panelUp[3],panelForward[3];
        if(id<0){
          const auto projection=groupPlane(members,script.scene,anchor,head,eyes,&frameBounds);
          panel=projection.transform;surfaceX=projection.sceneOrigin[0];surfaceY=projection.sceneOrigin[1];surfaceZ=projection.sceneOrigin[2];
          std::copy_n(projection.right,3,panelRight);std::copy_n(projection.up,3,panelUp);std::copy_n(projection.forward,3,panelForward);
          for(const auto *member:members){const float dx=member->x-surfaceX,dy=member->y-surfaceY,dz=member->z-surfaceZ;animatedWidth=std::max(animatedWidth,2.1f*(std::sqrt(dx*dx+dy*dy+dz*dz)+largestScale(*member)*objectRadius(*member)));}
        }else{
          for(int r=0;r<3;r++)panel.m[r][3]+=anchor.m[r][0]*surfaceX+anchor.m[r][1]*surfaceY-anchor.m[r][2]*surfaceZ;
          faceViewer(panel,anchor,head,panelRight,panelUp,panelForward);
        }
        float distanceSquared=0,facing=0;
        for(int r=0;r<3;r++){float delta=panel.m[r][3]-head.m[r][3];distanceSquared+=delta*delta;facing-=delta*head.m[r][2];}
        if(facing<.25f*std::sqrt(distanceSquared)-(id<0?animatedWidth*.5f:largestScale(*target)*(target->animationPose?objectRadius(*target):.6f))){
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
        // Grow immediately when needed; shrink only below 60% for 45 consecutive frames.
        if(crop<surface->eyePixels*.6f)++surface->shrinkFrames;else surface->shrinkFrames=0;
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
          }else if(!resizeSurfaceBuffers(*surface,surfaceBufferPixels(crop,surface->referenceEyePixels),gbm,display)){fprintf(stderr,"[surface] resize id=%d failed\n",id);running=false;break;}
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
            ModelCulling culling;
            auto selected=[&](const Object &object){
              if(!object.visible||(pass<0?objectSurfaceKey(object)==targetId:objectSurfaceKey(object)!=targetId))return false;
              if(pass<0&&!mayOcclude(id<0?frameBounds.common(object,i):frameBounds.project(object,eyes[i],panelRight,panelUp,panelForward),targetBounds[i]))return false;
              return true;
            };
            auto drawObject=[&](const Object &object){
              const auto &asset=script.scene.assets[(size_t)object.asset-1];
              glUniform1f(yawLocation,object.yaw);glUniform1f(pitchLocation,object.pitch);glUniform1f(rollLocation,object.roll);glUniform3fv(scaleLocation,1,object.scale.data());
              glUniform3f(offsetLocation,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
              // An external occluder draws only the clusters that pass the same test as
              // the whole object; the rest cannot cover any of the target's fragments.
              auto drawStatic=[&](const PrimitiveAsset &part){
                if(pass<0&&!modelLodLevel(part,object.lodLevel)&&drawDepthClusters(part,[&](const DepthCluster &cluster){
                  const auto world=worldBounds(object,cluster,anchor);
                  return mayOcclude(id<0?projectWorldBounds(world,eyes[i],frameBounds.right,frameBounds.up,frameBounds.forward):projectWorldBounds(world,eyes[i],panelRight,panelUp,panelForward),targetBounds[i]);
                }))return;
                drawModelPrimitive(part,0,object.lodLevel);
              };
              for(size_t primitive=0;primitive<asset.primitives.size();primitive++){
                const auto &part=asset.primitives[primitive];
                if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
                glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);glUniform3fv(sheenLocation,1,part.sheen);
                culling.material(part,object.animationPose.get());
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
                if(asset.pbr){
                  auto *variant=pass>=0?pbrVariants.ready(asset,part,frameObjectLights[size_t(&object-script.scene.objects.data())].count,surface->eyePixels):nullptr;
                  auto &activePbr=variant?*variant:(pass<0?depthPbrProgram:pbrProgram);
                  activePbr.view(anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f);
                  activePbr.object(object.pitch,object.yaw,object.roll,object.scale,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
                  if(pass>=0)activePbr.lights(frameObjectLights[size_t(&object-script.scene.objects.data())].values(),surfaceX,surfaceY,surfaceZ);
                  if(pass>=0)activePbr.shadows(spotShadows,frameObjectLights[size_t(&object-script.scene.objects.data())].values(),surfaceX,surfaceY,surfaceZ);
                  activePbr.material(asset,part,object.animationPose?object.animationPose->geometry.primitives[primitive]:part,object.animationPose?0:object.lodLevel,pass<0);
                  ScopeExit restorePbr{[&]{activePbr.release();glUseProgram(program);}};
                  if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);else drawStatic(part);
                  continue;
                }
                if(object.animationPose&&bindSkinPrimitive(asset,part,*object.animationPose,skinUniforms)){
                  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glDrawElements(GL_TRIANGLES,GLsizei(part.skinIndices.size()),GL_UNSIGNED_INT,nullptr);unbindSkinPrimitive(skinUniforms);
                  continue;
                }
                if(object.animationPose&&drawRigidBatch(asset,*object.animationPose,primitive)){
                  continue;
                }
                if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);
                else drawStatic(part);
              }
            };
            if(useInstancing){
              std::vector<const Object*> objects;objects.reserve(script.scene.objects.size());for(const auto &object:script.scene.objects)if(selected(object))objects.push_back(&object);
              const float origin[3]={surfaceX,surfaceY,surfaceZ};
              instancing.draw(script.scene,culling,objects,pass,program,anchor,surfaceCenter,eyes[i],panelRight,panelUp,panelForward,surface->width*.5f,origin,drawObject,[](unsigned){});return;
            }
            for(const auto &object:script.scene.objects)if(selected(object))drawObject(object);
          },&region);
        }
        ready.push_back(surface.get());
      }
      renderMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count();
      sceneRender.awaitingPublish=true;
    }
      return 0;
    };
    // One glFinish after every scene has rendered proves this frame's producer
    // completion; publish each scene afterwards. Resolve state again: callbacks of
    // later scenes ran in between, and stop/reload resets the scene's render state.
    auto publishScene=[&](int scriptInstanceId){
      const auto render=renderStates.find(scriptInstanceId);const auto resources=resourcesByInstance.find(scriptInstanceId);
      if(render==renderStates.end()||resources==resourcesByInstance.end()||!render->second->awaitingPublish)return;
      auto &sceneRender=*render->second;sceneRender.awaitingPublish=false;
      auto &surfaces=resources->second->surfaces;
      auto &pendingTileChange=sceneRender.pendingTileChange;auto &ready=sceneRender.ready;auto &regroupAfter=sceneRender.regroupAfter;
      auto &culledSurfaces=sceneRender.culledSurfaces;auto &pbrVariants=sceneRender.pbrVariants;
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
      const int completedFrame=frame+1;
      if(completedFrame%900==0)fprintf(stdout,"[pbr] prepared=%zu ready_draws=%zu fallback_draws=%zu\n",pbrVariants.programs.size(),pbrVariants.readyDraws,pbrVariants.fallbackDraws);
    };
    int sceneResult=0;
    for(int id:scriptRegistry.runningIds()){
      auto *instance=scriptRegistry.find(id);
      if(!instance||instance->state!=ScriptState::Running)continue;
      if(!renderStates.count(id)){
        auto owner=std::make_unique<SceneRenderState>();
        if(!owner->spotShadows.prepare())return 5;
        for(auto &asset:instance->host.scene.assets)if(!asset.uploaded)uploadModel(asset);
        owner->pbrVariants.prepare(instance->host.scene);
        renderStates.emplace(id,std::move(owner));
      }
      if(!resourcesByInstance.count(id))resourcesByInstance.emplace(id,std::make_unique<SceneResources>(id));
      sceneResult=renderActiveScene(*instance);
      if(sceneResult!=0)break;
    }
    if(sceneResult>0)return sceneResult;
    if(sceneResult<0)break;
    {
      const auto finishStart=std::chrono::steady_clock::now();
      glFinish();
      finishMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-finishStart).count();
      glBindFramebuffer(GL_FRAMEBUFFER,0);
      for(int id:scriptRegistry.runningIds())publishScene(id);
    }
    // Bus delivery runs after every scene's callbacks and update for this frame.
    scriptRegistry.dispatchBus();
    scriptRegistry.endFrame();
    // Lasers are shared overlays: apply visibility and the selected hit once after all scenes update.
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    const bool headValid=poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid;
    for(int hand=0;hand<2;hand++){
      const bool visible=laserVisible[hand];
      const auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      const auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      vr::TrackedDevicePose_t pose{};
      if(device!=vr::k_unTrackedDeviceIndexInvalid)pose=poses[device];
      const float distance=panelHitDistance[hand]>=0?panelHitDistance[hand]:rayTargets[hand]?rayTargets[hand]->distance:2.f;
      updateLaser(lasers[hand],laserShown[hand],visible&&headValid,hand,pose,
                  poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking,distance);
    }
    // glibc keeps a freed scene's pages in its arenas; return them once per teardown.
    // A worker thread would not help: allocations wait on the arena lock it holds.
    if(heapTrimPending){heapTrimPending=false;malloc_trim(0);}
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
      // Owned estimates per running script; shared items once. Watchers get the sample.
      uint64_t bytes=0;std::vector<const Scene*> scenes;
      for(int id:scriptRegistry.allIds()){
        auto *entry=scriptRegistry.find(id);
        if(!entry||entry->state!=ScriptState::Running){if(entry)entry->gpuEstimateBytes.reset();continue;}
        const auto resources=resourcesByInstance.find(id);const auto render=renderStates.find(id);
        static const std::map<int,std::unique_ptr<Surface>> noSurfaces;
        entry->gpuEstimateBytes=ownedGpuEstimate(entry->host.scene,resources==resourcesByInstance.end()?noSurfaces:resources->second->surfaces,render==renderStates.end()?nullptr:render->second.get());
        bytes+=*entry->gpuEstimateBytes;scenes.push_back(&entry->host.scene);
      }
      engineStats.sharedGpuEstimateBytes=sharedGpuEstimate(scenes);
      engineStats.gpuAllocatedEstimateBytes=bytes+engineStats.sharedGpuEstimateBytes;
      scriptRegistry.sampleCallbacks();
      if(controlServer.watcherCount())controlService.publishResources();
    }
    // Drop missed deadlines rather than rendering bursts to repay old frame debt.
    next=std::max(next+std::chrono::microseconds(11111),std::chrono::steady_clock::now());
    std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  return 0;
}
