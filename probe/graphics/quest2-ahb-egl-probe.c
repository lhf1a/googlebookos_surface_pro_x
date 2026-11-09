/* Standard AHardwareBuffer and platform EGL frontend, original native QTI buffers. */
#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef QUEST2_NATIVE_MAPPER5
#include <stddef.h>
#include <stdalign.h>
/* The pinned stable-C header uses bare enum names; supply their C typedefs. */
enum AIMapper_Error : int32_t;
typedef enum AIMapper_Error AIMapper_Error;
enum AIMapper_Version : uint32_t;
typedef enum AIMapper_Version AIMapper_Version;
#include <android/hardware/graphics/mapper/IMapper.h>
#include <system/window.h>
#include <dlfcn.h>
#include <poll.h>
#include <errno.h>
static AIMapper *mapper;
#endif
static EGLDisplay display;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client_buffer;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture;
static PFNEGLCREATESYNCKHRPROC create_sync;
static PFNEGLDESTROYSYNCKHRPROC destroy_sync;
static PFNEGLDUPNATIVEFENCEFDANDROIDPROC duplicate_fence;
static GLuint program;
static PFNEGLPRESENTATIONTIMEANDROIDPROC presentation_time;
static GLuint shader(GLenum kind,const char *src) {
    GLuint s=glCreateShader(kind);assert(s);
    glShaderSource(s,1,&src,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);puts(log);}
    assert(ok);return s;
}
static void check_maps(void) {
    FILE *f=fopen("/proc/self/maps","r");assert(f);
    char line[2048];int hal=0,egl=0,frontend=0;
    while(fgets(line,sizeof(line),f)) {
        assert(!strstr(line,"libquest2-native-mapper.so") &&
               !strstr(line,"libqti-mapper3-bridge.so") &&
               !strstr(line,"libquest2-v77-ahb.so") &&
               !strstr(line,"libquest2-egl-wrapper.so") &&
               !strstr(line,"mapper.minigbm.so"));
#ifdef QUEST2_NATIVE_MAPPER5
        assert(!strstr(line,"-impl-quest2.so") && !strstr(line,"libquest2-original-mapper3.so") &&
               !strstr(line,"-impl-qti-display.so"));
        if(strstr(line,"mapper.quest2.so"))hal=1;
#else
        if(strstr(line,"android.hardware.graphics.mapper@3.0-impl-quest2.so"))hal=1;
#endif
        if(strstr(line,"libEGL_adreno.so"))egl=1;
        if(strstr(line,"/libEGL.so"))frontend=1;
    }
    fclose(f);assert(hal && egl && frontend);
    puts("AHB_EGL_STANDARD_LOADER mapper_hal=1 platform_egl=1 native_qti=1 cpu_upload_bridge=0");
}
static void pattern(uint32_t format,unsigned x,unsigned y,unsigned cycle,unsigned char *out) {
    if(format==22) {
        const uint16_t values[]={0,0x3400,0x3800,0x3c00};
        uint16_t v[4]={values[(x+cycle)%4],values[(y+cycle)%4],values[(x+y)%4],0x3c00};
        memcpy(out,v,8);
    } else if(format==43) {
        uint32_t v=((x+cycle)%4)*341 | (((y+cycle)%4)*341<<10) |
                   (((x+y)%4)*341<<20) | 0xc0000000u;
        memcpy(out,&v,4);
    } else {out[0]=(unsigned char)(x+cycle*13);out[1]=(unsigned char)(y+cycle*17);
            out[2]=(unsigned char)(x+y);out[3]=format==2?73:255;}
}
static void check_buffer(uint32_t format,unsigned w,unsigned h,unsigned cycle,int locked_sync) {
    unsigned bpp=format==22?8:4;
    AHardwareBuffer_Desc d={.width=w,.height=h,.layers=1,.format=format,
        .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT};
    AHardwareBuffer *buffer=NULL;assert(AHardwareBuffer_allocate(&d,&buffer)==0 && buffer);
    AHardwareBuffer_Desc actual={0};AHardwareBuffer_describe(buffer,&actual);
    void *data=NULL;int32_t pixel=0,row=0;
    EGLClientBuffer native=client_buffer(buffer);assert(native);
#ifdef QUEST2_NATIVE_MAPPER5
    buffer_handle_t imported=NULL;
    if(locked_sync) {
        const ANativeWindowBuffer_t *window_buffer=native;
        assert(mapper->v5.importBuffer(window_buffer->handle,&imported)==AIMAPPER_ERROR_NONE);
        assert(mapper->v5.flushLockedBuffer(imported)==AIMAPPER_ERROR_BAD_BUFFER);
        assert(mapper->v5.rereadLockedBuffer(imported)==AIMAPPER_ERROR_BAD_BUFFER);
        assert(mapper->v5.lock(imported,0x33,(ARect){0,0,(int)w,(int)h},-1,&data)==AIMAPPER_ERROR_NONE && data);
        pixel=bpp;row=actual.stride*bpp;
        if(format==1 && w==64 && !cycle) {
            char path[64],target[256];snprintf(path,sizeof(path),"/proc/self/fd/%d",imported->data[1]);
            ssize_t n=readlink(path,target,sizeof(target)-1);assert(n>0);target[n]=0;
            printf("MAPPER5_METADATA_BACKING %s\n",target);
        }
    } else
#else
    assert(!locked_sync);
#endif
    assert(AHardwareBuffer_lockAndGetInfo(buffer,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
           -1,NULL,&data,&pixel,&row)==0 && data && pixel==(int)bpp && row==(int)(actual.stride*bpp));
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        pattern(format,x,y,cycle,(unsigned char *)data+(size_t)y*row+x*bpp);
#ifdef QUEST2_NATIVE_MAPPER5
    if(locked_sync)assert(mapper->v5.flushLockedBuffer(imported)==AIMAPPER_ERROR_NONE);
    else
#endif
    assert(AHardwareBuffer_unlock(buffer,NULL)==0);
    const EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
    EGLImageKHR image=create_image(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native,attrs);
    if(image==EGL_NO_IMAGE_KHR)printf("AHB_IMAGE_ERROR %x\n",eglGetError());
    assert(image!=EGL_NO_IMAGE_KHR);
    GLenum internal=format==22?GL_RGBA16F:format==43?GL_RGB10_A2:GL_RGBA8;
    GLenum type=format==22?GL_HALF_FLOAT:format==43?GL_UNSIGNED_INT_2_10_10_10_REV:GL_UNSIGNED_BYTE;
    GLuint textures[2],fbo;glGenTextures(2,textures);
    glBindTexture(GL_TEXTURE_2D,textures[0]);image_texture(GL_TEXTURE_2D,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexStorage2D(GL_TEXTURE_2D,1,internal,w,h);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"source"),0);
    glDisable(GL_DITHER);glViewport(0,0,w,h);glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char *pixels=malloc((size_t)w*h*bpp);assert(pixels);
    glReadPixels(0,0,w,h,GL_RGBA,type,pixels);assert(glGetError()==GL_NO_ERROR);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        unsigned char expected[8];pattern(format,x,y,cycle,expected);
        if(format==5){unsigned char t=expected[0];expected[0]=expected[2];expected[2]=t;}
        if(format==2)expected[3]=255; // X storage is ignored when sampling an opaque RGBX image.
        assert(!memcmp(pixels+((size_t)y*w+x)*bpp,expected,bpp));
    }
    free(pixels);
    glBindTexture(GL_TEXTURE_2D,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    unsigned char expected[8]={31,97,193,255};
    if(format==22){const uint16_t v[]={0x3400,0x3800,0x3a00,0x3c00};memcpy(expected,v,8);glClearColor(.25f,.5f,.75f,1);}
    else if(format==43){uint32_t v=341|(682<<10)|(1023<<20)|(3u<<30);memcpy(expected,&v,4);glClearColor(1.f/3,2.f/3,1,1);}
    else {glClearColor(31.f/255,97.f/255,193.f/255,1);
          if(format==5){expected[0]=193;expected[2]=31;}}
    glClear(GL_COLOR_BUFFER_BIT);assert(glGetError()==GL_NO_ERROR);
    const EGLint sync_attrs[]={EGL_NONE};
    EGLSyncKHR sync=create_sync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,sync_attrs);assert(sync!=EGL_NO_SYNC_KHR);
    glFlush();int fence=duplicate_fence(display,sync);assert(fence>=0);
    assert(destroy_sync(display,sync));
