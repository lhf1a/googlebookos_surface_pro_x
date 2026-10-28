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
int main(int argc, char **argv) {
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
        destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!client || !create_image || !destroy_image || !image_texture) fail("AHB EGL extensions");
        EGLClientBuffer native = client(ahb);
#ifdef SQ2_QUEST2_GLES
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
        image = create_image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native,NULL);
        if (image == EGL_NO_IMAGE_KHR) fail("AHB EGLImage import");
        image_texture(GL_TEXTURE_2D,image);
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
        destroy_image(d,image);AHardwareBuffer_release(ahb);
        eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(d,surface);
        eglDestroyContext(d,ctx);eglTerminate(d);
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
    if (ahb) AHardwareBuffer_release(ahb);
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(d,surface);
    eglDestroyContext(d,ctx); eglTerminate(d);
    free(pixels);
    if(cpu_readback)printf("GPU_GLES_DIRECT_AHB_READBACK_PASS frames=3 pixels=%d glReadPixels-not-tested\n",3*width*height);
    else {
        printf("GPU_GLES_READBACK_PASS frames=3 pixels=%d\n",3*width*height);
        if (use_ahb) puts("GPU_GLES_AHB_IMPORT_READBACK_PASS");
    }
    return 0;
}
