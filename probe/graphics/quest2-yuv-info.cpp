#include <android/hardware_buffer.h>
#include <cstdio>
#include <cstdlib>
#include "quest2-yuv-metadata.h"
extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer *);
int main(int argc,char **argv){
    AHardwareBuffer_Desc desc={};desc.width=640;desc.height=360;desc.layers=1;
    desc.format=argc>1?strtoul(argv[1],nullptr,0):0x32315659;
    desc.usage=AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer *buffer=nullptr;if(AHardwareBuffer_allocate(&desc,&buffer))return 1;
    sq2::YuvInfo b;bool ok=sq2::yuv_metadata(AHardwareBuffer_getNativeHandle(buffer),b);
    if(ok){
        printf("YUV_METADATA format=%x fourcc=%x width=%llu height=%llu fd_size=%llu planes=%llu\n",b.format,b.fourcc,
            (unsigned long long)b.width,(unsigned long long)b.height,(unsigned long long)b.fd_size,(unsigned long long)b.count);
        for(unsigned i=0;i<b.count;i++){
            const auto &p=b.planes[i];printf("PLANE %u offset=%llu step=%llu stride=%llu w=%llu h=%llu size=%llu subsample=%llu,%llu\n",i,
                (unsigned long long)p.offset,(unsigned long long)p.step,(unsigned long long)p.stride,(unsigned long long)p.width,
                (unsigned long long)p.height,(unsigned long long)p.size,(unsigned long long)p.hsub,(unsigned long long)p.vsub);
            for(unsigned j=0;j<p.count;j++)printf("COMPONENT type=%llu offset=%llu bits=%llu\n",(unsigned long long)p.components[j].type,
                (unsigned long long)p.components[j].offset,(unsigned long long)p.components[j].bits);
        }
    }else puts("YUV_METADATA_FAIL");
    AHardwareBuffer_release(buffer);return ok?0:1;
}
