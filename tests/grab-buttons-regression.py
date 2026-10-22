"""Exercise the actual native grab state machine without VR or graphics."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/main.cpp').read_text()
objects=source[source.index('enum class GrabButton'):source.index('struct Scene {')]
controller=source[source.index('struct ControllerSnapshot'):source.index('static vr::HmdMatrix34_t trackingAnchor')]
functions=source[source.index('struct GrabState {'):source.index('static bool rayTriangle(')]
header=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
'''
mocks=r'''
struct Scene {std::vector<Object> objects;bool laserVisible[2]{true,true};};
struct Component {bool isBoolean=false,boolean=false;float value=0;};
struct Hand {std::map<std::string,Component> raw;};
struct Monitor {
 Hand hands[2];int starts=0,ends=0;
 const Hand &hand(int h){return hands[h];}
 void addOverlayEvent(const std::string &type,int,int,float=0,float=0,float=0,float=0,float=0,float=0){starts+=type=="overlay.grabStart";ends+=type=="overlay.grabEnd";}
} frameInput;
struct PointerSnapshot {bool valid=false,down=false,secondaryDown=false;float scrollX=0,scrollY=0;int hand=-1;};
std::map<int,PointerSnapshot> pointerStates;
int meshCurrentObject[2]{};float meshHitDistance[2]{2,2};
'''
tests=r'''
static void buttons(int hand,bool trigger,bool grip){
 frameInput.hands[hand].raw["/input/trigger/click"]={true,trigger,0};
 frameInput.hands[hand].raw["/input/grip/click"]={true,grip,0};
}
int main(){
 for(int h=0;h<2;h++)for(auto mode:{GrabButton::Trigger,GrabButton::Grip,GrabButton::Both})for(int mask=0;mask<4;mask++){
  frameInput={};pointerStates.clear();meshCurrentObject[0]=meshCurrentObject[1]=0;
  controllers[0]=controllers[1]={};controllers[h].valid=true;
  for(int i=0;i<3;i++)controllers[h].rotation[i*3+i]=1;
  Scene scene;Object object{};object.id=1;object.scale=.2;object.z=1;object.grabbable=object.autoManipulate=true;object.grabButton=mode;
  scene.objects.push_back(object);std::array<GrabState,2> grabs{};
  meshCurrentObject[h]=1;buttons(h,mask&1,mask&2);
  const bool allowed=(mode!=GrabButton::Grip&&(mask&1))||(mode!=GrabButton::Trigger&&(mask&2));
  updateManipulation(scene,grabs,1.f/90);assert(bool(grabs[h].objectId)==allowed);assert(grabs[1-h].objectId==0);
  if(allowed){
   assert(!grabs[h].direct);assert(frameInput.starts==1);
   // Losing selection/pose does not drop a held raw grab.
   meshCurrentObject[h]=0;controllers[h].valid=false;updateManipulation(scene,grabs,1.f/90);assert(grabs[h].objectId==1);
   controllers[h].valid=true;
   if(mode==GrabButton::Both){buttons(h,false,true);updateManipulation(scene,grabs,1.f/90);assert(grabs[h].objectId==1);buttons(h,true,false);updateManipulation(scene,grabs,1.f/90);assert(grabs[h].objectId==1);}
   buttons(h,mode==GrabButton::Grip,mode==GrabButton::Trigger); // disallowed button must not keep a grab alive
   for(int i=0;i<4;i++)updateManipulation(scene,grabs,1.f/90);
   assert(grabs[h].objectId==1);updateManipulation(scene,grabs,1.f/90);assert(!grabs[h].objectId);assert(frameInput.ends==1);
  }
  // A native trigger event cannot bypass a grip-only policy.
  buttons(h,false,false);grabs={};scene.laserVisible[h]=false;
  pointerStates[1].down=true;pointerStates[1].hand=h;controllers[h].valid=true;
  updateManipulation(scene,grabs,1.f/90);assert(bool(grabs[h].objectId)==(mode!=GrabButton::Grip));
 }
 // Analog thresholds, defaults, direct mode, current-hit requirement and hand ownership.
 frameInput={};pointerStates.clear();Scene scene;Object object{};
 assert(object.grabButton==GrabButton::Trigger);
 object.id=1;object.scale=.2;object.z=.05;object.grabbable=object.autoManipulate=true;object.grabButton=GrabButton::Grip;scene.objects.push_back(object);
 controllers[0]=controllers[1]={};for(auto &c:controllers){c.valid=true;for(int i=0;i<3;i++)c.rotation[i*3+i]=1;}
 meshCurrentObject[0]=meshCurrentObject[1]=0;std::array<GrabState,2> grabs{};
 frameInput.hands[0].raw["/input/grip/value"].value=.21;
 updateManipulation(scene,grabs,1.f/90);assert(!grabs[0].objectId);
 meshCurrentObject[0]=meshCurrentObject[1]=1;buttons(1,false,true);
 updateManipulation(scene,grabs,1.f/90);assert(grabs[0].objectId==1&&grabs[0].direct);assert(!grabs[1].objectId);
 // Changing the allowed button while held follows the new policy.
 scene.objects[0].grabButton=GrabButton::Trigger;meshCurrentObject[0]=meshCurrentObject[1]=0;
 for(int i=0;i<5;i++)updateManipulation(scene,grabs,1.f/90);
 assert(!grabs[0].objectId);
 frameInput.hands[0].raw["/input/grip/value"].value=.2;assert(!frameGrabDown(0,GrabButton::Grip));
 frameInput.hands[0].raw["/input/trigger/value"].value=.21;assert(frameGrabDown(0,GrabButton::Trigger));
 puts("PASS grab buttons: modes, both hands, analogs, native gating, direct/laser, release, ownership, policy changes");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'grab.cpp';path.write_text(header+objects+controller+mocks+functions+tests)
 binary=Path(tmp)/'grab'
 subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra',str(path),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