#ifdef QUEST2_NATIVE_MAPPER5
    if(locked_sync) {
        struct pollfd p={.fd=fence,.events=POLLIN};int ready;
        do {ready=poll(&p,1,5000);}while(ready<0 && errno==EINTR);
        assert(ready==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));
        assert(mapper->v5.rereadLockedBuffer(imported)==AIMAPPER_ERROR_NONE);
        // Read the original CPU pointer: no unlock/relock or remap may hide stale cache.
    } else
#endif
    {
        data=NULL;pixel=0;row=0;
        assert(AHardwareBuffer_lockAndGetInfo(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
            fence,NULL,&data,&pixel,&row)==0 && data && pixel==(int)bpp && row==(int)(actual.stride*bpp));
    }
    close(fence);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        assert(!memcmp((unsigned char *)data+(size_t)y*row+x*bpp,expected,format==2?3:bpp));
#ifdef QUEST2_NATIVE_MAPPER5
    if(locked_sync) {
        assert(mapper->v5.freeBuffer(imported)==AIMAPPER_ERROR_BAD_VALUE);
        int release=-1;assert(mapper->v5.unlock(imported,&release)==AIMAPPER_ERROR_NONE);
        if(release>=0) {
            struct pollfd p={.fd=release,.events=POLLIN};int ready;
            do {ready=poll(&p,1,5000);}while(ready<0 && errno==EINTR);
            assert(ready==1 && (p.revents&POLLIN));close(release);
        }
        assert(mapper->v5.flushLockedBuffer(imported)==AIMAPPER_ERROR_BAD_BUFFER);
        assert(mapper->v5.rereadLockedBuffer(imported)==AIMAPPER_ERROR_BAD_BUFFER);
        assert(mapper->v5.freeBuffer(imported)==AIMAPPER_ERROR_NONE);
    } else
