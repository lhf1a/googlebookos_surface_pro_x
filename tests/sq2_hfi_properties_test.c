#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef uint32_t u32;
#include "sq2-hfi-properties.h"
static unsigned calls;
static int visit(void *ctx,u32 id,const u32 *p,u32 words){
 (void)ctx;(void)id;(void)p;assert(words);calls++;return 0;
}
int main(int argc,char **argv){
 u32 n=0, a[]={0x1007,1,0x1009,1,8192,1,0x1010,16};
 assert(!sq2_hfi_properties(a,8,visit,NULL,&n)&&n==2&&calls==2);
 a[1]=UINT32_MAX;assert(sq2_hfi_properties(a,8,NULL,NULL,&n));
 u32 fmt[]={0x1004,2,1,2,5};assert(sq2_hfi_properties(fmt,5,NULL,NULL,&n));
 u32 unknown[]={0xdeadbeef,0};assert(sq2_hfi_properties(unknown,2,NULL,NULL,&n));
 if(argc==2){
  FILE *f=fopen(argv[1],"rb");assert(f);u32 data[4096];
  size_t bytes=fread(data,1,sizeof(data),f);assert(!ferror(f)&&feof(f));fclose(f);
  assert(bytes>=16&&bytes%4==0&&data[0]==bytes&&data[1]==0x20001&&data[2]==0);
  calls=0;assert(!sq2_hfi_properties(data+4,bytes/4-4,visit,NULL,&n));
  assert(n==data[3]&&calls==75);
  /* Every truncation must fail or end exactly on a complete property boundary;
   * the packet-level property count rejects such shortened prefixes. */
  for(u32 cut=1;cut<bytes/4-4;cut++){
   n=0;int r=sq2_hfi_properties(data+4,cut,NULL,NULL,&n);
   assert(r||n<data[3]);
  }
  puts("REAL_PACKET_75_PROPERTIES_ALL_TRUNCATIONS_PASS");
 }
 puts("BOUNDS_UNKNOWN_ID_COUNT_OVERFLOW_PAYLOAD_ID_PASS");return 0;
}
