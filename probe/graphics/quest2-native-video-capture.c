/* Private native gralloc capture buffers: Venus writes the same dma-buf sampled by EGL.
 * CPU access is read-only validation; no decoded pixels are uploaded/copied to the GPU.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/gralloc1.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "quest2-native-egl-context.h"
#ifdef QUEST2_NATIVE_HWC
#include "quest2-vpu-hwc.h"
#endif
#ifdef QUEST2_NATIVE_MAPPER5
#include "quest2-mapper5-video-buffer.h"
static struct quest2_video_buffer video_buffers[32];
static int dev;
#else
extern uint64_t quest2_native_yuv_geometry(const native_handle_t *,unsigned,unsigned,int,uint64_t *,uint32_t *,uint32_t *,uint32_t *);
#define LOAD(name) GRALLOC1_PFN_##name name=(GRALLOC1_PFN_##name)dev->getFunction(dev,GRALLOC1_FUNCTION_##name);assert(name)
static gralloc1_device_t *dev;
static void *gralloc_library,*layout_library;
#endif
static buffer_handle_t handles[32];
static unsigned image_w,image_h,image_stride,image_scan,minimum_size,decoded_gpu_frames;
static uint64_t usage;
static uint32_t codec_usage;
#ifdef QUEST2_NATIVE_MAPPER5
static uint64_t capture_usage(void) {
    uint64_t value=0x400103ULL|codec_usage;
#ifdef QUEST2_NATIVE_HWC
    value|=AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
#endif
    return value;
}
#endif
/* shortcut: single private decoder; make this per-session before service integration. */
static unsigned source_w,source_h;
static unsigned sample_bytes;
static int image_format;
static void wait_fence(int fd) {
    assert(fd>=0);struct pollfd p={.fd=fd,.events=POLLIN};int r;
    do{r=poll(&p,1,5000);}while(r<0 && errno==EINTR);
    assert(r==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));assert(!close(fd));
}
void quest2_capture_source_size(unsigned *w,unsigned *h) {
    if(source_w && source_h){*w=source_w;*h=source_h;}
}
void quest2_capture_image_size(unsigned *w,unsigned *h) {*w=image_w;*h=image_h;}
int quest2_capture_codec_usage(uint32_t value) {
    if(dev || (value && value!=0x20000000))return -1;
    codec_usage=value;return 0;
}
const void *quest2_capture_handle(unsigned index) {
    return index<32?handles[index]:NULL;
}
int quest2_capture_config(unsigned w,unsigned h,unsigned stride,unsigned scan,unsigned size,unsigned bpp) {
    if(bpp!=1 && bpp!=2)return -1;
    sample_bytes=bpp;image_format=bpp==1?0x7fa30c04:0x7fa30c0a;
#ifdef QUEST2_NATIVE_MAPPER5
    if(dev || !w || !h || w%2 || h%2 || w>4096 || h>4096 || stride<w*bpp || scan<h)return -1;
    dev=1;
#else
    const char *path=getenv("SQ2_NATIVE_GRALLOC");
    if(!path || path[0]!='/' || !w || !h || w%2 || h%2 || w>4096 || h>4096 || stride<w*bpp || scan<h)return -1;
    gralloc_library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!gralloc_library){fprintf(stderr,"CAPTURE_GRALLOC_LOAD_FAIL %s\n",dlerror());return -1;}
    layout_library=dlopen("libadreno_utils.so",RTLD_NOW|RTLD_LOCAL);assert(layout_library);
    hw_module_t *module=dlsym(gralloc_library,HAL_MODULE_INFO_SYM_AS_STR);assert(module && module->tag==HARDWARE_MODULE_TAG);
    assert(!gralloc1_open(module,&dev));
    uint32_t count=0;dev->getCapabilities(dev,&count,NULL);assert(count<=32);
    int32_t caps[32]={0};dev->getCapabilities(dev,&count,caps);int deletes=0;
    for(unsigned i=0;i<count;i++)if(caps[i]==GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE)deletes=1;
    assert(deletes);