#endif
    assert(AHardwareBuffer_unlock(buffer,NULL)==0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,textures);
    assert(destroy_image(display,image));AHardwareBuffer_release(buffer);
    assert(glGetError()==GL_NO_ERROR);
    printf("%s format=%u width=%u height=%u cycle=%u sample=1 write=1 native_fence=1 pixel_exact=1\n",
           locked_sync?"MAPPER5_LOCKED_SYNC_GPU_PASS":"AHB_EGL_PASS",format,w,h,cycle);
}
static void check_window(EGLConfig config,EGLContext context,EGLSurface pbuffer,
                         uint32_t format,unsigned w,unsigned h) {
    AImageReader *reader=NULL;
    media_status_t r=AImageReader_newWithUsage(w,h,format,
        AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,4,&reader);
    printf("BQ_READER format=%u size=%ux%u status=%d\n",format,w,h,r);
    assert(r==AMEDIA_OK && reader);
    ANativeWindow *window=NULL;assert(AImageReader_getWindow(reader,&window)==AMEDIA_OK && window);
    EGLSurface surface=eglCreateWindowSurface(display,config,window,NULL);
    if(surface==EGL_NO_SURFACE)printf("BQ_WINDOW_ERROR %x\n",eglGetError());
    assert(surface!=EGL_NO_SURFACE);
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext window_context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
    assert(window_context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,surface,surface,window_context));
    for(unsigned frame=0;frame<6;frame++) {
        unsigned char expected[8]={(unsigned char)(31+frame*11),97,193,255};
        unsigned bpp=format==22?8:4;
        if(format==22){
            const uint16_t bits[]={0,0x3400,0x3800,0x3c00};
            const float levels[]={0,.25f,.5f,1};
            const uint16_t v[]={bits[frame%4],bits[frame/4],0x3a00,0x3c00};
            memcpy(expected,v,8);glClearColor(levels[frame%4],levels[frame/4],.75f,1);
        } else if(format==43){
            uint32_t v=(frame%4)*341|((frame/4)*341<<10)|(1023<<20)|(3u<<30);
            memcpy(expected,&v,4);glClearColor((frame%4)/3.f,(frame/4)/3.f,1,1);
        }
        else glClearColor(expected[0]/255.f,97.f/255,193.f/255,1);
        glViewport(0,0,w,h);glDisable(GL_DITHER);glClear(GL_COLOR_BUFFER_BIT);
        assert(glGetError()==GL_NO_ERROR);
        int64_t expected_time=1000000000LL+(int64_t)frame*16666667;
        assert(presentation_time(display,surface,expected_time));
        assert(eglSwapBuffers(display,surface));
        AImage *image=NULL;int fence=-1;
        for(unsigned attempt=0;attempt<200;attempt++) {
            r=AImageReader_acquireNextImageAsync(reader,&image,&fence);
            if(r!=AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)break;
            usleep(10000);
        }
        assert(r==AMEDIA_OK && image);
        int64_t timestamp=0;
        assert(AImage_getTimestamp(image,&timestamp)==AMEDIA_OK && timestamp==expected_time);
        AHardwareBuffer *buffer=NULL;assert(AImage_getHardwareBuffer(image,&buffer)==AMEDIA_OK && buffer);
        AHardwareBuffer_Desc actual={0};AHardwareBuffer_describe(buffer,&actual);
        printf("BQ_BUFFER requested=%u actual=%u size=%ux%u stride=%u frame=%u fence=%d\n",
               format,actual.format,actual.width,actual.height,actual.stride,frame,fence);
        assert(actual.format==format && actual.width==w && actual.height==h && actual.layers==1);
        void *data=NULL;int32_t pixel=0,row=0;
        assert(AHardwareBuffer_lockAndGetInfo(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
            fence,NULL,&data,&pixel,&row)==0 && data && pixel==(int)bpp && row==(int)(actual.stride*bpp));
        if(fence>=0)close(fence);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
            assert(!memcmp((unsigned char *)data+(size_t)y*row+x*bpp,expected,bpp));
        assert(AHardwareBuffer_unlock(buffer,NULL)==0);
        AImage_delete(image);
        printf("BQ_EGL_FRAME_PASS format=%u width=%u height=%u frame=%u pixel_exact=1 timestamp_exact=1\n",format,w,h,frame);
    }
    assert(eglMakeCurrent(display,pbuffer,pbuffer,context));
    assert(eglDestroySurface(display,surface));assert(eglDestroyContext(display,window_context));AImageReader_delete(reader);
    printf("BQ_EGL_WINDOW_PASS format=%u width=%u height=%u frames=6\n",format,w,h);
}

