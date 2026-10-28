#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern unsigned run_cache_code(void *,unsigned);
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 1;
    unsigned char *code=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(code==MAP_FAILED)return 1;
    const unsigned start=0x20f8fc,length=0x20fbd0-start;
    if(fseek(f,start,SEEK_SET)||fread(code,1,length,f)!=length)return 1;
    fclose(f);
    /* Stop only after the actual policy selected w21, before allocation.
     * Original branch distances and all selection instructions stay intact. */
    const uint32_t finish[]={0x2a1503e0,0xd65f03c0};
    memcpy(code+0x20f924-start,finish,sizeof(finish));
    memcpy(code+0x20f964-start,finish,sizeof(finish));
    __builtin___clear_cache((char*)code,(char*)code+length);
    unsigned cases=0;
    for(unsigned mode=0;mode<4;mode++)for(unsigned tail=0;tail<128;tail++) {
        unsigned flags=(mode<<26)|(tail*0x10201u);
        unsigned expected=mode==1?0x40000:mode==3?0x80000:0x20000;
        unsigned actual=run_cache_code(code,flags);
        if(actual!=expected){fprintf(stderr,"CACHE_POLICY_FAIL flags=%x got=%x expected=%x\n",flags,actual,expected);return 1;}
        cases++;
    }
    munmap(code,4096);printf("CACHE_POLICY_QEMU_PASS cases=%u\n",cases);return 0;
}
