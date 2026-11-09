/* Direct gralloc1 backend shared by the native mapper5/allocator candidates. */
#pragma once
#include <hardware/gralloc1.h>
#include <cutils/native_handle.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include "quest2-gralloc-utils-abi.h"
#include "quest2-rgb-layout.h"
#include "quest2-yv12-layout.h"
struct QuestGeometry {
    uint32_t width=0,height=0,layers=0,stride=0,size=0,scan=0;
    int32_t format=0;
    uint64_t usage=0,id=0;
    gralloc::PlaneLayoutInfo plane{},chroma{},chroma2{};
    bool yuv=false,ubwc=false,planar_yv12=false;
    unsigned sample_bytes=0;
};
class QuestGralloc {
public:
    gralloc1_device_t *device=nullptr;
#define DECLARE(n) GRALLOC1_PFN_##n n=nullptr
    DECLARE(CREATE_DESCRIPTOR);DECLARE(DESTROY_DESCRIPTOR);DECLARE(SET_DIMENSIONS);
    DECLARE(SET_FORMAT);DECLARE(SET_LAYER_COUNT);DECLARE(SET_PRODUCER_USAGE);DECLARE(SET_CONSUMER_USAGE);
    DECLARE(ALLOCATE);DECLARE(RETAIN);DECLARE(RELEASE);DECLARE(GET_DIMENSIONS);DECLARE(GET_STRIDE);
    DECLARE(GET_FORMAT);DECLARE(GET_LAYER_COUNT);DECLARE(GET_BACKING_STORE);DECLARE(GET_PRODUCER_USAGE);
    DECLARE(LOCK);DECLARE(UNLOCK);
#undef DECLARE
    bool release_deletes=false;
    QuestGralloc() {
        void *library=dlopen("gralloc.kona.so",RTLD_NOW|RTLD_LOCAL);if(!library)return;
        auto *module=static_cast<hw_module_t *>(dlsym(library,HAL_MODULE_INFO_SYM_AS_STR));
        if(!module || module->tag!=HARDWARE_MODULE_TAG || gralloc1_open(module,&device))return;
#define LOAD(n) n=reinterpret_cast<GRALLOC1_PFN_##n>(device->getFunction(device,GRALLOC1_FUNCTION_##n));if(!n){device=nullptr;return;}
        LOAD(CREATE_DESCRIPTOR);LOAD(DESTROY_DESCRIPTOR);LOAD(SET_DIMENSIONS);LOAD(SET_FORMAT);
        LOAD(SET_LAYER_COUNT);LOAD(SET_PRODUCER_USAGE);LOAD(SET_CONSUMER_USAGE);LOAD(ALLOCATE);
        LOAD(RETAIN);LOAD(RELEASE);LOAD(GET_DIMENSIONS);LOAD(GET_STRIDE);LOAD(GET_FORMAT);
        LOAD(GET_LAYER_COUNT);LOAD(GET_BACKING_STORE);LOAD(GET_PRODUCER_USAGE);LOAD(LOCK);LOAD(UNLOCK);
#undef LOAD
        uint32_t count=0;device->getCapabilities(device,&count,nullptr);
        if(count>32){device=nullptr;return;}
        int32_t caps[32]={};device->getCapabilities(device,&count,caps);
        for(uint32_t i=0;i<count;i++)if(caps[i]==GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE)release_deletes=true;
    }
    static uint64_t producer(uint64_t usage) {
        uint64_t value=usage&~0xffULL;
        if((usage&0xf0)==0x30)value|=GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN;
        else if(usage&0xf0)value|=GRALLOC1_PRODUCER_USAGE_CPU_WRITE;
        return value;
    }
    static uint64_t consumer(uint64_t usage) {
        if((usage&0xf)==3)return GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN;
        if(usage&0xf)return GRALLOC1_CONSUMER_USAGE_CPU_READ;
        return 0;
    }
    int allocate(uint32_t w,uint32_t h,int32_t format,uint64_t usage,buffer_handle_t *out) {
        if(!device)return GRALLOC1_ERROR_NO_RESOURCES;
        gralloc1_buffer_descriptor_t descriptor=0;
        int e=CREATE_DESCRIPTOR(device,&descriptor);if(e)return e;
        if(!(e=SET_DIMENSIONS(device,descriptor,w,h)) && !(e=SET_FORMAT(device,descriptor,format)) &&
           !(e=SET_LAYER_COUNT(device,descriptor,1)) &&
           !(e=SET_PRODUCER_USAGE(device,descriptor,producer(usage))) &&
           !(e=SET_CONSUMER_USAGE(device,descriptor,consumer(usage))))
            e=ALLOCATE(device,1,&descriptor,out);
        DESTROY_DESCRIPTOR(device,descriptor);return e;
    }
    int release(buffer_handle_t handle) {
        int e=RELEASE(device,handle);
        if(!e && !release_deletes){auto *h=const_cast<native_handle_t *>(handle);native_handle_close(h);native_handle_delete(h);}
        return e;
    }
    static bool yuvFormat(int32_t format) {
        return format==0x32315659 || format==0x36 || format==0x7fa30c04 || format==0x7fa30c0a;
    }
    static bool supportedFormat(int32_t format) {
        return format==1 || format==2 || format==5 || format==43 || format==22 || yuvFormat(format);
    }
    bool geometry(buffer_handle_t b,QuestGeometry &g) {
        if(!device || GET_DIMENSIONS(device,b,&g.width,&g.height) || GET_STRIDE(device,b,&g.stride) ||
           GET_FORMAT(device,b,&g.format) || GET_LAYER_COUNT(device,b,&g.layers) ||
           GET_BACKING_STORE(device,b,&g.id) || GET_PRODUCER_USAGE(device,b,&g.usage))return false;
        if(!g.width || !g.height || g.width>INT32_MAX || g.height>INT32_MAX || g.layers!=1 ||
           !supportedFormat(g.format) || (g.usage&0x4000))return false;
        g.ubwc=gralloc::IsUBwcEnabled(g.format,g.usage);
        if(g.ubwc && (yuvFormat(g.format) || (g.usage&0xff)))return false;
        gralloc::BufferInfo info{int(g.width),int(g.height),g.format,1,g.usage};
        unsigned aligned=0;
        if(gralloc::GetBufferSizeAndDimensions(info,&g.size,&aligned,&g.scan) || aligned!=g.stride ||
           !g.size || g.scan<g.height || g.scan>INT32_MAX || aligned>INT32_MAX)return false;
        struct stat backing{};
        if(fstat(b->data[0],&backing) || backing.st_size<0 || uint64_t(backing.st_size)<g.size)return false;
        int count=0;
        g.yuv=yuvFormat(g.format);g.planar_yv12=g.format==0x32315659;
        if(g.yuv) {
            if((g.width&1) || (g.height&1) || (g.scan&1))return false;
            gralloc::PlaneLayoutInfo planes[8]{};
            if(gralloc::GetYUVPlaneInfo(info,g.format,aligned,g.scan,0,&count,planes))return false;
            if(g.planar_yv12) {
                if(count!=3)return false;
                // Donor utility order is Y/Cb/Cr; standard metadata uses physical Y/Cr/Cb.
                g.plane=planes[0];g.chroma=planes[2];g.chroma2=planes[1];g.sample_bytes=1;
                return quest2_yv12_layout_valid(g.plane,g.chroma,g.chroma2,g.width,g.height,
                                               g.stride,g.scan,g.size);
            }
            if(count!=2)return false;
            g.plane=planes[0];g.chroma=planes[1];
            auto &y=g.plane;auto &uv=g.chroma;
            g.sample_bytes=g.format==0x7fa30c04?1:2;
            uint64_t row=uint64_t(g.stride)*g.sample_bytes;
            if(y.component!=1 || uv.component!=6 || y.offset || y.step!=1 ||
               y.h_subsampling || y.v_subsampling || uv.h_subsampling!=1 || uv.v_subsampling!=1 ||
               y.stride_bytes<=0 || uint64_t(y.stride_bytes)!=row || y.scanlines!=int(g.scan) ||
               uint64_t(y.size)!=row*g.scan || uv.offset!=y.size ||
               uv.step!=int(g.sample_bytes*2) || uv.stride_bytes!=y.stride_bytes)return false;
            // The matched donor reports one-byte Y steps for both 8-bit and P010.
            y.step=g.sample_bytes;
            if(g.format==0x36) {
                // Its standard P010 helper also reports full-height UV rows and one padding byte.
                if(uv.scanlines!=int(g.scan) || uint64_t(uv.size)!=row*(g.scan/2)+1)return false;
                uv.scanlines=g.scan/2;
            }
            return uv.scanlines>=int(g.height/2) && uint64_t(uv.stride_bytes)*uv.scanlines<=uv.size &&
                uint64_t(uv.offset)+uv.size<=g.size;
        }
        gralloc::GetRGBPlaneInfo(info,g.format,aligned,g.scan,0,&count,&g.plane);
        uint32_t meta=gralloc::GetRgbMetaSize(g.format,aligned,g.scan,g.usage);
        return count==1 && quest2_rgb_layout_valid(g.plane,g.format,g.stride,g.scan,g.size,g.ubwc,meta);
    }
};