#ifdef QUEST2_NATIVE_MAPPER5
static void check_ubwc(uint32_t format,unsigned w,unsigned h,uint64_t usage) {
    AHardwareBuffer_Desc d={.width=w,.height=h,.layers=1,.format=format,.usage=usage};
    AHardwareBuffer *buffer=NULL;assert(!AHardwareBuffer_allocate(&d,&buffer) && buffer);
    EGLClientBuffer native=client_buffer(buffer);assert(native);
    buffer_handle_t imported=NULL;
    assert(mapper->v5.importBuffer(((ANativeWindowBuffer_t *)native)->handle,&imported)==AIMAPPER_ERROR_NONE);
    void *cpu=NULL;
    assert(mapper->v5.lock(imported,0x3,(ARect){0,0,(int)w,(int)h},-1,&cpu)==AIMAPPER_ERROR_UNSUPPORTED && !cpu);
    assert(mapper->v5.flushLockedBuffer(imported)==AIMAPPER_ERROR_BAD_BUFFER);
    const EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
    EGLImageKHR image=create_image(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native,attrs);
    assert(image!=EGL_NO_IMAGE_KHR);
    GLuint textures[2],fbo;glGenTextures(2,textures);
    glBindTexture(GL_TEXTURE_2D,textures[0]);image_texture(GL_TEXTURE_2D,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,w,h);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glDisable(GL_DITHER);glViewport(0,0,w,h);
    unsigned char *pixels=malloc((size_t)w*h*4);assert(pixels);
    for(unsigned cycle=0;cycle<3;cycle++) {
        glBindTexture(GL_TEXTURE_2D,0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glEnable(GL_SCISSOR_TEST);
        for(unsigned q=0;q<4;q++) {
            unsigned left=(q&1)?w/2:0,bottom=(q&2)?h/2:0;
            glScissor(left,bottom,(q&1)?w-w/2:w/2,(q&2)?h-h/2:h/2);
            glClearColor((q&1)?1:0,(q&2)?1:0,cycle*.5f,1);glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);assert(glGetError()==GL_NO_ERROR);
        const EGLint sync_attrs[]={EGL_NONE};
        EGLSyncKHR sync=create_sync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,sync_attrs);assert(sync!=EGL_NO_SYNC_KHR);
        glFlush();int fence=duplicate_fence(display,sync);assert(fence>=0);
        assert(destroy_sync(display,sync));
        struct pollfd p={.fd=fence,.events=POLLIN};int ready;
        do {ready=poll(&p,1,5000);}while(ready<0 && errno==EINTR);
        assert(ready==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));close(fence);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
        glUseProgram(program);glUniform1i(glGetUniformLocation(program,"source"),0);
        glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        assert(glGetError()==GL_NO_ERROR);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            const unsigned char expected[]={x>=w/2?255:0,y>=h/2?255:0,cycle==1?128:cycle==2?255:0,255};
            for(unsigned c=0;c<4;c++)
                assert(abs((int)pixels[((size_t)y*w+x)*4+c]-expected[c])<=1);
        }
    }
    free(pixels);glBindTexture(GL_TEXTURE_2D,0);glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,textures);assert(destroy_image(display,image));
    assert(mapper->v5.freeBuffer(imported)==AIMAPPER_ERROR_NONE);AHardwareBuffer_release(buffer);
    printf("AHB_UBWC_PASS format=%u width=%u height=%u usage=%llx gpu_roundtrips=3 cpu_lock_rejected=1\n",
           format,w,h,(unsigned long long)usage);
}
#endif

