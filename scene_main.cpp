#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define GL_GLEXT_PROTOTYPES
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include "openvr.h"

static int initEgl(EGLContext& ctx,EGLSurface& surf,EGLDisplay& disp) {
    EGLint attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (disp == EGL_NO_DISPLAY) { EGLint err = eglGetError(); disp=eglGetDisplay((EGLNativeDisplayType)EGL_DEFAULT_DISPLAY); }
    if (!eglInitialize(disp, nullptr, nullptr)) return -1;
    EGLConfig cfg; EGLint n=0;
    const EGLint cfgattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE,8, EGL_GREEN_SIZE,8, EGL_BLUE_SIZE,8, EGL_ALPHA_SIZE,8, EGL_DEPTH_SIZE,24, EGL_NONE };
    if (!eglChooseConfig(disp,cfgattr,&cfg,1,&n) || n<1) return -2;
    ctx = eglCreateContext(disp,cfg,EGL_NO_CONTEXT,attrs);
    if (ctx == EGL_NO_CONTEXT) return -3;
    const EGLint pbuf[] = { EGL_WIDTH, 1280, EGL_HEIGHT, 720, EGL_NONE };
    surf = eglCreatePbufferSurface(disp, cfg, pbuf);
    if (surf == EGL_NO_SURFACE) return -4;
    if (!eglMakeCurrent(disp,surf,surf,ctx)) return -5;
    return 0;
}

static const char* VS =
"#version 300 es\n"
"layout(location=0) in vec4 a_pos;\n"
"layout(location=1) in vec4 a_col;\n"
"uniform mat4 u_mvp;\n"
"out vec4 v_col;\n"
"void main(){gl_Position=u_mvp*a_pos;v_col=a_col;}\n";
static const char* FS =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 v_col;\n"
"out vec4 frag;\n"
"void main(){frag=v_col;}\n";

static GLuint loadShader(GLenum type, const char* src) {
    GLuint s=glCreateShader(type); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char buf[512]; glGetShaderInfoLog(s,512,NULL,buf); fprintf(stderr,"shader err: %s\n",buf); }
    return s;
}
static GLuint makeProgram() {
    GLuint vs=loadShader(GL_VERTEX_SHADER,VS), fs=loadShader(GL_FRAGMENT_SHADER,FS);
    GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char buf[512]; glGetProgramInfoLog(p,512,NULL,buf); fprintf(stderr,"link err: %s\n",buf);}
    return p;
}

static uint64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();}

static std::atomic<bool> g_quit(false);
static std::thread g_net;
static std::atomic<uint64_t> g_frames{0};
static int g_sock=-1;

static void handleCmd(int c, bool& running) {
    char buf[128]; ssize_t n=read(c,buf,sizeof(buf)-1);
    if (n<=0){ close(c); return; } buf[n]=0;
    if (!strncmp(buf,"quit",4)){ g_quit.store(true); const char* r="{\"bye\":true}\n"; write(c,r,strlen(r)); close(c); return; }
    if (!strncmp(buf,"status",6)){ char r[256]; snprintf(r,sizeof(r),"{\"frames\":%llu}\n",(unsigned long long)g_frames.load()); write(c,r,strlen(r)); close(c); return; }
    const char* r="{\"ok\":true}\n"; write(c,r,strlen(r)); close(c);
}
static void socketThread() {
    unlink("/tmp/sf-scene.sock"); g_sock=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un addr{}; addr.sun_family=AF_UNIX; strcpy(addr.sun_path,"/tmp/sf-scene.sock");
    bind(g_sock,(struct sockaddr*)&addr,sizeof(addr)); listen(g_sock,8);
    while(!g_quit.load()) {
        fd_set fds; FD_ZERO(&fds); FD_SET(g_sock,&fds); timeval tv{0,20000}; int r=select(g_sock+1,&fds,NULL,NULL,&tv);
        if (r>0) { int c=accept(g_sock,NULL,NULL); if(c>=0){ bool dummy=false; handleCmd(c,dummy); } }
    }
    close(g_sock); unlink("/tmp/sf-scene.sock");
}

static void hmd34ToMat4(const vr::HmdMatrix34_t& m, float*out) {
    out[0]=m.m[0][0]; out[1]=m.m[1][0]; out[2]=m.m[2][0]; out[3]=0;
    out[4]=m.m[0][1]; out[5]=m.m[1][1]; out[6]=m.m[2][1]; out[7]=0;
    out[8]=m.m[0][2]; out[9]=m.m[1][2]; out[10]=m.m[2][2]; out[11]=0;
    out[12]=m.m[0][3]; out[13]=m.m[1][3]; out[14]=m.m[2][3]; out[15]=1;
}
static void mulMat4(const float*a,const float*b,float*o){ float t[16]; for(int r=0;r<4;r++) for(int c=0;c<4;c++){ float s=0; for(int k=0;k<4;k++) s+=a[r*4+k]*b[k*4+c]; t[r*4+c]=s; } memcpy(o,t,64);}
static void identMat4(float*o){ memset(o,0,64); o[0]=o[5]=o[10]=o[15]=1;}

