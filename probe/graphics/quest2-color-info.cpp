// Allocation/metadata only. Does not submit GPU work or change the mapper.
#include <android/hardware_buffer.h>
#include <cstdio>
#include <cstdlib>
#include "quest2-mapper5.h"
extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer *);
int main(int argc,char **argv) {
    AHardwareBuffer_Desc desc{};desc.width=384;desc.height=384;desc.layers=1;
    desc.format=argc==2?strtoul(argv[1],nullptr,0):22;
    desc.usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    AHardwareBuffer *buffer=nullptr;
    if(AHardwareBuffer_allocate(&desc,&buffer))return 1;
    const native_handle_t *native=AHardwareBuffer_getNativeHandle(buffer);
    sq2::BufferInfo b;bool ok=sq2::color_info(native,b);
    if(ok) printf("COLOR_METADATA format=%u fourcc=%x width=%llu height=%llu stride=%llu step=%llu offset=%llu plane_size=%llu fd_size=%llu modifier=%llu\n",
        b.format,b.fourcc,(unsigned long long)b.width,(unsigned long long)b.height,
        (unsigned long long)b.stride,(unsigned long long)b.sample_bits,(unsigned long long)b.offset,
        (unsigned long long)b.plane_size,(unsigned long long)b.fd_size,(unsigned long long)b.modifier);
    AIMapper *m=sq2::mapper5();buffer_handle_t imported=nullptr;
    if(!m||m->v5.importBuffer(native,&imported)!=AIMAPPER_ERROR_NONE)ok=false;
    if(ok) {
        sq2::Metadata raw;uint64_t planes=0,components=0;
        ok=raw.get(m,imported,15)&&raw.u64(planes)&&planes==1&&raw.u64(components)&&components<=16;
        for(uint64_t i=0;ok&&i<components;i++) {
            uint64_t len=0,type=0,offset=0,bits=0;
            const char name[]="android.hardware.graphics.common.PlaneLayoutComponentType";
            ok=raw.u64(len)&&len==sizeof(name)-1&&len<=raw.size-raw.pos&&
                !memcmp(raw.data+raw.pos,name,len)&&raw.read(nullptr,len)&&
                raw.u64(type)&&raw.u64(offset)&&raw.u64(bits);
            if(ok)printf("COLOR_COMPONENT index=%llu type=%llu offset=%llu bits=%llu\n",
                (unsigned long long)i,(unsigned long long)type,(unsigned long long)offset,(unsigned long long)bits);
        }
        uint64_t fields[8]={};for(auto &field:fields)if(!raw.u64(field))ok=false;
        ok=ok&&raw.pos==raw.size;
        if(ok)printf("COLOR_PLANE width_samples=%llu height_samples=%llu hsub=%llu vsub=%llu components=%llu\n",
            (unsigned long long)fields[3],(unsigned long long)fields[4],(unsigned long long)fields[6],
            (unsigned long long)fields[7],(unsigned long long)components);
    }
    if(imported&&m->v5.freeBuffer(imported)!=AIMAPPER_ERROR_NONE)ok=false;
    AHardwareBuffer_release(buffer);
    printf("COLOR_METADATA_RESULT=%s\n",ok?"PASS":"FAIL");return ok?0:1;
}
