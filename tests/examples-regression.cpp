// Exercise the published examples against real bindings without a VR session.
#define main overlayEngineMain
#include "../src/main.cpp"
#undef main
#include <cassert>

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
    printf("PASS example %s\n", entry.path().c_str());
    closeScript(host);
    currentScene = liveScene = nullptr;
  }
}
