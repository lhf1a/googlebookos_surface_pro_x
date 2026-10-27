#include <android/hardware_buffer.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    AHardwareBuffer_Desc desc={.width=64,.height=64,.layers=1,.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
    for(int frame=0;frame<3;frame++){
        AHardwareBuffer *b=NULL; int rc=AHardwareBuffer_allocate(&desc,&b);
        printf("AHB_ALLOC frame=%d rc=%d\n",frame,rc); if(rc||!b)return 1;
        AHardwareBuffer_Desc actual; AHardwareBuffer_describe(b,&actual);
        void *p=NULL; rc=AHardwareBuffer_lock(b,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&p);
        if(rc||!p){printf("AHB_WRITE_LOCK=%d\n",rc);return 1;}
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)((uint32_t*)p)[y*actual.stride+x]=0xff000000u|((uint32_t)frame<<16)|(y<<8)|x;
        if(AHardwareBuffer_unlock(b,NULL))return 1;
        p=NULL; rc=AHardwareBuffer_lock(b,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&p);
        if(rc||!p){printf("AHB_READ_LOCK=%d\n",rc);return 1;}
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)if(((uint32_t*)p)[y*actual.stride+x]!=(0xff000000u|((uint32_t)frame<<16)|(y<<8)|x)){puts("AHB_PIXEL_MISMATCH");return 1;}
        if(AHardwareBuffer_unlock(b,NULL))return 1;
        AHardwareBuffer_release(b);
        printf("AHB_FRAME_OK frame=%d pixels=4096 stride=%u\n",frame,actual.stride);
    }
    puts("AHB_ALLOCATOR_MAPPER_READBACK_PASS"); return 0;
}