int main(int argc,char **argv) {
    int ubwc=argc==2 && !strcmp(argv[1],"--ubwc");
    if(argc!=1 && !ubwc)return 64;
#ifndef QUEST2_NATIVE_MAPPER5
    if(ubwc)return 64;
#endif
    setvbuf(stdout,NULL,_IONBF,0);
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    EGLint major=0,minor=0;
    if(!eglInitialize(display,&major,&minor)){printf("AHB_EGL_INIT_ERROR %x\n",eglGetError());return 2;}
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT|EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;
    EGLBoolean chosen=eglChooseConfig(display,attrs,&config,1,&count);
    if(!chosen || count!=1) {
        printf("AHB_EGL_CONFIG_ERROR pid=%d chosen=%u count=%d error=%x version=%d.%d vendor=%s\n",
               getpid(),chosen,count,eglGetError(),major,minor,eglQueryString(display,EGL_VENDOR));
        EGLConfig configs[128];EGLint total=0;
        EGLBoolean listed=eglGetConfigs(display,configs,128,&total);
        printf("AHB_EGL_CONFIGS listed=%u count=%d error=%x\n",listed,total,eglGetError());
        for(EGLint i=0;listed && i<total && i<128;i++) {
            EGLint id=0,surface_type=0,renderable=0,red=0,alpha=0;
            eglGetConfigAttrib(display,configs[i],EGL_CONFIG_ID,&id);
            eglGetConfigAttrib(display,configs[i],EGL_SURFACE_TYPE,&surface_type);
            eglGetConfigAttrib(display,configs[i],EGL_RENDERABLE_TYPE,&renderable);
            eglGetConfigAttrib(display,configs[i],EGL_RED_SIZE,&red);
            eglGetConfigAttrib(display,configs[i],EGL_ALPHA_SIZE,&alpha);
            printf("AHB_EGL_CONFIG id=%d surface=%x renderable=%x red=%d alpha=%d\n",
                   id,surface_type,renderable,red,alpha);
        }
        FILE *maps=fopen("/proc/self/maps","r");
        if(maps) {
            char line[2048];
            while(fgets(line,sizeof(line),maps))
                if(strstr(line,".so") && (strstr(line,"egl") || strstr(line,"EGL") ||
                   strstr(line,"gralloc") || strstr(line,"mapper") || strstr(line,"libgsl") ||
                   strstr(line,"GLES") || strstr(line,"hardware.so")))fputs(line,stdout);
            fclose(maps);
        }
        return 3;
    }
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surf[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);assert(context!=EGL_NO_CONTEXT);
    EGLSurface surface=eglCreatePbufferSurface(display,config,surf);assert(surface!=EGL_NO_SURFACE);
    assert(eglMakeCurrent(display,surface,surface,context));
    printf("AHB_EGL_RENDERER %s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Adreno"));
