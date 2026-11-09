/* Original gralloc allocation into OEM OMX UseBuffer2; no fabricated handles. */
#define _GNU_SOURCE
#include <OMX_Component.h>
#include <assert.h>
#include <dlfcn.h>
#include <hardware/gralloc1.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#define LOAD(name) GRALLOC1_PFN_##name name=(GRALLOC1_PFN_##name)dev->getFunction(dev,GRALLOC1_FUNCTION_##name);assert(name)
int quest2_omx_native_import(OMX_HANDLETYPE component_handle,OMX_COMPONENTTYPE *component,
                            OMX_PARAM_PORTDEFINITIONTYPE *port,uint32_t codec_usage) {
    const char *path=getenv("SQ2_NATIVE_GRALLOC");
    if(!path || path[0]!='/' || codec_usage!=0x20000000 || port->format.video.eColorFormat!=0x7fa30c04)return 15;
    void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);assert(library);
    void *layout_library=dlopen("libadreno_utils.so",RTLD_NOW|RTLD_LOCAL);assert(layout_library);
    hw_module_t *module=dlsym(library,HAL_MODULE_INFO_SYM_AS_STR);assert(module && module->tag==HARDWARE_MODULE_TAG);
    gralloc1_device_t *dev=NULL;assert(!gralloc1_open(module,&dev));
    uint32_t count=0;dev->getCapabilities(dev,&count,NULL);assert(count<=32);
    int32_t caps[32]={0};dev->getCapabilities(dev,&count,caps);int deletes=0;
    for(unsigned i=0;i<count;i++)if(caps[i]==GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE)deletes=1;
    assert(deletes);
    LOAD(CREATE_DESCRIPTOR);LOAD(DESTROY_DESCRIPTOR);LOAD(SET_DIMENSIONS);LOAD(SET_FORMAT);
    LOAD(SET_LAYER_COUNT);LOAD(SET_PRODUCER_USAGE);LOAD(SET_CONSUMER_USAGE);LOAD(ALLOCATE);LOAD(RELEASE);
    gralloc1_buffer_descriptor_t descriptor=0;assert(!CREATE_DESCRIPTOR(dev,&descriptor));
    assert(!SET_DIMENSIONS(dev,descriptor,port->format.video.nFrameWidth,port->format.video.nFrameHeight));
    assert(!SET_FORMAT(dev,descriptor,port->format.video.eColorFormat));assert(!SET_LAYER_COUNT(dev,descriptor,1));
    assert(!SET_PRODUCER_USAGE(dev,descriptor,GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER|codec_usage));
    assert(!SET_CONSUMER_USAGE(dev,descriptor,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN|GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE));
    buffer_handle_t handle=NULL;int error=ALLOCATE(dev,1,&descriptor,&handle);
    assert(!DESTROY_DESCRIPTOR(dev,descriptor));
    if(error || !handle){
        fprintf(stderr,"NATIVE_IMPORT_ALLOC_FAIL error=%d\n",error);
        if(handle)assert(!RELEASE(dev,handle));
        assert(!gralloc1_close(dev));dlclose(layout_library);dlclose(library);return 16;
    }
    struct stat original;assert(handle->numFds==2 && !fstat(handle->data[0],&original));
    assert(original.st_size>=port->nBufferSize && original.st_size<=16*1024*1024);
    OMX_BUFFERHEADERTYPE *buffer=NULL;
    OMX_ERRORTYPE e=component->UseBuffer(component_handle,&buffer,port->nPortIndex,
                                        &original,(OMX_U32)original.st_size,(OMX_U8 *)handle);
    printf("NATIVE_USE_BUFFER result=0x%08x bytes=%lld fd=%d fd_count=%d ints=%d\n",
           (unsigned)e,(long long)original.st_size,handle->data[0],handle->numFds,handle->numInts);
    int result=e==OMX_ErrorNone?0:17;
    if(e==OMX_ErrorNone) {
        assert(buffer && buffer->pAppPrivate==&original && buffer->nAllocLen>0);
        printf("NATIVE_IMPORTED_HEADER bytes=%u handle_pointer_preserved=%u\n",buffer->nAllocLen,buffer->pBuffer==(OMX_U8 *)handle);
        e=component->FreeBuffer(component_handle,port->nPortIndex,buffer);
        printf("NATIVE_FREE_BUFFER result=0x%08x\n",(unsigned)e);
        /* On failure leave ownership intact until OMX_FreeHandle/process exit. */
        if(e!=OMX_ErrorNone)return 18;
    }
    struct stat after;assert(!fstat(handle->data[0],&after) && after.st_dev==original.st_dev && after.st_ino==original.st_ino);
    assert(!RELEASE(dev,handle));assert(!gralloc1_close(dev));dlclose(layout_library);dlclose(library);
    printf("NATIVE_IMPORT_FINISHED result=%d caller_handle_ownership=1\n",result);
    return result;
}
