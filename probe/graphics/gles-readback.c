/* Run directly against private Mesa libraries; never accepts software rendering.
 * Surfaceless FBO rendering exercises shaders, rasterization and GPU readback
 * without Android gralloc, SurfaceFlinger or DRM master ownership. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SQ2_QUEST2_GLES
#include "quest2-native-buffer-lifetime.h"
#endif

static void fail(const char *what) {
    fprintf(stderr, "GLES_READBACK_FAIL %s EGL=0x%x GL=0x%x\n", what, eglGetError(), glGetError());
    exit(1);
}
static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof(log), NULL, log); puts(log); fail("shader"); }
    return s;
}
static int dimension(const char *name){
    const char *value=getenv(name);if(!value)return 64;
    char *end=NULL;long n=strtol(value,&end,10);
    if(!*value||!end||*end||n<1||n>4096){fprintf(stderr,"invalid %s\n",name);exit(2);}
    return (int)n;
}
/* Probe adapter only: keep the test's small EGLint lists identical across KHR
 * and core entry points. The driver wrapper itself forwards EGLAttrib intact. */
static PFNEGLCREATEIMAGEPROC core_create;
static PFNEGLDESTROYIMAGEPROC core_destroy;
static EGLImageKHR create_core_test(EGLDisplay d,EGLContext c,EGLenum t,EGLClientBuffer b,const EGLint *attrs){
    EGLAttrib wide[33];unsigned i=0;
    if(attrs){
        for(;i<32 && attrs[i]!=EGL_NONE;i+=2){wide[i]=attrs[i];wide[i+1]=attrs[i+1];}
        if(i==32)fail("probe attribute list too long");
        wide[i]=EGL_NONE;
    }
    return (EGLImageKHR)core_create(d,c,t,b,attrs?wide:NULL);
}
static EGLBoolean destroy_core_test(EGLDisplay d,EGLImageKHR image){return core_destroy(d,(EGLImage)image);}
static int run_probe(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const int width=dimension("SQ2_GLES_WIDTH"),height=dimension("SQ2_GLES_HEIGHT");
    const size_t pixel_bytes=(size_t)width*height*4;
    int import_only = argc > 1 && !strcmp(argv[1], "--ahb-import-only");
    int cpu_readback = argc > 1 && !strcmp(argv[1], "--ahb-cpu-readback");
    int use_ahb = import_only || cpu_readback || (argc > 1 && !strcmp(argv[1], "--ahb"));
    if (argc > 2 || (argc == 2 && !use_ahb)) {
        fprintf(stderr,"usage: %s [--ahb|--ahb-import-only|--ahb-cpu-readback]\n",argv[0]);return 2;
    }
#ifdef SQ2_QUEST2_GLES
    /* Qualcomm's Android subdriver does not expose Mesa's platform. Use a
     * real pbuffer so this check does not depend on surfaceless support. */
    puts("QUEST2_GLES_STAGE get-display");
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!platform) fail("eglGetPlatformDisplayEXT");
    EGLDisplay d = platform(use_ahb ? EGL_PLATFORM_ANDROID_KHR : EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
#endif
    EGLint major, minor;
    if (!eglInitialize(d, &major, &minor)) fail("eglInitialize");
    printf("EGL %d.%d vendor=%s version=%s\n", major, minor, eglQueryString(d, EGL_VENDOR), eglQueryString(d, EGL_VERSION));
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint count;
    if (!eglChooseConfig(d, attrs, &cfg, 1, &count) || count != 1) fail("eglChooseConfig");
    EGLint ctxattrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctxattrs);
    EGLSurface surface = EGL_NO_SURFACE;
#ifdef SQ2_QUEST2_GLES
    EGLint pbattrs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    surface = eglCreatePbufferSurface(d, cfg, pbattrs);
    if (surface == EGL_NO_SURFACE) fail("pbuffer");
