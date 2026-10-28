/* Private EGL native-image import adapter. Not a system EGL implementation. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest2-native-buffer-lifetime.h"
#include "quest2-native-window-lifetime.h"
static pthread_once_t once=PTHREAD_ONCE_INIT;
static __eglMustCastToProperFunctionPointerType (*original_proc)(const char*);
static PFNEGLCREATEIMAGEKHRPROC original_create;
static PFNEGLCREATEIMAGEPROC original_create_core;
static EGLint (*original_error)(void);
static PFNEGLCREATEWINDOWSURFACEPROC original_window;
static PFNEGLCREATEPLATFORMWINDOWSURFACEPROC original_platform_window;
static PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC original_platform_window_ext;
static PFNEGLDESTROYSURFACEPROC original_destroy_surface;
static _Thread_local EGLint local_error=EGL_SUCCESS;
static void load(void){
    original_proc=(void*)dlsym(RTLD_NEXT,"eglGetProcAddress");
    original_error=(void*)dlsym(RTLD_NEXT,"eglGetError");
    if(original_proc)original_create=(void*)original_proc("eglCreateImageKHR");
    original_create_core=(void*)dlsym(RTLD_NEXT,"eglCreateImage");
    original_window=(void*)dlsym(RTLD_NEXT,"eglCreateWindowSurface");
    original_platform_window=(void*)dlsym(RTLD_NEXT,"eglCreatePlatformWindowSurface");
    original_platform_window_ext=(void*)dlsym(RTLD_NEXT,"eglCreatePlatformWindowSurfaceEXT");
    original_destroy_surface=(void*)dlsym(RTLD_NEXT,"eglDestroySurface");
}
static void set_error(EGLint error){
    /* A locally rejected call supersedes an older, unread donor error. */
    if(original_error)original_error();
    local_error=error;
}
static EGLSurface finish_window(EGLDisplay d,EGLSurface surface,struct sq2_window_proxy *window){
    if(surface!=EGL_NO_SURFACE&&atomic_load(&window->refs)==1){
        /* Do not return a surface holding an unowned pointer. This driver must
         * retain the window; deferred-destroy safety depends on that contract. */
        original_destroy_surface(d,surface);surface=EGL_NO_SURFACE;set_error(EGL_BAD_NATIVE_WINDOW);
    }
    sq2_window_dec(window);return surface;
}
EGLSurface eglCreateWindowSurface(EGLDisplay d,EGLConfig config,EGLNativeWindowType native,const EGLint *attrs){
    pthread_once(&once,load);
    if(!original_window||!original_destroy_surface){set_error(EGL_NOT_INITIALIZED);return EGL_NO_SURFACE;}
    struct sq2_window_proxy *window=sq2_window_wrap(native);
    if(!window){set_error(EGL_BAD_NATIVE_WINDOW);return EGL_NO_SURFACE;}
    return finish_window(d,original_window(d,config,(EGLNativeWindowType)window,attrs),window);
}
EGLSurface eglCreatePlatformWindowSurface(EGLDisplay d,EGLConfig config,void *native,const EGLAttrib *attrs){
    pthread_once(&once,load);
    if(!original_platform_window||!original_destroy_surface){set_error(EGL_NOT_INITIALIZED);return EGL_NO_SURFACE;}
    struct sq2_window_proxy *window=sq2_window_wrap(native);
    if(!window){set_error(EGL_BAD_NATIVE_WINDOW);return EGL_NO_SURFACE;}
    return finish_window(d,original_platform_window(d,config,window,attrs),window);
}
EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay d,EGLConfig config,void *native,const EGLint *attrs){
    pthread_once(&once,load);
    if(!original_platform_window_ext||!original_destroy_surface){set_error(EGL_NOT_INITIALIZED);return EGL_NO_SURFACE;}
    struct sq2_window_proxy *window=sq2_window_wrap(native);
    if(!window){set_error(EGL_BAD_NATIVE_WINDOW);return EGL_NO_SURFACE;}
    return finish_window(d,original_platform_window_ext(d,config,window,attrs),window);
}
EGLImageKHR eglCreateImageKHR(EGLDisplay d,EGLContext c,EGLenum target,EGLClientBuffer buffer,const EGLint *attrs){
    pthread_once(&once,load);
    if(!original_create){set_error(EGL_NOT_INITIALIZED);return EGL_NO_IMAGE_KHR;}
    if(target!=EGL_NATIVE_BUFFER_ANDROID)return original_create(d,c,target,buffer,attrs);
    if(!buffer){set_error(EGL_BAD_PARAMETER);return EGL_NO_IMAGE_KHR;}
    struct sq2_native_proxy *proxy=sq2_native_wrap(buffer);
    if(!proxy){set_error(EGL_BAD_MATCH);return EGL_NO_IMAGE_KHR;}
    EGLImageKHR image=original_create(d,c,target,proxy,attrs);
    sq2_native_dec(proxy);return image;
}
EGLImage eglCreateImage(EGLDisplay d,EGLContext c,EGLenum target,EGLClientBuffer buffer,const EGLAttrib *attrs){
    pthread_once(&once,load);
    if(!original_create_core){set_error(EGL_NOT_INITIALIZED);return EGL_NO_IMAGE;}
    if(target!=EGL_NATIVE_BUFFER_ANDROID)return original_create_core(d,c,target,buffer,attrs);
    if(!buffer){set_error(EGL_BAD_PARAMETER);return EGL_NO_IMAGE;}
    struct sq2_native_proxy *proxy=sq2_native_wrap(buffer);
    if(!proxy){set_error(EGL_BAD_MATCH);return EGL_NO_IMAGE;}
    /* Do not narrow EGLAttrib or translate the donor's EGL 1.5 contract. */
    EGLImage image=original_create_core(d,c,target,proxy,attrs);
    sq2_native_dec(proxy);return image;
}
EGLint eglGetError(void){
    pthread_once(&once,load);
    /* A later error from any forwarded donor call supersedes our local error.
     * Successful calls do not erase an unread error. Both stores are consumed. */
    EGLint donor_error=original_error?original_error():EGL_NOT_INITIALIZED;
    EGLint result=donor_error!=EGL_SUCCESS?donor_error:local_error;
    local_error=EGL_SUCCESS;return result;
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name){
    pthread_once(&once,load);
    if(!name)return NULL;
    if(!strcmp(name,"eglCreateImageKHR"))return (__eglMustCastToProperFunctionPointerType)eglCreateImageKHR;
    if(!strcmp(name,"eglCreateImage"))return (__eglMustCastToProperFunctionPointerType)eglCreateImage;
    if(!strcmp(name,"eglCreateWindowSurface"))return (__eglMustCastToProperFunctionPointerType)eglCreateWindowSurface;
    if(!strcmp(name,"eglCreatePlatformWindowSurface"))return (__eglMustCastToProperFunctionPointerType)eglCreatePlatformWindowSurface;
    if(!strcmp(name,"eglCreatePlatformWindowSurfaceEXT"))return (__eglMustCastToProperFunctionPointerType)eglCreatePlatformWindowSurfaceEXT;
    if(!strcmp(name,"eglGetError"))return (__eglMustCastToProperFunctionPointerType)eglGetError;
    return original_proc?original_proc(name):NULL;
}
