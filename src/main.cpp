#define _GNU_SOURCE
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>

extern "C" {
#include <openvr.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <quickjs.h>
}

static const char *kSocketPath = "/tmp/sf-mvp.sock";

static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_reloadReq{false};
static std::atomic<uint64_t> g_frames{0};
static std::atomic<uint64_t> g_lastReloadMs{0};
static std::atomic<uint64_t> g_reloadCount{0};
static std::mutex g_jsMutex;

static JSRuntime *g_rt = nullptr;
static JSContext *g_ctx = nullptr;
static JSValue g_updateFn = JS_UNDEFINED;
static vr::IVRSystem *g_sys = nullptr;

static uint64_t nowMs() {
    using clock = std::chrono::steady_clock;
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch()).count();
}

static int64_t statMtime(const std::string &path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return -1;
    return (int64_t)st.st_mtime;
}

static std::string readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static JSValue jsLog(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
    if (argc > 0) {
        const char *s = JS_ToCString(ctx, argv[0]);
        if (s) { std::fprintf(stdout, "[JS] %s\n", s); std::fflush(stdout); JS_FreeCString(ctx, s); }
    }
    return JS_UNDEFINED;
}


static JSValue jsPollControllers(JSContext *ctx, JSValueConst, int, JSValueConst *) {
    JSValue arr = JS_NewArray(ctx);
    if (!g_sys) return arr;
    uint32_t idx = 0;
    for (int i = 0; i < (int)vr::k_unMaxTrackedDeviceCount; i++) {
        vr::ETrackedDeviceClass cls = g_sys->GetTrackedDeviceClass((vr::TrackedDeviceIndex_t)i);
        if (cls != vr::TrackedDeviceClass_Controller && cls != vr::TrackedDeviceClass_HMD && cls != vr::TrackedDeviceClass_GenericTracker)
            continue;
        vr::VRControllerState_t st{};
        bool valid = g_sys->GetControllerState((vr::TrackedDeviceIndex_t)i, &st, sizeof(st));
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "index", JS_NewInt32(ctx, i));
        JS_SetPropertyStr(ctx, obj, "class", JS_NewInt32(ctx, (int)cls));
        JS_SetPropertyStr(ctx, obj, "valid", JS_NewBool(ctx, valid));
        JS_SetPropertyStr(ctx, obj, "buttons", JS_NewInt32(ctx, (int)st.ulButtonPressed));
        JS_SetPropertyStr(ctx, obj, "trigger", JS_NewFloat64(ctx, st.rAxis[1].x));
        JS_SetPropertyStr(ctx, obj, "axis0X", JS_NewFloat64(ctx, st.rAxis[0].x));
        JS_SetPropertyStr(ctx, obj, "axis0Y", JS_NewFloat64(ctx, st.rAxis[0].y));
        JSValue axes = JS_NewArray(ctx);
        for (int a = 0; a < (int)vr::k_unControllerStateAxisCount; a++) {
            JSValue ax = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, ax, "x", JS_NewFloat64(ctx, st.rAxis[a].x));
            JS_SetPropertyStr(ctx, ax, "y", JS_NewFloat64(ctx, st.rAxis[a].y));
            JS_SetPropertyUint32(ctx, axes, (uint32_t)a, ax);
        }
        JS_SetPropertyStr(ctx, obj, "axes", axes);
        JS_SetPropertyUint32(ctx, arr, (uint32_t)idx++, obj);
    }
    return arr;
}

static void freeJs() {
    std::lock_guard<std::mutex> lock(g_jsMutex);
    if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = nullptr; }
    if (g_rt) { JS_FreeRuntime(g_rt); g_rt = nullptr; }
    g_updateFn = JS_UNDEFINED;
}