int main() {
    std::fprintf(stdout,"[scene] init\n"); std::fflush(stdout);
    EGLContext ctx; EGLSurface surf; EGLDisplay disp;
    if (initEgl(ctx,surf,disp)!=0){ std::fprintf(stderr,"[scene] EGL fail\n"); return 1; }
    vr::EVRInitError err; vr::VR_Init(&err, vr::VRApplication_Scene);
    if (err!=vr::VRInitError_None){ std::fprintf(stderr,"[openvr] init fail %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(err)); return 2; }
    vr::IVRSystem* sys=vr::VRSystem();
    vr::IVRCompositor* comp=vr::VRCompositor();
    if(!comp){ std::fprintf(stderr,"[scene] no compositor\n"); vr::VR_Shutdown(); return 3; }
    GLuint prog=makeProgram();
    std::fprintf(stdout,"[scene] program=%u\n", prog); std::fflush(stdout);

    const int W=1280,H=720;
    GLuint eyetex[2], fbo[2]; GLuint depth[2];
    for (int i=0;i<2;i++) {
        glGenTextures(1,&eyetex[i]); glBindTexture(GL_TEXTURE_2D,eyetex[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenRenderbuffers(1,&depth[i]); glBindRenderbuffer(GL_RENDERBUFFER,depth[i]); glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,W,H);
        glGenFramebuffers(1,&fbo[i]); glBindFramebuffer(GL_FRAMEBUFFER,fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,eyetex[i],0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth[i]);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){ fprintf(stderr,"FBO bad\n"); }
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);

    // triangle
    float verts[] = { 0.0f,0.5f,0.0f, 1,1,0,  -0.5f,-0.5f,0.0f, 0,1,1, 0.5f,-0.5f,0.0f, 1,0,1 };
    GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);

    g_quit.store(false);
    std::thread net(socketThread);
    uint64_t loopStart=nowMs(); uint64_t lastStatus=loopStart; float ang=0;
    while(!g_quit.load()) {
        ang += 0.02f;
        vr::TrackedDevicePose_t rp[vr::k_unMaxTrackedDeviceCount], gp[vr::k_unMaxTrackedDeviceCount];
        comp->WaitGetPoses(rp, vr::k_unMaxTrackedDeviceCount, gp, vr::k_unMaxTrackedDeviceCount);
        for (int eye=0; eye<2; eye++) {
            vr::EVREye e = (eye==0)?vr::Eye_Left:vr::Eye_Right;
            vr::HmdMatrix44_t proj = sys->GetProjectionMatrix(e,0.05f,20.0f);
            vr::HmdMatrix34_t eyeToHead = sys->GetEyeToHeadTransform(e);
            float mvp[16]; for (int i=0;i<16;i++) mvp[i]=proj.m[i/4][i%4];
            float view[16]; hmd34ToMat4(eyeToHead, view); // note treated as view
            float model[16]; identMat4(model);
            // rotation Y
            model[0]=cosf(ang); model[2]=sinf(ang); model[8]=-sinf(ang); model[10]=cosf(ang);
            model[14]=0.0f;
            // combine mvp = proj * view * model
            float viewmodel[16]; mulMat4(view,model,viewmodel); float finalm[16]; mulMat4(mvp,viewmodel,finalm);
            glBindFramebuffer(GL_FRAMEBUFFER,fbo[eye]);
            glViewport(0,0,W,H); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST);
            glUseProgram(prog); glUniformMatrix4fv(glGetUniformLocation(prog,"u_mvp"),1,GL_TRUE,finalm);
            glBindBuffer(GL_ARRAY_BUFFER,vbo);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, sizeof(float)*6, (void*)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE, sizeof(float)*6, (void*)(3*sizeof(float)));
            glDrawArrays(GL_TRIANGLES,0,3);
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            vr::Texture_t vrTex{}; vrTex.eType=vr::TextureType_OpenGL; vrTex.eColorSpace=vr::ColorSpace_Auto; vrTex.handle=reinterpret_cast<void*>((uintptr_t)eyetex[eye]);
            comp->Submit(e,&vrTex,NULL,vr::Submit_Default);
        }
        g_frames.fetch_add(1);
        uint64_t now=nowMs();
        if (now-lastStatus>=3000){ fprintf(stdout,"[scene] frames=%llu\n",(unsigned long long)g_frames.load()); fflush(stdout); lastStatus=now; }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    net.join();
    vr::VR_Shutdown();
    return 0;
}
