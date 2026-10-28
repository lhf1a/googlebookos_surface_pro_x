/* Read actual minigbm metadata for an isolated QTI compatibility experiment.
 * Import/free a private mapper handle; returned fd remains borrowed from the
 * caller's original handle. No invented format, size, stride or modifier. */
#pragma once
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <sys/stat.h>
#include "vm_imapper5.h"

namespace sq2 {
struct BufferInfo {
    uint64_t id=0,width=0,height=0,layers=0,usage=0,size=0,modifier=0;
    uint32_t format=0,fourcc=0;
    uint64_t offset=0,stride=0,plane_size=0,sample_bits=0,fd_size=0;
    int fd=-1;
};
struct Metadata {
    unsigned char data[4096];size_t size=0,pos=0;
    bool read(void *dst,size_t count) {
        if(pos>size || count>size-pos)return false;
        if(dst)std::memcpy(dst,data+pos,count);
        pos+=count;return true;
    }
    bool u64(uint64_t &v){return read(&v,8);}
    bool string_skip(){uint64_t len;return u64(len)&&len<=size-pos&&read(nullptr,len);}
    bool get(AIMapper *m,buffer_handle_t h,int64_t key) {
        int32_t count=m->v5.getStandardMetadata(h,key,data,sizeof(data));
        if(count<=0 || static_cast<size_t>(count)>sizeof(data))return false;
        size=count;pos=0;
        const char name[]="android.hardware.graphics.common.StandardMetadataType";
        uint64_t len,type;
        return u64(len)&&len==sizeof(name)-1&&len<=size-pos&&
            !std::memcmp(data+pos,name,len)&&read(nullptr,len)&&u64(type)&&type==static_cast<uint64_t>(key);
    }
};
inline AIMapper *mapper5() {
    static AIMapper *instance=[](){
        void *lib=dlopen("/vendor/lib64/hw/mapper.minigbm.so",RTLD_NOW|RTLD_LOCAL);
        if(!lib)return static_cast<AIMapper*>(nullptr);
        auto load=reinterpret_cast<AIMapper_Error(*)(AIMapper**)>(dlsym(lib,"AIMapper_loadIMapper"));
        AIMapper *m=nullptr;
        if(!load || load(&m)!=AIMAPPER_ERROR_NONE || !m || m->version<5){dlclose(lib);return static_cast<AIMapper*>(nullptr);}
        return m;
    }();
    return instance;
}
inline bool color_info(const native_handle_t *original,BufferInfo &out) {
    AIMapper *m=mapper5();
    if(!m || !original || original->numFds<1)return false;
    struct Import {
        AIMapper *m;buffer_handle_t h=nullptr;
        ~Import(){if(h)m->v5.freeBuffer(h);}
    } imported{m};
    if(m->v5.importBuffer(original,&imported.h)!=AIMAPPER_ERROR_NONE)return false;
    BufferInfo b;
    auto scalar=[&](int64_t key,void *dst,size_t n){
        Metadata r;return r.get(m,imported.h,key)&&r.read(dst,n)&&r.pos==r.size;
    };
    if(!scalar(1,&b.id,8)||!scalar(3,&b.width,8)||!scalar(4,&b.height,8)||!scalar(5,&b.layers,8)||
       !scalar(6,&b.format,4)||!scalar(7,&b.fourcc,4)||!scalar(8,&b.modifier,8)||
       !scalar(9,&b.usage,8)||!scalar(10,&b.size,8))return false;
    Metadata r;uint64_t planes,components,unused;
    if(!r.get(m,imported.h,15)||!r.u64(planes)||planes!=1||!r.u64(components)||components>16)return false;
    for(uint64_t i=0;i<components;i++)
        if(!r.string_skip()||!r.u64(unused)||!r.u64(unused)||!r.u64(unused))return false;
    uint64_t fields[8];for(auto &field:fields)if(!r.u64(field))return false;
    if(r.pos!=r.size)return false;
    b.offset=fields[0];b.sample_bits=fields[1];b.stride=fields[2];b.plane_size=fields[5];
    b.fd=original->data[0];
    struct stat st={};
    if(b.fd<0 || fstat(b.fd,&st) || st.st_size<=0)return false;
    b.fd_size=static_cast<uint64_t>(st.st_size);
    if(b.fd<0 || !b.width || !b.height || !b.layers || !b.size || !b.stride || !b.sample_bits ||
       b.modifier!=0 || b.layers!=1 || b.offset>b.fd_size || b.plane_size>b.fd_size-b.offset)return false;
    // Only report the currently understood linear single-plane layout. Further
    // formats/modifiers must be implemented and tested, never flattened silently.
    out=b;return true;
}
}
