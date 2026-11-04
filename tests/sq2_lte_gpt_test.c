#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../probe/virtualization/sq2-lte-gpt.h"
static unsigned tests;
#define CHECK(x) do { tests++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void u32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8)); }
static void u64(uint8_t *p,uint64_t v) { for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(i*8)); }
int main(void) {
    uint8_t h[4096]={0}, e[128]={0}; size_t bytes=0; struct sq2_efs_partition p={0};
    memcpy(h,"EFI PART",8); u32(h+8,0x10000); u32(h+12,92);u64(h+24,1);u64(h+32,131071);
    u64(h+40,34);u64(h+48,131038);u64(h+72,2);u32(h+80,128);u32(h+84,128);
    CHECK(sq2_gpt_layout(h,512,512,131071,&bytes) && bytes==16384);
    CHECK(!sq2_gpt_layout(h,91,512,131071,&bytes));
    CHECK(!sq2_gpt_layout(h,512,0,131071,&bytes));
    CHECK(!sq2_gpt_layout(h,512,1024,131071,&bytes));
    CHECK(!sq2_gpt_layout(h,512,512,131070,&bytes));
    h[0]^=1;CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));h[0]^=1;
    u32(h+80,0);CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));u32(h+80,128);
    u32(h+80,0xffffffff);CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));u32(h+80,128);
    u32(h+84,127);CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));u32(h+84,128);
    u64(h+72,3);CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));u64(h+72,2);
    u64(h+40,131040);CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));u64(h+40,34);
    u64(h+48,131071);CHECK(!sq2_gpt_layout(h,512,512,131071,&bytes));u64(h+48,131038);
    for(unsigned i=0;i<4;i++) {
        memcpy(e,sq2_efs_guids[i],16);u64(e+32,2048);u64(e+40,6143);
        /* Deliberately not an EFS partition name: type GUID is authoritative. */
        memcpy(e+56,"u\0n\0k\0n\0o\0w\0n\0",14);
        CHECK(sq2_efs_partition(e,128,512,34,131038,&p) && p.bytes==2097152);
        CHECK(p.name && p.name[0]=='m');
        e[0]^=1;CHECK(!sq2_efs_partition(e,128,512,34,131038,&p));e[0]^=1;
    }
    CHECK(!sq2_efs_partition(e,127,512,34,131038,&p));
    CHECK(!sq2_efs_partition(e,128,0,34,131038,&p));
    u64(e+32,0);CHECK(!sq2_efs_partition(e,128,512,0,131038,&p));
    u64(e+32,33);CHECK(!sq2_efs_partition(e,128,512,34,131038,&p));
    u64(e+32,2048);u64(e+40,2047);CHECK(!sq2_efs_partition(e,128,512,34,131038,&p));
    u64(e+40,131039);CHECK(!sq2_efs_partition(e,128,512,34,131038,&p));
    u64(e+40,2048+16383);CHECK(sq2_efs_partition(e,128,512,34,131038,&p) && p.bytes==8388608);
    u64(e+40,2048+16384);CHECK(!sq2_efs_partition(e,128,512,34,131038,&p));
    u64(e+40,UINT64_MAX);CHECK(!sq2_efs_partition(e,128,512,0,UINT64_MAX,&p));
    u64(e+32,2048);u64(e+40,2559);CHECK(sq2_efs_partition(e,128,4096,34,131038,&p) && p.bytes==2097152);
    memset(e,0,16);memcpy(e+56,"m\0o\0d\0e\0m\0_\0f\0s\0",16);
    CHECK(!sq2_efs_partition(e,128,512,34,131038,&p));
    printf("PASS: %u bounded GPT/EFS GUID fixtures; no device I/O\n",tests);
    return 0;
}
