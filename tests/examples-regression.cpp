// Exercise the published examples against real bindings without a VR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <cstdlib>
#undef assert
#define assert(expression) do{if(!(expression)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expression);std::exit(1);}}while(0)

int main() {
  calibrationOutput = "/unused-example-calibration.json";
  for (const auto &entry : std::filesystem::directory_iterator("scripts")) {
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
        clear();add(1000);if(getEngineStats().objectCount!==81||objects.length!==80||!spawnStatus)throw Error('stress runtime failure');
        const fallen=objects[0], distant=objects[1], kept=objects[2];
        setObjectPosition(fallen,0,-1.3,0);setObjectPosition(distant,3.1,0,0);
        if(!prune()||getEngineStats().objectCount!==79||objects.includes(fallen)||objects.includes(distant)||!objects.includes(kept))throw Error('stress cleanup');
        add(2);if(getEngineStats().objectCount!==81||spawnStatus)throw Error('stress reclaimed capacity');
        clear();if(getEngineStats().objectCount!==1)throw Error('stress clear');
      )";
      JSValue result=JS_Eval(host.ctx,source,strlen(source),"stress-controls",JS_EVAL_TYPE_GLOBAL);
      if(JS_IsException(result)){auto error=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,error);fprintf(stderr,"stress example: %s\n",message?message:"exception");JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,error);}
      assert(!JS_IsException(result));JS_FreeValue(host.ctx,result);reserveObjectOverlay={};
      assert(host.scene.physics->contains(1)&&!host.scene.physics->contains(2)&&!host.scene.physics->contains(3));
    }
    printf("PASS example %s\n", entry.path().c_str());
    closeScript(host);
    currentScene = liveScene = nullptr;
  }
}
