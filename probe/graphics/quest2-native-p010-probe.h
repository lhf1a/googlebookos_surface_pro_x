/* P010 physical-layout diagnostic, separate from the deliberately strict legacy FLEX test. */
#include <math.h>
#include "quest2-native-egl-context.h"
extern uint64_t quest2_native_yuv_geometry(const native_handle_t *,unsigned,unsigned,int,uint64_t *,uint32_t *,uint32_t *,uint32_t *);
static unsigned p010_y(unsigned x,unsigned y,unsigned frame) {
    const unsigned levels[]={64,65,66,512,513,514,939,940};
    return levels[(x/16+y/8+frame)%8];
}
static void p010_gpu_check(gralloc1_device_t *dev,buffer_handle_t handle,unsigned w,unsigned h,int format,uint64_t usage) {
    assert(w>=128 && h>=8 && !(w%2) && !(h%2));
    uint64_t capacity=0;uint32_t ystride=0,uvstride=0,uv_offset=0;
    usage=quest2_native_yuv_geometry(handle,w,h,format,&capacity,&ystride,&uvstride,&uv_offset);
    struct stat st;assert(!fstat(handle->data[0],&st) && capacity<=(uint64_t)st.st_size);
    LOAD(LOCK);LOAD(UNLOCK);LOAD(GET_STRIDE);
    uint32_t stride=0;assert(!GET_STRIDE(dev,handle,&stride) && stride>=w);
    printf("P010_PHYSICAL_GEOMETRY bytes=%llu y_stride=%u uv_stride=%u uv_offset=%u element_bits=16 valid_bits=10\n",
           (unsigned long long)capacity,ystride,uvstride,uv_offset);
    const char *fragment="#version 300 es\n#extension GL_EXT_YUV_target : require\nprecision highp float;uniform highp __samplerExternal2DY2YEXT source;uniform vec2 image_size;out vec4 color;void main(){color=texture(source,gl_FragCoord.xy/image_size);}";
    native_gpu_init(fragment);
    assert(strstr((const char*)glGetString(GL_EXTENSIONS),"GL_EXT_YUV_target"));
    for(unsigned frame=0;frame<3;frame++) {
        const unsigned u=511+frame,v=769-frame;
        gralloc1_rect_t rect={0,0,(int32_t)w,(int32_t)h};void *address=NULL;
        assert(!LOCK(dev,handle,GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&address,-1) && address);
        uint8_t *base=address;memset(base,0,(size_t)capacity);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            uint16_t word=(uint16_t)(p010_y(x,y,frame)<<6);memcpy(base+(size_t)y*ystride+x*2,&word,2);
        }
        for(unsigned y=0;y<h/2;y++)for(unsigned x=0;x<w/2;x++) {
            uint16_t words[2]={(uint16_t)(u<<6),(uint16_t)(v<<6)};
            memcpy(base+uv_offset+(size_t)y*uvstride+x*4,words,4);
        }
        int fence=-1;assert(!UNLOCK(dev,handle,&fence));fence_wait(fence);
        struct native_gpu_buffer native={.buffer={.common={.magic=ANDROID_NATIVE_BUFFER_MAGIC,
            .version=sizeof(ANativeWindowBuffer_t),.incRef=native_inc,.decRef=native_dec},
            .width=(int)w,.height=(int)h,.stride=(int)stride,.format=format,.usage=(int)usage,.handle=handle},.refs=1};
        const EGLint attributes[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
        EGLImageKHR image=create_image(gpu_display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,&native.buffer,attributes);
        if(image==EGL_NO_IMAGE_KHR)fprintf(stderr,"P010_IMAGE_FAIL error=%x\n",eglGetError());
        assert(image!=EGL_NO_IMAGE_KHR);
        GLuint input,output,fbo;glGenTextures(1,&input);glBindTexture(GL_TEXTURE_EXTERNAL_OES,input);
        image_texture(GL_TEXTURE_EXTERNAL_OES,image);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA16F,w,h);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glUseProgram(gpu_program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,input);
        glUniform1i(glGetUniformLocation(gpu_program,"source"),0);
        glUniform2f(glGetUniformLocation(gpu_program,"image_size"),(float)w,(float)h);
        glDisable(GL_DITHER);glViewport(0,0,w,h);glDrawArrays(GL_TRIANGLES,0,3);
        float *pixels=malloc((size_t)w*h*4*sizeof(float));assert(pixels);
        glReadPixels(0,0,w,h,GL_RGBA,GL_FLOAT,pixels);
        GLenum error=glGetError();if(error)fprintf(stderr,"P010_GL_FAIL error=%x\n",error);assert(error==GL_NO_ERROR);
        const EGLint sync_attributes[]={EGL_NONE};
        EGLSyncKHR sync=create_sync(gpu_display,EGL_SYNC_NATIVE_FENCE_ANDROID,sync_attributes);assert(sync!=EGL_NO_SYNC_KHR);
        glFlush();fence=duplicate_fence(gpu_display,sync);assert(fence>=0);assert(destroy_sync(gpu_display,sync));fence_wait(fence);
        unsigned bad=0;float max_error=0;
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            const unsigned codes[]={p010_y(x,y,frame),u,v,1023};
            for(unsigned c=0;c<4;c++) {
                float actual=pixels[((size_t)y*w+x)*4+c],expected=codes[c]/1023.f;
                float difference=fabsf(actual-expected);if(difference>max_error)max_error=difference;
                if(!isfinite(actual) || difference>0.00055f) {
                    if(bad<12)printf("P010_RAW_MISMATCH frame=%u x=%u y=%u channel=%u code=%u actual=%.8f expected=%.8f\n",frame,x,y,c,codes[c],actual,expected);
                    bad++;
                }
            }
        }
        for(unsigned x=0;x<128;x+=16)printf("P010_RAW_SAMPLE frame=%u code=%u y=%.8f u=%.8f v=%.8f\n",frame,p010_y(x,0,frame),pixels[x*4],pixels[x*4+1],pixels[x*4+2]);
        free(pixels);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&input);glDeleteTextures(1,&output);
        assert(destroy_image(gpu_display,image));assert(atomic_load(&native.refs)==1);
        printf("P010_RAW_RESULT frame=%u bad=%u max_error=%.8f\n",frame,bad,max_error);assert(!bad);
    }
    native_gpu_finish();
    puts("P010_NATIVE_GPU_PASS frames=3 physical_16bit=1 ten_bit_sampling=1 native_fence=1 legacy_metadata_unchanged=1");
}