#define PROC(var,name) var=(void *)eglGetProcAddress(name);assert(var)
    PROC(create_image,"eglCreateImageKHR");PROC(destroy_image,"eglDestroyImageKHR");
    PROC(client_buffer,"eglGetNativeClientBufferANDROID");PROC(image_texture,"glEGLImageTargetTexture2DOES");
    PROC(presentation_time,"eglPresentationTimeANDROID");
    PROC(create_sync,"eglCreateSyncKHR");PROC(destroy_sync,"eglDestroySyncKHR");PROC(duplicate_fence,"eglDupNativeFenceFDANDROID");
#undef PROC
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;uniform highp sampler2D source;out vec4 color;void main(){color=texelFetch(source,ivec2(gl_FragCoord.xy),0);}");
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);glDeleteShader(vs);glDeleteShader(fs);
#ifdef QUEST2_NATIVE_MAPPER5
    void *support=dlopen("libvndksupport.so",RTLD_NOW|RTLD_LOCAL);assert(support);
    void *(*load_sphal)(const char *,int)=(void *)dlsym(support,"android_load_sphal_library");assert(load_sphal);
    void *library=load_sphal("mapper.quest2.so",RTLD_NOW|RTLD_LOCAL);assert(library);
    AIMapper_Error (*load_mapper)(AIMapper **)=(void *)dlsym(library,"AIMapper_loadIMapper");assert(load_mapper);
    assert(load_mapper(&mapper)==AIMAPPER_ERROR_NONE && mapper && mapper->version==AIMAPPER_VERSION_5);
#endif
#ifdef QUEST2_NATIVE_MAPPER5
    if(ubwc) {
        const uint32_t formats[]={1,2,43,22},widths[]={130,1280,2880},heights[]={74,720,1920};
        const uint64_t usages[]={0x10000900,0x1a00};
        for(unsigned f=0;f<4;f++)for(unsigned u=0;u<2;u++)for(unsigned w=0;w<3;w++)
            check_ubwc(formats[f],widths[w],heights[w],usages[u]);
        check_maps();glDeleteProgram(program);
        assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
        assert(eglDestroySurface(display,surface));assert(eglDestroyContext(display,context));
        assert(eglTerminate(display));assert(eglReleaseThread());
        puts("AHB_UBWC_ALL_PASS buffers=24 gpu_roundtrips=72 physical_scanout_verified=0");return 0;
    }
#endif
    #ifdef QUEST2_NATIVE_MAPPER5
    const uint32_t formats[]={1,43,22,2,5},widths[]={64,129,1280};
#else
    const uint32_t formats[]={1,43,22},widths[]={64,129,1280};
#endif
    const unsigned format_count=sizeof(formats)/sizeof(formats[0]);
    for(unsigned f=0;f<format_count;f++)for(unsigned w=0;w<3;w++)for(unsigned c=0;c<3;c++)
        check_buffer(formats[f],widths[w],w==2?720:65,c,0);
#ifdef QUEST2_NATIVE_MAPPER5
    for(unsigned f=0;f<format_count;f++)for(unsigned w=0;w<3;w++)for(unsigned c=0;c<3;c++)
        check_buffer(formats[f],widths[w],w==2?720:65,c,1);
#endif
    check_maps();
    for(unsigned f=0;f<3;f++) {
        const EGLint window_attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE,f==2?16:f==1?10:8,EGL_GREEN_SIZE,f==2?16:f==1?10:8,
            EGL_BLUE_SIZE,f==2?16:f==1?10:8,EGL_ALPHA_SIZE,f==2?16:f==1?2:8,
            EGL_COLOR_COMPONENT_TYPE_EXT,f==2?EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT:EGL_COLOR_COMPONENT_TYPE_FIXED_EXT,EGL_NONE};
        EGLConfig window_config;EGLint found=0;
        assert(eglChooseConfig(display,window_attrs,&window_config,1,&found) && found==1);
        for(unsigned w=0;w<3;w++)check_window(window_config,context,surface,formats[f],widths[w],w==2?720:65);
    }
    check_maps();glDeleteProgram(program);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    assert(eglDestroySurface(display,surface));assert(eglDestroyContext(display,context));
    assert(eglTerminate(display));assert(eglReleaseThread());
    printf("AHB_EGL_ALL_PASS buffers=%u bufferqueue_frames=54\n",format_count*9);return 0;
}
