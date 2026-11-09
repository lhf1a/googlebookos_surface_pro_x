/* Donor-native RGB EGLImage read/write and explicit fence verification. */
#include "quest2-native-egl-context.h"
static const char *native_rgb_fragment="#version 300 es\nprecision highp float;uniform highp sampler2D source;out vec4 color;void main(){color=texelFetch(source,ivec2(gl_FragCoord.xy),0);}";
static void native_gpu_check(gralloc1_device_t *dev, buffer_handle_t handle,
                             unsigned w,unsigned h,unsigned stride,unsigned cycle,int format) {
    uint64_t usage=quest2_native_mapper_check(handle,w,h,stride,format);
    unsigned bpp=pixel_bytes(format);
    GLenum internal=format==HAL_PIXEL_FORMAT_RGBA_FP16?GL_RGBA16F:format==HAL_PIXEL_FORMAT_RGBA_1010102?GL_RGB10_A2:GL_RGBA8;
    GLenum pixel_type=format==HAL_PIXEL_FORMAT_RGBA_FP16?GL_HALF_FLOAT:format==HAL_PIXEL_FORMAT_RGBA_1010102?GL_UNSIGNED_INT_2_10_10_10_REV:GL_UNSIGNED_BYTE;
    struct native_gpu_buffer native={.buffer={.common={.magic=ANDROID_NATIVE_BUFFER_MAGIC,
        .version=sizeof(ANativeWindowBuffer_t),.incRef=native_inc,.decRef=native_dec},
        .width=(int)w,.height=(int)h,.stride=(int)stride,.format=format,
        .usage=(int)usage,.handle=handle},.refs=1};
    const EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
    EGLImageKHR image=create_image(gpu_display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,&native.buffer,attrs);
    if(image==EGL_NO_IMAGE_KHR)fprintf(stderr,"NATIVE_IMAGE_FAIL error=%x\n",eglGetError());
    assert(image!=EGL_NO_IMAGE_KHR);
    GLuint textures[2],fbo;glGenTextures(2,textures);
    glBindTexture(GL_TEXTURE_2D,textures[0]);image_texture(GL_TEXTURE_2D,image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexStorage2D(GL_TEXTURE_2D,1,internal,(GLsizei)w,(GLsizei)h);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
    glUseProgram(gpu_program);glUniform1i(glGetUniformLocation(gpu_program,"source"),0);
    glDisable(GL_DITHER);glViewport(0,0,(GLsizei)w,(GLsizei)h);glDrawArrays(GL_TRIANGLES,0,3);
    uint8_t *pixels=malloc((size_t)w*h*bpp);assert(pixels);
    glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,pixel_type,pixels);
    assert(glGetError()==GL_NO_ERROR);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        uint8_t expected[8];pattern_pixel(format,x,y,cycle,expected);
        assert(!memcmp(pixels+((size_t)y*w+x)*bpp,expected,bpp));
    }
    free(pixels);
    glBindTexture(GL_TEXTURE_2D,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    uint8_t clear[8]={(uint8_t)(31+cycle*11),97,193,255};
    if(format==HAL_PIXEL_FORMAT_RGBA_1010102) {
        uint32_t word=341|(682<<10)|(1023<<20)|(3u<<30);memcpy(clear,&word,4);
        glClearColor(1.f/3,2.f/3,1,1);
    } else if(format==HAL_PIXEL_FORMAT_RGBA_FP16) {
        const uint16_t words[]={0x3400,0x3800,0x3a00,0x3c00};memcpy(clear,words,8);
        glClearColor(.25f,.5f,.75f,1);
    } else glClearColor(clear[0]/255.f,clear[1]/255.f,clear[2]/255.f,1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    assert(glGetError()==GL_NO_ERROR);
    const EGLint sync_attrs[]={EGL_NONE};
    EGLSyncKHR sync=create_sync(gpu_display,EGL_SYNC_NATIVE_FENCE_ANDROID,sync_attrs);assert(sync!=EGL_NO_SYNC_KHR);
    glFlush();int fence=duplicate_fence(gpu_display,sync);assert(fence>=0);
    assert(destroy_sync(gpu_display,sync));wait_fence(fence);
    LOAD(LOCK);LOAD(UNLOCK);
    gralloc1_rect_t rect={0,0,(int32_t)w,(int32_t)h};void *address=NULL;
    assert(!LOCK(dev,handle,GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&address,-1) && address);
    uint8_t *bytes=address;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        assert(!memcmp(bytes+((size_t)y*stride+x)*bpp,clear,bpp));
        pattern_pixel(format,x,y,cycle,bytes+((size_t)y*stride+x)*bpp);
    }
    fence=-1;assert(!UNLOCK(dev,handle,&fence));wait_fence(fence);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,textures);
    assert(destroy_image(gpu_display,image));assert(atomic_load(&native.refs)==1);
    assert(glGetError()==GL_NO_ERROR);
    printf("NATIVE_GPU_PASS cycle=%u width=%u height=%u format=%d shader_sample=1 gpu_write=1 native_fence=1 cpu_readback=1 refs=1\n",cycle,w,h,format);
}
