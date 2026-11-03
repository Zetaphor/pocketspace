"""Exercise the actual native grab state machine without VR or graphics."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
state=(root/'src/engine_state.h').read_text()
render_header=(root/'src/render_host.h').read_text()
render=(root/'src/render_host.cpp').read_text()
source=state
def body(start):
    begin=render.index(start)
    return render[begin:render.index('\n}',begin)+3]
objects=source[source.index('enum class GrabButton'):source.index('struct Scene {')]
controller=source[source.index('struct ControllerSnapshot'):source.index('inline vr::HmdMatrix34_t trackingAnchor')]
inline_input=render_header[render_header.index('inline bool frameButtonDown'):render_header.index('bool frameGrabDown')]
functions=(render_header[render_header.index('struct GrabState'):render_header.index('void beginGrab(')]
           +body('void beginGrab(')+body('void objectRotationMatrix(')+inline_input
           +body('bool frameGrabDown(')+body('void updateManipulation('))
header=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <memory>
#include "animation.h"
#include "scene_lookup.h"
struct ModelPose;
'''
mocks=r'''
// This harness covers physics-disabled grabs; real-body integration lives in
// physics-api-regression.cpp.
struct NoPhysics {bool contains(int)const{return false;}void hold(int){}void release(int,bool){}void cancelThrow(int){}};
struct Scene {std::vector<Object> objects;NoPhysics *physics=nullptr;};
struct Component {bool isBoolean=false,boolean=false;float value=0;};
struct Hand {bool valid=true;std::map<std::string,Component> raw;};
struct Monitor {
 Hand hands[2];int starts=0,ends=0;
 const Hand &hand(int h){return hands[h];}
 bool started() const{return true;}
 void addOverlayEvent(const std::string &type,int,int,float=0,float=0,float=0,float=0,float=0,float=0){ends+=type=="overlay.grabEnd";}
 void addMeshEvent(const std::string &type,int,int,float,const std::array<float,3>&){starts+=type=="overlay.grabStart";}
} frameInput;
Monitor &sceneInput(){return frameInput;}
int meshCurrentObject[2]{};float meshHitDistance[2]{2,2};std::array<float,3> meshHitPoint[2]{};
bool grabNeedsRelease[2]{};
'''
tests=r'''
static void buttons(int hand,bool trigger,bool grip){
 frameInput.hands[hand].raw["/input/trigger/click"]={true,trigger,0};
 frameInput.hands[hand].raw["/input/grip/click"]={true,grip,0};
}
int main(){
 std::array<int,2> grabOwners{};
 for(int h=0;h<2;h++)for(auto mode:{GrabButton::Trigger,GrabButton::Grip,GrabButton::Both})for(int mask=0;mask<4;mask++){
  frameInput={};grabNeedsRelease[0]=grabNeedsRelease[1]=false;meshCurrentObject[0]=meshCurrentObject[1]=0;
  controllers[0]=controllers[1]={};controllers[h].connected=true;controllers[h].valid=true;
  for(int i=0;i<3;i++)controllers[h].rotation[i*3+i]=1;
  Scene scene;Object object{};object.id=1;object.scale=.2;object.z=1;object.grabbable=true;object.grabButton=mode;
  scene.objects.push_back(object);std::array<GrabState,2> grabs{};
  meshCurrentObject[h]=1;buttons(h,mask&1,mask&2);
  const bool allowed=(mode!=GrabButton::Grip&&(mask&1))||(mode!=GrabButton::Trigger&&(mask&2));
  updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(bool(grabs[h].objectId)==allowed);assert(grabs[1-h].objectId==0);
  if(allowed){
   assert(!grabs[h].direct);assert(frameInput.starts==1);
   // Losing selection/pose does not drop a held raw grab.
   meshCurrentObject[h]=0;controllers[h].valid=false;updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[h].objectId==1);
   controllers[h].connected=true;controllers[h].valid=true;
   if(mode==GrabButton::Both){buttons(h,false,true);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[h].objectId==1);buttons(h,true,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[h].objectId==1);}
   buttons(h,mode==GrabButton::Grip,mode==GrabButton::Trigger); // disallowed button must not keep a grab alive
   for(int i=0;i<4;i++)updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);
   assert(grabs[h].objectId==1);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[h].objectId);assert(frameInput.ends==1);
  }
  // Without a current engine-ray hit, a held button cannot acquire.
  grabs={};meshCurrentObject[h]=0;buttons(h,true,true);controllers[h].connected=true;controllers[h].valid=true;
  updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[h].objectId);
 }
 // Analog thresholds, defaults, direct mode, current-hit requirement and hand ownership.
 frameInput={};Scene scene;Object object{};
 assert(object.grabButton==GrabButton::Trigger);
 object.id=1;object.scale=.2;object.z=.05;object.grabbable=true;object.grabButton=GrabButton::Grip;scene.objects.push_back(object);
 controllers[0]=controllers[1]={};for(auto &c:controllers){c.connected=true;c.valid=true;for(int i=0;i<3;i++)c.rotation[i*3+i]=1;}
 meshCurrentObject[0]=meshCurrentObject[1]=0;std::array<GrabState,2> grabs{};
 frameInput.hands[0].raw["/input/grip/value"].value=.21;
 updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId);
 meshCurrentObject[0]=meshCurrentObject[1]=1;buttons(1,false,true);
 updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[0].objectId==1&&grabs[0].direct);assert(!grabs[1].objectId);
 // Changing the allowed button while held follows the new policy.
 scene.objects[0].grabButton=GrabButton::Trigger;meshCurrentObject[0]=meshCurrentObject[1]=0;
 for(int i=0;i<5;i++)updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);
 assert(!grabs[0].objectId);
 frameInput.hands[0].raw["/input/grip/value"].value=.2;assert(!frameGrabDown(0,GrabButton::Grip));
 frameInput.hands[0].raw["/input/trigger/value"].value=.21;assert(frameGrabDown(0,GrabButton::Trigger));
 // Removed held IDs must end once and not block the other hand forever.
 frameInput={};grabNeedsRelease[0]=grabNeedsRelease[1]=false;
 scene.objects[0].grabButton=GrabButton::Trigger;grabs={};meshCurrentObject[0]=1;buttons(0,true,false);
 updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[0].objectId==1);
 scene.objects.clear();updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId&&frameInput.ends==1);
 updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(frameInput.ends==1);
 scene.objects.push_back(object);scene.objects[0].grabButton=GrabButton::Trigger;
 updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId); // held button cannot acquire replacement
 buttons(0,false,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);buttons(0,true,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[0].objectId==1);
 // Tracking loss preserves a hold, physical disconnect ends it and rearms.
 controllers[0].valid=false;updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[0].objectId==1);
 controllers[0].connected=false;updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId&&grabNeedsRelease[0]);
 controllers[0].connected=controllers[0].valid=true;updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId);
 buttons(0,false,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);buttons(0,true,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[0].objectId==1);
 scene.objects[0].visible=false;updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId);
 // A committed scene reload also waits for release before accepting a held button.
 scene.objects[0].visible=true;grabNeedsRelease[0]=true;updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(!grabs[0].objectId);
 buttons(0,false,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);buttons(0,true,false);updateManipulation(scene,grabs,1.f/90,0,{},grabOwners);assert(grabs[0].objectId==1);
 // A second scene may share local ID 1, but only the ray owner acquires it.
 frameInput={};grabNeedsRelease[0]=grabNeedsRelease[1]=false;
 controllers[0]={};controllers[0].connected=controllers[0].valid=true;
 for(int i=0;i<3;i++)controllers[0].rotation[i*3+i]=1;
 Scene competing;Object rival{};rival.id=1;rival.scale=.2f;rival.z=1;
 rival.visible=rival.grabbable=true;rival.grabButton=GrabButton::Trigger;
 competing.objects.push_back(rival);std::array<GrabState,2> competingGrabs{};
 meshCurrentObject[0]=1;buttons(0,true,false);
 updateManipulation(competing,competingGrabs,1.f/90,2,{1,0},grabOwners);assert(!competingGrabs[0].objectId);
 // Owning the ray is not enough while another scene holds this hand.
 grabOwners[0]=1;
 updateManipulation(competing,competingGrabs,1.f/90,2,{2,0},grabOwners);assert(!competingGrabs[0].objectId);
 grabOwners[0]=0;
 updateManipulation(competing,competingGrabs,1.f/90,2,{2,0},grabOwners);assert(competingGrabs[0].objectId==1);
 // Distant grabs read yaw and depth from the raw thumbstick, past a dead zone.
 {
  frameInput={};grabNeedsRelease[0]=grabNeedsRelease[1]=false;grabOwners={};
  Scene stickScene;Object held{};held.id=1;held.scale=.2f;held.z=1;held.grabbable=held.laserYaw=held.depthAdjust=true;
  stickScene.objects.push_back(held);std::array<GrabState,2> stickGrabs{};
  meshCurrentObject[0]=1;buttons(0,true,false);
  updateManipulation(stickScene,stickGrabs,1.f/90,0,{},grabOwners);assert(stickGrabs[0].objectId==1&&!stickGrabs[0].direct);
  auto length=[&]{const auto &o=stickGrabs[0].offset;return std::sqrt(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]);};
  auto stick=[&](float x,float y){frameInput.hands[0].raw["/input/thumbstick/x"].value=x;frameInput.hands[0].raw["/input/thumbstick/y"].value=y;};
  const float before=length();
  stick(.1f,-.1f);updateManipulation(stickScene,stickGrabs,1.f/90,0,{},grabOwners);
  assert(stickScene.objects[0].yaw==0&&std::fabs(length()-before)<1e-6f);
  stick(1,0);updateManipulation(stickScene,stickGrabs,1.f/90,0,{},grabOwners);
  assert(std::fabs(stickScene.objects[0].yaw-kStickTurnRate/90)<1e-5f);
  stick(0,1);updateManipulation(stickScene,stickGrabs,1.f/90,0,{},grabOwners);
  assert(std::fabs(length()-before-kStickDepthRate/90)<1e-4f);
  stick(0,-1);updateManipulation(stickScene,stickGrabs,1.f/90,0,{},grabOwners);
  assert(std::fabs(length()-before)<1e-4f);
 }
 puts("PASS grab buttons: modes, both hands, analogs, ray-hit gating, direct/laser, release, cross-scene ownership, policy changes, thumbstick yaw and depth");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'grab.cpp';path.write_text(header+objects+controller+mocks+functions+tests)
 binary=Path(tmp)/'grab'
 subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra','-I',str(root/'src'),str(path),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
