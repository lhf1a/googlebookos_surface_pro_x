/* System EGL -> real ANativeWindow/BufferQueue -> ImageReader consumer.
 * Run only inside the private EGL mount namespace. No SurfaceFlinger switch. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *what){
    fprintf(stderr,"GLES_WINDOW_FAIL %s EGL=0x%x GL=0x%x\n",what,eglGetError(),glGetError());exit(1);
}
static int option(const char *name,int fallback,int max){
    const char *s=getenv(name);if(!s)return fallback;
    char *end=NULL;long value=strtol(s,&end,10);
    if(!*s||*end||value<1||value>max){fprintf(stderr,"invalid %s\n",name);exit(2);}
    return (int)value;
}
static GLuint shader(GLenum type,const char *source){
    GLuint result=glCreateShader(type);glShaderSource(result,1,&source,NULL);glCompileShader(result);
    GLint ok=0;glGetShaderiv(result,GL_COMPILE_STATUS,&ok);if(!ok)fail("compile shader");return result;
}
static int has_extension(const char *extensions,const char *name){
    if(!extensions)return 0;
    size_t n=strlen(name);const char *p=extensions;
    while((p=strstr(p,name))){
        if((p==extensions||p[-1]==' ')&&(p[n]==' '||p[n]=='\0'))return 1;
        p+=n;
    }
    return 0;
}
static unsigned mismatches(const uint8_t *pixels,int width,int height,int stride,int phase,int flip){
    unsigned bad=0;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        int gy=flip?height-1-y:y;
        const uint8_t expected[]={x>=width/2?255:0,gy>=height/2?255:0,phase?255:0,255};
        if(memcmp(pixels+(size_t)y*stride+x*4,expected,4))bad++;
    }
    return bad;
}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    const int width=option("SQ2_WINDOW_WIDTH",64,4096),height=option("SQ2_WINDOW_HEIGHT",64,4096);
    const int frames=option("SQ2_WINDOW_FRAMES",12,300),cycles=option("SQ2_WINDOW_CYCLES",1,100);
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;
    if(!eglInitialize(display,&major,&minor))fail("initialize");
    const char *extensions=eglQueryString(EGL_NO_DISPLAY,EGL_EXTENSIONS);
    printf("WINDOW_CLIENT_EXTENSIONS=%s\n",extensions?extensions:"(none)");
    const char *entry=getenv("SQ2_WINDOW_ENTRY");
    if(entry&&!strcmp(entry,"ext")&&!has_extension(extensions,"EGL_EXT_platform_base")){
        puts("WINDOW_ENTRY_UNSUPPORTED EGL_EXT_platform_base is not advertised; not a pass");
        if(!eglTerminate(display))fail("unsupported entry teardown");
        return 77;
    }
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count,format;
    if(!eglChooseConfig(display,attributes,&config,1,&count)||count!=1||
       !eglGetConfigAttrib(display,config,EGL_NATIVE_VISUAL_ID,&format)||format!=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM)
        fail("RGBA window config");
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
    if(context==EGL_NO_CONTEXT)fail("context");
    uint8_t *readback=malloc((size_t)width*height*4);if(!readback)fail("allocate readback");
    unsigned total_fences=0,total_frames=0;
    for(int cycle=0;cycle<cycles;cycle++){
        AImageReader *reader=NULL;ANativeWindow *window=NULL;
        uint64_t usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
        if(AImageReader_newWithUsage(width,height,AIMAGE_FORMAT_RGBA_8888,usage,4,&reader)!=AMEDIA_OK||
           AImageReader_getWindow(reader,&window)!=AMEDIA_OK)fail("ImageReader window");
        printf("WINDOW_CREATE cycle=%d width=%d height=%d\n",cycle,width,height);
        unsigned native_header[2];memcpy(native_header,window,sizeof(native_header));
        printf("WINDOW_ABI magic=%x size=%u\n",native_header[0],native_header[1]);
        EGLSurface surface=EGL_NO_SURFACE;
        if(!entry||!strcmp(entry,"legacy"))surface=eglCreateWindowSurface(display,config,window,NULL);
        else if(!strcmp(entry,"core"))surface=eglCreatePlatformWindowSurface(display,config,window,NULL);
        else if(!strcmp(entry,"ext")){
            PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create=(void*)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
            if(!create)fail("EXT platform window entry");
            Dl_info ext_origin={0};if(dladdr((void*)create,&ext_origin))printf("WINDOW_EXT_ENTRY=%s\n",ext_origin.dli_fname);
            surface=create(display,config,window,NULL);
        }else fail("unknown window entry");
        if(surface==EGL_NO_SURFACE||!eglMakeCurrent(display,surface,surface,context))fail("window surface/current");
        const char *renderer=(const char*)glGetString(GL_RENDERER),*version=(const char*)glGetString(GL_VERSION);
        printf("WINDOW_RENDERER=%s VERSION=%s\n",renderer,version);
        if(!renderer||!strstr(renderer,"Adreno (TM) 680")||!version||!strstr(version,"V@0819.0")||strstr(version,"Mesa"))
            fail("expected closed renderer");
        Dl_info origin={0};
        if(!dladdr((void*)eglSwapBuffers,&origin)||!strstr(origin.dli_fname,"/system/lib64/libEGL.so"))fail("system dispatch");
        printf("WINDOW_SWAP_ENTRY=%s\n",origin.dli_fname);
        GLuint vs=shader(GL_VERTEX_SHADER,"attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}");
        GLuint fs=shader(GL_FRAGMENT_SHADER,"precision highp float;uniform vec2 halfSize;uniform float phase;void main(){gl_FragColor=vec4(step(halfSize.x,gl_FragCoord.x),step(halfSize.y,gl_FragCoord.y),phase,1.);}");
        GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
        glBindAttribLocation(program,0,"p");glLinkProgram(program);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(!linked)fail("link");glUseProgram(program);
        const GLfloat vertices[]={-1,-1,3,-1,-1,3};glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices);glEnableVertexAttribArray(0);
        glUniform2f(glGetUniformLocation(program,"halfSize"),width*0.5f,height*0.5f);
        glViewport(0,0,width,height);glDisable(GL_DITHER);
        int orientation=-1;uint64_t buffer_ids[16];unsigned id_count=0;AImage *held_image=NULL;
        for(int frame=0;frame<frames;frame++){
            glUniform1f(glGetUniformLocation(program,"phase"),(float)(frame&1));glDrawArrays(GL_TRIANGLES,0,3);
            if(frame<3){
                memset(readback,0x5a,(size_t)width*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,readback);
                if(mismatches(readback,width,height,width*4,frame&1,0))fail("window glReadPixels");
            }
            if(glGetError()!=GL_NO_ERROR||!eglSwapBuffers(display,surface))fail("swap");
            AImage *image=NULL;int fence=-1;media_status_t acquired=AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
            for(unsigned attempt=0;attempt<100 && acquired==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;attempt++){
                acquired=AImageReader_acquireNextImageAsync(reader,&image,&fence);
                if(acquired==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)usleep(10000);
            }
            if(acquired!=AMEDIA_OK||!image)fail("acquire queued image");
            if(fence>=0){
                struct pollfd pending={.fd=fence,.events=POLLIN};int result;
                do{result=poll(&pending,1,2000);}while(result<0&&errno==EINTR);
                close(fence);if(result!=1||!(pending.revents&POLLIN))fail("producer fence");total_fences++;
            }
            int32_t w=0,h=0,planes=0,row=0,pixel=0,length=0;uint8_t *data=NULL;AHardwareBuffer *buffer=NULL;uint64_t id=0;
            if(AImage_getWidth(image,&w)||AImage_getHeight(image,&h)||AImage_getNumberOfPlanes(image,&planes)||
               AImage_getPlaneRowStride(image,0,&row)||AImage_getPlanePixelStride(image,0,&pixel)||
               AImage_getPlaneData(image,0,&data,&length)||AImage_getHardwareBuffer(image,&buffer)||
               AHardwareBuffer_getId(buffer,&id)||w!=width||h!=height||planes!=1||pixel!=4||
               row<width*4||length<(height-1)*row+width*4)fail("consumer image layout");
            unsigned i=0;for(;i<id_count;i++)if(buffer_ids[i]==id)break;
            if(i==id_count){if(id_count==16)fail("unbounded buffer churn");buffer_ids[id_count++]=id;}
            if(orientation<0){
                if(!mismatches(data,width,height,row,frame&1,0))orientation=0;
                else if(!mismatches(data,width,height,row,frame&1,1))orientation=1;
                else fail("first consumer pixels");
            }
            if(mismatches(data,width,height,row,frame&1,orientation))fail("consumer pixel mismatch");
            /* Retain the previous image until the next one is acquired. This
             * forces actual buffer rotation instead of immediate same-slot reuse. */
            if(held_image)AImage_delete(held_image);
            held_image=image;total_frames++;
            printf("WINDOW_FRAME_OK cycle=%d frame=%d buffer=%llu row=%d flip_y=%d\n",cycle,frame,(unsigned long long)id,row,orientation);
        }
        if(held_image)AImage_delete(held_image);
        glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
        if(getenv("SQ2_WINDOW_DESTROY_CURRENT")){
            if(!eglDestroySurface(display,surface))fail("destroy current surface");
            /* EGL 1.5 sections 3.5.5/3.2: post-destroy rendering is undefined.
             * Exercise deferred resource release, not an invalid pixel oracle. */
            if(!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT))fail("release destroyed current surface");
            puts("WINDOW_DEFERRED_DESTROY_OK");
        }else if(!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT)||!eglDestroySurface(display,surface))fail("surface teardown");
        AImageReader_delete(reader);
        if(frames>=3&&id_count<2)fail("no buffer rotation observed");
        printf("WINDOW_CYCLE_OK cycle=%d buffers=%u\n",cycle,id_count);
    }
    if(!eglDestroyContext(display,context)||!eglTerminate(display))fail("EGL teardown");
    free(readback);
    printf("GLES_WINDOW_PASS cycles=%d frames=%u fences=%u\n",cycles,total_frames,total_fences);
    return 0;
}