#endif
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(d, surface, surface, ctx)) fail("context");
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n", glGetString(GL_VENDOR), renderer, glGetString(GL_VERSION));
#ifdef SQ2_SYSTEM_EGL
    Dl_info egl_origin={0},gles_origin={0};
    if(!dladdr((void*)eglInitialize,&egl_origin)||!dladdr((void*)glGetString,&gles_origin)||
       !strstr(egl_origin.dli_fname,"/system/lib64/libEGL.so")||
       !strstr(gles_origin.dli_fname,"/system/lib64/libGLESv2.so"))fail("expected platform EGL/GLES entry points");
    printf("SYSTEM_EGL_ENTRY=%s\nSYSTEM_GLES_ENTRY=%s\n",egl_origin.dli_fname,gles_origin.dli_fname);
    FILE *maps=fopen("/proc/self/maps","r");char line[1024];
    if(!maps)fail("read loaded libraries");
    while(fgets(line,sizeof(line),maps))if(strstr(line,"/egl/") && strstr(line,"r-xp"))printf("DRIVER_MAP %s",line);
    fclose(maps);
#endif
#ifdef SQ2_QUEST2_GLES
    const char *version = (const char *)glGetString(GL_VERSION);
    if (!renderer || !strstr(renderer,"Adreno (TM) 680") || !version ||
        !strstr(version,"V@0819.0") || strstr(version,"Mesa")) fail("expected Quest2 V819 A680 renderer");
#else
    if (!renderer || (!strstr(renderer, "FD680") && !strstr(renderer, "Adreno (TM) 680"))) fail("expected hardware A680 renderer");
