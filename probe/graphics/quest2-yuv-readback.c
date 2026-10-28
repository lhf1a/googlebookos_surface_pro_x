/* Real AHB YUV planes -> proprietary system EGL external texture -> RGB.
 * Only run in the private EGL mount namespace, never switch SurfaceFlinger. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest2-readback-trace.h"
static void check(int ok,const char *where){if(!ok){fprintf(stderr,"QUEST2_YUV_FAIL %s EGL=%x GL=%x\n",where,eglGetError(),glGetError());exit(1);}}
static unsigned option(const char *name,unsigned fallback){const char *s=getenv(name);if(!s)return fallback;char *end;unsigned long v=strtoul(s,&end,0);check(*s&&!*end&&v>0&&v<=4096,"option");return v;}
static GLuint shader(GLenum type,const char *source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);check(ok,"shader");return s;}
static const uint8_t colors[4][3]={{16,128,128},{235,128,128},{81,90,240},{145,54,34}};
static int clamp(int x){return x<0?0:x>255?255:x;}
static void verify(const uint8_t *pixels,unsigned stride,unsigned width,unsigned height,unsigned frame,const char *path){
    unsigned bad=0,max_error=0;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
        // Ignore only 2 pixels at each 4x4 color-cell edge for chroma siting.
        if(x%(width/4)<2||x%(width/4)>=width/4-2||y%(height/4)<2||y%(height/4)>=height/4-2)continue;
        const uint8_t *c=colors[(x*4/width+y*4/height+frame)%4];int yy=(int)c[0]-16,u=(int)c[1]-128,v=(int)c[2]-128;
        int expected[4]={clamp((298*yy+409*v+128)>>8),clamp((298*yy-100*u-208*v+128)>>8),clamp((298*yy+516*u+128)>>8),255};
        for(unsigned k=0;k<4;k++){
            unsigned actual=pixels[(size_t)y*stride+x*4+k];int delta=abs((int)actual-expected[k]);
            if((unsigned)delta>max_error)max_error=delta;
            if(delta>(k==3?0:20)){if(bad<12)printf("YUV_MISMATCH path=%s x=%u y=%u channel=%u actual=%u expected=%d\n",path,x,y,k,actual,expected[k]);bad++;}
        }
    }
    if(bad){FILE *dump=fopen("/data/local/tmp/sq2-closed/yuv-last.ppm","wb");if(dump){fprintf(dump,"P6\n%u %u\n255\n",width,height);for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)fwrite(pixels+(size_t)y*stride+x*4,1,3,dump);fclose(dump);}}
    printf("YUV_PIXELS path=%s frame=%u bad_channels=%u max_error=%u\n",path,frame,bad,max_error);check(!bad,"BT601 limited range grid pixels");
}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    unsigned width=option("SQ2_YUV_WIDTH",640),height=option("SQ2_YUV_HEIGHT",360);
    unsigned format=getenv("SQ2_YUV_NV12")?35:0x32315659;
    unsigned frames=option("SQ2_YUV_FRAMES",3);check(frames<=100,"bounded frames");
    check(width>=32&&height>=32&&!(width%8)&&!(height%8),"even chroma cells, 4x4 grid");
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);check(eglInitialize(d,NULL,NULL),"initialize");
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint n;check(eglChooseConfig(d,attrs,&config,1,&n)&&n,"config");
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE},pa[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLContext context=eglCreateContext(d,config,EGL_NO_CONTEXT,ca);EGLSurface surface=eglCreatePbufferSurface(d,config,pa);
    check(context!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(d,surface,surface,context),"current");
    if(getenv("SQ2_YUV_TRACE_COPY")){
        quest2_install_readback_trace(8);
        check(!atexit(quest2_report_readback_trace),"trace exit report");
    }
    const char *version=(const char*)glGetString(GL_VERSION);printf("YUV_RENDERER=%s VERSION=%s\n",glGetString(GL_RENDERER),version);
    check(version&&strstr(version,"V@0819.0")&&!strstr(version,"Mesa"),"closed renderer");
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client=(void*)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    PFNEGLCREATEIMAGEKHRPROC create=(void*)eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC destroy=(void*)eglGetProcAddress("eglDestroyImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind=(void*)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    check(client&&create&&destroy&&bind,"entrypoints");
    GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 p;varying vec2 uv;void main(){gl_Position=vec4(p,0.,1.);uv=(p+1.)*0.5;}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#extension GL_OES_EGL_image_external : require\nprecision highp float;varying vec2 uv;uniform samplerExternalOES src;void main(){gl_FragColor=texture2D(src,uv);}");
    GLuint prog=glCreateProgram();glAttachShader(prog,vs);glAttachShader(prog,fs);glBindAttribLocation(prog,0,"p");glLinkProgram(prog);GLint linked;glGetProgramiv(prog,GL_LINK_STATUS,&linked);check(linked,"link");glUseProgram(prog);
    const GLfloat vertices[]={-1,-1,3,-1,-1,3};glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glEnableVertexAttribArray(0);glDisable(GL_DITHER);
    uint8_t *pixels=malloc((size_t)width*height*4);check(pixels!=NULL,"readback allocation");
    for(unsigned frame=0;frame<frames;frame++){
        AHardwareBuffer_Desc desc={.width=width,.height=height,.layers=1,.format=format,.usage=AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
        AHardwareBuffer *buffer=NULL;check(!AHardwareBuffer_allocate(&desc,&buffer),"allocate");
        AHardwareBuffer_Planes planes;check(!AHardwareBuffer_lockPlanes(buffer,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&planes)&&planes.planeCount==3,"lockPlanes");
        for(unsigned p=0;p<3;p++){
            unsigned w=p?width/2:width,h=p?height/2:height;
            printf("YUV_CPU_PLANE frame=%u p=%u row=%u pixel=%u\n",frame,p,planes.planes[p].rowStride,planes.planes[p].pixelStride);
            for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
                ((uint8_t*)planes.planes[p].data)[(size_t)y*planes.planes[p].rowStride+x*planes.planes[p].pixelStride]=colors[(x*4/w+y*4/h+frame)%4][p];
        }
        check(!AHardwareBuffer_unlock(buffer,NULL),"unlock");
        EGLImageKHR image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,client(buffer),NULL);check(image!=EGL_NO_IMAGE_KHR,"image import");
        GLuint input,output,fbo;glGenTextures(1,&input);glBindTexture(GL_TEXTURE_EXTERNAL_OES,input);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        bind(GL_TEXTURE_EXTERNAL_OES,image);check(glGetError()==GL_NO_ERROR,"external bind");
        check(destroy(d,image),"early image destroy");
        AHardwareBuffer *output_buffer=NULL;EGLImageKHR output_image=EGL_NO_IMAGE_KHR;
        glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
        if(getenv("SQ2_YUV_LINEAR_OUTPUT")){
            AHardwareBuffer_Desc rgba={.width=width,.height=height,.layers=1,.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
                .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT};
            check(!AHardwareBuffer_allocate(&rgba,&output_buffer),"linear output allocate");
            output_image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,client(output_buffer),NULL);
            check(output_image!=EGL_NO_IMAGE_KHR,"linear output import");bind(GL_TEXTURE_2D,output_image);
        }else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO");
        glViewport(0,0,width,height);glDrawArrays(GL_TRIANGLES,0,3);
        GLuint relay=0,relay_fbo=0,relay_prog=0,relay_fs=0;
        if(getenv("SQ2_YUV_RELAY_OUTPUT")){
            check(!output_buffer,"relay requires internal first output");
            glFinish();check(glGetError()==GL_NO_ERROR,"before relay");
            AHardwareBuffer_Desc rgba={.width=width,.height=height,.layers=1,.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
                .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT};
            check(!AHardwareBuffer_allocate(&rgba,&output_buffer),"relay allocate");
            output_image=create(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,client(output_buffer),NULL);
            check(output_image!=EGL_NO_IMAGE_KHR,"relay import");
            glGenTextures(1,&relay);glBindTexture(GL_TEXTURE_2D,relay);bind(GL_TEXTURE_2D,output_image);
            glGenFramebuffers(1,&relay_fbo);glBindFramebuffer(GL_FRAMEBUFFER,relay_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,relay,0);
            check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"relay FBO");
            relay_fs=shader(GL_FRAGMENT_SHADER,"precision highp float;varying vec2 uv;uniform sampler2D src;void main(){gl_FragColor=texture2D(src,uv);}");
            relay_prog=glCreateProgram();glAttachShader(relay_prog,vs);glAttachShader(relay_prog,relay_fs);
            glBindAttribLocation(relay_prog,0,"p");glLinkProgram(relay_prog);glGetProgramiv(relay_prog,GL_LINK_STATUS,&linked);check(linked,"relay link");
            glUseProgram(relay_prog);glBindTexture(GL_TEXTURE_2D,output);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glDrawArrays(GL_TRIANGLES,0,3);puts("YUV_INTERNAL_TEXTURE_RELAY_TO_LINEAR");
        }
        memset(pixels,0x5a,(size_t)width*height*4);
        if(getenv("SQ2_YUV_LOG_READBACK_SETTINGS"))quest2_log_readback_settings();
        if(getenv("SQ2_YUV_PBO_READBACK")){
            void *(*map_range)(GLenum,GLintptr,GLsizeiptr,GLbitfield)=(void*)eglGetProcAddress("glMapBufferRange");
            GLboolean (*unmap)(GLenum)=(void*)eglGetProcAddress("glUnmapBuffer");check(map_range&&unmap,"PBO entrypoints");
            GLuint pbo;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER,(size_t)width*height*4,pixels,GL_STREAM_READ);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,NULL);check(glGetError()==GL_NO_ERROR,"PBO readback");
            void *mapped=map_range(GL_PIXEL_PACK_BUFFER,0,(size_t)width*height*4,GL_MAP_READ_BIT);
            check(mapped!=NULL,"PBO map");memcpy(pixels,mapped,(size_t)width*height*4);
            check(unmap(GL_PIXEL_PACK_BUFFER),"PBO unmap");glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pbo);
            puts("YUV_PBO_READBACK");
        }else glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        check(glGetError()==GL_NO_ERROR,"draw/readback");
        for(unsigned i=0;i<4;i++){uint8_t *p=pixels+((size_t)(height/2)*width+i*width/4+width/8)*4;printf("YUV_SAMPLE %u=%u,%u,%u,%u\n",i,p[0],p[1],p[2],p[3]);}
        verify(pixels,width*4,width,height,frame,"glReadPixels");
        if(output_buffer){
            void *cpu=NULL;AHardwareBuffer_Desc actual;AHardwareBuffer_describe(output_buffer,&actual);
            glFinish();check(!AHardwareBuffer_lock(output_buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&cpu),"output CPU lock");
            verify(cpu,actual.stride*4,width,height,frame,"AHB_CPU");check(!AHardwareBuffer_unlock(output_buffer,NULL),"output CPU unlock");
        }
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&input);glDeleteTextures(1,&output);
        if(relay){glDeleteFramebuffers(1,&relay_fbo);glDeleteTextures(1,&relay);glDeleteProgram(relay_prog);glDeleteShader(relay_fs);glUseProgram(prog);}
        if(output_image!=EGL_NO_IMAGE_KHR)check(destroy(d,output_image),"linear output destroy");
        if(output_buffer)AHardwareBuffer_release(output_buffer);
        AHardwareBuffer_release(buffer);
    }
    free(pixels);glDeleteProgram(prog);glDeleteShader(vs);glDeleteShader(fs);
    check(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)&&eglDestroySurface(d,surface)&&eglDestroyContext(d,context)&&eglTerminate(d),"teardown");
    printf("QUEST2_YUV_PASS format=%x width=%u height=%u frames=%u\n",format,width,height,frames);return 0;
}