static void loadScript(const std::string &path, uint64_t &reloadMs) {
    uint64_t t0 = nowMs();
    freeJs();
    std::lock_guard<std::mutex> lock(g_jsMutex);
    g_rt = JS_NewRuntime();
    g_ctx = JS_NewContext(g_rt);
    JS_SetMemoryLimit(g_rt, 128 * 1024 * 1024);  // 128MB hard cap, enforced by JS engine
    JS_SetMaxStackSize(g_rt, 10 * 1024 * 1024);  // 10MB C stack per JS
    // Provide print/log helper
    {
        JSValue g = JS_GetGlobalObject(g_ctx);
        JS_SetPropertyStr(g_ctx, g, "__log", JS_NewCFunction(g_ctx, jsLog, "__log", 1));
        JS_SetPropertyStr(g_ctx, g, "__pollControllers", JS_NewCFunction(g_ctx, jsPollControllers, "__pollControllers", 0));
        JSValue code = JS_NewString(g_ctx, "globalThis.print = __log; globalThis.console = {log: __log, error: __log};");
        JS_Eval(g_ctx, "globalThis.print = __log; globalThis.console = {log: __log, error: __log};", 0, "<boot>", JS_EVAL_TYPE_GLOBAL);
        (void)code;
        JS_FreeValue(g_ctx, g);
    }
    std::string src = readFile(path);
    if (src.empty()) {
        std::fprintf(stderr, "[host] script empty or unreadable: %s\n", path.c_str());
        std::fflush(stderr);
        g_lastReloadMs = nowMs() - t0;
        return;
    }
    JSValue r = JS_Eval(g_ctx, src.c_str(), src.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(g_ctx);
        const char *s = JS_ToCString(g_ctx, ex);
        std::fprintf(stderr, "[host] JS load error: %s\n", s ? s : "(unknown)");
        if (s) JS_FreeCString(g_ctx, s);
        JS_FreeValue(g_ctx, ex);
        JS_FreeValue(g_ctx, r);
        g_lastReloadMs = nowMs() - t0;
        g_reloadCount++;
        return;
    }
    JS_FreeValue(g_ctx, r);
    JSValue g = JS_GetGlobalObject(g_ctx);
    g_updateFn = JS_GetPropertyStr(g_ctx, g, "update");
    JS_FreeValue(g_ctx, g);
    reloadMs = nowMs() - t0;
    g_lastReloadMs = reloadMs;
    g_reloadCount++;
    std::fprintf(stdout, "[host] loaded %s (%zu bytes) in %lu ms (%s)\n",
                 path.c_str(), src.size(), (unsigned long)reloadMs,
                 JS_IsFunction(g_ctx, g_updateFn) ? "update present" : "no update()");
    std::fflush(stdout);
}

static void socketThread() {
    unlink(kSocketPath);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { std::fprintf(stderr, "[net] socket: %s\n", strerror(errno)); return; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);
    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) != 0) {
        std::fprintf(stderr, "[net] bind: %s\n", strerror(errno)); close(fd); return;
    }
    chmod(kSocketPath, 0666);
    listen(fd, 8);
    while (!g_quit) {
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 250) <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;
        int c = accept(fd, nullptr, nullptr);
        if (c < 0) continue;
        // read command line
        char buf[256]; ssize_t n = read(c, buf, sizeof(buf) - 1);
        if (n <= 0) { close(c); continue; }
        buf[n] = 0;
        std::string cmd(buf);
        std::string reply;
        if (cmd.rfind("status", 0) == 0) {
            struct stat st;
            size_t rssKb = 0;
            if (stat("/proc/self/statm", &st) == 0) {
                std::ifstream f("/proc/self/statm");
                long pagesTotal=0, rssPages=0;
                f >> pagesTotal >> rssPages;
                (void)pagesTotal;
                rssKb = (size_t)rssPages * (size_t)sysconf(_SC_PAGESIZE) / 1024;
            }
            char tmp[256];
            std::snprintf(tmp, sizeof(tmp),
                          "{\"frames\":%llu,\"reload_count\":%llu,\"last_reload_ms\":%llu,\"rss_kb\":%zu,\"quit\":%d}\n",
                          (unsigned long long)g_frames.load(),
                          (unsigned long long)g_reloadCount.load(),
                          (unsigned long long)g_lastReloadMs.load(),
                          rssKb, (int)g_quit.load());
            reply = tmp;
        } else if (cmd.rfind("reload", 0) == 0) {
            g_reloadReq.store(true);
            reply = "{\"queued\":\"reload\"}\n";
        } else if (cmd.rfind("quit", 0) == 0) {
            g_quit.store(true);
            reply = "{\"bye\":true}\n";
        } else {
            reply = "{\"err\":\"unknown cmd\"}\n";
        }
        write(c, reply.c_str(), reply.size());
        close(c);
    }
    close(fd);
    unlink(kSocketPath);
}

