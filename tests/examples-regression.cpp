// Exercise the published examples against real bindings without a VR session.
#include "../src/engine_state.h"
#include "../src/native_resource_key.h"
#include "../src/script_registry.h"
#include "../src/input_arbitration.h"
#include "../src/panel_pointer.h"
#include <cassert>
#include <cstdlib>
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)

int main() {
  const auto state=std::filesystem::temp_directory_path()/("sf-example-state-"+std::to_string(getpid()));
  scenePersistence::directory=state.string();
  ScopeExit removeState{[&]{std::filesystem::remove_all(state);}};
  calibrationOutput = "/unused-example-calibration.json";
  std::vector<std::filesystem::directory_entry> scenes;
  for(const auto &entry:std::filesystem::directory_iterator("scripts"))scenes.push_back(entry);
  scenes.emplace_back("tests/scenes/overlay-comparison.js");
  scenes.emplace_back("tests/scenes/auto-split.js");
  scenes.emplace_back("tests/scenes/fast-motion.js");
  scenes.emplace_back("tests/scenes/light-filtering.js");
  scenes.emplace_back("tests/scenes/pbr-variants.js");
  // Demos run twice: the second run checks the state the first one saved.
  std::vector<std::filesystem::directory_entry> demos;
  for(const auto &entry:std::filesystem::directory_iterator("demos"))demos.push_back(entry);
  std::sort(demos.begin(),demos.end());
  for(int pass=0;pass<2;pass++)scenes.insert(scenes.end(),demos.begin(),demos.end());
  std::map<std::string,int> runs;
  for (const auto &entry : scenes) {
    if (entry.path().extension() != ".js") continue;
    ScriptHost host;
    assert(loadScript(host, entry.path().string()));
    auto checkedCall = [&](JSValue function, int argc, JSValue *args) {
      JSValue result = JS_Call(host.ctx, function, JS_UNDEFINED, argc, args);
      if (JS_IsException(result)) {
        JSValue error = JS_GetException(host.ctx);
        const char *message = JS_ToCString(host.ctx, error);
        fprintf(stderr, "%s: %s\n", entry.path().c_str(), message ? message : "exception");
        assert(false);
      }
      JS_FreeValue(host.ctx, result);
    };
    if (JS_IsFunction(host.ctx, host.update)) {
      JSValue dt = JS_NewFloat64(host.ctx, 1.0 / 90);
      checkedCall(host.update, 1, &dt);
      JS_FreeValue(host.ctx, dt);
    }
    // Exercise lifecycle controls before the general button smoke check disposes owners.
    std::string controls;
    const auto name = entry.path().filename();
    const int run = runs[entry.path().string()]++;
    if(name=="shadows.js") controls=R"(
      inputEventForExample('/input/b/click');const oldTool=tool,oldLight=light;
      inputEventForExample('/input/b/click');
      if(tool===oldTool||light===oldLight||!getSceneContainerState(oldTool).disposed||!getSpotLight(light).shadow.enabled)throw Error('shadow restore');
      inputEventForExample('/input/a/click');if(getSpotLight(light).shadow.enabled)throw Error('shadow disable');
      inputEventForExample('/input/a/click');inputEventForExample('/input/bumper/click');
      if(!getSpotLight(light).shadow.enabled||getSpotLight(light).shadow.resolution!==1024)throw Error('shadow resolution');
    )";
    if(name=="point-shadows.js") controls=R"(
      inputEventForExample('/input/b/click');const oldTool=tool,oldLight=light;
      inputEventForExample('/input/b/click');
      if(tool===oldTool||light===oldLight||!getSceneContainerState(oldTool).disposed||!getPointLight(light).shadow.enabled)throw Error('shadow restore');
      inputEventForExample('/input/a/click');if(getPointLight(light).shadow.enabled)throw Error('shadow disable');
      inputEventForExample('/input/a/click');inputEventForExample('/input/bumper/click');
      if(!getPointLight(light).shadow.enabled||getPointLight(light).shadow.resolution!==512)throw Error('shadow resolution');
    )";
    if(name=="directional-shadows.js") controls=R"(
      inputEventForExample('/input/b/click');const oldTool=tool,oldLight=light;
      inputEventForExample('/input/b/click');
      if(tool===oldTool||light===oldLight||!getSceneContainerState(oldTool).disposed||!getDirectionalLight(light).shadow.enabled)throw Error('shadow restore');
      inputEventForExample('/input/a/click');if(getDirectionalLight(light).shadow.enabled)throw Error('shadow disable');
      inputEventForExample('/input/a/click');inputEventForExample('/input/bumper/click');
      if(!getDirectionalLight(light).shadow.enabled||getDirectionalLight(light).shadow.resolution!==1024)throw Error('shadow resolution');
    )";
    if(name=="sheen.js") controls=R"(
      environment=true;lighting();
      enabled=false;lighting();
      environment=false;enabled=true;lighting();
    )";
    if(name=="containers.js") controls=R"(
      toggleTool();
      if(tool!==0||getEngineStats().objectCount!==0)throw Error('tool disposal');
      toggleTool();
      if(getSceneContainerState(tool).objectIds.length!==4||!getSceneContainerState(tool).inputCallback)throw Error('tool recreation');
    )";
    if(name=="nested-containers.js") controls=R"(
      inputEventForExample('/input/b/click');const previousTool=tool,previousChild=module;
      inputEventForExample('/input/b/click');
      if(tool===previousTool||module===previousChild||getSceneContainerState(module).parentId!==tool)throw Error('nested restore');
      inputEventForExample('/input/a/click');if(getSceneContainerState(module).parentId!==0)throw Error('nested detach');
      inputEventForExample('/input/bumper/click');if(tool||getSceneContainerState(module).disposed)throw Error('detached child survived');
      inputEventForExample('/input/bumper/click');
    )";
    if(name=="asset-lifetime.js") controls=R"(
      inputEventForExample('/input/b/click');const oldAsset=asset;
      inputEventForExample('/input/a/click');if(asset!==oldAsset)throw Error('live asset guard');
      inputEventForExample('/input/bumper/click');inputEventForExample('/input/a/click');
      if(asset)throw Error('asset unload');inputEventForExample('/input/b/click');
      if(!tool||asset===oldAsset)throw Error('restore unloaded asset');
    )";
    if(name=="directional-lights.js") controls=R"(
      inputEventForExample('/input/b/click');const previousSun=light;
      inputEventForExample('/input/b/click');
      if(light===previousSun||getDirectionalLight(light).objectId===handle)throw Error('directional restore');
    )";
    if(name=="spotlights.js") controls=R"(
      inputEventForExample('/input/b/click');const previousSpot=light;
      inputEventForExample('/input/b/click');
      if(light===previousSpot||getSpotLight(light).objectId===handle)throw Error('flashlight restore');
    )";
    if(name=="lights.js") controls=R"(
      inputEventForExample('/input/b/click');
      const oldLight=light;
      inputEventForExample('/input/b/click');
      if(light===oldLight||getPointLight(light).objectId!==marker)throw Error('light tool restore');
    )";
    if(name=="scene-snapshot.js") controls=R"(
      for(let n=0;n<3;n++){
        inputEventForExample('/input/bumper/click');
        const previous=box;
        inputEventForExample('/input/a/click');
        if(box===previous||getSceneContainerState(tool).disposed)throw Error('snapshot fresh handles');
      }
    )";
    if(name=="container-transfer.js") controls=R"(
      inputEventForExample('/input/b/click');
      if(side!==1||getSceneContainerState(owners[1]).objectIds.length!==1)throw Error('ownership transfer');
      inputEventForExample('/input/a/click');
      if(getSceneContainerState(owners[1]).disposed)throw Error('destination survived');
    )";
    if(name=="physics-controls.js") controls=R"(
      inputEventForExample('/input/b/click');
      if(getObjectPhysics(ball)!==null)throw Error('body removal');
      inputEventForExample('/input/b/click');
      if(!getObjectPhysics(ball))throw Error('body recreation');
    )";
    if(name=="panel-placement.js") controls=R"(
      // The label survives its box and can follow a new one.
      removeObject(box);panelSetTransform(label,'object',0,.15,0,{objectId:createObject(loadModel('assets/samples/Box/Box.glb'),0,0,0,.1),viewerFacing:true});
      inputEventForExample('/input/b/click');
      if(head!==null)throw Error('panel removal');
      inputEventForExample('/input/b/click');
      if(!head)throw Error('panel recreation');
    )";
    if(name=="scene-save.js") controls=R"(
      const before=saves;inputEventForExample('/input/bumper/click');
      if(saves!==before+1||loadScriptData('stats').saves!==saves)throw Error('saved data count');
    )";
    if(entry.path()=="demos/clock.js"&&run==0) controls=R"(
      const press=(label,hand='right')=>{const b=buttons.find(b=>(typeof b.label==='function'?b.label():b.label)===label);
        exampleCallback({type:'panel.pointerDown',objectId:panel.id,control:'left',hand,x:b.x+b.w/2,y:b.y+b.h/2});};
      if(duration!==300||hour12||remaining!==300)throw Error('clock defaults');
      exampleCallback({type:'panel.hoverStart',objectId:panel.id,hand:'left',x:30,y:300});
      if(!getLaserVisible('left')||hovered!==0)throw Error('clock hover laser');
      exampleCallback({type:'panel.hoverEnd',objectId:panel.id,hand:'left',x:0,y:0});
      if(getLaserVisible('left')||hovered!==-1)throw Error('clock laser hidden');
      press('25');if(duration!==1500||remaining!==1500||loadScriptData('clock').duration!==1500)throw Error('clock duration');
      press('Start');if(!running)throw Error('clock start');
      endsAt=Date.now()+61500;press('Pause');
      if(running||Math.abs(remaining-61.5)>1||timerText(remaining)!=='01:02')throw Error('clock pause');
      press('Reset');if(running||remaining!==1500)throw Error('clock reset');
      press('Start');endsAt=Date.now()-1;update();
      if(running||remaining!==0||alarmUntil<=Date.now()||timerText(left(Date.now()))!=='00:00')throw Error('clock alarm');
      press('1');if(alarmUntil!==0||duration!==1500)throw Error('clock press silences alarm only');
      press('Start');if(!running||Math.abs(left(Date.now())-1500)>1)throw Error('clock restart after alarm');
      press('24h');if(!hour12||!loadScriptData('clock').hour12)throw Error('clock format');
      const at=(h,m)=>new Date(2026,0,1,h,m);
      if(clockText(at(13,5))!=='1:05 PM'||clockText(at(0,0))!=='12:00 AM'||clockText(at(12,30))!=='12:30 PM')throw Error('clock 12h');
      hour12=false;if(clockText(at(9,7))!=='09:07')throw Error('clock 24h');hour12=true;
      exampleCallback({type:'overlay.grabStart',objectId:handle,hand:'right'});
      if(!getLaserVisible('right'))throw Error('clock grab laser');
      setObjectPosition(handle,.1,.2,-.5);
      exampleCallback({type:'overlay.grabEnd',objectId:handle,hand:'right'});
      const kept=loadScriptData('clock');
      if(getLaserVisible('right')||Math.abs(kept.x-.1)>1e-6||Math.abs(kept.y-.2)>1e-6||Math.abs(kept.z+.5)>1e-6)throw Error('clock grab save');
    )";
    if(entry.path()=="demos/clock.js"&&run==1) controls=R"(
      const at=getObjectTransform(handle);
      if(Math.abs(at.x-.1)>1e-6||Math.abs(at.y-.2)>1e-6||Math.abs(at.z+.5)>1e-6||duration!==1500||remaining!==1500||!hour12||running)throw Error('clock restore');
    )";
    if(entry.path()=="demos/monitor.js"&&run==0) controls=R"(
      if(rows.some(row=>row.values.length))throw Error('monitor recorded before the first sample');
      for(let i=0;i<70;i++)record({fps:80+i%10,cpuPercent:i,ramBytes:(400+i)*MiB,vramBytes:i%3?null:1536*MiB,gpuAllocatedEstimateBytes:NaN});
      if(rows.some(row=>row.values.length!==60)||rows[0].values[0]!==80||rows[1].values.at(-1)!==69)throw Error('monitor history');
      if(rows[3].values.filter(v=>v===null).length!==40||rows[4].values.some(v=>v!==null))throw Error('monitor gaps');
      draw();
      if(bytes(512*MiB)!=='512 MiB'||bytes(1536*MiB)!=='1.50 GiB'||rows[1].format(42.4)!=='42%')throw Error('monitor format');
      exampleCallback({type:'overlay.meshHoverStart',objectId:handle,hand:'left'});
      if(!getLaserVisible('left'))throw Error('monitor hover laser');
      exampleCallback({type:'overlay.meshHoverEnd',objectId:handle,hand:'left'});
      if(getLaserVisible('left'))throw Error('monitor laser hidden');
      setObjectPosition(handle,-.2,.3,-.6);
      exampleCallback({type:'overlay.grabEnd',objectId:handle,hand:'right'});
      const kept=loadScriptData('monitor');
      if(Math.abs(kept.x+.2)>1e-6||Math.abs(kept.y-.3)>1e-6||Math.abs(kept.z+.6)>1e-6)throw Error('monitor grab save');
    )";
    if(entry.path()=="demos/monitor.js"&&run==1) controls=R"(
      const at=getObjectTransform(handle);
      if(Math.abs(at.x+.2)>1e-6||Math.abs(at.y-.3)>1e-6||Math.abs(at.z+.6)>1e-6)throw Error('monitor restore');
    )";
    if(entry.path()=="demos/sketch.js"&&run==0) controls=R"(
      const ev=(type,hand,x,y)=>exampleCallback({type,objectId:panel.id,control:'left',hand,x,y});
      const tap=(kind,index)=>{const t=tools.find(t=>t.kind===kind&&(index===undefined||t.index===index));ev('panel.pointerDown','right',t.x+5,t.y+5);ev('panel.pointerUp','right',t.x+5,t.y+5);};
      const saved=()=>loadScriptData('sketch').strokes;
      if(history.length||color!==0||size!==1||full)throw Error('sketch defaults');
      ev('panel.hoverStart','left',500,400);if(!getLaserVisible('left'))throw Error('sketch laser');
      ev('panel.hoverEnd','left',500,400);if(getLaserVisible('left'))throw Error('sketch laser hidden');
      tap('color',2);tap('size',2);if(color!==2||size!==2||history.length)throw Error('sketch tools');
      ev('panel.pointerDown','right',200,300);ev('panel.hover','right',210,300);ev('panel.hover','right',211,301);
      ev('panel.pointerDown','left',600,400);ev('panel.hover','left',620,20);
      ev('panel.hover','right',250,320);ev('panel.pointerUp','right',250,320);
      if(history.length!==1||JSON.stringify(saved())!=='[{"c":2,"s":2,"p":[200,300,210,300,250,320]}]')throw Error('sketch stroke');
      ev('panel.pointerUp','left',620,20);
      if(history.length!==2||JSON.stringify(saved()[1].p)!=='[600,400,620,110]')throw Error('sketch second hand and clamp');
      tap('undo');if(history.length!==1||saved().length!==1)throw Error('sketch undo');
      tap('clear');if(visible().length||saved().length)throw Error('sketch clear');
      tap('undo');if(visible().length!==1||saved().length!==1)throw Error('sketch undo clear');
      tap('size',0);ev('panel.pointerDown','right',100,200);
      for(let i=0;!full&&i<20000;i++)ev('panel.hover','right',100+(i%2)*800,200+(i%400));
      ev('panel.pointerUp','right',0,0);
      const bytes=JSON.stringify({strokes:saved()}).length;
      if(!full||bytes>65536||bytes<60000||history.length!==2)throw Error('sketch full '+bytes);
      ev('panel.pointerDown','right',300,300);ev('panel.pointerUp','right',300,300);
      if(history.length!==2)throw Error('sketch accepted a stroke while full');
      tap('clear');if(full)throw Error('sketch clear frees space');
      tap('color',4);ev('panel.pointerDown','right',300,300);ev('panel.hover','right',320,330);ev('panel.pointerUp','right',320,330);
      if(saved().length!==1)throw Error('sketch final stroke');
    )";
    if(entry.path()=="demos/sketch.js"&&run==1) controls=R"(
      if(history.length!==1||JSON.stringify(visible())!=='[{"c":4,"s":0,"p":[300,300,320,330]}]'||full)throw Error('sketch restore');
    )";
    if(entry.path()=="demos/notes.js"&&run==0) controls=R"(
      const take=color=>exampleCallback({type:'panel.pointerDown',objectId:dispenser.id,control:'left',hand:'right',x:16+color*124+50,y:60});
      const click=note=>exampleCallback({type:'panel.pointerDown',objectId:note.panel.id,control:'left',hand:'right',x:128,y:128});
      const drop=(note,x,y,z)=>{setObjectPosition(note.pin,x,y,z);exampleCallback({type:'overlay.grabEnd',objectId:note.pin,hand:'right'});};
      if(notes.length||getSceneContainerState(container).objectIds.length)throw Error('notes defaults');
      take(2);take(0);exampleCallback({type:'panel.pointerDown',objectId:dispenser.id,control:'left',hand:'right',x:130,y:140});
      if(notes.length!==2||notes[0].color!==2||notes[1].color!==0||notes[1].number!==2)throw Error('notes spawn');
      if(getSceneContainerState(container).panelIds.length!==2||loadScriptData('notes').notes.length!==2)throw Error('notes container save');
      for(let i=0;i<4;i++)click(notes[1]);if(labelText(notes[1])!=='#2')throw Error('notes number label');
      click(notes[1]);if(labelText(notes[1])!=='To do'||loadScriptData('notes').notes[1].label!==0)throw Error('notes label cycle');
      exampleCallback({type:'panel.hoverStart',objectId:notes[0].panel.id,hand:'left',x:5,y:5});
      if(!getLaserVisible('left'))throw Error('notes laser');
      const b=getObjectTransform(bin);drop(notes[0],b.x+.02,b.y,b.z-.03);
      if(notes.length!==1||getLaserVisible('left')||getSceneContainerState(container).panelIds.length!==1)throw Error('notes bin');
      for(let i=0;i<40;i++)take(i%4);
      if(notes.length!==32||getSceneContainerState(container).panelIds.length!==32)throw Error('notes cap');
      while(notes.length>3)drop(notes[notes.length-1],b.x,b.y,b.z);
      notes.forEach((note,i)=>{drop(note,i*.1,.2,-.4);for(let n=0;n<i;n++)click(note);});
      setObjectPosition(handle,-.3,-.2,-.5);exampleCallback({type:'overlay.grabEnd',objectId:handle,hand:'left'});
      setObjectPosition(bin,.3,-.2,-.5);exampleCallback({type:'overlay.grabEnd',objectId:bin,hand:'left'});
      if(notes.length!==3||loadScriptData('notes').notes.length!==3||Math.abs(loadScriptData('notes').bin.x-.3)>1e-6)throw Error('notes layout save');
    )";
    if(entry.path()=="demos/notes.js"&&run==1) controls=R"(
      const near=(id,x,y,z)=>{const a=getObjectTransform(id);return Math.abs(a.x-x)<1e-6&&Math.abs(a.y-y)<1e-6&&Math.abs(a.z-z)<1e-6;};
      if(notes.length!==3||getSceneContainerState(container).panelIds.length!==3)throw Error('notes restore count');
      notes.forEach((note,i)=>{
        if(!near(note.pin,i*.1,.2,-.4)||note.label!==i||!note.panel||!getSceneContainerState(container).objectIds.includes(note.pin))throw Error('notes restore '+i);
      });
      if(notes.map(n=>n.color).join()!=='0,0,1'||notes.map(n=>n.number).join()!=='2,3,4'||next!==34)throw Error('notes restore state');
      if(!near(handle,-.3,-.2,-.5)||!near(bin,.3,-.2,-.5))throw Error('notes restore layout');
      // Restored notes keep working: cycle a label and bin one.
      exampleCallback({type:'panel.pointerDown',objectId:notes[0].panel.id,control:'left',hand:'right',x:1,y:1});
      if(notes[0].label!==1)throw Error('notes restored click');
      setObjectPosition(notes[2].pin,.3,-.2,-.5);exampleCallback({type:'overlay.grabEnd',objectId:notes[2].pin,hand:'right'});
      if(notes.length!==2||loadScriptData('notes').notes.length!==2)throw Error('notes restored bin');
    )";
    if(entry.path()=="demos/basketball.js"&&run==0) controls=R"(
      const [a,b,c]=balls;
      const hit=(ball,other)=>exampleCallback({type:'physics.collisionStart',objectId:Math.min(ball.id,other),otherObjectId:Math.max(ball.id,other)});
      const shoot=(ball,speed=4)=>{exampleCallback({type:'overlay.grabStart',objectId:ball.id,hand:'right'});setObjectVelocity(ball.id,0,speed,speed);exampleCallback({type:'overlay.grabEnd',objectId:ball.id,hand:'right'});};
      const drop=ball=>{setObjectPosition(ball.id,RING.x,RING.y+.2,RING.z);update();setObjectPosition(ball.id,RING.x+.03,RING.y-.05,RING.z-.02);update();};
      if(floor!==-1.4||baskets||shots||streak||best||balls.length!==3)throw Error('basketball defaults');
      exampleCallback({type:'overlay.meshHoverStart',objectId:a.id,hand:'left'});
      if(!getLaserVisible('left'))throw Error('basketball laser');
      exampleCallback({type:'overlay.meshHoverEnd',objectId:a.id,hand:'left'});
      if(getLaserVisible('left'))throw Error('basketball laser hidden');
      shoot(a,0);if(shots||a.shot)throw Error('basketball gentle release is not a shot');
      shoot(a);drop(a);
      if(shots!==1||baskets!==1||streak!==1||status!=='Swish!'||loadScriptData('basketball').best!==1)throw Error('basketball swish');
      drop(a);if(baskets!==1)throw Error('basketball scored twice for one shot');
      shoot(b);hit(b,rim);drop(b);if(streak!==2||status!=='Basket!')throw Error('basketball rim');
      shoot(c);hit(c,board);drop(c);if(streak!==3||status!=='Bank shot!'||best!==3)throw Error('basketball bank');
      // Passing up through the ring, or beside it, is not a basket.
      shoot(a);setObjectPosition(a.id,RING.x,RING.y-.2,RING.z);update();setObjectPosition(a.id,RING.x,RING.y+.1,RING.z);update();
      setObjectPosition(a.id,RING.x+.3,RING.y-.1,RING.z);update();if(baskets!==3)throw Error('basketball upward or outside');
      hit(a,rim);hit(a,ground);if(streak!==0||status!=='So close'||a.shot||shots!==4)throw Error('basketball miss');
      shoot(b);exampleCallback({type:'overlay.grabStart',objectId:b.id,hand:'left'});if(streak!==0||b.shot||status!=='Missed')throw Error('basketball regrab');
      if(loadScriptData('basketball').best!==3)throw Error('basketball best saved');
      setObjectPosition(a.id,0,-5,0);update();
      const home=slot(a.slot),at=getObjectTransform(a.id);
      if(Math.abs(at.x-home.x)>1e-6||Math.abs(at.y-home.y)>1e-6)throw Error('basketball out of bounds');
      setObjectPosition(c.id,.5,-1,1);inputEventForExample('/input/x/click');
      const back=getObjectTransform(c.id),cs=slot(c.slot);
      if(Math.abs(back.x-cs.x)>1e-6||Math.abs(back.z-cs.z)>1e-6||getObjectPhysics(c.id).velocity.y!==0)throw Error('basketball return');
    )";
    if(entry.path()=="demos/basketball.js"&&run==1) controls=R"(
      if(best!==3||baskets||shots||streak)throw Error('basketball restore');
    )";
    if(entry.path()=="demos/desk-toys.js"&&run==0) controls=R"(
      if(motor||getPhysicsJoint(spin).motor.mode!=='off'||cradle.length!==5)throw Error('desk toys defaults');
      if(cradle.some(ball=>getPhysicsJoint(ball.joint).type!=='hinge'))throw Error('desk toys hinges');
      exampleCallback({type:'overlay.meshHoverStart',objectId:cradle[0].id,hand:'left'});
      if(!getLaserVisible('left'))throw Error('desk toys laser');
      exampleCallback({type:'overlay.meshHoverEnd',objectId:cradle[0].id,hand:'left'});
      exampleCallback({type:'overlay.meshHoverStart',objectId:desk,hand:'left'});
      if(getLaserVisible('left'))throw Error('desk toys laser on the desk');
      inputEventForExample('/input/a/click');
      const m=getPhysicsJoint(spin).motor;if(!motor||m.mode!=='velocity'||m.targetVelocity!==10)throw Error('desk toys motor');
      setObjectPosition(cradle[0].id,0,0,0);setObjectRotation(spinner,0,1,0);
      inputEventForExample('/input/bumper/click');
      const t=getObjectTransform(cradle[0].id),r=cradle[0].rest;
      if(motor||Math.abs(t.x-r.x)>1e-6||Math.abs(t.y-r.y)>1e-6||Math.abs(getObjectTransform(spinner).yaw)>1e-6)throw Error('desk toys reset');
    )";
    if(entry.path()=="demos/pet-fox.js"&&run==0) controls=R"(
      const near=(a,b,e=1e-6)=>Math.abs(a-b)<e;
      let now=Date.now()+10000;const run=(seconds,frames)=>{for(let i=0;i<frames;i++){now+=seconds*1000;step(seconds,now);}};
      if(mode!=='idle'||getAnimationState(fox).name!=='Survey'||!near(getObjectTransform(fox).y,floor+FEET))throw Error('fox defaults');
      // A far target runs, a near one walks, and arrival returns to the survey.
      target={x:AREA.x,z:AREA.z+1.2};mode='moving';run(.02,1);
      if(clip!=='Run'||getAnimationState(fox).transition===null)throw Error('fox run blend');
      run(.02,60);const facing=getObjectTransform(fox);
      if(!near(wrap(facing.yaw-headingYaw(target.x-facing.x,target.z-facing.z)),0,.05)||mode!=='moving')throw Error('fox facing '+facing.yaw);
      run(.02,200);if(clip!=='Survey'||mode!=='idle'||Math.hypot(getObjectTransform(fox).x-AREA.x,getObjectTransform(fox).z-AREA.z-1.2)>.05)throw Error('fox arrival');
      target={x:AREA.x+.3,z:AREA.z+1.2};mode='moving';run(.02,1);if(clip!=='Walk')throw Error('fox walk');
      // Wandering picks targets inside the area after the rest ends.
      mode='idle';target=null;restUntil=now-1;run(.02,1);
      if(mode!=='moving'||!inArea(target.x,target.z))throw Error('fox wander');
      for(let i=0;i<100;i++){const t=wanderTarget();if(!inArea(t.x,t.z))throw Error('fox wander area');}
      // A gaze held on one floor spot for 0.4 s becomes the target; one outside the area does not.
      const spot={x:-.4,z:.5},origin={x:0,y:.2,z:-1};
      const gazeAt=p=>{const d=[p.x-origin.x,floor-origin.y,p.z-origin.z],l=Math.hypot(...d);
        globalThis.getCalibratedEyeTracking=()=>({valid:true,origin,direction:{x:d[0]/l,y:d[1]/l,z:d[2]/l}});};
      gazeAt(spot);run(.1,3);if(target&&near(target.x,spot.x,.01))throw Error('fox gaze too soon');
      run(.1,2);if(!target||!near(target.x,spot.x,.01)||!near(target.z,spot.z,.01)||mode!=='moving')throw Error('fox gaze target');
      const before=target;gazeAt({x:5,z:5});run(.1,10);if(target!==before&&mode==='moving')throw Error('fox gaze outside');
      globalThis.getCalibratedEyeTracking=()=>({valid:false});
      exampleCallback({type:'overlay.meshHoverStart',objectId:fox,hand:'left'});if(!getLaserVisible('left'))throw Error('fox laser');
      exampleCallback({type:'overlay.meshHoverEnd',objectId:fox,hand:'left'});if(getLaserVisible('left'))throw Error('fox laser hidden');
      // Picked up it surveys; put down outside the area it lands at the edge, level, and drops.
      exampleCallback({type:'overlay.grabStart',objectId:fox,hand:'right'});
      if(mode!=='held'||clip!=='Survey'||!getLaserVisible('right'))throw Error('fox held');
      setObjectPosition(fox,AREA.x+3,floor+1,AREA.z);setObjectRotation(fox,.4,1,.2);run(.02,5);
      if(getObjectTransform(fox).x!==AREA.x+3)throw Error('fox moved while held');
      exampleCallback({type:'overlay.grabEnd',objectId:fox,hand:'right'});
      const put=getObjectTransform(fox);
      if(mode!=='falling'||!near(put.x,AREA.x+AREA.radius,1e-5)||put.pitch!==0||put.roll!==0||!near(put.yaw,1,1e-5))throw Error('fox put down');
      run(.02,60);if(mode!=='idle'||!near(getObjectTransform(fox).y,floor+FEET))throw Error('fox landed');
    )";
    if(name=="grabbing.js") controls=R"(
      setObjectPosition(object,.2,.1,-.3);
      exampleCallback({type:'overlay.grabEnd',objectId:object,hand:'left'});
      if(!releasedAt||Math.abs(releasedAt.x-.2)>1e-5||Math.abs(releasedAt.z+.3)>1e-5)throw Error('grab end transform');
    )";
    if(name=="animation.js") controls=R"(
      inputEventForExample('/input/b/click');
      if(getAnimationState(object).clip!==null)throw Error('animation stop');
      inputEventForExample('/input/a/click');
      pauseAnimation(object);seekAnimation(object,.25);
      if(Math.abs(getAnimationState(object).time-.25)>.0001)throw Error('animation seek');
    )";
    if(!controls.empty()){
      JSValue global=JS_GetGlobalObject(host.ctx);
      JS_SetPropertyStr(host.ctx,global,"exampleCallback",JS_DupValue(host.ctx,inputCallback));
      JS_FreeValue(host.ctx,global);
      JSValue result;
      controls="globalThis.inputEventForExample=control=>exampleCallback({type:'controller.button',down:true,control});\n"+controls;
      result=JS_Eval(host.ctx,controls.c_str(),controls.size(),"example-controls",JS_EVAL_TYPE_GLOBAL);
      if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"%s controls: %s\n",entry.path().c_str(),message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
    }
    if(entry.path()=="demos/basketball.js"&&run==0){
      // Simulated drops: through the ring scores, onto the rim's front deflects,
      // and a ball bounces on the invisible floor at the floor height.
      auto eval=[&](const std::string &source){
        JSValue result=JS_Eval(host.ctx,source.c_str(),source.size(),"basketball-physics",JS_EVAL_TYPE_GLOBAL);
        if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"basketball physics: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
        assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
      };
      auto simulate=[&](int steps){for(int i=0;i<steps;i++){updateScenePhysics(host.scene,1./90);eval("update();rise=Math.max(rise,getObjectPhysics(balls[1].id).velocity.y);low=Math.min(low,getObjectTransform(balls[2].id).y);");}};
      eval("globalThis.rise=-1;globalThis.low=0;{const [a,b,c]=balls;baskets=0;"
           "setObjectPosition(a.id,RING.x,RING.y+.4,RING.z);a.lastY=RING.y+.4;a.shot={touched:new Set()};"
           "setObjectPosition(b.id,RING.x,RING.y+.4,RING.z-.157);b.lastY=RING.y+.4;b.shot={touched:new Set()};"
           "setObjectPosition(c.id,-1,floor+.6,0);for(const ball of balls)setObjectVelocity(ball.id,0,0,0);}");
      simulate(120);
      eval("{const [a,b,c]=balls,tb=getObjectTransform(b.id),tc=getObjectTransform(c.id);"
           "if(baskets!==1||a.shot)throw Error('drop through ring');"
           "if(rise<=0||Math.hypot(tb.x-RING.x,tb.z-(RING.z-.157))<.03)throw Error('rim did not deflect '+rise);"
           "if(Math.abs(low-(floor+BALL_RADIUS))>.02||tc.y<low)throw Error('floor height '+low);}");
      puts("PASS basketball physics: a drop through the ring scores, the rim deflects, the floor holds");
    }
    if(entry.path()=="demos/desk-toys.js"&&run==0){
      // Simulated toys: an end ball swung into the row swings the far ball out while
      // the middle stays nearly still, and
      // the motor spins the spinner; a held toy blocks the reset.
      auto eval=[&](const std::string &source){
        JSValue result=JS_Eval(host.ctx,source.c_str(),source.size(),"desk-toys-physics",JS_EVAL_TYPE_GLOBAL);
        if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"desk toys physics: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
        assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
      };
      eval("globalThis.far=0;setObjectVelocity(cradle[0].id,-.6,0,0);motor=true;drive();");
      for(int i=0;i<180;i++){updateScenePhysics(host.scene,1./90);eval("for(const e of pollInputEvents())exampleCallback(e);update();far=Math.max(far,getObjectTransform(cradle[4].id).x-cradle[4].rest.x);globalThis.ext=globalThis.ext||[0,0,0,0,0];cradle.forEach((b,i)=>ext[i]=Math.max(ext[i],Math.abs(getObjectTransform(b.id).x-b.rest.x)));");}
      eval("if(far<.025||ext.slice(1,4).some(v=>v>.012))throw Error('cradle hand-off '+ext.map(v=>v.toFixed(4)));"
           "if(cradle.some(b=>Math.abs(getPhysicsJoint(b.joint).angle)>1.31))throw Error('cradle limits');"
           "const w=getObjectPhysics(spinner).angularVelocity;if(Math.hypot(w.x,w.y,w.z)<8)throw Error('spinner motor '+JSON.stringify(w));");
      int ball=0;{JSValue g=JS_GetGlobalObject(host.ctx);JSValue v=JS_Eval(host.ctx,"cradle[2].id",12,"id",JS_EVAL_TYPE_GLOBAL);JS_ToInt32(host.ctx,&ball,v);JS_FreeValue(host.ctx,v);JS_FreeValue(host.ctx,g);}
      host.scene.physics->hold(ball);
      eval("if(reset())throw Error('desk toys reset while held');");
      host.scene.physics->release(ball,false);
      eval("if(!reset())throw Error('desk toys reset after release');");
      puts("PASS desk toys physics: the cradle hands the swing to the far ball, the motor spins, holding blocks reset");
    }
    // Cover the animation switches and the panel's retained redraw callback.
    if (inputCallbackCtx == host.ctx) {
      for (const char *control : {"/input/bumper/click", "/input/a/click", "/input/x/click", "left"}) {
        frame_input::Event event{};
        event.type = std::string(control) == "left" ? "panel.pointerDown" : "controller.button";
        event.control = control; event.component.boolean = true;
        event.objectId = 1; event.x = 100; event.y = 140;
        JSValue value = inputEventValue(host.ctx, event);
        checkedCall(inputCallback, 1, &value);
        JS_FreeValue(host.ctx, value);
      }
    }
    if (entry.path().filename()=="physics.js") {
      for (const char *type : {"physics.collisionStart", "physics.collisionEnd"}) {
        frame_input::Event event;event.type=type;event.objectId=1;event.otherObjectId=2;event.relativeSpeed=2;
        JSValue value=inputEventValue(host.ctx,event);checkedCall(inputCallback,1,&value);JS_FreeValue(host.ctx,value);
      }
    }
    if(entry.path().filename()=="stress.js"){
      reserveObjectOverlay=[](int)->std::string{return currentScene->objects.size()>=81?"simulated runtime overlay limit":"";};
      const char *source=R"(
        clear();add(1000);if(getEngineStats().objectCount!==52||objects.length!==51||spawnStatus)throw Error('stress cap');
        const fallen=objects[0], distant=objects[1], kept=objects[2];
        setObjectPosition(fallen,0,-1.3,0);setObjectPosition(distant,3.1,0,0);
        if(!prune()||getEngineStats().objectCount!==50||objects.includes(fallen)||objects.includes(distant)||!objects.includes(kept))throw Error('stress cleanup');
        add(2);if(getEngineStats().objectCount!==52||spawnStatus)throw Error('stress reclaimed capacity');
        clear();if(getEngineStats().objectCount!==1)throw Error('stress clear');
      )";
      JSValue result=JS_Eval(host.ctx,source,strlen(source),"stress-controls",JS_EVAL_TYPE_GLOBAL);
      if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"stress example: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
      reserveObjectOverlay=[](int)->std::string{return currentScene->objects.size()>=11?"simulated runtime overlay limit":"";};
      const char *failure="add(1000);if(objects.length!==10||getEngineStats().objectCount!==11||!spawnStatus)throw Error('stress runtime failure');clear();";
      result=JS_Eval(host.ctx,failure,strlen(failure),"stress-resource-failure",JS_EVAL_TYPE_GLOBAL);
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);reserveObjectOverlay={};
      assert(host.scene.physics->contains(1)&&!host.scene.physics->contains(2)&&!host.scene.physics->contains(3));
    }
    if(entry.path().filename()=="grouped-stress.js"){
      std::set<int> handles;for(const auto &object:host.scene.objects)handles.insert(objectSurfaceKey(object));
      int released=0;const size_t initialHandles=handles.size(),groupCount=initialHandles-1;
      reserveObjectOverlay=[&](int key)->std::string{if(!handles.count(key)&&handles.size()>=initialHandles)return "simulated overlay budget";handles.insert(key);return {};};
      releaseObjectOverlay=[&](int key){assert(handles.erase(key)==1);released++;};
      const char *source=R"(
        add(150);if(objects.length<150)throw Error('grouped count');
        for(let i=objects.length-1;i>=0;i-=2)removeObject(objects.splice(i,1)[0]);
        objects.forEach(id=>setObjectPosition(id,0,0,0));
        for(const value of [-1,257,1.5,NaN,'1',null]){
          let failed=false;try{createObject(assets[0],0,0,0,.1,{renderGroup:value})}catch(error){failed=true}
          if(!failed)throw Error('group validation');
        }
        const before=getEngineStats().objectCount;
        let failed=false;try{createObject(assets[0],0,0,0,.1,{renderGroup:256})}catch(error){failed=true}
        if(!failed||getEngineStats().objectCount!==before)throw Error('reservation rollback');
        clear();add(80);if(objects.length!==80)throw Error('group recreation');
      )";
      JSValue result=JS_Eval(host.ctx,source,strlen(source),"grouped-controls",JS_EVAL_TYPE_GLOBAL);
      if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"grouped example: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);assert(handles.size()==initialHandles&&handles.count(-1)&&released==int(groupCount));
      reserveObjectOverlay={};releaseObjectOverlay={};
    }
    if(entry.path().filename()=="overlay-comparison.js"){
      const char *source=R"(
        for(const mode of ['single','grouped'])for(const kind of ['boxes','models']){
          renderer=mode;geometry=kind;targetCount=96;resetScene();
          if(objects.length!==96||getEngineStats().objectCount!==97||status)throw Error('comparison reset');
          const expected=kind==='models'?'convex':'box';
          if(getObjectPhysics(objects[0]).shape!==expected)throw Error('comparison collider');
          setObjectPosition(objects[0],0,-1.3,0);nextPrune=0;update();
          if(objects.length!==95||getEngineStats().objectCount!==96)throw Error('comparison cleanup');
        }
      )";
      JSValue result=JS_Eval(host.ctx,source,strlen(source),"overlay-comparison-controls",JS_EVAL_TYPE_GLOBAL);
      if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"comparison: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
      std::set<int> groups;for(const auto &object:host.scene.objects)groups.insert(objectSurfaceKey(object));assert(groups.size()==5);
    }
    if(entry.path().filename()=="fast-motion.js"){
      const char *source=R"({
        const asset=loadModel('assets/Avocado.glb',768,{optimizeVertexCache:true});
        if(asset!==loadModel('assets/Avocado.glb')||asset!==loadModel('assets/Avocado.glb',768,{}))throw Error('model cache identity');
        const count=assets.length;
        for(const value of [0,1,'true',null,{},[]]){
          let failed=false;try{loadModel('assets/Avocado.glb',768,{optimizeVertexCache:value})}catch(error){failed=true}
          if(!failed)throw Error('cache option boolean validation');
        }
        let failed=false;try{loadModel('assets/Avocado.glb',768,{optimizeVertexCache:false})}catch(error){failed=true}
        if(!failed)throw Error('cached model options conflict');
        for(const value of [null,0,'options',false]){
          failed=false;try{loadModel('assets/Avocado.glb',768,value)}catch(error){failed=true}
          if(!failed)throw Error('model options object validation');
        }
        failed=false;try{loadModel('assets/Avocado.glb',768,{get optimizeVertexCache(){throw Error('getter')}})}catch(error){failed=true}
        if(!failed)throw Error('model option getter propagation');
      })";
      JSValue result=JS_Eval(host.ctx,source,strlen(source),"model-cache-options",JS_EVAL_TYPE_GLOBAL);
      if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"cache options: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);
      assert(host.scene.assets.size()==3&&host.scene.assets.back().optimizeVertexCache);
    }
    printf("PASS example %s\n", entry.path().c_str());
    closeScript(host);
    currentScene = liveScene = nullptr;
  }
  // Generated demo props: authored size and center in meters (see assets/demos/README.md).
  struct DemoModel {const char *name;size_t primitives;float size[3],center[3];};
  const DemoModel demoModels[]={
    {"Basketball",2,{.1806f,.1806f,.1806f},{0,0,0}},
    {"HoopRim",1,{.328f,.014f,.378f},{0,1.6f,.461f}},
    {"HoopNet",1,{.319f,.265f,.319f},{0,1.47f,.486f}},
    {"HoopBackboard",2,{.6f,.4f,.022f},{0,1.76f,.261f}},
    {"HoopPost",1,{.4f,1.76f,.45f},{0,.88f,.025f}},
    {"Desk",1,{.9f,.03f,.5f},{0,0,0}},
    {"CradleFrame",2,{.32f,.224f,.18f},{0,.112f,0}},
    {"CradleBall",2,{.04f,.1405f,.1417f},{0,-.0698f,0}},
  };
  size_t demoFiles=0;
  for(const auto &entry:std::filesystem::directory_iterator("assets/demos"))demoFiles+=entry.path().extension()==".glb";
  assert(demoFiles==std::size(demoModels));
  for(const auto &expected:demoModels){
    ModelAsset asset;
    assert(loadModelFile(std::string("assets/demos/")+expected.name+".glb",asset));
    assert(asset.primitives.size()==expected.primitives&&!asset.hasAnimation&&!asset.hasSkin);
    const float extent=std::max({expected.size[0],expected.size[1],expected.size[2]});
    assert(std::abs(1/asset.normalization-extent)<5e-4f);
    for(int axis=0;axis<3;axis++){
      assert(std::abs(asset.center[axis]-expected.center[axis])<5e-4f);
      assert(std::abs(asset.high[axis]-expected.size[axis]/extent*.5f)<2e-3f&&std::abs(asset.low[axis]+asset.high[axis])<1e-5f);
    }
    printf("PASS demo model %s\n",expected.name);
  }
}
