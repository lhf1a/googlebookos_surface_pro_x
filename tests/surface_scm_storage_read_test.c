#include <assert.h>
#include <stdio.h>
#include "../probe/lte/surface-scm-storage-read.h"

int main(void) {
 uint8_t info[105]={0},r[80],before[80];struct spx_storage_geometry g={0};
 struct spx_storage_descriptor d={0},old;
 spx_storage_put64(info,8192);spx_storage_put64(info+8,4096ULL|(256ULL<<32));info[16]=1;
 assert(spx_storage_parse_info(info,sizeof(info),&g));
 assert(g.blocks==8192 && g.block_bytes==4096 && g.page_bytes==256);
 for(size_t n=0;n<105;n++)assert(!spx_storage_parse_info(info,n,&g));
 assert(!spx_storage_parse_info(info,106,&g));
 for(unsigned op=0;op<256;op++) {
  memset(r,0xa5,sizeof(r));memcpy(before,r,sizeof(r));old=d;
  int ok=spx_storage_encode(r,sizeof(r),&d,0x100000,op,0,op==1?&g:NULL);
  assert(ok==(op==1 || op==4));
  if(!ok){assert(!memcmp(r,before,sizeof(r)));assert(!memcmp(&d,&old,sizeof(d)));}
 }
 assert(spx_storage_encode(r,sizeof(r),&d,0x100000,4,0,NULL));
 assert(d.command==0x02001a01 && d.arginfo==0x214);
 assert(d.args[0]==0x100000 && d.args[1]==40 && d.args[2]==0x100028 && d.args[3]==32);
 assert(spx_storage_u64(r)==1 && !spx_storage_u64(r+8) && !spx_storage_u64(r+16) && !spx_storage_u64(r+24));
 assert(spx_storage_u64(r+32)==4 && !spx_storage_u64(r+40) && spx_storage_u64(r+48)==105);
 assert(spx_storage_u64(r+56)==0x101000 && !spx_storage_u64(r+64));
 for(size_t i=72;i<sizeof(r);i++)assert(r[i]==0xa5);
 assert(spx_storage_encode(r,sizeof(r),&d,0x100000,1,8191,&g));
 assert(spx_storage_u64(r+32)==1 && spx_storage_u64(r+40)==8191 && spx_storage_u64(r+48)==4096);
 assert(!spx_storage_encode(r,sizeof(r),&d,0x100000,1,8192,&g));
 assert(!spx_storage_encode(r,sizeof(r),&d,0x100000,1,UINT64_MAX,&g));
 for(size_t n=0;n<72;n++)assert(!spx_storage_encode(r,n,&d,0x100000,4,0,NULL));
 for(unsigned off=1;off<4096;off++)assert(!spx_storage_encode(r,sizeof(r),&d,0x100000+off,4,0,NULL));
 assert(!spx_storage_encode(r,sizeof(r),&d,0,4,0,NULL));
 assert(!spx_storage_encode(r,sizeof(r),&d,UINT64_MAX-4095,4,0,NULL));
 assert(!spx_storage_encode(r,sizeof(r),&d,0x100000,4,1,NULL));
 assert(!spx_storage_encode(r,sizeof(r),&d,0x100000,4,0,&g));
 assert(!spx_storage_encode(r,sizeof(r),&d,0x100000,1,0,NULL));
 spx_storage_put64(info,UINT64_MAX);assert(!spx_storage_parse_info(info,105,&g));
 spx_storage_put64(info,8192);info[8]=0;info[9]=2;assert(!spx_storage_parse_info(info,105,&g));
 info[9]=16;info[12]=3;assert(!spx_storage_parse_info(info,105,&g));
 info[12]=0;info[16]=0;assert(!spx_storage_parse_info(info,105,&g));
 puts("PASS storage READ/GET_INFO only: 40/32-byte ABI,105-byte info,4096-byte blocks, LBA/PA/cap bounds; NO hardware calls");
 return 0;
}
