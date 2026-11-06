#include "render_host.h"
float surfaceCoverageWidth(float width,int allocated,int requested,bool grouped){
  return width*(grouped?std::max(1.f,float(allocated)/requested):float(allocated)/requested);
}
void preserveSurfaceFallback(Surface &removed,Surface &survivor){
  if(!removed.previous)return;auto *tail=&survivor;while(tail->previous)tail=tail->previous.get();tail->previous=std::move(removed.previous);
}
bool initEgl(EGLDisplay &display,EGLContext &context,EGLSurface &surface){
  display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(display,nullptr,nullptr))return false;
  EGLConfig config;EGLint count=0;
  const EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  if(!eglChooseConfig(display,attrs,&config,1,&count)||!count)return false;
  const EGLint ctxAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttrs);
  const EGLint surfAttrs[]={EGL_WIDTH,kDefaultEyePixels,EGL_HEIGHT,kDefaultEyePixels,EGL_NONE};
  surface=eglCreatePbufferSurface(display,config,surfAttrs);
  return eglMakeCurrent(display,surface,surface,context)==EGL_TRUE;
}
void makeDepthStorage(Buffer &buffer,int width,int height,GLuint sharedDepth){
  buffer.depth=sharedDepth;
  if(!buffer.depth){glGenRenderbuffers(1,&buffer.depth);buffer.ownsDepth=true;glBindRenderbuffer(GL_RENDERBUFFER,buffer.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);}
}
bool makeBuffer(Buffer &b,gbm_device *gbm,EGLDisplay display,int eyePixels,GLuint sharedDepth){
  const int width=2*eyePixels,height=eyePixels;
  b.bo=gbm_bo_create(gbm,width,height,GBM_FORMAT_ABGR8888,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
  if(!b.bo||(b.fd=gbm_bo_get_fd(b.bo))<0)return false;
  vr::DmabufAttributes_t a{};
  a.unWidth=width;a.unHeight=height;a.unDepth=a.unMipLevels=a.unArrayLayers=a.unSampleCount=1;
  a.unFormat=DRM_FORMAT_ABGR8888;a.ulModifier=DRM_FORMAT_MOD_LINEAR;a.unPlaneCount=1;
  a.plane[0].nFd=b.fd;a.plane[0].unOffset=gbm_bo_get_offset(b.bo,0);a.plane[0].unStride=gbm_bo_get_stride(b.bo);
  if(!vr::VRIPCResourceManager()->ImportDmabuf(vr::VRApplication_Overlay,&a,&b.handle)||!b.handle)return false;
  const EGLint attrs[]={EGL_WIDTH,width,EGL_HEIGHT,height,EGL_LINUX_DRM_FOURCC_EXT,EGLint(DRM_FORMAT_ABGR8888),EGL_DMA_BUF_PLANE0_FD_EXT,b.fd,EGL_DMA_BUF_PLANE0_OFFSET_EXT,EGLint(a.plane[0].unOffset),EGL_DMA_BUF_PLANE0_PITCH_EXT,EGLint(a.plane[0].unStride),EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,EGLint(DRM_FORMAT_MOD_LINEAR&0xffffffff),EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,EGLint(DRM_FORMAT_MOD_LINEAR>>32),EGL_NONE};
  b.image=createImage(display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,nullptr,attrs);
  if(b.image==EGL_NO_IMAGE_KHR){fprintf(stderr,"eglCreateImage error=%x\n",(unsigned)eglGetError());return false;}
  glGenRenderbuffers(1,&b.color);glBindRenderbuffer(GL_RENDERBUFFER,b.color);imageRenderbuffer(GL_RENDERBUFFER,b.image);
  makeDepthStorage(b,width,height,sharedDepth);
  glGenFramebuffers(1,&b.fbo);glBindFramebuffer(GL_FRAMEBUFFER,b.fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,b.color);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,b.depth);
  bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;glBindFramebuffer(GL_FRAMEBUFFER,0);return ok;
}
void destroyBuffer(Buffer &buffer,EGLDisplay display){
  if(buffer.fbo)glDeleteFramebuffers(1,&buffer.fbo);
  if(buffer.depth&&buffer.ownsDepth)glDeleteRenderbuffers(1,&buffer.depth);
  if(buffer.color)glDeleteRenderbuffers(1,&buffer.color);
  if(buffer.image!=EGL_NO_IMAGE_KHR)destroyImage(display,buffer.image);
  if(buffer.handle)vr::VRIPCResourceManager()->UnrefResource(buffer.handle);
  if(buffer.fd>=0)close(buffer.fd);
  if(buffer.bo)gbm_bo_destroy(buffer.bo);
  buffer={};
}
void destroySurface(Surface &surface,EGLDisplay display){
  if(surface.previous){destroySurface(*surface.previous,display);surface.previous.reset();}
  if(surface.stagingOverlay!=vr::k_ulOverlayHandleInvalid){
    vr::VROverlay()->ClearOverlayTexture(surface.stagingOverlay);
    vr::VROverlay()->DestroyOverlay(surface.stagingOverlay);surface.stagingOverlay=vr::k_ulOverlayHandleInvalid;
  }
  if(surface.overlay!=vr::k_ulOverlayHandleInvalid){
    vr::VROverlay()->ClearOverlayTexture(surface.overlay);
    vr::VROverlay()->DestroyOverlay(surface.overlay);
  }
  for(auto &buffer:surface.buffers)destroyBuffer(buffer,display);
  groupBufferBudget.release(surface.groupBufferBytes);surface.groupBufferBytes=0;
  surface.overlay=vr::k_ulOverlayHandleInvalid;
}
bool createSurface(Surface &surface,int instanceId,int id,float width,int eyePixels,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &anchor,const std::string &suffix){
  if(id<0&&!surface.groupBufferBytes){
    const auto bytes=GroupBufferBudget::bytes(eyePixels);
    if(!groupBufferBudget.reserve(bytes))return false;
    surface.groupBufferBytes=bytes;
  }
  surface.instanceId=instanceId;surface.objectId=id;surface.width=width;surface.eyePixels=surface.referenceEyePixels=eyePixels;
  const std::string key=objectOverlayKey({instanceId,id},suffix);
  auto *overlays=vr::VROverlay();
  auto error=vr::VROverlayError_None;
  if(surface.overlay==vr::k_ulOverlayHandleInvalid){
    error=overlays->CreateOverlay(key.c_str(),key.c_str(),&surface.overlay);
    if(error!=vr::VROverlayError_None){fprintf(stderr,"[surface] create id=%d error=%d\n",id,(int)error);return false;}
  }
  error=overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_SideBySide_Parallel,true);
  if(error!=vr::VROverlayError_None)return false;
  // GL blending stores premultiplied colors; preserve that convention when
  // the compositor filters transparent silhouette/cutout borders.
  error=overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_IsPremultiplied,true);
  if(error!=vr::VROverlayError_None)return false;
  error=overlays->SetOverlayWidthInMeters(surface.overlay,width);
  if(error!=vr::VROverlayError_None)return false;
  vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(surface.overlay,&bounds);
  overlays->SetOverlayTransformAbsolute(surface.overlay,vr::TrackingUniverseStanding,&anchor);
  if(id<0){
    if(surface.stagingOverlay==vr::k_ulOverlayHandleInvalid){
      error=overlays->CreateOverlay((key+".stage").c_str(),(key+".stage").c_str(),&surface.stagingOverlay);
      if(error!=vr::VROverlayError_None)return false;
    }
    if(overlays->SetOverlayFlag(surface.stagingOverlay,vr::VROverlayFlags_SideBySide_Parallel,true)!=vr::VROverlayError_None)return false;
    if(overlays->SetOverlayFlag(surface.stagingOverlay,vr::VROverlayFlags_IsPremultiplied,true)!=vr::VROverlayError_None)return false;
    if(overlays->SetOverlayWidthInMeters(surface.stagingOverlay,width)!=vr::VROverlayError_None)return false;
    overlays->SetOverlayTextureBounds(surface.stagingOverlay,&bounds);
    overlays->SetOverlayTransformAbsolute(surface.stagingOverlay,vr::TrackingUniverseStanding,&anchor);
  }
  // The compositor imports color only. Rendering is ordered on one GL context,
  // so the three color FBOs can reuse one depth attachment without extra fences.
  GLuint sharedDepth=0;
  for(auto &buffer:surface.buffers){
    if(!makeBuffer(buffer,gbm,display,eyePixels,sharedDepth)){fprintf(stderr,"[surface] buffer id=%d failed\n",id);return false;}
    sharedDepth=buffer.depth;
  }
  fprintf(stdout,"[surface] created id=%d width=%.3f eye_px=%d\n",id,width,eyePixels);fflush(stdout);
  return true;
}
bool resizeSurfaceBuffers(Surface &surface,int pixels,gbm_device *gbm,EGLDisplay display){
  std::array<Buffer,3> next{};GLuint depth=0;
  for(auto &buffer:next){
    if(!makeBuffer(buffer,gbm,display,pixels,depth)){for(auto &item:next)destroyBuffer(item,display);return false;}
    depth=buffer.depth;
  }
  vr::VROverlay()->ClearOverlayTexture(surface.overlay);
  for(int i=0;i<3;i++){destroyBuffer(surface.buffers[i],display);surface.buffers[i]=next[i];}
  surface.eyePixels=pixels;
  return true;
}
bool replaceGroupedSurface(Surface &surface,int pixels,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &transform){
  if(surface.previous)return true;
  static uint64_t generation=0;
  Surface replacement;
  if(!createSurface(replacement,surface.instanceId,surface.objectId,surface.width,pixels,gbm,display,transform,".resize."+std::to_string(++generation))){destroySurface(replacement,display);return false;}
  replacement.referenceEyePixels=surface.referenceEyePixels;
  replacement.resolutionLimited=surface.resolutionLimited;
  replacement.previous=std::make_unique<Surface>(std::move(surface));
  surface=std::move(replacement);
  return true;
}
int groupBufferPixels(int required,int maximum){
  return int(std::min(double(maximum),std::ceil(required*1.25/256)*256));
}
GLuint compile(GLenum type,const char *source){
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);fprintf(stderr,"shader error: %s\n",log);}
  return shader;
}
void faceViewer(vr::HmdMatrix34_t &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,float right[3],float up[3],float forward[3]){
  float length=0;
  for(int i=0;i<3;i++){forward[i]=panel.m[i][3]-head.m[i][3];length+=forward[i]*forward[i];}
  if(length<.0001f){for(int i=0;i<3;i++)forward[i]=-anchor.m[i][2];}
  else {length=std::sqrt(length);for(int i=0;i<3;i++)forward[i]/=length;}
  float hint[3]={anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]};
  float alignment=forward[0]*hint[0]+forward[1]*hint[1]+forward[2]*hint[2];
  if(std::fabs(alignment)>.95f)for(int i=0;i<3;i++)hint[i]=anchor.m[i][2];
  right[0]=forward[1]*hint[2]-forward[2]*hint[1];
  right[1]=forward[2]*hint[0]-forward[0]*hint[2];
  right[2]=forward[0]*hint[1]-forward[1]*hint[0];
  length=std::sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
  for(int i=0;i<3;i++)right[i]/=length;
  up[0]=right[1]*forward[2]-right[2]*forward[1];
  up[1]=right[2]*forward[0]-right[0]*forward[2];
  up[2]=right[0]*forward[1]-right[1]*forward[0];
  for(int i=0;i<3;i++){panel.m[i][0]=right[i];panel.m[i][1]=up[i];panel.m[i][2]=-forward[i];}
}
vr::HmdMatrix34_t panelTransform(const Panel &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const Object *object){
  if(panel.mode=="object"){
    auto transform=anchor;if(!object)return transform;
    // Scene +Z is forward; the anchor's and the object's rotation matrices use OpenVR's -Z forward.
    float rotation[9];objectRotationMatrix(object->pitch,object->yaw,object->roll,rotation);
    const float offset[3]={panel.x,panel.y,-panel.z};
    float local[3]={object->x,object->y,-object->z};
    for(int row=0;row<3;row++)for(int axis=0;axis<3;axis++)local[row]+=rotation[row*3+axis]*offset[axis];
    for(int row=0;row<3;row++){
      for(int axis=0;axis<3;axis++)transform.m[row][3]+=anchor.m[row][axis]*local[axis];
      for(int col=0;col<3;col++){float value=0;for(int axis=0;axis<3;axis++)value+=anchor.m[row][axis]*rotation[axis*3+col];transform.m[row][col]=value;}
    }
    if(panel.facesViewer){float right[3],up[3],forward[3];faceViewer(transform,anchor,head,right,up,forward);}
    return transform;
  }
  auto transform=panel.mode=="headLocked"?head:anchor;
  for(int row=0;row<3;row++)transform.m[row][3]+=transform.m[row][0]*panel.x+transform.m[row][1]*panel.y-transform.m[row][2]*panel.z;
  if(panel.mode=="arbitrary")for(int row=0;row<3;row++)for(int col=0;col<3;col++){
    float value=0;for(int axis=0;axis<3;axis++)value+=anchor.m[row][axis]*panel.rotation[axis*3+col];transform.m[row][col]=value;
  }
  if(panel.mode=="viewerFacing"){float right[3],up[3],forward[3];faceViewer(transform,anchor,head,right,up,forward);}
  return transform;
}
bool createLaser(vr::VROverlayHandle_t &handle,int hand){
  auto *overlays=vr::VROverlay();const std::string key="pocketspace.laser."+std::to_string(hand);
  if(overlays->CreateOverlay(key.c_str(),key.c_str(),&handle)!=vr::VROverlayError_None)return false;
  unsigned char pixels[256*2*4]{};
  for(int x=0;x<256;x++)for(int y=0;y<2;y++){
    const int i=(y*256+x)*4;pixels[i]=hand?40:255;pixels[i+1]=hand?220:150;pixels[i+2]=255;
    pixels[i+3]=(unsigned char)(x<4?x*38:x>251?(255-x)*38:150);
  }
  if(overlays->SetOverlayRaw(handle,pixels,256,2,4)!=vr::VROverlayError_None)return false;
  overlays->SetOverlayInputMethod(handle,vr::VROverlayInputMethod_None);
  overlays->SetOverlayWidthInMeters(handle,2.f);
  return true;
}
void laserRay(const vr::HmdMatrix34_t &pose,int hand,float origin[3],float direction[3]){
  constexpr float tilt=.6981317008f;
  const float localOrigin[3]={hand==0?.012694f:-.012694f,-.02522f,.020687f};
  for(int i=0;i<3;i++){
    origin[i]=pose.m[i][3];
    for(int axis=0;axis<3;axis++)origin[i]+=pose.m[i][axis]*localOrigin[axis];
    direction[i]=-std::sin(tilt)*pose.m[i][1]-std::cos(tilt)*pose.m[i][2];
  }
}
void updateLaser(vr::VROverlayHandle_t handle,bool &shown,bool visible,int hand,const vr::TrackedDevicePose_t &controller,const vr::HmdMatrix34_t &head,float hitDistance){
  auto *overlays=vr::VROverlay();
  if(!visible||!controller.bPoseIsValid){if(shown){overlays->HideOverlay(handle);shown=false;}return;}
  const auto &pose=controller.mDeviceToAbsoluteTracking;
  const float length=std::clamp(hitDistance,.02f,2.f);
  float direction[3],origin[3],center[3],normal[3],up[3],viewLength=0;
  laserRay(pose,hand,origin,direction);
  for(int i=0;i<3;i++){
    center[i]=origin[i]+direction[i]*length*.5f;normal[i]=head.m[i][3]-center[i];
  }
  float along=normal[0]*direction[0]+normal[1]*direction[1]+normal[2]*direction[2];
  for(int i=0;i<3;i++){normal[i]-=along*direction[i];viewLength+=normal[i]*normal[i];}
  if(viewLength<.0001f){for(int i=0;i<3;i++)normal[i]=pose.m[i][1];}
  else {viewLength=std::sqrt(viewLength);for(int i=0;i<3;i++)normal[i]/=viewLength;}
  up[0]=normal[1]*direction[2]-normal[2]*direction[1];
  up[1]=normal[2]*direction[0]-normal[0]*direction[2];
  up[2]=normal[0]*direction[1]-normal[1]*direction[0];
  vr::HmdMatrix34_t beam{};
  for(int i=0;i<3;i++){beam.m[i][0]=direction[i];beam.m[i][1]=up[i];beam.m[i][2]=normal[i];beam.m[i][3]=center[i];}
  const auto widthError=overlays->SetOverlayWidthInMeters(handle,length);
  if(widthError!=vr::VROverlayError_None)fprintf(stderr,"[laser] width hand=%d error=%d\n",hand,(int)widthError);
  overlays->SetOverlayTransformAbsolute(handle,vr::TrackingUniverseStanding,&beam);
  if(!shown){overlays->ShowOverlay(handle);shown=true;}
}
void beginGrab(GrabState &grab,const Object &object,const ControllerSnapshot &hand,bool direct){
  grab={};grab.objectId=object.id;grab.direct=direct;
  const float delta[3]={object.x-hand.x,object.y-hand.y,object.z-hand.z};
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++)grab.offset[axis]+=hand.rotation[row*3+axis]*delta[row];
  std::copy(hand.rotation,hand.rotation+9,grab.startHand);
  grab.startPitch=object.pitch;grab.startYaw=object.yaw;grab.startRoll=object.roll;
}
void objectRotationMatrix(float pitch,float yaw,float roll,float m[9]){
  const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
  m[0]=cy*cr+sy*sp*sr;m[1]=-cy*sr+sy*sp*cr;m[2]=sy*cp;
  m[3]=cp*sr;m[4]=cp*cr;m[5]=-sp;
  m[6]=-sy*cr+cy*sp*sr;m[7]=sy*sr+cy*sp*cr;m[8]=cy*cp;
}
bool frameGrabDown(int hand,GrabButton button){
  const bool trigger=button!=GrabButton::Grip&&frameTriggerDown(hand);
  const bool grip=button!=GrabButton::Trigger&&(frameButtonDown(hand,"/input/grip/click")||frameAxis(hand,"/input/grip/value")>.2f);
  return trigger||grip;
}
void updateManipulation(Scene &scene,std::array<GrabState,2> &grabs,float dt,int instanceId,const std::array<int,2> &rayOwners,std::array<int,2> &grabOwners){
  for(int h=0;h<2;h++){
    const auto &hand=controllers[h];
    auto &grab=grabs[h];
    auto ownedByOther=[&](int id){return grabs[1-h].objectId==id;};
    const int previousGrab=grab.objectId;
    Object *object=findSceneItem(scene.objects,grab.objectId);
    if(previousGrab&&(!object||!hand.connected||!object->visible||!object->grabbable)){if(scene.physics)scene.physics->release(previousGrab,false);sceneInput().addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;grabNeedsRelease[h]=true;}
    if(object){
      grab.releaseFrames=frameGrabDown(h,object->grabButton)?0:grab.releaseFrames+1;
      if(grab.releaseFrames>=5){
        if(scene.physics)scene.physics->release(previousGrab,true);
        sceneInput().addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;
      }
    }
    if(grabNeedsRelease[h]&&hand.connected&&frameInput.started()&&frameInput.hand(h).valid&&!frameGrabDown(h,GrabButton::Both))grabNeedsRelease[h]=false;
    if(!object&&hand.valid&&hand.connected&&!grabNeedsRelease[h]&&(grabOwners[h]==0||grabOwners[h]==instanceId)){
      for(auto &candidate:scene.objects){
        if(!candidate.visible||!candidate.grabbable||ownedByOther(candidate.id))continue;
        // Acquisition requires a current engine-ray hit on this mesh.
        if(rayOwners[h]==instanceId&&meshCurrentObject[h]==candidate.id&&frameGrabDown(h,candidate.grabButton)){
          const float dx=candidate.x-hand.x,dy=candidate.y-hand.y,dz=candidate.z-hand.z;
          const float reach=.10f+candidate.scale*.5f;
          const bool direct=candidate.directGrab&&dx*dx+dy*dy+dz*dz<reach*reach;
          beginGrab(grab,candidate,hand,direct);
          grabOwners[h]=instanceId;
          if(scene.physics&&scene.physics->contains(candidate.id))scene.physics->hold(candidate.id);
          const float centerDistance=std::sqrt(dx*dx+dy*dy+dz*dz);
          grab.laserDistance=std::clamp(std::min(meshHitDistance[h],centerDistance-std::max(.1f,candidate.scale*.5f)),.02f,2.f);
          sceneInput().addMeshEvent("overlay.grabStart",candidate.id,h,meshHitDistance[h],meshHitPoint[h]);object=&candidate;break;
        }
      }
    }
    if(object&&!hand.valid&&scene.physics)scene.physics->cancelThrow(object->id);
    if(object&&hand.valid){
      const float oldX=object->x,oldY=object->y,oldZ=object->z,oldPitch=object->pitch,oldYaw=object->yaw,oldRoll=object->roll;
      // Thumbstick deflection past a small dead zone, read from the raw controller.
      auto stick=[&](const char *path){const float v=std::clamp(frameAxis(h,path),-1.f,1.f);return std::fabs(v)<kStickDeadZone?0.f:v;};
      const float x=stick("/input/thumbstick/x"),y=stick("/input/thumbstick/y");
      if(!grab.direct&&(x||y)){
        if(x){
          if(object->laserPitchWithStickClick&&frameButtonDown(h,"/input/thumbstick/click"))object->pitch+=x*kStickTurnRate*dt;
          else if(object->laserYaw)object->yaw+=x*kStickTurnRate*dt;
        }
        if(y&&object->depthAdjust){
          const float length=std::sqrt(grab.offset[0]*grab.offset[0]+grab.offset[1]*grab.offset[1]+grab.offset[2]*grab.offset[2]);
          if(length>.001f){const float next=std::max(.06f,length+y*kStickDepthRate*dt);for(float &v:grab.offset)v*=next/length;grab.laserDistance=std::clamp(grab.laserDistance+next-length,.02f,2.f);}
        }
      }
      const auto &r=hand.rotation;
      object->x=hand.x+r[0]*grab.offset[0]+r[1]*grab.offset[1]+r[2]*grab.offset[2];
      object->y=hand.y+r[3]*grab.offset[0]+r[4]*grab.offset[1]+r[5]*grab.offset[2];
      object->z=hand.z+r[6]*grab.offset[0]+r[7]*grab.offset[1]+r[8]*grab.offset[2];
      sceneInput().addOverlayEvent("overlay.grabMove",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
      if(std::fabs(object->x-oldX)+std::fabs(object->y-oldY)+std::fabs(object->z-oldZ)>.00001f)sceneInput().addOverlayEvent("overlay.move",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
      if(grab.direct&&(object->handPitch||object->handYaw||object->handRoll)){
        float delta[9]{};
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)for(int k=0;k<3;k++)delta[row*3+col]+=r[row*3+k]*grab.startHand[col*3+k];
        // Controller snapshots use forward-positive Z; model vertices use backward-positive Z.
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)if((row==2)!=(col==2))delta[row*3+col]=-delta[row*3+col];
        const float pitch=object->handPitch?std::asin(std::clamp(-delta[5],-1.f,1.f)):0;
        const float yaw=object->handYaw?std::atan2(delta[2],delta[8]):0;
        const float roll=object->handRoll?std::atan2(delta[3],delta[4]):0;
        float gesture[9],initial[9],combined[9]{};
        objectRotationMatrix(pitch,yaw,roll,gesture);
        objectRotationMatrix(grab.startPitch,grab.startYaw,grab.startRoll,initial);
        for(int row=0;row<3;row++)for(int col=0;col<3;col++)for(int k=0;k<3;k++)combined[row*3+col]+=gesture[row*3+k]*initial[k*3+col];
        object->pitch=std::asin(std::clamp(-combined[5],-1.f,1.f));
        object->yaw=std::atan2(combined[2],combined[8]);
        object->roll=std::atan2(combined[3],combined[4]);
      }
      if(std::fabs(object->pitch-oldPitch)+std::fabs(object->yaw-oldYaw)+std::fabs(object->roll-oldRoll)>.00001f)sceneInput().addOverlayEvent("overlay.rotate",object->id,h,0,0,0,object->pitch-oldPitch,object->yaw-oldYaw,object->roll-oldRoll);
    }
  }
}
ProjectedBounds projectWorldBounds(const WorldBounds &world,const float eye[3],const float right[3],const float up[3],const float forward[3]){
  ProjectedBounds result;
  for(const auto &corner:world){
    float ray[3];for(int row=0;row<3;row++)ray[row]=corner[row]-eye[row];
    float x=0,y=0,d=0;for(int axis=0;axis<3;axis++){x+=ray[axis]*right[axis];y+=ray[axis]*up[axis];d+=ray[axis]*forward[axis];}
    if(d<=.05f){result.valid=false;return result;}
    result.x0=std::min(result.x0,x/d);result.x1=std::max(result.x1,x/d);result.y0=std::min(result.y0,y/d);result.y1=std::max(result.y1,y/d);
    result.near=std::min(result.near,d);result.far=std::max(result.far,d);
  }
  return result;
}
ProjectedBounds projectedBounds(const Object &object,const ModelAsset &geometry,const vr::HmdMatrix34_t &anchor,const float eye[3],const float right[3],const float up[3],const float forward[3]){
  return projectWorldBounds(worldBounds(object,geometry,anchor),eye,right,up,forward);
}
bool mayOcclude(const ProjectedBounds &occluder,const ProjectedBounds &target){
  if(!occluder.valid||!target.valid)return true;
  constexpr float margin=1e-4f;
  return occluder.near<=target.far+margin&&occluder.x1+margin>=target.x0&&occluder.x0-margin<=target.x1&&occluder.y1+margin>=target.y0&&occluder.y0-margin<=target.y1;
}
ScissorRegion projectedScissor(const ProjectedBounds &bounds,const float center[3],const float eye[3],const float right[3],const float up[3],const float forward[3],float halfWidth,int pixels){
  const ScissorRegion full{0,0,pixels,pixels};
  if(!bounds.valid||halfWidth<=0)return full;
  float plane=0,centerX=0,centerY=0;
  for(int axis=0;axis<3;axis++){const float delta=center[axis]-eye[axis];plane+=delta*forward[axis];centerX+=delta*right[axis];centerY+=delta*up[axis];}
  if(plane<=0)return full;
  const float x0=((plane*bounds.x0-centerX)/halfWidth*.5f+.5f)*pixels;
  const float x1=((plane*bounds.x1-centerX)/halfWidth*.5f+.5f)*pixels;
  const float y0=((plane*bounds.y0-centerY)/halfWidth*.5f+.5f)*pixels;
  const float y1=((plane*bounds.y1-centerY)/halfWidth*.5f+.5f)*pixels;
  if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1))return full;
  const int lowX=int(std::floor(std::clamp(x0-2,0.f,float(pixels)))),lowY=int(std::floor(std::clamp(y0-2,0.f,float(pixels))));
  const int highX=int(std::ceil(std::clamp(x1+2,0.f,float(pixels)))),highY=int(std::ceil(std::clamp(y1+2,0.f,float(pixels))));
  return {lowX,lowY,std::max(0,highX-lowX),std::max(0,highY-lowY)};
}
int objectCropPixels(const ProjectedBounds bounds[2],const float center[3],const float eyes[2][3],const float right[3],const float up[3],const float forward[3],float referenceWidth,int referencePixels){
  int extent=0;
  for(int eye=0;eye<2;eye++){
    if(!bounds[eye].valid)return referencePixels;
    auto r=projectedScissor(bounds[eye],center,eyes[eye],right,up,forward,referenceWidth*.5f,referencePixels);
    if(!r.width||!r.height)return referencePixels;
    const int mid=referencePixels/2;
    extent=std::max({extent,mid-r.x,r.x+r.width-mid,mid-r.y,r.y+r.height-mid});
  }
  // Extra transparent border and 64-pixel buckets limit resize churn.
  return std::min(referencePixels,std::max(128,((2*extent+32+63)/64)*64));
}
GroupLayout groupLayout(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const float center[3],const float right[3],const float up[3],const float forward[3],float fallbackWidth,int maxPixels,const FrameBoundsCache *cache){
  GroupLayout result;float extent=0,density=0;
  for(int eye=0;eye<2;eye++){
    float plane=0,cx=0,cy=0;for(int axis=0;axis<3;axis++){const float delta=center[axis]-eyes[eye][axis];plane+=delta*forward[axis];cx+=delta*right[axis];cy+=delta*up[axis];}
    for(const auto *object:members){
      const auto &asset=scene.assets[size_t(object->asset)-1];const auto &geometry=object->animationPose?object->animationPose->geometry:asset;
      const auto bounds=cache?cache->common(*object,eye):projectedBounds(*object,geometry,anchor,eyes[eye],right,up,forward);
      auto &combined=result.bounds[eye];combined.valid=combined.valid&&bounds.valid;
      combined.x0=std::min(combined.x0,bounds.x0);combined.x1=std::max(combined.x1,bounds.x1);combined.y0=std::min(combined.y0,bounds.y0);combined.y1=std::max(combined.y1,bounds.y1);combined.near=std::min(combined.near,bounds.near);combined.far=std::max(combined.far,bounds.far);
      if(bounds.valid&&plane>.05f)extent=std::max({extent,std::abs(plane*bounds.x0-cx),std::abs(plane*bounds.x1-cx),std::abs(plane*bounds.y0-cy),std::abs(plane*bounds.y1-cy)});
      // Account for the common plane's obliquity. Its minimum angular
      // magnification is plane/cos(theta), so matching an object's own facing
      // plane requires referenceDensity * axialDepth / plane, not radial range.
      float axialDepth=0;for(int axis=0;axis<3;axis++){const float delta=anchor.m[axis][3]+anchor.m[axis][0]*object->x+anchor.m[axis][1]*object->y-anchor.m[axis][2]*object->z-head.m[axis][3];axialDepth+=delta*forward[axis];}
      const float referenceWidth=std::max(.55f,object->scale*2.1f*objectRadius(*object));
      density=std::max(density,asset.eyePixels/referenceWidth*std::max(.05f,axialDepth)/std::max(.05f,plane));
    }
  }
  density=std::max(1.f,density);
  result.width=(!result.bounds[0].valid||!result.bounds[1].valid)?fallbackWidth:std::max(.01f,2*extent+32/density);
  const double wanted=std::ceil(double(result.width)*density/64)*64;
  result.limited=wanted>maxPixels;
  result.pixels=int(std::clamp(wanted,128.0,double(maxPixels)));
  return result;
}
GroupPlane groupPlane(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache *cache){
  GroupPlane result;result.transform=anchor;
  faceViewer(result.transform,anchor,head,result.right,result.up,result.forward);
  float lowX=INFINITY,highX=-INFINITY,lowY=INFINITY,highY=-INFINITY;bool valid=true;
  for(int eye=0;eye<2;eye++){
    float distance=0,eyeX=0,eyeY=0;
    for(int axis=0;axis<3;axis++){const float delta=eyes[eye][axis]-anchor.m[axis][3];distance-=delta*result.forward[axis];eyeX+=delta*result.right[axis];eyeY+=delta*result.up[axis];}
    if(distance<=.05f){valid=false;break;}
    for(const auto *object:members){
      const auto &asset=scene.assets[size_t(object->asset)-1];const auto &geometry=object->animationPose?object->animationPose->geometry:asset;
      const auto bounds=cache?cache->common(*object,eye):projectedBounds(*object,geometry,anchor,eyes[eye],result.right,result.up,result.forward);
      if(!bounds.valid){valid=false;break;}
      lowX=std::min(lowX,eyeX+distance*bounds.x0);highX=std::max(highX,eyeX+distance*bounds.x1);
      lowY=std::min(lowY,eyeY+distance*bounds.y0);highY=std::max(highY,eyeY+distance*bounds.y1);
    }
    if(!valid)break;
  }
  if(valid&&std::isfinite(lowX)&&std::isfinite(highX)&&std::isfinite(lowY)&&std::isfinite(highY)){
    const float x=(lowX+highX)*.5f,y=(lowY+highY)*.5f;
    for(int row=0;row<3;row++)result.transform.m[row][3]+=result.right[row]*x+result.up[row]*y;
  }
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++)result.sceneOrigin[axis]+=(result.transform.m[row][3]-anchor.m[row][3])*anchor.m[row][axis]*(axis==2?-1.f:1.f);
  return result;
}
bool finiteGroupBounds(const std::vector<const Object*> &members,const FrameBoundsCache &cache){
  for(const auto *object:members)for(int eye=0;eye<2;eye++){
    const auto &b=cache.common(*object,eye);
    if(!object->visible||!b.valid||!std::isfinite(b.x0)||!std::isfinite(b.x1)||!std::isfinite(b.y0)||!std::isfinite(b.y1)||!std::isfinite(b.near)||!std::isfinite(b.far))return false;
  }
  return !members.empty();
}
GroupLayout tileLayout(const std::vector<const Object*> &items,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum){
  const auto plane=groupPlane(items,scene,anchor,head,eyes,&cache);
  const float center[3]={plane.transform.m[0][3],plane.transform.m[1][3],plane.transform.m[2][3]};
  return groupLayout(items,scene,anchor,head,eyes,center,plane.right,plane.up,plane.forward,1,maximum,&cache);
}
GroupSplitPlan partitionGroup(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,uint64_t baseline){
  GroupSplitPlan best;if(members.size()<2||!finiteGroupBounds(members,cache))return best;
  auto layout=[&](const std::vector<const Object*> &items){return tileLayout(items,scene,anchor,head,eyes,cache,maximum);};
  uint64_t bestCost=baseline*4/5; // Require at least 20% fewer allocated texels.
  for(int axis=0;axis<2;axis++){
    auto ordered=members;
    auto coordinate=[&](const Object *object){const auto &a=cache.common(*object,0),&b=cache.common(*object,1);return axis?(double(a.y0)+a.y1+b.y0+b.y1):(double(a.x0)+a.x1+b.x0+b.x1);};
    std::sort(ordered.begin(),ordered.end(),[&](const Object *a,const Object *b){const double x=coordinate(a),y=coordinate(b);return x==y?a->id<b->id:x<y;});
    for(int quarter=1;quarter<=3;quarter++){
      const size_t cut=ordered.size()*quarter/4;if(!cut||cut==ordered.size())continue;
      GroupSplitPlan candidate;candidate.members[0].assign(ordered.begin(),ordered.begin()+cut);candidate.members[1].assign(ordered.begin()+cut,ordered.end());
      uint64_t cost=0;bool limited=false;
      for(int tile=0;tile<2;tile++){
        const auto child=layout(candidate.members[tile]);limited|=child.limited;
        candidate.pixels[tile]=groupBufferPixels(child.pixels,maximum);candidate.widths[tile]=child.width*float(candidate.pixels[tile])/child.pixels;
        cost+=uint64_t(candidate.pixels[tile])*candidate.pixels[tile];
      }
      if(limited||cost>=bestCost)continue;
      candidate.parentPixelsSquared=baseline;candidate.childPixelsSquared=cost;bestCost=cost;best=std::move(candidate);
    }
  }
  return best;
}
GroupSplitPlan planGroupSplit(const std::vector<const Object*> &members,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,int allocated,const RenderGroupSplitPolicy &policy){
  if(members.size()<2||!finiteGroupBounds(members,cache))return {};
  const auto parent=tileLayout(members,scene,anchor,head,eyes,cache,maximum);
  if(!parent.limited&&parent.pixels<=policy.targetEyePixels)return {};
  const uint64_t baseline=std::max(allocated,groupBufferPixels(parent.pixels,maximum));
  return partitionGroup(members,scene,anchor,head,eyes,cache,maximum,baseline*baseline);
}
GroupAdjustmentPlan planGroupAdjustment(const std::vector<const Object*> &first,const std::vector<const Object*> &second,const Scene &scene,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,const float eyes[2][3],const FrameBoundsCache &cache,int maximum,const RenderGroupSplitPolicy &policy){
  if((!policy.merge&&!policy.rebalance)||!finiteGroupBounds(first,cache)||!finiteGroupBounds(second,cache))return {};
  auto members=first;members.insert(members.end(),second.begin(),second.end());
  // Moving blended objects across overlay surfaces can change compositor
  // ordering even when their pixel density and common plane are preserved.
  for(const auto *object:members)for(const auto &part:scene.assets[size_t(object->asset)-1].primitives)if(part.alphaMode==2)return {};

  auto layout=[&](const std::vector<const Object*> &items){return tileLayout(items,scene,anchor,head,eyes,cache,maximum);};
  // Compare steady requested allocations, not old oversized buffers awaiting
  // their normal shrink delay. Otherwise shrinking can masquerade as regrouping.
  const uint64_t a=groupBufferPixels(layout(first).pixels,maximum),b=groupBufferPixels(layout(second).pixels,maximum),baseline=a*a+b*b;
  if(policy.merge){
    const auto merged=layout(members);const uint64_t pixels=groupBufferPixels(merged.pixels,maximum);
    if(!merged.limited&&merged.pixels<=policy.targetEyePixels&&pixels*pixels<baseline*4/5){
      GroupAdjustmentPlan result;result.merge=true;result.partition.members[0]=std::move(members);result.partition.pixels[0]=int(pixels);
      result.partition.widths[0]=merged.width*float(pixels)/merged.pixels;result.partition.parentPixelsSquared=baseline;result.partition.childPixelsSquared=pixels*pixels;return result;
    }
  }
  if(!policy.rebalance)return {};
  GroupAdjustmentPlan result;result.partition=partitionGroup(members,scene,anchor,head,eyes,cache,maximum,baseline);
  if(result){
    const int key=objectSurfaceKey(*first.front());int unchanged=0,swapped=0;
    for(int side=0;side<2;side++)for(const auto *object:result.partition.members[side]){
      const bool wasFirst=objectSurfaceKey(*object)==key;unchanged+=wasFirst==(side==0);swapped+=wasFirst!=(side==0);
    }
    if(swapped>unchanged){std::swap(result.partition.members[0],result.partition.members[1]);std::swap(result.partition.pixels[0],result.partition.pixels[1]);std::swap(result.partition.widths[0],result.partition.widths[1]);}
  }
  return result;
}
void laserSceneRay(const ControllerSnapshot &hand,int handIndex,float origin[3],float direction[3]){
  constexpr float tilt=.6981317008f;
  const float tip[3]={handIndex==0?.012694f:-.012694f,-.02522f,.020687f};
  origin[0]=hand.x;origin[1]=hand.y;origin[2]=hand.z;
  for(int row=0;row<3;row++){
    for(int axis=0;axis<3;axis++)origin[row]+=hand.rotation[row*3+axis]*tip[axis];
    direction[row]=-std::sin(tilt)*hand.rotation[row*3+1]-std::cos(tilt)*hand.rotation[row*3+2];
  }
}
float meshHit(const Object &object,const ModelAsset &asset,const ControllerSnapshot &hand,int handIndex,bool accelerated){
  if(!hand.valid)return std::numeric_limits<float>::max();
  float origin[3],direction[3];laserSceneRay(hand,handIndex,origin,direction);
  const float miss=std::numeric_limits<float>::max();
  if(object.scale<=0)return miss;
  // Invert the renderer's F*R transform once per ray, rather than applying it
  // to every triangle. Keep direction unit length; convert hit distance back.
  float rotation[9];objectRotationMatrix(object.pitch,object.yaw,object.roll,rotation);
  const float delta[3]={origin[0]-object.x,origin[1]-object.y,-(origin[2]-object.z)};
  const float reflectedDirection[3]={direction[0],direction[1],-direction[2]};
  float localOrigin[3]{},localDirection[3]{};
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++){
    localOrigin[axis]+=rotation[row*3+axis]*delta[row]/object.scale;
    localDirection[axis]+=rotation[row*3+axis]*reflectedDirection[row];
  }
  if(object.animationPose&&object.animationPose->gpuSkin && &asset==&object.animationPose->geometry){
    float near;if(!picking::bounds(localOrigin,localDirection,asset.low,asset.high,miss,near))return miss;
    prepareSkinPicking(currentScene->assets[size_t(object.asset)-1],*object.animationPose);
  }
  float best=miss;
  if(accelerated)best=picking::intersect(asset,localOrigin,localDirection,rayTriangle);
  else{
    float near;
    if(!picking::bounds(localOrigin,localDirection,asset.low,asset.high,miss,near))return miss;
    for(const auto &part:asset.primitives)for(size_t index=0;index+14<part.vertices.size();index+=15){
      const float *v=part.vertices.data()+index;float distance;
      if(rayTriangle(localOrigin,localDirection,v,v+5,v+10,distance))best=std::min(best,distance);
    }
  }
  return best==miss?miss:best*object.scale;
}