#endif
    image_w=w;image_h=h;image_stride=stride;image_scan=scan;minimum_size=size;
    native_gpu_init("#version 300 es\n#extension GL_EXT_YUV_target : require\nprecision highp float;uniform highp __samplerExternal2DY2YEXT source;uniform vec2 image_size;out vec4 color;void main(){color=texture(source,(gl_FragCoord.xy*2.0+vec2(0.5))/image_size);}");
    return 0;
}
int quest2_capture_allocate(unsigned index,int *dmafd,size_t *length) {
    assert(dev && index<32);
    if(handles[index])goto duplicate;
#ifdef QUEST2_NATIVE_MAPPER5
    struct quest2_video_buffer *b=&video_buffers[index];
    if(quest2_video_buffer_allocate(image_w,image_h,image_format,capture_usage(),b))return -1;
    handles[index]=b->handle;
#ifdef QUEST2_NATIVE_HWC
    uint64_t id=0;assert(!AHardwareBuffer_getId(b->ahb,&id));
    struct stat identity;assert(!fstat(b->handle->data[0],&identity));
    struct timespec stamp;assert(!clock_gettime(CLOCK_MONOTONIC,&stamp));
    printf("VPU_ALLOCATION slot=%u id=%llu inode=%llu width=%u height=%u capacity=%llu time_ns=%lld\n",
        index,(unsigned long long)id,(unsigned long long)identity.st_ino,image_w,image_h,
        (unsigned long long)b->capacity,(long long)stamp.tv_sec*1000000000+stamp.tv_nsec);
#endif
    uint64_t capacity=b->capacity;uint32_t ystride=b->ystride,cstride=b->cstride,uv=b->uv;
    usage=b->usage;
#else
    LOAD(CREATE_DESCRIPTOR);LOAD(DESTROY_DESCRIPTOR);LOAD(SET_DIMENSIONS);LOAD(SET_FORMAT);
    LOAD(SET_LAYER_COUNT);LOAD(SET_PRODUCER_USAGE);LOAD(SET_CONSUMER_USAGE);LOAD(ALLOCATE);
    gralloc1_buffer_descriptor_t descriptor=0;assert(!CREATE_DESCRIPTOR(dev,&descriptor));
    assert(!SET_DIMENSIONS(dev,descriptor,image_w,image_h));assert(!SET_FORMAT(dev,descriptor,image_format));
    assert(!SET_LAYER_COUNT(dev,descriptor,1));
    assert(!SET_PRODUCER_USAGE(dev,descriptor,GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER|codec_usage));
    assert(!SET_CONSUMER_USAGE(dev,descriptor,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN|GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE));
    int error=ALLOCATE(dev,1,&descriptor,&handles[index]);assert(!DESTROY_DESCRIPTOR(dev,descriptor));
    if(error || !handles[index]){fprintf(stderr,"CAPTURE_NATIVE_ALLOC_FAIL result=%d\n",error);return -1;}
    uint64_t capacity=0;uint32_t ystride=0,cstride=0,uv=0;
    usage=quest2_native_yuv_geometry(handles[index],image_w,image_h,image_format,&capacity,&ystride,&cstride,&uv);
#endif
    struct stat st;assert(handles[index]->numFds==2 && !fstat(handles[index]->data[0],&st));
    printf("NATIVE_CAPTURE_ALLOCATION index=%u y_stride=%u uv_stride=%u uv_offset=%u capacity=%llu required=%u fd_bytes=%lld usage=0x%llx\n",
        index,ystride,cstride,uv,(unsigned long long)capacity,minimum_size,(long long)st.st_size,(unsigned long long)usage);
    if(ystride!=cstride || !ystride || uv%ystride || uv/ystride<image_h ||
       capacity<minimum_size || capacity>(uint64_t)st.st_size || st.st_size>16*1024*1024)return -1;
    if(index==0){image_stride=ystride;image_scan=uv/ystride;}
    else if(ystride!=image_stride || uv/ystride!=image_scan)return -1;
duplicate:
    *dmafd=fcntl(handles[index]->data[0],F_DUPFD_CLOEXEC,0);if(*dmafd<0)return -1;
    struct stat original;assert(!fstat(handles[index]->data[0],&original));
    struct stat imported;assert(!fstat(*dmafd,&imported) && imported.st_dev==original.st_dev && imported.st_ino==original.st_ino);
    *length=(size_t)original.st_size;return 0;
}
int quest2_capture_layout(unsigned *stride,unsigned *scan,unsigned *capacity) {
    int pixel=-1;size_t length=0;
    if(quest2_capture_allocate(0,&pixel,&length))return -1;
    assert(!close(pixel));*stride=image_stride;*scan=image_scan;*capacity=(unsigned)length;
    return 0;
}
/* Called by the opt-in bridge only after SOURCE_CHANGE and queue release.
 * A temporary original allocation determines the actual layout for that crop. */
