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
static pthread_once_t once=PTHREAD_ONCE_INIT;
static __eglMustCastToProperFunctionPointerType (*original_proc)(const char*);
static PFNEGLCREATEIMAGEKHRPROC original_create;
static EGLint (*original_error)(void);
static _Thread_local EGLint local_error=EGL_SUCCESS;
static void load(void){
    original_proc=(void*)dlsym(RTLD_NEXT,"eglGetProcAddress");
    original_error=(void*)dlsym(RTLD_NEXT,"eglGetError");
    if(original_proc)original_create=(void*)original_proc("eglCreateImageKHR");
}
EGLImageKHR eglCreateImageKHR(EGLDisplay d,EGLContext c,EGLenum target,EGLClientBuffer buffer,const EGLint *attrs){
    pthread_once(&once,load);
    if(!original_create){local_error=EGL_NOT_INITIALIZED;return EGL_NO_IMAGE_KHR;}
    if(target!=EGL_NATIVE_BUFFER_ANDROID)return original_create(d,c,target,buffer,attrs);
    if(!buffer){local_error=EGL_BAD_PARAMETER;return EGL_NO_IMAGE_KHR;}
    struct sq2_native_proxy *proxy=sq2_native_wrap(buffer);
    if(!proxy){local_error=EGL_BAD_MATCH;return EGL_NO_IMAGE_KHR;}
    EGLImageKHR image=original_create(d,c,target,proxy,attrs);
    sq2_native_dec(proxy);return image;
}
EGLint eglGetError(void){
    pthread_once(&once,load);
    if(local_error!=EGL_SUCCESS){EGLint result=local_error;local_error=EGL_SUCCESS;return result;}
    return original_error?original_error():EGL_NOT_INITIALIZED;
}
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name){
    pthread_once(&once,load);
    if(!name)return NULL;
    if(!strcmp(name,"eglCreateImageKHR"))return (__eglMustCastToProperFunctionPointerType)eglCreateImageKHR;
    if(!strcmp(name,"eglGetError"))return (__eglMustCastToProperFunctionPointerType)eglGetError;
    return original_proc?original_proc(name):NULL;
}
