#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <openvr.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <quickjs.h>
#include "model_asset.h"
#include "controller_input.h"
#include "frame_input.h"
#include "panel.h"

constexpr int kDefaultEyePixels=768;
struct Buffer {gbm_bo *bo=nullptr;int fd=-1;vr::SharedTextureHandle_t handle=0;EGLImageKHR image=EGL_NO_IMAGE_KHR;GLuint color=0,depth=0,fbo=0;};
struct Surface {int objectId=0,eyePixels=kDefaultEyePixels;float width=.7f;vr::VROverlayHandle_t overlay=vr::k_ulOverlayHandleInvalid;Buffer buffers[3];bool shown=false,interactive=false,panel=false;};
static PFNEGLCREATEIMAGEKHRPROC createImage=nullptr;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage=nullptr;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC imageRenderbuffer=nullptr;
static std::atomic<bool> running{true};
struct Object {
  int id,asset;float x,y,z,scale,yaw,pitch=0,roll=0;
  bool visible=true,laserPassThrough=true,grabbable=false,directGrab=true,autoManipulate=false;
  bool laserYaw=false,laserPitchWithStickClick=false,depthAdjust=false;
  bool handPitch=false,handYaw=false,handRoll=false;
};
struct Scene {std::vector<Object> objects;std::vector<ModelAsset> assets;std::vector<Panel> panels;std::vector<int> interactiveIds;int nextId=1,nextPanelId=1;bool laserVisible[2]{};};
static Scene *currentScene=nullptr;
struct ControllerSnapshot {bool valid=false;float x=0,y=0,z=0;float rotation[9]{};};
static ControllerSnapshot controllers[2];
static controller_input::Input controllerInput;
static frame_input::Monitor frameInput;
static bool inputPollingRequested=false;static int hoveredObject[2]={0,0},meshHoveredObject[2]={0,0};
struct PointerSnapshot {bool valid=false,down=false,secondaryDown=false;float x=0,y=0,scrollX=0,scrollY=0;int hand=-1;};
static std::map<int,PointerSnapshot> pointerStates;
static JSContext *inputCallbackCtx=nullptr;static JSValue inputCallback=JS_UNDEFINED;
static void stop(int){running=false;}
static JSValue inputEventValue(JSContext *ctx,const frame_input::Event &event){JSValue item=JS_NewObject(ctx);JS_SetPropertyStr(ctx,item,"type",JS_NewString(ctx,event.type.c_str()));JS_SetPropertyStr(ctx,item,"hand",JS_NewString(ctx,event.hand.c_str()));JS_SetPropertyStr(ctx,item,"control",JS_NewString(ctx,event.control.c_str()));JS_SetPropertyStr(ctx,item,"objectId",JS_NewInt32(ctx,event.objectId));JS_SetPropertyStr(ctx,item,"down",JS_NewBool(ctx,event.component.boolean));JS_SetPropertyStr(ctx,item,"value",JS_NewFloat64(ctx,event.component.value));JS_SetPropertyStr(ctx,item,"dx",JS_NewFloat64(ctx,event.dx));JS_SetPropertyStr(ctx,item,"dy",JS_NewFloat64(ctx,event.dy));JS_SetPropertyStr(ctx,item,"dz",JS_NewFloat64(ctx,event.dz));JS_SetPropertyStr(ctx,item,"dpitch",JS_NewFloat64(ctx,event.dpitch));JS_SetPropertyStr(ctx,item,"dyaw",JS_NewFloat64(ctx,event.dyaw));JS_SetPropertyStr(ctx,item,"droll",JS_NewFloat64(ctx,event.droll));return item;}
static JSValue jsLog(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc){const char *s=JS_ToCString(ctx,argv[0]);if(s){fprintf(stdout,"[js] %s\n",s);JS_FreeCString(ctx,s);}}
  return JS_UNDEFINED;
}
static JSValue jsGetController(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getController(hand): hand is left or right");
  const char *hand=JS_ToCString(ctx,argv[0]);if(!hand)return JS_EXCEPTION;
  int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;JS_FreeCString(ctx,hand);
  if(index<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &c=controllers[index];
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,c.valid));
  JS_SetPropertyStr(ctx,object,"x",JS_NewFloat64(ctx,c.x));
  JS_SetPropertyStr(ctx,object,"y",JS_NewFloat64(ctx,c.y));
  JS_SetPropertyStr(ctx,object,"z",JS_NewFloat64(ctx,c.z));
  JSValue rotation=JS_NewArray(ctx);
  for(int i=0;i<9;i++)JS_SetPropertyUint32(ctx,rotation,i,JS_NewFloat64(ctx,c.rotation[i]));
  JS_SetPropertyStr(ctx,object,"rotation",rotation);
  return object;
}
static int handIndex(JSContext *ctx,JSValueConst value){
  const char *hand=JS_ToCString(ctx,value);if(!hand)return -1;
  int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;
  JS_FreeCString(ctx,hand);return index;
}
static JSValue jsGetControllerInput(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getControllerInput(hand): hand is left or right");
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &state=frameInput.hand(h);JSValue object=JS_NewObject(ctx);JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));JS_SetPropertyStr(ctx,object,"hand",JS_NewString(ctx,h?"right":"left"));
  JSValue raw=JS_NewObject(ctx);for(const auto &[name,value]:state.raw){JS_SetPropertyStr(ctx,raw,name.c_str(),value.isBoolean?JS_NewBool(ctx,value.boolean):JS_NewFloat64(ctx,value.value));}JS_SetPropertyStr(ctx,object,"raw",raw);
  auto rawValue=[&](const char *name){auto it=state.raw.find(name);return it==state.raw.end()?0.f:it->second.isBoolean?(it->second.boolean?1.f:0.f):it->second.value;};
  JSValue buttons=JS_NewObject(ctx);const char *controls[]={"trigger","grip","bumper","thumbstick","a","b","x","y","menu","system","view","dpad_up","dpad_down","dpad_left","dpad_right"};for(const char *name:controls){JSValue item=JS_NewObject(ctx);std::string base=std::string("/input/")+name;JS_SetPropertyStr(ctx,item,"down",JS_NewBool(ctx,rawValue((base+"/click").c_str())!=0));JS_SetPropertyStr(ctx,item,"touched",JS_NewBool(ctx,rawValue((base+"/touch").c_str())!=0));JS_SetPropertyStr(ctx,buttons,name, item);}JS_SetPropertyStr(ctx,object,"buttons",buttons);
  JSValue analog=JS_NewObject(ctx);JS_SetPropertyStr(ctx,analog,"trigger",JS_NewFloat64(ctx,rawValue("/input/trigger/value")));JS_SetPropertyStr(ctx,analog,"grip",JS_NewFloat64(ctx,rawValue("/input/grip/value")));JSValue stick=JS_NewObject(ctx);JS_SetPropertyStr(ctx,stick,"x",JS_NewFloat64(ctx,rawValue("/input/thumbstick/x")));JS_SetPropertyStr(ctx,stick,"y",JS_NewFloat64(ctx,rawValue("/input/thumbstick/y")));JS_SetPropertyStr(ctx,analog,"thumbstick",stick);JS_SetPropertyStr(ctx,object,"analog",analog);
  JSValue capacitive=JS_NewObject(ctx);for(const char *name:controls){std::string path=std::string("/input/capsense/")+name;JS_SetPropertyStr(ctx,capacitive,name,JS_NewFloat64(ctx,rawValue(path.c_str())));}JS_SetPropertyStr(ctx,object,"capacitive",capacitive);return object;
}
static JSValue jsOnInputEvent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(argc<1||!JS_IsFunction(ctx,argv[0]))return JS_ThrowTypeError(ctx,"onInputEvent(callback)");if(inputCallbackCtx)JS_FreeValue(inputCallbackCtx,inputCallback);inputCallbackCtx=ctx;inputCallback=JS_DupValue(ctx,argv[0]);return JS_UNDEFINED;}
static JSValue jsPollInputEvents(JSContext *ctx,JSValueConst,int,JSValueConst *){
  JSValue array=JS_NewArray(ctx);auto events=frameInput.takeEvents();uint32_t i=0;for(const auto &event:events){JSValue item=JS_NewObject(ctx);JS_SetPropertyStr(ctx,item,"type",JS_NewString(ctx,event.type.c_str()));JS_SetPropertyStr(ctx,item,"hand",JS_NewString(ctx,event.hand.c_str()));JS_SetPropertyStr(ctx,item,"control",JS_NewString(ctx,event.control.c_str()));JS_SetPropertyStr(ctx,item,"objectId",JS_NewInt32(ctx,event.objectId));JS_SetPropertyStr(ctx,item,"down",JS_NewBool(ctx,event.component.boolean));JS_SetPropertyStr(ctx,item,"value",JS_NewFloat64(ctx,event.component.value));JS_SetPropertyStr(ctx,item,"dx",JS_NewFloat64(ctx,event.dx));JS_SetPropertyStr(ctx,item,"dy",JS_NewFloat64(ctx,event.dy));JS_SetPropertyStr(ctx,item,"dz",JS_NewFloat64(ctx,event.dz));JS_SetPropertyStr(ctx,item,"dpitch",JS_NewFloat64(ctx,event.dpitch));JS_SetPropertyStr(ctx,item,"dyaw",JS_NewFloat64(ctx,event.dyaw));JS_SetPropertyStr(ctx,item,"droll",JS_NewFloat64(ctx,event.droll));JS_SetPropertyUint32(ctx,array,i++,item);}return array;
}
static JSValue jsGetInput(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"getInput(hand): hand is left or right");
  inputPollingRequested=true;
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  const auto &state=controllerInput.hand(h);
  auto value=[&](controller_input::Control c)->const controller_input::Value &{return state.values[controller_input::index(c)];};
  JSValue object=JS_NewObject(ctx);JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));
  auto digital=[&](const char *name,controller_input::Control c){JS_SetPropertyStr(ctx,object,name,JS_NewBool(ctx,value(c).active&&value(c).down));};
  auto analog=[&](const char *name,controller_input::Control c){JS_SetPropertyStr(ctx,object,name,JS_NewFloat64(ctx,value(c).active?value(c).x:0));};
  digital("gripClick",controller_input::Control::GripClick);digital("gripTouch",controller_input::Control::GripTouch);analog("gripValue",controller_input::Control::GripValue);
  digital("triggerClick",controller_input::Control::TriggerClick);digital("triggerTouch",controller_input::Control::TriggerTouch);analog("triggerValue",controller_input::Control::TriggerValue);
  digital("thumbstickClick",controller_input::Control::ThumbstickClick);digital("thumbstickTouch",controller_input::Control::ThumbstickTouch);
  JSValue stick=JS_NewObject(ctx);const auto &axis=value(controller_input::Control::Thumbstick);
  JS_SetPropertyStr(ctx,stick,"x",JS_NewFloat64(ctx,axis.active?axis.x:0));JS_SetPropertyStr(ctx,stick,"y",JS_NewFloat64(ctx,axis.active?axis.y:0));
  JS_SetPropertyStr(ctx,object,"thumbstick",stick);
  digital("bumperClick",controller_input::Control::BumperClick);digital("bumperTouch",controller_input::Control::BumperTouch);
  digital("systemClick",controller_input::Control::SystemClick);digital("systemTouch",controller_input::Control::SystemTouch);
  digital("menuClick",controller_input::Control::MenuClick);digital("viewClick",controller_input::Control::ViewClick);
  digital("aClick",controller_input::Control::AClick);digital("bClick",controller_input::Control::BClick);
  digital("xClick",controller_input::Control::XClick);digital("yClick",controller_input::Control::YClick);
  digital("dpadUpClick",controller_input::Control::DpadUpClick);digital("dpadDownClick",controller_input::Control::DpadDownClick);
  digital("dpadLeftClick",controller_input::Control::DpadLeftClick);digital("dpadRightClick",controller_input::Control::DpadRightClick);
  return object;
}
static JSValue jsPulse(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(argc<1)return JS_ThrowTypeError(ctx,"pulse(hand, seconds=0.05, amplitude=0.5)");
  const int h=handIndex(ctx,argv[0]);if(h<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  double seconds=.05,amplitude=.5;if(argc>1&&JS_ToFloat64(ctx,&seconds,argv[1])<0)return JS_EXCEPTION;
  if(argc>2&&JS_ToFloat64(ctx,&amplitude,argv[2])<0)return JS_EXCEPTION;
  if(!std::isfinite(seconds)||!std::isfinite(amplitude)||seconds<0||seconds>1||amplitude<0||amplitude>1)return JS_ThrowRangeError(ctx,"invalid pulse duration or amplitude");
  return JS_NewBool(ctx,controllerInput.pulse(h,(float)seconds,(float)amplitude));
}
static JSValue jsSetLaserVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<2)return JS_ThrowTypeError(ctx,"setLaserVisible(hand, visible)");
  const char *hand=JS_ToCString(ctx,argv[0]);if(!hand)return JS_EXCEPTION;
  int index=std::string(hand)=="left"?0:std::string(hand)=="right"?1:-1;JS_FreeCString(ctx,hand);
  if(index<0)return JS_ThrowRangeError(ctx,"hand must be left or right");
  currentScene->laserVisible[index]=JS_ToBool(ctx,argv[1])>0;
  return JS_UNDEFINED;
}
static JSValue jsGetPointer(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc>0&&JS_ToInt32(ctx,&id,argv[0])<0)return JS_EXCEPTION;
  if(argc==0&&currentScene&&currentScene->interactiveIds.size()==1)id=currentScene->interactiveIds.front();
  const auto found=pointerStates.find(id);
  PointerSnapshot state=found==pointerStates.end()?PointerSnapshot{}:found->second;
  if(currentScene)for(const auto &object:currentScene->objects)if(object.id==id&&!object.grabbable)state.down=false;
  JSValue object=JS_NewObject(ctx);
  JS_SetPropertyStr(ctx,object,"valid",JS_NewBool(ctx,state.valid));
  JS_SetPropertyStr(ctx,object,"down",JS_NewBool(ctx,state.down));
  JS_SetPropertyStr(ctx,object,"x",JS_NewFloat64(ctx,state.x));
  JS_SetPropertyStr(ctx,object,"y",JS_NewFloat64(ctx,state.y));
  JS_SetPropertyStr(ctx,object,"hand",state.hand==0?JS_NewString(ctx,"left"):state.hand==1?JS_NewString(ctx,"right"):JS_NULL);
  return object;
}
static Panel *findPanel(int id){if(!currentScene)return nullptr;for(auto &panel:currentScene->panels)if(panel.id==id)return &panel;return nullptr;}
static int colorPart(JSContext *ctx,JSValueConst value,int fallback){if(JS_IsNumber(value)){int n=0;JS_ToInt32(ctx,&n,value);return std::clamp(n,0,255);}return fallback;}
static JSValue jsCreatePanel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){if(!currentScene||argc<1)return JS_ThrowTypeError(ctx,"createPanel(options)");if(!JS_IsObject(argv[0]))return JS_ThrowTypeError(ctx,"createPanel options must be an object");int w=1024,h=512;double wm=1.2;JSValue f=JS_GetPropertyStr(ctx,argv[0],"widthPixels");if(!JS_IsUndefined(f))JS_ToInt32(ctx,&w,f);JS_FreeValue(ctx,f);f=JS_GetPropertyStr(ctx,argv[0],"heightPixels");if(!JS_IsUndefined(f))JS_ToInt32(ctx,&h,f);JS_FreeValue(ctx,f);f=JS_GetPropertyStr(ctx,argv[0],"widthMeters");if(!JS_IsUndefined(f))JS_ToFloat64(ctx,&wm,f);JS_FreeValue(ctx,f);Panel panel;if(!panelInit(panel,currentScene->nextPanelId++,w,h,(float)wm,(float)(wm*h/w),""))return JS_ThrowInternalError(ctx,"panel initialization failed");currentScene->panels.push_back(std::move(panel));return JS_NewInt32(ctx,currentScene->panels.back().id);}
static JSValue jsPanelClear(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"panel.clear(id,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");int r=argc>1?colorPart(ctx,argv[1],0):0,g=argc>2?colorPart(ctx,argv[2],0):0,b=argc>3?colorPart(ctx,argv[3],0):0,a=argc>4?colorPart(ctx,argv[4],255):255;panelClear(*p,r,g,b,a);return JS_UNDEFINED;}
static JSValue jsPanelSetFont(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"panel.loadFont(id,path,pixels)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*path=JS_ToCString(ctx,argv[1]);if(!path)return JS_EXCEPTION;p->fontPath=path;if(argc>2)JS_ToInt32(ctx,&p->fontPixels,argv[2]);JS_FreeCString(ctx,path);return JS_UNDEFINED;}
static JSValue jsPanelText(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0,x=0,y=0;if(argc<4||JS_ToInt32(ctx,&id,argv[0])<0||JS_ToInt32(ctx,&x,argv[1])<0||JS_ToInt32(ctx,&y,argv[2])<0)return JS_ThrowTypeError(ctx,"panel.text(id,x,y,text,...)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*t=JS_ToCString(ctx,argv[3]);if(!t)return JS_EXCEPTION;int r=argc>4?colorPart(ctx,argv[4],255):255,g=argc>5?colorPart(ctx,argv[5],255):255,b=argc>6?colorPart(ctx,argv[6],255):255,a=argc>7?colorPart(ctx,argv[7],255):255;bool ok=panelText(*p,x,y,t,r,g,b,a,argc>8?colorPart(ctx,argv[8],p->fontPixels):p->fontPixels);JS_FreeCString(ctx,t);return JS_NewBool(ctx,ok);}
static JSValue jsPanelSetTransform(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<5||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"panel.setTransform(id,mode,x,y,z)");auto*p=findPanel(id);if(!p)return JS_ThrowRangeError(ctx,"unknown panel");const char*mode=JS_ToCString(ctx,argv[1]);if(mode){p->mode=mode;JS_FreeCString(ctx,mode);}double x=0,y=0,z=0;if(JS_ToFloat64(ctx,&x,argv[2])<0||JS_ToFloat64(ctx,&y,argv[3])<0||JS_ToFloat64(ctx,&z,argv[4])<0)return JS_EXCEPTION;p->x=(float)x;p->y=(float)y;p->z=(float)z;return JS_UNDEFINED;}
static JSValue jsPanelVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"panel.setVisible(id,visible)");auto*p=findPanel(id);if(p)p->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;}
static JSValue jsPanelPresent(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0)return JS_EXCEPTION;auto*p=findPanel(id);if(p)panelUpload(*p);return JS_UNDEFINED;}
static Object *findObject(int id){
  if(!currentScene)return nullptr;
  for(auto &object:currentScene->objects)if(object.id==id)return &object;
  return nullptr;
}
static JSValue jsEnableObjectInteraction(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||!findObject(id))return JS_ThrowRangeError(ctx,"enableObjectInteraction(objectId) requires a live object");
  auto *object=findObject(id);object->laserPassThrough=false;object->grabbable=true;
  auto &ids=currentScene->interactiveIds;
  if(std::find(ids.begin(),ids.end(),id)==ids.end()){
    ids.push_back(id);
  }
  return JS_UNDEFINED;
}
static JSValue jsEnableObjectSurface(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0||!findObject(id))return JS_ThrowRangeError(ctx,"enableObjectSurface(objectId) requires a live object");
  // Kept for existing scripts: every object now owns a full-sphere surface.
  return JS_UNDEFINED;
}
static JSValue jsSetObjectInteraction(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!JS_IsObject(argv[1]))return JS_ThrowTypeError(ctx,"setObjectInteraction(id, options)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  auto readBool=[&](const char *name,bool &value){JSValue field=JS_GetPropertyStr(ctx,argv[1],name);if(!JS_IsUndefined(field))value=JS_ToBool(ctx,field)>0;JS_FreeValue(ctx,field);};
  readBool("laserPassThrough",object->laserPassThrough);
  readBool("grabbable",object->grabbable);
  readBool("directGrab",object->directGrab);
  readBool("laserYaw",object->laserYaw);
  readBool("laserPitchWithStickClick",object->laserPitchWithStickClick);
  readBool("depthAdjust",object->depthAdjust);
  JSValue axes=JS_GetPropertyStr(ctx,argv[1],"handRotationAxes");
  if(JS_IsObject(axes)){
    auto readAxis=[&](const char *name,bool &value){JSValue field=JS_GetPropertyStr(ctx,axes,name);if(!JS_IsUndefined(field))value=JS_ToBool(ctx,field)>0;JS_FreeValue(ctx,field);};
    readAxis("pitch",object->handPitch);readAxis("yaw",object->handYaw);readAxis("roll",object->handRoll);
  }
  JS_FreeValue(ctx,axes);
  object->autoManipulate=object->grabbable;
  if(object->grabbable)object->laserPassThrough=false;
  auto &ids=currentScene->interactiveIds;
  ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
  if(!object->laserPassThrough)ids.push_back(id);
  return JS_UNDEFINED;
}
static bool number(JSContext *ctx,JSValueConst value,float &out){
  double n=0;if(JS_ToFloat64(ctx,&n,value)<0||!std::isfinite(n))return false;out=(float)n;return std::isfinite(out);
}
static JSValue jsLoadModel(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<1||currentScene->assets.size()>=32)return JS_ThrowRangeError(ctx,"loadModel(path) requires a path; limit is 32 assets");
  const char *path=JS_ToCString(ctx,argv[0]);if(!path)return JS_EXCEPTION;
  std::string name(path);JS_FreeCString(ctx,path);
  int eyePixels=kDefaultEyePixels;
  if(argc>1&&JS_ToInt32(ctx,&eyePixels,argv[1])<0)return JS_EXCEPTION;
  if(eyePixels<256||eyePixels>2048||eyePixels%64!=0)return JS_ThrowRangeError(ctx,"eyePixels must be a multiple of 64 from 256 to 2048");
  for(size_t i=0;i<currentScene->assets.size();i++)if(currentScene->assets[i].path==name){
    if(argc>1&&currentScene->assets[i].eyePixels!=eyePixels)return JS_ThrowRangeError(ctx,"model already loaded at a different eyePixels resolution");
    return JS_NewInt32(ctx,(int)i+1);
  }
  ModelAsset asset;
  if(!loadModelFile(name,asset))return JS_ThrowTypeError(ctx,"could not load model: %s",name.c_str());
  asset.eyePixels=eyePixels;
  currentScene->assets.push_back(std::move(asset));
  return JS_NewInt32(ctx,(int)currentScene->assets.size());
}
static JSValue jsCreateObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  if(!currentScene||argc<4||currentScene->objects.size()>=256)return JS_ThrowRangeError(ctx,"createObject needs asset, x, y, z; limit is 256 objects");
  int asset=0;if(JS_ToInt32(ctx,&asset,argv[0])<0||asset<1||asset>(int)currentScene->assets.size())return JS_ThrowRangeError(ctx,"unknown asset id");
  float values[4]={0,0,0,.55f};
  for(int i=1;i<std::min(argc,5);i++)if(!number(ctx,argv[i],values[i-1]))return JS_ThrowTypeError(ctx,"object coordinates and scale must be finite numbers");
  if(values[3]<=0||values[3]>2)return JS_ThrowRangeError(ctx,"scale must be greater than 0 and at most 2 meters");
  int id=currentScene->nextId++;
  currentScene->objects.push_back({id,asset,values[0],values[1],values[2],values[3],0});
  return JS_NewInt32(ctx,id);
}
static JSValue jsSetObjectPosition(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float x,y,z;
  if(argc<4||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],x)||!number(ctx,argv[2],y)||!number(ctx,argv[3],z))return JS_ThrowTypeError(ctx,"setObjectPosition(id, x, y, z)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->x=x;object->y=y;object->z=z;return JS_UNDEFINED;
}
static JSValue jsSetObjectYaw(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float yaw;
  if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],yaw))return JS_ThrowTypeError(ctx,"setObjectYaw(id, radians)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->yaw=yaw;return JS_UNDEFINED;
}
static JSValue jsSetObjectRotation(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float pitch,yaw,roll;
  if(argc<4||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],pitch)||!number(ctx,argv[2],yaw)||!number(ctx,argv[3],roll))
    return JS_ThrowTypeError(ctx,"setObjectRotation(id, pitch, yaw, roll)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->pitch=pitch;object->yaw=yaw;object->roll=roll;return JS_UNDEFINED;
}
static JSValue jsSetObjectScale(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;float scale;
  if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0||!number(ctx,argv[1],scale))return JS_ThrowTypeError(ctx,"setObjectScale(id, meters)");
  if(scale<=0||scale>2)return JS_ThrowRangeError(ctx,"scale must be greater than 0 and at most 2 meters");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->scale=scale;return JS_UNDEFINED;
}
static JSValue jsSetObjectVisible(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<2||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"setObjectVisible(id, visible)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  object->visible=JS_ToBool(ctx,argv[1])>0;return JS_UNDEFINED;
}
static JSValue jsRemoveObject(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
  int id=0;if(argc<1||JS_ToInt32(ctx,&id,argv[0])<0)return JS_ThrowTypeError(ctx,"removeObject(id)");
  auto *object=findObject(id);if(!object)return JS_ThrowRangeError(ctx,"unknown object id");
  auto &ids=currentScene->interactiveIds;
  ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
  pointerStates.erase(id);
  currentScene->objects.erase(std::remove_if(currentScene->objects.begin(),currentScene->objects.end(),[id](const Object &entry){return entry.id==id;}),currentScene->objects.end());
  return JS_UNDEFINED;
}
struct ScriptHost {JSRuntime *rt=nullptr;JSContext *ctx=nullptr;JSValue update=JS_UNDEFINED;std::filesystem::file_time_type mtime{};Scene scene;};
static void bind(JSContext *ctx,const char *name,JSCFunction *function,int argc){
  JSValue global=JS_GetGlobalObject(ctx);JS_SetPropertyStr(ctx,global,name,JS_NewCFunction(ctx,function,name,argc));JS_FreeValue(ctx,global);
}
static void closeScript(ScriptHost &s){
  for(auto &asset:s.scene.assets)deleteModel(asset);
  if(s.ctx){JS_FreeValue(s.ctx,s.update);JS_FreeContext(s.ctx);}if(s.rt)JS_FreeRuntime(s.rt);
  s={};
}
static bool loadScript(ScriptHost &active,const std::string &path){
  std::ifstream file(path);if(!file){fprintf(stderr,"script unreadable: %s\n",path.c_str());return false;}
  std::ostringstream stream;stream<<file.rdbuf();std::string source=stream.str();
  ScriptHost candidate;candidate.rt=JS_NewRuntime();if(!candidate.rt)return false;
  JS_SetMemoryLimit(candidate.rt,128*1024*1024);
  candidate.ctx=JS_NewContext(candidate.rt);if(!candidate.ctx){closeScript(candidate);return false;}
  currentScene=&candidate.scene;
  bind(candidate.ctx,"print",jsLog,1);
  bind(candidate.ctx,"getController",jsGetController,1);
  bind(candidate.ctx,"getControllerInput",jsGetControllerInput,1);
  bind(candidate.ctx,"pollInputEvents",jsPollInputEvents,0);
  bind(candidate.ctx,"onInputEvent",jsOnInputEvent,1);
  bind(candidate.ctx,"getInput",jsGetInput,1);
  bind(candidate.ctx,"pulse",jsPulse,3);
  bind(candidate.ctx,"setLaserVisible",jsSetLaserVisible,2);
  bind(candidate.ctx,"enableObjectInteraction",jsEnableObjectInteraction,1);
  bind(candidate.ctx,"enableObjectSurface",jsEnableObjectSurface,1);
  bind(candidate.ctx,"setObjectInteraction",jsSetObjectInteraction,2);
  bind(candidate.ctx,"getPointer",jsGetPointer,0);
  bind(candidate.ctx,"createPanel",jsCreatePanel,1);
  bind(candidate.ctx,"panelClear",jsPanelClear,5);
  bind(candidate.ctx,"panelText",jsPanelText,9);
  bind(candidate.ctx,"panelLoadFont",jsPanelSetFont,3);
  bind(candidate.ctx,"panelPresent",jsPanelPresent,1);
  bind(candidate.ctx,"panelCanvasClear",jsPanelClear,5);
  bind(candidate.ctx,"panelCanvasText",jsPanelText,9);
  bind(candidate.ctx,"panelSetTransform",jsPanelSetTransform,5);
  bind(candidate.ctx,"panelSetVisible",jsPanelVisible,2);
  bind(candidate.ctx,"loadModel",jsLoadModel,1);
  bind(candidate.ctx,"createObject",jsCreateObject,5);
  bind(candidate.ctx,"setObjectPosition",jsSetObjectPosition,4);
  bind(candidate.ctx,"setObjectYaw",jsSetObjectYaw,2);
  bind(candidate.ctx,"setObjectRotation",jsSetObjectRotation,4);
  bind(candidate.ctx,"setObjectScale",jsSetObjectScale,2);
  bind(candidate.ctx,"setObjectVisible",jsSetObjectVisible,2);
  bind(candidate.ctx,"removeObject",jsRemoveObject,1);
  JSValue result=JS_Eval(candidate.ctx,source.c_str(),source.size(),path.c_str(),JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(candidate.ctx);const char *message=JS_ToCString(candidate.ctx,exception);
    fprintf(stderr,"[js] load error: %s\n",message?message:"unknown");if(message)JS_FreeCString(candidate.ctx,message);
    JS_FreeValue(candidate.ctx,exception);JS_FreeValue(candidate.ctx,result);closeScript(candidate);currentScene=&active.scene;return false;
  }
  JS_FreeValue(candidate.ctx,result);
  JSValue global=JS_GetGlobalObject(candidate.ctx);candidate.update=JS_GetPropertyStr(candidate.ctx,global,"update");JS_FreeValue(candidate.ctx,global);
  std::error_code ec;candidate.mtime=std::filesystem::last_write_time(path,ec);
  closeScript(active);active=std::move(candidate);currentScene=&active.scene;
  fprintf(stdout,"[js] loaded %s objects=%zu assets=%zu\n",path.c_str(),active.scene.objects.size(),active.scene.assets.size());fflush(stdout);return true;
}
static bool initEgl(EGLDisplay &display,EGLContext &context,EGLSurface &surface) {
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
static bool makeBuffer(Buffer &b,gbm_device *gbm,EGLDisplay display,int eyePixels) {
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
  glGenRenderbuffers(1,&b.depth);glBindRenderbuffer(GL_RENDERBUFFER,b.depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
  glGenFramebuffers(1,&b.fbo);glBindFramebuffer(GL_FRAMEBUFFER,b.fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,b.color);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,b.depth);
  bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;glBindFramebuffer(GL_FRAMEBUFFER,0);return ok;
}
static void destroyBuffer(Buffer &buffer,EGLDisplay display){
  if(buffer.fbo)glDeleteFramebuffers(1,&buffer.fbo);
  if(buffer.depth)glDeleteRenderbuffers(1,&buffer.depth);
  if(buffer.color)glDeleteRenderbuffers(1,&buffer.color);
  if(buffer.image!=EGL_NO_IMAGE_KHR)destroyImage(display,buffer.image);
  if(buffer.handle)vr::VRIPCResourceManager()->UnrefResource(buffer.handle);
  if(buffer.fd>=0)close(buffer.fd);
  if(buffer.bo)gbm_bo_destroy(buffer.bo);
  buffer={};
}
static void destroySurface(Surface &surface,EGLDisplay display){
  if(surface.overlay!=vr::k_ulOverlayHandleInvalid){
    vr::VROverlay()->ClearOverlayTexture(surface.overlay);
    vr::VROverlay()->DestroyOverlay(surface.overlay);
  }
  for(auto &buffer:surface.buffers)destroyBuffer(buffer,display);
  surface.overlay=vr::k_ulOverlayHandleInvalid;
}
static bool createSurface(Surface &surface,int id,float width,int eyePixels,bool interactive,gbm_device *gbm,EGLDisplay display,const vr::HmdMatrix34_t &anchor){
  surface.objectId=id;surface.width=width;surface.eyePixels=eyePixels;surface.interactive=interactive;
  const std::string key="sf_overlay_engine."+std::to_string(id);
  auto *overlays=vr::VROverlay();
  auto error=overlays->CreateOverlay(key.c_str(),key.c_str(),&surface.overlay);
  if(error!=vr::VROverlayError_None){fprintf(stderr,"[surface] create id=%d error=%d\n",id,(int)error);return false;}
  error=overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_SideBySide_Parallel,true);
  if(error!=vr::VROverlayError_None)return false;
  error=overlays->SetOverlayWidthInMeters(surface.overlay,width);
  if(error!=vr::VROverlayError_None)return false;
  vr::VRTextureBounds_t bounds{0,1,1,0};overlays->SetOverlayTextureBounds(surface.overlay,&bounds);
  overlays->SetOverlayTransformAbsolute(surface.overlay,vr::TrackingUniverseStanding,&anchor);
  if(interactive){
    overlays->SetOverlayInputMethod(surface.overlay,vr::VROverlayInputMethod_Mouse);
    overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,true);
    overlays->SetOverlayFlag(surface.overlay,vr::VROverlayFlags_SendVRSmoothScrollEvents,true);
    vr::VROverlayIntersectionMaskPrimitive_t mask{};
    mask.m_nPrimitiveType=vr::OverlayIntersectionPrimitiveType_Circle;
    mask.m_Primitive.m_Circle.m_flCenterX=.5f;
    mask.m_Primitive.m_Circle.m_flCenterY=.5f;
    mask.m_Primitive.m_Circle.m_flRadius=.3f;
    overlays->SetOverlayIntersectionMask(surface.overlay,&mask,1);
    vr::HmdVector2_t scale{{1,1}};overlays->SetOverlayMouseScale(surface.overlay,&scale);
  }
  for(auto &buffer:surface.buffers)if(!makeBuffer(buffer,gbm,display,eyePixels)){fprintf(stderr,"[surface] buffer id=%d failed\n",id);return false;}
  fprintf(stdout,"[surface] created id=%d width=%.3f eye_px=%d\n",id,width,eyePixels);fflush(stdout);
  return true;
}
static GLuint compile(GLenum type,const char *source) {
  GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);fprintf(stderr,"shader error: %s\n",log);}
  return shader;
}
static void setVec3(GLuint program,const char *name,float x,float y,float z){glUniform3f(glGetUniformLocation(program,name),x,y,z);}
static void faceViewer(vr::HmdMatrix34_t &panel,const vr::HmdMatrix34_t &anchor,const vr::HmdMatrix34_t &head,float right[3],float up[3],float forward[3]){
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
static bool createLaser(vr::VROverlayHandle_t &handle,int hand){
  auto *overlays=vr::VROverlay();const std::string key="sf_overlay_engine.laser."+std::to_string(hand);
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
static void updateLaser(vr::VROverlayHandle_t handle,bool &shown,bool visible,int hand,const vr::TrackedDevicePose_t &controller,const vr::HmdMatrix34_t &head){
  auto *overlays=vr::VROverlay();
  if(!visible||!controller.bPoseIsValid){if(shown){overlays->HideOverlay(handle);shown=false;}return;}
  const auto &pose=controller.mDeviceToAbsoluteTracking;
  // Frame controller /pose/tip and /pose/openxr_aim: 40 degrees about local X.
  constexpr float tilt=.6981317008f;
  const float localOrigin[3]={hand==0?.012694f:-.012694f,-.02522f,.020687f};
  float direction[3],origin[3],center[3],normal[3],up[3],viewLength=0;
  for(int i=0;i<3;i++){
    origin[i]=pose.m[i][3];
    for(int axis=0;axis<3;axis++)origin[i]+=pose.m[i][axis]*localOrigin[axis];
    direction[i]=-std::sin(tilt)*pose.m[i][1]-std::cos(tilt)*pose.m[i][2];
    center[i]=origin[i]+direction[i];normal[i]=head.m[i][3]-center[i];
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
  overlays->SetOverlayTransformAbsolute(handle,vr::TrackingUniverseStanding,&beam);
  if(!shown){overlays->ShowOverlay(handle);shown=true;}
}
struct GrabState {
  int objectId=0;bool direct=false;float offset[3]{},startHand[9]{},startPitch=0,startYaw=0,startRoll=0;
};
static void beginGrab(GrabState &grab,const Object &object,const ControllerSnapshot &hand,bool direct){
  grab={};grab.objectId=object.id;grab.direct=direct;
  const float delta[3]={object.x-hand.x,object.y-hand.y,object.z-hand.z};
  for(int axis=0;axis<3;axis++)for(int row=0;row<3;row++)grab.offset[axis]+=hand.rotation[row*3+axis]*delta[row];
  std::copy(hand.rotation,hand.rotation+9,grab.startHand);
  grab.startPitch=object.pitch;grab.startYaw=object.yaw;grab.startRoll=object.roll;
}
static void objectRotationMatrix(float pitch,float yaw,float roll,float m[9]){
  const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
  m[0]=cy*cr+sy*sp*sr;m[1]=-cy*sr+sy*sp*cr;m[2]=sy*cp;
  m[3]=cp*sr;m[4]=cp*cr;m[5]=-sp;
  m[6]=-sy*cr+cy*sp*sr;m[7]=sy*sr+cy*sp*cr;m[8]=cy*cp;
}
static bool frameButtonDown(int hand,const char *path){auto it=frameInput.hand(hand).raw.find(path);return it!=frameInput.hand(hand).raw.end()&&it->second.isBoolean&&it->second.boolean;}
static float frameAxis(int hand,const char *path){auto it=frameInput.hand(hand).raw.find(path);return it==frameInput.hand(hand).raw.end()?0.f:it->second.value;}
static void updateManipulation(Scene &scene,std::array<GrabState,2> &grabs,float dt){
  for(int h=0;h<2;h++){
    const auto &hand=controllers[h];
    auto &grab=grabs[h];
    auto ownedByOther=[&](int id){return grabs[1-h].objectId==id;};
    const int previousGrab=grab.objectId;
    Object *object=nullptr;for(auto &candidate:scene.objects)if(candidate.id==grab.objectId){object=&candidate;break;}
    if(object&&(!object->visible||!object->autoManipulate||!object->grabbable||!hand.valid)){frameInput.addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;}
    if(object){
      auto state=pointerStates.find(object->id);
      const bool rawDown=frameButtonDown(h,"/input/trigger/click");
      if((state==pointerStates.end()||state->second.hand!=h||(!state->second.down&&!rawDown))){frameInput.addOverlayEvent("overlay.grabEnd",previousGrab,h);grab={};object=nullptr;}
    }
    if(!object&&hand.valid){
      for(auto &candidate:scene.objects){
        if(!candidate.visible||!candidate.autoManipulate||!candidate.grabbable||ownedByOther(candidate.id))continue;
        auto pointer=pointerStates.find(candidate.id);
        if(pointer!=pointerStates.end()&&pointer->second.down&&pointer->second.hand==h){
          const float dx=candidate.x-hand.x,dy=candidate.y-hand.y,dz=candidate.z-hand.z;
          const float reach=.10f+candidate.scale*.5f;
          const bool direct=candidate.directGrab&&dx*dx+dy*dy+dz*dz<reach*reach;
          beginGrab(grab,candidate,hand,direct);frameInput.addOverlayEvent("overlay.grabStart",candidate.id,h);object=&candidate;break;
        }
      }
    }
    if(object&&hand.valid){
      const float oldX=object->x,oldY=object->y,oldZ=object->z,oldPitch=object->pitch,oldYaw=object->yaw,oldRoll=object->roll;
      auto pointer=pointerStates.find(object->id);
      const float x=pointer!=pointerStates.end()?std::clamp(pointer->second.scrollX,-1.f,1.f):0;
      const float y=pointer!=pointerStates.end()?std::clamp(pointer->second.scrollY,-1.f,1.f):0;
      if(!grab.direct&&(x||y)){
        if(x){
          if(pointer!=pointerStates.end()&&pointer->second.secondaryDown&&object->laserPitchWithStickClick)object->pitch+=x*6.f*dt;
          else if(object->laserYaw)object->yaw+=x*6.f*dt;
        }
        if(y&&object->depthAdjust){
          const float length=std::sqrt(grab.offset[0]*grab.offset[0]+grab.offset[1]*grab.offset[1]+grab.offset[2]*grab.offset[2]);
          if(length>.001f){const float next=std::max(.06f,length+y*3.2f*dt);for(float &v:grab.offset)v*=next/length;}
        }
      }
      const auto &r=hand.rotation;
      object->x=hand.x+r[0]*grab.offset[0]+r[1]*grab.offset[1]+r[2]*grab.offset[2];
      object->y=hand.y+r[3]*grab.offset[0]+r[4]*grab.offset[1]+r[5]*grab.offset[2];
      object->z=hand.z+r[6]*grab.offset[0]+r[7]*grab.offset[1]+r[8]*grab.offset[2];
      frameInput.addOverlayEvent("overlay.grabMove",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
      if(std::fabs(object->x-oldX)+std::fabs(object->y-oldY)+std::fabs(object->z-oldZ)>.00001f)frameInput.addOverlayEvent("overlay.move",object->id,h,object->x-oldX,object->y-oldY,object->z-oldZ);
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
      if(std::fabs(object->pitch-oldPitch)+std::fabs(object->yaw-oldYaw)+std::fabs(object->roll-oldRoll)>.00001f)frameInput.addOverlayEvent("overlay.rotate",object->id,h,0,0,0,object->pitch-oldPitch,object->yaw-oldYaw,object->roll-oldRoll);
    }
  }
}
static bool rayTriangle(const float o[3],const float d[3],const float a[3],const float b[3],const float c[3],float &t){float e1[3],e2[3],p[3],q[3],s[3];for(int i=0;i<3;i++){e1[i]=b[i]-a[i];e2[i]=c[i]-a[i];p[i]=d[(i+1)%3]*e2[(i+2)%3]-d[(i+2)%3]*e2[(i+1)%3];}float det=e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2];if(std::fabs(det)<1e-6f)return false;float inv=1.f/det;for(int i=0;i<3;i++)s[i]=o[i]-a[i];float u=(s[0]*p[0]+s[1]*p[1]+s[2]*p[2])*inv;if(u<0||u>1)return false;for(int i=0;i<3;i++)q[i]=s[(i+1)%3]*e1[(i+2)%3]-s[(i+2)%3]*e1[(i+1)%3];float v=(d[0]*q[0]+d[1]*q[1]+d[2]*q[2])*inv;if(v<0||u+v>1)return false;t=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])*inv;return t>=0;}
static bool meshHit(const Object &object,const ModelAsset &asset,const ControllerSnapshot &hand){float o[3]={hand.x,hand.y,hand.z},d[3]={hand.rotation[2],hand.rotation[5],hand.rotation[8]};float best=std::numeric_limits<float>::max();float cp=std::cos(object.pitch),sp=std::sin(object.pitch),cy=std::cos(object.yaw),sy=std::sin(object.yaw),cr=std::cos(object.roll),sr=std::sin(object.roll);for(const auto &part:asset.primitives)for(size_t i=0;i+14<part.vertices.size();i+=15){float tri[3][3];for(int n=0;n<3;n++){float x=part.vertices[i+n*5]*object.scale,y=part.vertices[i+n*5+1]*object.scale,z=part.vertices[i+n*5+2]*object.scale;float rx=cr*x-sr*y,ry=sr*x+cr*y,rz=z,px=rx,py=cp*ry-sp*rz,pz=sp*ry+cp*rz;tri[n][0]=object.x+cy*px+sy*pz;tri[n][1]=object.y+py;tri[n][2]=object.z-sy*px+cy*pz;}float t;if(rayTriangle(o,d,tri[0],tri[1],tri[2],t))best=std::min(best,t);}return best<std::numeric_limits<float>::max();}
int main(int argc,char **argv) {
  if(argc>2&&std::string(argv[1])=="--inspect"){
    ModelAsset asset;return loadModelFile(argv[2],asset)?0:1;
  }
  std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
  std::string scriptPath=argc>1?argv[1]:"scripts/avocado.js";
  ScriptHost script;if(!loadScript(script,scriptPath))return 13;
  EGLDisplay display;EGLContext context;EGLSurface surface;if(!initEgl(display,context,surface))return 1;
  vr::EVRInitError initError;vr::VR_Init(&initError,vr::VRApplication_Overlay);if(initError!=vr::VRInitError_None)return 2;
  if(!controllerInput.init(std::filesystem::current_path().string(),argv[0]))fprintf(stderr,"[input] SteamVR actions unavailable; controller poses and overlay pointer events remain active\n");
  if(!frameInput.start())fprintf(stderr,"[input] raw Steam Frame WebSocket unavailable; action input remains active\n");
  auto *system=vr::VRSystem();auto *overlays=vr::VROverlay();
  int drm=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);auto *gbm=drm>=0?gbm_create_device(drm):nullptr;
  createImage=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  destroyImage=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  imageRenderbuffer=(PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
  if(!gbm||!createImage||!destroyImage||!imageRenderbuffer)return 3;
  const char *vs=R"(#version 300 es
layout(location=0) in vec3 p;layout(location=1) in vec2 uv;
uniform vec3 center,right,up,forward,panelRight,panelUp,panelForward,eye;
uniform vec3 offset;
uniform float yaw,pitch,roll,modelScale,panelHalfWidth;
out vec2 t;
void main(){
  float cr=cos(roll),sr=sin(roll),cp=cos(pitch),sp=sin(pitch),cy=cos(yaw),sy=sin(yaw);
  vec3 rolled=vec3(cr*p.x-sr*p.y,sr*p.x+cr*p.y,p.z);
  vec3 pitched=vec3(rolled.x,cp*rolled.y-sp*rolled.z,sp*rolled.y+cp*rolled.z);
  vec3 rotated=vec3(cy*pitched.x+sy*pitched.z,pitched.y,-sy*pitched.x+cy*pitched.z);
  vec3 world=center+right*offset.x+up*offset.y+forward*offset.z+modelScale*(right*rotated.x+up*rotated.y-forward*rotated.z);
  vec3 ray=world-eye;
  float distanceAlong=dot(ray,panelForward);
  float hitScale=dot(center-eye,panelForward)/distanceAlong;
  vec3 hit=eye+hitScale*ray;
  float x=dot(hit-center,panelRight)/panelHalfWidth;
  float y=dot(hit-center,panelUp)/panelHalfWidth;
  float z=(distanceAlong-0.05)/4.95*2.0-1.0;
  gl_Position=vec4(x*distanceAlong,y*distanceAlong,z*distanceAlong,distanceAlong);
  t=uv;
})";
  const char *fs=R"(#version 300 es
precision mediump float;in vec2 t;uniform sampler2D material;uniform vec4 factor;uniform vec3 sheen;uniform int alphaMode;uniform float alphaCutoff;out vec4 color;
void main(){vec4 texel=texture(material,t);color=texel*factor;color.rgb=clamp(color.rgb+texel.rgb*sheen*0.8,0.0,1.0);if(alphaMode==1&&color.a<alphaCutoff)discard;if(alphaMode==0)color.a=1.0;})";
  GLuint program=glCreateProgram(),v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);
  glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 5;
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)return 7;
  auto anchor=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
  for(int r=0;r<3;r++)anchor.m[r][3]+=anchor.m[r][1]*-.2f+anchor.m[r][2]*-1.f;
  fprintf(stdout,"anchor world=%f,%f,%f\n",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);fflush(stdout);
  vr::VROverlayHandle_t lasers[2]={vr::k_ulOverlayHandleInvalid,vr::k_ulOverlayHandleInvalid};bool laserShown[2]{};
  if(!createLaser(lasers[0],0)||!createLaser(lasers[1],1))return 9;
  std::map<int,std::unique_ptr<Surface>> surfaces;std::map<int,vr::VROverlayHandle_t> panelOverlays;std::map<int,vr::Texture_t> panelTextures;
  std::array<GrabState,2> grabs{};
  auto syncSurfaces=[&]()->bool{
    std::vector<int> desired;
    for(const auto &object:script.scene.objects)if(object.visible)desired.push_back(object.id);
    for(auto it=surfaces.begin();it!=surfaces.end();){
      if(std::find(desired.begin(),desired.end(),it->first)==desired.end()){
        pointerStates.erase(it->first);destroySurface(*it->second,display);it=surfaces.erase(it);
      }else ++it;
    }
    for(int id:desired){
      const Object *object=nullptr;for(const auto &entry:script.scene.objects)if(entry.id==id){object=&entry;break;}
      if(!object)continue;
      const float width=std::max(.55f,object->scale*2.1f);
      const int eyePixels=script.scene.assets[(size_t)object->asset-1].eyePixels;
      const bool interactive=!object->laserPassThrough;
      auto found=surfaces.find(id);
      if(found!=surfaces.end()&&(found->second->eyePixels!=eyePixels||found->second->interactive!=interactive)){
        pointerStates.erase(id);destroySurface(*found->second,display);surfaces.erase(found);found=surfaces.end();
      }
      if(found==surfaces.end()){
        auto created=std::make_unique<Surface>();
        if(!createSurface(*created,id,width,eyePixels,interactive,gbm,display,anchor)){destroySurface(*created,display);return false;}
        surfaces.emplace(id,std::move(created));
      }else if(std::fabs(found->second->width-width)>.0001f){
        found->second->width=width;
        overlays->SetOverlayWidthInMeters(found->second->overlay,width);
      }
    }
    return true;
  };
  if(!syncSurfaces())return 8;
  for(auto &panel:script.scene.panels){if(panel.dirty)panelUpload(panel);auto found=panelOverlays.find(panel.id);if(found==panelOverlays.end()){vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;std::string key="sf_overlay_engine.panel."+std::to_string(panel.id);if(overlays->CreateOverlay(key.c_str(),key.c_str(),&handle)==vr::VROverlayError_None){overlays->SetOverlayWidthInMeters(handle,panel.widthMeters);vr::VRTextureBounds_t panelBounds{0,1,1,0};overlays->SetOverlayTextureBounds(handle,&panelBounds);panelOverlays.emplace(panel.id,handle);}}}
  glUseProgram(program);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
  glUniform1i(glGetUniformLocation(program,"material"),0);glEnable(GL_DEPTH_TEST);
  setVec3(program,"center",anchor.m[0][3],anchor.m[1][3],anchor.m[2][3]);
  setVec3(program,"right",anchor.m[0][0],anchor.m[1][0],anchor.m[2][0]);
  setVec3(program,"up",anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]);
  setVec3(program,"forward",-anchor.m[0][2],-anchor.m[1][2],-anchor.m[2][2]);
  GLint yawLocation=glGetUniformLocation(program,"yaw"),pitchLocation=glGetUniformLocation(program,"pitch"),rollLocation=glGetUniformLocation(program,"roll"),scaleLocation=glGetUniformLocation(program,"modelScale"),offsetLocation=glGetUniformLocation(program,"offset");
  GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff");
  GLint sheenLocation=glGetUniformLocation(program,"sheen");
  auto next=std::chrono::steady_clock::now();auto reportStart=next;int frame=0;
  while(running){
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,.011f,poses,vr::k_unMaxTrackedDeviceCount);
    for(int hand=0;hand<2;hand++){
      auto &snapshot=controllers[hand];snapshot={};
      auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
      auto device=system->GetTrackedDeviceIndexForControllerRole(role);
      if(device==vr::k_unTrackedDeviceIndexInvalid){if(frame%900==0)fprintf(stdout,"[input] hand=%d device=none\n",hand);continue;}
      const auto &pose=poses[device];
      if(frame%900==0){fprintf(stdout,"[input] hand=%d device=%u pose=%d\n",hand,device,pose.bPoseIsValid);fflush(stdout);}
      if(!pose.bPoseIsValid)continue;
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
    if(frame%30==0){
      std::error_code ec;auto modified=std::filesystem::last_write_time(scriptPath,ec);
      if(!ec&&modified!=script.mtime){
        if(loadScript(script,scriptPath)){
          for(auto &[id,surface]:surfaces)destroySurface(*surface,display);
          surfaces.clear();pointerStates.clear();grabs={};
        }else script.mtime=modified;
      }
    }
    if(!syncSurfaces()){running=false;break;}
    for(auto &[id,surface]:surfaces){
      if(id==0)continue;
      vr::VREvent_t event{};
      auto &state=pointerStates[id];
      state.scrollX=state.scrollY=0;
      while(overlays->PollNextOverlayEvent(surface->overlay,&event,sizeof(event))){
        if(event.eventType==vr::VREvent_FocusLeave){state.valid=false;state.down=false;state.secondaryDown=false;if(state.hand>=0&&hoveredObject[state.hand]==id){frameInput.addOverlayEvent("overlay.surfaceHoverEnd",id,state.hand);hoveredObject[state.hand]=0;frameInput.setHoveredObject(state.hand,0);}state.hand=-1;continue;}
        if(event.eventType==vr::VREvent_ScrollSmooth){
          state.scrollX+=event.data.scroll.xdelta;
          state.scrollY+=event.data.scroll.ydelta;
          continue;
        }
        if(event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonDown||event.eventType==vr::VREvent_MouseButtonUp){
          state.valid=true;
          const int eventHand=event.trackedDeviceIndex==vr::k_unTrackedDeviceIndexInvalid?-1:system->GetControllerRoleForTrackedDeviceIndex(event.trackedDeviceIndex)==vr::TrackedControllerRole_RightHand?1:0;
          if(eventHand>=0&&event.eventType==vr::VREvent_MouseMove&&hoveredObject[eventHand]!=id){if(hoveredObject[eventHand])frameInput.addOverlayEvent("overlay.surfaceHoverEnd",hoveredObject[eventHand],eventHand);hoveredObject[eventHand]=id;frameInput.setHoveredObject(eventHand,id);frameInput.addOverlayEvent("overlay.surfaceHoverStart",id,eventHand);}
          frameInput.addOverlayEvent(event.eventType==vr::VREvent_MouseMove?"overlay.surfaceHover":"overlay.pointer",id,eventHand);
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
    controllerInput.poll();
    frameInput.poll();
    for(int h=0;h<2;h++){int hit=0;for(const auto &object:script.scene.objects){if(!object.visible)continue;const auto &asset=script.scene.assets[(size_t)object.asset-1];if(meshHit(object,asset,controllers[h])){hit=object.id;break;}}if(hit!=meshHoveredObject[h]){if(meshHoveredObject[h])frameInput.addOverlayEvent("overlay.meshHoverEnd",meshHoveredObject[h],h);if(hit)frameInput.addOverlayEvent("overlay.meshHoverStart",hit,h);meshHoveredObject[h]=hit;}}
    if(!frameInput.started() && frame%900==0)frameInput.reconnect();
    if(inputCallbackCtx){auto events=frameInput.takeCallbackEvents();for(const auto &event:events){JSValue value=inputEventValue(inputCallbackCtx,event);JSValue result=JS_Call(inputCallbackCtx,inputCallback,JS_UNDEFINED,1,&value);JS_FreeValue(inputCallbackCtx,value);if(JS_IsException(result))JS_FreeValue(inputCallbackCtx,JS_GetException(inputCallbackCtx));else JS_FreeValue(inputCallbackCtx,result);}}
    updateManipulation(script.scene,grabs,1.f/90.f);
    if(script.ctx&&JS_IsFunction(script.ctx,script.update)){
      JSValue dt=JS_NewFloat64(script.ctx,1.0/90.0);
      JSValue result=JS_Call(script.ctx,script.update,JS_UNDEFINED,1,&dt);JS_FreeValue(script.ctx,dt);
      if(JS_IsException(result)){JSValue exception=JS_GetException(script.ctx);const char *message=JS_ToCString(script.ctx,exception);fprintf(stderr,"[js] update error: %s\n",message?message:"unknown");if(message)JS_FreeCString(script.ctx,message);JS_FreeValue(script.ctx,exception);JS_FreeValue(script.ctx,script.update);script.update=JS_UNDEFINED;}
      JS_FreeValue(script.ctx,result);
    }
    for(auto &asset:script.scene.assets)if(!asset.uploaded)uploadModel(asset);
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
    if(poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid){
      auto &head=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
      for(int hand=0;hand<2;hand++){
        auto role=hand==0?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand;
        auto device=system->GetTrackedDeviceIndexForControllerRole(role);
        vr::TrackedDevicePose_t pose{};
        if(device!=vr::k_unTrackedDeviceIndexInvalid)pose=poses[device];
        updateLaser(lasers[hand],laserShown[hand],script.scene.laserVisible[hand],hand,pose,head);
      }
      std::vector<Surface*> ready;
      for(auto &panelData:script.scene.panels){auto h=panelOverlays.find(panelData.id);if(h==panelOverlays.end()||!panelData.texture||!panelData.visible)continue;auto &texture=panelTextures[panelData.id];texture={&panelData.texture,vr::TextureType_OpenGL,vr::ColorSpace_Gamma};overlays->SetOverlayTexture(h->second,&texture);auto transform=anchor;transform.m[0][3]+=anchor.m[0][0]*panelData.x+anchor.m[0][1]*panelData.y-anchor.m[0][2]*panelData.z;transform.m[1][3]+=anchor.m[1][0]*panelData.x+anchor.m[1][1]*panelData.y-anchor.m[1][2]*panelData.z;transform.m[2][3]+=anchor.m[2][0]*panelData.x+anchor.m[2][1]*panelData.y-anchor.m[2][2]*panelData.z;if(panelData.mode=="headLocked"){transform=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;transform.m[0][3]+=transform.m[0][0]*panelData.x+transform.m[0][1]*panelData.y-transform.m[0][2]*panelData.z;transform.m[1][3]+=transform.m[1][0]*panelData.x+transform.m[1][1]*panelData.y-transform.m[1][2]*panelData.z;transform.m[2][3]+=transform.m[2][0]*panelData.x+transform.m[2][1]*panelData.y-transform.m[2][2]*panelData.z;}if(panelData.mode=="viewerFacing"){float pr[3],pu[3],pf[3];faceViewer(transform,anchor,head,pr,pu,pf);for(int i=0;i<3;i++){transform.m[i][0]=pr[i];transform.m[i][1]=pu[i];transform.m[i][2]=-pf[i];}}overlays->SetOverlayTransformAbsolute(h->second,vr::TrackingUniverseStanding,&transform);overlays->ShowOverlay(h->second);}
      for(auto &[id,surface]:surfaces){
        const Object *target=nullptr;
        for(const auto &object:script.scene.objects)if(object.id==id){target=&object;break;}
        if(!target)continue;
        float surfaceX=target->x,surfaceY=target->y,surfaceZ=target->z;
        auto panel=anchor;
        for(int r=0;r<3;r++)panel.m[r][3]+=anchor.m[r][0]*surfaceX+anchor.m[r][1]*surfaceY-anchor.m[r][2]*surfaceZ;
        float distanceSquared=0,facing=0;
        for(int r=0;r<3;r++){float delta=panel.m[r][3]-head.m[r][3];distanceSquared+=delta*delta;facing-=delta*head.m[r][2];}
        if(facing<.25f*std::sqrt(distanceSquared)-target->scale*.6f){
          if(surface->shown){overlays->HideOverlay(surface->overlay);surface->shown=false;}
          continue;
        }
        float panelRight[3],panelUp[3],panelForward[3];
        faceViewer(panel,anchor,head,panelRight,panelUp,panelForward);
        overlays->SetOverlayTransformAbsolute(surface->overlay,vr::TrackingUniverseStanding,&panel);
        setVec3(program,"center",panel.m[0][3],panel.m[1][3],panel.m[2][3]);
        setVec3(program,"panelRight",panelRight[0],panelRight[1],panelRight[2]);
        setVec3(program,"panelUp",panelUp[0],panelUp[1],panelUp[2]);
        setVec3(program,"panelForward",panelForward[0],panelForward[1],panelForward[2]);
        glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),surface->width*.5f);
        Buffer &buffer=surface->buffers[frame%3];
        glBindFramebuffer(GL_FRAMEBUFFER,buffer.fbo);glViewport(0,0,2*surface->eyePixels,surface->eyePixels);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        for(int i=0;i<2;i++){
          auto eyeTransform=system->GetEyeToHeadTransform((vr::EVREye)i);
          float eye[3];for(int r=0;r<3;r++)eye[r]=head.m[r][3]+head.m[r][0]*eyeTransform.m[0][3]+head.m[r][1]*eyeTransform.m[1][3]+head.m[r][2]*eyeTransform.m[2][3];
          setVec3(program,"eye",eye[0],eye[1],eye[2]);glViewport(i*surface->eyePixels,0,surface->eyePixels,surface->eyePixels);
          for(int pass=0;pass<2;pass++){
            if(pass){glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}
            else {glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
            for(const auto &object:script.scene.objects){
              if(!object.visible||id!=object.id)continue;
              const auto &asset=script.scene.assets[(size_t)object.asset-1];
              glUniform1f(yawLocation,object.yaw);glUniform1f(pitchLocation,object.pitch);glUniform1f(rollLocation,object.roll);glUniform1f(scaleLocation,object.scale);
              glUniform3f(offsetLocation,object.x-surfaceX,object.y-surfaceY,object.z-surfaceZ);
              for(const auto &part:asset.primitives){
                if((part.alphaMode==2)!=bool(pass))continue;
                glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);
                glUniform3fv(sheenLocation,1,part.sheen);
                glBindBuffer(GL_ARRAY_BUFFER,part.vbo);
                glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
                glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
                glDrawArrays(GL_TRIANGLES,0,part.vertexCount);
              }
            }
          }
          glDepthMask(GL_TRUE);glDisable(GL_BLEND);
        }
        ready.push_back(surface.get());
      }
      glFinish();glBindFramebuffer(GL_FRAMEBUFFER,0);
      for(auto *surface:ready){
        auto &buffer=surface->buffers[frame%3];
        vr::Texture_t texture{&buffer.handle,vr::TextureType_SharedTextureHandle,vr::ColorSpace_Gamma};
        auto error=overlays->SetOverlayTexture(surface->overlay,&texture);
        if(error!=vr::VROverlayError_None)fprintf(stderr,"[surface] submit id=%d error=%d\n",surface->objectId,(int)error);
        if(!surface->shown){overlays->ShowOverlay(surface->overlay);surface->shown=true;}
      }
      ++frame;
      if(frame%900==0){auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-reportStart).count();reportStart=now;fprintf(stdout,"frames=%d fps=%.1f surfaces=%zu ready=%zu gl_error=%x\n",frame,900.0/seconds,surfaces.size(),ready.size(),(unsigned)glGetError());fflush(stdout);}
    }
    next+=std::chrono::microseconds(11111);std::this_thread::sleep_until(next);
  }
  fprintf(stdout,"completed frames=%d\n",frame);
  for(auto laser:lasers)if(laser!=vr::k_ulOverlayHandleInvalid)overlays->DestroyOverlay(laser);
  for(auto &[id,surface]:surfaces)destroySurface(*surface,display);
  closeScript(script);gbm_device_destroy(gbm);close(drm);vr::VR_Shutdown();
}
