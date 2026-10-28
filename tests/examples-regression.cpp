// Exercise the published examples against real bindings without a VR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
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
}