int quest2_omx_capture_layout(unsigned w,unsigned h,unsigned format,
                              unsigned *stride,unsigned *scan,unsigned *size) {
    if(!dev || codec_usage!=0x20000000 || format!=0x3231564e || !w || !h ||
       w>4096 || h>4096 || (w&1) || (h&1))return -1;
#ifdef QUEST2_NATIVE_MAPPER5
    struct quest2_video_buffer b={0};
    if(quest2_video_buffer_allocate(w,h,0x7fa30c04,capture_usage(),&b))return -1;
    const native_handle_t *handle=b.handle;
    uint64_t capacity=b.capacity;uint32_t ys=b.ystride,cs=b.cstride,uv=b.uv;
#else
    LOAD(CREATE_DESCRIPTOR);LOAD(DESTROY_DESCRIPTOR);LOAD(SET_DIMENSIONS);LOAD(SET_FORMAT);
    LOAD(SET_LAYER_COUNT);LOAD(SET_PRODUCER_USAGE);LOAD(SET_CONSUMER_USAGE);LOAD(ALLOCATE);LOAD(RELEASE);
    gralloc1_buffer_descriptor_t d=0;buffer_handle_t handle=NULL;
    assert(!CREATE_DESCRIPTOR(dev,&d));assert(!SET_DIMENSIONS(dev,d,w,h));
    assert(!SET_FORMAT(dev,d,0x7fa30c04));assert(!SET_LAYER_COUNT(dev,d,1));
    assert(!SET_PRODUCER_USAGE(dev,d,GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER|codec_usage));
    assert(!SET_CONSUMER_USAGE(dev,d,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN|GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE));
    int result=ALLOCATE(dev,1,&d,&handle);assert(!DESTROY_DESCRIPTOR(dev,d));
    if(result || !handle){if(handle)assert(!RELEASE(dev,handle));return -1;}
    uint64_t capacity=0;uint32_t ys=0,cs=0,uv=0;
    quest2_native_yuv_geometry(handle,w,h,0x7fa30c04,&capacity,&ys,&cs,&uv);
#endif
    struct stat st;assert(!fstat(handle->data[0],&st));
    int valid=ys && ys==cs && !(uv%ys) && uv/ys>=h &&
        capacity<=16*1024*1024 && capacity<=(uint64_t)st.st_size;
    if(valid){*stride=ys;*scan=uv/ys;*size=(unsigned)capacity;source_w=w;source_h=h;}
#ifdef QUEST2_NATIVE_MAPPER5
    quest2_video_buffer_release(&b);
#else
    assert(!RELEASE(dev,handle));
#endif
    return valid?0:-1;
}
int quest2_capture_sample(unsigned index,const uint8_t *reference,size_t offset) {
    assert(dev && index<32 && handles[index] && reference && offset==0);
#ifdef QUEST2_NATIVE_MAPPER5
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client=(void*)eglGetProcAddress("eglGetNativeClientBufferANDROID");assert(client);
    EGLClientBuffer native_buffer=client(video_buffers[index].ahb);assert(native_buffer);
#else
    LOAD(GET_STRIDE);uint32_t stride=0;assert(!GET_STRIDE(dev,handles[index],&stride));
    struct native_gpu_buffer native={.buffer={.common={.magic=ANDROID_NATIVE_BUFFER_MAGIC,
        .version=sizeof(ANativeWindowBuffer_t),.incRef=native_inc,.decRef=native_dec},
        .width=(int)image_w,.height=(int)image_h,.stride=(int)stride,.format=image_format,.usage=(int)usage,.handle=handles[index]},.refs=1};
    EGLClientBuffer native_buffer=&native.buffer;
#endif
    const EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
    EGLImageKHR image=create_image(gpu_display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native_buffer,attrs);assert(image!=EGL_NO_IMAGE_KHR);
    GLuint input,output,fbo;glGenTextures(1,&input);glBindTexture(GL_TEXTURE_EXTERNAL_OES,input);image_texture(GL_TEXTURE_EXTERNAL_OES,image);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_EXTERNAL_OES,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    unsigned width=image_w/2,height=image_h/2;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA16F,width,height);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_EXTERNAL_OES,input);glUseProgram(gpu_program);
    glUniform1i(glGetUniformLocation(gpu_program,"source"),0);glUniform2f(glGetUniformLocation(gpu_program,"image_size"),(float)image_w,(float)image_h);
    glDisable(GL_DITHER);glViewport(0,0,width,height);glDrawArrays(GL_TRIANGLES,0,3);
    float *pixels=malloc((size_t)width*height*4*sizeof(float));assert(pixels);
    glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels);assert(glGetError()==GL_NO_ERROR);
    const EGLint sync_attrs[]={EGL_NONE};EGLSyncKHR sync=create_sync(gpu_display,EGL_SYNC_NATIVE_FENCE_ANDROID,sync_attrs);assert(sync!=EGL_NO_SYNC_KHR);
    glFlush();int fence=duplicate_fence(gpu_display,sync);assert(destroy_sync(gpu_display,sync));wait_fence(fence);
    unsigned bad=0;float maximum=0;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
        const uint8_t *uv=reference+(size_t)image_stride*image_scan+(size_t)y*image_stride+x*2*sample_bytes;
        const uint8_t *yp=reference+(size_t)(y*2+1)*image_stride+(x*2+1)*sample_bytes;
        unsigned maximum_code=sample_bytes==1?255:1023;
        unsigned codes[4]={yp[0],uv[0],uv[1],maximum_code};
        if(sample_bytes==2) {
            uint16_t yy,uu,vv;memcpy(&yy,yp,2);memcpy(&uu,uv,2);memcpy(&vv,uv+2,2);
            assert(!(yy&63) && !(uu&63) && !(vv&63));
            codes[0]=yy>>6;codes[1]=uu>>6;codes[2]=vv>>6;
        }
        for(unsigned c=0;c<4;c++) {
            float actual=pixels[((size_t)y*width+x)*4+c],expected=codes[c]/(float)maximum_code,difference=fabsf(actual-expected);
            if(difference>maximum)maximum=difference;
            if(!isfinite(actual)||difference>0.00055f){if(bad<12)printf("NATIVE_CAPTURE_GPU_MISMATCH x=%u y=%u c=%u code=%u actual=%.7f expected=%.7f\n",x,y,c,codes[c],actual,expected);bad++;}
        }
    }
    free(pixels);glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&input);glDeleteTextures(1,&output);
    assert(destroy_image(gpu_display,image));