#endif
    GLuint tex, fbo;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    AHardwareBuffer *ahb = NULL;
    EGLImageKHR image = EGL_NO_IMAGE_KHR;
    EGLImageKHR shared_image = EGL_NO_IMAGE_KHR;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = NULL;
    if (use_ahb) {
        AHardwareBuffer_Desc desc = {.width=width,.height=height,.layers=1,.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
            .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
        if (AHardwareBuffer_allocate(&desc,&ahb)) fail("AHardwareBuffer_allocate");
        PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client = (void *)eglGetProcAddress("eglGetNativeClientBufferANDROID");
        if (!client) {
            /* Android's platform EGL shim owns this AHardwareBuffer adapter. */
            void *android_egl = dlopen("/system/lib64/libEGL.so",RTLD_NOW|RTLD_LOCAL);
            if (android_egl) client = (void *)dlsym(android_egl,"eglGetNativeClientBufferANDROID");
        }
        PFNEGLCREATEIMAGEKHRPROC create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
        if(getenv("SQ2_GLES_DIRECT_CREATE"))create_image=(void*)dlsym(RTLD_DEFAULT,"eglCreateImageKHR");
        void *create_entry=(void*)create_image;
        destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
        if(getenv("SQ2_GLES_CORE_IMAGE")){
            core_create=(void*)eglGetProcAddress("eglCreateImage");
            core_destroy=(void*)eglGetProcAddress("eglDestroyImage");
            if(getenv("SQ2_GLES_DIRECT_CREATE"))core_create=(void*)dlsym(RTLD_DEFAULT,"eglCreateImage");
            if(!core_create||!core_destroy)fail("core image entry points");
            create_entry=(void*)core_create;
            create_image=create_core_test;destroy_image=destroy_core_test;
            puts("EGL_CORE_IMAGE_PATH");
        }
        Dl_info create_origin={0};
        if(create_entry && dladdr(create_entry,&create_origin))printf("EGL_CREATE_ORIGIN=%s\n",create_origin.dli_fname);
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!client || !create_image || !destroy_image || !image_texture) fail("AHB EGL extensions");
        EGLClientBuffer native = client(ahb);
#ifdef SQ2_QUEST2_GLES
        int has_wrapper=create_origin.dli_fname && strstr(create_origin.dli_fname,"libquest2-egl-wrapper.so");
        if(has_wrapper && getenv("SQ2_GLES_NATIVE_PROXY"))fail("do not double-wrap native buffers");
        if(getenv("SQ2_GLES_IMAGE_ERRORS")){
            if(!has_wrapper)fail("image error test requires wrapper");
            unsigned (*refs)(void)=(void*)dlsym(RTLD_DEFAULT,"sq2_qti_mapper3_address_refs");
            if(!refs||refs()!=0)fail("error test initial references");
            eglGetError();
            if(create_image(d,ctx,EGL_NATIVE_BUFFER_ANDROID,native,NULL)!=EGL_NO_IMAGE_KHR)
                fail("native image with context accepted");
            if(eglGetError()!=EGL_BAD_CONTEXT||refs()!=0||eglGetError()!=EGL_SUCCESS)
                fail("failed import cleanup/error");
            /* Older donor error -> local error, and the opposite order. */
            EGLint unused=0;
            eglQueryContext(EGL_NO_DISPLAY,EGL_NO_CONTEXT,EGL_CONFIG_ID,&unused);
            if(create_image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,NULL,NULL)!=EGL_NO_IMAGE_KHR ||
               eglGetError()!=EGL_BAD_PARAMETER||eglGetError()!=EGL_SUCCESS)fail("local error precedence");
            create_image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,NULL,NULL);
            eglQueryContext(EGL_NO_DISPLAY,EGL_NO_CONTEXT,EGL_CONFIG_ID,&unused);
            if(eglGetError()!=EGL_BAD_DISPLAY||eglGetError()!=EGL_SUCCESS)fail("donor error precedence");
            puts("EGL_IMAGE_ERRORS_RELEASE_OK");
        }
        if (getenv("SQ2_GLES_TRACE_NATIVE") && native) {
            AHardwareBuffer_Desc actual;AHardwareBuffer_describe(ahb,&actual);
            printf("QUEST2_AHB_DESC w=%u h=%u stride=%u layers=%u format=%u usage=%llu\n",
                   actual.width,actual.height,actual.stride,actual.layers,actual.format,(unsigned long long)actual.usage);
            printf("QUEST2_NATIVE_HEADER");
            for (unsigned i=0;i<112;i+=4) {
                unsigned word;memcpy(&word,(const char*)native+i,4);printf(" %08x",word);
            }
            puts("");
            /* Diagnostic for this target's measured ANativeWindowBuffer ABI. */
            const int *handle=NULL;memcpy(&handle,(const char*)native+96,sizeof(handle));
            if(handle && handle[0]==12 && handle[1]>=0 && handle[2]>=0 && handle[1]+handle[2]<=128){
                printf("QUEST2_NATIVE_HANDLE");
                for(int i=0;i<3+handle[1]+handle[2];i++)printf(" %08x",(unsigned)handle[i]);
                puts("");
            }
        }
#endif
#ifdef SQ2_QUEST2_GLES
        struct sq2_native_proxy *proxy=NULL;
        if(getenv("SQ2_GLES_NATIVE_PROXY")){
            proxy=sq2_native_wrap(native);if(!proxy)fail("native proxy ABI");
            native=proxy;
        }
#endif
        const EGLint image_attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
        image = create_image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native,image_attrs);
#ifdef SQ2_QUEST2_GLES
        if(proxy)sq2_native_dec(proxy);
#endif
        if (image == EGL_NO_IMAGE_KHR) fail("AHB EGLImage import");
        image_texture(GL_TEXTURE_2D,image);
#ifdef SQ2_QUEST2_GLES
        if(getenv("SQ2_GLES_SHARED_IMAGE")){
            if(!has_wrapper&&!getenv("SQ2_GLES_NATIVE_PROXY"))fail("shared image requires proxy");
            struct sq2_native_proxy *other=NULL;
            EGLClientBuffer second=client(ahb);
            if(!has_wrapper){other=sq2_native_wrap(second);if(!other)fail("shared native proxy");second=other;}
            shared_image=create_image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,second,image_attrs);
            if(other)sq2_native_dec(other);
            if(shared_image==EGL_NO_IMAGE_KHR)fail("shared image import");
            unsigned (*refs)(void)=(void*)dlsym(RTLD_DEFAULT,"sq2_qti_mapper3_address_refs");
            if(!refs||refs()!=2)fail("shared image references");
            puts("SHARED_IMAGE_REFS_OK refs=2");
        }
