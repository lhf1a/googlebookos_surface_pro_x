#include "../probe/video/quest2-omx-node-usage.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint32_t wire[4]={16,1,1,0x80000b03U},usage=0;
    assert(!quest2_omx_decode_consumer_usage(wire,sizeof(wire),&usage)&&usage==0x80000b03U);
    wire[1]=0x101;
    assert(!quest2_omx_decode_consumer_usage(wire,sizeof(wire),&usage));
    for(size_t n=0;n<32;n++)if(n!=16){
        usage=0x1234;assert(quest2_omx_decode_consumer_usage(wire,n,&usage)==-EINVAL&&usage==0x1234);
    }
    assert(quest2_omx_decode_consumer_usage(NULL,16,&usage)==-EINVAL);
    assert(quest2_omx_decode_consumer_usage(wire,16,NULL)==-EINVAL);
    for(unsigned field=0;field<3;field++){
        uint32_t saved=wire[field];wire[field]=0;usage=0x1234;
        assert(quest2_omx_decode_consumer_usage(wire,16,&usage)==-EINVAL&&usage==0x1234);
        wire[field]=saved;
    }
    wire[2]=0;assert(quest2_omx_decode_consumer_usage(wire,16,&usage)==-EINVAL);wire[2]=2;
    assert(quest2_omx_decode_consumer_usage(wire,16,&usage)==-EINVAL);
    assert(quest2_omx_requested_usage(0,0x20000000)==0x20402900);
    assert(quest2_omx_allocation_usage(0,0x20000000)==0x20400900);
    assert(quest2_omx_requested_usage(0xb03,0x20000000)==0x20402b03);
    assert(quest2_omx_allocation_usage(0xb03,0x20000000)==0x20400b03);
    /* Unknown high consumer bits must reach the strict allocator check intact. */
    assert(quest2_omx_allocation_usage(0x80000000,0x20000000)==0xa0400900ULL);
    assert(quest2_omx_allocation_usage(0x80000000,0x20000000)==
           quest2_omx_allocation_usage(0x20000000,0x80000000));
    assert(quest2_omx_allocation_fits_capture(320,240,320,512,512,512,393216,393216));
    assert(quest2_omx_allocation_fits_capture(320,256,320,256,512,512,147456,393216));
    assert(!quest2_omx_allocation_fits_capture(320,240,320,128,512,512,393216,393216));
    assert(!quest2_omx_allocation_fits_capture(320,240,256,512,512,512,393216,393216));
    assert(!quest2_omx_allocation_fits_capture(320,240,320,513,512,512,393216,393216));
    assert(!quest2_omx_allocation_fits_capture(320,240,513,512,512,512,393216,393216));
    assert(!quest2_omx_allocation_fits_capture(320,240,320,512,512,512,393217,393216));
    assert(!quest2_omx_allocation_fits_capture(320,0,320,512,512,512,393216,393216));
    assert(!quest2_omx_allocation_fits_capture(320,240,320,512,512,512,0,393216));
    puts("OMX consumer wire and requested/allocator usage checks passed");
    return 0;
}