#ifndef QUEST2_NATIVE_MAPPER5
    assert(atomic_load(&native.refs)==1);
#endif
    printf("NATIVE_CAPTURE_GPU_FRAME index=%u bad=%u max_error=%.8f shared_dma_buf=1 gpu_fence=1\n",index,bad,maximum);
    if(bad)return -1;
    decoded_gpu_frames++;return 0;
}
#ifdef QUEST2_NATIVE_HWC
int quest2_capture_display(unsigned index,int64_t pts,int *released) {
    assert(index<32 && video_buffers[index].ahb);
    uint64_t id=0;assert(!AHardwareBuffer_getId(video_buffers[index].ahb,&id));
    return quest2_hwc_video_present(index,video_buffers[index].handle,id,image_w,image_h,pts,released);
}
int quest2_capture_display_flush(int *released) { return quest2_hwc_video_flush(released); }
int quest2_capture_queue(unsigned index) {
    assert(index<32 && video_buffers[index].ahb);
    uint64_t id=0;assert(!AHardwareBuffer_getId(video_buffers[index].ahb,&id));
    struct stat st;assert(!fstat(video_buffers[index].handle->data[0],&st));
    struct timespec stamp;assert(!clock_gettime(CLOCK_MONOTONIC,&stamp));
    printf("VPU_REFILL slot=%u id=%llu inode=%llu time_ns=%lld\n",index,
        (unsigned long long)id,(unsigned long long)st.st_ino,
        (long long)stamp.tv_sec*1000000000+stamp.tv_nsec);
    return 0;
}
#endif
void quest2_capture_finish(void) {
    if(!dev)return;
#ifdef QUEST2_NATIVE_HWC
    int released=-1;assert(!quest2_hwc_video_flush(&released) && released==-1);
#endif
#ifdef QUEST2_NATIVE_MAPPER5
    quest2_video_buffer_check_maps();
    for(unsigned i=0;i<32;i++){
#ifdef QUEST2_NATIVE_HWC
        uint64_t id=0;struct stat st={0};
        if(video_buffers[i].ahb) {
            assert(!AHardwareBuffer_getId(video_buffers[i].ahb,&id));
            assert(!fstat(video_buffers[i].handle->data[0],&st));
        }
#endif
        quest2_video_buffer_release(&video_buffers[i]);handles[i]=NULL;
#ifdef QUEST2_NATIVE_HWC
        if(id) {
            struct timespec stamp;assert(!clock_gettime(CLOCK_MONOTONIC,&stamp));
            printf("VPU_FREE slot=%u id=%llu inode=%llu time_ns=%lld\n",i,
                (unsigned long long)id,(unsigned long long)st.st_ino,
                (long long)stamp.tv_sec*1000000000+stamp.tv_nsec);
        }
#endif
    }
    native_gpu_finish();dev=0;
#else
    LOAD(RELEASE);
    for(unsigned i=0;i<32;i++)if(handles[i]){assert(!RELEASE(dev,handles[i]));handles[i]=NULL;}
    native_gpu_finish();assert(!gralloc1_close(dev));dev=NULL;dlclose(layout_library);dlclose(gralloc_library);
#endif
    printf("NATIVE_CAPTURE_GPU_FINISHED frames=%u\n",decoded_gpu_frames);
}
