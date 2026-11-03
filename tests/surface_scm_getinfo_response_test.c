/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
#include "surface-scm-storage-read.h"

int main(void) {
 uint8_t pages[8192],expected[64]; struct spx_storage_descriptor desc;
 memset(pages,0xa5,sizeof(pages));
 assert(spx_storage_encode(pages,4096,&desc,0x12345000,4,0,NULL));
 memcpy(expected,pages,64);
 assert(desc.command==0x02001a01 && desc.arginfo==0x214);
 assert(spx_storage_u64(pages+32)==4 && spx_storage_u64(pages+48)==105);
#ifdef SPX_STORAGE_GETINFO_RESPONSE_V1
 uint64_t result[4]={1,0,UINT64_MAX,UINT64_MAX};
 struct spx_storage_geometry out={99,99,99}, before=out;
 memset(pages+4096,0,105);
 spx_storage_put64(pages+4096,8192);
 pages[4096+9]=16; /* block=4096 */
 pages[4096+13]=1; /* page=256 */
 pages[4096+16]=1;
 assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_OK);
 assert(out.blocks==8192 && out.block_bytes==4096 && out.page_bytes==256);
 spx_storage_put64(pages+64,105);
 assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_OK);
 out=before;
 for(unsigned i=0;i<64;i++) {
  pages[i]^=1;
  assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_REQUEST_CHANGED);
  assert(!memcmp(&out,&before,sizeof(out))); pages[i]^=1;
 }
 for(unsigned i=72;i<8192;i++) {
  if(i>=4096 && i<4201)continue;
  pages[i]^=1;
  assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_GUARD);
  pages[i]^=1;
 }
 uint64_t sizes[]={1,104,106,4096,UINT64_MAX};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
  spx_storage_put64(pages+64,sizes[i]);
  assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_SIZE);
 }
 spx_storage_put64(pages+64,0);
 for(unsigned i=0;i<8192;i++)
  assert(spx_storage_check_info(pages,i,expected,result,&out)==SPX_INFO_ARGUMENT);
 result[0]=0;
 assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_RESPONSE);
 result[0]=1; result[1]=1ULL<<32;
 assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_RESPONSE);
 result[1]=0; pages[4096+9]=2;
 assert(spx_storage_check_info(pages,8192,expected,result,&out)==SPX_INFO_GEOMETRY);
 assert(!memcmp(&out,&before,sizeof(out)));
 puts("GET_INFO encoding=1 response_validation=1 guards=8192 failure_output_unchanged=1 NO hardware calls");
#else
 puts("GET_INFO encoding=1 response_validation=0 NO hardware calls");
#endif
 return 0;
}
