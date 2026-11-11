/* Canonical static FDT records for host/early-init comparison.
 * Deliberately excludes only measured EFI/EL2 runtime fixups and the runtime
 * reservation map. This is not measured-boot attestation or DT validation.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bad(const char *s) { fprintf(stderr,"sq2-fdt-contract: %s\n",s); exit(2); }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static size_t align4(size_t n) { if(n>SIZE_MAX-3)bad("overflow"); return (n+3)&~(size_t)3; }
static char **records; static size_t nr,cap,record_bytes;
#define RECORD_LIMIT 65536U
#define RECORD_BYTE_LIMIT (32U * 1024U * 1024U)
static char *hex(char *p,const unsigned char *v,size_t n) {
    static const char digits[]="0123456789abcdef";
    while(n--){*p++=digits[*v>>4];*p++=digits[*v++&15];} return p;
}
static void add(char kind,const char *path,const char *name,const unsigned char *v,size_t n) {
    size_t a=strlen(path),b=name?strlen(name):0;
    if(a+b+n>(SIZE_MAX-5)/2)bad("record too large");
    size_t bytes=2*(a+b+n)+5;
    if(nr>=RECORD_LIMIT||bytes>RECORD_BYTE_LIMIT-record_bytes)bad("record budget exceeded");
    char *s=malloc(bytes),*p=s;
    if(!s)bad("allocation");
    record_bytes+=bytes;
    *p++=kind; *p++=' '; p=hex(p,(const unsigned char *)path,a);
    if(name){*p++=' ';p=hex(p,(const unsigned char *)name,b);*p++=' ';p=hex(p,v,n);}
    *p=0;
    if(nr==cap){size_t next=cap?cap*2:128;if(next<cap||next>SIZE_MAX/sizeof(*records))bad("too many records");
        char **r=realloc(records,next*sizeof(*r));if(!r)bad("allocation");records=r;cap=next;}
    records[nr++]=s;
}
static int dynamic(const char *p,const char *n) {
    if(!strcmp(p,"/chosen")) {
        static const char *const names[]={"bootargs","kaslr-seed","linux,uefi-mmap-desc-size","linux,uefi-mmap-desc-ver","linux,uefi-mmap-size","linux,uefi-mmap-start","linux,uefi-system-table"};
        for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(n,names[i]))return 1;
    }
    if(!strcmp(p,"/")&&(!strcmp(n,"microsoft,el2-pas-trace")||!strcmp(n,"microsoft,el2-venus-prepared")))return 1;
    if((!strcmp(p,"/reserved-memory/memory@85500000")||!strcmp(p,"/soc@0/remoteproc@17300000")||
        !strcmp(p,"/soc@0/remoteproc@4080000")||!strcmp(p,"/soc@0/remoteproc@8300000"))&&
       (!strcmp(n,"microsoft,el2-preboot-complete")||!strcmp(n,"microsoft,el2-preboot-status")))return 1;
    return 0;
}
static int cmp(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"usage: sq2-fdt-contract DTB\n");return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f)bad("open");
    /* sysfs files report size0; stream rather than seek/ftell. */
    size_t size=0,allocated=65536;unsigned char *data=malloc(allocated);
    if(!data)bad("allocation");
    for(;;){if(size==allocated){if(allocated>=16*1024*1024)bad("FDT exceeds16MiB");allocated*=2;
            unsigned char *d=realloc(data,allocated);if(!d)bad("allocation");data=d;}
        size_t got=fread(data+size,1,allocated-size,f);size+=got;
        if(!got){if(ferror(f))bad("read");break;}}
    fclose(f);
    if(size<40||be32(data)!=0xd00dfeed)bad("header");
    size_t total=be32(data+4),so=be32(data+8),st=be32(data+12),ro=be32(data+16),ss=be32(data+32),sz=be32(data+36);
    if(total<40||total>size||so<40||st<40||ro<40||be32(data+20)<17||be32(data+24)>17)bad("header bounds/version");
    if(so>total||sz>total-so||st>total||ss>total-st||ro>total-16)bad("block bounds");
    if(!(so+sz<=st||st+ss<=so))bad("overlapping blocks");
    size_t rp=ro;for(;;){if(rp>total-16)bad("reservation map");
        int zero=1;for(size_t i=0;i<16;i++)if(data[rp+i])zero=0;rp+=16;if(zero)break;}
    if(!(rp<=so||ro>=so+sz)||!(rp<=st||ro>=st+ss))bad("overlapping reservations");
    unsigned char *block=data+so,*strings=data+st;
    char path[8192]="";size_t saved[256],depth=0,pos=0;int ended=0;
    while(pos+4<=sz){uint32_t tok=be32(block+pos);pos+=4;
        if(tok==1){if(depth==256)bad("depth");unsigned char *end=memchr(block+pos,0,sz-pos);
            if(!end)bad("node name");
            size_t n=(size_t)(end-(block+pos)),old=strlen(path);
            if(memchr(block+pos,'/',n)||(!depth&&n))bad("invalid node name");
            saved[depth++]=old;
            if(!old){strcpy(path,"/");old=1;}else if(strcmp(path,"/")){if(old+1>=sizeof(path))bad("path");path[old++]='/';}
            if(n>=sizeof(path)-old)bad("path");
            memcpy(path+old,block+pos,n);path[old+n]=0;
            add('N',path,NULL,NULL,0);pos=align4((size_t)(end-block)+1);
        }else if(tok==2){if(!depth)bad("END_NODE");path[saved[--depth]]=0;
        }else if(tok==3){if(!depth||sz-pos<8)bad("property header");size_t n=be32(block+pos),off=be32(block+pos+4);pos+=8;
            if(n>sz-pos||off>=ss)bad("property bounds");
            unsigned char *end=memchr(strings+off,0,ss-off);
            if(!end||end==strings+off)bad("property name");
            add('K',path,(char *)strings+off,NULL,0);
            if(!dynamic(path,(char *)strings+off))add('P',path,(char *)strings+off,block+pos,n);
            pos=align4(pos+n);
        }else if(tok==4){continue;
        }else if(tok==9){if(depth||!nr)bad("FDT_END");ended=1;break;
        }else bad("token");
        if(pos>sz)bad("alignment");
    }
    if(!ended)bad("missing FDT_END");
    qsort(records,nr,sizeof(*records),cmp);
    for(size_t i=1;i<nr;i++)if(!strcmp(records[i-1],records[i]))bad("duplicate node/property");
    /* A property key must be unique even when its values differ. */
    for(size_t i=1;i<nr;i++)if(records[i][0]=='P'&&records[i-1][0]=='P'){
        char *a=strrchr(records[i-1],' '),*b=strrchr(records[i],' ');
        size_t n=(size_t)(a-records[i-1]);
        if((size_t)(b-records[i])==n&&!memcmp(records[i-1],records[i],n))bad("duplicate property key");
    }
    for(size_t i=0;i<nr;i++){if(records[i][0]!='K')puts(records[i]);free(records[i]);}
    free(records);free(data);return ferror(stdout)?2:0;
}
