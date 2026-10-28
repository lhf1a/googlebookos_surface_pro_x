/* Run the actual patched donor queue loop in QEMU with mocked callbacks.
 * No GPU execution; verifies shared-node release/list ownership, both allocators.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void invoke_counter_loop(void *,void *,void *,void *,void *,void *);
static unsigned modes[4],calls,frees,seen[2],custom;
static void *objects[2];
static void release(void *p){
    unsigned i;
    for(i=0;i<2&&objects[i]!=p;i++){}
    if(i==2||seen[i]++)abort();
    frees++;
}
static void custom_release(void *user,void *p){if(user!=&custom)abort();release(p);}
static int select_counter(uint32_t dev,uint32_t ctx,uint32_t count,
                          const uint32_t *group,uint32_t *counter,void *a,void *b){
    if(dev>=4||count!=1||*group!=27||*counter||a||b)abort();
    calls++;
    /* 0=success/no second; 1=first failure; 2=both success; 3=second failure. */
    return modes[dev]==1||(modes[dev]==3&&ctx==1)?-1:0;
}
static void ptr(void *p,size_t at,void *v){memcpy((char *)p+at,&v,8);}
static void word(void *p,size_t at,uint32_t v){memcpy((char *)p+at,&v,4);}
static void *getptr(void *p,size_t at){void *v;memcpy(&v,(char *)p+at,8);return v;}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    size_t length=0x2d5000;
    unsigned char *code=mmap(NULL,length,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(code==MAP_FAILED)return 1;
    FILE *f=fopen(argv[1],"rb");if(!f||fread(code,1,length,f)!=length)return 1;fclose(f);
    /* Only external free and loop-exit are replaced in this test fixture. */
    word(code,0x2d4280,0x58000050); /* ldr x16, literal +8 */
    word(code,0x2d4284,0xd61f0200);ptr(code,0x2d4288,release);
    word(code,0xd307c,0xf943fbfe); /* ldr x30,[sp,#2032] */
    word(code,0xd3080,0xd65f03c0);
    __builtin___clear_cache((char *)code,(char *)code+length);
    unsigned cases=0;
    for(custom=0;custom<2;custom++)for(unsigned n=1;n<=4;n++)for(unsigned mask=0;mask<(1u<<(2*n));mask++){
        unsigned char device[800]={0},table[280]={0},records[4][160]={{0}},allocator[64]={0};
        void *record_ptrs[4],*cache=NULL;unsigned expected_calls=0,success=0;
        uint32_t payload[2]={27,0};void *node[3]={payload,NULL,NULL};
        objects[0]=payload;objects[1]=node;frees=calls=seen[0]=seen[1]=0;
        ptr(table,272,select_counter);ptr(device,128,table);word(device,80,custom);
        word(device,84,0);ptr(allocator,8,&custom);ptr(allocator,32,custom_release);
        for(unsigned i=0;i<n;i++){
            modes[i]=(mask>>(2*i))&3;record_ptrs[i]=records[i];
            word(records[i],144,i);word(records[i],148,0);
            word(records[i],152,modes[i]>=2?1:UINT32_MAX);
            expected_calls+=modes[i]>=2?2:1;
            success|=modes[i]==0||modes[i]==2;
        }
        ptr(device,776,record_ptrs);word(device,784,n);
        invoke_counter_loop(code+0xd238c,device,payload,node,&cache,allocator);
        if(calls!=expected_calls||frees!=(success?0u:2u)||
           getptr(device,720)!=(success?node:NULL)||cache!=(success?node:NULL)){
            fprintf(stderr,"FAIL allocator=%u n=%u mask=%u calls=%u frees=%u success=%u\n",custom,n,mask,calls,frees,success);return 1;
        }
        cases++;
    }
    printf("COUNTER_CLEANUP_QEMU_PASS cases=%u\n",cases);
    munmap(code,length);return 0;
}
