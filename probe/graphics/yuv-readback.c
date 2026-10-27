/* Reproduce the 640x360 YV12 SurfaceFlinger import without involving SF. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void check(int ok,const char *where) {
    if(!ok){fprintf(stderr,"YUV_FAIL %s EGL=%x GL=%x\n",where,eglGetError(),glGetError());exit(1);}
}
static GLuint shader(GLenum type,const char *text) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);check(ok,"shader");return s;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    unsigned format=argc>1?(unsigned)strtoul(argv[1],NULL,0):0x32315659;
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d=platform(EGL_PLATFORM_ANDROID_KHR,EGL_DEFAULT_DISPLAY,NULL);
    check(eglInitialize(d,NULL,NULL),"initialize");
    EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint n;check(eglChooseConfig(d,attrs,&config,1,&n)&&n,"config");
    EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(d,config,EGL_NO_CONTEXT,ctxattrs);
    check(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx),"context");
    printf("RENDERER=%s FORMAT=%u\n",glGetString(GL_RENDERER),format);
    AHardwareBuffer_Desc desc={.width=640,.height=360,.layers=1,.format=format,
        .usage=AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
    AHardwareBuffer *b=NULL;check(AHardwareBuffer_allocate(&desc,&b)==0,"allocate");
    AHardwareBuffer_Planes planes;
    check(AHardwareBuffer_lockPlanes(b,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&planes)==0,"lockPlanes");
    check(planes.planeCount==3,"three YUV planes");
    for(unsigned p=0;p<3;p++) {
        unsigned w=p?320:640,h=p?180:360;
        printf("plane=%u stride=%u pixel=%u\n",p,planes.planes[p].rowStride,planes.planes[p].pixelStride);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            unsigned char value=x<w/2?(p?128:16):(p==0?81:p==1?90:240);
            ((unsigned char*)planes.planes[p].data)[y*planes.planes[p].rowStride+x*planes.planes[p].pixelStride]=value;
        }
    }
    check(AHardwareBuffer_unlock(b,NULL)==0,"unlock");
    void *systemegl=dlopen("/system/lib64/libEGL.so",RTLD_NOW|RTLD_LOCAL);
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client=(void*)dlsym(systemegl,"eglGetNativeClientBufferANDROID");
    PFNEGLCREATEIMAGEKHRPROC create=(void*)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind=(void*)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    check(client&&create&&bind,"extensions");
    EGLImageKHR image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,client(b),NULL);
    check(image!=EGL_NO_IMAGE_KHR,"YV12 EGLImage import");
    GLuint input,output,fbo;glGenTextures(1,&input);glBindTexture(GL_TEXTURE_EXTERNAL_OES,input);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    bind(GL_TEXTURE_EXTERNAL_OES,image);check(glGetError()==GL_NO_ERROR,"external texture");
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,640,360,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO");
    GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 p; varying vec2 uv; void main(){gl_Position=vec4(p,0.,1.);uv=(p+1.)*0.5;}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision highp float; varying vec2 uv; uniform samplerExternalOES src; void main(){gl_FragColor=texture2D(src,uv);}");
    GLuint prog=glCreateProgram();glAttachShader(prog,vs);glAttachShader(prog,fs);glBindAttribLocation(prog,0,"p");glLinkProgram(prog);
    GLint linked;glGetProgramiv(prog,GL_LINK_STATUS,&linked);check(linked,"link");glUseProgram(prog);
    GLfloat triangle[]={-1,-1,3,-1,-1,3};glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,triangle);glEnableVertexAttribArray(0);
    glViewport(0,0,640,360);glDrawArrays(GL_TRIANGLES,0,3);glFinish();
    unsigned char black[4],red[4];glReadPixels(160,180,1,1,GL_RGBA,GL_UNSIGNED_BYTE,black);glReadPixels(480,180,1,1,GL_RGBA,GL_UNSIGNED_BYTE,red);
    printf("black=%u,%u,%u red=%u,%u,%u\n",black[0],black[1],black[2],red[0],red[1],red[2]);
    check(black[0]<30&&black[1]<30&&black[2]<30&&red[0]>210&&red[1]<50&&red[2]<50,"YUV planes/color readback");
    check(glGetError()==GL_NO_ERROR,"GL final");
    puts("YUV_IMPORT_SAMPLE_READBACK_PASS");return 0;
}
