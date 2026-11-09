/* Shared private donor EGL context and native-buffer lifetime checks. */
#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <system/window.h>
#include <stddef.h>
#include <stdatomic.h>
extern uint64_t quest2_native_mapper_check(const native_handle_t *, unsigned, unsigned, unsigned, int);
_Static_assert(sizeof(ANativeWindowBuffer_t)==168, "ARM64 native buffer ABI");
_Static_assert(offsetof(ANativeWindowBuffer_t,handle)==96, "native handle ABI");
struct native_gpu_buffer {ANativeWindowBuffer_t buffer;atomic_int refs;};
#ifndef QUEST2_NATIVE_MAPPER5
static void native_inc(android_native_base_t *base) {
    struct native_gpu_buffer *b=(void*)base;
    assert(atomic_fetch_add(&b->refs,1)>0);
}
static void native_dec(android_native_base_t *base) {
    struct native_gpu_buffer *b=(void*)base;
    assert(atomic_fetch_sub(&b->refs,1)>1);
}
#endif
static EGLDisplay gpu_display;
static EGLContext gpu_context;
static EGLSurface gpu_surface;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture;
static PFNEGLCREATESYNCKHRPROC create_sync;
static PFNEGLDESTROYSYNCKHRPROC destroy_sync;
static PFNEGLDUPNATIVEFENCEFDANDROIDPROC duplicate_fence;
static GLuint gpu_program;
static GLuint compile_shader(GLenum type,const char *text) {
    GLuint shader=glCreateShader(type);assert(shader);
    glShaderSource(shader,1,&text,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"NATIVE_SHADER_FAIL %s\n",log);}
    assert(ok);
    return shader;
}
static void native_gpu_init(const char *fragment_source) {
    gpu_display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(gpu_display!=EGL_NO_DISPLAY);
    EGLint major=0,minor=0;assert(eglInitialize(gpu_display,&major,&minor));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;assert(eglChooseConfig(gpu_display,attrs,&config,1,&count) && count==1);
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    const EGLint surface[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    gpu_context=eglCreateContext(gpu_display,config,EGL_NO_CONTEXT,ctx);assert(gpu_context!=EGL_NO_CONTEXT);
    gpu_surface=eglCreatePbufferSurface(gpu_display,config,surface);assert(gpu_surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(gpu_display,gpu_surface,gpu_surface,gpu_context));
    printf("NATIVE_GPU renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Adreno"));
#define PROC(var,name) var=(void*)eglGetProcAddress(name);assert(var)
    PROC(create_image,"eglCreateImageKHR");PROC(destroy_image,"eglDestroyImageKHR");
    PROC(image_texture,"glEGLImageTargetTexture2DOES");
    PROC(create_sync,"eglCreateSyncKHR");PROC(destroy_sync,"eglDestroySyncKHR");
    PROC(duplicate_fence,"eglDupNativeFenceFDANDROID");
#undef PROC
    GLuint vs=compile_shader(GL_VERTEX_SHADER,"#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}");
    GLuint fs=compile_shader(GL_FRAGMENT_SHADER,fragment_source);
    gpu_program=glCreateProgram();glAttachShader(gpu_program,vs);glAttachShader(gpu_program,fs);glLinkProgram(gpu_program);
    GLint ok=0;glGetProgramiv(gpu_program,GL_LINK_STATUS,&ok);assert(ok);
    glDeleteShader(vs);glDeleteShader(fs);assert(glGetError()==GL_NO_ERROR);
}
static void native_gpu_finish(void) {
    glDeleteProgram(gpu_program);
    assert(eglMakeCurrent(gpu_display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroySurface(gpu_display,gpu_surface));assert(eglDestroyContext(gpu_display,gpu_context));
    assert(eglTerminate(gpu_display));assert(eglReleaseThread());
}