static int initEgl(EGLContext &ctx, EGLSurface &surf, EGLDisplay &disp) {
    disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (disp == EGL_NO_DISPLAY) { std::fprintf(stderr, "[egl] no display\n"); return -1; }
    EGLint major=0, minor=0;
    if (!eglInitialize(disp, &major, &minor)) {
        std::fprintf(stderr, "[egl] init fail: %s\n", "unknown");
        return -1;
    }
    static const EGLint attribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE
    };
    EGLConfig config; EGLint num=0;
    if (!eglChooseConfig(disp, attribs, &config, 1, &num) || num == 0) {
        std::fprintf(stderr, "[egl] choose config fail (err=%d)\n", eglGetError());
        return -1;
    }
    static const EGLint ctxAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
    };
    ctx = eglCreateContext(disp, config, EGL_NO_CONTEXT, ctxAttribs);
    if (ctx == EGL_NO_CONTEXT) { std::fprintf(stderr, "[egl] ctx fail err=%d\n", eglGetError()); return -1; }
    static const EGLint pbufAttribs[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
    surf = eglCreatePbufferSurface(disp, config, pbufAttribs);
    if (surf == EGL_NO_SURFACE) { std::fprintf(stderr, "[egl] pbuf fail\n"); return -1; }
    if (!eglMakeCurrent(disp, surf, surf, ctx)) { std::fprintf(stderr, "[egl] make current fail\n"); return -1; }
    return 0;
}

static GLuint makeCheckerTexture() {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    static const int S = 128;
    std::vector<unsigned char> data(S * S * 4);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            bool a = ((x / 16 + y / 16) % 2) == 0;
            int idx = (y * S + x) * 4;
            data[idx]   = a ? 255 : 20;
            data[idx+1] = a ? 40 : 255;
            data[idx+2] = a ? 240 : 255;
            data[idx+3] = 255;
            data[idx + 3] = 255;
        }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

