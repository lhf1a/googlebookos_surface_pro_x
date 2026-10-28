#include <android/hardware_buffer.h>
#include <cstdio>
#include "quest2-mapper5.h"
extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer *);
int main() {
    AHardwareBuffer_Desc desc={};desc.width=64;desc.height=64;desc.layers=1;
    desc.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    AHardwareBuffer *buffer=nullptr;
    if(AHardwareBuffer_allocate(&desc,&buffer))return 1;
    sq2::BufferInfo b;bool ok=sq2::color_info(AHardwareBuffer_getNativeHandle(buffer),b);
    if(ok)std::printf("MAPPER5_INFO width=%llu height=%llu format=%u fourcc=%08x modifier=%llu size=%llu stride=%llu offset=%llu plane_size=%llu sample_bits=%llu fd_size=%llu fd=%d\n",
        (unsigned long long)b.width,(unsigned long long)b.height,b.format,b.fourcc,
        (unsigned long long)b.modifier,(unsigned long long)b.size,(unsigned long long)b.stride,
        (unsigned long long)b.offset,(unsigned long long)b.plane_size,(unsigned long long)b.sample_bits,(unsigned long long)b.fd_size,b.fd);
    else std::fprintf(stderr,"MAPPER5_INFO_FAILED\n");
    AHardwareBuffer_release(buffer);return ok?0:1;
}