#endif
        if(getenv("SQ2_GLES_DESTROY_IMAGE_EARLY")){
            if(!destroy_image(d,image))fail("early image destroy");
            image=EGL_NO_IMAGE_KHR;
            puts("EGL_IMAGE_DESTROYED_TEXTURE_RETAINED");
        }
#ifdef SQ2_QUEST2_GLES
        printf("QUEST2_GLES_AFTER_IMPORT renderer=%s context=%p expected=%p glerror=0x%x\n",
               glGetString(GL_RENDERER),eglGetCurrentContext(),ctx,glGetError());
#endif
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fail("framebuffer");
    if (import_only) {
        /* Separate metadata/import validation from the known failing GPU
         * submit path. This result is deliberately not a rendering PASS. */
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);
        if(image!=EGL_NO_IMAGE_KHR)destroy_image(d,image);AHardwareBuffer_release(ahb);
        eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(d,surface);
        if(!eglDestroyContext(d,ctx)||!eglTerminate(d))fail("import-only EGL teardown");
        puts("GPU_GLES_AHB_IMPORT_ONLY_OK no-draw-no-readback");return 0;
    }
    GLuint vs = shader(GL_VERTEX_SHADER, "attribute vec2 p; void main(){gl_Position=vec4(p,0.,1.);}");
    GLuint fs = shader(GL_FRAGMENT_SHADER, "precision highp float; uniform float phase; uniform vec2 halfSize; void main(){gl_FragColor=vec4(step(halfSize.x,gl_FragCoord.x),step(halfSize.y,gl_FragCoord.y),phase,1.);}");
    GLuint prog = glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "p"); glLinkProgram(prog);
    GLint linked; glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) fail("program link");
    glUseProgram(prog);
    glUniform2f(glGetUniformLocation(prog,"halfSize"),width*0.5f,height*0.5f);
    const GLfloat vertices[] = {-1,-1, 3,-1, -1,3};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices); glEnableVertexAttribArray(0);
    glViewport(0,0,width,height); glDisable(GL_DITHER);
    unsigned char *pixels=malloc(pixel_bytes);if(!pixels)fail("pixel allocation");
    for (int frame=0; frame<3; frame++) {
        glClearColor(1,0,1,0); glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(glGetUniformLocation(prog,"phase"), (float)(frame & 1));
        glDrawArrays(GL_TRIANGLES,0,3); glFinish();
        if(cpu_readback){
            if(glGetError()!=GL_NO_ERROR)fail("draw before direct AHB readback");
            AHardwareBuffer_Desc actual;AHardwareBuffer_describe(ahb,&actual);
            void *address=NULL;
            if(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&address)||!address)
                fail("direct AHB CPU lock");
            unsigned bad=0;
            for(int y=0;y<height;y++)for(int x=0;x<width;x++){
                const unsigned char expected[]={x>=width/2?255:0,y>=height/2?255:0,(frame&1)?255:0,255};
                const unsigned char *p=(const unsigned char*)address+(y*actual.stride+x)*4;
                if(memcmp(p,expected,4))bad++;
            }
            if(AHardwareBuffer_unlock(ahb,NULL))fail("direct AHB CPU unlock");
            if(bad){fprintf(stderr,"AHB_CPU_BAD_PIXELS=%u frame=%d\n",bad,frame);fail("direct AHB pixel mismatch");}
            printf("GPU_GLES_DIRECT_AHB_FRAME_OK frame=%d pixels=%d width=%d height=%d\n",frame,width*height,width,height);
            continue;
        }
        memset(pixels, 0x5a, pixel_bytes);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        if (glGetError() != GL_NO_ERROR) fail("draw/readback");
        for (int y=0;y<height;y++) for (int x=0;x<width;x++) {
            const unsigned char expected[] = {x>=width/2?255:0,y>=height/2?255:0,(frame&1)?255:0,255};
            const unsigned char *p = &pixels[(y*width+x)*4];
            if (memcmp(p,expected,4)) {
                fprintf(stderr,"frame=%d x=%d y=%d actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",frame,x,y,p[0],p[1],p[2],p[3],expected[0],expected[1],expected[2],expected[3]);
                if (ahb) {
                    void *address=NULL;
                    int result=AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&address);
                    fprintf(stderr,"AHB_MISMATCH_CPU_LOCK=%d\n",result);
                    if (!result && address) {
                        const unsigned char *cpu=address;
                        fprintf(stderr,"AHB_MISMATCH_CPU_FIRST=%u,%u,%u,%u\n",cpu[0],cpu[1],cpu[2],cpu[3]);
                        AHardwareBuffer_unlock(ahb,NULL);
                    }
                }
                fail("pixel mismatch");
            }
        }
        printf("GPU_GLES_FRAME_OK frame=%d pixels=%d channels=%zu\n",frame,width*height,pixel_bytes);
        if (ahb) {
            AHardwareBuffer_Desc actual; AHardwareBuffer_describe(ahb,&actual);
            void *address=NULL;
            if (AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&address) || !address) fail("AHB CPU lock after GPU draw");
            for (int y=0;y<height;y++) if (memcmp((unsigned char*)address+y*actual.stride*4,pixels+y*width*4,width*4)) fail("AHB DMA buffer GPU/CPU mismatch");
            if (AHardwareBuffer_unlock(ahb,NULL)) fail("AHB unlock");
            printf("GPU_AHB_SHARED_BUFFER_OK frame=%d stride=%u\n",frame,actual.stride);
        }
    }
    glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&tex); glDeleteProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    if (image != EGL_NO_IMAGE_KHR) destroy_image(d,image);
    if(shared_image!=EGL_NO_IMAGE_KHR){
        glFinish();
        unsigned (*refs)(void)=(void*)dlsym(RTLD_DEFAULT,"sq2_qti_mapper3_address_refs");
        if(!refs||refs()!=1)fail("shared survivor reference");
        if(!destroy_image(d,shared_image)||refs()!=0)fail("shared final release");
        puts("SHARED_IMAGE_RELEASE_OK refs=1->0");
    }
    if (ahb) AHardwareBuffer_release(ahb);
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(d,surface);
    if(!eglDestroyContext(d,ctx)||!eglTerminate(d))fail("EGL teardown");
    free(pixels);
    if(cpu_readback)printf("GPU_GLES_DIRECT_AHB_READBACK_PASS frames=3 pixels=%d glReadPixels-not-tested\n",3*width*height);
    else {
        printf("GPU_GLES_READBACK_PASS frames=3 pixels=%d\n",3*width*height);
        if (use_ahb) puts("GPU_GLES_AHB_IMPORT_READBACK_PASS");
    }
    return 0;
}
int main(int argc,char **argv){
    const char *value=getenv("SQ2_GLES_REPEATS");
    char *end=NULL;long repeats=value?strtol(value,&end,10):1;
    if(repeats<1||repeats>200||(value&&(!*value||*end))){fprintf(stderr,"invalid SQ2_GLES_REPEATS\n");return 2;}
    for(long i=0;i<repeats;i++){
        int result=run_probe(argc,argv);if(result)return result;
#if defined(SQ2_QUEST2_GLES) && !defined(SQ2_SYSTEM_EGL)
        // run_probe has destroyed all EGL/GL resources before this hook.
        void (*reset)(void)=(void*)dlsym(RTLD_DEFAULT,"sq2_qti_mapper3_reset_addresses");
        if(!reset){fprintf(stderr,"missing address teardown hook\n");return 1;}
        reset();
#endif
        if(repeats>1)printf("GLES_LIFECYCLE_OK iteration=%ld total=%ld\n",i+1,repeats);
    }
    return 0;
}