int main(int argc, char **argv) {
    const std::string scriptPath = (argc > 1 ? argv[1] : "/home/steamos/.sf-mvp/script.js");
    std::filesystem::create_directories(std::filesystem::path(scriptPath).parent_path());

    EGLContext eglCtx = EGL_NO_CONTEXT;
    EGLSurface eglSurf = EGL_NO_SURFACE;
    EGLDisplay eglDisp = EGL_NO_DISPLAY;
    if (initEgl(eglCtx, eglSurf, eglDisp) != 0) {
        std::fprintf(stderr, "[host] EGL init failed\n"); return 1;
    }
    // GL context is current now
    GLuint tex = makeCheckerTexture();
    std::fprintf(stdout, "[host] GL texture id=%u created (256x256 inside 64-image checkers is generated on device)\n", tex);
    glFinish();

    // Init script file if missing
    if (!std::filesystem::exists(scriptPath)) {
        std::ofstream f(scriptPath);
        f << R"JS(
let t = 0; let n = 0;
function update(dt) {
    t += dt; n++;
    if (n % 1000 === 0) console.log("hello from overlay js, tick=" + n + " t=" + t.toFixed(2));
}
 (0);
)JS";
        f.close();
    }

    vr::EVRInitError eErr = vr::VRInitError_None;
    vr::VR_Init(&eErr, vr::VRApplication_Overlay);
    if (eErr != vr::VRInitError_None) {
        std::fprintf(stderr, "[openvr] VR_Init failed: %s\n",
                     vr::VR_GetVRInitErrorAsEnglishDescription(eErr));
        return 2;
    }
    vr::IVROverlay *overlay = vr::VROverlay();
    g_sys = vr::VRSystem();
    if (!overlay) {
        std::fprintf(stderr, "[openvr] VR_Overlay() null; shutting down\n");
        vr::VR_Shutdown();
        return 3;
    }

    vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
    vr::EVROverlayError oe = overlay->CreateOverlay("sf_mvp.key", "SF MVP Overlay", &h);
    std::fprintf(stdout, "[openvr] CreateOverlay=%d (%s)\n", (int)oe, overlay->GetOverlayErrorNameFromEnum(oe)); std::fflush(stdout);
    if (oe != vr::VROverlayError_None) {
        std::fprintf(stderr, "[openvr] CreateOverlay fail: %d\n", (int)oe);
        vr::VR_Shutdown();
        return 4;
    }
    overlay->SetOverlayWidthInMeters(h, 2.0f);  // bigger panel
    overlay->SetOverlayInputMethod(h, vr::VROverlayInputMethod_Mouse);
    overlay->SetOverlayFlag(h, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
    // placement straight ahead, ~1.5m
    vr::HmdMatrix34_t mat{};
    mat.m[0][0] = 1; mat.m[1][1] = 1; mat.m[2][2] = 1;
    mat.m[0][3] = 0.1f; mat.m[1][3] = -0.1f; mat.m[2][3] = -1.5f;
    const vr::TrackingUniverseOrigin origin = vr::TrackingUniverseRawAndUncalibrated;
    overlay->SetOverlayTransformTrackedDeviceRelative(h, vr::k_unTrackedDeviceIndex_Hmd, &mat);

    vr::VRTextureBounds_t bounds{};
    bounds.uMin = 0; bounds.vMin = 0;
    bounds.uMax = 1; bounds.vMax = 1;
    vr::Texture_t texture{};
    texture.eType = vr::TextureType_OpenGL;
    texture.eColorSpace = vr::ColorSpace_Auto;
    texture.handle = reinterpret_cast<void *>((uintptr_t)tex);
    oe = overlay->SetOverlayTextureBounds(h, &bounds);
    (void)oe;
    oe = overlay->SetOverlayTexture(h, &texture);
    if (oe != vr::VROverlayError_None) {
        std::fprintf(stderr, "[openvr] SetOverlayTexture fail: %d\n", (int)oe);
    }
    vr::EVROverlayError showErr = overlay->ShowOverlay(h);
    std::fprintf(stdout, "[openvr] ShowOverlay=%d (%s) width=%.2f\n", (int)showErr, overlay->GetOverlayErrorNameFromEnum(showErr), 1.0f); std::fflush(stdout);

    // ---- unix socket command server ----
    std::thread net(socketThread);

    // ---- main loop ----
    uint64_t loopStart = nowMs();
    uint64_t lastFileCheck = loopStart;
    uint64_t lastStatus = loopStart;
    int64_t lastMtime = statMtime(scriptPath);

    // load script first
    uint64_t rm;
    g_reloadReq.store(true);
    bool lastUpdateOk = false;

    while (!g_quit.load()) {
        // handle pending reload
        // poll OpenVR overlay events -> JS onEvent(type, device, button)
        if (overlay) {
            vr::VREvent_t ev;
            while (overlay->PollNextOverlayEvent(h, &ev, sizeof(ev))) {
                const char *name = g_sys ? g_sys->GetEventTypeNameFromEnum((vr::EVREventType)ev.eventType) : "(unknown)";
                const char *bname = "(mouse)";
                std::fprintf(stdout, "[openvr] event %-28s type=%d device=%u button=%d %s\n", name?name:"?", (int)ev.eventType, ev.trackedDeviceIndex, (int)ev.data.controller.button, bname?bname:"?");
                std::fflush(stdout);
                std::lock_guard<std::mutex> lock(g_jsMutex);
                if (g_ctx) {
                    JSValue gobj = JS_GetGlobalObject(g_ctx);
                    JSValue evfn = JS_GetPropertyStr(g_ctx, gobj, "onEvent");
                    JS_FreeValue(g_ctx, gobj);
                    if (JS_IsFunction(g_ctx, evfn)) {
                        int kind = (int)ev.eventType;
                        float mx = ev.data.mouse.x;
                        float my = ev.data.mouse.y;
                        uint32_t mb = ev.data.mouse.button;
                        JSValue a[5];
                        a[0]=JS_NewInt32(g_ctx, kind);
                        a[1]=JS_NewFloat64(g_ctx, (double)mx);
                        a[2]=JS_NewFloat64(g_ctx, (double)my);
                        a[3]=JS_NewInt32(g_ctx, (int)mb);
                        a[4]=JS_NewInt32(g_ctx, (int)ev.trackedDeviceIndex);
                        JSValue r=JS_Call(g_ctx, evfn, JS_UNDEFINED, 5, a);
                        if(JS_IsException(r)){JSValue ex=JS_GetException(g_ctx); const char*s=JS_ToCString(g_ctx,ex); std::fprintf(stderr,"[js onEvent] %s\n",s?s:"exc"); if(s)JS_FreeCString(g_ctx,s); JS_FreeValue(g_ctx,ex);}
                        JS_FreeValue(g_ctx,r); for(int i=0;i<5;i++) JS_FreeValue(g_ctx,a[i]);
                    }
                    JS_FreeValue(g_ctx, evfn);
                }
            }
        }

        if (g_reloadReq.exchange(false)) {
            uint64_t rm2 = 0;
            loadScript(scriptPath, rm2);
            off_t curMtime = statMtime(scriptPath);
            if (curMtime >= 0) lastMtime = curMtime;
        }
        struct stat st{};
        if (stat(scriptPath.c_str(), &st) == 0 && st.st_mtime > lastMtime) {
            g_reloadReq.store(true);
            lastMtime = st.st_mtime;
            continue;
        }

        // call update(dt)
        {
            std::lock_guard<std::mutex> lock(g_jsMutex);
            if (g_ctx && JS_IsFunction(g_ctx, g_updateFn)) {
                double dt = 0.016;
                JSValue arg = JS_NewFloat64(g_ctx, dt);
                JSValue r = JS_Call(g_ctx, g_updateFn, JS_UNDEFINED, 1, &arg);
                JS_FreeValue(g_ctx, arg);
                if (JS_IsException(r)) {
                    // print and neutralise the bad update to avoid spamming
                    JSValue ex = JS_GetException(g_ctx);
                    const char *s = JS_ToCString(g_ctx, ex);
                    std::fprintf(stderr, "[js] %s\n", s ? s : "(exception)");
                    if (s) JS_FreeCString(g_ctx, s);
                    JS_FreeValue(g_ctx, ex);
                    JS_FreeValue(g_ctx, g_updateFn);
                    g_updateFn = JS_UNDEFINED;
                } else {
                    JS_FreeValue(g_ctx, r);
                    lastUpdateOk = true;
                }
            }
        }

        g_frames.fetch_add(1, std::memory_order_relaxed);

        if (nowMs() - lastStatus >= 3000) {
            lastStatus = nowMs();
            double elapsed = (nowMs() - loopStart) / 1000.0;
            double fps = elapsed > 0 ? (double)g_frames.load() / elapsed : 0;
            bool vis = overlay ? overlay->IsOverlayVisible(h) : false;
            std::fprintf(stdout, "[host] frames=%llu fps=%.1f reloads=%llu last_reload=%llu ms update=%s visible=%d\n",
                         (unsigned long long)g_frames.load(), fps,
                         (unsigned long long)g_reloadCount.load(),
                         (unsigned long long)g_lastReloadMs.load(),
                         lastUpdateOk ? "ok" : "down", vis ? 1 : 0);
            std::fflush(stdout);
        }
        eglSwapBuffers(eglDisp, eglSurf); // harmless; keeps GL context alive
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    net.join();
vr::VR_Shutdown();
    eglDestroySurface(eglDisp, eglSurf);
    eglDestroyContext(eglDisp, eglCtx);
    eglTerminate(eglDisp);
    return 0;
}
