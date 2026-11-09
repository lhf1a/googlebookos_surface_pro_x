/* Keep the existing ES1 API usable through the standard EGL/GLES frontends.
 * Real ES1 pbuffer clear/scissor/native-fence/readback; no shader, Surface, or
 * display-mode changes. Run in the matched native EGL/GSL private environment. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define REQUIRE(value, message) do { if(!(value)) { \
    fprintf(stderr,"NATIVE_GLES1_FAIL %s egl=%x gl=%x\n",message,eglGetError(),glGetError());goto cleanup; } } while(0)
static int check_maps(void) {
    FILE *file=fopen("/proc/self/maps","r");if(!file)return 0;
    char line[2048];int vendor_es1=0,platform_es1=0,egl=0,gsl=0,invalid=0;
    while(fgets(line,sizeof(line),file)) {
        if(strstr(line,"libqti-mapper3-bridge.so") || strstr(line,"libquest2-v77-ahb.so") ||
           strstr(line,"libquest2-egl-wrapper.so") || strstr(line,"libquest2-native-mapper.so") ||
           strstr(line,"-impl-qti-display.so") || strstr(line,"mapper.minigbm.so"))invalid=1;
        if(strstr(line,"r-xp")) {
            if(strstr(line,"libGLESv1_CM_adreno.so"))vendor_es1=1;
            if(strstr(line,"/libGLESv1_CM.so"))platform_es1=1;
            if(strstr(line,"libEGL_adreno.so"))egl=1;
            if(strstr(line,"libgsl.so"))gsl=1;
            if(strstr(line,"GLES") || strstr(line,"EGL") || strstr(line,"libgsl") || strstr(line,"mapper"))
                printf("NATIVE_GLES1_MAP %s",line);
        }
    }
    fclose(file);
    return vendor_es1 && platform_es1 && egl && gsl && !invalid;
}
static int wait_fence(int fd) {
    struct pollfd p={fd,POLLIN,0};int result;
    do {result=poll(&p,1,3000);}while(result<0 && errno==EINTR);
    close(fd);
    return result==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL));
}
static unsigned edge(unsigned i,unsigned size) {
    unsigned x=i*size/4;
    return i && i<4 && !(x&1u)?x+1:x;
}
int main(void) {
    setvbuf(stdout,NULL,_IOLBF,0);
    enum { WIDTH=257, HEIGHT=129, FRAMES=8 };
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    unsigned char *pixels=NULL;
    int result=1;unsigned checked=0,fences=0;
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    REQUIRE(display!=EGL_NO_DISPLAY,"default display");
    EGLint major=0,minor=0;
    REQUIRE(eglInitialize(display,&major,&minor),"EGL initialize");
    REQUIRE(eglBindAPI(EGL_OPENGL_ES_API),"bind OpenGL ES API");
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count=0;
    REQUIRE(eglChooseConfig(display,attrs,&config,1,&count)&&count==1,"ES1 RGBA config");
    const EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,1,EGL_NONE};
    const EGLint pbuffer[]={EGL_WIDTH,WIDTH,EGL_HEIGHT,HEIGHT,EGL_NONE};
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);
    REQUIRE(context!=EGL_NO_CONTEXT,"ES1 context");
    surface=eglCreatePbufferSurface(display,config,pbuffer);
    REQUIRE(surface!=EGL_NO_SURFACE,"pbuffer");
    REQUIRE(eglMakeCurrent(display,surface,surface,context),"make current");
    EGLint version=0;
    REQUIRE(eglQueryContext(display,context,EGL_CONTEXT_CLIENT_VERSION,&version)&&version==1,"actual ES1 context version");
    const char *renderer=(const char *)glGetString(GL_RENDERER),*gl_version=(const char *)glGetString(GL_VERSION);
    printf("NATIVE_GLES1_CONTEXT renderer=%s version=%s EGL=%d.%d client_version=%d\n",
            renderer?renderer:"null",gl_version?gl_version:"null",major,minor,version);
    REQUIRE(renderer&&strstr(renderer,"Adreno")&&gl_version&&strstr(gl_version,"1."),"real Adreno ES1");
    REQUIRE(check_maps(),"standard frontend/native donor provenance without handle bridge");
    PFNEGLCREATESYNCKHRPROC create_sync=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    PFNEGLDESTROYSYNCKHRPROC destroy_sync=(PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC duplicate_fence=(PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
    REQUIRE(create_sync&&destroy_sync&&duplicate_fence,"native fence entrypoints");
    pixels=malloc(WIDTH*HEIGHT*4);REQUIRE(pixels,"readback allocation");
    glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,WIDTH,HEIGHT);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glEnable(GL_SCISSOR_TEST);
    for(unsigned frame=0;frame<FRAMES;frame++) {
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++) {
            unsigned code=(y*4+x)^frame;
            glScissor((GLint)edge(x,WIDTH),(GLint)edge(y,HEIGHT),
                (GLsizei)(edge(x+1,WIDTH)-edge(x,WIDTH)),(GLsizei)(edge(y+1,HEIGHT)-edge(y,HEIGHT)));
            glClearColor((code&1u)!=0,(code&2u)!=0,(code&4u)!=0,(code&8u)!=0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        REQUIRE(glGetError()==GL_NO_ERROR,"clear/scissor commands");
        EGLSyncKHR sync=create_sync(display,EGL_SYNC_NATIVE_FENCE_ANDROID,NULL);
        REQUIRE(sync!=EGL_NO_SYNC_KHR,"create native fence");
        glFlush();int fd=duplicate_fence(display,sync);
        EGLBoolean destroyed=destroy_sync(display,sync);
        if(fd<0 || !destroyed){if(fd>=0)close(fd);REQUIRE(0,"duplicate/destroy native fence");}
        REQUIRE(wait_fence(fd),"real GPU fence signal");fences++;
        memset(pixels,0x5a,WIDTH*HEIGHT*4);
        glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        REQUIRE(glGetError()==GL_NO_ERROR,"ES1 read pixels");
        unsigned bad=0;
        for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++) {
            unsigned row=0,column=0;
            while(row<3 && y>=edge(row+1,HEIGHT))row++;
            while(column<3 && x>=edge(column+1,WIDTH))column++;
            unsigned code=(row*4+column)^frame;
            for(unsigned c=0;c<4;c++)if(pixels[(y*WIDTH+x)*4+c]!=(((code>>c)&1u)*255u))bad++;
        }
        printf("NATIVE_GLES1_FRAME frame=%u width=%u height=%u checked_pixels=%u bad_channels=%u native_fence=1\n",
                frame,WIDTH,HEIGHT,WIDTH*HEIGHT,bad);
        REQUIRE(!bad,"exact clear/scissor RGBA pixels");checked+=WIDTH*HEIGHT;
    }
    result=0;
cleanup:
    free(pixels);
    if(display!=EGL_NO_DISPLAY) {
        if(!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT))result=1;
        if(surface!=EGL_NO_SURFACE && !eglDestroySurface(display,surface))result=1;
        if(context!=EGL_NO_CONTEXT && !eglDestroyContext(display,context))result=1;
        if(!eglTerminate(display))result=1;
        if(!eglReleaseThread())result=1;
    }
    printf("NATIVE_GLES1_%s frames=%u checked_pixels=%u native_fences=%u pbuffer_only=1\n",
            result?"FAIL":"PASS",checked/(WIDTH*HEIGHT),checked,fences);
    return result;
}
